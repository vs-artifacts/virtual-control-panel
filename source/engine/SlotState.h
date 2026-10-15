#pragma once

#include <juce_core/juce_core.h>

// What a slot (a generator, an FX slot or a bay) plays; the same meaning everywhere, the mechanics differ. Uppercase in
// the UI: ON, MUTE, BYPASS, OFF.
enum class SlotState {
    on, // its plugin's sound
    mute, // the dry sound (a generator: silence) while the plugin keeps running
    bypass, // the dry sound at the plugin's latency; the plugin isn't run, and is reset
    off, // out of the processing graph: its latency no longer counts
};

inline const char* toName (SlotState state) noexcept
{
    switch (state)
    {
        case SlotState::mute:
            return "mute";
        case SlotState::bypass:
            return "bypass";
        case SlotState::off:
            return "off";
        case SlotState::on:
            break;
    }

    return "on";
}

inline std::optional<SlotState> slotStateFromName (const juce::String& name)
{
    for (const auto state : { SlotState::on, SlotState::mute, SlotState::bypass, SlotState::off })
        if (name == toName (state))
            return state;

    return std::nullopt;
}
