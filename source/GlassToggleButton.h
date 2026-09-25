#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Toggle button with a frosted-glass look: blurs whatever its parent paints behind it (like CSS backdrop-filter)
class GlassToggleButton : public juce::Button
{
public:
    explicit GlassToggleButton (const juce::String& buttonText);

    void setBlurRadius (float newRadius);

    // The blurred backdrop is cached; call this when the parent's background changes
    void invalidateBackdrop();

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void moved() override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    void captureBackdrop (float scale);

    float blurRadius = 14.0f;
    float cornerRadius = 14.0f;

    juce::Image backdrop;
    juce::Rectangle<int> backdropArea;
    float backdropScale = 0.0f;
    bool isCapturingBackdrop = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlassToggleButton)
};
