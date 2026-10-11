#include <KeyForwarder.h>
#include <PluginSettings.h>
#include <catch2/catch_test_macros.hpp>

namespace
{
    struct Post
    {
        std::uint32_t scanCode;
        bool down, isRepeat;

        bool operator== (const Post&) const = default;
    };

    constexpr std::uint32_t scanZ = 0x2C, scanSpace = 0x39;

    // Records what would be sent to the host, with a keyboard whose modifier keys the test controls
    struct FakeHost
    {
        KeyForwarder makeForwarder (const juce::String& chords = KeyForwarder::defaultChords)
        {
            return KeyForwarder (
                chords,
                [this] (std::uint32_t scanCode, bool down, bool isRepeat) { posts.push_back ({ scanCode, down, isRepeat }); },
                [this] { return modifiers; });
        }

        std::vector<Post> posts;
        KeyForwarder::Modifiers modifiers;
    };

    juce::var keyEvent (const juce::var& code, const juce::var& down, const juce::var& repeat = {})
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("code", code);
        object->setProperty ("down", down);

        if (!repeat.isVoid())
            object->setProperty ("repeat", repeat);

        return juce::var (object);
    }
}

TEST_CASE ("Forwarded keys", "[keys]")
{
    FakeHost host;
    auto keys = host.makeForwarder();

    SECTION ("allowed keys pass as presses and releases")
    {
        keys.handleKeyEvent (keyEvent ("KeyZ", true));
        keys.handleKeyEvent (keyEvent ("KeyZ", false));
        CHECK (host.posts == std::vector<Post> { { scanZ, true, false }, { scanZ, false, false } });
    }

    SECTION ("chords follow the physical modifier keys, not the page")
    {
        host.modifiers.ctrl = true;
        keys.handleKeyEvent (keyEvent ("KeyZ", true));
        keys.handleKeyEvent (keyEvent ("Space", true));
        CHECK (host.posts == std::vector<Post> { { scanSpace, true, false } });
    }

    SECTION ("nothing passes while Alt, Shift or the Windows key is held")
    {
        for (auto modifier : { &KeyForwarder::Modifiers::alt, &KeyForwarder::Modifiers::shift, &KeyForwarder::Modifiers::os })
        {
            host.modifiers = {};
            host.modifiers.*modifier = true;
            keys.handleKeyEvent (keyEvent ("Space", true));
        }

        CHECK (host.posts.empty());
    }

    SECTION ("a key that went down is always released, whatever the modifiers do meanwhile")
    {
        keys.handleKeyEvent (keyEvent ("Space", true));
        host.modifiers.alt = true;
        keys.handleKeyEvent (keyEvent ("Space", false));
        CHECK (host.posts == std::vector<Post> { { scanSpace, true, false }, { scanSpace, false, false } });
    }

    SECTION ("held keys repeat")
    {
        keys.handleKeyEvent (keyEvent ("KeyZ", true));
        keys.handleKeyEvent (keyEvent ("KeyZ", true));
        CHECK (host.posts == std::vector<Post> { { scanZ, true, false }, { scanZ, true, true } });
    }

    SECTION ("releases pass even for keys held since before the page had focus")
    {
        keys.handleKeyEvent (keyEvent ("Space", false));
        host.modifiers.shift = true;
        keys.handleKeyEvent (keyEvent ("KeyZ", false));
        CHECK (host.posts == std::vector<Post> { { scanSpace, false, false }, { scanZ, false, false } });
    }

    SECTION ("everything else is ignored")
    {
        for (const auto& event : { juce::var(), juce::var ("KeyZ"), keyEvent (5, true), keyEvent ("KeyZ", {}), keyEvent ("KeyZ", "yes"), keyEvent ("Escape", true), keyEvent ("Escape", false), keyEvent ("F4", true), keyEvent ("Ctrl+Space", true), keyEvent ("Ctrl+Space", false), keyEvent ("KeyZ ", true), keyEvent (juce::String::repeatedString ("KeyZ", 10000), true), keyEvent (juce::String::repeatedString ("KeyZ", 10000), false) })
            keys.handleKeyEvent (event);

        CHECK (host.posts.empty());
    }

    SECTION ("releasing everything, e.g. when the page loses focus")
    {
        keys.handleKeyEvent (keyEvent ("KeyZ", true));
        keys.handleKeyEvent (keyEvent ("Space", true));
        host.posts.clear();

        keys.releaseAll();
        CHECK (host.posts.size() == 2);

        for (const auto& post : host.posts)
            CHECK_FALSE (post.down);

        keys.releaseAll();
        CHECK (host.posts.size() == 2);
    }
}

TEST_CASE ("Forwarded key chords come from a list", "[keys]")
{
    FakeHost host;

    SECTION ("by default, the typing-keyboard keys plus Space and Ctrl+Space")
    {
        const auto chords = host.makeForwarder().getAllowedChords();
        CHECK (chords.size() == 45);

        for (const auto* chord : { "KeyQ", "KeyM", "Digit0", "BracketLeft", "Quote", "Slash", "Space", "Ctrl+Space" })
            CHECK (chords.contains (chord));

        CHECK_FALSE (chords.contains ("Ctrl+KeyS"));
    }

    SECTION ("a configured list keeps only chords it understands")
    {
        const auto chords = host.makeForwarder ("KeyA, Ctrl+KeyB ,Alt+KeyC,Bogus,Ctrl+Ctrl+KeyD,KeyA,,Escape").getAllowedChords();
        CHECK (chords == juce::StringArray { "KeyA", "Ctrl+KeyB" });
    }

    SECTION ("releases pass only for keys on the list")
    {
        auto keys = host.makeForwarder ("Ctrl+KeyB");
        keys.handleKeyEvent (keyEvent ("KeyA", false));
        keys.handleKeyEvent (keyEvent ("KeyB", false));
        CHECK (host.posts == std::vector<Post> { { 0x30, false, false } });
    }

    SECTION ("keys still held when the forwarder goes away are released")
    {
        {
            auto keys = host.makeForwarder();
            keys.handleKeyEvent (keyEvent ("KeyZ", true));
        }

        CHECK (host.posts == std::vector<Post> { { scanZ, true, false }, { scanZ, false, false } });
    }
}

TEST_CASE ("The key log names keys by their page code", "[keys]")
{
    CHECK (KeyForwarder::nameOf (0x2C) == "KeyZ");
    CHECK (KeyForwarder::nameOf (0x39) == "Space");
    CHECK (KeyForwarder::nameOf (0x99) == "0x99");
}

TEST_CASE ("Plugin settings persist in their file", "[settings]")
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("heartseeker-settings-test", ".settings");

    {
        PluginSettings settings (file);
        CHECK (settings.getKeepWebViewWarm());
        CHECK (settings.getForwardedKeys() == KeyForwarder::defaultChords);
        CHECK (settings.getResizeRendering() == EditorScale::Rendering::smooth);
        CHECK (juce::exactlyEqual (settings.getLastEditorScale(), 1.0));
        CHECK_FALSE (settings.getFlipAltResize());
        CHECK (settings.getResizeBlocksLanguageSwitch());
        CHECK (settings.getFlOutputCorrection());
        CHECK (settings.getRoundTripBlocks() == BayLink::autoRoundTrip);
        CHECK (settings.getRoundTripRealBlocks());
        CHECK (juce::exactlyEqual (settings.getLowestTempo(), 0.0));
        CHECK (settings.getStoppedShrink() == BayLink::StoppedShrink::whenQuiet);
        CHECK (settings.getQuietLevelDb() == BayLink::defaultQuietLevelDb);
        CHECK (settings.getQuietMs() == BayLink::defaultQuietMs);
        for (const auto fade : BayLink::fades)
            CHECK (settings.getFadeMs (fade) == BayLink::defaultFadeMs (fade));

        CHECK (settings.getLatencySettleMs() == BayLink::defaultSettleMs);
        CHECK (settings.getTailCutReleaseMs() == BayLink::defaultTailCutReleaseMs);
        CHECK (settings.getEditorPlacement() == EditorPlacement::Monitor::hostPlaced);
        CHECK (settings.getAttachedPlacement() == EditorPlacement::Attached::workspace);
        CHECK (settings.getDoubleClickMs() == PluginSettings::defaultDoubleClickMs);
        CHECK_FALSE (settings.getDoubleClickSets());

        settings.setKeepWebViewWarm (false);
        settings.setResizeRendering (EditorScale::Rendering::sharp);
        settings.setFlipAltResize (true);
        settings.setResizeBlocksLanguageSwitch (false);
        settings.setLastEditorScale (99.0);
        settings.setFlOutputCorrection (false);
        settings.setRoundTripBlocks (7);
        settings.setRoundTripRealBlocks (false);
        settings.setLowestTempo (5000.0);
        settings.setStoppedShrink (BayLink::StoppedShrink::always);
        settings.setQuietLevelDb (-500);
        settings.setQuietMs (99999);
        settings.setFadeMs (BayLink::Fade::roundTrip, 1000);
        settings.setFadeMs (BayLink::Fade::bypass, 50);
        settings.setFadeMs (BayLink::Fade::off, 5000);
        settings.setLatencySettleMs (5000);
        settings.setTailCutReleaseMs (5000);
        settings.setEditorPlacement (EditorPlacement::Monitor::mouse);
        settings.setAttachedPlacement (EditorPlacement::Attached::hostMonitor);
        settings.setDoubleClickMs (5000);
        settings.setDoubleClickSets (true);
    }

    PluginSettings reloaded (file);
    CHECK_FALSE (reloaded.getKeepWebViewWarm());
    CHECK (reloaded.getResizeRendering() == EditorScale::Rendering::sharp);
    CHECK (reloaded.getFlipAltResize());
    CHECK_FALSE (reloaded.getResizeBlocksLanguageSwitch());
    CHECK (juce::exactlyEqual (reloaded.getLastEditorScale(), EditorScale::maximum));
    CHECK_FALSE (reloaded.getFlOutputCorrection());
    CHECK (reloaded.getRoundTripBlocks() == BayLink::maxRoundTripBlocks);
    CHECK_FALSE (reloaded.getRoundTripRealBlocks());
    CHECK (juce::exactlyEqual (reloaded.getLowestTempo(), 0.0));
    CHECK (reloaded.getStoppedShrink() == BayLink::StoppedShrink::always);
    CHECK (reloaded.getQuietLevelDb() == BayLink::minQuietLevelDb);
    CHECK (reloaded.getQuietMs() == BayLink::maxQuietMs);
    CHECK (reloaded.getFadeMs (BayLink::Fade::roundTrip) == 100);
    CHECK (reloaded.getFadeMs (BayLink::Fade::bypass) == 50);
    CHECK (reloaded.getFadeMs (BayLink::Fade::off) == BayLink::maxFadeMs);
    CHECK (reloaded.getFadeMs (BayLink::Fade::mute) == BayLink::defaultFadeMs (BayLink::Fade::mute));
    CHECK (reloaded.getLatencySettleMs() == BayLink::maxSettleMs);
    CHECK (reloaded.getTailCutReleaseMs() == BayLink::maxTailCutReleaseMs);
    CHECK (reloaded.getEditorPlacement() == EditorPlacement::Monitor::mouse);
    CHECK (reloaded.getAttachedPlacement() == EditorPlacement::Attached::hostMonitor);
    CHECK (reloaded.getDoubleClickMs() == PluginSettings::maxDoubleClickMs);
    CHECK (reloaded.getDoubleClickSets());
    file.deleteFile();
}

TEST_CASE ("Settings: the user's own defaults, resetting, and the defaults file", "[settings]")
{
    // Declared first, so it deletes the file after the settings have saved it
    const juce::TemporaryFile temporary (".settings");
    PluginSettings settings (temporary.getFile());

    SECTION ("every choice in the defaults file is one the plugin knows")
    {
        for (const auto choice : SettingsDefaults::editorPlacement.choices)
            CHECK (EditorPlacement::fromSettingsValue (juce::String (choice.data(), choice.size())).has_value());

        for (const auto choice : SettingsDefaults::attachedPlacement.choices)
            CHECK (EditorPlacement::attachedFromSettingsValue (juce::String (choice.data(), choice.size())).has_value());

        for (const auto choice : SettingsDefaults::resizeRendering.choices)
            CHECK (EditorScale::renderingFromSettingsValue (juce::String (choice.data(), choice.size())).has_value());

        for (const auto choice : SettingsDefaults::bayStoppedShrink.choices)
            CHECK (BayLink::stoppedShrinkFromName (juce::String (choice.data(), choice.size())).has_value());
    }

    SECTION ("a user default replaces the factory's until the value is set; reset goes back to it")
    {
        CHECK (settings.setUserDefault ("bayQuietMs", 1200));
        CHECK (settings.getQuietMs() == 1200);
        CHECK (settings.getUserDefaults()["bayQuietMs"] == juce::var (1200));

        settings.setQuietMs (300);
        CHECK (settings.getQuietMs() == 300);
        CHECK (settings.reset ("bayQuietMs"));
        CHECK (settings.getQuietMs() == 1200);

        // Equal to the factory's: forgotten
        CHECK (settings.setUserDefault ("bayQuietMs", BayLink::defaultQuietMs));
        CHECK_FALSE (settings.getUserDefaults().hasProperty ("bayQuietMs"));
        CHECK (settings.getQuietMs() == BayLink::defaultQuietMs);
    }

    SECTION ("every kind of setting takes a user default; values that don't fit are refused")
    {
        CHECK (settings.setUserDefault ("keepWebViewWarm", false));
        CHECK_FALSE (settings.getKeepWebViewWarm());
        CHECK (settings.setUserDefault ("bayLowestTempo", 92.5));
        CHECK (juce::exactlyEqual (settings.getLowestTempo(), 92.5));
        CHECK (settings.setUserDefault ("bayStoppedShrink", "always"));
        CHECK (settings.getStoppedShrink() == BayLink::StoppedShrink::always);
        CHECK (settings.setUserDefault ("bayRoundTripFadeMs", 40));
        CHECK (settings.getFadeMs (BayLink::Fade::roundTrip) == 40);

        CHECK_FALSE (settings.setUserDefault ("noSuchSetting", 1));
        CHECK_FALSE (settings.setUserDefault ("bayQuietMs", 99999));
        CHECK_FALSE (settings.setUserDefault ("bayQuietMs", "500"));
        CHECK_FALSE (settings.setUserDefault ("bayQuietMs", 2.5));
        CHECK_FALSE (settings.setUserDefault ("keepWebViewWarm", 1));
        CHECK_FALSE (settings.setUserDefault ("bayStoppedShrink", "sometimes"));
        CHECK_FALSE (settings.setUserDefault ("bayLowestTempo", 1000.0));
        CHECK_FALSE (settings.reset ("noSuchSetting"));
        CHECK (settings.getUserDefaults().getDynamicObject()->getProperties().size() == 4);
    }

    SECTION ("reset all puts every setting back, and leaves the remembered size and forwarded keys")
    {
        settings.setUserDefault ("doubleClickMs", 800);
        settings.setDoubleClickMs (100);
        settings.setFadeMs (BayLink::Fade::off, 200);
        settings.setRoundTripBlocks (3);
        settings.setLastEditorScale (1.5);
        settings.resetAll();

        CHECK (settings.getDoubleClickMs() == 800);
        CHECK (settings.getFadeMs (BayLink::Fade::off) == BayLink::defaultFadeMs (BayLink::Fade::off));
        CHECK (settings.getRoundTripBlocks() == BayLink::defaultRoundTripBlocks);
        CHECK (juce::exactlyEqual (settings.getLastEditorScale(), 1.5));
    }
}

TEST_CASE ("Settings: a hand-edited file's user defaults are checked like its values", "[settings]")
{
    const juce::TemporaryFile temporary (".settings");
    temporary.getFile().replaceWithText (R"(<?xml version="1.0" encoding="UTF-8"?>
<PROPERTIES>
  <VALUE name="default.bayQuietMs" val="99999"/>
  <VALUE name="default.bayLowestTempo" val="5000"/>
  <VALUE name="default.bayStoppedShrink" val="sometimes"/>
  <VALUE name="default.doubleClickMs" val="750"/>
</PROPERTIES>
)");

    PluginSettings settings (temporary.getFile());
    CHECK (settings.getQuietMs() == BayLink::defaultQuietMs);
    CHECK (juce::exactlyEqual (settings.getLowestTempo(), 0.0));
    CHECK (settings.getStoppedShrink() == BayLink::StoppedShrink::whenQuiet);
    CHECK (settings.getDoubleClickMs() == 750);

    const auto defaults = settings.getUserDefaults();
    CHECK (defaults.getDynamicObject()->getProperties().size() == 1);
    CHECK (defaults["doubleClickMs"] == juce::var (750));
}
