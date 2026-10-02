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
    CHECK (relay.getTotalNumOutputChannels() == 2 * (1 + RelayProcessor::numControlOutputs)); // all start active
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

    SECTION ("it saves and restores its mode, and ignores anything else")
    {
        relay.setMode (RelayLink::Mode::transmitter);
        juce::MemoryBlock saved;
        relay.getStateInformation (saved);

        RelayProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (restored.getMode() == RelayLink::Mode::transmitter);

        const auto unknown = [&restored] (const juce::String& xml) {
            juce::MemoryBlock data;
            juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (xml), data);
            restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
        };

        restored.setStateInformation ("garbage", 7);
        unknown (R"(<HeartseekerRelayState mode="sideways"/>)");
        unknown (R"(<SomethingElse mode="receiver"/>)");
        CHECK (restored.getMode() == RelayLink::Mode::transmitter);

        unknown (R"(<HeartseekerRelayState mode="receiver"/>)");
        CHECK (restored.getMode() == RelayLink::Mode::receiver);
    }

    SECTION ("its window shows the mode and the link")
    {
        relay.setMode (RelayLink::Mode::transmitter);
        std::unique_ptr<juce::AudioProcessorEditor> editor (relay.createEditorAndMakeActive());
        REQUIRE (editor != nullptr);

        const auto lines = dynamic_cast<RelayEditor&> (*editor).getStatusLines();
        CHECK (lines[0] == "Not linked: waiting for a panel");
        CHECK (lines[1] == "Round trip: -");
        CHECK (lines[2] == "Missed blocks: 0");
        relay.editorBeingDeleted (editor.get());
    }

    relay.releaseResources();
}
