#pragma once

#include "../link/LinkApi.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

// The panel's side of an external module bay (one bay for now, after the engine's output). It sends the engine's
// output to a receiver relay, gets it back from a transmitter relay after FL's plugin, and plays the return (or the
// direct sound) a fixed round trip later, which the shell reports to the host as latency. It never waits for a relay.
// The shell finds the hub (see RelayHubFinder); this class only uses the hub's C interface.
class BayLink
{
public:
    // What looking for the hub found: the hub, or why there isn't one
    struct HubSearch
    {
        const hs_link_api* hub = nullptr;
        juce::String problem;
    };

    // Called on the message thread about once a second until it returns a hub
    using HubLocator = std::function<HubSearch()>;

    enum class Output {
        direct,
        bayReturn,
    };

    // What plays in a return block that's missing
    enum class MissingBlocks {
        silence,
        bypass, // the matching block that was sent, so only the bay is skipped
    };

    // How far the return is from the audio it was sent with, found by comparing the two
    struct Lag
    {
        enum class State {
            notMeasured,
            measuring,
            silent, // nothing to compare
            found,
            noMatch,
        };

        State state = State::notMeasured;
        int samples = 0; // positive: the return arrives later than the matching sent audio
        bool exact = false;
        double similarity = 0.0; // 1 is identical in shape
        int earliest = 0, latest = 0; // the range searched

        // Another delay fits about as well (a held note repeats every cycle of its waveform), so `samples` is a guess
        bool ambiguous = false;
        int alternative = 0;
    };

    struct Status
    {
        juce::String hubProblem; // empty once the hub is found
        juce::String linkName; // "Panel 1"; empty until linked
        juce::String linkProblem;
        bool receiverLinked = false, receiverPlaying = false;
        bool transmitterLinked = false, transmitterReturning = false;
        int roundTripBlocks = 0, roundTripSamples = 0, maxBlock = 0;
        double sampleRate = 0.0;
        Output output = Output::direct;
        MissingBlocks missingBlocks = MissingBlocks::silence;
        juce::uint64 missing = 0; // return blocks missing while the return was flowing
        juce::uint64 replaced = 0; // returned samples that weren't numbers (NaN, infinity), played as silence
        juce::uint64 oversized = 0; // blocks longer than the host announced, played as silence
        std::optional<int> headroomNow, headroomLowest; // samples of returned audio to spare
        std::optional<int> blockMin, blockMax;
        std::optional<float> differenceNow, differenceLargest; // return against the audio sent
        Lag lag;
    };

    explicit BayLink (HubLocator);
    ~BayLink();

    // Message thread (prepare: never while audio runs)
    void prepare (double sampleRate, int maxBlockSize);
    void setRoundTripBlocks (int);
    int getRoundTripBlocks() const noexcept { return roundTripBlocks.load(); }
    int getRoundTripSamples() const noexcept { return roundTrip.load(); }
    void setOutput (Output newOutput) noexcept { output.store (newOutput); }
    Output getOutput() const noexcept { return output.load(); }
    void setMissingBlocks (MissingBlocks newMode) noexcept { missingBlocks.store (newMode); }
    MissingBlocks getMissingBlocks() const noexcept { return missingBlocks.load(); }
    void maintain(); // looks for the hub until it's found, then claims this panel's link; a timer calls it once a second
    void measureLag();
    void resetCounters();
    Status getStatus(); // also takes the "now" figures, so they cover the time since the last call

    // Audio thread: the engine's output in, the panel's output out (channels 0 and 1)
    void process (juce::AudioBuffer<float>&);

    static constexpr int minRoundTripBlocks = 1;
    static constexpr int maxRoundTripBlocks = 3;
    static constexpr int defaultRoundTripBlocks = 2;

    // Blocks without new returned audio before the return counts as stopped rather than missing
    static constexpr int stallBlocks = 2;

    // Blocks a newly flowing return gets to fill the round trip before missing blocks count
    static constexpr int warmUpBlocks = maxRoundTripBlocks + 2;

    // Lag search: a stretch of the return, and how far apart the two may be (FL's linear-phase EQ 2 reaches 12,288)
    static constexpr int lagWindow = 1024;
    static constexpr int lagLatest = 32768;
    static constexpr int lagEarliest = 1024;

    // How close in similarity another delay must come for a match to count as unclear
    static constexpr double ambiguityMargin = 0.01;

    // Compares `window` samples of `returned` with `direct`, which holds `latest` samples before the same stretch and
    // `earliest` after it. An exact match wins (the nearest one); otherwise the most similar shape, flagged as
    // ambiguous when another peak comes within ambiguityMargin of it.
    static Lag findLag (const float* returned, const float* direct, int window, int earliest, int latest);

    static const char* toName (Output) noexcept;
    static std::optional<Output> outputFromName (const juce::String&);
    static const char* toName (MissingBlocks) noexcept;
    static std::optional<MissingBlocks> missingBlocksFromName (const juce::String&);

private:
    // Stereo audio by position on the panel's timeline, for the delayed direct sound and the lag search
    struct History
    {
        void allocate (int minimumFrames);
        void write (juce::int64 position, const float* left, const float* right, int numFrames);
        void silence (juce::int64 position, int numFrames);
        void read (juce::int64 position, float* left, float* right, int numFrames) const;
        void readMono (juce::int64 position, float* mono, int numFrames) const;

        std::vector<float> left, right;
        juce::int64 mask = 0;
    };

    enum LagStep {
        lagIdle,
        lagRequested, // the audio thread fills the lag buffers...
        lagReady, // ...and hands them to the message thread
    };

    void linkIfReady();
    void captureLag (juce::int64 returnStart, int numSamples, juce::int64 directEnd);

    static constexpr int unset = std::numeric_limits<int>::min();
    static void lower (std::atomic<int>&, int) noexcept;
    static void raise (std::atomic<int>&, int) noexcept;
    static std::optional<int> valueOf (int) noexcept;

    HubLocator locator;
    std::atomic<const hs_link_api*> hub { nullptr };
    std::atomic<int32_t> sendHandle { -1 }, returnHandle { -1 };

    // Message thread
    juce::String hubProblem { "Not looked for yet" }, linkName, linkProblem;
    double sampleRate = 0.0;
    int maxBlock = 0; // also read by the audio thread, which never runs during prepare()
    juce::uint64 statusReads = 0, statusWrites = 0;
    Lag lagResult;

    std::atomic<int> roundTripBlocks { defaultRoundTripBlocks }, roundTrip { 0 };
    std::atomic<Output> output { Output::direct };
    std::atomic<MissingBlocks> missingBlocks { MissingBlocks::silence };

    // The link's timeline never goes below this: past anything an earlier panel left in the channels
    std::atomic<juce::int64> minimumLinkPosition { juce::int64 (1) << 20 };

    // Audio thread. The history counts every sample played; the link's timeline is that plus an offset, which only
    // ever grows, so the delayed direct sound never jumps when the link does.
    juce::int64 position = 0, linkOffset = 0;
    juce::uint64 lastReturnWrites = 0;
    int returnStall = 0, flowingBlocks = 0;
    History history;
    std::vector<float> returnLeft, returnRight, directLeft, directRight;
    int lagCaptured = 0;
    juce::int64 lagNext = 0;

    // Lag buffers: the audio thread's while requested, the message thread's while ready
    std::atomic<int> lagStep { lagIdle };
    std::vector<float> lagReturn, lagDirect;
    int lagCapturedEarliest = 0;

    // Audio thread to message thread
    std::atomic<juce::uint64> missing { 0 }, replaced { 0 }, oversized { 0 };
    std::atomic<int> headroomNow { unset }, headroomLowest { unset }, blockMin { unset }, blockMax { unset };
    std::atomic<float> differenceNow { -1.0f }, differenceLargest { -1.0f };

    // Declared last, so it stops before anything it uses goes
    juce::TimedCallback hubCheck { [this] { maintain(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BayLink)
};
