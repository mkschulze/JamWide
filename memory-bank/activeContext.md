# Active Context - NINJAM CLAP Plugin

## Current Session Focus

**Date:** 2026-01-10  
**Phase:** 5 - Integration & Polish  
**Status:** ✅ Multi-format build working (CLAP, VST3, AU v2)

## Latest Build: r92+ (DEV BUILD)

### What's Working
- ✅ Plugin loads in REAPER and Bitwig
- ✅ **Multi-format builds**: CLAP, VST3, Audio Unit v2 (macOS)
- ✅ Connection to public NINJAM servers (ninbot.com, ninjamer.com)
- ✅ Server browser fetches live server list
- ✅ License agreement dialog
- ✅ BPM/BPI/Beat display updates in real-time
- ✅ Remote users list shows active users
- ✅ Chat room with message history and timestamps
- ✅ Visual timing guide with beat grid and transient dots
- ✅ Anonymous login (auto-prefix for public servers)
- ✅ Dev/Production build toggle

### Recent Changes (r90-r92)
| Change | Details |
|--------|--------|
| Multi-format builds | Integrated clap-wrapper for VST3 + AU v2 |
| CLAP-first pattern | Split into static impl library + entry export |
| AUv2 codes | Manufacturer: JMWD, Subtype: JWAU, Type: aufx |
| Bundle identifier | com.jamwide.client |

### Previous Fixes (r85-r90)
| Issue | Fix |
|-------|-----|
| Anonymous login rejected | Auto-prefix "anonymous:" when password empty |
| Timing guide no dots | Move transient detection before AudioProc |
| ImGui ID collisions | Add ##suffix pattern and PushID wrappers |

## Build System

```bash
# Configure with auto-download of VST3/AU SDKs
cmake .. -DCMAKE_BUILD_TYPE=Release -DCLAP_WRAPPER_DOWNLOAD_DEPENDENCIES=TRUE

# Build all formats
cmake --build . --config Release

# Output:
# - NINJAM.clap (CLAP)
# - NINJAM.vst3 (VST3)
# - NINJAM.component (AU v2, macOS only)

# Install locations (macOS):
# ~/Library/Audio/Plug-Ins/CLAP/
# ~/Library/Audio/Plug-Ins/VST3/
# ~/Library/Audio/Plug-Ins/Components/
```

### Logging Macros
| Macro | Description |
|-------|-------------|
| `NLOG(...)` | Always logs (errors, status changes) |
| `NLOG_VERBOSE(...)` | Only in dev builds (per-frame debug) |

## New Files Added (r90-r92)

| File | Purpose |
|------|---------|
| `src/plugin/clap_entry_export.cpp` | CLAP entry export shim for clap-wrapper |
| `tools/check_imgui_ids.py` | ImGui ID hygiene checker script |

## Key Architecture (clap-wrapper)

| Component | Description |
|-----------|-------------|
| `ninjam-impl` | Static library with all plugin code |
| `clap_entry_export.cpp` | Tiny export file recompiled per format |
| `make_clapfirst_plugins()` | CMake function that generates all formats |
| `memory-bank/plan-visual-latency-guide.md` | Timing guide implementation plan |
| `release.sh` | Automated release script |

## Key Architectural Features

| Feature | Description |
|---------|-------------|
| **Command Queue** | UI sends UiCommand to run thread via cmd_queue |
| **Chat Queue** | Run thread pushes ChatMessage to UI via chat_queue |
| **Transient Detection** | Audio thread detects peaks for timing guide |
| **Anonymous Login** | Auto-prefix "anonymous:" for public server compatibility |

## Priority Actions for Next Session

1. **Timing guide polish** - Add tooltips, reset button, color-coded dots per offset
2. **End-to-end audio test** - Connect when other musicians are online
3. **Test audio transmit/receive** - Verify encoding/decoding works
4. **State persistence test** - Save project, reload, verify settings

## Key Files

| File | Purpose |
|------|---------|
| `src/threading/run_thread.cpp` | Main network thread - processes commands, handles chat, anonymous login |
| `src/threading/ui_command.h` | UiCommand variant types (UI→Run thread) |
| `src/ui/ui_chat.cpp` | Chat room panel with message history |
| `src/ui/ui_latency_guide.cpp` | Visual timing guide with beat grid |
| `src/ui/ui_remote.cpp` | Remote users panel reads NJClient under `client_mutex` |
| `src/plugin/clap_entry.cpp` | Audio processing with transient detection |
| `src/debug/logging.h` | Logging macros |
| `release.sh` | Automated release workflow |

## Build Commands

```bash
# Build and install (increments build number)
./install.sh

# Current build: r67 (DEV BUILD)
# Installs to: ~/Library/Audio/Plug-Ins/CLAP/NINJAM.clap
```

## Debug Logging

```bash
# Watch live log
tail -f /tmp/ninjam-clap.log

# Clear log before test
: > /tmp/ninjam-clap.log
```

## Test Server

```bash
# Public server
ninbot.com:2049  user: anonymous

# Local test server
/Users/cell/dev/ninjam/ninjam/server/ninjamsrv /tmp/ninjam-test.cfg
```
