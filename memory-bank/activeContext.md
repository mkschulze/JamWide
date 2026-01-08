# Active Context - NINJAM CLAP Plugin

## Current Session Focus

**Date:** 2026-01-08  
**Phase:** 5 - Integration & Polish  
**Status:** ⚠️ Connection working; crashes when joining servers with existing users (under investigation).

## Latest Build: r67 (DEV BUILD)

### What's Working
- ✅ Plugin loads in REAPER and Bitwig
- ✅ Connection to public NINJAM servers (ninbot.com, ninjamer.com)
- ✅ Server browser fetches live server list
- ✅ License agreement dialog
- ✅ BPM/BPI/Beat display updates in real-time
- ✅ Remote users list shows active users without crash
- ⚠️ Crash possible on Disconnect (fix in progress)
- ✅ Dev/Production build toggle

### Recent Fixes (r45-r66)
| Issue | Fix |
|-------|-----|
| Race in NJClient access | Added `client_mutex` to serialize all NJClient API calls |
| License dialog deadlock | License callback releases `client_mutex` while waiting |
| Remote user UI crash | Removed snapshot path; UI reads NJClient directly under `client_mutex` |
| Disconnect crash | Lock `m_users_cs` + `m_remotechannel_rd_mutex` in `Disconnect()`, pre-clear cached_status |
| Server list URL | Default set to `http://ninbot.com/serverlist` |
| Verbose log spam | Added NLOG_VERBOSE for dev builds only |

## Build System

```bash
# Dev build (verbose logging) - DEFAULT
cmake .. -DNINJAM_CLAP_DEV_BUILD=ON

# Production build (minimal logging)
cmake .. -DNINJAM_CLAP_DEV_BUILD=OFF

# Quick install
./install.sh
```

### Logging Macros
| Macro | Description |
|-------|-------------|
| `NLOG(...)` | Always logs (errors, status changes) |
| `NLOG_VERBOSE(...)` | Only in dev builds (per-frame debug) |

## Recent Major Changes (r36-r54)

### Threading Architecture Overhaul
Reworked the threading/ownership model to align with ReaNINJAM:

1. **Command Queue Pattern** - UI sends commands, run thread executes
2. **Client Mutex** - `client_mutex` serializes all NJClient API calls (except `AudioProc`)
3. **ReaNINJAM-style UI Reads** - UI reads remote users directly from NJClient under `client_mutex`
4. **Server List Fetcher** - Async HTTP via JNetLib with JSON parsing

### New Files Added
| File | Purpose |
|------|---------|
| `src/threading/ui_command.h` | Command variants for UI→Run thread |
| `src/net/server_list.h/cpp` | Async server list fetcher (JNetLib + jsonparse) |
| `src/ui/server_list_types.h` | ServerListEntry struct |
| `src/ui/ui_server_browser.h/cpp` | Server browser panel |
| `src/debug/logging.h` | NLOG/NLOG_VERBOSE macros |

### Key Architectural Changes
| Before | After |
|--------|-------|
| UI called NJClient methods directly | UI sends UiCommand to cmd_queue |
| UI iterated `remote_users` unsafely | UI reads NJClient getters under `client_mutex` |
| Raw pointer to plugin in run thread | `shared_ptr` keepalive |
| No public server list | ServerListFetcher with HTTP GET |
| NJClient accessed under state_mutex | NJClient accessed under client_mutex |

## Debugging Journey Summary

The plugin was crashing after successful connection. Root causes identified:

1. **Race condition**: Remote user access during `Run()` updates
2. **Lifetime issue**: Plugin destroyed while run thread still held raw pointer
3. **Snapshot path crash**: Removed snapshot conversion in favor of direct NJClient reads

Addressed via architectural refactor; remaining crashes appear tied to multi-user servers.

## Priority Actions for Next Session

1. **End-to-end audio test** - Connect when other musicians are online
2. **Test audio transmit/receive** - Verify encoding/decoding works
3. **Test metronome** - Verify sync with server BPM/BPI
4. **State persistence test** - Save project, reload, verify settings

## Key Files

| File | Purpose |
|------|---------|
| `src/threading/run_thread.cpp` | Main network thread - processes commands, calls Run(), updates UI snapshot |
| `src/threading/ui_command.h` | UiCommand variant types (UI→Run thread) |
| `src/ui/ui_remote.cpp` | Remote users panel reads NJClient under `client_mutex` |
| `src/net/server_list.cpp` | HTTP server list fetcher |
| `src/ui/ui_state.h` | Default server list URL |
| `src/debug/logging.h` | Logging macros |
| `CMakeLists.txt` | NINJAM_CLAP_DEV_BUILD option |

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
