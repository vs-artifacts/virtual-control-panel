#pragma once

#include "PluginSettings.h"
#include "bridge/ControlSession.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Right-click menus for exposed parameters, listing the host's own options for them (e.g. FL Studio's
// "Create automation clip"): drawn by our page in our style, or shown by the host itself if the user prefers
class HostMenus
{
public:
    HostMenus (juce::AudioProcessorEditor&, ControlSession&, PluginSettings&);

    // The page's "parameterMenu" call with a handle: { native, items }, where native means the host showed its own menu
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
    PluginSettings& settings;
    std::vector<std::function<void()>> actions;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostMenus)
};
