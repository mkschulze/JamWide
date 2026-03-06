# Technology Stack

**Analysis Date:** 2026-03-07

## Languages

**Primary:**
- C++ 20 - Core plugin implementation, audio processing, networking
- Objective-C/Objective-C++ - macOS platform GUI integration
- C - WDL library components (jnetlib, SHA, RNG)

**Secondary:**
- Python 3 - Build tools and development utilities

## Runtime

**Environment:**
- Cross-platform native compilation (macOS, Windows)
- macOS: 10.15 (Catalina) or later
- Windows: Windows 10 or later (64-bit)

**Build System:**
- CMake 3.20+ - Primary build configuration
- MSVC 19.30+ (Visual Studio 2022) - Windows compilation
- Apple Clang 14+ (Xcode 14) - macOS compilation

## Frameworks

**Core Plugin:**
- CLAP 1.x - Plugin API standard (`libs/clap`)
- clap-helpers - CLAP utility library (`libs/clap-helpers`)
- clap-wrapper - Multi-format plugin wrapper (CLAP/VST3/AU) (`libs/clap-wrapper`)

**Audio/Codec:**
- libogg - OGG container format (`libs/libogg`)
- libvorbis - OGG Vorbis audio codec (`libs/libvorbis`)

**User Interface:**
- Dear ImGui - Immediate mode GUI framework (`libs/imgui`)
- ImGui backends:
  - Metal - macOS rendering
  - Direct3D 11 - Windows rendering
  - OS-specific window management (Win32/Cocoa)

**Networking:**
- WDL jnetlib - TCP/HTTP networking library (`wdl/jnetlib`)

## Key Dependencies

**Critical:**
- libvorbis 1.3+ - Audio codec for streaming (NINJAM protocol requirement)
- libogg 1.3+ - Container format for Vorbis encoding
- ImGui - GUI rendering across platforms
- clap-wrapper - Enables multi-format plugin support

**Infrastructure:**
- WDL (Cockos) - Networking, SHA hashing, RNG, string utilities
- picojson - JSON parsing for server list responses (`src/third_party/picojson.h`)

## Configuration

**Environment:**
- Build-time options:
  - `JAMWIDE_UNIVERSAL` - Build universal binary on macOS (arm64 + x86_64)
  - `JAMWIDE_DEV_BUILD` - Enable verbose development logging
  - `CMAKE_BUILD_TYPE=Release` - Production builds
  - `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=TRUE` - Auto-download VST3 SDK

**Build:**
- `CMakeLists.txt` - Main build configuration
- Platform-specific:
  - `src/platform/gui_macos.mm` - macOS Metal GUI
  - `src/platform/gui_win32.cpp` - Windows D3D11 GUI

**Plugin Formats:**
- CLAP (.clap) - Primary format
- VST3 (.vst3) - Compatibility format (via clap-wrapper)
- Audio Unit v2 (.component) - macOS legacy format (via clap-wrapper)

## Platform Requirements

**Development:**
- Git (for submodule dependencies)
- CMake 3.20+
- Compiler: Xcode 14+ (macOS) or Visual Studio 2022+ (Windows)
- C++20 support required

**Production:**
- Plugin installer paths:
  - macOS: `~/Library/Audio/Plug-Ins/{CLAP,VST3,Components}/`
  - Windows: `%LOCALAPPDATA%\Programs\Common\{CLAP,VST3}\`

**Runtime:**
- NINJAM server connectivity (TCP port 2049 default)
- HTTP connectivity for server list fetching (autosong.ninjam.com)
- Audio interface with ASIO/CoreAudio support (via DAW)

---

*Stack analysis: 2026-03-07*
