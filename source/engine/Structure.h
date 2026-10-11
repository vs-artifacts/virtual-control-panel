#pragma once

#include <juce_core/juce_core.h>

// The panel's routing as data: the generator stage, buses (a bus with a parent is a sub-bus) and the slots in them.
// Every generator, bus and slot has a permanent ID, never reused; positions give the names people see (GEN 1, BUS 2,
// slot 2.3). Each bus also has a fixed pre stage (b.0) and post stage (b.X), empty for now, which aren't listed here.
// Routing: a generator feeds b.0 of each of its buses; a bus's slots run in sequence; a bus outputs to X.0 (the
// master's pre stage), a sub-bus into its parent's b.X.
struct Structure
{
    using Id = int;
    static constexpr Id none = 0;
    static constexpr Id masterId = 1;

    struct Slot
    {
        Id id = none;
        juce::String plugin; // a plugin list key; empty for an empty slot
    };

    struct Generator
    {
        Id id = none;
        juce::String plugin;
        std::vector<Id> buses; // the buses it feeds
    };

    struct Bus
    {
        Id id = none;
        Id parent = none; // a sub-bus's parent: always a bus without a parent
        std::vector<Slot> slots;
    };

    // Where a slot is: its bus and position (0: the first after b.0)
    struct Place
    {
        Id bus = none;
        int index = 0;
    };

    std::vector<Generator> generators;
    std::vector<Bus> buses; // display order: each bus followed by its sub-buses
    Bus master { masterId, none, {} };
    Id nextId = masterId + 1;

    static constexpr int maxGenerators = 32;
    static constexpr int maxBuses = 32;
    static constexpr int maxSlotsPerBus = 32;
    static constexpr int maxPluginKeyLength = 64;
    static constexpr Id maxId = 1000000;

    // Generators, all feeding BUS 1, which holds the effects in sequence
    static Structure simple (const juce::StringArray& generatorPlugins, const juce::StringArray& fxPlugins);

    // Edits. Each returns false (or nothing) and changes nothing if refused. Indexes beyond the end mean the end.
    std::optional<Id> addGenerator (const juce::String& plugin, int index);
    std::optional<Id> addSlot (Id bus, int index, const juce::String& plugin);
    bool removeSlot (Id); // a generator or a bus's slot
    bool moveGenerator (Id, int index);
    bool moveSlot (Id, Id bus, int index); // index: where it lands, counted without the slot itself
    std::optional<Id> addBus (Id parent); // none: a top-level bus, last; else a sub-bus, after its parent's others
    bool removeBus (Id); // with its slots and sub-buses; generators stop feeding them
    bool setGeneratorBus (Id generator, Id bus, bool feeds);

    const Generator* findGenerator (Id) const;
    const Bus* findBus (Id) const; // the master included
    std::optional<Place> findSlot (Id) const; // a bus's slot (not a generator)
    bool isGenerator (Id id) const { return findGenerator (id) != nullptr; }

    // Every generator, then every bus's slots (the master's last), in display order
    std::vector<Id> slotIds() const;

    // The slots of a bus and its sub-buses
    std::vector<Id> slotsIn (Id bus) const;

    // "GEN 1", "1.2", "X.1" for slots; "BUS 2", "BUS X" for buses; empty if unknown
    juce::String labelOf (Id) const;
    int busNumber (Id bus) const; // 1 on, by display order; 0 if not a bus or the master

    std::unique_ptr<juce::XmlElement> toXml() const;

    // Saved data is untrusted: anything malformed (or from another version) gives nothing
    static std::optional<Structure> fromXml (const juce::XmlElement&);

    static constexpr auto xmlTag = "Structure";
    static constexpr int version = 1;

private:
    Bus* findBusToEdit (Id);
    Id takeId() { return nextId++; }
    bool canTakeId() const { return nextId < maxId; }
};
