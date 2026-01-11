---
layout: default
title: Download
---

# Download JamWide

Get the latest version of JamWide for your platform.

> ⚠️ **Early Development Warning**  
> JamWide is still under active development and not yet ready for production use. Expect bugs, missing features, and breaking changes. Use at your own risk!

---

## Latest Release

<div class="download-section">
  <a href="https://github.com/mkschulze/JamWide/releases/latest" class="btn btn-primary btn-large">
    Download Latest Release
  </a>
  <p class="version-info">Check the releases page for the latest version</p>
</div>

---

## Available Formats

### macOS

| Format | File | Install Location |
|--------|------|------------------|
| CLAP | `JamWide.clap` | `~/Library/Audio/Plug-Ins/CLAP/` |
| VST3 | `JamWide.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| Audio Unit | `JamWide.component` | `~/Library/Audio/Plug-Ins/Components/` |

### Windows

| Format | File | Install Location |
|--------|------|------------------|
| CLAP | `JamWide.clap` | `%LOCALAPPDATA%\Programs\Common\CLAP\` |
| VST3 | `JamWide.vst3` | `%LOCALAPPDATA%\Programs\Common\VST3\` |

---

## Installation

### macOS

1. Download the `.zip` file for your desired format
2. Extract the plugin file
3. Copy to the appropriate folder (see table above)
4. Restart your DAW
5. Scan for new plugins if necessary

### Windows

1. Download the `.zip` file for your desired format
2. Extract the plugin file
3. Copy to the appropriate folder (see table above)
4. Restart your DAW
5. Scan for new plugins if necessary

---

## System Requirements

### macOS
- macOS 10.15 (Catalina) or later
- Intel or Apple Silicon
- 64-bit DAW with CLAP, VST3, or AU support

### Windows
- Windows 10 or later
- 64-bit DAW with CLAP or VST3 support

---

## Build from Source

Prefer to compile yourself? See the [documentation](/documentation#building-from-source) for build instructions.

---

## Previous Releases

All releases are available on the [GitHub Releases page](https://github.com/mkschulze/JamWide/releases).

---

## Verification

Each release includes SHA256 checksums. Verify your download:

```bash
# macOS/Linux
shasum -a 256 JamWide-*.zip

# Windows (PowerShell)
Get-FileHash JamWide-*.zip -Algorithm SHA256
```
