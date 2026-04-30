# Technology Stack

**Analysis Date:** 2026-04-30

## Languages

**Primary:**
- **C++20** — Core plugin and audio engine. Standard set in `CMakeLists.txt` (`set(CMAKE_CXX_STANDARD 20)`, line 23). Used across `src/`, `juce/`, and the bundled `wdl/`.
- **C** — Low-level glue and Windows UTF-8 wrappers (e.g. `wdl/win32_utf8.c`). Project declares both languages: `project(jamwide VERSION 1.0.0 LANGUAGES C CXX)` (`CMakeLists.txt:2`).
- **Objective-C / Objective-C++** — Apple-only platform code. `enable_language(OBJC)` / `enable_language(OBJCXX)` (`CMakeLists.txt:13-14`). Source files: `juce/video/BrowserDetect_mac.mm`, `src/platform/gui_macos.mm`.

**Secondary:**
- **TypeScript ~5.x** — Video companion web app. `companion/tsconfig.json` targets `ES2020`, `module: ESNext`, `moduleResolution: bundler`, `strict: true`. Sources under `companion/src/` (e.g. `main.ts`, `popout.ts`, `ui.ts`, `ws-client.ts`).
- **Python 3** — Build-time tooling only. `scripts/generate_tosc.py` generates the `assets/JamWide.tosc` TouchOSC template (XML + zlib). Not a runtime dependency.
- **Bash** — Dev/release scripts: `scripts/build.sh`, `scripts/notarize.sh`, `release.sh`, `install.sh`.
- **PowerShell** — Windows installer: `install-win.ps1`.

## Runtime

**Native binary targets** (no managed runtime). Built artefacts:
- macOS: `JamWide.vst3`, `JamWide.component` (AU), `JamWide.clap`, `JamWide.app` (Standalone) — produced under `build*/JamWideJuce_artefacts/Release/`.
- Windows: `JamWide.vst3`, `JamWide.clap`, `JamWide.exe`.
- Linux: `JamWide.vst3`, `JamWide.clap`, `JamWide` (standalone executable).

**Plugin-host runtimes** (the binary is loaded by these):
- VST3-compatible DAWs.
- AudioUnit hosts (macOS only).
- CLAP hosts (via `clap-juce-extensions`).
- Standalone wraps JUCE's standalone audio app shell.

**Companion web runtime** (separate process, browser-side):
- Node.js for build-time only (Vite). At runtime, the companion is a static SPA loaded by the user's default browser; it talks to the plugin via local WebSocket (`127.0.0.1:7170`).

**Package Manager:**
- **CMake 3.20+** for native code. `cmake_minimum_required(VERSION 3.20)` (`CMakeLists.txt:1`). All third-party C/C++ deps are git submodules under `libs/` (no Conan, no vcpkg).
- **npm** for the TypeScript companion. `companion/package.json` + `companion/package-lock.json`. `"type": "module"`.

**Lockfiles:**
- `companion/package-lock.json` — present.
- C++ deps pinned via `.gitmodules` SHAs (one source of truth for upstream tags, see Key Dependencies).
- `.build_revision` (root) — stale leftover; live build counter now lives in `src/build_number.h` and is bumped by `cmake/increment-build-revision.cmake` on every build (target `jamwide-build-number`, `CMakeLists.txt:37-42`).

## Frameworks

**Core:**
- **JUCE 8.0.12** — Audio plugin framework. Vendored at `libs/juce/` (submodule of `https://github.com/juce-framework/JUCE.git`). Plugin defined in `CMakeLists.txt:145-227` via `juce_add_plugin(JamWideJuce ...)`. JUCE modules linked: `juce_audio_utils`, `juce_opengl`, `juce_gui_extra`, `juce_osc`, `juce_cryptography` (`CMakeLists.txt:218-227`).
- **CLAP 1.2.7** — Plugin format spec, vendored at `libs/clap/`. Wrapper provided by `clap-juce-extensions` (`libs/clap-juce-extensions/`, tag-derived `0.26.0-105-g02f91b7`) — `clap_juce_extensions_plugin(...)` at `CMakeLists.txt:230-234`. There is also `libs/clap-wrapper` (`v0.13.0`) and `libs/clap-helpers` (commit `a61bcdf`) — the JUCE path is the one wired into the production target; clap-wrapper appears to be retained for legacy/alternative builds (see memory note: `JAMWIDE_BUILD_JUCE` is vestigial w.r.t. the pre-JUCE CLAP build path).
- **NJClient (forked from NINJAM, GPLv2)** — JamTaba/NINJAM lineage networked-jam engine. Source under `src/core/` (`njclient.cpp`, `njclient.h`, `mpb.cpp`, `mpb.h`, `netmsg.cpp`, `netmsg.h`, `njmisc.cpp`). Built as static lib `njclient` (`CMakeLists.txt:110-132`).
- **WDL (Cockos)** — Networking + utility library, vendored as a flat directory at `wdl/` (NOT a submodule). Includes JNetLib (`wdl/jnetlib/`: `connection.cpp`, `httpget.cpp`, `listen.cpp`, `asyncdns.cpp`), SHA-1 (`wdl/sha.cpp`), RNG (`wdl/rng.cpp`), JSON parser (`wdl/jsonparse.h`), FLAC encoder/decoder shim (`wdl/flacencdec.h`), Vorbis encoder/decoder shim (referenced via `WDL_VORBIS_INTERFACE_ONLY` define at `CMakeLists.txt:347`). Built as static lib `wdl` (`CMakeLists.txt:75-89`).

**Audio Codecs:**
- **libogg `v1.3.6-6-g0288fad`** — `libs/libogg/`, submodule of `https://github.com/xiph/ogg.git`. Added at `CMakeLists.txt:51-55` with tests/docs/pkgconfig/cmake-package install all forced OFF.
- **libvorbis `v1.3.7-22-g2d79800b`** — `libs/libvorbis/`, submodule of `https://github.com/xiph/vorbis.git`. Wired at `CMakeLists.txt:58-60`. Targets linked: `vorbis`, `vorbisenc`. Currently the **only live codec on the wire** for NINJAM compatibility (see `JamWideJuceProcessor.cpp:46-48`: "Vorbis default for compatibility (most NINJAM clients use Vorbis)").
- **libFLAC 1.5.0** — `libs/libflac/`, submodule of `https://github.com/xiph/flac.git`. Configured native FLAC (not OGG-FLAC), C-only, tests/programs/examples/docs OFF (`CMakeLists.txt:63-72`). Codec FOURCC `'F','L','A','C'` registered as `NJ_ENCODER_FMT_FLAC` (`src/core/njclient.cpp:153`); selectable from the ConnectionBar UI for sessions where peers also support it. Status: in development per `FLAC_INTEGRATION_PLAN.md` and recent commit `docs: reframe FLAC claims as "in development" (currently OGG/Vorbis only)`.
- **Opus** — **Planned only.** No `libopus` submodule, no link, no encoder/decoder. Detailed roadmap in `CODEC_REDESIGN_PLAN.md` (positions Opus as the future real-time default with FLAC as opt-in lossless and Vorbis kept as legacy-compat). Phase 2 of that plan adds the `libopus` dependency and adapter; Phase 3 adds FLAC robust-mode packetization.

**Networking:**
- **JNetLib (WDL)** — Raw TCP for the NINJAM protocol (`wdl/jnetlib/connection.cpp`, `JNL_Connection`) and HTTP GET for the public server-list fetch (`wdl/jnetlib/httpget.h`, used in `src/net/server_list.cpp:35`).
- **IXWebSocket `v11.4.6-3-g150e3d83`** — Local WebSocket server for the video companion. Submodule at `libs/ixwebsocket/` (`https://github.com/machinezone/IXWebSocket.git`). Built with `USE_TLS=OFF`, `USE_ZLIB=OFF`, `USE_WS=OFF` (client mode disabled), `IXWEBSOCKET_INSTALL=OFF` — server-only, plaintext, `disablePerMessageDeflate()` (`CMakeLists.txt:135-139`, `juce/video/VideoCompanion.cpp:264-277`). Server bound to `127.0.0.1`, default port `7170` (`juce/video/VideoCompanion.h:122`).
- **JUCE OSC** (`juce_osc` module) — Two-way OSC over UDP. Receiver runs on `juce_osc` network thread (`juce/osc/OscServer.h:9`, `OSCReceiver::Listener<RealtimeCallback>`). Default ports `9000` recv / `9001` send (`juce/JamWideJuceProcessor.h:182-184`).

**Crypto (Phase 15):**
- **OpenSSL 3.x** (macOS / Linux) — `find_package(OpenSSL REQUIRED)` (`CMakeLists.txt:106`), with Homebrew fallback that runs `brew --prefix openssl@3` and sets `OPENSSL_ROOT_DIR` (`CMakeLists.txt:96-105`). CI builds a universal arm64+x86_64 OpenSSL 3.4.1 from source (`.github/workflows/juce-build.yml:25-49`). Linked as `OpenSSL::Crypto`.
- **Windows BCrypt (CNG)** — Built into Windows; no external dep. Used unconditionally on `WIN32` (`CMakeLists.txt:122-124`, `target_link_libraries(njclient PUBLIC bcrypt)`). Source: `src/crypto/nj_crypto.cpp:9-19, 31-50` (`BCryptOpenAlgorithmProvider(BCRYPT_AES_ALGORITHM)` with `BCRYPT_CHAIN_MODE_CBC`).
- **Algorithm:** AES-256-CBC with random per-message IV; 32-byte key derived as `SHA-256(password + 8-byte server challenge)`. Header constants at `src/crypto/nj_crypto.h:6-12` (`NJ_CRYPTO_IV_LEN=16`, `NJ_CRYPTO_BLOCK_LEN=16`, `NJ_CRYPTO_MAX_PLAINTEXT=16384`). API: `encrypt_payload(...)`, `decrypt_payload(...)`, `derive_encryption_key(...)` (`nj_crypto.h:33-56`).
- **No libsodium** — not pulled in.
- **JUCE crypto** — `juce::juce_cryptography` is linked into `JamWideJuce` (`CMakeLists.txt:223`) but only used UI-side (e.g. SHA-1 room-ID derivation in `juce/video/VideoCompanion.h:39-40`); the wire-protocol crypto goes through `nj_crypto`.

**Testing:**
- **CTest** (CMake-built test binaries) — gated by `JAMWIDE_BUILD_TESTS=ON`, declared in `CMakeLists.txt:338-481`. Each test is a standalone executable + `add_test(NAME ... COMMAND ...)`. ~14 binaries total. Examples:
  - `tests/test_flac_codec.cpp` (codec roundtrip)
  - `tests/test_encryption.cpp` (AES-256-CBC + KDF)
  - `tests/test_njclient_atomics.cpp` (release/acquire pattern, TSan-targeted)
  - `tests/test_spsc_state_updates.cpp`, `test_deferred_delete.cpp`, `test_local_channel_mirror.cpp`, `test_remote_user_mirror.cpp`, `test_block_queue_spsc.cpp`, `test_decode_media_buffer_spsc.cpp`, `test_decode_state_arming.cpp`, `test_decoder_prealloc.cpp`, `test_peer_churn_simulation.cpp` — Phase 15.1 SPSC + mirror infrastructure under `-fsanitize=thread`.
  - `tests/test_video_sync.cpp` — pure-C++ video buffer-delay math.
  - `tests/test_osc_loopback.cpp`, `tests/test_midi_mapping.cpp` — JUCE console apps inside the `JAMWIDE_BUILD_JUCE` block (`CMakeLists.txt:295-333`).
- **ThreadSanitizer** — `-DJAMWIDE_TSAN=ON` toggles `-fsanitize=thread -g -O1 -fno-omit-frame-pointer` for both compile and link, into `build-tsan/` (`scripts/build.sh:43,82-89`). When TSan is on, codesigning + hardened runtime are forcibly OFF (`CMakeLists.txt:35,244`).
- **pluginval** (Tracktion) — VST3/AU/Standalone validation, downloaded on demand. Custom target `validate` (macOS) at `CMakeLists.txt:281-291`, downloader script `cmake/run-pluginval.cmake`. CI runs strictness 5 with 120 s timeout (`.github/workflows/juce-build.yml:79-103, 185-188, 255-262`).
- **Vitest 4.1.x + jsdom 29.x** — Unit tests for the companion. `companion/vitest.config.ts` runs `src/__tests__/**/*.test.ts` in jsdom. Run via `npm test` (`companion/package.json:10`). Test suites include `__tests__/url-builder.test.ts`, `__tests__/buffer-delay.test.ts`.
- **Playwright 1.50.x** — Companion E2E tests. `companion/playwright.config.ts` points at `companion/e2e/`, base URL `http://localhost:5173`, dev server reused if up.

**Build/Dev:**
- **CMake** (Ninja preferred when available, see below).
- **Ninja** — Auto-detected by `scripts/build.sh:52-56`; falls back to CMake's default generator otherwise. Local build dir: `build-juce/`.
- **Vite 6.x** — Companion dev server + bundler. `companion/vite.config.ts` outputs to `../docs/video/` with two entry points (`index.html`, `popout.html`) so the built artefacts are GitHub-Pages-served at `<site>/video/`.
- **TypeScript Compiler (`tsc`)** — Type-check pass before `vite build` (`companion/package.json:9`).
- **GitHub Actions** — `.github/workflows/juce-build.yml`: three matrix jobs (`macos-14`, `windows-latest`, `ubuntu-24.04`) → build → pluginval → upload artefacts; on `v*` tags, additional `release` job creates/updates a GitHub Release with the macOS/Linux `tar.gz` and Windows `zip` (memory note: macOS/Linux must be tar.gz to preserve Unix permissions).

## Key Dependencies

**Critical (linked into the plugin):**
- **JUCE 8.0.12** (`libs/juce/`) — host plugin framework, audio I/O, GUI, OSC, OpenGL accel.
- **NJClient (in-tree fork)** (`src/core/`, `src/threading/`) — NINJAM client engine, SPSC mirror infrastructure, audio thread.
- **WDL** (`wdl/`) — JNetLib (TCP + HTTP), SHA-1, RNG, JSON, codec shims. Static lib `wdl`.
- **libogg `v1.3.6-6-g0288fad`** (`libs/libogg/`) — Ogg container. Linked as `ogg` (`CMakeLists.txt:121`).
- **libvorbis `v1.3.7-22-g2d79800b`** (`libs/libvorbis/`) — Vorbis codec (encoder + decoder). Linked as `vorbis vorbisenc` (`CMakeLists.txt:121`).
- **libFLAC 1.5.0** (`libs/libflac/`) — Native FLAC C API. Linked as `FLAC` (`CMakeLists.txt:121`).
- **OpenSSL 3.x (macOS/Linux)** — `OpenSSL::Crypto` for AES-256-CBC + SHA-256 (`CMakeLists.txt:106,125`). Windows uses BCrypt instead (`bcrypt.lib`).
- **IXWebSocket `v11.4.6-3-g150e3d83`** (`libs/ixwebsocket/`) — Local WebSocket server for video companion (`CMakeLists.txt:135-139, 217`).
- **CLAP 1.2.7** (`libs/clap/`) + `clap-juce-extensions 0.26.0-105-...` (`libs/clap-juce-extensions/`) — CLAP plugin format adapter (`CMakeLists.txt:230-234`).

**Vendored but NOT currently linked into the production target:**
- **clap-wrapper `v0.13.0`** (`libs/clap-wrapper/`) — kept for the legacy/alternative pre-JUCE CLAP path; production CLAP target uses `clap-juce-extensions`.
- **clap-helpers** (`libs/clap-helpers/`, commit `a61bcdf`) — CLAP utility headers.
- **Dear ImGui `v1.92.5-71-...`** (`libs/imgui/`) — submodule retained but not referenced from `CMakeLists.txt`. Likely from the pre-JUCE UI experiments.
- **IEMPluginSuite/** (working tree only, in `.gitignore`) — external reference checkout for JUCE plugin patterns. Not part of the build.

**Infrastructure:**
- **picojson** (`src/third_party/picojson.h`) — single-header JSON parser. Used for OSC config / persistence.
- **Tracktion pluginval** — fetched on demand by `cmake/run-pluginval.cmake` and the CI workflow.

## Configuration

**Build options** (CMake cache flags, all defined in `CMakeLists.txt`):
- `JAMWIDE_BUILD_TESTS` (default OFF) — build CTest binaries.
- `JAMWIDE_BUILD_JUCE` (default ON) — build VST3/AU/CLAP/Standalone via JUCE. Memory note: vestigial flag from the removed pre-JUCE CLAP build (commit `0d82641`); today only gates a fast-test convenience.
- `JAMWIDE_DEV_BUILD` (default ON) — verbose logging, propagates `JAMWIDE_DEV_BUILD=1` to `njclient` (`CMakeLists.txt:127-129`).
- `JAMWIDE_TSAN` (default OFF) — ThreadSanitizer build; forces hardened runtime / codesign OFF on macOS (`CMakeLists.txt:35,244`).
- `JAMWIDE_UNIVERSAL` (Apple-only, default ON) — `arm64;x86_64` universal binary (`CMakeLists.txt:15-20`). Local default via `scripts/build.sh:92-95` is `x86_64` only (matches Homebrew openssl arch).
- `JAMWIDE_CODESIGN_IDENTITY` (default `-`, ad-hoc) — macOS Developer ID for release signing.
- `JAMWIDE_HARDENED_RUNTIME` (default OFF) — required for notarization.
- `OPENSSL_ROOT_DIR` (cache PATH) — overridden in CI to point at universal-built OpenSSL.

**Plugin identity** (`CMakeLists.txt:45-47`):
- Bundle ID: `com.jamwide.juce-client`
- Manufacturer code: `JmWd`
- Plugin code: `JwJc`
- VST3 categories: `Fx Network`
- AU type: `kAudioUnitType_Effect`

**Compile-time defines** for `JamWideJuce` (`CMakeLists.txt:201-206`):
- `JUCE_WEB_BROWSER=0` (no embedded WebView)
- `JUCE_USE_CURL=0` (HTTP via JNetLib instead)
- `JUCE_VST3_CAN_REPLACE_VST2=0`
- `JUCE_DISPLAY_SPLASH_SCREEN=0`

**Wire-protocol constants** (`src/core/mpb.h:35-37`):
- `PROTO_VER_MIN 0x00020000`, `PROTO_VER_MAX 0x0002ffff`, `PROTO_VER_CUR 0x00020000` — NINJAM protocol v2.
- `NET_MESSAGE_MAX_SIZE 16384`; encrypted form +32 bytes for IV+padding (`src/core/netmsg.h:39-40`).

**Runtime config / persistence:**
- `juce::AudioProcessorValueTreeState` (APVTS) for parameters (`juce/JamWideJuceProcessor.h:106`, layout in `JamWideJuceProcessor.cpp:73-`).
- ValueTree members for non-APVTS state: `lastServerAddress` (default `"ninbot.com"`), `lastUsername` (default `"anonymous"`), OSC enable + ports, MIDI device IDs, chat sidebar visibility, local transmit/input selectors. Saved via `getStateInformation`/`setStateInformation` (`JamWideJuceProcessor.h:81-82, 141-198`).
- Public server-list URL: `http://autosong.ninjam.com/serverlist.php` (default in `src/ui/ui_state.h:105`, also hard-coded fallbacks at `juce/JamWideJuceEditor.cpp:85,512` and `juce/NinjamRunThread.cpp:828`).
- Working dir for recordings + temp files: `juce::File::tempDirectory/JamWide` (`juce/JamWideJuceProcessor.cpp:41-44`). `NJClient::config_savelocalaudio` (0=off, 1=compressed only, 2=compressed+wav, -1=delete remote .ogg ASAP) (`src/core/njclient.h:316-317`).
- Local WebSocket: `127.0.0.1:7170` (`VideoCompanion.h:122`, `VideoCompanion.cpp:4`).

**Environment / secrets:**
- `~/.appstoreconnect/notarize.env` — sourced by `scripts/notarize.sh:26-37` for `NOTARIZE_KEY_ID`, `NOTARIZE_ISSUER_ID`, `NOTARIZE_KEY_PATH` (App Store Connect API key for `xcrun notarytool`).
- CI secrets used: `DEVELOPER_ID_P12`, `DEVELOPER_ID_P12_PASSWORD`, `NOTARIZE_KEY_ID`, `NOTARIZE_ISSUER_ID`, `NOTARIZE_API_KEY_P8` (`.github/workflows/juce-build.yml:108-130`).
- macOS entitlements: `JamWide.entitlements` — `com.apple.security.device.audio-input`, `com.apple.security.network.client`, `com.apple.security.network.server`.
- MCP dev tooling: `.mcp.json` registers a local Sketch HTTP MCP server at `http://localhost:31126/mcp` (UI mockups; not a production runtime dep).

## Platform Requirements

**Development:**
- **macOS:** Xcode toolchain (Apple clang), CMake 3.20+, Ninja (recommended), Homebrew `openssl@3` (auto-detected via `brew --prefix openssl@3` at configure time). `scripts/build.sh` defaults to `x86_64`-only locally; pass `--universal` for arm64+x86_64.
- **Windows:** Visual Studio 2022 (`-G "Visual Studio 17 2022" -A x64` per CI), MSVC dynamic runtime (`MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`, `CMakeLists.txt:9`). BCrypt comes from the OS.
- **Linux:** GCC/Clang with C++20, plus the apt deps installed in CI (`.github/workflows/juce-build.yml:217-231`): `build-essential cmake pkg-config libasound2-dev libjack-jackd2-dev libfreetype-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev libgl1-mesa-dev libcurl4-openssl-dev libwebkit2gtk-4.1-dev libssl-dev`.
- **Companion:** Node.js (any LTS) for `npm install` + `vite`. Browsers (Chromium-class) for E2E via Playwright.

**CI runners:**
- `macos-14` (arm64), `windows-latest`, `ubuntu-24.04`, plus an `ubuntu-latest` release job. Defined in `.github/workflows/juce-build.yml`.

**Production:**
- Plugin: any DAW supporting one of VST3 / AU (macOS) / CLAP / Standalone, on macOS 11+, Windows 10/11 x64, or recent Linux.
- macOS Standalone needs microphone access (`MICROPHONE_PERMISSION_ENABLED TRUE`, prompt text at `CMakeLists.txt:160-161`) and the network entitlements above.

---

*Stack analysis: 2026-04-30*
