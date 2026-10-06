#include "Engine.h"

// Silences one generator's audio with a short fade, without touching the generator itself
class Engine::MuteStage final : public juce::AudioProcessor
{
public:
    MuteStage()
        : AudioProcessor (BusesProperties()
                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void setMuted (bool shouldMute) noexcept { muted.store (shouldMute, std::memory_order_relaxed); }
    bool isMuted() const noexcept { return muted.load (std::memory_order_relaxed); }

    void prepareToPlay (double sampleRate, int) override
    {
        gain.reset (sampleRate, muteFadeSeconds);
        gain.setCurrentAndTargetValue (isMuted() ? 0.0f : 1.0f);
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        gain.setTargetValue (isMuted() ? 0.0f : 1.0f);

        if (gain.isSmoothing() || !juce::exactlyEqual (gain.getTargetValue(), 1.0f))
            gain.applyGain (buffer, buffer.getNumSamples());
    }

    const juce::String getName() const override { return "Mute"; }
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

private:
    std::atomic<bool> muted { false };
    juce::SmoothedValue<float> gain { 1.0f };
};

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
    muteSlots.assign (plugins.size(), {});

    for (size_t slot = 0; slot < plugins.size(); ++slot)
    {
        const auto& plugin = plugins[slot];

        if (plugin->getRole() == HostedPlugin::Role::generator && plugin->getInstance() != nullptr)
        {
            auto stage = std::make_unique<MuteStage>();
            auto* stagePointer = stage.get();
            const auto stageID = graph.addNode (std::move (stage))->nodeID;
            muteSlots[slot] = { stagePointer, stageID };

            connectMidi (midiIn, plugin->getNodeID());
            connectAudio (plugin->getNodeID(), stageID);
            connectAudio (stageID, chain.front());
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

            if (canMute (slot))
                pluginState->setAttribute ("muted", isMuted (slot));

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
        {
            plugins[static_cast<size_t> (slot)]->restoreStateFromXml (*pluginState);
            setMuted (static_cast<size_t> (slot), pluginState->getBoolAttribute ("muted"));
        }
    }
}

bool Engine::isConnected (const HostedPlugin& source, const HostedPlugin& destination) const
{
    if (source.getInstance() == nullptr || destination.getInstance() == nullptr)
        return false;

    if (graph.isConnected (source.getNodeID(), destination.getNodeID()))
        return true;

    for (const auto& [stage, nodeID] : muteSlots)
        if (stage != nullptr && graph.isConnected (source.getNodeID(), nodeID) && graph.isConnected (nodeID, destination.getNodeID()))
            return true;

    return false;
}

bool Engine::setMuted (size_t slot, bool muted)
{
    if (!canMute (slot))
        return false;

    muteSlots[slot].stage->setMuted (muted);
    return true;
}

bool Engine::isMuted (size_t slot) const
{
    return canMute (slot) && muteSlots[slot].stage->isMuted();
}

bool Engine::canMute (size_t slot) const
{
    return slot < muteSlots.size() && muteSlots[slot].stage != nullptr;
}

juce::String Engine::getSlotLabel (size_t slot) const
{
    const auto role = plugins[slot]->getRole();
    const auto sameRoleBefore = std::count_if (plugins.begin(), plugins.begin() + static_cast<std::ptrdiff_t> (slot), [role] (const auto& p) { return p->getRole() == role; });

    return juce::String (role == HostedPlugin::Role::generator ? "Generator " : "FX ") + juce::String (sameRoleBefore + 1);
}

bool Engine::expose (size_t slot, const juce::String& parameterID)
{
    if (slot >= plugins.size())
        return false;

    auto* parameter = plugins[slot]->findControl (parameterID);

    if (parameter == nullptr)
        return false;

    exposed.push_back ({ slot, parameter });
    return true;
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
