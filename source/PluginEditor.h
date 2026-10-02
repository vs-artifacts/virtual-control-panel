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

    // Relative to the design size, limited and (in sharp mode) snapped
    void setScale (double);
    double getScale() const;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Drag
    {
        Corner corner;
        juce::Point<float> startMouse;
        double startScale;
        juce::Point<int> startSize;
        std::optional<juce::Rectangle<int>> startWindow;
    };

    double getPhysicalPixelsPerPixel() const;
    double constrainScale (double) const;
    WebUi::Handlers createPageHandlers();
    juce::var describeEditor() const;
    void handleResize (const juce::var&);
    void handleSetting (const juce::var&);
    void handleTab (const juce::var&);
    void handleOpenPlugin (const juce::var&);
    void reportProcessing();

    PluginProcessor& pluginProcessor;
    juce::SharedResourcePointer<PluginSettings> settings;
    juce::SharedResourcePointer<WebViewKeepAlive> webViewKeepAlive;
    KeyForwarder keyForwarder;
    ControlSession session;
    HostMenus hostMenus;
    HostedEditorWindows hostedEditors { *this };
    std::unique_ptr<WebUi> webUi;
    juce::Label webUiUnavailable;
    std::optional<Drag> drag;
    std::unique_ptr<EditorPlacement> placement;
    std::optional<bool> sentProcessing;
    juce::TimedCallback processingCheck { [this] { reportProcessing(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
