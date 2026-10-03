#include "RelayProcessor.h"
#include "../link/LinkHub.h"
#include "RelayEditor.h"

juce::AudioProcessor::BusesProperties RelayProcessor::createBuses()
{
    auto buses = BusesProperties()
                     .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    // Active from the start: FL leaves outputs that start off switched off. Mono: FL's Peak Controller reads one channel.
    for (int number = 1; number <= numControlOutputs; ++number)
        buses = buses.withOutput ("Control " + juce::String (number), juce::AudioChannelSet::mono(), true);

    return buses;
}

RelayProcessor::RelayProcessor()
    : AudioProcessor (createBuses()),
      link (*heartseeker_link_get_api (HS_LINK_ABI_VERSION))
{
    linkCheck.startTimerHz (4);
}

void RelayProcessor::prepareToPlay (double sampleRate, int maxBlockSize)
{
    link.prepare (sampleRate, maxBlockSize);
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

    // Without a stereo main input (a host may switch it off), channels 0 and 1 hold nothing a transmitter should send
    if (getMainBusNumInputChannels() < 2)
        for (int channel = 0; channel < std::min (2, buffer.getNumChannels()); ++channel)
            buffer.clear (channel, 0, buffer.getNumSamples());

    // The main output shares its channels with the input; every output is silent except what the link plays
    link.process (buffer);

    // A receiver's control outputs: each value as a steady level, held when the panel's values don't arrive
    if (getMode() != RelayLink::Mode::receiver)
        return;

    for (int bus = 1; bus <= std::min (numControlOutputs, getBusCount (false) - 1); ++bus)
    {
        auto output = getBusBuffer (buffer, false, bus);

        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill (output.getWritePointer (channel), link.getControl (bus - 1), output.getNumSamples());
    }
}

void RelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state (stateTag);
    state.setAttribute ("mode", getMode() == RelayLink::Mode::transmitter ? "transmitter" : "receiver");

    for (int i = 0; i < numControlOutputs; ++i)
        state.setAttribute ("control" + juce::String (i + 1), static_cast<double> (link.getControl (i)));

    copyXmlToBinary (state, destData);
}

void RelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Anything but our own saved state (e.g. a corrupt project) changes nothing
    const auto state = getXmlFromBinary (data, sizeInBytes);

    if (state == nullptr || !state->hasTagName (stateTag))
        return;

    const auto mode = state->getStringAttribute ("mode");

    if (mode == "transmitter")
        setMode (RelayLink::Mode::transmitter);
    else if (mode == "receiver")
        setMode (RelayLink::Mode::receiver);

    for (int i = 0; i < numControlOutputs; ++i)
        if (const auto name = "control" + juce::String (i + 1); state->hasAttribute (name))
            link.setControl (i, static_cast<float> (state->getDoubleAttribute (name)));
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
