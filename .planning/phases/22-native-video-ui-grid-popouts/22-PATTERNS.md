# Phase 22: Native Video UI (Grid + Popouts) — Pattern Map

**Mapped:** 2026-05-17
**Files analyzed:** 17 new + 3 modified
**Analogs found:** 17 / 17 (all new files have a direct analog; pure-utility layout helper has no analog by design)

This phase is an unusually clean port of Phase 19's `CameraPreviewWindow`/`CameraPreviewTile` template. Phase 21 ships the `JamWideRemoteFrameDistributor::Subscription` RAII surface; Phase 22 is the consumer. Every pattern is already proven in the codebase — the planner's job is to mirror, not invent.

---

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `juce/ui/video/VideoGridBand.h/.cpp` (NEW) | component | event-driven (Timer poll + repaint) | `juce/ui/ChannelStripArea.{h,cpp}` | exact (hand-rolled `resized()` + 30 Hz Timer poll roster) |
| `juce/ui/video/VideoTileBase.h/.cpp` (NEW) | component | event-driven (paint shared base) | `juce/video/native/CameraPreviewTile.{h,cpp}` (chrome subset) | role-match (no chrome layer exists today; tile chrome is fresh code) |
| `juce/ui/video/SelfVideoTile.h/.cpp` (NEW) | component | event-driven (callback Subscriber) | `juce/video/native/CameraPreviewTile.{h,cpp}` | **exact** (same Phase 19 distributor, same MEMBER-ORDER CONTRACT) |
| `juce/ui/video/RemotePeerTile.h/.cpp` (NEW) | component | event-driven (RAII Subscription) | `juce/video/native/CameraPreviewTile.{h,cpp}` + Phase 21 `subscribeToPeer` | exact (mirror of camera tile, swap callback for RAII Subscription handle) |
| `juce/ui/video/RemotePeerPopoutWindow.h/.cpp` (NEW) | component (window) | event-driven (bounds → editor) | `juce/video/native/CameraPreviewWindow.{h,cpp}` | **exact** (DocumentWindow + ComponentListener + closeButtonPressed-hides) |
| `juce/ui/video/DetachedGridWindow.h/.cpp` (NEW) | component (window) | event-driven (bounds → editor) | `juce/video/native/CameraPreviewWindow.{h,cpp}` | exact (DocumentWindow, but contains a `VideoGridBand` not a tile, NOT aspect-locked) |
| `juce/ui/video/PopoutPlaceholderCard.h/.cpp` (NEW) | component | event-driven (mouseDown → bring-back) | inline `ChatToggleButton` in `JamWideJuceEditor.h:95-136` | role-match (mouseEnter/mouseExit/mouseDown + custom paint pattern) |
| `juce/ui/video/DetachedGridPlaceholderCard.h/.cpp` (NEW) | component | event-driven (mouseDown → reattach) | same as `PopoutPlaceholderCard` | role-match |
| `juce/ui/video/computeGridLayout.h` (NEW) | utility | pure math (transform) | none — pure header, no analog needed | n/a (greenfield helper) |
| `ConnectionBar::GridButton` (NEW, file-local in `ConnectionBar.cpp`) | component | event-driven (click → editor) | `ConnectionBar::CameraButton` (file-local in `ConnectionBar.cpp:23-71`) | **exact** (file-local `juce::TextButton` subclass, `unique_ptr<>` member in `.h`, label-mirrors-state) |
| `juce/ui/ConnectionBar.{h,cpp}` (MODIFIED) | component | event-driven | self (CameraButton wiring) | n/a — additive |
| `juce/JamWideJuceEditor.{h,cpp}` (MODIFIED) | controller | event-driven (Timer + lazy windows) | self (`previewWindow_` wiring lines 236-253; `resized()` lines 372-405) | n/a — additive |
| `juce/JamWideJuceProcessor.{h,cpp}` (MODIFIED) | model (state persistence) | CRUD (ValueTree XML) | self (`<camera>` flat-property handling lines 968-979 + 1085-1099) | n/a — additive, mirrors v3→v4 bump |

---

## Pattern Assignments

### `juce/ui/video/RemotePeerTile.{h,cpp}` (component, event-driven)

**Analog:** `juce/video/native/CameraPreviewTile.{h,cpp}` (Phase 19-02 Task 2)

**MEMBER-ORDER CONTRACT pattern** (CameraPreviewTile.h:14-19, 50-69):

```cpp
// MEMBER-ORDER CONTRACT: subscription_ MUST be the LAST declared member.
// Members are destroyed in reverse declaration order, so subscription_'s
// dtor runs FIRST, which calls unregisterAndWait — blocking until every
// in-flight onFrame() returns. By the time the mutex and frame members
// are destroyed, no callback can reach them.

class CameraPreviewTile : public juce::Component,
                          public juce::AsyncUpdater,
                          public JamWideFrameDistributor::Subscriber {
    // ...
private:
    JamWideFrameDistributor& distributor_;
    std::mutex pendingMu_;
    juce::Image pendingFrame_;
    std::mutex currentMu_;
    juce::Image currentFrame_;
    // MEMBER-ORDER CONTRACT: subscription_ MUST stay as the LAST member.
    JamWideFrameDistributor::Subscription subscription_;
};
```

**Subscribe-in-ctor pattern** (CameraPreviewTile.cpp:6-15):

```cpp
CameraPreviewTile::CameraPreviewTile(JamWideFrameDistributor& d)
    : distributor_(d)
{
    // Member-order contract — registerSubscriber returns a moveable handle.
    // subscription_ is the LAST declared member so this assignment lands in
    // the last-to-be-destroyed slot; ~Subscription runs FIRST during dtor
    // (reverse declaration order), blocking any in-flight onFrame() before
    // the mutex/frame members go away.
    subscription_ = distributor_.registerSubscriber(this);
}
```

**Async-update + paint pattern** (CameraPreviewTile.cpp:49-79):

```cpp
void CameraPreviewTile::handleAsyncUpdate()
{
    // Message thread.
    juce::Image latest;
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        latest = pendingFrame_;
    }
    if (latest.isValid()) {
        { std::lock_guard<std::mutex> lock(currentMu_); currentFrame_ = latest; }
        repaint();
    }
}

void CameraPreviewTile::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    juce::Image toDraw;
    { std::lock_guard<std::mutex> lock(currentMu_); toDraw = currentFrame_; }
    if (toDraw.isValid()) {
        g.drawImage(toDraw, getLocalBounds().toFloat(),
                    juce::RectanglePlacement::centred);
    }
}
```

**What changes vs analog:**
- **Subscription style:** RAII `JamWideRemoteFrameDistributor::Subscription` (returned from `subscribeToPeer(username, chidx, onRepaint)`), NOT callback-style `Subscriber*` registration. The class therefore does **NOT** inherit from any `Subscriber` interface — it just holds the `Subscription` as the LAST member.
- **Frame source:** Read from `PeerVideoSink::image_front` under `sink->bufferLock` (a `juce::CriticalSection`), NOT from a `pendingFrame_` queued by the camera callback. The `onRepaint` lambda passed to `subscribeToPeer` is `[this]{ triggerAsyncUpdate(); }`; `handleAsyncUpdate()` snapshots `sink->image_front` then `repaint()`s.
- **Atomic status reads:** Phase 22 tiles read `sink->first_frame_seen` / `sink->hold_count` / `sink->synced` lock-free in `paint()` for overlay decisions (Phase 21 D-17/D-19/D-20).
- **MEMBER-ORDER CONTRACT preserved verbatim:** `Subscription subscription_` is still the LAST declared member; destructor blocks in-flight `handleAsyncUpdate` on the message thread before mutexes/frame members tear down.

**Member declaration order (REQUIRED):**
```cpp
private:
    jamwide::JamWideRemoteFrameDistributor& distributor_;
    jamwide::PeerVideoSink* sink_;        // non-owning; resolved at ctor
    juce::String username_;
    int chidx_;
    // ... any other state ...
    // MEMBER-ORDER CONTRACT: subscription_ MUST stay as the LAST member.
    jamwide::JamWideRemoteFrameDistributor::Subscription subscription_;
```

**Note on Phase 22 sink-resolution race:** `subscribeToPeer` already lazily creates a 320×240 sink even before NJClient observes a peer's first BEGIN (see `JamWideRemoteFrameDistributor.cpp:233-254` — `if (it == sinks_.end()) { auto newSink = std::make_unique<PeerVideoSink>(320, 240); ... }`), so the tile can safely cache the `findSink` result inside its `subscribeToPeer` call.

---

### `juce/ui/video/SelfVideoTile.{h,cpp}` (component, event-driven)

**Analog:** `juce/video/native/CameraPreviewTile.{h,cpp}` — **same template, same Phase 19 distributor.**

**Reuses pattern verbatim** — `SelfVideoTile` inherits from `juce::Component + juce::AsyncUpdater + JamWideFrameDistributor::Subscriber`; subscribes via `distributor_.registerSubscriber(this)` in the ctor. The body of `onFrame`/`handleAsyncUpdate`/`paint` is identical to `CameraPreviewTile` (lines 33-79 above).

**What changes vs analog:**
- **Adds the popout-affordance overlay** — `↗` icon hit-test in `mouseDown` calls back into the editor: `editor.showCameraPreviewWindow()` (reopen the existing Phase 19 `previewWindow_` per D-09). NO second self-popout window is created.
- **Adds the username strip + status overlay paint** (D-11) — both delegated to `VideoTileBase`.
- **Visibility gated by self-broadcast atomic** (D-07) — `VideoGridBand::timerCallback` polls `connectionBar.getCameraIsBroadcasting()` and mounts/unmounts the `SelfVideoTile` on edge transitions.

**Member declaration order (REQUIRED):** identical to `CameraPreviewTile` — subscription as LAST member.

---

### `juce/ui/video/RemotePeerPopoutWindow.{h,cpp}` (component window, event-driven)

**Analog:** `juce/video/native/CameraPreviewWindow.{h,cpp}` (Phase 19-02 Task 2)

**DocumentWindow construction pattern** (CameraPreviewWindow.cpp:6-49):

```cpp
CameraPreviewWindow::CameraPreviewWindow(JamWideFrameDistributor& distributor,
                                        juce::LookAndFeel* lookAndFeel,
                                        juce::Rectangle<int> initialBounds)
    : juce::DocumentWindow("JamWide — Camera",
                           juce::Colour(JamWideLookAndFeel::kSurfaceStrip),
                           juce::DocumentWindow::closeButton
                             | juce::DocumentWindow::minimiseButton)
{
    // D-08 — custom title bar so JamWideLookAndFeel paints dark chrome.
    setUsingNativeTitleBar(false);
    if (lookAndFeel) setLookAndFeel(lookAndFeel);

    // D-07 — 4:3 fixed aspect; 240x180 min / 2560x1920 max.
    setResizable(true, true);
    if (auto* constrainer = getConstrainer()) {
        constrainer->setFixedAspectRatio(4.0 / 3.0);
        constrainer->setSizeLimits(240, 180, 2560, 1920);
    }

    auto tile = std::make_unique<CameraPreviewTile>(distributor);
    tilePtr_ = tile.get();
    setContentOwned(tile.release(), /*resizeToFitWhenContentChangesSize*/ true);

    setBounds(initialBounds);
    setVisible(false);  // always starts hidden (D-10 mirror)

    addComponentListener(this);  // for bounds persistence
}
```

**closeButtonPressed = HIDE, not destroy** (CameraPreviewWindow.cpp:60-67):

```cpp
void CameraPreviewWindow::closeButtonPressed()
{
    // D-09 — clicking X does NOT stop capture. We just hide; the state
    // machine continues feeding the distributor (no consumer is fine);
    // the next Camera-button click reopens the popout per MEDIUM-1.
    setVisible(false);
    if (onCloseRequested) onCloseRequested();
}
```

**Bounds persistence via ComponentListener self-filter** (CameraPreviewWindow.cpp:69-78):

```cpp
void CameraPreviewWindow::componentMovedOrResized(juce::Component& which,
                                                  bool /*wasMoved*/,
                                                  bool /*wasResized*/)
{
    // ComponentListener fires for our own moves/resizes AND for the content
    // component (since we are the parent). Filter to events for ourselves
    // so the editor only persists window-level bounds.
    if (&which != this) return;
    if (onBoundsChanged) onBoundsChanged(getBounds());
}
```

**LookAndFeel cleanup pattern** (CameraPreviewWindow.cpp:51-58):

```cpp
CameraPreviewWindow::~CameraPreviewWindow()
{
    removeComponentListener(this);
    // Detach LookAndFeel BEFORE the editor's LookAndFeel is torn down. The
    // editor's destructor resets previewWindow_ BEFORE clearing its own
    // LookAndFeel, so the pointer we hold is valid through this call.
    setLookAndFeel(nullptr);
}
```

**What changes vs analog:**
- **Title bar:** `"JamWide — <username>"` per CONTEXT.md `<specifics>`; settable via a `setUsername(const juce::String&)` method (mirror of `CameraPreviewWindow::setDeviceName`).
- **Content:** Wraps a `RemotePeerTile` instead of `CameraPreviewTile`; constructor takes `JamWideRemoteFrameDistributor&` + `username` + `chidx=1` instead of `JamWideFrameDistributor&`.
- **Aspect lock kept:** Mirror of Phase 19 D-07 verbatim — `setFixedAspectRatio(4.0 / 3.0)` + `setSizeLimits(240, 180, 2560, 1920)` because `PeerVideoSink` surface is fixed 320×240 (Phase 21 Codex Cluster 10 LOW).
- **NEW: bring-back hook** — `closeButtonPressed` calls `setVisible(false)` AND `if (onCloseRequested) onCloseRequested();` — the editor's `onCloseRequested` lambda mounts the per-peer `PopoutPlaceholderCard` in the grid slot (D-03).
- **NEW: multi-monitor placement clamp** — at ctor, intersect `initialBounds` against `juce::Desktop::getInstance().getDisplays().getRectangleList(true)`; if no intersection, fall back to `(100, 100, 320, 240)` centered on primary (RESEARCH §Architecture Responsibility Map row "Multi-monitor clamp").

---

### `juce/ui/video/DetachedGridWindow.{h,cpp}` (component window, event-driven)

**Analog:** `juce/video/native/CameraPreviewWindow.{h,cpp}` — **same template**, with two divergences:

**What changes vs `CameraPreviewWindow` analog:**
- **Content is a `VideoGridBand`**, not a single tile. `setContentOwned(new VideoGridBand(...), true)`.
- **NOT aspect-locked.** Remove the `constrainer->setFixedAspectRatio(...)` call. Use `setSizeLimits(320, 240, 4096, 4096)` (planner-tunable min/max). The window contains an N-tile grid whose own `resized()` handles per-tile aspect-preservation.
- **Title:** `"JamWide — Video Grid"` (CONTEXT.md D-16, fixed).
- **Singleton enforcement:** Owned by the editor as `std::unique_ptr<DetachedGridWindow> detachedGrid_`; the editor's "open detached grid" lambda checks `if (!detachedGrid_) detachedGrid_ = std::make_unique<DetachedGridWindow>(...);` then `setVisible(true)`.
- **closeButtonPressed:** Per D-18, the planner picks between hide-with-placeholder vs destroy. Default recommendation: `closeButtonPressed` calls `setVisible(false)` + `onCloseRequested()`; the editor's lambda destroys the unique_ptr (`detachedGrid_.reset()`) and re-shows the in-main-view band (DISP-04 toggle is independent).

Everything else (LookAndFeel attachment, `setUsingNativeTitleBar(false)`, ComponentListener bounds publishing, multi-monitor clamp) is verbatim from the `CameraPreviewWindow` excerpts above.

---

### `juce/ui/video/VideoGridBand.{h,cpp}` (component, event-driven)

**Analog:** `juce/ui/ChannelStripArea.{h,cpp}` — for the **30 Hz Timer + roster-snapshot pattern** (the closest analog by data flow; ChannelStripArea is a hand-rolled-`resized()` container of dynamic child strips driven by roster events, which is exactly Phase 22's grid).

**30 Hz Timer setup pattern** (ChannelStripArea.h:10-12, ChannelStripArea.cpp:317-318):

```cpp
class ChannelStripArea : public juce::Component,
                          private juce::Timer  // REVIEW FIX #7: centralized timer
// ...
// In ctor:
    // Start centralized 30Hz timer (REVIEW FIX #7)
    startTimerHz(30);
```

**Roster-snapshot pattern under cachedUsersMutex** (ChannelStripArea.cpp:408-417):

```cpp
// Remote VU from cachedUsers, accounting for parent strips in multi-channel users.
// Hold cachedUsersMutex to prevent use-after-free when run thread replaces cachedUsers.
std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
const auto& users = processorRef.cachedUsers;
int stripIdx = 0;
for (const auto& user : users)
{
    // Skip bots — must mirror the filter in refreshFromUsers
    if (isBot(juce::String(user.name)))
        continue;
    // ...
}
```

**Hand-rolled `resized()` pattern** (ChannelStripArea.cpp:848-907):

```cpp
void ChannelStripArea::resized()
{
    auto area = getLocalBounds();

    // Fit button: bottom-left corner of the strip area, above viewport
    // Master strip pinned right
    auto masterArea = area.removeFromRight(kMasterWidth);
    masterStrip.setBounds(masterArea);

    // Separator
    area.removeFromRight(2);

    // Viewport with local + remote strips
    viewport.setBounds(area);

    // Layout strips inside container
    int localStripCount = 1 + (localExpanded_ ? static_cast<int>(localChildStrips.size()) : 0);
    int visibleRemoteStrips = 0;
    for (auto& strip : remoteStrips)
        if (strip->isVisible())
            ++visibleRemoteStrips;
    const int totalStrips = localStripCount + visibleRemoteStrips;
    const int containerWidth = totalStrips * kStripPitch;
    const int containerHeight = area.getHeight();
    stripContainer.setBounds(0, 0, containerWidth, containerHeight);

    int x = 0;
    localStrip.setBounds(x, 0, kStripWidth, containerHeight);
    x += kStripPitch;
    // ... for each remote strip: strip->setBounds(x, 0, kStripWidth, containerHeight); x += kStripPitch;
}
```

**Stop-timer-in-dtor pattern** (ChannelStripArea.cpp:324-326):

```cpp
ChannelStripArea::~ChannelStripArea()
{
    stopTimer();
    // ... attachment cleanup ...
}
```

**What changes vs analog:**
- **Layout math:** Instead of `containerWidth = totalStrips * kStripPitch`, call `computeGridLayout(N, W, H) → {cols, rows, tileW, tileH}` (a pure-C++ unit-testable helper). Then iterate `for (int i = 0; i < N; ++i) { tile[i]->setBounds(col*tileW + spacing, row*tileH + spacing, tileW, tileH); }` with `col = i % cols; row = i / cols`.
- **Timer responsibilities:** Sink-poll for D-13 add/remove notification (`for each cached user: distributor->findSink(name, 1)`; track delta; mount/unmount tiles) AND self-broadcast atomic observation (`connectionBar.getCameraIsBroadcasting()`) for D-07 self-tile gating. No VU updates.
- **Roster source:** Same `processorRef.cachedUsers` under `cachedUsersMutex` as `ChannelStripArea`. Use the same `isBot()` filter (memory: `project_ninbot_still_visible`).
- **Band header strip:** Thin (~24px) row at top with `↗` (detach) + `×` (close band) buttons painted in `paint()` (or as child `juce::TextButton`s). Tile flow area is `getLocalBounds().withTrimmedTop(kHeaderHeight)`.
- **Draggable bottom resizer:** Subclass `juce::Component::mouseDown/Drag/Up` on the bottom 4px strip; on drag, call `editor.setVideoGridBandHeight(newHeight)` which re-`resized()`s the editor.

---

### `juce/ui/ConnectionBar::GridButton` (file-local class in `ConnectionBar.cpp`)

**Analog:** `ConnectionBar::CameraButton` (file-local in `ConnectionBar.cpp:23-71`) — **exact match.**

**File-local subclass pattern** (ConnectionBar.cpp:19-71):

```cpp
// Phase 19-02 Task 1 — file-local subclass of juce::TextButton that intercepts
// right-clicks to show the camera quality + Stop Camera popup. Live-tracks
// the parent ConnectionBar so the menu can read currentCameraQualityPreset_
// (checkmark state) and cameraIsActive_ (Stop Camera enabled-state).
class ConnectionBar::CameraButton : public juce::TextButton {
public:
    explicit CameraButton(ConnectionBar& p) : parent(p) {}

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            // ... build menu reading parent.cameraIsActive_, parent.cameraIsBroadcasting_ ...
            menu.showMenuAsync(/* ... */);
            return;
        }
        juce::TextButton::mouseDown(e);
    }
private:
    ConnectionBar& parent;
};
```

**Forward-declared `unique_ptr<CameraButton>` in header** (ConnectionBar.h:107-108):

```cpp
// Backed by a file-local subclass (defined in .cpp) so we can intercept
// right-click for the quality + Stop Camera popup menu.
class CameraButton;
std::unique_ptr<CameraButton> cameraButton;
```

**Out-of-line dtor in header (REQUIRED for forward-declared unique_ptr)** (ConnectionBar.h:14-18):

```cpp
// Out-of-line destructor — unique_ptr<CameraButton> needs the complete
// CameraButton type (defined in ConnectionBar.cpp) to instantiate its
// deleter. Declared here so the compiler synthesises the dtor body in
// the .cpp where the full type is visible.
~ConnectionBar() override;
```

**Camera button construction + onClick wiring** (ConnectionBar.cpp:265-277):

```cpp
cameraButton = std::make_unique<CameraButton>(*this);
cameraButton->setButtonText("Camera");
cameraButton->setColour(juce::TextButton::buttonColourId,
    juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
cameraButton->setColour(juce::TextButton::textColourOffId,
    juce::Colour(JamWideLookAndFeel::kTextSecondary));
cameraButton->setTooltip("Toggle camera preview (v1.3 beta)");
cameraButton->onClick = [this]() {
    if (onCameraClicked) onCameraClicked();
};
addAndMakeVisible(*cameraButton);
```

**Camera button placement in `resized()`** (ConnectionBar.cpp:359-365):

```cpp
// Phase 19-02 — Camera button. 130px so the "Recheck permission" label
// (CameraState::Unavailable) fits without truncation in JUCE
// drawFittedText at 15pt. Legacy Video button removed 2026-05.
if (cameraButton) {
    cameraButton->setBounds(rightX - 130, y, 130, h);
    rightX -= 130 + gap;
}
```

**What changes vs analog:**
- **No right-click menu needed** for Grid (CONTEXT.md `<deferred>` rejected right-click). `GridButton` can be a plain `juce::TextButton` instance — no file-local subclass actually required. But for **state-mirroring symmetry** with Camera, the planner may still prefer a `GridButton` subclass that holds a reference to the parent so it can read `parent.gridIsVisible_` for label flipping ("Grid" idle / "Grid (on)" active).
- **Width budget:** ConnectionBar already uses `rightX -= 130 + gap` for Camera; planner picks a comparable width (~70-90px for "Grid (on)" text — narrower than "Recheck permission"). Placement is "next to Camera button" per D-05.
- **State accessor pair:** Mirror `setCameraIsBroadcasting(bool)` / `getCameraIsBroadcasting() const noexcept` (ConnectionBar.h:62-67) → `setGridVisible(bool)` / `getGridVisible() const noexcept`.
- **Tooltip:** "Toggle video grid" (idle) / "Hide video grid" (active) per CONTEXT.md `<specifics>`.

---

### `juce/JamWideJuceEditor.{h,cpp}` (controller, event-driven) — MODIFY

**Analog:** Self — existing `previewWindow_` wiring (lines 81-82 in `.h`, lines 230-253 in `.cpp`) is the direct template for grid-band + detached-grid + per-peer-popout ownership.

**Existing CameraPreviewWindow ownership pattern** (JamWideJuceEditor.h:81-82):

```cpp
// Phase 19-02 — native-camera UI surface. The popout window is constructed
// in the editor constructor and torn down by the editor destructor; the
// privacy dialog is lazily constructed on first need (~24 bytes idle).
std::unique_ptr<jamwide::CameraPreviewWindow> previewWindow_;
std::unique_ptr<jamwide::NativeCameraPrivacyDialog> privacyDialog_;
```

**Existing preview-window construction + callback wiring** (JamWideJuceEditor.cpp:236-253):

```cpp
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
```

**Existing `resized()` layout slot pattern** (JamWideJuceEditor.cpp:372-405):

```cpp
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

    // ... toggle button positioning ...

    // Channel strip area fills remaining center
    channelStripArea.setBounds(area);

    // Overlays: full editor bounds
    serverBrowser.setBounds(getLocalBounds());
    licenseDialog.setBounds(getLocalBounds());
    videoPrivacyDialog.setBounds(getLocalBounds());
}
```

**Existing 20 Hz Timer + drainEvents pattern** (JamWideJuceEditor.cpp:286 + 407+):

```cpp
// Start 20Hz timer for event drain and status polling
startTimerHz(20);
// ...
void JamWideJuceEditor::timerCallback()
{
    drainEvents();
    pollStatus();
    // ... beat-bar update from atomics ...
}
```

**What changes:**

1. **New `.h` members** (mirror `previewWindow_` pattern):
   ```cpp
   // Phase 22 — video grid surfaces. gridBand_ is a direct member (visibility
   // toggled via setVisible); detachedGrid_ is lazy singleton; remotePopouts_
   // is a lazy per-peer map (one popout window per peer, keyed by username).
   std::unique_ptr<jamwide::VideoGridBand> gridBand_;
   std::unique_ptr<jamwide::DetachedGridWindow> detachedGrid_;
   std::unordered_map<juce::String, std::unique_ptr<jamwide::RemotePeerPopoutWindow>>
       remotePopouts_;
   // Session-latch flag for D-05 auto-open trigger (set once, never reset).
   std::atomic<bool> gridAutoOpenedThisSession_{false};
   ```

2. **`resized()` insertion** — between `sessionInfoStrip.setBounds(...)` and `channelStripArea.setBounds(area)`:
   ```cpp
   if (infoStripVisible)
       sessionInfoStrip.setBounds(area.removeFromTop(kSessionInfoStripHeight));

   // Phase 22 — video grid band, conditional. Inserted ABOVE the channel
   // strip area so mixer shrinks vertically when the band is open.
   if (gridBand_ && gridBand_->isVisible()) {
       const int bandHeight = processorRef.getVideoGridBandHeight();
       gridBand_->setBounds(area.removeFromTop(bandHeight));
   }

   // ... chat panel + toggle button ...
   channelStripArea.setBounds(area);
   ```

3. **`timerCallback()` extension** — add the D-05 auto-open latch:
   ```cpp
   // Phase 22 D-05 — auto-open grid on first observed peer frame.
   if (!gridAutoOpenedThisSession_.load(std::memory_order_relaxed)) {
       if (auto* dist = processorRef.getRemoteFrameDistributor()) {
           std::vector<NJClient::RemoteUserInfo> snapshot;
           { std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
             snapshot = processorRef.cachedUsers; }
           for (const auto& u : snapshot) {
               if (auto* sink = dist->findSink(u.name, 1)) {
                   if (sink->first_frame_seen.load(std::memory_order_relaxed)) {
                       gridAutoOpenedThisSession_.store(true, std::memory_order_relaxed);
                       toggleGridBand(/*visible*/ true);
                       break;
                   }
               }
           }
       }
   }
   ```

4. **Bring-back controller methods** (new on editor):
   ```cpp
   void bringBackRemotePopout(const juce::String& username);  // erase remotePopouts_[username]
   void reattachGrid();                                        // destroy detachedGrid_
   void setVideoGridBandHeight(int h);                         // persist + resized()
   void toggleGridBand(bool visible);                          // setVisible + persist + resized()
   ```

5. **ConnectionBar wiring** — add the GridButton callback hookup in the editor ctor:
   ```cpp
   connectionBar.onGridClicked = [this]() { toggleGridBand(!gridBand_->isVisible()); };
   ```

---

### `juce/JamWideJuceProcessor.{h,cpp}` (model, CRUD via XML)

**Analog:** Self — existing `<camera>` flat-property handling (Phase 19 D-24 v3→v4 bump) is the direct template, with the variant for `Map<String,Rectangle>` (popoutBounds) which needs a child ValueTree node (RESEARCH §"Alternatives Considered" recommends structured XML).

**Existing state-version constant** (JamWideJuceProcessor.h:95-96):

```cpp
// v4: added camera flat properties (Phase 19-02; D-24, D-25).
static constexpr int currentStateVersion = 4;
```

**Existing accessor pair pattern (mutex-guarded composite)** (JamWideJuceProcessor.cpp:677-687):

```cpp
juce::Rectangle<int> JamWideJuceProcessor::getCameraPopoutBounds() const
{
    std::lock_guard<std::mutex> lk(cameraPopoutMu_);
    return cameraPopoutBounds_;
}

void JamWideJuceProcessor::setCameraPopoutBounds(juce::Rectangle<int> bounds)
{
    std::lock_guard<std::mutex> lk(cameraPopoutMu_);
    cameraPopoutBounds_ = bounds;
}
```

**Existing v4 save pattern (flat siblings)** (JamWideJuceProcessor.cpp:968-979):

```cpp
// Phase 19-02 — camera flat properties (state version 4; D-24, D-25).
// Approach A per RESEARCH §8: 7 sibling properties at the top level
// (NOT a nested child node) for symmetry with the existing oscEnabled /
// chatSidebarVisible / etc. shape.
auto popoutBounds = getCameraPopoutBounds();
state.setProperty("cameraPopoutX",       popoutBounds.getX(), nullptr);
state.setProperty("cameraPopoutY",       popoutBounds.getY(), nullptr);
state.setProperty("cameraPopoutWidth",   popoutBounds.getWidth(), nullptr);
state.setProperty("cameraPopoutHeight",  popoutBounds.getHeight(), nullptr);
state.setProperty("cameraQualityPreset", cameraQualityPreset_.load(std::memory_order_relaxed), nullptr);
state.setProperty("cameraPrivacyAck",    cameraPrivacyAck_.load(std::memory_order_relaxed), nullptr);
state.setProperty("cameraSelectedDevice", getCameraSelectedDevice(), nullptr);
```

**Existing v4 load pattern with `jlimit` clamping + graceful default** (JamWideJuceProcessor.cpp:1085-1099):

```cpp
// STEP 5: Phase 19-02 — camera flat properties (state version 4).
// T-19-03 mitigation: every field is read via tree.getProperty(key,
// default) and aggressively clamped. v3 state (no camera fields) gets
// D-25 defaults; malicious v4 state with out-of-range values is
// sanitised before being applied to the runtime camera/popout state.
{
    const int popX = juce::jlimit(-10000, 10000, (int) tree.getProperty("cameraPopoutX", 100));
    const int popY = juce::jlimit(-10000, 10000, (int) tree.getProperty("cameraPopoutY", 100));
    const int popW = juce::jlimit(240, 2560, (int) tree.getProperty("cameraPopoutWidth", 320));
    const int popH = juce::jlimit(180, 1920, (int) tree.getProperty("cameraPopoutHeight", 240));
    setCameraPopoutBounds(juce::Rectangle<int>(popX, popY, popW, popH));

    const int qp = juce::jlimit(0, 2, (int) tree.getProperty("cameraQualityPreset", 1));
    setCameraQualityPreset(qp);
    if (nativeCamera) nativeCamera->setQualityPreset(qp);
}
```

**What changes:**

1. **Bump version constant:**
   ```cpp
   // v5: added video grid + popout flat fields + <video> child ValueTree for
   // per-peer popout bounds map (Phase 22; D-19).
   static constexpr int currentStateVersion = 5;
   ```

2. **New persisted state fields + accessor pairs** (mirror `getCameraPopoutBounds`):
   ```cpp
   // Phase 22 D-19 — video grid persisted state.
   bool getVideoGridVisible() const;
   void setVideoGridVisible(bool v);
   int  getVideoGridBandHeight() const;             // default 280
   void setVideoGridBandHeight(int h);              // clamp to [120, 800]
   juce::Rectangle<int> getDetachedGridBounds() const;
   void setDetachedGridBounds(juce::Rectangle<int> b);
   juce::Rectangle<int> getRemotePopoutBounds(const juce::String& username) const;
   void setRemotePopoutBounds(const juce::String& username, juce::Rectangle<int> b);
   ```

3. **Save:** Mix flat siblings (for scalar grid fields) AND a `<video>` child ValueTree (for the `Map<String,Rectangle>` per-peer popout bounds — RESEARCH "Structured XML recommended"):
   ```cpp
   // Phase 22 — video grid scalar flat properties.
   state.setProperty("videoGridVisible",     getVideoGridVisible(), nullptr);
   state.setProperty("videoGridBandHeight",  getVideoGridBandHeight(), nullptr);
   auto dgB = getDetachedGridBounds();
   state.setProperty("videoDetachedGridX",      dgB.getX(), nullptr);
   state.setProperty("videoDetachedGridY",      dgB.getY(), nullptr);
   state.setProperty("videoDetachedGridWidth",  dgB.getWidth(), nullptr);
   state.setProperty("videoDetachedGridHeight", dgB.getHeight(), nullptr);

   // Phase 22 — per-peer popout bounds as a structured <video> child node.
   juce::ValueTree video("video");
   for (const auto& kv : remotePopoutBoundsMap_) {
       juce::ValueTree p("popout");
       p.setProperty("name",   kv.first, nullptr);
       p.setProperty("x",      kv.second.getX(), nullptr);
       p.setProperty("y",      kv.second.getY(), nullptr);
       p.setProperty("w",      kv.second.getWidth(), nullptr);
       p.setProperty("h",      kv.second.getHeight(), nullptr);
       video.addChild(p, -1, nullptr);
   }
   state.appendChild(video, nullptr);
   ```

4. **Load (forward-compatible — v4 states get v5 defaults):**
   ```cpp
   // STEP 6: Phase 22 — video grid flat properties (state version 5).
   // v4 state (no video fields) gets D-19 defaults.
   {
       const bool gv = (bool) tree.getProperty("videoGridVisible", false);
       setVideoGridVisible(gv);

       const int bh = juce::jlimit(120, 800, (int) tree.getProperty("videoGridBandHeight", 280));
       setVideoGridBandHeight(bh);

       const int dgX = juce::jlimit(-10000, 10000, (int) tree.getProperty("videoDetachedGridX", 100));
       const int dgY = juce::jlimit(-10000, 10000, (int) tree.getProperty("videoDetachedGridY", 100));
       const int dgW = juce::jlimit(320, 4096, (int) tree.getProperty("videoDetachedGridWidth", 800));
       const int dgH = juce::jlimit(240, 4096, (int) tree.getProperty("videoDetachedGridHeight", 450));
       setDetachedGridBounds(juce::Rectangle<int>(dgX, dgY, dgW, dgH));
   }

   // Per-peer popout bounds map — structured <video> child node.
   if (auto video = tree.getChildWithName("video"); video.isValid()) {
       std::lock_guard<std::mutex> lk(remotePopoutBoundsMu_);
       remotePopoutBoundsMap_.clear();
       for (int i = 0; i < video.getNumChildren(); ++i) {
           auto p = video.getChild(i);
           if (!p.hasType("popout")) continue;
           const juce::String name = p.getProperty("name", "").toString();
           if (name.isEmpty()) continue;
           const int x = juce::jlimit(-10000, 10000, (int) p.getProperty("x", 100));
           const int y = juce::jlimit(-10000, 10000, (int) p.getProperty("y", 100));
           const int w = juce::jlimit(240, 2560, (int) p.getProperty("w", 320));
           const int h = juce::jlimit(180, 1920, (int) p.getProperty("h", 240));
           remotePopoutBoundsMap_.emplace(name, juce::Rectangle<int>(x, y, w, h));
       }
   }
   ```

**Member declaration order (REQUIRED):** Add the mutex + map together (mirrors `cameraPopoutMu_` / `cameraPopoutBounds_` declared as a pair):
```cpp
mutable std::mutex remotePopoutBoundsMu_;
std::unordered_map<juce::String, juce::Rectangle<int>> remotePopoutBoundsMap_;
```

---

### `juce/ui/video/PopoutPlaceholderCard.{h,cpp}` (component, event-driven)

**Analog:** Inline `JamWideJuceEditor::ChatToggleButton` (JamWideJuceEditor.h:95-136) — for the **custom-paint + mouseEnter/mouseExit/mouseDown** pattern.

**Custom paint + hover pattern** (JamWideJuceEditor.h:95-136):

```cpp
struct ChatToggleButton : public juce::Component
{
    bool pointsRight = true;
    bool hovering = false;
    std::function<void()> onClick;

    ChatToggleButton() { setRepaintsOnMouseActivity(true); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        // Background with subtle hover brightening
        auto bgColour = juce::Colour(JamWideLookAndFeel::kBgElevated);
        if (hovering)
            bgColour = bgColour.brighter(0.15f);
        g.setColour(bgColour);
        g.fillRoundedRectangle(bounds, 2.0f);
        // ... arrow path ...
    }

    void mouseEnter(const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override  { hovering = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override   { if (onClick) onClick(); }
};
```

**What changes vs analog:**
- **Card-style frame** instead of arrow — dark VB-style rounded rect (use `JamWideLookAndFeel::kSurfaceStrip` background, `kBorderSubtle` outline). Per CONTEXT.md `<specifics>`.
- **Text:** Per-peer = `"Popped out →"` (with `setText(const String&)` setter for username injection if planner wants), whole-grid = `"Grid is in detached window →"`.
- **Cursor hint on hover:** `setMouseCursor(juce::MouseCursor::PointingHandCursor)`.
- **Same `std::function<void()> onClick`** wired by the editor to `bringBackRemotePopout(username)` or `reattachGrid()`.

---

### `juce/ui/video/computeGridLayout.h` (utility, pure math)

**Analog:** None — pure-C++ helper, no analog needed.

**Recommended signature** (per RESEARCH Pattern 2 mention):

```cpp
namespace jamwide {

struct GridLayoutResult {
    int cols;
    int rows;
    int tileW;
    int tileH;
};

// Pure function — given peer count N and available band geometry (W, H),
// compute the grid column count and per-tile size that:
//   (a) maximizes tile area while satisfying cols * tileW <= W;
//   (b) preserves 4:3 aspect (tileH = tileW * 3 / 4);
//   (c) caps at kMaxCols (default 4) so very wide bands don't collapse rows;
//   (d) caps min tile width at kMinTileW (default 120px) before falling back
//       to scroll mode (caller's responsibility).
//
// Unit-testable in isolation; called from VideoGridBand::resized().
inline GridLayoutResult computeGridLayout(int N, int W, int H,
                                          int kMinTileW = 120,
                                          int kMaxCols = 4) noexcept;

} // namespace jamwide
```

---

## Shared Patterns

### MEMBER-ORDER CONTRACT (RAII subscription lifetime)

**Source:** `juce/video/native/CameraPreviewTile.h:14-19, 50-69` (canonical) + `juce/video/native/CameraPreviewTile.cpp:6-31` (constructor + destructor commentary).

**Apply to:** `SelfVideoTile`, `RemotePeerTile` — both MUST declare the `Subscription` (or equivalent RAII handle) as the **LAST** member of the class.

**Rule:** C++ destroys members in reverse declaration order. `~Subscription` calls `owner_->unsubscribe_(...)` which blocks for any in-flight callback (Phase 19's `unregisterAndWait`, Phase 21's `removeListener` with `inFlightCv_`). Putting the subscription LAST means it tears down FIRST, blocking the callback thread before any mutex/frame/atomic member referenced by the callback is destroyed. Reversing this order (subscription declared FIRST) re-opens the HIGH-2/HIGH-4 UAF race that Phase 19-01 Codex explicitly closed.

### DocumentWindow with custom LookAndFeel + ComponentListener

**Source:** `juce/video/native/CameraPreviewWindow.cpp:6-58, 60-78`.

**Apply to:** `RemotePeerPopoutWindow`, `DetachedGridWindow`.

**Six-step recipe:**
1. `juce::DocumentWindow(title, bgColour, closeButton | minimiseButton)` in initializer list.
2. `setUsingNativeTitleBar(false); setLookAndFeel(lookAndFeel);` for VB-Banana dark chrome.
3. `setResizable(true, true);` then `getConstrainer()->setFixedAspectRatio + setSizeLimits` (popout only; detached grid skips aspect lock).
4. `setContentOwned(child.release(), /*resizeToFit*/ true)` to wrap the tile/grid.
5. `setBounds(initialBounds); setVisible(false);` — windows always start hidden (D-14 mirror of Phase 19 D-10).
6. `addComponentListener(this);` — implement `componentMovedOrResized` with the self-filter `if (&which != this) return;` to publish bounds to the editor via `onBoundsChanged` lambda.

**Cleanup:** `removeComponentListener(this); setLookAndFeel(nullptr);` in dtor (mirror of `~CameraPreviewWindow`).

### closeButtonPressed = HIDE, not destroy

**Source:** `juce/video/native/CameraPreviewWindow.cpp:60-67` (D-09).

**Apply to:** `RemotePeerPopoutWindow` (D-17 mirror); `DetachedGridWindow` (planner picks per D-18 — recommended default: hide + `onCloseRequested` → editor destroys).

**Rule:** `closeButtonPressed` body is `{ setVisible(false); if (onCloseRequested) onCloseRequested(); }` — the underlying `Subscription` keeps frames arriving so the next reopen is instant. The editor's `onCloseRequested` lambda mounts the placeholder card in the in-grid slot (D-03).

### 30 Hz Timer for roster polling + repaint coalescing

**Source:** `juce/ui/ChannelStripArea.h:10-12 + .cpp:317-318, 324-326`.

**Apply to:** `VideoGridBand` (sink-poll + self-broadcast observation per D-13 Option a).

**Three-step recipe:**
1. Inherit `private juce::Timer` (NOT public — Timer is an implementation detail).
2. `startTimerHz(30);` in the body of the constructor.
3. `stopTimer();` as the FIRST line of the destructor (before any child component is touched).

### Lazy unique_ptr ownership in the editor (popout windows + camera widgets)

**Source:** `juce/JamWideJuceEditor.h:81-82 + .cpp:230-253`.

**Apply to:** `gridBand_` (direct member, visibility-toggled), `detachedGrid_` (lazy singleton), `remotePopouts_` (lazy `unordered_map<String, unique_ptr<...>>`), `previewWindow_` (already exists — REUSED for self-tile popout per D-09).

**Construction site:** Editor ctor, with `onCloseRequested` + `onBoundsChanged` callbacks wired in the same block (mirror of `previewWindow_->onBoundsChanged = ...` at JamWideJuceEditor.cpp:249).

### ValueTree state save/load with version bump + jlimit clamping

**Source:** `juce/JamWideJuceProcessor.h:95-96 + .cpp:920-983 + 985-1099`.

**Apply to:** Phase 22's v4→v5 bump.

**Four-step recipe:**
1. Bump `static constexpr int currentStateVersion = N+1;` in `.h`.
2. In `getStateInformation`: `state.setProperty(key, value, nullptr);` for each new scalar; `state.appendChild(childTree, nullptr);` for structured per-key maps.
3. In `setStateInformation`: every `tree.getProperty(key, default)` MUST have a default; every numeric MUST be `juce::jlimit(min, max, ...)`-clamped (T-19-03 mitigation pattern — see `.cpp:1091-1099`).
4. Forward compatibility: missing keys silently fall to defaults. v4 states loaded by v5 code MUST work without user-visible degradation.

### Hand-rolled `resized()` with `area.removeFromTop/Bottom/Left/Right`

**Source:** `juce/JamWideJuceEditor.cpp:372-405` (editor); `juce/ui/ChannelStripArea.cpp:848-907` (container).

**Apply to:** `VideoGridBand::resized()` for band header + tile flow area split; `JamWideJuceEditor::resized()` for the band insertion between `sessionInfoStrip` and `channelStripArea`.

**Pattern:** `auto area = getLocalBounds();` then chain `area.removeFromTop(h)`, `area.removeFromRight(w)`, etc. The remaining `area` is what's left for the central child (channel strip area in editor; tile flow area in band).

---

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `juce/ui/video/computeGridLayout.h` | utility | pure math | Greenfield pure helper; no existing layout-math utilities in `juce/ui/`. By design — see RESEARCH Pattern 2 "Hand-rolled recommended over FlexBox." |

All component files have a strong analog; only the pure utility lacks one.

---

## Metadata

**Analog search scope:**
- `juce/video/native/` — Phase 19 camera UI (CameraPreviewTile, CameraPreviewWindow, JamWideFrameDistributor)
- `juce/video/distributor/` — Phase 21 receive substrate (JamWideRemoteFrameDistributor, PeerVideoSink)
- `juce/ui/` — Existing UI components (ChannelStripArea, ConnectionBar, JamWideLookAndFeel)
- `juce/JamWideJuceEditor.{h,cpp}` — Editor integration points
- `juce/JamWideJuceProcessor.{h,cpp}` — State persistence + distributor wiring

**Files scanned:** 14 source files + 2 headers + 2 phase docs
**Pattern extraction date:** 2026-05-17
