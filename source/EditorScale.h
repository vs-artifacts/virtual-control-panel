#pragma once

#include <juce_core/juce_core.h>

// The editor's size relative to its design size; the whole UI scales with it rather than rearranging
namespace EditorScale
{
    constexpr int designWidth = 1120;
    constexpr int designHeight = 600;
    constexpr double minimum = 0.5;
    constexpr double maximum = 5.0;

    // Ctrl+drag steps, as a share of the design size
    constexpr double step = 0.25;

    // How the page follows the editor's size (the page does it; C++ only stores the choice)
    enum class Rendering {
        smooth, // laid out once and scaled like a picture: everything keeps its proportions, edges soften
        sharp, // laid out again at every size: crisp, but elements can shift by a pixel against each other
    };

    // Values in the settings file and in messages from the page
    inline juce::String toSettingsValue (Rendering rendering) { return rendering == Rendering::sharp ? "sharp" : "smooth"; }

    inline std::optional<Rendering> renderingFromSettingsValue (const juce::String& value)
    {
        if (value == "smooth")
            return Rendering::smooth;

        if (value == "sharp")
            return Rendering::sharp;

        return std::nullopt;
    }

    inline double constrain (double scale)
    {
        return std::isfinite (scale) ? juce::jlimit (minimum, maximum, scale) : 1.0;
    }

    // The nearest step, or the virtual step (the size when Ctrl went down) if that's nearer
    inline double snap (double scale, double virtualStep)
    {
        const auto nearestStep = juce::jlimit (minimum, maximum, std::round (scale / step) * step);
        return std::abs (scale - virtualStep) < std::abs (scale - nearestStep) ? virtualStep : nearestStep;
    }
}
