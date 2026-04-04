---
phase: 04-core-ui-panels
plan: 04
subsystem: ui
tags: [juce, server-browser, license-dialog, editor-assembly, timing-guide-removal, overlays]

# Dependency graph
requires:
  - phase: 04-core-ui-panels
    plan: 02
    provides: ConnectionBar, ChatPanel, Editor skeleton with event drain
  - phase: 04-core-ui-panels
    plan: 03
    provides: BeatBar, ChannelStripArea, VuMeter, ChannelStrip

provides:
  - ServerBrowserOverlay with server list, single-click fill, double-click auto-connect
  - LicenseDialog with Accept/Decline only (no outside-click dismiss)
  - Complete composed editor with all Phase 4 panels
  - Timing Guide removal (D-31)

affects:
  - juce/JamWideJuceEditor.h
  - juce/JamWideJuceEditor.cpp
  - CMakeLists.txt

# Tech stack
added: []
patterns: [overlay-component-pattern, scrim-intercept-for-modal-blocking]

# Key files
created:
  - juce/ui/ServerBrowserOverlay.h
  - juce/ui/ServerBrowserOverlay.cpp
  - juce/ui/LicenseDialog.h
  - juce/ui/LicenseDialog.cpp
modified:
  - juce/JamWideJuceEditor.h
  - juce/JamWideJuceEditor.cpp
  - CMakeLists.txt
  - src/ui/ui_local.cpp
deleted:
  - src/ui/ui_latency_guide.cpp
  - src/ui/ui_latency_guide.h

# Decisions
decisions:
  - LicenseDialog has no mouseDown override -- scrim intercepts clicks but only Accept/Decline can dismiss
  - Double-click auto-connect uses existing password from ConnectionBar.getPassword()
  - Chat toggle redistributes space within existing bounds (no setSize call) -- REVIEW FIX #5
  - ChannelStripArea VU updates driven by its own 30Hz timer, not editor timerCallback
  - Overlay components use addChildComponent (hidden) and show/dismiss pattern

# Metrics
duration: 9min
completed: "2026-04-04T11:13:00Z"
status: checkpoint-pending
---

# Phase 04 Plan 04: Final Assembly + Overlays Summary

ServerBrowserOverlay with ListBox model, LicenseDialog with modal scrim blocking, editor composition of all Phase 4 panels with chat toggle and overlay wiring, Timing Guide code removal (D-31).

## Task Completion

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | ServerBrowserOverlay + LicenseDialog + Timing Guide removal | eacc24f | ServerBrowserOverlay.h/cpp, LicenseDialog.h/cpp, CMakeLists.txt, ui_local.cpp, ui_latency_guide.h/cpp (deleted) |
| 2 | Final editor assembly with all panels and overlays | ea8550e | JamWideJuceEditor.h, JamWideJuceEditor.cpp |
| 3 | Visual verification checkpoint | pending | Awaiting human verification |

## Implementation Details

### ServerBrowserOverlay (D-11, D-12, D-13, D-14)
- ListBoxModel with 56px row height showing server name, address, users, BPM/BPI, topic
- Single-click calls onServerSelected -> fills address into ConnectionBar
- Double-click calls onServerDoubleClicked -> fills address + auto-connects with existing password
- Outside-click and Escape key dismiss the overlay
- Scrim with kSurfaceScrim, dialog body at kSurfaceOverlay with 8px rounded corners
- Loading state shows "Loading servers..." status

### LicenseDialog (D-19, D-20, D-21)
- Modal overlay with Accept (green) and Decline (red) buttons
- NO mouseDown override -- scrim intercepts all clicks but only buttons can dismiss
- This prevents accidental dismissal while run thread is blocked on license_cv
- Accept sets license_response to 1, Decline sets to -1, both notify license_cv

### Final Editor Assembly
- Layout: ConnectionBar(44px) + BeatBar(22px) + ChannelStripArea(center) + ChatPanel(260px right)
- Chat toggle button redistributes space within existing bounds (REVIEW FIX #5: no setSize)
- BeatBar updates from uiSnapshot atomics at 20Hz
- ChannelStripArea.onBrowseClicked wired for D-29 empty state browse
- ServerBrowser/LicenseDialog are addChildComponent (hidden), shown via show()/dismiss()
- drainEvents handles UserInfoChangedEvent -> refreshChannelStrips
- drainEvents handles ServerListEvent -> updates browser if showing
- pollStatus transitions: connected -> channelStripArea.setConnectedState + chatPanel.setConnectedState

### Timing Guide Removal (D-31)
- Deleted src/ui/ui_latency_guide.cpp and src/ui/ui_latency_guide.h
- Removed include from src/ui/ui_local.cpp
- Removed Timing Guide toggle checkbox and rendering block from ui_local.cpp
- Removed from CMakeLists.txt CLAP target (jamwide-ui)
- UiState latency fields preserved (still used by CLAP audio processing code)

## Review Fixes Applied
- REVIEW FIX: LicenseDialog has no outside-click dismiss (prevents hanging run thread)
- REVIEW FIX: Double-click auto-connect uses connectionBar.getPassword() (not empty string)
- REVIEW FIX #5: Chat toggle does not call setSize -- redistributes within existing bounds
- REVIEW FIX #7: VU updates driven by ChannelStripArea 30Hz timer, not editor

## Deviations from Plan

None -- plan executed exactly as written.

## Known Stubs

None -- all components are fully wired to processor data sources.

## Checkpoint

Task 3 is a human-verify checkpoint. The user needs to launch the standalone app and visually verify the complete Phase 4 UI before this plan can be marked complete.

## Self-Check: PASSED

All 6 created/modified files verified present. Both deleted files confirmed removed. Both commit hashes verified in git log.
