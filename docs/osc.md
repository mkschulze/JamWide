---
layout: default
title: OSC Reference
---

# OSC Address Reference

Complete reference for JamWide's bidirectional OSC server. All addresses are under the `/JamWide/` namespace.

---

## Defaults

| Setting | Default | Description |
|---------|---------|-------------|
| Receive Port | 9000 | Port JamWide listens on (matches TouchOSC default send port) |
| Send IP | 127.0.0.1 | Target IP for outgoing OSC messages |
| Send Port | 9001 | Port JamWide sends to (matches TouchOSC default receive port) |
| Update Rate | 100ms | Dirty-flag sender fires every 100ms |

---

## Controllable Parameters

These addresses accept incoming OSC messages (float argument) and send feedback when values change.

### Master

| Address | Type | Range | Description |
|---------|------|-------|-------------|
| `/JamWide/master/volume` | float | 0 -- 1 | Master volume (normalized). 0.5 = unity gain (0 dB) |
| `/JamWide/master/volume/db` | float | -100 -- 6 | Master volume in dB |
| `/JamWide/master/mute` | float | 0 or 1 | Master mute (0 = unmuted, 1 = muted) |

### Metronome

| Address | Type | Range | Description |
|---------|------|-------|-------------|
| `/JamWide/metro/volume` | float | 0 -- 1 | Metronome volume (normalized) |
| `/JamWide/metro/volume/db` | float | -100 -- 6 | Metronome volume in dB |
| `/JamWide/metro/pan` | float | 0 -- 1 | Metronome pan (0 = left, 0.5 = center, 1 = right) |
| `/JamWide/metro/mute` | float | 0 or 1 | Metronome mute |

### Local Channels (1-4)

Replace `{n}` with channel number 1 through 4.

| Address | Type | Range | Description |
|---------|------|-------|-------------|
| `/JamWide/local/{n}/volume` | float | 0 -- 1 | Channel volume (normalized). 0.5 = unity gain |
| `/JamWide/local/{n}/volume/db` | float | -100 -- 6 | Channel volume in dB |
| `/JamWide/local/{n}/pan` | float | 0 -- 1 | Channel pan (0 = left, 0.5 = center, 1 = right) |
| `/JamWide/local/{n}/mute` | float | 0 or 1 | Channel mute |
| `/JamWide/local/{n}/solo` | float | 0 or 1 | Channel solo |

**Note:** Channel numbering is 1-based in OSC (channels 1-4), matching what you see in the UI.

---

## Session Telemetry (Read-Only)

These addresses are send-only. JamWide broadcasts them when values change. Incoming messages to these addresses are ignored.

| Address | Type | Description |
|---------|------|-------------|
| `/JamWide/session/bpm` | float | Current server BPM |
| `/JamWide/session/bpi` | int | Beats per interval |
| `/JamWide/session/beat` | int | Current beat position within the interval (0-based) |
| `/JamWide/session/status` | int | Connection status (NJClient status code) |
| `/JamWide/session/users` | int | Number of connected users |
| `/JamWide/session/codec` | string | Active codec name ("FLAC" or "Vorbis") |
| `/JamWide/session/samplerate` | float | Current sample rate in Hz |

---

## VU Meters (Read-Only)

VU meters are broadcast every 100ms regardless of whether values have changed. Range is 0 to 1.

### Master

| Address | Type | Description |
|---------|------|-------------|
| `/JamWide/master/vu/left` | float | Master left channel VU level |
| `/JamWide/master/vu/right` | float | Master right channel VU level |

### Local Channels (1-4)

| Address | Type | Description |
|---------|------|-------------|
| `/JamWide/local/{n}/vu/left` | float | Channel left VU level |
| `/JamWide/local/{n}/vu/right` | float | Channel right VU level |

---

## Volume Mapping

JamWide uses two volume namespaces:

**Normalized (default):** `/volume` addresses use a 0-1 range that maps to the APVTS parameter range of 0-2 (linear gain). This means:
- 0.0 = silence (-inf dB)
- 0.5 = unity gain (0 dB)
- 1.0 = +6 dB (maximum)

**dB:** `/volume/db` addresses use decibels directly:
- -100 dB = silence
- 0 dB = unity gain
- +6 dB = maximum

For TouchOSC faders, use the normalized namespace with range 0-1. The dB namespace is useful for precise control or display purposes.

---

## Pan Mapping

All pan addresses use a 0-1 range:
- 0.0 = hard left
- 0.5 = center
- 1.0 = hard right

This applies to both local channel pan and metronome pan, so your TouchOSC layout can use the same knob range for all pan controls.

---

## Echo Suppression

JamWide uses echo suppression to prevent feedback loops between the plugin and the control surface. When a value is received via OSC, it is marked as "OSC-sourced" and skipped on the next outgoing send tick. This means:

- Move a fader in TouchOSC: JamWide updates but does **not** echo the value back
- Move a fader in JamWide: TouchOSC receives the update
- No oscillation between the two

The suppression window is one timer tick (100ms), which is imperceptible.

---

## Bundle Mode

All outgoing OSC messages are grouped into a single OSC bundle per 100ms tick. This provides:
- Atomic updates (all values in a tick arrive together)
- Fewer UDP packets (one bundle instead of many individual messages)
- Lower network overhead

---

## Error Handling

- **Unknown addresses** are silently ignored
- **Invalid argument types** (not float or int) are silently ignored
- **Out-of-range values** are clamped to the parameter's valid range
- **Port bind failures** are shown in the config dialog and turn the status dot red

---

## Configuration

Click the grey/green/red OSC dot in the connection bar footer to open the config dialog:

| Field | Default | Description |
|-------|---------|-------------|
| Enable OSC | Off | Toggle OSC server on/off |
| Receive Port | 9000 | UDP port to listen for incoming OSC |
| Send IP | 127.0.0.1 | Target IP for outgoing OSC (use your device's IP for remote surfaces) |
| Send Port | 9001 | UDP port to send outgoing OSC |

Settings persist across DAW save/load cycles. Fields remain editable when OSC is disabled, so you can configure ports before enabling.

---

## Future (Phase 10)

The following OSC features are planned for a future release:
- Remote user control: `/JamWide/remote/{idx}/volume`, `/pan`, `/mute`, `/solo`
- Roster change broadcast (user names)
- Connect/disconnect via OSC trigger
- Shipped TouchOSC template (`.tosc` file)
