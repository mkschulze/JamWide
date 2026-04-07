---
layout: default
title: Download
---

# Download JamWide

---

## v1.1 Beta — JUCE Rewrite

The new JUCE-based version of JamWide is available as a beta. This is a complete rewrite with FLAC lossless audio, multichannel output routing, DAW transport sync, standalone mode, and Linux support.

**This is beta software** — it may contain bugs. Please report issues on [GitHub](https://github.com/mkschulze/JamWide/issues).

<div class="download-section">
  <a href="https://github.com/mkschulze/JamWide/releases/tag/v1.1-beta.7" class="btn btn-primary btn-large">
    Download v1.1-beta.7
  </a>
  <p class="version-info">macOS (Universal) / Windows (x64) / Linux (x64)</p>
</div>

> **Looking for the stable v1.0 release?** The previous CLAP/ImGui version is still available on the [releases page](https://github.com/mkschulze/JamWide/releases/tag/v1.0.0).

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

1. Download the `.zip` file for your platform from the [releases page](https://github.com/mkschulze/JamWide/releases)
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
