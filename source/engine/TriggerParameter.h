#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// An on/off parameter the host can automate that asks for an action each time it's switched on (an automation pulse,
// a controller button). Any thread may set it; the message thread takes the request.
class TriggerParameter final : public juce::AudioProcessorParameterWithID
{
public:
    TriggerParameter (const juce::String& id, const juce::String& name)
        : AudioProcessorParameterWithID ({ id, 1 }, name)
    {
    }

    float getValue() const override { return value.load (std::memory_order_relaxed); }

    void setValue (float newValue) override
    {
        if (newValue < 0.5f)
            value.store (0.0f, std::memory_order_relaxed);
        else if (value.exchange (1.0f, std::memory_order_relaxed) < 0.5f)
            requested.store (true, std::memory_order_release);
    }

    float getDefaultValue() const override { return 0.0f; }
    bool isDiscrete() const override { return true; }
    bool isBoolean() const override { return true; }
    int getNumSteps() const override { return 2; }
    juce::String getText (float normalisedValue, int) const override { return normalisedValue >= 0.5f ? "On" : "Off"; }
    float getValueForText (const juce::String& text) const override { return text.trim().equalsIgnoreCase ("on") || text.getFloatValue() >= 0.5f ? 1.0f : 0.0f; }

    bool takeRequest() noexcept { return requested.exchange (false, std::memory_order_acquire); }

private:
    std::atomic<float> value { 0.0f };
    std::atomic<bool> requested { false };
};
