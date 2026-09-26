#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "melatonin_inspector/melatonin_inspector.h"

class HostedEditorWindow;

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::ListBoxModel,
                     private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void timerCallback() override;
    void openHostedEditor();

    HostedPlugin& hostedPlugin;
    std::unique_ptr<melatonin::Inspector> inspector;
    juce::Label statusLabel;
    juce::TextButton openButton { "Open plugin window" };
    juce::TextButton inspectButton { "Inspect the UI" };
    juce::ListBox parameterList;
    std::vector<float> lastParameterValues;
    std::unique_ptr<HostedEditorWindow> hostedEditorWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
