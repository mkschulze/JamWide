---
layout: default
title: Documentation
---

# Documentation

Everything you need to know about using and building JamWide.

---

## Quick Start

1. **Install the plugin** — Download from the [releases page](/download) and copy to your plugin folder
2. **Load in your DAW** — Add JamWide to a track (or launch standalone)
3. **Connect to a server** — Enter a server address or pick from the browser
4. **Start jamming!** — Route audio to the plugin and play

---

## User Guide

### Connecting to a Server

1. Open the JamWide plugin GUI
2. In the connection bar at the top, enter:
   - **Server**: Address and port (e.g., `ninbot.com:2049`)
   - **Username**: Your display name
   - **Password**: Leave empty for public servers
3. Click **Connect**

**Tip:** Click the server browser button to see active public servers with player counts. Double-click a server to connect immediately.

### Audio Routing

JamWide receives audio from your DAW track and sends it to the NINJAM session:

```
Your Input -> DAW Track -> JamWide Plugin -> NINJAM Server
                               |
                         Plugin Output -> Your Speakers
                               ^
                      Other Players' Audio
```

**Best Practice:** Create a dedicated track for JamWide and route your instrument to it.

### Multichannel Routing

JamWide provides 17 stereo output buses for advanced DAW mixing:

- **Bus 0 (Main Mix)**: All participants mixed together (always active)
- **Bus 1-15 (Remote)**: Individual participant routing
- **Bus 16 (Metronome)**: Dedicated metronome output

Click the **Route** button to switch between modes:
- **Manual** — All audio on main mix only
- **Assign by User** — Each user automatically assigned to a separate bus
- **Assign by Channel** — Each remote channel on a separate bus

To use in your DAW: enable additional output buses in the plugin I/O settings, then create aux/return tracks receiving from each bus.

### Codec Selection

JamWide supports two audio codecs:
- **FLAC** (default) — Lossless audio quality, higher bandwidth
- **Vorbis** — Compressed audio, lower bandwidth

Switch codecs using the selector in the connection bar. The switch applies cleanly at the next interval boundary. FLAC and Vorbis users can coexist in the same session.

### DAW Transport Sync

When Sync is enabled:
1. Click the **Sync** button (turns amber — "Waiting")
2. Press Play in your DAW (button turns green — "Active")
3. JamWide only broadcasts while the DAW is playing
4. Stop the transport to mute your send

In **standalone mode**, audio broadcasts continuously — no sync button is shown.

### Understanding NINJAM Timing

NINJAM uses **intervals** instead of real-time audio:

- The server defines a BPM and BPI (beats per interval)
- You hear what others played in the **previous** interval
- Your audio is recorded and sent to others for the **next** interval

This means there's always a one-interval delay, but everyone is perfectly synchronized.

**Example:** At 120 BPM with 16 BPI, each interval is 8 seconds.

### BPM/BPI Voting

Click the BPM or BPI value in the beat bar to propose a change. Type a new value and press Enter. This sends a `!vote bpm` or `!vote bpi` command. The server applies the change if enough users vote.

### Chat

The built-in chat lets you communicate with other musicians:

- Messages appear with color-coded formatting (system, topic, user)
- Type a message and press Enter to send
- Chat history scrolls automatically with a jump-to-bottom button
- Toggle the chat sidebar via the context menu or keyboard shortcut

### Mixer Controls

Each channel strip provides:
- **Volume fader** — Custom fader with power-curve mapping
- **Pan slider** — Stereo position with center notch
- **Mute/Solo buttons** — Per-channel isolation
- **VU meter** — Real-time level display
- **Routing selector** — Choose output bus (when multichannel routing is active)

Local channels can be expanded to show up to 4 input channels with individual controls.

---

## Parameters Reference

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Master Volume | 0.0 -- 1.0 | 0.8 | Overall output level |
| Master Mute | On/Off | Off | Mute all output |
| Metronome Volume | 0.0 -- 1.0 | 0.5 | Click track level |
| Metronome Mute | On/Off | Off | Mute metronome |
| Local Vol 0-3 | 0.0 -- 1.0 | 0.8 | Per-channel local volume |
| Local Pan 0-3 | -1.0 -- 1.0 | 0.0 | Per-channel local pan |
| Local Mute 0-3 | On/Off | Off | Per-channel local mute |

---

## Building from Source

### Requirements

- CMake 3.20 or later
- C++20 compatible compiler
  - **macOS:** Xcode 14+ / Apple Clang 14+
  - **Windows:** Visual Studio 2022 / MSVC 19.30+
  - **Linux:** GCC 12+ or Clang 15+
- Git (for submodule dependencies)

### Clone the Repository

```bash
git clone --recursive https://github.com/mkschulze/JamWide.git
cd JamWide
```

### Build (macOS)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DJAMWIDE_BUILD_JUCE=ON

cmake --build build --config Release
```

### Build (Windows)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DJAMWIDE_BUILD_JUCE=ON
cmake --build build --config Release
```

### Build (Linux)

```bash
# Install dependencies
sudo apt-get install -y build-essential cmake pkg-config \
  libasound2-dev libjack-jackd2-dev libfreetype-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxcomposite-dev libgl1-mesa-dev libcurl4-openssl-dev \
  libwebkit2gtk-4.1-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release -DJAMWIDE_BUILD_JUCE=ON
cmake --build build --config Release
```

---

## Architecture

```
JamWide/
├── juce/                # JUCE plugin and UI
│   ├── JamWideJuceProcessor.h/cpp   # AudioProcessor, processBlock, state
│   ├── JamWideJuceEditor.h/cpp      # Editor shell, event drain, layout
│   ├── NinjamRunThread.h/cpp        # NJClient run loop, command dispatch
│   └── ui/              # JUCE UI components
├── src/
│   ├── core/            # NJClient (networking, audio encode/decode)
│   ├── threading/       # Command/event types, SPSC ring buffers
│   ├── net/             # Server list fetcher
│   └── ui/              # Shared state types
├── wdl/                 # WDL libraries (jnetlib, sha, FLAC/Vorbis codecs)
├── libs/                # Submodules (JUCE, libFLAC, libogg, libvorbis)
└── CMakeLists.txt
```

### Threading Model

JamWide uses a lock-free architecture for thread safety:

| Thread | Responsibility | Communication |
|--------|---------------|---------------|
| **Audio Thread** | processBlock, AudioProc, transport sync | Lock-free atomics |
| **Run Thread** | NJClient::Run(), command dispatch, network I/O | SPSC cmd_queue (UI to Run), evt_queue (Run to UI) |
| **UI Thread** | JUCE Components, 20 Hz timer, event drain | SPSC evt_queue + chat_queue |

---

## Troubleshooting

### Plugin doesn't appear in DAW

1. Verify the plugin is in the correct folder
2. Check that your DAW supports the format (VST3/AU/CLAP)
3. Rescan plugins in your DAW
4. On macOS, you may need to allow the plugin in System Preferences > Security

### Can't connect to server

1. Check your internet connection
2. Verify the server address and port
3. Try a different server from the browser
4. Check if a firewall is blocking the connection

### No multichannel output in DAW

1. Enable additional output buses in the plugin I/O configuration
2. Create aux/return tracks receiving from the plugin's output buses
3. Make sure routing mode is set to "Assign by User" or "Assign by Channel"

### Audio issues

1. Ensure your DAW's sample rate matches a common rate (44.1k, 48k)
2. Check that audio is routed to the JamWide track
3. Verify Master Volume is not muted
4. Check the VU meters to confirm audio is flowing

---

## Contributing

We welcome contributions! See the [GitHub repository](https://github.com/mkschulze/JamWide) for:

- Issue reporting
- Pull request guidelines
- Development setup
