---
phase: 14-midi-remote-control
plan: 03
subsystem: midi
tags: [midi, osc, apvts, cmd-queue, echo-suppression, centralized-bridge, deadlock-prevention]
dependency_graph:
  requires:
    - phase: 14-01
      provides: [MidiMapper, setEchoSuppression, "85 APVTS parameters", "centralized APVTS-to-NJClient bridge via timerCallback"]
  provides:
    - "OscServer remote group controls routed through APVTS only (no cmd_queue)"
    - "ChannelStripArea remote solo callbacks update APVTS with echo suppression"
    - "OscServer OSC feedback reads from APVTS for group controls"
    - "MidiMapper::timerCallback as sole APVTS-to-NJClient bridge for remote group controls"
    - "Zero double-dispatch for remote group controls"
  affects: [osc, midi, ui]
tech_stack:
  added: []
  patterns: [apvts-only-state-mutation, centralized-njclient-bridge, mutex-before-apvts-safety]
key_files:
  created: []
  modified:
    - juce/osc/OscServer.cpp
    - juce/ui/ChannelStripArea.cpp
key_decisions:
  - "Solo retains sub-channel cmd_queue dispatch alongside APVTS update because NJClient has no group solo primitive and sub-channels are not APVTS-backed (per D-18)"
  - "ChannelStripArea volume/pan/mute already APVTS-attached via ParameterAttachment -- no callback refactoring needed for those controls"
  - "sendDirtyRemoteUsers reads APVTS via getRawParameterValue for group controls to reflect all change sources (MIDI, OSC, UI, DAW automation)"
patterns_established:
  - "APVTS-only state mutation: OscServer and UI update APVTS params only, MidiMapper timerCallback is sole cmd_queue writer for remote group controls"
  - "Mutex-before-APVTS safety: cachedUsersMutex released before setValueNotifyingHost and setEchoSuppression calls to prevent deadlock"
  - "Captured visibleSlot in lambdas: solo callbacks capture APVTS slot index at wire time for correct parameter addressing after roster changes"
requirements_completed: [MIDI-01]
metrics:
  duration: 10min
  completed: 2026-04-15
---

# Phase 14 Plan 03: APVTS Centralization for Remote Group Controls Summary

**Eliminated double-dispatch by routing all remote group controls (volume, pan, mute, solo) through APVTS only in OscServer and ChannelStripArea, making MidiMapper::timerCallback the sole APVTS-to-NJClient bridge.**

## Performance

- **Duration:** 10 min
- **Started:** 2026-04-15T17:21:59Z
- **Completed:** 2026-04-15T17:31:59Z
- **Tasks:** 2/2
- **Files modified:** 2

## Accomplishments

- OscServer handleRemoteUserOsc group bus handlers (volume, volume/db, pan, mute) now update APVTS parameters only with MIDI echo suppression -- zero cmd_queue dispatch for these controls
- OscServer sendDirtyRemoteUsers reads from APVTS (getRawParameterValue) for group volume/pan/mute/solo feedback, ensuring all change sources (MIDI, OSC, UI, DAW automation) are reflected
- ChannelStripArea solo callbacks (single-channel and multi-channel parent) add APVTS remoteSolo_N update with MIDI echo suppression
- Multi-channel parent solo callback restructured with narrowed mutex scope: APVTS calls happen before lock acquisition, preventing deadlock (T-14-12 mitigation)
- Sub-channel handlers completely unchanged in both files (per D-18)
- MidiMapper::timerCallback (20ms) is now the sole centralized path from APVTS to NJClient cmd_queue for all remote group controls

## Task Commits

Each task was committed atomically:

1. **Task 1: Refactor OscServer remote handler to update APVTS only** - `2bfd1e6` (refactor)
2. **Task 2: Refactor ChannelStripArea remote user callbacks to update APVTS only** - `e0550a0` (refactor)

## Files Created/Modified

- `juce/osc/OscServer.cpp` - Group bus volume/volume-db/pan/mute handlers replaced with APVTS-only updates; solo handler adds APVTS update alongside sub-channel dispatch; sendDirtyRemoteUsers reads APVTS for group feedback; explicit MidiMapper.h include added
- `juce/ui/ChannelStripArea.cpp` - Single-channel and multi-channel parent onSoloToggled callbacks add APVTS remoteSolo_N update with MIDI echo suppression; multi-channel parent mutex scope restructured for deadlock safety; visibleSlot captured in lambda closures

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Volume/Pan/Mute already APVTS-attached in ChannelStripArea | These controls were already wired via ParameterAttachment in Plan 01/02, not via callbacks. No refactoring needed -- only solo callbacks needed APVTS update. |
| Solo retains sub-channel cmd_queue dispatch | NJClient has no group solo primitive; solo must be dispatched per sub-channel. Sub-channels are not APVTS-backed (per D-18), so cmd_queue dispatch remains for sub-channel solo state. |
| APVTS reads in sendDirtyRemoteUsers | Reading from APVTS instead of cached NJClient state ensures OSC feedback reflects the true source of truth, especially when MIDI or DAW automation changes values (NJClient state may lag by one 20ms timer tick). |
| Captured visibleSlot in lambda closures | Solo callbacks need the APVTS slot index to know which remoteSolo_N parameter to update. Capturing at wire time (before visibleSlot increments) ensures correct indexing even if roster changes. |

## Deviations from Plan

None -- plan executed exactly as written. The plan anticipated that ChannelStripArea volume/pan/mute would need refactoring from cmd_queue to APVTS, but these were already APVTS-attached (not wired via callbacks) from prior plans. Only solo callbacks needed the APVTS update addition. This is not a deviation but a simpler-than-expected outcome.

## Issues Encountered

None.

## Known Stubs

None. All remote group controls are fully wired through APVTS with MIDI echo suppression.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 14 (midi-remote-control) is now complete: all 3 plans executed
- MidiMapper core engine (Plan 01), MIDI Learn UX (Plan 02), and APVTS centralization (Plan 03) form a complete MIDI remote control system
- Remote user group controls have a single, clean data flow: any source (MIDI/OSC/UI/DAW) -> APVTS -> MidiMapper timerCallback (20ms) -> NJClient cmd_queue
- No blocking issues for subsequent phases

## Self-Check: PASSED

```
FOUND: juce/osc/OscServer.cpp
FOUND: juce/ui/ChannelStripArea.cpp
FOUND: 14-03-SUMMARY.md
FOUND: 2bfd1e6 (Task 1 commit)
FOUND: e0550a0 (Task 2 commit)
VST3 builds clean, no errors
```

---
*Phase: 14-midi-remote-control*
*Plan: 03*
*Completed: 2026-04-15*
