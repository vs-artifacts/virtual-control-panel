#pragma once

#include "HostedPlugin.h"

// A hosted plugin's parameter chosen for control from our UI (and, in the plugin, from the host)
struct ExposedParameter
{
    size_t slot;
    juce::AudioProcessorParameter* parameter;
};

// Runs every hosted plugin inside one routing graph. Knows nothing about plugin vs standalone shells.
// Current fixed routing: MIDI in -> each generator -> (summed) -> FX chain in series -> audio out; MIDI in -> MIDI out.
class Engine
{
public:
    Engine();

    // Message thread only, once, before audio starts
    void build (const std::vector<juce::File>& generators, const std::vector<juce::File>& fxChain);

    void prepare (double sampleRate, int maxBlockSize, bool isNonRealtime);
    void release();
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead);

    int getLatencySamples() const;
    double getTailLengthSeconds() const;

    std::unique_ptr<juce::XmlElement> createStateXml() const;
    void restoreStateFromXml (const juce::XmlElement& xml);

    // Generators first, then the FX chain in order; failed loads keep their slot
    const std::vector<std::unique_ptr<HostedPlugin>>& getPlugins() const noexcept { return plugins; }

    // True if source's audio feeds straight into destination
    bool isConnected (const HostedPlugin& source, const HostedPlugin& destination) const;

    // People-facing slot name, numbered per role from 1: "Generator 2", "FX 1"
    juce::String getSlotLabel (size_t slot) const;

    // Message thread only, after build(). False if the slot is empty or has no such control.
    bool expose (size_t slot, const juce::String& parameterID);

    // In the order they were exposed
    const std::vector<ExposedParameter>& getExposedParameters() const noexcept { return exposed; }

    static constexpr auto stateTag = "Engine";

private:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    void connectAudio (NodeID source, NodeID destination);
    void connectMidi (NodeID source, NodeID destination);

    juce::AudioPluginFormatManager formatManager;
    juce::AudioProcessorGraph graph;
    std::vector<std::unique_ptr<HostedPlugin>> plugins;
    std::vector<ExposedParameter> exposed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
