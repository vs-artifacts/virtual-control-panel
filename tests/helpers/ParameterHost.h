#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// A bare processor that only owns parameters, so tests can use them the way hosts and plugins do
struct ParameterHost final : juce::AudioProcessor
{
    const juce::String getName() const override { return "ParameterHost"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // From 0 to 1, starting at 0.5
    juce::AudioProcessorParameter& addFloat (const juce::String& id)
    {
        auto* parameter = new juce::AudioParameterFloat (juce::ParameterID { id, 1 }, id, 0.0f, 1.0f, 0.5f);
        addParameter (parameter);
        return *parameter;
    }
};

// What a host would hear from a processor about its parameters
struct HostLog final : juce::AudioProcessorListener
{
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float value) override { changes.emplace_back (index, value); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override { infoChanges += details.parameterInfoChanged ? 1 : 0; }
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { gestures.emplace_back (index, true); }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { gestures.emplace_back (index, false); }

    std::vector<std::pair<int, float>> changes;
    std::vector<std::pair<int, bool>> gestures;
    int infoChanges = 0; // parameter names (and other details) changed
};
