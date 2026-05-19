---
layout: default
title: Download
---

# Download JamWide

---

## Latest Beta — v1.1-beta.20.11 (macOS Universal + Windows x64)

**Critical fix release.** v1.1-beta.20.8 crashed on launch on every macOS user's machine because the ffmpeg dylibs weren't bundled inside the `.app` / `.vst3` / `.component` / `.clap` — the binaries pointed at the GitHub Actions runner's build path. v1.1-beta.20.11 ships dylibs inside each bundle's `Contents/Frameworks/` and uses `@loader_path` so they resolve anywhere.

**Highlights (carried over from beta.20.8):**
- Native webcam capture + H.264 broadcast/receive in standalone and DAW-hosted plugin
- Per-user video tile grid + detachable popout windows (multi-monitor)
- HD broadcast preset (1280×720 @ 30fps); Medium/Low presets for bandwidth-constrained sessions
- iOS NinjamZap-mobile receiver compat (cross-AI encoder review by Javier @ NinjamZap)
- Windows x64 build of the native video stack

**This is beta software** — report issues on [GitHub](https://github.com/mkschulze/JamWide/issues).

<div class="download-section">
  <a href="https://github.com/mkschulze/JamWide/releases/tag/v1.1-beta.20.11" class="btn btn-primary btn-large">
    Download v1.1-beta.20.11 (macOS + Windows)
  </a>
  <p class="version-info">macOS Universal (Apple Silicon + Intel) / Windows x64 — Linux next beta</p>
</div>

> macOS binary is Developer-ID signed + notarized (Gatekeeper-friendly). Windows binary is unsigned — SmartScreen will show "More info → Run anyway" on first launch.

---

## Linux — use v1.1-beta.20.5 for now

If you're on Linux, use the older cross-platform beta until the next tag lands the camera-code conditional. v1.1-beta.20.11 ships macOS + Windows only.

<div class="download-section">
  <a href="https://github.com/mkschulze/JamWide/releases/tag/v1.1-beta.20.5" class="btn btn-large">
    Download v1.1-beta.20.5 (Linux x64)
  </a>
  <p class="version-info">Linux x64 — last cross-platform beta with Linux build</p>
</div>

---

## v1.0 Stable (Legacy)

The previous CLAP/ImGui version. Recommended if you need a version that has been field-tested for longer, or if the beta has an issue on your system. No new features, but stable on all its supported platforms.

<div class="download-section">
  <a href="https://github.com/mkschulze/JamWide/releases/tag/v1.0.0" class="btn btn-large">
    Download v1.0 (Legacy)
  </a>
  <p class="version-info">macOS (Universal) / Windows (x64)</p>
</div>

---

## Available Formats

### macOS (Universal: Intel + Apple Silicon)

| Format | File | Install Location |
|--------|------|------------------|
| VST3 | `JamWide.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| Audio Unit | `JamWide.component` | `~/Library/Audio/Plug-Ins/Components/` |
| CLAP | `JamWide.clap` | `~/Library/Audio/Plug-Ins/CLAP/` |
| Standalone | `JamWide.app` | `/Applications/` |

### Windows (64-bit)

| Format | File | Install Location |
|--------|------|------------------|
| VST3 | `JamWide.vst3` | `%LOCALAPPDATA%\Programs\Common\VST3\` |
| CLAP | `JamWide.clap` | `%LOCALAPPDATA%\Programs\Common\CLAP\` |
| Standalone | `JamWide.exe` | Anywhere you like |

### Linux (64-bit)

| Format | File | Install Location |
|--------|------|------------------|
| VST3 | `JamWide.vst3` | `~/.vst3/` |
| CLAP | `JamWide.clap` | `~/.clap/` |
| Standalone | `JamWide` | Anywhere you like |

---

## Installation

1. Download the archive for your platform from the [releases page](https://github.com/mkschulze/JamWide/releases) (`.tar.gz` for macOS/Linux, `.zip` for Windows)
2. Extract the plugin files
3. Copy to the appropriate folder (see tables above)
4. Restart your DAW
5. Scan for new plugins if necessary

---

## System Requirements

### macOS
- macOS 10.15 (Catalina) or later
- Intel or Apple Silicon (universal binary)
- 64-bit DAW with VST3, AU, or CLAP support

### Windows
- Windows 10 or later (64-bit)
- 64-bit DAW with VST3 or CLAP support

### Linux
- Ubuntu 22.04+ or equivalent
- X11 display server
- ALSA or JACK audio
- 64-bit DAW with VST3 or CLAP support

---

## Build from Source

Prefer to compile yourself? See the [documentation](/documentation#building-from-source) for build instructions.

---

## Previous Releases

All releases are available on the [GitHub Releases page](https://github.com/mkschulze/JamWide/releases).
