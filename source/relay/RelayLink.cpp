#include "RelayLink.h"
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

    // Raised while the audio thread may be using a handle; the message thread waits for it before closing one
    struct AudioUse
    {
        explicit AudioUse (std::atomic<bool>& flagToUse) : flag (flagToUse) { flag.store (true); }
        ~AudioUse() { flag.store (false); }

        std::atomic<bool>& flag;
    };

    constexpr juce::int64 markerMask = (juce::int64 (1) << RelayLink::markerBits) - 1;
    // 2^-40: the largest marker value is below 2^-18 (about -108 dB), so a marker that ends up in audio stays inaudible
    constexpr float markerScale = 1.0f / 1099511627776.0f;
    constexpr float markerUnscale = 1099511627776.0f;
    constexpr float markerLimit = 1.0f / 262144.0f; // 2^-18

    // Compared by their bits: fast-math builds may assume NaN never happens
    juce::uint32 bitsOf (float value) noexcept { return std::bit_cast<juce::uint32> (value); }
}

RelayLink::RelayLink (const hs_link_api& hubToUse)
    : hub (hubToUse)
{
}

RelayLink::~RelayLink()
{
    unlink();
}

void RelayLink::setMode (Mode newMode)
{
    if (newMode == mode.load())
        return;

    unlink();
    mode.store (newMode);
}

void RelayLink::prepare (double sampleRate, int maxBlockSize)
{
    ownSampleRate.store (sampleRate);
    ownMaxBlock.store (maxBlockSize);

    if (const auto writer = returnWriter.load(); writer >= 0)
        hub.set_writer_details (writer, sampleRate, maxBlockSize, 0);
}

void RelayLink::maintain()
{
    if (linkedPanel.load() != 0)
    {
        auto info = emptyInfo();

        if (hub.get_info (sendObserver.load(), &info) == HS_LINK_OK && info.has_writer != 0)
            return;

        // The panel has gone; look again (it may come back under the same name)
        unlink();
    }

    // Not prepared yet: no sample rate to check against, nor (as a transmitter) to announce
    if (ownSampleRate.load() <= 0.0 || ownMaxBlock.load() <= 0)
        return;

    const auto currentMode = mode.load();
    auto foundPanel = false;

    for (int panel = 1; panel <= BayNames::maxPanels; ++panel)
    {
        const auto sendName = BayNames::send (panel, 1);
        const auto observer = hub.open (sendName.c_str(), HS_LINK_OBSERVER);
        auto info = emptyInfo();

        if (observer < 0 || hub.get_info (observer, &info) != HS_LINK_OK || info.has_writer == 0)
            continue;

        foundPanel = true;
        auto claim = int32_t { -1 };

        if (currentMode == Mode::receiver)
        {
            claim = hub.open (sendName.c_str(), HS_LINK_READER);
        }
        else
        {
            claim = hub.open (BayNames::back (panel, 1).c_str(), HS_LINK_WRITER);

            if (claim >= 0 && hub.set_writer_details (claim, ownSampleRate.load(), ownMaxBlock.load(), 0) != HS_LINK_OK)
            {
                hub.close (claim);
                claim = -1;
            }
        }

        // Another relay has this role here; try the next panel
        if (claim < 0)
            continue;

        sendObserver.store (observer);
        (currentMode == Mode::receiver ? sendReader : returnWriter).store (claim);
        linkedPanel.store (panel);
        links.fetch_add (1);
        state.store (State::linked);
        return;
    }

    state.store (foundPanel ? State::roleTaken : State::waitingForPanel);
}

void RelayLink::unlink()
{
    const auto reader = sendReader.exchange (-1);
    const auto writer = returnWriter.exchange (-1);
    sendObserver.store (-1);
    linkedPanel.store (0);
    state.store (State::waitingForPanel);

    // Any block still using the old handles finishes first (one block at most). Closing early would still be
    // memory-safe: the hub refuses closed handles.
    const auto started = juce::Time::getMillisecondCounter();

    while (audioInUse.load() && juce::Time::getMillisecondCounter() - started < 200)
        juce::Thread::yield();

    if (reader >= 0)
        hub.close (reader);

    if (writer >= 0)
        hub.close (writer);
}

void RelayLink::process (juce::AudioBuffer<float>& buffer, int markerInput, int markerOutput)
{
    const AudioUse use (audioInUse);
    const auto numSamples = buffer.getNumSamples();
    lower (blockMin, numSamples);
    raise (blockMax, numSamples);

    if (const auto count = links.load(); count != seenLinks)
    {
        seenLinks = count;
        synced = primed = streaming = false;
        panelStall = receiverStall = 0;
        returnedEnd = 0;
    }

    controlsLive.store (false, std::memory_order_relaxed);

    if (mode.load() == Mode::receiver)
    {
        buffer.clear();
        receive (buffer, markerOutput >= 0 && markerOutput + 1 < buffer.getNumChannels() ? markerOutput : -1);
    }
    else
    {
        transmit (buffer, markerInput >= 0 && markerInput + 1 < buffer.getNumChannels() ? markerInput : -1);
        buffer.clear();
    }
}

void RelayLink::receive (juce::AudioBuffer<float>& buffer, int markerOutput)
{
    markerState.store (markerOutput < 0 ? MarkerState::noPin : MarkerState::silent, std::memory_order_relaxed);

    const auto reader = sendReader.load();
    auto info = emptyInfo();

    if (reader < 0 || buffer.getNumChannels() < 2 || hub.get_info (reader, &info) != HS_LINK_OK || info.has_writer == 0)
    {
        otherSideRunning.store (false, std::memory_order_relaxed);
        synced = streaming = false;
        return;
    }

    // The panel hasn't written since our last block or two (e.g. FL stopped calling it): nothing to play, nothing missed
    if (info.writes != lastPanelWrites)
    {
        lastPanelWrites = info.writes;
        panelStall = 0;
    }
    else if (++panelStall >= stallBlocks)
    {
        otherSideRunning.store (false, std::memory_order_relaxed);
        synced = streaming = false;
        return;
    }

    otherSideRunning.store (true, std::memory_order_relaxed);

    if (!matchesOwnRate (info.sample_rate))
    {
        sampleRateDiffers.store (true, std::memory_order_relaxed);
        synced = streaming = false;
        return;
    }

    sampleRateDiffers.store (false, std::memory_order_relaxed);

    // Far enough behind the panel's newest audio to be safe whichever of us runs first in a cycle, and no further
    // than the round trip allows (docs/decisions.md, "Bay link timing"). A 1-block round trip only works by playing
    // the very block the panel has just written, which needs the receiver to run after the panel.
    const auto numSamples = buffer.getNumSamples();
    const auto block = static_cast<juce::int64> (std::max (info.max_block, ownMaxBlock.load (std::memory_order_relaxed)));
    const auto followNewest = info.round_trip <= block;
    const auto lead = followNewest ? static_cast<juce::int64> (numSamples) : std::max (block, static_cast<juce::int64> (info.round_trip) - block);

    if (!synced || info.session != session)
    {
        // The panel has only just started
        if (info.readable_end - info.readable_start < lead)
            return;

        cursor = info.readable_end - lead;
        session = info.session;
        synced = true;
    }
    else if (followNewest && cursor < info.readable_end - numSamples)
    {
        // Behind the panel's newest block (e.g. a wire into the receiver was added after linking): skip ahead
        if (streaming)
            noteMiss (Miss::caughtUp);

        cursor = info.readable_end;
        return;
    }

    float* channels[] { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    const auto result = hub.read (reader, cursor, channels, 2, numSamples);

    if (result == HS_LINK_OK)
    {
        const auto headroom = static_cast<int> (std::max<juce::int64> (0, info.readable_end - (cursor + numSamples)));
        lower (headroomNow, headroom);
        lower (headroomLowest, headroom);
        receiveControls (reader, cursor + numSamples - 1);

        if (markerOutput >= 0)
        {
            writeMarker (buffer.getWritePointer (markerOutput), buffer.getWritePointer (markerOutput + 1), cursor, numSamples);
            markerState.store (MarkerState::flowing, std::memory_order_relaxed);
        }

        cursor += numSamples;
        streaming = true;
        return;
    }

    // Silence for this block (never a repeat), then line up again where either processing order is safe
    if (streaming)
        noteMiss (result == HS_LINK_NOT_YET ? Miss::late : Miss::gone);

    cursor = info.readable_end - lead + numSamples;
}

void RelayLink::receiveControls (int32_t reader, juce::int64 position)
{
    // The values in effect by the end of the block just played, so they stay in step with the audio
    std::array<float, numControls> values {};
    auto count = int32_t { 0 };

    if (hub.read_values (reader, position, values.data(), numControls, &count) != HS_LINK_OK)
        return;

    for (int i = 0; i < std::min<int> (count, numControls); ++i)
        setControl (i, values[static_cast<size_t> (i)]);

    controlsLive.store (true, std::memory_order_relaxed);
}

void RelayLink::setControl (int index, float value) noexcept
{
    // Checked by its bits: fast-math builds may assume NaN never happens
    if ((std::bit_cast<juce::uint32> (value) & 0x7f800000u) != 0x7f800000u)
        controls[static_cast<size_t> (index)].store (juce::jlimit (0.0f, 1.0f, value), std::memory_order_relaxed);
}

void RelayLink::transmit (const juce::AudioBuffer<float>& buffer, int markerInput)
{
    const auto marker = readMarkerInput (buffer, markerInput);
    const auto writer = returnWriter.load();
    const auto observer = sendObserver.load();
    auto info = emptyInfo();

    if (writer < 0 || observer < 0 || buffer.getNumChannels() < 2 || hub.get_info (observer, &info) != HS_LINK_OK || info.has_writer == 0)
    {
        otherSideRunning.store (false, std::memory_order_relaxed);
        streaming = false;
        return;
    }

    if (!matchesOwnRate (info.sample_rate))
    {
        sampleRateDiffers.store (true, std::memory_order_relaxed);
        streaming = false;
        return;
    }

    sampleRateDiffers.store (false, std::memory_order_relaxed);

    if (!primed)
    {
        lastReads = info.reads;
        primed = true;
        return;
    }

    // The receiver hasn't played since our last block: stopped, removed, or running after us
    if (info.reads == lastReads)
    {
        if (++receiverStall < stallBlocks)
        {
            if (streaming)
                noteMiss (Miss::receiverIdle);
        }
        else
        {
            otherSideRunning.store (false, std::memory_order_relaxed);
            streaming = false;
        }

        return;
    }

    const auto readsSince = info.reads - lastReads;
    lastReads = info.reads;
    receiverStall = 0;
    otherSideRunning.store (true, std::memory_order_relaxed);

    // The receiver has just played up to readEnd; the marker shows which of its blocks is arriving here now
    if (marker.has_value())
    {
        const auto readEnd = info.last_read_position + info.last_read_length;
        auto latency = (readEnd - (*marker + buffer.getNumSamples())) & markerMask;

        if (latency > markerMask / 2)
            latency -= markerMask + 1;

        const auto samples = static_cast<int> (latency);
        bayLatencyNow.store (samples, std::memory_order_relaxed);
        lower (bayLatencyLowest, samples);
        raise (bayLatencyHighest, samples);
    }

    if (readsSince > 1 && streaming)
        noteMiss (Miss::receiverRanTwice);

    if (info.last_read_ok == 0)
    {
        if (streaming)
            noteMiss (Miss::receiverHadNothing);

        return;
    }

    if (info.last_read_length != buffer.getNumSamples())
    {
        noteMiss (Miss::blockSize);
        return;
    }

    // Stamped with the position the receiver played, so the panel knows exactly which audio this is. A receiver
    // lining up again may go back over audio it already played: what came back the first time stays, only the new
    // part is sent.
    const auto numSamples = buffer.getNumSamples();
    const auto alreadySent = static_cast<int> (juce::jlimit<juce::int64> (0, numSamples, returnedEnd - info.last_read_position));

    if (alreadySent == numSamples)
        return;

    const float* channels[] { buffer.getReadPointer (0, alreadySent), buffer.getReadPointer (1, alreadySent) };

    if (hub.write (writer, info.last_read_position + alreadySent, channels, 2, numSamples - alreadySent) == HS_LINK_OK)
    {
        returnedEnd = info.last_read_position + numSamples;
        streaming = true;
    }
}

bool RelayLink::matchesOwnRate (double panelRate) const noexcept
{
    const auto own = ownSampleRate.load (std::memory_order_relaxed);
    return own > 0.0 && std::abs (panelRate - own) < 0.001;
}

std::optional<juce::int64> RelayLink::readMarkerInput (const juce::AudioBuffer<float>& buffer, int markerInput) noexcept
{
    if (markerInput < 0)
    {
        markerState.store (MarkerState::noPin, std::memory_order_relaxed);
        return std::nullopt;
    }

    const auto* left = buffer.getReadPointer (markerInput);
    const auto* right = buffer.getReadPointer (markerInput + 1);
    const auto numSamples = buffer.getNumSamples();
    const auto isZero = [] (float sample) { return (bitsOf (sample) & 0x7fffffffu) == 0; };

    if (std::all_of (left, left + numSamples, isZero) && std::all_of (right, right + numSamples, isZero))
    {
        markerState.store (MarkerState::silent, std::memory_order_relaxed);
        return std::nullopt;
    }

    const auto marker = readMarker (left, right, numSamples);

    if (!marker.has_value())
        markerUnreadable.fetch_add (1, std::memory_order_relaxed);

    markerState.store (marker.has_value() ? MarkerState::flowing : MarkerState::unreadable, std::memory_order_relaxed);
    return marker;
}

void RelayLink::writeMarker (float* left, float* right, juce::int64 position, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto value = (position + i) & markerMask;
        left[i] = static_cast<float> (value) * markerScale;
        right[i] = static_cast<float> (markerMask - value) * markerScale;
    }
}

std::optional<juce::int64> RelayLink::readMarker (const float* left, const float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return std::nullopt;

    // Positive floats order like their bits, so this also turns away negatives, NaN and infinity
    const auto limit = bitsOf (markerLimit);
    juce::int64 first = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        if (bitsOf (left[i]) >= limit)
            return std::nullopt;

        const auto value = static_cast<juce::int64> (left[i] * markerUnscale);

        // Exactly a marker value, its exact complement beside it, and the one after the previous sample's
        if (bitsOf (static_cast<float> (value) * markerScale) != bitsOf (left[i])
            || bitsOf (static_cast<float> (markerMask - value) * markerScale) != bitsOf (right[i])
            || (i > 0 && value != ((first + i) & markerMask)))
            return std::nullopt;

        if (i == 0)
            first = value;
    }

    return first;
}

void RelayLink::noteMiss (Miss reason) noexcept
{
    missed.fetch_add (1, std::memory_order_relaxed);
    lastMiss.store (reason, std::memory_order_relaxed);
}

void RelayLink::resetCounters()
{
    missed.store (0);
    lastMiss.store (Miss::none);
    markerUnreadable.store (0);

    for (auto* value : { &headroomNow, &headroomLowest, &blockMin, &blockMax, &bayLatencyNow, &bayLatencyLowest, &bayLatencyHighest })
        value->store (unset);
}

RelayLink::Status RelayLink::getStatus()
{
    Status status;
    status.mode = mode.load();
    status.state = state.load();

    if (const auto panel = linkedPanel.load(); panel != 0)
    {
        status.linkName = BayNames::linkName (panel);
        auto info = emptyInfo();

        if (hub.get_info (sendObserver.load(), &info) == HS_LINK_OK)
        {
            status.roundTrip = info.round_trip;
            status.sampleRate = info.sample_rate;
        }
    }

    status.otherSideRunning = otherSideRunning.load();
    status.sampleRateDiffers = sampleRateDiffers.load();
    status.missed = missed.load();
    status.lastMiss = lastMiss.load();
    status.headroomNow = valueOf (headroomNow.exchange (unset));
    status.headroomLowest = valueOf (headroomLowest.load());
    status.blockMin = valueOf (blockMin.load());
    status.blockMax = valueOf (blockMax.load());
    status.controlsLive = controlsLive.load();
    status.marker = markerState.load();
    status.bayLatencyNow = valueOf (bayLatencyNow.exchange (unset));
    status.bayLatencyLowest = valueOf (bayLatencyLowest.load());
    status.bayLatencyHighest = valueOf (bayLatencyHighest.load());
    status.markerUnreadable = markerUnreadable.load();

    for (int i = 0; i < numControls; ++i)
        status.controls[static_cast<size_t> (i)] = getControl (i);

    return status;
}

void RelayLink::lower (std::atomic<int>& lowest, int value) noexcept
{
    const auto current = lowest.load (std::memory_order_relaxed);

    if (current == unset || value < current)
        lowest.store (value, std::memory_order_relaxed);
}

void RelayLink::raise (std::atomic<int>& highest, int value) noexcept
{
    const auto current = highest.load (std::memory_order_relaxed);

    if (current == unset || value > current)
        highest.store (value, std::memory_order_relaxed);
}

std::optional<int> RelayLink::valueOf (int stored) noexcept
{
    return stored == unset ? std::nullopt : std::optional<int> (stored);
}

std::vector<RelayLink::PanelView> RelayLink::getPanels()
{
    std::vector<PanelView> panels;

    for (int panel = 1; panel <= BayNames::maxPanels; ++panel)
    {
        auto send = emptyInfo();
        auto back = emptyInfo();

        // Observers never create a channel: a name never used has none
        if (hub.get_info (hub.open (BayNames::send (panel, 1).c_str(), HS_LINK_OBSERVER), &send) != HS_LINK_OK)
            continue;

        const auto hasReturn = hub.get_info (hub.open (BayNames::back (panel, 1).c_str(), HS_LINK_OBSERVER), &back) == HS_LINK_OK;
        auto& last = panelCounts[static_cast<size_t> (panel)];

        PanelView view;
        view.number = panel;
        view.panel = send.has_writer != 0;
        view.panelRunning = view.panel && send.writes != last[0];
        view.receiver = send.has_reader != 0;
        view.receiverRunning = view.receiver && send.reads != last[1];
        view.transmitter = hasReturn && back.has_writer != 0;
        view.transmitterRunning = view.transmitter && back.writes != last[2];
        last = { send.writes, send.reads, back.writes };
        panels.push_back (view);
    }

    return panels;
}
