#include "PluginSettings.h"
#include "KeyForwarder.h"

namespace
{
    constexpr auto keepWebViewWarmKey = "keepWebViewWarm";
    constexpr auto forwardedKeysKey = "forwardedKeys";
    constexpr auto editorPlacementKey = "editorPlacement";
    constexpr auto resizingKey = "resizing";
    constexpr auto lastEditorScaleKey = "lastEditorScale";
    constexpr auto useHostMenusKey = "useHostMenus";
    constexpr auto keepKeyboardFocusInHostKey = "keepKeyboardFocusInHost";
}

PluginSettings::PluginSettings()
    : file (createOptions())
{
}

PluginSettings::PluginSettings (const juce::File& fileToUse)
    : file (fileToUse, createOptions())
{
}

bool PluginSettings::getKeepWebViewWarm() const
{
    return file.getBoolValue (keepWebViewWarmKey, true);
}

void PluginSettings::setKeepWebViewWarm (bool shouldKeepWarm)
{
    file.setValue (keepWebViewWarmKey, shouldKeepWarm);
}

juce::String PluginSettings::getForwardedKeys() const
{
    return file.getValue (forwardedKeysKey, KeyForwarder::defaultChords);
}

EditorPlacement::Monitor PluginSettings::getEditorPlacement() const
{
    return EditorPlacement::fromSettingsValue (file.getValue (editorPlacementKey)).value_or (EditorPlacement::Monitor::hostPlaced);
}

void PluginSettings::setEditorPlacement (EditorPlacement::Monitor monitor)
{
    file.setValue (editorPlacementKey, EditorPlacement::toSettingsValue (monitor));
}

EditorScale::Resizing PluginSettings::getResizing() const
{
    return file.getValue (resizingKey) == "sharp" ? EditorScale::Resizing::sharp : EditorScale::Resizing::smooth;
}

void PluginSettings::setResizing (EditorScale::Resizing resizing)
{
    file.setValue (resizingKey, resizing == EditorScale::Resizing::sharp ? "sharp" : "smooth");
}

double PluginSettings::getLastEditorScale() const
{
    return EditorScale::constrain (file.getDoubleValue (lastEditorScaleKey, 1.0), EditorScale::Resizing::smooth, 1.0);
}

void PluginSettings::setLastEditorScale (double scale)
{
    file.setValue (lastEditorScaleKey, scale);
}

bool PluginSettings::getUseHostMenus() const
{
    return file.getBoolValue (useHostMenusKey, false);
}

void PluginSettings::setUseHostMenus (bool useHostMenus)
{
    file.setValue (useHostMenusKey, useHostMenus);
}

bool PluginSettings::getKeepKeyboardFocusInHost() const
{
    return file.getBoolValue (keepKeyboardFocusInHostKey, false);
}

void PluginSettings::setKeepKeyboardFocusInHost (bool keepInHost)
{
    file.setValue (keepKeyboardFocusInHostKey, keepInHost);
}

juce::PropertiesFile::Options PluginSettings::createOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = JucePlugin_Name;
    options.folderName = JucePlugin_Name;
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 500;
    return options;
}
