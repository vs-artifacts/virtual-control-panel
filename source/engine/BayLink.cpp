#include "BayLink.h"
#include "../link/BayNames.h"
#include <bit>
#include <juce_dsp/juce_dsp.h>

namespace
{
    hs_link_info emptyInfo() noexcept
    {
        hs_link_info info {};
        info.struct_size = sizeof (info);
        return info;
    }

    // Checked by its bits: fast-math builds may assume NaN and infinity never happen
    juce::uint64 replaceNonFinite (float* samples, int numSamples) noexcept
    {
        juce::uint64 replaced = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            if ((std::bit_cast<juce::uint32> (samples[i]) & 0x7f800000u) == 0x7f800000u)
            {
                samples[i] = 0.0f;
                ++replaced;
            }
        }

        return replaced;
    }

    float largestDifference (const float* a, const float* b, int numSamples) noexcept
    {
        auto largest = 0.0f;

        for (int i = 0; i < numSamples; ++i)
            largest = std::max (largest, std::abs (a[i] - b[i]));

        return largest;
    }

    void keepLargest (std::atomic<float>& largest, float value) noexcept
    {
        if (value > largest.load (std::memory_order_relaxed))
            largest.store (value, std::memory_order_relaxed);
    }

    std::optional<float> takeFloat (float stored) noexcept
    {
        return stored < 0.0f ? std::nullopt : std::optional<float> (stored);
    }

    bool isFinite (float value) noexcept
    {
        return (std::bit_cast<juce::uint32> (value) & 0x7f800000u) != 0x7f800000u;
    }
}

BayControl::BayControl (int numberToUse)
    : number (numberToUse)
{
}

void BayControl::setValue (float newValue)
{
    if (isFinite (newValue))
        value.store (juce::jlimit (0.0f, 1.0f, newValue), std::memory_order_relaxed);
}

juce::String BayControl::getName (int maximumStringLength) const
{
    return ("Control " + juce::String (number)).substring (0, maximumStringLength);
}

juce::String BayControl::getText (float normalisedValue, int maximumStringLength) const
{
    return (juce::String (normalisedValue * 100.0f, 2) + "%").substring (0, maximumStringLength);
}

float BayControl::getValueForText (const juce::String& text) const
{
    const auto percent = text.upToFirstOccurrenceOf ("%", false, false).trim().getFloatValue();
    return juce::jlimit (0.0f, 1.0f, percent / 100.0f);
}

BayLink::BayLink (HubLocator locatorToUse)
    : locator (std::move (locatorToUse))
{
    for (int number = 1; number <= numControls; ++number)
        controls.push_back (std::make_unique<BayControl> (number));

    maintain();
    hubCheck.startTimer (1000);
}

BayLink::~BayLink()
{
    hubCheck.stopTimer();

    // The host has stopped our audio by now, so nothing else is using these handles
    if (const auto* linkHub = hub.load())
    {
        if (const auto send = sendHandle.exchange (-1); send >= 0)
            linkHub->close (send);

        if (const auto back = returnHandle.exchange (-1); back >= 0)
            linkHub->close (back);
    }
}

void BayLink::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maxBlockSize);

    const auto blockFrames = static_cast<size_t> (maxBlock);

    for (auto* buffer : { &returnLeft, &returnRight, &directLeft, &directRight })
        buffer->assign (blockFrames, 0.0f);

    history.allocate ((maxRoundTripBlocks + 1) * maxBlock);

    probeSettle = static_cast<int> (std::ceil (probeSettleSeconds * sampleRate));
    probeBefore = static_cast<int> (std::ceil (probeBeforeSeconds * sampleRate));
    probeAfter = static_cast<int> (std::ceil (probeAfterSeconds * sampleRate));
    probeEnvelope.assign (static_cast<size_t> (probeSettle + maxBlock + probeAfter), -1.0f);
    lagCapture.assign (static_cast<size_t> (lagEarliest + lagBurst + lagLatest), 0.0f);
    testStep.store (testIdle);
    muteUntil = 0;

    if (probeResult.state == Probe::State::running)
        probeResult = {};

    if (lagResult.state == Lag::State::measuring)
        lagResult = {};

    roundTrip.store (roundTripBlocks.load() * maxBlock);
    lastRoundTrip = -1;
    returnStall = flowingRun = 0;
    blockMin.store (unset);
    blockMax.store (unset);

    // A new session: receivers line up again with the new block size and round trip
    if (const auto send = sendHandle.load(); send >= 0)
        hub.load()->set_writer_details (send, sampleRate, maxBlock, roundTrip.load());
    else
        linkIfReady();
}

void BayLink::setRoundTripBlocks (int blocksOrAuto)
{
    const auto setting = blocksOrAuto == autoRoundTrip ? autoRoundTrip : juce::jlimit (minRoundTripBlocks, maxRoundTripBlocks, blocksOrAuto);
    roundTripSetting.store (setting);

    // Switching to automatic checks 1 block afresh
    if (setting == autoRoundTrip)
        startAutoCheck();

    updateRoundTrip();
}

bool BayLink::updateRoundTrip()
{
    const auto setting = roundTripSetting.load();

    if (setting != autoRoundTrip)
    {
        autoCheckActive.store (false);
        autoState.store (AutoState::off);
        return applyRoundTrip (setting);
    }

    // A relay linking (a new one, or one switching modes) may run in a different order: check 1 block again
    if (const auto* linkHub = hub.load(); linkHub != nullptr && sendHandle.load() >= 0)
    {
        auto sendInfo = emptyInfo();
        auto backInfo = emptyInfo();

        if (linkHub->get_info (sendHandle.load(), &sendInfo) == HS_LINK_OK && linkHub->get_info (returnHandle.load(), &backInfo) == HS_LINK_OK)
        {
            const auto claims = (juce::uint64 (sendInfo.reader_claims) << 32) | backInfo.writer_claims;

            if (relayClaims != claims)
            {
                relayClaims = claims;
                startAutoCheck();
            }
        }
    }

    if (autoState.load() == AutoState::checking && autoCheckActive.load() && sampleRate > 0.0)
    {
        if (autoCheckMisses.load() > 0)
        {
            autoBlocks = 2;
            autoState.store (AutoState::fellBack);
            autoCheckActive.store (false);
        }
        else if (static_cast<double> (autoCheckSamples.load()) >= autoCheckSeconds * sampleRate)
        {
            autoState.store (AutoState::oneBlock);
        }
    }
    else if (autoState.load() == AutoState::oneBlock && autoCheckMisses.load() > 0)
    {
        // Still watching: e.g. the wire into the receiver was removed
        autoBlocks = 2;
        autoState.store (AutoState::fellBack);
        autoCheckActive.store (false);
    }

    const auto changed = applyRoundTrip (autoBlocks);

    // Counted only at the round trip being checked, so nothing from before the change gets in
    if (autoState.load() == AutoState::checking && !autoCheckActive.load())
    {
        autoCheckMisses.store (0);
        autoCheckSamples.store (0);
        autoCheckRoundTrip.store (roundTrip.load());
        autoCheckActive.store (true);
    }

    return changed;
}

void BayLink::startAutoCheck()
{
    autoBlocks = minRoundTripBlocks;
    autoCheckActive.store (false);
    autoState.store (AutoState::checking);
}

bool BayLink::applyRoundTrip (int blocks)
{
    roundTripBlocks.store (blocks);
    const auto samples = blocks * maxBlock;

    if (samples == roundTrip.load())
        return false;

    roundTrip.store (samples);

    if (const auto send = sendHandle.load(); send >= 0 && maxBlock > 0)
        hub.load()->set_writer_details (send, sampleRate, maxBlock, samples);

    return true;
}

void BayLink::maintain()
{
    if (hub.load() == nullptr)
    {
        const auto search = locator();

        if (search.hub == nullptr)
        {
            hubProblem = search.problem;
            return;
        }

        hubProblem = {};
        hub.store (search.hub, std::memory_order_release);
    }

    linkIfReady();
}

void BayLink::linkIfReady()
{
    const auto* linkHub = hub.load();

    if (linkHub == nullptr || sendHandle.load() >= 0 || maxBlock <= 0)
        return;

    if (maxBlock > HS_LINK_MAX_FRAMES)
    {
        linkProblem = "Blocks of " + juce::String (maxBlock) + " samples are too long for the link";
        return;
    }

    for (int panel = 1; panel <= BayNames::maxPanels; ++panel)
    {
        const auto send = linkHub->open (BayNames::send (panel, 1).c_str(), HS_LINK_WRITER);

        if (send < 0)
            continue;

        const auto back = linkHub->open (BayNames::back (panel, 1).c_str(), HS_LINK_READER);

        if (back < 0)
        {
            linkHub->close (send);
            continue;
        }

        if (linkHub->set_writer_details (send, sampleRate, maxBlock, roundTrip.load()) != HS_LINK_OK)
        {
            linkHub->close (send);
            linkHub->close (back);
            linkProblem = "The link refused this panel's sample rate, block size or round trip";
            return;
        }

        // Start past anything an earlier panel left in these channels, so old audio can never pass for new
        auto sendInfo = emptyInfo();
        auto backInfo = emptyInfo();
        linkHub->get_info (send, &sendInfo);
        linkHub->get_info (back, &backInfo);
        const auto pastOldAudio = std::max (sendInfo.readable_end, backInfo.readable_end) + 2 * HS_LINK_CAPACITY;
        minimumLinkPosition.store (std::max (minimumLinkPosition.load(), pastOldAudio));

        linkName = BayNames::linkName (panel);
        linkProblem = {};
        returnHandle.store (back, std::memory_order_release);
        sendHandle.store (send, std::memory_order_release);
        return;
    }

    linkProblem = "Every link name is taken";
}

void BayLink::process (juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() < 2 || maxBlock <= 0)
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    // A host breaking its promise about block sizes: nothing here is sized for it
    if (numSamples > maxBlock)
    {
        oversized.fetch_add (1, std::memory_order_relaxed);
        buffer.clear (0, 0, numSamples);
        buffer.clear (1, 0, numSamples);
        history.silence (position, numSamples);
        position += numSamples;
        return;
    }

    lower (blockMin, numSamples);
    raise (blockMax, numSamples);

    const auto start = position;
    const auto roundTripSamples = roundTrip.load (std::memory_order_relaxed);

    if (roundTripSamples != lastRoundTrip)
    {
        lastRoundTrip = roundTripSamples;
        roundTripRun = 0;
    }
    else
    {
        roundTripRun += numSamples;
    }

    if (const auto minimum = minimumLinkPosition.load (std::memory_order_relaxed); start + linkOffset < minimum)
        linkOffset = minimum - start;

    const auto linkStart = start + linkOffset;
    history.write (start, left, right, numSamples);

    // From here on the buffer only carries what's sent: the output is written last, from the history or the return
    runTest (linkStart, numSamples, left, right);
    const auto testing = testStep.load (std::memory_order_relaxed) == testRunning;

    const auto* linkHub = hub.load (std::memory_order_acquire);
    const auto send = sendHandle.load (std::memory_order_acquire);
    const auto back = returnHandle.load (std::memory_order_acquire);

    if (linkHub != nullptr && send >= 0)
    {
        const float* channels[] { left, right };
        linkHub->write (send, linkStart, channels, 2, numSamples);
        sendValues (*linkHub, send, linkStart);
    }

    // The receiver's misses since the last block: each leaves a gap in the return, which may still be far off
    auto receiverMissed = false;

    if (auto info = emptyInfo(); linkHub != nullptr && send >= 0 && linkHub->get_info (send, &info) == HS_LINK_OK)
    {
        if (send == receiverCountedOn && info.failed_reads > receiverFailures)
        {
            receiverMisses.fetch_add (info.failed_reads - receiverFailures, std::memory_order_relaxed);
            receiverMissed = true;
        }

        receiverCountedOn = send;
        receiverFailures = info.failed_reads;
    }

    auto result = int32_t { HS_LINK_CLOSED };
    auto flowing = false;
    const auto returnStart = linkStart - roundTripSamples;

    if (linkHub != nullptr && back >= 0)
    {
        float* channels[] { returnLeft.data(), returnRight.data() };
        result = linkHub->read (back, returnStart, channels, 2, numSamples);

        auto info = emptyInfo();

        if (linkHub->get_info (back, &info) == HS_LINK_OK && info.has_writer != 0)
        {
            if (info.writes != lastReturnWrites)
            {
                lastReturnWrites = info.writes;
                returnStall = 0;
            }
            else
            {
                returnStall += numSamples;
            }

            // Measured in time: a receiver that misses now and then makes the return stop and start, which must
            // still count as flowing
            flowing = static_cast<double> (returnStall) < std::max (stallSeconds * sampleRate, 2.0 * maxBlock);

            if (flowing)
            {
                const auto headroom = static_cast<int> (juce::jlimit<juce::int64> (-HS_LINK_CAPACITY, HS_LINK_CAPACITY, info.readable_end - (returnStart + numSamples)));
                lower (headroomNow, headroom);
                lower (headroomLowest, headroom);
            }
        }
    }

    flowingRun = flowing ? flowingRun + numSamples : 0;

    // A newly flowing return fills the round trip first; counted in samples, since FL's blocks can be far smaller
    // than it announced
    const auto warmedUp = flowingRun > roundTripSamples + warmUpBlocks * maxBlock;
    history.read (start - roundTripSamples, directLeft.data(), directRight.data(), numSamples);
    const auto returned = result == HS_LINK_OK;

    if (returned)
    {
        const auto badSamples = replaceNonFinite (returnLeft.data(), numSamples) + replaceNonFinite (returnRight.data(), numSamples);

        if (badSamples > 0)
            replaced.fetch_add (badSamples, std::memory_order_relaxed);

        // A test signal (or what's still coming back of it) isn't the sent audio, so the comparison waits
        if (!testing && linkStart >= muteUntil)
        {
            const auto difference = std::max (largestDifference (returnLeft.data(), directLeft.data(), numSamples),
                largestDifference (returnRight.data(), directRight.data(), numSamples));
            keepLargest (differenceNow, difference);
            keepLargest (differenceLargest, difference);
        }
    }
    else
    {
        // A return that's flowing but late or broken; not while a new one fills the round trip
        if (flowing && warmedUp)
            missing.fetch_add (1, std::memory_order_relaxed);
    }

    // The automatic round trip's check, once the return flows and the relays have lined up at the round trip being
    // checked. A miss on either side counts.
    if (autoCheckActive.load (std::memory_order_relaxed) && roundTripSamples == autoCheckRoundTrip.load (std::memory_order_relaxed)
        && flowing && warmedUp && static_cast<double> (roundTripRun) >= autoSettleSeconds * sampleRate)
    {
        if (returned && !receiverMissed)
            autoCheckSamples.fetch_add (static_cast<juce::uint64> (numSamples), std::memory_order_relaxed);
        else
            autoCheckMisses.fetch_add (1, std::memory_order_relaxed);
    }

    captureTest (returnStart, numSamples, returned, linkStart + numSamples);

    // A test fades the output out before it starts, and back in once its signal has gone
    const auto muted = testing || testStep.load (std::memory_order_relaxed) == testRequested || linkStart < muteUntil;

    if (muted && outputGain <= 0.0f)
    {
        buffer.clear (0, 0, numSamples);
        buffer.clear (1, 0, numSamples);
        returnMix = returned && !bypass.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    }
    else
    {
        mix (left, right, numSamples, returned);
        fadeOutput (left, right, numSamples, muted ? 0.0f : 1.0f);
    }

    position = start + numSamples;
}

void BayLink::fadeOutput (float* left, float* right, int numSamples, float target)
{
    if (juce::exactlyEqual (outputGain, target) && juce::exactlyEqual (target, 1.0f))
        return;

    const auto step = static_cast<float> (1.0 / std::max (1.0, muteFadeSeconds * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        outputGain = target > outputGain ? std::min (target, outputGain + step) : std::max (target, outputGain - step);
        left[i] *= outputGain;
        right[i] *= outputGain;
    }
}

void BayLink::mix (float* left, float* right, int numSamples, bool returned)
{
    const auto bypassed = bypass.load (std::memory_order_relaxed);

    if (!returned)
    {
        // A dropout starts at once (fading out of it would need a block of lookahead). Silence mode comes back from
        // it at once too, so dropouts stay plain to hear; safe mode fades back into the return.
        if (!bypassed && dropouts.load (std::memory_order_relaxed) == Dropouts::silence)
        {
            juce::FloatVectorOperations::clear (left, numSamples);
            juce::FloatVectorOperations::clear (right, numSamples);
            returnMix = 1.0f;
            return;
        }

        juce::FloatVectorOperations::copy (left, directLeft.data(), numSamples);
        juce::FloatVectorOperations::copy (right, directRight.data(), numSamples);
        returnMix = 0.0f;
        return;
    }

    const auto target = bypassed ? 0.0f : 1.0f;

    if (juce::exactlyEqual (returnMix, target))
    {
        juce::FloatVectorOperations::copy (left, bypassed ? directLeft.data() : returnLeft.data(), numSamples);
        juce::FloatVectorOperations::copy (right, bypassed ? directRight.data() : returnRight.data(), numSamples);
        return;
    }

    const auto fadeSamples = crossfadeMs.load (std::memory_order_relaxed) * sampleRate / 1000.0;
    const auto step = fadeSamples > 1.0 ? static_cast<float> (1.0 / fadeSamples) : 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        returnMix = target > returnMix ? std::min (target, returnMix + step) : std::max (target, returnMix - step);
        const auto index = static_cast<size_t> (i);
        left[i] = directLeft[index] + returnMix * (returnLeft[index] - directLeft[index]);
        right[i] = directRight[index] + returnMix * (returnRight[index] - directRight[index]);
    }
}

void BayLink::sendValues (const hs_link_api& linkHub, int32_t send, juce::int64 linkStart)
{
    std::array<float, numControls> values {};

    for (size_t i = 0; i < values.size(); ++i)
        values[i] = controls[i]->getValue();

    if (testStep.load (std::memory_order_relaxed) == testRunning && testKind.load (std::memory_order_relaxed) == probeTest)
        values[0] = probeStepAt >= 0 ? 1.0f : 0.0f;

    // A set applies until the next, so only changes (and each new link's first set) are sent
    const auto same = std::equal (values.begin(), values.end(), valuesSent.begin(), [] (float a, float b) { return juce::exactlyEqual (a, b); });

    if (send == valuesSentTo && same)
        return;

    if (linkHub.write_values (send, linkStart, values.data(), numControls) == HS_LINK_OK)
    {
        valuesSent = values;
        valuesSentTo = send;
        valueSets.fetch_add (1, std::memory_order_relaxed);
    }
}

void BayLink::runTest (juce::int64 linkStart, int numSamples, float* left, float* right)
{
    auto step = testStep.load (std::memory_order_acquire);

    if (step == testRequested)
    {
        // Not until the output has faded out
        if (outputGain > 0.0f)
            return;

        testStart = linkStart;
        probeStepAt = -1;
        std::fill (probeEnvelope.begin(), probeEnvelope.end(), -1.0f);
        std::fill (lagCapture.begin(), lagCapture.end(), 0.0f);
        lagCaptured = 0;
        testStep.store (testRunning, std::memory_order_relaxed);
        step = testRunning;
    }

    if (step != testRunning)
        return;

    if (testKind.load (std::memory_order_relaxed) == lagTest)
    {
        // Silence, the burst, then silence while it comes back
        for (int i = 0; i < numSamples; ++i)
        {
            const auto index = linkStart + i - testStart - lagEarliest;
            left[i] = right[i] = index >= 0 && index < lagBurst ? lagBurstSamples[static_cast<size_t> (index)] : 0.0f;
        }

        return;
    }

    if (probeStepAt < 0 && linkStart >= testStart + probeSettle)
        probeStepAt = linkStart;

    // sin (pi/2 n + pi/4): a tone whose every sample has the same size, so the return's level shows sample by sample
    for (int i = 0; i < numSamples; ++i)
        left[i] = right[i] = ((linkStart + i) & 2) == 0 ? probeToneLevel : -probeToneLevel;
}

void BayLink::captureTest (juce::int64 returnStart, int numSamples, bool returned, juce::int64 linkEnd)
{
    if (testStep.load (std::memory_order_relaxed) != testRunning)
        return;

    if (testKind.load (std::memory_order_relaxed) == lagTest)
    {
        const auto length = static_cast<juce::int64> (lagCapture.size());

        if (returned)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                if (const auto index = returnStart + i - testStart; index >= 0 && index < length)
                {
                    const auto sample = static_cast<size_t> (i);
                    lagCapture[static_cast<size_t> (index)] = 0.5f * (returnLeft[sample] + returnRight[sample]);
                    ++lagCaptured;
                }
            }
        }

        // Done once the return has passed the stretch, or long after it should have
        if (const auto end = testStart + length; returnStart + numSamples >= end || linkEnd >= end + lastRoundTrip + probeSettle)
            finishTest (linkEnd);

        return;
    }

    const auto capacity = static_cast<juce::int64> (probeEnvelope.size());

    if (returned)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto index = returnStart + i - testStart;

            if (index >= 0 && index < capacity)
                probeEnvelope[static_cast<size_t> (index)] = std::max (std::abs (returnLeft[static_cast<size_t> (i)]), std::abs (returnRight[static_cast<size_t> (i)]));
        }
    }

    if (probeStepAt < 0)
        return;

    // Done once the return has passed the stretch after the step, or long after it should have
    if (const auto end = probeStepAt + probeAfter; returnStart + numSamples >= end || linkEnd >= end + lastRoundTrip + probeSettle)
        finishTest (linkEnd);
}

void BayLink::finishTest (juce::int64 linkEnd)
{
    // The test signal stops now; what's still on its way back is muted too
    muteUntil = linkEnd + lastRoundTrip + maxBlock;
    auto running = int { testRunning };
    testStep.compare_exchange_strong (running, testReady, std::memory_order_release);
}

void BayLink::measureLag()
{
    if (probeResult.state == Probe::State::running)
        probeResult = {};

    lagResult = {};
    lagResult.state = Lag::State::measuring;
    startTest (lagTest);
}

void BayLink::startProbe()
{
    if (lagResult.state == Lag::State::measuring)
        lagResult = {};

    probeResult = {};
    probeResult.state = Probe::State::running;
    startTest (probeTest);
}

void BayLink::startTest (TestKind kind)
{
    // A finished test's capture is the message thread's until the next status takes it
    if (probeEnvelope.empty() || testStep.load() == testReady)
        return;

    // Another test running (or this one again) starts over
    testStep.store (testIdle, std::memory_order_release);
    testKind.store (kind, std::memory_order_relaxed);
    testStep.store (testRequested, std::memory_order_release);
}

void BayLink::resetCounters()
{
    missing.store (0);
    receiverMisses.store (0);
    replaced.store (0);
    oversized.store (0);
    valueSets.store (0);

    for (auto* value : { &headroomNow, &headroomLowest, &blockMin, &blockMax })
        value->store (unset);

    differenceNow.store (-1.0f);
    differenceLargest.store (-1.0f);
}

BayLink::Status BayLink::getStatus()
{
    Status status;
    const auto* linkHub = hub.load();
    status.hubProblem = linkHub == nullptr ? hubProblem : juce::String();
    status.linkName = linkName;
    status.linkProblem = linkProblem;

    if (linkHub != nullptr)
    {
        auto info = emptyInfo();

        if (linkHub->get_info (sendHandle.load(), &info) == HS_LINK_OK)
        {
            status.receiverLinked = info.has_reader != 0;
            status.receiverPlaying = status.receiverLinked && info.reads != statusReads;
            statusReads = info.reads;
        }

        info = emptyInfo();

        if (linkHub->get_info (returnHandle.load(), &info) == HS_LINK_OK)
        {
            status.transmitterLinked = info.has_writer != 0;
            status.transmitterReturning = status.transmitterLinked && info.writes != statusWrites;
            statusWrites = info.writes;
        }
    }

    status.roundTripSetting = roundTripSetting.load();
    status.autoState = autoState.load();
    status.roundTripBlocks = roundTripBlocks.load();
    status.roundTripSamples = roundTrip.load();
    status.maxBlock = maxBlock;
    status.sampleRate = sampleRate;
    status.bypass = bypass.load();
    status.dropouts = dropouts.load();
    status.crossfadeMs = crossfadeMs.load();
    status.missing = missing.load();
    status.receiverMisses = receiverMisses.load();
    status.replaced = replaced.load();
    status.oversized = oversized.load();
    status.valueSets = valueSets.load();
    status.headroomNow = valueOf (headroomNow.exchange (unset));
    status.headroomLowest = valueOf (headroomLowest.load());
    status.blockMin = valueOf (blockMin.load());
    status.blockMax = valueOf (blockMax.load());
    status.differenceNow = takeFloat (differenceNow.exchange (-1.0f));
    status.differenceLargest = takeFloat (differenceLargest.load());

    if (testStep.load (std::memory_order_acquire) == testReady)
    {
        if (testKind.load() == lagTest)
        {
            // Most of the stretch must have come back to tell anything
            if (static_cast<double> (lagCaptured) < 0.9 * static_cast<double> (lagCapture.size()))
            {
                lagResult = {};
                lagResult.state = Lag::State::noReturn;
                lagResult.earliest = lagEarliest;
                lagResult.latest = lagLatest;
            }
            else
            {
                lagResult = findLag (lagCapture.data(), lagBurstSamples.data(), lagBurst, lagEarliest, lagLatest);
            }
        }
        else
        {
            probeResult = findProbeResult (probeEnvelope.data(), static_cast<int> (probeStepAt - testStart), probeBefore, probeAfter);
        }

        testStep.store (testIdle, std::memory_order_release);
    }

    status.lag = lagResult;
    status.probe = probeResult;
    return status;
}

BayLink::Probe BayLink::findProbeResult (const float* envelope, int step, int before, int after)
{
    Probe probe;

    // The average level over a stretch, from the samples that came back
    const auto average = [envelope] (int from, int to, int& counted) {
        auto sum = 0.0;
        counted = 0;

        for (int i = from; i < to; ++i)
        {
            if (envelope[i] >= 0.0f)
            {
                sum += envelope[i];
                ++counted;
            }
        }

        return counted > 0 ? static_cast<float> (sum / counted) : 0.0f;
    };

    auto countedBefore = 0, countedAfter = 0;
    probe.before = average (step - before, step, countedBefore);
    probe.after = average (step + after / 2, step + after, countedAfter);

    if (countedBefore < before / 2 || countedAfter < after / 4)
    {
        probe.state = Probe::State::noReturn;
        return probe;
    }

    const auto change = probe.after - probe.before;

    // The first sample (from the start of the stretch before the step) past a fraction of the change
    const auto crossing = [&] (float fraction) -> std::optional<int> {
        const auto level = probe.before + fraction * change;

        for (int i = step - before; i < step + after; ++i)
            if (envelope[i] >= 0.0f && (change > 0.0f ? envelope[i] >= level : envelope[i] <= level))
                return i - step;

        return std::nullopt;
    };

    const auto low = crossing (0.1f);
    const auto half = crossing (0.5f);
    const auto high = crossing (0.9f);

    if (std::abs (change) < 0.001f || !low.has_value() || !half.has_value() || !high.has_value())
    {
        probe.state = Probe::State::noChange;
        return probe;
    }

    probe.state = Probe::State::found;
    probe.delay = *half;
    probe.rise = *high - *low;
    return probe;
}

std::vector<float> BayLink::makeLagBurst()
{
    juce::Random random (0x5eed);
    std::vector<float> burst (static_cast<size_t> (lagBurst));

    for (auto& sample : burst)
        sample = (2.0f * random.nextFloat() - 1.0f) * lagBurstLevel;

    return burst;
}

BayLink::Lag BayLink::findLag (const float* captured, const float* burst, int burstLength, int earliest, int latest)
{
    Lag lag;
    lag.earliest = earliest;
    lag.latest = latest;

    // Every delay at once: the correlation of the capture with the burst, through the FFT
    const auto capturedLength = earliest + burstLength + latest;
    auto order = 0;

    while ((1 << order) < capturedLength + burstLength)
        ++order;

    const auto size = static_cast<size_t> (1) << order;
    std::vector<float> spectrum (2 * size, 0.0f), burstSpectrum (2 * size, 0.0f);
    std::copy_n (captured, capturedLength, spectrum.begin());
    std::copy_n (burst, burstLength, burstSpectrum.begin());

    juce::dsp::FFT fft (order);
    fft.performRealOnlyForwardTransform (spectrum.data());
    fft.performRealOnlyForwardTransform (burstSpectrum.data());

    for (size_t bin = 0; bin < size; ++bin)
    {
        const auto re = spectrum[2 * bin], im = spectrum[2 * bin + 1];
        const auto burstRe = burstSpectrum[2 * bin], burstIm = burstSpectrum[2 * bin + 1];
        spectrum[2 * bin] = re * burstRe + im * burstIm;
        spectrum[2 * bin + 1] = im * burstRe - re * burstIm;
    }

    fft.performRealOnlyInverseTransform (spectrum.data());

    // spectrum[earliest + k] now follows the burst coming back k samples late (its scale doesn't matter here)
    const auto span = earliest + latest + 1;
    const auto strength = [&spectrum] (int index) { return std::abs (spectrum[static_cast<size_t> (index)]); };
    auto strongest = 0.0f;

    for (int index = 0; index < span; ++index)
        strongest = std::max (strongest, strength (index));

    std::vector<int> peaks;

    for (int index = 0; index < span; ++index)
    {
        const auto value = strength (index);

        if (value > 0.0f && value >= lagPathShare * strongest && (index == 0 || value > strength (index - 1)) && (index == span - 1 || value >= strength (index + 1)))
            peaks.push_back (index);
    }

    std::sort (peaks.begin(), peaks.end(), [&strength] (int a, int b) { return strength (a) > strength (b); });

    // The exact gain of each path; a filter's ringing next to a path isn't a path of its own
    auto burstEnergy = 0.0;

    for (int i = 0; i < burstLength; ++i)
        burstEnergy += static_cast<double> (burst[i]) * burst[i];

    constexpr int nearby = 16;
    constexpr size_t mostPaths = 4;

    for (const auto peak : peaks)
    {
        if (lag.paths.size() == mostPaths)
            break;

        const auto delay = peak - earliest;

        if (std::any_of (lag.paths.begin(), lag.paths.end(), [delay] (const Lag::Path& path) { return std::abs (path.samples - delay) <= nearby; }))
            continue;

        auto product = 0.0;

        for (int i = 0; i < burstLength; ++i)
            product += static_cast<double> (burst[i]) * captured[earliest + delay + i];

        lag.paths.push_back ({ delay, static_cast<float> (product / burstEnergy) });
    }

    // Unrelated sound correlates by chance at about 1 / sqrt (burst length) (0.011), up to some 0.05 at the luckiest delay
    if (lag.paths.empty() || std::abs (lag.paths.front().gain) < 0.1f)
    {
        lag.paths.clear();
        lag.state = Lag::State::noMatch;
        return lag;
    }

    const auto* firstPath = captured + earliest + lag.paths.front().samples;
    lag.exact = std::equal (burst, burst + burstLength, firstPath, [] (float a, float b) { return juce::exactlyEqual (a, b); });
    lag.state = Lag::State::found;
    return lag;
}

const char* BayLink::toName (Dropouts value) noexcept
{
    return value == Dropouts::silence ? "silence" : "safe";
}

std::optional<BayLink::Dropouts> BayLink::dropoutsFromName (const juce::String& name)
{
    if (name == "safe")
        return Dropouts::safe;

    if (name == "silence")
        return Dropouts::silence;

    return std::nullopt;
}

const char* BayLink::toName (AutoState value) noexcept
{
    switch (value)
    {
        case AutoState::checking:
            return "checking";
        case AutoState::oneBlock:
            return "oneBlock";
        case AutoState::fellBack:
            return "fellBack";
        case AutoState::off:
            break;
    }

    return "off";
}

void BayLink::History::allocate (int minimumFrames)
{
    const auto frames = juce::nextPowerOfTwo (minimumFrames);
    left.assign (static_cast<size_t> (frames), 0.0f);
    right.assign (static_cast<size_t> (frames), 0.0f);
    mask = frames - 1;
}

void BayLink::History::write (juce::int64 position, const float* fromLeft, const float* fromRight, int numFrames)
{
    for (int i = 0; i < numFrames; ++i)
    {
        const auto index = static_cast<size_t> ((position + i) & mask);
        left[index] = fromLeft[i];
        right[index] = fromRight[i];
    }
}

void BayLink::History::silence (juce::int64 position, int numFrames)
{
    const auto frames = std::min (static_cast<juce::int64> (numFrames), mask + 1);

    for (juce::int64 i = 0; i < frames; ++i)
    {
        const auto index = static_cast<size_t> ((position + numFrames - frames + i) & mask);
        left[index] = right[index] = 0.0f;
    }
}

void BayLink::History::read (juce::int64 position, float* toLeft, float* toRight, int numFrames) const
{
    for (int i = 0; i < numFrames; ++i)
    {
        const auto index = static_cast<size_t> ((position + i) & mask);
        toLeft[i] = left[index];
        toRight[i] = right[index];
    }
}

void BayLink::lower (std::atomic<int>& lowest, int value) noexcept
{
    const auto current = lowest.load (std::memory_order_relaxed);

    if (current == unset || value < current)
        lowest.store (value, std::memory_order_relaxed);
}

void BayLink::raise (std::atomic<int>& highest, int value) noexcept
{
    const auto current = highest.load (std::memory_order_relaxed);

    if (current == unset || value > current)
        highest.store (value, std::memory_order_relaxed);
}

std::optional<int> BayLink::valueOf (int stored) noexcept
{
    return stored == unset ? std::nullopt : std::optional<int> (stored);
}
