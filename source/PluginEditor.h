#pragma once

#include "BinaryData.h"
#include "EditorPlacement.h"
#include "EditorScale.h"
#include "HostMenus.h"
#include "HostedEditorWindows.h"
#include "KeyForwarder.h"
#include "PluginProcessor.h"
#include "PluginSettings.h"
#include "WebUi.h"
#include "WebViewKeepAlive.h"

//==============================================================================
// The whole editor is our web page; C++ keeps the parts only it can do (window size and position, host menus,
// hosted plugins' own windows, settings)
class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    enum class Corner {
        topLeft,
        topRight,
        bottomLeft,
        bottomRight,
    };

    // A corner drag from the page; follows the mouse pointer
    void beginResize (Corner);
    void continueResize();
    void endResize();

    // Relative to the design size, limited
    void setScale (double);
    double getScale() const { return scale; }

    // The page's layout area as shares of the design size on each axis, limited; the scale stays
    void setLayout (juce::Point<double>);
    juce::Point<double> getLayout() const { return layout; }

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Drag
    {
        Corner corner;
        juce::Point<float> startMouse;
        double startScale;
        juce::Point<double> startLayout;
        juce::Point<int> startSize;
        std::optional<juce::Rectangle<int>> startWindow;
        bool aroundCenter;
        bool freeAspect; // Shift: width and height apart (the layout), not the scale
        std::optional<double> virtualStep; // with Ctrl: the size when Ctrl went down, a step of its own
        std::optional<juce::Point<double>> virtualLayoutStep; // likewise for each axis of the layout
    };

    Drag startDrag (Corner, bool aroundCenter, bool freeAspect) const;
    bool wantsCenterAnchor() const;
    void guardLanguageSwitch();
    void applySize();

    double getPhysicalPixelsPerPixel() const;
    WebUi::Handlers createPageHandlers();
    juce::var describeEditor() const;
    juce::var describeLink() const;
    void handleResize (const juce::var&);
    void handleSetting (const juce::var&);
    void handleTab (const juce::var&);
    void handleOpenPlugin (const juce::var&);
    void handleMutePlugin (const juce::var&);
    void handleDiagnostics (const juce::var&);
    void handleScroll (const juce::var&);
    void handleBayBypass (const juce::var&);
    void reportProcessing();
    void reportLink();
    void logKey (const juce::String&);
    void reportHostDiagnostics();

    PluginProcessor& pluginProcessor;
    juce::SharedResourcePointer<PluginSettings> settings;
    juce::SharedResourcePointer<WebViewKeepAlive> webViewKeepAlive;

    // Newest first (diagnostic); before the key forwarder, which still releases keys as it goes
    juce::StringArray keyLog;
    static constexpr int maxKeyLogLines = 12;
    juce::String sentHostDiagnostics;

    KeyForwarder keyForwarder;
    ControlSession session;
    HostMenus hostMenus;
    HostedEditorWindows hostedEditors { *this };
    std::unique_ptr<WebUi> webUi;
    juce::Label webUiUnavailable;
    std::optional<Drag> drag;
    bool chordInterrupted = false; // for the modifiers held now
    static constexpr int languageSwitchGuardHz = 100; // a quick Shift tap is still several checks long
    juce::TimedCallback languageSwitchGuard { [this] { guardLanguageSwitch(); } };
    double scale = 1.0;
    juce::Point<double> layout { 1.0, 1.0 };
    std::unique_ptr<EditorPlacement> placement;
    std::optional<bool> sentProcessing;
    juce::TimedCallback processingCheck { [this] {
        reportProcessing();
        reportLink();
        reportHostDiagnostics();
    } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
