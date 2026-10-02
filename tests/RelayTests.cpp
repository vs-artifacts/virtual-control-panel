#include <catch2/catch_test_macros.hpp>
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

    relay.prepareToPlay (48000.0, relayBlockSize);
    juce::AudioBuffer<float> buffer (relay.getTotalNumOutputChannels(), relayBlockSize);
    juce::MidiBuffer midi;

    SECTION ("reads the level arriving on its input, and every output stays silent")
    {
        buffer.clear();
        juce::FloatVectorOperations::fill (buffer.getWritePointer (0), 0.25f, relayBlockSize);
        juce::FloatVectorOperations::fill (buffer.getWritePointer (1), -0.5f, relayBlockSize);
        relay.processBlock (buffer, midi);

        CHECK (juce::exactlyEqual (relay.getInputPeak(), 0.5f));
        CHECK (relay.isBeingProcessed());
        CHECK (juce::exactlyEqual (buffer.getMagnitude (0, relayBlockSize), 0.0f));
    }

    SECTION ("its window opens")
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (relay.createEditorAndMakeActive());
        CHECK (editor != nullptr);
    }

    relay.releaseResources();
}
