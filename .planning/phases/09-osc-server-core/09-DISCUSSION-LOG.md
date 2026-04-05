# Phase 9: OSC Server Core - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-05
**Phase:** 09-osc-server-core
**Areas discussed:** OSC namespace, Config UI placement, Feedback behavior, Telemetry scope, Port defaults & error handling, VU meter OSC detail, OSC dialog design

---

## OSC Namespace

| Option | Description | Selected |
|--------|-------------|----------|
| Hierarchical | /JamWide/local/{ch}/volume — mirrors mixer layout, easy TouchOSC mapping | ✓ |
| Flat IEM-style | /JamWide/localVol1 — simpler parsing but harder to extend | |
| You decide | Claude picks best structure | |

**User's choice:** Hierarchical (Recommended)

### Root Prefix

| Option | Description | Selected |
|--------|-------------|----------|
| /JamWide/ | Standard convention, prevents collision | ✓ |
| /jw/ | Shorter but non-standard | |
| Configurable | User-settable prefix | |

**User's choice:** /JamWide/ (Recommended)

### Value Range

| Option | Description | Selected |
|--------|-------------|----------|
| 0-1 normalized | TouchOSC default range | |
| dB scale | More precise for audio engineers | |
| Both | Primary 0-1, secondary /db/ namespace | ✓ |

**User's choice:** Both — dual namespace with primary 0-1 and secondary dB

---

## Config UI Placement

| Option | Description | Selected |
|--------|-------------|----------|
| Footer dot + popup | IEM pattern, minimal footprint, always visible | ✓ |
| Settings panel | Dedicated collapsible panel | |
| ConnectionBar addition | Groups networking together | |

**User's choice:** Footer dot + popup (Recommended)

### Status Dot Colors

| Option | Description | Selected |
|--------|-------------|----------|
| 3-state | Green/Red/Grey — matches IEM | ✓ |
| 4-state | Green/Yellow/Red/Grey — more informative | |
| You decide | Claude picks | |

**User's choice:** 3-state (Recommended)

### Enable Toggle

| Option | Description | Selected |
|--------|-------------|----------|
| Explicit toggle | On/off switch, IEM has this | ✓ |
| Always active | If ports set, OSC is on | |

**User's choice:** Explicit toggle (Recommended)

---

## Feedback Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| 100ms dirty-flag timer | IEM pattern, proven, low bandwidth | ✓ |
| Immediate on change | Lower latency but feedback risk | |
| Configurable interval | User sets rate 50-500ms | |

**User's choice:** 100ms dirty-flag timer (Recommended)

### Bundling

| Option | Description | Selected |
|--------|-------------|----------|
| Bundles | Group dirty values per tick, atomic | ✓ |
| Individual messages | One UDP per change | |
| You decide | Claude picks | |

**User's choice:** Bundles (Recommended)

### Echo Suppression

| Option | Description | Selected |
|--------|-------------|----------|
| Suppress echo | Skip sending OSC-sourced values back for one tick | ✓ |
| Always send all dirty | Simpler, slight feedback risk | |
| You decide | Claude picks | |

**User's choice:** Suppress echo (Recommended)

---

## Telemetry Scope

| Option | Description | Selected |
|--------|-------------|----------|
| Core session | BPM, BPI, beat, status, user count | |
| Core + codec info | Above + codec, sample rate | |
| Core + VU meters | Above + master VU levels | |
| Everything | All: BPM, BPI, beat, status, users, codec, sample rate, VU | ✓ |

**User's choice:** Everything

---

## Port Defaults & Error Handling

### Default Ports

| Option | Description | Selected |
|--------|-------------|----------|
| 9000/9001 | Matches TouchOSC convention | ✓ |
| 8000/8001 | Also common, DAW conflicts | |
| 49000/49001 | High ports, avoids conflicts | |

**User's choice:** 9000/9001 (Recommended)

### Port Error

| Option | Description | Selected |
|--------|-------------|----------|
| Show error, stay disabled | Red dot, error message in dialog | ✓ |
| Auto-increment port | Automatic but TouchOSC mismatch | |
| Retry with backoff | Handles DAW restart scenarios | |

**User's choice:** Show error, stay disabled (Recommended)

---

## VU Meter OSC Detail

| Option | Description | Selected |
|--------|-------------|----------|
| Master + local | Master and 4 local channels | |
| Master only | Minimal traffic | |
| All including remote | Master + local + remote users | ✓ |

**User's choice:** All channels including remote

---

## OSC Dialog Design

| Option | Description | Selected |
|--------|-------------|----------|
| IEM-style minimal | Enable, ports, IP, feedback interval. Dark theme. ~200x300px | ✓ |
| Extended with namespace info | Above + read-only address tree | |
| Full with presets | Above + preset buttons | |

**User's choice:** IEM-style minimal (Recommended)

---

## Claude's Discretion

- Power curve mapping for dB namespace
- Internal dirty-flag data structure
- Error message wording
- Timer implementation pattern
- Dialog layout details

## Deferred Ideas

- Remote user index addressing — Phase 10
- TouchOSC template — Phase 10
- Connect/disconnect via OSC — Phase 10
- Video control via OSC — Phase 13
