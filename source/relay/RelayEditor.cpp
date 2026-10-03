#include "RelayEditor.h"

namespace
{
    using Mode = RelayLink::Mode;

    juce::String describeMiss (RelayLink::Miss miss)
    {
        switch (miss)
        {
            case RelayLink::Miss::late:
                return "the panel's audio arrived late";
            case RelayLink::Miss::gone:
                return "the panel's audio was gone";
            case RelayLink::Miss::receiverIdle:
                return "the receiver hadn't played";
            case RelayLink::Miss::receiverHadNothing:
                return "the receiver had no audio";
            case RelayLink::Miss::receiverRanTwice:
                return "the receiver played twice";
            case RelayLink::Miss::blockSize:
                return "the block sizes differed";
            case RelayLink::Miss::caughtUp:
                return "skipped ahead to the panel's newest block";
            case RelayLink::Miss::none:
                break;
        }

        return {};
    }

    juce::String describeLink (const RelayLink::Status& status)
    {
        const auto receiver = status.mode == Mode::receiver;

        switch (status.state)
        {
            case RelayLink::State::waitingForPanel:
                return "Not linked: waiting for a panel";
            case RelayLink::State::roleTaken:
                return juce::String ("Not linked: every panel already has a ") + (receiver ? "receiver" : "transmitter");
            case RelayLink::State::linked:
                break;
        }

        auto text = "Linked to " + status.linkName;

        if (status.sampleRateDiffers)
            return text + ", but its sample rate differs";

        if (receiver)
            return text + (status.otherSideRunning ? ", panel running" : ", panel not running");

        return text + (status.otherSideRunning ? ", receiver playing" : ", receiver not playing");
    }

    juce::String describeRange (std::optional<int> low, std::optional<int> high)
    {
        if (!low.has_value() || !high.has_value())
            return "-";

        return *low == *high ? juce::String (*low) : juce::String (*low) + " to " + juce::String (*high);
    }
}

RelayEditor::RelayEditor (RelayProcessor& p)
    : AudioProcessorEditor (&p),
      relay (p)
{
    title.setText (relay.getName() + " (test build)", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));

    for (auto* button : { &receiverButton, &transmitterButton })
    {
        button->setRadioGroupId (1);
        button->setClickingTogglesState (true);
        addAndMakeVisible (button);
    }

    receiverButton.onClick = [this] {
        if (receiverButton.getToggleState())
            relay.setMode (Mode::receiver);
    };

    transmitterButton.onClick = [this] {
        if (transmitterButton.getToggleState())
            relay.setMode (Mode::transmitter);
    };

    resetButton.onClick = [this] {
        relay.getLink().resetCounters();
        timerCallback();
    };

    addAndMakeVisible (resetButton);

    for (auto* label : { &title, &audioStatus, &inputLevel, &linkStatus, &roundTrip, &missed, &headroom, &blocks, &controlsStatus, &controlValues })
        addAndMakeVisible (label);

    controlValues.setJustificationType (juce::Justification::topLeft);
    controlValues.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));

    timerCallback();
    startTimerHz (4);
    setSize (420, 440);
}

void RelayEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void RelayEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    title.setBounds (area.removeFromTop (28));

    auto modes = area.removeFromTop (48);
    receiverButton.setBounds (modes.removeFromTop (24));
    transmitterButton.setBounds (modes);

    for (auto* label : { &linkStatus, &roundTrip, &missed, &headroom, &blocks, &audioStatus, &inputLevel, &controlsStatus })
        label->setBounds (area.removeFromTop (26));

    controlValues.setBounds (area.removeFromTop (80));
    resetButton.setBounds (area.removeFromTop (30).removeFromLeft (130).reduced (0, 3));
}

juce::StringArray RelayEditor::getStatusLines() const
{
    juce::StringArray lines;

    for (const auto* label : { &linkStatus, &roundTrip, &missed, &headroom, &blocks, &audioStatus, &inputLevel, &controlsStatus, &controlValues })
        lines.add (label->getText());

    return lines;
}

void RelayEditor::timerCallback()
{
    const auto status = relay.getLink().getStatus();
    const auto receiver = status.mode == Mode::receiver;

    (receiver ? receiverButton : transmitterButton).setToggleState (true, juce::dontSendNotification);
    linkStatus.setText (describeLink (status), juce::dontSendNotification);

    const auto linked = status.state == RelayLink::State::linked && status.sampleRate > 0.0;
    roundTrip.setText ("Round trip: " + (linked ? juce::String (status.roundTrip) + " samples (" + juce::String (1000.0 * status.roundTrip / status.sampleRate, 1) + " ms), set by the panel" : juce::String ("-")),
        juce::dontSendNotification);

    const auto lastMiss = describeMiss (status.lastMiss);
    missed.setText ("Missed blocks: " + juce::String (status.missed) + (lastMiss.isEmpty() ? juce::String() : " (last: " + lastMiss + ")"), juce::dontSendNotification);

    if (receiver)
        headroom.setText ("Headroom: now " + (status.headroomNow.has_value() ? juce::String (*status.headroomNow) : juce::String ("-")) + ", lowest " + (status.headroomLowest.has_value() ? juce::String (*status.headroomLowest) : juce::String ("-")) + " samples",
            juce::dontSendNotification);
    else
        headroom.setText ("Headroom: (receivers only)", juce::dontSendNotification);

    blocks.setText ("Block sizes: " + describeRange (status.blockMin, status.blockMax) + " samples", juce::dontSendNotification);
    audioStatus.setText (relay.isBeingProcessed() ? "Audio: running" : "Audio: not running (stopped by the host)", juce::dontSendNotification);
    inputLevel.setText ("Input peak: " + juce::String (relay.getInputPeak(), 7), juce::dontSendNotification);

    if (!receiver)
    {
        controlsStatus.setText ("Control outputs: silent (receivers only)", juce::dontSendNotification);
        controlValues.setText ({}, juce::dontSendNotification);
        return;
    }

    controlsStatus.setText (status.controlsLive ? "Control outputs: from the panel" : "Control outputs: holding the last values", juce::dontSendNotification);

    // Exact values, four to a line
    juce::String values;

    for (int i = 0; i < RelayLink::numControls; ++i)
    {
        values << juce::String (i + 1).paddedLeft (' ', 2) << ": " << juce::String (status.controls[static_cast<size_t> (i)], 7);
        values << (i % 4 == 3 ? "\n" : "  ");
    }

    controlValues.setText (values.trimEnd(), juce::dontSendNotification);
}
