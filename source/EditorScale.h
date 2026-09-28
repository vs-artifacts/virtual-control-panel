#pragma once

#include <juce_core/juce_core.h>

// The editor's size relative to its design size; the whole UI scales with it rather than rearranging
namespace EditorScale
{
    constexpr int designWidth = 560;
    constexpr int designHeight = 600;
    constexpr double minimum = 0.5;
    constexpr double maximum = 5.0;

    enum class Resizing {
        smooth,
        sharp, // the scale on screen (physical pixels per design pixel) stays a whole number, so 1-pixel lines stay crisp
    };

    inline double constrain (double scale, Resizing resizing, double physicalPixelsPerPixel)
    {
        if (!std::isfinite (scale))
            return 1.0;

        if (resizing == Resizing::smooth || !(physicalPixelsPerPixel > 0.0))
            return juce::jlimit (minimum, maximum, scale);

        const auto smallest = std::ceil (minimum * physicalPixelsPerPixel - 1.0e-9);
        const auto largest = std::floor (maximum * physicalPixelsPerPixel + 1.0e-9);
        const auto steps = juce::jlimit (std::max (1.0, smallest), std::max (1.0, largest), std::round (scale * physicalPixelsPerPixel));
        return steps / physicalPixelsPerPixel;
    }
}
