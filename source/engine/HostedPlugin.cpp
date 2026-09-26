#include "HostedPlugin.h"

HostedPlugin::HostedPlugin()
{
    juce::addDefaultFormatsToManager (formatManager);
}

bool HostedPlugin::load (const juce::File& pluginFile)
{
    jassert (instance == nullptr);
    loadError.clear();

    juce::OwnedArray<juce::PluginDescription> types;

    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (pluginFile.getFullPathName()))
            format->findAllTypesForFile (types, pluginFile.getFullPathName());

    if (types.isEmpty())
    {
        loadError = "No plugin found at " + pluginFile.getFullPathName();
        return false;
    }

    // Placeholder rate and block size; the real ones arrive in prepare()
    instance = formatManager.createPluginInstance (*types.getFirst(), 44100.0, 512, loadError);

    if (instance == nullptr)
        return false;

    if (!configureStereoLayout())
    {
        loadError = instance->getName() + " doesn't support stereo";
        instance.reset();
        return false;
    }

    for (auto* param : instance->getParameters())
        if (param->isAutomatable())
            controls.push_back (param);

    return true;
}

bool HostedPlugin::configureStereoLayout()
{
    if (!instance->disableNonMainBuses())
        return false;

    auto layout = instance->getBusesLayout();

    if (!layout.inputBuses.isEmpty())
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();

    if (!layout.outputBuses.isEmpty())
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();

    return instance->setBusesLayout (layout);
}

void HostedPlugin::prepare (double sampleRate, int maxBlockSize, bool isNonRealtime)
{
    if (instance == nullptr)
        return;

    // Hosted VST3s must be prepared on the message thread; VST3 hosts call ours there
    instance->setNonRealtime (isNonRealtime);
    instance->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    instance->prepareToPlay (sampleRate, maxBlockSize);
}

void HostedPlugin::release()
{
    if (instance != nullptr)
        instance->releaseResources();
}

void HostedPlugin::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead)
{
    if (instance == nullptr)
        return;

    instance->setPlayHead (playHead);

    // Hosted plugins replace the MIDI with their own output, so an audio-only plugin would swallow it
    if (instance->acceptsMidi() || instance->producesMidi())
        instance->processBlock (buffer, midi);
    else
        instance->processBlock (buffer, emptyMidi);
}

int HostedPlugin::getLatencySamples() const
{
    return instance != nullptr ? instance->getLatencySamples() : 0;
}

double HostedPlugin::getTailLengthSeconds() const
{
    return instance != nullptr ? instance->getTailLengthSeconds() : 0.0;
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
