#include "RelayProcessor.h"
#include "RelayEditor.h"

juce::AudioProcessor::BusesProperties RelayProcessor::createBuses()
{
    auto buses = BusesProperties()
                     .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    // Active from the start: FL leaves outputs that start off switched off
    for (int number = 1; number <= numControlOutputs; ++number)
        buses = buses.withOutput ("Control " + juce::String (number), juce::AudioChannelSet::stereo(), true);

    return buses;
}

RelayProcessor::RelayProcessor()
    : AudioProcessor (createBuses())
{
}

bool RelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto input = layouts.getMainInputChannelSet();

    if (layouts.getMainOutputChannelSet() != stereo || (!input.isDisabled() && input != stereo))
        return false;

    // Control outputs take mono, stereo or off, so a host's pick for them can't reject the whole layout
    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
        if (layouts.outputBuses[bus].size() > 2)
            return false;

    return true;
}

void RelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    lastBlockTime.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    const auto input = getBusBuffer (buffer, true, 0);
    inputPeak.store (input.getMagnitude (0, input.getNumSamples()), std::memory_order_relaxed);

    // The main output shares its channels with the input; every output stays silent until the link exists
    for (int channel = 0; channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

bool RelayProcessor::isBeingProcessed() const noexcept
{
    constexpr juce::uint32 stallMs = 500;
    return juce::Time::getMillisecondCounter() - lastBlockTime.load (std::memory_order_relaxed) < stallMs;
}

juce::AudioProcessorEditor* RelayProcessor::createEditor()
{
    return new RelayEditor (*this);
}
