#pragma once

#include "BinaryData.h"
#include "KeyForwarder.h"
#include "PluginProcessor.h"
#include "PluginSettings.h"
#include "WebUi.h"
#include "WebViewKeepAlive.h"
#include "melatonin_inspector/melatonin_inspector.h"

class HostedEditorWindow;
struct SlotRow;

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void openHostedEditor (SlotRow&);

    Engine& engine;
    juce::SharedResourcePointer<PluginSettings> settings;
    juce::SharedResourcePointer<WebViewKeepAlive> webViewKeepAlive;
    std::unique_ptr<melatonin::Inspector> inspector;
    std::vector<std::unique_ptr<SlotRow>> slotRows;
    juce::ToggleButton keepWarmToggle { "Keep web UI warm (reopens instantly, uses more memory)" };
    juce::TextButton inspectButton { "Inspect the UI" };
    KeyForwarder keyForwarder;
    std::unique_ptr<WebUi> webUi;
    juce::Label webUiUnavailable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
