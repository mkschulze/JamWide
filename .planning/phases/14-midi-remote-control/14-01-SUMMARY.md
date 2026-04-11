---
phase: 14-midi-remote-control
plan: 01
subsystem: midi
tags: [midi, apvts, cc-mapping, feedback, echo-suppression, state-persistence, tdd]
dependency_graph:
  requires: []
  provides: [MidiMapper, MidiLearnManager, "85 APVTS parameters", "state version 3", "MIDI processBlock integration"]
  affects: [JamWideJuceProcessor, CMakeLists.txt]
tech_stack:
  added: []
  patterns: [atomic-pointer-swap, per-mapping-echo-suppression, centralized-apvts-njclient-bridge, tdd-red-green]
key_files:
  created:
    - juce/midi/MidiMapper.h
    - juce/midi/MidiMapper.cpp
    - juce/midi/MidiLearnManager.h
    - juce/midi/MidiLearnManager.cpp
    - tests/test_midi_mapping.cpp
  modified:
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - CMakeLists.txt
decisions:
  - "Timer interval 20ms (not 100ms) for responsive MIDI mixer control"
  - "Per-mapping echo suppression with counter-based approach (not global 2-frame window)"
  - "MIDI Learn checks before mapping table early return (zero-mapping learn support)"
  - "producesMidi returns true for CC feedback to motorized controllers"
  - "std::shared_ptr<const MappingTable> with atomic_load/store for thread-safe audio access"
metrics:
  duration_seconds: 826
  completed: "2026-04-11T15:56:35Z"
  tasks_completed: 1
  tasks_total: 1
  files_created: 5
  files_modified: 3
  test_count: 15
  test_pass: 15
---

# Phase 14 Plan 01: MIDI Mapper Core Engine Summary

MIDI mapper core with CC dispatch, bidirectional feedback, per-mapping echo suppression, APVTS expansion to 85 parameters, centralized 20ms APVTS-to-NJClient bridge, MIDI Learn state machine, state version 3 persistence with validation, and 15-test TDD suite.

## Task Results

| Task | Name | Commit | Status | Key Files |
|------|------|--------|--------|-----------|
| 1 | APVTS expansion, MidiMapper/MidiLearnManager core, APVTS-NJClient bridge, tests | d530d15 | Complete | MidiMapper.h/.cpp, MidiLearnManager.h/.cpp, test_midi_mapping.cpp |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] MIDI Learn fails with zero existing mappings**
- **Found during:** Task 1 GREEN phase (test 9 failed)
- **Issue:** `processIncomingMidi` had an early return when the mapping table was empty (`if (!table || table->ccToParam.empty()) return;`), which prevented MIDI Learn from receiving CC messages when no mappings existed yet
- **Fix:** Changed early return guard to `if (!hasTable && !isLearning) return;` -- only skip processing when there are no mappings AND learn is not active
- **Files modified:** juce/midi/MidiMapper.cpp, tests/test_midi_mapping.cpp
- **Commit:** d530d15

## Implementation Details

### MidiMapper Architecture
- **Thread safety:** `std::shared_ptr<const MappingTable>` with `std::atomic_load/store` for lock-free audio thread reads. Message thread writes to `staging_` copy, publishes via atomic pointer swap.
- **Echo suppression:** Per-mapping counter-based approach. When CC arrives from MIDI input, `echoSuppression_[key] = 2` suppresses feedback for that specific (channel, cc) pair. External callers (OSC/UI) use `setEchoSuppression(paramId)` API.
- **Composite key:** `((channel-1) << 7) | ccNumber` gives unique 14-bit integer per mapping, enabling same CC on different channels to map to different parameters.
- **Centralized APVTS-NJClient bridge:** `timerCallback` at 20ms is the SOLE path from APVTS remote parameters to NJClient cmd_queue via `SetUserStateCommand`. Runs on message thread preserving SPSC single-producer invariant.
- **Duplicate conflict:** Last-write-wins strategy. Adding a mapping for an already-mapped CC+Ch removes the old paramId mapping. Adding for an already-mapped paramId removes the old CC+Ch mapping.

### APVTS Parameter Expansion
- 16 existing parameters (version 1): masterVol, masterMute, metroVol, metroMute, localVol_0..3, localPan_0..3, localMute_0..3
- 64 new remote parameters (version 3): remoteVol_0..15, remotePan_0..15, remoteMute_0..15, remoteSolo_0..15
- 4 new local solo parameters (version 3): localSolo_0..3
- 1 new metro pan parameter (version 3): metroPan
- **Total: 85 APVTS parameters**

### State Persistence
- State version bumped from 2 to 3
- MIDI mappings stored as ValueTree child "MidiMappings" with "Mapping" entries (paramId, cc, channel)
- Validation on load: cc 0-127 (reject out-of-range), channel 1-16 (reject out-of-range), non-empty paramId, paramId must exist in APVTS

### Processor Integration
- `acceptsMidi()` returns `true` (was false)
- `producesMidi()` returns `true` (was false)
- `processBlock` processes MidiBuffer: `processIncomingMidi` FIRST, then `appendFeedbackMidi`
- CMake `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT TRUE`

## Test Coverage

15 unit tests in `tests/test_midi_mapping.cpp`:
1. APVTS expansion (85 parameters total)
2. CC dispatch to float parameter (CC value/127 -> normalized)
3. Toggle dispatch for bool parameters (value>0 toggles, value==0 ignored)
4. Feedback CC output on parameter change
5. Per-mapping echo suppression (suppressed mapping vs unsuppressed)
6. Mapping CRUD operations (add, remove, clear, count)
7. Composite key (same CC, different channels -> different params)
8. State persistence round-trip (save/load all mappings)
9. MIDI Learn assignment (works with zero existing mappings)
10. Mapping cap enforcement (validation of cc/channel/paramId)
11. APVTS-NJClient sync via timer callback (SetUserStateCommand dispatch)
12. Duplicate mapping conflict (last-write-wins)
13. Malformed state rejection (out-of-range CC, invalid channel, empty paramId, non-existent paramId)
14. Empty slot reset defaults (vol=1.0, pan=0.0, mute=false, solo=false)
15. Learn state transitions (start/cancel/tryLearn state machine)

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| 20ms timer (not 100ms) | Responsive MIDI mixer control -- 50Hz update rate for physical fader control |
| Per-mapping echo suppression | Counter-based per (channel, cc) pair handles varying block sizes correctly |
| Learn before table check | MIDI Learn must work even with zero existing mappings |
| producesMidi returns true | Enables CC feedback to motorized controllers via DAW routing |
| atomic shared_ptr swap | Lock-free audio thread access to mapping table |
| try_push not push | SpscRing API uses try_push -- plan referenced push which doesn't exist |

## Self-Check: PASSED

```
FOUND: juce/midi/MidiMapper.h
FOUND: juce/midi/MidiMapper.cpp
FOUND: juce/midi/MidiLearnManager.h
FOUND: juce/midi/MidiLearnManager.cpp
FOUND: tests/test_midi_mapping.cpp
FOUND: d530d15
All 15 tests pass
Plugin builds successfully
```
