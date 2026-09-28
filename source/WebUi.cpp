#include "WebUi.h"
#include "bridge/UiFiles.h"

namespace
{
    juce::var describeDisplay (double scale)
    {
        auto* display = new juce::DynamicObject();
        display->setProperty ("scale", scale);
        return display;
    }
}

WebUi::WebUi (const juce::String& helloReply, KeyForwarder& keys, ControlSession& sessionIn, Handlers handlers)
    : WebBrowserComponent (createOptions (helloReply, keys, sessionIn, handlers, *this)),
      session (sessionIn)
{
    goToURL (getResourceProviderRoot());
    startTimerHz (ControlSession::updateRateHz);
}

bool WebUi::isSupported()
{
    // Each check briefly starts a browser session, so do it once per process
    static const bool supported = areOptionsSupported (createPlatformOptions());
    return supported;
}

juce::WebBrowserComponent::Options WebUi::createPlatformOptions()
{
    Options options;

#if JUCE_WINDOWS
    // Plugins can't write next to the host's executable, which is where WebView2 keeps its data by default
    const auto userDataFolder = juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                                    .getChildFile (JucePlugin_Name)
                                    .getChildFile ("WebView2");

    // The default Windows backend is the obsolete Internet Explorer engine
    options = options.withBackend (Options::Backend::webview2)
                  .withWinWebView2Options (Options::WinWebView2 {}
                          .withUserDataFolder (userDataFolder)
                          .withStatusBarDisabled()
                          .withBuiltInErrorPageDisabled()
                          .withBackgroundColour (juce::Colour (placeholderColour)));
#endif

    return options;
}

bool WebUi::isAllowedNavigation (const juce::String& url)
{
    // JUCE swaps in a blank page while the editor is hidden
    return url.startsWith (getResourceProviderRoot()) || url == "about:blank";
}

juce::WebBrowserComponent::Resource WebUi::serve (const juce::String& path)
{
    const auto file = findUiFile (path);

    if (!file.has_value())
        return { {}, "text/plain" };

    const auto* bytes = reinterpret_cast<const std::byte*> (file->data);
    return { { bytes, bytes + file->size }, file->mimeType };
}

void WebUi::paint (juce::Graphics& g)
{
    // JUCE's placeholder is plain white; it's also where JUCE starts the web view if nothing else has
    WebBrowserComponent::paint (g);
    g.fillAll (juce::Colour (placeholderColour));
}

bool WebUi::pageAboutToLoad (const juce::String& newURL)
{
    return isAllowedNavigation (newURL);
}

void WebUi::pageFinishedLoading (const juce::String&)
{
    if (onPageLoaded != nullptr)
        onPageLoaded();
}

bool WebUi::pageLoadHadNetworkError (const juce::String&)
{
    // JUCE's default shows an error page from a data: URL, which isAllowedNavigation refuses anyway
    return false;
}

void WebUi::timerCallback()
{
    // Skipped while hidden, so the page catches up on everything it missed once it's shown again
    if (!isShowing())
        return;

    if (const auto scale = getDisplayScale(); !juce::approximatelyEqual (scale, sentDisplayScale))
    {
        sentDisplayScale = scale;
        emitEventIfBrowserIsVisible ("display", describeDisplay (scale));
    }

    if (const auto changes = session.poll(); !changes.isVoid())
        emitEventIfBrowserIsVisible ("params", changes);
}

double WebUi::getDisplayScale() const
{
    auto scale = static_cast<double> (getApproximateScaleFactorForComponent (this));

    // The same factor JUCE applies when it positions the web view
    if (auto* peer = getPeer())
        scale *= peer->getPlatformScaleFactor();

    return scale;
}

juce::WebBrowserComponent::Options WebUi::createOptions (const juce::String& helloReply, KeyForwarder& keys, ControlSession& session, const Handlers& handlers, WebUi& self)
{
    auto options = createPlatformOptions()
                       .withKeepPageLoadedWhenBrowserIsHidden()
                       .withNativeIntegrationEnabled()
                       .withResourceProvider ([] (const juce::String& path) { return std::optional (serve (path)); })
                       .withNativeFunction ("hello", [helloReply] (const juce::Array<juce::var>&, NativeFunctionCompletion complete) {
                           complete (helloReply);
                       })
                       .withNativeFunction ("display", [&self] (const juce::Array<juce::var>&, NativeFunctionCompletion complete) {
                           complete (describeDisplay (self.getDisplayScale()));
                       })
                       .withNativeFunction ("catalog", [&session] (const juce::Array<juce::var>&, NativeFunctionCompletion complete) {
                           complete (session.catalog());
                       })
                       .withNativeFunction ("parameterMenu", [open = handlers.openParameterMenu] (const juce::Array<juce::var>& arguments, NativeFunctionCompletion complete) {
                           complete (open != nullptr && arguments.size() == 1 ? open (arguments[0]) : juce::var());
                       })
                       .withEventListener ("menuChoice", [choose = handlers.chooseMenuItem] (const juce::var& choice) {
                           if (choose != nullptr)
                               choose (choice);
                       })
                       .withEventListener ("menuClosed", [close = handlers.closeMenu] (const juce::var&) {
                           if (close != nullptr)
                               close();
                       })
                       .withEventListener ("resize", [resize = handlers.resize] (const juce::var& drag) {
                           if (resize != nullptr)
                               resize (drag);
                       })
                       .withEventListener ("pointerDown", [pointerDown = handlers.pointerDown] (const juce::var& press) {
                           if (pointerDown != nullptr)
                               pointerDown (press);
                       })
                       .withEventListener ("set", [&session] (const juce::var& batch) { session.applySet (batch); })
                       .withEventListener ("gesture", [&session] (const juce::var& gesture) { session.applyGesture (gesture); })
                       .withEventListener ("key", [&keys] (const juce::var& event) { keys.handleKeyEvent (event); })
                       .withEventListener ("releaseKeys", [&keys] (const juce::var&) { keys.releaseAll(); });

    for (const auto& chord : keys.getAllowedChords())
        options = options.withInitialisationData ("forwardedKeys", chord);

    return options;
}
