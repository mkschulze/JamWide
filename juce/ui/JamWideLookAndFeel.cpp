#include "JamWideLookAndFeel.h"

JamWideLookAndFeel::JamWideLookAndFeel()
{
    // Window / top-level
    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(kBgPrimary));

    // TextEditor
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(kSurfaceInput));
    setColour(juce::TextEditor::textColourId, juce::Colour(kTextPrimary));
    setColour(juce::TextEditor::outlineColourId, juce::Colour(kBorderSubtle));
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kBorderFocus));

    // TextButton
    setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceStrip));
    setColour(juce::TextButton::textColourOffId, juce::Colour(kTextPrimary));
    setColour(juce::TextButton::textColourOnId, juce::Colour(kAccentConnect));

    // ComboBox
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(kSurfaceInput));
    setColour(juce::ComboBox::textColourId, juce::Colour(kTextPrimary));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(kBorderSubtle));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(kTextSecondary));

    // PopupMenu
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(kSurfaceOverlay));
    setColour(juce::PopupMenu::textColourId, juce::Colour(kTextPrimary));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(kSurfaceStrip));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(kTextHeading));

    // Label
    setColour(juce::Label::textColourId, juce::Colour(kTextPrimary));

    // ScrollBar
    setColour(juce::ScrollBar::thumbColourId, juce::Colour(kBorderSubtle));
    setColour(juce::ScrollBar::trackColourId, juce::Colour(kVuBackground));

    // ListBox
    setColour(juce::ListBox::backgroundColourId, juce::Colour(kSurfaceOverlay));
    setColour(juce::ListBox::textColourId, juce::Colour(kTextPrimary));
}

//==============================================================================
void JamWideLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    constexpr float cornerRadius = 4.0f;

    auto baseColour = backgroundColour;
    if (shouldDrawButtonAsDown)
        baseColour = baseColour.darker(0.15f);
    else if (shouldDrawButtonAsHighlighted)
        baseColour = baseColour.brighter(0.08f);

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, cornerRadius);

    // Border
    if (shouldDrawButtonAsHighlighted || button.hasKeyboardFocus(true))
    {
        g.setColour(juce::Colour(kBorderFocus));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }
    else
    {
        g.setColour(juce::Colour(kBorderSubtle));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }
}

//==============================================================================
void JamWideLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height,
                                       bool isButtonDown, int /*buttonX*/, int /*buttonY*/,
                                       int /*buttonW*/, int /*buttonH*/, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                          static_cast<float>(width),
                                          static_cast<float>(height));
    constexpr float cornerRadius = 4.0f;

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, cornerRadius);

    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);

    // Down-arrow
    auto arrowArea = bounds.removeFromRight(static_cast<float>(height)).reduced(8.0f);
    juce::Path arrow;
    arrow.addTriangle(arrowArea.getX(), arrowArea.getCentreY() - 3.0f,
                      arrowArea.getRight(), arrowArea.getCentreY() - 3.0f,
                      arrowArea.getCentreX(), arrowArea.getCentreY() + 3.0f);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId)
                    .withAlpha(isButtonDown ? 1.0f : 0.7f));
    g.fillPath(arrow);
}

//==============================================================================
void JamWideLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height,
                                                juce::TextEditor& editor)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                          static_cast<float>(width),
                                          static_cast<float>(height));
    constexpr float cornerRadius = 4.0f;

    if (editor.hasKeyboardFocus(true))
        g.setColour(juce::Colour(kBorderFocus));
    else
        g.setColour(juce::Colour(kBorderSubtle));

    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
}

//==============================================================================
void JamWideLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& /*scrollbar*/,
                                        int x, int y, int width, int height,
                                        bool isScrollbarVertical, int thumbStartPosition,
                                        int thumbSize, bool isMouseOver, bool /*isMouseDown*/)
{
    auto trackBounds = juce::Rectangle<int>(x, y, width, height).toFloat();

    // Track
    g.setColour(juce::Colour(kVuBackground));
    g.fillRoundedRectangle(trackBounds, 3.0f);

    // Thumb
    juce::Rectangle<float> thumbBounds;
    if (isScrollbarVertical)
        thumbBounds = { trackBounds.getX(), static_cast<float>(thumbStartPosition),
                        trackBounds.getWidth(), static_cast<float>(thumbSize) };
    else
        thumbBounds = { static_cast<float>(thumbStartPosition), trackBounds.getY(),
                        static_cast<float>(thumbSize), trackBounds.getHeight() };

    auto thumbColour = juce::Colour(kBorderSubtle);
    if (isMouseOver)
        thumbColour = thumbColour.brighter(0.15f);

    g.setColour(thumbColour);
    g.fillRoundedRectangle(thumbBounds.reduced(1.0f), 3.0f);
}

//==============================================================================
void JamWideLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.setColour(label.findColour(juce::Label::textColourId));

    auto textArea = label.getBorderSize().subtractedFrom(label.getLocalBounds());
    g.setFont(label.getFont());
    g.drawFittedText(label.getText(), textArea, label.getJustificationType(),
                     juce::jmax(1, static_cast<int>(static_cast<float>(textArea.getHeight())
                                                     / label.getFont().getHeight())),
                     label.getMinimumHorizontalScale());
}
