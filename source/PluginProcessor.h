#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditorScale.h"
#include "HostControls.h"
#include "PluginSettings.h"
#include "RelayHubFinder.h"
#include "WebViewKeepAlive.h"
#include "bridge/ControlSession.h"
#include "engine/BayLink.h"
#include "engine/Engine.h"
#include "engine/TriggerParameter.h"

#if (MSVC)
    #include "ipps.h"
#endif

// The EQ's low band gain (named "Low"), the one parameter exposed until the user can choose
inline constexpr auto eqLowGainID = "0";

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    Engine& getEngine() noexcept { return engine; }
    HostControls& getHostControls() noexcept { return hostControls; }

    // The external module bay (link test): its slot state and control values are saved with the instance
    BayLink& getBayLink() noexcept { return bayLink; }

    // Takes the plugin-wide link settings (round trip, crossfade, settle time, tail cut release), runs the automatic
    // round trip, takes a settled bay latency, updates the link's event log, and reports the latency if it changed.
    // Also takes the command parameters' requests.
    void applyLinkSettings();

    // Commands the host can automate, after the pool, with permanent IDs: one for all bays, and one per bay (planned for
    // up to maxBays; bays 2 on do nothing yet)
    static constexpr int maxBays = 4;
    TriggerParameter& getRecheckBlockSize() noexcept { return *recheckBlockSize; }
    TriggerParameter& getRetryOneBlock (int bay) noexcept { return *retryOneBlock[static_cast<size_t> (bay)]; }

    // Every exposed parameter, for a page to control through its host control
    std::vector<ControlSession::Parameter> createSessionParameters();

    // The bay's controls are always host controls 2 to 17 (indexes from 1)
    static constexpr int firstBayControl = 1;

    // For the editor (message thread): a slot's plugin is about to go (close its window, drop its parameters), and the
    // structure changed
    std::function<void (HostedPlugin*)> onSlotRemoving;
    std::function<void()> onStructureChanged;

    // Takes the plugin-wide settings for structure changes (the dip's fades)
    void applyEngineSettings();

    // Whether this instance's editor has had its first-open placement; saved with the instance
    bool isEditorPlaced() const noexcept { return editorPlaced; }
    void setEditorPlaced() noexcept { editorPlaced = true; }

    // The editor's last tab and size (see EditorScale), saved with the instance; no size until it has had one
    static constexpr int numEditorTabs = 2;
    int getEditorTab() const noexcept { return editorTab; }
    void setEditorTab (int tab) noexcept { editorTab = tab; }
    std::optional<double> getEditorScale() const noexcept { return editorScale; }
    void setEditorScale (double scale) noexcept { editorScale = scale; }
    std::optional<juce::Point<double>> getEditorLayout() const noexcept { return editorLayout; }
    void setEditorLayout (juce::Point<double> layout) noexcept { editorLayout = layout; }

    // Whether the editor shows the Diagnostics panel (it widens the window); saved with the instance
    bool isDiagnosticsOpen() const noexcept { return diagnosticsOpen; }
    void setDiagnosticsOpen (bool open) noexcept { diagnosticsOpen = open; }

    // Where each of the editor's scrolling areas was left, in page pixels, by the page's names: the Panel tab (down, and
    // its generator and bus rows sideways), the Settings tab, and Diagnostics. Saved with the instance.
    static constexpr std::array<const char*, 5> editorScrollAreas {
        "panel",
        "panel.generators",
        "panel.buses",
        "settings",
        "diagnostics"
    };
    static constexpr int maxEditorScroll = 1000000;
    static std::optional<size_t> findEditorScrollArea (const juce::String& name);
    int getEditorScroll (size_t area) const noexcept { return editorScroll[area]; }
    void setEditorScroll (size_t area, int top) noexcept { editorScroll[area] = juce::jlimit (0, maxEditorScroll, top); }

    // The Panel tab's sections that can collapse to their bar; which are collapsed is saved with the instance
    static constexpr std::array<const char*, 3> editorSections { "header", "generators", "buses" };
    static std::optional<size_t> findEditorSection (const juce::String& name);
    bool isSectionCollapsed (size_t section) const noexcept { return collapsed[section]; }
    void setSectionCollapsed (size_t section, bool isCollapsed) noexcept { collapsed[section] = isCollapsed; }

    // Whether the host has run our audio in the last half second; false while it doesn't (e.g. FL's smart disable)
    bool isBeingProcessed() const noexcept;

private:
    void reportLatency();

    // "SLOT 1.1: kHs 3-Band EQ - Low" (until 1b.5d's dynamic names), "Param 1" when unbound
    void nameParam1();
    void bindExposedParameters();

    static constexpr auto bayControlsTag = "BayControls";
    static constexpr auto editorScrollTag = "EditorScroll";

    // Saved states of another version open as a new instance
    static constexpr int stateVersion = 1;

    Engine engine;
    HostControls hostControls { *this };
    TriggerParameter* recheckBlockSize = nullptr; // owned by the processor, like every parameter
    std::array<TriggerParameter*, maxBays> retryOneBlock {};
    bool editorPlaced = false;
    int editorTab = 0;
    std::optional<double> editorScale;
    std::optional<juce::Point<double>> editorLayout;
    bool diagnosticsOpen = false;
    std::array<int, editorScrollAreas.size()> editorScroll {};
    std::array<bool, editorSections.size()> collapsed {};

    std::atomic<juce::uint32> lastBlockTime { 0 }; // Time::getMillisecondCounter()

    // Keeps the editors' hidden web view (once an editor starts it) for as long as any instance exists
    juce::SharedResourcePointer<WebViewKeepAlive> webViewKeepAlive;

    juce::SharedResourcePointer<PluginSettings> settings;

    // The finder outlives the link, since it keeps the relay's file (where the hub lives) loaded
    RelayHubFinder relayHubFinder;
    BayLink bayLink { [this] { return relayHubFinder.find(); } };

    // Other instances' editors may change the plugin-wide settings at any time; declared last, so it stops first
    juce::TimedCallback linkSettingsCheck { [this] {
        applyLinkSettings();
        applyEngineSettings();
    } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
