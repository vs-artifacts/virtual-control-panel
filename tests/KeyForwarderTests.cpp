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

TEST_CASE ("Plugin settings persist in their file", "[settings]")
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("heartseeker-settings-test", ".settings");

    {
        PluginSettings settings (file);
        CHECK (settings.getKeepWebViewWarm());
        CHECK (settings.getForwardedKeys() == KeyForwarder::defaultChords);
        CHECK (settings.getResizing() == EditorScale::Resizing::smooth);
        CHECK (juce::exactlyEqual (settings.getLastEditorScale(), 1.0));
        CHECK (settings.getResizeAnchor() == EditorScale::Anchor::oppositeCorner);
        CHECK (settings.getFlOutputCorrection());
        CHECK (settings.getMissingReturnBlocks() == BayLink::MissingBlocks::silence);
        CHECK (settings.getRoundTripBlocks() == BayLink::defaultRoundTripBlocks);

        settings.setKeepWebViewWarm (false);
        settings.setResizing (EditorScale::Resizing::sharp);
        settings.setResizeAnchor (EditorScale::Anchor::windowCenter);
        settings.setLastEditorScale (99.0);
        settings.setFlOutputCorrection (false);
        settings.setMissingReturnBlocks (BayLink::MissingBlocks::bypass);
        settings.setRoundTripBlocks (7);
    }

    PluginSettings reloaded (file);
    CHECK_FALSE (reloaded.getKeepWebViewWarm());
    CHECK (reloaded.getResizing() == EditorScale::Resizing::sharp);
    CHECK (reloaded.getResizeAnchor() == EditorScale::Anchor::windowCenter);
    CHECK (juce::exactlyEqual (reloaded.getLastEditorScale(), EditorScale::maximum));
    CHECK_FALSE (reloaded.getFlOutputCorrection());
    CHECK (reloaded.getMissingReturnBlocks() == BayLink::MissingBlocks::bypass);
    CHECK (reloaded.getRoundTripBlocks() == BayLink::maxRoundTripBlocks);
    file.deleteFile();
}
