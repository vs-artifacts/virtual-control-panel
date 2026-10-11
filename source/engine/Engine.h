#pragma once

#include "HostedPlugin.h"
#include "PluginCatalog.h"
#include "Structure.h"

// A hosted plugin's parameter chosen for control from our UI, and the host control it's bound to
struct ExposedParameter
{
    Structure::Id slot;
    juce::String parameterID;
    juce::AudioProcessorParameter* parameter;
    int hostControl;
};

// Runs every hosted plugin inside one routing graph, built from the panel's structure (see Structure). Each generator
// and slot is one permanent box in the graph with its plugin inside; each bus has a box for b.0 and b.X. Knows
// nothing about plugin vs standalone shells. MIDI in goes to every generator and straight to MIDI out.
class Engine : private juce::Timer
{
public:
    using Id = Structure::Id;

    explicit Engine (PluginCatalog = PluginCatalog::standard());
    ~Engine() override;

    // Message thread only: replaces everything at once (no dip), loading each slot's plugin
    void build (const Structure&);

    const Structure& getStructure() const noexcept { return structure; }
    const PluginCatalog& getCatalog() const noexcept { return catalog; }

    // A generator's or slot's plugin; nullptr for an empty slot or an unknown ID
    HostedPlugin* findPlugin (Id) const;

    // Every slot's plugin (empty slots have none) in display order: generators, then the buses' slots, the master's last
    std::vector<HostedPlugin*> getPlugins() const;

    // Structure changes (message thread). Each is checked against the structure as it stands and refused if it doesn't
    // fit; the structure changes at once. While audio runs, the output dips around a change of what's heard: the graph
    // follows once the output has faded out, then it fades back in. A new slot's plugin loads before the dip.
    std::optional<Id> addGenerator (const juce::String& pluginKey, int index);
    std::optional<Id> addSlot (Id bus, int index, const juce::String& pluginKey);
    bool removeSlot (Id); // a generator or a slot
    bool moveSlot (Id, Id bus, int index); // a generator ignores the bus
    std::optional<Id> addBus (Id parent);
    bool removeBus (Id);
    bool setGeneratorBus (Id generator, Id bus, bool feeds);

    // Message thread: before a slot's plugin goes, with its exposed parameters (already dropped from the list: unbind
    // them), and after any change of the structure
    std::function<void (Id, const std::vector<ExposedParameter>&)> onSlotRemoving;
    std::function<void()> onStructureChanged;

    // The dip around a structure change (ms; 0: at once)
    void setChangeFades (int outMs, int inMs) noexcept;

    // Whether the graph still waits for the dip to apply a change
    bool isChangeWaiting() const noexcept { return graphOutdated; }

    // Applies the waiting changes once the output has faded out (or isn't running); a timer calls it
    void applyWaitingChanges();

    void prepare (double sampleRate, int maxBlockSize, bool isNonRealtime);
    void release();
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead);

    int getLatencySamples() const;
    double getTailLengthSeconds() const;

    std::unique_ptr<juce::XmlElement> createStateXml() const;

    // False (and nothing changed) unless it's this version's saved state
    bool restoreStateFromXml (const juce::XmlElement& xml);

    // True if source's audio reaches destination (through any boxes in between)
    bool feeds (Id source, Id destination) const;

    // Generators only: fades the generator's output out (or back in) over muteFadeSeconds while the plugin keeps
    // running, so the load and timing stay the same. Any thread. False if it isn't a generator.
    bool setMuted (Id, bool muted);
    bool isMuted (Id) const;
    bool canMute (Id) const;
    static constexpr double muteFadeSeconds = 0.03;

    // People-facing name: "GEN 2", "SLOT 1.1", "SLOT X.2"; empty if unknown
    juce::String getSlotLabel (Id) const;

    // Message thread only. Binds a slot's control to a host control (replacing what was bound to that one). False if the
    // slot has no plugin or no such control.
    bool expose (Id slot, const juce::String& parameterID, int hostControl);
    void unexpose (int hostControl);

    const std::vector<ExposedParameter>& getExposedParameters() const noexcept { return exposed; }

    static constexpr auto stateTag = "Engine";
    static constexpr int stateVersion = 2;

    // Without a block for this long, audio counts as stopped: changes apply at once
    static constexpr juce::uint32 stoppedAfterMs = 200;

    // A dip that hasn't reached silence by its fade out plus this long applies anyway
    static constexpr juce::uint32 dipTimeoutMs = 250;

private:
    using NodeID = juce::AudioProcessorGraph::NodeID;
    class SlotBox;

    // A box: in the graph (owned there), or waiting to be added (owned here)
    struct Box
    {
        SlotBox* box = nullptr;
        NodeID node;
        std::unique_ptr<SlotBox> waiting;

        Box();
        Box (Box&&) noexcept;
        Box& operator= (Box&&) noexcept;
        ~Box();
    };

    struct BusPoints
    {
        Box entry, exit; // b.0 and b.X
    };

    Box createBox (const juce::String& pluginKey, bool generator);
    void retire (Box&);
    void releaseSlot (Id);
    void retireSlot (Id);
    void replaceStructure (const Structure&);
    void graphChanged (bool audible);
    void syncGraph();
    bool isAudioRunning() const;
    void timerCallback() override { applyWaitingChanges(); }

    void connectAudio (NodeID source, NodeID destination);
    void connectMidi (NodeID source, NodeID destination);

    PluginCatalog catalog;
    juce::AudioPluginFormatManager formatManager;
    juce::AudioProcessorGraph graph;
    NodeID midiIn, midiOut, audioOut;
    Structure structure;
    std::map<Id, Box> boxes; // generators and slots
    std::map<Id, BusPoints> busPoints; // the master's too
    std::vector<NodeID> retiring; // boxes leaving the graph at the next sync
    std::vector<ExposedParameter> exposed;

    // Message thread: the graph no longer matches the structure; waiting for the dip since dipStartedMs
    bool graphOutdated = false;
    juce::uint32 dipStartedMs = 0;

    // The dip: the message thread sets the target, the audio thread ramps to it and says when it's silent
    std::atomic<float> dipTarget { 1.0f };
    std::atomic<bool> dipSilent { false };
    std::atomic<int> dipOutMs { 0 }, dipInMs { 0 };
    float dipGain = 1.0f; // audio thread
    double sampleRate = 0.0;
    std::atomic<bool> prepared { false };
    std::atomic<juce::uint32> lastBlockMs { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
