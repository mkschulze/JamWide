#include "JamWideJuceEditor.h"
#include "core/njclient.h"
#include "threading/ui_event.h"
#include "threading/ui_command.h"
#include "video/BrowserDetect.h"
#include "video/VideoCompanion.h"
#include "video/native/CameraAuthorization.h"
#include "video/native/CameraPreviewWindow.h"
#include "video/native/NativeCameraPrivacyDialog.h"
#include "video/native/CameraStatusDialog.h"
#include "ui/BotFilter.h"   // Phase 22-02 codex H1 — jamwide::isBot for D-05 latch
#include "video/distributor/JamWideRemoteFrameDistributor.h"
#include "video/distributor/PeerVideoSink.h"

#include <chrono>
#include <mutex>
#include <variant>

// Phase 19-02 — file-scope helper for log messages. The state machine doesn't
// expose a public string-conversion helper today; keeping this inline avoids
// touching JamWideCameraDevice.cpp from this plan.
static juce::String stateToString(jamwide::CameraState s) {
    switch (s) {
        case jamwide::CameraState::Idle:        return "Idle";
        case jamwide::CameraState::Opening:     return "Opening";
        case jamwide::CameraState::Capturing:   return "Capturing";
        case jamwide::CameraState::Failed:      return "Failed";
        case jamwide::CameraState::Retrying:    return "Retrying";
        case jamwide::CameraState::Unavailable: return "Unavailable";
    }
    return "Unknown";
}

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

    // Legacy VDO.Ninja video privacy dialog + click handler were removed in
    // 2026-05 alongside the Video button. The dialog field is kept for now
    // (its hidden child + setBounds + isVisible() guard are harmless) but
    // there is no longer any UI path that activates VDO.Ninja video; the
    // Camera button (Phase 19) replaces that flow.

    // 2026-05-03 tx-silent-and-orphan-cutoff: debug snapshot button writes
    // a timestamped log file with the full diagnostic report. Less intrusive
    // alternative to the /rcmstats chat command. Confirms via a System chat
    // message naming the saved file path so the user can find / share it.
    connectionBar.onDebugSnapshotClicked = [this]() {
        const juce::File saved = processorRef.writeDebugSnapshot();
        ChatMessage msg;
        msg.type = ChatMessageType::System;
        msg.timestamp = ""; // not relevant for status messages
        if (saved.existsAsFile()) {
            msg.content = ("debug snapshot saved: " + saved.getFullPathName()).toStdString();
        } else {
            msg.content = "debug snapshot failed (could not create log file)";
        }
        chatPanel.addMessage(msg);
    };

    // Phase 19-02 — Camera button wiring (MEDIUM-1 decision tree).
    connectionBar.onCameraClicked = [this]() {
        auto* cam = processorRef.getNativeCamera();
        if (! cam) return;

        // Phase 19-03 Task 2 (D-27) — VDO.Ninja coexistence soft warning.
        // Fires at MOST ONCE per editor lifetime (atomic exchange guarantees
        // even with parallel timer callbacks the toast is shown once). The
        // toast is INFORMATIONAL — we fall through to the state-switch so
        // the camera toggle proceeds in parallel (D-27 "User can ignore and
        // proceed").
        if (processorRef.videoCompanion
            && processorRef.videoCompanion->isActive()
            && ! coexistenceToastShown_.exchange(true)) {
            auto opts = juce::MessageBoxOptions{}
                .withIconType(juce::MessageBoxIconType::NoIcon)
                .withTitle("Multiple video stacks active")
                .withMessage("VDO.Ninja video is also active. Bandwidth and "
                             "CPU may be high - consider stopping one for "
                             "better quality.")
                .withButton("OK");
            juce::AlertWindow::showAsync(opts, [](int){});
            // Fall through — D-27 says the toast does NOT block the toggle.
        }

        const auto state = cam->getState();

        switch (state) {
            case jamwide::CameraState::Capturing: {
                // Popout open → toggle stops capture; popout hidden → re-show.
                if (previewWindow_ && ! previewWindow_->isVisible()) {
                    previewWindow_->setVisible(true);
                } else {
                    cam->toggle();
                }
                return;
            }
            case jamwide::CameraState::Idle: {
                // HIGH-5 first-launch sequence (Task 3 Edit 5 fills the body).
                handleCameraIdleClick();
                return;
            }
            case jamwide::CameraState::Unavailable: {
                cam->recheckPermission();   // D-12
                return;
            }
            case jamwide::CameraState::Opening:
            case jamwide::CameraState::Retrying:
            case jamwide::CameraState::Failed:
                juce::Logger::writeToLog(
                    "[JamWideEditor] Camera click ignored (state="
                    + stateToString(state) + ")");
                return;
        }
    };

    // Right-click "Stop Camera" — unconditional toggle while Capturing.
    connectionBar.onCameraStopRequested = [this]() {
        if (auto* cam = processorRef.getNativeCamera()) {
            if (cam->getState() == jamwide::CameraState::Capturing)
                cam->toggle();
        }
    };

    // Quality preset selection (right-click menu). Persist + reflect in menu.
    connectionBar.onCameraQualitySelected = [this](int preset) {
        if (auto* cam = processorRef.getNativeCamera()) {
            cam->setQualityPreset(preset);
            processorRef.setCameraQualityPreset(preset);
            connectionBar.setCameraQualityPreset(preset);
        }
    };

    // Phase 20-03 — Broadcast toggle wiring. setBroadcastVideo enforces the
    // T-20-03 lifecycle ordering (SetVideoBroadcastActive(false) BEFORE
    // encoder.close()) and the R4 M11 END-on-broadcast-off path 1.
    connectionBar.onBroadcastToggleRequested = [this]() {
        processorRef.setBroadcastVideo(!processorRef.isBroadcastingVideo());
        connectionBar.setCameraIsBroadcasting(processorRef.isBroadcastingVideo());
    };

    // Register the editor as the FallbackListener (processor outlives editor;
    // the destructor unregisters before the editor goes away).
    if (auto* cam = processorRef.getNativeCamera()) {
        cam->setFallbackListener(this);
        // Reflect the current preset in the right-click menu's checkmark.
        connectionBar.setCameraQualityPreset(cam->getQualityPreset());
    }

    // Phase 19-02 Task 2 — construct the camera popout window. Initial bounds
    // come from the processor (restored from the persisted v4 ValueTree by
    // Task 3's setStateInformation; defaults to (100,100,320,240) on first
    // launch). The window starts hidden; onCameraStateChanged(Capturing)
    // makes it visible via drivePreviewWindowVisibility.
    if (auto* dist = processorRef.getFrameDistributor()) {
        previewWindow_ = std::make_unique<jamwide::CameraPreviewWindow>(
            *dist, &lookAndFeel, processorRef.getCameraPopoutBounds());

        previewWindow_->onCloseRequested = [this]() {
            // D-09 — close hides; capture stays on. No editor-side action.
            juce::ignoreUnused(this);
        };
        previewWindow_->onBoundsChanged = [this](juce::Rectangle<int> r) {
            // D-25 — persist popout bounds so they survive plugin reload.
            processorRef.setCameraPopoutBounds(r);
        };
    }

    // Phase 22-02 — construct the in-main-view VideoGridBand. Required
    // inputs: BOTH the self-camera distributor (Phase 19) AND the remote
    // frame distributor (Phase 21). If either is unavailable the band is
    // not constructed (gridBand_ stays null) and the GridButton toggle
    // simply no-ops. M7 codex closure — explicitly pass Mode::MainBand
    // at construction.
    if (auto* selfDist = processorRef.getFrameDistributor()) {
        if (auto* remoteDist = processorRef.getRemoteFrameDistributor()) {
            gridBand_ = std::make_unique<jamwide::VideoGridBand>(
                processorRef,
                connectionBar,
                selfDist,
                remoteDist,
                jamwide::VideoGridBand::Mode::MainBand);
            gridBand_->onCloseRequested = [this]() {
                toggleGridBand(false);
            };
            gridBand_->onHeightChangeRequested = [this](int h) {
                gridBandHeight_ = juce::jlimit(140, 800, h);
                resized();
            };
            // Plan 22-03 Task 2 — wire detach + popout + placeholder bring-back.
            gridBand_->onDetachRequested = [this]() {
                openOrToggleDetachedGrid();
            };
            // codex M5 — typed VideoPopoutTarget callback (no magic string).
            gridBand_->onPeerPopoutRequested = [this](jamwide::VideoPopoutTarget t) {
                openOrToggleRemotePopout(std::move(t));
            };
            gridBand_->onPlaceholderBringBack = [this](const juce::String& username) {
                if (username.isEmpty())
                    reattachGrid();
                else
                    bringBackRemotePopout(username);
            };
            addChildComponent(*gridBand_);   // starts hidden (D-14 mirror)
        }
    }

    // Wire the new ConnectionBar Grid button to toggle the band. Pure UI;
    // DISP-04 — toggleGridBand() does not call any NJClient API.
    connectionBar.onGridToggleClicked = [this]() {
        toggleGridBand(! gridBandVisible_);
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
    // Plan 22-03 Task 2 / RESEARCH Pitfall 3 — popouts (RemotePeerPopoutWindow
    // + DetachedGridWindow) hold a raw LookAndFeel pointer. Their destructors
    // detach it before returning, but the editor's lookAndFeel member must
    // still be alive at that moment. The grid band similarly holds children
    // that reference the editor's look-and-feel.
    //
    // Pitfall 3 POSITIONAL ordering invariant — clear all three BEFORE the
    // existing Phase 19 teardown body. The final lookAndFeel detach call
    // below MUST be the last line of this destructor.
    //
    // std::unordered_map::clear (H2 NARROWED). The map's value-type
    // ~RemotePeerPopoutWindow runs for each entry; Phase 21 D-06's listener
    // teardown (in RemotePeerTile dtor, via the inner subscription_) waits
    // for in-flight handleAsyncUpdate per the HIGH-2 mirror.
    remotePopouts_.clear();
    detachedGrid_.reset();
    gridBand_.reset();

    // ── existing Phase 19 dtor body preserved verbatim below ──────────────
    removeMouseListener(this);
    stopTimer();
    // Phase 19-02 — unregister as FallbackListener BEFORE the editor goes
    // away. The processor (and its JamWideCameraDevice) outlives the editor,
    // so leaving a dangling listener pointer would crash on the next state
    // change. Task 2 also tears down previewWindow_ here (handled by
    // unique_ptr destruction); no explicit reset needed.
    if (auto* cam = processorRef.getNativeCamera()) {
        cam->setFallbackListener(nullptr);
    }
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
            && dynamic_cast<VbFader*>(e.eventComponent) == nullptr
            && dynamic_cast<BeatBar*>(e.eventComponent) == nullptr)
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

    // Phase 22-02 — VideoGridBand inserted between sessionInfoStrip and
    // channelStripArea. Width follows the mixer column (i.e. AFTER chat panel
    // is removed from the right — W2 checker decision, iter-1). Height is
    // gridBandHeight_ (default 280 px, persisted by Plan 22-04).
    if (gridBand_ && gridBandVisible_) {
        gridBand_->setBounds(area.removeFromTop(gridBandHeight_));
    }

    // Channel strip area fills remaining center
    channelStripArea.setBounds(area);

    // Overlays: full editor bounds
    serverBrowser.setBounds(getLocalBounds());
    licenseDialog.setBounds(getLocalBounds());
    videoPrivacyDialog.setBounds(getLocalBounds());
}

// Phase 22-02 — pure UI toggle (DISP-04). No NJClient call. Audio session
// continues uninterrupted across the toggle. Callable from:
//   - GridButton's onClick → ConnectionBar::onGridToggleClicked lambda
//   - D-05 auto-open latch in timerCallback (lock-release-then-sync)
//   - VideoGridBand's onCloseRequested callback (header × icon)
void JamWideJuceEditor::toggleGridBand(bool visible)
{
    if (! gridBand_) return;
    gridBandVisible_ = visible;
    gridBand_->setVisible(visible);
    connectionBar.setGridVisible(visible);
    resized();
}

// ─── Plan 22-03 Task 2 — popout + detached-grid controllers ────────────────
// codex M5 — typed VideoPopoutTarget dispatch (NO magic-string sentinel).
// codex H3 — explicit 4-state truth table for tile ↗ semantics.
// codex M7 — sync setPeerPoppedOut on BOTH gridBand_ AND detachedGrid_->
//            getGridBand() (when non-null) so placeholder state stays
//            consistent across surfaces.

void JamWideJuceEditor::openOrToggleRemotePopout(jamwide::VideoPopoutTarget t)
{
    // codex M5 closure — typed-target dispatch. Switching on `kind` is the
    // single source of truth; NO magic-string comparison anywhere in this
    // function. A peer whose NINJAM username happens to collide with any
    // literal cannot spoof the Self branch.
    if (t.kind == jamwide::VideoPopoutTargetKind::Self)
    {
        // D-09 — self-tile popout reuses Phase 19's CameraPreviewWindow.
        // No second self-popout window is ever created (that would
        // double-subscribe to the camera and violate the Phase 19 subscriber
        // MEMBER-ORDER assumption).
        drivePreviewWindowVisibility(jamwide::CameraState::Capturing);
        return;
    }

    // RemotePeer branch — codex H3 4-state truth table.
    const juce::String& username = t.username;
    if (username.isEmpty()) return;   // defensive — typed-target invariant

    auto it = remotePopouts_.find(username);   // std::unordered_map::find (H2 NARROWED)
    if (it != remotePopouts_.end())
    {
        auto& popout = it->second;
        if (popout)
        {
            if (popout->isVisible())
            {
                // State (B) — visible → HIDE. Placeholder stays mounted
                // because poppedOutPeers_.contains(username) is true. The
                // window's own × also lands here.
                popout->setVisible(false);
            }
            else
            {
                // State (C) — hidden → RE-SHOW. codex H3 disambiguation:
                // hitting ↗ on a tile whose popout is hidden brings it back
                // to visible WITHOUT destroying. Bounds + the underlying
                // RemotePeerTile + Subscription are preserved.
                // The destroy path is EXCLUSIVELY via placeholder click
                // (bringBackRemotePopout below).
                popout->setVisible(true);
            }
            // Placeholder remains in either case — band rendering rule is
            // poppedOutPeers_.contains(username) → show placeholder,
            // independent of popout's visible state.
            return;
        }
        // Fall through to state (A)/(D) construction if the slot is null
        // (which can happen if a prior reset() left an empty unique_ptr
        // without erasing the key — defensive).
    }

    // State (A) or (D) — popout absent → CREATE+SHOW.
    if (auto* dist = processorRef.getRemoteFrameDistributor())
    {
        auto popout = std::make_unique<jamwide::RemotePeerPopoutWindow>(
            *dist, username, &lookAndFeel, getInitialPopoutBounds(username));

        // onCloseRequested fires AFTER closeButtonPressed has already done
        // setVisible(false) → state B→C transition. The placeholder stays
        // mounted (poppedOutPeers_ membership unchanged). The user can
        // re-show via tile ↗ (state C→B) OR destroy via placeholder click
        // (state C→D — bringBackRemotePopout).
        const juce::String capturedUsername = username;
        popout->onCloseRequested = [capturedUsername]() {
            juce::ignoreUnused(capturedUsername);
        };
        popout->onBoundsChanged = [](juce::Rectangle<int> r) {
            // Plan 22-04 swaps this for processorRef.setRemotePopoutBounds.
            juce::ignoreUnused(r);
        };
        popout->setVisible(true);
        remotePopouts_.emplace(username, std::move(popout));   // std::unordered_map::emplace (H2 NARROWED — accepts move-only value)

        // codex M7 dual-band sync — drive setPeerPoppedOut on BOTH the main
        // band AND the detached band's inner band (when the detached grid
        // is open). Without this, popping out a peer while the detached
        // grid is open would only update the main band's placeholder,
        // leaving the detached band showing the live tile (broken UX).
        if (gridBand_) gridBand_->setPeerPoppedOut(username, true);
        if (detachedGrid_ && detachedGrid_->getGridBand())
            detachedGrid_->getGridBand()->setPeerPoppedOut(username, true);
    }
}

void JamWideJuceEditor::bringBackRemotePopout(const juce::String& username)
{
    // codex H3 — bring-back is the EXCLUSIVE destroy path. After this, the
    // popout is in state (D); next ↗ click creates a fresh one at the
    // last-persisted bounds (Plan 22-04 wires bounds persistence).

    // codex M7 dual-band sync — clear placeholder on BOTH bands before the
    // destroy so both surfaces rebind the live tile simultaneously.
    if (gridBand_) gridBand_->setPeerPoppedOut(username, false);
    if (detachedGrid_ && detachedGrid_->getGridBand())
        detachedGrid_->getGridBand()->setPeerPoppedOut(username, false);

    // std::unordered_map erase (H2 NARROWED — was juce::HashMap::remove in iter-1).
    auto it = remotePopouts_.find(username);
    if (it != remotePopouts_.end())
    {
        it->second.reset();    // Subscription dtor blocks for in-flight (Phase 21 D-06).
        remotePopouts_.erase(it);
    }
}

void JamWideJuceEditor::openOrToggleDetachedGrid()
{
    if (detachedGrid_)
    {
        // Already open — toggle = bring back (destroy).
        reattachGrid();
        return;
    }
    auto* selfDist = processorRef.getFrameDistributor();
    auto* remoteDist = processorRef.getRemoteFrameDistributor();
    if (selfDist && remoteDist)
    {
        detachedGrid_ = std::make_unique<jamwide::DetachedGridWindow>(
            processorRef, connectionBar, selfDist, remoteDist, &lookAndFeel,
            getInitialDetachedGridBounds());

        detachedGrid_->onCloseRequested = [this]() {
            // closeButtonPressed already setVisible(false). Editor's policy
            // per D-18 default is FULL DESTROY on close.
            reattachGrid();
        };
        detachedGrid_->onBoundsChanged = [](juce::Rectangle<int>) {
            // Plan 22-04 swap.
        };

        // codex M7 closure — replay existing popout state into the new
        // detached band so it starts up consistent with the main band.
        // Without this, opening the detached grid while popouts are already
        // open would leave the detached band showing live tiles for
        // popped-out peers (broken UX).
        if (auto* detachedBand = detachedGrid_->getGridBand())
        {
            // H2 NARROWED — C++17 structured-binding range-for over the
            // std::unordered_map; the original iter-1 plan called for a
            // HashMap iterator but H2-NARROWED proved std::unordered_map
            // compiles cleanly for the move-only unique_ptr value type.
            for (auto const& [username, window] : remotePopouts_)
            {
                if (window)
                    detachedBand->setPeerPoppedOut(username, true);
            }
        }

        detachedGrid_->setVisible(true);
        if (gridBand_) gridBand_->setDetachedActive(true);
    }
}

void JamWideJuceEditor::reattachGrid()
{
    if (gridBand_) gridBand_->setDetachedActive(false);
    detachedGrid_.reset();
}

juce::Rectangle<int> JamWideJuceEditor::getInitialPopoutBounds(const juce::String& /*username*/) const
{
    // Plan 22-03 stub — Plan 22-04 swaps for
    // processorRef.getRemotePopoutBounds(username). The default is
    // intentionally NOT display-aware here; RemotePeerPopoutWindow's
    // constructor performs the Desktop::getDisplays() clamp at window-open
    // time (T-22-MM mitigation).
    return juce::Rectangle<int>{100, 100, 320, 240};
}

juce::Rectangle<int> JamWideJuceEditor::getInitialDetachedGridBounds() const
{
    return juce::Rectangle<int>{200, 200, 800, 450};
}

void JamWideJuceEditor::timerCallback()
{
    // === Profiling (cpu-spikes-beta12-regression): RAII wrap of the entire
    //   message-thread timer callback. Records on every exit path. See
    //   .planning/debug/cpu-spikes-beta12-regression.md.
    struct ProfTimerCb {
        std::chrono::steady_clock::time_point t0;
        ProfTimerCb() : t0(std::chrono::steady_clock::now()) {}
        ~ProfTimerCb() {
            auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0).count();
            NJClient::ProfilingRecordTimerCallback(static_cast<uint64_t>(dt));
        }
    } _prof_timer_cb;

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
    // === ABTEST 2 (cpu-spikes-beta12-regression) — heartbeat broadcast stubbed.
    //   Hypothesis: ~1.5 Hz JSON build + wsMutex_ acquisition + WebSocket send
    //   is the source of the beta-12-onward baseline-CPU bump that users have
    //   reported. Diagnosis build only. To restore: uncomment the if-block.
    /*
    if (processorRef.videoCompanion && processorRef.videoCompanion->isActive())
    {
        int intervalCount = processorRef.uiSnapshot.interval_count.load(std::memory_order_relaxed);
        processorRef.videoCompanion->broadcastBeatHeartbeat(beat, bpi, intervalCount);
    }
    */
    // === END ABTEST 2

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

    // Phase 22 D-05 — auto-open the band on the first observed peer frame.
    // Codex review M4 — lock-release-then-sync. The timer is already on the
    // message thread, so async-dispatching this toggle would be both
    // unnecessary AND unsafe: a queued lambda captures raw `this`, and if
    // the editor is destroyed before the async callback runs, the resumed
    // toggleGridBand() segfaults. Releasing the cachedUsersMutex BEFORE
    // calling toggleGridBand also avoids any callback re-entering the mutex.
    bool shouldOpen = false;
    if (! gridAutoOpenLatchFired_.load(std::memory_order_acquire))
    {
        if (auto* dist = processorRef.getRemoteFrameDistributor())
        {
            std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
            for (const auto& u : processorRef.cachedUsers)
            {
                if (jamwide::isBot(juce::String(u.name))) continue;   // H1: namespaced
                if (auto* sink = dist->findSink(u.name, /*chidx*/ 1))
                {
                    if (sink->first_frame_seen.load(std::memory_order_acquire))
                    {
                        if (! gridAutoOpenLatchFired_.exchange(true))
                            shouldOpen = true;
                        break;
                    }
                }
            }
            // mutex released here as `lk` falls out of scope
        }
    }
    if (shouldOpen)
        toggleGridBand(true);    // SYNCHRONOUS — no callAsync, no UAF (M4)
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

// ─── Phase 19-02 — FallbackListener implementation ──────────────────────────
//
// onCameraStateChanged drives the Camera button's text + active-state colour
// AND the popout window's visibility. Task 1 lands the button-state portion;
// Task 2 extends this with the previewWindow_ visibility branch.
//
// onCameraFallback is intentionally a stub in this plan — 19-03 Task 1 wires
// CameraStatusDialog here to show the fallback copy when permission is denied
// or the device is unavailable.

void JamWideJuceEditor::onCameraStateChanged(jamwide::CameraState newState)
{
    switch (newState) {
        case jamwide::CameraState::Unavailable:
            connectionBar.setCameraLabel("Recheck permission");
            connectionBar.setCameraActive(false);
            break;
        case jamwide::CameraState::Capturing:
            connectionBar.setCameraLabel("Camera");
            connectionBar.setCameraActive(true);
            // 19-03 — once we have a successful capture, clear the fallback
            // dialog's suppression so the next denial re-shows the dialog
            // (cause-change re-show, D-14).
            cameraStatusDialog_.reset();
            break;
        case jamwide::CameraState::Idle:
            connectionBar.setCameraLabel("Camera");
            connectionBar.setCameraActive(false);
            // Phase 20-03 — clear the Broadcast UI mirror so the popup label
            // returns to "Start Broadcast" on the next open.
            connectionBar.setCameraIsBroadcasting(false);
            break;
        case jamwide::CameraState::Opening:
            // 19-03 — state machine left Unavailable; clear suppression so a
            // subsequent denial (e.g. open fails differently) re-shows.
            cameraStatusDialog_.reset();
            break;
        default:
            // Retrying / Failed: keep current label until a stable
            // state is reached. Avoids label-thrash during retry storms.
            break;
    }

    // Phase 20-03 — forward to the processor so it can drive the encoder
    // lifecycle (D-13: construct Openh264Encoder on Capturing, destroy on
    // Idle/Failed/Unavailable). Encoder thread does NOT start here — that
    // happens in setBroadcastVideo(true).
    processorRef.onCameraStateChangedFromEditor(newState);

    // Task 2 extends this method with the previewWindow_ visibility branch
    // (D-09 orthogonality: only Idle / Capturing / Unavailable touch the
    // window; Opening / Retrying / Failed leave visibility as-is).
    drivePreviewWindowVisibility(newState);
}

// 19-03 Task 1 — cause-aware fallback dialog (D-13..D-16, T-19-01).
//
// The dialog is owned by the editor (cameraStatusDialog_ member). Its show()
// is suppression-aware: same cause back-to-back returns Dismiss immediately.
// onCameraStateChanged resets the suppression on transitions out of
// Unavailable (Opening/Capturing) so the next denial re-shows.
//
// The Action returned by the dialog dispatches per Codex HIGH-7:
//
//   OpenSystemSettings -> platform-conditional deep-link (macOS x-apple URL,
//                         Windows ms-settings URL). The OS opens its privacy
//                         pane; the user can flip the switch back, then click
//                         Recheck on the dialog (which we re-show via the
//                         FallbackListener next time the state machine emits
//                         a fallback).
//
//   RecheckPermission  -> nativeCamera->recheckPermission() — D-12 explicit
//                         re-check. The state machine routes back through
//                         the auth path.
//
//   Dismiss            -> close + focus the Camera button so the user can
//                         try again later via keyboard nav.
void JamWideJuceEditor::onCameraFallback(jamwide::CameraFallbackCause cause)
{
    const juce::String hostName = juce::PluginHostType().getHostDescription();
    cameraStatusDialog_.show(cause, hostName,
        [this](jamwide::CameraStatusDialog::Action action) {
            auto* cam = processorRef.getNativeCamera();
            switch (action) {
                case jamwide::CameraStatusDialog::Action::OpenSystemSettings: {
                #if JUCE_MAC
                    // NOLINTNEXTLINE(misc-line-length): single-line URL keeps
                    // the verify-grep literal in one match (plan §<verification>).
                    juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser();
                #elif JUCE_WINDOWS
                    juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser();
                #else
                    // Linux / other — no-op (Phase 19 ships macOS + Windows only).
                #endif
                    return;
                }
                case jamwide::CameraStatusDialog::Action::RecheckPermission: {
                    // 2026-05-16 UX fix (Phase 19 UAT Cell 3 gap): reset the
                    // dialog's last-shown-cause cache before issuing the
                    // recheck. Without this, D-14 duplicate-suppression in
                    // CameraStatusDialog::show silently swallows the re-show
                    // when the status is still unchanged (cause stays equal),
                    // firing onResult(Dismiss) with no visible feedback —
                    // making the button appear broken to the user. Resetting
                    // here lets the state-machine-emitted fallback (if still
                    // denied after recheck) re-display the same dialog and
                    // confirm the unchanged state. When status DID flip to
                    // Authorized, no fallback is emitted and the camera
                    // opens normally — reset is a no-op for that path.
                    cameraStatusDialog_.reset();
                    if (cam) cam->recheckPermission();   // D-12
                    return;
                }
                case jamwide::CameraStatusDialog::Action::Dismiss: {
                    // Focus the Camera button so the user can re-trigger via
                    // keyboard. Cheap UX cue; no functional dependency.
                    connectionBar.grabKeyboardFocus();
                    return;
                }
            }
        });
}

// HIGH-5 first-launch sequence — switches on the CURRENT auth status and
// runs the correct request-access → privacy-modal → toggle path. The prior
// design only checked "is Authorized" at click time and missed the real
// first-launch case (NotDetermined → request → grant → modal). This version
// dispatches per-status so the modal fires on the actual first-launch path.
//
// The flow per-status:
//   Authorized      → showPrivacyOrToggle (modal if !privacyAck, else toggle)
//   NotDetermined   → requestCameraAuthorization → on-grant: showPrivacyOrToggle;
//                                                  on-deny:  toggle (→ Unavailable)
//   Denied/Restricted → toggle (state machine routes to Unavailable; 19-03
//                       surfaces CameraStatusDialog via FallbackListener)
//   NotApplicable (Windows) → showPrivacyOrToggle (no TCC on Windows desktop;
//                              still gate on privacyAck for the D-22 modal)
void JamWideJuceEditor::handleCameraIdleClick()
{
    auto* cam = processorRef.getNativeCamera();
    if (! cam) return;

    const auto status = jamwide::queryCameraAuthorization();
    switch (status) {
        case jamwide::CameraAuthStatus::Authorized: {
            showPrivacyOrToggle(cam);
            return;
        }
        case jamwide::CameraAuthStatus::NotDetermined: {
            // OS prompt path. Apple's contract: completion handler fires on
            // an unspecified thread, so we marshal to the message thread
            // before touching `this` or the camera.
            jamwide::requestCameraAuthorization(
                [this](jamwide::CameraAuthStatus result) {
                    juce::MessageManager::callAsync(
                        [this, result]() {
                            auto* cam2 = processorRef.getNativeCamera();
                            if (! cam2) return;
                            if (result == jamwide::CameraAuthStatus::Authorized) {
                                showPrivacyOrToggle(cam2);
                            } else {
                                // Denied / Restricted — let the state machine
                                // route to Unavailable. 19-03 surfaces the
                                // CameraStatusDialog via FallbackListener.
                                cam2->toggle();
                            }
                        });
                });
            return;
        }
        case jamwide::CameraAuthStatus::Denied:
        case jamwide::CameraAuthStatus::Restricted: {
            cam->toggle();   // → Unavailable; 19-03 surfaces the dialog
            return;
        }
        case jamwide::CameraAuthStatus::NotApplicable: {
            // Windows — no TCC pre-check. Still gate on privacyAck so the
            // first-launch D-22 modal fires here too.
            showPrivacyOrToggle(cam);
            return;
        }
    }
}

void JamWideJuceEditor::showPrivacyOrToggle(jamwide::JamWideCameraDevice* cam)
{
    if (! cam) return;

    if (! processorRef.getCameraPrivacyAck()) {
        if (! privacyDialog_) {
            privacyDialog_ = std::make_unique<jamwide::NativeCameraPrivacyDialog>();
        }
        privacyDialog_->show(
            [this, cam](bool acknowledged) {
                if (acknowledged) {
                    processorRef.setCameraPrivacyAck(true);
                    // proceed to openDeviceAsync via the state machine
                    cam->toggle();
                }
                // else: user cancelled; camera stays Idle. No state change.
            });
    } else {
        // Ack already given on a prior session — proceed directly.
        cam->toggle();
    }
}

// Task 2 hook — see header. Task 1 ships an empty stub; Task 2 replaces it
// with the Capturing→setVisible(true) / Idle/Unavailable→setVisible(false)
// branch. Kept as a separate method so the FallbackListener path doesn't
// need to know about the popout's lifecycle in Task 1.
void JamWideJuceEditor::drivePreviewWindowVisibility(jamwide::CameraState newState)
{
    if (! previewWindow_) return;
    if (newState == jamwide::CameraState::Capturing) {
        if (auto* cam = processorRef.getNativeCamera())
            previewWindow_->setDeviceName(cam->getDeviceName());
        previewWindow_->setVisible(true);
    } else if (newState == jamwide::CameraState::Idle
            || newState == jamwide::CameraState::Unavailable) {
        previewWindow_->setVisible(false);
    }
    // Opening / Retrying / Failed: leave visibility as-is (D-09).
}
