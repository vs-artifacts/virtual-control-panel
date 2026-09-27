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
    : AudioProcessorEditor (&p),
      engine (p.getEngine()),
      keyForwarder (settings->getForwardedKeys(), KeyForwarder::postToHostOf (*this), KeyForwarder::readPhysicalModifiers)
{
    for (const auto& plugin : engine.getPlugins())
    {
        auto& row = *slotRows.emplace_back (std::make_unique<SlotRow> (*plugin));

        row.status.setText (describe (*plugin), juce::dontSendNotification);
        addAndMakeVisible (row.status);

        row.openButton.setEnabled (plugin->getInstance() != nullptr && plugin->getInstance()->hasEditor());
        row.openButton.onClick = [this, &row] { openHostedEditor (row); };
        addAndMakeVisible (row.openButton);
    }

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

    if (WebUi::isSupported())
    {
        webUi = std::make_unique<WebUi> (p.getName() + " v" VERSION, keyForwarder);

        // Started after our own page, so it never delays the first editor open
        webUi->onPageLoaded = [this] {
            if (settings->getKeepWebViewWarm())
                webViewKeepAlive->start();
        };

        addAndMakeVisible (*webUi);
    }
    else
    {
        webUiUnavailable.setText ("Web UI unavailable: this system has no supported web view (on Windows, install the Microsoft Edge WebView2 Runtime).", juce::dontSendNotification);
        webUiUnavailable.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (webUiUnavailable);
    }

    keepWarmToggle.setToggleState (settings->getKeepWebViewWarm(), juce::dontSendNotification);
    keepWarmToggle.setEnabled (webUi != nullptr);
    keepWarmToggle.onClick = [this] {
        const auto keepWarm = keepWarmToggle.getToggleState();
        settings->setKeepWebViewWarm (keepWarm);

        if (keepWarm)
            webViewKeepAlive->start();
        else
            webViewKeepAlive->stop();
    };
    addAndMakeVisible (keepWarmToggle);

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
    auto settingsRow = area.removeFromTop (30);
    inspectButton.setBounds (settingsRow.removeFromRight (130));
    keepWarmToggle.setBounds (settingsRow);
    area.removeFromTop (10);

    if (webUi != nullptr)
        webUi->setBounds (area);
    else
        webUiUnavailable.setBounds (area);
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
