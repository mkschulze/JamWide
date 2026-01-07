# NINJAM CLAP Plugin

A modern [CLAP](https://clap.audio) audio plugin client for [NINJAM](https://www.cockos.com/ninjam/) — the open-source, internet-based real-time collaboration software for musicians.

![License](https://img.shields.io/badge/license-GPL--2.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-lightgrey.svg)
![Status](https://img.shields.io/badge/status-in%20development-yellow.svg)

## What is NINJAM?

NINJAM (Novel Intervallic Network Jamming Architecture for Music) allows musicians to jam together over the internet in real-time. Unlike traditional approaches that try to minimize latency, NINJAM embraces it by using a time-synchronized approach where everyone plays along with what was recorded in the previous interval. This creates a unique collaborative experience where musicians can perform together regardless of geographic location.

## About This Plugin

This project ports the NINJAM client functionality into a cross-platform CLAP audio plugin, allowing you to:

- **Use NINJAM directly in your DAW** — Connect to jam sessions without leaving your production environment
- **Route audio flexibly** — Use your DAW's mixer for monitoring and processing
- **Record sessions natively** — Capture everything directly in your DAW's timeline
- **Apply effects** — Process incoming audio from other musicians with your favorite plugins

## Features

### Core Functionality
- Connect to any NINJAM server
- Real-time audio streaming with OGG/Vorbis encoding
- Automatic BPM/BPI synchronization with the server
- Local channel management
- View connected users and their channels

### Audio
- Stereo input/output (expandable in future versions)
- Master volume control with soft clipping
- Per-channel gain adjustment
- Configurable audio quality

### User Interface
- Modern ImGui-based interface
- Native look and feel (Metal on macOS, D3D11 on Windows)
- Connection panel with server browser
- Real-time status display
- User/channel visualization

## Supported Hosts

Any DAW that supports CLAP plugins, including:
- [Bitwig Studio](https://www.bitwig.com/)
- [REAPER](https://www.reaper.fm/)
- [MultitrackStudio](https://www.multitrackstudio.com/)
- [And many more...](https://github.com/free-audio/clap/wiki/Hosts)

## Building

### Requirements

- CMake 3.20 or later
- C++20 compatible compiler
  - macOS: Xcode 14+ / Apple Clang 14+
  - Windows: Visual Studio 2022 / MSVC 19.30+
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
git clone --recursive https://github.com/yourusername/ninjam-clap.git
cd ninjam-clap

# Create build directory
mkdir build && cd build

# Configure (macOS)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Configure (Windows with Visual Studio)
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
cmake --build . --config Release
```

### Output

- **macOS**: `build/NINJAM.clap` (bundle)
- **Windows**: `build/Release/NINJAM.clap` (DLL)

## Installation

### macOS
Copy `NINJAM.clap` to one of:
- `~/Library/Audio/Plug-Ins/CLAP/` (user)
- `/Library/Audio/Plug-Ins/CLAP/` (system-wide)

### Windows
Copy `NINJAM.clap` to one of:
- `%LOCALAPPDATA%\Programs\Common\CLAP\` (user)
- `C:\Program Files\Common Files\CLAP\` (system-wide)

## Usage

1. Load the NINJAM plugin on a track in your DAW
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

🚧 **In Active Development**

### Completed
- [x] Core NJClient port (audio engine, networking)
- [x] CLAP wrapper (audio processing, parameters, state)
- [x] Platform GUI framework (macOS Metal, Windows D3D11)
- [x] Basic UI panels

### In Progress
- [ ] Multi-instance support improvements
- [ ] Full remote user visualization
- [ ] Local channel editing

### Planned
- [ ] Built-in server browser
- [ ] Per-user mixing controls
- [ ] Chat integration
- [ ] Linux support (X11/Wayland + OpenGL)

## Architecture

```
ninjam-clap/
├── src/
│   ├── core/           # NJClient port (networking, audio decode/encode)
│   ├── plugin/         # CLAP entry point and wrapper
│   ├── platform/       # OS-specific GUI (Metal/D3D11 + ImGui)
│   └── ui/             # ImGui UI panels
├── external/           # Third-party dependencies
└── CMakeLists.txt
```

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

### Development Setup

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the **GNU General Public License v2.0** — see the [LICENSE](LICENSE) file for details.

NINJAM and the original client code are Copyright © Cockos Incorporated.

## Acknowledgments

- [Cockos](https://www.cockos.com/) for creating NINJAM and making it open source
- [WDL](https://www.cockos.com/wdl/) library by Cockos
- The [CLAP](https://clap.audio) team for the excellent plugin API
- [Omar Cornut](https://github.com/ocornut) for Dear ImGui

## See Also

- [NINJAM Official Site](https://www.cockos.com/ninjam/)
- [NINJAM Server List](https://ninbot.com/)
- [CLAP Audio Plugin Standard](https://clap.audio)
- [ReaNINJAM](https://www.reaper.fm/ninjam.php) — Original REAPER extension

---

*Made with ♪ for musicians who want to jam together, anywhere in the world.*