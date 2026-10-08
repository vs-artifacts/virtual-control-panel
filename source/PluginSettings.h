#pragma once

#include "EditorPlacement.h"
#include "EditorScale.h"
#include "engine/BayLink.h"
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

    // Where a window attached inside FL Studio's workspace is centred the first time it opens
    EditorPlacement::Attached getAttachedPlacement() const;
    void setAttachedPlacement (EditorPlacement::Attached);

    // Whether the page scales like a picture while resizing (smooth) or is laid out again at every size (sharp)
    EditorScale::Rendering getResizeRendering() const;
    void setResizeRendering (EditorScale::Rendering);

    // Off: dragging a corner keeps the opposite corner in place and Alt+drag the window's center. On: the other way round.
    bool getFlipAltResize() const;
    void setFlipAltResize (bool);

    // The size new instances' editors open at: the last one used
    double getLastEditorScale() const;
    void setLastEditorScale (double);

    // What FL Studio multiplies a generator's added outputs by (measured, see docs/decisions.md); effects' aren't scaled
    static constexpr double flGeneratorOutputGain = 6536795.0 / 8388608.0;

    // Undo that gain on our own control outputs, in FL Studio only. We have none yet: a safety setting for when we do.
    bool getFlOutputCorrection() const;
    void setFlOutputCorrection (bool);

    // A bay's round trip: BayLink::autoRoundTrip, or blocks of the host's largest block size. Reported to the host as latency.
    int getRoundTripBlocks() const;
    void setRoundTripBlocks (int);

    // How long a bay's bypass switch fades between the return and the sent audio; 0 switches at once. Also the blend when
    // the bay's delay changes.
    int getBypassCrossfadeMs() const;
    void setBypassCrossfadeMs (int);

    // How long a new bay latency must hold before the panel uses it
    int getLatencySettleMs() const;
    void setLatencySettleMs (int);

    // After a bay leaves Bypass or Off, its sound is held back this much longer (its plugin, cleared through a Peak
    // Controller, reacts some ms late)
    int getTailCutReleaseMs() const;
    void setTailCutReleaseMs (int);

    // The longest gap between the two clicks of a double-click on the page's controls
    static constexpr int minDoubleClickMs = 1, maxDoubleClickMs = 2000, defaultDoubleClickMs = 500;
    int getDoubleClickMs() const;
    void setDoubleClickMs (int);

    // Placeholder (not built yet): double-click opens a value entry instead of resetting to the default
    bool getDoubleClickSets() const;
    void setDoubleClickSets (bool);

private:
    static juce::PropertiesFile::Options createOptions();

    juce::PropertiesFile file;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSettings)
};
