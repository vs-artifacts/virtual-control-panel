#pragma once

#include "RelayProcessor.h"

// A small native window (no web view, to keep relays light): status and the input level
class RelayEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit RelayEditor (RelayProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    RelayProcessor& relay;
    juce::Label title, audioStatus, inputLevel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelayEditor)
};
