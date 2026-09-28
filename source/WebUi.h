#pragma once

#include "KeyForwarder.h"
#include "bridge/ControlSession.h"
#include <juce_gui_extra/juce_gui_extra.h>

// The editor's web UI: our embedded page in a web view, talking to C++ through JUCE's native bridge (no network)
class WebUi final : public juce::WebBrowserComponent,
                    private juce::Timer
{
public:
    // What the page asks of the editor; any may be left empty
    struct Handlers
    {
        std::function<juce::var (const juce::var& handle)> openParameterMenu;
        std::function<void (const juce::var& choice)> chooseMenuItem;
        std::function<void()> closeMenu;
        std::function<void (const juce::var& drag)> resize;
        std::function<void (const juce::var& press)> pointerDown;
    };

    WebUi (const juce::String& helloReply, KeyForwarder&, ControlSession&, Handlers = {});

    // False if the platform's web view is missing (e.g. no WebView2 runtime); never fall back to another engine
    static bool isSupported();

    // Engine and data folder settings; every web view we create must share them to share one browser process
    static Options createPlatformOptions();

    // Only our own embedded page may load; links and redirects anywhere else are refused
    static bool isAllowedNavigation (const juce::String& url);

    // Answers every request from our own files, so nothing ever goes out to the network
    static Resource serve (const juce::String& path);

    // Shown until the web view has drawn the page
    static constexpr juce::uint32 placeholderColour = 0xff0f1012;

    // Physical pixels per pixel of our UI here; the page zooms to match, since the web view itself follows the monitor
    double getDisplayScale() const;

    std::function<void()> onPageLoaded;

    void paint (juce::Graphics&) override;
    bool pageAboutToLoad (const juce::String& newURL) override;
    void pageFinishedLoading (const juce::String& url) override;
    bool pageLoadHadNetworkError (const juce::String& errorInfo) override;

private:
    static Options createOptions (const juce::String& helloReply, KeyForwarder&, ControlSession&, const Handlers&, WebUi&);

    void timerCallback() override;

    ControlSession& session;
    double sentDisplayScale = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebUi)
};
