#include "ControlSession.h"

namespace
{
    // Values from the page are untrusted: numbers only, finite, clamped to the normalised range
    std::optional<float> toNormalisedValue (const juce::var& value)
    {
        if (!value.isDouble() && !value.isInt() && !value.isInt64())
            return std::nullopt;

        const auto number = static_cast<double> (value);

        if (!std::isfinite (number))
            return std::nullopt;

        return static_cast<float> (juce::jlimit (0.0, 1.0, number));
    }

    juce::var describeValue (size_t handle, const juce::AudioProcessorParameter& source, float value)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("h", static_cast<int> (handle));
        entry->setProperty ("v", static_cast<double> (value));
        entry->setProperty ("t", source.getText (value, ControlSession::maxTextLength));
        return entry;
    }
}

ControlSession::ControlSession (std::vector<Parameter> parametersIn)
    : parameters (std::move (parametersIn)), lastSent (parameters.size()), inGesture (parameters.size())
{
}

ControlSession::~ControlSession()
{
    endAllGestures();
}

juce::var ControlSession::catalog()
{
    // A page that reloads mid-drag never sends the end of its gesture
    endAllGestures();
    started = true;

    juce::Array<juce::var> entries;

    for (size_t handle = 0; handle < parameters.size(); ++handle)
    {
        const auto& parameter = parameters[handle];
        lastSent[handle] = parameter.source->getValue();

        auto entry = describeValue (handle, *parameter.source, lastSent[handle]);
        auto* object = entry.getDynamicObject();
        object->setProperty ("slot", parameter.slot);
        object->setProperty ("plugin", parameter.plugin);
        object->setProperty ("name", parameter.name);
        object->setProperty ("host", parameter.hostName);
        object->setProperty ("d", static_cast<double> (parameter.source->getDefaultValue()));
        entries.add (entry);
    }

    return entries;
}

void ControlSession::applySet (const juce::var& batch)
{
    const auto* entries = batch.getArray();

    if (!started || entries == nullptr || entries->size() > maxBatchSize)
        return;

    for (const auto& entry : *entries)
    {
        const auto handle = findHandle (entry["h"]);
        const auto value = toNormalisedValue (entry["v"]);

        if (!handle.has_value() || !value.has_value())
            continue;

        auto& target = *parameters[*handle].writeTo;

        if (!juce::exactlyEqual (target.getValue(), *value))
            target.setValueNotifyingHost (*value);
    }
}

void ControlSession::applyGesture (const juce::var& gesture)
{
    const auto handle = findHandle (gesture["h"]);
    const auto& active = gesture["active"];

    if (!started || !handle.has_value() || !active.isBool())
        return;

    const auto begins = static_cast<bool> (active);

    if (inGesture[*handle] == begins)
        return;

    inGesture[*handle] = begins;
    auto& target = *parameters[*handle].writeTo;

    if (begins)
        target.beginChangeGesture();
    else
        target.endChangeGesture();
}

juce::var ControlSession::poll()
{
    if (!started)
        return {};

    juce::Array<juce::var> changes;

    for (size_t handle = 0; handle < parameters.size(); ++handle)
    {
        const auto& source = *parameters[handle].source;
        const auto value = source.getValue();

        if (juce::exactlyEqual (value, lastSent[handle]))
            continue;

        lastSent[handle] = value;
        changes.add (describeValue (handle, source, value));
    }

    if (changes.isEmpty())
        return {};

    return changes;
}

juce::AudioProcessorParameter* ControlSession::findWriteTarget (const juce::var& handle) const
{
    const auto index = findHandle (handle);
    return started && index.has_value() ? parameters[*index].writeTo : nullptr;
}

std::optional<size_t> ControlSession::findHandle (const juce::var& handle) const
{
    if (!handle.isInt() && !handle.isInt64())
        return std::nullopt;

    const auto index = static_cast<juce::int64> (handle);

    if (index < 0 || index >= static_cast<juce::int64> (parameters.size()))
        return std::nullopt;

    return static_cast<size_t> (index);
}

void ControlSession::endAllGestures()
{
    for (size_t handle = 0; handle < parameters.size(); ++handle)
    {
        if (inGesture[handle])
        {
            inGesture[handle] = false;
            parameters[handle].writeTo->endChangeGesture();
        }
    }
}
