#include "PluginEditor.h"

namespace
{
    constexpr int cornerSize = 16;
    constexpr int tabBarDepth = 28;
    const auto background = juce::Colour (0xff1b1f3b);

    using Corner = PluginEditor::Corner;

    bool isLeft (Corner corner) { return corner == Corner::topLeft || corner == Corner::bottomLeft; }
    bool isTop (Corner corner) { return corner == Corner::topLeft || corner == Corner::topRight; }

    std::optional<Corner> cornerFromName (const juce::var& name)
    {
        constexpr std::pair<Corner, const char*> names[] {
            { Corner::topLeft, "topLeft" },
            { Corner::topRight, "topRight" },
            { Corner::bottomLeft, "bottomLeft" },
            { Corner::bottomRight, "bottomRight" },
        };

        if (name.isString())
            for (const auto& [corner, text] : names)
                if (name.toString() == text)
                    return corner;

        return std::nullopt;
    }
}

// A drag handle in one corner of the editor
class CornerHandle final : public juce::Component
{
public:
    CornerHandle (PluginEditor& editorIn, Corner cornerIn) : editor (editorIn), corner (cornerIn)
    {
        using Cursor = juce::MouseCursor;
        setMouseCursor (isTop (corner) ? (isLeft (corner) ? Cursor::TopLeftCornerResizeCursor : Cursor::TopRightCornerResizeCursor)
                                       : (isLeft (corner) ? Cursor::BottomLeftCornerResizeCursor : Cursor::BottomRightCornerResizeCursor));
        setWantsKeyboardFocus (false);
    }

    Corner getCorner() const noexcept { return corner; }

    void paint (juce::Graphics& g) override
    {
        // Two short strokes across the corner
        const auto area = getLocalBounds().toFloat().reduced (3.0f);
        const auto tip = juce::Point<float> (isLeft (corner) ? area.getX() : area.getRight(), isTop (corner) ? area.getY() : area.getBottom());
        const auto across = juce::Point<float> (isLeft (corner) ? 1.0f : -1.0f, isTop (corner) ? 1.0f : -1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.35f));

        for (const auto length : { 0.5f, 0.9f })
            g.drawLine (tip.x + across.x * length * area.getWidth(), tip.y, tip.x, tip.y + across.y * length * area.getHeight(), 1.0f);
    }

    void mouseDown (const juce::MouseEvent&) override { editor.beginResize (corner); }
    void mouseDrag (const juce::MouseEvent&) override { editor.continueResize(); }
    void mouseUp (const juce::MouseEvent&) override { editor.endResize(); }

private:
    PluginEditor& editor;
    Corner corner;
};

void ScaleConstrainer::checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previousBounds, const juce::Rectangle<int>& limits, bool isStretchingTop, bool isStretchingLeft, bool isStretchingBottom, bool isStretchingRight)
{
    ComponentBoundsConstrainer::checkBounds (bounds, previousBounds, limits, isStretchingTop, isStretchingLeft, isStretchingBottom, isStretchingRight);

    if (constrainScale == nullptr)
        return;

    const auto scale = constrainScale (bounds.getWidth() / static_cast<double> (EditorScale::designWidth));
    const auto width = juce::roundToInt (EditorScale::designWidth * scale);
    const auto height = juce::roundToInt (EditorScale::designHeight * scale);

    bounds = { isStretchingLeft ? bounds.getRight() - width : bounds.getX(), isStretchingTop ? bounds.getBottom() - height : bounds.getY(), width, height };
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      pluginProcessor (p),
      keyForwarder (settings->getForwardedKeys(), withKeyLog (KeyForwarder::postToHostOf (*this)), KeyForwarder::readPhysicalModifiers),
      session (p.createSessionParameters()),
      hostMenus (*this, session, *settings)
{
    if (WebUi::isSupported())
    {
        WebUi::Handlers handlers;
        handlers.openParameterMenu = [this] (const juce::var& handle) { return hostMenus.open (handle); };
        handlers.chooseMenuItem = [this] (const juce::var& choice) { hostMenus.choose (choice); };
        handlers.closeMenu = [this] { hostMenus.close(); };
        handlers.resize = [this] (const juce::var& message) { handlePageResize (message); };
        handlers.pointerDown = [this] (const juce::var& press) { handlePagePointerDown (press); };
        webUi = std::make_unique<WebUi> (p.getName() + " v" VERSION, keyForwarder, session, std::move (handlers));

        // Started after our own page, so it never delays the first editor open
        webUi->onPageLoaded = [this] {
            if (settings->getKeepWebViewWarm())
                webViewKeepAlive->start();
        };

        panelPage.addAndMakeVisible (*webUi);
    }
    else
    {
        webUiUnavailable.setText ("Web UI unavailable: this system has no supported web view (on Windows, install the Microsoft Edge WebView2 Runtime).", juce::dontSendNotification);
        webUiUnavailable.setJustificationType (juce::Justification::centred);
        panelPage.addAndMakeVisible (webUiUnavailable);
    }

    pluginsPage = std::make_unique<PluginsPage> (p.getEngine());

    SettingsPage::Callbacks callbacks;
    callbacks.keepWarmChanged = [this] (bool keepWarm) {
        if (keepWarm)
            webViewKeepAlive->start();
        else
            webViewKeepAlive->stop();
    };
    callbacks.resizingChanged = [this] { setScale (getWidth() / static_cast<double> (EditorScale::designWidth)); };
    callbacks.resetSize = [this] { setScale (1.0); };
    callbacks.focusModeChanged = [this] { applyFocusMode(); };
    callbacks.inspect = [this] {
        if (inspector == nullptr)
        {
            inspector = std::make_unique<melatonin::Inspector> (*this);
            inspector->onClose = [this] { inspector.reset(); };
        }

        inspector->setVisible (true);
    };

    settingsPage = std::make_unique<SettingsPage> (*settings, p.getName() + " v" VERSION " (" CMAKE_BUILD_TYPE ")", std::move (callbacks));
    settingsPage->setWebUiAvailable (webUi != nullptr);
#if JUCE_DEBUG
    settingsPage->showKeyLog (keyDiagnostics);
#endif

    tabs.setTabBarDepth (tabBarDepth);
    tabs.barIndent = cornerSize;
    tabs.addTab ("Panel", background, &panelPage, false);
    tabs.addTab ("Plugins", background, pluginsPage.get(), false);
    tabs.addTab ("Settings", background, settingsPage.get(), false);
    tabs.setCurrentTabIndex (p.getEditorTab(), false);
    tabs.onTabChanged = [&p] (int index) { p.setEditorTab (index); };
    tabs.setBounds (0, 0, EditorScale::designWidth, EditorScale::designHeight);
    content.setBounds (tabs.getBounds());
    content.addAndMakeVisible (tabs);
    addAndMakeVisible (content);

    for (const auto corner : { Corner::topLeft, Corner::topRight, Corner::bottomLeft, Corner::bottomRight })
        addAndMakeVisible (*corners.emplace_back (std::make_unique<CornerHandle> (*this, corner)));

    using namespace EditorScale;
    constrainer.setFixedAspectRatio (designWidth / static_cast<double> (designHeight));
    constrainer.setSizeLimits (juce::roundToInt (designWidth * minimum), juce::roundToInt (designHeight * minimum), juce::roundToInt (designWidth * maximum), juce::roundToInt (designHeight * maximum));
    constrainer.constrainScale = [this] (double scale) { return constrainScale (scale); };
    setConstrainer (&constrainer);
    setResizable (true, false);

    // Every click in the editor, for keeping keyboard focus in the host
    addMouseListener (this, true);
    applyFocusMode();

    if (!p.isEditorPlaced())
        placement = std::make_unique<EditorPlacement> (*this, settings->getEditorPlacement(), [&p] { p.setEditorPlaced(); });

    setScale (p.getEditorScale().value_or (settings->getLastEditorScale()));
}

PluginEditor::~PluginEditor()
{
    removeMouseListener (this);
    tabs.clearTabs();
    setConstrainer (nullptr);
}

void PluginEditor::beginResize (Corner corner)
{
    drag = Drag { corner, juce::Desktop::getInstance().getMainMouseSource().getScreenPosition(), getWidth() / static_cast<double> (EditorScale::designWidth) };
}

void PluginEditor::continueResize()
{
    if (!drag.has_value())
        return;

    const auto moved = (juce::Desktop::getInstance().getMainMouseSource().getScreenPosition() - drag->startMouse) / getApproximateScaleFactorForComponent (this);
    const auto dx = static_cast<double> (isLeft (drag->corner) ? -moved.x : moved.x);
    const auto dy = static_cast<double> (isTop (drag->corner) ? -moved.y : moved.y);

    using namespace EditorScale;
    const auto widthScale = drag->startScale + dx / designWidth;
    const auto heightScale = drag->startScale + dy / designHeight;

    const auto oldSize = juce::Point<int> (getWidth(), getHeight());
    setScale ((widthScale + heightScale) / 2.0);
    const auto grown = juce::Point<int> (getWidth(), getHeight()) - oldSize;

    // Hosts grow windows from their top-left corner, so a top or left handle moves the window to stay under the pointer
    if (grown != juce::Point<int>() && (isLeft (drag->corner) || isTop (drag->corner)))
    {
        const auto physical = getPhysicalPixelsPerPixel();
        EditorPlacement::moveFloatingWindowBy (*this, { isLeft (drag->corner) ? -juce::roundToInt (grown.x * physical) : 0, isTop (drag->corner) ? -juce::roundToInt (grown.y * physical) : 0 });
    }
}

void PluginEditor::endResize()
{
    drag.reset();
}

void PluginEditor::setScale (double scale)
{
    const auto constrained = constrainScale (scale);
    setSize (juce::roundToInt (EditorScale::designWidth * constrained), juce::roundToInt (EditorScale::designHeight * constrained));
}

double PluginEditor::constrainScale (double scale) const
{
    return EditorScale::constrain (scale, settings->getResizing(), getPhysicalPixelsPerPixel());
}

double PluginEditor::getPhysicalPixelsPerPixel() const
{
    auto scale = static_cast<double> (getApproximateScaleFactorForComponent (this));

    if (auto* peer = getPeer())
        scale *= peer->getPlatformScaleFactor();

    return scale;
}

void PluginEditor::applyFocusMode()
{
    const auto grabsFocus = !settings->getKeepKeyboardFocusInHost();

    // Text fields keep their own behaviour: they need focus to be typed into
    const std::function<void (juce::Component&)> apply = [&] (juce::Component& component) {
        if (dynamic_cast<juce::TextEditor*> (&component) == nullptr)
            component.setMouseClickGrabsKeyboardFocus (grabsFocus);

        for (auto* child : component.getChildren())
            apply (*child);
    };

    // Pages on hidden tabs aren't in the tree
    for (auto* page : std::initializer_list<juce::Component*> { this, &panelPage, pluginsPage.get(), settingsPage.get() })
        apply (*page);
}

void PluginEditor::mouseDown (const juce::MouseEvent&)
{
#if JUCE_DEBUG
    keyDiagnostics.note ("editor mouse down");
#endif

    if (settings->getKeepKeyboardFocusInHost())
        KeyForwarder::giveFocusToHostOf (*this);
}

void PluginEditor::handlePageResize (const juce::var& message)
{
    const auto corner = cornerFromName (message["corner"]);
    const auto phase = message["phase"].toString();

    if (!corner.has_value())
        return;

    if (phase == "start")
        beginResize (*corner);
    else if (phase == "move" && drag.has_value() && drag->corner == *corner)
        continueResize();
    else if (phase == "end")
        endResize();
}

void PluginEditor::handlePagePointerDown (const juce::var& press)
{
    const auto& textEntry = press["textEntry"];
    const auto isTextEntry = textEntry.isBool() && static_cast<bool> (textEntry);

#if JUCE_DEBUG
    keyDiagnostics.note (isTextEntry ? "page pointer down (text field)" : "page pointer down");
#endif

    if (!isTextEntry && settings->getKeepKeyboardFocusInHost())
        KeyForwarder::giveFocusToHostOf (*this);
}

KeyForwarder::Post PluginEditor::withKeyLog (KeyForwarder::Post post)
{
#if JUCE_DEBUG
    return [this, post = std::move (post)] (std::uint32_t scanCode, bool down, bool isRepeat) {
        keyDiagnostics.note (juce::String ("forwarder posts key ") + (down ? "down" : "up") + (isRepeat ? " (repeat)" : ""));
        post (scanCode, down, isRepeat);
    };
#else
    return post;
#endif
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void PluginEditor::resized()
{
    const auto scale = getWidth() / static_cast<double> (EditorScale::designWidth);
    content.setTransform (juce::AffineTransform::scale (static_cast<float> (scale)));

    const auto handleSize = std::max (10, juce::roundToInt (cornerSize * scale));

    for (auto& handle : corners)
    {
        const auto corner = handle->getCorner();
        handle->setBounds (isLeft (corner) ? 0 : getWidth() - handleSize, isTop (corner) ? 0 : getHeight() - handleSize, handleSize, handleSize);
    }

    pluginProcessor.setEditorScale (scale);
    settings->setLastEditorScale (scale);
    settingsPage->setSizeText ("Size: " + juce::String (getWidth()) + " x " + juce::String (getHeight()) + " (" + juce::String (juce::roundToInt (scale * 100.0)) + "%)");
}
