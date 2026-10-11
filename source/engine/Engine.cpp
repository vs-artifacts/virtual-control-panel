#include "Engine.h"

// One generator or slot in the graph, with its plugin inside (none: an empty slot, or b.0 and b.X for now). A
// generator's box can silence its output with a short fade while the plugin keeps running.
class Engine::SlotBox final : public juce::AudioProcessor
{
public:
    SlotBox (std::unique_ptr<HostedPlugin> pluginIn, bool generatorIn)
        : AudioProcessor (BusesProperties()
                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          plugin (std::move (pluginIn)),
          generator (generatorIn)
    {
    }

    HostedPlugin* getPlugin() const noexcept { return plugin.get(); }
    juce::String getPluginKey() const { return pluginKey; }
    void setPluginKey (const juce::String& key) { pluginKey = key; }
    bool isGenerator() const noexcept { return generator; }

    void setMuted (bool shouldMute) noexcept { muted.store (shouldMute, std::memory_order_relaxed); }
    bool isMuted() const noexcept { return muted.load (std::memory_order_relaxed); }

    void prepareToPlay (double sampleRate, int maximumBlockSize) override
    {
        if (auto* instance = getInstance())
        {
            instance->setRateAndBufferSizeDetails (sampleRate, maximumBlockSize);
            instance->prepareToPlay (sampleRate, maximumBlockSize);
            setLatencySamples (instance->getLatencySamples());
        }

        gain.reset (sampleRate, muteFadeSeconds);
        gain.setCurrentAndTargetValue (isMuted() ? 0.0f : 1.0f);
    }

    void releaseResources() override
    {
        if (auto* instance = getInstance())
            instance->releaseResources();
    }

    void setNonRealtime (bool isNonRealtime) noexcept override
    {
        AudioProcessor::setNonRealtime (isNonRealtime);

        if (auto* instance = getInstance())
            instance->setNonRealtime (isNonRealtime);
    }

    void reset() override
    {
        if (auto* instance = getInstance())
            instance->reset();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (auto* instance = getInstance())
        {
            instance->setPlayHead (getPlayHead());
            instance->processBlock (buffer, midi);
        }
        else if (generator)
        {
            buffer.clear();
        }

        if (!generator)
            return;

        gain.setTargetValue (isMuted() ? 0.0f : 1.0f);

        if (gain.isSmoothing() || !juce::exactlyEqual (gain.getTargetValue(), 1.0f))
            gain.applyGain (buffer, buffer.getNumSamples());
    }

    const juce::String getName() const override { return plugin != nullptr ? plugin->getName() : juce::String ("Empty"); }
    double getTailLengthSeconds() const override { return getInstance() != nullptr ? getInstance()->getTailLengthSeconds() : 0.0; }
    bool acceptsMidi() const override { return generator; }
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
    juce::AudioPluginInstance* getInstance() const noexcept { return plugin != nullptr ? plugin->getInstance() : nullptr; }

    std::unique_ptr<HostedPlugin> plugin;
    juce::String pluginKey;
    const bool generator;
    std::atomic<bool> muted { false };
    juce::SmoothedValue<float> gain { 1.0f };
};

Engine::Box::Box() = default;

Engine::Box::Box (Box&& other) noexcept
    : box (std::exchange (other.box, nullptr)), node (std::exchange (other.node, {})), waiting (std::move (other.waiting))
{
}

Engine::Box& Engine::Box::operator= (Box&& other) noexcept
{
    box = std::exchange (other.box, nullptr);
    node = std::exchange (other.node, {});
    waiting = std::move (other.waiting);
    return *this;
}

Engine::Box::~Box() = default;

Engine::Engine (PluginCatalog catalogIn)
    : catalog (std::move (catalogIn))
{
    juce::addDefaultFormatsToManager (formatManager);

    // No audio input, stereo output. Must be set before the graph's I/O nodes are created.
    graph.setPlayConfigDetails (0, 2, 44100.0, 512);

    using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    midiIn = graph.addNode (std::make_unique<IO> (IO::midiInputNode))->nodeID;
    midiOut = graph.addNode (std::make_unique<IO> (IO::midiOutputNode))->nodeID;
    audioOut = graph.addNode (std::make_unique<IO> (IO::audioOutputNode))->nodeID;
    connectMidi (midiIn, midiOut);
}

Engine::~Engine()
{
    stopTimer();
}

void Engine::build (const Structure& newStructure)
{
    replaceStructure (newStructure);
}

HostedPlugin* Engine::findPlugin (Id id) const
{
    const auto box = boxes.find (id);
    return box != boxes.end() ? box->second.box->getPlugin() : nullptr;
}

std::vector<HostedPlugin*> Engine::getPlugins() const
{
    std::vector<HostedPlugin*> plugins;

    for (const auto id : structure.slotIds())
        if (auto* plugin = findPlugin (id))
            plugins.push_back (plugin);

    return plugins;
}

Engine::Box Engine::createBox (const juce::String& pluginKey, bool generator)
{
    const auto role = generator ? HostedPlugin::Role::generator : HostedPlugin::Role::fx;
    std::unique_ptr<HostedPlugin> plugin;

    if (pluginKey.isNotEmpty())
    {
        const auto* entry = catalog.find (pluginKey);

        if (entry != nullptr && entry->generator == generator)
            plugin = std::make_unique<HostedPlugin> (entry->file, role, formatManager);
        else
            plugin = std::make_unique<HostedPlugin> (pluginKey, role, "Not in the plugin list");
    }

    Box box;
    box.waiting = std::make_unique<SlotBox> (std::move (plugin), generator);
    box.waiting->setPluginKey (pluginKey);
    box.box = box.waiting.get();
    return box;
}

void Engine::retire (Box& box)
{
    if (box.waiting == nullptr && box.box != nullptr)
        retiring.push_back (box.node);

    box = {};
}

void Engine::releaseSlot (Id id)
{
    std::vector<ExposedParameter> released;

    for (const auto& entry : exposed)
        if (entry.slot == id)
            released.push_back (entry);

    std::erase_if (exposed, [id] (const ExposedParameter& entry) { return entry.slot == id; });

    if (onSlotRemoving)
        onSlotRemoving (id, released);
}

void Engine::retireSlot (Id id)
{
    releaseSlot (id);

    if (const auto box = boxes.find (id); box != boxes.end())
    {
        retire (box->second);
        boxes.erase (box);
    }
}

std::optional<Engine::Id> Engine::addGenerator (const juce::String& pluginKey, int index)
{
    const auto* entry = catalog.find (pluginKey);

    if (entry == nullptr || !entry->generator || !entry->offered)
        return std::nullopt;

    const auto id = structure.addGenerator (pluginKey, index);

    if (!id.has_value())
        return std::nullopt;

    boxes[*id] = createBox (pluginKey, true);

    // A new generator feeds BUS 1, if there is one
    if (!structure.buses.empty())
        structure.setGeneratorBus (*id, structure.buses.front().id, true);

    graphChanged (true);
    return id;
}

std::optional<Engine::Id> Engine::addSlot (Id bus, int index, const juce::String& pluginKey)
{
    if (pluginKey.isNotEmpty())
        if (const auto* entry = catalog.find (pluginKey); entry == nullptr || entry->generator || !entry->offered)
            return std::nullopt;

    const auto id = structure.addSlot (bus, index, pluginKey);

    if (!id.has_value())
        return std::nullopt;

    boxes[*id] = createBox (pluginKey, false);
    graphChanged (true);
    return id;
}

bool Engine::removeSlot (Id id)
{
    if (!structure.isGenerator (id) && !structure.findSlot (id).has_value())
        return false;

    retireSlot (id);
    structure.removeSlot (id);
    graphChanged (true);
    return true;
}

bool Engine::moveSlot (Id id, Id bus, int index)
{
    if (structure.isGenerator (id))
    {
        // The generators' order changes no routing
        if (!structure.moveGenerator (id, index))
            return false;

        graphChanged (false);
        return true;
    }

    if (!structure.moveSlot (id, bus, index))
        return false;

    graphChanged (true);
    return true;
}

std::optional<Engine::Id> Engine::addBus (Id parent)
{
    const auto id = structure.addBus (parent);

    if (!id.has_value())
        return std::nullopt;

    auto& points = busPoints[*id];
    points.entry = createBox ({}, false);
    points.exit = createBox ({}, false);

    // Nothing reaches a new bus yet, so nothing heard changes
    graphChanged (false);
    return id;
}

bool Engine::removeBus (Id id)
{
    if (id == Structure::masterId || structure.findBus (id) == nullptr)
        return false;

    for (const auto slot : structure.slotsIn (id))
        retireSlot (slot);

    for (const auto& bus : structure.buses)
    {
        if (bus.id != id && bus.parent != id)
            continue;

        if (const auto points = busPoints.find (bus.id); points != busPoints.end())
        {
            retire (points->second.entry);
            retire (points->second.exit);
            busPoints.erase (points);
        }
    }

    structure.removeBus (id);
    graphChanged (true);
    return true;
}

bool Engine::setGeneratorBus (Id generator, Id bus, bool feeds)
{
    if (!structure.setGeneratorBus (generator, bus, feeds))
        return false;

    graphChanged (true);
    return true;
}

void Engine::replaceStructure (const Structure& newStructure)
{
    // Everything is unbound; plugins already loaded are reused, in order, where the new structure has the same one
    std::vector<std::pair<juce::String, Box>> loaded;

    for (const auto id : structure.slotIds())
    {
        releaseSlot (id);

        if (const auto box = boxes.find (id); box != boxes.end())
            loaded.emplace_back (box->second.box->getPluginKey(), std::move (box->second));
    }

    boxes.clear();
    exposed.clear();

    const auto takeBox = [&] (const juce::String& key, bool generator) {
        for (auto& [loadedKey, box] : loaded)
        {
            if (box.box != nullptr && loadedKey == key && box.box->isGenerator() == generator)
            {
                box.box->setMuted (false);
                return std::move (box);
            }
        }

        return createBox (key, generator);
    };

    for (const auto& generator : newStructure.generators)
        boxes[generator.id] = takeBox (generator.plugin, true);

    const auto takeSlots = [&] (const Structure::Bus& bus) {
        for (const auto& slot : bus.slots)
            boxes[slot.id] = takeBox (slot.plugin, false);
    };

    for (const auto& bus : newStructure.buses)
        takeSlots (bus);

    takeSlots (newStructure.master);

    for (auto& [key, box] : loaded)
        if (box.box != nullptr)
            retire (box);

    for (auto& [id, points] : busPoints)
    {
        retire (points.entry);
        retire (points.exit);
    }

    busPoints.clear();

    for (const auto& bus : newStructure.buses)
        busPoints[bus.id] = { createBox ({}, false), createBox ({}, false) };

    busPoints[Structure::masterId] = { createBox ({}, false), createBox ({}, false) };

    structure = newStructure;
    graphOutdated = true;
    syncGraph();

    if (onStructureChanged)
        onStructureChanged();
}

void Engine::graphChanged (bool audible)
{
    graphOutdated = true;

    if (audible && isAudioRunning())
    {
        if (dipTarget.exchange (0.0f) > 0.0f)
        {
            dipSilent.store (false);
            dipStartedMs = juce::Time::getMillisecondCounter();
        }

        startTimerHz (100);
    }
    else
    {
        applyWaitingChanges();
    }

    if (onStructureChanged)
        onStructureChanged();
}

void Engine::applyWaitingChanges()
{
    if (!graphOutdated)
    {
        stopTimer();
        return;
    }

    const auto dipping = dipTarget.load() < 1.0f;
    const auto timedOut = juce::Time::getMillisecondCounter() - dipStartedMs > static_cast<juce::uint32> (dipOutMs.load()) + dipTimeoutMs;

    if (dipping && !dipSilent.load() && isAudioRunning() && !timedOut)
        return;

    syncGraph();
    dipSilent.store (false);
    dipTarget.store (1.0f);
    stopTimer();
}

void Engine::syncGraph()
{
    for (const auto node : retiring)
        graph.removeNode (node, juce::AudioProcessorGraph::UpdateKind::none);

    retiring.clear();

    const auto addWaiting = [this] (Box& box) {
        if (box.waiting != nullptr)
            box.node = graph.addNode (std::move (box.waiting), std::nullopt, juce::AudioProcessorGraph::UpdateKind::none)->nodeID;
    };

    for (auto& [id, box] : boxes)
        addWaiting (box);

    for (auto& [id, points] : busPoints)
    {
        addWaiting (points.entry);
        addWaiting (points.exit);
    }

    for (const auto& connection : graph.getConnections())
        graph.removeConnection (connection, juce::AudioProcessorGraph::UpdateKind::none);

    connectMidi (midiIn, midiOut);

    for (const auto& generator : structure.generators)
    {
        const auto node = boxes.at (generator.id).node;
        connectMidi (midiIn, node);

        for (const auto bus : generator.buses)
            connectAudio (node, busPoints.at (bus).entry.node);
    }

    const auto chain = [this] (const Structure::Bus& bus) {
        auto previous = busPoints.at (bus.id).entry.node;

        for (const auto& slot : bus.slots)
        {
            const auto node = boxes.at (slot.id).node;
            connectAudio (previous, node);
            previous = node;
        }

        connectAudio (previous, busPoints.at (bus.id).exit.node);
    };

    for (const auto& bus : structure.buses)
    {
        chain (bus);
        connectAudio (busPoints.at (bus.id).exit.node, bus.parent != Structure::none ? busPoints.at (bus.parent).exit.node : busPoints.at (Structure::masterId).entry.node);
    }

    chain (structure.master);
    connectAudio (busPoints.at (Structure::masterId).exit.node, audioOut);

    graph.rebuild();
    graphOutdated = false;
}

bool Engine::isAudioRunning() const
{
    return prepared.load() && juce::Time::getMillisecondCounter() - lastBlockMs.load() < stoppedAfterMs;
}

void Engine::setChangeFades (int outMs, int inMs) noexcept
{
    dipOutMs.store (std::max (0, outMs));
    dipInMs.store (std::max (0, inMs));
}

void Engine::prepare (double newSampleRate, int maxBlockSize, bool isNonRealtime)
{
    // Prepares every hosted plugin on this thread; VST3s need the message thread, which is where VST3 hosts call from.
    // Audio isn't running now, so a waiting change needs no dip.
    if (graphOutdated)
        syncGraph();

    stopTimer();
    dipSilent.store (false);
    dipTarget.store (1.0f);
    dipGain = 1.0f;
    sampleRate = newSampleRate;
    graph.setNonRealtime (isNonRealtime);
    graph.prepareToPlay (newSampleRate, maxBlockSize);
    prepared.store (true);
}

void Engine::release()
{
    prepared.store (false);
    graph.releaseResources();
}

void Engine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead)
{
    lastBlockMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    graph.setPlayHead (playHead);
    graph.processBlock (buffer, midi);

    const auto target = dipTarget.load (std::memory_order_relaxed);

    if (dipGain < 1.0f || target < 1.0f)
    {
        const auto fadeMs = target < dipGain ? dipOutMs.load (std::memory_order_relaxed) : dipInMs.load (std::memory_order_relaxed);
        const auto fadeSamples = fadeMs * sampleRate / 1000.0;
        const auto step = fadeSamples > 1.0 ? static_cast<float> (1.0 / fadeSamples) : 1.0f;
        const auto channels = buffer.getNumChannels();

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            dipGain = target > dipGain ? std::min (target, dipGain + step) : std::max (target, dipGain - step);

            for (int channel = 0; channel < channels; ++channel)
                buffer.getWritePointer (channel)[i] *= dipGain;
        }
    }

    dipSilent.store (target <= 0.0f && dipGain <= 0.0f, std::memory_order_relaxed);
}

int Engine::getLatencySamples() const
{
    return graph.getLatencySamples();
}

double Engine::getTailLengthSeconds() const
{
    auto longest = 0.0;

    for (const auto& [id, box] : boxes)
        longest = std::max (longest, box.box->getTailLengthSeconds());

    return longest;
}

std::unique_ptr<juce::XmlElement> Engine::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (stateTag);
    xml->setAttribute ("version", stateVersion);
    xml->addChildElement (structure.toXml().release());

    for (const auto id : structure.slotIds())
    {
        auto* slotState = xml->createNewChildElement ("SlotState");
        slotState->setAttribute ("id", id);

        if (canMute (id))
            slotState->setAttribute ("muted", isMuted (id));

        if (auto* plugin = findPlugin (id))
            if (auto pluginState = plugin->createStateXml())
                slotState->addChildElement (pluginState.release());
    }

    for (const auto& entry : exposed)
    {
        auto* exposure = xml->createNewChildElement ("Exposed");
        exposure->setAttribute ("slot", entry.slot);
        exposure->setAttribute ("parameter", entry.parameterID);
        exposure->setAttribute ("control", entry.hostControl);
    }

    return xml;
}

bool Engine::restoreStateFromXml (const juce::XmlElement& xml)
{
    if (!xml.hasTagName (stateTag) || xml.getIntAttribute ("version") != stateVersion)
        return false;

    const auto* structureXml = xml.getChildByName (Structure::xmlTag);
    const auto saved = structureXml != nullptr ? Structure::fromXml (*structureXml) : std::nullopt;

    if (!saved.has_value())
        return false;

    replaceStructure (*saved);

    // By slot, so two copies of the same plugin each get their own settings back
    for (auto* slotState : xml.getChildWithTagNameIterator ("SlotState"))
    {
        const auto id = slotState->getIntAttribute ("id");

        if (auto* plugin = findPlugin (id))
            if (auto* pluginState = slotState->getChildByName (HostedPlugin::stateTag))
                plugin->restoreStateFromXml (*pluginState);

        setMuted (id, slotState->getBoolAttribute ("muted"));
    }

    for (auto* exposure : xml.getChildWithTagNameIterator ("Exposed"))
        expose (exposure->getIntAttribute ("slot"), exposure->getStringAttribute ("parameter"), exposure->getIntAttribute ("control", -1));

    return true;
}

bool Engine::feeds (Id source, Id destination) const
{
    const auto from = boxes.find (source);
    const auto to = boxes.find (destination);

    if (from == boxes.end() || to == boxes.end() || from->second.waiting != nullptr || to->second.waiting != nullptr)
        return false;

    return graph.isAnInputTo (from->second.node, to->second.node);
}

bool Engine::setMuted (Id id, bool muted)
{
    if (!canMute (id))
        return false;

    boxes.at (id).box->setMuted (muted);
    return true;
}

bool Engine::isMuted (Id id) const
{
    return canMute (id) && boxes.at (id).box->isMuted();
}

bool Engine::canMute (Id id) const
{
    const auto box = boxes.find (id);
    return box != boxes.end() && box->second.box->isGenerator();
}

juce::String Engine::getSlotLabel (Id id) const
{
    if (structure.isGenerator (id))
        return structure.labelOf (id);

    if (structure.findSlot (id).has_value())
        return "SLOT " + structure.labelOf (id);

    return {};
}

bool Engine::expose (Id slot, const juce::String& parameterID, int hostControl)
{
    auto* plugin = findPlugin (slot);
    auto* parameter = plugin != nullptr ? plugin->findControl (parameterID) : nullptr;

    if (parameter == nullptr || hostControl < 0)
        return false;

    std::erase_if (exposed, [hostControl] (const ExposedParameter& entry) { return entry.hostControl == hostControl; });
    exposed.push_back ({ slot, parameterID, parameter, hostControl });
    return true;
}

void Engine::unexpose (int hostControl)
{
    std::erase_if (exposed, [hostControl] (const ExposedParameter& entry) { return entry.hostControl == hostControl; });
}

void Engine::connectAudio (NodeID source, NodeID destination)
{
    for (int channel = 0; channel < 2; ++channel)
    {
        [[maybe_unused]] const auto connected = graph.addConnection ({ { source, channel }, { destination, channel } }, juce::AudioProcessorGraph::UpdateKind::none);
        jassert (connected);
    }
}

void Engine::connectMidi (NodeID source, NodeID destination)
{
    constexpr auto midi = juce::AudioProcessorGraph::midiChannelIndex;
    [[maybe_unused]] const auto connected = graph.addConnection ({ { source, midi }, { destination, midi } }, juce::AudioProcessorGraph::UpdateKind::none);
    jassert (connected);
}
