---
phase: 13-video-display-modes-and-osc-integration
plan: "02"
subsystem: osc-video-control
tags: [osc, video, touchosc, popout, companion]
dependency_graph:
  requires: [13-01]
  provides: [osc-video-addresses, touchosc-video-section, video-popout-osc]
  affects: [juce/video/VideoCompanion.h, juce/video/VideoCompanion.cpp, juce/osc/OscServer.h, juce/osc/OscServer.cpp, docs/osc.md, assets/JamWide.tosc]
tech_stack:
  added: []
  patterns: [osc-video-dispatch, cached-roster-lookup, osc-relaunch-privacy-gate, deactivate-broadcast-before-stop]
key_files:
  created: []
  modified:
    - juce/video/VideoCompanion.h
    - juce/video/VideoCompanion.cpp
    - juce/osc/OscServer.h
    - juce/osc/OscServer.cpp
    - docs/osc.md
    - assets/JamWide.tosc
decisions:
  - "OSC /video/active uses VideoCompanion::relaunchFromOsc() with stored session config instead of processor fields (addresses Codex review HIGH)"
  - "Privacy gate backed by explicit hasLaunchedThisSession_ flag -- OSC can only relaunch, never first-launch (addresses Codex review HIGH)"
  - "Grid=default, popout=per-user additive, no dedicated mode-switch OSC address (satisfies D-20, addresses Codex review HIGH)"
  - "Popout handler accepts 1-16 range (code), docs say {1-16}, template has 8 buttons (addresses Codex review MEDIUM bounds inconsistency)"
  - "storedPassword_ retained in memory for OSC relaunch -- acceptable since NJClient already holds same password (addresses Codex Round 2 MEDIUM)"
  - "deactivate() broadcasts {type:'deactivate'} before stopping WS server (D-13, Pitfall 5)"
metrics:
  duration: "5m49s"
  completed: "2026-04-07"
  tasks_completed: 2
  tasks_total: 2
requirements: [VID-11]
---

# Phase 13 Plan 02: OSC Video Control Addresses Summary

OSC video addresses for /JamWide/video/active and /JamWide/video/popout/{1-16} with privacy-gate relaunch pattern and TouchOSC VIDEO section

## What Was Done

### Task 1: VideoCompanion + OscServer video address handling (3a50e45)

**VideoCompanion extensions:**
- Added `requestPopout(streamId)` -- broadcasts `{type:'popout', streamId:'...'}` JSON to all connected WebSocket clients
- Added `getStreamIdForUserIndex(index)` -- looks up resolved stream ID from cached roster (message-thread safe)
- Added `relaunchFromOsc()` -- uses stored session config (storedServerAddr_, storedUsername_, storedPassword_) to relaunch without editor/privacy modal; returns false if video was never launched via UI this session (hasLaunchedThisSession_ flag)
- Updated `launchCompanion()` to store launch parameters and set hasLaunchedThisSession_ flag
- Updated `broadcastRoster()` to populate cachedRoster_ vector alongside JSON broadcast
- Replaced `deactivate()` to send `{type:'deactivate'}` to all WS clients before stopping server; intentionally preserves stored params across deactivate/reactivate cycle

**OscServer extensions:**
- Added `handleVideoOsc()` dispatching `/JamWide/video/active` (activate/deactivate via relaunchFromOsc) and `/JamWide/video/popout/{1-16}` (momentary trigger, bounds-checked)
- Added `sendVideoState()` providing bidirectional video active feedback via dirty-flag pattern
- Wired video prefix dispatch into `handleOscOnMessageThread()` (after remote, before session)
- Wired `sendVideoState()` into `timerCallback()` bundle send
- Added `lastSentVideoActive` dirty tracking member, reset on OSC start()

**Documentation:**
- Added Video Control section to docs/osc.md with correct {1-16} bounds
- Added display mode explanation (grid=default, popout=additive per-user)
- Updated Template Contents table to include VIDEO section

### Task 2: TouchOSC template VIDEO section (a196577)

- Added VIDEO group positioned in top-right area (x=526, y=4, 490x192) alongside Session/Master/Metro row
- ACTIVE toggle button: bidirectional, buttonType=1, send/receive enabled, green color, maps to /JamWide/video/active
- 8 popout momentary buttons: buttonType=0, send-only, blue color, maps to /JamWide/video/popout/1 through /JamWide/video/popout/8
- VIDEO and POPOUT section labels for visual clarity
- Template size grew from 5124 to 5768 bytes (compressed)

## Deviations from Plan

None -- plan executed exactly as written.

## Decisions Made

1. **OSC relaunch uses stored session config** -- VideoCompanion stores serverAddr, username, password during launchCompanion(). OSC relaunch uses these stored values, not processor fields (which may not exist for password). Addresses Codex review HIGH concern.
2. **Privacy gate via hasLaunchedThisSession_ flag** -- Explicit boolean flag set on first successful launchCompanion(). relaunchFromOsc() checks this flag first. Addresses Codex review HIGH concern about privacy modal bypass.
3. **Stored params survive deactivate()** -- storedServerAddr_, storedUsername_, storedPassword_, hasLaunchedThisSession_ are intentionally NOT cleared during deactivate(). This enables the full cycle: UI launch -> deactivate -> OSC relaunch.
4. **VIDEO section in top-right area** -- Placed at x=526 in the same row as Session/Master/Metro, utilizing the previously empty space on the right side of the template.

## Commits

| Task | Commit | Description |
|------|--------|-------------|
| 1 | 3a50e45 | feat(13-02): add OSC video control addresses and VideoCompanion popout/relaunch |
| 2 | a196577 | feat(13-02): add VIDEO section to TouchOSC template |

## Self-Check: PASSED

All 6 modified files exist. Both task commits (3a50e45, a196577) verified in git log. SUMMARY.md created.
