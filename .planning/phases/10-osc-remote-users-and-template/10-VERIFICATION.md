---
phase: 10-osc-remote-users-and-template
verified: 2026-04-07T18:28:38Z
status: human_needed
score: 7/8 must-haves verified
re_verification: false
human_verification:
  - test: "Import assets/JamWide.tosc into TouchOSC on an iPad or desktop. Confirm the layout renders: 8 remote user strips, 4 local channel strips, master section, metronome section, and session info panel with connect text field."
    expected: "Template imports without errors, layout shows all sections, labels are readable with dark Voicemeeter theme, server address text field is visible and editable."
    why_human: "Template file is valid zlib-compressed XML (43 addresses verified programmatically) but actual TouchOSC rendering cannot be tested without the TouchOSC app."
  - test: "Connect JamWide to a NINJAM server with other users. Type a server address in the TEXT field and send it. Move a remote user volume fader in TouchOSC and observe JamWide's mixer. Move a fader in JamWide and observe the TouchOSC fader."
    expected: "Remote user volume/pan/mute/solo changes bidirectionally. Remote user name labels update when participants join/leave."
    why_human: "Bidirectional OSC parameter binding requires live NINJAM session and physical control surface."
  - test: "Press the Disconnect button in TouchOSC. Verify JamWide disconnects from the server."
    expected: "JamWide disconnects; session/status indicator changes."
    why_human: "Requires live NINJAM session to observe disconnect behavior."
---

# Phase 10: OSC Remote Users and Template Verification Report

**Phase Goal:** Users can control remote participants via stable index-based OSC addressing and get started instantly with a shipped TouchOSC template
**Verified:** 2026-04-07T18:28:38Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (from ROADMAP.md Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | User can control a remote participant's volume/pan/mute/solo via `/JamWide/remote/{idx}/volume` (and similar) OSC addresses | VERIFIED | `OscServer.cpp:485` — `handleRemoteUserOsc` parses `/JamWide/remote/{idx}/volume|pan|mute|solo` and dispatches `SetUserStateCommand`/`SetUserChannelStateCommand` via `processor.cmd_queue.try_push`. `sendDirtyRemoteUsers` at line 707 sends outbound state with echo suppression. |
| 2 | User's TouchOSC layout updates with correct usernames when participants join or leave the session | VERIFIED | `OscServer.cpp:849` — `sendRemoteRoster` computes a hash (count + name lengths) per tick, only broadcasts when changed. `OscServer.h:120-121` stores `lastSentRosterCount`/`lastSentRosterHash`. Cache reset on roster change at line 870 prevents stale inherited state. |
| 3 | User can connect to and disconnect from a NINJAM server by sending an OSC trigger message | VERIFIED | `OscServer.cpp:634` — `handleOscStringOnMessageThread` handles `/JamWide/session/connect` with IPv6 + port validation, dispatches `ConnectCommand` to `cmd_queue`. Line 192 handles `/JamWide/session/disconnect` dispatching `DisconnectCommand`. |
| 4 | User can import the shipped `.tosc` template into TouchOSC and immediately control JamWide without manual layout creation | VERIFIED (programmatic) / NEEDS HUMAN (import test) | `assets/JamWide.tosc` exists (5124 bytes, 84316 bytes decompressed XML). All 26 spot-checked OSC addresses verified present. 1 TEXT node bound to `/JamWide/session/connect`. 8 remote slots with volume/pan/mute/solo/name, 4 local strips, master, metro, session info. Actual TouchOSC import requires human test. |

**Score:** 7/8 must-haves verified (all automated checks pass; 1 truth requires human validation of TouchOSC import)

### Plan 01 Must-Have Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Remote user volume/pan/mute controllable via `/JamWide/remote/{idx}/volume`, `/pan`, `/mute` | VERIFIED | `OscServer.cpp:574-605` dispatches `SetUserStateCommand` for each parameter |
| 2 | Remote user group solo via `/JamWide/remote/{idx}/solo` sets solo on ALL sub-channels simultaneously | VERIFIED | `OscServer.cpp:611-625` — iterates all sub-channels and sets solo on each via `SetUserChannelStateCommand` |
| 3 | Sub-channel control via `/JamWide/remote/{idx}/ch/{n}/volume` uses sequential 1-based indexing | VERIFIED | `OscServer.cpp:517-528` — `seqIdx = oscChIdx - 1`, resolves NINJAM bit index via `user.channels[seqIdx].channel_index` |
| 4 | Remote user names broadcast only when roster changes (dirty-flag, NOT every tick) | VERIFIED | `OscServer.cpp:854-863` — hash comparison gates broadcast |
| 5 | Connect trigger dispatches `ConnectCommand` with string parsing and validation | VERIFIED | `OscServer.cpp:634-700` — full IPv6/port validation, length limit (256 chars), dispatches to `cmd_queue` |
| 6 | Disconnect trigger dispatches `DisconnectCommand` | VERIFIED | `OscServer.cpp:192-197` — float trigger dispatches `DisconnectCommand` |
| 7 | Remote user VU meters broadcast every 100ms tick | VERIFIED | `OscServer.cpp:307` — `sendRemoteVuMeters` called in every `timerCallback` (no dirty-flag gating) |
| 8 | All cached send-state and echo suppression flags reset when roster changes | VERIFIED | `OscServer.cpp:867-872` — `lastSentRemoteUsers`, `lastSentRemoteChannels`, `remoteOscSourced` all reset on hash mismatch |

### Plan 02 Must-Have Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Template shows 8 remote user strips, 4 local strips, master, metronome, session info, connect/disconnect controls | VERIFIED (programmatic) | Node counts: BUTTON×28, LABEL×26, FADER×24, GROUP×16, ENCODER×13, TEXT×1. All 8 remote slots have volume/pan/mute/solo/name. All session addresses confirmed. |
| 2 | All fader/knob/button OSC addresses in the template match docs/osc.md namespace | VERIFIED | 26 address spot-checks all pass. `generate_tosc.py` validates 43 required addresses at generation time. |
| 3 | Template includes a TEXT field for server address and a connect button | VERIFIED | 1 TEXT node (`server_addr`) bound to `/JamWide/session/connect`, sends string `text` on ANY trigger. Disconnect BUTTON sends float to `/JamWide/session/disconnect`. |
| 4 | Template requires zero manual configuration beyond setting host IP in TouchOSC connection settings | VERIFIED | No hardcoded IP. Default text is `ninbot.com:2049`. Single-page layout with all controls pre-configured. |
| 5 | Remote VU meters intentionally omitted from template (documented in docs/osc.md) | VERIFIED | Template: `/JamWide/remote/1/vu/` NOT present. `docs/osc.md:204` — "Template note: The shipped TouchOSC template omits remote VU meters intentionally for layout density reasons." |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `juce/osc/OscServer.cpp` | `sendDirtyRemoteUsers` implementation | VERIFIED | Line 707 — full implementation with 8-slot iteration, group bus + sub-channel dirty tracking, echo suppression |
| `juce/osc/OscServer.h` | New method declarations, dirty-tracking structs | VERIFIED | Lines 49-57 — all 5 methods declared; lines 103-121 — echo suppression arrays, roster hash state |
| `src/core/njclient.h` | `RemoteUserInfo` with `float volume`/`float pan` | VERIFIED | Lines 180-181 — `float volume = 1.0f; float pan = 0.0f;` in `RemoteUserInfo` struct |
| `src/core/njclient.cpp` | Snapshot populates `info.volume`/`info.pan` | VERIFIED | Lines 2724-2725 — `info.volume = user->volume; info.pan = user->pan;` |
| `docs/osc.md` | Remote user address reference including roster, connect/disconnect | VERIFIED | Lines 172-224 — complete remote user address tables, session control, Template note |
| `assets/JamWide.tosc` | Valid zlib-compressed XML template | VERIFIED | 5124 bytes, decompresses to 84316-byte valid XML, 26/26 address spot-checks pass |
| `scripts/generate_tosc.py` | Reproducible generator with `def main` | VERIFIED | Line 603 — `def main()`, line 621 — `required_addresses` manifest, exits 0 with "All checks passed" |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `OscServer.cpp (sendDirtyRemoteUsers)` | `processor.cachedUsers` | Direct iteration | WIRED | Line 709 — `const auto& users = processor.cachedUsers` |
| `OscServer.cpp (handleRemoteUserOsc)` | `processor.cmd_queue` | `SetUserStateCommand` / `SetUserChannelStateCommand` dispatch | WIRED | Lines 526-625 — dispatches all four parameter types |
| `OscServer.cpp (handleOscStringOnMessageThread)` | `processor.cmd_queue` | `ConnectCommand` at line 696 | WIRED | Lines 696-700 — `ConnectCommand cmd; ... processor.cmd_queue.try_push(std::move(cmd))` |
| `assets/JamWide.tosc` | `docs/osc.md` namespace | All OSC addresses match documented namespace | WIRED | 26/26 spot-checks pass; `generate_tosc.py` validates 43 required addresses at build time |
| `OscServer.cpp (timerCallback)` | `sendDirtyRemoteUsers` / `sendRemoteVuMeters` / `sendRemoteRoster` | Called in timer loop | WIRED | Lines 305-307 |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `OscServer.cpp:sendDirtyRemoteUsers` | `processor.cachedUsers` | `GetRemoteUsersSnapshot` populates `cachedUsers` in `NinjamRunThread` | Yes — `njclient.cpp:2724` reads live `user->volume`/`user->pan` from NJClient state | FLOWING |
| `OscServer.cpp:sendRemoteRoster` | `users[i].name` | Populated from `NJ_GetUserName` in snapshot | Yes — live roster from NINJAM connection | FLOWING |
| `OscServer.cpp:handleRemoteUserOsc` → `cmd_queue` | `SetUserStateCommand.set_volume` / etc. | Parsed float from OSC message | Yes — routes to `NJ_SetUserChannelState` | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| `generate_tosc.py` exits 0 and self-validates | `python3 scripts/generate_tosc.py` | "Validated 43 required addresses / All checks passed" | PASS |
| `JamWide.tosc` is valid zlib with correct addresses | Python zlib decompress + 26-address check | All 26 checks pass including presence/absence constraints | PASS |
| All 8 remote slots have volume/pan/mute/solo/name | Python address scan per slot | 8/8 slots complete | PASS |
| Build compiles cleanly | `cmake --build . --parallel` | `[100%] Built target JamWideJuce_AU` — zero errors | PASS |
| TouchOSC import + bidirectional control | Requires physical TouchOSC + NINJAM session | N/A | SKIP (human required) |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|---------|
| OSC-04 | 10-01-PLAN.md | User can control remote users via index-based OSC addressing (`/remote/{idx}/volume`) | SATISFIED | `handleRemoteUserOsc` + `sendDirtyRemoteUsers` implement full bidirectional control |
| OSC-05 | 10-01-PLAN.md | User can see remote user names update on their control surface when roster changes | SATISFIED | `sendRemoteRoster` with hash-based dirty detection broadcasts `/JamWide/remote/{idx}/name` on change |
| OSC-08 | 10-01-PLAN.md | User can connect/disconnect from a NINJAM server via OSC trigger | SATISFIED | `handleOscStringOnMessageThread` + disconnect float trigger dispatch `ConnectCommand`/`DisconnectCommand` |
| OSC-11 | 10-02-PLAN.md | User can load a shipped TouchOSC template for immediate use with JamWide | SATISFIED (programmatic) / NEEDS HUMAN (live test) | `assets/JamWide.tosc` ships with all required controls; import not tested in TouchOSC |

All 4 requirement IDs declared across both plans (OSC-04, OSC-05, OSC-08, OSC-11) are covered. No orphaned requirements found for Phase 10 in REQUIREMENTS.md.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| None | — | — | — | — |

No TODOs, FIXMEs, placeholder returns, or stub patterns found in any Phase 10 modified files.

### Notable: Commit Hash Discrepancy in 10-01-SUMMARY.md

The 10-01-SUMMARY.md claims task commits `4f7dbd5` and `6b22d2e`. Actual commits in git history are `2984b2f` and `f792353` (same message content, same file changes). This was caused by the worktree merge (`b5c7010`) and subsequent regression fix (`8a3b2c7`). The code is present and correct; only the hashes in the SUMMARY are stale. Not a blocker.

### Human Verification Required

#### 1. TouchOSC Template Import

**Test:** Copy `assets/JamWide.tosc` to an iPad or desktop running TouchOSC. Use File > Import to load the template.

**Expected:**
- Template imports without errors
- Layout displays: session info panel (top-left), master + metronome (top-center/right), 4 local channel strips (middle row), 8 remote user strips (bottom row)
- Server address text field (`ninbot.com:2049` default) and Disconnect button visible in session panel
- Colors match dark Voicemeeter theme (green faders, red mute, yellow solo, cyan pan knobs)

**Why human:** zlib-compressed XML has been validated programmatically. Actual TouchOSC node rendering requires the TouchOSC app.

#### 2. Bidirectional Remote User Control

**Test:** Load JamWide in a DAW or standalone. Enable OSC (click the OSC dot in the footer, set receive port 9000 / send port 9001 / send IP to iPad IP). Connect to a NINJAM server with at least one other user. In TouchOSC, move a remote user volume fader. In JamWide, move the same user's volume fader.

**Expected:**
- Remote user name labels in TouchOSC update with connected usernames
- Moving fader in TouchOSC → volume changes in JamWide
- Moving fader in JamWide → fader moves in TouchOSC (bidirectional feedback)
- Mute and solo buttons work in both directions
- When a user joins/leaves, their name label in TouchOSC updates

**Why human:** Requires live NINJAM session and physical TouchOSC device.

#### 3. Connect/Disconnect Triggers

**Test:** In the TouchOSC template, clear the server address text field, type `ninbot.com:2049`, press Enter/confirm. Verify JamWide initiates a connection. Press the Disconnect button.

**Expected:** JamWide connects to the server. Disconnect button causes JamWide to disconnect. `/JamWide/session/status` label in TouchOSC reflects connection state changes.

**Why human:** Requires live NINJAM server and OSC network connection.

### Gaps Summary

No automated gaps found. All 5 new OscServer methods are implemented, all key links are wired, all artifacts are substantive, the build passes with zero errors, and all 43 OSC addresses in the template are validated. The phase goal is achieved at the code level. Human verification of TouchOSC import and live bidirectional control remains pending (Task 2 was acknowledged as a human-verify checkpoint in the plan).

---

_Verified: 2026-04-07T18:28:38Z_
_Verifier: Claude (gsd-verifier)_
