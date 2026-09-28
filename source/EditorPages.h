#pragma once

#include "KeyDiagnostics.h"
#include "PluginSettings.h"
#include "engine/Engine.h"
#include <juce_gui_basics/juce_gui_basics.h>

class HostedEditorWindow;

// The editor's tabs; reports changes so each instance can reopen on the tab it was closed at
class EditorTabs final : public juce::TabbedComponent
{
public:
    EditorTabs() : TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}

    std::function<void (int)> onTabChanged;

    // Room left of the tabs, e.g. for a resize handle in the corner
    int barIndent = 0;

    void resized() override
    {
        TabbedComponent::resized();
        auto& bar = getTabbedButtonBar();
        bar.setBounds (bar.getBounds().withTrimmedLeft (barIndent));
    }

    void currentTabChanged (int index, const juce::String&) override
    {
        if (onTabChanged != nullptr)
            onTabChanged (index);
    }
};

// A page whose children all fill it (the Panel tab: the web view, or the message saying it's unavailable)
class FillPage final : public juce::Component
{
public:
    void resized() override
    {
        for (auto* child : getChildren())
            child->setBounds (getLocalBounds());
    }
};

// Temporary: one line per hosted plugin, with a button that opens its own window
class PluginsPage final : public juce::Component
{
public:
    explicit PluginsPage (const Engine&);
    ~PluginsPage() override;

    void resized() override;

private:
    struct Row;
    void openHostedEditor (Row&);

    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginsPage)
};

// Temporary: every setting, plus developer tools
class SettingsPage final : public juce::Component,
                           private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void (bool)> keepWarmChanged;
        std::function<void()> resizingChanged;
        std::function<void()> resetSize;
        std::function<void()> focusModeChanged;
        std::function<void()> inspect;
    };

    SettingsPage (PluginSettings&, const juce::String& title, Callbacks);
    ~SettingsPage() override;

    void setWebUiAvailable (bool);
    void setSizeText (const juce::String&);

#if JUCE_DEBUG
    void showKeyLog (KeyDiagnostics&);
#endif

    void resized() override;

private:
    void timerCallback() override;

    PluginSettings& settings;
    Callbacks callbacks;
    juce::Label title;
    juce::ToggleButton keepWarmToggle { "Keep web UI warm (reopens instantly, uses more memory)" };
    juce::Label placementLabel { {}, "Centre on first open:" };
    juce::ComboBox placementChoice;
    juce::Label resizingLabel { {}, "Resizing:" };
    juce::ComboBox resizingChoice;
    juce::Label sizeLabel;
    juce::TextButton resetSizeButton { "Reset size" };
    juce::ToggleButton hostMenusToggle { "Use the host's UI for menus" };
    juce::ToggleButton focusToggle { "Keep keyboard focus in the host (experimental)" };
    juce::TextButton inspectButton { "Inspect the UI" };

#if JUCE_DEBUG
    KeyDiagnostics* keyLog = nullptr;
    int shownKeyLogVersion = -1;
    int nextStep = 1;
    juce::Label keyLogLabel { {}, "Key log (Debug builds only; records no key identities; stops after 2 minutes):" };
    juce::TextButton keyLogStartButton { "Start" }, keyLogStepButton { "Mark next step" }, keyLogCopyButton { "Copy" }, keyLogClearButton { "Clear" };
    juce::TextEditor keyLogText;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPage)
};
