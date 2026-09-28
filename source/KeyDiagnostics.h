#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_DEBUG

// Debug builds only, temporary (to diagnose key pass-through; delete once fixed). Shows how key and mouse-button
// messages travel on the host's UI thread and which window has focus. Records no key identities, only that a key
// went down or up; keeps its log in memory; stops itself after two minutes and when its editor closes.
class KeyDiagnostics final : private juce::Timer
{
public:
    explicit KeyDiagnostics (juce::Component& editor);
    ~KeyDiagnostics() override;

    void start();
    void stop();
    bool isRunning() const noexcept { return running; }

    // Our own side of the story, e.g. "forwarder posts key down"
    void note (const juce::String& event);
    void clear();

    juce::String getLog() const { return lines.joinIntoString ("\n"); }

    // Changes whenever the log does
    int getVersion() const noexcept { return version; }

    // Called by the message hook (a native MSG)
    void recordWindowMessage (const void* message);

    static constexpr int maxLines = 5000;
    static constexpr int runSeconds = 120;

private:
    void timerCallback() override { stop(); }
    void add (const juce::String& line);

    juce::Component& editor;
    juce::StringArray lines;
    double startTime = juce::Time::getMillisecondCounterHiRes();
    int version = 0;
    bool running = false;
    void* hook = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyDiagnostics)
};

#endif
