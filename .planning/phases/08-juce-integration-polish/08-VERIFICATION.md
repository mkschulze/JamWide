---
phase: 08-juce-integration-polish
verified: 2026-04-05T00:00:00Z
status: gaps_found
score: 2/3 success criteria verified
re_verification: false
gaps:
  - truth: "Codec selector shows FLAC as the default on plugin load, matching NJClient's actual default encoder"
    status: failed
    reason: "Code explicitly defaults to Vorbis (ID 2). Commit 9c1d552 set FLAC default for CODEC-05, but commit 2c620b5 deliberately reverted to Vorbis for interop compatibility. The ROADMAP SC and REQUIREMENTS.md still describe FLAC as the required default. This is also an orphaned requirement — CODEC-05 appears in REQUIREMENTS.md traceability as a Phase 8 fix and in the ROADMAP success criteria, but no plan in Phase 08 claims it in its requirements frontmatter."
    artifacts:
      - path: "juce/ui/ConnectionBar.cpp"
        issue: "Line 90: codecSelector.setSelectedId(2) — hardcoded to Vorbis default"
      - path: "juce/JamWideJuceProcessor.cpp"
        issue: "Lines 46-47: comment says Vorbis default for compatibility, no FLAC initialization"
    missing:
      - "Decide the authoritative default: either revert to FLAC (matching CODEC-05 definition) or update REQUIREMENTS.md and ROADMAP success criteria SC#1 to reflect the intentional Vorbis-default decision"
      - "If FLAC default is chosen: set codecSelector.setSelectedId(1) in ConnectionBar.cpp and restore NJClient FLAC initialization in Processor.cpp"
      - "If Vorbis default is kept: update REQUIREMENTS.md CODEC-05 text, mark it with a note, and update ROADMAP Phase 8 SC#1 accordingly"
human_verification:
  - test: "Codec badge appears on remote channel strip"
    expected: "When a remote user is connected, their channel strip header shows 'FLAC' or 'Vorbis' badge text below the username"
    why_human: "Requires an active NINJAM session with at least one remote participant — cannot verify badge rendering without a live connection"
  - test: "Server error message shown in status label"
    expected: "On connection failure (wrong password, unreachable server), the status label shows the server-provided error text rather than the generic 'Connection failed' or 'Invalid credentials' string"
    why_human: "Requires triggering real NJC_STATUS_CANTCONNECT or NJC_STATUS_INVALIDAUTH events from a live server"
---

# Phase 08: JUCE Integration Polish Verification Report

**Phase Goal:** Wire remaining integration polish — codec badge on remote strips, error message surfacing
**Verified:** 2026-04-05
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (from ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Codec selector shows FLAC as the default on plugin load, matching NJClient's actual default encoder | FAILED | `ConnectionBar.cpp:90` sets `codecSelector.setSelectedId(2)` (Vorbis). Commit `9c1d552` set FLAC default but commit `2c620b5` deliberately overrode it to Vorbis "for compatibility". |
| 2 | Remote channel strips display the codec each participant is using (FLAC or Vorbis badge) | VERIFIED | Full data flow: `RemoteChannelInfo.codec_fourcc` field added (`njclient.h:187`), populated in `GetRemoteUsersSnapshot()` (`njclient.cpp:2754`), FOURCC decoded to string in `ChannelStripArea::refreshFromUsers()` (lines 383-385, 489-490, 509-510), passed to `ChannelStrip::configure()` which sets `codecLabel` text and visibility (`ChannelStrip.cpp:156-157`). FOURCC constants verified correct (little-endian: 0x43414C46 = 'FLAC', 0x7647474F = 'OGGv'). |
| 3 | Connection error messages from the server are displayed in the status label (not hardcoded strings) | VERIFIED | `lastErrorMsg` field added to processor (`JamWideJuceProcessor.h:112`). Editor `drainEvents()` stores `StatusChangedEvent::error_msg` into it (`JamWideJuceEditor.cpp:235-236`). `NinjamRunThread` prefers `GetErrorStr()` first, falls back to hardcoded text only when server provides nothing (`NinjamRunThread.cpp:269-275`). `ConnectionBar::updateStatus()` uses `lastErrorMsg` when non-empty for CANTCONNECT and INVALIDAUTH cases, falls back to generic text, clears on NJC_STATUS_OK (`ConnectionBar.cpp:372-395`). |

**Score:** 2/3 success criteria verified

### Required Artifacts

| Artifact | Role | Exists | Substantive | Wired | Status |
|----------|------|--------|-------------|-------|--------|
| `src/core/njclient.h` | `codec_fourcc` field on `RemoteChannelInfo` | Yes | Yes (field at line 187) | Yes (populated in njclient.cpp) | VERIFIED |
| `src/core/njclient.cpp` | Populate `codec_fourcc` in snapshot | Yes | Yes (line 2754) | Yes (flows to ChannelStripArea) | VERIFIED |
| `juce/ui/ChannelStripArea.cpp` | Decode FOURCC, pass to configure() | Yes | Yes (lines 383-385, 489-510) | Yes (calls strip->configure with codecStr) | VERIFIED |
| `juce/JamWideJuceProcessor.h` | `lastErrorMsg` member for cross-component error | Yes | Yes (line 112) | Yes (written by editor, read by ConnectionBar) | VERIFIED |
| `juce/JamWideJuceEditor.cpp` | Store error_msg from StatusChangedEvent | Yes | Yes (lines 235-236) | Yes (reads from event queue, writes processor) | VERIFIED |
| `juce/NinjamRunThread.cpp` | Prefer GetErrorStr() over hardcoded strings | Yes | Yes (lines 269-275) | Yes (pushes StatusChangedEvent with error_msg) | VERIFIED |
| `juce/ui/ConnectionBar.cpp` | Use lastErrorMsg in updateStatus() | Yes | Yes (lines 383-395) | Yes (reads processor.lastErrorMsg) | VERIFIED |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `njclient.cpp GetRemoteUsersSnapshot()` | `RemoteChannelInfo.codec_fourcc` | `ch_info.codec_fourcc = chan->codec_fourcc` | WIRED | Line 2754 |
| `ChannelStripArea.cpp refreshFromUsers()` | `ChannelStrip::configure()` | FOURCC decode + codecStr argument | WIRED | Lines 383-387, 488-514 |
| `NinjamRunThread.cpp` | `JamWideJuceProcessor.evt_queue` | `StatusChangedEvent.error_msg` push | WIRED | Lines 266-276 |
| `JamWideJuceEditor.cpp drainEvents()` | `processorRef.lastErrorMsg` | `std::is_same_v<T, StatusChangedEvent>` handler | WIRED | Lines 232-236 |
| `ConnectionBar::updateStatus()` | `processorRef.lastErrorMsg` | direct member read | WIRED | Lines 383-384, 391-392, 375 |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|-------------------|--------|
| `ChannelStrip.cpp` codec badge | `codecStr` / `codecLabel` | `chan->codec_fourcc` from active decode state in NJClient (set at `njclient.cpp:3376` during decode) | Yes — set from live network stream decode | FLOWING |
| `ConnectionBar.cpp` status label | `lastErrorMsg` | `GetErrorStr()` on NJClient (server-provided C-string from protocol) | Yes — real server error text when provided, hardcoded fallback otherwise | FLOWING |

### Behavioral Spot-Checks

Step 7b: SKIPPED — this is a JUCE audio plugin. There are no CLI entry points or runnable API servers to invoke without launching the full plugin host. Badge rendering and error display require a live NINJAM session.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CODEC-03 | 08-01-PLAN.md | User can switch between FLAC and Vorbis via UI toggle | SATISFIED | Codec badge wiring complete — badge reflects what remote peer is actually sending |
| UI-01 | 08-01-PLAN.md | Connection panel (server, username, password, connect/disconnect) | SATISFIED | Error messages now flow from server through StatusChangedEvent to status label |
| UI-03 | 08-01-PLAN.md | Status display (connection state, BPM/BPI, user count) | SATISFIED | Status label shows server-provided error text instead of hardcoded strings |
| CODEC-05 | ROADMAP Phase 8 (orphaned — not in any plan's requirements field) | Default codec is FLAC | BLOCKED | Current code defaults to Vorbis (`ConnectionBar.cpp:90`, `JamWideJuceProcessor.cpp:46-47`). Commit `9c1d552` implemented FLAC default; commit `2c620b5` deliberately reverted it. REQUIREMENTS.md marks it complete but implementation does not match the requirement text. |

**Orphaned requirements:** CODEC-05 is listed in REQUIREMENTS.md traceability as a "Phase 8 fix" and appears in the ROADMAP Phase 8 success criteria (SC#1), but does not appear in the `requirements` frontmatter of plan 08-01-PLAN.md. No plan in Phase 08 owns this requirement.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `juce/ui/ConnectionBar.cpp` | 90 | `setSelectedId(2)` — Vorbis hardcoded as default | Warning | Contradicts CODEC-05 requirement definition and ROADMAP SC#1. Intentional but undocumented decision in planning artifacts. |

No TODO/FIXME/PLACEHOLDER comments found in modified files. No stub return patterns found. The single anti-pattern flag above is a requirements contract contradiction, not a code quality issue.

### Human Verification Required

#### 1. Codec Badge Visible on Remote Strip

**Test:** Join a NINJAM session with at least one other participant connected. Observe the remote channel strip header area.
**Expected:** Each remote strip shows "FLAC" or "Vorbis" as a small badge label below the username. Badge should reflect the codec the remote user is actively sending.
**Why human:** Badge rendering requires a live NINJAM connection with active audio from a remote participant. Cannot simulate `codec_fourcc` being set without a real decode event from the server.

#### 2. Server Error Message in Status Label

**Test:** Attempt to connect with incorrect credentials, then with an unreachable server.
**Expected:** Status label displays the actual server-provided error string (e.g., "server is full", "incorrect protocol version", auth rejection reason) rather than generic "Connection failed" or "Invalid credentials".
**Why human:** Requires triggering `NJC_STATUS_CANTCONNECT` and `NJC_STATUS_INVALIDAUTH` from a real server to verify the `GetErrorStr()` path produces non-empty output and it flows through to the label.

### Gaps Summary

One gap blocks full goal achievement:

**CODEC-05 / SC#1 — Codec default contradiction.** The ROADMAP declares SC#1 as "Codec selector shows FLAC as the default on plugin load." REQUIREMENTS.md defines CODEC-05 as "Default codec is FLAC" and marks it complete via Phase 8. However, commit `2c620b5` (after the FLAC-default fix `9c1d552`) deliberately switched the default to Vorbis for NINJAM interop compatibility. The current code state contradicts the ROADMAP success criterion and the requirement definition.

This gap requires a decision rather than just a code fix:
- **Option A (Restore FLAC default):** Revert `2c620b5` changes to `ConnectionBar.cpp` and `JamWideJuceProcessor.cpp`. Accept that FLAC may cause interop issues with legacy Vorbis-only clients.
- **Option B (Accept Vorbis default):** Update REQUIREMENTS.md CODEC-05 text to "Default codec is Vorbis for interop compatibility (user can switch to FLAC)." Update ROADMAP Phase 8 SC#1 to match. Mark CODEC-05 as satisfied with the new definition.

The two implemented tasks (codec badge for CODEC-03, error message surfacing for UI-01/UI-03) are fully wired and substantive. Commits `46c7cf2` and `5464b4e` both exist and implement exactly what the plan describes.

---

_Verified: 2026-04-05_
_Verifier: Claude (gsd-verifier)_
