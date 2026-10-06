#include "EditorPlacement.h"
#include <juce_audio_processors/juce_audio_processors.h>

#if JUCE_WINDOWS
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace
{
    constexpr std::pair<EditorPlacement::Monitor, const char*> settingsValues[] {
        { EditorPlacement::Monitor::off, "off" },
        { EditorPlacement::Monitor::hostPlaced, "hostPlaced" },
        { EditorPlacement::Monitor::mouse, "mouse" },
        { EditorPlacement::Monitor::hostMainWindow, "hostMainWindow" },
    };

    constexpr std::pair<EditorPlacement::Attached, const char*> attachedSettingsValues[] {
        { EditorPlacement::Attached::off, "off" },
        { EditorPlacement::Attached::workspace, "workspace" },
        { EditorPlacement::Attached::hostMonitor, "hostMonitor" },
    };

#if JUCE_WINDOWS
    enum class WindowState {
        notShownYet,
        cantMove,
        floating,
        attached,
    };

    struct MovableWindow
    {
        WindowState state;
        HWND window = nullptr;
        HWND workspace = nullptr; // attached windows only: the window they move in
    };

    bool hasClass (HWND window, const wchar_t* name)
    {
        wchar_t found[64] {};
        return GetClassNameW (window, found, static_cast<int> (std::size (found))) > 0 && std::wcscmp (found, name) == 0;
    }

    // FL Studio's form around a plugin editor when it's attached inside FL's workspace (a child window there), else nullptr
    HWND findFlAttachedForm (HWND editorWindow)
    {
        if (!juce::PluginHostType().isFruityLoops())
            return nullptr;

        auto window = editorWindow;

        for (int depth = 0; window != nullptr && window != GetDesktopWindow() && depth < 16; ++depth)
        {
            if (hasClass (window, L"TPluginForm"))
                return (GetWindowLongPtrW (window, GWL_STYLE) & WS_CHILD) != 0 ? window : nullptr;

            window = GetAncestor (window, GA_PARENT);
        }

        return nullptr;
    }

    // The window around the editor, if it's one we may move
    MovableWindow findMovableWindow (const juce::Component& editor)
    {
        auto* peer = editor.getPeer();

        if (peer == nullptr)
            return { WindowState::notShownYet };

        const auto editorWindow = static_cast<HWND> (peer->getNativeHandle());

        if (const auto form = findFlAttachedForm (editorWindow))
        {
            if (!IsWindowVisible (form))
                return { WindowState::notShownYet };

            return { WindowState::attached, form, GetAncestor (form, GA_PARENT) };
        }

        const auto window = GetAncestor (editorWindow, GA_ROOT);

        if (window == nullptr || !IsWindowVisible (window))
            return { WindowState::notShownYet };

        // An unowned top-level window is the host's main window (with the editor attached inside it) or our own standalone window
        if (GetWindow (window, GW_OWNER) == nullptr)
            return { WindowState::cantMove };

        return { WindowState::floating, window };
    }

    // On screen for a floating window, in its workspace's client area for an attached one
    std::optional<RECT> boundsOf (const MovableWindow& found)
    {
        RECT bounds {};

        if (!GetWindowRect (found.window, &bounds))
            return std::nullopt;

        if (found.workspace != nullptr)
            MapWindowPoints (HWND_DESKTOP, found.workspace, reinterpret_cast<POINT*> (&bounds), 2);

        return bounds;
    }

    RECT clientAreaOf (HWND window)
    {
        RECT area {};
        GetClientRect (window, &area);
        return area;
    }

    // A window bigger than the area keeps its top-left corner, and so its title bar, inside
    POINT keepInside (POINT topLeft, SIZE size, const RECT& area)
    {
        return { std::max (area.left, std::min (topLeft.x, area.right - size.cx)),
            std::max (area.top, std::min (topLeft.y, area.bottom - size.cy)) };
    }

    POINT centreIn (SIZE size, const RECT& area)
    {
        return keepInside ({ area.left + (area.right - area.left - size.cx) / 2, area.top + (area.bottom - area.top - size.cy) / 2 }, size, area);
    }

    void moveTo (HWND window, POINT topLeft)
    {
        SetWindowPos (window, nullptr, topLeft.x, topLeft.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
}

juce::String EditorPlacement::toSettingsValue (Attached attached)
{
    for (const auto& [value, name] : attachedSettingsValues)
        if (value == attached)
            return name;

    jassertfalse;
    return {};
}

std::optional<EditorPlacement::Attached> EditorPlacement::attachedFromSettingsValue (const juce::String& text)
{
    for (const auto& [value, name] : attachedSettingsValues)
        if (text == name)
            return value;

    return std::nullopt;
}

juce::String EditorPlacement::toSettingsValue (Monitor monitor)
{
    for (const auto& [value, name] : settingsValues)
        if (value == monitor)
            return name;

    jassertfalse;
    return {};
}

std::optional<EditorPlacement::Monitor> EditorPlacement::fromSettingsValue (const juce::String& text)
{
    for (const auto& [value, name] : settingsValues)
        if (text == name)
            return value;

    return std::nullopt;
}

EditorPlacement::EditorPlacement (juce::Component& editorIn, Monitor monitorIn, Attached attachedIn, std::function<void()> onDoneIn)
    : editor (editorIn), monitor (monitorIn), attached (attachedIn), onDone (std::move (onDoneIn))
{
    startTimer (50);
}

void EditorPlacement::timerCallback()
{
    if (tryToPlace())
    {
        stopTimer();
        onDone();
    }
    else if (--attemptsLeft <= 0)
    {
        // Never shown (e.g. opened hidden): the next open tries again
        stopTimer();
    }
}

bool EditorPlacement::tryToPlace()
{
#if JUCE_WINDOWS
    const auto found = findMovableWindow (editor);

    if (found.state == WindowState::notShownYet)
        return false;

    const auto bounds = found.state == WindowState::cantMove ? std::optional<RECT> {} : boundsOf (found);

    if (!bounds.has_value())
        return true;

    const SIZE size { bounds->right - bounds->left, bounds->bottom - bounds->top };

    if (found.state == WindowState::attached)
    {
        if (attached == Attached::off)
            return true;

        const auto workspaceArea = clientAreaOf (found.workspace);
        auto area = workspaceArea;
        MONITORINFO info {};
        info.cbSize = sizeof (info);

        // The part of the workspace on that monitor
        if (attached == Attached::hostMonitor && GetMonitorInfoW (MonitorFromWindow (found.workspace, MONITOR_DEFAULTTONEAREST), &info))
        {
            auto monitorArea = info.rcWork;
            MapWindowPoints (HWND_DESKTOP, found.workspace, reinterpret_cast<POINT*> (&monitorArea), 2);

            if (RECT overlap {}; IntersectRect (&overlap, &monitorArea, &workspaceArea))
                area = overlap;
        }

        moveTo (found.window, keepInside (centreIn (size, area), size, workspaceArea));
        return true;
    }

    if (monitor == Monitor::off)
        return true;

    const auto target = [&] {
        switch (monitor)
        {
            case Monitor::mouse:
            {
                POINT pointer {};
                GetCursorPos (&pointer);
                return MonitorFromPoint (pointer, MONITOR_DEFAULTTONEAREST);
            }
            case Monitor::hostMainWindow:
                return MonitorFromWindow (GetAncestor (found.window, GA_ROOTOWNER), MONITOR_DEFAULTTONEAREST);
            case Monitor::off:
            case Monitor::hostPlaced:
                break;
        }

        return MonitorFromWindow (found.window, MONITOR_DEFAULTTONEAREST);
    }();

    MONITORINFO info {};
    info.cbSize = sizeof (info);

    if (GetMonitorInfoW (target, &info))
        moveTo (found.window, centreIn (size, info.rcWork));

    return true;
#else
    juce::ignoreUnused (editor, monitor, attached);
    return true;
#endif
}

std::optional<juce::Rectangle<int>> EditorPlacement::getMovableWindowBounds (const juce::Component& editor)
{
#if JUCE_WINDOWS
    const auto found = findMovableWindow (editor);

    if (found.state != WindowState::floating && found.state != WindowState::attached)
        return std::nullopt;

    const auto bounds = boundsOf (found);

    if (!bounds.has_value())
        return std::nullopt;

    return juce::Rectangle<int>::leftTopRightBottom (bounds->left, bounds->top, bounds->right, bounds->bottom);
#else
    juce::ignoreUnused (editor);
    return std::nullopt;
#endif
}

void EditorPlacement::moveWindowTo (const juce::Component& editor, juce::Point<int> topLeft)
{
#if JUCE_WINDOWS
    const auto found = findMovableWindow (editor);

    if (found.state == WindowState::floating)
    {
        moveTo (found.window, { topLeft.x, topLeft.y });
    }
    else if (found.state == WindowState::attached)
    {
        if (const auto bounds = boundsOf (found))
            moveTo (found.window, keepInside ({ topLeft.x, topLeft.y }, { bounds->right - bounds->left, bounds->bottom - bounds->top }, clientAreaOf (found.workspace)));
    }
#else
    juce::ignoreUnused (editor, topLeft);
#endif
}

juce::StringArray EditorPlacement::describeWindows (const juce::Component& editor)
{
    juce::StringArray lines;

#if JUCE_WINDOWS
    auto* peer = editor.getPeer();
    auto window = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;

    for (int depth = 0; window != nullptr && depth < 16; ++depth)
    {
        wchar_t name[128] {};
        GetClassNameW (window, name, static_cast<int> (std::size (name)));

        const auto style = GetWindowLongPtrW (window, GWL_STYLE);
        juce::StringArray kinds;

        if ((style & WS_CHILD) != 0)
            kinds.add ("child");
        else
            kinds.add (GetWindow (window, GW_OWNER) != nullptr ? "top-level, owned" : "top-level");

        if ((style & WS_CAPTION) == WS_CAPTION)
            kinds.add ("title bar");

        if ((style & WS_THICKFRAME) != 0)
            kinds.add ("sizing frame");

        if ((style & WS_VISIBLE) == 0)
            kinds.add ("hidden");

        const auto parent = GetAncestor (window, GA_PARENT);
        RECT bounds {};
        GetWindowRect (window, &bounds);
        POINT topLeft { bounds.left, bounds.top };

        if ((style & WS_CHILD) != 0 && parent != nullptr)
            ScreenToClient (parent, &topLeft);

        lines.add (juce::String (name) + " (" + kinds.joinIntoString (", ") + "): "
                   + juce::String (bounds.right - bounds.left) + " x " + juce::String (bounds.bottom - bounds.top)
                   + " at " + juce::String (topLeft.x) + ", " + juce::String (topLeft.y)
                   + ((style & WS_CHILD) != 0 ? " in its parent" : " on screen"));

        window = parent == GetDesktopWindow() ? nullptr : parent;
    }
#else
    juce::ignoreUnused (editor);
#endif

    return lines;
}
