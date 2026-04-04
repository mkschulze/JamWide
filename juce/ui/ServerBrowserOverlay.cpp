#include "ServerBrowserOverlay.h"
#include "JamWideLookAndFeel.h"

ServerBrowserOverlay::ServerBrowserOverlay()
{
    titleLabel.setText("Browse Servers", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
    titleLabel.setColour(juce::Label::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextHeading));
    addAndMakeVisible(titleLabel);

    statusLabel.setFont(juce::FontOptions(13.0f));
    statusLabel.setColour(juce::Label::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextSecondary));
    addAndMakeVisible(statusLabel);

    closeButton.setButtonText("X");
    closeButton.onClick = [this]() { dismiss(); };
    addAndMakeVisible(closeButton);

    refreshButton.setButtonText("Refresh");
    refreshButton.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect));
    refreshButton.onClick = [this]() {
        setLoading();
        if (onRefreshClicked) onRefreshClicked();
    };
    addAndMakeVisible(refreshButton);

    listBox.setModel(this);
    listBox.setRowHeight(56);
    listBox.setColour(juce::ListBox::backgroundColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceOverlay));
    listBox.setColour(juce::ListBox::outlineColourId,
        juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    addAndMakeVisible(listBox);

    setVisible(false);
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(true);
}

void ServerBrowserOverlay::show()
{
    showing = true;
    setVisible(true);
    setInterceptsMouseClicks(true, true);
    toFront(true);
    grabKeyboardFocus();
    resized();
}

void ServerBrowserOverlay::dismiss()
{
    showing = false;
    setVisible(false);
    setInterceptsMouseClicks(false, false);
    if (onDismissed)
        onDismissed();
}

bool ServerBrowserOverlay::isShowing() const
{
    return showing;
}

void ServerBrowserOverlay::updateList(const std::vector<ServerListEntry>& newServers, const juce::String& error)
{
    servers = newServers;
    errorMessage = error;
    loading = false;

    if (!error.isEmpty())
    {
        statusLabel.setText("Could not load server list. Check your connection.",
            juce::dontSendNotification);
        statusLabel.setVisible(true);
    }
    else if (servers.empty())
    {
        statusLabel.setText("No servers found.", juce::dontSendNotification);
        statusLabel.setVisible(true);
    }
    else
    {
        statusLabel.setVisible(false);
    }

    listBox.updateContent();
    listBox.repaint();
}

void ServerBrowserOverlay::setLoading()
{
    loading = true;
    servers.clear();
    errorMessage.clear();
    statusLabel.setText("Loading servers...", juce::dontSendNotification);
    statusLabel.setVisible(true);
    listBox.updateContent();
    listBox.repaint();
}

void ServerBrowserOverlay::paint(juce::Graphics& g)
{
    // Scrim over entire area
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

void ServerBrowserOverlay::resized()
{
    auto dialogBounds = getLocalBounds().withSizeKeepingCentre(kDialogWidth, kDialogHeight);
    auto area = dialogBounds.reduced(16);

    // Title row: title ... refresh ... close
    auto titleRow = area.removeFromTop(32);
    closeButton.setBounds(titleRow.removeFromRight(32));
    titleRow.removeFromRight(4);
    refreshButton.setBounds(titleRow.removeFromRight(70).withTrimmedTop(4).withTrimmedBottom(4));
    titleLabel.setBounds(titleRow);

    area.removeFromTop(8);

    // Status label
    if (statusLabel.isVisible())
    {
        statusLabel.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
    }

    // List box fills remaining space
    listBox.setBounds(area);
}

bool ServerBrowserOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        dismiss();
        return true;
    }
    return false;
}

int ServerBrowserOverlay::getNumRows()
{
    return static_cast<int>(servers.size());
}

void ServerBrowserOverlay::paintListBoxItem(int row, juce::Graphics& g,
                                             int width, int height, bool isSelected)
{
    if (row < 0 || row >= static_cast<int>(servers.size()))
        return;

    const auto& entry = servers[static_cast<size_t>(row)];

    if (isSelected)
        g.fillAll(juce::Colour(JamWideLookAndFeel::kBorderSubtle));

    // Left side: server name + address
    auto nameFont = juce::FontOptions(13.0f);
    auto smallFont = juce::FontOptions(11.0f);

    g.setFont(nameFont);
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary));
    g.drawText(juce::String(entry.name),
        8, 4, width / 2 - 16, 18, juce::Justification::centredLeft, true);

    g.setFont(smallFont);
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
    juce::String addr = juce::String(entry.host);
    if (entry.port != 2049)
        addr += ":" + juce::String(entry.port);
    g.drawText(addr,
        8, 22, width / 2 - 16, 14, juce::Justification::centredLeft, true);

    // Right side: users + BPM/BPI
    g.setFont(smallFont);
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary));
    juce::String usersStr = juce::String(entry.users) + "/" + juce::String(entry.max_users) + " users";
    g.drawText(usersStr,
        width / 2, 4, width / 2 - 8, 14, juce::Justification::centredRight, true);

    juce::String bpmBpiStr;
    if (entry.bpm > 0)
        bpmBpiStr = juce::String(entry.bpm) + " BPM / " + juce::String(entry.bpi) + " BPI";
    else
        bpmBpiStr = "Lobby";
    g.drawText(bpmBpiStr,
        width / 2, 18, width / 2 - 8, 14, juce::Justification::centredRight, true);

    // Bottom: topic (truncated)
    if (!entry.topic.empty())
    {
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
        g.drawText(juce::String(entry.topic),
            8, 36, width - 16, 14, juce::Justification::centredLeft, true);
    }

    // 1px bottom border
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle).withAlpha(0.5f));
    g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
}

juce::String ServerBrowserOverlay::formatAddress(int row) const
{
    if (row < 0 || row >= static_cast<int>(servers.size()))
        return {};

    const auto& entry = servers[static_cast<size_t>(row)];
    juce::String addr = juce::String(entry.host);
    if (entry.port != 2049)
        addr += ":" + juce::String(entry.port);
    return addr;
}

void ServerBrowserOverlay::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    // D-11: Single-click fills address into connection bar
    auto addr = formatAddress(row);
    if (addr.isNotEmpty() && onServerSelected)
        onServerSelected(addr);
}

void ServerBrowserOverlay::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    // D-12: Double-click fills address AND auto-connects
    auto addr = formatAddress(row);
    if (addr.isNotEmpty() && onServerDoubleClicked)
        onServerDoubleClicked(addr);
    // D-14: Server browser overlay closes on connect
    dismiss();
}

void ServerBrowserOverlay::mouseDown(const juce::MouseEvent& e)
{
    // Click outside dialog rect -> dismiss
    auto dialogBounds = getLocalBounds().withSizeKeepingCentre(kDialogWidth, kDialogHeight);
    if (!dialogBounds.contains(e.getPosition()))
        dismiss();
}
