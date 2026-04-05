---
phase: 08
plan: 01
subsystem: ui, core
tags: [codec-badge, error-messages, integration-polish]
dependency_graph:
  requires: [01-02, 04-03, 05-01]
  provides: [CODEC-03, UI-01, UI-03]
  affects: [ChannelStripArea, ConnectionBar, NinjamRunThread]
tech_stack:
  added: []
  patterns: [snapshot-based-fourcc-propagation, server-error-surfacing]
key_files:
  created: []
  modified:
    - src/core/njclient.h
    - src/core/njclient.cpp
    - juce/ui/ChannelStripArea.cpp
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceEditor.cpp
    - juce/NinjamRunThread.cpp
    - juce/ui/ConnectionBar.cpp
decisions:
  - "codec_fourcc propagated via RemoteChannelInfo snapshot (not separate API call)"
  - "NinjamRunThread prefers GetErrorStr() over hardcoded strings for all error statuses"
  - "lastErrorMsg stored on processor for cross-component access (message thread only)"
metrics:
  duration: 3min
  completed: "2026-04-05T15:44:09Z"
  tasks: 2
  files: 7
---

# Phase 08 Plan 01: Wire Remote Codec Badge and Surface Error Messages Summary

Remote codec badges now display FLAC/Vorbis on remote channel strips by propagating codec_fourcc through the snapshot API; server error messages are surfaced in the status label instead of hardcoded text.

## Task Results

### Task 1: Wire Remote Codec Badge (CODEC-03)
**Commit:** 46c7cf2

Added `codec_fourcc` field to the `RemoteChannelInfo` struct and populated it from `RemoteUser_Channel::codec_fourcc` inside `GetRemoteUsersSnapshot()`. In `ChannelStripArea::refreshFromUsers()`, the FOURCC value is now decoded to "FLAC" (0x43414C46) or "Vorbis" (0x7647474F) and passed to `ChannelStrip::configure()` for both single-channel and multi-channel user paths. Multi-channel parent strips use the first channel's codec; child strips use per-channel codec values.

### Task 2: Surface Error Messages (UI-01, UI-03)
**Commit:** 5464b4e

Added `lastErrorMsg` field to `JamWideJuceProcessor` for cross-component error passing. The editor `drainEvents()` handler now stores `StatusChangedEvent::error_msg` into this field. `ConnectionBar::updateStatus()` uses the stored message for `NJC_STATUS_CANTCONNECT` and `NJC_STATUS_INVALIDAUTH` cases, falling back to generic text only if the server provided no error. Additionally, `NinjamRunThread` now calls `GetErrorStr()` first for all error statuses (previously only for the else branch), so server-provided messages like protocol version mismatch or auth rejection reasons are propagated. The error message is cleared on successful connect (`NJC_STATUS_OK`).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] NinjamRunThread error message priority**
- **Found during:** Task 2
- **Issue:** NinjamRunThread hardcoded error strings for CANTCONNECT and INVALIDAUTH before checking GetErrorStr(). This meant server-provided errors (e.g., "server is incorrect protocol version", auth rejection reasons) were always overridden by generic text.
- **Fix:** Reversed priority: check GetErrorStr() first, fall back to hardcoded text only when server provides nothing.
- **Files modified:** juce/NinjamRunThread.cpp
- **Commit:** 5464b4e

## Self-Check: PASSED

All 7 modified files verified present. Both commits (46c7cf2, 5464b4e) verified in git log.
