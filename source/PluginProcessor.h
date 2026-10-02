#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditorScale.h"
#include "HostControls.h"
#include "WebViewKeepAlive.h"
#include "bridge/ControlSession.h"
#include "engine/Engine.h"

#if (MSVC)
    #include "ipps.h"
#endif

// Stand-ins until plugin scanning and routing configuration exist
inline constexpr auto serum2Path = R"(C:\Program Files\Common Files\VST3\Serum2.vst3)";
inline constexpr auto sylenth1Path = R"(C:\Program Files\Common Files\VST3\Sylenth1.vst3)";
inline constexpr auto eqPath = R"(C:\Program Files\Common Files\VST3\Kilohearts\kHs 3-Band EQ.vst3)";

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

    // Every exposed parameter, for a page to control through its host control
    std::vector<ControlSession::Parameter> createSessionParameters();

    // Whether this instance's editor has had its first-open placement; saved with the instance
    bool isEditorPlaced() const noexcept { return editorPlaced; }
    void setEditorPlaced() noexcept { editorPlaced = true; }

    // The editor's last tab and size (see EditorScale), saved with the instance; no size until it has had one
    static constexpr int numEditorTabs = 3;
    int getEditorTab() const noexcept { return editorTab; }
    void setEditorTab (int tab) noexcept { editorTab = tab; }
    std::optional<double> getEditorScale() const noexcept { return editorScale; }
    void setEditorScale (double scale) noexcept { editorScale = scale; }

    // Whether the host has run our audio in the last half second; false while it doesn't (e.g. FL's smart disable)
    bool isBeingProcessed() const noexcept;

private:
    Engine engine;
    HostControls hostControls { *this };
    bool editorPlaced = false;
    int editorTab = 0;
    std::optional<double> editorScale;

    std::atomic<juce::uint32> lastBlockTime { 0 }; // Time::getMillisecondCounter()

    // Keeps the editors' hidden web view (once an editor starts it) for as long as any instance exists
    juce::SharedResourcePointer<WebViewKeepAlive> webViewKeepAlive;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
