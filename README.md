# JamWide

A modern audio plugin client for [NINJAM](https://www.cockos.com/ninjam/) — the open-source, internet-based real-time collaboration software for musicians.

🌐 **Website:** [jamwide.audio](https://jamwide.audio)

![License](https://img.shields.io/badge/license-GPL--2.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-lightgrey.svg)
![Formats](https://img.shields.io/badge/formats-CLAP%20%7C%20VST3%20%7C%20AU-blue.svg)
![Version](https://img.shields.io/badge/version-v0.132-blue.svg)
![Status](https://img.shields.io/badge/status-beta-yellow.svg)

## What is NINJAM?

NINJAM (Novel Intervallic Network Jamming Architecture for Music) allows musicians to jam together over the internet in real-time. Unlike traditional approaches that try to minimize latency, NINJAM embraces it by using a time-synchronized approach where everyone plays along with what was recorded in the previous interval. This creates a unique collaborative experience where musicians can perform together regardless of geographic location.

## About This Plugin

JamWide ports the NINJAM client functionality into a cross-platform CLAP audio plugin, allowing you to:

- **Use NINJAM directly in your DAW** — Connect to jam sessions without leaving your production environment
- **Route audio flexibly** — Use your DAW's mixer for monitoring and processing
- **Record sessions natively** — Capture everything directly in your DAW's timeline
- **Apply effects** — Process incoming audio from other musicians with your favorite plugins

## Features

### Core Functionality
- Connect to any NINJAM server
- Real-time audio streaming with OGG/Vorbis encoding
- Automatic BPM/BPI synchronization with the server
- BPM/BPI voting via chat commands
- Local and remote channel management
- Chat room with message history and timestamps

### Audio
- Stereo input/output
- Master and metronome volume/pan/mute controls
- Per-channel volume, pan, mute, and solo
- VU meters for all channels
- Visual timing guide for beat alignment
- Soft clipping on master output

### User Interface
- Modern ImGui-based interface
- Native rendering (Metal on macOS, D3D11 on Windows)
- Server browser with live user lists
- Real-time status and connection display
- Collapsible panels for all sections

## Supported Hosts

Works with any DAW that supports CLAP, VST3, or Audio Unit plugins:

| Format | Hosts |
|--------|-------|
| **CLAP** | Bitwig Studio, REAPER, MultitrackStudio |
| **VST3** | Ableton Live, Cubase, FL Studio, REAPER, Studio One |
| **AU v2** | Logic Pro, GarageBand, MainStage (macOS only) |

### System Requirements

- **Windows**: Windows 10 or later (64-bit)
- **macOS**: macOS 10.15 (Catalina) or later

## Building

### Requirements

- CMake 3.20 or later
- C++20 compatible compiler
  - macOS: Xcode 14+ / Apple Clang 14+ (macOS 10.15+)
  - Windows: Visual Studio 2022 / MSVC 19.30+ (Windows 10+)
- Git (for submodule dependencies)

### Dependencies (included as submodules)

- [CLAP](https://github.com/free-audio/clap) — Plugin API
- [Dear ImGui](https://github.com/ocornut/imgui) — User interface
- [libogg](https://github.com/xiph/ogg) — Audio container format
- [libvorbis](https://github.com/xiph/vorbis) — Audio codec
- [picojson](https://github.com/kazuho/picojson) — JSON parser

### Build Instructions

```bash
# Clone the repository
git clone --recursive https://github.com/mkschulze/JamWide.git
cd JamWide

# Initialize submodules if not cloned with --recursive
git submodule update --init --recursive
```

#### macOS

```bash
# Configure - Dev build with verbose logging
cmake -B build -DCMAKE_BUILD_TYPE=Release -DJAMWIDE_DEV_BUILD=ON

# Configure - Production build with minimal logging  
cmake -B build -DCMAKE_BUILD_TYPE=Release -DJAMWIDE_DEV_BUILD=OFF

# Build
cmake --build build --config Release

# Quick install (builds and installs to user plugin folders)
./install.sh
```

#### Windows

**Requirements:**
- Visual Studio 2022 (or newer) with C++ Desktop Development workload
- CMake 3.20+ (included with Visual Studio)
- Git for Windows

```powershell
# Configure with Visual Studio 2022 (or Visual Studio 18 for VS 2026)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=TRUE

# Build with MSBuild
$MSBUILD = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
& $MSBUILD build\jamwide.sln /p:Configuration=Release /v:minimal

# Quick install (builds and installs to user plugin folders)
.\install-win.ps1
```

**Note:** The `-DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=TRUE` flag automatically downloads VST3 SDK and other dependencies.

### Quick Install

#### macOS
```bash
./install.sh
# Installs to:
# - ~/Library/Audio/Plug-Ins/CLAP/JamWide.clap
# - ~/Library/Audio/Plug-Ins/VST3/JamWide.vst3
# - ~/Library/Audio/Plug-Ins/Components/JamWide.component
```

#### Windows
```powershell
.\install-win.ps1
# Installs to:
# - %LOCALAPPDATA%\Programs\Common\CLAP\JamWide.clap
# - %LOCALAPPDATA%\Programs\Common\VST3\JamWide.vst3
```

### Build Output

#### macOS
- `build/JamWide.clap` — CLAP plugin
- `build/JamWide.vst3` — VST3 plugin
- `build/JamWide.component` — Audio Unit v2

#### Windows
- `build/CLAP/Release/JamWide.clap` — CLAP plugin
- `build/Release/JamWide.vst3` — VST3 plugin

## Installation

### macOS (CLAP)
Copy `JamWide.clap` to:
- `~/Library/Audio/Plug-Ins/CLAP/` (user)

### macOS (VST3)
Copy `JamWide.vst3` to:
- `~/Library/Audio/Plug-Ins/VST3/` (user)

### macOS (Audio Unit)
Copy `JamWide.component` to:
- `~/Library/Audio/Plug-Ins/Components/` (user)

### Windows (CLAP)
Copy `JamWide.clap` to:
- `%LOCALAPPDATA%\Programs\Common\CLAP\` (user)

### Windows (VST3)
Copy `JamWide.vst3` to:
- `%LOCALAPPDATA%\Programs\Common\VST3\` (user)

## Usage

1. Load the JamWide plugin on a track in your DAW
2. Open the plugin GUI
3. Enter a server address (e.g., `ninbot.com:2049`) or select from the list
4. Enter your username and optional password
5. Click **Connect**
6. Route audio to the plugin's input for your local channel
7. The plugin output contains the mixed audio from all participants

### Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| Master Volume | 0.0 – 1.0 | Overall output level |
| Metronome Volume | 0.0 – 1.0 | Click track level |
| Metronome Pan | -1.0 – 1.0 | Click track stereo position |
| Monitor Input | On/Off | Hear your own input |
| Connected | On/Off | Connection state |

## Project Status

⚠️ **Beta Release** — macOS tested, Windows builds successfully (testing in progress)

### Features
- [x] Core NJClient port (audio engine, networking)
- [x] CLAP, VST3, and Audio Unit v2 plugin formats
- [x] Platform GUI (macOS Metal, Windows D3D11)
- [x] Full UI: status, connection, chat, local/remote channels, master controls
- [x] Server browser with live user lists (autosong.ninjam.com)
- [x] VU meters and visual timing guide
- [x] BPM/BPI voting via chat
- [x] Anonymous login support
- [x] Thread-safe command queue architecture
- [x] GitHub Actions CI/CD for Windows and macOS

### Future
- [ ] Linux support (X11/Wayland + OpenGL)
- [ ] UI styling and graphics improvements
- [ ] Per-channel receive toggle

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

The plugin uses a command queue architecture for thread safety:

- **UI Thread** - Renders ImGui, sends commands to run thread
- **Run Thread** - Processes NJClient, handles network I/O
- **Audio Thread** - Calls AudioProc() for sample processing

Communication is lock-free via SPSC ring buffers.

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

### Development Setup

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)

### UI ID Check

To avoid Dear ImGui ID collisions, run the local checker:

```bash
python3 tools/check_imgui_ids.py
```

It flags unscoped widget labels that are not wrapped in `ImGui::PushID(...)`.
5. Open a Pull Request

## License

This project is licensed under the **GNU General Public License v2.0** — see the [LICENSE](LICENSE) file for details.

NINJAM and the original client code are Copyright © Cockos Incorporated.

## Acknowledgments

- [Cockos](https://www.cockos.com/) for creating NINJAM and making it open source
- [WDL](https://www.cockos.com/wdl/) library by Cockos
- The [CLAP](https://cleveraudio.org/) team for the excellent plugin API
- [Omar Cornut](https://github.com/ocornut) for Dear ImGui

## See Also

- [NINJAM Official Site](https://www.cockos.com/ninjam/)
- [NINJAM Server List](https://ninbot.com/)
- [CLAP Audio Plugin Standard](https://cleveraudio.org/)
- [ReaNINJAM](https://github.com/justinfrankel/ninjam) — Original REAPER extension

---

*Made with ♪ for musicians who want to jam together, anywhere in the world.*
