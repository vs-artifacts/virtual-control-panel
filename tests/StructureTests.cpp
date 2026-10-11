#include <PluginProcessor.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("The structure's edits keep IDs and number slots by position", "[structure]")
{
    auto structure = Structure::simple ({ "a", "b" }, { "x", "y" });
    const auto bus1 = structure.buses[0].id;
    const auto genA = structure.generators[0].id;
    const auto x = structure.buses[0].slots[0].id;
    const auto y = structure.buses[0].slots[1].id;

    CHECK (structure.labelOf (genA) == "GEN 1");
    CHECK (structure.labelOf (bus1) == "BUS 1");
    CHECK (structure.labelOf (x) == "1.1");
    CHECK (structure.labelOf (y) == "1.2");
    CHECK (structure.labelOf (Structure::masterId) == "BUS X");
    CHECK (structure.findGenerator (genA)->buses == std::vector<Structure::Id> { bus1 });

    SECTION ("moving a slot renumbers, but its ID stays")
    {
        REQUIRE (structure.moveSlot (x, bus1, 1));
        CHECK (structure.labelOf (x) == "1.2");
        CHECK (structure.labelOf (y) == "1.1");

        REQUIRE (structure.moveSlot (x, Structure::masterId, 0));
        CHECK (structure.labelOf (x) == "X.1");
        CHECK_FALSE (structure.moveSlot (x, 999, 0));
        CHECK_FALSE (structure.moveSlot (genA, bus1, 0)); // generators aren't a bus's slots
    }

    SECTION ("sub-buses follow their parent, and only one level deep")
    {
        const auto bus2 = *structure.addBus (Structure::none);
        const auto sub1 = *structure.addBus (bus1);
        const auto sub2 = *structure.addBus (bus1);

        REQUIRE (structure.buses.size() == 4);
        CHECK (structure.buses[1].id == sub1);
        CHECK (structure.buses[2].id == sub2);
        CHECK (structure.buses[3].id == bus2);
        CHECK (structure.labelOf (sub2) == "BUS 3");
        CHECK_FALSE (structure.addBus (sub1).has_value());
        CHECK_FALSE (structure.addBus (Structure::masterId).has_value());

        const auto inSub = *structure.addSlot (sub1, 0, "z");
        REQUIRE (structure.setGeneratorBus (genA, sub1, true));
        CHECK (structure.slotsIn (bus1) == std::vector<Structure::Id> { x, y, inSub });

        // Removing a bus takes its sub-buses, their slots, and the generators' routes to them
        REQUIRE (structure.removeBus (bus1));
        REQUIRE (structure.buses.size() == 1);
        CHECK (structure.buses[0].id == bus2);
        CHECK_FALSE (structure.findSlot (inSub).has_value());
        CHECK (structure.findGenerator (genA)->buses.empty());
    }

    SECTION ("IDs are never reused")
    {
        REQUIRE (structure.removeSlot (y));
        const auto next = *structure.addSlot (bus1, 5, "y");
        CHECK (next != y);
        CHECK (next > y);
        CHECK (structure.labelOf (next) == "1.2");
    }

    SECTION ("edits that don't fit are refused")
    {
        CHECK_FALSE (structure.addSlot (bus1, 0, "not a key!").has_value());
        CHECK_FALSE (structure.addSlot (999, 0, "x").has_value());
        CHECK_FALSE (structure.removeBus (Structure::masterId));
        CHECK_FALSE (structure.setGeneratorBus (genA, Structure::masterId, true));

        for (int i = static_cast<int> (structure.buses[0].slots.size()); i < Structure::maxSlotsPerBus; ++i)
            REQUIRE (structure.addSlot (bus1, 0, "x").has_value());

        CHECK_FALSE (structure.addSlot (bus1, 0, "x").has_value());
    }
}

TEST_CASE ("The structure saves and refuses malformed saved data", "[structure]")
{
    auto structure = Structure::simple ({ "a" }, { "x" });
    const auto sub = *structure.addBus (structure.buses[0].id);
    structure.addSlot (sub, 0, "y");
    structure.addSlot (Structure::masterId, 0, "");
    structure.setGeneratorBus (structure.generators[0].id, sub, true);

    const auto xml = structure.toXml();
    const auto restored = Structure::fromXml (*xml);
    REQUIRE (restored.has_value());
    CHECK (restored->toXml()->isEquivalentTo (xml.get(), false));
    CHECK (restored->nextId == structure.nextId);

    const auto refused = [] (const char* text) {
        const auto parsed = juce::parseXML (text);
        return parsed != nullptr && !Structure::fromXml (*parsed).has_value();
    };

    CHECK (refused (R"(<Structure version="2"/>)"));
    CHECK (refused (R"(<Structure version="1"><Bus id="2" parent="0"/><Bus id="2" parent="0"/></Structure>)"));
    CHECK (refused (R"(<Structure version="1"><Bus id="1" parent="0"/></Structure>)"));
    CHECK (refused (R"(<Structure version="1"><Bus id="3" parent="2"/></Structure>)"));
    CHECK (refused (R"(<Structure version="1"><Bus id="2" parent="0"/><Bus id="3" parent="2"/><Bus id="4" parent="3"/></Structure>)"));
    CHECK (refused (R"(<Structure version="1"><Bus id="2" parent="0"><Slot id="3" plugin="../x"/></Bus></Structure>)"));
    CHECK (refused (R"(<Structure version="1"><Generator id="2" plugin="a" buses="9"/></Structure>)"));
    CHECK (refused (R"(<Structure version="1"><Generator id="2" plugin="a" buses="3x"/><Bus id="3" parent="0"/></Structure>)"));

    // A sub-bus must follow its parent: one placed after another bus is refused
    CHECK (refused (R"(<Structure version="1"><Bus id="2" parent="0"/><Bus id="3" parent="0"/><Bus id="4" parent="2"/></Structure>)"));

    const auto fine = juce::parseXML (R"(<Structure version="1" nextId="1"><Generator id="7" plugin="a" buses="2"/><Bus id="2" parent="0"/></Structure>)");
    const auto parsed = Structure::fromXml (*fine);
    REQUIRE (parsed.has_value());
    CHECK (parsed->nextId == 8);
}

TEST_CASE ("The engine wires slots in sequence, buses into the master, sub-buses into their parent's end", "[engine]")
{
    // Generators that can't load still have their box in the graph
    auto catalog = PluginCatalog::standard();
    catalog.entries.push_back ({ "missing", "Missing", juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no-such-plugin.vst3"), true, true });

    Engine engine (catalog);
    auto structure = Structure::simple ({ "missing", "missing" }, { "", "" });
    const auto bus1 = structure.buses[0].id;
    const auto bus2 = *structure.addBus (Structure::none);
    const auto sub = *structure.addBus (bus1);
    const auto genA = structure.generators[0].id;
    const auto genB = structure.generators[1].id;
    const auto a = structure.buses[0].slots[0].id;
    const auto b = structure.buses[0].slots[1].id;
    const auto inSub = *structure.addSlot (sub, 0, "");
    const auto inBus2 = *structure.addSlot (bus2, 0, "");
    const auto inMaster = *structure.addSlot (Structure::masterId, 0, "");
    structure.setGeneratorBus (genB, bus1, false);
    structure.setGeneratorBus (genB, bus2, true);
    structure.setGeneratorBus (genB, sub, true);
    engine.build (structure);

    CHECK (engine.feeds (genA, a));
    CHECK (engine.feeds (a, b));
    CHECK_FALSE (engine.feeds (b, a));
    CHECK (engine.feeds (b, inMaster));
    CHECK (engine.feeds (inBus2, inMaster));
    CHECK_FALSE (engine.feeds (genA, inBus2));
    CHECK (engine.feeds (genB, inBus2));
    CHECK_FALSE (engine.feeds (genB, a));

    // The sub-bus joins its parent after the parent's slots
    CHECK (engine.feeds (inSub, inMaster));
    CHECK_FALSE (engine.feeds (inSub, b));
    CHECK (engine.feeds (genB, inSub));

    SECTION ("moves rewire, and the slot keeps its ID")
    {
        // Not running: the change applies at once
        REQUIRE (engine.moveSlot (a, bus2, 1));
        CHECK_FALSE (engine.isChangeWaiting());
        CHECK (engine.feeds (inBus2, a));
        CHECK (engine.feeds (genB, a));
        CHECK_FALSE (engine.feeds (genA, a));
        CHECK (engine.getSlotLabel (a) == "SLOT 3.2");
    }

    SECTION ("removing a bus tells about each slot that goes")
    {
        std::vector<Structure::Id> removed;
        engine.onSlotRemoving = [&removed] (Structure::Id id, const std::vector<ExposedParameter>&) { removed.push_back (id); };
        auto changes = 0;
        engine.onStructureChanged = [&changes] { ++changes; };

        REQUIRE (engine.removeBus (bus1));
        CHECK (removed == std::vector<Structure::Id> { a, b, inSub });
        CHECK (changes == 1);
        CHECK (engine.feeds (genB, inBus2));
        CHECK (engine.getSlotLabel (inBus2) == "SLOT 1.1");
    }

    SECTION ("only listed plugins can be added")
    {
        CHECK_FALSE (engine.addSlot (bus1, 0, "missing").has_value()); // a generator
        CHECK_FALSE (engine.addSlot (bus1, 0, "sylenth1").has_value());
        CHECK_FALSE (engine.addGenerator ("sylenth1", 0).has_value()); // not offered
        CHECK_FALSE (engine.addGenerator ("unknown", 0).has_value());
        CHECK (engine.addSlot (bus1, 0, "").has_value());
    }

    SECTION ("the structure saves with the engine; another version is refused")
    {
        const auto saved = engine.createStateXml();
        Engine restored (catalog);
        REQUIRE (restored.restoreStateFromXml (*saved));
        CHECK (restored.getStructure().toXml()->isEquivalentTo (engine.getStructure().toXml().get(), false));
        CHECK (restored.feeds (inSub, inMaster));

        saved->setAttribute ("version", 1);
        CHECK_FALSE (restored.restoreStateFromXml (*saved));
    }
}
