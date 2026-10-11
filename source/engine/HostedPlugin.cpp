#include "HostedPlugin.h"

namespace
{
    bool configureStereoLayout (juce::AudioPluginInstance& plugin)
    {
        if (!plugin.disableNonMainBuses())
            return false;

        auto layout = plugin.getBusesLayout();

        if (!layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();

        if (!layout.outputBuses.isEmpty())
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();

        return plugin.setBusesLayout (layout);
    }
}

HostedPlugin::HostedPlugin (const juce::File& pluginFile, Role roleIn, juce::AudioPluginFormatManager& formatManager)
    : file (pluginFile), role (roleIn)
{
    instance = load (formatManager);

    if (instance == nullptr)
        return;

    for (auto* param : instance->getParameters())
        if (param->isAutomatable())
            controls.push_back (param);
}

HostedPlugin::HostedPlugin (const juce::String& name, Role roleIn, const juce::String& loadErrorIn)
    : role (roleIn), loadError (loadErrorIn), unloadableName (name)
{
}

std::unique_ptr<juce::AudioPluginInstance> HostedPlugin::load (juce::AudioPluginFormatManager& formatManager)
{
    juce::OwnedArray<juce::PluginDescription> types;

    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (file.getFullPathName()))
            format->findAllTypesForFile (types, file.getFullPathName());

    // One file can hold several plugins (Serum2.vst3 holds both Serum 2 and Serum 2 FX)
    const auto wantInstrument = role == Role::generator;
    const auto type = std::find_if (types.begin(), types.end(), [&] (const auto* t) { return t->isInstrument == wantInstrument; });

    if (type == types.end())
    {
        loadError = juce::String ("No ") + (wantInstrument ? "instrument" : "effect") + " found at " + file.getFullPathName();
        return nullptr;
    }

    // Placeholder rate and block size; the slot's box prepares it with the real ones
    auto loaded = formatManager.createPluginInstance (**type, 44100.0, 512, loadError);

    if (loaded != nullptr && !configureStereoLayout (*loaded))
    {
        loadError = loaded->getName() + " doesn't support stereo";
        return nullptr;
    }

    return loaded;
}

juce::String HostedPlugin::getName() const
{
    if (instance != nullptr)
        return instance->getName();

    return file == juce::File() ? unloadableName : file.getFileNameWithoutExtension();
}

juce::AudioProcessorParameter* HostedPlugin::findControl (const juce::String& parameterID) const
{
    for (auto* control : controls)
        if (auto* withID = dynamic_cast<juce::HostedAudioProcessorParameter*> (control); withID != nullptr && withID->getParameterID() == parameterID)
            return control;

    return nullptr;
}

std::unique_ptr<juce::XmlElement> HostedPlugin::createStateXml() const
{
    if (instance == nullptr)
        return nullptr;

    juce::MemoryBlock data;
    instance->getStateInformation (data);

    auto xml = std::make_unique<juce::XmlElement> (stateTag);
    xml->setAttribute ("id", instance->getPluginDescription().createIdentifierString());
    xml->addTextElement (data.toBase64Encoding());
    return xml;
}

bool HostedPlugin::restoreStateFromXml (const juce::XmlElement& xml)
{
    if (instance == nullptr || !xml.hasTagName (stateTag))
        return false;

    // Never hand one plugin's saved state to a different plugin
    if (!instance->getPluginDescription().matchesIdentifierString (xml.getStringAttribute ("id")))
        return false;

    juce::MemoryBlock data;

    if (!data.fromBase64Encoding (xml.getAllSubText()) || data.isEmpty())
        return false;

    instance->setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    return true;
}
