#include "Engine.h"

// One generator or slot in the graph, with its plugin inside (none: an empty slot, or b.0 and b.X for now, which pass
// their input on). With a plugin it has the slot states (see SlotState) and a linear dry/wet mix; the dry sound is the
// input delayed by the plugin's latency, so Mute and Bypass keep the slot's timing. A generator's dry is silence.
class Engine::SlotBox final : public juce::AudioProcessor
{
public:
    SlotBox (std::unique_ptr<HostedPlugin> pluginIn, bool generatorIn, const StateFades& fadesIn)
        : AudioProcessor (BusesProperties()
                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          plugin (std::move (pluginIn)),
          generator (generatorIn),
          fades (fadesIn)
    {
    }

    HostedPlugin* getPlugin() const noexcept { return plugin.get(); }
    juce::String getPluginKey() const { return pluginKey; }
    void setPluginKey (const juce::String& key) { pluginKey = key; }
    bool isGenerator() const noexcept { return generator; }

    // Message thread. Off only takes effect once the graph leaves the box out (setOffline); until then it plays on as it was.
    void setState (SlotState newState) noexcept { state.store (newState); }
    SlotState getState() const noexcept { return state.load(); }
    void setMix (float newMix) noexcept { mix.store (juce::jlimit (0.0f, 1.0f, newMix)); }
    float getMix() const noexcept { return mix.load(); }
    void setEndsPathWhenOff (bool ends) noexcept { endsPath.store (ends); }
    bool endsPathWhenOff() const noexcept { return endsPath.load(); }

    // Message thread, as the graph is rewired: out of it (Off, or cut off by an earlier slot): silent, plugin not run
    void setOffline (bool isOffline) noexcept { offline.store (isOffline); }

    // Message thread: resets the plugin once the audio thread has stopped running it, before it may run again
    void resetIfIdle()
    {
        if (!needsReset.load())
            return;

        // With running (audio thread), a handshake: the plugin is never reset while it runs, nor run while it's reset
        holding.store (true);

        if (!running.load())
        {
            if (auto* instance = getInstance())
                instance->reset();

            needsReset.store (false);
        }

        holding.store (false);
    }

    void prepareToPlay (double sampleRate, int maximumBlockSize) override
    {
        if (auto* instance = getInstance())
        {
            instance->setRateAndBufferSizeDetails (sampleRate, maximumBlockSize);
            instance->prepareToPlay (sampleRate, maximumBlockSize);
            setLatencySamples (instance->getLatencySamples());
        }

        latency = getLatencySamples();
        blockCapacity = maximumBlockSize;
        dryLine.setSize (2, latency + maximumBlockSize);
        dryLine.clear();
        dryBlock.setSize (2, maximumBlockSize);
        writePosition = 0;
        rate = sampleRate;
        mixGain.reset (sampleRate, mixRampSeconds);
        mixGain.setCurrentAndTargetValue (mix.load());
        stateGain = state.load() == SlotState::on ? 1.0f : 0.0f;
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
        auto* instance = getInstance();

        if (instance == nullptr)
        {
            if (generator)
                buffer.clear();

            return;
        }

        if (offline.load (std::memory_order_relaxed))
        {
            stopRunning();
            buffer.clear();
            wasOffline = true;
            return;
        }

        // Back in the graph (under the dip): no old dry sound, and the state's level at once
        const auto returning = std::exchange (wasOffline, false);

        if (returning)
            dryLine.clear();

        auto target = state.load (std::memory_order_relaxed);

        if (target == SlotState::off)
            target = playing;
        else
            playing = target;

        if (target != SlotState::on)
            lastLeft = target;

        const auto numSamples = buffer.getNumSamples();
        const auto channels = std::min (2, buffer.getNumChannels());
        const auto withDry = !generator && numSamples <= blockCapacity;

        if (withDry)
            delayDry (buffer, numSamples, channels);

        const auto wanted = target == SlotState::on ? 1.0f : 0.0f;
        const auto needsPlugin = target != SlotState::bypass || stateGain > 0.0f;
        const auto runs = needsPlugin && startRunning();

        if (!needsPlugin)
            stopRunning();

        if (runs)
        {
            instance->setPlayHead (getPlayHead());
            instance->processBlock (buffer, midi);
        }
        else
        {
            buffer.clear();
        }

        if (returning || !runs)
            stateGain = runs ? wanted : 0.0f;

        mixGain.setTargetValue (mix.load (std::memory_order_relaxed));

        // The plugin's sound alone, as it was
        if (juce::exactlyEqual (stateGain, 1.0f) && juce::exactlyEqual (wanted, 1.0f) && !mixGain.isSmoothing() && juce::exactlyEqual (mixGain.getTargetValue(), 1.0f))
            return;

        const auto fadeMs = (wanted < stateGain ? target : lastLeft) == SlotState::bypass ? fades.bypassMs.load (std::memory_order_relaxed) : fades.muteMs.load (std::memory_order_relaxed);
        const auto fadeSamples = fadeMs * rate / 1000.0;
        const auto step = fadeSamples > 1.0 ? static_cast<float> (1.0 / fadeSamples) : 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            stateGain = wanted > stateGain ? std::min (wanted, stateGain + step) : std::max (wanted, stateGain - step);
            const auto wet = stateGain * mixGain.getNextValue();

            for (int channel = 0; channel < channels; ++channel)
            {
                auto* samples = buffer.getWritePointer (channel);
                samples[i] = wet * samples[i] + (withDry ? (1.0f - wet) * dryBlock.getSample (channel, i) : 0.0f);
            }
        }
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

    // The input into the delay line, and the input of latency samples ago out of it (into dryBlock)
    void delayDry (const juce::AudioBuffer<float>& buffer, int numSamples, int channels)
    {
        const auto size = dryLine.getNumSamples();

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto* input = buffer.getReadPointer (channel);
            auto* line = dryLine.getWritePointer (channel);
            auto* out = dryBlock.getWritePointer (channel);

            for (int i = 0; i < numSamples; ++i)
                line[(writePosition + i) % size] = input[i];

            const auto start = writePosition - latency + size;

            for (int i = 0; i < numSamples; ++i)
                out[i] = line[(start + i) % size];
        }

        writePosition = (writePosition + numSamples) % size;
    }

    // Audio thread: false while the message thread resets the plugin, or a reset is still to come
    bool startRunning() noexcept
    {
        if (isRunning)
            return true;

        if (needsReset.load())
            return false;

        running.store (true);

        if (holding.load())
        {
            running.store (false);
            return false;
        }

        isRunning = true;
        return true;
    }

    void stopRunning() noexcept
    {
        if (!isRunning)
            return;

        isRunning = false;
        running.store (false);
        needsReset.store (true);
    }

    std::unique_ptr<HostedPlugin> plugin;
    juce::String pluginKey;
    const bool generator;
    const StateFades& fades;

    std::atomic<SlotState> state { SlotState::on };
    std::atomic<float> mix { 1.0f };
    std::atomic<bool> endsPath { false }, offline { false };
    std::atomic<bool> running { false }, holding { false }, needsReset { false };

    // Audio thread
    bool isRunning = false, wasOffline = false;
    SlotState playing = SlotState::on, lastLeft = SlotState::mute;
    float stateGain = 1.0f;
    juce::SmoothedValue<float> mixGain { 1.0f };
    juce::AudioBuffer<float> dryLine, dryBlock;
    int latency = 0, blockCapacity = 0, writePosition = 0;
    double rate = 44100.0;
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
    : catalog (std::move (catalogIn)),
      resetCheck ([this] {
          for (auto& [id, box] : boxes)
              box.box->resetIfIdle();
      })
{
    juce::addDefaultFormatsToManager (formatManager);

    // No audio input, stereo output. Must be set before the graph's I/O nodes are created.
    graph.setPlayConfigDetails (0, 2, 44100.0, 512);

    using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    midiIn = graph.addNode (std::make_unique<IO> (IO::midiInputNode))->nodeID;
    midiOut = graph.addNode (std::make_unique<IO> (IO::midiOutputNode))->nodeID;
    audioOut = graph.addNode (std::make_unique<IO> (IO::audioOutputNode))->nodeID;
    connectMidi (midiIn, midiOut);
    resetCheck.startTimerHz (20);
}

Engine::~Engine()
{
    resetCheck.stopTimer();
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
    box.waiting = std::make_unique<SlotBox> (std::move (plugin), generator, stateFades);
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
                box.box->setState (SlotState::on);
                box.box->setMix (1.0f);
                box.box->setEndsPathWhenOff (false);
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
        auto& box = boxes.at (generator.id);
        const auto off = box.box->getPlugin() != nullptr && box.box->getState() == SlotState::off;
        box.box->setOffline (off);
        connectMidi (midiIn, box.node);

        if (!off)
            for (const auto bus : generator.buses)
                connectAudio (box.node, busPoints.at (bus).entry.node);
    }

    // A slot that's Off is left out; one that also ends the signal path leaves out every later slot in its bus
    const auto chain = [this] (const Structure::Bus& bus) {
        std::optional<NodeID> previous = busPoints.at (bus.id).entry.node;

        for (const auto& slot : bus.slots)
        {
            auto& box = boxes.at (slot.id);
            const auto off = box.box->getPlugin() != nullptr && box.box->getState() == SlotState::off;
            box.box->setOffline (off || !previous.has_value());

            if (off && box.box->endsPathWhenOff())
                previous.reset();

            if (off || !previous.has_value())
                continue;

            connectAudio (*previous, box.node);
            previous = box.node;
        }

        if (previous.has_value())
            connectAudio (*previous, busPoints.at (bus.id).exit.node);
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

        if (hasStates (id))
        {
            slotState->setAttribute ("state", toName (getSlotState (id)));
            slotState->setAttribute ("mix", getMix (id));
            slotState->setAttribute ("endsPath", endsPathWhenOff (id));
        }

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

        setMix (id, static_cast<float> (slotState->getDoubleAttribute ("mix", 1.0)));
        setEndsPathWhenOff (id, slotState->getBoolAttribute ("endsPath"));
        setSlotState (id, slotStateFromName (slotState->getStringAttribute ("state")).value_or (SlotState::on));
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

Engine::SlotBox* Engine::findBox (Id id) const
{
    const auto box = boxes.find (id);
    return box != boxes.end() && box->second.box->getPlugin() != nullptr ? box->second.box : nullptr;
}

bool Engine::hasStates (Id id) const
{
    return findBox (id) != nullptr;
}

bool Engine::setSlotState (Id id, SlotState newState)
{
    auto* box = findBox (id);

    if (box == nullptr)
        return false;

    const auto wasOff = box->getState() == SlotState::off;
    box->setState (newState);

    // Off, and back, rewires the graph
    if (wasOff != (newState == SlotState::off))
        graphChanged (true);

    return true;
}

SlotState Engine::getSlotState (Id id) const
{
    const auto* box = findBox (id);
    return box != nullptr ? box->getState() : SlotState::on;
}

bool Engine::setMix (Id id, float mix)
{
    auto* box = findBox (id);

    if (box == nullptr || !std::isfinite (mix))
        return false;

    box->setMix (mix);
    return true;
}

float Engine::getMix (Id id) const
{
    const auto* box = findBox (id);
    return box != nullptr ? box->getMix() : 1.0f;
}

bool Engine::setEndsPathWhenOff (Id id, bool ends)
{
    auto* box = findBox (id);

    if (box == nullptr)
        return false;

    const auto changed = box->endsPathWhenOff() != ends;
    box->setEndsPathWhenOff (ends);

    if (changed && box->getState() == SlotState::off && !box->isGenerator())
        graphChanged (true);

    return true;
}

bool Engine::endsPathWhenOff (Id id) const
{
    const auto* box = findBox (id);
    return box != nullptr && box->endsPathWhenOff();
}

bool Engine::isCutOff (Id id) const
{
    const auto place = structure.findSlot (id);

    if (!place.has_value())
        return false;

    const auto* bus = structure.findBus (place->bus);

    if (bus == nullptr)
        return false;

    for (const auto& slot : bus->slots)
    {
        if (slot.id == id)
            return false;

        if (getSlotState (slot.id) == SlotState::off && endsPathWhenOff (slot.id))
            return true;
    }

    return false;
}

void Engine::setStateFades (int muteMs, int bypassMs) noexcept
{
    stateFades.muteMs.store (std::max (0, muteMs));
    stateFades.bypassMs.store (std::max (0, bypassMs));
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
