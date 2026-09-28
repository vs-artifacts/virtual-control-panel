#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class HostControl;

// Our plugin's parameters as the host sees them: a fixed pool of generic controls, each bound to at most one
// hosted parameter. Hosts can't cope with parameters that come and go, so exposing a parameter binds it to a
// free control instead. The pool may grow in later versions but must never shrink: projects' automation refers to these IDs.
class HostControls final : private juce::Timer
{
public:
    static constexpr int count = 256;
    static constexpr int syncRateHz = 30;

    // Adds the pool to the processor, which owns the parameters. Call from the processor's constructor.
    explicit HostControls (juce::AudioProcessor&);

    // Message thread only, before audio starts: rebinding while running needs a realtime-safe handover
    void bind (int index, juce::AudioProcessorParameter& target);

    juce::AudioProcessorParameter& operator[] (int index) const;

    // Tells the host about bound parameters changed from elsewhere, e.g. the hosted plugin's own window. Message thread only.
    void reportTargetChanges();

    // Takes the bound parameters' values without telling the host, e.g. after restoring saved state
    void syncWithoutNotifying();

private:
    void timerCallback() override { reportTargetChanges(); }

    std::vector<HostControl*> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostControls)
};
