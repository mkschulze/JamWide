# Phase 22: Native Video UI (Grid + Popouts) - Context

**Gathered:** 2026-05-17
**Updated:** 2026-05-18 — codex `--reviews` replan added M6 option (b) extraction to deferred items (v1.4)
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 22 is the consumer-side UI for Phase 21's decoded video. It builds three coexistent rendering surfaces, all consuming the same per-peer `PeerVideoSink` (Phase 21 substrate) via independent `JamWideRemoteFrameDistributor::Subscription` handles:

1. **In-main-view grid band** — A toggleable horizontal band stacked between `SessionInfoStrip` and `ChannelStripArea` inside `JamWideJuceEditor`. Contains a self-tile (when self is broadcasting) plus one tile per remote peer that is actively broadcasting video. Tiles flow-layout adaptively (fill-width with computed N×M based on peer count + available width, preserving 4:3 source aspect). Band has a fixed default height (~280px planner-picked baseline) with a draggable horizontal resizer on its bottom edge.

2. **Detachable whole-grid window** — A `juce::DocumentWindow` that hosts a second instance of the grid component (same tile set as the in-main-view band, populated via the same distributor subscriptions). Triggered by a `↗` button on the band header. Singleton — at most one detached-grid window at a time. When open, the in-main-view band shows a single full-band placeholder card: `"Grid is in detached window →"` with a "Bring back" affordance that destroys the detached window and restores the band tiles.

3. **Per-user popout windows** — One `juce::DocumentWindow` per remote peer (and self), triggered by a `↗` button in each tile's top-right corner. Multiple popouts coexist; each is independent. Closing a popout HIDES it (mirror of Phase 19 D-09 — does not destroy the underlying PeerVideoSink listener). When a peer is popped out, that peer's slot in the in-main-view band (and in the detached-grid window) shows a per-peer placeholder card: `"Popped out →"` with a "Bring back" click affordance.

**Toggle behavior:** New "Grid" button in `ConnectionBar` (next to the existing Camera button) controls the in-main-view band's visibility. State persisted across sessions. Auto-opens on the first peer's `first_frame_seen` flip from false→true. Subsequent toggles are manual (button or band-header `×`). Toggling the band does NOT close popouts or the detached-grid window (DISP-04, success criterion 4).

**Maps to:** Requirements **DISP-01** (per-user tile grid in main view), **DISP-02** (per-user popout `juce::DocumentWindow`), **DISP-03** (grid + popouts active simultaneously, survives grid toggle), **DISP-04** (toggle grid on/off without disconnecting NINJAM).

**Out of scope:** Phase 23 (macOS universal + Windows packaging + codesign), Phase 24 (per-DAW UAT + beta validation + `docs/SERVER.md`). Phase 22 ships zero audio-path code — `JamWideRemoteFrameDistributor` is already wired by Phase 21 (`JamWideJuceProcessor::remoteFrameDistributor` exists since Plan 21-03 Task 2). Phase 22 also ships zero changes to the Phase 20 send-path or the Phase 19 camera-capture stack — the self-tile is a NEW subscription to the existing `JamWideFrameDistributor`; the camera popout (`CameraPreviewWindow`) is reused unchanged.

</domain>

<decisions>
## Implementation Decisions

### Grid Surfaces + Layout (Area 1)

- **D-01: Three coexistent rendering surfaces — in-main-view band, detachable whole-grid window, per-user popouts.** All three consume the same per-peer `PeerVideoSink::image_front` via independent `JamWideRemoteFrameDistributor::Subscription` handles. `juce::Image` is ref-counted, so multi-surface paint is cheap (each surface takes its own brief `bufferLock` snapshot then paints outside the lock). Phase 21's `listenerLock_` snapshot-then-fan-out pattern already handles multi-listener fan-out per sink without contention.

- **D-02: In-main-view grid band is a stacked horizontal band between `SessionInfoStrip` and `ChannelStripArea`.** Existing `JamWideJuceEditor::resized()` order: ConnectionBar → BeatBar → SessionInfoStrip (conditional) → ChannelStripArea (fills remaining). Phase 22 inserts the grid band ABOVE `channelStripArea.setBounds(area)` so the channel strips shrink vertically when the band is open. Mixer remains the visual anchor (VB-Banana "mixer is sacred"). Does NOT use side-by-side (would squeeze VbFader width) or replace-mode (would hide mixer entirely).

- **D-03: Placeholder cards when a tile is popped out or the grid is detached.** Per-peer popout active → that peer's slot in the in-main-view band (and in the detached-grid window) renders a placeholder card: dark VB-style frame + `"Popped out →"` label + click-to-bring-back affordance (clicking destroys the popout window and rebinds the live tile in-place). Whole-grid detach active → the entire in-main-view band renders a SINGLE full-band placeholder: `"Grid is in detached window →"` + click-to-bring-back. RESEARCH-ADDENDUM "UI rendering model [LOCKED-2026-05-15]" called this out as the recommended pattern.

- **D-04: Always-visible corner `↗` icons for detach/popout affordances.** Per-tile `↗` icon top-right corner, always visible (not hover-reveal). Grid-band header gets a `↗` icon (next to the band-toggle `×`). Matches the VB-Banana "controls are visible, not hidden in menus" feel (memory: `feedback_ui_preferences`). Differentiates from Phase 19 D-19's right-click quality menu pattern; for one-shot detach actions, a clickable visible icon is the cleaner affordance.

- **D-05: ConnectionBar "Grid" button (next to Camera button) + auto-open on first peer frame.** New button in `juce/ui/ConnectionBar.cpp` adjacent to the existing Camera button. State persisted across sessions. Auto-opens the band the first time ANY peer's `PeerVideoSink::first_frame_seen` atomic flips false→true (driven by an existing message-thread Timer that already polls the sink for repaint snapshots). Once auto-opened, the band stays open until the user explicitly closes it (band-header `×` or ConnectionBar button toggle).

### Tile Layout + Sizing (Area 2)

- **D-06: Fill-width adaptive layout — compute N×M from peer count + available width.** Tile component is 4:3 aspect (matches `PeerVideoSink` fixed 320×240 receive surface from Phase 21 Codex Cluster 10 LOW). Given peer count N and available band width W: solve for column count `cols` that maximizes tile size while satisfying `tile_width × cols ≤ W` and `tile_width × 0.75 (4:3) × rows ≤ band_height`. Layout updates when peers join/leave. 1 peer = full-band tile; 2-4 peers = side-by-side; 5-9 peers = 3 cols; 10+ peers = scroll. Implementation via `juce::FlexBox` or hand-rolled in the grid component's `resized()`. Aspect-preservation (4:3 with side margins when band is wider than tile×cols) is Claude's discretion.

- **D-07: Self-tile is always the first slot in the grid; appears only when self is BROADCASTING (not just camera ON).** Self-tile semantics symmetric with peer tiles — both appear only when actively on the wire. Self-broadcast state is the Phase 20-03 `ConnectionBar::getCameraIsBroadcasting()` atomic. When self is broadcasting, the self-tile subscribes to Phase 19's `JamWideFrameDistributor` (callback-style `Subscriber::onFrame(const juce::Image&)`). When self is NOT broadcasting, no self-tile renders. Phase 19's existing `CameraPreviewWindow` remains the user's "set up my camera before going live" surface and is unchanged.

- **D-08: Two specialized tile component classes — `SelfVideoTile` (for self, Phase 19 distributor) + `RemotePeerTile` (for peers, Phase 21 distributor) — sharing a common chrome/render base class.** Cleanest cut for the two different subscription shapes (callback `Subscriber` for self vs RAII `Subscription` for peers). Shared base handles: 4:3 letterbox painting, username overlay strip, status overlays ("video starting…", "syncing…"), popout `↗` button rendering. Each derived class owns its subscription lifecycle. Both classes follow Phase 19 `CameraPreviewTile`'s MEMBER-ORDER CONTRACT (subscription as LAST declared member).

- **D-09: Self-tile popout button re-opens Phase 19's existing `CameraPreviewWindow`.** Clicking the self-tile's `↗` calls into a `JamWideJuceProcessor` controller that shows the existing camera preview window (toggles visibility, mirroring how `JamWideJuceEditor::drivePreviewWindowVisibility` already works). NO second self-popout window is created — that would render the same camera frame twice. Per-peer popouts are NEW windows (Phase 19 popout has no per-peer counterpart). **Codex M5 codex closure (2026-05-18 `--reviews` replan):** the dispatch is now typed via `enum class VideoPopoutTargetKind { Self, RemotePeer }` + `struct VideoPopoutTarget { kind; username; }` — NOT a magic-string `username == "__self__"` comparison. The editor's `openOrToggleRemotePopout(VideoPopoutTarget)` switches on `t.kind`.

- **D-10: Fixed default band height (~280px planner-picked baseline) with draggable horizontal resizer on bottom edge.** Resizer drag updates band height; persisted across sessions in the new `<video>` state subtree (D-19). Mixer height = main_area − band_height when band is open. Predictable mixer behavior (mixer doesn't auto-shrink when peers join). User has a recourse (drag resizer) to recover mixer height or grow video band.

### Tile Chrome (Area 3)

- **D-11: Minimal tile chrome — username strip (bottom, hover-hide) + popout `↗` (top-right always visible) + Phase 21 status overlays only.** Username in a semi-transparent dark strip across the bottom of the tile; strip auto-hides on hover-of-video (so user can see the full frame when inspecting). Top-right `↗` popout icon always visible. Phase 21 D-19 `"video starting…"` overlay until `first_frame_seen` flips; Phase 21 D-17 `"syncing…"` overlay once `hold_count ≥ 2` — both verbatim copy + soft-white at ~70% opacity over faint dark-translucent backdrop (per Phase 21 specifics). NO audio mute/solo, NO VU meter, NO codec/error badges on the tile. Audio controls remain in `ChannelStripArea` where the user expects them — clean "video tile = video, mixer = mixer" separation.

- **D-12: Tile appears ONLY when peer is actively broadcasting (first H264 BEGIN observed); disappears when they stop broadcasting or leave room.** No empty tile slots for non-broadcasters. Symmetric with self-tile rule (D-07). Cleaner empty-state UX — grid stays compact and informative. Phase 21's `deferredListeners_` side-table is still functional but not the primary path for Phase 22 (tiles subscribe AT first BEGIN, not before). Layout reshuffles on tile add/remove (snap-reshuffle by default; animation is Claude's discretion).

- **D-13: Tile add/remove notification — Phase 22 grid polls the distributor's sink map via a message-thread `juce::Timer` (or via a new `JamWideRemoteFrameDistributor::onSinkAdded/onSinkRemoved` listener if planner prefers).** Phase 21's distributor surface is sink-centric (no event surface today). Two acceptable mechanisms: (a) grid polls `findSink(name, chidx=1)` for each roster member each Timer tick (simple, lightweight at modest N — JamWide already polls 30Hz for VU updates per `ChannelStripArea::timerCallback`), or (b) extend `JamWideRemoteFrameDistributor` with sink-add/remove listeners (cleaner, more code, requires Phase 21 distributor touch-up). Planner picks based on what fits cleanest with the existing 30Hz Timer pattern.

### Popout State Persistence (Area 4)

- **D-14: Bounds-only persistence; popouts always start CLOSED on plugin/standalone launch.** Mirror of Phase 19 D-10 ("Camera always starts OFF on launch"). Detached-grid window and per-peer popouts persist their last bounds across sessions but do NOT auto-reopen on load. User must explicitly click `↗` to open; the window then appears at its last-persisted bounds. Eliminates "surprise windows appearing on DAW reload" UX. Matches the privacy-default tone of Phase 19.

- **D-15: Per-peer popout bounds keyed by username — `Map<String username, Rectangle bounds>`.** Dave's popout opens at Dave's last bounds whenever Dave is in the room, regardless of server. Survives username collisions with the rarer server-collision edge case left unhandled (NINJAM use rarely sees identical usernames across servers in the same user session). Matches the user's mental model "Dave's popout window." Simple map structure; planner serializes as XML inside the `<video>` ValueTree subtree. **Codex H2 codex closure (2026-05-18):** production storage is `juce::HashMap<juce::String, juce::Rectangle<int>>` (NOT `std::unordered_map`) per the `juce/osc/OscAddressMap.h:65` precedent — iteration order is hash-stable per session, giving deterministic `<popout>` child node order in saved XML for diffability.

- **D-16: Detached-grid window is a single state slot — `Rectangle bounds` (just position+size, no per-peer state inside the window).** Detached-grid window is a singleton (only one can be open at a time). Persists its bounds across sessions. Auto-restore = NO (matches D-14). Title bar shows "JamWide — Video Grid" (planner can refine).

- **D-17: Closing a popout HIDES the window; does NOT destroy the underlying distributor Subscription.** Mirror of Phase 19 D-09 (`CameraPreviewWindow::closeButtonPressed` hides without affecting capture state). The popout window's `closeButtonPressed` calls `setVisible(false)`; the underlying `RemotePeerTile` keeps its `JamWideRemoteFrameDistributor::Subscription` alive (so frames keep arriving and the tile stays warm for next reopen — also keeps the in-grid placeholder card showing "Popped out →" with the bring-back affordance until the user either reopens or fully closes via an "X destroy" affordance — planner picks the explicit destroy semantics).

  **Codex H3 codex closure (2026-05-18 `--reviews` replan) — 4-state truth table for tile `↗` semantics:**
  | State | Popout window | Tile `↗` click | Window X click | Placeholder click |
  |-------|---------------|----------------|----------------|-------------------|
  | (A) Absent (initial) | not created | CREATE+SHOW; placeholder mounts | n/a | n/a |
  | (B) Visible | visible | HIDE; placeholder stays | HIDE; placeholder stays | RE-SHOW (placeholder usually hidden when popout visible — rare path) |
  | (C) Hidden | hidden | **RE-SHOW** (non-destructive); placeholder stays | n/a | **DESTROY**; tile returns |
  | (D) Destroyed | not created | (same as A) | n/a | n/a |

  Codex H3 explicitly disambiguated state (C): clicking `↗` on a tile whose popout is hidden RE-SHOWS the popout (preserving bounds + Subscription). The destroy path is exclusively via placeholder click → `bringBackRemotePopout`. This makes "bring back" the single, predictable destroy path; the tile `↗` is the toggle-visibility path.

- **D-18: "Bring back" affordance on placeholder cards = click the card → destroy the popout/detached-grid window → restore the live tile/grid in-place.** Bring-back is the explicit destroy path for popouts and detached-grid windows. After bring-back, the next click on `↗` opens a fresh popout at the last-persisted bounds. (Consistent with codex H3 state-machine above — placeholder click is the only path from C to D.)

- **D-19: State schema bump v4 → v5; new `<video>` ValueTree subtree (NOT extending `<camera>`).** Phase 19 D-24 bumped state to v4 for the `<camera>` subtree. Phase 22 bumps to v5 and adds a separate `<video>` subtree with: `gridVisible` (bool, default false), `gridBandHeight` (int, default ~280px), `detachedGridBounds` (Rectangle), `popoutBounds` (Map<username, Rectangle>). NEW subtree (not extending `<camera>`) because grid state is independent of camera state — coupling them violates the existing Phase 19 D-09 "orthogonal camera/popout" principle. `loadState` handles missing v4→v5 fields gracefully via defaults.

### Claude's Discretion

- **Band default height baseline** (probably ~280px = ~1 row of 4:3 tiles at modest scale, but planner can profile against typical NINJAM session window sizes).
- **Detached-grid-window default monitor placement** on first open (centered on primary monitor vs. positioned next to main editor — planner picks).
- **Mixer min-height clamp behavior** when band+mixer can't fit (planner picks: scroll mixer? cap band? force-collapse band?).
- **Aspect-preservation policy** when band is wider than tile×cols (4:3 letterbox with side margins is the natural default).
- **Min tile size threshold before scroll-fallback** (when computed `tile_width < ~120px`, switch to fixed 120-160px tiles + vertical scroll within the band).
- **Inter-tile spacing/padding** (4-8px likely; planner picks per VB-style chrome).
- **"Popped out" placeholder card visual** (dark VB-style frame matching surrounding chrome).
- **Layout reshuffle animation** on tile add/remove (snap is fine for v1.3; animation is polish).
- **Sink-add/remove notification mechanism** — D-13 Option (a) Timer-poll or Option (b) distributor event surface. Planner picks per cleanliness vs Phase 21 distributor touch surface.
- **Stale popout bounds handling** when monitor disconnected (use `juce::Component::setBoundsConstrained` to clamp to current screen — standard JUCE pattern).
- **Aspect-lock policy for remote popouts** (4:3 mirror of Phase 19 D-07 — both source surfaces are 320×240).
- **Keyboard shortcuts** for grid toggle / popout / detach (planner: none for v1.3 beta; revisit per beta UAT feedback).
- **"Popped out window's explicit destroy" affordance** vs always treating close as hide-with-card-affordance-for-bring-back (per D-17 nuance). Codex H3 picked the side: tile `↗` toggles visibility on state-C; placeholder click is the explicit destroy.
- **Detached-grid window title bar text and icon** (planner picks "JamWide — Video Grid" or similar).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Spike + research locked decisions (authoritative for v1.3 UI rendering model)

- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-RESEARCH-ADDENDUM.md` — `## UI rendering model [LOCKED-2026-05-15]`. Confirms native rendering only, grid + popouts both active, placeholder-vs-mirror recommendation. **This is the prior-art justification for D-01 and D-03.**
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` — Item E.2 (display widget). UI surface scope reference.
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-CONTEXT.md` — Locked decisions (Section 3 "Locked Decisions to Honor"). Native-rendering grid + popouts, no browser companion.

### Phase 21 substrate (immediate predecessor — distributor + sink contracts)

- `.planning/phases/21-h-264-decoder-receive-pipeline/21-CONTEXT.md` — Phase 21 D-01 (atomic `image_front` snapshot lock-free read), D-02 (per-peer `juce::AsyncUpdater`), D-06 (multi-subscriber per peer for grid+popout — DIRECTLY exercised by Phase 22), D-08 (double-buffered front/back + `bufferLock`), D-17 (`syncing…` overlay at `hold_count ≥ 2`), D-19 (`video starting…` overlay until `first_frame_seen`), D-20 (atomic status fields read lock-free per-paint).
- `juce/video/distributor/JamWideRemoteFrameDistributor.h` — Subscription API surface Phase 22 consumes. `subscribeToPeer(username, chidx, onRepaint) → Subscription`. RAII unsubscribe waits for in-flight handleAsyncUpdate.
- `juce/video/distributor/JamWideRemoteFrameDistributor.cpp` — Subscription lifecycle implementation.
- `juce/video/distributor/PeerVideoSink.h` — Status field contract: `image_front` + `image_back` + `bufferLock` + atomic `generation` + atomic `hold_count` / `decode_error_count` / `drop_resync_count` / `synced` / `first_frame_seen`. Listener API `addListener(cb) → id` + `removeListener(id)`.
- `juce/video/distributor/PeerVideoSink.cpp` — handleAsyncUpdate fan-out implementation.

### Phase 19 patterns (popout window + tile templates — reuse verbatim)

- `.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md` — D-05 (popout-only placement pattern), D-07 (resizable, aspect-locked, position-persists popout), D-08 (custom dark-theme `DocumentWindow` chrome via `JamWideLookAndFeel`), D-09 (close hides not destroys), D-10 (always-starts-closed on launch — D-14 mirrors this), D-24 (state schema bump precedent v3→v4 — D-19 mirrors this v4→v5), D-25 (`<camera>` subtree scalar/serializable fields layout — D-19 mirrors this for `<video>`).
- `juce/video/native/CameraPreviewWindow.h` + `.cpp` — Direct template for per-peer popout and detached-grid windows. `DocumentWindow + ComponentListener::componentMovedOrResized + onBoundsChanged hook for persistence + closeButtonPressed hides`. Phase 22 builds `RemotePeerPopoutWindow` and `DetachedGridWindow` as analogs.
- `juce/video/native/CameraPreviewTile.h` + `.cpp` — Direct template for `SelfVideoTile` and `RemotePeerTile`. `juce::Component + juce::AsyncUpdater + Subscriber/Subscription as LAST declared member + MEMBER-ORDER CONTRACT`. Phase 22 mirrors this pattern.
- `juce/video/native/JamWideFrameDistributor.h` — Self-tile subscription type (`Subscriber` callback shape) — different from Phase 21's RAII `Subscription` style. D-08 calls out the two-class split.
- `juce/video/native/CameraStateMachine.h` — Reference for the broadcast-state atomic that gates self-tile visibility (D-07).

### JamWide editor integration points

- `juce/JamWideJuceEditor.h` (lines 1-80) — Editor owns the existing video/camera surfaces; Phase 22 adds the grid band component as a new member.
- `juce/JamWideJuceEditor.cpp:370-405` (`resized()`) — Layout entry point. Phase 22 inserts the grid band between `sessionInfoStrip.setBounds(...)` and `channelStripArea.setBounds(area)`.
- `juce/ui/ConnectionBar.h` (lines 14-115) — Where the new "Grid" button lives. Mirror the existing `CameraButton` (`unique_ptr<...>` defined in `.cpp` to keep header lightweight) pattern.
- `juce/JamWideJuceProcessor.h:143-187` — `remoteFrameDistributor` member (Phase 21) + `getRemoteFrameDistributor()` accessor — already wired; Phase 22 just uses it.
- `juce/JamWideJuceProcessor.cpp:72-74` — Distributor instantiation. Phase 22 does NOT touch this.
- `juce/ui/ChannelStripArea.h:13` (`refreshFromUsers(const std::vector<NJClient::RemoteUserInfo>&)`) — Roster discovery pattern. Phase 22 reuses `client->GetRemoteUsersSnapshot(cachedUsers)` to iterate peers for tile-per-peer rendering.
- `juce/ui/JamWideLookAndFeel.h` + `.cpp` — VB-Banana dark-theme LookAndFeel for tile chrome + popout window chrome (memory: `feedback_ui_preferences`).
- `juce/ui/BotFilter.h` + `.cpp` — **NEW (codex H1 closure, 2026-05-18):** namespace-scoped `jamwide::isBot` + `jamwide::stripAtSuffix` extracted from `ChannelStripArea.cpp`'s former anonymous namespace (where they had internal linkage, blocking any cross-TU use). Plan 22-02 Task 0 creates this header pair; Plans 22-02 + JamWideJuceEditor `#include "ui/BotFilter.h"` and call through `jamwide::` prefix.
- `juce/osc/OscAddressMap.h:65` — `juce::HashMap<juce::String, int>` precedent for the codex H2 NARROWED closure: applies only to copyable value types where deterministic iteration matters (Plan 22-04's `juce::HashMap<juce::String, juce::Rectangle<int>>` for `remotePopoutBoundsMap_`).
- `juce/midi/MidiMapper.h:105` — `std::unordered_map<juce::String, int>` precedent proving `std::hash<juce::String>` ships with JUCE 8. Used for in-memory `std::unique_ptr<T>`-valued maps in Plans 22-02/22-03 (`peerTiles_`, `remotePopouts_`, `peerPlaceholders_`) because `juce::HashMap::set()` does copy-assign and cannot hold move-only types.

### State persistence pattern (precedent)

- `juce/JamWideJuceProcessor.cpp` — Plugin state save/load methods (search `loadState` / `getStateInformation` / `setStateInformation`). Phase 19 D-24 bumped to v4 with `<camera>` subtree; Phase 22 D-19 bumps to v5 with NEW `<video>` subtree. `loadState` handles missing fields gracefully via defaults — same pattern.

### Memory references

- `feedback_ui_preferences` — VB-Audio Voicemeeter Banana style, dark theme, full custom LookAndFeel. Drives D-04 (always-visible controls), D-11 (minimal tile chrome), and the band's visual integration with the mixer.
- `feedback_no_scroll_wheel_faders` — Scroll passes through to viewport, not adjust volume. **Applies to grid-band-with-many-peers scroll case** (D-06 fallback) — scroll wheel should scroll the band, not interact with tiles.
- `feedback_uat_scope_redflags` — Never let an executor's "verify only X, skip Y" UAT pass when Y is a user-visible happy-path. Phase 22's UAT must explicitly cover DISP-01/02/03/04 — grid opens, popout opens, both coexist, toggling doesn't disconnect.
- `feedback_video_surfaces_detachable` (2026-05-17 NEW) — Every JamWide video surface needs detach-to-window for multi-monitor jam workflows. Phase 22 surfaces this: grid band + whole-grid-window + per-peer-window, all simultaneous. Treat detachability as Tier-1 gray area in any video phase. Codex M7 reinforces this — the detached band must stay in sync with the main band's placeholder state via editor-driven `setPeerPoppedOut` on BOTH bands.

### Project + requirements

- `.planning/PROJECT.md` — v1.3 Native Video milestone; "Native rendering — `juce::Component` grid in main view + `juce::DocumentWindow` per-user popouts (multi-monitor friendly), both active simultaneously" target.
- `.planning/REQUIREMENTS.md` — DISP-01, DISP-02, DISP-03, DISP-04 (Phase 22 maps to these).
- `.planning/ROADMAP.md:368-381` — Phase 22 roadmap entry. Success criteria 1-4.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`JamWideRemoteFrameDistributor` + `PeerVideoSink`** (`juce/video/distributor/`) — Phase 21 substrate. Phase 22 calls `subscribeToPeer(username, chidx=1, onRepaint)` per remote tile; each `Subscription` is stored as a LAST member of the tile (MEMBER-ORDER CONTRACT mirroring Phase 19). Listeners fan out via `PeerVideoSink::handleAsyncUpdate` on message thread.
- **`JamWideFrameDistributor`** (`juce/video/native/JamWideFrameDistributor.h`) — Phase 19 substrate. Phase 22's `SelfVideoTile` subscribes via the older callback-style `Subscriber::onFrame(const juce::Image&)` API.
- **`CameraPreviewWindow`** (`juce/video/native/CameraPreviewWindow.h`) — Direct template for `RemotePeerPopoutWindow` and `DetachedGridWindow`. DocumentWindow + LookAndFeel + ComponentListener + closeButtonPressed-hides + onBoundsChanged-callback for persistence.
- **`CameraPreviewTile`** (`juce/video/native/CameraPreviewTile.h`) — Direct template for `SelfVideoTile` and `RemotePeerTile`. Component + AsyncUpdater + MEMBER-ORDER CONTRACT (subscription as LAST member).
- **`JamWideLookAndFeel`** (`juce/ui/JamWideLookAndFeel.h`) — VB-Banana dark-theme used for popout chrome, tile borders, placeholder cards.
- **`ConnectionBar` button pattern** (`juce/ui/ConnectionBar.cpp`) — Existing Camera button is the template for the new Grid button (forward-declared `class GridButton; unique_ptr<GridButton>` in header, definition in `.cpp`).
- **`ChannelStripArea` 30Hz `juce::Timer` polling pattern** — Reference for D-13 Option (a) sink-map polling.

### Established Patterns

- **MEMBER-ORDER CONTRACT** (Phase 19 `CameraPreviewTile`): Subscription/subscription-handle must be the LAST declared member so its destructor runs FIRST and waits for in-flight callbacks. Phase 22 tiles MUST follow this.
- **JUCE message thread is the canonical UI thread.** All tile paint + popout state updates happen here. `PeerVideoSink::handleAsyncUpdate` already dispatches onto it.
- **Audio thread is sacred.** Phase 22 ships ZERO audio-path code. All state mutations and observations are message-thread or distributor-internal.
- **State version bumps for schema additions.** Phase 14 v2→v3 (MIDI), Phase 19 v3→v4 (camera), Phase 22 v4→v5 (video). `loadState` handles missing fields via defaults.
- **Per-tile/per-window chrome via custom `LookAndFeel`.** Phase 19 D-08 established this; Phase 22 reuses verbatim.
- **Singleton popout pattern** — Phase 19's `CameraPreviewWindow` is owned by the editor and toggled visible. Phase 22's per-peer popouts are NOT singletons (one per peer); the detached-grid window IS a singleton.
- **Container choice for `juce::String`-keyed maps** (codex H2 NARROWED, 2026-05-18) — for **copyable** value types (e.g. `juce::Rectangle<int>`), prefer `juce::HashMap<juce::String, V>` for deterministic per-session iteration order (good for serialized XML diffability) per `juce/osc/OscAddressMap.h:65`. For **move-only** value types (e.g. `std::unique_ptr<T>`), use `std::unordered_map<juce::String, V>` because `juce::HashMap::set()` does copy-assign at `juce_HashMap.h:244` (fails template instantiation on `unique_ptr`); `juce/midi/MidiMapper.h:105` proves `std::hash<juce::String>` ships with JUCE 8 so `std::unordered_map<juce::String, ...>` compiles cleanly.

### Integration Points

- **`JamWideJuceEditor::resized()`** (`juce/JamWideJuceEditor.cpp:370-405`) — Phase 22 inserts the grid band between `sessionInfoStrip.setBounds(...)` and `channelStripArea.setBounds(area)`. New conditional `if (gridBandVisible)` block.
- **`JamWideJuceEditor` member list** — Phase 22 adds: `VideoGridBand gridBand` (`juce::Component`), `std::unique_ptr<DetachedGridWindow> detachedGrid` (lazy), `std::unordered_map<juce::String, std::unique_ptr<RemotePeerPopoutWindow>> remotePopouts` (lazy per peer — codex H2 NARROWED: `std::unordered_map` required because value type is move-only `unique_ptr`; `juce::HashMap::set` copy-assigns), `std::unique_ptr<SelfVideoPopoutDelegate>` (delegates to existing `CameraPreviewWindow`).
- **`JamWideJuceProcessor::getRemoteFrameDistributor()`** (`juce/JamWideJuceProcessor.h:186`) — Editor obtains the distributor reference here; passes to grid band + popouts via constructor.
- **`ConnectionBar`** — New `GridButton` member; `onClick` lambda calls `editor.toggleGridBand()`.
- **Plugin state save/load** — Phase 22 adds `<video>` ValueTree subtree write/read; bumps version constant 4→5.
- **`ChannelStripArea::refreshFromUsers`** — Phase 22's grid band uses the same `NJClient::RemoteUserInfo` snapshot pattern for roster discovery (`client->GetRemoteUsersSnapshot(cachedUsers)`).

</code_context>

<specifics>
## Specific Ideas

- **Placeholder card visual:** Dark VB-style frame matching surrounding chrome, faint dark-translucent backdrop. Text: per-peer = `"Popped out →"` ; whole-grid = `"Grid is in detached window →"`. Click anywhere on the card to bring back. Cursor changes to pointer on hover.
- **Tile username strip:** Semi-transparent dark band across the bottom ~18px tall. Username in soft white at ~70% opacity, left-aligned, 4px padding. Auto-hides on mouse-hover-of-video (so user can inspect the full frame).
- **Popout `↗` icon:** Small (~12-14px), top-right corner, 4px inset. Soft-white at ~70% opacity over faint dark-translucent backdrop (matches Phase 21 overlay style). On hover, opacity bumps to 100% and tile shows a tooltip "Pop out window". **Codex L9 closure (2026-05-18):** UAT Cell 1 explicitly verifies the `↗` glyph renders correctly (no tofu). Fallback if missing on Windows: small `Path`-drawn triangle.
- **Grid band header (when band is open):** Thin (~24px tall) strip across the top of the band. Right edge: `↗ ×` (detach + close). Left edge: peer count badge `[3 peers]` (optional, Claude's discretion). Below the header: the tile flow area. **Codex M7 closure (2026-05-18):** `VideoGridBand` has a `Mode` enum (`MainBand`|`DetachedBand`) — the `↗` detach icon is painted ONLY when `mode_ == MainBand` (the detached band's inner band suppresses its own detach affordance since it's already detached).
- **ConnectionBar Grid button label state machine:** "Grid" (idle/off) → "Grid (on)" (band visible). Tooltip on hover: "Toggle video grid" (idle), "Hide video grid" (active).
- **Detached-grid window title:** `JamWide — Video Grid`. Window icon = the JamWide app icon. Default size = 800×450 or sized to fit current band content + chrome (planner picks).
- **Per-peer popout window title:** `JamWide — <username>`. Default size = 320×240 + chrome (matches Phase 19 D-07 default). Aspect-locked 4:3 (mirror of Phase 19 D-07).
- **Auto-open trigger** (D-05): observes `PeerVideoSink::first_frame_seen` atomic flipping false→true on ANY peer for the first time in the session. After first auto-open, subsequent peer-first-frames do NOT re-auto-open if the user has explicitly closed the band. **Codex M4 closure (2026-05-18):** the latch fires SYNCHRONOUSLY after releasing the `cachedUsersMutex` — NO `MessageManager::callAsync` (the timer is already on the message thread; the async shim was both unnecessary AND a UAF surface).
- **Bring-back from placeholder card:** clicking the card triggers `editor.bringBackRemotePopout(username)` or `editor.reattachGrid()`. The corresponding window is destroyed; the live tile/grid renders in-place again. **Codex H3 closure (2026-05-18):** "bring back" is the EXCLUSIVE destroy path. Clicking tile `↗` on a hidden popout RE-SHOWS it (state C → B, non-destructive).

</specifics>

<deferred>
## Deferred Ideas

- **Side-by-side layout** — User rejected in Area 1 (would squeeze VbFader width). Captured here in case beta UAT reveals stacked band doesn't work for some workflows.
- **Mode-toggle (video XOR mixer)** — Rejected in Area 1. Captured if beta testers ask for "full-screen video mode."
- **Resizable splitter (drag the divide between band and mixer)** — Rejected in favor of fixed-default + draggable band-bottom resizer (D-10). Splitter is more JUCE work for marginal benefit.
- **Hover-reveal popout icons** — Rejected in Area 1 in favor of always-visible icons. Captured if VB-style chrome feels too busy in beta.
- **Right-click context menu for popout/detach** — Rejected in Area 1. Captured if beta testers ask.
- **Double-click to pop out** — Rejected in Area 1. Could add as a secondary affordance later.
- **Tile add/remove animation** — Snap-reshuffle is the v1.3 default. Animation is polish, deferred to v1.4 if needed.
- **Audio mute/solo on tile** — Rejected in Area 3 (Medium chrome option). Captured if beta testers ask for "mute peer's audio from video tile."
- **VU meter on tile** — Rejected in Area 3. Could be a v1.4 polish item.
- **Color-coded sync state on username strip** — Rejected in Area 3 (Rich chrome). Could be a developer-mode toggle.
- **Decode-error badge on tile** — Rejected in Area 3. Diagnostic counters surfaced via UAT report, not user UI.
- **Full restore (auto-reopen popouts on DAW reload)** — Rejected in Area 4 in favor of bounds-only-no-auto-open. Captured if user feedback suggests "session pickup where you left off" is desired.
- **Server-scoped popout keys (server_address, username)** — Rejected in Area 4 in favor of username-only. Captured if NINJAM use later shows username-collision pain.
- **Empty-tile slots for non-broadcasting peers** — Rejected in Area 3 (D-12 → only active broadcasters). Captured if beta testers report "I want to know Dave is in the room even when his video is off."
- **Per-tile audio controls integration with mixer** — Deferred. Today the mixer is the audio surface; tiles are video-only.
- **Keyboard shortcuts** (e.g., `Ctrl+G` for grid toggle, `Ctrl+P` for popout active peer) — Deferred to v1.3 beta UAT feedback.
- **Multi-camera tile (PIP within self-tile)** — Phase 19 ships single-camera selection (D-17 auto-pick deviceIndex=0). If a multi-camera UI lands, the self-tile may need a camera-source selector.
- **Detached-grid window with its own controls (e.g., per-peer remove from grid)** — v1.3 detached-grid is a 1:1 mirror of the band. Custom layouts in detached mode is a v1.4+ idea.
- **(NEW from codex M6 review, 2026-05-18) Extract pure helpers `serializeVideoStateToValueTree(...)` / `deserializeVideoStateFromValueTree(...)`** — Currently the production processor's `<video>` save/load logic and the test's inline replica are independent code paths kept in sync via a source-level grep gate (M6 option (a)). Long-term cleaner discipline is option (b): extract the two pure helpers into `juce/state/VideoStateSerialization.{h,cpp}`, have BOTH production AND test call them. This eliminates the inline-replica drift class entirely. Deferred from v1.3 (cost: 2 new files + plumbing; benefit: structural; current grep gate is sufficient defense for v1.3 ship). Track as a v1.4 cleanup task.

</deferred>

---

*Phase: 22-native-video-ui-grid-popouts*
*Context gathered: 2026-05-17 · Codex `--reviews` replan applied: 2026-05-18 (H1+H2+H3+M4+M5+M6+M7+L8+L9)*
