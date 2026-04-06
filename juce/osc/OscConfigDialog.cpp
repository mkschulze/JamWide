#include "OscConfigDialog.h"
#include "osc/OscServer.h"
#include "JamWideJuceProcessor.h"
#include "ui/JamWideLookAndFeel.h"

OscConfigDialog::OscConfigDialog(OscServer& server)
    : oscServer(server)
{
    // Enable toggle (per D-11)
    enableToggle.setButtonText("Enable OSC");
    enableToggle.setToggleState(oscServer.isEnabled(), juce::dontSendNotification);
    enableToggle.setWantsKeyboardFocus(true);
    enableToggle.setExplicitFocusOrder(1);
    enableToggle.onClick = [this]()
    {
        if (enableToggle.getToggleState())
        {
            applyConfig();
            // If start failed, revert toggle
            if (oscServer.hasError())
                enableToggle.setToggleState(false, juce::dontSendNotification);
        }
        else
        {
            oscServer.stop();
            oscServer.getProcessor().oscEnabled = false;
            updateErrorDisplay();
        }
    };
    addAndMakeVisible(enableToggle);

    // Receive Port label and editor
    receivePortLabel.setText("Receive Port", juce::dontSendNotification);
    receivePortLabel.setFont(juce::FontOptions(11.0f));
    receivePortLabel.setColour(juce::Label::textColourId,
                               juce::Colour(JamWideLookAndFeel::kTextPrimary));
    addAndMakeVisible(receivePortLabel);

    receivePortEditor.setText(juce::String(oscServer.isEnabled()
        ? oscServer.getReceivePort()
        : oscServer.getProcessor().oscReceivePort));
    receivePortEditor.setInputRestrictions(5, "0123456789");
    receivePortEditor.setFont(juce::FontOptions(13.0f));
    receivePortEditor.setColour(juce::TextEditor::backgroundColourId,
                                juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    receivePortEditor.setColour(juce::TextEditor::textColourId,
                                juce::Colour(JamWideLookAndFeel::kTextPrimary));
    receivePortEditor.setColour(juce::TextEditor::outlineColourId,
                                juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    receivePortEditor.setColour(juce::TextEditor::focusedOutlineColourId,
                                juce::Colour(JamWideLookAndFeel::kBorderFocus));
    receivePortEditor.setWantsKeyboardFocus(true);
    receivePortEditor.setExplicitFocusOrder(2);
    receivePortEditor.onFocusLost = [this]()
    {
        if (enableToggle.getToggleState())
            applyConfig();
    };
    receivePortEditor.onReturnKey = [this]()
    {
        if (enableToggle.getToggleState())
            applyConfig();
    };
    addAndMakeVisible(receivePortEditor);

    // Send IP label and editor
    sendIpLabel.setText("Send IP", juce::dontSendNotification);
    sendIpLabel.setFont(juce::FontOptions(11.0f));
    sendIpLabel.setColour(juce::Label::textColourId,
                          juce::Colour(JamWideLookAndFeel::kTextPrimary));
    addAndMakeVisible(sendIpLabel);

    sendIpEditor.setText(oscServer.isEnabled()
        ? oscServer.getSendIP()
        : oscServer.getProcessor().oscSendIP);
    sendIpEditor.setFont(juce::FontOptions(13.0f));
    sendIpEditor.setColour(juce::TextEditor::backgroundColourId,
                           juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    sendIpEditor.setColour(juce::TextEditor::textColourId,
                           juce::Colour(JamWideLookAndFeel::kTextPrimary));
    sendIpEditor.setColour(juce::TextEditor::outlineColourId,
                           juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    sendIpEditor.setColour(juce::TextEditor::focusedOutlineColourId,
                           juce::Colour(JamWideLookAndFeel::kBorderFocus));
    sendIpEditor.setWantsKeyboardFocus(true);
    sendIpEditor.setExplicitFocusOrder(3);
    sendIpEditor.onFocusLost = [this]()
    {
        if (enableToggle.getToggleState())
            applyConfig();
    };
    sendIpEditor.onReturnKey = [this]()
    {
        if (enableToggle.getToggleState())
            applyConfig();
    };
    addAndMakeVisible(sendIpEditor);

    // Send Port label and editor
    sendPortLabel.setText("Send Port", juce::dontSendNotification);
    sendPortLabel.setFont(juce::FontOptions(11.0f));
    sendPortLabel.setColour(juce::Label::textColourId,
                            juce::Colour(JamWideLookAndFeel::kTextPrimary));
    addAndMakeVisible(sendPortLabel);

    sendPortEditor.setText(juce::String(oscServer.isEnabled()
        ? oscServer.getSendPort()
        : oscServer.getProcessor().oscSendPort));
    sendPortEditor.setInputRestrictions(5, "0123456789");
    sendPortEditor.setFont(juce::FontOptions(13.0f));
    sendPortEditor.setColour(juce::TextEditor::backgroundColourId,
                             juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    sendPortEditor.setColour(juce::TextEditor::textColourId,
                             juce::Colour(JamWideLookAndFeel::kTextPrimary));
    sendPortEditor.setColour(juce::TextEditor::outlineColourId,
                             juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    sendPortEditor.setColour(juce::TextEditor::focusedOutlineColourId,
                             juce::Colour(JamWideLookAndFeel::kBorderFocus));
    sendPortEditor.setWantsKeyboardFocus(true);
    sendPortEditor.setExplicitFocusOrder(4);
    sendPortEditor.onFocusLost = [this]()
    {
        if (enableToggle.getToggleState())
            applyConfig();
    };
    sendPortEditor.onReturnKey = [this]()
    {
        if (enableToggle.getToggleState())
            applyConfig();
    };
    addAndMakeVisible(sendPortEditor);

    // Feedback info label (read-only)
    feedbackInfoLabel.setText("Feedback: 100ms (fixed)", juce::dontSendNotification);
    feedbackInfoLabel.setFont(juce::FontOptions(9.0f));
    feedbackInfoLabel.setColour(juce::Label::textColourId,
                                juce::Colour(JamWideLookAndFeel::kTextSecondary));
    addAndMakeVisible(feedbackInfoLabel);

    // Error label (hidden initially)
    errorLabel.setFont(juce::FontOptions(11.0f));
    errorLabel.setColour(juce::Label::textColourId,
                         juce::Colour(JamWideLookAndFeel::kAccentDestructive));
    errorLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(errorLabel);

    // Show current error if any
    updateErrorDisplay();
}

void OscConfigDialog::paint(juce::Graphics& g)
{
    // Fill background with kBgPrimary
    g.fillAll(juce::Colour(JamWideLookAndFeel::kBgPrimary));

    // Title "OSC Configuration" at 13px bold, kTextHeading, top-left with 24px padding
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextHeading));
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText("OSC Configuration",
               24, 24, getWidth() - 48, 13,
               juce::Justification::centredLeft, false);

    // 1px separator line in kBorderSubtle, 8px below title
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.drawHorizontalLine(24 + 13 + 8, 24.0f, static_cast<float>(getWidth() - 24));
}

void OscConfigDialog::resized()
{
    // Layout per UI-SPEC (200x300px, 24px outer padding)
    const int pad = 24;
    const int fieldW = getWidth() - 2 * pad;
    int curY = pad;

    // Title: 13px height + 8px below + 1px separator + 8px below = ~30px
    curY += 13 + 8 + 1 + 8;

    // Enable toggle: 20px height + 8px gap
    enableToggle.setBounds(pad, curY, fieldW, 20);
    curY += 20 + 8;

    // receivePortLabel: 14px height + 4px gap
    receivePortLabel.setBounds(pad, curY, fieldW, 14);
    curY += 14 + 4;

    // receivePortEditor: 24px height + 8px gap
    receivePortEditor.setBounds(pad, curY, fieldW, 24);
    curY += 24 + 8;

    // sendIpLabel: 14px height + 4px gap
    sendIpLabel.setBounds(pad, curY, fieldW, 14);
    curY += 14 + 4;

    // sendIpEditor: 24px height + 8px gap
    sendIpEditor.setBounds(pad, curY, fieldW, 24);
    curY += 24 + 8;

    // sendPortLabel: 14px height + 4px gap
    sendPortLabel.setBounds(pad, curY, fieldW, 14);
    curY += 14 + 4;

    // sendPortEditor: 24px height + 8px gap
    sendPortEditor.setBounds(pad, curY, fieldW, 24);
    curY += 24 + 8;

    // feedbackInfoLabel: 12px height + 8px gap
    feedbackInfoLabel.setBounds(pad, curY, fieldW, 12);
    curY += 12 + 8;

    // errorLabel: remaining space (wraps to 2 lines max)
    errorLabel.setBounds(pad, curY, fieldW, getHeight() - curY - pad);
}

void OscConfigDialog::applyConfig()
{
    // Parse receive port
    int recvPort = receivePortEditor.getText().getIntValue();
    if (recvPort < 1 || recvPort > 65535)
    {
        errorLabel.setText("Invalid port", juce::dontSendNotification);
        enableToggle.setToggleState(false, juce::dontSendNotification);
        return;
    }

    // Parse send IP
    juce::String sendIP = sendIpEditor.getText().trim();
    if (sendIP.isEmpty())
    {
        errorLabel.setText("Send IP required", juce::dontSendNotification);
        enableToggle.setToggleState(false, juce::dontSendNotification);
        return;
    }

    // Parse send port
    int sndPort = sendPortEditor.getText().getIntValue();
    if (sndPort < 1 || sndPort > 65535)
    {
        errorLabel.setText("Invalid port", juce::dontSendNotification);
        enableToggle.setToggleState(false, juce::dontSendNotification);
        return;
    }

    // Stop current, then start with new config
    oscServer.stop();

    if (!oscServer.start(recvPort, sendIP, sndPort))
    {
        // Start failed — show error, revert toggle
        enableToggle.setToggleState(false, juce::dontSendNotification);
        oscServer.getProcessor().oscEnabled = false;
        updateErrorDisplay();
        return;
    }

    // Success — update processor config for persistence
    auto& proc = oscServer.getProcessor();
    proc.oscEnabled = true;
    proc.oscReceivePort = recvPort;
    proc.oscSendIP = sendIP;
    proc.oscSendPort = sndPort;

    // Clear error
    errorLabel.setText("", juce::dontSendNotification);
    updateErrorDisplay();
}

void OscConfigDialog::updateErrorDisplay()
{
    if (oscServer.hasError())
        errorLabel.setText(oscServer.getErrorMessage(), juce::dontSendNotification);
    else
        errorLabel.setText("", juce::dontSendNotification);
}
