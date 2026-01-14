# JamWide - Implementation Progress

**Project Start:** January 2026  
**Current Build:** r135 (v0.135)
**Status:** ✅ Beta - Ready for user testing, approaching 1.0
**Target Platforms:** Windows 10+ (MSVC), macOS 10.15+ (Xcode/Clap)  
**Plugin Formats:** CLAP, VST3, Audio Unit v2 (via clap-wrapper)  
**UI Framework:** Dear ImGui (Metal on macOS, D3D11 on Windows)  
**Language:** C++20 (std::variant/std::optional + designated initializers)  
**CI/CD:** GitHub Actions (Windows + macOS builds on tag push)  
**Windows Build:** Visual Studio 2022+ (MSVC 19.30+), MSBuild-based, PowerShell install script, ✅ Keyboard focus fixed  
**macOS Build:** ✅ REAPER keyboard hint, ✅ Chat focus retention, ✅ Solo crash fixed

---

## Release History

| Version | Date | Highlights |
|---------|------|------------|
| v0.135 | 2026-01-14 | Fix: Windows text input (simplified message hook) |
| v0.133 | 2026-01-14 | Fix: Solo crash (mutex deadlock), Windows keyboard duplication |
| v0.132 | 2026-01-13 | macOS/REAPER keyboard hint, chat focus retention, UI polish |
| v0.119 | 2026-01-12 | Windows: Keyboard focus fix (message hook) |
| v0.117 | 2026-01-12 | Windows: Dummy EDIT control + IME support |
| v0.108 | 2026-01-12 | UI: Transmit toggle visible |
| v0.107 | 2026-01-11 | Fix: License dialog single-click |
| v0.106 | 2026-01-11 | Default 256 kbps audio quality |
| v0.105 | 2026-01-11 | Server browser with usernames |

---

## Phase 0: Project Setup ✅

| Task | Status | Notes |
|------|--------|-------|
| Create `JamWide/` directory structure | ✅ | `/Users/cell/dev/JamWide/` |
| Initialize git repository | ✅ | |
| Add CLAP SDK submodule (`libs/clap`) | ✅ | v1.2.7 |
| Add clap-helpers submodule (`libs/clap-helpers`) | ✅ | |
| Add Dear ImGui submodule (`libs/imgui`) | ✅ | |
| Add libogg submodule (`libs/libogg`) | ✅ | v1.3.6 |
| Add libvorbis submodule (`libs/libvorbis`) | ✅ | v1.3.7 |
| Copy WDL dependencies to `wdl/` | ✅ | jnetlib/, sha, queue, heapbuf, mutex, ptrlist, etc. |
| Create root CMakeLists.txt | ✅ | C++20; ObjC/ObjCXX enabled only on macOS |
| Create cmake/ClapPlugin.cmake | ⬜ | Not needed for MVP |
| Create resources/Info.plist.in | ✅ | macOS bundle |
| Verify empty build on Windows | ✅ | VS 2022/2026, MSBuild successful |
| Verify empty build on macOS | ✅ | `_clap_entry` exported, bundle verified |

**Deliverable:** ✅ CLAP bundle builds, exports correct entry point

---

## Phase 1: Core NJClient Port ✅

| Task | Status | Notes |
|------|--------|-------|
| Copy njclient.h/cpp to `src/core/` | ✅ | From `/Users/cell/dev/ninjam/ninjam/` |
| Copy netmsg.h/cpp to `src/core/` | ✅ | |
| Copy mpb.h/cpp to `src/core/` | ✅ | |
| Copy njmisc.h/cpp to `src/core/` | ✅ | |
| Add atomic config fields to njclient.h | ✅ | master/metronome vol/pan/mute + metronome_channel + play_prebuffer |
| Add `cached_status` atomic | ✅ | Updated in Connect/Disconnect/Run (incl. early returns) |
| Remove REAPER Vorbis callback indirection | ✅ | Not needed - REANINJAM not defined, uses direct VorbisEncoder/Decoder |
| Implement `SpscRing<T, N>` in `src/threading/spsc_ring.h` | ✅ | Lock-free SPSC queue, API aligned with plan |
| Define `UiEvent` variant types | ✅ | ChatMessage, StatusChanged, UserInfoChanged, TopicChanged |
| Implement run thread wrapper | ✅ | `run_thread.h/cpp` - callbacks wired, adaptive sleep |
| Add UI atomic snapshot struct | ✅ | `UiAtomicSnapshot` in `src/ui/ui_state.h` |
| Create `UiState` struct | ✅ | `src/ui/ui_state.h` - connection, local channel, remote users, license |
| Update run thread to refresh UI snapshot | ✅ | BPM/BPI/position/beat updated in run loop |
| Implement chat callback | ✅ | Pushes ChatMessageEvent to ui_queue |
| Implement license callback | ✅ | Blocking wait with cv, 60s timeout |
| Unit test: connect to public server | ⬜ | Deferred - requires CLAP wrapper first |

**Deliverable:** ✅ NJClient core compiles, threading infrastructure in place, build verified

---

## Phase 2: CLAP Wrapper ✅

| Task | Status | Notes |
|------|--------|-------|
| Create `src/plugin/jamwide_plugin.h` | ✅ | Plugin instance struct (Part 1 Section 5) |
| Implement clap_entry.cpp | ✅ | Factory, descriptor (Part 2 Section 1) |
| Implement plugin lifecycle | ✅ | init, destroy, activate, deactivate, start/stop_processing (Part 2 Section 2) |
| Implement audio ports extension | ✅ | Stereo I/O (Part 2 Section 3) |
| Implement process() | ✅ | AudioProc() call, pass-through when disconnected (Part 2 Section 4) |
| Implement params extension | ✅ | 4 params: master vol/mute, metro vol/mute (Part 2 Section 5) |
| Implement state extension | ✅ | JSON save/load via picojson, no password (Part 2 Section 6) |
| Add picojson.h to `src/third_party/` | ✅ | Single-header JSON parser downloaded |
| Test with clap-validator | ⬜ | Deferred |

**Deliverable:** ✅ Plugin loads, processes audio, saves/restores state

---

## Phase 3: Platform GUI ✅

| Task | Status | Notes |
|------|--------|-------|
| Create `src/platform/gui_context.h` | ✅ | Abstract interface (Part 3 Section 1) |
| Implement `gui_win32.cpp` | ✅ | Win32 + D3D11 + ImGui (Part 3 Section 1.1) |
| Implement `gui_macos.mm` | ✅ | Cocoa + Metal + ImGui (Part 3 Section 1.2) |
| Implement CLAP GUI extension | ✅ | Added to clap_entry.cpp (Part 2 Section 7) |
| Create `src/ui/ui_main.cpp` | ✅ | Basic UI with status, connection, master, local panels |
| Build label in status bar | ✅ | rN shown top-right (build_number.h) |
| Per-instance ImGui contexts | ✅ | Multi-instance safety (code review fix) |
| Run thread UI events | ✅ | Emits status/user/topic events (code review fix) |
| GUI teardown in plugin_destroy | ✅ | Safety net if host skips gui_destroy (code review fix) |
| macOS responder chain fix | ✅ | ImGui NSTextInputClient becomes first responder |
| Windows keyboard focus fix | ✅ | WH_GETMESSAGE hook + dummy EDIT + IME support (r117-r119) |
| Test build on macOS | ✅ | Build verified (x86_64 bundle, ~2.8MB) |
| Test build on Windows | ✅ | Build verified (VS 2022/2026, CLAP + VST3) |
| Test loads in Bitwig | ✅ | Loads from user path (macOS + Windows) |
| Mouse/keyboard input working | ✅ | Text input + clicks verified (macOS + Windows) |
| Test in REAPER | ✅ | CLAP/VST3 verified (macOS + Windows) |

**Deliverable:** ✅ ImGui GUI framework complete

---

## Phase 4: UI Panels 🔄

| Task | Status | Notes |
|------|--------|-------|
| Create `src/ui/ui_state.h` | ✅ | Already created in Phase 1 |
| Implement ui_main.cpp | ✅ | Done in Phase 3 with basic panels |
| Implement ui_status.cpp | ✅ | Connection dot, BPM/BPI, beat progress bar |
| Implement ui_connection.cpp | ✅ | Server/user/pass inputs, connect/disconnect |
| Implement ui_local.cpp | ✅ | Name, bitrate, transmit, vol/pan/mute/solo |
| Implement ui_master.cpp | ✅ | Master + metronome controls |
| Implement ui_remote.cpp | ✅ | Remote users tree, per-channel controls |
| Implement ui_license.cpp | ✅ | Modal dialog accept/reject (still in ui_main.cpp) |
| Implement ui_meters.cpp | ✅ | VU meter widget (green/yellow/red) |
| Wire VU snapshot updates | ✅ | Audio thread updates UiAtomicSnapshot |

**Deliverable:** Full UI functional

---

## Phase 5: Integration & Polish 🔄

| Task | Status | Notes |
|------|--------|-------|
| Command queue architecture | ✅ | UI sends UiCommand, run thread executes |
| ReaNINJAM-aligned client mutex | ✅ | Serialize all NJClient API calls except AudioProc |
| Remote users (ReaNINJAM-style) | ✅ | UI reads NJClient under client_mutex; snapshot path removed |
| Server list fetcher | ✅ | JNetLib HTTP + jsonparse |
| Server browser UI | ✅ | New panel with refresh button |
| Shared_ptr plugin keepalive | ✅ | Prevents use-after-free in run thread |
| License callback unlock | ✅ | Release client_mutex while waiting on UI |
| End-to-end test: connect, transmit, receive | ⬜ | Use public NINJAM server |
| Verify multi-instance works | ⬜ | No globals except read-only descriptor |
| State persistence test | ⬜ | Save project, reload, verify settings |
| Parameter automation test | ⬜ | Automate master volume in DAW |
| Memory leak check (Windows) | ⬜ | Visual Studio diagnostics |
| Memory leak check (macOS) | ⬜ | Instruments/Leaks |
| Windows build system setup | ✅ | install-win.ps1 created, VS 2022/2026 tested |
| Test in REAPER (Win) | 🔄 | Ready for testing |
| Test in REAPER (macOS) | ⬜ | |
| Test in Bitwig (Win) | 🔄 | Ready for testing |
| Test in Bitwig (macOS) | ⬜ | |

**Deliverable:** Release candidate

---

## Current Status

| Item | Value |
|------|-------|
| **Current Phase** | Phase 5 - Integration & Polish |
| **Latest Build** | r90 (2026-01-10) DEV BUILD |
| **GitHub** | https://github.com/mkschulze/JamWide |
| **Latest Tag** | v0.90-chat |

### Recent Features (r85-r90)
- ✅ Visual timing guide with beat grid and transient dots
- ✅ Chat room with message history, timestamps, and input field
- ✅ Anonymous login fix (auto-prefix "anonymous:" for public servers)
- ✅ ImGui ID collision fixes throughout UI
- ✅ Release automation script (release.sh)
| **Build Number** | r54+ (auto-incremented via install.sh) |
| **Blockers** | Crash when joining servers with existing users |
| **Next Action** | Investigate crash on multi-user servers |

### Architecture Status

Major refactor complete (r36) + confirmed working (r41+):
- ✅ Command queue pattern: UI→Run thread communication
- ✅ Snapshot pattern: Safe remote user data access
- ✅ Shared_ptr keepalive: Plugin lifetime safety
- ✅ Server list fetcher: Async HTTP via JNetLib (default URL: http://ninbot.com/serverlist)
- ✅ Dev/Production build system with JAMWIDE_DEV_BUILD option
- ⚠️ Connection stable on empty servers; crash when users are present

---

## Immediate Next Steps

### End-to-End Audio Testing
- Connect to server with other musicians
- Verify audio transmit/receive works
- Test metronome sync
- Test local channel monitoring

### Polish
- Test state save/load
- Test multi-instance in DAW
- Memory leak check

---

## Session Log

| Date | Session | Progress |
|------|---------|----------|
| 2026-01-06 | Planning | ✅ Completed functional design |
| | | ✅ Completed threading/sync plan |
| | | ✅ Completed technical design (3 parts) |
| | | ✅ Created progress.md |
| 2026-01-06 | Phase 0 | ✅ Created project at `/Users/cell/dev/JamWide/` |
| | | ✅ Added all submodules (clap, clap-helpers, imgui, libogg, libvorbis) |
| | | ✅ Copied WDL files |
| | | ✅ Created CMakeLists.txt, Info.plist.in |
| | | ✅ Created stub clap_entry.cpp |
| | | ✅ Build verified on macOS (x86_64 bundle, clap_entry exported) |
| 2026-01-06 | Phase 1 | ✅ Copied NJClient core files to src/core/ |
| | | ✅ Added atomic config fields (master/metro vol/pan/mute, prebuffer, metronome_channel) |
| | | ✅ Added cached_status atomic with updates in Connect/Disconnect/Run |
| | | ✅ Created SpscRing<T,N> lock-free queue |
| | | ✅ Created UiEvent variant types |
| | | ✅ Created run_thread.h/cpp with adaptive sleep |
| | | ✅ Created jamwide_plugin.h struct |
| 2026-01-07 | Phase 1 Review | ✅ Code review by senior developer |
| | | ✅ Created ui_state.h with UiState + UiAtomicSnapshot |
| | | ✅ Implemented chat_callback and license_callback |
| | | ✅ Added UI snapshot refresh in run loop |
| | | ✅ Fixed atomic metronome channel read |
| | | ✅ Build verified on macOS |
| 2026-01-07 | Phase 2 | ✅ Rewrote clap_entry.cpp with full plugin lifecycle |
| | | ✅ Implemented audio ports extension (stereo I/O) |
| | | ✅ Implemented process() with AudioProc call and pass-through |
| | | ✅ Implemented params extension (4 params with dB display) |
| | | ✅ Implemented state extension (JSON save/load with picojson) |
| | | ✅ Downloaded picojson.h to src/third_party/ |
| | | ✅ Fixed namespace conflict in run_thread.h |
| | | ✅ Build verified on macOS |
| 2026-01-07 | Phase 2 Review | ✅ Code review by senior developer |
| | | ✅ Transport default changed to "not playing" |
| | | ✅ Added null guard for data32 buffers (returns CLAP_PROCESS_ERROR) |
| | | ✅ State save: snapshot UI data under mutex before serialization |
| | | ✅ State save: handle partial writes in loop |
| | | ✅ State load: parse first, apply under mutex atomically |
| | | ✅ Build verified on macOS |
| 2026-01-07 | Phase 3 | ✅ Created gui_context.h abstract interface |
| | | ✅ Implemented gui_macos.mm (Cocoa + Metal + ImGui) |
| | | ✅ Implemented gui_win32.cpp (Win32 + D3D11 + ImGui) |
| | | ✅ Added CLAP GUI extension to clap_entry.cpp |
| | | ✅ Created ui_main.h/cpp with functional UI panels |
| | | ✅ Updated CMakeLists.txt for GUI sources |
| | | ✅ Fixed ImGui OSX backend API (removed obsolete HandleEvent calls) |
| | | ✅ Build verified on macOS (x86_64 bundle, ~2MB) |
| 2026-01-07 | Phase 3 Review | ✅ Code review by senior developer |
| | | ✅ Per-instance ImGui contexts (multi-instance safety) |
| | | ✅ Run thread emits status/user/topic events |
| | | ✅ GUI teardown in plugin_destroy |
| | | ✅ Added server_topic field to UiState |
| | | ✅ Plugin tested - loads in Bitwig Studio |
| 2026-01-07 | Release | ✅ Tagged v0.1.0 |
| | | ✅ Pushed to GitHub (commit 82c0dac) |
| | | ✅ Added comprehensive README.md |
| 2026-01-07 | Debugging | 🔄 Plugin loads in Bitwig but input not working |
| | | ✅ Fixed run thread mutex contention (release between Run() calls) |
| 2026-01-08 | Debugging | ✅ Added client_mutex to serialize NJClient access |
| | | ✅ License callback releases client_mutex while waiting |
| | | ✅ Added explicit mouse/keyboard event handlers to gui_macos.mm |
| | | ✅ Added MacKeyCodeToImGuiKey() for keyboard input |
| | | ✅ Added tracking areas for mouse move events |
| | | ✅ Added build number display (r1, r2, etc.) |
| | | ✅ Created install.sh for easy rebuild/install |
| | | ✅ clap-validator passes all tests |
| | | ❌ Plugin sometimes doesn't appear in Bitwig after reinstall |
| | | ❌ Mouse clicks and keyboard input still not reaching ImGui |
| 2026-01-07 | Connection Debug | 🔄 Crash investigation after successful connection |
| | | ✅ Identified race condition in remote user access |
| | | ✅ Added mutex locking to NJClient accessor methods |
| | | ✅ Found dual-mutex bug (m_users_cs vs m_remotechannel_rd_mutex) |
| | | ✅ Added extensive logging throughout run thread |
| | | ✅ Identified use-after-free of plugin object |
| 2026-01-08 | Architecture | ✅ Major threading refactor by senior developer |
| | | ✅ Added UiCommand queue (UI→Run thread) |
| | | ✅ Added shared_ptr keepalive for run thread |
| | | ✅ Implemented ServerListFetcher (JNetLib HTTP) |
| | | ✅ Added server browser UI panel |
| | | ✅ UI reads NJClient getters under client_mutex; mutations via cmd_queue |
| | | ✅ Build r36 ready for testing |
| 2026-01-08 | Architecture | ✅ ReaNINJAM-style remote UI |
| | | ✅ Removed GetRemoteUsersSnapshot() usage in UI |
| | | ✅ UI enumerates users/channels directly under client_mutex |
| 2026-01-08 | Debugging | 🔄 Disconnect crash in Bitwig |
| | | ✅ Pre-clear cached_status before Disconnect |
| | | ✅ Lock m_users_cs + m_remotechannel_rd_mutex during Disconnect cleanup |
| 2026-01-08 | Testing | ✅ Server list URL set to ninbot.com/serverlist |
| | | ✅ Server browser fetches from ninbot.com/serverlist |
| | | ✅ Successfully connected to ninjamer.com:2050 |
| | | ✅ BPM/BPI/Beat display working (120 BPM, 32 BPI) |
| | | ✅ No crash on connection - race condition fixed! |
| | | ✅ Created debug/logging.h with NLOG/NLOG_VERBOSE macros |
| | | ✅ Added JAMWIDE_DEV_BUILD cmake option |
| | | ✅ Build r44 (DEV BUILD) verified stable |

---

## Decisions Made

| Decision | Rationale | Reference |
|----------|-----------|-----------|
| Port NJClient vs rewrite | Core is battle-tested, 99% portable | Initial analysis |
| Single stereo I/O (MVP) | Simplifies audio routing, covers most use cases | Functional Design F04 |
| No chat in MVP | Reduces scope, can add later | Functional Design F18 |
| Dear ImGui for UI | Cross-platform, immediate mode, simple | Functional Design 3.2 |
| Metal + D3D11 backends | Native GPU, best performance | Part 3 Section 1 |
| Password in-memory only | Security - never saved to disk | Plan Section 3 |
| Command queue for UI→Run | Eliminates NJClient race conditions | r36 refactor |
| Snapshot for remote users | Atomic copy prevents iterator invalidation | r36 refactor |
| shared_ptr keepalive | Prevents plugin use-after-free | r36 refactor |
| Atomic config fields | Lock-free audio thread access | Plan Section 2 |
| Dedicated license slot | Guaranteed delivery vs queue | Plan Section 4 |
| Run thread always ticks | Handle connection state transitions | Plan Section 1 |
| C++20 required | std::variant, std::optional, designated initializers | Plan Overview |

---

## Reference Documents

| Document | Purpose |
|----------|---------|
| [Functional Design](functional-design-clapNinjam.md) | Features, requirements, architecture overview |
| [Threading & Sync Plan](plan-clapNinjamThreadingSync.md) | Thread roles, atomics, callbacks, license handling |
| [Technical Design Part 1](technical-design-part1-core.md) | Project structure, CMake, JamWidePlugin struct, SpscRing |
| [Technical Design Part 2](technical-design-part2-clap.md) | CLAP entry, lifecycle, audio, params, state, GUI ext |
| [Technical Design Part 3](technical-design-part3-ui.md) | Platform GUI layers, ImGui panels, VU meters |

---

## Legend

- ⬜ Not started
- 🔄 In progress
- ✅ Completed
- ❌ Blocked
