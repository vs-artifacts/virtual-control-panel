#include "PluginSettings.h"
#include "KeyForwarder.h"

namespace
{
    constexpr auto keepWebViewWarmKey = "keepWebViewWarm";
    constexpr auto forwardedKeysKey = "forwardedKeys";
    constexpr auto editorPlacementKey = "editorPlacement";
    constexpr auto attachedPlacementKey = "attachedPlacement";
    constexpr auto resizeRenderingKey = "resizeRendering";
    constexpr auto flipAltResizeKey = "resizeFlipAlt";
    constexpr auto lastEditorScaleKey = "lastEditorScale";
    constexpr auto flOutputCorrectionKey = "flOutputCorrection";
    constexpr auto dropoutModeKey = "bayDropouts";
    constexpr auto roundTripBlocksKey = "bayRoundTrip";
    constexpr auto bypassCrossfadeKey = "bayBypassCrossfadeMs";
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

EditorPlacement::Attached PluginSettings::getAttachedPlacement() const
{
    return EditorPlacement::attachedFromSettingsValue (file.getValue (attachedPlacementKey)).value_or (EditorPlacement::Attached::workspace);
}

void PluginSettings::setAttachedPlacement (EditorPlacement::Attached attached)
{
    file.setValue (attachedPlacementKey, EditorPlacement::toSettingsValue (attached));
}

EditorScale::Rendering PluginSettings::getResizeRendering() const
{
    return EditorScale::renderingFromSettingsValue (file.getValue (resizeRenderingKey)).value_or (EditorScale::Rendering::smooth);
}

void PluginSettings::setResizeRendering (EditorScale::Rendering rendering)
{
    file.setValue (resizeRenderingKey, EditorScale::toSettingsValue (rendering));
}

bool PluginSettings::getFlipAltResize() const
{
    return file.getBoolValue (flipAltResizeKey, false);
}

void PluginSettings::setFlipAltResize (bool flip)
{
    file.setValue (flipAltResizeKey, flip);
}

double PluginSettings::getLastEditorScale() const
{
    return EditorScale::constrain (file.getDoubleValue (lastEditorScaleKey, 1.0));
}

void PluginSettings::setLastEditorScale (double scale)
{
    file.setValue (lastEditorScaleKey, scale);
}

bool PluginSettings::getFlOutputCorrection() const
{
    return file.getBoolValue (flOutputCorrectionKey, true);
}

void PluginSettings::setFlOutputCorrection (bool shouldCorrect)
{
    file.setValue (flOutputCorrectionKey, shouldCorrect);
}

BayLink::Dropouts PluginSettings::getBayDropouts() const
{
    return BayLink::dropoutsFromName (file.getValue (dropoutModeKey)).value_or (BayLink::Dropouts::safe);
}

void PluginSettings::setBayDropouts (BayLink::Dropouts dropouts)
{
    file.setValue (dropoutModeKey, BayLink::toName (dropouts));
}

int PluginSettings::getRoundTripBlocks() const
{
    const auto blocks = file.getIntValue (roundTripBlocksKey, BayLink::defaultRoundTripBlocks);
    return blocks == BayLink::autoRoundTrip ? blocks : juce::jlimit (BayLink::minRoundTripBlocks, BayLink::maxRoundTripBlocks, blocks);
}

void PluginSettings::setRoundTripBlocks (int blocks)
{
    file.setValue (roundTripBlocksKey, blocks == BayLink::autoRoundTrip ? blocks : juce::jlimit (BayLink::minRoundTripBlocks, BayLink::maxRoundTripBlocks, blocks));
}

int PluginSettings::getBypassCrossfadeMs() const
{
    return juce::jlimit (0, BayLink::maxCrossfadeMs, file.getIntValue (bypassCrossfadeKey, BayLink::defaultCrossfadeMs));
}

void PluginSettings::setBypassCrossfadeMs (int milliseconds)
{
    file.setValue (bypassCrossfadeKey, juce::jlimit (0, BayLink::maxCrossfadeMs, milliseconds));
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
