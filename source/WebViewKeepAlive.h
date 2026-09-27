#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Keeps one hidden, empty web view alive so the shared WebView2 browser process doesn't shut down
// between editor opens; restarting it while the old one exits can take seconds.
// One per process, shared through juce::SharedResourcePointer. Message thread only.
class WebViewKeepAlive
{
public:
    WebViewKeepAlive();
    ~WebViewKeepAlive();

    void start();
    void stop();
    bool isRunning() const noexcept { return window != nullptr; }

private:
    std::unique_ptr<juce::Component> window;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebViewKeepAlive)
};
