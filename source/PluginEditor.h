#pragma once

#include "BinaryData.h"
#include "EditorPages.h"
#include "EditorPlacement.h"
#include "EditorScale.h"
#include "HostMenus.h"
#include "KeyDiagnostics.h"
#include "KeyForwarder.h"
#include "PluginProcessor.h"
#include "PluginSettings.h"
#include "WebUi.h"
#include "WebViewKeepAlive.h"
#include "melatonin_inspector/melatonin_inspector.h"

class CornerHandle;

// Keeps the design's aspect ratio and, when resizing in sharp steps, snaps to them. Plugin hosts dragging their own
// window frame use only its limits and aspect ratio (JUCE's VST3 wrapper), so those resizes are always smooth.
class ScaleConstrainer final : public juce::ComponentBoundsConstrainer
{
public:
    std::function<double (double)> constrainScale;

    void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previousBounds, const juce::Rectangle<int>& limits, bool isStretchingTop, bool isStretchingLeft, bool isStretchingBottom, bool isStretchingRight) override;
};

//==============================================================================
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

    // A corner drag, from our own handles or the page's; follows the mouse pointer
    void beginResize (Corner);
    void continueResize();
    void endResize();

    // Relative to the design size, limited and (in sharp mode) snapped
    void setScale (double);

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Drag
    {
        Corner corner;
        juce::Point<float> startMouse;
        double startScale;
    };

    double getPhysicalPixelsPerPixel() const;
    double constrainScale (double) const;
    void applyFocusMode();
    void handlePageResize (const juce::var&);
    void handlePagePointerDown (const juce::var&);
    KeyForwarder::Post withKeyLog (KeyForwarder::Post);

    PluginProcessor& pluginProcessor;
    juce::SharedResourcePointer<PluginSettings> settings;
    juce::SharedResourcePointer<WebViewKeepAlive> webViewKeepAlive;
#if JUCE_DEBUG
    KeyDiagnostics keyDiagnostics { *this };
#endif
    KeyForwarder keyForwarder;
    ControlSession session;
    HostMenus hostMenus;
    ScaleConstrainer constrainer;

    // Laid out at the design size and scaled as a whole
    juce::Component content;
    EditorTabs tabs;
    FillPage panelPage;
    std::unique_ptr<WebUi> webUi;
    juce::Label webUiUnavailable;
    std::unique_ptr<PluginsPage> pluginsPage;
    std::unique_ptr<SettingsPage> settingsPage;

    std::vector<std::unique_ptr<CornerHandle>> corners;
    std::optional<Drag> drag;
    std::unique_ptr<EditorPlacement> placement;
    std::unique_ptr<melatonin::Inspector> inspector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
