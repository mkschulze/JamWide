---
layout: default
title: Features
---

# Features

JamWide packs the full NINJAM experience into a plugin you can use in any DAW, or as a standalone app.

---

## Audio

### FLAC Lossless Codec (in development)
- FLAC encode/decode is partially implemented but does not yet round-trip cleanly
- This beta uses OGG/Vorbis exclusively for sessions
- Once FLAC round-trips: switch between FLAC and Vorbis per session via UI toggle, with switches applying at the next interval boundary, and mixed-codec sessions working naturally

### Multichannel Output Routing
- 17 stereo output buses: main mix + 15 remote + metronome
- **Assign by User** — each participant gets their own stereo pair
- **Assign by Channel** — each remote channel gets its own pair
- Metronome on a dedicated bus, independent of master volume
- Main mix always available on bus 0

### DAW Transport Sync
- Plugin reads host transport state via AudioPlayHead
- Broadcasting only occurs when the DAW is playing
- Stop the transport to mute your send
- Live BPM/BPI changes applied at interval boundaries without reconnecting
- Session position tracking (interval count, elapsed time, beat position)
- Standalone mode provides pseudo-transport driven by server BPM

### 4-Channel Local Input
- Up to 4 stereo local input buses
- Per-channel volume, pan, mute, and input bus selector
- Transmit toggle per channel
- Expand/collapse UI for child channels

---

## Mixer

### Per-Channel Controls
- Volume fader with power-curve mapping (more travel in low/mid dB range)
- Pan slider with center notch
- Mute and solo buttons
- Real-time VU meters (30 Hz refresh)

### Remote User Mixer
- Automatic channel strips for each remote participant
- Stable identity tracking (no UI jumps when users join/leave)
- Per-strip routing selector for multichannel output assignment

### Metronome
- Volume and mute controls
- Dedicated output bus (independent of master volume)
- Follows server BPM

### State Persistence
- All mixer settings saved via APVTS (AudioProcessorValueTreeState)
- Persists across DAW save/load cycles
- Includes: volumes, pans, mutes, codec selection, routing mode, UI scale, chat visibility

---

## OSC Remote Control

### Bidirectional OSC Server
- Control all mixer parameters from any OSC control surface (TouchOSC, Open Stage Control, etc.)
- Real-time bidirectional feedback: move a fader on your surface, it moves in JamWide, and vice versa
- Echo suppression prevents feedback oscillation between plugin and control surface
- 100ms update rate with dirty-flag optimization (only changed values are sent)

### Full Parameter Mapping
- Master volume, mute
- Metronome volume, pan, mute
- 4 local channels: volume, pan, mute, solo
- Dual namespace: normalized 0-1 (`/JamWide/local/1/volume`) and dB scale (`/JamWide/local/1/volume/db`)

### Session Telemetry
- BPM, BPI, beat position, connection status
- User count, codec name, sample rate
- VU meters for master and all local channels

### Configuration
- Click the OSC status dot in the footer to configure
- Set receive port (default 9000), send IP (default 127.0.0.1), send port (default 9001)
- 3-state status indicator: green (active), red (error), grey (disabled)
- Settings persist across DAW sessions

See the [OSC Reference](/osc) for the complete address list.

---

## User Interface

### Custom Dark Theme
- Custom JUCE LookAndFeel with consistent pro-audio styling
- Scalable UI (1x, 1.5x, 2x) via right-click context menu

### Connection Bar
- Server address, username, and password fields
- Connect/disconnect with status indicator
- Codec selector (Vorbis; FLAC option present but in development)
- Route button with mode popup
- Sync button with 3-state feedback (Idle/Waiting/Active)

### Chat Panel
- Color-coded message history (system, topic, user messages)
- Auto-scroll with jump-to-bottom indicator
- Topic display
- Toggleable sidebar

### Server Browser
- Public server list from autosong.ninjam.com
- Live user counts per server
- Single-click to fill address, double-click to connect

### Beat Bar
- Visual interval progress with beat markers
- Adaptive numbering density (adjusts to BPI)
- Click-to-edit BPM/BPI voting
- Flash animation on server BPM/BPI changes

### Session Info Strip
- Interval count, elapsed time, current beat
- Sync state indicator
- Toggleable via context menu

---

## Plugin Formats

| Format | Status | Platforms |
|--------|--------|-----------|
| VST3 | Full support | macOS, Windows, Linux |
| Audio Unit v2 | Full support | macOS |
| CLAP | Full support | macOS, Windows, Linux |
| Standalone | Full support | macOS, Windows, Linux |

---

## Parameters

All APVTS parameters are automatable in your DAW:

| Parameter | Range | Description |
|-----------|-------|-------------|
| Master Volume | 0.0 -- 1.0 | Overall output level |
| Master Mute | On/Off | Mute all output |
| Metronome Volume | 0.0 -- 1.0 | Click track level |
| Metronome Mute | On/Off | Mute metronome |
| Local Vol 0-3 | 0.0 -- 1.0 | Per-channel local input volume |
| Local Pan 0-3 | -1.0 -- 1.0 | Per-channel local input pan |
| Local Mute 0-3 | On/Off | Per-channel local mute |

---

<div class="cta-section">
  <a href="/download" class="btn btn-primary">Download Now</a>
</div>
