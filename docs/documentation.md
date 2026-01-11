---
layout: default
title: Documentation
---

# Documentation

Everything you need to know about using and building JamWide.

---

## Quick Start

1. **Install the plugin** — Download from the [releases page](/download) and copy to your plugin folder
2. **Load in your DAW** — Add JamWide to a track
3. **Connect to a server** — Enter a server address or pick from the browser
4. **Start jamming!** — Route audio to the plugin and play

---

## User Guide

### Connecting to a Server

1. Open the JamWide plugin GUI
2. In the connection panel, enter:
   - **Server**: Address and port (e.g., `ninbot.com:2049`)
   - **Username**: Your display name
   - **Password**: Leave empty for public servers
3. Click **Connect**

**Tip:** Use the built-in server browser to see active public servers with player counts.

### Audio Routing

JamWide receives audio from your DAW track and sends it to the NINJAM session:

```
Your Input → DAW Track → JamWide Plugin → NINJAM Server
                              ↓
                        Plugin Output → Your Speakers
                              ↑
                     Other Players' Audio
```

**Best Practice:** Create a dedicated track for JamWide and route your instrument to it.

### Understanding NINJAM Timing

NINJAM uses **intervals** instead of real-time audio:

- The server defines a BPM and BPI (beats per interval)
- You hear what others played in the **previous** interval
- Your audio is recorded and sent to others for the **next** interval

This means there's always a one-interval delay, but everyone is perfectly synchronized.

**Example:** At 120 BPM with 16 BPI, each interval is 8 seconds. You hear what others played 8 seconds ago.

### Chat

The built-in chat lets you communicate with other musicians:

- Messages appear in the chat panel with timestamps
- Type a message and press Enter to send
- Chat history is preserved during your session

### Metronome

Use the metronome to stay in time:

- Adjust volume with the Metronome Volume control
- Pan left/right with Metronome Pan
- The metronome follows the server's BPM

---

## Parameters Reference

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Master Volume | 0.0 – 1.0 | 0.8 | Overall output level |
| Metronome Volume | 0.0 – 1.0 | 0.5 | Click track level |
| Metronome Pan | -1.0 – 1.0 | 0.0 | Click track stereo position |
| Monitor Input | On/Off | Off | Hear your own input |
| Connected | On/Off | Off | Connection state |

---

## Building from Source

### Requirements

- CMake 3.20 or later
- C++20 compatible compiler
  - **macOS:** Xcode 14+ / Apple Clang 14+
  - **Windows:** Visual Studio 2022 / MSVC 19.30+
- Git (for submodule dependencies)

### Clone the Repository

```bash
git clone --recursive https://github.com/mkschulze/JamWide.git
cd JamWide
```

### Build (macOS)

```bash
# Create build directory
mkdir build && cd build

# Configure - Development build (verbose logging)
cmake .. -DCMAKE_BUILD_TYPE=Release -DJAMWIDE_DEV_BUILD=ON

# Or Production build (minimal logging)
cmake .. -DCMAKE_BUILD_TYPE=Release -DJAMWIDE_DEV_BUILD=OFF

# Build
cmake --build . --config Release
```

### Build (Windows)

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Quick Install (macOS)

```bash
./install.sh
```

This builds and installs all formats to:
- `~/Library/Audio/Plug-Ins/CLAP/JamWide.clap`
- `~/Library/Audio/Plug-Ins/VST3/JamWide.vst3`
- `~/Library/Audio/Plug-Ins/Components/JamWide.component`

### Build Output

After building:
- `build/JamWide.clap` — CLAP plugin
- `build/JamWide.vst3` — VST3 plugin  
- `build/JamWide.component` — Audio Unit v2 (macOS only)

---

## Architecture

```
JamWide/
├── src/
│   ├── core/           # NJClient port (networking, audio decode/encode)
│   ├── plugin/         # CLAP entry point and wrapper
│   ├── platform/       # OS-specific GUI (Metal/D3D11 + ImGui)
│   ├── threading/      # Run thread, command queue, SPSC ring
│   ├── net/            # Server list fetcher
│   ├── ui/             # ImGui UI panels
│   └── debug/          # Logging utilities
├── wdl/                # WDL libraries (jnetlib, sha, etc.)
├── libs/               # Third-party submodules
└── CMakeLists.txt
```

### Threading Model

JamWide uses a command queue architecture for thread safety:

| Thread | Responsibility |
|--------|---------------|
| **UI Thread** | Renders ImGui, sends commands to run thread |
| **Run Thread** | Processes NJClient, handles network I/O |
| **Audio Thread** | Calls AudioProc() for sample processing |

Communication between threads is lock-free via SPSC ring buffers.

---

## Troubleshooting

### Plugin doesn't appear in DAW

1. Verify the plugin is in the correct folder
2. Check that your DAW supports the format (CLAP/VST3/AU)
3. Rescan plugins in your DAW
4. On macOS, you may need to allow the plugin in System Preferences → Security

### Can't connect to server

1. Check your internet connection
2. Verify the server address and port
3. Try a different server from the browser
4. Check if a firewall is blocking the connection

### Audio issues

1. Ensure your DAW's sample rate matches a common rate (44.1k, 48k)
2. Check that audio is routed to the JamWide track
3. Verify Master Volume is not at zero
4. Check Monitor Input setting

---

## Contributing

We welcome contributions! See the [GitHub repository](https://github.com/mkschulze/JamWide) for:

- Issue reporting
- Pull request guidelines
- Development setup
