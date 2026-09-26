#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/Engine.h"

#if (MSVC)
    #include "ipps.h"
#endif

// Stand-ins until plugin scanning and routing configuration exist
inline constexpr auto serum2Path = R"(C:\Program Files\Common Files\VST3\Serum2.vst3)";
inline constexpr auto sylenth1Path = R"(C:\Program Files\Common Files\VST3\Sylenth1.vst3)";
inline constexpr auto eqPath = R"(C:\Program Files\Common Files\VST3\Kilohearts\kHs 3-Band EQ.vst3)";

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

    Engine& getEngine() noexcept { return engine; }

private:
    Engine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
