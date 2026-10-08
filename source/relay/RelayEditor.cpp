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

    juce::String describeMarker (const RelayLink::Status& status)
    {
        using State = RelayLink::MarkerState;

        if (status.mode == Mode::receiver)
        {
            switch (status.marker)
            {
                case State::noPin:
                    return "Marker output: switched off";
                case State::flowing:
                    return "Marker output: sending positions";
                case State::silent:
                case State::unreadable:
                    return "Marker output: silent (nothing played)";
                case State::unknown:
                    break;
            }

            return "Marker output: -";
        }

        const auto unreadable = ", unreadable blocks " + juce::String (status.markerUnreadable);
        const auto stamp = status.stamp == RelayLink::Stamp::receiver ? "; return stamped by the receiver's position" : "";

        switch (status.marker)
        {
            case State::noPin:
                return juce::String ("Marker: the Marker input is switched off") + stamp;
            case State::silent:
                if (status.markerHeld)
                    return "Marker: nothing arriving, return held back for up to " + juce::String (RelayLink::markerHoldSamples) + " samples" + unreadable;

                return "Marker: nothing arriving" + unreadable + stamp;
            case State::unreadable:
                return "Marker: unreadable, return held back" + unreadable;
            case State::flowing:
                break;
            case State::unknown:
                return "Marker: -";
        }

        auto text = juce::String ("Marker: bay latency ");

        if (status.bayLatencyNow.has_value() && status.sampleRate > 0.0)
        {
            text << *status.bayLatencyNow << " samples (" << juce::String (1000.0 * *status.bayLatencyNow / status.sampleRate, 1) << " ms)";

            if (*status.bayLatencyNow < 0 || *status.bayLatencyNow > HS_LINK_MAX_LATENCY)
                text << ", out of range (0 to " << HS_LINK_MAX_LATENCY << "), return held back";
        }
        else
        {
            text << "- (no receiver playing)";
        }

        return text + ", range " + describeRange (status.bayLatencyLowest, status.bayLatencyHighest) + unreadable;
    }

    juce::String describePanels (const std::vector<RelayLink::PanelView>& panels, int linkedPanel)
    {
        if (panels.empty())
            return "Panels in the hub: none yet";

        const auto role = [] (bool present, bool running) { return present ? (running ? "running" : "stopped") : "none"; };
        juce::String text ("Panels in the hub (* this relay's):");

        for (const auto& view : panels)
            text << "\n"
                 << (view.number == linkedPanel ? "* " : "  ") << "Panel " << view.number << ": panel " << role (view.panel, view.panelRunning)
                 << ", receiver " << role (view.receiver, view.receiverRunning) << ", transmitter " << role (view.transmitter, view.transmitterRunning);

        return text;
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

    for (auto* label : { &title, &audioStatus, &prepared, &inputLevel, &linkStatus, &roundTrip, &missed, &headroom, &blocks, &marker, &controlsStatus, &controlValues, &panels })
        addAndMakeVisible (label);

    controlValues.setJustificationType (juce::Justification::topLeft);
    controlValues.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    panels.setJustificationType (juce::Justification::topLeft);
    panels.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));

    timerCallback();
    startTimerHz (4);
    setSize (680, 666);
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

    for (auto* label : { &linkStatus, &roundTrip, &missed, &headroom, &blocks, &marker, &audioStatus, &prepared, &inputLevel, &controlsStatus })
        label->setBounds (area.removeFromTop (26));

    controlValues.setBounds (area.removeFromTop (80));
    resetButton.setBounds (area.removeFromTop (30).removeFromLeft (130).reduced (0, 3));
    panels.setBounds (area.removeFromTop (160));
}

juce::StringArray RelayEditor::getStatusLines() const
{
    juce::StringArray lines;

    for (const auto* label : { &linkStatus, &roundTrip, &missed, &headroom, &blocks, &marker, &audioStatus, &prepared, &inputLevel, &controlsStatus, &controlValues, &panels })
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
    marker.setText (describeMarker (status), juce::dontSendNotification);
    panels.setText (describePanels (relay.getLink().getPanels(), relay.getLink().getLinkedPanel()), juce::dontSendNotification);
    audioStatus.setText (relay.isBeingProcessed() ? "Audio: running" : "Audio: not running (stopped by the host)", juce::dontSendNotification);
    prepared.setText ("Prepared by the host: " + juce::String (status.prepares) + " times"
                          + (receiver ? juce::String() : "; fallbacks to the receiver's position: " + juce::String (status.fallbacks)) + " (since reset)",
        juce::dontSendNotification);
    inputLevel.setText ("Input peak: " + juce::String (relay.getInputPeak(), 7), juce::dontSendNotification);

    if (!receiver)
    {
        controlsStatus.setText ("Control outputs and tail cut: silent (receivers only)", juce::dontSendNotification);
        controlValues.setText ({}, juce::dontSendNotification);
        return;
    }

    controlsStatus.setText (juce::String (status.controlsLive ? "Control outputs: from the panel" : "Control outputs: holding the last values") + "; tail cut " + juce::String (status.tailCut, 7),
        juce::dontSendNotification);

    // Exact values, four to a line
    juce::String values;

    for (int i = 0; i < RelayLink::numControls; ++i)
    {
        values << juce::String (i + 1).paddedLeft (' ', 2) << ": " << juce::String (status.controls[static_cast<size_t> (i)], 7);
        values << (i % 4 == 3 ? "\n" : "  ");
    }

    controlValues.setText (values.trimEnd(), juce::dontSendNotification);
}
