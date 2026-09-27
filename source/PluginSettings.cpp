#include "PluginSettings.h"
#include "KeyForwarder.h"

namespace
{
    constexpr auto keepWebViewWarmKey = "keepWebViewWarm";
    constexpr auto forwardedKeysKey = "forwardedKeys";
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
