#include "DetachedGridPlaceholderCard.h"
#include "../JamWideLookAndFeel.h"

namespace jamwide {

DetachedGridPlaceholderCard::DetachedGridPlaceholderCard()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setInterceptsMouseClicks(true, false);
}

void DetachedGridPlaceholderCard::setLabel(const juce::String& s)
{
    label_ = s;
    repaint();
}

void DetachedGridPlaceholderCard::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Slightly larger card than the per-peer variant since it fills the
    // whole band area; use the same VB-style palette.
    g.setColour(juce::Colour(JamWideLookAndFeel::kBgElevated));
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawFittedText(label_,
                     bounds.toNearestInt().reduced(16),
                     juce::Justification::centred,
                     2,
                     0.85f);
}

void DetachedGridPlaceholderCard::mouseDown(const juce::MouseEvent&)
{
    if (onBringBack) onBringBack();
}

} // namespace jamwide
