#include "JamWideJuceEditor.h"
#include "core/njclient.h"
#include "threading/ui_event.h"
#include "threading/ui_command.h"
#include "video/BrowserDetect.h"
#include "video/VideoCompanion.h"

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
    connectionBar.onRouteModeChanged = [this](int mode) {
        jamwide::SetRoutingModeCommand cmd;
        cmd.mode = mode;
        processorRef.cmd_queue.try_push(cmd);
    };
    // Initialize Route button highlight from persisted routing mode
    connectionBar.setRoutingModeHighlight(
        processorRef.routingMode.load(std::memory_order_relaxed));

    // BeatBar
    addAndMakeVisible(beatBar);
    beatBar.setProcessor(processorRef);

    // SessionInfoStrip (hidden by default, toggleable via context menu)
    addChildComponent(sessionInfoStrip);
    infoStripVisible = processorRef.infoStripVisible;
    sessionInfoStrip.setVisible(infoStripVisible);

    // ChannelStripArea
    addAndMakeVisible(channelStripArea);
    channelStripArea.onBrowseClicked = [this]() { showServerBrowser(); };  // D-29
    channelStripArea.onLayoutChanged = [this]() {
        int chatW = chatSidebarVisible ? kChatPanelWidth : 0;
        int available = getWidth() - chatW - kChatToggleWidth - 4;
        connectionBar.setFitHighlight(channelStripArea.getDesiredWidth() > available);
    };

    // Fit button in ConnectionBar -- resize editor to show all channels
    connectionBar.onFitClicked = [this]() {
        int chatW = chatSidebarVisible ? kChatPanelWidth : 0;
        int needed = channelStripArea.getDesiredWidth() + chatW + kChatToggleWidth + 4;
        needed = juce::jmax(kBaseWidth, needed);
        if (needed != getWidth())
            setSize(needed, kBaseHeight);
        connectionBar.setFitHighlight(false);  // we just fit, no overflow
    };

    // ChatPanel
    addAndMakeVisible(chatPanel);

    // Load persistent chat history from processor
    const auto& history = processorRef.chatHistory.getMessages();
    if (!history.empty())
        chatPanel.loadHistory(history);

    // Sync chat visibility from processor (survives editor reconstruction, per D-23)
    chatSidebarVisible = processorRef.chatSidebarVisible;

    // Chat toggle button (custom painted arrow)
    chatToggleButton.pointsRight = chatSidebarVisible;
    chatToggleButton.onClick = [this]() { toggleChatSidebar(); };
    addAndMakeVisible(chatToggleButton);
    chatPanel.setVisible(chatSidebarVisible);

    // ServerBrowserOverlay (hidden by default)
    addChildComponent(serverBrowser);
    serverBrowser.onServerSelected = [this](const juce::String& addr) { handleServerSelected(addr); };
    serverBrowser.onServerDoubleClicked = [this](const juce::String& addr) { handleServerDoubleClicked(addr); };
    serverBrowser.onRefreshClicked = [this]() {
        jamwide::RequestServerListCommand cmd;
        cmd.url = "http://autosong.ninjam.com/serverlist.php";
        processorRef.cmd_queue.try_push(std::move(cmd));
    };

    serverBrowser.onListenClicked = [this](const std::string& host, int port) {
        jamwide::PrelistenCommand cmd;
        cmd.host = host;
        cmd.port = port;
        processorRef.cmd_queue.try_push(std::move(cmd));
    };

    serverBrowser.onStopListenClicked = [this]() {
        processorRef.cmd_queue.try_push(jamwide::StopPrelistenCommand{});
    };

    // LicenseDialog (hidden by default)
    addChildComponent(licenseDialog);
    licenseDialog.onResponse = [this](bool accepted) { handleLicenseResponse(accepted); };

    // Video privacy dialog (hidden by default)
    addChildComponent(videoPrivacyDialog);
    videoPrivacyDialog.onResponse = [this](bool accepted) {
        if (accepted) {
            if (processorRef.videoCompanion) {
                bool launched = processorRef.videoCompanion->launchCompanion(
                    connectionBar.getServerAddress(),
                    connectionBar.getUsername(),
                    connectionBar.getPassword());
                if (launched) {
                    connectionBar.setVideoActive(true);
                } else {
                    // Addresses review concern #8: port bind failure.
                    // launchCompanion returned false -- WS server failed to start.
                    // Button stays inactive. Log for debugging.
                    DBG("VideoCompanion: launch failed (WebSocket server could not start)");
                }
            }
        }
    };

    // Wire video button click (D-01 through D-07)
    connectionBar.onVideoClicked = [this]() {
        if (!processorRef.videoCompanion) return;

        // D-04 + review concern #12: If already active, re-open companion page only.
        // No modal shown. No server restart. Just call relaunchBrowser().
        // Addresses review concern #13: privacy modal ONLY on first activation per session.
        if (processorRef.videoCompanion->isActive()) {
            processorRef.videoCompanion->relaunchBrowser();
            return;
        }

        // D-05, D-06: Show privacy modal on every new activation.
        // D-07 + review concern #5: Browser detection is best-effort advisory.
        // If detection fails, defaults to true (assume Chromium = skip warning).
        // Warning is shown but NEVER blocks launch.
        bool showBrowserWarning = !jamwide::isDefaultBrowserChromium();
        videoPrivacyDialog.show(showBrowserWarning);
    };

    // Restore state if already connected (editor recreated while session active).
    // HasUserInfoChanged() is destructive — the flag was consumed before the old
    // editor was destroyed, so no UserInfoChangedEvent will fire. We must
    // populate strips and set connected state from the processor's cached data.
    {
        auto* client = processorRef.getClient();
        if (client && client->cached_status.load(std::memory_order_acquire) == NJClient::NJC_STATUS_OK)
        {
            channelStripArea.setConnectedState();
            chatPanel.setConnectedState();
            {
                // Snapshot cachedUsers under the lock so refreshFromUsers
                // iterates a stable copy, not the live shared vector.
                std::vector<NJClient::RemoteUserInfo> usersCopy;
                {
                    std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
                    usersCopy = processorRef.cachedUsers;
                }
                if (!usersCopy.empty())
                    channelStripArea.refreshFromUsers(usersCopy);
            }
            prevPollStatus_ = NJClient::NJC_STATUS_OK;

            // Restore video button state if videoCompanion is active (editor recreated mid-session)
            if (processorRef.videoCompanion && processorRef.videoCompanion->isActive()) {
                connectionBar.setVideoActive(true);
            }
        }
    }

    // Listen to mouse clicks on all child components so that any left-click
    // in the plugin window focuses the chat input (saves an extra click when
    // switching from the DAW).
    addMouseListener(this, true);

    // Start 20Hz timer for event drain and status polling
    startTimerHz(20);

    // Apply initial scale if not 1.0
    if (!juce::approximatelyEqual(processorRef.scaleFactor, 1.0f))
        applyScale(processorRef.scaleFactor);
}

JamWideJuceEditor::~JamWideJuceEditor()
{
    removeMouseListener(this);
    stopTimer();
    setLookAndFeel(nullptr);
}

void JamWideJuceEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kBgPrimary));
}

void JamWideJuceEditor::mouseDown(const juce::MouseEvent& e)
{
    // Clicks from child components arrive here via addMouseListener(this, true).
    // Forward left-clicks to the chat input so users can start typing immediately
    // after clicking anywhere in the plugin window (no extra click needed).
    // Skip interactive controls so they keep working without stealing focus.
    if (e.eventComponent != this)
    {
        if (!e.mods.isPopupMenu()
            && chatSidebarVisible
            && !serverBrowser.isVisible()
            && !licenseDialog.isVisible()
            && !videoPrivacyDialog.isVisible()
            && dynamic_cast<juce::TextEditor*>(e.eventComponent) == nullptr
            && dynamic_cast<juce::Button*>(e.eventComponent) == nullptr
            && dynamic_cast<juce::Slider*>(e.eventComponent) == nullptr
            && dynamic_cast<juce::ComboBox*>(e.eventComponent) == nullptr
            && dynamic_cast<VbFader*>(e.eventComponent) == nullptr)
        {
            chatPanel.focusChatInput();
        }
        return;
    }

    // Direct clicks on editor background
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("UI Scale");
        menu.addItem(1, "1x",   true, juce::approximatelyEqual(processorRef.scaleFactor, 1.0f));
        menu.addItem(2, "1.5x", true, juce::approximatelyEqual(processorRef.scaleFactor, 1.5f));
        menu.addItem(3, "2x",   true, juce::approximatelyEqual(processorRef.scaleFactor, 2.0f));
        menu.addSeparator();
        menu.addItem(4, "Show Session Info", true, infoStripVisible);
        menu.showMenuAsync(juce::PopupMenu::Options().withParentComponent(this),
            [this](int result) {
                if (result >= 1 && result <= 3)
                {
                    float newScale = 1.0f;
                    if (result == 2) newScale = 1.5f;
                    else if (result == 3) newScale = 2.0f;
                    processorRef.scaleFactor = newScale;
                    applyScale(newScale);
                }
                else if (result == 4)
                {
                    toggleSessionInfoStrip();
                }
            });
    }
    else
    {
        if (chatSidebarVisible)
            chatPanel.focusChatInput();
        AudioProcessorEditor::mouseDown(e);
    }
}

void JamWideJuceEditor::resized()
{
    auto area = getLocalBounds();

    // ConnectionBar at top: full width, kConnectionBarHeight tall
    connectionBar.setBounds(area.removeFromTop(kConnectionBarHeight));

    // BeatBar below connection bar: full width, kBeatBarHeight tall
    beatBar.setBounds(area.removeFromTop(kBeatBarHeight));

    // Conditional session info strip below BeatBar
    if (infoStripVisible)
        sessionInfoStrip.setBounds(area.removeFromTop(kSessionInfoStripHeight));

    // Chat panel at right (if visible)
    int chatWidth = chatSidebarVisible ? kChatPanelWidth : 0;
    if (chatSidebarVisible)
        chatPanel.setBounds(area.removeFromRight(chatWidth));

    // Chat toggle button overlapping the left edge of the chat panel
    int toggleX = chatSidebarVisible
        ? (getWidth() - chatWidth)
        : (getWidth() - kChatToggleWidth);
    int toggleY = kConnectionBarHeight + kBeatBarHeight + (area.getHeight() - kChatToggleHeight) / 2;
    chatToggleButton.setBounds(toggleX, toggleY, kChatToggleWidth, kChatToggleHeight);

    // Channel strip area fills remaining center
    channelStripArea.setBounds(area);

    // Overlays: full editor bounds
    serverBrowser.setBounds(getLocalBounds());
    licenseDialog.setBounds(getLocalBounds());
    videoPrivacyDialog.setBounds(getLocalBounds());
}

void JamWideJuceEditor::timerCallback()
{
    drainEvents();
    pollStatus();

    // Sync prelisten volume from browser slider to processor atomic (20Hz)
    // Ownership: browser UI slider is source of truth, processor atomic is runtime consumer
    if (processorRef.prelisten_mode.load(std::memory_order_relaxed))
        processorRef.prelisten_volume.store(
            serverBrowser.prelistenVolume, std::memory_order_relaxed);

    // Update beat bar from atomics
    int bpi = processorRef.uiSnapshot.bpi.load(std::memory_order_relaxed);
    int beat = processorRef.uiSnapshot.beat_position.load(std::memory_order_relaxed);
    int iPos = processorRef.uiSnapshot.interval_position.load(std::memory_order_relaxed);
    int iLen = processorRef.uiSnapshot.interval_length.load(std::memory_order_relaxed);
    beatBar.update(bpi, beat, iPos, iLen);

    // Broadcast beat position to video companion page for sync indicator
    if (processorRef.videoCompanion && processorRef.videoCompanion->isActive())
    {
        int intervalCount = processorRef.uiSnapshot.interval_count.load(std::memory_order_relaxed);
        processorRef.videoCompanion->broadcastBeatHeartbeat(beat, bpi, intervalCount);
    }

    // Update BeatBar BPM for label area display
    beatBar.setBpm(processorRef.uiSnapshot.bpm.load(std::memory_order_relaxed));

    // Update SessionInfoStrip if visible
    if (infoStripVisible)
    {
        int intervalCount = processorRef.uiSnapshot.interval_count.load(std::memory_order_relaxed);
        unsigned int elapsedMs = processorRef.uiSnapshot.session_elapsed_ms.load(std::memory_order_relaxed);
        bool isStandalone = (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone);

        // Read sync state from single atomic int (not two booleans -- review fix)
        int syncState = processorRef.syncState_.load(std::memory_order_relaxed);

        int userCount = processorRef.userCount.load(std::memory_order_relaxed);

        // Look up max user slots from the cached public server list. The
        // NINJAM protocol itself does not expose max-slots, so this is only
        // populated when the user has refreshed the server browser and the
        // connected server appears in the list. maxUsers=0 => unknown; the
        // strip falls back to rendering just the current count.
        int maxUsers = 0;
        const juce::String& addr = processorRef.lastServerAddress;
        if (addr.isNotEmpty() && !processorRef.cachedServerList.empty())
        {
            // Parse "host:port" out of the editor's last-used address string.
            // Port is optional — match on host alone if it's missing.
            int colon = addr.lastIndexOfChar(':');
            juce::String host = colon >= 0 ? addr.substring(0, colon) : addr;
            int port = colon >= 0 ? addr.substring(colon + 1).getIntValue() : 0;
            for (const auto& entry : processorRef.cachedServerList)
            {
                if (juce::String(entry.host).equalsIgnoreCase(host)
                    && (port == 0 || entry.port == port))
                {
                    maxUsers = entry.max_users;
                    break;
                }
            }
        }

        sessionInfoStrip.update(intervalCount, elapsedMs, beat, bpi, syncState, isStandalone, userCount, maxUsers);
    }

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
                // During prelisten, suppress error message storage -- prelisten
                // failure is handled by PrelistenStateEvent, not StatusChangedEvent.
                // This prevents "Connection failed" from flashing in the connection bar
                // when the user is just previewing rooms.
                if (!processorRef.prelisten_mode.load(std::memory_order_relaxed))
                {
                    if (!e.error_msg.empty())
                        processorRef.lastErrorMsg = juce::String(e.error_msg);
                }
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
            else if constexpr (std::is_same_v<T, jamwide::BpmChangedEvent>)
            {
                beatBar.triggerFlash();
                // D-01, D-02: Forward BPM change to VideoCompanion for buffer delay update.
                // Source is session state change (NJClient event), not editor visibility,
                // so this works for hidden/minimized plugin and standalone mode.
                if (processorRef.videoCompanion && processorRef.videoCompanion->isActive())
                {
                    float bpm = e.newBpm;
                    int bpi = processorRef.uiSnapshot.bpi.load(std::memory_order_relaxed);
                    processorRef.videoCompanion->broadcastBufferDelay(bpm, bpi);
                }
            }
            else if constexpr (std::is_same_v<T, jamwide::BpiChangedEvent>)
            {
                beatBar.triggerFlash();
                // D-01, D-02: Forward BPI change to VideoCompanion for buffer delay update.
                if (processorRef.videoCompanion && processorRef.videoCompanion->isActive())
                {
                    float bpm = processorRef.uiSnapshot.bpm.load(std::memory_order_relaxed);
                    int bpi = e.newBpi;
                    processorRef.videoCompanion->broadcastBufferDelay(bpm, bpi);
                }
            }
            else if constexpr (std::is_same_v<T, jamwide::SyncStateChangedEvent>)
            {
                connectionBar.updateSyncState(e.newState);
                // Reason is available in e.reason for future UI feedback
                // (e.g., toast notification on ServerBpmChanged)
            }
            else if constexpr (std::is_same_v<T, jamwide::PrelistenStateEvent>)
            {
                serverBrowser.setPrelistenState(e.status, e.host, e.port);
                // On connected, sync initial volume from browser to processor
                if (e.status == jamwide::PrelistenStatus::Connected)
                {
                    processorRef.prelisten_volume.store(
                        serverBrowser.prelistenVolume, std::memory_order_relaxed);
                }
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
    int numUsers = processorRef.userCount.load(std::memory_order_relaxed);

    // Suppress connection bar update during prelisten (Research Gray Area 6).
    // Without this, the bar shows green dot / "Connecting..." for preview connections,
    // which is misleading -- the user is not in a session.
    if (processorRef.prelisten_mode.load(std::memory_order_relaxed))
    {
        // Show disconnected state in connection bar during prelisten
        connectionBar.updateStatus(NJClient::NJC_STATUS_DISCONNECTED, 0);
    }
    else
    {
        connectionBar.updateStatus(status, numUsers);
    }

    // REVIEW FIX: Use member variable prevPollStatus_ instead of static int lastStatus.
    // This prevents state leaking across editor reconstructions.
    if (status != prevPollStatus_)
    {
        // Skip UI state transitions during prelisten -- mixer and chat should
        // not react to preview connections (Research Gray Area 6)
        if (!processorRef.prelisten_mode.load(std::memory_order_relaxed))
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
        }
        prevPollStatus_ = status;
    }
}

void JamWideJuceEditor::showServerBrowser()
{
    serverBrowser.setLoading();
    serverBrowser.setBounds(getLocalBounds());
    serverBrowser.show();

    // Disable Listen buttons when in a non-prelisten session
    bool inSession = (processorRef.getClient()->cached_status.load(std::memory_order_acquire)
                      == NJClient::NJC_STATUS_OK)
                  && !processorRef.prelisten_mode.load(std::memory_order_relaxed);
    serverBrowser.setListenEnabled(!inSession);

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
    // Snapshot under the lock so refreshFromUsers iterates a stable copy.
    std::vector<NJClient::RemoteUserInfo> usersCopy;
    {
        std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
        usersCopy = processorRef.cachedUsers;
    }
    channelStripArea.refreshFromUsers(usersCopy);

    // Highlight Fit button red when strips overflow the viewport
    int chatW = chatSidebarVisible ? kChatPanelWidth : 0;
    int available = getWidth() - chatW - kChatToggleWidth - 4;
    connectionBar.setFitHighlight(channelStripArea.getDesiredWidth() > available);
}

void JamWideJuceEditor::toggleChatSidebar()
{
    chatSidebarVisible = !chatSidebarVisible;
    processorRef.chatSidebarVisible = chatSidebarVisible;  // Persist to processor
    chatToggleButton.pointsRight = chatSidebarVisible;
    chatToggleButton.repaint();
    chatPanel.setVisible(chatSidebarVisible);
    resized();
}

void JamWideJuceEditor::toggleSessionInfoStrip()
{
    infoStripVisible = !infoStripVisible;
    sessionInfoStrip.setVisible(infoStripVisible);
    processorRef.infoStripVisible = infoStripVisible;
    resized();
}

int JamWideJuceEditor::getCurrentSyncState() const
{
    return processorRef.syncState_.load(std::memory_order_relaxed);
}

void JamWideJuceEditor::applyScale(float factor)
{
    // setTransform scales rendering; JUCE communicates physical size to host.
    // Do NOT also call setSize with scaled dims -- that causes double scaling.
    setTransform(juce::AffineTransform::scale(factor));
}
