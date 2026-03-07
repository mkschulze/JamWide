#include "JamWideJuceEditor.h"
#include "core/njclient.h"

JamWideJuceEditor::JamWideJuceEditor(JamWideJuceProcessor& p)
    : AudioProcessorEditor(p), processor(p)
{
    setSize(800, 600);

    // Server field
    addAndMakeVisible(serverLabel);
    serverLabel.setText("Server:", juce::dontSendNotification);

    addAndMakeVisible(serverField);
    serverField.setText("ninbot.com");
    serverField.setMultiLine(false);

    // Username field
    addAndMakeVisible(usernameLabel);
    usernameLabel.setText("Username:", juce::dontSendNotification);

    addAndMakeVisible(usernameField);
    usernameField.setText("anonymous");
    usernameField.setMultiLine(false);

    // Connect/Disconnect button
    addAndMakeVisible(connectButton);
    connectButton.setButtonText("Connect");
    connectButton.onClick = [this]() { onConnectClicked(); };

    // Status label
    addAndMakeVisible(statusLabel);
    statusLabel.setText("Disconnected", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::FontOptions(18.0f));

    startTimerHz(10);
}

JamWideJuceEditor::~JamWideJuceEditor()
{
    stopTimer();
}

void JamWideJuceEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId));
}

void JamWideJuceEditor::resized()
{
    serverLabel.setBounds(10, 20, 80, 30);
    serverField.setBounds(100, 20, 300, 30);

    usernameLabel.setBounds(10, 60, 80, 30);
    usernameField.setBounds(100, 60, 300, 30);

    connectButton.setBounds(10, 100, 120, 30);

    statusLabel.setBounds(10, 150, getWidth() - 20, 30);
}

void JamWideJuceEditor::onConnectClicked()
{
    if (isConnected())
    {
        jamwide::DisconnectCommand cmd;
        processor.cmd_queue.try_push(std::move(cmd));
        connectButton.setButtonText("Connect");
    }
    else
    {
        jamwide::ConnectCommand cmd;
        cmd.server = serverField.getText().toStdString();
        cmd.username = usernameField.getText().toStdString();
        cmd.password = "";  // Public servers, no password
        processor.cmd_queue.try_push(std::move(cmd));
        connectButton.setButtonText("Disconnect");
    }
}

bool JamWideJuceEditor::isConnected() const
{
    if (auto* client = processor.getClient())
        return client->cached_status.load(std::memory_order_acquire) == NJClient::NJC_STATUS_OK;
    return false;
}

void JamWideJuceEditor::timerCallback()
{
    auto* client = processor.getClient();
    if (!client) return;

    int status = client->cached_status.load(std::memory_order_acquire);
    juce::String statusText;
    switch (status)
    {
        case NJClient::NJC_STATUS_OK:
            statusText = "Connected";
            connectButton.setButtonText("Disconnect");
            break;
        case NJClient::NJC_STATUS_PRECONNECT:
            statusText = "Connecting...";
            break;
        case NJClient::NJC_STATUS_CANTCONNECT:
            statusText = "Cannot connect";
            connectButton.setButtonText("Connect");
            break;
        case NJClient::NJC_STATUS_INVALIDAUTH:
            statusText = "Invalid auth";
            connectButton.setButtonText("Connect");
            break;
        default:
            statusText = "Disconnected";
            connectButton.setButtonText("Connect");
            break;
    }
    statusLabel.setText(statusText, juce::dontSendNotification);
}
