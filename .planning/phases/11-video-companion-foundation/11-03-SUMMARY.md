---
phase: 11-video-companion-foundation
plan: 03
subsystem: ui
tags: [juce, video, vdo-ninja, privacy-modal, browser-detect, websocket, connectionbar]

# Dependency graph
requires:
  - phase: 11-01
    provides: VideoCompanion class with WebSocket server, room ID derivation, browser launch
provides:
  - Video toggle button in ConnectionBar with 3-state UX (disabled/inactive/active)
  - VideoPrivacyDialog modal with IP disclosure and conditional browser warning
  - Cross-platform browser detection (macOS Launch Services, Windows registry, Linux fallback)
  - Full video launch flow wired through Processor/Editor/RunThread
  - Roster forwarding from run thread to VideoCompanion via thread-safe onRosterChanged
affects: [12-video-advanced, 13-video-popout]

# Tech tracking
tech-stack:
  added: [CoreServices (macOS, for LSCopyDefaultHandlerForURLScheme)]
  patterns: [VideoPrivacyDialog follows LicenseDialog modal pattern, BrowserDetect best-effort advisory pattern]

key-files:
  created:
    - juce/video/VideoPrivacyDialog.h
    - juce/video/VideoPrivacyDialog.cpp
    - juce/video/BrowserDetect.h
    - juce/video/BrowserDetect_mac.mm
    - juce/video/BrowserDetect_win.cpp
  modified:
    - juce/ui/ConnectionBar.h
    - juce/ui/ConnectionBar.cpp
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - juce/JamWideJuceEditor.h
    - juce/JamWideJuceEditor.cpp
    - juce/NinjamRunThread.cpp
    - CMakeLists.txt

key-decisions:
  - "BrowserDetect is best-effort advisory: never blocks video launch, defaults to assume Chromium on failure"
  - "Privacy modal shown on every new activation per session, skipped on re-click while active (D-04)"
  - "VideoCompanion owned by Processor, destroyed before OscServer and RunThread for clean lifecycle"
  - "Roster forwarding uses onRosterChanged (callAsync pattern) preserving SPSC cmd_queue invariant"

patterns-established:
  - "VideoPrivacyDialog: dark modal with scrim overlay, accept/decline buttons, escape key support -- follows LicenseDialog pattern"
  - "BrowserDetect: platform-conditional compilation via CMake (APPLE -> .mm, else -> .cpp with #if guards)"
  - "Video button state restoration on editor reconstruction (same pattern as sync state and routing mode)"

requirements-completed: [VID-01, VID-05, VID-06]

# Metrics
duration: 14min
completed: 2026-04-07
---

# Phase 11 Plan 03: Video UI Integration Summary

**Video toggle button in ConnectionBar with privacy modal, browser detection advisory, and full launch flow wired through Processor/Editor/RunThread**

## Performance

- **Duration:** 14 min
- **Started:** 2026-04-06T21:57:43Z
- **Completed:** 2026-04-07T00:12:00Z
- **Tasks:** 2 auto + 1 checkpoint (pending)
- **Files modified:** 13

## Accomplishments
- Video toggle button with 3 states (disabled/inactive/active) in ConnectionBar between OSC dot and Fit button
- VideoPrivacyDialog modal with IP disclosure always shown and browser compatibility warning shown conditionally
- Cross-platform browser detection: macOS via LSCopyDefaultHandlerForURLScheme, Windows via HKCU registry, Linux fallback to assume Chromium
- Full video launch flow: button click -> privacy modal -> accept -> launchCompanion -> browser opens -> button turns green
- Re-click while active re-opens browser without modal (D-04 semantics)
- Roster changes forwarded from run thread to VideoCompanion WebSocket clients
- Video deactivates on NINJAM disconnect (D-19)
- Port bind failure handled gracefully: launchCompanion return value checked, button stays inactive on failure

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement BrowserDetect and VideoPrivacyDialog** - `4687731` (feat)
2. **Task 2: Add Video button to ConnectionBar and wire full launch flow** - `5928429` (feat)
3. **Task 3: Verify complete video launch flow** - checkpoint:human-verify (pending)

## Files Created/Modified
- `juce/video/BrowserDetect.h` - Cross-platform browser detection API (best-effort advisory)
- `juce/video/BrowserDetect_mac.mm` - macOS implementation via LSCopyDefaultHandlerForURLScheme
- `juce/video/BrowserDetect_win.cpp` - Windows registry + Linux fallback implementation
- `juce/video/VideoPrivacyDialog.h` - Privacy modal dialog class declaration
- `juce/video/VideoPrivacyDialog.cpp` - Privacy modal with IP disclosure, browser warning, escape key, scrim
- `juce/ui/ConnectionBar.h` - Added videoButton member, onVideoClicked callback, setVideoActive method
- `juce/ui/ConnectionBar.cpp` - Video button setup, layout, enable/disable logic, disconnect cleanup
- `juce/JamWideJuceProcessor.h` - Added VideoCompanion include and unique_ptr member
- `juce/JamWideJuceProcessor.cpp` - VideoCompanion creation in constructor, reset in destructor
- `juce/JamWideJuceEditor.h` - Added VideoPrivacyDialog member
- `juce/JamWideJuceEditor.cpp` - Privacy modal wiring, video button callback, state restoration
- `juce/NinjamRunThread.cpp` - Roster forwarding to VideoCompanion via onRosterChanged
- `CMakeLists.txt` - Added VideoPrivacyDialog.cpp + platform-conditional BrowserDetect sources

## Decisions Made
- BrowserDetect defaults to true (assume Chromium) on any failure -- advisory warning never blocks the user
- VideoCompanion destroyed first in Processor destructor (before OscServer and RunThread) for clean WebSocket shutdown
- Privacy modal uses em dash in title per UI-SPEC copywriting contract
- Button layout: Video positioned between OSC status dot and Fit button (right-to-left order)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- IXWebSocket submodule was uninitialized in this worktree -- resolved by running `git submodule update --init libs/ixwebsocket`

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Human verification checkpoint pending (Task 3) -- full video flow ready for end-to-end testing
- Plans 01 (VideoCompanion core) and 02 (companion web page) already complete
- All three plans together deliver the complete VID-01 through VID-06 requirements

## Self-Check: PASSED

All 5 created files exist. Both task commits (4687731, 5928429) verified in git log. All 13 key content checks passed (BEST-EFFORT advisory, LSCopy, UrlAssociations, IP disclosure, browser warning, escape key, videoButton, videoCompanion, onRosterChanged, relaunchBrowser, launch return check, deactivate).

---
*Phase: 11-video-companion-foundation*
*Completed: 2026-04-07*
