#include "PluginSettings.h"
#include "KeyForwarder.h"

namespace
{
    namespace Defaults = SettingsDefaults;

    // Not in the defaults file: the forwarded keys have no page (a list edited in the file), the rest is remembered
    constexpr auto forwardedKeysKey = "forwardedKeys";
    constexpr auto lastEditorScaleKey = "lastEditorScale";
    constexpr auto lastEditorLayoutWidthKey = "lastEditorLayoutWidth";
    constexpr auto lastEditorLayoutHeightKey = "lastEditorLayoutHeight";

    juce::String nameOf (std::string_view key)
    {
        return juce::String (key.data(), key.size());
    }

    // Where a setting's own default is kept, beside its value
    juce::String userDefaultName (std::string_view key)
    {
        return "default." + nameOf (key);
    }

    // By its bits: fast-math builds may assume NaN and infinity never happen
    bool isFinite (double value)
    {
        return (std::bit_cast<juce::uint64> (value) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
    }

    template <typename Value>
    bool same (const Value& a, const Value& b)
    {
        if constexpr (std::is_floating_point_v<Value>)
            return juce::exactlyEqual (a, b);
        else
            return a == b;
    }

    // What the file holds under this name for the setting, if it fits it
    std::optional<bool> stored (const juce::PropertySet& file, const Defaults::Flag&, const juce::String& name)
    {
        return file.containsKey (name) ? std::optional (file.getBoolValue (name)) : std::nullopt;
    }

    std::optional<int> stored (const juce::PropertySet& file, const Defaults::Whole& setting, const juce::String& name)
    {
        if (!file.containsKey (name))
            return std::nullopt;

        const auto value = file.getIntValue (name);
        return setting.allows (value) ? std::optional (value) : std::nullopt;
    }

    std::optional<double> stored (const juce::PropertySet& file, const Defaults::Decimal& setting, const juce::String& name)
    {
        if (!file.containsKey (name))
            return std::nullopt;

        const auto value = file.getDoubleValue (name);
        return isFinite (value) && setting.allows (value) ? std::optional (value) : std::nullopt;
    }

    std::optional<juce::String> stored (const juce::PropertySet& file, const Defaults::Choice& setting, const juce::String& name)
    {
        const auto value = file.getValue (name);
        return setting.allows (value.toStdString()) ? std::optional (value) : std::nullopt;
    }

    template <typename Entry>
    auto factory (const Entry& setting)
    {
        return setting.value;
    }

    juce::String factory (const Defaults::Choice& setting)
    {
        return nameOf (setting.value);
    }

    // The value in use: the file's, else the user's own default, else the factory's
    template <typename Entry>
    auto read (const juce::PropertySet& file, const Entry& setting)
    {
        const auto fallback = stored (file, setting, userDefaultName (setting.key)).value_or (factory (setting));
        return stored (file, setting, nameOf (setting.key)).value_or (fallback);
    }

    template <typename Entry, typename Value>
    void write (juce::PropertySet& file, const Entry& setting, const Value& value)
    {
        file.setValue (nameOf (setting.key), juce::var (value));
    }

    // A value from the page, if it fits the setting
    std::optional<bool> fromVar (const Defaults::Flag&, const juce::var& value)
    {
        return value.isBool() ? std::optional (static_cast<bool> (value)) : std::nullopt;
    }

    std::optional<int> fromVar (const Defaults::Whole& setting, const juce::var& value)
    {
        return value.isInt() && setting.allows (static_cast<int> (value)) ? std::optional (static_cast<int> (value)) : std::nullopt;
    }

    std::optional<double> fromVar (const Defaults::Decimal& setting, const juce::var& value)
    {
        if (!value.isInt() && !value.isDouble())
            return std::nullopt;

        const auto number = static_cast<double> (value);
        return isFinite (number) && setting.allows (number) ? std::optional (number) : std::nullopt;
    }

    std::optional<juce::String> fromVar (const Defaults::Choice& setting, const juce::var& value)
    {
        return value.isString() && setting.allows (value.toString().toStdString()) ? std::optional (value.toString()) : std::nullopt;
    }

    const Defaults::Setting* find (const juce::String& key)
    {
        for (const auto& setting : Defaults::all)
            if (std::visit ([&key] (const auto& entry) { return nameOf (entry.key) == key; }, setting))
                return &setting;

        return nullptr;
    }
}

PluginSettings::PluginSettings()
    : file (createOptions())
{
}

PluginSettings::PluginSettings (const juce::File& fileToUse)
    : file (fileToUse, createOptions())
{
}

bool PluginSettings::setUserDefault (const juce::String& key, const juce::var& value)
{
    const auto* setting = find (key);

    if (setting == nullptr)
        return false;

    return std::visit (
        [this, &value] (const auto& entry) {
            const auto chosen = fromVar (entry, value);

            if (!chosen.has_value())
                return false;

            if (same (*chosen, factory (entry)))
                file.removeValue (userDefaultName (entry.key));
            else
                file.setValue (userDefaultName (entry.key), juce::var (*chosen));

            return true;
        },
        *setting);
}

juce::var PluginSettings::getUserDefaults() const
{
    auto* defaults = new juce::DynamicObject();

    for (const auto& setting : Defaults::all)
    {
        std::visit (
            [this, defaults] (const auto& entry) {
                if (const auto value = stored (file, entry, userDefaultName (entry.key)))
                    defaults->setProperty (nameOf (entry.key), juce::var (*value));
            },
            setting);
    }

    return defaults;
}

bool PluginSettings::reset (const juce::String& key)
{
    if (find (key) == nullptr)
        return false;

    file.removeValue (key);
    return true;
}

void PluginSettings::resetAll()
{
    for (const auto& setting : Defaults::all)
        std::visit ([this] (const auto& entry) { file.removeValue (nameOf (entry.key)); }, setting);
}

bool PluginSettings::getKeepWebViewWarm() const
{
    return read (file, Defaults::keepWebViewWarm);
}

void PluginSettings::setKeepWebViewWarm (bool shouldKeepWarm)
{
    write (file, Defaults::keepWebViewWarm, shouldKeepWarm);
}

juce::String PluginSettings::getForwardedKeys() const
{
    return file.getValue (forwardedKeysKey, KeyForwarder::defaultChords);
}

// Every choice in the defaults file converts (tested), so the fallbacks below are never used
EditorPlacement::Monitor PluginSettings::getEditorPlacement() const
{
    return EditorPlacement::fromSettingsValue (read (file, Defaults::editorPlacement)).value_or (EditorPlacement::Monitor {});
}

void PluginSettings::setEditorPlacement (EditorPlacement::Monitor monitor)
{
    write (file, Defaults::editorPlacement, EditorPlacement::toSettingsValue (monitor));
}

EditorPlacement::Attached PluginSettings::getAttachedPlacement() const
{
    return EditorPlacement::attachedFromSettingsValue (read (file, Defaults::attachedPlacement)).value_or (EditorPlacement::Attached {});
}

void PluginSettings::setAttachedPlacement (EditorPlacement::Attached attached)
{
    write (file, Defaults::attachedPlacement, EditorPlacement::toSettingsValue (attached));
}

EditorScale::Rendering PluginSettings::getResizeRendering() const
{
    return EditorScale::renderingFromSettingsValue (read (file, Defaults::resizeRendering)).value_or (EditorScale::Rendering {});
}

void PluginSettings::setResizeRendering (EditorScale::Rendering rendering)
{
    write (file, Defaults::resizeRendering, EditorScale::toSettingsValue (rendering));
}

bool PluginSettings::getFlipAltResize() const
{
    return read (file, Defaults::resizeFlipAlt);
}

void PluginSettings::setFlipAltResize (bool flip)
{
    write (file, Defaults::resizeFlipAlt, flip);
}

bool PluginSettings::getResizeBlocksLanguageSwitch() const
{
    return read (file, Defaults::resizeBlocksLanguageSwitch);
}

void PluginSettings::setResizeBlocksLanguageSwitch (bool shouldBlock)
{
    write (file, Defaults::resizeBlocksLanguageSwitch, shouldBlock);
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
    return read (file, Defaults::flOutputCorrection);
}

void PluginSettings::setFlOutputCorrection (bool shouldCorrect)
{
    write (file, Defaults::flOutputCorrection, shouldCorrect);
}

int PluginSettings::getRoundTripBlocks() const
{
    return read (file, Defaults::bayRoundTrip);
}

void PluginSettings::setRoundTripBlocks (int blocks)
{
    write (file, Defaults::bayRoundTrip, Defaults::bayRoundTrip.limit (blocks));
}

bool PluginSettings::getRoundTripRealBlocks() const
{
    return read (file, Defaults::bayRealBlocks);
}

void PluginSettings::setRoundTripRealBlocks (bool shouldCount)
{
    write (file, Defaults::bayRealBlocks, shouldCount);
}

double PluginSettings::getLowestTempo() const
{
    return read (file, Defaults::bayLowestTempo);
}

void PluginSettings::setLowestTempo (double bpm)
{
    write (file, Defaults::bayLowestTempo, isFinite (bpm) && Defaults::bayLowestTempo.allows (bpm) ? bpm : Defaults::bayLowestTempo.value);
}

BayLink::StoppedShrink PluginSettings::getStoppedShrink() const
{
    return BayLink::stoppedShrinkFromName (read (file, Defaults::bayStoppedShrink)).value_or (BayLink::StoppedShrink {});
}

void PluginSettings::setStoppedShrink (BayLink::StoppedShrink rule)
{
    write (file, Defaults::bayStoppedShrink, juce::String (BayLink::toName (rule)));
}

int PluginSettings::getQuietLevelDb() const
{
    return read (file, Defaults::bayQuietLevelDb);
}

void PluginSettings::setQuietLevelDb (int decibels)
{
    write (file, Defaults::bayQuietLevelDb, Defaults::bayQuietLevelDb.limit (decibels));
}

int PluginSettings::getQuietMs() const
{
    return read (file, Defaults::bayQuietMs);
}

void PluginSettings::setQuietMs (int milliseconds)
{
    write (file, Defaults::bayQuietMs, Defaults::bayQuietMs.limit (milliseconds));
}

int PluginSettings::getFadeMs (BayLink::Fade fade) const
{
    return read (file, BayLink::fadeSetting (fade));
}

void PluginSettings::setFadeMs (BayLink::Fade fade, int milliseconds)
{
    write (file, BayLink::fadeSetting (fade), BayLink::fadeSetting (fade).limit (milliseconds));
}

const char* PluginSettings::fadeKey (BayLink::Fade fade) noexcept
{
    // The keys are string literals, so they end in a null
    return BayLink::fadeSetting (fade).key.data();
}

int PluginSettings::getLatencySettleMs() const
{
    return read (file, Defaults::bayLatencySettleMs);
}

void PluginSettings::setLatencySettleMs (int milliseconds)
{
    write (file, Defaults::bayLatencySettleMs, Defaults::bayLatencySettleMs.limit (milliseconds));
}

int PluginSettings::getTailCutReleaseMs() const
{
    return read (file, Defaults::bayTailCutReleaseMs);
}

void PluginSettings::setTailCutReleaseMs (int milliseconds)
{
    write (file, Defaults::bayTailCutReleaseMs, Defaults::bayTailCutReleaseMs.limit (milliseconds));
}

int PluginSettings::getSlotChangeFadeOutMs() const
{
    return read (file, Defaults::slotChangeFadeOutMs);
}

void PluginSettings::setSlotChangeFadeOutMs (int milliseconds)
{
    write (file, Defaults::slotChangeFadeOutMs, Defaults::slotChangeFadeOutMs.limit (milliseconds));
}

int PluginSettings::getSlotChangeFadeInMs() const
{
    return read (file, Defaults::slotChangeFadeInMs);
}

void PluginSettings::setSlotChangeFadeInMs (int milliseconds)
{
    write (file, Defaults::slotChangeFadeInMs, Defaults::slotChangeFadeInMs.limit (milliseconds));
}

int PluginSettings::getDoubleClickMs() const
{
    return read (file, Defaults::doubleClickMs);
}

void PluginSettings::setDoubleClickMs (int milliseconds)
{
    write (file, Defaults::doubleClickMs, Defaults::doubleClickMs.limit (milliseconds));
}

bool PluginSettings::getDoubleClickSets() const
{
    return read (file, Defaults::doubleClickSets);
}

void PluginSettings::setDoubleClickSets (bool sets)
{
    write (file, Defaults::doubleClickSets, sets);
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
