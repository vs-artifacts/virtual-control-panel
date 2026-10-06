#include "KeyForwarder.h"
#include <juce_audio_processors/juce_audio_processors.h>

#if JUCE_WINDOWS
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace
{
    // Physical key positions (KeyboardEvent.code) and their PC scan codes: the main-block keys only
    constexpr std::pair<const char*, std::uint32_t> knownKeys[] {
        { "Digit1", 0x02 },
        { "Digit2", 0x03 },
        { "Digit3", 0x04 },
        { "Digit4", 0x05 },
        { "Digit5", 0x06 },
        { "Digit6", 0x07 },
        { "Digit7", 0x08 },
        { "Digit8", 0x09 },
        { "Digit9", 0x0A },
        { "Digit0", 0x0B },
        { "KeyQ", 0x10 },
        { "KeyW", 0x11 },
        { "KeyE", 0x12 },
        { "KeyR", 0x13 },
        { "KeyT", 0x14 },
        { "KeyY", 0x15 },
        { "KeyU", 0x16 },
        { "KeyI", 0x17 },
        { "KeyO", 0x18 },
        { "KeyP", 0x19 },
        { "BracketLeft", 0x1A },
        { "BracketRight", 0x1B },
        { "KeyA", 0x1E },
        { "KeyS", 0x1F },
        { "KeyD", 0x20 },
        { "KeyF", 0x21 },
        { "KeyG", 0x22 },
        { "KeyH", 0x23 },
        { "KeyJ", 0x24 },
        { "KeyK", 0x25 },
        { "KeyL", 0x26 },
        { "Semicolon", 0x27 },
        { "Quote", 0x28 },
        { "KeyZ", 0x2C },
        { "KeyX", 0x2D },
        { "KeyC", 0x2E },
        { "KeyV", 0x2F },
        { "KeyB", 0x30 },
        { "KeyN", 0x31 },
        { "KeyM", 0x32 },
        { "Comma", 0x33 },
        { "Period", 0x34 },
        { "Slash", 0x35 },
        { "Space", 0x39 },
    };

    std::optional<std::uint32_t> findScanCode (const juce::String& code)
    {
        for (const auto& [name, scanCode] : knownKeys)
            if (code == name)
                return scanCode;

        return std::nullopt;
    }

    constexpr auto ctrlPrefix = "Ctrl+";

    constexpr std::pair<KeyForwarder::HeldMouseRoute, const char*> routeNames[] {
        { KeyForwarder::HeldMouseRoute::panelControl, "A" },
        { KeyForwarder::HeldMouseRoute::chainControl, "B" },
        { KeyForwarder::HeldMouseRoute::panelDirect, "C" },
        { KeyForwarder::HeldMouseRoute::mainDirect, "D" },
    };

#if JUCE_WINDOWS
    juce::String classNameOf (HWND window)
    {
        if (window == nullptr)
            return "none";

        wchar_t name[128] {};
        GetClassNameW (window, name, static_cast<int> (std::size (name)));
        return juce::String (name);
    }

    // Physical buttons, so swapped mouse buttons count too
    bool isMouseButtonDown()
    {
        for (const auto button : { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON })
            if ((GetAsyncKeyState (button) & 0x8000) != 0)
                return true;

        return false;
    }

    // A window's parent, or a top-level window's owner
    HWND nextWindowUp (HWND window)
    {
        if ((GetWindowLongPtrW (window, GWL_STYLE) & WS_CHILD) != 0)
            return GetAncestor (window, GA_PARENT);

        return GetWindow (window, GW_OWNER);
    }
#endif
}

juce::String KeyForwarder::toName (HeldMouseRoute route)
{
    for (const auto& [value, name] : routeNames)
        if (value == route)
            return name;

    jassertfalse;
    return {};
}

std::optional<KeyForwarder::HeldMouseRoute> KeyForwarder::heldMouseRouteFromName (const juce::String& text)
{
    for (const auto& [value, name] : routeNames)
        if (text == name)
            return value;

    return std::nullopt;
}

juce::String KeyForwarder::nameOf (std::uint32_t scanCode)
{
    for (const auto& [name, code] : knownKeys)
        if (code == scanCode)
            return name;

    return "0x" + juce::String::toHexString (static_cast<int> (scanCode));
}

KeyForwarder::KeyForwarder (const juce::String& chords, Post postIn, ReadModifiers readModifiersIn)
    : post (std::move (postIn)), readModifiers (std::move (readModifiersIn))
{
    for (const auto& token : juce::StringArray::fromTokens (chords, ",", ""))
    {
        const auto chord = token.trim();
        const auto code = chord.startsWith (ctrlPrefix) ? chord.substring (juce::String (ctrlPrefix).length()) : chord;

        if (findScanCode (code).has_value())
            allowedChords.addIfNotAlreadyThere (chord);
    }
}

KeyForwarder::~KeyForwarder()
{
    releaseAll();
}

void KeyForwarder::handleKeyEvent (const juce::var& event)
{
    const auto& code = event["code"];
    const auto& down = event["down"];

    if (!code.isString() || !down.isBool())
        return;

    const auto codeName = code.toString();

    if (!static_cast<bool> (down))
    {
        if (const auto held = heldKeys.find (codeName); held != heldKeys.end())
        {
            post (held->second, false, false);
            heldKeys.erase (held);
            return;
        }

        // Held since before the page had focus: the host saw it go down, so it must see it come up
        if (const auto scanCode = findScanCode (codeName); scanCode.has_value() && (allowedChords.contains (codeName) || allowedChords.contains (ctrlPrefix + codeName)))
            post (*scanCode, false, false);

        return;
    }

    const auto scanCode = findScanCode (codeName);

    if (!scanCode.has_value())
        return;

    const auto modifiers = readModifiers();

    if (modifiers.alt || modifiers.shift || modifiers.os)
        return;

    if (!allowedChords.contains ((modifiers.ctrl ? juce::String (ctrlPrefix) : juce::String()) + codeName))
        return;

    const auto& repeat = event["repeat"];
    const auto isRepeat = heldKeys.contains (codeName) || (repeat.isBool() && static_cast<bool> (repeat));

    post (*scanCode, true, isRepeat);
    heldKeys[codeName] = *scanCode;
}

void KeyForwarder::releaseAll()
{
    for (const auto& [code, scanCode] : heldKeys)
        post (scanCode, false, false);

    heldKeys.clear();
}

KeyForwarder::Post KeyForwarder::postToHostOf (juce::Component& editor, std::function<void (const juce::String&)> log, std::function<HeldMouseRoute()> route)
{
#if JUCE_WINDOWS
    // Remembered, so key releases still arrive after the editor's own window has gone
    auto host = std::make_shared<HWND> (nullptr);
    const auto isFlStudio = juce::PluginHostType().isFruityLoops();

    return [&editor, host, isFlStudio, log, route] (std::uint32_t scanCode, bool down, bool isRepeat) {
        if (down)
            if (auto* peer = editor.getPeer())
                *host = GetParent (static_cast<HWND> (peer->getNativeHandle()));

        if (*host == nullptr)
            return;

        // The same messages JUCE passes to the host window for keys the editor doesn't use
        const auto message = static_cast<UINT> (down ? WM_KEYDOWN : WM_KEYUP);
        const auto virtualKey = MapVirtualKeyW (scanCode, MAPVK_VSC_TO_VK);
        auto flags = 1u | (scanCode << 16);

        if (!down)
            flags |= 0xC0000000u;
        else if (isRepeat)
            flags |= 0x40000000u;

        // FL Studio's Delphi message loop hands keys to FL's own controls (as VCL "control notifications", CN_BASE + message)
        // unless a window it didn't create has captured the mouse, as the web view does while a button is held on the page.
        // Which route still reaches FL's typing keyboard then is being tested (routes A to D).
        const auto ourCapture = GetCapture();
        const auto mouseHeld = isMouseButtonDown();
        auto described = juce::String ("posted");
        auto delivered = false;

        if (isFlStudio && (ourCapture != nullptr || mouseHeld))
        {
            constexpr UINT vclControlNotificationBase = 0xBC00;
            const auto controlMessage = vclControlNotificationBase + message;
            const auto chosen = route != nullptr ? route() : HeldMouseRoute::panelControl;
            described = "route " + toName (chosen) + ": ";

            switch (chosen)
            {
                case HeldMouseRoute::panelControl:
                    delivered = SendMessageW (*host, controlMessage, virtualKey, static_cast<LPARAM> (flags)) != 0;
                    described += delivered ? "control message taken by " + classNameOf (*host) : "control message refused by " + classNameOf (*host) + ", posted";
                    break;

                case HeldMouseRoute::chainControl:
                {
                    juce::StringArray refused;

                    for (auto window = *host; window != nullptr && window != GetDesktopWindow() && refused.size() < 16; window = nextWindowUp (window))
                    {
                        if (SendMessageW (window, controlMessage, virtualKey, static_cast<LPARAM> (flags)) != 0)
                        {
                            delivered = true;
                            described += "control message taken by " + classNameOf (window);
                            break;
                        }

                        refused.add (classNameOf (window));
                    }

                    if (!delivered)
                        described += "control message refused by all, posted";

                    if (!refused.isEmpty())
                        described += " (refused: " + refused.joinIntoString (", ") + ")";

                    break;
                }

                case HeldMouseRoute::panelDirect:
                case HeldMouseRoute::mainDirect:
                {
                    const auto target = chosen == HeldMouseRoute::panelDirect ? *host : GetAncestor (*host, GA_ROOTOWNER);
                    const auto result = target != nullptr ? SendMessageW (target, message, virtualKey, static_cast<LPARAM> (flags)) : 0;
                    delivered = target != nullptr;
                    described += "key message sent to " + classNameOf (target) + ", returned " + juce::String (static_cast<juce::int64> (result));
                    break;
                }
            }
        }

        if (!delivered)
            PostMessageW (*host, message, virtualKey, static_cast<LPARAM> (flags));

        if (log != nullptr)
        {
            GUITHREADINFO foreground {};
            foreground.cbSize = sizeof (foreground);
            const auto foregroundCapture = GetGUIThreadInfo (0, &foreground) != 0 ? foreground.hwndCapture : nullptr;

            log (nameOf (scanCode) + (down ? (isRepeat ? " repeat" : " down") : " up")
                 + "; mouse " + (mouseHeld ? "held" : "up")
                 + "; capture: ours " + classNameOf (ourCapture) + ", foreground " + classNameOf (foregroundCapture)
                 + "; " + described);
        }
    };
#else
    juce::ignoreUnused (editor, log, route);
    return [] (std::uint32_t, bool, bool) {};
#endif
}

KeyForwarder::Modifiers KeyForwarder::readPhysicalModifiers()
{
#if JUCE_WINDOWS
    const auto isDown = [] (int key) { return (GetAsyncKeyState (key) & 0x8000) != 0; };
    return { isDown (VK_CONTROL), isDown (VK_MENU), isDown (VK_SHIFT), isDown (VK_LWIN) || isDown (VK_RWIN) };
#else
    const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
    return { mods.isCtrlDown(), mods.isAltDown(), mods.isShiftDown(), mods.isCommandDown() };
#endif
}
