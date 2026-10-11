#pragma once

#include "engine/HostedPlugin.h"
#include <juce_gui_basics/juce_gui_basics.h>

class HostedEditorWindow;

// Hosted plugins' own editors, each in a separate desktop window; at most one per plugin
class HostedEditorWindows
{
public:
    // New windows open centred on this component
    explicit HostedEditorWindows (juce::Component& centreAround);
    ~HostedEditorWindows();

    // Brings the window to the front if it's already open; false if the plugin has no editor
    bool open (const HostedPlugin&);

    // Before the plugin goes
    void close (const HostedPlugin*);

private:
    juce::Component& owner;
    std::map<const HostedPlugin*, std::unique_ptr<HostedEditorWindow>> windows;

    JUCE_DECLARE_WEAK_REFERENCEABLE (HostedEditorWindows)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedEditorWindows)
};
