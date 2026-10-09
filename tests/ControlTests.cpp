#include "helpers/ParameterHost.h"
#include <HostControls.h>
#include <bridge/ControlSession.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("A control session", "[control]")
{
    HostLog log;
    ParameterHost plugin;
    plugin.addListener (&log);
    auto& a = plugin.addFloat ("a");
    auto& b = plugin.addFloat ("b");

    auto session = std::make_unique<ControlSession> (std::vector<ControlSession::Parameter> {
        { "FX 1", "Test plugin", "A", "Param 1", &a, &a },
        { "FX 2", "Test plugin", "B", {}, &b, &b },
    });

    const auto set = [&] (const char* json) { session->applySet (juce::JSON::parse (json)); };
    const auto gesture = [&] (const char* json) { session->applyGesture (juce::JSON::parse (json)); };

    SECTION ("ignores the page until it has asked for the catalog")
    {
        set (R"([{ "h": 0, "v": 0.9 }])");
        gesture (R"({ "h": 0, "active": true })");
        b.setValueNotifyingHost (0.1f);

        CHECK_THAT (a.getValue(), WithinAbs (0.5, 1e-6));
        CHECK (log.gestures.empty());
        CHECK (session->poll().isVoid());
    }

    SECTION ("the catalog lists every parameter with its handle, names, value, display text and default")
    {
        b.setValueNotifyingHost (0.25f);
        const auto catalog = session->catalog();
        REQUIRE (catalog.size() == 2);
        CHECK (static_cast<int> (catalog[0]["h"]) == 0);
        CHECK (catalog[0]["slot"] == "FX 1");
        CHECK (catalog[0]["plugin"] == "Test plugin");
        CHECK (catalog[0]["name"] == "A");
        CHECK (catalog[0]["host"] == "Param 1");
        CHECK_THAT (static_cast<double> (catalog[0]["v"]), WithinAbs (0.5, 1e-6));
        CHECK (catalog[0]["t"] == a.getCurrentValueAsText());
        CHECK_THAT (static_cast<double> (catalog[0]["d"]), WithinAbs (0.5, 1e-6));
        CHECK (static_cast<int> (catalog[1]["h"]) == 1);
        CHECK (catalog[1]["host"] == "");
        CHECK_THAT (static_cast<double> (catalog[1]["v"]), WithinAbs (0.25, 1e-6));
        CHECK_THAT (static_cast<double> (catalog[1]["d"]), WithinAbs (0.5, 1e-6));
    }

    SECTION ("polls report only what changed since the last poll")
    {
        session->catalog();
        CHECK (session->poll().isVoid());

        b.setValueNotifyingHost (0.25f);
        const auto changes = session->poll();
        REQUIRE (changes.size() == 1);
        CHECK (static_cast<int> (changes[0]["h"]) == 1);
        CHECK_THAT (static_cast<double> (changes[0]["v"]), WithinAbs (0.25, 1e-6));
        CHECK (changes[0]["t"] == b.getCurrentValueAsText());

        CHECK (session->poll().isVoid());
    }

    SECTION ("sets are written through, clamped to the parameter's range")
    {
        session->catalog();
        set (R"([{ "h": 0, "v": 0.75 }, { "h": 1, "v": 7 }])");
        CHECK_THAT (a.getValue(), WithinAbs (0.75, 1e-6));
        CHECK_THAT (b.getValue(), WithinAbs (1.0, 1e-6));
        CHECK (log.changes.size() == 2);

        set (R"([{ "h": 1, "v": -3 }])");
        CHECK_THAT (b.getValue(), WithinAbs (0.0, 1e-6));
    }

    SECTION ("malformed sets and unknown handles are ignored")
    {
        session->catalog();

        for (const auto* json : { R"({ "h": 0, "v": 0.9 })", R"([{ "h": 2, "v": 0.9 }])", R"([{ "h": -1, "v": 0.9 }])", R"([{ "h": "0", "v": 0.9 }])", R"([{ "h": 0.5, "v": 0.9 }])", R"([{ "h": 0, "v": "0.9" }])", R"([{ "h": 0, "v": true }])", R"([{ "h": 0, "v": null }])", R"([{ "h": 0, "v": 1e999 }])", R"([{ "h": 0 }])", "[0.9]", "[[0, 0.9]]", "null", "\"0.9\"" })
        {
            INFO (json);
            set (json);
            CHECK_THAT (a.getValue(), WithinAbs (0.5, 1e-6));
        }

        auto* nan = new juce::DynamicObject();
        nan->setProperty ("h", 0);
        nan->setProperty ("v", std::numeric_limits<double>::quiet_NaN());
        session->applySet (juce::Array<juce::var> { nan });
        CHECK_THAT (a.getValue(), WithinAbs (0.5, 1e-6));

        CHECK (log.changes.empty());
    }

    SECTION ("an oversized batch is ignored as a whole")
    {
        session->catalog();
        juce::Array<juce::var> batch;

        for (int i = 0; i <= ControlSession::maxBatchSize; ++i)
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty ("h", 0);
            entry->setProperty ("v", 0.9);
            batch.add (entry);
        }

        session->applySet (batch);
        CHECK_THAT (a.getValue(), WithinAbs (0.5, 1e-6));
    }

    SECTION ("gestures reach the parameter, without repeats")
    {
        session->catalog();
        gesture (R"({ "h": 0, "active": true })");
        gesture (R"({ "h": 0, "active": true })");
        gesture (R"({ "h": 0, "active": 1 })");
        gesture (R"({ "h": 5, "active": false })");
        gesture (R"({ "h": 0, "active": false })");
        gesture (R"({ "h": 0, "active": false })");

        CHECK (log.gestures == std::vector<std::pair<int, bool>> { { 0, true }, { 0, false } });
    }

    SECTION ("open gestures end when the page restarts or the session ends")
    {
        session->catalog();
        gesture (R"({ "h": 0, "active": true })");
        session->catalog();
        gesture (R"({ "h": 1, "active": true })");
        session.reset();

        CHECK (log.gestures == std::vector<std::pair<int, bool>> { { 0, true }, { 0, false }, { 1, true }, { 1, false } });
    }

    SECTION ("handles lead to the parameter their changes go to")
    {
        CHECK (session->findWriteTarget (0) == nullptr);

        session->catalog();
        CHECK (session->findWriteTarget (0) == &a);
        CHECK (session->findWriteTarget (1) == &b);

        for (const auto& handle : { juce::var (2), juce::var (-1), juce::var ("0"), juce::var (0.5), juce::var() })
            CHECK (session->findWriteTarget (handle) == nullptr);
    }

    session.reset();
    plugin.removeListener (&log);
}

TEST_CASE ("Host controls", "[control]")
{
    HostLog log;
    ParameterHost processor;
    HostControls controls (processor);
    processor.addListener (&log);

    ParameterHost hostedPlugin;
    auto& target = hostedPlugin.addFloat ("target");

    SECTION ("are a fixed pool of generic, automatable parameters with permanent IDs")
    {
        const auto& parameters = processor.getParameters();
        REQUIRE (parameters.size() == HostControls::count);
        CHECK (HostControls::count == 256);

        for (int i = 0; i < parameters.size(); ++i)
        {
            const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameters[i]);
            REQUIRE (parameter != nullptr);
            CHECK (parameter->getParameterID() == "param" + juce::String (i + 1));
            CHECK (parameter->getName (64) == "Param " + juce::String (i + 1));
            CHECK (parameter->isAutomatable());
        }
    }

    SECTION ("an unbound control just keeps its value")
    {
        controls[3].setValue (0.3f);
        CHECK_THAT (controls[3].getValue(), WithinAbs (0.3, 1e-6));
        CHECK_THAT (target.getValue(), WithinAbs (0.5, 1e-6));
        CHECK (controls[3].getCurrentValueAsText().isNotEmpty());
    }

    SECTION ("a bound control takes on its target and passes the host's changes on")
    {
        target.setValueNotifyingHost (0.8f);
        controls.bind (0, target);
        CHECK_THAT (controls[0].getValue(), WithinAbs (0.8, 1e-6));
        CHECK_THAT (controls[0].getDefaultValue(), WithinAbs (target.getDefaultValue(), 1e-6));

        controls[0].setValue (0.2f);
        CHECK_THAT (target.getValue(), WithinAbs (0.2, 1e-6));
        CHECK (controls[0].getText (0.2f, 64) == target.getText (0.2f, 64));
    }

    SECTION ("changes made elsewhere are reported to the host once")
    {
        controls.bind (0, target);
        target.setValueNotifyingHost (0.6f);
        controls.reportTargetChanges();
        controls.reportTargetChanges();

        REQUIRE (log.changes.size() == 1);
        CHECK (log.changes[0].first == 0);
        CHECK_THAT (log.changes[0].second, WithinAbs (0.6, 1e-6));
        CHECK_THAT (controls[0].getValue(), WithinAbs (0.6, 1e-6));
    }

    SECTION ("syncing after a state restore doesn't tell the host")
    {
        controls.bind (0, target);
        target.setValue (0.1f);
        controls.syncWithoutNotifying();
        controls.reportTargetChanges();

        CHECK_THAT (controls[0].getValue(), WithinAbs (0.1, 1e-6));
        CHECK (log.changes.empty());
    }

    SECTION ("a renamed control tells the host once per change, without control characters, within the length limit")
    {
        controls.setName (4, "SLOT 1.1: Serum 2 - Cutoff");
        controls.setName (4, "SLOT 1.1: Serum 2 - Cutoff");
        CHECK (controls[4].getName (128) == "SLOT 1.1: Serum 2 - Cutoff");
        CHECK (controls[4].getName (8) == "SLOT 1.1");
        CHECK (log.infoChanges == 1);

        controls.setName (4, juce::String ("  Bad\n\tname\x7f") + juce::String::repeatedString ("x", 300));
        CHECK (controls[4].getName (1000) == "Badname" + juce::String::repeatedString ("x", HostControls::maxNameLength - 7));
        CHECK (log.infoChanges == 2);
        CHECK (controls[5].getName (64) == "Param 6");
    }

    processor.removeListener (&log);
}
