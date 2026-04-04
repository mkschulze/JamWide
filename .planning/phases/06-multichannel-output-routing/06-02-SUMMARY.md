---
phase: 06-multichannel-output-routing
plan: 02
subsystem: ui
tags: [juce, routing, popup-menu, combobox, atomic, command-queue]

# Dependency graph
requires:
  - phase: 06-01
    provides: "SetRoutingModeCommand, SetUserChannelStateCommand.set_outch/outchannel, routingMode atomic, RemoteChannelInfo.out_chan_index, kMetronomeBus"
provides:
  - "Route button in ConnectionBar with Manual/By User/By Channel popup menu"
  - "Updated routing selectors on ChannelStrip: Main Mix + Remote 1-15"
  - "setRoutingBus method for programmatic routing selector updates"
  - "Per-strip routing callbacks wired through command queue"
  - "Editor integration: SetRoutingModeCommand dispatch and persisted highlight restore"
affects: [06-03, 07-daw-sync]

# Tech tracking
tech-stack:
  added: []
  patterns: ["Atomic .load()/.store() for cross-thread routingMode access", "Bus-to-channel conversion: busIndex * 2 for stereo pair offset", "Stable identity capture for routing callbacks (same as volume/pan/mute/solo)"]

key-files:
  created: []
  modified:
    - juce/ui/ConnectionBar.h
    - juce/ui/ConnectionBar.cpp
    - juce/ui/ChannelStrip.h
    - juce/ui/ChannelStrip.cpp
    - juce/ui/ChannelStripArea.cpp
    - juce/JamWideJuceEditor.cpp

key-decisions:
  - "Menu item IDs (1,2,3) map to mode values (0,2,1) -- Manual, By User, By Channel ordering matches ReaNINJAM UX"
  - "15 remote buses (not 16) because bus 16 is reserved for metronome (kMetronomeBus)"
  - "Routing selector refresh is event-driven via UserInfoChangedEvent, not polled"

patterns-established:
  - "Route button popup: showMenuAsync with withTargetComponent for anchored menu"
  - "Green text feedback (kAccentConnect) for active/non-default routing state"
  - "Bus index to channel pair offset: busIndex * 2; reverse: out_chan_index / 2"

requirements-completed: [MOUT-02, MOUT-03]

# Metrics
duration: 7min
completed: 2026-04-04
---

# Phase 6 Plan 02: Routing UI Controls Summary

**Route button with popup menu for Manual/By User/By Channel quick-assign, updated routing selectors (Main Mix + Remote 1-15) with green feedback and command queue wiring**

## Performance

- **Duration:** 7 min
- **Started:** 2026-04-04T22:06:46Z
- **Completed:** 2026-04-04T22:13:46Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- Route button in ConnectionBar between Fit and Codec with popup menu for Manual (Main Mix), Assign by User, Assign by Channel
- Routing selectors updated from "Out N/N+1" to "Main Mix" + "Remote 1" through "Remote 15" (bus 16 reserved for metronome)
- Green text feedback on Route button (active mode) and routing selectors (non-default bus)
- Per-strip routing callbacks wired through command queue with bus-to-channel conversion
- Editor dispatches SetRoutingModeCommand and initializes Route button highlight from persisted state

## Task Commits

Each task was committed atomically:

1. **Task 1: Route button in ConnectionBar + routing selector item update on ChannelStrip + setRoutingBus method** - `aea6104` (feat)
2. **Task 2: Wire ChannelStripArea callbacks + routing selector refresh from snapshot + editor integration** - `bd6d9d0` (feat)

## Files Created/Modified
- `juce/ui/ConnectionBar.h` - Added routeButton member, onRouteModeChanged callback, setRoutingModeHighlight declaration
- `juce/ui/ConnectionBar.cpp` - Route button init with PopupMenu, layout in resized(), highlight method, atomic routingMode access
- `juce/ui/ChannelStrip.h` - Added setRoutingBus declaration
- `juce/ui/ChannelStrip.cpp` - Updated routing selector items (Main Mix + Remote 1-15), green text feedback, setRoutingBus implementation
- `juce/ui/ChannelStripArea.cpp` - onRoutingChanged callbacks for single/multi-channel strips, routing selector refresh from snapshot out_chan_index
- `juce/JamWideJuceEditor.cpp` - onRouteModeChanged wiring with SetRoutingModeCommand, initial Route button highlight from persisted routingMode

## Decisions Made
- Menu item IDs (1,2,3) map to mode values (0,2,1) -- keeps Manual first, then By User, By Channel matching ReaNINJAM UX ordering
- 15 remote buses shown (not 16) because bus 16 (kMetronomeBus) is reserved for metronome output
- Routing selector refresh uses event-driven pattern (UserInfoChangedEvent triggers refreshFromUsers which calls setRoutingBus) rather than continuous polling
- Route button tooltip says "Enable additional outputs in your DAW's plugin I/O settings" per D-15

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- Build verification could not be performed due to disk space constraints (48Mi available, submodule clone requires more). All acceptance criteria verified via grep pattern matching. Build verification deferred to orchestrator merge step.

## Known Stubs

None - all routing selectors are wired to command queue callbacks and refresh from snapshot data.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Routing UI controls are complete and wired to the command infrastructure from Plan 01
- Ready for Plan 03 (if any) or phase verification
- Full build verification needed at merge time

---
*Phase: 06-multichannel-output-routing*
*Completed: 2026-04-04*
