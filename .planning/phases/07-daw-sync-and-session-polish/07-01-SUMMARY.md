---
phase: 07-daw-sync-and-session-polish
plan: 01
subsystem: audio, transport
tags: [juce, audioplayhead, daw-sync, atomic, transport, ninjam]

# Dependency graph
requires:
  - phase: 06-multichannel-output-routing
    provides: Multi-bus AudioProc call in processBlock, SPSC command/event queues, uiSnapshot atomics
provides:
  - AudioPlayHead transport query in processBlock with isPlaying passed to AudioProc
  - Single-atomic sync state machine (IDLE/WAITING/ACTIVE) with compare_exchange_strong
  - SyncCommand, SyncCancelCommand, SyncDisableCommand command types
  - BpmChangedEvent, BpiChangedEvent, SyncStateChangedEvent event types with SyncReason enum
  - NJClient SetIntervalPosition() API for sync offset alignment
  - BPM/BPI change detection in run thread with system chat messages
  - Session tracking fields (interval_count, session_elapsed_ms) in UiAtomicSnapshot
affects: [07-02 sync UI and vote controls, 07-03 session info strip]

# Tech tracking
tech-stack:
  added: []
  patterns: [single-atomic state machine with compare_exchange_strong, raw transport edge detection separate from overridden state, PPQ discontinuity detection for seek/loop]

key-files:
  created: []
  modified:
    - src/threading/ui_command.h
    - src/threading/ui_event.h
    - src/ui/ui_state.h
    - src/core/njclient.h
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - juce/NinjamRunThread.cpp

key-decisions:
  - "Single std::atomic<int> syncState_ replaces two-boolean approach to eliminate race between run thread auto-disable and audio thread WAITING->ACTIVE"
  - "Raw transport state (rawHostPlaying_) tracked separately from overridden hostPlaying for correct edge detection after WAITING->ACTIVE"
  - "SetIntervalPosition(int) added as inline public method on NJClient -- safe because only called from processBlock (same thread as AudioProc)"
  - "BPM comparison uses integer truncation per JamTaba pattern to avoid float comparison issues"
  - "Seek/loop detection uses PPQ delta threshold (2x expected + 1.0 tolerance) to auto-disable sync"

patterns-established:
  - "Single-atomic state machine: use compare_exchange_strong for all state transitions instead of separate boolean flags"
  - "Raw vs overridden transport: track raw host state separately when processBlock logic overrides values"
  - "BPM/BPI change detection: compare cached snapshot values before/after store in run thread loop"

requirements-completed: [SYNC-01, SYNC-02, SYNC-04, SYNC-05]

# Metrics
duration: 17min
completed: 2026-04-05
---

# Phase 7 Plan 01: Transport Sync Backbone Summary

**AudioPlayHead transport integration with single-atomic sync state machine, PPQ offset calculation, seek/loop handling, and BPM/BPI change detection with reason-aware events**

## Performance

- **Duration:** 17 min
- **Started:** 2026-04-05T10:03:55Z
- **Completed:** 2026-04-05T10:21:34Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- processBlock now queries AudioPlayHead and passes actual isPlaying to NJClient::AudioProc (was hardcoded true)
- Single-atomic sync state machine with compare_exchange_strong for race-safe IDLE/WAITING/ACTIVE transitions
- PPQ-based sync offset calculation adapted from JamTaba algorithm for JUCE AudioPlayHead::PositionInfo
- Seek/loop discontinuity auto-disables sync with TransportSeek reason via PPQ delta detection
- Run thread detects BPM/BPI changes and pushes events + system chat messages
- Server BPM change auto-disables active sync per D-05 with ServerBpmChanged reason
- Session tracking fields (interval_count, session_elapsed_ms) exposed in uiSnapshot

## Task Commits

Each task was committed atomically:

1. **Task 1: Command/event types with reason payloads, NJClient API, and UiAtomicSnapshot extensions** - `5601f1e` (feat)
2. **Task 2: processBlock AudioPlayHead integration, single-atomic sync state machine, raw transport edge detection, seek handling, and run thread BPM change detection** - `2fa2382` (feat)

## Files Created/Modified
- `src/threading/ui_command.h` - Added SyncCommand, SyncCancelCommand, SyncDisableCommand to UiCommand variant
- `src/threading/ui_event.h` - Added SyncReason enum, BpmChangedEvent, BpiChangedEvent, SyncStateChangedEvent to UiEvent variant
- `src/ui/ui_state.h` - Added interval_count and session_elapsed_ms to UiAtomicSnapshot
- `src/core/njclient.h` - Added SetIntervalPosition(int) public method for sync offset
- `juce/JamWideJuceProcessor.h` - Added syncState_ atomic, cachedHostBpm_, wasPlaying_, rawHostPlaying_, prevPpqPos_
- `juce/JamWideJuceProcessor.cpp` - AudioPlayHead query, transport edge detection, sync offset calc, seek handling
- `juce/NinjamRunThread.cpp` - Sync command handlers, BPM/BPI change detection, session tracking

## Decisions Made
- Single std::atomic<int> syncState_ replaces two-boolean approach (addresses review consensus #1)
- Raw transport state tracked separately from overridden state (addresses Claude MEDIUM review concern)
- SetIntervalPosition() is inline in header -- no njclient.cpp change needed (naturally serialized on audio thread)
- BPM comparison uses integer truncation: static_cast<int>(hostBpm) == static_cast<int>(serverBpm)
- PPQ seek detection threshold: ppqDelta < -0.01 (backward) or ppqDelta > expectedDelta * 2.0 + 1.0 (large jump)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- Build target name was `JamWide_VST3` not `JamWideJuce_VST3` as stated in plan verification commands. The CMakeLists.txt uses `JamWide` as the JUCE target name. Resolved by checking available targets with `cmake --build build-juce --target help`.
- JUCE build required separate `build-juce` directory with `-DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_BUILD_CLAP=OFF` since `build-clap` had `JAMWIDE_BUILD_JUCE=OFF` cached.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Transport sync backbone is complete -- Plan 02 can now build the Sync button UI in ConnectionBar
- All command/event types are registered -- Plan 02 can push SyncCommand and handle SyncStateChangedEvent
- BPM/BPI change events ready for BeatBar flash animation in Plan 02
- Session tracking fields ready for Session Info Strip in Plan 02

## Self-Check: PASSED

All 8 files found. Both commit hashes verified. All 6 key artifacts confirmed present.

---
*Phase: 07-daw-sync-and-session-polish*
*Completed: 2026-04-05*
