#pragma once

#include "RelayProcessor.h"

// A small native window (no web view, to keep relays light): the mode, and the link's status and diagnostics
class RelayEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit RelayEditor (RelayProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Every status line as shown, top to bottom
    juce::StringArray getStatusLines() const;

private:
    void timerCallback() override;

    RelayProcessor& relay;
    juce::Label title, audioStatus, inputLevel, linkStatus, roundTrip, missed, headroom, blocks, marker, controlsStatus, controlValues, panels;
    juce::ToggleButton receiverButton { "Receiver (before the FL plugin)" };
    juce::ToggleButton transmitterButton { "Transmitter (after the FL plugin)" };
    juce::TextButton resetButton { "Reset counters" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelayEditor)
};
