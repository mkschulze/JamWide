#include "JamWideJuceEditor.h"
#include "core/njclient.h"
#include "threading/ui_event.h"
#include "threading/ui_command.h"

#include <variant>

JamWideJuceEditor::JamWideJuceEditor(JamWideJuceProcessor& p)
    : AudioProcessorEditor(p),
      processorRef(p),
      connectionBar(p),
      channelStripArea(p),
      chatPanel(p)
{
    setLookAndFeel(&lookAndFeel);
    setSize(kBaseWidth, kBaseHeight);
    setResizable(false, false);  // D-22: not resizable

    // ConnectionBar
    addAndMakeVisible(connectionBar);
    connectionBar.onBrowseClicked = [this]() { showServerBrowser(); };
    connectionBar.onScaleChanged = [this](float factor) { applyScale(factor); };

    // BeatBar
    addAndMakeVisible(beatBar);

    // ChannelStripArea
    addAndMakeVisible(channelStripArea);
    channelStripArea.onBrowseClicked = [this]() { showServerBrowser(); };  // D-29

    // ChatPanel
    addAndMakeVisible(chatPanel);

    // Load persistent chat history from processor
    const auto& history = processorRef.chatHistory.getMessages();
    if (!history.empty())
        chatPanel.loadHistory(history);

    // Chat toggle button
    chatToggleButton.setButtonText(chatSidebarVisible ? "<" : ">");
    chatToggleButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kBgElevated));
    chatToggleButton.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kTextSecondary));
    chatToggleButton.onClick = [this]() { toggleChatSidebar(); };
    addAndMakeVisible(chatToggleButton);

    // ServerBrowserOverlay (hidden by default)
    addChildComponent(serverBrowser);
    serverBrowser.onServerSelected = [this](const juce::String& addr) { handleServerSelected(addr); };
    serverBrowser.onServerDoubleClicked = [this](const juce::String& addr) { handleServerDoubleClicked(addr); };

    // LicenseDialog (hidden by default)
    addChildComponent(licenseDialog);
    licenseDialog.onResponse = [this](bool accepted) { handleLicenseResponse(accepted); };

    // Start 20Hz timer for event drain and status polling
    startTimerHz(20);

    // Apply initial scale if not 1.0
    if (!juce::approximatelyEqual(processorRef.scaleFactor, 1.0f))
        applyScale(processorRef.scaleFactor);
}

JamWideJuceEditor::~JamWideJuceEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void JamWideJuceEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kBgPrimary));
}

void JamWideJuceEditor::resized()
{
    auto area = getLocalBounds();

    // ConnectionBar at top: full width, kConnectionBarHeight tall
    connectionBar.setBounds(area.removeFromTop(kConnectionBarHeight));

    // BeatBar below connection bar: full width, kBeatBarHeight tall
    beatBar.setBounds(area.removeFromTop(kBeatBarHeight));

    // Chat panel at right (if visible)
    int chatWidth = chatSidebarVisible ? kChatPanelWidth : 0;
    if (chatSidebarVisible)
        chatPanel.setBounds(area.removeFromRight(chatWidth));

    // Chat toggle button on the divider edge
    int toggleX = chatSidebarVisible
        ? (getWidth() - chatWidth - kChatToggleWidth)
        : (getWidth() - kChatToggleWidth);
    int toggleY = kConnectionBarHeight + kBeatBarHeight + (area.getHeight() - kChatToggleHeight) / 2;
    chatToggleButton.setBounds(toggleX, toggleY, kChatToggleWidth, kChatToggleHeight);

    // Channel strip area fills remaining center
    channelStripArea.setBounds(area);

    // Overlays: full editor bounds
    serverBrowser.setBounds(getLocalBounds());
    licenseDialog.setBounds(getLocalBounds());
}

void JamWideJuceEditor::timerCallback()
{
    drainEvents();
    pollStatus();

    // Update beat bar from atomics
    int bpi = processorRef.uiSnapshot.bpi.load(std::memory_order_relaxed);
    int beat = processorRef.uiSnapshot.beat_position.load(std::memory_order_relaxed);
    int iPos = processorRef.uiSnapshot.interval_position.load(std::memory_order_relaxed);
    int iLen = processorRef.uiSnapshot.interval_length.load(std::memory_order_relaxed);
    beatBar.update(bpi, beat, iPos, iLen);

    // Note: VU updates are driven by ChannelStripArea's own 30Hz timer (REVIEW FIX #7)
    // The editor does NOT need to call channelStripArea.updateVuLevels() here.
}

void JamWideJuceEditor::drainEvents()
{
    // Drain event queue (StatusChanged, ServerList, TopicChanged, UserInfoChanged)
    processorRef.evt_queue.drain([this](jamwide::UiEvent&& evt) {
        std::visit([this](auto&& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, jamwide::StatusChangedEvent>)
            {
                // Status updates are handled in pollStatus()
            }
            else if constexpr (std::is_same_v<T, jamwide::ServerListEvent>)
            {
                // Store server list on processor and update browser if showing
                processorRef.cachedServerList = std::move(e.servers);
                if (serverBrowser.isShowing())
                    serverBrowser.updateList(processorRef.cachedServerList, juce::String(e.error));
            }
            else if constexpr (std::is_same_v<T, jamwide::TopicChangedEvent>)
            {
                chatPanel.setTopic(juce::String(e.topic));
            }
            else if constexpr (std::is_same_v<T, jamwide::UserInfoChangedEvent>)
            {
                refreshChannelStrips();
            }
        }, std::move(evt));
    });

    // Drain chat queue
    processorRef.chat_queue.drain([this](ChatMessage&& msg) {
        chatPanel.addMessage(msg);
    });

    // Check license pending
    if (processorRef.license_pending.load(std::memory_order_acquire) && !licenseDialog.isShowing())
    {
        showLicenseDialog();
    }
}

void JamWideJuceEditor::pollStatus()
{
    auto* client = processorRef.getClient();
    if (!client) return;

    int status = client->cached_status.load(std::memory_order_acquire);
    float bpm = processorRef.uiSnapshot.bpm.load(std::memory_order_relaxed);
    int bpi = processorRef.uiSnapshot.bpi.load(std::memory_order_relaxed);
    int beat = processorRef.uiSnapshot.beat_position.load(std::memory_order_relaxed);
    int numUsers = processorRef.userCount.load(std::memory_order_relaxed);

    connectionBar.updateStatus(status, bpm, bpi, beat, numUsers);

    // REVIEW FIX: Use member variable prevPollStatus_ instead of static int lastStatus.
    // This prevents state leaking across editor reconstructions.
    if (status != prevPollStatus_)
    {
        if (status == NJClient::NJC_STATUS_OK)
        {
            channelStripArea.setConnectedState();
            chatPanel.setConnectedState();
        }
        else if (prevPollStatus_ == NJClient::NJC_STATUS_OK || prevPollStatus_ == -1)
        {
            channelStripArea.setDisconnectedState();
            chatPanel.setNotConnectedState();
        }
        prevPollStatus_ = status;
    }
}

void JamWideJuceEditor::showServerBrowser()
{
    serverBrowser.setLoading();
    serverBrowser.setBounds(getLocalBounds());
    serverBrowser.show();
    jamwide::RequestServerListCommand cmd;
    cmd.url = "http://autosong.ninjam.com/serverlist.php";
    processorRef.cmd_queue.try_push(std::move(cmd));
}

void JamWideJuceEditor::showLicenseDialog()
{
    juce::String text;
    {
        std::lock_guard<std::mutex> lock(processorRef.license_mutex);
        text = processorRef.license_text;
    }
    licenseDialog.setBounds(getLocalBounds());
    licenseDialog.show(text);
}

void JamWideJuceEditor::handleLicenseResponse(bool accepted)
{
    processorRef.license_response.store(accepted ? 1 : -1, std::memory_order_release);
    processorRef.license_cv.notify_one();
}

void JamWideJuceEditor::handleServerSelected(const juce::String& address)
{
    // D-11: Single-click fills address into connection bar
    connectionBar.setServerAddress(address);
}

void JamWideJuceEditor::handleServerDoubleClicked(const juce::String& address)
{
    // D-12: Double-click fills address AND auto-connects
    connectionBar.setServerAddress(address);
    // REVIEW FIX: Double-click auto-connect must use the current password field value,
    // not an empty string. Passworded servers would fail with empty password.
    jamwide::ConnectCommand cmd;
    cmd.server = address.toStdString();
    cmd.username = connectionBar.getUsername().toStdString();
    cmd.password = connectionBar.getPassword().toStdString();  // Use existing password
    processorRef.cmd_queue.try_push(std::move(cmd));
    serverBrowser.dismiss();  // D-14
}

void JamWideJuceEditor::refreshChannelStrips()
{
    channelStripArea.refreshFromUsers(processorRef.cachedUsers);
}

void JamWideJuceEditor::toggleChatSidebar()
{
    chatSidebarVisible = !chatSidebarVisible;
    chatToggleButton.setButtonText(chatSidebarVisible ? "<" : ">");
    chatPanel.setVisible(chatSidebarVisible);
    // REVIEW FIX #5: Do NOT call setSize(). Redistribute space within current bounds.
    // The channel strip area expands to fill the space freed by hiding chat.
    resized();
}

void JamWideJuceEditor::applyScale(float factor)
{
    // REVIEW FIX #1: Transform ONLY. Do NOT call setSize with scaled dimensions.
    // Plugin hosts see the component bounds as the "real" size. If we both
    // transform AND resize, the host applies its own DPI scaling on top,
    // resulting in incorrect 2x or 3x scaling.
    //
    // setSize stays at kBaseWidth x kBaseHeight. The AffineTransform handles
    // the visual scaling. The host constrains the window to base dimensions,
    // and the transform makes content appear larger within those bounds.
    setTransform(juce::AffineTransform::scale(factor));

    // In standalone mode only, the window can grow.
    // In plugin mode, the host manages the window size.
    if (auto* peer = getPeer())
    {
        if (peer->getStyleFlags() & juce::ComponentPeer::windowHasTitleBar)
        {
            // Standalone: resize window to fit scaled content
            setSize(static_cast<int>(kBaseWidth * factor),
                    static_cast<int>(kBaseHeight * factor));
        }
    }
}
