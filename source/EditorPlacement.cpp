#include "EditorPlacement.h"

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

#if JUCE_WINDOWS
    enum class WindowState {
        notShownYet,
        cantMove,
        floating,
    };

    // The top-level window around the editor, if it's one we may move
    std::pair<WindowState, HWND> findFloatingWindow (const juce::Component& editor)
    {
        auto* peer = editor.getPeer();

        if (peer == nullptr)
            return { WindowState::notShownYet, nullptr };

        const auto window = GetAncestor (static_cast<HWND> (peer->getNativeHandle()), GA_ROOT);

        if (window == nullptr || !IsWindowVisible (window))
            return { WindowState::notShownYet, nullptr };

        // An unowned top-level window is the host's main window (with the editor attached inside it) or our own standalone window
        if (GetWindow (window, GW_OWNER) == nullptr)
            return { WindowState::cantMove, nullptr };

        return { WindowState::floating, window };
    }
#endif
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

EditorPlacement::EditorPlacement (juce::Component& editorIn, Monitor monitorIn, std::function<void()> onDoneIn)
    : editor (editorIn), monitor (monitorIn), onDone (std::move (onDoneIn))
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
    if (monitor == Monitor::off)
        return true;

#if JUCE_WINDOWS
    const auto [state, window] = findFloatingWindow (editor);

    if (state != WindowState::floating)
        return state == WindowState::cantMove;

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
                return MonitorFromWindow (GetAncestor (window, GA_ROOTOWNER), MONITOR_DEFAULTTONEAREST);
            case Monitor::off:
            case Monitor::hostPlaced:
                break;
        }

        return MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST);
    }();

    MONITORINFO info {};
    info.cbSize = sizeof (info);
    RECT bounds {};

    if (!GetMonitorInfoW (target, &info) || !GetWindowRect (window, &bounds))
        return true;

    const auto& area = info.rcWork;
    const auto width = bounds.right - bounds.left;
    const auto height = bounds.bottom - bounds.top;

    // A window bigger than the monitor keeps its top-left corner, and so its title bar, on screen
    const auto x = std::max (area.left, area.left + (area.right - area.left - width) / 2);
    const auto y = std::max (area.top, area.top + (area.bottom - area.top - height) / 2);

    SetWindowPos (window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
#else
    juce::ignoreUnused (editor);
    return true;
#endif
}

bool EditorPlacement::moveFloatingWindowBy (const juce::Component& editor, juce::Point<int> physicalPixels)
{
#if JUCE_WINDOWS
    const auto [state, window] = findFloatingWindow (editor);
    RECT bounds {};

    if (state != WindowState::floating || !GetWindowRect (window, &bounds))
        return false;

    return SetWindowPos (window, nullptr, bounds.left + physicalPixels.x, bounds.top + physicalPixels.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
#else
    juce::ignoreUnused (editor, physicalPixels);
    return false;
#endif
}
