# Codebase Structure

**Analysis Date:** 2026-04-30

## Directory Layout

```
JamWide/
├── CMakeLists.txt              # Top-level build; defines njclient lib + JamWideJuce target
├── README.md
├── CHANGELOG.md
├── LICENSE                     # GPLv2+ (inherited from NINJAM/Cockos)
├── JamWide.entitlements        # macOS hardened-runtime entitlements (microphone, JIT)
├── install.sh / install-win.ps1 / release.sh
├── JamWide.code-workspace      # VS Code workspace config
│
├── src/                        # JamWide-specific C++ source (the "core" lib + dead code)
│   ├── build_number.h          # Auto-incremented by cmake/increment-build-revision.cmake
│   ├── core/                   # NJClient + NINJAM protocol (linked into njclient lib)
│   ├── crypto/                 # AES-256-CBC payload crypto (Phase 15)
│   ├── net/                    # Public-server-list HTTP fetcher (used by JUCE target)
│   ├── threading/              # SPSC primitives + payload variants + run-thread shell
│   ├── ui/                     # ImGui UI (DEAD CODE — not in CMake build graph)
│   ├── plugin/                 # Pre-JUCE CLAP entry (DEAD CODE — not in CMake build graph)
│   ├── platform/               # Native window/HDPI helpers (DEAD CODE — used by `src/ui` only)
│   ├── debug/                  # logging.h
│   └── third_party/            # Vendored single-header utilities (e.g. picojson)
│
├── juce/                       # ACTIVE plugin shim (VST3, AU, Standalone, CLAP)
│   ├── JamWideJuceProcessor.{h,cpp}   # juce::AudioProcessor — owns NJClient, run thread, OSC, MIDI, video
│   ├── JamWideJuceEditor.{h,cpp}      # juce::AudioProcessorEditor — VB-style mixer UI
│   ├── NinjamRunThread.{h,cpp}        # juce::Thread — runs NJClient::Run + drains queues
│   ├── ui/                            # JUCE UI components (channel strip, fader, VU, beat bar, …)
│   ├── osc/                           # OSC server, address map, status indicator, config dialog
│   ├── midi/                          # MIDI mapper, MIDI Learn manager, status indicator, config dialog
│   └── video/                         # Embedded WS server + browser detection (.mm on macOS, .cpp on Win)
│
├── companion/                  # External web companion (Vite + TypeScript + Playwright)
│   ├── src/                    # main.ts, popout.ts, ui.ts, ws-client.ts, types.ts, __tests__/
│   ├── e2e/                    # Playwright end-to-end tests
│   ├── package.json / package-lock.json / tsconfig.json / vite.config.ts / vitest.config.ts / playwright.config.ts
│   ├── index.html / popout.html / style.css
│
├── wdl/                        # Vendored WDL audio utilities (Cockos)
│   ├── jnetlib/                # Async TCP, async DNS, HTTP GET (statically linked into wdl)
│   ├── vorbisencdec.h          # I_NJEncoder/I_NJDecoder via VorbisEncoder/Decoder
│   ├── flacencdec.h            # FlacEncoder/FlacDecoder (same interface, in development)
│   ├── lice/                   # Software graphics primitives (used by legacy ImGui path)
│   ├── sha.{h,cpp}             # SHA-1 (auth) — SHA-256 lives in nj_crypto via OpenSSL/BCrypt
│   ├── rng.{h,cpp}             # PRNG
│   ├── mutex.h, queue.h, ptrlist.h, heapbuf.h, wdlstring.h, ...     # Container/utility headers
│   ├── win32_utf8.{c,h}        # Windows UTF-8 wrappers (linked on WIN32)
│   ├── audiobuffercontainer.h, fastqueue.h, resample.h, fft.{c,h}    # Audio utilities
│   └── …                       # Many single-header WDL components (~70 files)
│
├── libs/                       # Third-party library subtrees (CMake add_subdirectory)
│   ├── juce/                   # JUCE 7 framework
│   ├── clap/                   # CLAP SDK headers
│   ├── clap-helpers/           # CLAP C++ helpers
│   ├── clap-juce-extensions/   # JUCE → CLAP wrapper plugin generator
│   ├── clap-wrapper/           # (companion to clap-juce-extensions)
│   ├── ixwebsocket/            # WebSocket server for video companion (per D-12)
│   ├── libflac/                # FLAC reference encoder/decoder
│   ├── libogg/                 # OGG container
│   ├── libvorbis/              # Vorbis codec
│   └── imgui/                  # ImGui (only used by dead src/ui — pulled by build for tests?)
│
├── cmake/                      # CMake helper scripts
│   ├── increment-build-revision.cmake
│   └── run-pluginval.cmake     # Pluginval VST3 validation harness
│
├── scripts/                    # Build / sign / dev scripts
│   ├── build.sh                # Local Ninja x86_64 build → build-juce/
│   ├── notarize.sh             # macOS notarization (Team ID T3KK66Q67T)
│   └── generate_tosc.py        # TouchOSC layout generator
│
├── tests/                      # Standalone unit tests (CTest, opt-in via JAMWIDE_BUILD_TESTS)
│   ├── test_encryption.cpp           # nj_crypto round-trip + known vectors
│   ├── test_flac_codec.cpp           # FlacEncoder/Decoder round-trip
│   ├── test_njclient_atomics.cpp     # 15.1-02 release/acquire BPM/BPI pattern
│   ├── test_spsc_state_updates.cpp   # 15.1-04 SPSC primitive + payload contract
│   ├── test_deferred_delete.cpp      # 15.1-05 deferred-delete burst stress
│   ├── test_local_channel_mirror.cpp # 15.1-06 mirror + HIGH-2 + HIGH-3
│   ├── test_remote_user_mirror.cpp   # 15.1-07a peer-churn + HIGH-2 + HIGH-3
│   ├── test_block_queue_spsc.cpp     # 15.1-07b BlockRecord SPSC fill/drain
│   ├── test_decode_media_buffer_spsc.cpp  # 15.1-07c DecodeChunk byte-stream SPSC
│   ├── test_decode_state_arming.cpp  # 15.1-09 DecodeState arming + HIGH-1
│   ├── test_decoder_prealloc.cpp     # 15.1-08 prealloc-hardening
│   ├── test_peer_churn_simulation.cpp # 15.1-10 NINJAM peer-churn simulation
│   ├── test_video_sync.cpp           # Video companion sync
│   ├── test_osc_loopback.cpp         # OSC round-trip (juce_console_app)
│   └── test_midi_mapping.cpp         # MidiMapper unit tests (juce_console_app)
│
├── assets/                     # JamWide.tosc TouchOSC layout
├── resources/                  # Info.plist.in (macOS plugin metadata template)
├── docs/                       # Project docs (download page, changelog, sketch designs)
├── memory-bank/                # Session-handoff notes for AI agents
├── .planning/                  # GSD command artifacts (planning, debug, codebase docs)
├── .claude/                    # Claude Code settings + project skills (bump-beta, install-vst3)
│
├── build/                      # Universal CMake build output (CI; ignored)
├── build-juce/                 # Local Ninja x86_64 build (ignored)
├── build-test/, build-tsan/    # CTest + ThreadSanitizer build dirs (ignored)
├── tools/                      # Misc scripts (check_imgui_ids.py)
└── IEMPluginSuite/             # Sibling repo checked out for reference (NOT built — out of scope)
```

## Directory Purposes

**`src/core/`:**
- Purpose: The NJClient + NINJAM protocol implementation. Compiled into the `njclient` static library.
- Contains: `njclient.{h,cpp}` (5300+ LOC, the core class), `netmsg.{h,cpp}` (TCP framing + payload encryption hook), `mpb.{h,cpp}` (NINJAM protocol message builders/parsers), `njmisc.{h,cpp}` (helpers), `njclient_stub.cpp` (test fallback).
- Key files: `njclient.cpp` (5296 lines), `njclient.h` (947 lines).

**`src/crypto/`:**
- Purpose: AES-256-CBC payload encryption + SHA-256 key derivation (Phase 15 security hardening).
- Contains: `nj_crypto.{h,cpp}`. Cross-platform: BCrypt on Windows, OpenSSL on macOS/Linux (selected at `CMakeLists.txt:94-107`).
- Linked into `njclient` static lib.

**`src/threading/`:**
- Purpose: Thread-safety primitives for the audio ↔ run ↔ UI handoff.
- Contains: `spsc_ring.h` (lock-free SPSC ring template), `spsc_payloads.h` (frozen payload variant types — `RemoteUserUpdate`, `LocalChannelUpdate`, `BlockRecord`, `DecodeChunk`, `DecodeArmRequest`), `ui_command.h` (UI→Run command variants), `ui_event.h` (Run→UI event variants), `run_thread.{h,cpp}` (legacy CLAP run-thread shell — not in CMake today).

**`src/net/`:**
- Purpose: Public-server-list HTTP polling. Used by JUCE target only (`CMakeLists.txt:191`).
- Contains: `server_list.{h,cpp}` — wraps `JNL_HTTPGet` from WDL, parses NINJAM list format and JSON.

**`src/ui/` (DEAD CODE):**
- Purpose: ImGui-based UI from the pre-JUCE prototype.
- Status: Not referenced by `CMakeLists.txt` — never compiled or linked into the active build. Kept as reference / for `JAMWIDE_BUILD_JUCE=OFF` future revival, but per memory `project_legacy_clap_flag.md` the toggle is vestigial post-commit `0d82641`.
- Files: `ui_main.{h,cpp}`, `ui_chat.{h,cpp}`, `ui_connection.{h,cpp}`, `ui_local.{h,cpp}`, `ui_master.{h,cpp}`, `ui_remote.{h,cpp}`, `ui_meters.{h,cpp}`, `ui_status.{h,cpp}`, `ui_server_browser.{h,cpp}`, `ui_util.{h,cpp}`, `ui_state.h`, `server_list_types.h`.
- Note: `ui_state.h` IS reused by the JUCE target via `#include "ui/ui_state.h"` for `UiAtomicSnapshot`, `UiRemoteUser`, `ChatMessage`, etc. — that single header is alive even though its `*.cpp` siblings are dead.

**`src/plugin/` (DEAD CODE):**
- Purpose: Original CLAP entry point + `JamWidePlugin` instance struct from before the JUCE port.
- Status: Not in CMake. Active CLAP build path is `clap-juce-extensions` invoked at `CMakeLists.txt:230-234`.
- Files: `clap_entry.cpp`, `clap_entry_export.cpp`, `jamwide_plugin.h`.

**`src/platform/` (DEAD CODE):**
- Purpose: Win32 / macOS native window + HiDPI helpers for the dead ImGui UI.
- Status: Referenced only from `src/ui/`. Not in CMake.
- Files: `gui_context.h`, `gui_macos.mm`, `gui_win32.cpp`.

**`src/debug/`:**
- Purpose: Macros / functions for `JAMWIDE_DEV_BUILD` log output.
- Contains: `logging.h` (header-only).

**`juce/`:**
- Purpose: ACTIVE plugin shim. Every binary the user installs (VST3, AU, Standalone, CLAP) is built from this single `JamWideJuce` CMake target (`CMakeLists.txt:145-234`).
- Subdirs:
  - `juce/ui/` — JUCE Component subclasses for the channel strips, faders, VU meters, chat panel, beat bar, server browser overlay, license dialog, custom `LookAndFeel`.
  - `juce/osc/` — `OscServer` (built on `juce::juce_osc`), address-map dispatcher, status indicator, config dialog.
  - `juce/midi/` — `MidiMapper`, `MidiLearnManager`, status indicator, config dialog, `MidiTypes.h`.
  - `juce/video/` — `VideoCompanion` (embedded WebSocket server, links `ixwebsocket`), `VideoPrivacyDialog`, `BrowserDetect_mac.mm` / `BrowserDetect_win.cpp`.

**`companion/`:**
- Purpose: External video sidecar — a Vite + TypeScript single-page app that the plugin opens in the user's browser. Connects via WebSocket to the embedded server in `juce/video/VideoCompanion`.
- Contains: TS source (`src/main.ts`, `src/ui.ts`, `src/ws-client.ts`, `src/popout.ts`, `src/types.ts`), Vitest unit tests (`src/__tests__/`), Playwright e2e tests (`e2e/`).
- Build: `npm install && npm run build` — output not bundled into the plugin; users open the served / static URL.

**`wdl/`:**
- Purpose: Cockos WDL audio utility library (vendored). Provides JNetLib (async TCP/DNS/HTTP), Vorbis & FLAC encoder/decoder wrappers, audio buffer containers, mutexes/queues/strings, SHA-1, RNG, FFT, resample, wave I/O.
- Status: Single static lib (`wdl`) at `CMakeLists.txt:75-89`. `wdl/jnetlib/*.cpp` are explicitly listed; the rest are header-only.

**`libs/`:**
- Purpose: Third-party library subtrees, each pulled in via `add_subdirectory`. JUCE, libogg/libvorbis/libFLAC (codec), CLAP SDK + helpers + JUCE extension wrapper, ixwebsocket (video companion), ImGui (only used by dead `src/ui/`).

**`cmake/`:**
- Purpose: CMake helper scripts.
- Contains: `increment-build-revision.cmake` (writes `src/build_number.h`), `run-pluginval.cmake` (downloads + invokes Pluginval against the built VST3).

**`scripts/`:**
- Purpose: Build / packaging / signing scripts.
- Files: `build.sh` (local Ninja x86_64 build → `build-juce/`), `notarize.sh` (macOS notarization via API key, Team ID `T3KK66Q67T` per memory `project_apple_signing.md`), `generate_tosc.py` (TouchOSC layout generator).

**`tests/`:**
- Purpose: CTest unit/integration tests, gated by `-DJAMWIDE_BUILD_TESTS=ON`. Most are pure-C++ stress tests for the SPSC + mirror infrastructure designed to also run cleanly under `-fsanitize=thread`.
- See file-by-file table under "Directory Layout" above.

**`assets/`:** TouchOSC `JamWide.tosc` layout.

**`resources/`:** `Info.plist.in` template for macOS plugin bundles.

**`docs/`:** User-facing docs and the GitHub Pages site for `jamwide.github.io` (download page).

**`memory-bank/`:** Session-handoff notes preserved by the AI workflow (separate from `.planning/`).

**`.planning/`:** GSD command artifacts. `MILESTONES.md`, `STATE.md`, `ROADMAP.md`, plus `codebase/` (this analysis), `debug/`, `config.json`. Mentioned briefly here — not source code, not built.

**`.claude/`:** Claude Code project config + custom skills (`bump-beta`, `install-vst3`).

**`build/`, `build-juce/`, `build-test/`, `build-tsan/`:** CMake build directories. Gitignored.

## Key File Locations

**Plugin entry / factory:**
- `juce/JamWideJuceProcessor.cpp:750` — JUCE `createPluginFilter()` factory (the JUCE-required plugin entry point used by VST3, AU, Standalone, CLAP).
- `juce/JamWideJuceProcessor.cpp:151` — `prepareToPlay` (audio-graph activation).
- `juce/JamWideJuceProcessor.cpp:486` — `processBlock` (audio thread entry).

**Vestigial entry (NOT built):**
- `src/plugin/clap_entry.cpp` — old direct-CLAP entry. Not compiled.
- `src/plugin/clap_entry_export.cpp` — old `clap_entry` export. Not compiled.

**Configuration:**
- `CMakeLists.txt` — single source of truth for build options (`JAMWIDE_BUILD_TESTS`, `JAMWIDE_BUILD_JUCE`, `JAMWIDE_DEV_BUILD`, `JAMWIDE_TSAN`, `JAMWIDE_UNIVERSAL`, `JAMWIDE_HARDENED_RUNTIME`, `JAMWIDE_CODESIGN_IDENTITY`).
- `JamWide.entitlements` — macOS hardened-runtime entitlements (microphone, JIT for AU host).
- `resources/Info.plist.in` — macOS plugin bundle metadata template.
- `cmake/increment-build-revision.cmake` — bumps `src/build_number.h` on every build.

**Core logic:**
- `src/core/njclient.{h,cpp}` — NJClient (audio engine + run-thread API).
- `src/core/netmsg.{h,cpp}` — TCP message framing + crypto hook.
- `src/core/mpb.{h,cpp}` — NINJAM protocol message types.
- `src/crypto/nj_crypto.{h,cpp}` — AES-256-CBC + SHA-256 key derivation.
- `src/threading/spsc_ring.h` + `src/threading/spsc_payloads.h` — lock-free transports between threads.
- `juce/JamWideJuceProcessor.{h,cpp}` — JUCE host shim, owns NJClient.
- `juce/NinjamRunThread.{h,cpp}` — dedicated run thread wrapping NJClient::Run.
- `juce/JamWideJuceEditor.{h,cpp}` — editor shell.

**Testing:**
- `tests/` — all unit/stress tests (see directory layout).
- `companion/src/__tests__/` — Vitest unit tests for the web companion.
- `companion/e2e/` — Playwright e2e tests.

**Build / packaging:**
- `scripts/build.sh` — local dev build (Ninja, x86_64 only locally per memory `project_local_build_setup.md`).
- `scripts/notarize.sh` — macOS notarization wrapper.
- `release.sh` — top-level release script.
- `install.sh` / `install-win.ps1` — end-user install scripts.

## Naming Conventions

**Files:**
- C++ headers: `.h` (preferred) or `.hpp` (rare, only in third-party trees).
- C++ sources: `.cpp`.
- Objective-C++ (macOS-specific JUCE/native code): `.mm` (e.g. `juce/video/BrowserDetect_mac.mm`, `src/platform/gui_macos.mm`).
- Plain C (only WDL UTF-8 helpers): `.c` (`wdl/win32_utf8.c`).
- Two casing styles coexist:
  - **Legacy NINJAM / WDL / `src/`** — `snake_case.{h,cpp}` (e.g. `njclient.cpp`, `netmsg.cpp`, `nj_crypto.cpp`, `spsc_ring.h`, `ui_command.h`, `server_list.cpp`).
  - **Modern JUCE shim under `juce/`** — `PascalCase.{h,cpp}` matching the class it defines (e.g. `JamWideJuceProcessor.cpp`, `ChannelStrip.cpp`, `MidiMapper.cpp`, `OscServer.cpp`, `VideoCompanion.cpp`).
- Tests: `tests/test_<feature>.cpp` (snake_case, always `test_` prefix).

**Directories:**
- All-lowercase, sometimes `kebab-case` for third-party (`clap-helpers`, `clap-juce-extensions`).
- No mixed-case directories.

**Classes / types:**
- Active C++ uses `PascalCase` (`NJClient`, `Net_Connection`, `JamWideJuceProcessor`, `LocalChannelMirror`, `RemoteUserMirror`, `OscServer`, `MidiMapper`).
- WDL-inherited types occasionally use `snake_case_with_PascalCase` hybrid (`Net_Message`, `WDL_PtrList`, `JNL_Connection`).

**Member variables:**
- Legacy NINJAM C++ → `m_` prefix + lowercase (`m_netcon`, `m_audio_enable`, `m_remoteusers`, `m_locchan_cs`).
- New 15.x mirror/SPSC fields → `m_` + snake_case (`m_locchan_mirror`, `m_remoteuser_update_q`, `m_audio_drain_generation`).
- JUCE shim → camelCase, no prefix (`clientLock`, `cachedUsers`, `userCount`, `runThread`, `pttActive`).

**Constants:**
- `MACRO_CASE` for compile-time constants (`MAX_LOCAL_CHANNELS`, `MAX_USER_CHANNELS`, `MAX_PEERS`, `NJ_CRYPTO_IV_LEN`, `NET_MESSAGE_MAX_SIZE`).
- `PascalCase` constexpr (`kBaseWidth`, `kRemoteNameMax`, `kSyncIdle`, `kTotalOutChannels`).

**SPSC ring / payload variants:** `PascalCase` struct names ending in `Update` / `Command` / `Event` (`PeerAddedUpdate`, `LocalChannelMonitoringUpdate`, `ConnectCommand`, `ChatMessageEvent`).

**Namespaces:** `jamwide` for new code in `src/threading/`, `src/net/`, `juce/video/`. Legacy NINJAM code under `src/core/` uses no namespace (preserves Cockos-source diff parity).

## How to Add New Code

**New JUCE UI component (channel strip widget, dialog, popup):**
- Implementation: `juce/ui/<ComponentName>.{h,cpp}` (PascalCase).
- Add to `target_sources(JamWideJuce PRIVATE …)` in `CMakeLists.txt:166-191`.
- Register in the editor: instantiate as a member of `JamWideJuceEditor`, `addAndMakeVisible(...)` in the constructor, position in `resized()`.
- Persistent state must live on `JamWideJuceProcessor`, not the editor (editor is destroyed/recreated by the host).

**New OSC / MIDI feature:**
- Implementation: `juce/osc/` or `juce/midi/`.
- Add to `target_sources(JamWideJuce PRIVATE …)` in `CMakeLists.txt`.
- Address mapping → extend `juce/osc/OscAddressMap.cpp` or `juce/midi/MidiMapper.cpp`. Audio-thread MIDI processing already wired in `processBlock` via `midiMapper->processIncomingMidi` / `appendFeedbackMidi`.

**New cross-thread state field:**
- If audio thread reads + run/UI thread writes: declare `std::atomic<T>` on `NJClient` (when audio-related) or `JamWideJuceProcessor` (when JUCE-related). See examples at `src/core/njclient.h:320-330` (`config_metronome`, `config_mastervolume`).
- If the field is part of channel/peer state: extend the appropriate mirror in `src/core/njclient.h` AND the matching payload variant in `src/threading/spsc_payloads.h` (note the `static_assert` `is_trivially_copyable_v` contract). Then wire the producer-side push and the audio-thread drain.
- DO NOT add raw pointers into mirrors (Codex HIGH-2 — see ARCHITECTURE.md anti-patterns).

**New UI command (UI → Run):**
- Define a struct in `src/threading/ui_command.h` (e.g. `MyNewCommand { … }`).
- Add to the `using UiCommand = std::variant<…>` at the bottom of that file.
- Push from the editor: `processorRef.cmd_queue.try_push(MyNewCommand{...})`.
- Handle in `NinjamRunThread::processCommands` via `std::visit`.

**New UI event (Run → UI):**
- Define a struct in `src/threading/ui_event.h`.
- Add to `using UiEvent = std::variant<…>`.
- Push from the run thread: `processor.evt_queue.try_push(MyNewEvent{...})`.
- Drain in `JamWideJuceEditor::drainEvents` via `std::visit`.

**New protocol message:**
- Define a `mpb_*` builder/parser in `src/core/mpb.h` + `src/core/mpb.cpp` mirroring the existing patterns (parse from `Net_Message`, build to `Net_Message`).
- Wire send: call `m_netcon->Send(builder.build())` on the run thread inside `NJClient::Run` or `Connect`.
- Wire receive: extend the `switch (msg->get_type())` in `NJClient::Run` (`src/core/njclient.cpp` — search for the giant switch starting around the auth-challenge case at `src/core/njclient.cpp:1506`).

**New codec:**
- Implement `I_NJEncoder` / `I_NJDecoder` in `wdl/<name>encdec.h` (use `wdl/flacencdec.h:34,158` as the template — both implement the `VorbisEncoderInterface` / `VorbisDecoderInterface` typedefs).
- Add a `MAKE_NJ_FOURCC` constant (4 ASCII chars) to `src/core/njclient.cpp` near `NJ_ENCODER_FMT_TYPE` / `NJ_ENCODER_FMT_FLAC` (`src/core/njclient.cpp:152-153`).
- Wire `CreateNJEncoder` / `CreateNJDecoder` switch in `src/core/njclient.cpp` (currently selects on `m_encoder_fmt_active` for upload; on incoming FOURCC for download — search for `NJ_ENCODER_FMT_TYPE` / `NJ_ENCODER_FMT_FLAC` in `src/core/njclient.cpp`).
- If new codec needs a third-party library, add it via `add_subdirectory(libs/<lib> EXCLUDE_FROM_ALL)` in `CMakeLists.txt` and link into `njclient`.

**New unit / stress test:**
- Add `tests/test_<feature>.cpp`.
- Register in `CMakeLists.txt` inside the `if(JAMWIDE_BUILD_TESTS)` block (`CMakeLists.txt:338-481`). Most tests use `add_executable + add_test`; OSC / MIDI tests use `juce_add_console_app` and need the JUCE module dependencies (`CMakeLists.txt:295-333`).
- Pure-C++ tests intended for TSan should NOT link `njclient` (they re-implement the smallest pattern in isolation). Tests that need codec libs link `wdl vorbis vorbisenc ogg FLAC` (see `test_decoder_prealloc` at `CMakeLists.txt:459-465`).

**New build option / CMake flag:**
- Add `option(JAMWIDE_<NAME> "Description" <DEFAULT>)` near the top of `CMakeLists.txt` (`CMakeLists.txt:29-35`).
- Conditionally add compile definitions / sources inside the existing `if(JAMWIDE_BUILD_JUCE)` / `if(JAMWIDE_BUILD_TESTS)` / `if(JAMWIDE_TSAN)` blocks.

**New companion-side feature (web video sidecar):**
- Implementation: `companion/src/`.
- Tests: `companion/src/__tests__/` (Vitest) + `companion/e2e/` (Playwright).
- Wire WS messages with the C++ side via `juce/video/VideoCompanion.cpp` (uses `ixwebsocket` server).

## Special Directories

**`build/`, `build-juce/`, `build-test/`, `build-tsan/`:**
- Purpose: CMake build output dirs. `build/` is reserved for CI universal-binary builds; `build-juce/` is the local-dev Ninja x86_64 build (`scripts/build.sh`). `build-test/` and `build-tsan/` for CTest and ThreadSanitizer respectively.
- Generated: Yes (every build).
- Committed: No (gitignored).

**`.planning/`:**
- Purpose: GSD command artifacts (Get Stuff Done workflow). Includes `MILESTONES.md`, `STATE.md`, `ROADMAP.md`, `RETROSPECTIVE.md`, `PROJECT.md`, `REQUIREMENTS.md`, `HANDOFF.json`, plus subdirs `codebase/` (this analysis), `debug/`, `config.json`.
- Generated: By GSD commands (planning, research, summaries).
- Committed: Generally yes (project-history value).

**`.claude/`:**
- Purpose: Claude Code project-local config + custom skills.
- Skills: `bump-beta`, `install-vst3` (project-specific shortcuts).
- Generated: Manually authored.
- Committed: Yes.

**`memory-bank/`:**
- Purpose: Session-handoff notes for AI agents working on the project.
- Generated: Authored during agent sessions.
- Committed: Yes.

**`IEMPluginSuite/`:**
- Purpose: Sibling repository checked out for reference (Ambisonics plugins). NOT built by JamWide's CMake — out of build scope.
- Committed: Repo present per `git status`; not a submodule.

**`libs/juce`, `libs/clap*`, `libs/libflac`, `libs/libogg`, `libs/libvorbis`, `libs/ixwebsocket`, `libs/imgui`:**
- Purpose: Third-party dependencies. Each pulled via `add_subdirectory(... EXCLUDE_FROM_ALL)` so they only build when their consuming target requires them.
- Committed: Yes (subtree or submodule depending on lib).

## CMake Targets

```text
jamwide-build-number  ALL custom target — bumps src/build_number.h every build (CMakeLists.txt:37-42)
wdl                   STATIC          — WDL utilities + JNetLib (CMakeLists.txt:75-89)
ogg                   STATIC          — libogg (libs/libogg)
vorbis vorbisenc      STATIC          — libvorbis (libs/libvorbis)
FLAC                  STATIC          — libFLAC (libs/libflac, native FLAC, no OGG-FLAC, C API only)
njclient              STATIC          — NJClient core (links wdl + codec libs + crypto)
ixwebsocket           STATIC          — WebSocket server for video companion (libs/ixwebsocket)

# Gated by JAMWIDE_BUILD_JUCE (default ON):
juce_*                                 — JUCE 7 modules (libs/juce)
JamWideJuce           SHARED+plugin   — combined target producing:
  ├─ JamWideJuce_VST3       VST3 bundle
  ├─ JamWideJuce_AU         AU bundle (macOS)
  ├─ JamWideJuce_Standalone executable
  └─ JamWideJuce_CLAP       CLAP bundle (via clap-juce-extensions)
validate              custom target   — runs Pluginval against the VST3 (macOS only)

# Gated by JAMWIDE_BUILD_TESTS (default OFF) — see tests/ above
test_flac_codec, test_encryption, test_video_sync, test_njclient_atomics,
test_spsc_state_updates, test_deferred_delete, test_local_channel_mirror,
test_block_queue_spsc, test_remote_user_mirror, test_decode_media_buffer_spsc,
test_decode_state_arming, test_decoder_prealloc, test_peer_churn_simulation,
test_osc_loopback, test_midi_mapping
```

**`JAMWIDE_BUILD_JUCE` toggle (`CMakeLists.txt:31`):**
- Default `ON`. Toggling `OFF` skips the entire `juce/` plugin target and the OSC/MIDI tests but still builds `njclient`, `wdl`, codec libs, crypto, and the non-JUCE tests.
- **Per memory `project_legacy_clap_flag.md`:** This flag is **vestigial** — a leftover from before commit `0d82641` removed the parallel direct-CLAP build (the `src/plugin/clap_entry.cpp` path). Today there is only ONE plugin build path (`juce/`), and `OFF` produces no plugin at all. The flag survives only as a fast-test convenience (skip JUCE compile time when iterating on `tests/`).

**Plugin identity:** Centralized at `CMakeLists.txt:44-47` so VST3 / AU / CLAP all carry the same bundle ID (`com.jamwide.juce-client`), manufacturer code (`JmWd`), plugin code (`JwJc`).

**macOS architecture:** `JAMWIDE_UNIVERSAL=ON` (default) → universal binary (arm64 + x86_64). Local builds typically set `-DJAMWIDE_UNIVERSAL=OFF -DCMAKE_OSX_ARCHITECTURES=x86_64` for speed (per memory `project_local_build_setup.md`).

**ThreadSanitizer:** `JAMWIDE_TSAN=ON` skips codesigning entirely (TSan injects a runtime not covered by ad-hoc signing — per memory and `CMakeLists.txt:32-35,244`).

---

*Structure analysis: 2026-04-30*
