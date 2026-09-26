#include "Engine.h"

Engine::Engine()
{
    juce::addDefaultFormatsToManager (formatManager);

    // No audio input, stereo output. Must be set before the graph's I/O nodes are created.
    graph.setPlayConfigDetails (0, 2, 44100.0, 512);
}

void Engine::build (const std::vector<juce::File>& generators, const std::vector<juce::File>& fxChain)
{
    jassert (plugins.empty());

    using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    const auto midiIn = graph.addNode (std::make_unique<IO> (IO::midiInputNode))->nodeID;
    const auto midiOut = graph.addNode (std::make_unique<IO> (IO::midiOutputNode))->nodeID;
    const auto audioOut = graph.addNode (std::make_unique<IO> (IO::audioOutputNode))->nodeID;

    // Pass-through lane; bypass controls come later
    connectMidi (midiIn, midiOut);

    for (const auto& file : generators)
        plugins.push_back (std::make_unique<HostedPlugin> (file, HostedPlugin::Role::generator, formatManager, graph));

    for (const auto& file : fxChain)
        plugins.push_back (std::make_unique<HostedPlugin> (file, HostedPlugin::Role::fx, formatManager, graph));

    std::vector<NodeID> chain;

    for (const auto& plugin : plugins)
        if (plugin->getRole() == HostedPlugin::Role::fx && plugin->getInstance() != nullptr)
            chain.push_back (plugin->getNodeID());

    chain.push_back (audioOut);

    for (size_t i = 0; i + 1 < chain.size(); ++i)
        connectAudio (chain[i], chain[i + 1]);

    // Every generator gets the same MIDI; the graph sums their audio where they meet
    for (const auto& plugin : plugins)
    {
        if (plugin->getRole() == HostedPlugin::Role::generator && plugin->getInstance() != nullptr)
        {
            connectMidi (midiIn, plugin->getNodeID());
            connectAudio (plugin->getNodeID(), chain.front());
        }
    }
}

void Engine::prepare (double sampleRate, int maxBlockSize, bool isNonRealtime)
{
    // Prepares every hosted plugin on this thread; VST3s need the message thread, which is where VST3 hosts call from
    graph.setNonRealtime (isNonRealtime);
    graph.prepareToPlay (sampleRate, maxBlockSize);
}

void Engine::release()
{
    graph.releaseResources();
}

void Engine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead)
{
    graph.setPlayHead (playHead);
    graph.processBlock (buffer, midi);
}

int Engine::getLatencySamples() const
{
    return graph.getLatencySamples();
}

double Engine::getTailLengthSeconds() const
{
    auto longest = 0.0;

    for (const auto& plugin : plugins)
        if (auto* instance = plugin->getInstance())
            longest = std::max (longest, instance->getTailLengthSeconds());

    return longest;
}

std::unique_ptr<juce::XmlElement> Engine::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (stateTag);

    for (size_t slot = 0; slot < plugins.size(); ++slot)
    {
        if (auto pluginState = plugins[slot]->createStateXml())
        {
            pluginState->setAttribute ("slot", static_cast<int> (slot));
            xml->addChildElement (pluginState.release());
        }
    }

    return xml;
}

void Engine::restoreStateFromXml (const juce::XmlElement& xml)
{
    // By slot, so two copies of the same plugin each get their own settings back
    for (auto* pluginState : xml.getChildWithTagNameIterator (HostedPlugin::stateTag))
    {
        const auto slot = pluginState->getIntAttribute ("slot", -1);

        if (juce::isPositiveAndBelow (slot, static_cast<int> (plugins.size())))
            plugins[static_cast<size_t> (slot)]->restoreStateFromXml (*pluginState);
    }
}

bool Engine::isConnected (const HostedPlugin& source, const HostedPlugin& destination) const
{
    return source.getInstance() != nullptr && destination.getInstance() != nullptr
           && graph.isConnected (source.getNodeID(), destination.getNodeID());
}

void Engine::connectAudio (NodeID source, NodeID destination)
{
    for (int channel = 0; channel < 2; ++channel)
    {
        [[maybe_unused]] const auto connected = graph.addConnection ({ { source, channel }, { destination, channel } });
        jassert (connected);
    }
}

void Engine::connectMidi (NodeID source, NodeID destination)
{
    constexpr auto midi = juce::AudioProcessorGraph::midiChannelIndex;
    [[maybe_unused]] const auto connected = graph.addConnection ({ { source, midi }, { destination, midi } });
    jassert (connected);
}
