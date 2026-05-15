# Requirements: JamWide

**Defined:** 2026-04-05 (v1.1) — extended 2026-05-15 (v1.3 Native Video)
**Core Value:** Musicians can jam together online with lossless audio quality and per-user mixing -- in any DAW or standalone.

## v1.1 Requirements

Requirements for OSC remote control and VDO.Ninja video companion.

### OSC Server

- [ ] **OSC-01**: User can receive OSC messages to control any mixer parameter (volume, pan, mute, solo)
- [ ] **OSC-02**: User can send OSC feedback to control surfaces reflecting current parameter state
- [ ] **OSC-03**: User can configure OSC send/receive ports and target IP via a settings dialog
- [ ] **OSC-04**: User can control remote users via index-based OSC addressing (`/remote/{idx}/volume`)
- [ ] **OSC-05**: User can see remote user names update on their control surface when the roster changes
- [ ] **OSC-06**: User can monitor session state (BPM, BPI, beat position, connection status) via OSC
- [ ] **OSC-07**: User can control metronome volume, pan, and mute via OSC
- [ ] **OSC-08**: User can connect/disconnect from a NINJAM server via OSC trigger
- [ ] **OSC-09**: User can see an OSC status indicator in the plugin UI (active/error/off)
- [ ] **OSC-10**: User's OSC configuration persists across DAW sessions
- [ ] **OSC-11**: User can load a shipped TouchOSC template for immediate use with JamWide

### Video Companion

- [ ] **VID-01**: User can launch VDO.Ninja video with one click from the plugin UI
- [ ] **VID-02**: User's video room ID is auto-generated from the NINJAM server address
- [ ] **VID-03**: User hears no duplicate audio from VDO.Ninja (audio suppressed automatically)
- [ ] **VID-04**: User sees all session participants in a video grid layout
- [ ] **VID-05**: User receives a privacy notice about IP exposure before first video use
- [ ] **VID-06**: User is warned if their default browser is not Chromium-based
- [ ] **VID-07**: User can pop out individual participant video into separate windows
- [ ] **VID-08**: User's video buffering syncs to NINJAM interval timing via setBufferDelay
- [ ] **VID-09**: User's video room is secured with a password derived from the NINJAM session
- [ ] **VID-10**: User can see which VDO.Ninja streams map to which NINJAM users (roster discovery)
- [ ] **VID-11**: User can control video features (open, close, mode switch, popout) via OSC
- [ ] **VID-12**: User can select a bandwidth-aware video profile (mobile/balanced/desktop)
- [x] **VID-13**: User's video buffering uses measured latency probe (instamode channel) for accurate audio-video sync, falling back to BPM/BPI calculation when no probe is available

### MIDI Remote Control

- [x] **MIDI-01**: User can map MIDI CC to any mixer parameter (local, remote, master, metronome) with bidirectional feedback and persistent mappings

## v1.3 Requirements — Native Video (Testable Beta)

Requirements for replacing the VDO.Ninja browser companion with a native ffmpeg + JUCE video stack using NinjamZap-compatible H264 wire format and GUID-pairing audio-video sync. Substrate (RawDataSend API + cross-platform LGPL ffmpeg + receive-path dispatch) already landed as Phase 14.3 (2026-05-15). **Beta ships on macOS (arm64 + x86_64 universal) + Windows x86_64, with upstream ninjamzap-server as the recommended reference server.** Linux capture, VDO.Ninja teardown, and a JamWide-owned server fork are deferred.

**Source of truth:** `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (Items C, D, E, F.1–F.4, G, B partial (macOS+Windows only — Linux deferred), reduced I, J = option (c) doc-only; H/K deferred).

### Camera Capture

- [ ] **CAM-01**: User can grant the JamWide standalone application and DAW-hosted plugin (VST3/AU, CLAP best-effort) access to their webcam via the OS permission prompt
- [ ] **CAM-02**: User sees a graceful "Camera unavailable" fallback in the UI (no crash, audio still works) when the DAW host (e.g., REAPER, Live, Bitwig) does not request `com.apple.security.device.camera` for itself
- [ ] **CAM-03**: User sees their own local camera preview rendered in the plugin/standalone UI whenever camera access has been granted

### Codec (Encode + Decode)

- [ ] **COD-01**: User's webcam frames encode to H.264 via vendored Cisco openh264 (libavcodec backend) at the spike-validated baseline of ~98 kbps at 320×240 / 10 fps
- [ ] **COD-02**: User's encoded H.264 frames are chunked into NINJAM upload-interval payloads following the NinjamZap 4 KB-chunk + 4-byte BE length prefix convention
- [ ] **COD-03**: Remote video bytes decode per-peer to a `juce::Image`, with one independent decoder instance per remote user

### Wire Transport (NinjamZap-compatible)

- [ ] **WIRE-01**: User broadcasts video on NINJAM channel index 1 with fourCC `H264` (`MAKE_NJ_FOURCC('H','2','6','4')`), 24-byte interval marker (`[4B BE prefix=20][4B BE swap_count][16B audio_ch0_guid]`), cached SPS/PPS as the second chunk, and per-frame 4-byte BE length-prefix wrapping — bit-for-bit NinjamZap wire-compatible
- [ ] **WIRE-02**: Receiving user's video plays in sync with audio at interval boundaries via the GUID-pairing decision tree: DS-match → 1-swap-defer, PREV-match → play-immediately, no-match → HOLD with `kHoldCapDrop=4` resync
- [ ] **WIRE-03**: Concurrent audio + video producers do not race on `Net_Connection::Send`; the producer call is mediated by SPSC ring or mutex per spike Q3 resolution
- [ ] **WIRE-04**: User can join a NinjamZap-server-hosted room with a NinjamZap mobile (iOS or Android) peer and see the peer's video correctly

### Display (Native Rendering)

- [ ] **DISP-01**: User sees a per-user video tile grid inside the plugin/standalone main view
- [ ] **DISP-02**: User can pop out an individual peer's video into a separate `juce::DocumentWindow` (multi-monitor friendly)
- [ ] **DISP-03**: User can have grid and popouts active simultaneously; popouts survive grid toggling
- [ ] **DISP-04**: User can toggle the grid view on/off without disconnecting from the NINJAM session

### Platform Packaging (macOS + Windows)

- [ ] **PKG-01**: macOS arm64 (Apple Silicon) build runs the native video stack end-to-end in JamWide standalone and at least one DAW-hosted plugin format
- [ ] **PKG-02**: macOS x86_64 (Intel) build runs the same
- [ ] **PKG-03**: macOS universal binary stitches arm64 + x86_64 dylibs via `lipo` and ships as a single signed artifact
- [ ] **PKG-04**: JamWide.entitlements declares `com.apple.security.device.camera`, each vendored ffmpeg dylib is codesigned individually before bundle signing, `install_name_tool` rewrites load paths to `@loader_path/../Frameworks/`, and the final macOS bundle passes `--deep --strict` codesign verification
- [ ] **PKG-05**: CI verifies LGPL discipline (`strings *.dylib | grep -E 'libx264|x264_'` is empty for macOS, equivalent gate for Windows DLLs) and clean dependency check (`otool -L` for macOS, `dumpbin /dependents` for Windows) for every vendored ffmpeg artifact on every shipping architecture
- [ ] **PKG-06**: Windows x86_64 build runs the native video stack end-to-end in JamWide standalone and at least one DAW-hosted plugin format (VST3 minimum)
- [ ] **PKG-07**: Windows installer/bundle ships the vendored ffmpeg DLLs (libavcodec, libavformat, libavutil, libswscale, libopenh264) alongside the plugin/standalone executable with correct load-path resolution; signtool codesigning applied where a code-signing certificate is available

### Reference Server

- [ ] **SRV-01**: JamWide ships `docs/SERVER.md` documenting upstream `ninjamzap-server` as the recommended JamWide reference server: section 1 names `video.ninjamzap.com:2049` as the public shared instance ("Recommended for v1.3 beta — community-operated, no SLA") and explains that beta testers can connect to it via the existing JamWide server browser (manual entry, no new preset infrastructure); section 2 walks through self-hosting `ninjamzap-server` (Docker Compose example for one-command deployment, the minimum required server config `AllowVideoChannels yes` + `PrivateGroupMode N`, latency / privacy rationale for choosing self-host)

### Beta Validation

- [ ] **BETA-01**: Two JamWide standalone users on macOS connect to `video.ninjamzap.com:2049` (or equivalent ninjamzap-server) via the existing server browser, join the same room, and broadcast + receive each other's video for at least 5 minutes with no audio glitches and no decoder freezes
- [ ] **BETA-02**: JamWide plugin loaded in REAPER (macOS) reaches the "Camera unavailable" fallback gracefully per SPARTA Issue #82; audio still works for the remote peers
- [ ] **BETA-03**: JamWide plugin loaded in Logic Pro (macOS) broadcasts video successfully to `video.ninjamzap.com:2049` (or equivalent) — Logic Pro requests camera permission for itself
- [ ] **BETA-04**: At least 20 of the 26 NinjamZap video-sync test scenarios at `/Users/cell/dev/ninjamzap-core/tests/video-sync/scenarios/` are ported to JamWide `tests/` and pass under `./scripts/build.sh --tests` on macOS and Windows
- [ ] **BETA-05**: A macOS user (standalone or DAW-hosted) and a Windows user (standalone or DAW-hosted) both connect to `video.ninjamzap.com:2049` (or equivalent) and successfully broadcast + receive each other's video for at least 5 minutes — cross-platform end-to-end gate
- [ ] **BETA-06**: JamWide standalone on Windows + REAPER VST3 on Windows both reach the camera happy path and broadcast video to `video.ninjamzap.com:2049` (Windows DAW UAT)

## v2 Requirements

Deferred to future release. Tracked but not in current roadmap.

### Advanced Integration

- **ADV-01**: User can see video embedded directly in the plugin window (JUCE WebBrowserComponent)
- **ADV-02**: User can address remote users by name in OSC (`/remote/Dave/volume`)
- **ADV-03**: User can use MIDI controllers for remote control (MIDI-to-parameter mapping)
- **ADV-04**: User can connect to a self-hosted VDO.Ninja signaling server for privacy

## Out of Scope

| Feature | Reason |
|---------|--------|
| Embedded WebView video in plugin window (JUCE WebBrowserComponent) | Adds 50-100MB to plugin, massive build complexity; v1.3 native ffmpeg+JUCE stack is the chosen path |
| Username-based OSC addressing | Names contain special chars, change mid-session, break TouchOSC layouts; index-based is stable |
| OSC auto-discovery (mDNS/Bonjour) | No standard, platform-specific, unreliable; one-time manual config is fine |
| Video recording from plugin | Out of v1.3 scope; OBS handles this well |
| ~~MIDI remote control~~ | ~~7-bit resolution, no bidirectional feedback standard~~ — moved to Phase 14 |
| Embedded TURN server | Only ~10% of users need TURN; was relevant for VDO.Ninja approach (now superseded) |
| ~~H.264-over-NINJAM video (JamTaba approach)~~ | ~~0.03-0.13 FPS at typical BPI; VDO.Ninja WebRTC is 30fps at 100-300ms~~ **REVERSED 2026-05-15** — NinjamZap pivot proves H.264-over-NINJAM works at 10 fps / 320×240 / ~98 kbps / 4% CPU. JamTaba's numbers were a JamTaba-specific implementation limit, not a wire-format limit. See v1.3 above. |
| ~~Real-time video sync with interval audio~~ | ~~NINJAM audio is 8-32s delayed by design; sub-second video sync is fundamentally incompatible~~ **PARTIALLY REVERSED 2026-05-15** — sub-second video sync remains out, but interval-aligned sync via NinjamZap's GUID-pairing decision tree IS in scope as v1.3 WIRE-02 |
| Linux V4L2 webcam capture (send-side) | JUCE `juce_video` has no Linux camera backend; deferred to Item K (post-v1.3, ~1 plan, separate phase) |
| Linux receive-only build | Deferred to v1.3 post-beta or v1.4 — v1.3 beta targets macOS + Windows only |
| ~~Cross-platform vendored ffmpeg (Linux + Windows builds)~~ | ~~macOS-only for the v1.3 beta~~ **REVISED 2026-05-15** — Windows is now in v1.3 beta scope (PKG-06, PKG-07). Linux remains deferred (Item B.2 Linux portion + Item K) |
| VDO.Ninja teardown | Deferred to v1.3 post-beta or v1.4 (Item H); kept operational in parallel until native stack is testable |
| ~~ninjamzap-server fork or upstream contributions (Q8 options (a) and (b))~~ | ~~Vanilla NINJAM is wire-compatible for v1.3 beta; server-side adaptation deferred~~ **REVISED 2026-05-15** — v1.3 commits to upstream ninjamzap-server as the reference server (Q8 = option (c) doc-only, SRV-01). JamWide-owned fork or upstream PRs are deferred to v1.4 if needed. |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| OSC-01 | Phase 9 | Pending |
| OSC-02 | Phase 9 | Pending |
| OSC-03 | Phase 9 | Pending |
| OSC-04 | Phase 10 | Pending |
| OSC-05 | Phase 10 | Pending |
| OSC-06 | Phase 9 | Pending |
| OSC-07 | Phase 9 | Pending |
| OSC-08 | Phase 10 | Pending |
| OSC-09 | Phase 9 | Pending |
| OSC-10 | Phase 9 | Pending |
| OSC-11 | Phase 10 | Pending |
| VID-01 | Phase 11 | Pending |
| VID-02 | Phase 11 | Pending |
| VID-03 | Phase 11 | Pending |
| VID-04 | Phase 11 | Pending |
| VID-05 | Phase 11 | Pending |
| VID-06 | Phase 11 | Pending |
| VID-07 | Phase 13 | Pending |
| VID-08 | Phase 12 | Pending |
| VID-09 | Phase 12 | Pending |
| VID-10 | Phase 12 | Pending |
| VID-11 | Phase 13 | Pending |
| VID-12 | Phase 12 | Pending |
| VID-13 | Phase 14.2 | Complete |
| MIDI-01 | Phase 14 | Complete |
| CAM-01 | Phase 19 | Pending |
| CAM-02 | Phase 19 | Pending |
| CAM-03 | Phase 19 | Pending |
| COD-01 | Phase 20 | Pending |
| COD-02 | Phase 20 | Pending |
| COD-03 | Phase 21 | Pending |
| WIRE-01 | Phase 20 | Pending |
| WIRE-02 | Phase 21 | Pending |
| WIRE-03 | Phase 20 | Pending |
| WIRE-04 | Phase 24 | Pending |
| DISP-01 | Phase 22 | Pending |
| DISP-02 | Phase 22 | Pending |
| DISP-03 | Phase 22 | Pending |
| DISP-04 | Phase 22 | Pending |
| PKG-01 | Phase 23 | Pending |
| PKG-02 | Phase 23 | Pending |
| PKG-03 | Phase 23 | Pending |
| PKG-04 | Phase 19 (entitlements) + Phase 23 (codesign) | Pending |
| PKG-05 | Phase 23 | Pending |
| BETA-01 | Phase 24 | Pending |
| BETA-02 | Phase 24 | Pending |
| BETA-03 | Phase 24 | Pending |
| BETA-04 | Phase 24 | Pending |
| PKG-06 | Phase 23 | Pending |
| PKG-07 | Phase 23 | Pending |
| SRV-01 | Phase 24 | Pending |
| BETA-05 | Phase 24 | Pending |
| BETA-06 | Phase 24 | Pending |

**Coverage:**
- v1.1 requirements: 25 total — all mapped
- v1.3 requirements: **28 total** — 28 mapped, 0 pending (SRV-02 preset-entry requirement removed per user 2026-05-15 — existing JamWide server browser already supports manual entry of `video.ninjamzap.com:2049`, no UI work needed)

---
*Requirements defined: 2026-04-05 (v1.1)*
*Last updated: 2026-05-15 (revision pass 3, final) — SRV-02 removed per user clarification (existing JamWide server browser handles manual entry of `video.ninjamzap.com:2049`; no preset-entry UI work needed); BETA-01/05 wording adjusted to drop "via the SRV-02 preset" references. Final coverage: 28/28 v1.3 requirements mapped, 0 pending.*
