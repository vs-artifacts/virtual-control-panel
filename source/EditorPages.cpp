#include "EditorPages.h"

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

struct PluginsPage::Row
{
    explicit Row (const HostedPlugin& p) : plugin (p) {}

    const HostedPlugin& plugin;
    juce::Label status;
    juce::TextButton openButton { "Open window" };
    std::unique_ptr<HostedEditorWindow> window;
};

namespace
{
    constexpr int rowHeight = 28;
    constexpr int margin = 10;

    juce::String describe (const HostedPlugin& plugin)
    {
        const auto role = juce::String (plugin.getRole() == HostedPlugin::Role::generator ? "Generator" : "FX");

        if (auto* instance = plugin.getInstance())
            return role + ": " + instance->getName() + " (" + juce::String (static_cast<int> (plugin.getControls().size())) + " of " + juce::String (instance->getParameters().size()) + " parameters)";

        return role + ": " + plugin.getName() + " failed: " + plugin.getLoadError();
    }
}

//==============================================================================
PluginsPage::PluginsPage (const Engine& engine)
{
    for (const auto& plugin : engine.getPlugins())
    {
        auto& row = *rows.emplace_back (std::make_unique<Row> (*plugin));

        row.status.setText (describe (*plugin), juce::dontSendNotification);
        addAndMakeVisible (row.status);

        row.openButton.setEnabled (plugin->getInstance() != nullptr && plugin->getInstance()->hasEditor());
        row.openButton.onClick = [this, &row] { openHostedEditor (row); };
        addAndMakeVisible (row.openButton);
    }
}

PluginsPage::~PluginsPage() = default;

void PluginsPage::resized()
{
    auto area = getLocalBounds().reduced (margin);

    for (auto& row : rows)
    {
        auto line = area.removeFromTop (rowHeight);
        row->openButton.setBounds (line.removeFromRight (110).reduced (0, 2));
        row->status.setBounds (line);
    }
}

void PluginsPage::openHostedEditor (Row& row)
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
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<PluginsPage> (this), &row] {
            if (safeThis != nullptr)
                row.window.reset();
        });
    });

    row.window->centreAroundComponent (this, row.window->getWidth(), row.window->getHeight());
    row.window->setVisible (true);
}

//==============================================================================
SettingsPage::SettingsPage (PluginSettings& settingsIn, const juce::String& titleText, Callbacks callbacksIn)
    : settings (settingsIn), callbacks (std::move (callbacksIn))
{
    title.setText (titleText, juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f));
    addAndMakeVisible (title);

    keepWarmToggle.setToggleState (settings.getKeepWebViewWarm(), juce::dontSendNotification);
    keepWarmToggle.onClick = [this] {
        settings.setKeepWebViewWarm (keepWarmToggle.getToggleState());
        callbacks.keepWarmChanged (keepWarmToggle.getToggleState());
    };
    addAndMakeVisible (keepWarmToggle);

    using Monitor = EditorPlacement::Monitor;
    placementChoice.addItem ("Off", 1 + static_cast<int> (Monitor::off));
    placementChoice.addItem ("The monitor the host opened it on", 1 + static_cast<int> (Monitor::hostPlaced));
    placementChoice.addItem ("The monitor under the mouse", 1 + static_cast<int> (Monitor::mouse));
    placementChoice.addItem ("The monitor with the host's main window", 1 + static_cast<int> (Monitor::hostMainWindow));
    placementChoice.setSelectedId (1 + static_cast<int> (settings.getEditorPlacement()), juce::dontSendNotification);
    placementChoice.onChange = [this] { settings.setEditorPlacement (static_cast<Monitor> (placementChoice.getSelectedId() - 1)); };
    addAndMakeVisible (placementLabel);
    addAndMakeVisible (placementChoice);

    using Resizing = EditorScale::Resizing;
    resizingChoice.addItem ("Smooth", 1 + static_cast<int> (Resizing::smooth));
    resizingChoice.addItem ("Sharp steps (1-pixel lines stay crisp)", 1 + static_cast<int> (Resizing::sharp));
    resizingChoice.setSelectedId (1 + static_cast<int> (settings.getResizing()), juce::dontSendNotification);
    resizingChoice.onChange = [this] {
        settings.setResizing (static_cast<Resizing> (resizingChoice.getSelectedId() - 1));
        callbacks.resizingChanged();
    };
    addAndMakeVisible (resizingLabel);
    addAndMakeVisible (resizingChoice);

    addAndMakeVisible (sizeLabel);
    resetSizeButton.onClick = [this] { callbacks.resetSize(); };
    addAndMakeVisible (resetSizeButton);

    hostMenusToggle.setToggleState (settings.getUseHostMenus(), juce::dontSendNotification);
    hostMenusToggle.onClick = [this] { settings.setUseHostMenus (hostMenusToggle.getToggleState()); };
    addAndMakeVisible (hostMenusToggle);

    focusToggle.setToggleState (settings.getKeepKeyboardFocusInHost(), juce::dontSendNotification);
    focusToggle.onClick = [this] {
        settings.setKeepKeyboardFocusInHost (focusToggle.getToggleState());
        callbacks.focusModeChanged();
    };
    addAndMakeVisible (focusToggle);

    inspectButton.onClick = [this] { callbacks.inspect(); };
    addAndMakeVisible (inspectButton);
}

SettingsPage::~SettingsPage() = default;

void SettingsPage::setWebUiAvailable (bool available)
{
    keepWarmToggle.setEnabled (available);
}

void SettingsPage::setSizeText (const juce::String& text)
{
    sizeLabel.setText (text, juce::dontSendNotification);
}

#if JUCE_DEBUG
void SettingsPage::showKeyLog (KeyDiagnostics& diagnostics)
{
    keyLog = &diagnostics;

    keyLogStartButton.onClick = [this] {
        if (keyLog->isRunning())
            keyLog->stop();
        else
            keyLog->start();
    };

    keyLogStepButton.onClick = [this] { keyLog->note ("--- step " + juce::String (nextStep++)); };
    keyLogCopyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (keyLog->getLog()); };
    keyLogClearButton.onClick = [this] {
        keyLog->clear();
        nextStep = 1;
    };

    keyLogText.setMultiLine (true);
    keyLogText.setReadOnly (true);
    keyLogText.setScrollbarsShown (true);
    keyLogText.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));

    // Clicking into the log must not take keys away from the test
    keyLogText.setWantsKeyboardFocus (false);
    keyLogText.setMouseClickGrabsKeyboardFocus (false);

    for (auto* component : std::initializer_list<juce::Component*> { &keyLogLabel, &keyLogStartButton, &keyLogStepButton, &keyLogCopyButton, &keyLogClearButton, &keyLogText })
        addAndMakeVisible (component);

    startTimerHz (4);
    resized();
}
#endif

void SettingsPage::timerCallback()
{
#if JUCE_DEBUG
    if (keyLog == nullptr)
        return;

    keyLogStartButton.setButtonText (keyLog->isRunning() ? "Stop" : "Start");

    if (keyLog->getVersion() != shownKeyLogVersion)
    {
        shownKeyLogVersion = keyLog->getVersion();
        keyLogText.setText (keyLog->getLog(), false);
        keyLogText.moveCaretToEnd();
    }
#endif
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().reduced (margin);
    const auto line = [&area] (int height = rowHeight) { return area.removeFromTop (height).reduced (0, 2); };

    title.setBounds (line());
    keepWarmToggle.setBounds (line());

    auto placementRow = line();
    placementLabel.setBounds (placementRow.removeFromLeft (150));
    placementChoice.setBounds (placementRow.removeFromLeft (300));

    auto resizingRow = line();
    resizingLabel.setBounds (resizingRow.removeFromLeft (150));
    resizingChoice.setBounds (resizingRow.removeFromLeft (300));

    auto sizeRow = line();
    resetSizeButton.setBounds (sizeRow.removeFromRight (110));
    sizeLabel.setBounds (sizeRow);

    hostMenusToggle.setBounds (line());
    focusToggle.setBounds (line());
    inspectButton.setBounds (line().removeFromLeft (130));

#if JUCE_DEBUG
    if (keyLog == nullptr)
        return;

    area.removeFromTop (8);
    keyLogLabel.setBounds (line());

    auto buttons = line();

    for (auto* button : { &keyLogStartButton, &keyLogStepButton, &keyLogCopyButton, &keyLogClearButton })
    {
        button->setBounds (buttons.removeFromLeft (button == &keyLogStepButton ? 130 : 70));
        buttons.removeFromLeft (6);
    }

    keyLogText.setBounds (area);
#endif
}
