#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The relay node, test build: a small effect plugin that will link FL's own plugins into the panel's bays.
// No link yet: it reads the level arriving on its input, and its control outputs are silent.
// Shares nothing with the panel's shell: the Tests target compiles both, so no JucePlugin_ macros here.
class RelayProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int numControlOutputs = 4;

    RelayProcessor();

    const juce::String getName() const override { return "heartseeker relay"; }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Loudest sample of the main input's last block, to read the exact level arriving on a wire
    float getInputPeak() const noexcept { return inputPeak.load (std::memory_order_relaxed); }

    // Whether the host has run our audio in the last half second; false while it doesn't (e.g. FL's smart disable)
    bool isBeingProcessed() const noexcept;

private:
    static BusesProperties createBuses();

    std::atomic<float> inputPeak { 0.0f };
    std::atomic<juce::uint32> lastBlockTime { 0 }; // Time::getMillisecondCounter()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelayProcessor)
};
