#include "PluginEditor.h"

namespace
{
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

    juce::var describePlugin (const Engine& engine, size_t slot)
    {
        const auto& plugin = *engine.getPlugins()[slot];
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("slot", static_cast<int> (slot));
        entry->setProperty ("label", engine.getSlotLabel (slot));
        entry->setProperty ("name", plugin.getName());

        if (auto* instance = plugin.getInstance())
        {
            entry->setProperty ("status", juce::String (static_cast<int> (plugin.getControls().size())) + " of " + juce::String (instance->getParameters().size()) + " parameters");
            entry->setProperty ("canOpen", instance->hasEditor());
        }
        else
        {
            entry->setProperty ("status", "Failed to load: " + plugin.getLoadError());
            entry->setProperty ("canOpen", false);
        }

        return entry;
    }
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      pluginProcessor (p),
      keyForwarder (settings->getForwardedKeys(), KeyForwarder::postToHostOf (*this), KeyForwarder::readPhysicalModifiers),
      session (p.createSessionParameters()),
      hostMenus (*this, session)
{
    if (WebUi::isSupported())
    {
        webUi = std::make_unique<WebUi> (p.getName() + " v" VERSION, keyForwarder, session, createPageHandlers());

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

    if (!p.isEditorPlaced())
        placement = std::make_unique<EditorPlacement> (*this, settings->getEditorPlacement(), [&p] { p.setEditorPlaced(); });

    // Only our own corner handles resize the window (not the host's window frame), always at the design's aspect ratio
    setScale (p.getEditorScale().value_or (settings->getLastEditorScale()));
}

PluginEditor::~PluginEditor() = default;

WebUi::Handlers PluginEditor::createPageHandlers()
{
    WebUi::Handlers handlers;
    handlers.describeEditor = [this] { return describeEditor(); };
    handlers.openParameterMenu = [this] (const juce::var& handle) { return hostMenus.open (handle); };
    handlers.events = {
        { "menuChoice", [this] (const juce::var& choice) { hostMenus.choose (choice); } },
        { "menuClosed", [this] (const juce::var&) { hostMenus.close(); } },
        { "resize", [this] (const juce::var& message) { handleResize (message); } },
        { "resetSize", [this] (const juce::var&) { setScale (1.0); } },
        { "setting", [this] (const juce::var& message) { handleSetting (message); } },
        { "tab", [this] (const juce::var& message) { handleTab (message); } },
        { "openPlugin", [this] (const juce::var& message) { handleOpenPlugin (message); } },
    };
    return handlers;
}

juce::var PluginEditor::describeEditor() const
{
    auto* current = new juce::DynamicObject();
    current->setProperty ("keepWebViewWarm", settings->getKeepWebViewWarm());
    current->setProperty ("editorPlacement", EditorPlacement::toSettingsValue (settings->getEditorPlacement()));
    current->setProperty ("resizing", EditorScale::toSettingsValue (settings->getResizing()));
    current->setProperty ("resizeAnchor", EditorScale::toSettingsValue (settings->getResizeAnchor()));

    const auto& engine = pluginProcessor.getEngine();
    juce::Array<juce::var> plugins;

    for (size_t slot = 0; slot < engine.getPlugins().size(); ++slot)
        plugins.add (describePlugin (engine, slot));

    auto* state = new juce::DynamicObject();
    state->setProperty ("title", pluginProcessor.getName() + " v" VERSION " (" CMAKE_BUILD_TYPE ")");
    state->setProperty ("tab", pluginProcessor.getEditorTab());
    state->setProperty ("settings", current);
    state->setProperty ("plugins", plugins);
    return state;
}

void PluginEditor::handleSetting (const juce::var& message)
{
    const auto name = message["name"].toString();
    const auto& value = message["value"];

    if (name == "keepWebViewWarm" && value.isBool())
    {
        settings->setKeepWebViewWarm (static_cast<bool> (value));

        if (static_cast<bool> (value))
            webViewKeepAlive->start();
        else
            webViewKeepAlive->stop();
    }
    else if (name == "editorPlacement" && value.isString())
    {
        if (const auto monitor = EditorPlacement::fromSettingsValue (value.toString()))
            settings->setEditorPlacement (*monitor);
    }
    else if (name == "resizing" && value.isString())
    {
        if (const auto resizing = EditorScale::resizingFromSettingsValue (value.toString()))
        {
            settings->setResizing (*resizing);
            setScale (getScale());
        }
    }
    else if (name == "resizeAnchor" && value.isString())
    {
        if (const auto anchor = EditorScale::anchorFromSettingsValue (value.toString()))
            settings->setResizeAnchor (*anchor);
    }
}

void PluginEditor::handleTab (const juce::var& message)
{
    const auto& index = message["index"];

    if (index.isInt() && juce::isPositiveAndBelow (static_cast<int> (index), PluginProcessor::numEditorTabs))
        pluginProcessor.setEditorTab (static_cast<int> (index));
}

void PluginEditor::handleOpenPlugin (const juce::var& message)
{
    const auto& slot = message["slot"];
    const auto& plugins = pluginProcessor.getEngine().getPlugins();

    if (slot.isInt() && juce::isPositiveAndBelow (static_cast<int> (slot), static_cast<int> (plugins.size())))
        hostedEditors.open (*plugins[static_cast<size_t> (static_cast<int> (slot))]);
}

void PluginEditor::handleResize (const juce::var& message)
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

void PluginEditor::beginResize (Corner corner)
{
    drag = Drag { corner,
        juce::Desktop::getInstance().getMainMouseSource().getScreenPosition(),
        getScale(),
        { getWidth(), getHeight() },
        EditorPlacement::getFloatingWindowBounds (*this) };
}

void PluginEditor::continueResize()
{
    if (!drag.has_value())
        return;

    const auto moved = (juce::Desktop::getInstance().getMainMouseSource().getScreenPosition() - drag->startMouse) / getApproximateScaleFactorForComponent (this);
    const auto dx = static_cast<double> (isLeft (drag->corner) ? -moved.x : moved.x);
    const auto dy = static_cast<double> (isTop (drag->corner) ? -moved.y : moved.y);

    // Growing around its center, the window grows on both sides, so twice as fast keeps the corner under the pointer
    const auto aroundCenter = settings->getResizeAnchor() == EditorScale::Anchor::windowCenter && drag->startWindow.has_value();
    const auto speed = aroundCenter ? 2.0 : 1.0;

    using namespace EditorScale;
    const auto widthScale = drag->startScale + speed * dx / designWidth;
    const auto heightScale = drag->startScale + speed * dy / designHeight;
    setScale ((widthScale + heightScale) / 2.0);

    // Hosts keep a window's top-left corner when it grows, so the window moves to keep the anchor in place.
    // Only separate floating windows can move; attached ones grow down and to the right.
    if (!drag->startWindow.has_value() || !(aroundCenter || isLeft (drag->corner) || isTop (drag->corner)))
        return;

    const auto& start = *drag->startWindow;
    const auto physical = getPhysicalPixelsPerPixel();
    const auto width = start.getWidth() + juce::roundToInt ((getWidth() - drag->startSize.x) * physical);
    const auto height = start.getHeight() + juce::roundToInt ((getHeight() - drag->startSize.y) * physical);

    const auto topLeft = aroundCenter ? juce::Point<int> (start.getCentreX() - width / 2, start.getCentreY() - height / 2)
                                      : juce::Point<int> (isLeft (drag->corner) ? start.getRight() - width : start.getX(),
                                            isTop (drag->corner) ? start.getBottom() - height : start.getY());

    EditorPlacement::moveFloatingWindowTo (*this, topLeft);
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

double PluginEditor::getScale() const
{
    return getWidth() / static_cast<double> (EditorScale::designWidth);
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

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (WebUi::placeholderColour));
}

void PluginEditor::resized()
{
    if (webUi != nullptr)
    {
        webUi->setBounds (getLocalBounds());
        webUi->setUiScale (getScale());
    }
    else
    {
        webUiUnavailable.setBounds (getLocalBounds().reduced (10));
    }

    pluginProcessor.setEditorScale (getScale());
    settings->setLastEditorScale (getScale());
}
