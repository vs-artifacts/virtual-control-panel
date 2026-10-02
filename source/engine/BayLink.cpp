#include "BayLink.h"
#include "../link/BayNames.h"
#include <bit>

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
}

BayLink::BayLink (HubLocator locatorToUse)
    : locator (std::move (locatorToUse))
{
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

    history.allocate ((maxRoundTripBlocks + 1) * maxBlock + lagWindow + lagLatest + lagEarliest);
    lagReturn.assign (static_cast<size_t> (lagWindow), 0.0f);
    lagDirect.assign (static_cast<size_t> (lagWindow + lagLatest + lagEarliest), 0.0f);
    lagStep.store (lagIdle);
    lagCaptured = 0;

    if (lagResult.state == Lag::State::measuring)
        lagResult = {};

    roundTrip.store (roundTripBlocks.load() * maxBlock);
    returnStall = flowingBlocks = 0;
    blockMin.store (unset);
    blockMax.store (unset);

    // A new session: receivers line up again with the new block size and round trip
    if (const auto send = sendHandle.load(); send >= 0)
        hub.load()->set_writer_details (send, sampleRate, maxBlock, roundTrip.load());
    else
        linkIfReady();
}

void BayLink::setRoundTripBlocks (int blocks)
{
    roundTripBlocks.store (juce::jlimit (minRoundTripBlocks, maxRoundTripBlocks, blocks));
    roundTrip.store (roundTripBlocks.load() * maxBlock);

    if (const auto send = sendHandle.load(); send >= 0 && maxBlock > 0)
        hub.load()->set_writer_details (send, sampleRate, maxBlock, roundTrip.load());
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

    if (const auto minimum = minimumLinkPosition.load (std::memory_order_relaxed); start + linkOffset < minimum)
        linkOffset = minimum - start;

    const auto linkStart = start + linkOffset;
    history.write (start, left, right, numSamples);

    const auto* linkHub = hub.load (std::memory_order_acquire);
    const auto send = sendHandle.load (std::memory_order_acquire);
    const auto back = returnHandle.load (std::memory_order_acquire);

    if (linkHub != nullptr && send >= 0)
    {
        const float* channels[] { left, right };
        linkHub->write (send, linkStart, channels, 2, numSamples);
    }

    auto result = int32_t { HS_LINK_CLOSED };
    auto flowing = false;

    if (linkHub != nullptr && back >= 0)
    {
        const auto returnStart = linkStart - roundTripSamples;
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
                ++returnStall;
            }

            flowing = returnStall < stallBlocks;

            if (flowing)
            {
                const auto headroom = static_cast<int> (juce::jlimit<juce::int64> (-HS_LINK_CAPACITY, HS_LINK_CAPACITY, info.readable_end - (returnStart + numSamples)));
                lower (headroomNow, headroom);
                lower (headroomLowest, headroom);
            }
        }
    }

    flowingBlocks = flowing ? std::min (flowingBlocks + 1, warmUpBlocks + 1) : 0;
    history.read (start - roundTripSamples, directLeft.data(), directRight.data(), numSamples);

    if (result == HS_LINK_OK)
    {
        const auto badSamples = replaceNonFinite (returnLeft.data(), numSamples) + replaceNonFinite (returnRight.data(), numSamples);

        if (badSamples > 0)
            replaced.fetch_add (badSamples, std::memory_order_relaxed);

        const auto difference = std::max (largestDifference (returnLeft.data(), directLeft.data(), numSamples),
            largestDifference (returnRight.data(), directRight.data(), numSamples));
        keepLargest (differenceNow, difference);
        keepLargest (differenceLargest, difference);
        captureLag (start - roundTripSamples, numSamples, start + numSamples);
    }
    else
    {
        // A return that's flowing but late or broken; not while a new one fills the round trip
        if (flowing && flowingBlocks > warmUpBlocks)
            missing.fetch_add (1, std::memory_order_relaxed);

        lagCaptured = 0;
    }

    const float* fromLeft = directLeft.data();
    const float* fromRight = directRight.data();

    if (output.load (std::memory_order_relaxed) == Output::bayReturn)
    {
        if (result == HS_LINK_OK)
        {
            fromLeft = returnLeft.data();
            fromRight = returnRight.data();
        }
        else if (missingBlocks.load (std::memory_order_relaxed) == MissingBlocks::silence)
        {
            fromLeft = fromRight = nullptr;
        }
    }

    if (fromLeft == nullptr)
    {
        buffer.clear (0, 0, numSamples);
        buffer.clear (1, 0, numSamples);
    }
    else
    {
        juce::FloatVectorOperations::copy (left, fromLeft, numSamples);
        juce::FloatVectorOperations::copy (right, fromRight, numSamples);
    }

    position = start + numSamples;
}

void BayLink::captureLag (juce::int64 returnStart, int numSamples, juce::int64 directEnd)
{
    if (lagStep.load (std::memory_order_acquire) != lagRequested)
    {
        lagCaptured = 0;
        return;
    }

    // The stretch must be unbroken
    if (lagCaptured > 0 && returnStart != lagNext)
        lagCaptured = 0;

    const auto take = std::min (numSamples, lagWindow - lagCaptured);

    for (int i = 0; i < take; ++i)
        lagReturn[static_cast<size_t> (lagCaptured + i)] = returnLeft[static_cast<size_t> (i)] + returnRight[static_cast<size_t> (i)];

    lagCaptured += take;
    lagNext = returnStart + take;

    if (lagCaptured < lagWindow)
        return;

    // The sent audio around the same stretch: lagLatest samples before it, and as much after it as exists yet
    lagCapturedEarliest = static_cast<int> (std::min<juce::int64> (lagEarliest, directEnd - lagNext));
    history.readMono (lagNext - lagWindow - lagLatest, lagDirect.data(), lagWindow + lagLatest + lagCapturedEarliest);
    lagCaptured = 0;
    lagStep.store (lagReady, std::memory_order_release);
}

void BayLink::measureLag()
{
    if (lagStep.load() == lagReady)
        return;

    lagResult = {};
    lagResult.state = Lag::State::measuring;
    lagStep.store (lagRequested, std::memory_order_release);
}

void BayLink::resetCounters()
{
    missing.store (0);
    replaced.store (0);
    oversized.store (0);

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

    status.roundTripBlocks = roundTripBlocks.load();
    status.roundTripSamples = roundTrip.load();
    status.maxBlock = maxBlock;
    status.sampleRate = sampleRate;
    status.output = output.load();
    status.missingBlocks = missingBlocks.load();
    status.missing = missing.load();
    status.replaced = replaced.load();
    status.oversized = oversized.load();
    status.headroomNow = valueOf (headroomNow.exchange (unset));
    status.headroomLowest = valueOf (headroomLowest.load());
    status.blockMin = valueOf (blockMin.load());
    status.blockMax = valueOf (blockMax.load());
    status.differenceNow = takeFloat (differenceNow.exchange (-1.0f));
    status.differenceLargest = takeFloat (differenceLargest.load());

    if (lagStep.load (std::memory_order_acquire) == lagReady)
    {
        lagResult = findLag (lagReturn.data(), lagDirect.data(), lagWindow, lagCapturedEarliest, lagLatest);
        lagStep.store (lagIdle, std::memory_order_release);
    }

    status.lag = lagResult;
    return status;
}

BayLink::Lag BayLink::findLag (const float* returned, const float* direct, int window, int earliest, int latest)
{
    Lag lag;
    lag.earliest = earliest;
    lag.latest = latest;

    auto returnedEnergy = 0.0;

    for (int i = 0; i < window; ++i)
        returnedEnergy += static_cast<double> (returned[i]) * returned[i];

    if (returnedEnergy < 1.0e-12)
    {
        lag.state = Lag::State::silent;
        return lag;
    }

    // Candidate i is a return (i - earliest) samples late, matching the sent audio that many samples earlier
    const auto numCandidates = earliest + latest + 1;
    std::vector<double> similarities (static_cast<size_t> (numCandidates));
    std::optional<int> exactLag;
    auto sentEnergy = 0.0;

    for (int index = 0; index < numCandidates; ++index)
    {
        const auto candidate = index - earliest;
        const auto* sent = direct + (latest - candidate);

        // Each step moves the stretch one sample earlier; recomputed now and then so rounding can't build up
        if (index % 512 == 0)
        {
            sentEnergy = 0.0;

            for (int i = 0; i < window; ++i)
                sentEnergy += static_cast<double> (sent[i]) * sent[i];
        }
        else
        {
            sentEnergy += static_cast<double> (sent[0]) * sent[0] - static_cast<double> (sent[window]) * sent[window];
        }

        auto product = 0.0;

        for (int i = 0; i < window; ++i)
            product += static_cast<double> (returned[i]) * sent[i];

        const auto similarity = sentEnergy > 0.0 ? product / std::sqrt (returnedEnergy * sentEnergy) : 0.0;
        similarities[static_cast<size_t> (index)] = similarity;

        if (similarity > 0.9999 && (!exactLag.has_value() || std::abs (candidate) < std::abs (*exactLag))
            && std::equal (returned, returned + window, sent, [] (float a, float b) { return juce::exactlyEqual (a, b); }))
            exactLag = candidate;
    }

    if (exactLag.has_value())
    {
        lag.state = Lag::State::found;
        lag.samples = *exactLag;
        lag.exact = true;
        lag.similarity = 1.0;
        return lag;
    }

    const auto best = static_cast<int> (std::max_element (similarities.begin(), similarities.end()) - similarities.begin());
    lag.similarity = similarities[static_cast<size_t> (best)];
    lag.samples = best - earliest;
    lag.state = lag.similarity < 0.5 ? Lag::State::noMatch : Lag::State::found;

    // The strongest other peak: a sound that repeats (a held note) matches at many delays about equally well
    auto secondBest = -2.0;

    for (int index = 0; index < numCandidates; ++index)
    {
        const auto value = similarities[static_cast<size_t> (index)];
        const auto isPeak = (index == 0 || value > similarities[static_cast<size_t> (index - 1)])
                            && (index == numCandidates - 1 || value >= similarities[static_cast<size_t> (index + 1)]);

        if (isPeak && index != best && value > secondBest)
        {
            secondBest = value;
            lag.alternative = index - earliest;
        }
    }

    lag.ambiguous = lag.state == Lag::State::found && secondBest >= lag.similarity - ambiguityMargin;
    return lag;
}

const char* BayLink::toName (Output value) noexcept
{
    return value == Output::bayReturn ? "return" : "direct";
}

std::optional<BayLink::Output> BayLink::outputFromName (const juce::String& name)
{
    if (name == "direct")
        return Output::direct;

    if (name == "return")
        return Output::bayReturn;

    return std::nullopt;
}

const char* BayLink::toName (MissingBlocks value) noexcept
{
    return value == MissingBlocks::bypass ? "bypass" : "silence";
}

std::optional<BayLink::MissingBlocks> BayLink::missingBlocksFromName (const juce::String& name)
{
    if (name == "silence")
        return MissingBlocks::silence;

    if (name == "bypass")
        return MissingBlocks::bypass;

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

void BayLink::History::readMono (juce::int64 position, float* mono, int numFrames) const
{
    for (int i = 0; i < numFrames; ++i)
    {
        const auto index = static_cast<size_t> ((position + i) & mask);
        mono[i] = left[index] + right[index];
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
