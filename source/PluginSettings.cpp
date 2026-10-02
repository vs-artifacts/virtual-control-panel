#include "PluginSettings.h"
#include "KeyForwarder.h"

namespace
{
    constexpr auto keepWebViewWarmKey = "keepWebViewWarm";
    constexpr auto forwardedKeysKey = "forwardedKeys";
    constexpr auto editorPlacementKey = "editorPlacement";
    constexpr auto resizingKey = "resizing";
    constexpr auto resizeAnchorKey = "resizeAnchor";
    constexpr auto lastEditorScaleKey = "lastEditorScale";
    constexpr auto flOutputCorrectionKey = "flOutputCorrection";
    constexpr auto missingReturnBlocksKey = "missingReturnBlocks";
    constexpr auto roundTripBlocksKey = "roundTripBlocks";
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
    return EditorScale::resizingFromSettingsValue (file.getValue (resizingKey)).value_or (EditorScale::Resizing::smooth);
}

void PluginSettings::setResizing (EditorScale::Resizing resizing)
{
    file.setValue (resizingKey, EditorScale::toSettingsValue (resizing));
}

EditorScale::Anchor PluginSettings::getResizeAnchor() const
{
    return EditorScale::anchorFromSettingsValue (file.getValue (resizeAnchorKey)).value_or (EditorScale::Anchor::oppositeCorner);
}

void PluginSettings::setResizeAnchor (EditorScale::Anchor anchor)
{
    file.setValue (resizeAnchorKey, EditorScale::toSettingsValue (anchor));
}

double PluginSettings::getLastEditorScale() const
{
    return EditorScale::constrain (file.getDoubleValue (lastEditorScaleKey, 1.0), EditorScale::Resizing::smooth, 1.0);
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

BayLink::MissingBlocks PluginSettings::getMissingReturnBlocks() const
{
    return BayLink::missingBlocksFromName (file.getValue (missingReturnBlocksKey)).value_or (BayLink::MissingBlocks::silence);
}

void PluginSettings::setMissingReturnBlocks (BayLink::MissingBlocks missingBlocks)
{
    file.setValue (missingReturnBlocksKey, BayLink::toName (missingBlocks));
}

int PluginSettings::getRoundTripBlocks() const
{
    return juce::jlimit (BayLink::minRoundTripBlocks, BayLink::maxRoundTripBlocks, file.getIntValue (roundTripBlocksKey, BayLink::defaultRoundTripBlocks));
}

void PluginSettings::setRoundTripBlocks (int blocks)
{
    file.setValue (roundTripBlocksKey, juce::jlimit (BayLink::minRoundTripBlocks, BayLink::maxRoundTripBlocks, blocks));
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
