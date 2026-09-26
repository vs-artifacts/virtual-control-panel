#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Loads one plugin and runs our audio and MIDI through it. Knows nothing about plugin vs standalone shells.
class HostedPlugin
{
public:
    HostedPlugin();

    // Message thread only, before audio starts. Not safe to call while processing.
    bool load (const juce::File& pluginFile);

    void prepare (double sampleRate, int maxBlockSize, bool isNonRealtime);
    void release();
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead);

    int getLatencySamples() const;
    double getTailLengthSeconds() const;

    std::unique_ptr<juce::XmlElement> createStateXml() const;
    bool restoreStateFromXml (const juce::XmlElement& xml);

    juce::AudioPluginInstance* getInstance() const noexcept { return instance.get(); }
    const juce::String& getLoadError() const noexcept { return loadError; }

    // Automatable parameters only; VST3 plugins also expose ~2000 MIDI CC slots as parameters
    const std::vector<juce::AudioProcessorParameter*>& getControls() const noexcept { return controls; }

    static constexpr auto stateTag = "HostedPlugin";

private:
    bool configureStereoLayout();

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::vector<juce::AudioProcessorParameter*> controls;
    juce::String loadError;
    juce::MidiBuffer emptyMidi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedPlugin)
};
