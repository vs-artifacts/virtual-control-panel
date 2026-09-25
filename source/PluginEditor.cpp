#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    juce::ignoreUnused (processorRef);

    addAndMakeVisible (inspectButton);
    addAndMakeVisible (glassButton);

    // this chunk of code instantiates and opens the melatonin inspector
    inspectButton.onClick = [&] {
        if (!inspector)
        {
            inspector = std::make_unique<melatonin::Inspector> (*this);
            inspector->onClose = [this]() { inspector.reset(); };
        }

        inspector->setVisible (true);
    };

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);
}

PluginEditor::~PluginEditor()
{
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1f3b), 0.0f, 0.0f, juce::Colour (0xff3a1f45), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Colourful shapes behind the glass button so the backdrop blur is visible
    const auto centre = glassButton.getBounds().getCentre().toFloat();
    g.setColour (juce::Colour (0xffff5f8f));
    g.fillEllipse (juce::Rectangle<float> (70.0f, 70.0f).withCentre (centre.translated (-45.0f, -8.0f)));
    g.setColour (juce::Colour (0xff4fd1ff));
    g.fillEllipse (juce::Rectangle<float> (60.0f, 60.0f).withCentre (centre.translated (42.0f, 12.0f)));
    g.setColour (juce::Colour (0xffffd166));
    g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (centre.translated (8.0f, -26.0f)));

    auto area = getLocalBounds();
    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    auto helloWorld = juce::String ("Hello from ") + PRODUCT_NAME_WITHOUT_VERSION + " v" VERSION + " running in " + CMAKE_BUILD_TYPE;
    g.drawText (helloWorld, area.removeFromTop (150), juce::Justification::centred, false);
}

void PluginEditor::resized()
{
    // layout the positions of your child components here
    auto area = getLocalBounds();
    inspectButton.setBounds (area.removeFromBottom (60).withSizeKeepingCentre (120, 32));
    glassButton.setBounds (area.withSizeKeepingCentre (160, 56));
}
