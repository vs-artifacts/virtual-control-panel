#include "RelayEditor.h"

RelayEditor::RelayEditor (RelayProcessor& p)
    : AudioProcessorEditor (&p),
      relay (p)
{
    title.setText (relay.getName() + " (test build)", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));

    for (auto* label : { &title, &audioStatus, &inputLevel })
        addAndMakeVisible (label);

    timerCallback();
    startTimerHz (10);
    setSize (320, 110);
}

void RelayEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void RelayEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    for (auto* label : { &title, &audioStatus, &inputLevel })
        label->setBounds (area.removeFromTop (30));
}

void RelayEditor::timerCallback()
{
    audioStatus.setText (relay.isBeingProcessed() ? "Audio: running" : "Audio: not running (stopped by the host)", juce::dontSendNotification);
    inputLevel.setText ("Input peak: " + juce::String (relay.getInputPeak(), 7), juce::dontSendNotification);
}
