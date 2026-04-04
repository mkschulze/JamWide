#include "ConnectionBar.h"
#include "JamWideJuceProcessor.h"
#include "ui/JamWideLookAndFeel.h"
#include "core/njclient.h"
#include "threading/ui_command.h"
#include "build_number.h"

// MAKE_NJ_FOURCC is private to njclient.cpp -- define locally (per Plan 03-01 decision)
#ifndef MAKE_NJ_FOURCC
#define MAKE_NJ_FOURCC(a, b, c, d) \
    (static_cast<unsigned int>(a) | (static_cast<unsigned int>(b) << 8) | \
     (static_cast<unsigned int>(c) << 16) | (static_cast<unsigned int>(d) << 24))
#endif

ConnectionBar::ConnectionBar(JamWideJuceProcessor& processor)
    : processorRef(processor)
{
    // Server field
    serverField.setText(processorRef.lastServerAddress);
    serverField.setTextToShowWhenEmpty("server:port", juce::Colour(JamWideLookAndFeel::kTextSecondary));
    serverField.setMultiLine(false);
    serverField.setFont(juce::FontOptions(13.0f));
    serverField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    serverField.setColour(juce::TextEditor::textColourId, juce::Colour(JamWideLookAndFeel::kTextPrimary));
    serverField.setColour(juce::TextEditor::outlineColourId, juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    addAndMakeVisible(serverField);

    // Username field
    usernameField.setText(processorRef.lastUsername);
    usernameField.setTextToShowWhenEmpty("username", juce::Colour(JamWideLookAndFeel::kTextSecondary));
    usernameField.setMultiLine(false);
    usernameField.setFont(juce::FontOptions(13.0f));
    usernameField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    usernameField.setColour(juce::TextEditor::textColourId, juce::Colour(JamWideLookAndFeel::kTextPrimary));
    usernameField.setColour(juce::TextEditor::outlineColourId, juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    addAndMakeVisible(usernameField);

    // Password toggle button
    passwordToggle.setButtonText("PW");
    passwordToggle.setDescription("Toggle password visibility");
    passwordToggle.setColour(juce::TextButton::buttonColourId, juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    passwordToggle.setColour(juce::TextButton::textColourOffId, juce::Colour(JamWideLookAndFeel::kTextSecondary));
    passwordToggle.onClick = [this]() {
        passwordVisible = !passwordVisible;
        passwordField.setVisible(passwordVisible);
        resized();
    };
    addAndMakeVisible(passwordToggle);

    // Password field (hidden initially)
    passwordField.setPasswordCharacter('*');
    passwordField.setTextToShowWhenEmpty("password", juce::Colour(JamWideLookAndFeel::kTextSecondary));
    passwordField.setMultiLine(false);
    passwordField.setFont(juce::FontOptions(13.0f));
    passwordField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    passwordField.setColour(juce::TextEditor::textColourId, juce::Colour(JamWideLookAndFeel::kTextPrimary));
    passwordField.setColour(juce::TextEditor::outlineColourId, juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    passwordField.setVisible(false);
    addAndMakeVisible(passwordField);

    // Connect button
    connectButton.setButtonText("Connect");
    connectButton.setColour(juce::TextButton::buttonColourId, juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    connectButton.setColour(juce::TextButton::textColourOffId, juce::Colour(JamWideLookAndFeel::kAccentConnect));
    connectButton.onClick = [this]() { handleConnectClick(); };
    addAndMakeVisible(connectButton);

    // Browse button
    browseButton.setButtonText("Browse");
    browseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    browseButton.setColour(juce::TextButton::textColourOffId, juce::Colour(JamWideLookAndFeel::kTextSecondary));
    browseButton.onClick = [this]() { if (onBrowseClicked) onBrowseClicked(); };
    addAndMakeVisible(browseButton);

    // Status label
    statusLabel.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    statusLabel.setText("Disconnected", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kTextSecondary));
    addAndMakeVisible(statusLabel);

    // BPM/BPI label
    bpmBpiLabel.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    bpmBpiLabel.setText("", juce::dontSendNotification);
    bpmBpiLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kTextPrimary));
    addAndMakeVisible(bpmBpiLabel);

    // User count label
    userCountLabel.setFont(juce::FontOptions(11.0f));
    userCountLabel.setText("", juce::dontSendNotification);
    userCountLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kTextSecondary));
    addAndMakeVisible(userCountLabel);

    // Codec selector -- FLAC default per CODEC-05
    codecSelector.addItem("FLAC", 1);
    codecSelector.addItem("Vorbis", 2);
    codecSelector.setSelectedId(1);  // FLAC default per CODEC-05
    codecSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    codecSelector.setColour(juce::ComboBox::textColourId, juce::Colour(JamWideLookAndFeel::kTextPrimary));
    codecSelector.setColour(juce::ComboBox::outlineColourId, juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    codecSelector.onChange = [this]() { handleCodecChange(); };
    addAndMakeVisible(codecSelector);
}

void ConnectionBar::resized()
{
    auto area = getLocalBounds().reduced(8, 0);
    const int h = 28;
    const int y = (getHeight() - h) / 2;
    const int gap = 6;

    // Left section: input fields and buttons
    int x = area.getX();

    serverField.setBounds(x, y, 180, h);
    x += 180 + gap;

    usernameField.setBounds(x, y, 120, h);
    x += 120 + gap;

    passwordToggle.setBounds(x, y, 32, h);
    x += 32 + gap;

    if (passwordVisible)
    {
        passwordField.setBounds(x, y, 120, h);
        x += 120 + gap;
    }

    connectButton.setBounds(x, y, 90, h);
    x += 90 + gap;

    browseButton.setBounds(x, y, 70, h);
    x += 70 + 16;  // 16px gap before status section

    // Status section
    // Reserve 14px for the status dot painted in paint()
    statusLabel.setBounds(x + 14, y, 100, h);
    x += 14 + 100 + gap;

    // Right-aligned section
    int rightX = area.getRight();
    codecSelector.setBounds(rightX - 80, y, 80, h);
    rightX -= 80 + gap;

    userCountLabel.setBounds(rightX - 70, y, 70, h);
    rightX -= 70 + gap;

    // BPM/BPI gets remaining space
    int bpmWidth = rightX - x;
    if (bpmWidth > 0)
        bpmBpiLabel.setBounds(x, y, bpmWidth, h);
}

void ConnectionBar::paint(juce::Graphics& g)
{
    // Background
    g.setColour(juce::Colour(JamWideLookAndFeel::kBgElevated));
    g.fillRect(getLocalBounds());

    // 1px bottom border
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));

    // Status dot (8px circle) left of statusLabel
    auto statusBounds = statusLabel.getBounds();
    float dotX = static_cast<float>(statusBounds.getX() - 12);
    float dotY = static_cast<float>(statusBounds.getCentreY()) - 4.0f;

    juce::Colour dotColour;
    switch (currentStatus)
    {
        case NJClient::NJC_STATUS_OK:
            dotColour = juce::Colour(JamWideLookAndFeel::kAccentConnect);
            break;
        case NJClient::NJC_STATUS_PRECONNECT:
            dotColour = juce::Colour(JamWideLookAndFeel::kAccentWarning);
            break;
        case NJClient::NJC_STATUS_CANTCONNECT:
        case NJClient::NJC_STATUS_INVALIDAUTH:
            dotColour = juce::Colour(JamWideLookAndFeel::kAccentDestructive);
            break;
        default:
            dotColour = juce::Colour(JamWideLookAndFeel::kTextSecondary);
            break;
    }

    g.setColour(dotColour);
    g.fillEllipse(dotX, dotY, 8.0f, 8.0f);

    // Build rev in bottom-right corner
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary).withAlpha(0.5f));
    g.setFont(juce::FontOptions(9.0f));
    g.drawText("r" + juce::String(JAMWIDE_BUILD_NUMBER),
               getLocalBounds().reduced(4, 2),
               juce::Justification::bottomRight, false);
}

void ConnectionBar::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        // D-23: Right-click scale selector context menu
        juce::PopupMenu menu;
        menu.addSectionHeader("UI Scale");
        menu.addItem(1, "1x",   true, juce::approximatelyEqual(processorRef.scaleFactor, 1.0f));
        menu.addItem(2, "1.5x", true, juce::approximatelyEqual(processorRef.scaleFactor, 1.5f));
        menu.addItem(3, "2x",   true, juce::approximatelyEqual(processorRef.scaleFactor, 2.0f));
        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this](int result) {
                float newScale = 1.0f;
                if (result == 2) newScale = 1.5f;
                else if (result == 3) newScale = 2.0f;
                else if (result != 1) return;
                processorRef.scaleFactor = newScale;
                if (onScaleChanged) onScaleChanged(newScale);
            });
    }
    else
    {
        Component::mouseDown(e);
    }
}

void ConnectionBar::handleConnectClick()
{
    bool isConnected = (currentStatus == NJClient::NJC_STATUS_OK
                     || currentStatus == NJClient::NJC_STATUS_PRECONNECT);

    if (isConnected)
    {
        jamwide::DisconnectCommand cmd;
        processorRef.cmd_queue.try_push(std::move(cmd));
    }
    else
    {
        jamwide::ConnectCommand cmd;
        cmd.server = serverField.getText().toStdString();
        cmd.username = usernameField.getText().toStdString();
        cmd.password = passwordField.getText().toStdString();

        // Save for persistence across editor reconstructions
        processorRef.lastServerAddress = serverField.getText();
        processorRef.lastUsername = usernameField.getText();

        processorRef.cmd_queue.try_push(std::move(cmd));
    }

    if (onConnectClicked) onConnectClicked();
}

void ConnectionBar::handleCodecChange()
{
    unsigned int fourcc = 0;
    int selectedId = codecSelector.getSelectedId();
    if (selectedId == 1)
        fourcc = MAKE_NJ_FOURCC('F', 'L', 'A', 'C');
    else if (selectedId == 2)
        fourcc = MAKE_NJ_FOURCC('O', 'G', 'G', 'v');

    if (fourcc != 0)
    {
        jamwide::SetEncoderFormatCommand cmd;
        cmd.fourcc = fourcc;
        processorRef.cmd_queue.try_push(std::move(cmd));
    }
}

void ConnectionBar::updateStatus(int njcStatus, float bpm, int bpi, int beat, int numUsers)
{
    currentStatus = njcStatus;

    // Update status text
    switch (njcStatus)
    {
        case NJClient::NJC_STATUS_OK:
            statusLabel.setText("Connected", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kAccentConnect));
            break;
        case NJClient::NJC_STATUS_PRECONNECT:
            statusLabel.setText("Connecting...", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kAccentWarning));
            break;
        case NJClient::NJC_STATUS_INVALIDAUTH:
            statusLabel.setText("Invalid credentials", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kAccentDestructive));
            break;
        case NJClient::NJC_STATUS_CANTCONNECT:
            statusLabel.setText("Connection failed", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kAccentDestructive));
            break;
        default:
            statusLabel.setText("Disconnected", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kTextSecondary));
            break;
    }

    // Update connect button
    bool connected = (njcStatus == NJClient::NJC_STATUS_OK
                   || njcStatus == NJClient::NJC_STATUS_PRECONNECT);
    updateConnectedState(connected, njcStatus == NJClient::NJC_STATUS_PRECONNECT);

    // Update BPM/BPI/beat and user count
    if (njcStatus == NJClient::NJC_STATUS_OK)
    {
        bpmBpiLabel.setText(juce::String(static_cast<int>(bpm)) + " BPM | "
                          + juce::String(bpi) + " BPI | Beat "
                          + juce::String(beat),
                          juce::dontSendNotification);
        userCountLabel.setText(juce::String(numUsers) + " users", juce::dontSendNotification);
    }
    else
    {
        bpmBpiLabel.setText("", juce::dontSendNotification);
        userCountLabel.setText("", juce::dontSendNotification);
    }

    repaint();
}

void ConnectionBar::updateConnectedState(bool connected, bool connecting)
{
    if (connected)
    {
        connectButton.setButtonText("Disconnect");
        connectButton.setColour(juce::TextButton::textColourOffId,
                                juce::Colour(JamWideLookAndFeel::kAccentDestructive));
    }
    else
    {
        connectButton.setButtonText("Connect");
        connectButton.setColour(juce::TextButton::textColourOffId,
                                juce::Colour(JamWideLookAndFeel::kAccentConnect));
    }

    // Disable input fields when connected or connecting
    bool enableInputs = !connected && !connecting;
    serverField.setEnabled(enableInputs);
    usernameField.setEnabled(enableInputs);
    passwordField.setEnabled(enableInputs);
}

void ConnectionBar::setServerAddress(const juce::String& addr)
{
    serverField.setText(addr);
}

juce::String ConnectionBar::getServerAddress() const
{
    return serverField.getText();
}

juce::String ConnectionBar::getUsername() const
{
    return usernameField.getText();
}

juce::String ConnectionBar::getPassword() const
{
    return passwordField.getText();
}
