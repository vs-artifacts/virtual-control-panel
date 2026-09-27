#include "WebViewKeepAlive.h"
#include "WebUi.h"

namespace
{
    // It only has to exist: it never loads a page and has no bridge
    struct EmptyWebView final : juce::WebBrowserComponent
    {
        EmptyWebView() : WebBrowserComponent (WebUi::createPlatformOptions()) {}

        bool pageAboutToLoad (const juce::String&) override { return false; }
    };

    struct HiddenWindow final : juce::Component
    {
        HiddenWindow()
        {
            setSize (1, 1);
            webView.setBounds (getLocalBounds());
            addAndMakeVisible (webView);

            // Never shown; on the desktop only so the web view gets a native parent window
            addToDesktop (juce::ComponentPeer::windowIsTemporary);
        }

        EmptyWebView webView;
    };
}

WebViewKeepAlive::WebViewKeepAlive() = default;

WebViewKeepAlive::~WebViewKeepAlive() = default;

void WebViewKeepAlive::start()
{
    if (window == nullptr && WebUi::isSupported())
        window = std::make_unique<HiddenWindow>();
}

void WebViewKeepAlive::stop()
{
    window.reset();
}
