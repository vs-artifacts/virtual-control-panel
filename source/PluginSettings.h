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

    // What plays when a bay's return block is missing: the audio that was sent (safe), or silence (makes dropouts audible)
    BayLink::Dropouts getBayDropouts() const;
    void setBayDropouts (BayLink::Dropouts);

    // A bay's round trip: BayLink::autoRoundTrip, or blocks of the host's largest block size. Reported to the host as latency.
    int getRoundTripBlocks() const;
    void setRoundTripBlocks (int);

    // How long a bay's bypass switch fades between the return and the sent audio; 0 switches at once
    int getBypassCrossfadeMs() const;
    void setBypassCrossfadeMs (int);

private:
    static juce::PropertiesFile::Options createOptions();

    juce::PropertiesFile file;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSettings)
};
