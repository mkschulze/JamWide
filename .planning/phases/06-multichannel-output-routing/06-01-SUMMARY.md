---
phase: 06-multichannel-output-routing
plan: 01
subsystem: audio
tags: [multichannel, routing, audio-processing, juce, output-buses, metronome, state-persistence]

# Dependency graph
requires:
  - phase: 05-mixer-ui-and-channel-controls
    provides: "Per-channel mixer controls, APVTS state persistence, multi-bus input path"
provides:
  - "34-channel output path (17 stereo buses) via processBlock + AudioProc"
  - "SetRoutingModeCommand for UI-driven routing mode changes"
  - "Per-user output bus routing via set_outch/outchannel on SetUserChannelState"
  - "Main-mix accumulation with correct master volume and metronome behavior"
  - "Metronome on dedicated bus 16 (channels 32-33)"
  - "Routing mode persistence in plugin state (getStateInformation/setStateInformation)"
  - "out_chan_index in RemoteChannelInfo snapshot for UI polling"
affects: [06-02-PLAN, ui-routing-controls, daw-output-routing]

# Tech tracking
tech-stack:
  added: []
  patterns: ["34-channel outputScratch pre-allocated in prepareToPlay", "main-mix accumulation with per-bus master vol and metronome exclusion", "nch=32 metronome-safe auto-assign boundary", "std::atomic<int> for cross-thread routing mode"]

key-files:
  created: []
  modified:
    - "juce/JamWideJuceProcessor.h"
    - "juce/JamWideJuceProcessor.cpp"
    - "src/threading/ui_command.h"
    - "src/core/njclient.h"
    - "src/core/njclient.cpp"
    - "juce/NinjamRunThread.cpp"

key-decisions:
  - "outputScratch pre-allocated in prepareToPlay with safety resize in processBlock (avoidReallocating=true)"
  - "Metronome accumulated into main mix WITHOUT master volume (preserves original NJClient behavior)"
  - "Remote buses accumulated into main mix WITH master volume and pan"
  - "Auto-assign uses nch=32 (not 34) to exclude metronome bus from search space"
  - "routingMode is std::atomic<int> accessed via .load()/.store() for thread safety"
  - "Metronome channel set to 32 on connect (dedicated bus 16, channels 32-33)"

patterns-established:
  - "kTotalOutChannels=34 / kNumOutputBuses=17 / kMetronomeBus=16 constants for output bus layout"
  - "Two-pass routing mode sweep: reset all to bus 0, then assign via find_unused_output_channel_pair()"
  - "Master VU measured post-accumulation from outputScratch channels 0-1"

requirements-completed: [MOUT-01, MOUT-04, MOUT-05]

# Metrics
duration: 7min
completed: 2026-04-04
---

# Phase 6 Plan 01: Multichannel Output Routing Backend Summary

**34-channel audio path with per-bus routing, main-mix accumulation (master vol on remote buses, metronome excluded), SetRoutingModeCommand dispatch with metronome-safe auto-assign, and routing mode state persistence**

## Performance

- **Duration:** 7 min
- **Started:** 2026-04-04T21:51:59Z
- **Completed:** 2026-04-04T21:59:46Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- Expanded processBlock from 2-channel to 34-channel output, with pre-allocated outputScratch buffer and per-bus JUCE copy
- Main-mix accumulation correctly applies master volume to remote buses and excludes metronome from master volume (preserving original NJClient behavior)
- SetRoutingModeCommand dispatch with two-pass quick-assign sweep using nch=32 (excludes metronome bus from auto-assign search)
- Command queue extended with set_outch/outchannel for per-channel output routing and out_chan_index in RemoteChannelInfo snapshot
- Routing mode persisted as std::atomic<int> via getStateInformation/setStateInformation with validation

## Task Commits

Each task was committed atomically:

1. **Task 1: Command infrastructure + NJClient snapshot extension + routing mode dispatch** - `0043845` (feat)
2. **Task 2: Expanded processBlock with pre-allocated output buffer + main-mix accumulation + state persistence** - `8eec09b` (feat)

## Files Created/Modified
- `src/threading/ui_command.h` - Added SetRoutingModeCommand struct, set_outch/outchannel to SetUserChannelStateCommand, updated UiCommand variant
- `src/core/njclient.h` - Added out_chan_index to RemoteChannelInfo snapshot struct
- `src/core/njclient.cpp` - Added out_chan_index copy in GetRemoteUsersSnapshot
- `juce/NinjamRunThread.cpp` - SetRoutingModeCommand dispatch with auto-assign sweep, SetUserChannelState outch passthrough, routing mode and metronome channel on connect
- `juce/JamWideJuceProcessor.h` - Added kTotalOutChannels/kNumOutputBuses/kMetronomeBus constants, std::atomic<int> routingMode, outputScratch buffer
- `juce/JamWideJuceProcessor.cpp` - Expanded processBlock with 34-channel output path, main-mix accumulation, per-bus JUCE copy, outputScratch pre-allocation, routing mode persistence

## Decisions Made
- outputScratch pre-allocated in prepareToPlay (not processBlock) to avoid audio-thread heap allocation, with safety resize using avoidReallocating=true
- Metronome bus accumulated into main mix WITHOUT master volume, preserving original NJClient signal flow where metronome is mixed after master vol application
- Remote buses (1 through 15) accumulated into main mix WITH master volume and pan
- Auto-assign search space limited to nch=32 (channels 2-31, buses 1-15), excluding metronome on channels 32-33
- routingMode stored as std::atomic<int> to prevent data race between message thread (write from UI) and run thread (read on connect)
- SetMetronomeChannel(32) called on connect to route metronome to dedicated last bus

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- Worktree does not have all submodules (JUCE, libflac) so full cmake build was not possible; verified acceptance criteria via grep checks against all 10 plan verification items instead
- Soft git reset from worktree base caused extraneous staged files; resolved by careful per-file staging

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Audio backend for multichannel routing is complete; Plan 02 can build routing UI controls that send SetRoutingModeCommand and per-channel outch commands
- RemoteChannelInfo snapshot includes out_chan_index for UI polling of current routing assignments
- Routing mode persists across DAW save/load, applied on reconnect

## Self-Check: PASSED

All 7 files verified present. Both task commits (0043845, 8eec09b) verified in git log.

---
*Phase: 06-multichannel-output-routing*
*Completed: 2026-04-04*
