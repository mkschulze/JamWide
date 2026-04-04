#include "LicenseDialog.h"
#include "JamWideLookAndFeel.h"

LicenseDialog::LicenseDialog()
{
    titleLabel.setText("Server License Agreement", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
    titleLabel.setColour(juce::Label::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextHeading));
    addAndMakeVisible(titleLabel);

    licenseTextEditor.setMultiLine(true, true);
    licenseTextEditor.setReadOnly(true);
    licenseTextEditor.setFont(juce::FontOptions(13.0f));
    licenseTextEditor.setColour(juce::TextEditor::backgroundColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    licenseTextEditor.setColour(juce::TextEditor::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    licenseTextEditor.setColour(juce::TextEditor::outlineColourId,
        juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    addAndMakeVisible(licenseTextEditor);

    acceptButton.setButtonText("Accept");
    acceptButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect).withAlpha(0.2f));
    acceptButton.setColour(juce::TextButton::textColourOnId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect));
    acceptButton.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect));
    acceptButton.onClick = [this]()
    {
        if (onResponse)
            onResponse(true);
        dismiss();
    };
    addAndMakeVisible(acceptButton);

    declineButton.setButtonText("Decline");
    declineButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kAccentDestructive).withAlpha(0.2f));
    declineButton.setColour(juce::TextButton::textColourOnId,
        juce::Colour(JamWideLookAndFeel::kAccentDestructive));
    declineButton.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kAccentDestructive));
    declineButton.onClick = [this]()
    {
        if (onResponse)
            onResponse(false);
        dismiss();
    };
    addAndMakeVisible(declineButton);

    setVisible(false);
    setInterceptsMouseClicks(false, false);
}

void LicenseDialog::show(const juce::String& licenseText)
{
    showing = true;
    licenseTextEditor.setText(licenseText, false);
    setVisible(true);
    setInterceptsMouseClicks(true, true);
    toFront(true);
}

void LicenseDialog::dismiss()
{
    showing = false;
    setVisible(false);
    setInterceptsMouseClicks(false, false);
}

bool LicenseDialog::isShowing() const
{
    return showing;
}

void LicenseDialog::paint(juce::Graphics& g)
{
    // REVIEW FIX: The scrim intercepts all mouse clicks but there is NO mouseDown handler
    // that calls dismiss(). The only way to dismiss is Accept or Decline buttons.
    // This prevents accidental outside-click dismissal while run thread is blocked.
    g.fillAll(juce::Colour(JamWideLookAndFeel::kSurfaceScrim));

    // Dialog body: centered
    auto dialogBounds = getLocalBounds().withSizeKeepingCentre(kDialogWidth, kDialogHeight);

    // Background
    g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceOverlay));
    g.fillRoundedRectangle(dialogBounds.toFloat(), 8.0f);

    // Border
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.drawRoundedRectangle(dialogBounds.toFloat(), 8.0f, 1.0f);
}

void LicenseDialog::resized()
{
    auto dialogBounds = getLocalBounds().withSizeKeepingCentre(kDialogWidth, kDialogHeight);
    auto area = dialogBounds.reduced(16);

    // Title at top
    titleLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);

    // Button row at bottom
    auto buttonRow = area.removeFromBottom(36);
    area.removeFromBottom(8);

    auto buttonWidth = 120;
    auto gap = 16;
    auto totalButtonWidth = buttonWidth * 2 + gap;
    auto startX = buttonRow.getX() + (buttonRow.getWidth() - totalButtonWidth) / 2;

    declineButton.setBounds(startX, buttonRow.getY(), buttonWidth, 36);
    acceptButton.setBounds(startX + buttonWidth + gap, buttonRow.getY(), buttonWidth, 36);

    // License text fills middle
    licenseTextEditor.setBounds(area);
}
