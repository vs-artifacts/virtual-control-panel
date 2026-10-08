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

    for (const auto fade : fades)
        fadeMs[index (fade)].store (defaultFadeMs (fade));

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

    for (auto* buffer : { &returnLeft, &returnRight, &directLeft, &directRight, &jumpLeft, &jumpRight, &jumpReturnLeft, &jumpReturnRight })
        buffer->assign (blockFrames, 0.0f);

    // The direct sound as far back as the longest delay: the round trip plus the bay plugin's latency
    history.allocate ((maxRoundTripBlocks + 1) * maxBlock + HS_LINK_MAX_LATENCY);

    // What played, for a dropout's reversed fade: twice the longest crossfade, so the fade never reads what it overwrote
    const auto playedFrames = static_cast<size_t> (2 * static_cast<int> (std::ceil (maxFadeMs * sampleRate / 1000.0)) + 2);
    playedLeft.assign (playedFrames, 0.0f);
    playedRight.assign (playedFrames, 0.0f);
    playedNext = playedFilled = 0;
    inDropout = false;

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

    // Real blocks start at the lowest tempo's tick, or the announced block until the survey has seen real ones
    const auto floor = floorUnit();
    blockUnit.store (realBlocks.load() && floor > 0 ? floor : maxBlock);
    startSurvey (false);
    unitSurveyPending.store (realBlocks.load());
    countingRealBlocks = realBlocks.load();
    quietSamples.store (0);
    stoppedChecked = false;
    mixFade = levelFade = Fade::dropout;
    shownFade.store (mixFade);

    roundTrip.store (roundTripBlocks.load() * blockUnit.load());
    lastRoundTrip = lastLinkDelay = -1;
    returnStall = flowingRun = 0;
    blockMin.store (unset);
    blockMax.store (unset);

    // A new latency report is needed to settle on another bay latency; the output starts at its delay without a blend
    candidate = -1;
    candidateRun = 0;
    latencyCandidate.store (packCandidate (-1, 0));
    outputDelay = -1;
    jumpGain = 0.0f;

    logEvent ("Prepared: " + juce::String (juce::roundToInt (sampleRate)) + " Hz, blocks of " + juce::String (maxBlock) + "; round trip " + juce::String (roundTrip.load()) + " samples");

    // A new session: receivers line up again with the new block size and round trip
    if (const auto send = sendHandle.load(); send >= 0)
        hub.load()->set_writer_details (send, sampleRate, blockUnit.load(), roundTrip.load());
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
    const auto unitChanged = updateBlockUnit();
    const auto unitReason = "counted in blocks of " + juce::String (blockUnit.load());

    if (setting != autoRoundTrip)
    {
        autoCheckActive.store (false);
        autoState.store (AutoState::off);
        return applyRoundTrip (setting, unitChanged ? unitReason : "set in Settings");
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
                logEvent (relayClaims.has_value() ? "Automatic: a relay linked; checking 1 block" : "Automatic: checking 1 block");
                relayClaims = claims;
                startAutoCheck();
            }
        }
    }

    const auto describeMisses = [this] {
        const auto all = autoCheckMisses.load();
        const auto receiver = autoCheckReceiverMisses.load();
        return "panel " + juce::String (all - std::min (all, receiver)) + ", receiver " + juce::String (receiver);
    };

    if (autoState.load() == AutoState::checking && autoCheckActive.load() && sampleRate > 0.0)
    {
        if (autoCheckMisses.load() > 0)
        {
            logEvent ("Automatic: 1 block missed (" + describeMisses() + "); 2 blocks until a relay links again");
            autoBlocks = 2;
            autoState.store (AutoState::fellBack);
            autoCheckActive.store (false);
        }
        else if (static_cast<double> (autoCheckSamples.load()) >= autoCheckSeconds * sampleRate)
        {
            logEvent ("Automatic: 1 block works (a second without a miss)");
            autoState.store (AutoState::oneBlock);
        }
    }
    else if (autoState.load() == AutoState::oneBlock && autoCheckMisses.load() > 0)
    {
        // Still watching: e.g. the wire into the receiver was removed
        logEvent ("Automatic: 1 block missed while watching (" + describeMisses() + "); 2 blocks until a relay links again");
        autoBlocks = 2;
        autoState.store (AutoState::fellBack);
        autoCheckActive.store (false);
    }

    const auto changed = applyRoundTrip (autoBlocks, unitChanged ? unitReason : autoBlocks == minRoundTripBlocks ? "Automatic, trying 1 block"
                                                                                                                 : "Automatic, falling back");

    // The check carries on at the new size of the same number of blocks
    if (unitChanged && autoCheckActive.load())
        autoCheckRoundTrip.store (roundTrip.load());

    // Counted only at the round trip being checked, so nothing from before the change gets in
    if (autoState.load() == AutoState::checking && !autoCheckActive.load())
    {
        autoCheckMisses.store (0);
        autoCheckReceiverMisses.store (0);
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

int BayLink::floorUnit() const
{
    const auto bpm = lowestTempo.load();

    if (bpm <= 0.0 || sampleRate <= 0.0 || maxBlock <= 0)
        return 0;

    const auto tick = sampleRate * 60.0 / (ticksPerBeat * bpm);
    return std::min (maxBlock, static_cast<int> (std::ceil (unitHeadroom * tick)));
}

int BayLink::unitFor (int largestBlock) const
{
    return std::min (maxBlock, std::max (1, static_cast<int> (std::ceil (unitHeadroom * largestBlock))));
}

bool BayLink::updateBlockUnit()
{
    if (maxBlock <= 0 || sampleRate <= 0.0)
        return false;

    const auto current = blockUnit.load();
    auto unit = maxBlock;
    juce::String why = "the announced block";

    if (realBlocks.load())
    {
        // Switched on from announced blocks, or a new lowest tempo: survey first
        if (tempoFloorChanged.exchange (false) || !countingRealBlocks)
            startSurvey (false);

        // While stopped (and quiet, if so set), one check per stop: again only once playing has started, the host's tempo
        // changes, or a check is asked for. A check that couldn't shrink (playing or loud again by its end) is tried again.
        const auto shrinkAllowed = mayShrinkNow();

        if (transport.load() != Transport::stopped)
            stoppedChecked = false;

        if (const auto tempo = hostTempo.load(); !juce::exactlyEqual (tempo, checkedTempo))
        {
            checkedTempo = tempo;
            stoppedChecked = false;
        }

        if (shrinkAllowed && !stoppedChecked && !unitSurveyPending.load())
        {
            startSurvey (true);
            stoppedChecked = true;
        }

        const auto floor = floorUnit();
        const auto largest = valueOf (unitLargest.load());
        unit = std::max (current, floor);
        why = "the lowest tempo";

        if (unitSurveyPending.load() && largest.has_value() && static_cast<double> (surveyedSamples.load()) >= unitSurveySeconds * sampleRate)
        {
            unitSurveyPending.store (false);

            // A survey started while stopped doesn't shrink once playing has started (or it got loud)
            if (const auto surveyed = std::max (floor, unitFor (*largest)); surveyed > unit || !surveyWhileStopped || shrinkAllowed)
            {
                unit = surveyed;
                why = "largest real block " + juce::String (*largest) + (surveyWhileStopped ? ", while stopped" : "");
            }
            else
            {
                stoppedChecked = false;
            }

            // Any check that could shrink counts as this stop's
            if (shrinkAllowed)
                stoppedChecked = true;
        }
        else if (largest.has_value() && *largest > unitGrowAt * unit)
        {
            unit = std::max (unit, unitFor (*largest));
            why = "a real block of " + juce::String (*largest);
        }
    }

    countingRealBlocks = realBlocks.load();

    if (unit == current)
        return false;

    blockUnit.store (unit);
    logEvent ("Round trip block: " + juce::String (unit) + " samples (" + why + ")");
    return true;
}

void BayLink::startSurvey (bool whileStopped)
{
    surveyWhileStopped = whileStopped;
    unitLargest.store (unset);
    surveyedSamples.store (0);
    unitSurveyPending.store (true);
}

bool BayLink::mayShrinkNow() const
{
    if (transport.load() != Transport::stopped)
        return false;

    switch (stoppedShrink.load())
    {
        case StoppedShrink::never:
            return false;
        case StoppedShrink::always:
            return true;
        case StoppedShrink::whenQuiet:
            break;
    }

    return static_cast<double> (quietSamples.load()) >= quietMs.load() * sampleRate / 1000.0;
}

void BayLink::setLowestTempo (double bpm) noexcept
{
    const auto floor = bpm > 0.0 && bpm <= maxLowestTempo ? bpm : 0.0;

    if (!juce::exactlyEqual (lowestTempo.exchange (floor), floor))
        tempoFloorChanged.store (true);
}

void BayLink::setQuiet (int levelDb, int milliseconds) noexcept
{
    quietLevel.store (juce::Decibels::decibelsToGain (static_cast<float> (juce::jlimit (minQuietLevelDb, maxQuietLevelDb, levelDb))));
    quietMs.store (juce::jlimit (0, maxQuietMs, milliseconds));
}

void BayLink::recheckBlockSize()
{
    if (!realBlocks.load())
        return;

    startSurvey (false);
    logEvent ("Real block size: checking again (asked)");
}

void BayLink::retryOneBlock()
{
    if (roundTripSetting.load() != autoRoundTrip)
        return;

    logEvent ("Automatic: trying 1 block again (asked)");
    startAutoCheck();
}

bool BayLink::updateBayLatency()
{
    const auto packed = latencyCandidate.load();
    const auto value = static_cast<int> (static_cast<juce::uint32> (packed >> 32));
    const auto held = static_cast<double> (static_cast<juce::uint32> (packed));
    const auto inUse = bayLatency.load();

    if (value >= 0 && value != inUse && value != loggedSettling)
    {
        logEvent ("Bay latency: settling to " + juce::String (value) + " (in use: " + juce::String (inUse) + ")");
        loggedSettling = value;
    }
    else if ((value < 0 || value == inUse) && loggedSettling >= 0)
    {
        logEvent ("Bay latency: stopped settling to " + juce::String (loggedSettling) + "; still " + juce::String (inUse));
        loggedSettling = -1;
    }

    if (value < 0 || value > HS_LINK_MAX_LATENCY || value == inUse || sampleRate <= 0.0)
        return false;

    if (held < settleMs.load() * sampleRate / 1000.0)
        return false;

    bayLatency.store (value);
    logEvent ("Bay latency: now " + juce::String (value) + " (was " + juce::String (inUse) + "; held " + juce::String (juce::roundToInt (1000.0 * held / sampleRate)) + " ms)");
    loggedSettling = -1;
    return true;
}

int BayLink::getDelaySamples() const noexcept
{
    return slotState.load() == SlotState::off ? 0 : roundTrip.load() + bayLatency.load();
}

void BayLink::setSlotState (SlotState state)
{
    if (slotState.exchange (state) != state)
        logEvent (juce::String ("Slot state: ") + toName (state));
}

bool BayLink::applyRoundTrip (int blocks, const juce::String& why)
{
    roundTripBlocks.store (blocks);
    const auto samples = blocks * blockUnit.load();

    if (samples == roundTrip.load())
        return false;

    roundTrip.store (samples);
    logEvent ("Round trip: " + juce::String (blocks) + (blocks == 1 ? " block (" : " blocks (") + juce::String (samples) + " samples), " + why);

    if (const auto send = sendHandle.load(); send >= 0 && maxBlock > 0)
        hub.load()->set_writer_details (send, sampleRate, blockUnit.load(), samples);

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

        if (linkHub->set_writer_details (send, sampleRate, blockUnit.load(), roundTrip.load()) != HS_LINK_OK)
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
    raise (unitLargest, numSamples);
    surveyedSamples.fetch_add (static_cast<juce::uint64> (numSamples), std::memory_order_relaxed);

    const auto start = position;
    const auto roundTripSamples = roundTrip.load (std::memory_order_relaxed);
    const auto latencyInUse = bayLatency.load (std::memory_order_relaxed);
    const auto state = slotState.load (std::memory_order_relaxed);
    const auto dropped = state == SlotState::off;
    const auto bayGetsSilence = state == SlotState::bypass || dropped;
    currentLinkDelay = roundTripSamples + latencyInUse;

    // Leaving a state is a transition, not a dropout: the sent audio plays on until the bay's sound is back. Crossfading
    // between dry and the bay's sound takes the length of the state entered, or of the one left.
    if (state != lastSlotState)
    {
        mixFade = fadeFor (state != SlotState::active ? state : lastSlotState);
        shownFade.store (mixFade, std::memory_order_relaxed);

        if (state == SlotState::active)
            awaiting = true;
    }

    lastSlotState = state;

    if (roundTripSamples != lastRoundTrip || currentLinkDelay != lastLinkDelay)
    {
        lastRoundTrip = roundTripSamples;
        lastLinkDelay = currentLinkDelay;
        delayRun = 0;
    }
    else
    {
        delayRun += numSamples;
    }

    if (const auto minimum = minimumLinkPosition.load (std::memory_order_relaxed); start + linkOffset < minimum)
        linkOffset = minimum - start;

    const auto linkStart = start + linkOffset;
    history.write (start, left, right, numSamples);

    // From here on the buffer only carries what's sent: the output is written last, from the history or the return
    runTest (linkStart, numSamples, left, right);
    const auto testing = testStep.load (std::memory_order_relaxed) == testRunning;

    // Bypass and Off fade the bay's send out (once the output has crossfaded away from the bay's sound, so that never
    // dips), and turn the tail cut on once it's silent. Leaving them, the send stays silent for the tail cut release
    // (while the bay's plugin comes back), then fades in. A switch would click into the bay.
    if (sentSilence && !bayGetsSilence)
        sendSilentUntil = linkStart + static_cast<juce::int64> (tailCutReleaseMs.load (std::memory_order_relaxed) * sampleRate / 1000.0);

    // The send's fades take the length of the state silencing it (Bypass or Off)
    if (bayGetsSilence)
        sendFade = fadeFor (state);

    if (!sentSilence && bayGetsSilence)
        sendFullUntil = linkStart + static_cast<juce::int64> (getFadeMs (sendFade) * sampleRate / 1000.0);

    sentSilence = bayGetsSilence;
    const auto tailCut = bayGetsSilence && sendGain <= 0.0f;

    // A test signal still goes out
    if (!testing)
    {
        const auto step = stepFor (sendFade);
        auto faded = false;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto at = linkStart + i;
            const auto target = (bayGetsSilence && at >= sendFullUntil) || at < sendSilentUntil ? 0.0f : 1.0f;
            sendGain = target > sendGain ? std::min (target, sendGain + step) : std::max (target, sendGain - step);

            if (sendGain < 1.0f)
            {
                left[i] *= sendGain;
                right[i] *= sendGain;
                faded = true;
            }
        }

        if (faded)
        {
            if (linkStart != silentEnd)
                silentStart = linkStart;

            silentEnd = linkStart + numSamples;
        }
    }

    const auto* linkHub = hub.load (std::memory_order_acquire);
    const auto send = sendHandle.load (std::memory_order_acquire);
    const auto back = returnHandle.load (std::memory_order_acquire);

    if (linkHub != nullptr && send >= 0)
    {
        const float* channels[] { left, right };
        linkHub->write (send, linkStart, channels, 2, numSamples);
        sendValues (*linkHub, send, linkStart, tailCut);
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
    const auto returnStart = linkStart - currentLinkDelay;

    if (linkHub != nullptr && back >= 0)
    {
        float* channels[] { returnLeft.data(), returnRight.data() };
        result = linkHub->read (back, returnStart, channels, 2, numSamples);

        auto info = emptyInfo();

        if (linkHub->get_info (back, &info) == HS_LINK_OK && info.has_writer != 0)
        {
            noteLatencyReport (info, numSamples);

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
        else
        {
            transmitterHolding = false;
        }
    }

    flowingRun = flowing ? flowingRun + numSamples : 0;

    // A newly flowing return fills the round trip first; counted in samples, since FL's blocks can be far smaller
    // than it announced
    const auto warmedUp = flowingRun > roundTripSamples + warmUpBlocks * maxBlock;
    history.read (start - currentLinkDelay, directLeft.data(), directRight.data(), numSamples);
    const auto returned = result == HS_LINK_OK;

    // A new bay latency waiting to settle: the return is held back meanwhile
    const auto settling = candidate >= 0 && candidate != latencyInUse;

    // Returned audio that isn't the bay's sound at those positions: a stretch it was sent faded or silent, or audio
    // stamped by the receiver's position (before the marker arrived) once a bay latency is in use
    const auto hadSilence = silentEnd > silentStart;
    const auto inSilentStretch = hadSilence && returnStart < silentEnd && returnStart + numSamples > silentStart;
    const auto stampedByReceiver = latencyInUse > 0 && returnStart < receiverStampedEnd;
    const auto notTheBay = inSilentStretch || stampedByReceiver;

    // A change in the transmitter's reading (noted above) restarts the window, as any change of delay does
    const auto delaySettled = static_cast<double> (delayRun) >= autoSettleSeconds * sampleRate;

    if (settling)
        heldBack.fetch_add (1, std::memory_order_relaxed);
    else if (returned && stampedByReceiver && !inSilentStretch)
        heldStamped.fetch_add (1, std::memory_order_relaxed);

    if (returned)
    {
        const auto badSamples = replaceNonFinite (returnLeft.data(), numSamples) + replaceNonFinite (returnRight.data(), numSamples);

        if (badSamples > 0)
            replaced.fetch_add (badSamples, std::memory_order_relaxed);

        // A test signal (or what's still coming back of it) isn't the sent audio, so the comparison waits
        if (!testing && linkStart >= muteUntil && !notTheBay)
        {
            const auto difference = std::max (largestDifference (returnLeft.data(), directLeft.data(), numSamples),
                largestDifference (returnRight.data(), directRight.data(), numSamples));
            keepLargest (differenceNow, difference);
            keepLargest (differenceLargest, difference);
        }
    }
    else
    {
        // Held back by the transmitter (marker unreadable, out of range, or paused): not a miss. Otherwise a return
        // that's flowing but late or broken; not while a new one fills the round trip, nor while a new bay latency
        // settles, nor while the relays line up again after a change of delay.
        if (transmitterHolding)
        {
            heldByTransmitter.fetch_add (1, std::memory_order_relaxed);
        }
        else if (flowing && warmedUp && !settling && delaySettled)
        {
            missing.fetch_add (1, std::memory_order_relaxed);
            (result == HS_LINK_NOT_YET ? missingLate : result == HS_LINK_GONE ? missingGone
                                                                              : missingOther)
                .fetch_add (1, std::memory_order_relaxed);
        }
    }

    // The automatic round trip's check, once the return flows and the relays have lined up at the round trip being
    // checked. A miss on either side counts.
    if (autoCheckActive.load (std::memory_order_relaxed) && roundTripSamples == autoCheckRoundTrip.load (std::memory_order_relaxed)
        && flowing && warmedUp && !settling && delaySettled && !transmitterHolding)
    {
        if (returned && !receiverMissed)
        {
            autoCheckSamples.fetch_add (static_cast<juce::uint64> (numSamples), std::memory_order_relaxed);
        }
        else
        {
            autoCheckMisses.fetch_add (1, std::memory_order_relaxed);

            if (receiverMissed)
                autoCheckReceiverMisses.fetch_add (1, std::memory_order_relaxed);
        }
    }

    captureTest (returnStart, numSamples, returned, linkStart + numSamples);

    // A test fades the output out before it starts, and back in once its signal has gone
    const auto muted = testing || testStep.load (std::memory_order_relaxed) == testRequested || linkStart < muteUntil;
    const auto delayNow = dropped ? 0 : currentLinkDelay;
    const auto playable = returned && !notTheBay && !settling;

    // A return held back for the bay latency (settling, the transmitter holding, stamped by the receiver) fades with its
    // own length; a missing one with the dropout fade
    const auto holdFade = settling || transmitterHolding || (returned && stampedByReceiver) ? Fade::bayLatency : Fade::dropout;

    // The bay's sound is back once it plays positions sent after its latest faded or silent stretch
    if (state == SlotState::active && playable && (!hadSilence || returnStart >= silentEnd))
        awaiting = false;

    awaitingBay.store (awaiting, std::memory_order_relaxed);
    const auto dry = state != SlotState::active || awaiting;

    if (muted && outputGain <= 0.0f)
    {
        buffer.clear (0, 0, numSamples);
        buffer.clear (1, 0, numSamples);
        returnMix = playable && !dry ? 1.0f : 0.0f;
        outputDelay = delayNow;
        outputRoundTrip = roundTripSamples;
        outputLatency = latencyInUse;
        jumpGain = 0.0f;
        level = 1.0f;
        inDropout = false;
    }
    else
    {
        // The output's delay changed: blend from the old timeline, over the fade of what changed (Off and back, the bay
        // latency, the round trip; the longer if both of the last two did)
        if (outputDelay >= 0 && delayNow != outputDelay)
        {
            auto jumpMs = getFadeMs (Fade::off);

            if (dropped == lastDropped)
                jumpMs = std::max (latencyInUse != outputLatency ? getFadeMs (Fade::bayLatency) : 0,
                    roundTripSamples != outputRoundTrip || latencyInUse == outputLatency ? getFadeMs (Fade::roundTrip) : 0);

            const auto fadeSamples = jumpMs * sampleRate / 1000.0;
            jumpFromDelay = outputDelay;
            jumpFromMix = returnMix;
            jumpFromLevel = level;
            jumpGain = jumpMs > 0 ? 1.0f : 0.0f;
            jumpStep = fadeSamples > 1.0 ? static_cast<float> (1.0 / fadeSamples) : 1.0f;
        }

        outputDelay = delayNow;
        outputRoundTrip = roundTripSamples;
        outputLatency = latencyInUse;

        if (dropped)
        {
            history.read (start, left, right, numSamples);
            returnMix = 0.0f;
            inDropout = false;
            fadeIn (left, right, numSamples);
        }
        else
        {
            mix (left, right, numSamples, playable, dry, holdFade);
        }

        if (jumpGain > 0.0f)
            blendJump (linkHub, back, start, linkStart, numSamples, left, right);

        fadeOutput (left, right, numSamples, muted ? 0.0f : 1.0f);
    }

    lastDropped = dropped;

    // How long the output has been quiet, for shrinking the round trip while stopped
    const auto peak = std::max (juce::FloatVectorOperations::findMaximum (left, numSamples), -juce::FloatVectorOperations::findMinimum (left, numSamples));
    const auto peakRight = std::max (juce::FloatVectorOperations::findMaximum (right, numSamples), -juce::FloatVectorOperations::findMinimum (right, numSamples));
    quietSamples.store (std::max (peak, peakRight) < quietLevel.load (std::memory_order_relaxed) ? quietSamples.load (std::memory_order_relaxed) + numSamples : 0, std::memory_order_relaxed);

    // What played, for the next dropout's reversed fade
    if (const auto ringSize = static_cast<int> (playedLeft.size()); ringSize > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            playedLeft[static_cast<size_t> (playedNext)] = left[i];
            playedRight[static_cast<size_t> (playedNext)] = right[i];
            playedNext = (playedNext + 1) % ringSize;
        }

        playedFilled = std::min (ringSize, playedFilled + numSamples);
    }

    position = start + numSamples;
}

void BayLink::noteLatencyReport (const hs_link_info& info, int numSamples)
{
    if (info.latency_reports == lastLatencyReports)
        return;

    lastLatencyReports = info.latency_reports;

    // The latency each report means, if it can be used; the hub checks reports, but they come from another plugin
    auto state = Reading::unreadable;
    std::optional<int> value;

    switch (info.latency_state)
    {
        case HS_LINK_LATENCY_UNKNOWN:
            state = Reading::none;
            value = 0;
            break;
        case HS_LINK_LATENCY_NO_INPUT:
            state = Reading::noInput;
            value = 0;
            break;
        case HS_LINK_LATENCY_MEASURED:
            if (info.latency >= 0 && info.latency <= HS_LINK_MAX_LATENCY)
            {
                state = Reading::measured;
                value = info.latency;
                lower (readingLowest, info.latency);
                raise (readingHighest, info.latency);
            }
            else
            {
                state = Reading::outOfRange;
            }
            break;
        case HS_LINK_LATENCY_OUT_OF_RANGE:
            state = Reading::outOfRange;
            break;
        case HS_LINK_LATENCY_HELD:
            state = Reading::held;
            break;
        default:
            break;
    }

    // Until here, the return was stamped by the receiver's position
    if (state == Reading::none || state == Reading::noInput)
        receiverStampedEnd = std::max (receiverStampedEnd, info.readable_end);

    const auto samples = state == Reading::measured || state == Reading::outOfRange ? info.latency : 0;

    // FL rearranging its latency compensation shows here first: the relays line up again from it
    if (state != reading.load (std::memory_order_relaxed) || samples != readingSamples.load (std::memory_order_relaxed))
        delayRun = 0;

    transmitterHolding = state == Reading::unreadable || state == Reading::outOfRange || state == Reading::held;
    reading.store (state, std::memory_order_relaxed);
    readingSamples.store (samples, std::memory_order_relaxed);

    if (!value.has_value())
    {
        candidateRun = 0;
    }
    else if (*value == candidate)
    {
        candidateRun += numSamples;
    }
    else
    {
        candidate = *value;
        candidateRun = numSamples;
    }

    latencyCandidate.store (packCandidate (candidate, candidateRun), std::memory_order_relaxed);
}

void BayLink::blendJump (const hs_link_api* linkHub, int32_t back, juce::int64 start, juce::int64 linkStart, int numSamples, float* left, float* right)
{
    // The old timeline: the sent audio at its delay, with as much of the return as it was playing
    history.read (start - jumpFromDelay, jumpLeft.data(), jumpRight.data(), numSamples);

    if (jumpFromMix > 0.0f)
    {
        float* channels[] { jumpReturnLeft.data(), jumpReturnRight.data() };
        auto read = linkHub != nullptr && back >= 0 && linkHub->read (back, linkStart - jumpFromDelay, channels, 2, numSamples) == HS_LINK_OK;

        // Missing there: silence, or (safe dropouts) the sent audio
        if (!read && dropouts.load (std::memory_order_relaxed) == Dropouts::silence)
        {
            juce::FloatVectorOperations::clear (jumpReturnLeft.data(), numSamples);
            juce::FloatVectorOperations::clear (jumpReturnRight.data(), numSamples);
            read = true;
        }

        if (read)
        {
            replaceNonFinite (jumpReturnLeft.data(), numSamples);
            replaceNonFinite (jumpReturnRight.data(), numSamples);

            for (size_t i = 0; i < static_cast<size_t> (numSamples); ++i)
            {
                jumpLeft[i] += jumpFromMix * (jumpReturnLeft[i] - jumpLeft[i]);
                jumpRight[i] += jumpFromMix * (jumpReturnRight[i] - jumpRight[i]);
            }
        }
    }

    if (jumpFromLevel < 1.0f)
    {
        juce::FloatVectorOperations::multiply (jumpLeft.data(), jumpFromLevel, numSamples);
        juce::FloatVectorOperations::multiply (jumpRight.data(), jumpFromLevel, numSamples);
    }

    const auto step = jumpStep;

    for (int i = 0; i < numSamples && jumpGain > 0.0f; ++i)
    {
        jumpGain = std::max (0.0f, jumpGain - step);
        const auto index = static_cast<size_t> (i);
        left[i] += jumpGain * (jumpLeft[index] - left[i]);
        right[i] += jumpGain * (jumpRight[index] - right[i]);
    }
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

void BayLink::mix (float* left, float* right, int numSamples, bool playable, bool dry, Fade holdFade)
{
    if (!playable && !dry)
    {
        if (dropouts.load (std::memory_order_relaxed) == Dropouts::silence)
        {
            // The return fades back in from silence, never through the sent audio
            playDropout (left, right, numSamples, holdFade);
            returnMix = 1.0f;
            return;
        }

        // Safe: the sent audio at once (fading out of a dropout would need a block of lookahead); the return fades back in
        inDropout = false;
        juce::FloatVectorOperations::copy (left, directLeft.data(), numSamples);
        juce::FloatVectorOperations::copy (right, directRight.data(), numSamples);
        returnMix = 0.0f;
        fadeIn (left, right, numSamples);
        return;
    }

    inDropout = false;

    // A return that can't play counts as silence, so fading away from it fades the sent audio in
    if (!playable)
    {
        juce::FloatVectorOperations::clear (returnLeft.data(), numSamples);
        juce::FloatVectorOperations::clear (returnRight.data(), numSamples);
    }

    const auto target = dry ? 0.0f : 1.0f;

    if (juce::exactlyEqual (returnMix, target))
    {
        juce::FloatVectorOperations::copy (left, dry ? directLeft.data() : returnLeft.data(), numSamples);
        juce::FloatVectorOperations::copy (right, dry ? directRight.data() : returnRight.data(), numSamples);
    }
    else
    {
        const auto step = stepFor (mixFade);

        for (int i = 0; i < numSamples; ++i)
        {
            returnMix = target > returnMix ? std::min (target, returnMix + step) : std::max (target, returnMix - step);
            const auto index = static_cast<size_t> (i);
            left[i] = directLeft[index] + returnMix * (returnLeft[index] - directLeft[index]);
            right[i] = directRight[index] + returnMix * (returnRight[index] - directRight[index]);
        }
    }

    fadeIn (left, right, numSamples);
}

void BayLink::playDropout (float* left, float* right, int numSamples, Fade holdFade)
{
    // It starts with what last played, reversed, under a falling ramp: it carries on from the last sample played without
    // a jump, and needs no lookahead. Its length, and the fade back in, are those of what held the return back.
    const auto ringSize = static_cast<int> (playedLeft.size());

    if (!inDropout)
    {
        inDropout = true;
        levelFade = holdFade;
        const auto fade = juce::roundToInt (getFadeMs (holdFade) * sampleRate / 1000.0);
        ghostLength = std::min ({ fade, playedFilled, ringSize / 2 });
        ghostDone = 0;
        ghostFrom = ringSize > 0 ? (playedNext + ringSize - 1) % ringSize : 0;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (ghostDone < ghostLength)
        {
            const auto index = static_cast<size_t> ((ghostFrom - ghostDone + ringSize) % ringSize);
            const auto gain = 1.0f - static_cast<float> (ghostDone + 1) / static_cast<float> (ghostLength);
            left[i] = gain * playedLeft[index];
            right[i] = gain * playedRight[index];
            ++ghostDone;
        }
        else
        {
            left[i] = right[i] = 0.0f;
        }
    }

    level = 0.0f;
}

void BayLink::fadeIn (float* left, float* right, int numSamples)
{
    if (level >= 1.0f)
        return;

    const auto step = stepFor (levelFade);

    for (int i = 0; i < numSamples; ++i)
    {
        level = std::min (1.0f, level + step);
        left[i] *= level;
        right[i] *= level;
    }
}

float BayLink::stepFor (Fade fade) const noexcept
{
    const auto fadeSamples = getFadeMs (fade) * sampleRate / 1000.0;
    return fadeSamples > 1.0 ? static_cast<float> (1.0 / fadeSamples) : 1.0f;
}

BayLink::Fade BayLink::fadeFor (SlotState state) noexcept
{
    switch (state)
    {
        case SlotState::mute:
            return Fade::mute;
        case SlotState::bypass:
            return Fade::bypass;
        case SlotState::off:
            return Fade::off;
        case SlotState::active:
            break;
    }

    return Fade::dropout;
}

void BayLink::sendValues (const hs_link_api& linkHub, int32_t send, juce::int64 linkStart, bool tailCut)
{
    std::array<float, numControls + 1> values {};

    for (size_t i = 0; i < static_cast<size_t> (numControls); ++i)
        values[i] = controls[i]->getValue();

    values[static_cast<size_t> (numControls)] = tailCut ? 1.0f : 0.0f;

    if (testStep.load (std::memory_order_relaxed) == testRunning && testKind.load (std::memory_order_relaxed) == probeTest)
        values[0] = probeStepAt >= 0 ? 1.0f : 0.0f;

    // A set applies until the next, so only changes (and each new link's first set) are sent
    const auto same = std::equal (values.begin(), values.end(), valuesSent.begin(), [] (float a, float b) { return juce::exactlyEqual (a, b); });

    if (send == valuesSentTo && same)
        return;

    if (linkHub.write_values (send, linkStart, values.data(), static_cast<int32_t> (values.size())) == HS_LINK_OK)
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
        if (const auto end = testStart + length; returnStart + numSamples >= end || linkEnd >= end + currentLinkDelay + probeSettle)
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
    if (const auto end = probeStepAt + probeAfter; returnStart + numSamples >= end || linkEnd >= end + currentLinkDelay + probeSettle)
        finishTest (linkEnd);
}

void BayLink::finishTest (juce::int64 linkEnd)
{
    // The test signal stops now; what's still on its way back is muted too
    muteUntil = linkEnd + currentLinkDelay + maxBlock;
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
    missingLate.store (0);
    missingGone.store (0);
    missingOther.store (0);
    receiverMisses.store (0);
    replaced.store (0);
    oversized.store (0);
    valueSets.store (0);
    heldBack.store (0);
    heldStamped.store (0);
    heldByTransmitter.store (0);

    for (auto* value : { &headroomNow, &headroomLowest, &blockMin, &blockMax, &readingLowest, &readingHighest })
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
    status.blockUnit = blockUnit.load();
    status.realBlocks = realBlocks.load();
    status.surveyingBlocks = status.realBlocks && unitSurveyPending.load();
    status.lowestTempo = lowestTempo.load();
    status.transport = transport.load();
    status.hostTempo = hostTempo.load();
    status.outputQuiet = sampleRate > 0.0 && static_cast<double> (quietSamples.load()) >= quietMs.load() * sampleRate / 1000.0;
    status.stoppedShrink = stoppedShrink.load();
    status.sampleRate = sampleRate;
    status.slotState = slotState.load();
    status.awaitingBay = status.slotState == SlotState::active && awaitingBay.load();
    status.tailCutReleaseMs = tailCutReleaseMs.load();
    status.delay = getDelaySamples();
    status.bayLatency = bayLatency.load();
    status.reading = reading.load();
    status.readingSamples = readingSamples.load();
    status.readingLowest = valueOf (readingLowest.load());
    status.readingHighest = valueOf (readingHighest.load());
    status.settleMs = settleMs.load();
    status.heldBack = heldBack.load();
    status.heldStamped = heldStamped.load();
    status.heldByTransmitter = heldByTransmitter.load();

    if (const auto packed = latencyCandidate.load(); sampleRate > 0.0)
    {
        const auto value = static_cast<int> (static_cast<juce::uint32> (packed >> 32));

        if (value >= 0 && value != status.bayLatency)
        {
            status.settlingTo = value;
            status.settledMs = juce::roundToInt (1000.0 * static_cast<juce::uint32> (packed) / sampleRate);
        }
    }

    status.dropouts = dropouts.load();
    status.crossfadeMs = getFadeMs (shownFade.load());
    status.missing = missing.load();
    status.missingLate = missingLate.load();
    status.missingGone = missingGone.load();
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

    const juce::ScopedLock lock (eventLock);
    status.events = events;
    return status;
}

void BayLink::logEvent (const juce::String& text)
{
    const auto now = juce::Time::getCurrentTime();
    const auto line = now.formatted ("%H:%M:%S.") + juce::String (now.getMilliseconds()).paddedLeft ('0', 3) + " " + text;

    const juce::ScopedLock lock (eventLock);
    events.insert (0, line);
    events.removeRange (maxEvents, events.size());
}

void BayLink::updateEventLog()
{
    // Counters only go up, except on Reset counters
    const auto logCount = [this] (const std::atomic<juce::uint64>& counter, juce::uint64& logged, const char* what) {
        const auto now = counter.load();

        if (now > logged)
            logEvent (juce::String (now - logged) + " " + what);

        logged = now;
    };

    logCount (missingLate, loggedMissingLate, "blocks missing (late: not returned yet)");
    logCount (missingGone, loggedMissingGone, "blocks missing (gone: no longer there)");
    logCount (missingOther, loggedMissingOther, "blocks missing (no return)");
    logCount (receiverMisses, loggedReceiverMisses, "blocks missed by the receiver");
    logCount (heldBack, loggedHeldBack, "blocks held back (a new bay latency settling)");
    logCount (heldStamped, loggedHeldStamped, "blocks held back (stamped by the receiver's position)");
    logCount (heldByTransmitter, loggedHeldByTransmitter, "blocks held back by the transmitter (marker unreadable, out of range or paused)");

    if (const auto now = reading.load(); loggedReading != now)
    {
        auto text = juce::String ("Marker reading: ") + toName (now);

        if (now == Reading::measured || now == Reading::outOfRange)
            text << " " << readingSamples.load();

        logEvent (text);
        loggedReading = now;
    }

    if (const auto now = slotState.load() == SlotState::active && awaitingBay.load(); loggedAwaiting != now)
    {
        logEvent (now ? "Waiting for the bay's sound: the sent audio plays" : "The bay's sound plays");
        loggedAwaiting = now;
    }
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
    return value == Dropouts::safe ? "safe" : "silence";
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

const char* BayLink::toName (SlotState value) noexcept
{
    switch (value)
    {
        case SlotState::mute:
            return "mute";
        case SlotState::bypass:
            return "bypass";
        case SlotState::off:
            return "off";
        case SlotState::active:
            break;
    }

    return "active";
}

std::optional<BayLink::SlotState> BayLink::slotStateFromName (const juce::String& name)
{
    for (const auto state : { SlotState::active, SlotState::mute, SlotState::bypass, SlotState::off })
        if (name == toName (state))
            return state;

    return std::nullopt;
}

const char* BayLink::toName (Reading value) noexcept
{
    switch (value)
    {
        case Reading::measured:
            return "measured";
        case Reading::unreadable:
            return "unreadable";
        case Reading::outOfRange:
            return "outOfRange";
        case Reading::noInput:
            return "noInput";
        case Reading::held:
            return "held";
        case Reading::none:
            break;
    }

    return "none";
}

const char* BayLink::toName (Transport value) noexcept
{
    switch (value)
    {
        case Transport::playing:
            return "playing";
        case Transport::stopped:
            return "stopped";
        case Transport::unknown:
            break;
    }

    return "unknown";
}

const char* BayLink::toName (StoppedShrink value) noexcept
{
    switch (value)
    {
        case StoppedShrink::never:
            return "never";
        case StoppedShrink::always:
            return "always";
        case StoppedShrink::whenQuiet:
            break;
    }

    return "whenQuiet";
}

std::optional<BayLink::StoppedShrink> BayLink::stoppedShrinkFromName (const juce::String& name)
{
    for (const auto rule : { StoppedShrink::never, StoppedShrink::whenQuiet, StoppedShrink::always })
        if (name == toName (rule))
            return rule;

    return std::nullopt;
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
