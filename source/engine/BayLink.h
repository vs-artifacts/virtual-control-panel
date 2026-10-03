#pragma once

#include "../link/LinkApi.h"
#include <juce_audio_processors/juce_audio_processors.h>

// A value the panel sends to a bay's receiver relay, which plays it as a steady level on its control output of the
// same number (into an FL Peak Controller linked to one knob). Any thread may set it.
class BayControl final : public juce::AudioProcessorParameter
{
public:
    explicit BayControl (int number);

    float getValue() const override { return value.load (std::memory_order_relaxed); }
    void setValue (float) override;
    float getDefaultValue() const override { return 0.0f; }
    juce::String getName (int maximumStringLength) const override;
    juce::String getLabel() const override { return {}; }
    juce::String getText (float normalisedValue, int maximumStringLength) const override;
    float getValueForText (const juce::String&) const override;

private:
    const int number;
    std::atomic<float> value { 0.0f };
};

// The panel's side of an external module bay (one bay for now, after the engine's output). It sends the engine's
// output and the bay's control values to a receiver relay, gets the audio back from a transmitter relay after FL's
// plugin, and plays the return (or, bypassed, the sent audio) a fixed round trip later, which the shell reports to the
// host as latency. It never waits for a relay.
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

    // What plays when a return block is missing (a dropout)
    enum class Dropouts {
        safe, // the matching block that was sent, so only the bay is skipped
        silence, // makes dropouts audible
    };

    // How the automatic round trip got to its choice
    enum class AutoState {
        off, // a fixed round trip is set
        checking, // trying 1 block since the relays last linked
        oneBlock, // 1 block has run a second without a miss; any miss still falls back
        fellBack, // 1 block missed, so 2 until the relays link again
    };

    // How late the bay returns what it's sent: the panel sends a known noise burst instead of its sound (output muted)
    // and finds every delay at which the burst comes back. Two paths through the bay show as two delays.
    struct Lag
    {
        enum class State {
            notMeasured,
            measuring,
            found,
            noReturn, // too little audio came back
            noMatch, // the burst didn't come back recognisably
        };

        // A delay at which the burst comes back, and how strongly (1: as loud as sent)
        struct Path
        {
            int samples = 0; // positive: later than sent
            float gain = 0.0f;
        };

        State state = State::notMeasured;
        std::vector<Path> paths; // the strongest first
        bool exact = false; // the strongest path is the burst itself, unchanged
        int earliest = 0, latest = 0; // the range searched
    };

    // How long a step on control 1 takes to show in the return: the panel sends a steady tone instead of its sound,
    // holds control 1 at 0, then steps it to 1 and watches the return's level (control 1 must drive a level knob)
    struct Probe
    {
        enum class State {
            notRun,
            running,
            found,
            noReturn, // too little returned audio to compare
            noChange, // the return's level didn't follow control 1
        };

        State state = State::notRun;
        int delay = 0; // samples from the step to the return's level halfway between before and after
        int rise = 0; // samples from 10% to 90% of the change
        float before = 0.0f, after = 0.0f; // the return's level (peak per sample) before and after the step
    };

    struct Status
    {
        juce::String hubProblem; // empty once the hub is found
        juce::String linkName; // "Panel 1"; empty until linked
        juce::String linkProblem;
        bool receiverLinked = false, receiverPlaying = false;
        bool transmitterLinked = false, transmitterReturning = false;
        int roundTripSetting = 0; // autoRoundTrip, or a number of blocks
        AutoState autoState = AutoState::off;
        int roundTripBlocks = 0, roundTripSamples = 0, maxBlock = 0;
        double sampleRate = 0.0;
        bool bypass = false;
        Dropouts dropouts = Dropouts::safe;
        int crossfadeMs = 0;
        juce::uint64 missing = 0; // return blocks missing while the return was flowing
        juce::uint64 receiverMisses = 0; // blocks the receiver couldn't play (each also leaves a gap in the return)
        juce::uint64 replaced = 0; // returned samples that weren't numbers (NaN, infinity), played as silence
        juce::uint64 oversized = 0; // blocks longer than the host announced, played as silence
        juce::uint64 valueSets = 0; // control value sets sent
        std::optional<int> headroomNow, headroomLowest; // samples of returned audio to spare
        std::optional<int> blockMin, blockMax;
        std::optional<float> differenceNow, differenceLargest; // return against the audio sent
        Lag lag;
        Probe probe;
    };

    explicit BayLink (HubLocator);
    ~BayLink();

    // Message thread (prepare: never while audio runs)
    void prepare (double sampleRate, int maxBlockSize);
    void setRoundTripBlocks (int blocksOrAuto); // autoRoundTrip, or 1 to 3 blocks
    int getRoundTripBlocks() const noexcept { return roundTripSetting.load(); }
    int getRoundTripSamples() const noexcept { return roundTrip.load(); }
    bool updateRoundTrip(); // runs the automatic round trip; true if the round trip changed. A timer calls it twice a second.
    void setBypass (bool shouldBypass) noexcept { bypass.store (shouldBypass); }
    bool isBypassed() const noexcept { return bypass.load(); }
    void setDropouts (Dropouts newMode) noexcept { dropouts.store (newMode); }
    Dropouts getDropouts() const noexcept { return dropouts.load(); }
    void setCrossfadeMs (int milliseconds) noexcept { crossfadeMs.store (juce::jlimit (0, maxCrossfadeMs, milliseconds)); }
    int getCrossfadeMs() const noexcept { return crossfadeMs.load(); }
    void maintain(); // looks for the hub until it's found, then claims this panel's link; a timer calls it once a second
    void measureLag();
    void startProbe();
    void resetCounters();
    Status getStatus(); // also takes the "now" figures, so they cover the time since the last call

    // The values this bay sends to its receiver, one per control output
    static constexpr int numControls = 16;
    BayControl& getControl (int index) const { return *controls[static_cast<size_t> (index)]; }

    // Audio thread: the engine's output in, the panel's output out (channels 0 and 1)
    void process (juce::AudioBuffer<float>&);

    static constexpr int autoRoundTrip = 0;
    static constexpr int minRoundTripBlocks = 1;
    static constexpr int maxRoundTripBlocks = 3;
    static constexpr int defaultRoundTripBlocks = autoRoundTrip;

    static constexpr int maxCrossfadeMs = 50;
    static constexpr int defaultCrossfadeMs = 30;

    // No new returned audio for this long: the return counts as stopped rather than missing (e.g. FL's smart disable)
    static constexpr double stallSeconds = 0.1;

    // Largest blocks a newly flowing return gets, on top of the round trip, before missing blocks count
    static constexpr int warmUpBlocks = 2;

    // After a new round trip (or block size), the relays line up again; the automatic round trip ignores this long
    static constexpr double autoSettleSeconds = 0.25;

    // How long 1 block must run without a miss before the automatic round trip says it works
    static constexpr double autoCheckSeconds = 1.0;

    // Fading the output out and in around a probe
    static constexpr double muteFadeSeconds = 0.02;

    // Probe timing: control 1 held at 0, then the stretch of return compared before and after the step
    static constexpr double probeSettleSeconds = 0.5;
    static constexpr double probeBeforeSeconds = 0.25;
    static constexpr double probeAfterSeconds = 1.0;
    static constexpr float probeToneLevel = 0.25f;

    // Lag measurement: the burst, and how far apart sent and returned may be (FL's linear-phase EQ 2 reaches 12,288)
    static constexpr int lagBurst = 8192;
    static constexpr float lagBurstLevel = 0.25f;
    static constexpr int lagLatest = 32768;
    static constexpr int lagEarliest = 1024;

    // Another delay counts as a path of its own if it comes back at least this strongly, relative to the strongest
    static constexpr float lagPathShare = 0.2f;

    // The burst: the same noise every time
    static std::vector<float> makeLagBurst();

    // Finds the paths in `captured`, which holds the return from `earliest` samples before the burst was sent to
    // `latest` samples after its end (earliest + burstLength + latest samples)
    static Lag findLag (const float* captured, const float* burst, int burstLength, int earliest, int latest);

    // Reads a probe's capture: `envelope` holds the return's level per sample (negative where missing), with the step
    // at index `step`, and `before` samples before it and `after` from it to compare
    static Probe findProbeResult (const float* envelope, int step, int before, int after);

    static const char* toName (Dropouts) noexcept;
    static std::optional<Dropouts> dropoutsFromName (const juce::String&);
    static const char* toName (AutoState) noexcept;

private:
    // Stereo audio by position on the panel's timeline, for the delayed direct sound
    struct History
    {
        void allocate (int minimumFrames);
        void write (juce::int64 position, const float* left, const float* right, int numFrames);
        void silence (juce::int64 position, int numFrames);
        void read (juce::int64 position, float* left, float* right, int numFrames) const;

        std::vector<float> left, right;
        juce::int64 mask = 0;
    };

    // The probe and the lag measurement: one at a time, each replacing the panel's sound for a moment
    enum TestKind {
        probeTest,
        lagTest,
    };

    enum TestStep {
        testIdle,
        testRequested,
        testRunning, // the audio thread sends the test signal and captures the return...
        testReady, // ...and hands the capture to the message thread
    };

    void linkIfReady();
    bool applyRoundTrip (int blocks);
    void startAutoCheck();
    void startTest (TestKind);
    void sendValues (const hs_link_api&, int32_t send, juce::int64 linkStart);
    void runTest (juce::int64 linkStart, int numSamples, float* left, float* right);
    void captureTest (juce::int64 returnStart, int numSamples, bool returned, juce::int64 linkEnd);
    void finishTest (juce::int64 linkEnd);
    void mix (float* left, float* right, int numSamples, bool returned);
    void fadeOutput (float* left, float* right, int numSamples, float target);

    static constexpr int unset = std::numeric_limits<int>::min();
    static void lower (std::atomic<int>&, int) noexcept;
    static void raise (std::atomic<int>&, int) noexcept;
    static std::optional<int> valueOf (int) noexcept;

    HubLocator locator;
    std::atomic<const hs_link_api*> hub { nullptr };
    std::atomic<int32_t> sendHandle { -1 }, returnHandle { -1 };
    std::vector<std::unique_ptr<BayControl>> controls;

    // Message thread
    juce::String hubProblem { "Not looked for yet" }, linkName, linkProblem;
    double sampleRate = 0.0; // also read by the audio thread, which never runs during prepare()
    int maxBlock = 0; // likewise
    juce::uint64 statusReads = 0, statusWrites = 0;
    Lag lagResult;
    Probe probeResult;
    int autoBlocks = minRoundTripBlocks;
    std::optional<juce::uint64> relayClaims; // the receiver's and transmitter's claim counts when last checked

    std::atomic<int> roundTripSetting { defaultRoundTripBlocks }, roundTripBlocks { minRoundTripBlocks }, roundTrip { 0 };
    std::atomic<AutoState> autoState { AutoState::checking };
    std::atomic<bool> bypass { false };
    std::atomic<Dropouts> dropouts { Dropouts::safe };
    std::atomic<int> crossfadeMs { defaultCrossfadeMs };

    // The link's timeline never goes below this: past anything an earlier panel left in the channels
    std::atomic<juce::int64> minimumLinkPosition { juce::int64 (1) << 20 };

    // Audio thread. The history counts every sample played; the link's timeline is that plus an offset, which only
    // ever grows, so the delayed direct sound never jumps when the link does.
    juce::int64 position = 0, linkOffset = 0;
    juce::uint64 lastReturnWrites = 0;
    juce::int64 returnStall = 0; // samples since the transmitter last wrote
    juce::int64 flowingRun = 0; // samples since the return started flowing
    int lastRoundTrip = -1;
    juce::int64 roundTripRun = 0; // samples since the round trip (or the block size) last changed
    int32_t receiverCountedOn = -1; // the send handle receiverFailures counts for
    juce::uint64 receiverFailures = 0;
    float returnMix = 0.0f; // how much of the output is the return rather than the sent audio
    float outputGain = 1.0f;
    History history;
    std::vector<float> returnLeft, returnRight, directLeft, directRight;
    int32_t valuesSentTo = -1; // the send handle the last value set went to
    std::array<float, numControls> valuesSent {};

    // The automatic round trip's check, while at 1 block: counted by the audio thread at the round trip it was started with
    std::atomic<bool> autoCheckActive { false };
    std::atomic<int> autoCheckRoundTrip { 0 };
    std::atomic<juce::uint64> autoCheckMisses { 0 }, autoCheckSamples { 0 };

    // Probe and lag measurement: the audio thread's while running, then the message thread's while ready
    std::atomic<int> testStep { testIdle }, testKind { probeTest };
    juce::int64 testStart = 0, muteUntil = 0;
    std::vector<float> probeEnvelope;
    int probeSettle = 0, probeBefore = 0, probeAfter = 0;
    juce::int64 probeStepAt = -1;
    const std::vector<float> lagBurstSamples = makeLagBurst();
    std::vector<float> lagCapture; // the return, mono
    juce::int64 lagCaptured = 0; // samples of it that arrived

    // Audio thread to message thread
    std::atomic<juce::uint64> missing { 0 }, receiverMisses { 0 }, replaced { 0 }, oversized { 0 }, valueSets { 0 };
    std::atomic<int> headroomNow { unset }, headroomLowest { unset }, blockMin { unset }, blockMax { unset };
    std::atomic<float> differenceNow { -1.0f }, differenceLargest { -1.0f };

    // Declared last, so it stops before anything it uses goes
    juce::TimedCallback hubCheck { [this] { maintain(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BayLink)
};
