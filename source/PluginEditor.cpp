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
        entry->setProperty ("canMute", engine.canMute (slot));
        entry->setProperty ("muted", engine.isMuted (slot));

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
      keyForwarder (settings->getForwardedKeys(), KeyForwarder::postToHostOf (*this, [this] (const juce::String& line) { logKey (line); }, [this] { return keyRoute; }), KeyForwarder::readPhysicalModifiers),
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
        placement = std::make_unique<EditorPlacement> (*this, settings->getEditorPlacement(), settings->getAttachedPlacement(), [&p] { p.setEditorPlaced(); });

    // Only our own corner handles resize the window (not the host's window frame), always at the design's aspect ratio
    setScale (p.getEditorScale().value_or (settings->getLastEditorScale()));

    processingCheck.startTimerHz (4);
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
        { "mutePlugin", [this] (const juce::var& message) { handleMutePlugin (message); } },
        { "diagnostics", [this] (const juce::var& message) { handleDiagnostics (message); } },
        { "scroll", [this] (const juce::var& message) { handleScroll (message); } },
        { "bayBypass", [this] (const juce::var& message) { handleBayBypass (message); } },
        { "keyRoute", [this] (const juce::var& message) { handleKeyRoute (message); } },
        { "measureLag", [this] (const juce::var&) { pluginProcessor.getBayLink().measureLag(); } },
        { "startProbe", [this] (const juce::var&) { pluginProcessor.getBayLink().startProbe(); } },
        { "resetLinkCounters", [this] (const juce::var&) { pluginProcessor.getBayLink().resetCounters(); } },
    };
    return handlers;
}

juce::var PluginEditor::describeEditor() const
{
    auto* current = new juce::DynamicObject();
    current->setProperty ("keepWebViewWarm", settings->getKeepWebViewWarm());
    current->setProperty ("editorPlacement", EditorPlacement::toSettingsValue (settings->getEditorPlacement()));
    current->setProperty ("attachedPlacement", EditorPlacement::toSettingsValue (settings->getAttachedPlacement()));
    current->setProperty ("resizeRendering", EditorScale::toSettingsValue (settings->getResizeRendering()));
    current->setProperty ("resizeFlipAlt", settings->getFlipAltResize());
    current->setProperty ("flOutputCorrection", settings->getFlOutputCorrection());
    current->setProperty ("bayDropouts", BayLink::toName (settings->getBayDropouts()));
    current->setProperty ("roundTripBlocks", settings->getRoundTripBlocks());
    current->setProperty ("bypassCrossfadeMs", settings->getBypassCrossfadeMs());

    const auto& engine = pluginProcessor.getEngine();
    juce::Array<juce::var> plugins;

    for (size_t slot = 0; slot < engine.getPlugins().size(); ++slot)
        plugins.add (describePlugin (engine, slot));

    auto* scroll = new juce::DynamicObject();

    for (size_t area = 0; area < PluginProcessor::editorScrollAreas.size(); ++area)
        scroll->setProperty (PluginProcessor::editorScrollAreas[area], pluginProcessor.getEditorScroll (area));

    auto* state = new juce::DynamicObject();
    state->setProperty ("title", pluginProcessor.getName() + " v" VERSION " (" CMAKE_BUILD_TYPE ")");
    state->setProperty ("tab", pluginProcessor.getEditorTab());
    state->setProperty ("diagnostics", pluginProcessor.isDiagnosticsOpen());
    state->setProperty ("scroll", scroll);
    state->setProperty ("settings", current);
    state->setProperty ("plugins", plugins);
    state->setProperty ("processing", pluginProcessor.isBeingProcessed());
    state->setProperty ("link", describeLink());
    return state;
}

juce::var PluginEditor::describeLink() const
{
    const auto status = pluginProcessor.getBayLink().getStatus();
    auto* link = new juce::DynamicObject();

    const auto setIfKnown = [link] (const char* name, auto value) {
        if (value.has_value())
            link->setProperty (name, *value);
    };

    link->setProperty ("hubProblem", status.hubProblem);
    link->setProperty ("name", status.linkName);
    link->setProperty ("linkProblem", status.linkProblem);
    link->setProperty ("receiver", status.receiverPlaying ? "playing" : status.receiverLinked ? "linked"
                                                                                              : "none");
    link->setProperty ("transmitter", status.transmitterReturning ? "returning" : status.transmitterLinked ? "linked"
                                                                                                           : "none");
    link->setProperty ("roundTripSetting", status.roundTripSetting);
    link->setProperty ("autoState", BayLink::toName (status.autoState));
    link->setProperty ("roundTrip", status.roundTripSamples);
    link->setProperty ("roundTripBlocks", status.roundTripBlocks);
    link->setProperty ("maxBlock", status.maxBlock);
    link->setProperty ("sampleRate", status.sampleRate);
    link->setProperty ("latency", pluginProcessor.getLatencySamples());
    link->setProperty ("bypass", status.bypass);
    link->setProperty ("dropouts", BayLink::toName (status.dropouts));
    link->setProperty ("crossfadeMs", status.crossfadeMs);
    link->setProperty ("missing", static_cast<juce::int64> (status.missing));
    link->setProperty ("receiverMisses", static_cast<juce::int64> (status.receiverMisses));
    link->setProperty ("replaced", static_cast<juce::int64> (status.replaced));
    link->setProperty ("oversized", static_cast<juce::int64> (status.oversized));
    link->setProperty ("valueSets", static_cast<juce::int64> (status.valueSets));
    setIfKnown ("headroomNow", status.headroomNow);
    setIfKnown ("headroomLowest", status.headroomLowest);
    setIfKnown ("blockMin", status.blockMin);
    setIfKnown ("blockMax", status.blockMax);
    setIfKnown ("differenceNow", status.differenceNow);
    setIfKnown ("differenceLargest", status.differenceLargest);

    constexpr const char* lagStates[] { "notMeasured", "measuring", "found", "noReturn", "noMatch" };
    auto* lag = new juce::DynamicObject();
    lag->setProperty ("state", lagStates[static_cast<size_t> (status.lag.state)]);
    lag->setProperty ("exact", status.lag.exact);
    lag->setProperty ("earliest", status.lag.earliest);
    lag->setProperty ("latest", status.lag.latest);

    juce::Array<juce::var> paths;

    for (const auto& [samples, gain] : status.lag.paths)
    {
        auto* path = new juce::DynamicObject();
        path->setProperty ("samples", samples);
        path->setProperty ("gain", gain);
        paths.add (path);
    }

    lag->setProperty ("paths", paths);
    link->setProperty ("lag", lag);

    constexpr const char* probeStates[] { "notRun", "running", "found", "noReturn", "noChange" };
    auto* probe = new juce::DynamicObject();
    probe->setProperty ("state", probeStates[static_cast<size_t> (status.probe.state)]);
    probe->setProperty ("delay", status.probe.delay);
    probe->setProperty ("rise", status.probe.rise);
    probe->setProperty ("before", status.probe.before);
    probe->setProperty ("after", status.probe.after);
    link->setProperty ("probe", probe);
    return link;
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
    else if (name == "attachedPlacement" && value.isString())
    {
        if (const auto attached = EditorPlacement::attachedFromSettingsValue (value.toString()))
            settings->setAttachedPlacement (*attached);
    }
    else if (name == "resizeRendering" && value.isString())
    {
        if (const auto rendering = EditorScale::renderingFromSettingsValue (value.toString()))
            settings->setResizeRendering (*rendering);
    }
    else if (name == "resizeFlipAlt" && value.isBool())
    {
        settings->setFlipAltResize (static_cast<bool> (value));
    }
    else if (name == "flOutputCorrection" && value.isBool())
    {
        settings->setFlOutputCorrection (static_cast<bool> (value));
    }
    else if (name == "bayDropouts" && value.isString())
    {
        if (const auto dropouts = BayLink::dropoutsFromName (value.toString()))
        {
            settings->setBayDropouts (*dropouts);
            pluginProcessor.applyLinkSettings();
        }
    }
    else if (name == "roundTripBlocks" && value.isInt())
    {
        const auto blocks = static_cast<int> (value);

        if (blocks == BayLink::autoRoundTrip || (blocks >= BayLink::minRoundTripBlocks && blocks <= BayLink::maxRoundTripBlocks))
        {
            settings->setRoundTripBlocks (blocks);
            pluginProcessor.applyLinkSettings();
        }
    }
    else if (name == "bypassCrossfadeMs" && value.isInt())
    {
        const auto milliseconds = static_cast<int> (value);

        if (milliseconds >= 0 && milliseconds <= BayLink::maxCrossfadeMs)
        {
            settings->setBypassCrossfadeMs (milliseconds);
            pluginProcessor.applyLinkSettings();
        }
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

void PluginEditor::handleMutePlugin (const juce::var& message)
{
    const auto& slot = message["slot"];
    const auto& muted = message["muted"];

    // The engine refuses slots that aren't generators
    if (slot.isInt() && static_cast<int> (slot) >= 0 && muted.isBool())
        pluginProcessor.getEngine().setMuted (static_cast<size_t> (static_cast<int> (slot)), static_cast<bool> (muted));
}

void PluginEditor::handleDiagnostics (const juce::var& message)
{
    const auto& open = message["open"];

    if (open.isBool())
        pluginProcessor.setDiagnosticsOpen (static_cast<bool> (open));
}

void PluginEditor::handleScroll (const juce::var& message)
{
    const auto& name = message["area"];
    const auto& top = message["top"];

    if (!name.isString() || !top.isInt() || !juce::isPositiveAndNotGreaterThan (static_cast<int> (top), PluginProcessor::maxEditorScroll))
        return;

    if (const auto area = PluginProcessor::findEditorScrollArea (name.toString()))
        pluginProcessor.setEditorScroll (*area, static_cast<int> (top));
}

void PluginEditor::handleBayBypass (const juce::var& message)
{
    const auto& value = message["value"];

    if (value.isBool())
        pluginProcessor.getBayLink().setBypass (static_cast<bool> (value));
}

void PluginEditor::handleKeyRoute (const juce::var& message)
{
    const auto& name = message["route"];

    if (!name.isString())
        return;

    if (const auto route = KeyForwarder::heldMouseRouteFromName (name.toString()))
        keyRoute = *route;
}

void PluginEditor::reportLink()
{
    if (webUi != nullptr && webUi->isShowing())
        webUi->emitEventIfBrowserIsVisible ("link", describeLink());
}

void PluginEditor::logKey (const juce::String& line)
{
    keyLog.insert (0, juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + line);
    keyLog.removeRange (maxKeyLogLines, keyLog.size());
}

void PluginEditor::reportHostDiagnostics()
{
    if (webUi == nullptr || !webUi->isShowing())
        return;

    auto* message = new juce::DynamicObject();
    message->setProperty ("keys", keyLog);
    message->setProperty ("keyRoute", KeyForwarder::toName (keyRoute));
    message->setProperty ("windows", EditorPlacement::describeWindows (*this));
    const juce::var diagnostics (message);

    if (const auto text = juce::JSON::toString (diagnostics, true); text != sentHostDiagnostics)
    {
        sentHostDiagnostics = text;
        webUi->emitEventIfBrowserIsVisible ("hostDiagnostics", diagnostics);
    }
}

void PluginEditor::reportProcessing()
{
    const auto processing = pluginProcessor.isBeingProcessed();

    // Re-sent once shown, so a hidden page catches up
    if (webUi == nullptr || !webUi->isShowing() || sentProcessing == processing)
        return;

    sentProcessing = processing;
    auto* message = new juce::DynamicObject();
    message->setProperty ("running", processing);
    webUi->emitEventIfBrowserIsVisible ("processing", message);
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
    drag = startDrag (corner, wantsCenterAnchor());
}

PluginEditor::Drag PluginEditor::startDrag (Corner corner, bool aroundCenter) const
{
    return { corner,
        juce::Desktop::getInstance().getMainMouseSource().getScreenPosition(),
        getScale(),
        { getWidth(), getHeight() },
        EditorPlacement::getMovableWindowBounds (*this),
        aroundCenter,
        std::nullopt };
}

bool PluginEditor::wantsCenterAnchor() const
{
    return KeyForwarder::readPhysicalModifiers().alt != settings->getFlipAltResize();
}

void PluginEditor::continueResize()
{
    if (!drag.has_value())
        return;

    const auto modifiers = KeyForwarder::readPhysicalModifiers();

    // Alt pressed or released mid-drag: carry on from where the window is, so it doesn't jump
    if (const auto aroundCenter = modifiers.alt != settings->getFlipAltResize(); aroundCenter != drag->aroundCenter)
    {
        const auto virtualStep = drag->virtualStep;
        drag = startDrag (drag->corner, aroundCenter);
        drag->virtualStep = virtualStep;
    }

    // Ctrl steps by 25%, and the size Ctrl went down at stays a step until Ctrl is released
    if (!modifiers.ctrl)
        drag->virtualStep.reset();
    else if (!drag->virtualStep.has_value())
        drag->virtualStep = getScale();

    const auto moved = (juce::Desktop::getInstance().getMainMouseSource().getScreenPosition() - drag->startMouse) / getApproximateScaleFactorForComponent (this);
    const auto dx = static_cast<double> (isLeft (drag->corner) ? -moved.x : moved.x);
    const auto dy = static_cast<double> (isTop (drag->corner) ? -moved.y : moved.y);

    // Growing around its center, the window grows on both sides, so twice as fast keeps the corner under the pointer.
    // Only windows we can move (floating ones, and FL's attached ones) can grow that way; others grow down and to the right.
    const auto aroundCenter = drag->aroundCenter && drag->startWindow.has_value();
    const auto speed = aroundCenter ? 2.0 : 1.0;

    using namespace EditorScale;
    const auto widthScale = drag->startScale + speed * dx / designWidth;
    const auto heightScale = drag->startScale + speed * dy / designHeight;

    // Placeholder: Shift will set width and height apart (each stepped with Ctrl) once the panel's layout can follow
    auto scale = (widthScale + heightScale) / 2.0;

    if (drag->virtualStep.has_value())
        scale = snap (scale, *drag->virtualStep);

    setScale (scale);

    // Hosts keep a window's top-left corner when it grows, so the window moves to keep the anchor in place
    if (!drag->startWindow.has_value() || !(aroundCenter || isLeft (drag->corner) || isTop (drag->corner)))
        return;

    const auto& start = *drag->startWindow;
    const auto physical = getPhysicalPixelsPerPixel();
    const auto width = start.getWidth() + juce::roundToInt ((getWidth() - drag->startSize.x) * physical);
    const auto height = start.getHeight() + juce::roundToInt ((getHeight() - drag->startSize.y) * physical);

    const auto topLeft = aroundCenter ? juce::Point<int> (start.getCentreX() - width / 2, start.getCentreY() - height / 2)
                                      : juce::Point<int> (isLeft (drag->corner) ? start.getRight() - width : start.getX(),
                                            isTop (drag->corner) ? start.getBottom() - height : start.getY());

    EditorPlacement::moveWindowTo (*this, topLeft);
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
    return EditorScale::constrain (scale);
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
