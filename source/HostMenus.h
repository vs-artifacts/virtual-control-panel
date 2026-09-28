#pragma once

#include "bridge/ControlSession.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Right-click menus for exposed parameters, drawn by our page in our style. For now they hold one submenu,
// "Host menu": the host's own options for that parameter (e.g. FL Studio's "Create automation clip").
class HostMenus
{
public:
    HostMenus (juce::AudioProcessorEditor&, ControlSession&);

    // The page's "parameterMenu" call with a handle: { items }
    juce::var open (const juce::var& handle);

    // The page's "menuChoice" message: { id }. Runs that item, then forgets the menu.
    void choose (const juce::var& choice);
    void close();

    // Items for the page: { id, text, enabled, checked }, { separator }, { header }, or a submenu { text, enabled, items }.
    // Each id is an index into actions.
    static juce::var describe (const juce::PopupMenu&, std::vector<std::function<void()>>& actions);

    static constexpr int maxItems = 512;
    static constexpr int maxDepth = 8;

private:
    juce::AudioProcessorEditor& editor;
    ControlSession& session;
    std::vector<std::function<void()>> actions;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostMenus)
};
