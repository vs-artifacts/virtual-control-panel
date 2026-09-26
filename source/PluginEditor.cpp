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

// One hosted plugin's line in the editor: what it is, plus a button that opens its own window
struct SlotRow
{
    explicit SlotRow (const HostedPlugin& p) : plugin (p) {}

    const HostedPlugin& plugin;
    juce::Label status;
    juce::TextButton openButton { "Open window" };
    std::unique_ptr<HostedEditorWindow> window;
};

namespace
{
    constexpr int slotRowHeight = 28;

    juce::String describe (const HostedPlugin& plugin)
    {
        const auto role = juce::String (plugin.getRole() == HostedPlugin::Role::generator ? "Generator" : "FX");

        if (auto* instance = plugin.getInstance())
            return role + ": " + instance->getName() + " (" + juce::String (static_cast<int> (plugin.getControls().size())) + " of " + juce::String (instance->getParameters().size()) + " parameters)";

        return role + ": " + plugin.getName() + " failed: " + plugin.getLoadError();
    }
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), engine (p.getEngine())
{
    parameterList.setModel (this);
    addAndMakeVisible (parameterList);

    const auto& plugins = engine.getPlugins();

    for (size_t i = 0; i < plugins.size(); ++i)
    {
        const auto& plugin = *plugins[i];
        auto& row = *slotRows.emplace_back (std::make_unique<SlotRow> (plugin));

        row.status.setText (describe (plugin), juce::dontSendNotification);
        addAndMakeVisible (row.status);

        row.openButton.setEnabled (plugin.getInstance() != nullptr && plugin.getInstance()->hasEditor());
        row.openButton.onClick = [this, &row] { openHostedEditor (row); };
        addAndMakeVisible (row.openButton);

        if (plugin.getInstance() != nullptr)
            controlsSelector.addItem ("Controls: " + plugin.getName(), static_cast<int> (i) + 1);
    }

    controlsSelector.onChange = [this] {
        const auto index = controlsSelector.getSelectedId() - 1;
        const auto& all = engine.getPlugins();
        showControlsOf (juce::isPositiveAndBelow (index, static_cast<int> (all.size())) ? all[static_cast<size_t> (index)].get() : nullptr);
    };
    controlsSelector.setSelectedItemIndex (0, juce::sendNotificationSync);
    addAndMakeVisible (controlsSelector);

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

    startTimerHz (10);
    setSize (560, 560);
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
    area.removeFromTop (28); // title, drawn in paint()

    for (auto& row : slotRows)
    {
        auto line = area.removeFromTop (slotRowHeight);
        row->openButton.setBounds (line.removeFromRight (110).reduced (0, 2));
        row->status.setBounds (line);
    }

    area.removeFromTop (8);
    auto selectorRow = area.removeFromTop (30);
    inspectButton.setBounds (selectorRow.removeFromRight (130));
    selectorRow.removeFromRight (10);
    controlsSelector.setBounds (selectorRow);
    area.removeFromTop (10);

    parameterList.setBounds (area);
}

void PluginEditor::showControlsOf (const HostedPlugin* plugin)
{
    shownPlugin = plugin;
    lastParameterValues.assign (plugin != nullptr ? plugin->getControls().size() : 0, 0.0f);
    parameterList.updateContent();
    parameterList.repaint();
}

int PluginEditor::getNumRows()
{
    return shownPlugin != nullptr ? static_cast<int> (shownPlugin->getControls().size()) : 0;
}

void PluginEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (shownPlugin == nullptr || !juce::isPositiveAndBelow (row, getNumRows()))
        return;

    const auto* param = shownPlugin->getControls()[static_cast<size_t> (row)];

    if (row % 2 == 0)
        g.fillAll (juce::Colours::white.withAlpha (0.04f));

    auto area = juce::Rectangle<int> (width, height).reduced (8, 0);

    g.setColour (juce::Colours::grey);
    g.drawText (juce::String (param->getParameterIndex()), area.removeFromLeft (40), juce::Justification::centredLeft);

    g.setColour (juce::Colours::white);
    g.drawText (param->getName (64), area.removeFromLeft (area.getWidth() / 2), juce::Justification::centredLeft);
    g.drawText ((param->getCurrentValueAsText() + " " + param->getLabel()).trim(), area, juce::Justification::centredRight);
}

void PluginEditor::timerCallback()
{
    if (shownPlugin == nullptr)
        return;

    const auto& controls = shownPlugin->getControls();

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

void PluginEditor::openHostedEditor (SlotRow& row)
{
    if (row.window != nullptr)
    {
        row.window->toFront (true);
        return;
    }

    auto* instance = row.plugin.getInstance();
    auto* editor = instance != nullptr ? instance->createEditorAndMakeActive() : nullptr;

    if (editor == nullptr)
        return;

    row.window = std::make_unique<HostedEditorWindow> (editor, [this, &row] {
        // Deferred, because a window can't be deleted from inside its own callback
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<PluginEditor> (this), &row] {
            if (safeThis != nullptr)
                row.window.reset();
        });
    });

    row.window->centreAroundComponent (this, row.window->getWidth(), row.window->getHeight());
    row.window->setVisible (true);
}
