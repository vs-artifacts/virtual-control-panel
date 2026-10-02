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

    // Whether dragging a corner resizes smoothly or in steps that keep lines sharp
    EditorScale::Resizing getResizing() const;
    void setResizing (EditorScale::Resizing);

    // Whether the opposite corner or the window's center stays put while a corner is dragged
    EditorScale::Anchor getResizeAnchor() const;
    void setResizeAnchor (EditorScale::Anchor);

    // The size new instances' editors open at: the last one used
    double getLastEditorScale() const;
    void setLastEditorScale (double);

    // What FL Studio multiplies a generator's added outputs by (measured, see docs/decisions.md); effects' aren't scaled
    static constexpr double flGeneratorOutputGain = 6536795.0 / 8388608.0;

    // Undo that gain on our own control outputs, in FL Studio only. We have none yet: a safety setting for when we do.
    bool getFlOutputCorrection() const;
    void setFlOutputCorrection (bool);

    // What plays when a bay's return block is missing: silence (makes dropouts audible) or the audio that was sent
    BayLink::MissingBlocks getMissingReturnBlocks() const;
    void setMissingReturnBlocks (BayLink::MissingBlocks);

    // A bay's round trip, in blocks of the host's largest block size; reported to the host as latency
    int getRoundTripBlocks() const;
    void setRoundTripBlocks (int);

private:
    static juce::PropertiesFile::Options createOptions();

    juce::PropertiesFile file;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSettings)
};
