#include <PluginProcessor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    constexpr int testBlockSize = 512;

    juce::AudioBuffer<float> makeSineBuffer()
    {
        juce::AudioBuffer<float> buffer (2, testBlockSize);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int i = 0; i < testBlockSize; ++i)
                buffer.setSample (channel, i, 0.5f * std::sin (0.05f * static_cast<float> (i)));

        return buffer;
    }

    bool buffersMatch (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        for (int channel = 0; channel < a.getNumChannels(); ++channel)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (!juce::exactlyEqual (a.getSample (channel, i), b.getSample (channel, i)))
                    return false;

        return true;
    }

    bool allFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (!std::isfinite (buffer.getSample (channel, i)))
                    return false;

        return true;
    }

    bool testPluginInstalled()
    {
        return juce::File (hostedPluginPath).exists();
    }
}

TEST_CASE ("Without a hosted plugin, audio and MIDI pass through unchanged", "[hosting]")
{
    HostedPlugin hosted;
    hosted.prepare (48000.0, testBlockSize, false);

    auto buffer = makeSineBuffer();
    const auto original = buffer;
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 10);

    hosted.process (buffer, midi, nullptr);

    CHECK (buffersMatch (buffer, original));
    CHECK (midi.getNumEvents() == 1);
}

TEST_CASE ("Loading a missing plugin fails with a message", "[hosting]")
{
    HostedPlugin hosted;

    CHECK_FALSE (hosted.load (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no-such-plugin.vst3")));
    CHECK (hosted.getInstance() == nullptr);
    CHECK (hosted.getLoadError().isNotEmpty());
}

TEST_CASE ("Hosting the test VST3", "[hosting][vst3]")
{
    if (!testPluginInstalled())
        SKIP ("Test plugin not installed: " << hostedPluginPath);

    HostedPlugin hosted;
    REQUIRE (hosted.load (juce::File (hostedPluginPath)));
    auto* instance = hosted.getInstance();
    REQUIRE (instance != nullptr);

    SECTION ("lists its real controls, without the MIDI CC slots")
    {
        const auto& controls = hosted.getControls();
        REQUIRE (!controls.empty());
        CHECK (controls.size() < static_cast<size_t> (instance->getParameters().size()));

        for (const auto* param : controls)
        {
            CHECK (param->isAutomatable());
            CHECK (param->getName (64).isNotEmpty());
        }
    }

    SECTION ("processes audio, and MIDI flows past an audio-only plugin")
    {
        hosted.prepare (48000.0, testBlockSize, false);

        auto buffer = makeSineBuffer();
        juce::MidiBuffer midi;

        for (int block = 0; block < 8; ++block)
        {
            buffer = makeSineBuffer();
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 10);
            hosted.process (buffer, midi, nullptr);
        }

        CHECK (allFinite (buffer));
        CHECK (buffer.getMagnitude (0, testBlockSize) > 0.01f);

        if (!instance->acceptsMidi() && !instance->producesMidi())
            CHECK (midi.getNumEvents() == 1);

        hosted.release();
    }

    SECTION ("restores saved state only into the same plugin")
    {
        auto state = hosted.createStateXml();
        REQUIRE (state != nullptr);
        CHECK (hosted.restoreStateFromXml (*state));

        state->setAttribute ("id", "VST3-Some Other Plugin-0-0");
        CHECK_FALSE (hosted.restoreStateFromXml (*state));
    }
}

TEST_CASE ("Hosted plugin settings survive a save and reload", "[hosting][vst3]")
{
    if (!testPluginInstalled())
        SKIP ("Test plugin not installed: " << hostedPluginPath);

    juce::MemoryBlock saved;
    float savedValue = 0.0f;

    {
        PluginProcessor original;
        auto* param = original.getHostedPlugin().getInstance()->getParameters()[0];
        param->setValueNotifyingHost (param->getValue() < 0.5f ? 0.8f : 0.2f);

        // Parameter changes reach the plugin's audio side during processing
        original.prepareToPlay (48000.0, testBlockSize);
        auto buffer = makeSineBuffer();
        juce::MidiBuffer midi;
        original.processBlock (buffer, midi);
        original.releaseResources();

        savedValue = param->getValue();
        original.getStateInformation (saved);
    }

    PluginProcessor restored;
    auto* param = restored.getHostedPlugin().getInstance()->getParameters()[0];
    REQUIRE_FALSE (juce::exactlyEqual (param->getValue(), savedValue));

    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK_THAT (param->getValue(), Catch::Matchers::WithinAbs (savedValue, 0.001));

    SECTION ("and garbage state is ignored")
    {
        restored.setStateInformation ("garbage", 7);
        CHECK_THAT (param->getValue(), Catch::Matchers::WithinAbs (savedValue, 0.001));
    }
}
