#pragma once

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "melatonin_inspector/melatonin_inspector.h"

class HostedEditorWindow;
struct SlotRow;

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
    void showControlsOf (const HostedPlugin*);
    void openHostedEditor (SlotRow&);

    Engine& engine;
    std::unique_ptr<melatonin::Inspector> inspector;
    std::vector<std::unique_ptr<SlotRow>> slotRows;
    juce::ComboBox controlsSelector;
    juce::TextButton inspectButton { "Inspect the UI" };
    juce::ListBox parameterList;
    const HostedPlugin* shownPlugin = nullptr;
    std::vector<float> lastParameterValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
