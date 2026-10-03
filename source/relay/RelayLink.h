#pragma once

#include "../link/LinkApi.h"
#include <juce_audio_basics/juce_audio_basics.h>

// A relay's side of a bay link, for one bay ("bay 1") of the first panel it finds.
// Receiver mode (before the FL plugin): plays what the panel sends, and holds the panel's control values for the
// relay's control outputs. Transmitter mode (after it): sends what comes out of the FL plugin back to the panel,
// stamped with the position the receiver played in the same cycle.
class RelayLink
{
public:
    // Control values a receiver holds, one per control output
    static constexpr int numControls = 16;

    enum class Mode {
        receiver,
        transmitter,
    };

    enum class State {
        waitingForPanel,
        roleTaken, // a panel exists, but another relay already has this mode's role on every one
        linked,
    };

    // Why the last block went missing
    enum class Miss {
        none,
        late, // receiver: the panel's audio hadn't arrived yet
        gone, // receiver: the panel's audio was no longer there
        receiverIdle, // transmitter: the receiver hadn't played since our last block
        receiverHadNothing, // transmitter: the receiver missed that block itself
        receiverRanTwice, // transmitter: the receiver played twice since our last block, so one block was lost
        blockSize, // transmitter: the receiver's block size differed from ours
        caughtUp, // receiver, 1-block round trip: skipped ahead to the panel's newest block
    };

    struct Status
    {
        Mode mode = Mode::receiver;
        State state = State::waitingForPanel;
        juce::String linkName;
        int roundTrip = 0; // samples, set by the panel
        double sampleRate = 0.0; // the panel's
        bool otherSideRunning = false; // receiver: the panel; transmitter: the receiver
        bool sampleRateDiffers = false;
        juce::uint64 missed = 0;
        Miss lastMiss = Miss::none;
        std::optional<int> headroomNow, headroomLowest; // receiver: samples of the panel's audio to spare
        std::optional<int> blockMin, blockMax;
        bool controlsLive = false; // receiver: the last block's values came from the panel (otherwise they're held)
        std::array<float, numControls> controls {};
    };

    explicit RelayLink (const hs_link_api&);
    ~RelayLink();

    // Message thread
    void setMode (Mode);
    Mode getMode() const noexcept { return mode.load(); }
    void prepare (double sampleRate, int maxBlockSize);
    void maintain(); // ~4 times a second: links to a panel, and drops a link whose panel has gone
    void resetCounters();
    Status getStatus();

    // Audio thread, with the relay's whole buffer (main input and output on channels 0 and 1): a receiver fills the
    // main output, a transmitter sends the main input. Every output channel ends up silent otherwise.
    void process (juce::AudioBuffer<float>&);

    // A control output's value: what the panel last sent (held through misses and without a link), kept with the
    // relay's state so knobs don't drop when a project opens. Any thread.
    float getControl (int index) const noexcept { return controls[static_cast<size_t> (index)].load (std::memory_order_relaxed); }
    void setControl (int index, float value) noexcept;

    // Blocks without new audio from the other side before it counts as stopped rather than missed
    static constexpr int stallBlocks = 2;

private:
    void unlink();
    void receive (juce::AudioBuffer<float>&);
    void receiveControls (int32_t reader, juce::int64 position);
    void transmit (const juce::AudioBuffer<float>&);
    bool matchesOwnRate (double) const noexcept;
    void noteMiss (Miss) noexcept;

    const hs_link_api& hub;
    std::atomic<Mode> mode { Mode::receiver };
    std::atomic<State> state { State::waitingForPanel };
    std::atomic<int> linkedPanel { 0 }; // 0: none
    std::atomic<int32_t> sendObserver { -1 }, sendReader { -1 }, returnWriter { -1 };
    std::atomic<juce::uint32> links { 0 }; // counts new links, so the audio thread starts each one afresh
    std::atomic<double> ownSampleRate { 0.0 };
    std::atomic<int> ownMaxBlock { 0 };
    std::atomic<bool> audioInUse { false };

    // Audio thread only
    juce::uint32 seenLinks = 0;
    bool synced = false, primed = false;
    juce::int64 cursor = 0, returnedEnd = 0;
    juce::uint64 session = 0, lastPanelWrites = 0, lastReads = 0;
    int panelStall = 0, receiverStall = 0;

    // Audio thread to message thread
    std::atomic<bool> otherSideRunning { false }, sampleRateDiffers { false }, controlsLive { false };
    std::array<std::atomic<float>, numControls> controls {};
    std::atomic<juce::uint64> missed { 0 };
    std::atomic<Miss> lastMiss { Miss::none };
    bool streaming = false; // audio thread: only a block missing from a running stream counts as missed

    static constexpr int unset = std::numeric_limits<int>::min();
    static void lower (std::atomic<int>&, int) noexcept;
    static void raise (std::atomic<int>&, int) noexcept;
    static std::optional<int> valueOf (int) noexcept;
    std::atomic<int> headroomNow { unset }, headroomLowest { unset }, blockMin { unset }, blockMax { unset };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelayLink)
};
