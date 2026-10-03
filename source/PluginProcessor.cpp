#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
#if !JucePlugin_IsMidiEffect
    #if !JucePlugin_IsSynth
              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
    #endif
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
      )
{
    engine.build ({ juce::File (serum2Path), juce::File (sylenth1Path) }, { juce::File (eqPath) });
    engine.expose (2, eqLowGainID); // slot 2: the EQ, after the two generators

    // Exposed parameter i is bound to host control i, then the bay's controls follow
    const auto& exposed = engine.getExposedParameters();
    jassert (exposed.size() + BayLink::numControls <= HostControls::count);

    for (size_t i = 0; i < exposed.size(); ++i)
        hostControls.bind (static_cast<int> (i), *exposed[i].parameter);

    for (int i = 0; i < BayLink::numControls; ++i)
        hostControls.bind (static_cast<int> (exposed.size()) + i, bayLink.getControl (i));

    applyLinkSettings();
    linkSettingsCheck.startTimerHz (2);
}

PluginProcessor::~PluginProcessor()
{
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PluginProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PluginProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    return engine.getTailLengthSeconds();
}

int PluginProcessor::getNumPrograms()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    // Steinberg's VST3 validator fails plugins whose single default program has no name
    return "Default";
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, isNonRealtime());
    applyLinkSettings();
    bayLink.prepare (sampleRate, samplesPerBlock);
    reportLatency();
}

void PluginProcessor::applyLinkSettings()
{
    bayLink.setDropouts (settings->getBayDropouts());
    bayLink.setCrossfadeMs (settings->getBypassCrossfadeMs());

    if (const auto blocks = settings->getRoundTripBlocks(); blocks != bayLink.getRoundTripBlocks())
        bayLink.setRoundTripBlocks (blocks);

    bayLink.updateRoundTrip();
    reportLatency();
}

void PluginProcessor::reportLatency()
{
    // The bay's round trip delays everything we play, the direct sound included. Only a change reaches the host.
    setLatencySamples (engine.getLatencySamples() + bayLink.getRoundTripSamples());
}

void PluginProcessor::releaseResources()
{
    engine.release();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    // Hosted plugins are set up for stereo, so that's the only layout we offer
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
    #if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    #endif

    return true;
#endif
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    lastBlockTime.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Output channels without matching input may contain garbage (as an instrument that's all of them)
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    engine.process (buffer, midiMessages, getPlayHead());
    bayLink.process (buffer);
}

bool PluginProcessor::isBeingProcessed() const noexcept
{
    constexpr juce::uint32 stallMs = 500;
    return juce::Time::getMillisecondCounter() - lastBlockTime.load (std::memory_order_relaxed) < stallMs;
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("HeartseekerPanelState");
    state.setAttribute ("editorPlaced", editorPlaced);
    state.setAttribute ("editorTab", editorTab);
    state.setAttribute ("bayBypass", bayLink.isBypassed());

    if (editorScale.has_value())
        state.setAttribute ("editorScale", *editorScale);

    auto* controls = state.createNewChildElement (bayControlsTag);

    for (int i = 0; i < BayLink::numControls; ++i)
        controls->setAttribute ("control" + juce::String (i + 1), static_cast<double> (bayLink.getControl (i).getValue()));

    state.addChildElement (engine.createStateXml().release());
    copyXmlToBinary (state, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // nullptr if the data isn't valid saved XML (e.g. a corrupt project file)
    if (auto state = getXmlFromBinary (data, sizeInBytes))
    {
        editorPlaced = state->getBoolAttribute ("editorPlaced");
        editorTab = juce::jlimit (0, numEditorTabs - 1, state->getIntAttribute ("editorTab"));

        // Projects saved before the bypass switch have the output switch instead
        if (state->hasAttribute ("bayBypass"))
            bayLink.setBypass (state->getBoolAttribute ("bayBypass"));
        else
            bayLink.setBypass (state->getStringAttribute ("bayOutput") == "direct");

        if (state->hasAttribute ("editorScale"))
            editorScale = EditorScale::constrain (state->getDoubleAttribute ("editorScale"), EditorScale::Resizing::smooth, 1.0);

        if (auto* controls = state->getChildByName (bayControlsTag))
            for (int i = 0; i < BayLink::numControls; ++i)
                if (const auto name = "control" + juce::String (i + 1); controls->hasAttribute (name))
                    bayLink.getControl (i).setValue (static_cast<float> (controls->getDoubleAttribute (name)));

        if (auto* engineState = state->getChildByName (Engine::stateTag))
            engine.restoreStateFromXml (*engineState);

        // The host reads our controls' values after this, so they must already match the restored plugins
        hostControls.syncWithoutNotifying();
    }
}

std::vector<ControlSession::Parameter> PluginProcessor::createSessionParameters()
{
    std::vector<ControlSession::Parameter> parameters;
    const auto& exposed = engine.getExposedParameters();

    for (size_t i = 0; i < exposed.size(); ++i)
    {
        const auto& [slot, parameter] = exposed[i];
        auto& hostControl = hostControls[static_cast<int> (i)];

        parameters.push_back ({ engine.getSlotLabel (slot),
            engine.getPlugins()[slot]->getName(),
            parameter->getName (ControlSession::maxTextLength),
            hostControl.getName (ControlSession::maxTextLength),
            parameter,
            &hostControl });
    }

    for (int i = 0; i < BayLink::numControls; ++i)
    {
        auto& control = bayLink.getControl (i);
        auto& hostControl = hostControls[static_cast<int> (exposed.size()) + i];

        parameters.push_back ({ "Bay 1",
            "FL plugin, through relays",
            control.getName (ControlSession::maxTextLength),
            hostControl.getName (ControlSession::maxTextLength),
            &control,
            &hostControl });
    }

    return parameters;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
