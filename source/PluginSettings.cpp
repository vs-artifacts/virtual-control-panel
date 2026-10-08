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
    constexpr auto resizeBlocksLanguageSwitchKey = "resizeBlocksLanguageSwitch";
    constexpr auto lastEditorScaleKey = "lastEditorScale";
    constexpr auto lastEditorLayoutWidthKey = "lastEditorLayoutWidth";
    constexpr auto lastEditorLayoutHeightKey = "lastEditorLayoutHeight";
    constexpr auto flOutputCorrectionKey = "flOutputCorrection";
    constexpr auto roundTripBlocksKey = "bayRoundTrip";
    constexpr auto realBlocksKey = "bayRealBlocks";
    constexpr auto lowestTempoKey = "bayLowestTempo";
    constexpr auto stoppedShrinkKey = "bayStoppedShrink";
    constexpr auto quietLevelKey = "bayQuietLevelDb";
    constexpr auto quietMsKey = "bayQuietMs";
    constexpr auto latencySettleKey = "bayLatencySettleMs";
    constexpr auto tailCutReleaseKey = "bayTailCutReleaseMs";
    constexpr auto doubleClickMsKey = "doubleClickMs";
    constexpr auto doubleClickSetsKey = "doubleClickSets";
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

bool PluginSettings::getResizeBlocksLanguageSwitch() const
{
    return file.getBoolValue (resizeBlocksLanguageSwitchKey, true);
}

void PluginSettings::setResizeBlocksLanguageSwitch (bool shouldBlock)
{
    file.setValue (resizeBlocksLanguageSwitchKey, shouldBlock);
}

double PluginSettings::getLastEditorScale() const
{
    return EditorScale::constrain (file.getDoubleValue (lastEditorScaleKey, 1.0));
}

void PluginSettings::setLastEditorScale (double scale)
{
    file.setValue (lastEditorScaleKey, scale);
}

juce::Point<double> PluginSettings::getLastEditorLayout() const
{
    return { EditorScale::constrainLayout (file.getDoubleValue (lastEditorLayoutWidthKey, 1.0)),
        EditorScale::constrainLayout (file.getDoubleValue (lastEditorLayoutHeightKey, 1.0)) };
}

void PluginSettings::setLastEditorLayout (juce::Point<double> layout)
{
    file.setValue (lastEditorLayoutWidthKey, layout.x);
    file.setValue (lastEditorLayoutHeightKey, layout.y);
}

bool PluginSettings::getFlOutputCorrection() const
{
    return file.getBoolValue (flOutputCorrectionKey, true);
}

void PluginSettings::setFlOutputCorrection (bool shouldCorrect)
{
    file.setValue (flOutputCorrectionKey, shouldCorrect);
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

bool PluginSettings::getRoundTripRealBlocks() const
{
    return file.getBoolValue (realBlocksKey, true);
}

void PluginSettings::setRoundTripRealBlocks (bool shouldCount)
{
    file.setValue (realBlocksKey, shouldCount);
}

double PluginSettings::getLowestTempo() const
{
    const auto bpm = file.getDoubleValue (lowestTempoKey, 0.0);
    return bpm > 0.0 && bpm <= BayLink::maxLowestTempo ? bpm : 0.0;
}

void PluginSettings::setLowestTempo (double bpm)
{
    file.setValue (lowestTempoKey, bpm > 0.0 && bpm <= BayLink::maxLowestTempo ? bpm : 0.0);
}

BayLink::StoppedShrink PluginSettings::getStoppedShrink() const
{
    return BayLink::stoppedShrinkFromName (file.getValue (stoppedShrinkKey)).value_or (BayLink::StoppedShrink::whenQuiet);
}

void PluginSettings::setStoppedShrink (BayLink::StoppedShrink rule)
{
    file.setValue (stoppedShrinkKey, BayLink::toName (rule));
}

int PluginSettings::getQuietLevelDb() const
{
    return juce::jlimit (BayLink::minQuietLevelDb, BayLink::maxQuietLevelDb, file.getIntValue (quietLevelKey, BayLink::defaultQuietLevelDb));
}

void PluginSettings::setQuietLevelDb (int decibels)
{
    file.setValue (quietLevelKey, juce::jlimit (BayLink::minQuietLevelDb, BayLink::maxQuietLevelDb, decibels));
}

int PluginSettings::getQuietMs() const
{
    return juce::jlimit (0, BayLink::maxQuietMs, file.getIntValue (quietMsKey, BayLink::defaultQuietMs));
}

void PluginSettings::setQuietMs (int milliseconds)
{
    file.setValue (quietMsKey, juce::jlimit (0, BayLink::maxQuietMs, milliseconds));
}

int PluginSettings::getFadeMs (BayLink::Fade fade) const
{
    return juce::jlimit (0, BayLink::maxFadeMsFor (fade), file.getIntValue (fadeKey (fade), BayLink::defaultFadeMs (fade)));
}

void PluginSettings::setFadeMs (BayLink::Fade fade, int milliseconds)
{
    file.setValue (fadeKey (fade), juce::jlimit (0, BayLink::maxFadeMsFor (fade), milliseconds));
}

const char* PluginSettings::fadeKey (BayLink::Fade fade) noexcept
{
    using Fade = BayLink::Fade;

    switch (fade)
    {
        case Fade::roundTrip:
            return "bayJumpCrossfadeMs"; // the name it had when it was the only delay change crossfade
        case Fade::bayLatency:
            return "bayLatencyFadeMs";
        case Fade::mute:
            return "bayMuteFadeMs";
        case Fade::bypass:
            return "bayBypassFadeMs";
        case Fade::off:
            return "bayOffFadeMs";
        case Fade::dropout:
            break;
    }

    return "bayDropoutFadeMs";
}

int PluginSettings::getLatencySettleMs() const
{
    return juce::jlimit (0, BayLink::maxSettleMs, file.getIntValue (latencySettleKey, BayLink::defaultSettleMs));
}

void PluginSettings::setLatencySettleMs (int milliseconds)
{
    file.setValue (latencySettleKey, juce::jlimit (0, BayLink::maxSettleMs, milliseconds));
}

int PluginSettings::getTailCutReleaseMs() const
{
    return juce::jlimit (0, BayLink::maxTailCutReleaseMs, file.getIntValue (tailCutReleaseKey, BayLink::defaultTailCutReleaseMs));
}

void PluginSettings::setTailCutReleaseMs (int milliseconds)
{
    file.setValue (tailCutReleaseKey, juce::jlimit (0, BayLink::maxTailCutReleaseMs, milliseconds));
}

int PluginSettings::getDoubleClickMs() const
{
    return juce::jlimit (minDoubleClickMs, maxDoubleClickMs, file.getIntValue (doubleClickMsKey, defaultDoubleClickMs));
}

void PluginSettings::setDoubleClickMs (int milliseconds)
{
    file.setValue (doubleClickMsKey, juce::jlimit (minDoubleClickMs, maxDoubleClickMs, milliseconds));
}

bool PluginSettings::getDoubleClickSets() const
{
    return file.getBoolValue (doubleClickSetsKey, false);
}

void PluginSettings::setDoubleClickSets (bool sets)
{
    file.setValue (doubleClickSetsKey, sets);
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
