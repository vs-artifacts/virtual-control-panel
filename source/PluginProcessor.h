#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/HostedPlugin.h"

#if (MSVC)
    #include "ipps.h"
#endif

// Slice 1a stand-in until plugin scanning and selection exist
inline constexpr auto hostedPluginPath = R"(C:\Program Files\Common Files\VST3\Kilohearts\kHs 3-Band EQ.vst3)";

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    HostedPlugin& getHostedPlugin() noexcept { return hostedPlugin; }

private:
    HostedPlugin hostedPlugin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
