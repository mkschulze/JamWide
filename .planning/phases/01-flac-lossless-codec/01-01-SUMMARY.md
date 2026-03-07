---
phase: 01-flac-lossless-codec
plan: 01
subsystem: codec
tags: [flac, libflac, audio-codec, lossless, cmake, submodule, tdd]

# Dependency graph
requires: []
provides:
  - FlacEncoder class implementing I_NJEncoder (VorbisEncoderInterface)
  - FlacDecoder class implementing I_NJDecoder (VorbisDecoderInterface)
  - libFLAC 1.5.0 as git submodule with CMake integration
  - FLAC codec test infrastructure with 8 passing tests
affects: [01-02-PLAN, 01-03-PLAN]

# Tech tracking
tech-stack:
  added: [libFLAC 1.5.0]
  patterns: [streaming FLAC encode/decode via memory callbacks, TDD for codec classes]

key-files:
  created:
    - wdl/flacencdec.h
    - tests/test_flac_codec.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "Encoder/decoder scale factor uses 32767 (2^15 - 1) for symmetric float<->int16 conversion"
  - "Decoder loops process_single() until ABORT/END_OF_STREAM because libFLAC buffers read data internally"
  - "Extracted initEncoder()/initDecoder() helpers to deduplicate constructor and reinit/Reset code"

patterns-established:
  - "FlacEncoder/FlacDecoder mirror VorbisEncoder/VorbisDecoder structure: WDL_Queue for compressed I/O, static callbacks as class methods"
  - "FLAC test pattern: encode sine wave, feed compressed bytes to decoder, compare roundtrip within 1/32767 tolerance"
  - "JAMWIDE_BUILD_TESTS CMake option with per-codec test executables"

requirements-completed: [CODEC-01, CODEC-02]

# Metrics
duration: 18min
completed: 2026-03-07
---

# Phase 1 Plan 01: libFLAC + FlacEncoder/FlacDecoder Summary

**FlacEncoder and FlacDecoder classes with libFLAC 1.5.0, streaming encode/decode via memory callbacks, 16-bit lossless roundtrip verified by 8 TDD tests**

## Performance

- **Duration:** 18 min
- **Started:** 2026-03-07T09:35:19Z
- **Completed:** 2026-03-07T09:53:54Z
- **Tasks:** 2 (Task 2 was TDD with 3 sub-commits)
- **Files modified:** 3 created, 1 modified

## Accomplishments
- libFLAC 1.5.0 integrated as git submodule with all unnecessary build options disabled (WITH_OGG OFF, BUILD_CXXLIBS OFF, no programs/examples/tests/docs)
- FlacEncoder converts float PCM to 16-bit FLAC via FLAC__stream_encoder_init_stream with write callback appending to WDL_Queue
- FlacDecoder decodes FLAC stream via FLAC__stream_decoder_init_stream with read/write/metadata/error callbacks
- Encode/decode roundtrip preserves audio within 16-bit quantization tolerance (1/32767) for both mono and stereo
- Advance/spacing calling convention matches VorbisEncoder exactly (interleaved and planar layouts verified)

## Task Commits

Each task was committed atomically:

1. **Task 1: Add libFLAC submodule and CMake integration** - `7e492f4` (chore)
2. **Task 2: Implement FlacEncoder and FlacDecoder (TDD)**
   - RED: `9b020e2` (test) - Failing tests for all 8 behaviors
   - GREEN: `bf4f5f5` (feat) - Working implementation, all tests pass
   - REFACTOR: `b3ca0fd` (refactor) - Extract init helpers, clean up comments

## Files Created/Modified
- `wdl/flacencdec.h` - FlacEncoder and FlacDecoder classes (~260 lines) implementing I_NJEncoder/I_NJDecoder interfaces
- `tests/test_flac_codec.cpp` - 8 assert-based tests covering mono/stereo encoding, decoding, reinit, metadata, roundtrip, advance/spacing
- `CMakeLists.txt` - libFLAC subdirectory with options, FLAC linked to njclient, test target with JAMWIDE_BUILD_TESTS
- `libs/libflac/` - Git submodule (xiph/flac @ 1.5.0 tag)

## Decisions Made
- Used 32767 (not 32768) as symmetric scale factor for float-to-int16 and int16-to-float conversion, ensuring roundtrip precision within exactly 1 LSB
- Decoder's DecodeWrote() loops process_single() without checking m_inbuf.Available(), because libFLAC reads all available bytes into its internal buffer on the first read_cb call and processes from there
- Used simple assert-based testing rather than a test framework (Catch2/gtest) to keep test dependencies minimal

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed encoder/decoder scale factor mismatch**
- **Found during:** Task 2 (GREEN phase, roundtrip tests)
- **Issue:** Encoder scaled float by 32767 but decoder scaled int by 1/32768 (1 << 15), causing roundtrip error of ~0.00004554 exceeding 16-bit tolerance
- **Fix:** Changed decoder scale to 1/((1 << (bps-1)) - 1) = 1/32767 to match encoder
- **Files modified:** wdl/flacencdec.h
- **Verification:** Roundtrip tests now pass with max error within tolerance
- **Committed in:** bf4f5f5

**2. [Rule 1 - Bug] Fixed decoder not producing output (process_single loop)**
- **Found during:** Task 2 (GREEN phase, roundtrip tests)
- **Issue:** DecodeWrote() checked m_inbuf.Available() before each process_single() call, but libFLAC's read_cb consumes the entire buffer on first read, leaving m_inbuf empty while decoder still has internal buffered data to process
- **Fix:** Removed m_inbuf empty check; loop now continues until process_single returns false or decoder state is END_OF_STREAM/ABORTED
- **Files modified:** wdl/flacencdec.h
- **Verification:** All roundtrip tests produce decoded output
- **Committed in:** bf4f5f5

---

**Total deviations:** 2 auto-fixed (2 bugs found during TDD GREEN phase)
**Impact on plan:** Both fixes were necessary for correct codec behavior. No scope creep -- issues discovered and resolved within the planned TDD cycle.

## Issues Encountered
- Stale CMake build cache (previous build used Ninja generator, needed to clean build directory) -- resolved by removing build/ before reconfiguring
- VST3 SDK not found during CMake configure -- resolved by adding -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=TRUE flag

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- FlacEncoder and FlacDecoder classes are ready for Plan 02 to wire into NJClient's encode/decode paths
- The FLAC FOURCC, CreateFLACEncoder/CreateFLACDecoder macros, and njclient.cpp integration are Plan 02's scope
- Test infrastructure is in place; Plan 02 can add additional test targets under JAMWIDE_BUILD_TESTS

## Self-Check: PASSED

All files verified present, all commits verified in git log.

---
*Phase: 01-flac-lossless-codec*
*Completed: 2026-03-07*
