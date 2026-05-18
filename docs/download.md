---
layout: default
title: Download
---

# Download JamWide

---

## Latest macOS Beta — v1.1-beta.20.7 (Universal: Apple Silicon + Intel)

**First public beta of the native H.264 video stack.** Replaces VDO.Ninja browser companion with an in-app/in-plugin video stack compatible with the NinjamZap mobile/web clients via `video.ninjamzap.com:2049`. Single universal binary runs natively on both Apple Silicon and Intel Macs.

**Highlights:**
- Native webcam capture + H.264 broadcast/receive in standalone and DAW-hosted plugin
- Per-user video tile grid + detachable popout windows (multi-monitor)
- HD broadcast preset (1280×720 @ 30fps); Medium/Low presets for bandwidth-constrained sessions
- iOS NinjamZap-mobile receiver compat (cross-AI encoder review by Javier @ NinjamZap)

**This is beta software** — report issues on [GitHub](https://github.com/mkschulze/JamWide/issues).

<div class="download-section">
  <a href="https://github.com/mkschulze/JamWide/releases/tag/v1.1-beta.20.7" class="btn btn-primary btn-large">
    Download v1.1-beta.20.7 (macOS Universal)
  </a>
  <p class="version-info">macOS Universal (Apple Silicon + Intel) — Windows + Linux next beta</p>
</div>

---

## Last Cross-Platform Beta — v1.1-beta.20.5

If you're on Windows or Linux, use this older beta until the next cross-platform tag lands. macOS users — prefer v1.1-beta.20.7 above; it has the new native video stack + variant-C freeze fix + Javier's encoder fixes.

<div class="download-section">
  <a href="https://github.com/mkschulze/JamWide/releases/tag/v1.1-beta.20.5" class="btn btn-large">
    Download v1.1-beta.20.5
  </a>
  <p class="version-info">macOS (Universal) / Windows (x64) / Linux (x64)</p>
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
