---
phase: 12-video-sync-and-roster-discovery
plan: 01
subsystem: video
tags: [websocket, sha256, juce_cryptography, vdoninja, buffer-delay]

# Dependency graph
requires:
  - phase: 11-video-companion-foundation
    provides: VideoCompanion WebSocket server, config message, roster broadcast
provides:
  - broadcastBufferDelay() method for interval-synced video buffering
  - deriveRoomPassword() SHA-256 derived password for VDO.Ninja room security
  - Companion URL password fragment (#password=) for passworded servers
  - BPM/BPI event forwarding from editor to VideoCompanion
affects: [12-02-plan, 13-video-display-modes-and-osc-integration]

# Tech tracking
tech-stack:
  added: [juce_cryptography]
  patterns: [SHA-256 derived password with truncation, URL fragment for secrets, cached state for late-connecting WS clients]

key-files:
  created: []
  modified: [CMakeLists.txt, juce/video/VideoCompanion.h, juce/video/VideoCompanion.cpp, juce/JamWideJuceEditor.cpp]

key-decisions:
  - "SHA-256 truncated to 16 hex chars (64 bits) for VDO.Ninja room password -- 2^48 times stronger than VDO.Ninja's internal 4 hex char (16 bit) truncation"
  - "Derived password appended as URL fragment (#password=) not query parameter -- fragment never sent to server per RFC 3986"
  - "Buffer delay cached on launch and broadcast, sent to new WS clients after config message"
  - "juce_cryptography include via JuceHeader.h (module linkage) not direct header include -- avoids redefinition conflict"

patterns-established:
  - "URL fragment for secrets: Use # not ? for sensitive data in companion URLs"
  - "Cached WS state: Cache computed values and send to newly connecting clients"
  - "Event forwarding: Editor drainEvents visitor forwards session events to VideoCompanion"

requirements-completed: [VID-08, VID-09]

# Metrics
duration: 8min
completed: 2026-04-07
---

# Phase 12 Plan 01: Buffer Delay Broadcast and Derived Room Password Summary

**VideoCompanion buffer delay calculation from BPM/BPI with WebSocket broadcast, plus SHA-256 derived room password appended as URL fragment for VDO.Ninja room security**

## Performance

- **Duration:** 8 min
- **Started:** 2026-04-07T21:04:41Z
- **Completed:** 2026-04-07T21:13:00Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- VideoCompanion computes buffer delay as floor((60/bpm)*bpi*1000) and broadcasts via WebSocket to all connected companion pages
- SHA-256 derived room password from NINJAM password + room ID, truncated to 16 hex chars, appended as URL fragment
- Editor BPM/BPI change events forwarded to VideoCompanion for live buffer delay updates
- Full input validation: NaN, zero, negative BPM/BPI guards prevent broadcast and crash
- Cached buffer delay sent to newly connecting WebSocket clients after config message
- All new state (password, derived password, cached delay) cleared on deactivate

## Task Commits

Each task was committed atomically:

1. **Task 1: Add juce_cryptography linkage, SHA-256 derived room password, buffer delay calculation and broadcast** - `45776dc` (feat)
2. **Task 2: Wire BPM/BPI change events to VideoCompanion buffer delay broadcast** - `519b50a` (feat)

## Files Created/Modified
- `CMakeLists.txt` - Added juce::juce_cryptography to JamWideJuce link libraries
- `juce/video/VideoCompanion.h` - Added broadcastBufferDelay(), deriveRoomPassword(), updated buildCompanionUrl signature, new member variables
- `juce/video/VideoCompanion.cpp` - Implemented buffer delay broadcast, SHA-256 derived password, updated URL builder with #password= fragment, cached delay on launch, delay sent on WS connect, state cleared on deactivate
- `juce/JamWideJuceEditor.cpp` - BpmChangedEvent and BpiChangedEvent handlers forward to VideoCompanion::broadcastBufferDelay

## Decisions Made
- Used juce_cryptography module (linked via CMake) for SHA-256 instead of WDL SHA-1 -- SHA-256 is stronger and matches protocol spec
- SHA-256 truncation to 16 hex chars (64 bits) explicitly justified: 2^48 times stronger than VDO.Ninja's internal 4 hex char truncation
- Derived password passed as URL fragment (#password=) not query parameter -- fragment never sent to server per RFC 3986 section 3.5
- Integer truncation (not rounding) for buffer delay milliseconds -- sub-ms precision irrelevant for multi-second video buffering
- Removed explicit juce_cryptography header include -- module is available via JuceHeader.h when linked, direct include causes redefinition error

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Removed explicit juce_cryptography header include**
- **Found during:** Task 1 (build verification)
- **Issue:** Plan specified `#include <juce_cryptography/hashing/juce_SHA256.h>` but JUCE modules included via target_link_libraries are automatically available through JuceHeader.h. Direct include caused `error: redefinition of 'SHA256'`.
- **Fix:** Removed the explicit include. juce::SHA256 is available through JuceHeader.h.
- **Files modified:** juce/video/VideoCompanion.cpp
- **Verification:** Build compiles cleanly with zero errors
- **Committed in:** 45776dc (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Auto-fix necessary for compilation. No scope creep.

## Issues Encountered
- CLAP submodule target naming conflict in worktree prevented cmake configure with default flags. Resolved by using `-DJAMWIDE_BUILD_CLAP=OFF` (matching main repo configuration). Pre-existing issue unrelated to this plan's changes.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- VideoCompanion now broadcasts buffer delay and includes derived room password in companion URL
- Plan 02 (TypeScript companion page) can implement bufferDelay message handling and password-based room joining
- Phase 13 can integrate video display modes knowing the buffer delay and password infrastructure is in place

## Self-Check: PASSED

All created/modified files verified present. All commit hashes verified in git log.

---
*Phase: 12-video-sync-and-roster-discovery*
*Completed: 2026-04-07*
