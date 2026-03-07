---
phase: 03-njclient-audio-bridge
plan: 01
subsystem: audio
tags: [njclient, audioproc, juce, processblock, spsc, command-queue]

# Dependency graph
requires:
  - phase: 02-juce-scaffolding
    provides: "JamWideJuceProcessor skeleton, NinjamRunThread lifecycle, APVTS parameters, pluginval CI"
provides:
  - "NJClient ownership in Processor (unique_ptr, constructor init with FLAC/workdir)"
  - "processBlock -> AudioProc bridge with in-place buffer safety"
  - "APVTS parameter sync to NJClient atomics (masterVol, masterMute, metroVol, metroMute)"
  - "NinjamRunThread run loop with NJClient::Run() under clientLock"
  - "Command queue dispatch (Connect, Disconnect, SetEncoderFormat, SendChat, SetLocalChannelInfo, SetUserChannelState)"
  - "Chat callback (no-op stub) and license callback (auto-accept)"
affects: [04-ui-framework, 05-mixing-routing]

# Tech tracking
tech-stack:
  added: []
  patterns: [processBlock-AudioProc-bridge, spsc-command-dispatch, in-place-buffer-safety, adaptive-sleep-run-loop]

key-files:
  created: []
  modified:
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - juce/NinjamRunThread.h
    - juce/NinjamRunThread.cpp

key-decisions:
  - "MAKE_NJ_FOURCC defined locally in processor .cpp (macro is private to njclient.cpp)"
  - "AudioProc called without clientLock from processBlock (AudioProc is designed lock-free)"
  - "License auto-accepted in Phase 3 (Phase 4 adds proper UI dialog)"
  - "Chat callback is no-op stub (Phase 4 adds chat integration)"
  - "inputScratch buffer with setSize(keepExisting=true) for in-place safety"

patterns-established:
  - "processBlock-AudioProc bridge: copy input to scratch, extract float** arrays, call AudioProc, clear remaining channels"
  - "APVTS-to-NJClient sync: getRawParameterValue() stores to atomics each audio callback"
  - "Command dispatch: drain SpscRing, std::visit with ScopedLock, type-safe variant handling"
  - "Run loop pattern: processCommands -> Run() under lock -> status tracking -> adaptive wait"

requirements-completed: [JUCE-03]

# Metrics
duration: 12min
completed: 2026-03-07
---

# Phase 3 Plan 1: NJClient Audio Bridge Summary

**NJClient wired into JUCE processBlock via AudioProc with in-place buffer safety, command queue dispatch, and adaptive run loop**

## Performance

- **Duration:** 12 min
- **Started:** 2026-03-07T15:10:30Z
- **Completed:** 2026-03-07T15:22:44Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Processor owns NJClient (unique_ptr), creates in constructor with temp work dir and FLAC default codec
- processBlock bridges to NJClient::AudioProc with in-place-safe float** arrays when connected, silences output when disconnected
- APVTS parameters (masterVol, masterMute, metroVol, metroMute) sync to NJClient atomics every audio callback
- NinjamRunThread calls NJClient::Run() under clientLock with adaptive sleep and full command queue processing
- Plugin builds as VST3 and passes pluginval at strictness 5

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire NJClient into Processor and processBlock** - `9a221a7` (feat)
2. **Task 2: Implement NinjamRunThread run loop with command queue and callbacks** - `8ca3204` (feat)

## Files Created/Modified
- `juce/JamWideJuceProcessor.h` - Added NJClient ownership, clientLock, cmd_queue, inputScratch, getClient()/getClientLock() accessors
- `juce/JamWideJuceProcessor.cpp` - NJClient construction with work dir/FLAC, processBlock AudioProc bridge, APVTS sync
- `juce/NinjamRunThread.h` - Added processCommands() declaration, updated doc comment
- `juce/NinjamRunThread.cpp` - Full run loop with Run() under lock, command dispatch, chat/license callbacks

## Decisions Made
- MAKE_NJ_FOURCC defined locally in processor .cpp since the macro is private to njclient.cpp (follows existing pattern in ui_local.cpp, ui_remote.cpp)
- AudioProc called without clientLock from processBlock -- AudioProc is designed to be called without external locks
- License auto-accepted (return 1) for Phase 3; Phase 4 will add proper license dialog UI
- Chat callback set as no-op stub to prevent NJClient default behavior; Phase 4 adds chat UI
- inputScratch uses setSize with keepExistingContent=true to avoid unnecessary reallocation

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- NJClient fully wired: audio flows end-to-end when connected to a NINJAM server
- Command queue ready for UI integration (Phase 4 can push ConnectCommand etc.)
- Plan 03-02 (parameter binding and state persistence) can proceed independently
- Phase 4 (UI Framework) has all the hooks it needs: getClient(), getClientLock(), cmd_queue

## Self-Check: PASSED

- All 4 modified files exist on disk
- Commit 9a221a7 (Task 1) verified in git log
- Commit 8ca3204 (Task 2) verified in git log
- VST3 build succeeds
- pluginval validation passes at strictness 5

---
*Phase: 03-njclient-audio-bridge*
*Completed: 2026-03-07*
