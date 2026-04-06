---
phase: 11-video-companion-foundation
plan: 01
subsystem: video
tags: [ixwebsocket, websocket, sha1, vdo-ninja, json-protocol, browser-launch]

# Dependency graph
requires: []
provides:
  - "VideoCompanion class with frozen public interface (consumed by Plan 03)"
  - "WebSocket server on 127.0.0.1:7170 for plugin-to-companion communication"
  - "Room ID derivation via WDL_SHA1 (D-16/D-17)"
  - "Username sanitization with deterministic collision resolution (D-22/D-23)"
  - "Config/roster JSON protocol (D-13/D-14)"
  - "IXWebSocket linked to JamWideJuce target"
affects: [11-02-video-companion-ui, 11-03-video-companion-wiring]

# Tech tracking
tech-stack:
  added: [IXWebSocket 11.4.6 (BSD-3, no-TLS mode)]
  patterns: [WebSocket server lifecycle with mutex-guarded start/stop, callAsync+alive_ UAF safety pattern]

key-files:
  created:
    - juce/video/VideoCompanion.h
    - juce/video/VideoCompanion.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "Include NJClient::RemoteUserInfo header directly (nested type requires complete type, cannot forward-declare)"
  - "Use WDL_SHA1 for room ID hashing (zero new deps, already linked in wdl target)"
  - "Fixed port 7170 for WebSocket server (unassigned by IANA, companion URL includes port param for future flexibility)"
  - "Password included in room ID hash by design (D-16: private servers with different passwords get unique rooms)"

patterns-established:
  - "VideoCompanion ownership pattern: processor owns unique_ptr, same as OscServer"
  - "Thread-safe roster dispatch: onRosterChanged copies vector, callAsync+alive_ flag marshals to message thread"
  - "JSON protocol: manual string construction for 2 flat message types (config, roster)"

requirements-completed: [VID-01, VID-02, VID-03]

# Metrics
duration: 9min
completed: 2026-04-06
---

# Phase 11 Plan 01: VideoCompanion Core Summary

**IXWebSocket submodule added and VideoCompanion class implemented with WebSocket server (127.0.0.1:7170), SHA-1 room ID derivation, username sanitization, config/roster JSON protocol, and frozen public interface for Plan 03**

## Performance

- **Duration:** 9 min
- **Started:** 2026-04-06T21:41:25Z
- **Completed:** 2026-04-06T21:50:42Z
- **Tasks:** 2
- **Files modified:** 3 (CMakeLists.txt, VideoCompanion.h, VideoCompanion.cpp)

## Accomplishments
- Added IXWebSocket as git submodule with TLS disabled and linked to JamWideJuce CMake target
- Implemented VideoCompanion class with frozen public interface documented for Plan 03 consumption
- WebSocket server binds to 127.0.0.1 only (T-11-01 mitigation), with graceful port-bind failure handling (T-11-03)
- Room ID derivation via WDL_SHA1 with password inclusion (D-16) and public salt "jamwide-public" (D-17)
- Username sanitization to alphanumeric + underscore with deterministic index-based collision resolution (D-22/D-23)
- Thread-safe roster dispatch via callAsync + alive_ flag (same pattern as OscServer, T-11-06 mitigation)
- Config JSON includes "noaudio":true for VDO.Ninja audio suppression (VID-03)
- Separate launchCompanion/relaunchBrowser methods (D-04: re-click re-opens browser only)
- Full build succeeds: JamWideJuce_VST3

## Task Commits

Each task was committed atomically:

1. **Task 1: Add IXWebSocket submodule and link in CMake** - `404fefb` (chore)
2. **Task 2: Implement VideoCompanion class with frozen public interface, WebSocket server, room ID, and JSON protocol** - `7b1f1a7` (feat)

## Files Created/Modified
- `libs/ixwebsocket/` - IXWebSocket submodule (BSD-3, WebSocket client+server library)
- `juce/video/VideoCompanion.h` - Frozen public interface: launchCompanion, relaunchBrowser, onRosterChanged, deactivate, isActive
- `juce/video/VideoCompanion.cpp` - Full implementation (298 lines): WS server, room ID, sanitization, JSON protocol, thread-safe roster
- `CMakeLists.txt` - IXWebSocket add_subdirectory + link + VideoCompanion.cpp in target_sources
- `.gitmodules` - IXWebSocket submodule registration

## Decisions Made
- Included `core/njclient.h` directly in VideoCompanion.h rather than forward-declaring NJClient, because `NJClient::RemoteUserInfo` is a nested type requiring complete type definition
- Used `strlen(utf8.getAddress())` for SHA1 input length instead of `sizeInBytes() - 1` to avoid potential off-by-one with null terminator semantics
- JSON string construction done manually (2 flat message types, no need for JSON library)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Changed NJClient forward declaration to full include**
- **Found during:** Task 2 (build verification)
- **Issue:** Plan specified forward-declaring `class NJClient` in the header, but `NJClient::RemoteUserInfo` is a nested type that requires the complete type definition. Build failed with "incomplete type named in nested name specifier."
- **Fix:** Replaced forward declaration with `#include "core/njclient.h"` in VideoCompanion.h. This follows the same pattern as other headers in the project (e.g., JamWideJuceProcessor.h already includes njclient.h).
- **Files modified:** juce/video/VideoCompanion.h
- **Verification:** Build succeeds after change
- **Committed in:** 7b1f1a7 (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minimal -- same include pattern used elsewhere in codebase. No scope creep.

## Issues Encountered
- Git submodules not initialized in worktree -- resolved by running `git submodule update --init --recursive` before build

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- VideoCompanion frozen public interface ready for Plan 03 wiring
- Plan 02 (UI: VideoPrivacyDialog, browser detection, ConnectionBar button) can proceed independently
- WebSocket server tested via build only (runtime testing requires companion page from Plan 02/03)

## Self-Check: PASSED

All files exist, all commits verified.

---
*Phase: 11-video-companion-foundation*
*Completed: 2026-04-06*
