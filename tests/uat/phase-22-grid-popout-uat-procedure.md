# Phase 22 — Native Video UI (Grid + Popouts) UAT Procedure

**Plan:** 22-04 Task 4 (`checkpoint:human-verify`, gate="blocking")
**Phase:** 22 — native-video-ui-grid-popouts
**Requirements verified:** DISP-01, DISP-02, DISP-03, DISP-04
**Estimated duration:** ~60-90 minutes (13 cells)

This procedure validates the end-to-end Phase 22 native video UI surface
(in-main-view grid band + per-peer popout DocumentWindows + detached-grid
DocumentWindow) against the populated public NinjamZap server
(`video.ninjamzap.com:2049`). It is the user-visible happy-path verification
required to close Phase 22 per `feedback_uat_scope_redflags` — Plan 22-01/02/03
unit tests + Plan 22-04 plugin-state inline-replica test are NOT a substitute
for live multi-peer + multi-monitor sign-off.

---

## Phase Closure Policy

Phase 22 closure REQUIRES one of:

- **State A — Full PASS:** all 13 cells marked PASS (or SKIPPED with justification
  per the per-cell prerequisites — only Cells where the operator's environment
  does NOT meet the stated prerequisites). Status line: `Phase 22 closed (Cells 1-13 PASS)`.

- **State B — Deferred-risk close:** 1-3 cells BLOCKED on environment (no
  coordinated peer / no second monitor / no DAW host installed). Each BLOCKED
  cell records the symptom + the environmental reason + a tracked follow-up
  pointer to Phase 24 BETA validation. Status line:
  `Phase 22 closed with deferred-risk Cells X, Y tracked in STATE.md → Phase 24`.

- **State C — FAIL:** any cell FAILs due to an actual bug → Phase 22 does NOT
  close; revise the relevant plan and re-execute.

**NOT acceptable:** "BLOCKED-but-closed" without a deferred-risk record (per
`feedback_uat_scope_redflags`).

---

## Prerequisites

1. macOS or Windows host, build present at:

   ```text
   build-juce/JamWideJuce_artefacts/Release/Standalone/JamWide.app
   build-juce/JamWideJuce_artefacts/Release/VST3/JamWide.vst3
   ```

   If missing, run `./scripts/build.sh JamWideJuce_VST3 JamWideJuce_Standalone`.

2. Internet connection that can reach `video.ninjamzap.com:2049`.

3. **For most cells:** ONE other peer broadcasting video into the same
   `jamwide-uat-22` room (a second JamWide instance on another machine, or a
   NinjamZap mobile peer). Coordinate the second peer out-of-band (Slack /
   SMS) before starting the UAT.

4. **For Cells 3, 6, 13:** TWO collaborators broadcasting simultaneously.

5. **For Cell 13:** TWO physical displays attached to the operator's machine.

6. Cell execution labels:
   - `EXECUTABLE_NOW` — runnable today on a single-host setup; no peer required.
   - `REQUIRES_COORDINATED_PEER` — needs at least 1 coordinated remote peer
     broadcasting video (Phase 21 receive path must be active for the tile
     to render).
   - `REQUIRES_MULTI_MONITOR` — needs ≥ 2 physical displays attached.

---

## Counter-readout pattern (all cells)

For diagnostic purposes when a cell BLOCKS, lldb-attach to read per-peer
counters via `JamWideRemoteFrameDistributor::findSink(username, chidx=1)`:

```sh
lldb -p $(pgrep -f "JamWide.app/Contents/MacOS/JamWide")

# Inside lldb (substitute appropriate symbol path):
(lldb) expr ((NJClient*)processor->client.get())->getRunVideoReceiveBlockMaxNanosForTest()
(lldb) expr $sink->first_frame_seen.load()
(lldb) expr $sink->hold_count.load()
(lldb) expr $sink->decode_error_count.load()
```

Sink-side counters require the `JamWideRemoteFrameDistributor::findSink`
accessor — added in Plan 21-03 Task 1, JAMWIDE_BUILD_TESTS-gated.

---

## Cell 1 — DISP-01 happy path (single peer)

**Label:** `REQUIRES_COORDINATED_PEER` (partial smoke `EXECUTABLE_NOW` for self-tile only)
**Maps to:** DISP-01, D-01, D-06, D-07, D-08, D-11, D-12

### Setup

1. Launch JamWide standalone. Open Camera (Phase 19 affordance) and verify the
   local preview window appears.
2. Connect to `video.ninjamzap.com:2049` room `jamwide-uat-22` with a unique
   username.
3. Toggle Camera Broadcast ON (Phase 20 affordance).
4. Coordinate with peer A to broadcast video into the same room.

### Steps

1. Wait for at least 1 collaborator's video to start broadcasting.
2. The band should auto-open within ~100ms of the peer's first H264 frame
   (verifies D-05 auto-open latch).
3. Confirm in the band: self-tile present (first slot) + peer A's tile present.
4. Confirm: each tile is 4:3 aspect; username strip visible at bottom of each;
   live H264 video rendering at native 320×240 (Phase 21 D-08 fixed sink).
5. **Codex L9 glyph check:** confirm the `↗` glyph in the top-right of each
   tile renders correctly. If you see a `□` tofu indicator or any missing-glyph
   artifact, RECORD as TYPOGRAPHY ISSUE — fallback fix is a small `Path`-drawn
   triangle. Acceptable for v1.3 beta sign-off if reported; mandatory fix for
   v1.3 release.

### Expected

- Peer A's username (without `@server` suffix) appears in the bottom strip.
- Live decoded H264 frame renders (320×240 native, 4:3 letterbox if band is wider).
- Top-right `↗` icon is always visible (D-04) AND renders as the unicode arrow
  (no tofu — codex L9).
- "video starting..." overlay disappears once `first_frame_seen` flips true.

### Record here

[PASS / FAIL / BLOCKED / SKIP — describe any visual artifact, freeze, wrong
username, or tofu glyph]

### Diagnostics if FAIL

- Run `lldb` and read `((PeerVideoSink*)sink)->first_frame_seen` — if false,
  the H.264 decode path never produced a frame (Phase 21 issue, not Phase 22).
- Check `((PeerVideoSink*)sink)->image_front` size — should be 320×240.

---

## Cell 2 — DISP-02 popout open + drag (single monitor)

**Label:** `REQUIRES_COORDINATED_PEER`
**Maps to:** DISP-02, D-08, D-09, D-15, D-17, T-22-MM

### Setup

1. Continue from Cell 1: band is open, peer A's tile is decoding.

### Steps

1. Click the `↗` icon in peer A's tile.
2. A `juce::DocumentWindow` titled `JamWide — <username>` opens at the
   default bounds `{100, 100, 320, 240}` (or persisted bounds if Cell 13
   already ran).
3. Verify: the popout window contains a live H264 tile decoding from peer A.
4. Drag the popout to a new screen position.
5. Resize the popout (4:3 aspect-locked per D-07 mirror).
6. Confirm: the in-band slot for peer A now shows a `"Popped out →"`
   placeholder card with click-to-bring-back affordance.
7. Click the placeholder. The popout is DESTROYED (codex H3 EXCLUSIVE destroy
   path) and the live tile rebinds in the band.
8. Click `↗` again. The popout reopens at the LAST bounds (verified by Plan
   22-04 D-19 persistence — needs full plugin restart to verify across-session).

### Expected

- Popout opens within ~100ms of `↗` click.
- Popout window can be dragged and resized; bounds change is visible.
- Closing via the placeholder destroys the window (state D); re-clicking
  `↗` creates a fresh popout at the LAST persisted bounds.

### Record here

[PASS / FAIL / BLOCKED / SKIP]

### Diagnostics if FAIL

- If the popout doesn't open: `((JamWideJuceEditor*)editor)->remotePopouts_.count(username)`
  should return 1.
- If the placeholder doesn't appear: check `((VideoGridBand*)gridBand_)->setPeerPoppedOut`
  was called with `true`.

---

## Cell 3 — DISP-03 grid + popouts coexist (two peers)

**Label:** `REQUIRES_COORDINATED_PEER` (needs 2 peers)
**Maps to:** DISP-03, D-01, D-03, D-17 (close hides, bring-back destroys)

### Setup

1. Coordinate with TWO collaborators (peer A + peer B) broadcasting in the
   same room.

### Steps

1. Verify: both peer tiles render in the band.
2. Click `↗` on peer A's tile. Popout A opens; peer A's band slot shows
   placeholder; peer B's band slot still shows live tile.
3. Click `↗` on peer B's tile. Popout B opens; peer B's band slot shows
   placeholder. BOTH popouts are visible simultaneously.
4. Drag popout B onto popout A's space (overlapping is fine — multi-window
   stack is OK).
5. Bring back popout A via its placeholder. Popout A destroyed; peer A's
   live tile rebinds in the band. Popout B remains open.

### Expected

- Both popouts are live and decoding simultaneously (DISP-03 hard requirement).
- Band placeholders correctly track per-peer popout state.
- Bring-back is per-peer (only the clicked peer's popout destroys).

### Record here

[PASS / FAIL / BLOCKED / SKIP]

---

## Cell 4 — DISP-04 toggle grid without disconnecting NINJAM

**Label:** `REQUIRES_COORDINATED_PEER`
**Maps to:** DISP-04, D-05

### Setup

1. Continue from Cell 1 or 3: band is visible, at least 1 peer broadcasting,
   NINJAM session live (you can hear peer's audio).

### Steps

1. Note the current beat counter in `BeatBar`.
2. Toggle the band OFF via the ConnectionBar Grid button (or band header ×).
3. Verify: band disappears; mixer expands vertically; AUDIO continues
   uninterrupted (still hear peer's audio).
4. Toggle the band ON again.
5. Repeat OFF/ON cycle 5 times rapidly.
6. Verify after each cycle: beat counter has NOT reset; peer audio is still
   live; peer is still in `ChannelStripArea`.

### Expected

- 5+ toggle cycles complete without peer drop from `ChannelStripArea`.
- Audio session continues across all toggles (no NINJAM disconnect).
- Beat counter advances monotonically through the cycles (jam clock keeps
  ticking).

### Record here

[PASS / FAIL / BLOCKED / SKIP]

### Diagnostics if FAIL

- If any peer drops from `ChannelStripArea`: `toggleGridBand` is touching
  NJClient state — DISP-04 hard violation. Check `awk` of `toggleGridBand`
  body for any `client.` or `NJClient::` references.

---

## Cell 5 — D-02 band placement between SessionInfoStrip and ChannelStripArea + visual

**Label:** `EXECUTABLE_NOW` (kBaseWidth visual check absorbed from Plan 22-02 deferred cell A)
**Maps to:** DISP-01, D-02

### Setup

1. Launch standalone WITHOUT connecting. The connection bar is at the top.

### Steps

1. Toggle Camera ON (Phase 19) so the self-tile is potentially visible.
2. Visually inspect the connection bar at the default `kBaseWidth = 1280`:
   - Camera button (right cluster) does NOT overlap any status label.
   - Grid button (right cluster) does NOT overlap any status label.
   - "Connected" / "Disconnected" status text remains fully readable.
3. Open the band via the Grid button (no peers yet — band shows just the
   self-tile if camera is broadcasting, or empty if not).
4. Verify: band appears BELOW the SessionInfoStrip (when visible) and ABOVE
   the ChannelStripArea. The mixer shrinks vertically when the band is open.
5. Drag the band's bottom resizer down. Mixer shrinks further.

### Expected

- D-02 layout order honored: ConnectionBar → BeatBar → SessionInfoStrip →
  Band → ChannelStripArea.
- Right cluster of ConnectionBar fits at 1280 with no overlap.

### Record here

[PASS / FAIL / BLOCKED / SKIP]

---

## Cell 6 — D-03 placeholder cards (peer popped out + grid detached)

**Label:** `REQUIRES_COORDINATED_PEER` (needs ≥ 1 peer + visual check)
**Maps to:** DISP-03, D-03, D-18 (M7 dual-band sync absorbed from Plan 22-03 deferred cell)

### Setup

1. Coordinate with peer A broadcasting.

### Steps

1. Open the band; verify peer A's tile is decoding.
2. Click `↗` on peer A's band-tile. Popout opens. Band slot now shows
   "Popped out →" placeholder card (D-03).
3. Click the band's `↗` (detach grid affordance). Detached-grid DocumentWindow
   opens. In-main-view band now shows ONE full-band "Grid is in detached
   window →" placeholder (D-18 / M7 — the entire band becomes a placeholder,
   not per-peer).
4. **M7 codex closure verification:** the detached-grid window itself shows
   peer A's slot ALSO as a "Popped out →" placeholder (dual-band sync — both
   surfaces must reflect that peer A is popped out).
5. Click the in-band whole-band placeholder. Detached-grid window destroyed;
   band reverts to per-peer rendering (peer A is still popped out — slot still
   shows "Popped out →").
6. Click peer A's per-peer placeholder. Popout destroyed; peer A's live tile
   rebinds.

### Expected

- Both placeholder forms render correctly with VB-style chrome.
- Clicking a placeholder is the EXCLUSIVE destroy path (codex H3).
- M7 dual-band sync: detached-grid placeholders mirror main-band placeholders.

### Record here

[PASS / FAIL / BLOCKED / SKIP — note M7 specifically]

---

## Cell 7 — D-05 auto-open latch (fires once per session)

**Label:** `REQUIRES_COORDINATED_PEER` (Plan 22-02 deferred cell E absorbed)
**Maps to:** DISP-01, D-05, M4 (lock-release-then-sync, no callAsync UAF)

### Setup

1. Coordinate with peer A: peer A is NOT yet broadcasting at T-0.
2. Launch JamWide standalone, connect to `jamwide-uat-22`, but do NOT open
   the band (verify band is hidden, GridButton shows "Grid" not "Grid (on)").

### Steps

1. At T+0, peer A starts broadcasting.
2. Wait. Within ~100ms of peer A's first H264 frame (`first_frame_seen` flips),
   the band should auto-open ONCE (D-05).
3. Verify: band appears; peer A's tile is live.
4. Close the band via the Grid button (explicit user close).
5. Peer A stops broadcasting; restarts. Peer A's `first_frame_seen` flips
   again (because Phase 21 lazy-decoder-startup re-allocates the sink on next
   BEGIN — or `first_frame_seen` may already be sticky-true for the session,
   depending on Phase 21 sink lifecycle).
6. Verify: band does NOT re-auto-open. The latch is once-per-session sticky
   (`gridAutoOpenLatchFired_` atomic exchanges to true on first fire).

### Expected

- Band auto-opens within ~100ms of peer A's first frame.
- Subsequent peer first-frames (even after explicit user close) do NOT re-fire
  the auto-open.
- No JUCE crash / message-thread UAF from the auto-open lambda (M4 codex
  closure — synchronous lock-release-then-sync pattern).

### Record here

[PASS / FAIL / BLOCKED / SKIP]

---

## Cell 8 — D-09 self-tile popout reuses CameraPreviewWindow

**Label:** `EXECUTABLE_NOW` (single-host smoke)
**Maps to:** DISP-02, D-09, M5 (typed dispatch — no `__self__` magic string)

### Setup

1. Launch standalone. Open Camera (Phase 19) so the self-camera is active.
2. Toggle Broadcast ON (Phase 20).

### Steps

1. Open the band. Self-tile appears in the first slot.
2. Click `↗` on the SELF tile.
3. Verify: NO new DocumentWindow opens. Instead, the existing Phase 19
   `CameraPreviewWindow` becomes visible (or toggles visibility if already
   visible — Phase 19 D-09 behavior).
4. Confirm: there is NO second self-popout window in the window manager.

### Expected

- Self-popout ↗ click reuses CameraPreviewWindow (D-09).
- M5 closure: dispatch via `VideoPopoutTargetKind::Self` enum, not a string
  comparison. A user named `__self__` (hypothetical) would NOT spoof this.

### Record here

[PASS / FAIL / BLOCKED / SKIP]

---

## Cell 9 — D-10 draggable band resizer + persistence

**Label:** `EXECUTABLE_NOW` (Plan 22-02 deferred cell F + Plan 22-04 D-19 persistence)
**Maps to:** DISP-01, D-10, D-19

### Setup

1. Launch standalone (no peer required for this cell).

### Steps

1. Open the band. Default height ≈ 280 px.
2. Drag the bottom edge of the band to ≈ 500 px.
3. Verify: band resizes; mixer shrinks proportionally.
4. Close JamWide (quit completely — File → Quit or kill window).
5. Re-launch JamWide standalone. Open the band again.
6. Verify: band's height is ≈ 500 px (persisted across reload via Plan 22-04
   D-19 — `processorRef.setVideoGridBandHeight` was wired in Plan 22-04 Task 1).

### Expected

- Band height jlimit-clamped to [140, 800] — try dragging beyond either bound
  and confirm clamp.
- Height persists across plugin/standalone restart.

### Record here

[PASS / FAIL / BLOCKED / SKIP]

---

## Cell 10 — D-13 sink-poll Timer mount/unmount on peer add/leave

**Label:** `REQUIRES_COORDINATED_PEER` (Plan 22-02 deferred cell H absorbed)
**Maps to:** DISP-01, D-13

### Setup

1. Launch standalone. Connect to `jamwide-uat-22`. Band OPEN before any peer.

### Steps

1. Coordinate with peer A to connect AFTER you have the band open.
2. Peer A joins and starts broadcasting.
3. Time observation: from peer A's first H264 frame arriving, peer A's tile
   should mount within ~33ms × 2 polls (the band's internal `juce::Timer`
   ticks at 30Hz). Practically: visible within a single frame budget.
4. Peer A disconnects. Their tile unmounts (D-12 — no empty slots).

### Expected

- Tile mount-on-peer-join visible within ~67ms.
- Tile unmount-on-peer-leave visible immediately (next poll tick).

### Record here

[PASS / FAIL / BLOCKED / SKIP]

---

## Cell 11 — H3 4-state truth table walk

**Label:** `REQUIRES_COORDINATED_PEER` (Plan 22-03 deferred Task 4 cell absorbed)
**Maps to:** DISP-02, codex H3

### Setup

1. Coordinate with peer A broadcasting.

### Steps

Walk the 4-state machine A → B → C → B → C → D by hand for one peer:

| Step | Action | Expected state transition | Visible result |
|------|--------|---------------------------|----------------|
| 1 | (initial) | (A) absent | Peer A's tile live in band; no popout |
| 2 | Click peer A's band-tile `↗` | A → B (CREATE+SHOW) | Popout opens; band shows placeholder |
| 3 | Click popout's `×` button | B → C (HIDE, no destroy) | Popout hidden; placeholder STILL visible |
| 4 | Click peer A's band-tile `↗` (popout currently hidden) | **C → B (RE-SHOW, non-destructive)** | **Popout re-shows at last bounds; placeholder still visible** |
| 5 | Click popout's `×` button again | B → C (HIDE) | Popout hidden again |
| 6 | Click peer A's placeholder card | **C → D (DESTROY)** | Popout destroyed; peer A's live tile rebinds in band |

### Expected

- Step 4 is THE H3 codex disambiguation: clicking `↗` on a hidden popout
  RE-SHOWS it; does NOT destroy. The destroy path is EXCLUSIVELY via
  placeholder click (Step 6).

### Record here

[PASS / FAIL / BLOCKED / SKIP — note specifically whether Step 4 RE-SHOWS or DESTROYS]

---

## Cell 12 — D-15 per-peer popout bounds persistence (M7 + D-19)

**Label:** `REQUIRES_COORDINATED_PEER` (Plan 22-03 deferred Task 4 cell absorbed)
**Maps to:** DISP-02, D-15, D-19

### Setup

1. Coordinate with peer A broadcasting.

### Steps

1. Click `↗` on peer A's tile. Popout opens at default bounds.
2. Drag the popout to position `{600, 400}` and resize to `{640, 480}`.
3. Close JamWide entirely.
4. Re-launch JamWide. Reconnect to `jamwide-uat-22`. Peer A is broadcasting.
5. Click `↗` on peer A's tile. Popout opens at `{600, 400, 640, 480}` (the
   bounds persisted via `processorRef.setRemotePopoutBounds(username, r)`
   in Plan 22-04 Task 1).

### Expected

- Per-peer popout bounds survive plugin reload (D-15 / D-19).
- Bounds keyed by username — peer B opens at peer B's own last-known bounds.

### Record here

[PASS / FAIL / BLOCKED / SKIP]

---

## Cell 13 — T-22-MM multi-monitor (drag popout to second display)

**Label:** `REQUIRES_MULTI_MONITOR` + `REQUIRES_COORDINATED_PEER` for full coverage; can also run partial single-host without peer for primary-display-fallback portion
**Maps to:** DISP-02, T-22-MM

### Setup

1. Two displays attached, system extended-desktop mode (NOT mirrored).
2. Peer A broadcasting (full coverage) or no peer (partial smoke).

### Steps (full coverage with peer)

1. Click `↗` on peer A's tile. Popout opens on primary display.
2. Drag popout to SECONDARY display. Resize. Bounds change is visible.
3. Close JamWide.
4. Re-launch JamWide. Reconnect. Click `↗` on peer A's tile.
5. Verify: popout opens on the SECONDARY display at last bounds.

### Steps (single-host fallback verification)

1. Close JamWide while secondary display has a popout's last-known bounds.
2. Physically disconnect the secondary display.
3. Re-launch JamWide. Connect. (Or open the camera popout — Phase 19 — to
   exercise the same multi-monitor clamp logic without a peer.)
4. Click `↗` on a tile (or open the camera popout).
5. Verify: the popout opens with bounds that intersect the PRIMARY display
   (T-22-MM mitigation — `Desktop::getDisplays()` clamp in the popout window
   constructor). If saved bounds reference the (now-disconnected) secondary
   display, the bounds are clamped to intersect the primary display, NOT
   opened off-screen.

### Expected

- Popout follows the operator across displays.
- Disconnecting the secondary display does NOT result in an off-screen popout
  (T-22-MM clamp keeps the window user-recoverable).

### Record here

[PASS / FAIL / BLOCKED / SKIP]

---

## Report write-up

Open `tests/uat/phase-22-grid-popout-uat-report.md` (template). For each Cell:

1. Record PASS / FAIL / BLOCKED / SKIP + observation notes.
2. If BLOCKED: write a deferred-risk record (environmental reason + tracked
   follow-up pointer to Phase 24 BETA validation).
3. If FAIL: write a root-cause hypothesis + proposed remediation scope. Do
   NOT close the phase.
4. Set the report status line to one of:
   - `Phase 22 closed (Cells 1-13 PASS)` — all cells PASS
   - `Phase 22 closed with deferred-risk Cells X, Y tracked in STATE.md → Phase 24`
     — some BLOCKED, all FAILs resolved
   - (Do NOT write a status line if any cell remains FAIL — phase NOT closed.)

Commit the report file separately from the procedure file so the procedure
stays clean for the next phase that reuses the pattern.

---

## Cell label legend (orchestrator routing)

| Cell | Label | Notes |
|------|-------|-------|
| 1 | `REQUIRES_COORDINATED_PEER` (partial `EXECUTABLE_NOW`) | Self-tile portion is single-host smoke; peer tile needs collaborator |
| 2 | `REQUIRES_COORDINATED_PEER` | Per-peer popout |
| 3 | `REQUIRES_COORDINATED_PEER` (2 peers) | Coexistence test |
| 4 | `REQUIRES_COORDINATED_PEER` | NINJAM session continuity |
| 5 | `EXECUTABLE_NOW` | Layout + ConnectionBar visual |
| 6 | `REQUIRES_COORDINATED_PEER` | Placeholder cards |
| 7 | `REQUIRES_COORDINATED_PEER` | Auto-open latch |
| 8 | `EXECUTABLE_NOW` | Self-tile popout reuses CameraPreviewWindow |
| 9 | `EXECUTABLE_NOW` | Band resizer + persistence (no peer needed) |
| 10 | `REQUIRES_COORDINATED_PEER` | Sink-poll add/remove |
| 11 | `REQUIRES_COORDINATED_PEER` | H3 4-state walk |
| 12 | `REQUIRES_COORDINATED_PEER` | Per-peer popout bounds persistence |
| 13 | `REQUIRES_MULTI_MONITOR` + `REQUIRES_COORDINATED_PEER` (full) / `REQUIRES_MULTI_MONITOR` (partial smoke) | Multi-monitor |

Cells executable NOW without a peer (single-host smoke): **1 (partial), 5, 8, 9, 13 (partial)**.
Cells requiring a coordinated peer: **1 (full), 2, 3, 4, 6, 7, 10, 11, 12, 13 (full)**.

---

## Plan 22-04 cross-references

- v4 → v5 schema migration (D-19): tests/test_plugin_state_v4_v5 has 10
  sub-tests covering defaults, round-trip, graceful upgrade, T-22-SP cap
  (200 → 64), T-22-SP username cap (300 → drop), T-22-SP bounds clamp,
  gridBandHeight clamp, detachedGridBounds clamp, write-side no-cap +
  read-side cap behavior. The M6 codex grep gate in Plan 22-04 Task 1
  acceptance criteria verifies the production source uses all 11 property
  names this replica reads/writes — catches schema-level drift at the
  source level before this UAT runs.
- T-22-MM multi-monitor (T-22-MM threat): Cell 13 + tests/test_remote_peer_popout_lifetime
  Test 5.
- L9 `↗` glyph rendering: Cell 1 step 5 explicitly verifies (codex L9
  closure).
