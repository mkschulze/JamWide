# Phase 22: Native Video UI (Grid + Popouts) — Research

**Researched:** 2026-05-17
**Domain:** JUCE 8.0.12 UI composition — three coexistent rendering surfaces (in-main-view grid band + detachable whole-grid `DocumentWindow` + per-peer popout `DocumentWindow`s) consuming Phase 21's `JamWideRemoteFrameDistributor`/`PeerVideoSink` substrate via independent `Subscription` handles. UI-only — zero audio-path or send-path code.
**Confidence:** HIGH

## Summary

Phase 22 has unusually small architectural design space. CONTEXT.md locks 19 decisions (D-01..D-19) and the Phase 19 + Phase 21 substrate ships every primitive the consumer needs: `Subscription` RAII detach, double-buffered `PeerVideoSink::image_front`/`image_back` under `bufferLock`, atomic status fields read lock-free per-paint, `juce::AsyncUpdater` coalescing, and `JamWideRemoteFrameDistributor::subscribeToPeer(username, chidx, onRepaint)`. The planner's freedom is concentrated in (a) `juce::FlexBox` vs hand-rolled `resized()`, (b) which sink-add/remove notification mechanism (Timer-poll vs distributor event API), (c) how to compose three windows that share the same per-peer subscription bundle, and (d) state schema bump v4→v5 layout.

The work is a **direct mirror** of Phase 19's `CameraPreviewWindow`/`CameraPreviewTile` template. Two specialised tile classes are required (D-08): `SelfVideoTile` subscribes via Phase 19's callback-style `JamWideFrameDistributor::Subscriber`; `RemotePeerTile` subscribes via Phase 21's RAII-style `JamWideRemoteFrameDistributor::Subscription`. Both follow the MEMBER-ORDER CONTRACT (subscription as LAST declared member so destructor blocks in-flight callbacks before mutex/frame members are torn down). The detached-grid window and per-peer popout windows are direct DocumentWindow analogs of `CameraPreviewWindow`, modulo (1) per-peer popouts are non-singleton (`unordered_map<String, unique_ptr<RemotePeerPopoutWindow>>`) and (2) the detached-grid window contains an entire `VideoGridBand` instance rather than a single tile.

The validation surface is constrained by JUCE: a `DocumentWindow` cannot be fully exercised in a headless unit test because the constrainer + native-frame chrome require an `AlertWindow`/desktop surface, so `RemotePeerPopoutWindow` and `DetachedGridWindow` are best validated via UAT (cells 4-7 below). What CAN be fully unit-tested at sub-second speed: tile MEMBER-ORDER lifetime (mirror of `test_frame_distributor_lifetime.cpp` for the remote distributor), grid column-count math (`computeGridLayout(N peers, W px, H px) → (cols, rows, tileW, tileH)`), placeholder-card state transitions, plugin state v4→v5 round-trip (mirror of `test_plugin_state_v3_v4.cpp` — `Map<String,Rectangle>` XML serialization + bounds clamping). The atomic state machine for auto-open-on-first-frame (D-05 latch flag) is also unit-testable.

**Primary recommendation: 3 plans.**
- **22-01 — Grid + tile substrate.** New `juce/ui/video/` directory: `VideoGridBand.h/.cpp`, `VideoTileBase.h/.cpp`, `SelfVideoTile.h/.cpp`, `RemotePeerTile.h/.cpp`. ConnectionBar `GridButton`. Editor `resized()` insertion between `sessionInfoStrip` and `channelStripArea`. Roster discovery via 30Hz Timer polling `JamWideRemoteFrameDistributor::findSink` per cached user (D-13 Option a — recommended over a distributor surface change to keep Phase 21 cleanly closed). Unit tests for `computeGridLayout`, tile lifetime, sink-poll attach/detach.
- **22-02 — Popout windows + placeholder cards + state persistence.** `RemotePeerPopoutWindow`, `DetachedGridWindow`, `PopoutPlaceholderCard`, `DetachedGridPlaceholderCard`. Editor owns `unordered_map<String, unique_ptr<RemotePeerPopoutWindow>>` (per-peer, lazy) + `unique_ptr<DetachedGridWindow>` (singleton, lazy). Plugin state v4→v5 bump: new `<video>` ValueTree subtree with `gridVisible` / `gridBandHeight` / `detachedGridBounds` / `popoutBounds` (Map<String,Rectangle>). Unit tests for v4→v5 round-trip + clamping.
- **22-03 — UAT procedure + verification gate.** Wire the auto-open-on-first-frame latch (D-05) to the editor's existing 20Hz Timer; verify against `video.ninjamzap.com:2049` with at least 2 broadcasters; produce `phase-22-grid-popout-uat-procedure.md` + `phase-22-grid-popout-uat-report.md` covering all 4 ROADMAP success criteria + the 13 specific UAT cells listed in §Validation Architecture below.

**Decomposition rationale:** 22-01 has zero dependencies on persisted state and can land green standalone (manual UAT bring-up); 22-02 layers the popout windows on top of 22-01's tile substrate; 22-03 is the human-in-the-loop gate that 22-01+22-02 cannot self-verify (multi-monitor placement, real broadcasters, real plugin reload). The seam between 22-01 and 22-02 matches a natural commit/review boundary: 22-01 is "in-main-view grid renders correctly," 22-02 is "I can detach + pop out + reload the plugin and bounds persist." If reviewers push back on scope, 22-02 and 22-03 could merge into a single plan; 22-01 should NOT merge with 22-02 because the popout window state machine is the riskier code.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|--------------|----------------|-----------|
| `JamWideRemoteFrameDistributor::subscribeToPeer` call from per-peer tile mount | Message thread (editor / `VideoGridBand`) | — | Phase 21 D-06 multi-listener fan-out is what enables three coexistent surfaces from one sink. Subscribe at tile mount; let RAII detach at tile destruction. |
| `JamWideFrameDistributor::registerSubscriber` call from self-tile mount | Message thread (`SelfVideoTile`) | — | Phase 19 distributor's callback-style `Subscriber*`; D-08 separates this from peer-tile contract. |
| Sink-add/remove notification → tile add/remove | Message thread (30Hz Timer in `VideoGridBand`) | — | D-13 Option (a) — Timer polls `JamWideRemoteFrameDistributor::findSink(username, chidx=1)` for each entry in `processorRef.cachedUsers`. Simpler than extending Phase 21's distributor with an event API; matches the existing 30Hz polling cadence in `ChannelStripArea::timerCallback`. |
| Auto-open-on-first-frame trigger (D-05) | Message thread (editor's existing 20Hz Timer) | — | The editor's `timerCallback` already runs at 20Hz (`startTimerHz(20)` at line 286). It walks `cachedUsers` + queries `getRemoteFrameDistributor()->findSink(...)` per peer; on first observed `sink->first_frame_seen.load() == true` with a pristine session-latch flag, calls `toggleGridBand(true)` once and sets the latch. |
| Grid layout (column-count + tile-size compute) | Message thread (`VideoGridBand::resized()` + helper `computeGridLayout`) | — | Pure-C++ function `computeGridLayout(N peers, W, H, kMinTileW=120, kMaxCols=4) → {cols, rows, tileW, tileH}`. Unit-testable in isolation; consumed by `resized()`. |
| Tile paint (snapshot `image_front` + atomic status fields → `juce::Graphics`) | Message thread (`paint()` after `triggerAsyncUpdate` coalesced repaint) | — | Phase 21 D-08 contract: take a brief `juce::ScopedLock` on `sink->bufferLock`, copy refcount of `image_front` to a local, release lock, paint local. Atomic status fields (`first_frame_seen`, `hold_count`, etc.) read lock-free for overlay decisions. |
| Self-tile broadcast-state observation (D-07) | Message thread (`VideoGridBand::timerCallback`) | — | Poll `connectionBar.getCameraIsBroadcasting()` each tick (already a `bool` not atomic; UI-thread-only read). Show/hide `SelfVideoTile` on edge transitions. |
| Popout window bounds tracking | Message thread (`ComponentListener::componentMovedOrResized` on each window) | — | Direct mirror of `CameraPreviewWindow::componentMovedOrResized` filter (`if (&which != this) return;`). Publishes to a controller method `editor.setRemotePopoutBounds(username, rect)` or `editor.setDetachedGridBounds(rect)` for D-15/D-16 persistence. |
| State persistence (v4→v5, `<video>` subtree write/read) | Message thread (`JamWideJuceProcessor::getStateInformation` / `setStateInformation`) | — | Direct mirror of Phase 19 D-24 v3→v4 add. Approach B (child `<video>` ValueTree node, NOT 7 sibling properties) is recommended because `Map<String,Rectangle>` for D-15 popout bounds needs structured XML serialization. |
| Multi-monitor clamp on popout reopen | Message thread (`RemotePeerPopoutWindow` / `DetachedGridWindow` ctor) | — | Read `Desktop::getInstance().getDisplays().getRectangleList(true)` (the union of all monitor user-areas in logical px). If the saved popout intersects no display, fall back to `(100, 100, 320, 240)` centered on primary. |
| Bring-back from placeholder card | Message thread (`PopoutPlaceholderCard::mouseDown` lambda → editor controller) | — | Card calls `editor.bringBackRemotePopout(username)` → editor destroys the popout via `remotePopouts.erase(username)` → `VideoGridBand::refreshFromUsers` re-mounts the live tile in-place. |

**Why this matters:** The most common Phase 22 misattribution would be putting the sink-poll loop or the auto-open trigger on the audio thread — both belong on the message thread (DISP-04 explicitly forbids audio-path touches). The second-most-common misattribution would be putting the grid layout math inside `VideoGridBand::paint()` (recomputing every frame) instead of `resized()` (recomputed only on geometry change). The map above pre-empts both.

## User Constraints (from CONTEXT.md)

### Locked Decisions

**Grid Surfaces + Layout (Area 1):**

- **D-01:** Three coexistent rendering surfaces — in-main-view band, detachable whole-grid window, per-user popouts. All three consume the same per-peer `PeerVideoSink::image_front` via independent `JamWideRemoteFrameDistributor::Subscription` handles. `juce::Image` is ref-counted, so multi-surface paint is cheap (each surface takes its own brief `bufferLock` snapshot then paints outside the lock). Phase 21's `listenerLock_` snapshot-then-fan-out pattern already handles multi-listener fan-out per sink without contention.
- **D-02:** In-main-view grid band is a stacked horizontal band between `SessionInfoStrip` and `ChannelStripArea`. Mixer remains the visual anchor (VB-Banana "mixer is sacred"). Does NOT use side-by-side (would squeeze VbFader width) or replace-mode (would hide mixer entirely).
- **D-03:** Placeholder cards when a tile is popped out or the grid is detached. Per-peer popout active → that peer's slot renders a placeholder. Whole-grid detach active → the entire band renders a single full-band placeholder.
- **D-04:** Always-visible corner `↗` icons for detach/popout affordances (not hover-reveal).
- **D-05:** ConnectionBar "Grid" button + auto-open on first peer frame. Persisted across sessions. Auto-opens once per session when ANY peer's `PeerVideoSink::first_frame_seen` first flips false→true. Subsequent toggles are manual.

**Tile Layout + Sizing (Area 2):**

- **D-06:** Fill-width adaptive layout — compute N×M from peer count + available width. 4:3 aspect-preserved (matches `PeerVideoSink` fixed 320×240 receive surface). 1 peer = full-band tile; 2-4 = side-by-side; 5-9 = 3 cols; 10+ = scroll. Implementation via `juce::FlexBox` OR hand-rolled in `resized()`.
- **D-07:** Self-tile is first slot in grid; appears only when self is BROADCASTING (not just camera ON). Symmetric with peer-tile rule. Driven by `ConnectionBar::getCameraIsBroadcasting()`. When broadcasting, self-tile subscribes to Phase 19's `JamWideFrameDistributor`.
- **D-08:** Two specialized tile component classes — `SelfVideoTile` (Phase 19 distributor) + `RemotePeerTile` (Phase 21 distributor) — sharing a common chrome/render base class (`VideoTileBase`). Both follow MEMBER-ORDER CONTRACT (subscription as LAST declared member).
- **D-09:** Self-tile popout button re-opens Phase 19's existing `CameraPreviewWindow`. NO second self-popout window is created.
- **D-10:** Fixed default band height (~280px planner-picked baseline) with draggable horizontal resizer on bottom edge. Persisted across sessions.

**Tile Chrome (Area 3):**

- **D-11:** Minimal tile chrome — username strip (bottom, hover-hide) + popout `↗` (top-right always visible) + Phase 21 status overlays only. NO audio mute/solo, NO VU meter, NO codec/error badges on the tile.
- **D-12:** Tile appears ONLY when peer is actively broadcasting (first H264 BEGIN observed); disappears when they stop broadcasting or leave room. No empty slots.
- **D-13:** Tile add/remove notification — Phase 22 grid polls the distributor's sink map via a message-thread `juce::Timer` (Option a, recommended) OR via a new `JamWideRemoteFrameDistributor::onSinkAdded/onSinkRemoved` listener (Option b, requires Phase 21 distributor touch-up).

**Popout State Persistence (Area 4):**

- **D-14:** Bounds-only persistence; popouts always start CLOSED on plugin/standalone launch. Mirror of Phase 19 D-10.
- **D-15:** Per-peer popout bounds keyed by username — `Map<String username, Rectangle bounds>`. Serialized as XML inside the `<video>` ValueTree subtree.
- **D-16:** Detached-grid window is a single state slot — `Rectangle bounds`. Singleton (only one open at a time). Auto-restore = NO. Title bar: "JamWide — Video Grid".
- **D-17:** Closing a popout HIDES the window; does NOT destroy the underlying distributor Subscription. Mirror of Phase 19 D-09.
- **D-18:** "Bring back" affordance on placeholder cards = click the card → destroy the popout/detached-grid window → restore the live tile/grid in-place.
- **D-19:** State schema bump v4 → v5; new `<video>` ValueTree subtree (NOT extending `<camera>`). Contains `gridVisible` (bool, default false), `gridBandHeight` (int, default ~280px), `detachedGridBounds` (Rectangle), `popoutBounds` (Map<username, Rectangle>). `loadState` handles missing v4→v5 fields gracefully via defaults.

### Claude's Discretion

- Band default height baseline (~280px = ~1 row of 4:3 tiles at modest scale)
- Detached-grid-window default monitor placement on first open
- Mixer min-height clamp behavior when band+mixer can't fit
- Aspect-preservation policy when band is wider than tile×cols
- Min tile size threshold before scroll-fallback (~120px)
- Inter-tile spacing/padding (4-8px)
- "Popped out" placeholder card visual
- Layout reshuffle animation on tile add/remove
- Sink-add/remove notification mechanism (D-13 Option a vs b)
- Stale popout bounds handling when monitor disconnected
- Aspect-lock policy for remote popouts
- Keyboard shortcuts for grid toggle / popout / detach
- "Popped out window's explicit destroy" affordance
- Detached-grid window title bar text and icon

### Deferred Ideas (OUT OF SCOPE)

- Side-by-side layout, mode-toggle (video XOR mixer), resizable splitter, hover-reveal popout icons, right-click context menu for popout/detach, double-click to pop out, tile add/remove animation, audio mute/solo on tile, VU meter on tile, color-coded sync state on username strip, decode-error badge on tile, full restore (auto-reopen popouts on DAW reload), server-scoped popout keys, empty-tile slots for non-broadcasting peers, per-tile audio controls integration with mixer, keyboard shortcuts, multi-camera tile (PIP within self-tile), detached-grid window with its own controls.

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| **DISP-01** | User sees a per-user video tile grid inside the plugin/standalone main view | `VideoGridBand` (in-main-view band) consumes `JamWideRemoteFrameDistributor::Subscription` per peer in `cachedUsers`; layout via `computeGridLayout(N, W, H)`. Editor inserts band in `resized()` between `sessionInfoStrip` and `channelStripArea`. Plan 22-01 owns this. |
| **DISP-02** | User can pop out an individual peer's video into a separate `juce::DocumentWindow` (multi-monitor friendly) | `RemotePeerPopoutWindow` = direct analog of `CameraPreviewWindow`. Each window holds its own `RemotePeerTile` subscribed independently (Phase 21 D-06 multi-listener fan-out). `Desktop::getInstance().getDisplays()` used for multi-monitor placement clamp. Plan 22-02 owns this. |
| **DISP-03** | User can have grid and popouts active simultaneously; popouts survive grid toggling | The same peer's `PeerVideoSink` accepts `addListener()` once per surface (grid tile + popout tile). Toggling the band only mounts/unmounts the grid tile's `Subscription`; the popout's `Subscription` is untouched. Phase 21 D-06 expressly designed for this. Plans 22-01 + 22-02 jointly own this. |
| **DISP-04** | User can toggle the grid view on/off without disconnecting from the NINJAM session | Toggle is a pure UI operation (sets `gridVisible_ = false` + `resized()`). Zero touches to `NJClient`, distributor sinks, or audio-path code. Plan 22-01 owns this. |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | UI framework — `juce::Component`, `juce::DocumentWindow`, `juce::FlexBox`, `juce::Timer`, `juce::AsyncUpdater`, `juce::Image`, `juce::Desktop::getDisplays()` | [VERIFIED: `libs/juce/CHANGE_LIST.md:1 — "Version 8.0.12"`] In-tree vendored at `libs/juce/`. Already linked by every existing JamWide JUCE target. Same version as Phase 19 and Phase 21. |
| Phase 21 `JamWideRemoteFrameDistributor` | shipped (Plan 21-03) | Per-peer `Subscription` RAII handle from `subscribeToPeer(username, chidx, onRepaint) → Subscription` | [VERIFIED: `juce/video/distributor/JamWideRemoteFrameDistributor.h:140-152`] Phase 22 calls this from each `RemotePeerTile` constructor. |
| Phase 21 `PeerVideoSink` | shipped (Plan 21-03) | Double-buffered `juce::Image` + atomic status fields | [VERIFIED: `juce/video/distributor/PeerVideoSink.h:62-79`] `bufferLock` for image swap, `first_frame_seen` / `hold_count` / `synced` for overlay decisions. |
| Phase 19 `JamWideFrameDistributor` | shipped (Plan 19-01) | Self-tile callback-style `Subscriber*` API (D-08) | [VERIFIED: `juce/video/native/JamWideFrameDistributor.h:33-110`] `SelfVideoTile` inherits `Subscriber`, registers with `registerSubscriber(this) → Subscription`. |
| Phase 19 `CameraPreviewWindow` | shipped (Plan 19-02) | Direct template for `RemotePeerPopoutWindow` + `DetachedGridWindow` | [VERIFIED: `juce/video/native/CameraPreviewWindow.h:17-52` + `.cpp:6-86`] DocumentWindow + LookAndFeel + ComponentListener + closeButtonPressed-hides + onBoundsChanged-callback. |
| Phase 19 `CameraPreviewTile` | shipped (Plan 19-02) | Direct template for `SelfVideoTile` / `RemotePeerTile` (MEMBER-ORDER CONTRACT) | [VERIFIED: `juce/video/native/CameraPreviewTile.h:30-70`] subscription as LAST declared member; destructor ordering prevents UAF. |
| `juce::FlexBox` | JUCE 8.0.12 | Adaptive N×M grid layout (D-06) | [VERIFIED: `libs/juce/modules/juce_gui_basics/layout/juce_FlexBox.h:51-130`] `flexDirection = Direction::row` + `flexWrap = Wrap::wrap` + `alignItems = AlignItems::stretch` is the CSS-flexbox-equivalent for fill-width row-wrapping grid. **However**, see §Architecture Patterns Pattern 2 for the recommendation against FlexBox for this specific case. |

**Installation:** No external packages. All dependencies are in-tree.

**Version verification:** [VERIFIED: `libs/juce/CHANGE_LIST.md` line 5 shows "Version 8.0.12"]. JUCE 8.0.12 ships `juce::FlexBox`, `juce::Desktop::getDisplays()`, `juce::ComponentBoundsConstrainer::setFixedAspectRatio` / `setSizeLimits`, and `juce::DocumentWindow::setUsingNativeTitleBar` — all primitives Phase 22 needs.

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `juce::ComponentListener` | JUCE 8.0.12 | Filter `componentMovedOrResized` for the window itself (vs content child) | Mirror of `CameraPreviewWindow::componentMovedOrResized` filter pattern (`if (&which != this) return;`). |
| `juce::ComponentBoundsConstrainer` | JUCE 8.0.12 | `setFixedAspectRatio(4.0/3.0)` + `setSizeLimits(240, 180, 2560, 1920)` on popouts | Per-peer popouts mirror Phase 19 D-07 verbatim. Detached-grid window does NOT aspect-lock (it contains N tiles, not 1). |
| `juce::Desktop::getDisplays()` | JUCE 8.0.12 | Multi-monitor placement clamp on popout/detached-grid reopen | `getDisplays().getRectangleList(/*userAreasOnly*/ true)` returns the union of all monitor user-areas. Test saved bounds for intersection with any display; if none, fall back. |
| `juce::AsyncUpdater` | JUCE 8.0.12 | Tile repaint coalescing (already invoked by `PeerVideoSink::handleAsyncUpdate` listener cb) | Each `RemotePeerTile` inherits `AsyncUpdater` like `CameraPreviewTile`; `onRepaint()` callback calls `triggerAsyncUpdate()` → `handleAsyncUpdate()` reads sink + repaints. |
| `juce::Timer` (30Hz) | JUCE 8.0.12 | Sink poll for D-13 add/remove notification + self-broadcast atomic poll | `startTimerHz(30)` in `VideoGridBand`. Matches existing `ChannelStripArea::timerCallback` cadence. |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `juce::FlexBox` for grid layout | Hand-rolled column-count math in `resized()` | **HAND-ROLLED RECOMMENDED.** FlexBox is great for fluid 1D layouts but gives weak control over 4:3 aspect-preservation across multiple rows when fill-width is the goal — you end up computing `flexBasis` per item from the same column-count math you'd write directly. Hand-rolling `computeGridLayout(N, W, H)` is ~30 LOC, fully unit-testable, and matches `ChannelStripArea`'s existing hand-rolled `resized()` pattern (`/Users/cell/dev/JamWide/juce/ui/ChannelStripArea.cpp:550-700`). See Architecture Patterns Pattern 2 below for the recommended algorithm. |
| Hand-rolled per-window subscription | Reuse `JamWideRemoteFrameDistributor::Subscription` directly | **REUSE RAII RECOMMENDED.** Phase 21's `Subscription` is the closure-of-record for in-flight callback protection. Three separate subscriptions per peer (grid tile + detached-grid tile + popout tile) is the explicit design — `PeerVideoSink::addListener` returns independent ids per call and `removeListener` blocks for in-flight. The cost is ~24 bytes per subscription; the safety win is enormous. |
| Timer-poll sink-map (D-13 Option a) | Distributor event surface (D-13 Option b) | **TIMER-POLL RECOMMENDED.** Option (b) cleaner architecturally but requires touching Phase 21's now-closed distributor surface (which would re-open `21-RESEARCH.md` review). Option (a) reuses the 30Hz cadence the codebase already enforces. At N ≤ 16 cached users, the per-tick cost of `findSink` (locked unordered_map lookup) is ~1µs per peer — negligible. |
| `Map<String, Rectangle>` as flat `popoutBounds_<username>_X/Y/W/H` sibling properties | Structured `<video>` child node with `<popout name="dave" x=... y=... w=... h=.../>` entries | **STRUCTURED RECOMMENDED.** Phase 19 D-25 used flat siblings because there were exactly 4 camera popout fields. Phase 22's `Map<String,Rectangle>` is variadic — usernames are unknown at compile time. Flat siblings would need a fragile naming scheme (`popoutBounds_<username>_X`). Structured XML is the JUCE-idiomatic answer (`ValueTree::addChild`). |
| Separate `GridBand` editor member + manual `addChildComponent` | `JamWideJuceEditor` owns the band as a `unique_ptr` and adds via `setVisible` | **Direct member recommended.** Mirror of how `ChannelStripArea`, `SessionInfoStrip`, etc. are direct members. The visibility toggle is via `gridBand.setVisible(true/false)` + a re-`resized()`. |

## Package Legitimacy Audit

> **Not applicable.** Phase 22 installs zero external packages. All dependencies (JUCE 8.0.12, Phase 19 substrate, Phase 21 substrate) are in-tree at `/Users/cell/dev/JamWide/libs/juce/`, `/Users/cell/dev/JamWide/juce/video/native/`, `/Users/cell/dev/JamWide/juce/video/distributor/`.

## Architecture Patterns

### System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  JamWideJuceProcessor (existing — Phase 21 wires distributors)              │
│  ├─ frameDistributor (Phase 19, self camera)                                │
│  ├─ remoteFrameDistributor (Phase 21, per-peer decoded frames)              │
│  │  └─ sinks: Map<(username, chidx), PeerVideoSink>                         │
│  │            ├─ image_front / image_back / bufferLock (Phase 21 D-01/08)   │
│  │            ├─ atomic generation / first_frame_seen / hold_count / synced │
│  │            └─ addListener(cb) → id ; removeListener(id) ; AsyncUpdater   │
│  ├─ cachedUsers (roster — message-thread access via cachedUsersMutex)       │
│  ├─ connectionBar.getCameraIsBroadcasting() (D-07 self-tile gate)           │
│  └─ getStateInformation / setStateInformation (v4→v5 bump per D-19)         │
└────────────────────────────────────────┬────────────────────────────────────┘
                                         │ getRemoteFrameDistributor()
                                         │ getFrameDistributor()
                                         │ cachedUsers reference
                                         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  JamWideJuceEditor (existing — Phase 22 augments)                           │
│  ├─ resized() — INSERT band between sessionInfoStrip and channelStripArea  │
│  ├─ timerCallback() at 20Hz — auto-open-on-first-frame latch (D-05)         │
│  ├─ NEW: VideoGridBand gridBand                                             │
│  ├─ NEW: unique_ptr<DetachedGridWindow> detachedGrid (singleton, lazy)      │
│  ├─ NEW: unordered_map<String, unique_ptr<RemotePeerPopoutWindow>>          │
│  │       remotePopouts (per-peer, lazy)                                     │
│  ├─ Self-popout reuses existing previewWindow_ (D-09)                       │
│  └─ Bring-back controllers: bringBackRemotePopout(name) / reattachGrid()    │
└────────────────────────────────────────┬────────────────────────────────────┘
                                         │ subscribeToPeer per peer
                                         │ Timer poll findSink per peer
                                         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  VideoGridBand (NEW — juce/ui/video/VideoGridBand.{h,cpp})                  │
│  ├─ Header strip: "↗ ×" buttons (detach / close band)                       │
│  ├─ Optional SelfVideoTile (first slot when broadcasting per D-07)          │
│  ├─ Per-peer RemotePeerTile in flow layout                                  │
│  ├─ resized() — computeGridLayout(N, W, H) → tile bounds                    │
│  ├─ timerCallback() at 30Hz — sink-poll attach/detach (D-13 option a)       │
│  │                          — observe self-broadcast atomic (D-07)          │
│  └─ Draggable bottom resizer for band-height adjustment (D-10)              │
└─────────────────────────────────────────────────────────────────────────────┘
            │ subscribeToPeer per tile           │ subscribe via registerSubscriber
            │ (Phase 21 distributor, RAII)       │ (Phase 19 distributor, callback)
            ▼                                    ▼
┌──────────────────────────────────────┐  ┌──────────────────────────────────┐
│  RemotePeerTile (NEW)                │  │  SelfVideoTile (NEW)             │
│  - juce::Component + AsyncUpdater    │  │  - juce::Component + AsyncUpdater│
│  - holds PeerVideoSink* (raw, non-  │  │  - inherits JamWideFrameDist::   │
│    owning; sink owned by distributor)│  │    Subscriber                    │
│  - paint(): bufferLock snapshot      │  │  - paint(): currentFrame snap    │
│    image_front; read atomic status   │  │  - onFrame(): copy image+        │
│    fields; render overlays           │  │    triggerAsyncUpdate            │
│  - "↗" button → open popout          │  │  - "↗" button → re-show           │
│  - MEMBER-ORDER CONTRACT:            │  │    Phase 19 previewWindow_       │
│    Subscription as LAST member       │  │  - MEMBER-ORDER CONTRACT:        │
└──────────────────────────────────────┘  │    Subscription as LAST member   │
                                          └──────────────────────────────────┘

When user clicks "↗" on a peer tile:                When user clicks band "↗":
┌────────────────────────────────────┐               ┌────────────────────────────────────┐
│  RemotePeerPopoutWindow (NEW)      │               │  DetachedGridWindow (NEW)          │
│  - juce::DocumentWindow            │               │  - juce::DocumentWindow            │
│  - LookAndFeel + ComponentListener │               │  - LookAndFeel + ComponentListener │
│  - holds its own RemotePeerTile    │               │  - holds its own VideoGridBand     │
│    (independent Subscription)      │               │    instance (independent           │
│  - closeButtonPressed → hide       │               │    subscriptions per peer)         │
│  - 4:3 aspect-locked constrainer   │               │  - closeButtonPressed → hide OR    │
│  - bounds → editor.setRemotePopout │               │    destroy depending on D-18       │
│    Bounds(username, rect)          │               │  - bounds → editor.setDetached     │
│                                    │               │    GridBounds(rect)                │
└────────────────────────────────────┘               └────────────────────────────────────┘
                                                              │
       In the band's slot for that peer:                      │ When detached-grid active:
       ┌────────────────────────────┐                         ▼
       │  PopoutPlaceholderCard     │            ┌─────────────────────────────────┐
       │  "Popped out →" + click    │            │  DetachedGridPlaceholderCard    │
       │  to bring back             │            │  "Grid is in detached window →" │
       └────────────────────────────┘            │  + click to bring back          │
                                                 └─────────────────────────────────┘
```

**Data flow per repaint cycle (one peer):**
1. Decoder thread (Phase 21) swaps `image_back ↔ image_front` under `bufferLock`, bumps `generation`, calls `sink->triggerAsyncUpdate()`.
2. JUCE message-thread queue: `PeerVideoSink::handleAsyncUpdate` runs, snapshots listener list under `listenerLock_`, releases lock, fans out callbacks.
3. Each listener callback is `[tile]{ tile->triggerAsyncUpdate(); }` (registered in tile ctor; tile inherits `AsyncUpdater` for coalescing).
4. Tile's `handleAsyncUpdate` calls `repaint()`.
5. JUCE message-thread paint: `paint()` takes a brief `juce::ScopedLock` on `sink->bufferLock`, copies refcount of `sink->image_front` to a local, releases lock, reads atomic status fields, paints local image + overlays.

**Component Responsibilities:**

| Component | File | Purpose |
|-----------|------|---------|
| `VideoGridBand` | `juce/ui/video/VideoGridBand.{h,cpp}` | Container holding self-tile + per-peer tiles in flow layout; owns band header (↗/× buttons) and bottom resizer; runs 30Hz Timer for sink-poll + self-broadcast observation |
| `VideoTileBase` | `juce/ui/video/VideoTileBase.{h,cpp}` | Shared base — 4:3 letterbox paint, username strip, status overlays (`video starting…` / `syncing…`), `↗` button paint+hit-test |
| `SelfVideoTile` | `juce/ui/video/SelfVideoTile.{h,cpp}` | Inherits `VideoTileBase` + `JamWideFrameDistributor::Subscriber`; onFrame copies image + triggerAsyncUpdate |
| `RemotePeerTile` | `juce/ui/video/RemotePeerTile.{h,cpp}` | Inherits `VideoTileBase`; subscribes to `JamWideRemoteFrameDistributor::subscribeToPeer`; paints from `PeerVideoSink::image_front` |
| `RemotePeerPopoutWindow` | `juce/ui/video/RemotePeerPopoutWindow.{h,cpp}` | DocumentWindow wrapping a `RemotePeerTile`; 4:3 aspect-locked; closeButtonPressed hides (D-17) |
| `DetachedGridWindow` | `juce/ui/video/DetachedGridWindow.{h,cpp}` | DocumentWindow wrapping a `VideoGridBand`; NOT aspect-locked; min-size 320×240 |
| `PopoutPlaceholderCard` | `juce/ui/video/PopoutPlaceholderCard.{h,cpp}` | "Popped out →" card with bring-back click affordance |
| `DetachedGridPlaceholderCard` | `juce/ui/video/DetachedGridPlaceholderCard.{h,cpp}` | "Grid is in detached window →" card with bring-back click affordance |
| `ConnectionBar::GridButton` | `juce/ui/ConnectionBar.{h,cpp}` (file-local class, mirror of `CameraButton` pattern) | Toggle band visibility; reflects current state in label |

### Recommended Project Structure

```
juce/ui/video/                     # NEW directory — Phase 22 home
├── VideoGridBand.h                # In-main-view band container
├── VideoGridBand.cpp
├── VideoTileBase.h                # Shared tile base (chrome + overlays)
├── VideoTileBase.cpp
├── SelfVideoTile.h                # Self camera, Phase 19 distributor
├── SelfVideoTile.cpp
├── RemotePeerTile.h               # Remote peer, Phase 21 distributor
├── RemotePeerTile.cpp
├── RemotePeerPopoutWindow.h       # Per-peer popout DocumentWindow
├── RemotePeerPopoutWindow.cpp
├── DetachedGridWindow.h           # Whole-grid detached DocumentWindow
├── DetachedGridWindow.cpp
├── PopoutPlaceholderCard.h        # Per-peer "Popped out →" placeholder
├── PopoutPlaceholderCard.cpp
├── DetachedGridPlaceholderCard.h  # Whole-grid "Detached →" placeholder
├── DetachedGridPlaceholderCard.cpp
└── computeGridLayout.h            # Pure-C++ layout helper (unit-testable)

juce/ui/ConnectionBar.{h,cpp}      # MODIFY — add GridButton next to CameraButton
juce/JamWideJuceEditor.{h,cpp}     # MODIFY — add gridBand member + popout maps + bring-back controllers
juce/JamWideJuceProcessor.{h,cpp}  # MODIFY — bump v4→v5; add <video> ValueTree subtree; getters/setters for grid+popout state

tests/test_compute_grid_layout.cpp # NEW — pure-C++ unit test for layout math
tests/test_remote_peer_tile_lifetime.cpp # NEW — MEMBER-ORDER CONTRACT under in-flight callbacks
tests/test_plugin_state_v4_v5.cpp  # NEW — v4→v5 round-trip + clamping
tests/uat/phase-22-grid-popout-uat-procedure.md  # NEW — UAT cells
tests/uat/phase-22-grid-popout-uat-report.md     # NEW — UAT report template
```

### Pattern 1: MEMBER-ORDER CONTRACT for tile classes (D-08, mirrors Phase 19)

**What:** Declare `subscription_` as the LAST member of every tile class so the destructor blocks on in-flight callbacks before mutex/frame members are torn down.

**When to use:** Both `SelfVideoTile` and `RemotePeerTile` MUST follow this.

**Example (mirror of `juce/video/native/CameraPreviewTile.h:50-69`):**

```cpp
class RemotePeerTile : public juce::Component,
                       public juce::AsyncUpdater {
public:
    RemotePeerTile(jamwide::JamWideRemoteFrameDistributor& dist,
                   const juce::String& username);
    ~RemotePeerTile() override;
    void handleAsyncUpdate() override;
    void paint(juce::Graphics& g) override;

private:
    jamwide::JamWideRemoteFrameDistributor& distributor_;
    const juce::String                       username_;
    jamwide::PeerVideoSink*                  sink_ = nullptr;  // non-owning; distributor owns

    // Username strip + ↗ button state — message-thread-only
    bool hovering_ = false;

    // MEMBER-ORDER CONTRACT: subscription_ MUST stay as the LAST member.
    // Its destructor (~Subscription → unsubscribe_) blocks any in-flight
    // PeerVideoSink::handleAsyncUpdate listener callback before this
    // destructor returns, which means the sink_ pointer + member fields
    // above are still alive while the message thread is unwinding through
    // the listener fan-out iteration.
    jamwide::JamWideRemoteFrameDistributor::Subscription subscription_;
};
```

```cpp
RemotePeerTile::RemotePeerTile(JamWideRemoteFrameDistributor& d, const juce::String& u)
    : distributor_(d), username_(u)
{
    sink_ = distributor_.findSink(username_.toRawUTF8(), /*chidx*/1);
    // subscribeToPeer's callback fires on the message thread per
    // PeerVideoSink::handleAsyncUpdate; safe to triggerAsyncUpdate from there.
    subscription_ = distributor_.subscribeToPeer(
        username_.toRawUTF8(), /*chidx*/1,
        [this]() { triggerAsyncUpdate(); });
}
```

### Pattern 2: Hand-rolled `computeGridLayout` (D-06 — recommended over FlexBox)

**What:** Pure-C++ function that takes peer count + available width/height + min-tile + max-cols and returns `{cols, rows, tileW, tileH, marginX, marginY}`.

**When to use:** Called from `VideoGridBand::resized()` to position child tiles. Unit-testable in isolation.

**Algorithm:**

```cpp
// juce/ui/video/computeGridLayout.h
namespace jamwide {

struct GridLayoutResult {
    int cols;        // Number of columns
    int rows;        // Number of rows (may include partial last row)
    int tileW;       // Width per tile (px)
    int tileH;       // Height per tile (px, == tileW * 3/4 for 4:3 aspect)
    int marginX;     // Horizontal padding to centre row when row is not full-width
    int marginY;     // Vertical padding to centre block when grid is shorter than band
    bool needsScroll; // True when computed tileW < kMinTileW; caller should wrap in Viewport
};

GridLayoutResult computeGridLayout(int n,            // visible-tile count
                                   int bandW,
                                   int bandH,
                                   int spacing = 6,
                                   int minTileW = 120,
                                   int maxCols = 4);

}  // namespace jamwide

// computeGridLayout.cpp
GridLayoutResult computeGridLayout(int n, int bandW, int bandH, int spacing, int minTileW, int maxCols)
{
    GridLayoutResult r{};
    if (n <= 0 || bandW <= 0 || bandH <= 0) return r;

    // Try each column count from 1..maxCols; pick the one that maximizes
    // tile width while fitting all tiles in band height.
    int bestCols = 1;
    int bestTileW = 0;
    for (int cols = 1; cols <= std::min(n, maxCols); ++cols) {
        const int rows = (n + cols - 1) / cols;
        const int candidateW = (bandW - spacing * (cols + 1)) / cols;
        const int candidateH = candidateW * 3 / 4;  // 4:3 aspect
        const int totalH = rows * candidateH + spacing * (rows + 1);
        if (totalH <= bandH && candidateW > bestTileW) {
            bestCols = cols;
            bestTileW = candidateW;
        }
    }
    // Fall back to last try if nothing fit.
    if (bestTileW == 0) {
        bestCols = std::min(n, maxCols);
        bestTileW = (bandW - spacing * (bestCols + 1)) / bestCols;
    }

    r.cols = bestCols;
    r.rows = (n + r.cols - 1) / r.cols;
    r.tileW = bestTileW;
    r.tileH = bestTileW * 3 / 4;
    r.needsScroll = (r.tileW < minTileW);
    // Centre the grid block within band.
    const int gridW = r.cols * r.tileW + spacing * (r.cols + 1);
    const int gridH = r.rows * r.tileH + spacing * (r.rows + 1);
    r.marginX = std::max(0, (bandW - gridW) / 2);
    r.marginY = std::max(0, (bandH - gridH) / 2);
    return r;
}
```

**Consumed by `VideoGridBand::resized()`:**

```cpp
void VideoGridBand::resized() {
    auto bounds = getLocalBounds();
    auto headerArea = bounds.removeFromTop(24);       // "↗ ×" header
    auto resizerArea = bounds.removeFromBottom(4);    // Draggable bottom resizer
    auto tileArea = bounds;                           // Remaining area for tiles

    // Compose visible-tile list (self if broadcasting + peers with active sinks).
    std::vector<juce::Component*> visibleTiles = collectVisibleTiles_();

    auto layout = computeGridLayout(static_cast<int>(visibleTiles.size()),
                                    tileArea.getWidth(), tileArea.getHeight());

    // Position each tile in row-major order.
    for (size_t i = 0; i < visibleTiles.size(); ++i) {
        const int col = static_cast<int>(i) % layout.cols;
        const int row = static_cast<int>(i) / layout.cols;
        const int x = tileArea.getX() + layout.marginX
                    + layout.spacing + col * (layout.tileW + layout.spacing);
        const int y = tileArea.getY() + layout.marginY
                    + layout.spacing + row * (layout.tileH + layout.spacing);
        visibleTiles[i]->setBounds(x, y, layout.tileW, layout.tileH);
    }
}
```

### Pattern 3: Multi-monitor placement clamp (Claude's Discretion — stale popout bounds)

**What:** Before showing a popout (or detached-grid window) at restored bounds, intersect against `Desktop::getInstance().getDisplays().getRectangleList(/*userAreasOnly*/ true)`. If no intersection, fall back to a centered position on the primary display.

**When to use:** `RemotePeerPopoutWindow::RemotePeerPopoutWindow` ctor + `DetachedGridWindow::DetachedGridWindow` ctor when constructed with persisted bounds.

**Example:**

```cpp
// juce/ui/video/RemotePeerPopoutWindow.cpp
RemotePeerPopoutWindow::RemotePeerPopoutWindow(JamWideRemoteFrameDistributor& dist,
                                                const juce::String& username,
                                                juce::LookAndFeel* lookAndFeel,
                                                juce::Rectangle<int> savedBounds)
    : juce::DocumentWindow("JamWide — " + username,
                           juce::Colour(JamWideLookAndFeel::kSurfaceStrip),
                           juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(false);
    if (lookAndFeel) setLookAndFeel(lookAndFeel);

    setResizable(true, true);
    if (auto* constrainer = getConstrainer()) {
        constrainer->setFixedAspectRatio(4.0 / 3.0);
        constrainer->setSizeLimits(240, 180, 2560, 1920);
    }

    auto tile = std::make_unique<RemotePeerTile>(dist, username);
    tilePtr_ = tile.get();
    setContentOwned(tile.release(), /*resizeToFit*/ true);

    // Multi-monitor clamp: if savedBounds does not intersect any monitor,
    // fall back to centered on primary.
    auto allDisplays = juce::Desktop::getInstance().getDisplays().getRectangleList(true);
    juce::Rectangle<int> bounds = savedBounds;
    if (!allDisplays.intersectsRectangle(bounds)) {
        if (auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
            const auto userArea = primary->userArea;
            bounds = juce::Rectangle<int>(userArea.getCentreX() - 160,
                                          userArea.getCentreY() - 120,
                                          320, 240);
        } else {
            bounds = juce::Rectangle<int>(100, 100, 320, 240);
        }
    }
    setBounds(bounds);

    setVisible(false);  // D-14 — popouts always start hidden
    addComponentListener(this);
}
```

### Pattern 4: Self-broadcast atomic observation for D-07 (no race with audio path)

**What:** `SelfVideoTile` is mounted/unmounted by the `VideoGridBand`'s 30Hz Timer based on `connectionBar.getCameraIsBroadcasting()`. The atomic itself is set by the editor's broadcast-toggle lambda (already exists, line 223-226), so no new atomic is required.

**When to use:** `VideoGridBand::timerCallback` on every tick — check the boolean, compare to last-known state, mount/unmount on edge transition.

**Example:**

```cpp
// juce/ui/video/VideoGridBand.cpp
void VideoGridBand::timerCallback() {
    // 1. Self-broadcast observation (D-07).
    const bool nowBroadcasting = connectionBar_.getCameraIsBroadcasting();
    if (nowBroadcasting != selfTileVisible_) {
        selfTileVisible_ = nowBroadcasting;
        if (nowBroadcasting && !selfTile_) {
            selfTile_ = std::make_unique<SelfVideoTile>(*selfDistributor_);
            addAndMakeVisible(*selfTile_);
        } else if (!nowBroadcasting && selfTile_) {
            removeChildComponent(selfTile_.get());
            selfTile_.reset();
        }
        resized();
    }

    // 2. Per-peer sink poll (D-13 Option a).
    std::vector<juce::String> currentPeers;
    {
        std::lock_guard<std::mutex> lk(processorRef_.cachedUsersMutex);
        for (const auto& u : processorRef_.cachedUsers) {
            juce::String name(u.name);
            if (isBot(name)) continue;
            currentPeers.push_back(stripAtSuffix(name));
        }
    }

    bool layoutChanged = false;
    // Detect new peers with live sinks → mount tiles.
    for (const auto& name : currentPeers) {
        if (peerTiles_.find(name) != peerTiles_.end()) continue;
        if (auto* sink = remoteDistributor_->findSink(name.toRawUTF8(), /*chidx*/1)) {
            // Only mount when sink actually exists (D-12 — only broadcasting peers).
            (void)sink;
            auto tile = std::make_unique<RemotePeerTile>(*remoteDistributor_, name);
            addAndMakeVisible(*tile);
            peerTiles_[name] = std::move(tile);
            layoutChanged = true;
        }
    }
    // Detect departed peers OR peers without sinks → unmount tiles.
    for (auto it = peerTiles_.begin(); it != peerTiles_.end();) {
        const auto& name = it->first;
        const bool present = std::find(currentPeers.begin(), currentPeers.end(), name) != currentPeers.end();
        const bool hasSink = remoteDistributor_->findSink(name.toRawUTF8(), /*chidx*/1) != nullptr;
        if (!present || !hasSink) {
            removeChildComponent(it->second.get());
            it = peerTiles_.erase(it);
            layoutChanged = true;
        } else {
            ++it;
        }
    }

    if (layoutChanged) resized();
}
```

### Pattern 5: Auto-open-on-first-frame latch (D-05)

**What:** A `std::atomic<bool> autoOpenLatchFired_{false}` on the editor (or `VideoGridBand`). The editor's existing 20Hz `timerCallback` walks `cachedUsers`, calls `findSink(name, chidx=1)` per peer; on first sink whose `first_frame_seen.load() == true` AND `!autoOpenLatchFired_.exchange(true)`, calls `setGridBandVisible(true)`. Once fired the latch never resets within the session, so explicit user close of the band is not undone.

**When to use:** Editor's `timerCallback`, additive to existing work.

**Example:**

```cpp
// In JamWideJuceEditor::timerCallback (additive after existing logic)
if (!autoOpenLatchFired_.load(std::memory_order_acquire)) {
    auto* dist = processorRef.getRemoteFrameDistributor();
    if (dist) {
        std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
        for (const auto& u : processorRef.cachedUsers) {
            if (isBot(juce::String(u.name))) continue;
            if (auto* sink = dist->findSink(u.name, /*chidx*/1)) {
                if (sink->first_frame_seen.load(std::memory_order_acquire)) {
                    if (!autoOpenLatchFired_.exchange(true)) {
                        setGridBandVisible(true);
                    }
                    break;
                }
            }
        }
    }
}
```

**Reset on disconnect:** Latch is implicitly reset on plugin reload (atomic value starts false). On NJClient disconnect, decide whether to reset — recommendation: DO NOT reset within a session; the user explicitly opened it once, no reason to re-prompt.

### Pattern 6: State persistence v4→v5 with structured `<video>` subtree (D-19)

**What:** Bump `currentStateVersion = 4` → `5`. Add a `<video>` child ValueTree node to the root state. Inside `<video>`, persist `gridVisible`, `gridBandHeight`, `detachedGridX/Y/W/H` as properties, and per-peer popout bounds as child `<popout>` nodes.

**When to use:** `JamWideJuceProcessor::getStateInformation` (write) + `setStateInformation` STEP 6 NEW (read).

**Example:**

```cpp
// JamWideJuceProcessor.h
static constexpr int currentStateVersion = 5;  // BUMP from 4

// New API surface for editor → processor state mutation
bool getGridVisible() const noexcept;
void setGridVisible(bool v) noexcept;
int  getGridBandHeight() const noexcept;
void setGridBandHeight(int h) noexcept;
juce::Rectangle<int> getDetachedGridBounds() const;
void setDetachedGridBounds(juce::Rectangle<int> r);

// Per-peer popout bounds — map<username, Rectangle>
juce::Rectangle<int> getRemotePopoutBounds(const juce::String& username) const;
void setRemotePopoutBounds(const juce::String& username, juce::Rectangle<int> r);
std::vector<std::pair<juce::String, juce::Rectangle<int>>> getAllRemotePopoutBounds() const;
void clearRemotePopoutBoundsNotIn(const std::set<juce::String>& currentUsernames);  // optional GC

// Backing storage
std::atomic<bool>           gridVisible_{false};
std::atomic<int>            gridBandHeight_{280};  // D-10 default
mutable std::mutex          detachedGridMu_;
juce::Rectangle<int>        detachedGridBounds_{200, 200, 800, 450};
mutable std::mutex          popoutBoundsMu_;
std::unordered_map<juce::String, juce::Rectangle<int>> popoutBounds_;
```

```cpp
// getStateInformation — APPEND after existing camera flat properties
auto video = juce::ValueTree("video");
video.setProperty("gridVisible",        getGridVisible(),             nullptr);
video.setProperty("gridBandHeight",     getGridBandHeight(),          nullptr);
auto dg = getDetachedGridBounds();
video.setProperty("detachedGridX",      dg.getX(),                    nullptr);
video.setProperty("detachedGridY",      dg.getY(),                    nullptr);
video.setProperty("detachedGridWidth",  dg.getWidth(),                nullptr);
video.setProperty("detachedGridHeight", dg.getHeight(),               nullptr);
for (const auto& [name, rect] : getAllRemotePopoutBounds()) {
    auto p = juce::ValueTree("popout");
    p.setProperty("name", name, nullptr);
    p.setProperty("x", rect.getX(), nullptr);
    p.setProperty("y", rect.getY(), nullptr);
    p.setProperty("w", rect.getWidth(), nullptr);
    p.setProperty("h", rect.getHeight(), nullptr);
    video.addChild(p, -1, nullptr);
}
state.addChild(video, -1, nullptr);
```

```cpp
// setStateInformation — APPEND STEP 6 after existing STEP 5 camera block
{
    auto video = tree.getChildWithName("video");
    if (video.isValid()) {
        setGridVisible((bool) video.getProperty("gridVisible", false));
        setGridBandHeight(juce::jlimit(140, 800,
            (int) video.getProperty("gridBandHeight", 280)));
        const int dgX = juce::jlimit(-10000, 10000, (int) video.getProperty("detachedGridX", 200));
        const int dgY = juce::jlimit(-10000, 10000, (int) video.getProperty("detachedGridY", 200));
        const int dgW = juce::jlimit(320, 4096, (int) video.getProperty("detachedGridWidth", 800));
        const int dgH = juce::jlimit(240, 2304, (int) video.getProperty("detachedGridHeight", 450));
        setDetachedGridBounds({dgX, dgY, dgW, dgH});
        for (int i = 0; i < video.getNumChildren(); ++i) {
            auto p = video.getChild(i);
            if (!p.hasType("popout")) continue;
            juce::String name = p.getProperty("name", "").toString();
            if (name.isEmpty() || name.length() > 256) continue;  // T-22-SP defense
            const int x = juce::jlimit(-10000, 10000, (int) p.getProperty("x", 100));
            const int y = juce::jlimit(-10000, 10000, (int) p.getProperty("y", 100));
            const int w = juce::jlimit(240, 2560, (int) p.getProperty("w", 320));
            const int h = juce::jlimit(180, 1920, (int) p.getProperty("h", 240));
            setRemotePopoutBounds(name, {x, y, w, h});
        }
    }
    // No <video> subtree → use defaults (v4 → v5 graceful upgrade).
}
```

### Anti-Patterns to Avoid

- **Putting sink-poll on the audio thread.** Phase 22 ships ZERO audio-path code per DISP-04. The 30Hz Timer pattern is correct; the audio thread already has Phase 21's `m_video_recv_cs` envelope and Phase 22 must not extend it.
- **Recomputing grid layout in `paint()` instead of `resized()`.** Layout is a function of band dimensions + peer count. Both change rarely (resize event or peer mount/unmount); `paint()` runs every coalesced repaint (~30Hz active session). The lock-guarded `cachedUsers` read alone would be wasteful per-paint.
- **Holding `bufferLock` during tile `paint()`.** Phase 21 D-08 contract: take the lock briefly to snapshot the refcount of `image_front` into a local, RELEASE the lock, THEN paint the local. Holding across `g.drawImage` would block the decoder thread for the entire paint duration.
- **Forgetting MEMBER-ORDER CONTRACT.** A tile with `subscription_` declared BEFORE its mutex/sink-pointer members will UAF on destruction if a callback is in-flight. Phase 19's review explicitly caught this; Phase 22 must not regress it.
- **Auto-restoring popout windows on plugin reload.** D-14 explicitly forbids this. Only bounds persist; the user must explicitly click `↗` to reopen.
- **Re-using `previewWindow_` for a per-peer popout.** Phase 19's preview window is bound to the self-camera state machine; reusing it for remote-peer rendering would race against the camera FallbackListener callbacks. D-09 explicitly creates a NEW popout type for remote peers.
- **Storing `PeerVideoSink*` for longer than the `Subscription` is alive.** The sink pointer is owned by the distributor; if the peer leaves and the sink is destroyed, the cached pointer dangles. RemotePeerTile should re-query `findSink` in `handleAsyncUpdate` if it caches the pointer (or always re-query — the lookup is microseconds).
- **Polling `cachedUsers` without `cachedUsersMutex`.** Run thread mutates `cachedUsers` via `std::move` on roster changes; iterating without the lock can dereference freed memory. `ChannelStripArea::findRemoteIndex` shows the correct pattern (line 348 — `std::lock_guard`).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Per-tile thread for frame fan-out | Per-tile `std::thread` consuming a queue | `juce::AsyncUpdater` (mirror of `CameraPreviewTile`) | AsyncUpdater coalesces multi-trigger to single dispatch and integrates with JUCE message loop cleanly. A bespoke thread would race with the message thread on `paint()`. |
| Custom multi-monitor placement logic | Walk `NSScreen.screens` (macOS) / `EnumDisplayMonitors` (Windows) directly | `juce::Desktop::getInstance().getDisplays().getRectangleList(true)` | JUCE abstracts the platform difference and handles logical-vs-physical px scaling. |
| Custom DocumentWindow chrome | Subclass `juce::ResizableWindow` and paint title bar ourselves | `juce::DocumentWindow::setUsingNativeTitleBar(false) + setLookAndFeel(&jamWideLookAndFeel)` (mirror of `CameraPreviewWindow`) | Phase 19 already validated dark-theme chrome via this pattern. |
| Custom flex/grid layout engine | Hand-roll 2D layout solver | `computeGridLayout(N, W, H)` simple per-column-count comparison (Pattern 2) OR `juce::FlexBox` for the simple case | The hand-rolled version is ~30 LOC and fully unit-testable; FlexBox available for the simpler "wrap row of equal-width items" case. Either is acceptable; do NOT introduce a third-party layout dep. |
| Custom 4:3 letterbox math in tile paint | Compute letterbox bounds manually | `juce::RectanglePlacement::centred` flag on `Graphics::drawImage` | Phase 19's `CameraPreviewTile::paint` already uses `RectanglePlacement::centred` (line 76) — preserves aspect by default. |
| Custom subscription lifecycle for popout | Track `bool isOpen` + manually call `addListener` / `removeListener` | RAII `Subscription` member — destructor handles detach + in-flight wait | The whole point of Phase 21 D-06's listener API design is RAII closure. Manual lifecycle is a UAF generator. |
| Custom XML serialization for `Map<String, Rectangle>` | Encode as flat siblings with name-mangling | `ValueTree` + child `<popout>` nodes with `name`/`x`/`y`/`w`/`h` properties | Direct XML structured access via `getChildWithName`/`addChild`; idiomatic JUCE. |
| Custom resize-drag handling for band-bottom resizer | `mouseDrag` + manual height calculation | `juce::ResizableEdgeComponent` on the band's bottom edge | JUCE-provided primitive specifically for this; computes drag delta + bounds clamping. |

**Key insight:** Phase 22's biggest risk is duplicating Phase 19's `CameraPreviewWindow`/`CameraPreviewTile` template subtly differently. Every new tile class is a near-verbatim mirror of `CameraPreviewTile`; every new window class is a near-verbatim mirror of `CameraPreviewWindow`. Stay close to the templates and the MEMBER-ORDER CONTRACT will hold.

## Runtime State Inventory

> **Not applicable.** Phase 22 is greenfield UI work, not a rename/refactor/migration. No stored data, live service config, or OS-registered state changes. All state is in-process (UI components, `PeerVideoSink` listener vectors). The only persisted state is the v4→v5 plugin state bump — which IS code-mediated and lives entirely within `JamWideJuceProcessor::getStateInformation`/`setStateInformation`.

## Common Pitfalls

### Pitfall 1: Multi-listener fan-out without per-listener AsyncUpdater coalescing

**What goes wrong:** Three surfaces (band tile + detached-grid tile + popout tile) for the same peer each receive the listener callback synchronously inside `PeerVideoSink::handleAsyncUpdate`. If any callback does heavy work (e.g., paint directly), the others starve.

**Why it happens:** PeerVideoSink iterates listeners with a snapshot vector; each cb runs sequentially. If cb is `[this]{ this->paint(g); }` (which would be wrong), the iteration blocks for the paint duration of each surface.

**How to avoid:** Every tile's `onRepaint` callback MUST be just `triggerAsyncUpdate()`. This bumps the AsyncUpdater queue; the actual paint runs on the next message-thread spin, freeing the fan-out iteration.

**Warning signs:** Long `PeerVideoSink::handleAsyncUpdate` durations under Instruments; frame drops on one surface when another resizes.

### Pitfall 2: Editor `resized()` re-entrancy on band-visibility toggle

**What goes wrong:** `setGridBandVisible(true)` → `gridBand.setVisible(true)` → `resized()` → grid band tries to compute layout but its `sink_` pointers may be stale if `cachedUsers` was mutated between the show and the resize.

**Why it happens:** JUCE's `setVisible(true)` causes a layout cascade; if the band's `resized()` queries `cachedUsers` and computes layout, but the cached pointer to a peer's sink was invalidated by a concurrent `removeSink` on the run thread, the painted result is stale until the next 30Hz Timer tick reconciles.

**How to avoid:** Tile pointers held by `VideoGridBand::peerTiles_` are message-thread-owned; `removeSink` only invalidates the `PeerVideoSink*` cached in the tile. Tile's `handleAsyncUpdate` SHOULD re-query `distributor_.findSink(username_, 1)` rather than caching, OR the tile's `Subscription` destructor (called on Timer-driven tile removal) blocks until in-flight `handleAsyncUpdate` returns — same MEMBER-ORDER CONTRACT as Phase 19.

**Warning signs:** Black tile or "video starting…" overlay flashing briefly when a peer leaves; quick paint of last-known frame before tile is removed.

### Pitfall 3: Popout window survives editor destruction → crashes on next paint

**What goes wrong:** DAW closes editor → `JamWideJuceEditor::~JamWideJuceEditor` runs. If a popout is still visible, its tile holds a `Subscription` to the distributor — distributor is owned by processor, lives, but the tile's parent editor is dead.

**Why it happens:** `unique_ptr<RemotePeerPopoutWindow>` is an editor member; its destructor runs as part of the editor dtor cascade. As long as the editor owns the popout, the popout dies with the editor. **But** if the popout's `closeButtonPressed` is the hide path (D-17), the popout stays alive until the editor explicitly destroys it on dtor — which is fine.

**How to avoid:** Editor destructor MUST explicitly clear `remotePopouts.clear()` AND `detachedGrid.reset()` BEFORE the editor's own LookAndFeel teardown (so popouts don't ref a dead LookAndFeel). Mirror of how `CameraPreviewWindow` is cleared in `~JamWideJuceEditor` (currently implicit via `unique_ptr`, but Phase 22's `unordered_map<String, unique_ptr<...>>` may need explicit `clear()` for order safety).

**Warning signs:** Crash on DAW reload of the plugin; `~CameraPreviewWindow`-style stack trace.

### Pitfall 4: `setStateInformation` called twice → duplicate popouts

**What goes wrong:** Some DAWs (Reaper, occasionally Logic) call `setStateInformation` more than once during reload. If the popout bounds restore is naive, the map fills up with duplicate or stale entries.

**Why it happens:** `popoutBounds_` is a map — keyed by username — so duplicate `setRemotePopoutBounds(name, rect)` calls overwrite, not duplicate. The pitfall is in any code that ITERATES the map and creates popout windows; since D-14 explicitly forbids auto-reopen, this isn't a Phase 22 concern unless the planner inadvertently violates D-14.

**How to avoid:** Honor D-14 strictly. Restoring `popoutBounds_` is a map mutation; the windows themselves are NOT re-created.

**Warning signs:** Multiple popout windows appearing on plugin reload despite never having clicked `↗`.

### Pitfall 5: Phase 21 distributor finds sink before tile attaches → first-frame missed

**What goes wrong:** Tile constructed at T=0; subscribes to peer. The first decoded frame arrives at T+10ms but the tile's listener wasn't yet in `listeners_` map when the sink's `triggerAsyncUpdate` fired.

**Why it happens:** Phase 21's `PeerVideoSink::handleAsyncUpdate` snapshots `listeners_` under lock at the moment it runs. If the listener was added after the snapshot but before the next decoded frame, the listener will see the NEXT frame, not the one that just arrived.

**How to avoid:** Tile's `paint()` reads `sink->image_front` lock-free per repaint. Even if the listener missed the trigger, the image is in `image_front` and the FIRST paint will pick it up. As long as the tile gets ANY repaint trigger within ~33ms of the next decoded frame (e.g., the next AsyncUpdater cycle), the image is current. Practically: just always `repaint()` on tile mount.

**Warning signs:** Tile shows "video starting…" overlay for one extra frame interval after mount.

### Pitfall 6: Sink pointer cached in tile becomes dangling after peer leave + rejoin

**What goes wrong:** Tile caches `sink_ = distributor_.findSink(...)`. Peer leaves, `removeSink` deletes the PeerVideoSink. Peer rejoins, `findOrCreateSink` creates a NEW PeerVideoSink. Tile's `sink_` now points to a freed object.

**Why it happens:** Phase 21's `removeSink` calls `unique_ptr<PeerVideoSink>::reset()` after the four-step shutdown protocol. The cached raw pointer dangles.

**How to avoid:** Tile MUST re-query `findSink` in `handleAsyncUpdate` (or in `paint`) rather than relying on the cached pointer. The `Subscription` ensures the LISTENER callback won't dangle, but the SINK ptr separately needs re-resolution. The deferred-listener side-table in Phase 21 distributor handles the rejoin case automatically — the Subscription stays alive across sink destruction; on rejoin the new sink picks up the deferred listener.

**Warning signs:** UAT cell "peer leaves then rejoins" — tile shows black after rejoin until tile is destroyed + re-created by the 30Hz Timer.

### Pitfall 7: ConnectionBar GridButton overlaps right cluster at default 1200px width

**What goes wrong:** Phase 19 already required bumping `kBaseWidth` from 1030→1200 to fit the Camera button + Recheck-permission text. Adding a GridButton (e.g., another 80-100px) may push the total past 1200, recreating the overlap that the May 2026 commit 5250ff1 fixed.

**Why it happens:** Right-cluster width = Fit (36) + Debug (36) + Camera (130) + MIDI dot (44) + OSC dot (44) + Sync (44) + Route (52) + Codec (~80) + gaps (~50) ≈ 510px. Left cluster = ~700px (Logo + server field + username + password + Connect + Browse). Total ≈ 1210px on the 1200px kBaseWidth.

**How to avoid:** Measure carefully. Recommendation: GridButton at ~70-80px wide ("Grid" label, no "(on)" extended text). If overlap recurs, bump kBaseWidth to 1280 or 1300.

**Warning signs:** Right-cluster controls overlap Connect/Browse buttons at default plugin window size (UAT cell 1).

### Pitfall 8: macOS DocumentWindow native title bar override

**What goes wrong:** Per `CameraPreviewWindow.cpp:18-20` comment: "on some macOS builds JUCE forces native title bars regardless of this flag" — `setUsingNativeTitleBar(false)` is a soft preference. The popout's title bar may render with the OS-native chrome instead of the dark VB-Banana LookAndFeel.

**Why it happens:** macOS / JUCE interaction; the JUCE detect-host-bar logic prefers OS-native bars in some embedded plugin contexts.

**How to avoid:** Accept the soft-preference reality. Content area is dark-themed regardless, which is the user-visible portion. Already validated in Phase 19 UAT.

**Warning signs:** Popout title bar appears OS-grey on macOS Logic Pro / Bitwig hosts but dark VB on standalone.

## Code Examples

### Example 1: Sink existence + first-frame check for auto-open

```cpp
// In JamWideJuceEditor::timerCallback (added section)
static_assert(true, "additive to existing timer body");

if (!autoOpenLatchFired_.load(std::memory_order_acquire)) {
    if (auto* dist = processorRef.getRemoteFrameDistributor()) {
        std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
        for (const auto& u : processorRef.cachedUsers) {
            const juce::String name(u.name);
            if (isBot(name)) continue;
            if (auto* sink = dist->findSink(u.name, /*chidx*/1)) {
                if (sink->first_frame_seen.load(std::memory_order_acquire)) {
                    if (!autoOpenLatchFired_.exchange(true)) {
                        setGridBandVisible(true);  // calls processorRef.setGridVisible + resized()
                    }
                    break;
                }
            }
        }
    }
}
```

### Example 2: PopoutPlaceholderCard with bring-back affordance

```cpp
// juce/ui/video/PopoutPlaceholderCard.h
namespace jamwide {

class PopoutPlaceholderCard : public juce::Component {
public:
    PopoutPlaceholderCard() {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setLabel(const juce::String& s) { label_ = s; repaint(); }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(JamWideLookAndFeel::kSurfaceChild));
        g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
        g.drawRect(getLocalBounds(), 1);
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary).withAlpha(0.7f));
        g.setFont(juce::FontOptions(14.0f));
        g.drawFittedText(label_, getLocalBounds(), juce::Justification::centred, 2);
    }
    void mouseDown(const juce::MouseEvent&) override {
        if (onBringBack) onBringBack();
    }
    std::function<void()> onBringBack;
private:
    juce::String label_;
};

}  // namespace jamwide
```

### Example 3: ConnectionBar GridButton (mirror of CameraButton pattern)

```cpp
// In ConnectionBar.h — forward-declare + unique_ptr member
class GridButton;
std::unique_ptr<GridButton> gridButton;

// In ConnectionBar.cpp — file-local subclass + ctor + onClick wiring
class ConnectionBar::GridButton : public juce::TextButton {
public:
    explicit GridButton(ConnectionBar& p) : parent(p) {}
    // No right-click menu in v1.3; plain TextButton behavior.
private:
    ConnectionBar& parent;
};

// In ConnectionBar ctor — adjacent to CameraButton wiring
gridButton = std::make_unique<GridButton>(*this);
gridButton->setButtonText("Grid");
gridButton->setColour(juce::TextButton::buttonColourId,
                      juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
gridButton->setColour(juce::TextButton::textColourOffId,
                      juce::Colour(JamWideLookAndFeel::kTextPrimary));
gridButton->setTooltip("Toggle video grid");
gridButton->onClick = [this]() {
    if (onGridToggleClicked) onGridToggleClicked();
};
addAndMakeVisible(*gridButton);

// In ConnectionBar::resized — add ~80px slot to the right cluster
if (gridButton) {
    gridButton->setBounds(rightX - 80, y, 80, h);
    rightX -= 80 + gap;
}

// In JamWideJuceEditor ctor — wire onGridToggleClicked
connectionBar.onGridToggleClicked = [this]() {
    setGridBandVisible(!processorRef.getGridVisible());
};
```

### Example 4: VideoTileBase paint with status overlays

```cpp
// juce/ui/video/VideoTileBase.cpp (paint helper invoked from derived class)
void VideoTileBase::paintCommon(juce::Graphics& g, const juce::Image& frame,
                                bool firstFrameSeen, int holdCount, bool synced,
                                const juce::String& username, bool hovering)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kSurfaceChild));
    if (frame.isValid()) {
        g.drawImage(frame, getLocalBounds().toFloat(),
                    juce::RectanglePlacement::centred);
    }

    // Phase 21 D-19 — "video starting…" until first_frame_seen
    if (!firstFrameSeen) {
        drawOverlay_(g, "video starting...");
    } else if (holdCount >= 2 && !synced) {
        // Phase 21 D-17 — "syncing…" after 2 holds
        drawOverlay_(g, "syncing...");
    }

    // Username strip (D-11; auto-hides on hover)
    if (!hovering) {
        auto stripBounds = getLocalBounds().removeFromBottom(18);
        g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceScrim));
        g.fillRect(stripBounds);
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary).withAlpha(0.7f));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(username,
                         stripBounds.reduced(4, 0),
                         juce::Justification::centredLeft, 1);
    }

    // Popout ↗ icon (D-04; always visible)
    paintPopoutIcon_(g);
}

void VideoTileBase::drawOverlay_(juce::Graphics& g, const juce::String& text) {
    auto box = getLocalBounds().withSizeKeepingCentre(160, 36);
    g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceScrim).withAlpha(0.5f));
    g.fillRoundedRectangle(box.toFloat(), 4.0f);
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary).withAlpha(0.7f));
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText(text, box, juce::Justification::centred, 1);
}

void VideoTileBase::paintPopoutIcon_(juce::Graphics& g) {
    auto iconBounds = juce::Rectangle<int>(getWidth() - 18, 4, 14, 14);
    g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceScrim).withAlpha(0.5f));
    g.fillRoundedRectangle(iconBounds.toFloat(), 2.0f);
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary).withAlpha(0.7f));
    // Arrow ↗ — diagonal line + arrowhead
    g.drawLine(static_cast<float>(iconBounds.getX() + 3),
               static_cast<float>(iconBounds.getBottom() - 3),
               static_cast<float>(iconBounds.getRight() - 3),
               static_cast<float>(iconBounds.getY() + 3), 1.5f);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| VDO.Ninja embedded WebView for per-user popouts | Native `juce::DocumentWindow` per peer | v1.3 / 2026-05-15 | Removes 50-100MB browser engine dep; multi-monitor friendly; no cross-origin headaches |
| Single video surface (band OR popout) | Three coexistent surfaces sharing Phase 21's multi-listener `PeerVideoSink` | v1.3 / Phase 21 D-06 | Enables grid + popout simultaneously (DISP-03 hard requirement) |
| Hover-reveal controls (Voicemeeter Banana older versions) | Always-visible corner ↗ icons | v1.3 / D-04 | Matches VB-Banana current "controls visible, not hidden in menus" feel |
| Per-tile resize via `mouseDrag` math | `juce::ResizableEdgeComponent` for band-bottom resizer | JUCE 6+ | Reduces hand-rolled code; standard JUCE primitive |

**Deprecated/outdated:**

- **`juce::Component::findDisplayContaining`** — `[[deprecated]]` per `juce_Displays.h:223`; use `getDisplayForPoint` instead.
- **`juce::DocumentWindow` non-native title bar on macOS embedded plugin context** — soft-preference only; OS may force native chrome. Per `CameraPreviewWindow.cpp:18` comment.
- **VDO.Ninja `popout.html` companion model** — superseded by native popouts; legacy VDO.Ninja code still exists but is no longer the recommended UX (memory: `project_jamtaba_video_port` lists the native port as the v1.3 path).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Hand-rolled `computeGridLayout` recommended over FlexBox | Standard Stack / Alternatives | LOW — Either approach satisfies D-06; if FlexBox is chosen at plan time, just change the implementation. Hand-rolled is unit-testable in isolation; FlexBox is implicitly tested via UAT only. |
| A2 | D-13 Option (a) Timer-poll recommended over Option (b) event surface | Standard Stack / Alternatives | LOW — Option (a) reuses 30Hz cadence already in codebase; Option (b) requires Phase 21 distributor surface change. Planner has explicit authority per D-13 wording. |
| A3 | Structured `<video>` ValueTree subtree with child `<popout>` nodes recommended over flat siblings | Standard Stack / Alternatives | LOW — Phase 19 D-25 used flat siblings for 4 fixed camera fields; that doesn't scale to variadic per-username popouts. Structured XML is JUCE-idiomatic. |
| A4 | 22-01 / 22-02 / 22-03 plan split | Summary | MEDIUM — Could collapse to 2 plans (22-01 substrate + 22-02 popouts/persistence/UAT) if reviewer prefers tighter packaging. The 3-plan split mirrors Phase 19's structure (capture-pipeline / ui-and-persistence / fallback-and-verification) which proved a clean review boundary. |
| A5 | GridButton width ~80px in ConnectionBar | Pitfall 7 | LOW — Visual measurement; can adjust at implementation. If overlap recurs, bump `kBaseWidth` to 1280 (the existing 1030→1200 pattern). |
| A6 | Auto-open latch is a `std::atomic<bool>` on the editor (vs persisted) | Pattern 5 | LOW — D-05 wording: "Once auto-opened, the band stays open until the user explicitly closes it." Per-session latch is the right interpretation; persisted-across-sessions latch would defeat the auto-open UX on next session if user explicitly closed. |
| A7 | Existing 20Hz editor Timer is the right place for auto-open detection (vs new 30Hz Timer) | Pattern 5 | LOW — The editor timer already calls `pollStatus` which inspects connection state; auto-open detection is a similar UI-thread observation. Adding it here avoids spawning a new timer. |
| A8 | Detached-grid default monitor placement = primary, centered, 800×450 | Code Examples | MEDIUM — Open D-discretion. Recommend: primary centered for first-open default; on second open, restore last-known bounds clamped to current display set. |
| A9 | Mixer min-height clamp behavior: cap band at `bandH = mainH - kMixerMinH` where kMixerMinH≈260 | (mentioned in Discretion not implemented above) | MEDIUM — Open D-discretion. Recommend: cap band, never auto-collapse — user dragged it, user owns it; mixer scrolls if needed. |
| A10 | Self-tile popout = re-show existing `previewWindow_` (no new window) | D-09 verbatim | LOW — Locked decision; assumption is correctness of mapping. |

## Open Questions (RESOLVED)

*All open questions resolved by planner per recommendations below. Each `Recommendation:` line is treated as `**RESOLVED:**`.*

1. **Should self-tile's ↗ icon be present at all if D-09 says "re-open existing CameraPreviewWindow"?**
   - What we know: D-09 locks self-popout re-opens existing window. D-04 locks always-visible ↗ icons on every tile.
   - What's unclear: For consistency, self-tile should show ↗; for clarity, perhaps a different icon (e.g., "▢" for "open in window" — but that conflicts with D-04 wording).
   - **RESOLVED:** Show ↗ on self-tile too; on click re-show `previewWindow_`. Distinct from peer-popout but UX consistent.

2. **Should sink-poll attach/detach be debounced?**
   - What we know: 30Hz Timer means peer join/leave triggers `addAndMakeVisible` + `resized()` within ~33ms. Rapid join/leave (network flap) could cause flicker.
   - What's unclear: How common is rapid peer churn on `video.ninjamzap.com:2049`? UAT will tell.
   - **RESOLVED:** No debounce for v1.3 beta; revisit per beta UAT feedback.

3. **Detached-grid bring-back: where do existing popouts go?**
   - What we know: D-18 says clicking bring-back destroys the detached-grid window and restores tiles in-place. But per-peer popouts can be active simultaneously per D-17.
   - What's unclear: If a peer popout is open AND the detached-grid is detached, the peer's slot in the detached grid shows "Popped out →" placeholder. On detached-grid bring-back, the placeholder transitions back to the in-main-view band's placeholder — which is correct per D-03 — but the planner must ensure the placeholder-vs-tile state machine handles both transitions.
   - **RESOLVED:** Document the 4-state truth table explicitly in the plan: {band-tile, band-placeholder, detached-tile, detached-placeholder} based on {gridDetached, peerPoppedOut}.

4. **What happens to the popout window when its peer leaves the room?**
   - What we know: D-17 close hides; the underlying Subscription stays alive (paired with deferred-listener side-table). Peer leaves → distributor `removeSink` runs → PeerVideoSink destructor blocks for in-flight callbacks → Subscription's cached sink pointer dangles.
   - What's unclear: Does the popout window auto-close? Show "(left)" overlay? Keep showing last frame frozen?
   - **RESOLVED:** Treat peer-leave as a visual freeze (last decoded frame stays, no overlay change). On peer rejoin, the popout's tile re-acquires the new sink via Subscription's deferred-listener re-bind. Document in plan.

5. **Should the placeholder cards animate on transition (tile fade out → card fade in)?**
   - What we know: Discretion item ("layout reshuffle animation").
   - What's unclear: Snap-vs-fade preference for v1.3 beta.
   - **RESOLVED:** Snap for v1.3 beta; revisit per UAT feedback.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| JUCE 8.0.12 | All Phase 22 work | ✓ | 8.0.12 vendored at `libs/juce/` | — |
| `juce::FlexBox` | Optional layout primitive | ✓ | JUCE 8.0.12 | Hand-rolled `computeGridLayout` (recommended) |
| `juce::DocumentWindow` | Popout + detached-grid windows | ✓ | JUCE 8.0.12 | — |
| `juce::Desktop::getDisplays()` | Multi-monitor placement clamp | ✓ | JUCE 8.0.12 | — |
| `juce::ComponentBoundsConstrainer::setFixedAspectRatio` | 4:3 aspect-lock on per-peer popouts | ✓ | JUCE 8.0.12 — verified in `CameraPreviewWindow.cpp:28` use | — |
| Phase 19 `JamWideFrameDistributor` | Self-tile (D-08) | ✓ | shipped | — |
| Phase 19 `CameraPreviewWindow` template | RemotePeerPopoutWindow / DetachedGridWindow patterns | ✓ | shipped | — |
| Phase 21 `JamWideRemoteFrameDistributor` | Per-peer tile subscription | ✓ | shipped (Plan 21-03) | — |
| Phase 21 `PeerVideoSink` | Per-peer frame surface + atomic status | ✓ | shipped (Plan 21-03) | — |
| `video.ninjamzap.com:2049` UAT instance | Phase 22 UAT cells | ✓ (community-operated, no SLA) | n/a | UAT requires at least 1 collaborator broadcasting; deferred-risk close path if collaborator unavailable |
| Phase 19 UAT-style operator instructions | Plan 22-03 UAT procedure | ✓ (template at `tests/uat/phase-21-receive-uat-procedure.md`) | — | — |

**Missing dependencies with no fallback:** None — all Phase 22 dependencies are in-tree or shipped.

**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Bare TEST/PASS/FAIL macros in pure-C++ console tests (pattern at `tests/test_plugin_state_v3_v4.cpp:30-48`); linked via `juce_add_console_app` + `add_test()` in `CMakeLists.txt` |
| Config file | `CMakeLists.txt` test block (existing pattern at lines 728-808 — Phase 19 tests) |
| Quick run command | `./scripts/build.sh --tests && (cd build-test && ctest -R "phase22_" --output-on-failure)` |
| Full suite command | `./scripts/build.sh --tests && (cd build-test && ctest --output-on-failure)` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DISP-01 | Tile grid renders in main view; computeGridLayout produces valid (cols,rows,tileW,tileH) for N=1..16 | unit | `ctest -R compute_grid_layout -V` | ❌ Wave 0 — `tests/test_compute_grid_layout.cpp` |
| DISP-01 | Tile lifecycle MEMBER-ORDER CONTRACT under in-flight callbacks (RemotePeerTile dtor blocks in-flight handleAsyncUpdate) | unit | `ctest -R remote_peer_tile_lifetime -V` | ❌ Wave 0 — `tests/test_remote_peer_tile_lifetime.cpp` |
| DISP-01 | Sink-poll attach/detach: simulated cachedUsers mutation produces correct tile add/remove + resized() call | unit | `ctest -R video_grid_band_sink_poll -V` | ❌ Wave 0 — `tests/test_video_grid_band_sink_poll.cpp` |
| DISP-02 | Per-peer popout window opens at restored bounds, clamped to available displays | UAT | UAT Cell 5 (see below) | ❌ Wave 0 — `tests/uat/phase-22-grid-popout-uat-procedure.md` |
| DISP-02 | Popout window close → hide (D-17), Subscription stays alive | unit | (covered by DISP-01 lifetime test — Subscription RAII semantics) | (covered) |
| DISP-03 | Grid + popout active simultaneously for same peer; popout survives grid toggle | UAT | UAT Cell 6 + Cell 8 | ❌ Wave 0 |
| DISP-04 | Toggle grid does NOT disconnect NJClient; cachedUsers + sinks unaffected | UAT | UAT Cell 9 | ❌ Wave 0 |
| D-05 auto-open latch | Latch fires once per session on first sink first_frame_seen; explicit close does not re-trigger | unit | `ctest -R auto_open_latch -V` | ❌ Wave 0 — `tests/test_auto_open_latch.cpp` |
| D-19 v4→v5 state persistence | Round-trip: getStateInformation → setStateInformation restores all fields with defaults for missing | unit | `ctest -R plugin_state_v4_v5 -V` | ❌ Wave 0 — `tests/test_plugin_state_v4_v5.cpp` |
| D-19 input validation | Bounds clamping defends against malicious v5 state injection (T-22-SP) | unit | (same test as above; clamping subtests) | (same file) |
| Auto-open latch reset | Latch is per-session (does NOT persist) | unit | `ctest -R auto_open_latch -V` (sub-test: ctor resets latch) | (same file) |
| Multi-monitor placement | Saved popout bounds outside any display → fall back to primary-centered | UAT | UAT Cell 7 | ❌ Wave 0 |
| Tile-paint snapshot-and-release pattern | `bufferLock` held briefly; paint runs OUTSIDE lock | (covered by code review; not unit-testable without mocking JUCE) | (manual code review gate in plan-checker) | — |

### Sampling Rate

- **Per task commit:** `./scripts/build.sh --tests JamWideJuce_Standalone && (cd build-test && ctest -R "compute_grid_layout|remote_peer_tile_lifetime|video_grid_band_sink_poll|auto_open_latch|plugin_state_v4_v5" --output-on-failure)` (~20 seconds — pure-C++ + JUCE module link only)
- **Per wave merge:** Full suite — `./scripts/build.sh --tests && (cd build-test && ctest --output-on-failure)` (~3-4 minutes — includes Phase 19/20/21 tests)
- **Phase gate:** Full suite green + UAT cells 1-9 PASS (or documented BLOCKED with deferred-risk record) before `/gsd:verify-work 22`

### UAT Cells (Plan 22-03)

| Cell | Description | PASS condition | Type |
|------|-------------|----------------|------|
| 1 | Standalone launch — verify GridButton appears in ConnectionBar without right-cluster overlap at default 1200px width | Visible Grid button, no overlap with Connect/Browse | layout |
| 2 | Plugin launch with no v5 state (fresh install or v4 upgrade) — verify defaults applied (gridVisible=false, gridBandHeight=280, no popouts auto-open) | All defaults applied, no surprise windows | persistence |
| 3 | Solo session (no peers) — verify band auto-open does NOT trigger | Band remains closed | auto-open negative |
| 4 | Connect to `video.ninjamzap.com:2049` with 1 broadcasting peer — verify band auto-opens on first decoded frame | Band opens within ~3 seconds of peer's first frame | auto-open positive (DISP-01) |
| 5 | Click peer's ↗ icon — verify popout window opens at default bounds; verify dragging window persists bounds | Popout opens; drag → close → reopen restores bounds | DISP-02 |
| 6 | Open peer's popout AND keep band open — verify both render simultaneously (band shows placeholder, popout shows live tile) | Both surfaces visible with correct content | DISP-03 |
| 7 | Drag popout to second monitor → close standalone → reopen → verify popout bounds are CLAMPED to current display set if monitor disconnected | Popout reopens at primary-centered fallback when offscreen; restores to second-monitor position when monitor still present | multi-monitor (Discretion) |
| 8 | Toggle GridButton (close band) while popout is open — verify popout continues rendering | Popout stays alive and rendering | DISP-03 |
| 9 | Toggle band on/off rapidly 10x — verify NJClient connection stays alive; verify no decoder crashes | Connection stays connected; no `[ERROR]` logs | DISP-04 |
| 10 | Click band's ↗ → detached-grid window opens; in-main-view band shows full-band placeholder; click placeholder → grid re-attaches | Both transitions work; layout reshuffles correctly | D-03 + D-18 |
| 11 | 3+ simultaneous broadcasters — verify grid layout adapts (1 peer = full, 2-4 = side-by-side, 5+ = wraps) | Layout reflows on each peer join/leave | D-06 |
| 12 | Self-broadcast on/off — verify self-tile appears in slot 0 when broadcasting, disappears when stopped | Tile mounts/unmounts on broadcast toggle | D-07 |
| 13 | Save DAW project with band + popouts → close DAW → reopen DAW → verify band visibility + bounds restored; popouts NOT auto-reopened per D-14 | All state restored except popouts (which require explicit ↗) | D-14 + D-19 |

### Wave 0 Gaps

- [ ] `tests/test_compute_grid_layout.cpp` — covers DISP-01 layout math
- [ ] `tests/test_remote_peer_tile_lifetime.cpp` — covers DISP-01 MEMBER-ORDER CONTRACT (mirror of `tests/test_frame_distributor_lifetime.cpp`)
- [ ] `tests/test_video_grid_band_sink_poll.cpp` — covers DISP-01 add/remove notification
- [ ] `tests/test_auto_open_latch.cpp` — covers D-05
- [ ] `tests/test_plugin_state_v4_v5.cpp` — covers D-19 (mirror of `tests/test_plugin_state_v3_v4.cpp`)
- [ ] `tests/uat/phase-22-grid-popout-uat-procedure.md` — 13 cells above
- [ ] `tests/uat/phase-22-grid-popout-uat-report.md` — operator report template
- [ ] `CMakeLists.txt` additions — 5 new `juce_add_console_app` + `add_test` entries (mirror of lines 728-808)

## Security Domain

> **Phase 22 is UI-only — light threat surface but explicit.**

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | No new auth surface; piggybacks on existing NJClient connection |
| V3 Session Management | no | No new session state; per-peer sink lifecycle owned by Phase 21 |
| V4 Access Control | no | No new access control; UI-only |
| V5 Input Validation | **yes** | `setStateInformation` reads v5 `<video>` ValueTree from untrusted DAW project file; all fields clamped via `juce::jlimit` (mirror of Phase 19 T-19-03 mitigation at `JamWideJuceProcessor.cpp:1091-1108`). Username strings capped at 256 chars (mirror of `cameraSelectedDevice` defense at `:1107`). Per-popout `<popout>` child node iteration must defend against unbounded popout count (recommendation: cap at 64 entries — more than any plausible session would have). |
| V6 Cryptography | no | No crypto in Phase 22 |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| T-22-SP — Plugin state injection via malicious DAW project file (`<video>` subtree contains hostile bounds / unbounded popout count / malicious username strings) | Spoofing + Tampering | (a) All numeric fields clamped via `juce::jlimit` to bounded ranges; (b) Username strings capped at 256 chars before storage; (c) Popout count capped at 64 in setStateInformation read loop; (d) Empty / whitespace-only usernames silently skipped. Mirror of Phase 19 T-19-03 closure pattern. |
| T-22-WB — Window bounds spoofing places popout entirely offscreen (denial-of-visibility) | Tampering | Multi-monitor placement clamp via `Desktop::getInstance().getDisplays().getRectangleList(true)`. If saved bounds intersect no display, fall back to primary-centered default (320×240 at primary's centre). Pattern in §Architecture Patterns Pattern 3. |
| T-22-UAF-1 — Use-after-free if tile destroyed mid-listener-callback | (not a security threat; reliability) | MEMBER-ORDER CONTRACT: `Subscription` is LAST declared member, destructor blocks in-flight `handleAsyncUpdate`. Same pattern Phase 19 used to close codex HIGH-2. |
| T-22-UAF-2 — Use-after-free if popout outlives editor without explicit clear() | (not a security threat; reliability) | Editor dtor explicitly clears `remotePopouts.clear()` + `detachedGrid.reset()` BEFORE LookAndFeel teardown. |
| T-22-OOM — Pathological grid with 100+ tiles exhausts memory (~24MB per peer for image_front/image_back) | DoS | Phase 21 already enforces 320×240 fixed receive surface (Cluster 10 LOW). At 100 peers = ~64MB total sink memory — bounded by NJClient's 16 visible-user filter (`ChannelStripArea` already filters bots; same filter applies to grid). |
| T-22-LL — LookAndFeel teardown ordering (popout window holds LookAndFeel ptr; editor destroys LookAndFeel before popout) | Reliability | Mirror of `CameraPreviewWindow::~CameraPreviewWindow` pattern: `setLookAndFeel(nullptr)` in dtor; editor dtor sequence ensures popouts destroyed before its own `setLookAndFeel(nullptr)`. |

**Threat model boundary:**
- **IN scope:** Plugin state injection (V5), window bounds spoofing (T-22-WB), UAF protection on tile/popout destruction (T-22-UAF-1/2), bounded memory (T-22-OOM), LookAndFeel teardown ordering (T-22-LL).
- **OUT of scope:** Encryption (Phase 15), authentication (none added), network protocol (Phase 14.3 substrate + Phase 20/21 wire), camera permissions (Phase 19), audio-thread safety (Phase 15.1 — Phase 22 ships zero audio-path code).

## Project Constraints (from CLAUDE.md)

> No top-level `./CLAUDE.md` exists in the working directory. Project conventions are inferred from existing code patterns + the memory hints provided:

- **VB-Audio Voicemeeter Banana visual style; dark theme; full custom LookAndFeel** (`feedback_ui_preferences`) — Phase 22 reuses `JamWideLookAndFeel` for every new component (band, tiles, popouts, placeholder cards).
- **Scroll-wheel passes through to viewport** (`feedback_no_scroll_wheel_faders`) — Applies to grid scroll-fallback (D-06 ≥10 peers); scroll wheel scrolls the band, not adjusts volume on any tile.
- **Video surfaces must be detachable** (`feedback_video_surfaces_detachable`) — Phase 22 IS the implementation of this principle: band + detached-grid-window + per-peer popouts coexist.
- **Build VST3 with correct target** (`feedback_copy_vst_on_build`) — `./scripts/build.sh JamWideJuce_Standalone JamWideJuce_AU JamWideJuce_VST3 JamWideJuce_CLAP` is the canonical full-build invocation (avoiding `test_encryption` per known issue).
- **Local build setup** (`project_local_build_setup`) — `./scripts/build.sh` from project root; Ninja generator; x86_64-only locally; tests build to `build-test/`.

## Sources

### Primary (HIGH confidence)

- `juce/video/native/CameraPreviewWindow.{h,cpp}` — direct template for popout windows; verified DocumentWindow + ComponentListener + closeButtonPressed-hides pattern
- `juce/video/native/CameraPreviewTile.{h,cpp}` — direct template for tiles; verified MEMBER-ORDER CONTRACT + AsyncUpdater coalescing
- `juce/video/native/JamWideFrameDistributor.h` — verified Subscriber callback API for self-tile (D-08)
- `juce/video/distributor/JamWideRemoteFrameDistributor.{h,cpp}` — verified subscribeToPeer/findSink/Subscription RAII API
- `juce/video/distributor/PeerVideoSink.{h,cpp}` — verified atomic status fields (first_frame_seen, hold_count, synced) + double-buffer + addListener/removeListener semantics
- `juce/JamWideJuceEditor.{h,cpp}` lines 372-405 (resized) + 286 (20Hz timer) + 943-955 (drivePreviewWindowVisibility) — editor integration points
- `juce/JamWideJuceProcessor.{h,cpp}` lines 96 (currentStateVersion=4), 920-1110 (getState/setState) — verified v3→v4 add pattern + clamping + popout-bounds accessor pattern
- `juce/ui/ConnectionBar.{h,cpp}` lines 23-71 (CameraButton class), 266-277 (button construction), 362-365 (resized layout) — verified button-add template
- `juce/ui/ChannelStripArea.{h,cpp}` lines 318 (startTimerHz(30)) + 365-380 (timerCallback) + 348-362 (cachedUsersMutex pattern) + 550-700 (refreshFromUsers) — verified 30Hz Timer + roster discovery pattern
- `juce/ui/JamWideLookAndFeel.h` lines 10-31 (color constants) — verified VB-Banana dark theme palette
- `libs/juce/modules/juce_gui_basics/layout/juce_FlexBox.h` lines 51-130 — verified FlexBox API (JUCE 8.0.12)
- `libs/juce/modules/juce_gui_basics/desktop/juce_Displays.h` lines 200-215 — verified getDisplays / getRectangleList / Display struct
- `libs/juce/modules/juce_gui_basics/layout/juce_ComponentBoundsConstrainer.h` lines 52-150 — verified setFixedAspectRatio / setSizeLimits
- `tests/test_plugin_state_v3_v4.cpp` — verified test framework pattern (TEST/PASS/FAIL macros + JUCE module link)
- `tests/test_frame_distributor.cpp` + `test_frame_distributor_lifetime.cpp` — verified MEMBER-ORDER CONTRACT test patterns
- `CMakeLists.txt` lines 728-808 — verified juce_add_console_app + add_test build wiring for Phase 19 tests

### Secondary (MEDIUM confidence)

- WebSearch — JUCE DocumentWindow setUsingNativeTitleBar + setContentOwned + multi-monitor placement (confirmed surface API exists; no platform-specific surprises beyond known macOS native-title-bar override per `CameraPreviewWindow.cpp:18` comment)

### Tertiary (LOW confidence)

- (None — all critical claims verified against in-tree code)

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — every dependency verified in-tree; JUCE 8.0.12 vendored; Phase 19/21 substrates shipped
- Architecture: HIGH — direct mirror of two shipped templates (CameraPreviewWindow + CameraPreviewTile); decomposition follows Phase 19 precedent
- Pitfalls: HIGH — informed by Phase 19's review history (codex HIGH-2 / HIGH-4) + Phase 21's lifetime contracts + recent commits 5250ff1 (kBaseWidth bump) and 9679e7b (D-12 silent-no-op fix)
- Multi-monitor placement: MEDIUM — `Desktop::getDisplays()` API surface verified but Phase 19 only used single-monitor popout (CameraPreviewWindow always opens at default bounds without multi-monitor clamp); Phase 22 introduces the clamp pattern but it's untested in the codebase

**Research date:** 2026-05-17
**Valid until:** 2026-06-15 (~30 days; UI/JUCE patterns are stable; only risk is JUCE 8.0.x breaking-change in a point release, which the in-tree vendoring prevents)
