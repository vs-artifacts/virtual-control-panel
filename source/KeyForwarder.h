#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Passes key presses from the editor's page on to the host (e.g. FL Studio's typing keyboard),
// which can't see them while the web view has keyboard focus. Only allowed chords get through;
// modifier state is read from the physical keyboard, never trusted from the page.
class KeyForwarder
{
public:
    struct Modifiers
    {
        bool ctrl = false, alt = false, shift = false, os = false;
    };

    using Post = std::function<void (std::uint32_t scanCode, bool down, bool isRepeat)>;
    using ReadModifiers = std::function<Modifiers()>;

    // Test build: how keys reach FL Studio while a mouse button is held (see docs/decisions.md, "FL check of the resizing build")
    enum class HeldMouseRoute {
        panelControl, // A: FL's control message to the plugin panel, else posted
        chainControl, // B: FL's control message up the window chain, to the first that takes it, else posted
        panelDirect, // C: the plain key message, delivered at once to the plugin panel
        mainDirect, // D: the plain key message, delivered at once to FL's main window
    };

    static juce::String toName (HeldMouseRoute);
    static std::optional<HeldMouseRoute> heldMouseRouteFromName (const juce::String&);

    // Comma-separated chords using KeyboardEvent.code names, optionally prefixed with "Ctrl+"
    static constexpr auto defaultChords =
        "KeyQ,KeyW,KeyE,KeyR,KeyT,KeyY,KeyU,KeyI,KeyO,KeyP,KeyA,KeyS,KeyD,KeyF,KeyG,KeyH,KeyJ,KeyK,KeyL,"
        "KeyZ,KeyX,KeyC,KeyV,KeyB,KeyN,KeyM,Digit1,Digit2,Digit3,Digit4,Digit5,Digit6,Digit7,Digit8,Digit9,Digit0,"
        "BracketLeft,BracketRight,Semicolon,Quote,Comma,Period,Slash,Space,Ctrl+Space";

    KeyForwarder (const juce::String& allowedChords, Post, ReadModifiers);
    ~KeyForwarder();

    // A "key" event from the page: { code, down, repeat }
    void handleKeyEvent (const juce::var& event);

    // Releases every key still held, so the host never keeps a stuck note
    void releaseAll();

    // The chords actually in force (unknown or unsupported entries dropped), for the page
    const juce::StringArray& getAllowedChords() const noexcept { return allowedChords; }

    // Delivers keys to the window that hosts the given editor, as if typed there (Windows only for now). Each delivery
    // is described to log, if given (a diagnostic). route is asked for each key while a mouse button is held in FL Studio.
    static Post postToHostOf (juce::Component& editor, std::function<void (const juce::String&)> log = {}, std::function<HeldMouseRoute()> route = {});
    static Modifiers readPhysicalModifiers();

    // The KeyboardEvent.code name of a forwarded key, or its scan code in hex
    static juce::String nameOf (std::uint32_t scanCode);

private:
    juce::StringArray allowedChords;
    Post post;
    ReadModifiers readModifiers;
    std::map<juce::String, std::uint32_t> heldKeys;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyForwarder)
};
