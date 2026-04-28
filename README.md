# JamWide

A cross-platform audio plugin and standalone app for [NINJAM](https://www.cockos.com/ninjam/) — the open-source, internet-based real-time collaboration software for musicians.

Built with [JUCE](https://juce.com/) for native performance across macOS, Windows, and Linux.

[jamwide.audio](https://jamwide.audio)

![License](https://img.shields.io/badge/license-GPL--2.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey.svg)
![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU%20%7C%20CLAP%20%7C%20Standalone-blue.svg)
![Build](https://github.com/mkschulze/JamWide/actions/workflows/juce-build.yml/badge.svg)

## What is NINJAM?

NINJAM (Novel Intervallic Network Jamming Architecture for Music) allows musicians to jam together over the internet in real-time. Unlike traditional approaches that try to minimize latency, NINJAM embraces it by using a time-synchronized approach where everyone plays along with what was recorded in the previous interval. This creates a unique collaborative experience where musicians can perform together regardless of geographic location.

## About JamWide

JamWide brings the full NINJAM experience into your DAW as a plugin, or runs as a standalone application:

- **Use NINJAM directly in your DAW** — Connect to jam sessions without leaving your production environment
- **Multichannel output routing** — Route each remote participant to a separate stereo track for independent mixing
- **FLAC lossless audio (in development)** — work in progress; this beta uses OGG/Vorbis
- **DAW transport sync** — Plugin only broadcasts when the DAW is playing
- **Standalone mode** — Use JamWide without a DAW, with built-in audio device selection
- **OSC remote control** — Bidirectional OSC server for control surfaces like TouchOSC

## Features

### Audio
- OGG/Vorbis encoding (FLAC lossless support in development)
- 17 stereo output buses (main mix + 15 remote + metronome)
- Auto-assign routing modes: by user or by channel
- 4 stereo local input channels with per-channel controls
- DAW transport sync — broadcasting gates on play/stop
- Live BPM/BPI changes applied at interval boundaries without reconnect
- Session position tracking (interval count, elapsed time, beat position)

### Mixer
- Per-channel volume, pan, mute, and solo for remote participants
- Local channel controls with input bus selection and transmit toggle
- Metronome with dedicated output bus (independent of master volume)
- Real-time VU meters for all channels (30 Hz refresh)
- Custom fader with power-curve mapping for precise low-end control
- Full state persistence across DAW save/load cycles

### OSC Remote Control
- Bidirectional OSC server for control surfaces (TouchOSC, Open Stage Control, etc.)
- Full parameter mapping: volume, pan, mute, solo for all local channels, master, and metronome
- Dual namespace: normalized 0-1 values and dB scale (`/volume` and `/volume/db`)
- Session telemetry: BPM, BPI, beat position, connection status, user count, codec, sample rate
- VU meters for all channels broadcast at 100ms rate
- 100ms dirty-flag sender with echo suppression (no feedback oscillation)
- Configurable send/receive ports and target IP via status dot popup dialog
- 3-state status indicator: green (active), red (error), grey (disabled)
- Config persists across DAW sessions (state version 2)
- See [OSC Documentation](docs/osc.md) for the full address reference

### User Interface
- Custom JUCE LookAndFeel (dark pro-audio theme)
- Connection bar with server address, codec selector, routing mode, and sync controls
- Chat panel with color-coded messages, auto-scroll, and jump-to-bottom
- Server browser with public server list and double-click connect
- Beat/interval progress bar with BPM/BPI voting via inline edit
- Session info strip with interval count and elapsed time
- Scalable UI (1x, 1.5x, 2x) via right-click context menu

## Supported Formats

| Format | Platform | Hosts |
|--------|----------|-------|
| **VST3** | macOS, Windows, Linux | Ableton Live, Bitwig, Cubase, REAPER, Studio One |
| **AU v2** | macOS | Logic Pro, GarageBand, MainStage |
| **CLAP** | macOS, Windows, Linux | Bitwig Studio, REAPER |
| **Standalone** | macOS, Windows, Linux | No DAW required |

### System Requirements

- **macOS**: macOS 10.15 (Catalina) or later (Intel and Apple Silicon universal binary)
- **Windows**: Windows 10 or later (64-bit)
- **Linux**: Ubuntu 22.04+ or equivalent (X11, ALSA/JACK)

## Building

### Requirements

- CMake 3.20 or later
- C++20 compatible compiler
  - macOS: Xcode 14+ / Apple Clang 14+
  - Windows: Visual Studio 2022 / MSVC 19.30+
  - Linux: GCC 12+ or Clang 15+
- Git (for submodule dependencies)

### Dependencies (included as submodules)

- [JUCE](https://juce.com/) 8.0.12 — Plugin framework and UI
- [libFLAC](https://github.com/xiph/flac) 1.5.0 — Lossless audio codec
- [libogg](https://github.com/xiph/ogg) — Audio container format
- [libvorbis](https://github.com/xiph/vorbis) — Lossy audio codec

### Build Instructions

```bash
# Clone the repository
git clone --recursive https://github.com/mkschulze/JamWide.git
cd JamWide
```

#### macOS

```bash
# Configure (universal binary: arm64 + x86_64)
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DJAMWIDE_BUILD_JUCE=ON

# Build
cmake --build build --config Release
```

#### Windows

```powershell
# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64 -DJAMWIDE_BUILD_JUCE=ON

# Build
cmake --build build --config Release
```

#### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y build-essential cmake pkg-config \
  libasound2-dev libjack-jackd2-dev libfreetype-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxcomposite-dev libgl1-mesa-dev libcurl4-openssl-dev \
  libwebkit2gtk-4.1-dev

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DJAMWIDE_BUILD_JUCE=ON
cmake --build build --config Release
```

### Build Output

| Platform | VST3 | AU | CLAP | Standalone |
|----------|------|-----|------|------------|
| macOS | `build/JamWide_artefacts/Release/VST3/JamWide.vst3` | `build/JamWide_artefacts/Release/AU/JamWide.component` | `build/JamWide_artefacts/Release/CLAP/JamWide.clap` | `build/JamWide_artefacts/Release/Standalone/JamWide.app` |
| Windows | `build\JamWide_artefacts\Release\VST3\JamWide.vst3` | — | `build\JamWide_artefacts\Release\CLAP\JamWide.clap` | `build\JamWide_artefacts\Release\Standalone\JamWide.exe` |
| Linux | `build/JamWide_artefacts/Release/VST3/JamWide.vst3` | — | `build/JamWide_artefacts/Release/CLAP/JamWide.clap` | `build/JamWide_artefacts/Release/Standalone/JamWide` |

## Installation

### macOS
Copy to:
- `~/Library/Audio/Plug-Ins/VST3/JamWide.vst3`
- `~/Library/Audio/Plug-Ins/Components/JamWide.component`
- `~/Library/Audio/Plug-Ins/CLAP/JamWide.clap`
- `/Applications/JamWide.app` (standalone)

### Windows
Copy to:
- `%LOCALAPPDATA%\Programs\Common\VST3\JamWide.vst3`
- `%LOCALAPPDATA%\Programs\Common\CLAP\JamWide.clap`

### Linux
Copy to:
- `~/.vst3/JamWide.vst3`
- `~/.clap/JamWide.clap`

## Usage

1. Load JamWide on a track in your DAW (or launch standalone)
2. Enter a server address or browse the server list
3. Click **Connect**
4. Route audio to the plugin's input channels
5. Use the mixer to adjust remote participants' levels
6. Enable multichannel routing to send each user to a separate DAW track

### Multichannel Routing

JamWide provides 17 stereo output buses:
- **Bus 0 (Main Mix)**: All participants mixed together (always active)
- **Bus 1-15 (Remote)**: Individual participant routing
- **Bus 16 (Metronome)**: Dedicated metronome output

Click the **Route** button to switch between:
- **Manual** — All audio on main mix
- **Assign by User** — Each user on a separate bus
- **Assign by Channel** — Each channel on a separate bus

### DAW Transport Sync

When enabled, JamWide only broadcasts audio while your DAW transport is playing. Stop the transport to mute your send. In standalone mode, audio broadcasts continuously.

## Architecture

```
JamWide/
├── juce/                # JUCE plugin and UI
│   ├── JamWideJuceProcessor.h/cpp   # AudioProcessor, processBlock, state
│   ├── JamWideJuceEditor.h/cpp      # Editor shell, event drain, layout
│   ├── NinjamRunThread.h/cpp        # NJClient run loop, command dispatch
│   ├── osc/             # OSC remote control
│   │   ├── OscServer.h/cpp          # Bidirectional OSC server
│   │   ├── OscAddressMap.h/cpp      # Address-to-parameter mapping
│   │   ├── OscStatusDot.h/cpp       # Footer status indicator
│   │   └── OscConfigDialog.h/cpp    # Config popup dialog
│   └── ui/              # JUCE UI components
│       ├── ConnectionBar.h/cpp      # Server, codec, routing, sync controls
│       ├── ChatPanel.h/cpp          # Chat messages
│       ├── ChannelStrip.h/cpp       # Per-channel mixer controls
│       ├── ChannelStripArea.h/cpp   # Mixer container with VU timer
│       ├── VbFader.h/cpp            # Custom fader component
│       ├── BeatBar.h/cpp            # Beat/interval display
│       ├── SessionInfoStrip.h/cpp   # Session position info
│       ├── VuMeter.h/cpp            # LED VU meter
│       ├── ServerBrowserOverlay.h/cpp
│       ├── LicenseDialog.h/cpp
│       └── JamWideLookAndFeel.h/cpp # Custom dark theme
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

| Thread | Responsibility | Communication |
|--------|---------------|---------------|
| **Audio Thread** | `processBlock` → `AudioProc`, transport sync, VU peaks | Lock-free atomics |
| **Run Thread** | `NJClient::Run()`, command dispatch, network I/O | SPSC cmd_queue (UI→Run), evt_queue (Run→UI) |
| **UI Thread** | JUCE Components, 20 Hz timer, event drain | SPSC evt_queue + chat_queue |
| **OSC Thread** | juce_osc UDP receive | callAsync to UI thread |

All inter-thread communication is lock-free via SPSC ring buffers and atomics. OSC receive callbacks dispatch to the UI thread via `callAsync()` to preserve the SPSC single-producer invariant.

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes
4. Push to the branch
5. Open a Pull Request

## License

This project is licensed under the **GNU General Public License v2.0** — see the [LICENSE](LICENSE) file for details.

NINJAM and the original client code are Copyright Cockos Incorporated.

## Acknowledgments

- [Cockos](https://www.cockos.com/) for creating NINJAM and making it open source
- [WDL](https://www.cockos.com/wdl/) library by Cockos
- [JUCE](https://juce.com/) framework by Raw Material Software
- The [CLAP](https://cleveraudio.org/) team for the plugin format extensions

## See Also

- [NINJAM Official Site](https://www.cockos.com/ninjam/)
- [NINJAM Server List](https://ninbot.com/)
- [JUCE Framework](https://juce.com/)
- [ReaNINJAM](https://github.com/justinfrankel/ninjam) — Original REAPER extension

---

*Made with music for musicians who want to jam together, anywhere in the world.*
