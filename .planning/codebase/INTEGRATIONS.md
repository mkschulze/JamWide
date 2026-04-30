# External Integrations

**Analysis Date:** 2026-04-30

## APIs & External Services

**NINJAM session protocol (implemented, primary):**
- Custom binary TCP protocol inherited from Cockos NINJAM, implemented by NJClient and the WDL JNetLib layer. Wire-format constants in `src/core/mpb.h:35-37`:
  - `PROTO_VER_MIN 0x00020000`, `PROTO_VER_MAX 0x0002ffff`, `PROTO_VER_CUR 0x00020000` (NINJAM protocol v2).
- Connection entry point: `NJClient::Connect(host, user, pass)` (`src/core/njclient.h:301`). The run thread (`src/threading/run_thread.cpp`) drives the state machine via `NJClient::Run()`.
- Default server: `ninbot.com` (`juce/JamWideJuceProcessor.h:141`, ValueTree key `lastServer`). Default port: `2049` (`juce/osc/OscServer.cpp:690`, "NINJAM default"). UAT/legacy bug: `ninbot_` bots still appear in the mixer despite filtering — see project memory.
- Anonymous logins: hard-coded "anonymous:" prefix logic in `src/threading/run_thread.cpp:202` ("required by NINJAM server protocol for anonymous logins").
- License-agreement handshake: server may push a license string; UI prompts the user via `LicenseDialog` (`juce/ui/LicenseDialog.h`) and the Run thread waits on `license_pending` / `license_response` / `license_cv` (`JamWideJuceProcessor.h:200-204`). Wire-side handling at `src/core/njclient.cpp:1537-1547`.
- Encryption (Phase 15): AES-256-CBC, random per-message IV, key = `SHA-256(password + 8-byte server challenge)`. Implementation `src/crypto/nj_crypto.cpp` (BCrypt on Windows, OpenSSL `EVP_*` on macOS/Linux). Max plaintext per frame: `NJ_CRYPTO_MAX_PLAINTEXT 16384` (`nj_crypto.h:12`); on-the-wire envelope `NET_MESSAGE_MAX_SIZE_ENCRYPTED = 16384 + 32` (`netmsg.h:40`).
- Audio shipped between peers as Ogg/Vorbis (default, today) or native FLAC frames (in-development codec mode). Ogg/Vorbis FOURCC `'O','G','G','v'`, FLAC FOURCC `'F','L','A','C'` (`src/core/njclient.cpp:153, 2489`). Opus is **planned only** (see `CODEC_REDESIGN_PLAN.md`); not on the wire.

**Public server discovery (implemented):**
- Endpoint: `http://autosong.ninjam.com/serverlist.php` — referenced as default at `src/ui/ui_state.h:105` and hard-coded as the fallback URL at `juce/JamWideJuceEditor.cpp:85,512` and `juce/NinjamRunThread.cpp:828`.
- HTTP client: `JNL_HTTPGet` from WDL JNetLib (`src/net/server_list.h:35`, `src/net/server_list.cpp:80-`). Plain HTTP, no TLS — `JUCE_USE_CURL=0` is set so JUCE does not pull in libcurl.
- Response parsers: `parse_ninjam_format` (legacy plain-text) and `parse_json_format` (`src/net/server_list.h:32-33`). JSON parsed via `wdl/jsonparse.h`.

**VDO.Ninja (implemented as embed; protocol is HTML5 + WebRTC, not a JamWide-side API):**
- The plugin never speaks WebRTC itself. Instead, it derives a deterministic room ID + room password from the NINJAM session and launches the user's default browser at the JamWide-hosted companion page (which embeds a VDO.Ninja iframe).
- Companion page production URL is the GitHub-Pages-hosted build (Vite outputs to `docs/video/` per `companion/vite.config.ts:8`).
- VDO.Ninja iframe base URLs (`companion/src/ui.ts:7-8`):
  - Production: `https://vdo.ninja/`
  - Alpha: `https://vdo.ninja/alpha/`
  - postMessage origin: `https://vdo.ninja` (`companion/src/ui.ts:241`).
- Room derivation: `SHA-1(serverAddr + ":" + password)` (or `+ ":jamwide-public"` for password-less servers), prefix `jw-`, 16 hex chars (`juce/video/VideoCompanion.h:38-40`).
- Room password derivation: `SHA-256(password + ":" + roomId)` truncated to 16 hex chars (64 bits) (`juce/video/VideoCompanion.h:140-150`, `VideoCompanion.cpp:104-108`). Sent to VDO.Ninja via `#password=` URL fragment.
- Buffer-delay sync: plugin computes a target audio delay from BPM/BPI (or Phase 14.2 measured delay) and posts `{ setBufferDelay: N }` to the iframe with `targetOrigin = 'https://vdo.ninja'` (`companion/src/__tests__/buffer-delay.test.ts:19-48`).
- Privacy gating: `VideoPrivacyDialog` (`juce/video/VideoPrivacyDialog.h`) requires explicit user consent before the first launch ("Video uses peer-to-peer WebRTC via VDO.Ninja. Your IP address …" `VideoPrivacyDialog.cpp:70`).
- Status: implemented as a launch-and-embed integration. The deeper VDO.Ninja interop work (sharing join links, joining external rooms) is **planned only** per project memory `project_vdoninja_interop.md`.

**OSC (implemented, two-way over UDP):**
- Server class: `OscServer` in `juce/osc/OscServer.{h,cpp}` — derives from `juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>` (callbacks fire on JUCE's `juce_osc` network thread).
- Default ports: receive `9000`, send `127.0.0.1:9001` (`JamWideJuceProcessor.h:182-184`, "per D-17"). User-configurable via `OscConfigDialog` (`juce/osc/OscConfigDialog.h`), persisted in ValueTree (`oscEnabled`, `oscReceivePort`, `oscSendIP`, `oscSendPort`).
- Address map: `OscAddressMap` (`juce/osc/OscAddressMap.h`). Master + per-channel volumes/mutes/solos/pans, remote-user volumes, video state, video popout (`/JamWide/video/popout/{idx}` per `VideoCompanion.h:101-103`).
- TouchOSC template: `assets/JamWide.tosc`, regeneratable via `python3 scripts/generate_tosc.py` (target: iPad 1024x768 landscape, Voicemeeter dark palette).

**MIDI (implemented):**
- Plugin advertises `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT TRUE` (`CMakeLists.txt:154-155`). DAW supplies the buses; standalone uses JUCE's host devices.
- MIDI mapper: `juce/midi/MidiMapper.{h,cpp}`, MIDI-learn manager: `juce/midi/MidiLearnManager.{h,cpp}`, status indicator: `MidiStatusDot.{h,cpp}`, config UI: `MidiConfigDialog.{h,cpp}`. MIDI mappings are persisted alongside other state via APVTS / ValueTree.
- Standalone-mode persistence: `midiInputDeviceId`, `midiOutputDeviceId` stored as JUCE String (stable IDs, per review feedback) on `JamWideJuceProcessor.h:177-178`.

## Data Storage

**Databases:** None. JamWide does not use any database (SQLite, Postgres, Redis, etc.).

**File Storage:**
- Local filesystem only.
- Working / temp dir: `juce::File::tempDirectory/JamWide` (created at construction, `JamWideJuceProcessor.cpp:41-44`). Used by NJClient for streaming `.ogg` (and optionally `.wav`) interval recordings.
- Recording knobs: `NJClient::config_savelocalaudio` — `0` off, `1` save compressed only, `2` save compressed + WAV, `-1` delete remote `.ogg` ASAP (`src/core/njclient.h:316-317`). Output uses `WaveWriter` (`wdl/wavwrite.h`, `src/core/njclient.cpp:147,697`) and the bundled FLAC/Vorbis encoders.
- Plugin state persisted by host via `getStateInformation` / `setStateInformation` (JUCE ValueTree). Default APVTS save layout includes `currentStateVersion = 3` (`JamWideJuceProcessor.h:88`).

**Caching:**
- In-memory only. Examples: `cachedServerList`, `cachedUsers` (`JamWideJuceProcessor.h:117, 127`), VideoCompanion `cachedRoster_` (`VideoCompanion.h:201`).

## Authentication & Identity

**NINJAM auth:**
- Per-server: username + optional password supplied by user; the server can also issue a license-agreement prompt at handshake time.
- Anonymous logins use the literal `anonymous:` prefix on the NINJAM server side (see `run_thread.cpp:202`).
- Implementation lives in NJClient (`src/core/njclient.cpp:1511-1547`) — protocol-version check + license agreement + cred reply.
- No JamWide-side account system, no JWT, no OAuth.

**Companion auth:**
- The local WebSocket server (`127.0.0.1:7170`) only accepts clients whose `Origin` and `Host` headers match the companion URL the plugin just launched. Validated clients are tracked in a `std::set<ix::WebSocket*> validatedClients_` (`VideoCompanion.h:178-180`); broadcasts iterate that set, not `wsServer_->getClients()`, so a rogue local webpage cannot eavesdrop on roster/config messages.
- VDO.Ninja-side auth is the derived 64-bit room password (see VDO.Ninja section above).

## Monitoring & Observability

**Error tracking:** None — no Sentry, no Crashlytics, no remote telemetry. Crash logs are local (`Crash 2026-04-12_013917.log`, `Startup 2026-04-12_013716.log` in repo root from a recent UAT run).

**Logs:**
- `JAMWIDE_DEV_BUILD=1` enables verbose logging in `njclient` and the run thread (`CMakeLists.txt:127-129`). Logging headers: `src/debug/logging.h`.
- JUCE `DBG(...)` for editor/UI diagnostics.
- No log shipping; all logs stay on the user's machine.

**Build provenance:**
- Build number auto-incremented on every build via `cmake/increment-build-revision.cmake` → `src/build_number.h` (`#define JAMWIDE_BUILD_NUMBER N`). Custom target `jamwide-build-number ALL` (`CMakeLists.txt:37-42`).
- Memory note: still tracked as not-fully-automated (`project_build_number_automation.md`).

## CI/CD & Deployment

**Hosting / distribution:**
- **GitHub Releases** — primary distribution. Asset names: `JamWide-macOS.tar.gz` (preserves Unix permissions/symlinks for `.app` and `.component` bundles), `JamWide-Linux.tar.gz`, `JamWide-Windows.zip`. Workflow gates the release job on `startsWith(github.ref, 'refs/tags/v')` (`.github/workflows/juce-build.yml:280`).
- **GitHub Pages** — hosts the video companion. Vite emits to `docs/video/` (`companion/vite.config.ts:6-9`); Pages serves the `docs/` folder.
- Beta workflow: tagged `*-beta.N` triggers prerelease (`PRERELEASE_FLAG="--prerelease"` if tag matches `beta|alpha|rc`, `juce-build.yml:308-310`). Memory note: always update `docs/download.md` link when tagging a beta.

**CI Pipeline:**
- `.github/workflows/juce-build.yml` — single workflow, three platform jobs + a release job:
  1. `build-macos` (`macos-14`, arm64): builds universal OpenSSL 3.4.1 from source → CMake configure + build → pluginval (strictness 5, 120 s) → optional sign+notarize on tag → upload `tar.gz`.
  2. `build-windows` (`windows-latest`, VS 2022 x64): CMake → pluginval VST3 → upload staged `.vst3 / .clap / .exe`.
  3. `build-linux` (`ubuntu-24.04`): apt deps → CMake → pluginval (continue-on-error) → upload `tar.gz`.
  4. `release` (on tags only): downloads all artefacts, zips Windows, runs `gh release create/upload`.

**macOS signing & notarization (implemented):**
- Team ID: **`T3KK66Q67T`** (`scripts/notarize.sh:20`, see also project memory `project_apple_signing.md`).
- Sign identity: `Developer ID Application: Mark-Kristopher Schulze (T3KK66Q67T)`.
- Entitlements: `JamWide.entitlements` — audio input, network client, network server.
- Hardened runtime + timestamp + entitlements applied via `codesign --force --deep --options runtime --timestamp --entitlements ...` (`notarize.sh:59-67`).
- Notarization: **Apple App Store Connect API** via `xcrun notarytool submit … --key … --key-id … --issuer … --wait` (`scripts/notarize.sh:75-79`).
- Local cred store: `~/.appstoreconnect/notarize.env` defining `NOTARIZE_KEY_ID`, `NOTARIZE_ISSUER_ID`, `NOTARIZE_KEY_PATH` (path to `.p8` API key).
- CI cred store: GitHub Actions secrets `DEVELOPER_ID_P12`, `DEVELOPER_ID_P12_PASSWORD`, `NOTARIZE_KEY_ID`, `NOTARIZE_ISSUER_ID`, `NOTARIZE_API_KEY_P8` (`juce-build.yml:108-128`). The P12 cert is imported into a temporary keychain at runtime and the `.p8` is written under `$RUNNER_TEMP`.
- Stapling: `xcrun stapler staple` per bundle after notarization succeeds (`notarize.sh:91-93`).
- Build-time codesigning: every JUCE artefact is signed post-build via `add_custom_command(POST_BUILD)` (`CMakeLists.txt:258-269`). Default identity is `-` (ad-hoc) for dev; release builds override `JAMWIDE_CODESIGN_IDENTITY` and set `JAMWIDE_HARDENED_RUNTIME=ON`. **TSan builds skip codesign entirely** (`NOT JAMWIDE_TSAN`, `CMakeLists.txt:244`) — TSan injects a runtime ad-hoc signing does not cover.

**Windows signing:** Not implemented in CI. Plugin distributed unsigned for Windows currently.

**Linux signing:** N/A.

## Environment Configuration

**Required env vars (release flow only — the running plugin does not read env vars):**
- `NOTARIZE_KEY_ID` — App Store Connect API Key ID.
- `NOTARIZE_ISSUER_ID` — App Store Connect Issuer ID.
- `NOTARIZE_KEY_PATH` — filesystem path to the Apple `.p8` private key (or `NOTARIZE_API_KEY_B64` in CI, base64-decoded into `$RUNNER_TEMP/AuthKey.p8`).
- `OPENSSL_ROOT_DIR` — overridden in CI to the universal-built OpenSSL 3.4.1; locally falls back to `$(brew --prefix openssl@3)` (`CMakeLists.txt:96-105`).

**Secrets location:**
- Local dev: `~/.appstoreconnect/notarize.env` for notarization. Apple Developer ID cert + private key live in macOS Keychain.
- CI: GitHub Actions secrets (see above). Cert P12 + API key P8 are written to `$RUNNER_TEMP` and shredded at end-of-job (`juce-build.yml:120, 132`).
- No `.env` file is read at runtime by the plugin or by the companion.

## Webhooks & Callbacks

**Incoming (server-side):** None — JamWide does not host any cloud endpoint.

**Outgoing:**
- HTTP GET to the public server-list endpoint above (no signed payload, no auth headers).
- TCP to NINJAM servers on the user-supplied host:port.
- The companion browser fetches `https://vdo.ninja/` (or `/alpha/`) and connects WebRTC via VDO.Ninja's signaling — JamWide takes no part.

**Local IPC (cross-process within the user's machine):**
- WebSocket server: `127.0.0.1:7170` (TCP), plaintext, no TLS. `IXWebSocketServer` (`VideoCompanion.cpp:264`); per-message-deflate disabled (`VideoCompanion.cpp:277`); ping interval 5 s. Messages: `bufferDelay`, `beatHeartbeat`, `roster`, `popout`. Client = the companion page running in the user's browser.
- OSC: UDP `9000` (recv) and `127.0.0.1:9001` (send) by default, both user-configurable.

## Plugin Formats & DAW Integration

**Plugin formats produced** (`CMakeLists.txt:151`):
- **VST3** — `FORMATS VST3 …`, categories `Fx Network` (`CMakeLists.txt:152`), `VST3_AUTO_MANIFEST FALSE` (manifest is generated manually). `JUCE_VST3_CAN_REPLACE_VST2=0`.
- **AU (AudioUnit)** — macOS only. `AU_MAIN_TYPE kAudioUnitType_Effect`. Manufacturer code `JmWd`, plugin code `JwJc`.
- **CLAP** — via `clap-juce-extensions` (`CMakeLists.txt:230-234`). `CLAP_FEATURES audio-effect utility mixing`.
- **Standalone** — JUCE-wrapped standalone host-app. `MICROPHONE_PERMISSION_ENABLED TRUE` with prompt text "JamWide needs microphone access for standalone mode" (`CMakeLists.txt:160-161`). Memory note: JUCE's "Copy After Build" auto-installs VST3/AU/CLAP but NOT Standalone — for development one must build the `JamWideJuce_VST3` (or `_AU` / `_CLAP`) target.

**DAW host integration:**
- Bus layout: 4 stereo inputs (Local 1-4) + 17 stereo outputs (Main Mix + Remote 1-16) (`JamWideJuceProcessor.cpp:13-36`). `kTotalOutChannels = 34`, `kNumOutputBuses = 17`, `kMetronomeBus = 16` (`JamWideJuceProcessor.h:89-91`).
- `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` (`CMakeLists.txt:157`).
- DAW transport sync: `getPlayHead()` reads BPM, transport state, PPQ; processor implements a 3-state `IDLE / WAITING / ACTIVE` machine (`JamWideJuceProcessor.h:154-160`, `JamWideJuceProcessor.cpp:285-410`). Phase 4 deferred work: full DAW sync from JamTaba (memory note `project_phase4_deferred.md`).
- MIDI: full input + output buses; standalone uses JUCE's host MIDI devices.
- OpenGL: `juce::juce_opengl` linked for accelerated rendering (`CMakeLists.txt:219`).
- Validation: every release build is run through Tracktion `pluginval` strictness 5 in CI (VST3 mandatory, AU + Standalone `continue-on-error`, Linux VST3 `continue-on-error`).

## Other External Tools (dev only)

- **Sketch MCP server** (`.mcp.json` → `http://localhost:31126/mcp`) — used in dev for UI mockups and SVG asset generation. Not part of any shipped artefact. (See memory note `reference_sketch_mcp.md`.)
- **TouchOSC Editor** — primary asset is `assets/JamWide.tosc`; users can re-import after editing. `scripts/generate_tosc.py` regenerates the baseline from OSC address spec.

---

*Integration audit: 2026-04-30*
