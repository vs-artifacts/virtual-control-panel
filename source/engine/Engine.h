#pragma once

#include "HostedPlugin.h"

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

    static constexpr auto stateTag = "Engine";

private:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    void connectAudio (NodeID source, NodeID destination);
    void connectMidi (NodeID source, NodeID destination);

    juce::AudioPluginFormatManager formatManager;
    juce::AudioProcessorGraph graph;
    std::vector<std::unique_ptr<HostedPlugin>> plugins;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
