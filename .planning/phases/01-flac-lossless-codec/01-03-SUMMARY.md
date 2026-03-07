---
phase: 01-flac-lossless-codec
plan: 03
subsystem: ui
tags: [imgui, codec-selector, recording, flac, vorbis, ui-state, fourcc]

# Dependency graph
requires:
  - phase: 01-02
    provides: SetEncoderFormatCommand in SPSC queue, NJClient FLAC encode/decode paths, GetUserChannelCodec()
provides:
  - Codec selection combo box (FLAC/Vorbis) in local channel panel
  - Sender-side codec indicator badge
  - Receiver-side per-channel codec indicator with unsupported codec error display
  - Session recording checkbox (OGG + WAV)
  - UiState fields for codec index and recording toggle
  - UiRemoteChannel codec_fourcc field for remote codec display
affects: [04-PLAN]

# Tech tracking
tech-stack:
  added: []
  patterns: [ImGui combo for codec selection, FOURCC badge rendering, client_mutex for recording toggle]

key-files:
  created: []
  modified:
    - src/ui/ui_state.h
    - src/ui/ui_local.cpp
    - src/ui/ui_remote.cpp
    - src/core/njclient.h
    - src/core/njclient.cpp

key-decisions:
  - "Codec combo uses index-based selection (0=FLAC, 1=Vorbis) mapped to FOURCC on change"
  - "Recording checkbox directly sets config_savelocalaudio under client_mutex (2=OGG+WAV, 0=off)"
  - "Remote codec indicator reads FOURCC via GetUserChannelCodec() in render loop (client_mutex already held)"

patterns-established:
  - "Codec UI pattern: combo selection pushes SetEncoderFormatCommand, indicator badge reads active state"
  - "Recording toggle pattern: checkbox maps boolean to config_savelocalaudio int under mutex"

requirements-completed: [CODEC-03, REC-01, REC-02]

# Metrics
duration: 3min
completed: 2026-03-07
---

# Phase 1 Plan 03: Codec Selection UI, Indicators, and Recording Toggle Summary

**ImGui codec combo (FLAC/Vorbis), sender and receiver codec indicator badges, session recording checkbox, and human-verified end-to-end FLAC codec flow**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-07T10:17:00Z
- **Completed:** 2026-03-07T10:21:25Z
- **Tasks:** 3 (2 auto + 1 human-verify checkpoint)
- **Files modified:** 5

## Accomplishments
- Codec selection combo box in local channel panel defaults to FLAC, pushes SetEncoderFormatCommand on change
- Sender-side [FLAC] (green) / [Vorbis] (dimmed) indicator badge next to codec combo
- Receiver-side per-channel codec badges reading FOURCC from GetUserChannelCodec(), with red [Unsupported codec] error for unknown codecs
- Session recording checkbox toggling config_savelocalaudio (0=off, 2=OGG+WAV) with format label
- End-to-end FLAC codec flow verified by human in live session (checkpoint approved)

## Task Commits

Each task was committed atomically:

1. **Task 1: Add codec and recording UI state fields** - `b2aeb14` (feat)
2. **Task 2: Add codec toggle, indicators, and recording checkbox to UI panels** - `f75a72f` (feat)
3. **Task 3: Verify FLAC codec end-to-end in live session** - checkpoint:human-verify (approved, no code changes)

## Files Created/Modified
- `src/ui/ui_state.h` - Added local_codec_index (default 0=FLAC), recording_enabled bool, codec_fourcc to UiRemoteChannel
- `src/ui/ui_local.cpp` - Codec combo widget, sender-side codec badge, recording checkbox with mutex-protected config_savelocalaudio
- `src/ui/ui_remote.cpp` - Per-channel codec indicator badges (FLAC/Vorbis/Unsupported) via GetUserChannelCodec()
- `src/core/njclient.h` - GetUserChannelCodec(int useridx, int channelidx) method declaration
- `src/core/njclient.cpp` - GetUserChannelCodec() implementation reading FOURCC from RemoteUser_Channel download state

## Decisions Made
- Codec combo uses simple index-based selection (0=FLAC, 1=Vorbis) that maps to FOURCC constants when pushing SetEncoderFormatCommand -- matches the existing bitrate combo pattern
- Recording checkbox directly writes config_savelocalaudio under client_mutex rather than going through the SPSC command queue, since it is a simple int assignment and the mutex is held briefly
- Remote codec indicator calls GetUserChannelCodec() directly in the render loop rather than reading from UiState snapshot, because the render loop already holds client_mutex and has access to the NJClient

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Phase 1 (FLAC Lossless Codec) is fully complete -- all 7 requirements delivered (CODEC-01 through CODEC-05, REC-01, REC-02)
- The FLAC codec is the default, users can toggle to Vorbis, codec switching happens at interval boundaries, and session recording works
- Phase 2 (JUCE Scaffolding) can begin -- FLAC codec is independent of the JUCE migration and will carry forward unchanged
- The ImGui UI built here (codec combo, indicators, recording checkbox) will be reimplemented as JUCE Components in Phase 4

## Self-Check: PASSED

All files verified present, all commits verified in git log.

---
*Phase: 01-flac-lossless-codec*
*Completed: 2026-03-07*
