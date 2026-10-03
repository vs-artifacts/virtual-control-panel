#include <catch2/catch_test_macros.hpp>
#include <relay/RelayEditor.h>
#include <relay/RelayProcessor.h>

namespace
{
    constexpr int relayBlockSize = 512;
}

TEST_CASE ("The relay test build", "[relay]")
{
    RelayProcessor relay;
    REQUIRE (relay.getBusCount (true) == 1);
    REQUIRE (relay.getBusCount (false) == 1 + RelayProcessor::numControlOutputs);
    CHECK (RelayProcessor::numControlOutputs == 16);
    CHECK (relay.getTotalNumOutputChannels() == 2 + RelayProcessor::numControlOutputs); // mono, and all start active
    CHECK (relay.getBus (false, 1)->getName() == "Control 1");
    CHECK_FALSE (relay.isBeingProcessed());
    CHECK (relay.getMode() == RelayLink::Mode::receiver);

    relay.prepareToPlay (48000.0, relayBlockSize);
    juce::AudioBuffer<float> buffer (relay.getTotalNumOutputChannels(), relayBlockSize);
    juce::MidiBuffer midi;

    SECTION ("without a panel it reads the level arriving on its input, and every output stays silent, in both modes")
    {
        for (const auto mode : { RelayLink::Mode::receiver, RelayLink::Mode::transmitter })
        {
            relay.setMode (mode);
            relay.maintainLink();
            CHECK (relay.getLink().getStatus().state == RelayLink::State::waitingForPanel);

            buffer.clear();
            juce::FloatVectorOperations::fill (buffer.getWritePointer (0), 0.25f, relayBlockSize);
            juce::FloatVectorOperations::fill (buffer.getWritePointer (1), -0.5f, relayBlockSize);
            relay.processBlock (buffer, midi);

            CHECK (juce::exactlyEqual (relay.getInputPeak(), 0.5f));
            CHECK (relay.isBeingProcessed());

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                CHECK (juce::exactlyEqual (buffer.getMagnitude (channel, 0, relayBlockSize), 0.0f));
        }
    }

    SECTION ("it saves and restores its mode and control values, and ignores anything else")
    {
        relay.setMode (RelayLink::Mode::transmitter);
        relay.getLink().setControl (0, 0.3f);
        relay.getLink().setControl (15, 1.0f);
        juce::MemoryBlock saved;
        relay.getStateInformation (saved);

        RelayProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (restored.getMode() == RelayLink::Mode::transmitter);
        CHECK (juce::exactlyEqual (restored.getLink().getControl (0), 0.3f));
        CHECK (juce::exactlyEqual (restored.getLink().getControl (15), 1.0f));
        CHECK (juce::exactlyEqual (restored.getLink().getControl (1), 0.0f));

        const auto unknown = [&restored] (const juce::String& xml) {
            juce::MemoryBlock data;
            juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (xml), data);
            restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
        };

        restored.setStateInformation ("garbage", 7);
        unknown (R"(<HeartseekerRelayState mode="sideways" control1="nonsense" control2="7"/>)");
        unknown (R"(<SomethingElse mode="receiver" control1="0.5"/>)");
        CHECK (restored.getMode() == RelayLink::Mode::transmitter);
        CHECK (juce::exactlyEqual (restored.getLink().getControl (0), 0.0f)); // not a number: read as 0
        CHECK (juce::exactlyEqual (restored.getLink().getControl (1), 1.0f)); // out of range: limited

        unknown (R"(<HeartseekerRelayState mode="receiver"/>)");
        CHECK (restored.getMode() == RelayLink::Mode::receiver);
    }

    SECTION ("a receiver plays its control values as steady levels, even without a panel")
    {
        relay.getLink().setControl (2, 0.4f);
        buffer.clear();
        relay.processBlock (buffer, midi);

        for (int i = 0; i < relayBlockSize; ++i)
            CHECK (juce::exactlyEqual (buffer.getSample (2 + 2, i), 0.4f));

        CHECK (juce::exactlyEqual (buffer.getMagnitude (2 + 3, 0, relayBlockSize), 0.0f));
    }

    SECTION ("its window shows the mode and the link")
    {
        relay.setMode (RelayLink::Mode::transmitter);
        std::unique_ptr<juce::AudioProcessorEditor> editor (relay.createEditorAndMakeActive());
        REQUIRE (editor != nullptr);

        auto lines = dynamic_cast<RelayEditor&> (*editor).getStatusLines();
        CHECK (lines[0] == "Not linked: waiting for a panel");
        CHECK (lines[1] == "Round trip: -");
        CHECK (lines[2] == "Missed blocks: 0");
        CHECK (lines[7] == "Control outputs: silent (receivers only)");
        relay.editorBeingDeleted (editor.get());
        editor.reset();

        relay.setMode (RelayLink::Mode::receiver);
        relay.getLink().setControl (0, 0.5f);
        editor.reset (relay.createEditorAndMakeActive());
        lines = dynamic_cast<RelayEditor&> (*editor).getStatusLines();
        CHECK (lines[7] == "Control outputs: holding the last values");
        CHECK (lines[8].startsWith (" 1: 0.5000000   2: 0.0000000"));
        relay.editorBeingDeleted (editor.get());
    }

    relay.releaseResources();
}
