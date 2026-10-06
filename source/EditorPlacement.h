#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Centres a plugin editor's window when the editor first opens, and moves it while resizing. Separate floating windows
// (e.g. FL Studio's detached plugin windows) are centred on a chosen monitor. In FL Studio, an attached window (FL's
// plugin form inside its workspace) moves within the workspace. Any other window inside a host's main window stays put.
class EditorPlacement final : private juce::Timer
{
public:
    // For separate floating windows
    enum class Monitor {
        off,
        hostPlaced, // the one the host opened the window on
        mouse, // the one under the mouse pointer
        hostMainWindow, // the one showing most of the host's main window
    };

    // For windows attached inside FL Studio's workspace; always kept inside it
    enum class Attached {
        off,
        workspace, // centred in FL's workspace
        hostMonitor, // centred on the monitor showing most of FL's workspace
    };

    static juce::String toSettingsValue (Monitor);
    static std::optional<Monitor> fromSettingsValue (const juce::String&);
    static juce::String toSettingsValue (Attached);
    static std::optional<Attached> attachedFromSettingsValue (const juce::String&);

    // onDone runs once the window has been handled (placed, or left alone because it can't or shouldn't move)
    EditorPlacement (juce::Component& editor, Monitor, Attached, std::function<void()> onDone);

    // The editor's outer window in physical pixels, if we may move it: on screen for a floating window, in FL's
    // workspace for an attached one
    static std::optional<juce::Rectangle<int>> getMovableWindowBounds (const juce::Component& editor);

    // Same coordinates as getMovableWindowBounds; an attached window is kept inside FL's workspace
    static void moveWindowTo (const juce::Component& editor, juce::Point<int> topLeft);

    // Diagnostic: the windows from the editor's own up to the top, one line each (class, kind, size, position in its
    // parent), to find which of the host's windows can move
    static juce::StringArray describeWindows (const juce::Component& editor);

private:
    void timerCallback() override;

    // False while the host hasn't shown the window yet
    bool tryToPlace();

    juce::Component& editor;
    Monitor monitor;
    Attached attached;
    std::function<void()> onDone;
    int attemptsLeft = 100;
};
