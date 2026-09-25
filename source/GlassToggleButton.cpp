#include "GlassToggleButton.h"

GlassToggleButton::GlassToggleButton (const juce::String& buttonText)
    : juce::Button (buttonText)
{
    setClickingTogglesState (true);
}

void GlassToggleButton::setBlurRadius (float newRadius)
{
    blurRadius = newRadius;
    invalidateBackdrop();
}

void GlassToggleButton::invalidateBackdrop()
{
    backdrop = {};
    repaint();
}

void GlassToggleButton::moved()
{
    invalidateBackdrop();
}

void GlassToggleButton::resized()
{
    invalidateBackdrop();
}

void GlassToggleButton::parentHierarchyChanged()
{
    juce::Button::parentHierarchyChanged();
    invalidateBackdrop();
}

void GlassToggleButton::captureBackdrop (float scale)
{
    backdrop = {};

    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    // Grab extra pixels around the button so the blur has real content to pull in at the edges
    const auto padding = (int) std::ceil (blurRadius);
    backdropArea = getBoundsInParent().expanded (padding);
    backdropScale = scale;

    // The snapshot repaints the parent and its children (including us), so skip ourselves while capturing
    const juce::ScopedValueSetter<bool> capturing (isCapturingBackdrop, true);
    backdrop = parent->createComponentSnapshot (backdropArea, false, scale);

    if (auto pixelData = backdrop.getPixelData())
        pixelData->applyGaussianBlurEffect (blurRadius * scale);
}

void GlassToggleButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    if (isCapturingBackdrop)
        return;

    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (!backdrop.isValid() || !juce::approximatelyEqual (scale, backdropScale))
        captureBackdrop (scale);

    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    juce::Path shape;
    shape.addRoundedRectangle (bounds, cornerRadius);

    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);

        if (backdrop.isValid())
            g.drawImage (backdrop, (backdropArea - getPosition()).toFloat());

        auto tint = getToggleState() ? juce::Colour (0xff9fc3ff).withAlpha (0.28f) : juce::Colours::white.withAlpha (0.10f);

        if (shouldDrawButtonAsDown)
            tint = tint.withMultipliedAlpha (1.6f);
        else if (shouldDrawButtonAsHighlighted)
            tint = tint.withMultipliedAlpha (1.3f);

        g.setColour (tint);
        g.fillPath (shape);

        // Soft sheen towards the top edge
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), bounds.getTopLeft(), juce::Colours::white.withAlpha (0.0f), { bounds.getX(), bounds.getCentreY() }, false));
        g.fillPath (shape);
    }

    g.setColour (juce::Colours::white.withAlpha (getToggleState() ? 0.55f : 0.30f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    g.setColour (juce::Colours::white.withAlpha (isEnabled() ? 0.95f : 0.5f));
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (getButtonText() + (getToggleState() ? ": On" : ": Off"), getLocalBounds(), juce::Justification::centred, false);
}
