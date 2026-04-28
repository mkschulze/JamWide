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
| `/JamWide/session/codec` | string | Active codec name. The parameter accepts `"FLAC"` and `"Vorbis"`, but FLAC encode/decode is in development and does not yet round-trip cleanly — Vorbis is the working codec in this beta. |
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

## Remote Users (1-16)

Replace `{idx}` with a 1-based index. Slot 1 = first connected user, slot 2 = second, etc. When users leave, subsequent indices shift down. The roster name broadcast tells the control surface who is in each slot.

### Group Bus Control

| Address | Type | Range | Description |
|---------|------|-------|-------------|
| `/JamWide/remote/{idx}/volume` | float | 0 -- 1 | User volume (normalized) |
| `/JamWide/remote/{idx}/volume/db` | float | -100 -- 6 | User volume in dB |
| `/JamWide/remote/{idx}/pan` | float | 0 -- 1 | User pan |
| `/JamWide/remote/{idx}/mute` | float | 0 or 1 | User mute |
| `/JamWide/remote/{idx}/solo` | float | 0 or 1 | User solo (sets solo on ALL sub-channels) |

### Sub-Channel Control

Replace `{n}` with a **sequential 1-based** channel index (NOT the sparse NINJAM bit index). `/ch/1` is the user's first channel, `/ch/2` is the second, etc.

| Address | Type | Range | Description |
|---------|------|-------|-------------|
| `/JamWide/remote/{idx}/ch/{n}/volume` | float | 0 -- 1 | Sub-channel volume |
| `/JamWide/remote/{idx}/ch/{n}/pan` | float | 0 -- 1 | Sub-channel pan |
| `/JamWide/remote/{idx}/ch/{n}/mute` | float | 0 or 1 | Sub-channel mute |
| `/JamWide/remote/{idx}/ch/{n}/solo` | float | 0 or 1 | Sub-channel solo |

### Remote VU Meters (Read-Only)

| Address | Type | Description |
|---------|------|-------------|
| `/JamWide/remote/{idx}/vu/left` | float | User left VU level |
| `/JamWide/remote/{idx}/vu/right` | float | User right VU level |
| `/JamWide/remote/{idx}/ch/{n}/vu/left` | float | Sub-channel left VU |
| `/JamWide/remote/{idx}/ch/{n}/vu/right` | float | Sub-channel right VU |

**Template note:** The shipped TouchOSC template omits remote VU meters intentionally for layout density reasons. The OSC server still broadcasts them.

### Roster Broadcast (Read-Only)

Sent when users join or leave the session. Empty string for unused slots.

| Address | Type | Description |
|---------|------|-------------|
| `/JamWide/remote/{idx}/name` | string | Username (stripped of @IP suffix) |
| `/JamWide/remote/{idx}/ch/{n}/name` | string | Sub-channel name |

---

## Video Control

### Video

| Address | Type | Range | Direction | Description |
|---------|------|-------|-----------|-------------|
| `/JamWide/video/active` | float | 0.0-1.0 | Bidirectional | Toggle video companion (1.0 = activate, 0.0 = deactivate). Sends feedback reflecting current state. Note: first activation in a session must be via the plugin UI (privacy dialog). Subsequent OSC activations relaunch the companion directly using stored session credentials. |
| `/JamWide/video/popout/{1-16}` | float | 1.0 (momentary) | Receive only | Pop out the remote user at roster index {idx} into a separate browser window. Sends popout request via WebSocket to companion page. No-op if index exceeds current roster size. TouchOSC template provides buttons for indices 1-8; indices 9-16 are accessible via custom OSC layouts. |

**Display modes:** Grid (default, all participants in the companion page) and popout (per-user separate windows via `/video/popout/{idx}`). There is no dedicated "mode switch" OSC address. Grid mode is always active in the main companion; popouts are additive per-user windows. This satisfies VID-11 "switch display modes" -- the user switches from grid-only to grid+popout by triggering popouts.

---

## Session Control

### Connect/Disconnect via OSC

| Address | Type | Description |
|---------|------|-------------|
| `/JamWide/session/connect` | string | Connect to server. Send `"host:port"` (e.g. `"ninbot.com:2049"`). Uses stored username/password. |
| `/JamWide/session/disconnect` | float | Disconnect from server. Send `1.0` to trigger. |

**Connect validation:** Host is required. Port defaults to 2049 if omitted. IPv6 addresses must be in brackets (e.g. `"[::1]:2049"`). Maximum string length is 256 characters.

---

## TouchOSC Setup Guide

### Quick Start

1. Download JamWide's TouchOSC template: [`assets/JamWide.tosc`](https://github.com/mkschulze/JamWide/raw/main/assets/JamWide.tosc)
2. Import the `.tosc` file into TouchOSC (File > Open)
3. Configure the OSC connection (see below)
4. Switch to Play mode and start controlling

### Connection Setup

**In JamWide** (click the OSC dot in the footer):

| Setting | Value | Notes |
|---------|-------|-------|
| Enable OSC | On | Check the box |
| Receive Port | 9000 | JamWide listens here |
| Send IP | *your device's IP* | Use `127.0.0.1` if TouchOSC is on the same computer. Use your iPad/phone's IP if on a different device. |
| Send Port | 9001 | JamWide sends feedback here |

**In TouchOSC** (Connections > OSC > Connection 1):

| Setting | Value | Notes |
|---------|-------|-------|
| Protocol | UDP | |
| Host | *your computer's IP* | Use `127.0.0.1` if on the same computer. Otherwise your Mac/PC's local IP (e.g. `192.168.1.x`). |
| Send Port | 9000 | Must match JamWide's Receive Port |
| Receive Port | 9001 | Must match JamWide's Send Port |

**Finding your IP:**
- **macOS:** `ifconfig en0 | grep "inet "` or System Settings > Network
- **Windows:** `ipconfig` in Command Prompt
- **iPad/iPhone:** Settings > Wi-Fi > tap your network > IP Address

### Template Contents

The shipped template (`JamWide.tosc`) targets iPad landscape (1024x768) and includes:

| Section | Controls |
|---------|----------|
| **Session Info** | BPM, BPI, beat, status, users display |
| **Connect** | Server address text field, Connect + Disconnect buttons |
| **Master** | Volume fader, mute button, VU L/R meters |
| **Metronome** | Volume fader, pan knob, mute button |
| **Local 1-4** | Volume, pan, mute, solo, VU L/R per channel |
| **Remote 1-8** | Name label, volume, pan, mute, solo per user |
| **Video** | Active toggle, 8 popout trigger buttons (1-8) |

The OSC server supports 16 remote user slots. The template shows 8 (covers most sessions). You can extend it in the TouchOSC editor if needed.

### Troubleshooting

- **Nothing happens when moving faders:** Check that both ports match between JamWide and TouchOSC. The Send Port in TouchOSC must equal the Receive Port in JamWide (default: 9000).
- **Faders don't update in TouchOSC:** Check JamWide's Send IP points to your TouchOSC device's IP.
- **OSC dot is red:** Port is in use by another application. Try different ports (e.g. 9002/9003).
- **Feedback oscillation:** Should not happen (echo suppression is built-in). If it does, check that you don't have two OSC sources sending to the same port.
