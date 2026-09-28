#pragma once

#include "EditorPlacement.h"
#include "EditorScale.h"
#include <juce_data_structures/juce_data_structures.h>

// Plugin-wide preferences, shared by every instance and project, kept in the user's settings folder.
// Share one copy per process through juce::SharedResourcePointer<PluginSettings>.
class PluginSettings
{
public:
    PluginSettings();
    explicit PluginSettings (const juce::File& file);

    // Keep a hidden web view running so the editor reopens instantly, at the cost of memory
    bool getKeepWebViewWarm() const;
    void setKeepWebViewWarm (bool);

    // Keys the editor's page passes on to the host (see KeyForwarder). No UI yet: edit the settings file.
    juce::String getForwardedKeys() const;

    // Which monitor an editor's floating window is centred on the first time it opens
    EditorPlacement::Monitor getEditorPlacement() const;
    void setEditorPlacement (EditorPlacement::Monitor);

    // Whether dragging a corner resizes smoothly or in steps that keep lines sharp
    EditorScale::Resizing getResizing() const;
    void setResizing (EditorScale::Resizing);

    // The size new instances' editors open at: the last one used
    double getLastEditorScale() const;
    void setLastEditorScale (double);

    // Right-click menus shown by the host itself instead of drawn by our page
    bool getUseHostMenus() const;
    void setUseHostMenus (bool);

    // Experimental: clicks leave keyboard focus with the host, except in text fields
    bool getKeepKeyboardFocusInHost() const;
    void setKeepKeyboardFocusInHost (bool);

private:
    static juce::PropertiesFile::Options createOptions();

    juce::PropertiesFile file;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSettings)
};
