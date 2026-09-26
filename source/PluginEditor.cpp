#include "PluginEditor.h"

// Separate desktop window showing the hosted plugin's own editor
class HostedEditorWindow final : public juce::DocumentWindow
{
public:
    HostedEditorWindow (juce::AudioProcessorEditor* editor, std::function<void()> onCloseIn)
        : DocumentWindow (editor->getAudioProcessor()->getName(), juce::Colours::black, DocumentWindow::closeButton),
          onClose (std::move (onCloseIn))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);

        // Otherwise it disappears behind the DAW as soon as the DAW is clicked
        setAlwaysOnTop (true);
    }

    void closeButtonPressed() override { onClose(); }

private:
    std::function<void()> onClose;
};

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), hostedPlugin (p.getHostedPlugin())
{
    auto* instance = hostedPlugin.getInstance();
    const auto& controls = hostedPlugin.getControls();

    statusLabel.setText (instance != nullptr
                             ? "Hosting: " + instance->getName() + " (" + juce::String (static_cast<int> (controls.size())) + " of " + juce::String (instance->getParameters().size()) + " parameters shown)"
                             : "Not loaded: " + hostedPlugin.getLoadError(),
        juce::dontSendNotification);
    addAndMakeVisible (statusLabel);

    openButton.setEnabled (instance != nullptr && instance->hasEditor());
    openButton.onClick = [this] { openHostedEditor(); };
    addAndMakeVisible (openButton);

    // this chunk of code instantiates and opens the melatonin inspector
    inspectButton.onClick = [&] {
        if (!inspector)
        {
            inspector = std::make_unique<melatonin::Inspector> (*this);
            inspector->onClose = [this]() { inspector.reset(); };
        }

        inspector->setVisible (true);
    };
    addAndMakeVisible (inspectButton);

    parameterList.setModel (this);
    addAndMakeVisible (parameterList);

    lastParameterValues.resize (controls.size());

    if (!controls.empty())
        startTimerHz (10);

    setSize (520, 480);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1f3b));

    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    auto title = processor.getName() + " v" VERSION " (" CMAKE_BUILD_TYPE ")";
    g.drawText (title, getLocalBounds().reduced (10).removeFromTop (22), juce::Justification::centredLeft, false);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (22); // title, drawn in paint()

    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    auto buttonRow = area.removeFromTop (30);
    openButton.setBounds (buttonRow.removeFromLeft (160));
    buttonRow.removeFromLeft (10);
    inspectButton.setBounds (buttonRow.removeFromLeft (130));
    area.removeFromTop (10);

    parameterList.setBounds (area);
}

int PluginEditor::getNumRows()
{
    return static_cast<int> (hostedPlugin.getControls().size());
}

void PluginEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    const auto& controls = hostedPlugin.getControls();

    if (!juce::isPositiveAndBelow (row, static_cast<int> (controls.size())))
        return;

    const auto* param = controls[static_cast<size_t> (row)];

    if (row % 2 == 0)
        g.fillAll (juce::Colours::white.withAlpha (0.04f));

    auto area = juce::Rectangle<int> (width, height).reduced (8, 0);

    g.setColour (juce::Colours::grey);
    g.drawText (juce::String (param->getParameterIndex()), area.removeFromLeft (32), juce::Justification::centredLeft);

    g.setColour (juce::Colours::white);
    g.drawText (param->getName (64), area.removeFromLeft (area.getWidth() / 2), juce::Justification::centredLeft);
    g.drawText ((param->getCurrentValueAsText() + " " + param->getLabel()).trim(), area, juce::Justification::centredRight);
}

void PluginEditor::timerCallback()
{
    const auto& controls = hostedPlugin.getControls();

    // Only repaint rows whose value changed since the last tick
    for (size_t i = 0; i < controls.size(); ++i)
    {
        const auto value = controls[i]->getValue();

        if (!juce::exactlyEqual (value, lastParameterValues[i]))
        {
            lastParameterValues[i] = value;
            parameterList.repaintRow (static_cast<int> (i));
        }
    }
}

void PluginEditor::openHostedEditor()
{
    if (hostedEditorWindow != nullptr)
    {
        hostedEditorWindow->toFront (true);
        return;
    }

    auto* instance = hostedPlugin.getInstance();
    auto* editor = instance != nullptr ? instance->createEditorAndMakeActive() : nullptr;

    if (editor == nullptr)
        return;

    hostedEditorWindow = std::make_unique<HostedEditorWindow> (editor, [this] {
        // Deferred, because a window can't be deleted from inside its own callback
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<PluginEditor> (this)] {
            if (safeThis != nullptr)
                safeThis->hostedEditorWindow.reset();
        });
    });

    hostedEditorWindow->centreAroundComponent (this, hostedEditorWindow->getWidth(), hostedEditorWindow->getHeight());
    hostedEditorWindow->setVisible (true);
}
