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

    volumeLabel.setText("VOL", juce::dontSendNotification);
    volumeLabel.setFont(juce::FontOptions(10.0f));
    volumeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8888ff));
    volumeLabel.setJustificationType(juce::Justification::centredRight);
    addChildComponent(volumeLabel);

    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(0.7, juce::dontSendNotification);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff8888ff));
    volumeSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffaaccff));
    volumeSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0x26ffffff));
    volumeSlider.onValueChange = [this]() {
        prelistenVolume = static_cast<float>(volumeSlider.getValue());
    };
    addChildComponent(volumeSlider);

    listBox.setModel(this);
    listBox.setRowHeight(72);
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
    if (!prelistenHost.empty())
    {
        prelistenHost.clear();
        prelistenPort = 0;
        prelistenRow = -1;
        prelistenConnected = false;
        volumeSlider.setVisible(false);
        volumeLabel.setVisible(false);
        if (onStopListenClicked) onStopListenClicked();
    }

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

    // Re-resolve prelisten row after list change (addresses review UI#1:
    // prelistenRow index is stale after server list refresh)
    if (!prelistenHost.empty())
    {
        resolvePrelistenRow();
        // Volume slider visibility tracks row resolution
        bool hasRow = (prelistenRow >= 0);
        volumeSlider.setVisible(hasRow && prelistenConnected);
        volumeLabel.setVisible(hasRow && prelistenConnected);
        resized();
    }
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

    if (prelistenRow >= 0)
    {
        auto volArea = titleRow.removeFromRight(90);
        titleRow.removeFromRight(4);
        volumeLabel.setBounds(volArea.removeFromLeft(28).withTrimmedTop(6).withTrimmedBottom(6));
        volumeSlider.setBounds(volArea.withTrimmedTop(6).withTrimmedBottom(6));
    }

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

    // Username line (below topic)
    auto userFont = juce::FontOptions(11.0f);
    g.setFont(userFont);
    if (!entry.user_list.empty())
    {
        g.setColour(juce::Colour(JamWideLookAndFeel::kAccentConnect));
        g.drawText(juce::String(entry.user_list),
            8, 52, width - 16, 16, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary).withAlpha(0.4f));
        g.drawText("No users",
            8, 52, width - 16, 16, juce::Justification::centredLeft, true);
    }

    // Active prelisten row: blue tint + left edge bar + LISTENING badge
    if (row == prelistenRow)
    {
        g.setColour(juce::Colour(0x14648cff));
        g.fillRect(0, 0, width, height);
        g.setColour(juce::Colour(0xff8888ff));
        g.fillRect(0, 0, 3, height);

        if (prelistenConnected)
        {
            // LISTENING badge next to server name
            auto nameWidth = juce::Font(juce::FontOptions(13.0f, juce::Font::bold))
                .getStringWidth(juce::String(entry.name));
            int badgeX = 8 + nameWidth + 8;
            g.setColour(juce::Colour(0x4d648cff));
            g.fillRoundedRectangle(static_cast<float>(badgeX), 3.0f, 62.0f, 14.0f, 3.0f);
            g.setFont(juce::FontOptions(9.0f));
            g.setColour(juce::Colour(0xffaaccff));
            g.drawText("LISTENING", badgeX, 3, 62, 14, juce::Justification::centred, false);
        }
        else
        {
            // CONNECTING... badge while waiting for server handshake
            auto nameWidth = juce::Font(juce::FontOptions(13.0f, juce::Font::bold))
                .getStringWidth(juce::String(entry.name));
            int badgeX = 8 + nameWidth + 8;
            g.setColour(juce::Colour(0x33648cff));
            g.fillRoundedRectangle(static_cast<float>(badgeX), 3.0f, 80.0f, 14.0f, 3.0f);
            g.setFont(juce::FontOptions(9.0f));
            g.setColour(juce::Colour(0x99aaccff));
            g.drawText("CONNECTING...", badgeX, 3, 80, 14, juce::Justification::centred, false);
        }
    }

    // Listen/Stop button (rooms with users only, respects listenEnabled)
    if (entry.users > 0 && (listenEnabled || row == prelistenRow))
    {
        int btnX = width - 70, btnY = height - 22, btnW = 62, btnH = 16;

        if (row == prelistenRow)
        {
            // Stop button (blue filled)
            g.setColour(juce::Colour(0x40648cff));
            g.fillRoundedRectangle((float)btnX, (float)btnY, (float)btnW, (float)btnH, 4.0f);
            g.setColour(juce::Colour(0xff8888ff));
            g.drawRoundedRectangle((float)btnX, (float)btnY, (float)btnW, (float)btnH, 4.0f, 1.0f);
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(juce::CharPointer_UTF8("\xe2\x96\xa0 Stop"), btnX, btnY, btnW, btnH,
                       juce::Justification::centred, false);
        }
        else
        {
            // Listen button (subtle)
            g.setColour(juce::Colour(0x0fffffff));
            g.fillRoundedRectangle((float)btnX, (float)btnY, (float)btnW, (float)btnH, 4.0f);
            g.setColour(juce::Colour(0x1fffffff));
            g.drawRoundedRectangle((float)btnX, (float)btnY, (float)btnW, (float)btnH, 4.0f, 1.0f);
            g.setFont(juce::FontOptions(10.0f));
            g.setColour(juce::Colour(0xff888888));
            g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb6 Listen"), btnX, btnY, btnW, btnH,
                       juce::Justification::centred, false);
        }
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

void ServerBrowserOverlay::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= static_cast<int>(servers.size()))
        return;

    const auto& entry = servers[static_cast<size_t>(row)];

    // Button hit-testing (Research Gray Area 3: coordinates are relative to RowComponent)
    int btnX = listBox.getRowPosition(row, true).getWidth() - 70;
    int btnY = listBox.getRowPosition(row, true).getHeight() - 22;
    auto pt = e.getPosition();

    if (entry.users > 0 && (listenEnabled || row == prelistenRow) &&
        pt.x >= btnX && pt.x <= btnX + 62 && pt.y >= btnY && pt.y <= btnY + 16)
    {
        if (row == prelistenRow)
        {
            // Stop: clear identity state
            prelistenHost.clear();
            prelistenPort = 0;
            prelistenRow = -1;
            prelistenConnected = false;
            volumeSlider.setVisible(false);
            volumeLabel.setVisible(false);
            resized(); listBox.repaint();
            if (onStopListenClicked) onStopListenClicked();
        }
        else
        {
            // Listen: set identity state by host+port (NOT row index)
            prelistenHost = entry.host;
            prelistenPort = entry.port;
            prelistenRow = row;
            prelistenConnected = false;  // Will become true on Connected event
            // Don't show volume slider yet -- wait for Connected state
            resized(); listBox.repaint();
            if (onListenClicked) onListenClicked(entry.host, entry.port);
        }
        return;
    }

    // Normal single-click: fill address bar
    if (onServerSelected) onServerSelected(formatAddress(row));
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

void ServerBrowserOverlay::resolvePrelistenRow()
{
    // Derive row index from host+port identity.
    // Called after every updateList() and after setPrelistenState().
    if (prelistenHost.empty())
    {
        prelistenRow = -1;
        return;
    }
    prelistenRow = -1;
    for (int i = 0; i < static_cast<int>(servers.size()); ++i)
    {
        if (servers[static_cast<size_t>(i)].host == prelistenHost &&
            servers[static_cast<size_t>(i)].port == prelistenPort)
        {
            prelistenRow = i;
            break;
        }
    }
    // If server disappeared from list, prelistenRow stays -1.
    // Audio continues; visual indicator is gone until next refresh.
}

void ServerBrowserOverlay::setPrelistenState(jamwide::PrelistenStatus status,
                                              const std::string& host, int port)
{
    switch (status)
    {
        case jamwide::PrelistenStatus::Connecting:
            prelistenHost = host;
            prelistenPort = port;
            prelistenConnected = false;
            resolvePrelistenRow();
            // No volume slider during connecting
            volumeSlider.setVisible(false);
            volumeLabel.setVisible(false);
            break;

        case jamwide::PrelistenStatus::Connected:
            // Update identity from server response (may differ from command host)
            if (!host.empty())
            {
                prelistenHost = host;
                prelistenPort = port;
                resolvePrelistenRow();
            }
            prelistenConnected = true;
            volumeSlider.setVisible(prelistenRow >= 0);
            volumeLabel.setVisible(prelistenRow >= 0);
            break;

        case jamwide::PrelistenStatus::Stopped:
        case jamwide::PrelistenStatus::Failed:
            prelistenHost.clear();
            prelistenPort = 0;
            prelistenRow = -1;
            prelistenConnected = false;
            volumeSlider.setVisible(false);
            volumeLabel.setVisible(false);
            break;
    }

    resized();
    listBox.repaint();
}

void ServerBrowserOverlay::setListenEnabled(bool enabled)
{
    listenEnabled = enabled;
    listBox.repaint();
}
