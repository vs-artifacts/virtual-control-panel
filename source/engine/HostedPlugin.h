#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// One slot's plugin: loads it and describes it. The slot's box in the engine's graph runs it.
class HostedPlugin
{
public:
    enum class Role {
        generator,
        fx
    };

    // Message thread only. On failure the slot stays empty and getLoadError() says why.
    HostedPlugin (const juce::File& pluginFile, Role role, juce::AudioPluginFormatManager&);

    // A slot whose plugin can't load at all (e.g. not in the plugin list)
    HostedPlugin (const juce::String& name, Role role, const juce::String& loadError);

    juce::AudioPluginInstance* getInstance() const noexcept { return instance.get(); }
    Role getRole() const noexcept { return role; }
    const juce::String& getLoadError() const noexcept { return loadError; }

    // The plugin's name, or the file name if it failed to load
    juce::String getName() const;

    // Automatable parameters only; VST3 plugins also expose ~2000 MIDI CC slots as parameters
    const std::vector<juce::AudioProcessorParameter*>& getControls() const noexcept { return controls; }

    // The control with this plugin-given ID, or nullptr
    juce::AudioProcessorParameter* findControl (const juce::String& parameterID) const;

    std::unique_ptr<juce::XmlElement> createStateXml() const;
    bool restoreStateFromXml (const juce::XmlElement& xml);

    static constexpr auto stateTag = "HostedPlugin";

private:
    std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager&);

    juce::File file;
    Role role;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::vector<juce::AudioProcessorParameter*> controls;
    juce::String loadError;
    juce::String unloadableName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedPlugin)
};
