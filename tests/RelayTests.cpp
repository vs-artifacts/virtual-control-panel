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
    REQUIRE (relay.getBusCount (true) == 2);
    REQUIRE (relay.getBusCount (false) == 1 + RelayProcessor::numControlOutputs + 1 + 1);
    CHECK (RelayProcessor::numControlOutputs == 16);
    CHECK (relay.getTotalNumOutputChannels() == 2 + RelayProcessor::numControlOutputs + 2 + 1); // controls and tail cut mono, and all start active
    CHECK (relay.getTotalNumInputChannels() == 4);
    CHECK (relay.getBus (false, 1)->getName() == "Control 1");
    CHECK (relay.getBus (false, RelayProcessor::markerOutputBus)->getName() == "Marker");
    CHECK (relay.getBus (false, RelayProcessor::tailCutOutputBus)->getName() == "Tail cut");
    CHECK (relay.getBus (true, RelayProcessor::markerInputBus)->getName() == "Marker");
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

    SECTION ("it saves and restores its mode, control values and tail cut, and ignores anything else")
    {
        relay.setMode (RelayLink::Mode::transmitter);
        relay.getLink().setControl (0, 0.3f);
        relay.getLink().setControl (15, 1.0f);
        relay.getLink().setTailCut (1.0f);
        juce::MemoryBlock saved;
        relay.getStateInformation (saved);

        RelayProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (restored.getMode() == RelayLink::Mode::transmitter);
        CHECK (juce::exactlyEqual (restored.getLink().getControl (0), 0.3f));
        CHECK (juce::exactlyEqual (restored.getLink().getControl (15), 1.0f));
        CHECK (juce::exactlyEqual (restored.getLink().getControl (1), 0.0f));
        CHECK (juce::exactlyEqual (restored.getLink().getTailCut(), 1.0f));

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

    SECTION ("a receiver plays its control values and tail cut as steady levels, even without a panel")
    {
        relay.getLink().setControl (2, 0.4f);
        relay.getLink().setTailCut (1.0f);
        buffer.clear();
        relay.processBlock (buffer, midi);
        const auto tailCut = relay.getChannelIndexInProcessBlockBuffer (false, RelayProcessor::tailCutOutputBus, 0);

        for (int i = 0; i < relayBlockSize; ++i)
        {
            CHECK (juce::exactlyEqual (buffer.getSample (2 + 2, i), 0.4f));
            CHECK (juce::exactlyEqual (buffer.getSample (tailCut, i), 1.0f));
        }

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
        CHECK (lines[5] == "Marker: -");
        CHECK (lines[7] == "Prepared by the host: 1 times; fallbacks to the receiver's position: 0 (since reset)");
        CHECK (lines[9] == "Control outputs and tail cut: silent (receivers only)");
        relay.editorBeingDeleted (editor.get());
        editor.reset();

        relay.setMode (RelayLink::Mode::receiver);
        relay.getLink().setControl (0, 0.5f);
        editor.reset (relay.createEditorAndMakeActive());
        lines = dynamic_cast<RelayEditor&> (*editor).getStatusLines();
        CHECK (lines[9] == "Control outputs: holding the last values; tail cut 0.0000000");
        CHECK (lines[10].startsWith (" 1: 0.5000000   2: 0.0000000"));
        relay.editorBeingDeleted (editor.get());
    }

    SECTION ("a transmitter tells a marker from silence and from anything else on its Marker input, even without a panel")
    {
        relay.setMode (RelayLink::Mode::transmitter);
        const auto feed = [&] (auto fill) {
            buffer.clear();
            fill (buffer.getWritePointer (2), buffer.getWritePointer (3));
            relay.processBlock (buffer, midi);
            return relay.getLink().getStatus().marker;
        };

        CHECK (feed ([] (float*, float*) {}) == RelayLink::MarkerState::silent);
        CHECK (feed ([] (float* left, float* right) { RelayLink::writeMarker (left, right, 1000, relayBlockSize); }) == RelayLink::MarkerState::flowing);
        CHECK (feed ([] (float* left, float*) { left[0] = 0.1f; }) == RelayLink::MarkerState::unreadable);
        CHECK (relay.getLink().getStatus().markerUnreadable == 1);
        CHECK_FALSE (relay.getLink().getStatus().bayLatencyLowest.has_value()); // no receiver to compare with

        // Every channel is silent afterwards, the Marker input's included
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            CHECK (juce::exactlyEqual (buffer.getMagnitude (channel, 0, relayBlockSize), 0.0f));

        relay.getBus (true, RelayProcessor::markerInputBus)->enable (false);
        CHECK (feed ([] (float* left, float* right) { RelayLink::writeMarker (left, right, 1000, relayBlockSize); }) == RelayLink::MarkerState::noPin);
    }

    relay.releaseResources();
}

TEST_CASE ("The position marker carries every sample's position exactly, and nothing else reads as one", "[relay][marker]")
{
    constexpr int length = 300;
    std::array<float, length> left {}, right {};
    constexpr auto period = juce::int64 (1) << RelayLink::markerBits;

    for (const auto position : { juce::int64 (0), juce::int64 (123456789), period - 100, period * 5 + 7 })
    {
        RelayLink::writeMarker (left.data(), right.data(), position, length);
        CHECK (RelayLink::readMarker (left.data(), right.data(), length) == position % period);
        // Inaudible even if it ends up in audio: below 2^-18 (about -108 dB)
        CHECK (*std::max_element (left.begin(), left.end()) < 1.0f / 262144.0f);
        CHECK (*std::max_element (right.begin(), right.end()) < 1.0f / 262144.0f);
    }

    const auto changed = [&] (auto change) {
        RelayLink::writeMarker (left.data(), right.data(), 5000, length);
        change();
        return RelayLink::readMarker (left.data(), right.data(), length).has_value();
    };

    CHECK_FALSE (changed ([&] { std::swap (left[10], left[11]); })); // a gap
    CHECK_FALSE (changed ([&] { right[20] = 0.0f; })); // the complement doesn't match
    CHECK_FALSE (changed ([&] { left[30] += 1.0f / 2199023255552.0f; })); // half a step (2^-41): not a marker value
    CHECK_FALSE (changed ([&] { left[40] = -left[40]; }));
    CHECK_FALSE (changed ([&] { left[50] = std::numeric_limits<float>::infinity(); }));
    CHECK_FALSE (changed ([&] { left[60] = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK_FALSE (changed ([&] { juce::FloatVectorOperations::multiply (left.data(), 0.5f, length); juce::FloatVectorOperations::multiply (right.data(), 0.5f, length); }));
    CHECK_FALSE (changed ([&] { std::fill (left.begin(), left.end(), 0.0f); std::fill (right.begin(), right.end(), 0.0f); }));
    CHECK_FALSE (RelayLink::readMarker (left.data(), right.data(), 0).has_value());
}
