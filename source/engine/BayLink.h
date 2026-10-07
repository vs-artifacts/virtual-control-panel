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
// plugin, and plays the return (or, bypassed, the sent audio) a fixed delay later: the round trip plus the bay plugin's
// own latency, which the transmitter reads from the position marker. The shell reports that delay to the host as
// latency. It never waits for a relay.
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

    // The bay's bypass switch
    enum class BypassMode {
        off, // the return plays
        synced, // the sent audio plays, at the same delay as the return
        dropLatency, // the sent audio plays at once: the bay's delay is dropped from the latency the host is told
    };

    // What a bay bypassed with its latency dropped is sent
    enum class DropSends {
        silence, // FL may switch the bay's plugin off (smart disable)
        audio, // the panel's sound: the bay keeps processing, its tails carry on
    };

    // The transmitter's latest report of the bay plugin's latency
    enum class Reading {
        none, // no marker arriving: the return is stamped by the receiver's position, so the latency counts as 0
        measured,
        unreadable, // the return is held back
        outOfRange, // below 0 or above HS_LINK_MAX_LATENCY: the return is held back
        noInput, // the transmitter's Marker input is off: counts as 0
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
        BypassMode bypass = BypassMode::off;
        DropSends dropSends = DropSends::silence;
        int delay = 0; // samples the output plays behind the engine: round trip + bay latency, or 0 with latency dropped
        int bayLatency = 0; // the one in use
        Reading reading = Reading::none;
        int readingSamples = 0;
        std::optional<int> readingLowest, readingHighest; // measured readings since the counters were reset
        std::optional<int> settlingTo; // a new bay latency waiting to hold for the settle time
        int settledMs = 0; // how long it has held so far
        int settleMs = 0;
        juce::uint64 heldBack = 0; // blocks held back while a new bay latency settled
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
    bool updateRoundTrip(); // runs the automatic round trip; true if the round trip changed. A timer calls it.
    bool updateBayLatency(); // takes a new bay latency once it has held for the settle time; true if it did. A timer calls it.
    int getBayLatency() const noexcept { return bayLatency.load(); }
    int getDelaySamples() const noexcept; // what the output plays behind the engine, for the host's latency
    void setBypass (BypassMode mode) noexcept { bypass.store (mode); }
    BypassMode getBypass() const noexcept { return bypass.load(); }
    void setDropSends (DropSends value) noexcept { dropSends.store (value); }
    DropSends getDropSends() const noexcept { return dropSends.load(); }
    void setSettleMs (int milliseconds) noexcept { settleMs.store (juce::jlimit (0, maxSettleMs, milliseconds)); }
    int getSettleMs() const noexcept { return settleMs.load(); }
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

    // How long a new bay latency must hold before it's used
    static constexpr int maxSettleMs = 1000;
    static constexpr int defaultSettleMs = 50;

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
    static const char* toName (BypassMode) noexcept;
    static std::optional<BypassMode> bypassFromName (const juce::String&);
    static const char* toName (DropSends) noexcept;
    static std::optional<DropSends> dropSendsFromName (const juce::String&);
    static const char* toName (Reading) noexcept;

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
    void noteLatencyReport (const hs_link_info&, int numSamples);
    void mix (float* left, float* right, int numSamples, bool returned, bool bypassed);
    void blendJump (const hs_link_api*, int32_t back, juce::int64 start, juce::int64 linkStart, int numSamples, float* left, float* right);
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
    std::atomic<BypassMode> bypass { BypassMode::off };
    std::atomic<DropSends> dropSends { DropSends::silence };
    std::atomic<Dropouts> dropouts { Dropouts::safe };
    std::atomic<int> crossfadeMs { defaultCrossfadeMs };
    std::atomic<int> settleMs { defaultSettleMs };
    std::atomic<int> bayLatency { 0 }; // the one in use; set on the message thread

    // The audio thread's settling bay latency, for the message thread: the value in the upper half (-1: none), the
    // samples it has held in the lower
    static constexpr juce::uint64 packCandidate (int value, juce::int64 samples) noexcept
    {
        return (static_cast<juce::uint64> (static_cast<juce::uint32> (value)) << 32) | static_cast<juce::uint32> (std::min<juce::int64> (samples, 0x7fffffff));
    }

    std::atomic<juce::uint64> latencyCandidate { packCandidate (-1, 0) };

    // The link's timeline never goes below this: past anything an earlier panel left in the channels
    std::atomic<juce::int64> minimumLinkPosition { juce::int64 (1) << 20 };

    // Audio thread. The history counts every sample played; the link's timeline is that plus an offset, which only
    // ever grows, so the delayed direct sound never jumps when the link does.
    juce::int64 position = 0, linkOffset = 0;
    juce::uint64 lastReturnWrites = 0;
    juce::int64 returnStall = 0; // samples since the transmitter last wrote
    juce::int64 flowingRun = 0; // samples since the return started flowing
    int lastRoundTrip = -1;
    int currentLinkDelay = 0; // the return is read this far behind: round trip + bay latency
    juce::int64 roundTripRun = 0; // samples since the round trip (or the block size) last changed
    int32_t receiverCountedOn = -1; // the send handle receiverFailures counts for
    juce::uint64 receiverFailures = 0;
    float returnMix = 0.0f; // how much of the output is the return rather than the sent audio
    float outputGain = 1.0f;
    History history;
    std::vector<float> returnLeft, returnRight, directLeft, directRight;
    int32_t valuesSentTo = -1; // the send handle the last value set went to
    std::array<float, numControls> valuesSent {};

    // Audio thread: the transmitter's latency reports, and return audio that isn't played
    juce::uint64 lastLatencyReports = 0;
    int candidate = -1; // the latest valid reading
    juce::int64 candidateRun = 0; // samples it has been reported unchanged
    juce::int64 receiverStampedEnd = 0; // where the return ended when the transmitter last stamped by the receiver
    juce::int64 silentStart = 0, silentEnd = 0; // the latest stretch sent as silence (link positions)

    // Audio thread: a change of the output's delay blends the old timeline into the new
    int outputDelay = -1; // -1: none yet
    int jumpFromDelay = 0;
    float jumpFromMix = 0.0f; // the old timeline's share of return, held through the blend
    float jumpGain = 0.0f; // the old timeline's share of the output, falling to 0
    std::vector<float> jumpLeft, jumpRight, jumpReturnLeft, jumpReturnRight;

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
    std::atomic<juce::uint64> missing { 0 }, receiverMisses { 0 }, replaced { 0 }, oversized { 0 }, valueSets { 0 }, heldBack { 0 };
    std::atomic<int> headroomNow { unset }, headroomLowest { unset }, blockMin { unset }, blockMax { unset };
    std::atomic<float> differenceNow { -1.0f }, differenceLargest { -1.0f };
    std::atomic<Reading> reading { Reading::none };
    std::atomic<int> readingSamples { 0 }, readingLowest { unset }, readingHighest { unset };

    // Declared last, so it stops before anything it uses goes
    juce::TimedCallback hubCheck { [this] { maintain(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BayLink)
};
