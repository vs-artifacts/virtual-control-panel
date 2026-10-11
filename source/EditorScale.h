#pragma once

#include <juce_core/juce_core.h>

// The editor's size: a scale relative to the design size (the whole UI scales with it), times the page's layout area as a
// share of the design size on each axis (Shift+drag sets those apart; the page shows more or less at the same scale)
namespace EditorScale
{
    // Portrait: FL's window adds 10 px of border, making it 800 × 1,280
    constexpr int designWidth = 790;
    constexpr int designHeight = 1270;

    // Diagnostics adds this much width (design pixels) to the right of the layout area
    constexpr int diagnosticsWidth = 400;
    constexpr double minimum = 0.5;
    constexpr double maximum = 5.0;
    constexpr double minimumLayout = 0.5;
    constexpr double maximumLayout = 4.0;

    // Ctrl+drag steps, as a share of the design size (the scale, or each axis of the layout)
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

    inline double constrainLayout (double share)
    {
        return std::isfinite (share) ? juce::jlimit (minimumLayout, maximumLayout, share) : 1.0;
    }

    // The nearest step, or the virtual step (the size when Ctrl went down) if that's nearer
    inline double snap (double scale, double virtualStep, double low = minimum, double high = maximum)
    {
        const auto nearestStep = juce::jlimit (low, high, std::round (scale / step) * step);
        return std::abs (scale - virtualStep) < std::abs (scale - nearestStep) ? virtualStep : nearestStep;
    }
}
