#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// One connected page's view of the exposed parameters, whatever carries the messages (native bridge now, WebSocket later).
// Each parameter gets a numeric handle for this session only. Message thread only: display text needs it.
class ControlSession
{
public:
    struct Parameter
    {
        juce::String slot, plugin, name;

        // What the host calls it; empty where no host sees it
        juce::String hostName;

        // Where the value and its display text come from: the hosted plugin's parameter
        const juce::AudioProcessorParameter* source;

        // Where changes go: in the plugin, the host's control bound to it, so the host sees (and can record) them
        juce::AudioProcessorParameter* writeTo;

        // The slot it belongs to (its permanent ID); 0 for none
        int slotId = 0;
    };

    explicit ControlSession (std::vector<Parameter>);
    ~ControlSession();

    // A new list (e.g. after a slot is removed): every handle ends, and nothing is sent or taken until the page asks for
    // the catalog again
    void replaceParameters (std::vector<Parameter>);

    // Starts or restarts the session: [{ h, slot, s, plugin, name, host, v, t, d }, ...], s the slot's ID, d the default value
    juce::var catalog();

    // A "set" message from the page: [{ h, v }, ...] with v from 0 to 1. Anything malformed is ignored.
    void applySet (const juce::var& batch);

    // A "gesture" message from the page: { h, active }, around a drag so hosts can record it as one move
    void applyGesture (const juce::var& gesture);

    // Values that changed since the last poll or catalog, as one "params" batch [{ h, v, t }, ...]; void if none
    juce::var poll();

    // Where a handle's changes go (e.g. for the host's menu for that parameter); nullptr if unknown
    juce::AudioProcessorParameter* findWriteTarget (const juce::var& handle) const;

    static constexpr int maxBatchSize = 4096;
    static constexpr int maxTextLength = 64;
    static constexpr int updateRateHz = 30;

private:
    std::optional<size_t> findHandle (const juce::var& handle) const;
    void endAllGestures();

    std::vector<Parameter> parameters;
    std::vector<float> lastSent;
    std::vector<bool> inGesture;
    bool started = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlSession)
};
