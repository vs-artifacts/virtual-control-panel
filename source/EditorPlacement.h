#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Centres a plugin editor's window on a chosen monitor when the editor first opens. Only separate floating windows
// move (e.g. FL Studio's detached plugin windows); a window inside the host's main window stays where the host put it.
class EditorPlacement final : private juce::Timer
{
public:
    enum class Monitor {
        off,
        hostPlaced, // the one the host opened the window on
        mouse, // the one under the mouse pointer
        hostMainWindow, // the one showing most of the host's main window
    };

    static juce::String toSettingsValue (Monitor);
    static std::optional<Monitor> fromSettingsValue (const juce::String&);

    // onDone runs once the window has been handled (placed, or left alone because it can't or shouldn't move)
    EditorPlacement (juce::Component& editor, Monitor, std::function<void()> onDone);

    // The editor's window in physical screen pixels, if it's a separate floating one we may move
    static std::optional<juce::Rectangle<int>> getFloatingWindowBounds (const juce::Component& editor);
    static void moveFloatingWindowTo (const juce::Component& editor, juce::Point<int> physicalTopLeft);

private:
    void timerCallback() override;

    // False while the host hasn't shown the window yet
    bool tryToPlace();

    juce::Component& editor;
    Monitor monitor;
    std::function<void()> onDone;
    int attemptsLeft = 100;
};
