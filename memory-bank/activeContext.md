# Active Context - NINJAM CLAP Plugin

## Current Session Focus

**Date:** 2026-01-10  
**Phase:** 5 - Integration & Polish  
**Status:** ✅ Stable - Chat, Timing Guide, and Anonymous Login working

## Latest Build: r90 (DEV BUILD)

### What's Working
- ✅ Plugin loads in REAPER and Bitwig
- ✅ Connection to public NINJAM servers (ninbot.com, ninjamer.com)
- ✅ Server browser fetches live server list
- ✅ License agreement dialog
- ✅ BPM/BPI/Beat display updates in real-time
- ✅ Remote users list shows active users
- ✅ Chat room with message history and timestamps
- ✅ Visual timing guide with beat grid and transient dots
- ✅ Anonymous login (auto-prefix for public servers)
- ✅ Dev/Production build toggle

### Recent Fixes (r85-r90)
| Issue | Fix |
|-------|-----|
| Anonymous login rejected | Auto-prefix "anonymous:" when password empty |
| Timing guide no dots | Move transient detection before AudioProc |
| ImGui ID collisions | Add ##suffix pattern and PushID wrappers |
| Build errors | Fix namespace issues (ninjam::SendChatCommand) |
| Variable ordering | Fix label_col used before declaration |

## Build System

```bash
# Dev build (verbose logging) - DEFAULT
cmake .. -DNINJAM_CLAP_DEV_BUILD=ON

# Production build (minimal logging)
cmake .. -DNINJAM_CLAP_DEV_BUILD=OFF

# Quick install
./install.sh

# Full release workflow (commit, build, push, tag)
./release.sh
```

### Logging Macros
| Macro | Description |
|-------|-------------|
| `NLOG(...)` | Always logs (errors, status changes) |
| `NLOG_VERBOSE(...)` | Only in dev builds (per-frame debug) |

## New Files Added (r85-r90)

| File | Purpose |
|------|---------|
| `src/ui/ui_chat.cpp/h` | Chat room UI with message history |
| `src/ui/ui_latency_guide.cpp/h` | Visual timing guide with beat grid |
| `memory-bank/plan-chat-room.md` | Chat implementation plan |
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
