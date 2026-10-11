#include "Structure.h"

namespace
{
    int clampIndex (int index, size_t size)
    {
        return juce::jlimit (0, static_cast<int> (size), index);
    }

    template <typename Item>
    auto findById (std::vector<Item>& items, Structure::Id id)
    {
        return std::find_if (items.begin(), items.end(), [id] (const auto& item) { return item.id == id; });
    }

    template <typename Item>
    auto findById (const std::vector<Item>& items, Structure::Id id)
    {
        return std::find_if (items.begin(), items.end(), [id] (const auto& item) { return item.id == id; });
    }

    bool isValidPluginKey (const juce::String& key)
    {
        return key.length() <= Structure::maxPluginKeyLength && key.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    }
}

Structure Structure::simple (const juce::StringArray& generatorPlugins, const juce::StringArray& fxPlugins)
{
    Structure structure;
    const auto bus = *structure.addBus (none);

    for (const auto& plugin : generatorPlugins)
        if (const auto id = structure.addGenerator (plugin, maxGenerators))
            structure.setGeneratorBus (*id, bus, true);

    for (const auto& plugin : fxPlugins)
        structure.addSlot (bus, maxSlotsPerBus, plugin);

    return structure;
}

std::optional<Structure::Id> Structure::addGenerator (const juce::String& plugin, int index)
{
    if (generators.size() >= maxGenerators || !canTakeId() || !isValidPluginKey (plugin))
        return std::nullopt;

    const auto id = takeId();
    generators.insert (generators.begin() + clampIndex (index, generators.size()), { id, plugin, {} });
    return id;
}

std::optional<Structure::Id> Structure::addSlot (Id busID, int index, const juce::String& plugin)
{
    auto* bus = findBusToEdit (busID);

    if (bus == nullptr || bus->slots.size() >= maxSlotsPerBus || !canTakeId() || !isValidPluginKey (plugin))
        return std::nullopt;

    const auto id = takeId();
    bus->slots.insert (bus->slots.begin() + clampIndex (index, bus->slots.size()), { id, plugin });
    return id;
}

bool Structure::removeSlot (Id id)
{
    if (const auto generator = findById (generators, id); generator != generators.end())
    {
        generators.erase (generator);
        return true;
    }

    const auto place = findSlot (id);

    if (!place.has_value())
        return false;

    auto& slots = findBusToEdit (place->bus)->slots;
    slots.erase (slots.begin() + place->index);
    return true;
}

bool Structure::moveGenerator (Id id, int index)
{
    const auto generator = findById (generators, id);

    if (generator == generators.end())
        return false;

    auto moved = *generator;
    generators.erase (generator);
    generators.insert (generators.begin() + clampIndex (index, generators.size()), std::move (moved));
    return true;
}

bool Structure::moveSlot (Id id, Id busID, int index)
{
    const auto place = findSlot (id);
    auto* destination = findBusToEdit (busID);

    if (!place.has_value() || destination == nullptr || (place->bus != busID && destination->slots.size() >= maxSlotsPerBus))
        return false;

    auto& from = findBusToEdit (place->bus)->slots;
    auto moved = from[static_cast<size_t> (place->index)];
    from.erase (from.begin() + place->index);
    destination->slots.insert (destination->slots.begin() + clampIndex (index, destination->slots.size()), std::move (moved));
    return true;
}

std::optional<Structure::Id> Structure::addBus (Id parent)
{
    if (buses.size() >= maxBuses || !canTakeId())
        return std::nullopt;

    auto position = buses.end();

    if (parent != none)
    {
        const auto parentBus = findById (buses, parent);

        if (parentBus == buses.end() || parentBus->parent != none)
            return std::nullopt;

        position = std::find_if (parentBus + 1, buses.end(), [parent] (const Bus& bus) { return bus.parent != parent; });
    }

    const auto id = takeId();
    buses.insert (position, { id, parent, {} });
    return id;
}

bool Structure::removeBus (Id id)
{
    if (findById (buses, id) == buses.end())
        return false;

    const auto goes = [id] (const Bus& bus) { return bus.id == id || bus.parent == id; };

    for (auto& generator : generators)
        std::erase_if (generator.buses, [&] (Id fed) {
            const auto bus = findById (buses, fed);
            return bus != buses.end() && goes (*bus);
        });

    std::erase_if (buses, goes);
    return true;
}

bool Structure::setGeneratorBus (Id generatorID, Id busID, bool feeds)
{
    const auto generator = findById (generators, generatorID);

    if (generator == generators.end() || findById (buses, busID) == buses.end())
        return false;

    auto& fed = generator->buses;
    const auto existing = std::find (fed.begin(), fed.end(), busID);

    if (feeds && existing == fed.end())
        fed.push_back (busID);
    else if (!feeds && existing != fed.end())
        fed.erase (existing);

    return true;
}

const Structure::Generator* Structure::findGenerator (Id id) const
{
    const auto generator = findById (generators, id);
    return generator != generators.end() ? &*generator : nullptr;
}

const Structure::Bus* Structure::findBus (Id id) const
{
    if (id == masterId)
        return &master;

    const auto bus = findById (buses, id);
    return bus != buses.end() ? &*bus : nullptr;
}

Structure::Bus* Structure::findBusToEdit (Id id)
{
    return const_cast<Bus*> (std::as_const (*this).findBus (id));
}

std::optional<Structure::Place> Structure::findSlot (Id id) const
{
    const auto search = [id] (const Bus& bus) -> std::optional<Place> {
        if (const auto slot = findById (bus.slots, id); slot != bus.slots.end())
            return Place { bus.id, static_cast<int> (slot - bus.slots.begin()) };

        return std::nullopt;
    };

    for (const auto& bus : buses)
        if (const auto place = search (bus))
            return place;

    return search (master);
}

std::vector<Structure::Id> Structure::slotIds() const
{
    std::vector<Id> ids;

    for (const auto& generator : generators)
        ids.push_back (generator.id);

    for (const auto& bus : buses)
        for (const auto& slot : bus.slots)
            ids.push_back (slot.id);

    for (const auto& slot : master.slots)
        ids.push_back (slot.id);

    return ids;
}

std::vector<Structure::Id> Structure::slotsIn (Id busID) const
{
    std::vector<Id> ids;

    for (const auto& bus : buses)
        if (bus.id == busID || bus.parent == busID)
            for (const auto& slot : bus.slots)
                ids.push_back (slot.id);

    if (busID == masterId)
        for (const auto& slot : master.slots)
            ids.push_back (slot.id);

    return ids;
}

int Structure::busNumber (Id id) const
{
    const auto bus = findById (buses, id);
    return bus != buses.end() ? static_cast<int> (bus - buses.begin()) + 1 : 0;
}

juce::String Structure::labelOf (Id id) const
{
    if (const auto generator = findById (generators, id); generator != generators.end())
        return "GEN " + juce::String (generator - generators.begin() + 1);

    if (id == masterId)
        return "BUS X";

    if (const auto number = busNumber (id); number > 0)
        return "BUS " + juce::String (number);

    if (const auto place = findSlot (id))
        return (place->bus == masterId ? juce::String ("X") : juce::String (busNumber (place->bus))) + "." + juce::String (place->index + 1);

    return {};
}

std::unique_ptr<juce::XmlElement> Structure::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (xmlTag);
    xml->setAttribute ("version", version);
    xml->setAttribute ("nextId", nextId);

    const auto addSlots = [] (juce::XmlElement& parent, const Bus& bus) {
        for (const auto& slot : bus.slots)
        {
            auto* entry = parent.createNewChildElement ("Slot");
            entry->setAttribute ("id", slot.id);
            entry->setAttribute ("plugin", slot.plugin);
        }
    };

    for (const auto& generator : generators)
    {
        auto* entry = xml->createNewChildElement ("Generator");
        entry->setAttribute ("id", generator.id);
        entry->setAttribute ("plugin", generator.plugin);

        juce::StringArray fed;

        for (const auto bus : generator.buses)
            fed.add (juce::String (bus));

        entry->setAttribute ("buses", fed.joinIntoString (" "));
    }

    for (const auto& bus : buses)
    {
        auto* entry = xml->createNewChildElement ("Bus");
        entry->setAttribute ("id", bus.id);
        entry->setAttribute ("parent", bus.parent);
        addSlots (*entry, bus);
    }

    addSlots (*xml->createNewChildElement ("Master"), master);
    return xml;
}

std::optional<Structure> Structure::fromXml (const juce::XmlElement& xml)
{
    if (!xml.hasTagName (xmlTag) || xml.getIntAttribute ("version") != version)
        return std::nullopt;

    Structure structure;
    std::set<Id> taken { masterId };
    auto highest = masterId;

    const auto takeSavedId = [&] (const juce::XmlElement& entry) -> std::optional<Id> {
        const auto id = entry.getIntAttribute ("id");

        if (id <= masterId || id >= maxId || !taken.insert (id).second)
            return std::nullopt;

        highest = std::max (highest, id);
        return id;
    };

    const auto readSlots = [&] (const juce::XmlElement& parent, Bus& bus) {
        for (auto* entry : parent.getChildWithTagNameIterator ("Slot"))
        {
            const auto id = takeSavedId (*entry);
            const auto plugin = entry->getStringAttribute ("plugin");

            if (!id.has_value() || !isValidPluginKey (plugin) || bus.slots.size() >= maxSlotsPerBus)
                return false;

            bus.slots.push_back ({ *id, plugin });
        }

        return true;
    };

    // Generators name their buses, which come later in the file: checked once all are read
    std::vector<juce::StringArray> fedBuses;

    for (auto* entry : xml.getChildIterator())
    {
        if (entry->hasTagName ("Generator"))
        {
            const auto id = takeSavedId (*entry);
            const auto plugin = entry->getStringAttribute ("plugin");

            if (!id.has_value() || !isValidPluginKey (plugin) || structure.generators.size() >= maxGenerators)
                return std::nullopt;

            structure.generators.push_back ({ *id, plugin, {} });
            fedBuses.push_back (juce::StringArray::fromTokens (entry->getStringAttribute ("buses"), " ", {}));
        }
        else if (entry->hasTagName ("Bus"))
        {
            const auto id = takeSavedId (*entry);
            const auto parent = entry->getIntAttribute ("parent");

            if (!id.has_value() || structure.buses.size() >= maxBuses)
                return std::nullopt;

            // A sub-bus follows its parent (a top-level bus) or its parent's other sub-buses, so there are no loops
            if (parent != none)
            {
                if (structure.buses.empty())
                    return std::nullopt;

                const auto& previous = structure.buses.back();

                if ((previous.id != parent || previous.parent != none) && previous.parent != parent)
                    return std::nullopt;
            }

            Bus bus { *id, parent, {} };

            if (!readSlots (*entry, bus))
                return std::nullopt;

            structure.buses.push_back (std::move (bus));
        }
        else if (entry->hasTagName ("Master"))
        {
            if (!structure.master.slots.empty() || !readSlots (*entry, structure.master))
                return std::nullopt;
        }
    }

    for (size_t i = 0; i < structure.generators.size(); ++i)
    {
        for (const auto& token : fedBuses[i])
        {
            const auto bus = token.getIntValue();

            if (token != juce::String (bus) || findById (structure.buses, bus) == structure.buses.end())
                return std::nullopt;

            if (!structure.setGeneratorBus (structure.generators[i].id, bus, true))
                return std::nullopt;
        }
    }

    structure.nextId = juce::jlimit (highest + 1, maxId, xml.getIntAttribute ("nextId"));
    return structure;
}
