---
phase: 01-flac-lossless-codec
plan: 02
subsystem: codec
tags: [flac, njclient, encoder, decoder, fourcc, spsc-queue, codec-switching]

# Dependency graph
requires:
  - phase: 01-01
    provides: FlacEncoder/FlacDecoder classes in wdl/flacencdec.h implementing I_NJEncoder/I_NJDecoder
provides:
  - FLAC encode/decode paths wired into NJClient (encoder creation, decoder dispatch)
  - Codec format state management (m_encoder_fmt_requested atomic, m_encoder_fmt_active)
  - SetEncoderFormatCommand in SPSC command queue (UI to Run thread)
  - Chat notification on codec change
  - Default encoder format is FLAC (CODEC-05)
  - Interval-boundary codec switching (CODEC-04)
affects: [01-03-PLAN]

# Tech tracking
tech-stack:
  added: []
  patterns: [atomic format state with interval-boundary application, FOURCC-based codec dispatch]

key-files:
  created: []
  modified:
    - src/core/njclient.h
    - src/core/njclient.cpp
    - src/threading/ui_command.h
    - src/threading/run_thread.cpp

key-decisions:
  - "Include flacencdec.h before #undef of VorbisEncoderInterface/VorbisDecoderInterface macros so FlacEncoder/FlacDecoder resolve to I_NJEncoder/I_NJDecoder"
  - "Chat notification uses /me action format for natural session display"
  - "Decoder dispatch uses fourcc parameter for network streams, matched file type for disk files"

patterns-established:
  - "Codec dispatch pattern: branch on FOURCC at encoder creation and decoder creation, using CreateFLACEncoder/CreateFLACDecoder vs CreateNJEncoder/CreateNJDecoder macros"
  - "Format change detection at interval boundary: compare m_encoder_fmt_active vs m_encoder_fmt_requested, delete encoder to trigger recreation with new format"
  - "SetEncoderFormatCommand follows existing SPSC command pattern: struct in ui_command.h, variant member, if-constexpr handler in run_thread.cpp"

requirements-completed: [CODEC-04, CODEC-05]

# Metrics
duration: 4min
completed: 2026-03-07
---

# Phase 1 Plan 02: NJClient FLAC Integration Summary

**FLAC encoder/decoder wired into NJClient encode/decode paths with atomic format state, interval-boundary switching, SPSC command queue, and chat notification on codec change**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-07T09:58:04Z
- **Completed:** 2026-03-07T10:02:42Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- NJClient creates FlacEncoder when m_encoder_fmt_active is FLAC FOURCC, VorbisEncoder otherwise
- NJClient creates FlacDecoder when incoming message FOURCC is FLAC, VorbisDecoder otherwise (both network and file decode paths)
- Default encoder format is FLAC (m_encoder_fmt_requested initialized to NJ_ENCODER_FMT_FLAC in constructor)
- Codec switch only takes effect at interval boundary (encoder deleted when format changes, recreated at next encode cycle)
- Chat notification sent when codec changes ("/me switched to FLAC lossless" or "/me switched to Vorbis compressed")
- SetEncoderFormatCommand wired through SPSC queue from UI thread to Run thread

## Task Commits

Each task was committed atomically:

1. **Task 1: Add FLAC format state and methods to NJClient** - `8f0050f` (feat)
2. **Task 2: Add SetEncoderFormatCommand to SPSC command queue** - `cb5d95a` (feat)

## Files Created/Modified
- `src/core/njclient.h` - Added m_encoder_fmt_requested (atomic), m_encoder_fmt_active, m_encoder_fmt_prev, SetEncoderFormat(), GetEncoderFormat()
- `src/core/njclient.cpp` - FLAC FOURCC define, flacencdec.h include, FLAC encoder/decoder macros, format-branching encoder creation, FOURCC-dispatching decoder creation, interval-boundary format change detection, chat notification, constructor initialization
- `src/threading/ui_command.h` - SetEncoderFormatCommand struct and UiCommand variant entry
- `src/threading/run_thread.cpp` - SetEncoderFormatCommand handler calling client->SetEncoderFormat()

## Decisions Made
- Included flacencdec.h within the VorbisEncoderInterface/VorbisDecoderInterface macro scope (before #undef) so that FlacEncoder inherits from I_NJEncoder and FlacDecoder from I_NJDecoder -- the same class names the rest of njclient.cpp uses
- Chat notification uses "/me switched to X" format which renders as an action message in the chat system
- Decoder dispatch for network streams uses the fourcc from the message directly; for file-based decoding, uses the type that matched during the file probe loop

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed flacencdec.h include order for macro scope**
- **Found during:** Task 1 (build verification)
- **Issue:** Plan specified including flacencdec.h after the #undef of VorbisEncoderInterface/VorbisDecoderInterface. This caused "only virtual member functions can be marked 'override'" errors because FlacEncoder/FlacDecoder couldn't resolve VorbisEncoderInterface to I_NJEncoder (the actual class name after macro substitution)
- **Fix:** Moved the #include "../wdl/flacencdec.h" to before the #undef lines, within the macro scope
- **Files modified:** src/core/njclient.cpp
- **Verification:** Full project compiles with no errors
- **Committed in:** 8f0050f

---

**Total deviations:** 1 auto-fixed (1 blocking issue)
**Impact on plan:** Include order fix was necessary for compilation. No scope creep.

## Issues Encountered
None beyond the include order fix documented above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- NJClient now fully supports FLAC encoding and decoding at the protocol level
- SetEncoderFormatCommand provides the UI-to-engine path for codec selection
- Plan 01-03 can implement the ImGui codec selection UI, indicators, and recording toggle
- The full encode/decode path is ready: UI pushes SetEncoderFormatCommand -> Run thread calls SetEncoderFormat() -> m_encoder_fmt_requested updated atomically -> interval boundary detects change -> encoder recreated as FlacEncoder or VorbisEncoder -> FOURCC sent in upload message -> remote clients decode based on received FOURCC

## Self-Check: PASSED

All files verified present, all commits verified in git log.

---
*Phase: 01-flac-lossless-codec*
*Completed: 2026-03-07*
