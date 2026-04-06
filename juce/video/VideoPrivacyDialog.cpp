#include "VideoPrivacyDialog.h"
#include "ui/JamWideLookAndFeel.h"

VideoPrivacyDialog::VideoPrivacyDialog()
{
    // Title: "Video -- Privacy Notice" (em dash per UI-SPEC copywriting)
    titleLabel_.setText("Video \xe2\x80\x94 Privacy Notice", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
    titleLabel_.setColour(juce::Label::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextHeading));
    addAndMakeVisible(titleLabel_);

    // Body text: multi-line, read-only
    bodyTextEditor_.setMultiLine(true, true);
    bodyTextEditor_.setReadOnly(true);
    bodyTextEditor_.setFont(juce::FontOptions(13.0f));
    bodyTextEditor_.setColour(juce::TextEditor::backgroundColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    bodyTextEditor_.setColour(juce::TextEditor::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    bodyTextEditor_.setColour(juce::TextEditor::outlineColourId,
        juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    addAndMakeVisible(bodyTextEditor_);

    // Accept button: "I Understand -- Open Video"
    acceptButton_.setButtonText("I Understand \xe2\x80\x94 Open Video");
    acceptButton_.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect).withAlpha(0.2f));
    acceptButton_.setColour(juce::TextButton::textColourOnId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect));
    acceptButton_.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect));
    acceptButton_.onClick = [this]()
    {
        if (onResponse)
            onResponse(true);
        dismiss();
    };
    addAndMakeVisible(acceptButton_);

    // Decline button: "Skip Video"
    declineButton_.setButtonText("Skip Video");
    declineButton_.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kAccentDestructive).withAlpha(0.2f));
    declineButton_.setColour(juce::TextButton::textColourOnId,
        juce::Colour(JamWideLookAndFeel::kAccentDestructive));
    declineButton_.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kAccentDestructive));
    declineButton_.onClick = [this]()
    {
        if (onResponse)
            onResponse(false);
        dismiss();
    };
    addAndMakeVisible(declineButton_);

    setVisible(false);
    setInterceptsMouseClicks(false, false);
}

void VideoPrivacyDialog::show(bool showBrowserWarning)
{
    showBrowserWarning_ = showBrowserWarning;

    // Build body text
    juce::String bodyText;

    // Section 1: IP disclosure (always shown, per UI-SPEC copywriting contract)
    bodyText += "IP Address Disclosure\n\n";
    bodyText += "Video uses peer-to-peer WebRTC via VDO.Ninja. Your IP address "
                "will be visible to other participants in the session. This is "
                "inherent to peer-to-peer video and cannot be avoided.\n";

    // Section 2: Browser warning (shown only when non-Chromium detected, per D-07)
    // NOTE: This is ADVISORY only. It does NOT prevent the user from proceeding.
    // Both Accept and Skip buttons work regardless. Addresses review concern #5:
    // browser detection brittleness is mitigated by never blocking.
    if (showBrowserWarning)
    {
        bodyText += "\n\nBrowser Compatibility\n\n";
        bodyText += "Your default browser may not fully support the video features. "
                    "For the best experience, use a Chromium-based browser such as "
                    "Chrome, Edge, or Brave.\n";
    }

    bodyTextEditor_.setText(bodyText, false);

    showing_ = true;
    setVisible(true);
    setInterceptsMouseClicks(true, true);
    toFront(true);

    // Per UI-SPEC accessibility: accept button receives initial focus
    acceptButton_.grabKeyboardFocus();
}

void VideoPrivacyDialog::dismiss()
{
    showing_ = false;
    setVisible(false);
    setInterceptsMouseClicks(false, false);
}

bool VideoPrivacyDialog::isShowing() const
{
    return showing_;
}

void VideoPrivacyDialog::paint(juce::Graphics& g)
{
    // Scrim: full-screen overlay behind modal (per UI-SPEC: kSurfaceScrim = 70% black)
    // No mouseDown override -- scrim intercepts clicks but does nothing,
    // forcing user to use buttons (per D-05: must click Accept or Skip).
    g.fillAll(juce::Colour(JamWideLookAndFeel::kSurfaceScrim));

    // Dialog body: centered
    auto dialogBounds = getLocalBounds().withSizeKeepingCentre(kDialogWidth, kDialogHeight);

    // Background with 8px corner radius (per UI-SPEC)
    g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceOverlay));
    g.fillRoundedRectangle(dialogBounds.toFloat(), 8.0f);

    // Border: kBorderSubtle 1px
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.drawRoundedRectangle(dialogBounds.toFloat(), 8.0f, 1.0f);
}

void VideoPrivacyDialog::resized()
{
    auto dialogBounds = getLocalBounds().withSizeKeepingCentre(kDialogWidth, kDialogHeight);
    auto area = dialogBounds.reduced(16);

    // Title at top: 28px height
    titleLabel_.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);  // gap

    // Button row at bottom: 36px height
    auto buttonRow = area.removeFromBottom(36);
    area.removeFromBottom(8);  // gap above buttons

    // Center buttons: decline (90px) + 16px gap + accept (200px)
    int declineWidth = 90;
    int acceptWidth = 200;
    int gap = 16;
    int totalButtonWidth = declineWidth + gap + acceptWidth;
    int startX = buttonRow.getX() + (buttonRow.getWidth() - totalButtonWidth) / 2;

    declineButton_.setBounds(startX, buttonRow.getY(), declineWidth, 36);
    acceptButton_.setBounds(startX + declineWidth + gap, buttonRow.getY(), acceptWidth, 36);

    // Body text fills remaining middle area
    bodyTextEditor_.setBounds(area);
}

bool VideoPrivacyDialog::keyPressed(const juce::KeyPress& key)
{
    // Per UI-SPEC accessibility: Escape key triggers "Skip Video" (dismiss without action)
    if (key == juce::KeyPress::escapeKey)
    {
        if (onResponse)
            onResponse(false);
        dismiss();
        return true;
    }
    return false;
}
