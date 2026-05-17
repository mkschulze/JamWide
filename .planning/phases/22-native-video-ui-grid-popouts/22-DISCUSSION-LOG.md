# Phase 22: Native Video UI (Grid + Popouts) - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-17
**Phase:** 22-native-video-ui-grid-popouts
**Areas discussed:** Grid placement vs the mixer, Tile layout + sizing, Tile chrome, Popout state persistence

---

## Grid placement vs the mixer

### Q1 — Coexistence with the mixer

| Option | Description | Selected |
|--------|-------------|----------|
| Stacked band above mixer | Toggleable video band between SessionInfoStrip and ChannelStripArea, ChatPanel sidebar pattern | ✓ (with clarification) |
| Side-by-side | Grid on left, mixer on right | |
| Mode toggle | Video OR mixer, never both | |
| Resizable splitter | User drags the divide | |

**User's choice:** Stacked band above mixer — clarified to ALSO support full-grid detach to its own window + per-user detachable windows (multi-monitor)
**Notes:** Initial recommendation lacked the detachable-grid + per-user-popout dimensions. User intent: three coexistent surfaces.

### Q2 — Mirror vs placeholder behavior when detached

| Option | Description | Selected |
|--------|-------------|----------|
| Mirror live frames | Both surfaces show live frames; juce::Image ref-counted | |
| Placeholder card | "Popped out →" / "Grid in window →" cards with bring-back | ✓ |
| Whole-band collapse | Detaching grid hides the in-main-view band | |

**User's choice:** Placeholder cards
**Notes:** Matches RESEARCH-ADDENDUM "UI rendering model [LOCKED-2026-05-15]" recommendation.

### Q3 — Affordance UI placement

| Option | Description | Selected |
|--------|-------------|----------|
| Always-visible corner buttons | Per-tile ↗ top-right, grid-band header ↗ next to band-toggle × | ✓ |
| Hover-reveal icons | Pop-out icon appears on hover | |
| Right-click context menu | Right-click → 'Pop out' / 'Detach grid' | |
| Double-click to pop out | Double-click triggers detach | |

**User's choice:** Always-visible corner buttons
**Notes:** Matches VB-Banana feel — controls visible, not hidden in menus.

### Q4 — Toggle placement + default behavior

| Option | Description | Selected |
|--------|-------------|----------|
| ConnectionBar button + auto-open on first frame | New 'Grid' button next to Camera, auto-opens on first peer first_frame_seen | ✓ |
| ConnectionBar button + manual only | Same button, no auto-open | |
| Band-edge collapse handle only | No top-bar entry; thin strip remains when collapsed | |
| Always-on auto | Auto-show whenever peers broadcast; no manual toggle | |

**User's choice:** ConnectionBar button + auto-open on first remote frame
**Notes:** Mirrors Camera button placement pattern; auto-open is the "happy path" affordance for new users.

---

## Tile layout + sizing

### Q1 — Tile size adaptation

| Option | Description | Selected |
|--------|-------------|----------|
| Fill-width adaptive | Compute N×M, scale tiles to fit | ✓ |
| Fixed 320×240 + horizontal scroll | Native source size, overflow scroll | |
| Fixed 320×240 + paginate | Paginated with prev/next buttons | |
| User S/M/L preset | Right-click chooses size | |

**User's choice:** Fill-width adaptive (FlexBox or hand-rolled)
**Notes:** Matches Zoom/Meet convention; adapts as peers join/leave.

### Q2 — Self-tile inclusion

| Option | Description | Selected |
|--------|-------------|----------|
| Peers only | DISP-01 reads "per-remote-user"; camera popout handles self | |
| Self + peers always | Self first slot in grid | ✓ |
| Self + peers, self toggleable | "Show self / Hide self" right-click toggle | |

**User's choice:** Self + peers (self always first slot)
**Notes:** Requires tile component to support both JamWideFrameDistributor (self) and JamWideRemoteFrameDistributor (peers). Cleanest cut = two specialized tile classes sharing a chrome base. Self-tile popout button reuses Phase 19's existing CameraPreviewWindow (no second self-popout).

### Q3 — Band height policy

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed default + draggable resizer | ~280px baseline with bottom-edge drag handle, persisted | ✓ |
| Auto-grow up to N rows + scroll | Mixer auto-shrinks as peers join | |
| Fixed no-resize | Locked height, tiles always scale | |
| Mixer-first | Band gets whatever's left after mixer min-height | |

**User's choice:** Fixed default band height (~280px) with draggable horizontal resizer on bottom edge; persisted across sessions
**Notes:** Predictable mixer behavior; user has recourse via drag-resizer.

---

## Tile chrome

### Q1 — Chrome density

| Option | Description | Selected |
|--------|-------------|----------|
| Minimal | username strip + ↗ + Phase 21 status overlays only | ✓ |
| Medium | minimal + tiny VU strip + peer-mute toggle | |
| Rich | medium + color-coded sync state + decode-error badge | |
| You decide | Claude picks | |

**User's choice:** Minimal — username strip (bottom, hover-hide) + always-visible top-right ↗ + Phase 21 overlays
**Notes:** Audio controls remain in ChannelStripArea where the user expects them.

### Q2 — Empty-state behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Tile per roster member always, "video starting…" fills empties | Predictable layout; uses deferredListeners_ side-table | |
| Only tiles for active broadcasters | Tile appears at first H264 BEGIN; disappears on stop | ✓ |
| Lazy band — show only when at least one broadcaster | Auto-hide when empty | |

**User's choice:** Only tiles for actively broadcasting peers
**Notes:** Self-tile also only when broadcast=ON (broadcast toggle, not just camera ON) — symmetric with peers. Phase 21's deferredListeners_ side-table still functional but not the primary path for Phase 22.

---

## Popout state persistence

### Q1 — Auto-restore on session reload

| Option | Description | Selected |
|--------|-------------|----------|
| Bounds yes, open-state no | Popouts start closed, remember position when opened | ✓ |
| Full restore | Reopen what was open + bounds | |
| Full restore + placeholder for absent peers | Aggressive restore | |
| Bounds yes + open-state yes only for detached-grid | Middle ground | |

**User's choice:** Bounds-only — popouts always start closed; remember position when opened
**Notes:** Mirrors Phase 19 D-10 privacy default ("Camera always starts OFF on launch"). Eliminates "surprise windows on DAW reload."

### Q2 — Per-peer popout key

| Option | Description | Selected |
|--------|-------------|----------|
| Username only | Map<username, bounds> | ✓ |
| (Server address, username) compound | Survives cross-server username collisions | |
| Don't persist per-peer | Default position always | |

**User's choice:** Username only
**Notes:** Matches user mental model "Dave's popout window." Cross-server collision is rare in NINJAM use.

---

## Claude's Discretion

- Band default height baseline (~280px likely)
- Detached-grid-window default monitor placement
- Mixer min-height clamp behavior on tight editor sizes
- Aspect-preservation policy (4:3 letterbox with side margins is natural default)
- Min tile size threshold before scroll-fallback (~120-160px)
- Inter-tile spacing/padding (~4-8px)
- "Popped out" placeholder card visual treatment
- Layout reshuffle animation on tile add/remove (snap-reshuffle for v1.3)
- Sink-add/remove notification mechanism: Timer-poll vs distributor event surface
- Stale popout bounds handling (juce::Component::setBoundsConstrained standard pattern)
- Aspect-lock policy for remote popouts (4:3 mirror of Phase 19 D-07)
- Keyboard shortcuts (none for v1.3 beta; revisit per UAT feedback)
- "Popped out window's explicit destroy" affordance design
- Detached-grid window title bar text

## Deferred Ideas

- Side-by-side layout (rejected; squeezes VbFader width)
- Mode-toggle (video XOR mixer) — rejected for v1.3; revisit if beta asks
- Resizable splitter between band and mixer — rejected in favor of fixed-default + drag-resizer
- Hover-reveal popout icons — captured if VB-style chrome feels too busy
- Right-click context menu for detach actions — captured if beta asks
- Double-click to pop out — captured as potential secondary affordance
- Tile add/remove animation — snap-reshuffle for v1.3
- Audio mute/solo on tile — rejected; mixer handles audio
- VU meter on tile — captured for v1.4 polish
- Color-coded sync state on username strip — captured as developer-mode toggle
- Decode-error badge on tile — diagnostic counters via UAT report, not user UI
- Full restore (auto-reopen popouts on DAW reload) — rejected for privacy default
- Server-scoped popout keys — captured if username-collision pain emerges
- Empty-tile slots for non-broadcasting peers — captured if beta testers want "Dave is in room but video off" indication
- Per-tile audio controls integration with mixer
- Keyboard shortcuts (deferred to v1.3 beta UAT feedback)
- Multi-camera tile (PIP within self-tile)
- Detached-grid window with its own controls (e.g., per-peer remove from grid)
