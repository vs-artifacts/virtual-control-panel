#pragma once

#include "RelayLink.h"
#include <juce_audio_processors/juce_audio_processors.h>

// The relay node, test build: a small effect plugin that links FL's own plugins into the panel's bays.
// Receiver mode (before the FL plugin) plays what the panel sends, and the panel's control values as steady levels
// on its control outputs (into FL Peak Controllers); transmitter mode (after it) sends the FL plugin's audio back.
// Shares nothing with the panel's shell: the Tests target compiles both, so no JucePlugin_ macros here.
class RelayProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int numControlOutputs = RelayLink::numControls;

    RelayProcessor();

    const juce::String getName() const override { return "heartseeker relay"; }

    void prepareToPlay (double, int) override;
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

    // Saves and restores the mode and the control values
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static constexpr auto stateTag = "HeartseekerRelayState";

    RelayLink::Mode getMode() const noexcept { return link.getMode(); }
    void setMode (RelayLink::Mode mode) { link.setMode (mode); }
    RelayLink& getLink() noexcept { return link; }

    // What a timer does 4 times a second; tests call it directly
    void maintainLink() { link.maintain(); }

    // Loudest sample of the main input's last block, to read the exact level arriving on a wire
    float getInputPeak() const noexcept { return inputPeak.load (std::memory_order_relaxed); }

    // Whether the host has run our audio in the last half second; false while it doesn't (e.g. FL's smart disable)
    bool isBeingProcessed() const noexcept;

private:
    static BusesProperties createBuses();

    RelayLink link;
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<juce::uint32> lastBlockTime { 0 }; // Time::getMillisecondCounter()

    // Declared last, so it stops before anything it uses goes
    juce::TimedCallback linkCheck { [this] { link.maintain(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelayProcessor)
};
