---
phase: 09-osc-server-core
plan: 02
subsystem: osc-ui
tags: [osc, config, ui, persistence, state-version]

# Dependency graph
requires:
  - phase: 09-osc-server-core
    plan: 01
    provides: OscServer, OscAddressMap
provides:
  - OscStatusDot 3-state footer indicator
  - OscConfigDialog CallOutBox popup
  - Processor OscServer ownership and lifecycle
  - State version 2 with OSC config persistence
affects: [touchosc-layouts, phase-10-osc-remote]

# Tech tracking
tech-stack:
  added: []
  version-changes: []
---

## What was built

Wired the OSC engine (Plan 01) into the plugin lifecycle with UI controls and state persistence.

### Key files

**Created:**
- `juce/osc/OscStatusDot.h/.cpp` — 3-state indicator (grey/green/red) in ConnectionBar footer. 500ms poll timer, opens CallOutBox on click, tooltips per copywriting contract.
- `juce/osc/OscConfigDialog.h/.cpp` — 200x300px CallOutBox with enable toggle, receive port, send IP, send port fields, error display. Voicemeeter dark theme. Port validation (1-65535).

**Modified:**
- `juce/JamWideJuceProcessor.h/.cpp` — Owns `OscServer` (created after NJClient, destroyed before runThread). State version bumped to 2 with oscEnabled, oscReceivePort, oscSendIP, oscSendPort fields. v1 states load with OSC defaults.
- `juce/ui/ConnectionBar.h/.cpp` — Added OscStatusDot between Sync button and Fit button.
- `juce/osc/OscServer.h` — Added `getProcessor()` accessor for config persistence.
- `CMakeLists.txt` — Added OscStatusDot.cpp, OscConfigDialog.cpp to target_sources.

### Review fixes applied (post-execution)
- Solo feedback: reads actual state from NJClient::GetLocalChannelMonitoring() instead of local bitmask
- Metro pan normalized to 0..1 (consistent with local pan)
- isPanParam flag replaces string-based pan detection
- callAsync UAF safety via shared alive flag
- Failed state restore shows red dot instead of grey

## Self-Check: PASSED

- [x] OscStatusDot shows 3-state indicator per D-09
- [x] Click opens CallOutBox per D-08
- [x] Config dialog has all fields per D-10
- [x] Fields editable when disabled per D-11
- [x] Default ports 9000/9001 per D-17
- [x] Port bind failure shows error, dot turns red per D-18
- [x] State version 2, OSC config persists per D-21
- [x] v1 states load with OSC defaults per D-21
- [x] Build compiles with zero errors

## Deviations

- OscStatusDot placed between Sync and Fit buttons (not between Sync and statusLabel as originally planned) — better visual grouping with other controls.
- Added `getProcessor()` to OscServer.h for config persistence coupling (recommended by both cross-AI reviewers).
