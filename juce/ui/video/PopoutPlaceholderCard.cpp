#include "PopoutPlaceholderCard.h"
#include "../JamWideLookAndFeel.h"

namespace jamwide {

PopoutPlaceholderCard::PopoutPlaceholderCard()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setInterceptsMouseClicks(true, false);
}

void PopoutPlaceholderCard::setLabel(const juce::String& s)
{
    label_ = s;
    repaint();
}

void PopoutPlaceholderCard::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Dark VB-style backdrop matching surrounding chrome.
    g.setColour(juce::Colour(JamWideLookAndFeel::kBgElevated));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Subtle border for legibility.
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Soft-white centred label. Sized to fit the card (drawFittedText so
    // small tiles don't truncate to "...").
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawFittedText(label_,
                     bounds.toNearestInt().reduced(8),
                     juce::Justification::centred,
                     2,
                     0.85f);
}

void PopoutPlaceholderCard::mouseDown(const juce::MouseEvent&)
{
    // codex H3 — placeholder click is the EXCLUSIVE destroy path for a
    // popped-out peer. The editor's lambda calls bringBackRemotePopout
    // which destroys the popout window AND clears poppedOutPeers_ on
    // BOTH bands (M7 dual-band sync).
    if (onBringBack) onBringBack();
}

} // namespace jamwide
