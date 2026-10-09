#include "HostControls.h"

// One control in the pool. The host may set it from any thread, including the audio thread.
class HostControl final : public juce::AudioProcessorParameterWithID
{
public:
    explicit HostControl (int number)
        : AudioProcessorParameterWithID ({ "param" + juce::String (number), 1 }, "Param " + juce::String (number)),
          currentName (name)
    {
    }

    juce::String getName (int maximumStringLength) const override
    {
        const juce::ScopedLock lock (nameLock);
        return currentName.substring (0, maximumStringLength);
    }

    // True if it changed
    bool rename (const juce::String& newName)
    {
        const juce::ScopedLock lock (nameLock);
        return std::exchange (currentName, newName) != newName;
    }

    void bind (juce::AudioProcessorParameter& newTarget)
    {
        value = newTarget.getValue();
        target = &newTarget;
    }

    float getValue() const override { return value; }

    void setValue (float newValue) override
    {
        // Target first: a report running meanwhile on the message thread then sees the new value, not the old one
        if (auto* t = target.load(); t != nullptr && !juce::exactlyEqual (t->getValue(), newValue))
            t->setValue (newValue);

        value = newValue;
    }

    float getDefaultValue() const override
    {
        auto* t = target.load();
        return t != nullptr ? t->getDefaultValue() : 0.0f;
    }

    // Hosted plugins' text lookups wait for the message thread, so they're only asked from there
    juce::String getText (float normalisedValue, int maximumLength) const override
    {
        if (auto* t = target.load(); t != nullptr && juce::MessageManager::existsAndIsCurrentThread())
            return t->getText (normalisedValue, maximumLength);

        return AudioProcessorParameterWithID::getText (normalisedValue, maximumLength);
    }

    float getValueForText (const juce::String& text) const override
    {
        if (auto* t = target.load(); t != nullptr && juce::MessageManager::existsAndIsCurrentThread())
            return t->getValueForText (text);

        return juce::jlimit (0.0f, 1.0f, text.getFloatValue());
    }

    void reportTargetChange()
    {
        if (auto* t = target.load())
            if (const auto current = t->getValue(); !juce::exactlyEqual (current, value.load()))
                setValueNotifyingHost (current);
    }

    void syncWithoutNotifying()
    {
        if (auto* t = target.load())
            value = t->getValue();
    }

private:
    std::atomic<juce::AudioProcessorParameter*> target { nullptr };
    std::atomic<float> value { 0.0f };
    juce::CriticalSection nameLock;
    juce::String currentName;
};

HostControls::HostControls (juce::AudioProcessor& processorIn)
    : processor (processorIn)
{
    for (int number = 1; number <= count; ++number)
    {
        auto control = std::make_unique<HostControl> (number);
        controls.push_back (control.get());
        processor.addParameter (control.release());
    }

    startTimerHz (syncRateHz);
}

void HostControls::bind (int index, juce::AudioProcessorParameter& target)
{
    controls[static_cast<size_t> (index)]->bind (target);
}

juce::AudioProcessorParameter& HostControls::operator[] (int index) const
{
    return *controls[static_cast<size_t> (index)];
}

void HostControls::setName (int index, const juce::String& newName)
{
    // Plugin-supplied parts are untrusted: no control characters, and a length every format can hold
    juce::String clean;

    for (const auto character : newName)
        if (character >= 0x20 && character != 0x7f)
            clean += character;

    if (controls[static_cast<size_t> (index)]->rename (clean.trim().substring (0, maxNameLength)))
        processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withParameterInfoChanged (true));
}

void HostControls::reportTargetChanges()
{
    for (auto* control : controls)
        control->reportTargetChange();
}

void HostControls::syncWithoutNotifying()
{
    for (auto* control : controls)
        control->syncWithoutNotifying();
}
