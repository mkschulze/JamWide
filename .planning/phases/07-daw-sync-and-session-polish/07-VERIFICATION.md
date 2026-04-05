---
phase: 07-daw-sync-and-session-polish
verified: 2026-04-05T13:35:54Z
status: human_needed
score: 8/8 must-haves verified (automated); UI behaviors require human confirmation
re_verification: false
human_verification:
  - test: "Load plugin in a DAW (REAPER or Logic), hit play, then stop"
    expected: "Audio broadcasts while playing and silences when stopped; meter activity visible on remote participants"
    why_human: "Real-time AudioPlayHead behavior cannot be tested without a running DAW host"
  - test: "Click the Sync button when connected; observe state transitions"
    expected: "Button label changes color: grey (IDLE) -> amber WAITING -> green ACTIVE once DAW transport starts"
    why_human: "3-state visual feedback requires live plugin session with DAW transport control"
  - test: "Click BPM value in BeatBar and enter a new value, press Enter"
    expected: "!vote bpm N command appears in session chat; server confirms or rejects vote"
    why_human: "Vote round-trip requires live NINJAM server connection"
  - test: "Verify BeatBar flashes green when server changes BPM or BPI"
    expected: "BPM and BPI label flashes green for ~2.5 seconds on server-driven change"
    why_human: "Flash animation requires a server BPM/BPI change event during a live session"
  - test: "Right-click in plugin window and toggle Show Session Info"
    expected: "Info strip appears/disappears; visibility persists after closing and reopening the editor"
    why_human: "Persistence across editor reconstructions requires DAW save/load cycle"
  - test: "Launch plugin in standalone mode; connect to a NINJAM server"
    expected: "No Sync button visible; audio broadcasts continuously driven by server BPM (no transport control needed)"
    why_human: "Standalone pseudo-transport requires live connection to verify server BPM drives timing"
---

# Phase 7: DAW Sync and Session Polish Verification Report

**Phase Goal:** The plugin feels native in any DAW with transport-aware broadcasting, live tempo changes, and session tracking; research deliverables complete
**Verified:** 2026-04-05T13:35:54Z
**Status:** human_needed
**Re-verification:** No - initial verification

## Goal Achievement

### Observable Truths (Roadmap Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Plugin only broadcasts when DAW transport is playing; stopping silences the send | ? HUMAN | Code: `hostPlaying` passed to `AudioProc(..., false, hostPlaying)` (line 340). NJClient `process_samples` gates `bcast_active` on `isPlaying` (njclient.cpp lines 1947-1956). Wiring is complete; live DAW required to confirm behavior |
| 2 | BPM or BPI changes from the server apply at next interval boundary without reconnect | ✓ VERIFIED | `BpmChangedEvent`/`BpiChangedEvent` pushed from NinjamRunThread (line 362, 394). NJClient's `updateBPMinfo` is called by protocol handler at interval boundary (njclient.cpp line 1192). No reconnect needed — NINJAM protocol handles this natively |
| 3 | Session position (interval count, beat position) tracked and visible to the user | ✓ VERIFIED | `uiSnapshot.interval_count` and `session_elapsed_ms` populated by NinjamRunThread from `GetLoopCount()` and `GetSessionPosition()`. `SessionInfoStrip.update()` called with these values from editor timer. Strip renders Intervals, Elapsed, Beat fields |
| 4 | Standalone mode provides pseudo-transport driven by server BPM | ? HUMAN | Code: `hostPlaying` defaults to `true` when no AudioPlayHead available (standalone), so NJClient always broadcasts. NJClient's run thread drives internal timing from server BPM/BPI natively. Sync button hidden in standalone. Live standalone session needed to confirm |
| 5 | Video feasibility document, OSC evaluation matrix, and MCP assessment written and available | ✓ VERIFIED | All three files exist: `VIDEO-FEASIBILITY.md` (139 lines), `OSC-EVALUATION.md` (60 lines), `MCP-ASSESSMENT.md` (73 lines). All contain required content (VDO.Ninja, latency table, STUN/TURN, fixed rubric, use-case separation) |

**Score:** 5/5 truths verified programmatically where possible; 2 require human DAW session confirmation

### Required Artifacts (Plan 01)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/threading/ui_command.h` | SyncCommand, SyncCancelCommand, SyncDisableCommand types | ✓ VERIFIED | Lines 88-90: all three structs present; variant includes them (lines 103-105) |
| `src/threading/ui_event.h` | BpmChangedEvent, BpiChangedEvent, SyncStateChangedEvent with SyncReason | ✓ VERIFIED | SyncReason enum (line 60) with ServerBpmChanged (65), TransportSeek (66), HostTimingUnavailable (67); all three event structs present; added to UiEvent variant (lines 108-110) |
| `src/ui/ui_state.h` | UiAtomicSnapshot with interval_count and session_elapsed_ms | ✓ VERIFIED | Lines 130-131: both fields present as atomics |
| `juce/JamWideJuceProcessor.h` | syncState_ atomic, kSyncIdle/kSyncWaiting/kSyncActive, cachedHostBpm_, wasPlaying_, rawHostPlaying_ | ✓ VERIFIED | Lines 125-131, 164-172: all members present; no old syncWaiting_/syncActive_ found |
| `juce/JamWideJuceProcessor.cpp` | AudioPlayHead query, transport edge detection, sync offset calculation, seek/loop handling | ✓ VERIFIED | getPlayHead (line 216), rawHostPlaying_ edge detection (230-231), TransportSeek (263), SetIntervalPosition (314), compare_exchange_strong (321), `false, hostPlaying` in AudioProc call (340) |
| `src/core/njclient.h` | SetIntervalPosition(int) public method | ✓ VERIFIED | Line 166: `void SetIntervalPosition(int pos) { m_interval_pos = pos; }` |

### Required Artifacts (Plan 02)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `juce/ui/ConnectionBar.h` | syncButton member, updateSyncState handler | ✓ VERIFIED | Line 57: `juce::TextButton syncButton`; line 32: `void updateSyncState(int state)` |
| `juce/ui/BeatBar.h` | flashStartMs_, triggerFlash() | ✓ VERIFIED | Line 34: `double flashStartMs_ = 0.0`; line 18: `void triggerFlash()` |
| `juce/ui/SessionInfoStrip.h` | class SessionInfoStrip | ✓ VERIFIED | File exists; `class SessionInfoStrip : public juce::Component` confirmed |
| `juce/ui/SessionInfoStrip.cpp` | SessionInfoStrip::paint | ✓ VERIFIED | Line 22: `void SessionInfoStrip::paint(juce::Graphics& g)` — renders Intervals, Elapsed, Beat, Sync fields |
| `juce/JamWideJuceEditor.h` | sessionInfoStrip member, kSessionInfoStripHeight | ✓ VERIFIED | Line 45: `SessionInfoStrip sessionInfoStrip`; line 107: `static constexpr int kSessionInfoStripHeight = 20` |

### Required Artifacts (Plan 03)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `.planning/references/VIDEO-FEASIBILITY.md` | VDO.Ninja WebRTC sidecar analysis | ✓ VERIFIED | 139 lines; 26 occurrences of "VDO.Ninja"; contains "100-300ms" latency table; STUN/TURN mentioned 3 times; Recommendation and Open Questions sections present |
| `.planning/references/OSC-EVALUATION.md` | Per-DAW support matrix with fixed rubric | ✓ VERIFIED | 60 lines; "Bridge Feasibility" column present; covers REAPER, Logic, Ableton, Bitwig, Pro Tools, FL Studio, Cubase, Studio One (8 DAWs) |
| `.planning/references/MCP-ASSESSMENT.md` | MCP feasibility with transport/session/workflow separation | ✓ VERIFIED | 73 lines; 25 occurrences of MCP; "Transport sync", "Session control", "Workflow tooling" separation present; Recommendation section present |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `juce/JamWideJuceProcessor.cpp` | `src/core/njclient.h` | processBlock passes hostPlaying to AudioProc isPlaying param | ✓ WIRED | Line 340: `client->AudioProc(..., false, hostPlaying)` |
| `juce/NinjamRunThread.cpp` | `src/threading/ui_event.h` | BPM change detection pushes BpmChangedEvent | ✓ WIRED | Line 362: `processor.evt_queue.try_push(jamwide::BpmChangedEvent{prevBpm, newBpm})` |
| `juce/JamWideJuceProcessor.cpp` | `src/core/njclient.h` | Sync offset applied via SetIntervalPosition from processBlock | ✓ WIRED | Line 314: `client->SetIntervalPosition(newPos)` |
| `juce/ui/ConnectionBar.cpp` | `src/threading/ui_command.h` | Sync button pushes SyncCommand to cmd_queue | ✓ WIRED | Line 485: `processorRef.cmd_queue.try_push(jamwide::SyncCommand{})` |
| `juce/ui/BeatBar.cpp` | `src/threading/ui_command.h` | BPM/BPI inline edit pushes SendChatCommand with !vote | ✓ WIRED | Line 191-192: `"!vote bpm " + std::to_string(newVal)` in chat push |
| `juce/JamWideJuceEditor.cpp` | `juce/ui/SessionInfoStrip.h` | Editor lays out and updates SessionInfoStrip | ✓ WIRED | Line 37: addChildComponent; line 218: sessionInfoStrip.update(...) |
| `juce/JamWideJuceEditor.cpp` | `src/threading/ui_event.h` | Editor drains BpmChangedEvent and triggers BeatBar flash | ✓ WIRED | Line 251-253: `std::is_same_v<T, BpmChangedEvent>` -> `beatBar.triggerFlash()` |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `SessionInfoStrip` | intervalCount_, elapsedMs_, currentBeat_ | `uiSnapshot.interval_count` / `session_elapsed_ms` (NinjamRunThread writes from NJClient) | Yes — populated by `GetLoopCount()` and `GetSessionPosition()` on connected NJClient | ✓ FLOWING |
| `ConnectionBar.syncButton` | syncState (via updateSyncState) | `processorRef.syncState_` atomic (audio thread + run thread writes) | Yes — compare_exchange_strong transitions are wired | ✓ FLOWING |
| `BeatBar` (flash) | flashStartMs_ | `BpmChangedEvent` drained from evt_queue in editor timer | Yes — NinjamRunThread pushes event when server BPM changes | ✓ FLOWING |

### Behavioral Spot-Checks

Step 7b: SKIPPED — behaviors require running DAW host with AudioPlayHead; no runnable standalone check for transport state.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| SYNC-01 | 07-01 | Plugin reads host transport state via AudioPlayHead | ✓ SATISFIED | `getPlayHead()` query in processBlock, `pos->getIsPlaying()` used |
| SYNC-02 | 07-01 | Broadcasting only occurs when DAW is playing | ✓ SATISFIED | `isPlaying` param wired through AudioProc to `bcast_active` gating in NJClient |
| SYNC-03 | 07-02 | Session position tracked across intervals | ✓ SATISFIED | `interval_count`, `session_elapsed_ms` in uiSnapshot; displayed in SessionInfoStrip |
| SYNC-04 | 07-01, 07-02 | Live BPM/BPI changes applied at interval boundaries without reconnect | ✓ SATISFIED | NINJAM protocol applies BPM changes at interval boundary natively; BpmChangedEvent/BpiChangedEvent events push to UI and flash BeatBar |
| SYNC-05 | 07-01, 07-02 | Standalone mode provides pseudo-transport with server BPM | ✓ SATISFIED (code) | `hostPlaying` defaults to `true` in standalone (no AudioPlayHead); Sync button hidden; NJClient timing driven by server BPM. Live session needed to confirm broadcast behavior |
| RES-01 | 07-03 | Video feasibility document (JamTaba/VDO.Ninja analysis) | ✓ SATISFIED | `VIDEO-FEASIBILITY.md` (139 lines) with VDO.Ninja sidecar analysis, latency table, WebRTC vs JamTaba comparison, STUN/TURN privacy section |
| RES-02 | 07-03 | OSC cross-DAW sync evaluation (per-DAW support matrix) | ✓ SATISFIED | `OSC-EVALUATION.md` (60 lines) with 8 DAWs, fixed rubric columns (OSC Native / Transport Read / Transport Write / Tempo Access / Bridge Feasibility / Setup Burden) |
| RES-03 | 07-03 | MCP bridge feasibility assessment | ✓ SATISFIED | `MCP-ASSESSMENT.md` (73 lines) with transport/session/workflow separation, request/response latency explanation, future v3+ recommendation |

All 8 requirements (SYNC-01..05, RES-01..03) are accounted for. No orphaned requirements.

### Anti-Patterns Found

No stub patterns, TODO/FIXME comments, or empty implementations detected in any of the modified files:
- `src/threading/ui_command.h` — clean struct definitions
- `src/threading/ui_event.h` — clean enum and struct definitions
- `src/ui/ui_state.h` — clean atomic fields
- `juce/JamWideJuceProcessor.cpp` — substantive AudioPlayHead logic, no console.log stubs
- `juce/NinjamRunThread.cpp` — substantive BPM detection and event pushing
- `juce/ui/ConnectionBar.cpp` — functional Sync button with 3-state color logic
- `juce/ui/BeatBar.cpp` — functional flash animation and !vote dispatch
- `juce/ui/SessionInfoStrip.cpp` — functional paint with all required fields

### Human Verification Required

#### 1. Transport Gate: DAW Play/Stop Silences Broadcast

**Test:** Load plugin as VST3/AU in REAPER or Logic. Connect to a NINJAM server. Observe remote channel meter activity. Hit Play, then Stop.
**Expected:** Remote participants see the local user's audio appear on Play and disappear on Stop. Local user sees no encoding/broadcast while stopped.
**Why human:** AudioPlayHead provides transport state only when hosted inside a running DAW. Cannot simulate in isolation.

#### 2. Sync Button 3-State Visual Feedback

**Test:** While connected to a NINJAM server in a DAW, click the Sync button (visible in plugin mode). Leave DAW transport stopped, then press Play.
**Expected:** Button shows grey (IDLE) immediately after connection, amber "WAITING" after click, green "ACTIVE" once DAW transport starts.
**Why human:** Requires live plugin session with DAW transport control and visual inspection.

#### 3. BPM/BPI Vote Round-Trip

**Test:** Click the BPM value in BeatBar while in a session. Enter a new value. Press Enter.
**Expected:** `!vote bpm N` message appears in session chat. Server responds confirming or rejecting the vote.
**Why human:** Requires live NINJAM server connection to complete the vote round-trip.

#### 4. BeatBar Flash on Server BPM Change

**Test:** Join a session where someone votes to change BPM/BPI (or trigger it yourself).
**Expected:** BPM and/or BPI label in BeatBar flashes green for approximately 2.5 seconds.
**Why human:** Flash animation triggered by server-driven `BpmChangedEvent` — requires live server event.

#### 5. Session Info Strip Persistence

**Test:** Right-click in plugin window, toggle "Show Session Info". Close editor. Reopen editor (or save/reload DAW session).
**Expected:** Info strip visibility matches the last-selected state after editor is reconstructed.
**Why human:** Requires editor lifecycle (open/close/reopen) which cannot be driven programmatically without a running DAW.

#### 6. Standalone Pseudo-Transport

**Test:** Launch JamWide standalone. Connect to a NINJAM server. Verify audio broadcasts without pressing any play button. Verify no Sync button is visible.
**Expected:** Audio broadcast active immediately on connect, driven by server BPM. Sync button absent from UI.
**Why human:** Standalone audio broadcasting to server requires live NINJAM connection.

### Gaps Summary

No programmatically verifiable gaps found. All artifacts are substantive, wired, and data flows are connected. The 6 human verification items above are behavioral confirmations that require a running DAW or live NINJAM server — they are not code gaps.

The ROADMAP notes Phase 4 (Core UI Panels) and Phase 5 (Mixer) as "In Progress" and "Not started" respectively, but those are earlier phases, not gaps in Phase 7. Phase 7 was executed out of sequence relative to ROADMAP state — the code builds on Phase 6's infrastructure which is already in place.

---

_Verified: 2026-04-05T13:35:54Z_
_Verifier: Claude (gsd-verifier)_
