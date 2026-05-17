# Roadmap: JamWide

## Milestones

- ✅ **v1.0 MVP** -- Phases 1-8 (shipped 2026-04-05) -- see `milestones/v1.0-ROADMAP.md`
- 🚧 **v1.1 OSC + Video** -- Phases 9-14.3 (in progress)
- 📋 **v1.2 Security & Quality** -- Phases 15-18 (planned)
- 🚧 **v1.3 Native Video** -- Phases 19-24 (in progress; substrate Phase 14.3 complete 2026-05-15)
- 📋 **v2.0 Codec & Transport Redesign** -- (planned)

## Phases

<details>
<summary>v1.0 MVP (Phases 1-8) -- SHIPPED 2026-04-05</summary>

- [x] Phase 1: FLAC Lossless Codec (3/3 plans) -- completed 2026-03-07
- [x] Phase 2: JUCE Scaffolding (2/2 plans) -- completed 2026-03-07
- [x] Phase 3: NJClient Audio Bridge (2/2 plans) -- completed 2026-03-07
- [x] Phase 4: Core UI Panels (4/4 plans) -- completed 2026-04-04
- [x] Phase 5: Mixer UI and Channel Controls (4/4 plans) -- completed 2026-04-04
- [x] Phase 6: Multichannel Output Routing (2/2 plans) -- completed 2026-04-04
- [x] Phase 7: DAW Sync and Session Polish (3/3 plans) -- completed 2026-04-05
- [x] Phase 8: JUCE Integration Polish (1/1 plan) -- completed 2026-04-05

</details>

### v1.1 OSC + Video (In Progress)

**Milestone Goal:** Add remote control via OSC and video collaboration via VDO.Ninja companion, expanding JamWide from audio-only to a full visual jam experience.

- [ ] **Phase 9: OSC Server Core** -- Bidirectional OSC with parameter mapping, config persistence, and session telemetry
- [x] **Phase 10: OSC Remote Users and Template** -- Index-based remote user control, roster broadcasts, connect/disconnect, shipped TouchOSC template (completed 2026-04-07)
- [x] **Phase 11: Video Companion Foundation** -- One-click VDO.Ninja launch with auto room ID, companion page, local WebSocket server, safety notices (completed 2026-04-07)
- [x] **Phase 12: Video Sync and Roster Discovery** -- Interval-synced buffering, VDO.Ninja API roster mapping, room security, bandwidth profiles (completed 2026-04-07)
- [ ] **Phase 12.1: Video-Audio Sync Fix** -- Fix setBufferDelay pipeline + manual delay slider for video-audio sync
- [x] **Phase 13: Video Display Modes and OSC Integration** -- Per-user popout windows, OSC video control, grid/popout mode switching (completed 2026-04-07)
- [x] **Phase 14: MIDI Remote Control** -- MIDI CC mapping for mixer parameters including remote channels, bidirectional feedback where possible (completed 2026-04-15)
- [ ] **Phase 14.1: Audio Prelisten** -- Listen button in server browser to hear room audio before joining, NJClient receive-only mode
- [x] **Phase 14.2: Instamode Video Sync** -- Latency-probed video buffering using NINJAM instamode channel for accurate audio-video alignment (completed 2026-04-16)
- [x] **Phase 14.3: Native Video Foundation** -- Foundational scaffolding for the future native ffmpeg+JUCE video stack (NinjamZap-compatible H264 wire format): re-vendor LGPL ffmpeg cross-platform, add codec-agnostic `RawDataSendBegin/Write` API + `RawDataCallback` receive scaffolding to NJClient. Additive only — does NOT replace VDO.Ninja; existing video stack stays operational. Unblocks the v1.3 Native Video milestone. (completed 2026-05-15)

## Phase Details

### Phase 9: OSC Server Core

**Goal**: Users can control all local mixer parameters from a TouchOSC surface with real-time bidirectional feedback
**Depends on**: Phase 8 (v1.0 complete)
**Requirements**: OSC-01, OSC-02, OSC-03, OSC-06, OSC-07, OSC-09, OSC-10
**Success Criteria** (what must be TRUE):

  1. User can move a fader in TouchOSC and see the corresponding volume/pan change in JamWide within 100ms
  2. User can move a fader in JamWide and see the TouchOSC fader update to match (no feedback oscillation)
  3. User can configure OSC send/receive ports and target IP in a settings dialog, and those settings persist across DAW sessions
  4. User can observe BPM, BPI, beat position, and connection status updating live on their TouchOSC layout
  5. User can see an OSC status indicator in the plugin footer showing active, error, or off state

**Plans**: 2 plans
Plans:

- [x] 09-01-PLAN.md — OSC server engine: juce_osc linkage, OscAddressMap, OscServer (bidirectional OSC, dirty-flag sender, echo suppression, telemetry, VU)
- [x] 09-02-PLAN.md — OSC UI and persistence: OscStatusDot, OscConfigDialog, ConnectionBar integration, state version 2

**UI hint**: yes

### Phase 10: OSC Remote Users and Template

**Goal**: Users can control remote participants via stable index-based OSC addressing and get started instantly with a shipped TouchOSC template
**Depends on**: Phase 9
**Requirements**: OSC-04, OSC-05, OSC-08, OSC-11
**Success Criteria** (what must be TRUE):

  1. User can control a remote participant's volume/pan/mute/solo via `/JamWide/remote/{idx}/volume` (and similar) OSC addresses
  2. User's TouchOSC layout updates with correct usernames when participants join or leave the session
  3. User can connect to and disconnect from a NINJAM server by sending an OSC trigger message
  4. User can import the shipped `.tosc` template into TouchOSC and immediately control JamWide without manual layout creation

**Plans**: 2 plans
Plans:

- [x] 10-01-PLAN.md — Remote user OSC send/receive: extend RemoteUserInfo snapshot, dynamic address generation, prefix-based dispatch, roster broadcast, connect/disconnect triggers, docs/osc.md update
- [x] 10-02-PLAN.md — TouchOSC template: generate and ship JamWide.tosc with 8 remote slots, local channels, master, metronome, session info, verification checkpoint

### Phase 11: Video Companion Foundation

**Goal**: Users can launch video collaboration with one click and see all session participants in a browser-based grid
**Depends on**: Phase 8 (v1.0 complete; independent of OSC phases)
**Requirements**: VID-01, VID-02, VID-03, VID-04, VID-05, VID-06
**Success Criteria** (what must be TRUE):

  1. User can click a single button in JamWide and see a browser tab open showing a VDO.Ninja video grid of all session participants
  2. User hears no duplicate audio from the video companion (VDO.Ninja audio is suppressed)
  3. User's video room ID is automatically derived from the NINJAM server address (no manual room setup)
  4. User sees a privacy notice about IP exposure before their first video use, and a warning if their default browser is not Chromium-based

**Plans**: 3 plans
Plans:

- [x] 11-01-PLAN.md -- VideoCompanion core: IXWebSocket dependency, WebSocket server, room ID derivation (SHA-1), username sanitization, config/roster JSON protocol
- [x] 11-02-PLAN.md -- Web companion page: Vite/TypeScript project in docs/video/, VDO.Ninja iframe with &noaudio, branded UI, WebSocket client, connection status
- [x] 11-03-PLAN.md -- Video button + privacy modal: ConnectionBar integration, VideoPrivacyDialog, BrowserDetect, processor/editor wiring, human verification checkpoint

**UI hint**: yes

### Phase 12: Video Sync and Roster Discovery

**Goal**: Users experience video buffering synced to NINJAM timing with automatic participant discovery and room security
**Depends on**: Phase 11
**Requirements**: VID-08, VID-09, VID-10, VID-12
**Success Criteria** (what must be TRUE):

  1. User's video streams buffer according to the current NINJAM interval timing (setBufferDelay matches BPM/BPI)
  2. User can see which VDO.Ninja video streams correspond to which NINJAM usernames in the companion page
  3. User's video room is automatically secured with a password derived from the NINJAM session (unauthorized viewers cannot join)
  4. User can select a bandwidth-aware video profile (mobile/balanced/desktop) and see the quality change accordingly

**Plans**: 2 plans
Plans:

- [x] 12-01-PLAN.md — C++ plugin: juce_cryptography linkage, buffer delay calculation+broadcast, SHA-256 room hash derivation, companion URL hash fragment, BPM/BPI event wiring
- [x] 12-02-PLAN.md — Companion page: Vitest setup, BufferDelayMessage type, URL builder (chunked/quality/hash), bandwidth dropdown, roster name label strip, buffer delay relay, all tests

### Phase 12.1: Video-Audio Sync Fix

**Goal**: Receiving participants see video delayed to match NINJAM audio timing, with a manual slider override for fine-tuning
**Depends on**: Phase 12
**Requirements**: VID-08 (fix)
**Success Criteria** (what must be TRUE):

  1. Receiving participant sees video and hears audio within ~1s of each other (not 8s gap)
  2. Companion page footer shows current buffer delay and auto/manual mode
  3. User can override auto delay with a manual slider (0-30s, 500ms steps)
  4. Console diagnostics trace the full setBufferDelay pipeline

**Plans**: 1 plan
Plans:

- [x] 12.1-01-PLAN.md — Fix setBufferDelay pipeline (URL param, onload re-send, logging) + manual delay slider with auto/manual toggle (completed 2026-04-13)

### Phase 13: Video Display Modes and OSC Integration

**Goal**: Users can pop out individual participant video into separate windows and control all video features from their OSC surface
**Depends on**: Phase 10, Phase 12
**Requirements**: VID-07, VID-11
**Success Criteria** (what must be TRUE):

  1. User can pop out an individual participant's video into a separate browser window (multi-monitor support)
  2. User can open video, close video, switch display modes, and trigger popouts via OSC commands from their control surface

**Plans**: 2 plans
Plans:

- [x] 13-01-PLAN.md — Companion popout windows: popout.html page, popout.ts entry point, URL builder &view= extension, roster pill click-to-popout, window tracking Map, postMessage roster relay, deactivate handler, Vite multi-page build, tests
- [x] 13-02-PLAN.md — C++ OSC video control + TouchOSC: VideoCompanion requestPopout + deactivate broadcast + cached roster, OscServer /video/active + /video/popout/{idx} dispatch + feedback, TouchOSC VIDEO section, docs/osc.md update

**UI hint**: yes

### Phase 14: MIDI Remote Control

**Goal**: Users can control all mixer parameters including remote channels via MIDI CC, with bidirectional feedback to motorized controllers
**Depends on**: Phase 10
**Requirements**: MIDI-01
**Success Criteria** (what must be TRUE):

  1. User can map MIDI CC messages to any mixer parameter (local, remote, master, metronome)
  2. User can control remote participant volume/pan/mute via MIDI controller
  3. Parameter changes in JamWide send MIDI CC feedback to the controller
  4. MIDI mappings persist across DAW sessions

**Plans**: 3 plans
Plans:

- [x] 14-01-PLAN.md -- MIDI mapper core: 69 APVTS params, MidiMapper CC dispatch + per-mapping echo suppression + 20ms centralized APVTS-to-NJClient bridge, MidiLearnManager, state version 3, 15 unit tests
- [x] 14-02-PLAN.md -- MIDI Learn UX: right-click menus + config dialog Learn button (host fallback), visual feedback, MidiConfigDialog (slot-labeled mapping table + Range column + standalone device selector), MidiStatusDot 4-state footer indicator
- [x] 14-03-PLAN.md -- Centralized remote state: refactor OscServer + ChannelStripArea to APVTS-only for remote group controls (no direct cmd_queue), eliminating double dispatch

**UI hint**: yes

### Phase 14.1: Audio Prelisten

**Goal**: Users can hear what's happening in a NINJAM server room before joining, via a Listen button in the server browser
**Depends on**: Phase 8 (v1.0 complete, server browser exists)
**Requirements**: BROWSE-01
**Success Criteria** (what must be TRUE):

  1. User can click a Listen button on a populated server row and hear the room's audio through their output
  2. User can stop listening or switch to a different room with one click
  3. User can adjust prelisten volume via a slider in the browser title bar
  4. Closing the server browser stops any active prelisten
  5. Listen buttons are disabled when already connected to a session

**Plans**: 2 plans
Plans:

- [x] 14.1-01-PLAN.md — Audio prelisten backend: PrelistenCommand/StopPrelistenCommand types, PrelistenStateEvent with status enum and host+port, processor atomics, NinjamRunThread DeleteLocalChannel cleanup + lastUsername + auto-accept license, channel strip/connection bar suppression
- [ ] 14.1-02-PLAN.md — Audio prelisten UI and editor wiring: Listen/Stop button per row, volume slider, host+port based active row identity (survives refresh), CONNECTING/LISTENING states, editor command/event wiring, connection bar suppression, session guard, human verification checkpoint

**UI hint**: yes

### Phase 14.2: Instamode Video Sync

**Goal**: Remote video is accurately synced to interval-buffered audio using a visible Instatalk channel (flags=0x02) that doubles as push-to-talk voice talkback and latency probe, replacing theoretical BPM/BPI calculation with real network measurement
**Depends on**: Phase 12.1 (video-audio sync fix)
**Requirements**: VID-13
**Success Criteria** (what must be TRUE):

  1. Plugin opens a visible instamode channel named "Instatalk" (flags=0x02) on normal connect that other users see in their mixer
  2. Instatalk channel sends silence when PTT is inactive, voice audio when PTT is active (dual purpose: voice talkback + latency probe)
  3. Receiver measures actual audio delay by comparing wall-clock timestamps: instamode arrival vs interval audio playback for the same remote user
  4. Measured delay is sent to companion page via existing bufferDelay WebSocket message with syncMode:"measured" field
  5. Companion page footer shows active sync mode: "Sync: measured (Xms)" or "Sync: calculated (Xms)" or "Sync: manual (Xms)"
  6. Companion page shows syncing overlay that fades out after buffer fills when measured delay arrives
  7. Three-tier delay priority: manual slider (highest) > measured probe (middle) > BPM/BPI calculation (lowest)
  8. Falls back to BPM/BPI calculation if no instamode measurement is available (no remote JamWide users with Instatalk)

**Plans**: 2 plans
Plans:

- [x] 14.2-01-PLAN.md — Plugin-side: Instatalk channel setup (ch 4, flags=0x02), PTT silence callback, NJClient measurement atomics (t_insta/t_interval hooks in mixInChannel/on_new_interval), RemoteChannelInfo.flags, VideoCompanion broadcastMeasuredDelay + syncMode in broadcastBufferDelay, run-thread polling
- [x] 14.2-02-PLAN.md — Companion-side: BufferDelayMessage syncMode field, three-tier delay display (measured/calculated/manual), syncing overlay with fade-out, updated video-sync tests, new instamode-sync tests, human verification checkpoint

**Reference**: `.planning/references/INSTAMODE-VIDEO-SYNC-DESIGN.md`

### Phase 14.3: Native Video Foundation

**Goal**: JamWide's NJClient gains the codec-agnostic transport scaffolding (RawDataSendBegin/Write + RawDataCallback) and a re-built cross-platform LGPL ffmpeg vendoring tree, so the future v1.3 "Native Video" milestone can land capture/encode/decode/UI on a stable foundation without further plumbing churn.
**Depends on**: Phase 14.2 (current video pipeline is operational; this phase is purely additive — no changes to existing video stack)
**Reference**: `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/` (full design, spike evidence, NinjamZap addendum, deferred-items milestone scope)
**Success Criteria** (what must be TRUE):

  1. `libs/ffmpeg/macos-{arm64,x86_64}/`, `libs/ffmpeg/linux-x86_64/`, `libs/ffmpeg/windows-x86_64/` are populated with LGPL-only dylibs/so/dll for {libavcodec, libavformat, libavutil, libswscale, libopenh264}; `scripts/build_ffmpeg_lgpl.sh` produces them reproducibly with `--disable-gpl --disable-libx264 --enable-libopenh264 --disable-xlib --disable-libxcb --disable-sdl2`
  2. CI step verifies vendored dylibs are not GPL-tainted (`strings *.dylib | grep -E 'libx264|x264_'` returns empty for every vendored lib on every platform)
  3. CI step verifies vendored dylibs have no spurious system-path dependencies on macOS (`otool -L` shows only `@rpath`, `/usr/lib`, `/System` entries)
  4. NJClient gains a public `RawDataSendBegin(outGuid, fourcc, chidx, estsize)` + `RawDataSendWrite(guid, data, len, isEnd)` API that any caller can use to upload an opaque-byte stream on a NINJAM channel, mirroring `ninjamzap-core/njclient.cpp:2047-2065`
  5. NJClient gains a public `RawDataCallback` (delivered with `eventType ∈ {begin, data, end}`, plus `guid`, `fourcc`, `username`, `chidx`, `data`, `dataLen`) so future callers can subscribe to non-OGGv channels mirroring `ninjamzap-core/njclient.h:205-212`
  6. The receive path at `src/core/njclient.cpp:2148` no longer routes unknown-fourCC streams into the Vorbis decoder by default — instead it dispatches to `RawDataCallback` if registered, else logs and discards the bytes (no silent corruption — RESEARCH §6 documents the original behavior as a wire-compatibility BUG)
  7. The existing VDO.Ninja video pipeline (`juce/video/VideoCompanion.{h,cpp}`, `companion/`) is BYTE-IDENTICAL after this phase — no behavioral change to existing video
  8. Tests cover: RawData send roundtrip (begin → write → end → recipient gets all three callback events with correct payload), unknown-fourCC receive isolation (Vorbis decoder NOT entered), each platform's vendored ffmpeg loads + `avcodec_find_encoder_by_name("libopenh264") != nullptr`

**Plans**: 3 plans
Plans:

- [x] 14.3-01-PLAN.md — Cross-platform LGPL ffmpeg + openh264 vendoring (re-run + extend the spike's `scripts/build_ffmpeg_lgpl.sh` to produce macOS arm64+x86_64, Linux x86_64, Windows x86_64; lipo macOS into universal; CI gates for LGPL discipline + clean otool output)
- [x] 14.3-02-PLAN.md — `RawDataSendBegin/Write` + `RawDataDownloadTracker` + `RawDataCallback` API (port from `ninjamzap-core/njclient.cpp:2047-2123` + `njclient.h:205-236`; queue + drain pattern matching JamWide's existing audio upload pattern; thread-safety for non-audio callers per spike Q3 finding)
- [x] 14.3-03-PLAN.md — Receive-path dispatch fix: at `src/core/njclient.cpp:2148`, route unknown fourCC to `RawDataCallback` if registered (else log + discard); audit `start_decode` callers; tests covering OGGv unchanged + arbitrary fourCC isolated; `is_video_fourcc` helper for {`H264`, `VP8 `, `MJPG`}

**UI hint**: no (this phase is non-UI scaffolding only — the actual UI work is in v1.3)

### v1.2 Security & Quality (Planned)

**Milestone Goal:** Harden JamWide with connection encryption, modern Opus codec, resilient networking, and production-grade testing infrastructure.

- [x] **Phase 15: Connection Encryption** -- AES-256-CBC end-to-end encryption for credentials and audio, backward-compatible with unencrypted NINJAM servers (completed 2026-04-11)
- [ ] **Phase 15.1: RT-Safety Hardening** -- Audit and remove mutex acquisitions, heap deallocations, and file I/O from the JUCE audio callback path; SPSC-queue migration of NJClient audio-thread state (proactive; symptomatic CPU spike already mitigated in 14.2)
- [ ] **Phase 16: Opus Codec Integration** -- Native libopus with automatic bitrate adaptation, packet loss concealment, and mixed-codec capability negotiation
- [ ] **Phase 17: Network Resilience** -- Exponential backoff reconnection (1s-30s), per-peer adaptive jitter buffer, graceful degradation on network loss
- [ ] **Phase 18: Testing Infrastructure** -- Stress tests (1000x create/destroy, 10 concurrent instances), documented shutdown sequence, CI-gated test pipeline

## Phase Details (v1.2)

### Phase 15: Connection Encryption

**Goal**: Users' credentials and audio are encrypted in transit when connecting with a session password, while maintaining backward compatibility with legacy NINJAM servers
**Depends on**: Phase 8 (v1.0 complete)
**Requirements**: SEC-01, SEC-02, SEC-03
**Success Criteria** (what must be TRUE):

  1. User connecting with a password has their credentials encrypted via AES-256-CBC (SHA-256 key derivation from password)
  2. User's audio stream is encrypted end-to-end when a session password is set
  3. User can still connect to legacy NINJAM servers without encryption (graceful fallback)
  4. Encryption is transparent — no extra configuration required beyond the existing password field

**Plans**: 2 plans
Plans:

- [x] 15-01-PLAN.md — Crypto module (TDD): nj_crypto.h/.cpp with AES-256-CBC encrypt/decrypt via OpenSSL EVP, SHA-256 key derivation, test-only IV injection API, 15+ unit tests (round-trip, known-vector, size overhead, zero-length, tamper), CMake OpenSSL linkage
- [x] 15-02-PLAN.md — Protocol integration: redesigned auth flow (server advertises encryption in AUTH_CHALLENGE, client encrypts AUTH_USER credentials), Net_Connection encrypt-on-send/decrypt-on-receive hooks, downgrade detection, CI OpenSSL setup for all platforms

**Reference**: AES-256-CBC with OpenSSL EVP, SHA-256 key derivation from password, random IV per message, server_caps/client_caps/flag capability negotiation

### Phase 15.1: RT-Safety Hardening

**Goal**: The JUCE audio callback path is free of mutex acquisitions, heap deallocations, and file I/O — eliminating the latent CPU-spike-on-interval class of bug at the architectural level
**Depends on**: Phase 15 (encryption stable; landed before audio-path refactor)
**Scope**: Audit `processBlock` → `AudioProc` → `on_new_interval` / `process_samples` / `mixInChannel`; remove all CRITICAL / HIGH RT-safety violations the `realtime-audio-reviewer` agent reports.
**Success Criteria** (what must be TRUE):

  1. ThreadSanitizer build of the test binaries (`./scripts/build.sh --tests` with `-fsanitize=thread`) passes a NINJAM session simulation with peer churn at interval boundaries.
  2. Re-running the `realtime-audio-reviewer` agent on `src/core/njclient.cpp` and `juce/JamWideJuceProcessor.cpp` reports zero CRITICAL findings on the audio path.
  3. Manual UAT on a populated NINJAM server (3+ peers) shows no audible audio glitches and no CPU spike pattern at the interval period in Activity Monitor's CPU history.
  4. The `m_users_cs` and `m_locchan_cs` mutexes are no longer acquired from `AudioProc` or any function it calls.
  5. `writeLog` / `writeUserChanLog` / the `JAMWIDE_DEV_BUILD` `fopen` block at `njclient.cpp:2133` are removed from the audio path.

**Plans**: 11 plans (15.1-01 audit done as `d2db893`; 15.1-02 through 15.1-10 below — revised post-Codex review, old 15.1-07 split into 07a/07b/07c)
Plans:

- [x] 15.1-01 — Auditor pass (committed `d2db893`; produced `15.1-AUDIT.md` with 21 findings: 12 CRITICAL, 4 HIGH, 3 MEDIUM, 2 LOW)
- [x] 15.1-02-atomic-promotion-PLAN.md — m_misc_cs / BPM / BPI / m_interval_pos atomic promotion (CR-03; documents `m_beatinfo_updated` edge-trigger semantics per L-10) — completed 2026-04-26 (commits ff45da8, 1fbcdc7; SUMMARY at 15.1-02-SUMMARY.md)
- [x] 15.1-03-eliminate-audio-path-logging-PLAN.md — remove writeLog / writeUserChanLog / JAMWIDE_DEV_BUILD fopen from audio path (CR-04, H-01, H-02, L-02) — completed 2026-04-26 (commit bec421e; SUMMARY at 15.1-03-SUMMARY.md). writeUserChanLog body+declaration deleted entirely per Codex per-plan delta; m_debug_logged_remote field also removed; writeLog API survives for non-RT callers.
- [x] 15.1-04-spsc-infrastructure-PLAN.md — SPSC payloads.h (FINAL Wave-0 payloads incl. DecodeArmRequest), `--tsan` flag, JAMWIDE_TSAN option, primitive unit tests, `MAX_BLOCK_SAMPLES` contract — completed 2026-04-26 (commits 8f47965, 812427f, 177297d; SUMMARY at 15.1-04-SUMMARY.md). Codex M-9 / M-7 / HIGH-2 / HIGH-3 all closed; 10/10 SPSC tests pass under TSan with zero ThreadSanitizer reports.
- [x] 15.1-05-deferred-delete-PLAN.md — DecodeState* deferred-delete SPSC for all 7 audio-thread delete sites (CR-05, CR-06, CR-07) + overflow counter accessor — completed 2026-04-26 (commits af184d9, d82dd1c, 1062c92; SUMMARY at 15.1-05-SUMMARY.md). All 7 audit-cited delete sites converted; m_deferred_delete_q SPSC + GetDeferredDeleteOverflowCount accessor wired (Codex M-8 phase-close gate); test_deferred_delete 3/3 PASSED under both Release and TSan with zero ThreadSanitizer reports.
- [x] 15.1-06-locchan-cs-snapshot-PLAN.md — m_locchan_cs replaced with audio-thread mirror + LocalChannelUpdate SPSC + Local_Channel deferred-free with generation gate (CR-02) — completed 2026-04-26 (commits 0eb6914, 3846aa1, a010edc; SUMMARY at 15.1-06-SUMMARY.md). LocalChannelMirror[MAX_LOCAL_CHANNELS] (no Local_Channel* / lc_ptr — Codex HIGH-2 closed) + m_locchan_update_q (SpscRing<LocalChannelUpdate, 32>) + m_audio_drain_generation atomic + m_locchan_deferred_delete_q (Codex HIGH-3 generation-gated free for canonical Local_Channel) all wired; drainLocalChannelUpdates called at top of AudioProc; drainLocalChannelDeferredDelete called from NinjamRunThread::run() in-loop + post-loop. test_local_channel_mirror 5/5 PASSED under both Release and TSan with zero ThreadSanitizer reports. Two deviations: (1) Instatalk PTT cbf preserved via separate m_locchan_processor_q ring (AUDIT H-03 said no callers; juce/NinjamRunThread.cpp:374 actually registers one); (2) VU peak migrated to atomic mirror fields for lock-free GetLocalChannelPeak. UAT approved 2026-04-26 (build 249 VST3 + TSan standalone — populated-server listening test, no audible glitches, zero TSan reports, DeleteLocalChannel under 200 ms gate).
- [ ] 15.1-07a-remote-user-mirror-PLAN.md — m_users_cs mirror + RemoteUser deferred-free with generation gate (CR-01) — has human-verify checkpoint
- [x] 15.1-07b-buffer-queue-PLAN.md — BlockRecord SPSC replacing BufferQueue (CR-09, CR-10) with `<5%` CPU perf-budget acceptance criterion — completed 2026-04-26 (commits dbdaf98, edb2769; SUMMARY at 15.1-07b-SUMMARY.md). 7 audio-thread BufferQueue::AddBlock sites (4 process_samples + 1 m_wavebq + 2 on_new_interval) replaced with SPSC try_push via pushBlockRecord/pushWaveBlockRecord helpers; per-call-site bounds-check (Codex M-7) + m_block_queue_drops counter exposed via GetBlockQueueDropCount() (Codex M-8); broadcast restored end-to-end after 15.1-06 left it dormant. Run-thread bridge: drainBroadcastBlocks/drainWaveBlocks forwards from mirror block_q rings into legacy lc->m_bq.AddBlock for the existing encoder consumer. test_block_queue_spsc 5/5 PASSED under both Release and TSan with zero ThreadSanitizer reports. Build 251 VST3 + Standalone + TSan all green. Plan landed OUT-OF-ORDER ahead of 07a (frontmatter dependency was conservative; 07b operates on LocalChannelMirror which 15.1-06 already provides and does not touch m_users_cs which is 07a's job). **STABILIZATION**: real-DAW UAT exposed three regressions, each fixed and re-UAT'd against build 261: (1) `e631aef` restored on_new_interval `bcast_active` state machine (executor port had dropped legacy state-transition logic, never set `lcm.bcast_active=true` for normal channels → per-block sample push at process_samples:2241 never fired); (2) `1633bfd` guards NJClient::Run encoder loop against null `m_netcon` (Disconnect tears down `m_netcon` before per-channel state, drainBroadcastBlocks is a NEW writer to lc->m_bq from the run thread on every Run() tick → encoder loop derefs null on next tick → DAW crash); (3) `3799e8a` removes `_reinit` `channel_idx` renormalize that was silently overwriting `channel_idx` on the canonical `Local_Channel` during every Disconnect, stranding the audio-thread mirror entry (mirror is indexed BY `channel_idx` — pre-Connect `syncInstatalkBroadcast()` added ch=4, then Connect→Disconnect→`_reinit` renamed to ch=0, mirror[4] kept `.active=true` and mirror[0] stayed `.active=false`, so handleStatusChange's `SetLocalChannelInfo(0,"Ch1")` found existing canonical and published `InfoUpdate`, never `AddedUpdate` → local Ch1-4 VU dead in connected session). **UAT approved 2026-04-27 (build 261)** covering all four checks: local Ch1-4 VU moves with DAW audio playing, peers hear DAW audio, Disconnect doesn't crash, Disconnect→reconnect path works. Throwaway debug instrumentation from `1dd8db3`/`c3420c5`/`953d006` to be removed before 07a dispatch. Architecture-mirror audit owed before 15.1-10: grep ALL writes to `Local_Channel.channel_idx` AND `RemoteUser_Channel.channel_index` and verify each publishes a corresponding mirror update.
- [ ] 15.1-07c-decode-media-buffer-PLAN.md — DecodeMediaBuffer SPSC backend (CR-12)
- [ ] 15.1-08-prealloc-hardening-PLAN.md — tmpblock + decoder Prealloc; SetMaxAudioBlockSize throws (M-01, M-02 reclassify, M-03, CR-11 mitigation)
- [ ] 15.1-09-codec-call-site-integration-PLAN.md — file-reader refill loop closes H-04 in steady state; start_decode arming via run-thread; ds->decode_fp = nullptr invariant on audio thread (CR-08, H-04)
- [ ] 15.1-10-phase-verification-PLAN.md — 7 separated signals: TSan ctest (1a) + automated peer-churn simulation (1b) + manual standalone-callback UAT (1c) + auditor re-run + drop-counter gates + perf-budget + goal-backward greps; produces 15.1-AUDIT-final.md and 15.1-VERIFICATION.md (has UAT human-verify checkpoint)

**Reference**: SPSC queues via `src/threading/spsc_ring.h`, JUCE `AbstractFifo` as fallback. Auditor agent at `.claude/agents/realtime-audio-reviewer.md`. Bug-investigation context in `.planning/phases/15.1-rt-safety-hardening/15.1-CONTEXT.md`. Historically-related fixes in commits `9cd23c0` and `9fa0d32` (Instatalk-specific spike — already shipped, not undone).

### Phase 16: Opus Codec Integration

**Goal**: Users get low-latency, high-quality audio with automatic bitrate adaptation and packet loss resilience
**Depends on**: Phase 1 (FLAC codec pattern)
**Requirements**: COD-01, COD-02, COD-03
**Success Criteria** (what must be TRUE):

  1. User can select Opus as codec, achieving lower latency than Vorbis with comparable quality
  2. User experiences smooth audio despite occasional packet loss (Opus PLC fills gaps)
  3. User in a mixed session (some peers on Opus, others on Vorbis/FLAC) hears all participants correctly
  4. Opus bitrate adapts automatically based on connection quality

**Plans**: 2 plans
**Reference**: libopus v1.5.2 at 48kHz, OPUS_APPLICATION_AUDIO mode, per-peer decoder instances

### Phase 17: Network Resilience

**Goal**: Users maintain stable sessions through transient network interruptions with minimal audio disruption
**Depends on**: Phase 3 (NJClient audio bridge)
**Requirements**: NET-01, NET-02
**Success Criteria** (what must be TRUE):

  1. User's connection automatically retries with exponential backoff (1s, 2s, 4s... up to 30s) after disconnection
  2. User hears smooth audio from each remote peer despite network jitter (per-peer adaptive buffer)
  3. User sees reconnection status in the UI during retry attempts
  4. Reconnection preserves session state (codec selection, mixer settings) when possible

**Plans**: 2 plans
**Reference**: 1s-30s exponential backoff with idle cadence; 20ms pre-fill jitter buffer per peer

### Phase 18: Testing Infrastructure

**Goal**: Plugin reliability is validated by automated stress and integration tests before every release
**Depends on**: All prior phases (runs against full codebase)
**Requirements**: QA-01, QA-02, QA-03
**Success Criteria** (what must be TRUE):

  1. Plugin survives 1000x rapid create/destroy cycles without crash or memory leak
  2. 10 concurrent plugin instances in one DAW operate without interference
  3. Thread shutdown sequence is documented and follows multi-phase pattern (no DAW state-save timeouts)
  4. CI pipeline runs full test suite and blocks release on failure

**Plans**: 2 plans
**Reference**: Integration tests (load/unload, activate, process audio), stress tests (rapid create/destroy, concurrent instances, memory leak), fuzz tests; CI-gated with binary scanning

### v1.3 Native Video (In Progress)

**Milestone Goal:** Replace the VDO.Ninja browser companion with a native in-app/in-plugin video stack using NinjamZap-compatible H264 wire format and GUID-pairing audio-video sync. Reach a testable beta on **macOS + Windows** (Apple Silicon + Intel + Windows x86_64), backed by upstream **ninjamzap-server** as the recommended reference server (per-room threading + two-pass audio-priority + per-subscriber video congestion drop). The substrate (codec-agnostic RawData transport API, NinjamZap-compatible receive-path dispatch, vendored LGPL ffmpeg + Cisco openh264 across macOS + Linux + Windows) already landed in Phase 14.3 (completed 2026-05-15). Beta scope is macOS + Windows with `docs/SERVER.md` framed in two sections: section 1 names the public `video.ninjamzap.com:2049` ninjamzap-server (recommended for the v1.3 beta — community-operated, no SLA — beta testers manually enter the address into JamWide's existing untouched NINJAM server browser); section 2 walks through self-hosting upstream ninjamzap-server (Docker Compose example, version pin) for users who prefer their own latency/privacy guarantees. Linux full client (capture + receive), VDO.Ninja teardown, a JamWide-owned ninjamzap-server fork, and the full per-DAW UAT matrix are deferred to v1.3 post-beta or v1.4.

**Source of truth:** `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (Items C, D, E, F.1–F.4, G, B partial (macOS arm64+x86_64 universal + Windows x86_64; Linux deferred), reduced I, J = option (c) doc-only; H/K deferred to post-beta).

- [x] **Phase 19: Camera Capture & Permission UX** -- JUCE CameraDevice integration in standalone + DAW-hosted plugin, camera entitlement, local preview, per-DAW permission-denial fallback dialog (completed 2026-05-16)
- [x] **Phase 20: H.264 Encoder & Send Pipeline** -- Port JamTaba's FFMpegMuxer to JamWide via openh264 + libavcodec, wire through RawDataSendBegin/Write substrate, implement NinjamZap sender state machine (24-byte interval marker, SPS/PPS chunk, 4-byte BE length prefix per frame, `Net_Connection::Send` thread-safety mitigation) **(completed 2026-05-17; visually validated against canonical NinjamZap web viewer on `video.ninjamzap.com:2049` after 6-fix UAT-driven diagnostic session; perf/TSan/teardown formal re-runs scheduled into Plan 24-01)**
- [ ] **Phase 21: H.264 Decoder & Receive Pipeline** -- Port JamTaba's FFMpegDemuxer to JamWide, implement NinjamZap's 4-stage receive pipeline (`accumulating → next → pending → playing`) with per-stream WRITE-time accumulation, marker parse, and GUID-pairing decision tree (DS/PREV/no-match with `kHoldCapDrop=4`)
- [ ] **Phase 22: Native Video UI (Grid + Popouts)** -- Per-user `juce::Image` display tile, grid layout in main view, `juce::DocumentWindow` popout per user, hide/show toggle, grid + popouts active simultaneously
- [ ] **Phase 23: macOS Universal + Windows Build & Codesign** -- Reproducible macOS arm64 + x86_64 universal binary (lipo + per-dylib codesign + `install_name_tool` Frameworks-path rewriting + camera entitlement) AND Windows x86_64 build (bundled ffmpeg DLLs with correct load-path resolution + signtool codesigning where applicable); CI lanes on both platforms with LGPL discipline gates (`otool -L` on macOS, `dumpbin /dependents` on Windows)
- [ ] **Phase 24: Beta Validation, Server Docs & Per-DAW UAT** -- Ship `docs/SERVER.md` (section 1: public `video.ninjamzap.com:2049` recommended path, manual entry via existing server browser; section 2: self-host with Docker Compose + version pin), port ≥20 of 26 NinjamZap video-sync test scenarios cross-platform (macOS + Windows), manual UAT on macOS standalone + REAPER (fallback expected) + Logic Pro (camera grant expected) + Windows standalone + Windows REAPER VST3, cross-platform macOS↔Windows interop on `video.ninjamzap.com:2049`, NinjamZap mobile interop check, document beta release notes and known issues

## Phase Details (v1.3)

### Phase 19: Camera Capture & Permission UX

**Goal**: Users can grant camera access in JamWide standalone and DAW-hosted plugin and see their local preview rendered on both macOS and Windows, with a graceful fallback when the DAW host does not request camera permission for itself
**Depends on**: Phase 14.3 (Native Video Foundation — RawData transport API + ffmpeg vendoring substrate complete on macOS + Linux + Windows)
**Reference**: `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (Item C — JUCE CameraDevice integration; Item G partial — entitlements + plugin plumbing). Files: `juce/JamWideJuceProcessor.{h,cpp}`, `juce/ui/ConnectionBar.cpp:206-217,512-528,644-650`, `JamWide.entitlements`, `libs/juce/modules/juce_video/capture/juce_CameraDevice.h`. **Cross-platform backends:** macOS uses `juce_CameraDevice_mac.mm` (AVFoundation) and Windows uses `juce_CameraDevice_windows.h` (Media Foundation / DirectShow). Spike Risk #2 (`CameraDevice::openDevice` returns non-null even when TCC denies frames on macOS) — needs `AVCaptureDevice authorizationStatusForMediaType` pre-check + watchdog timer. **Note:** the REAPER permission-denial fallback is macOS-specific (SPARTA Issue #82 is a macOS TCC/entitlement issue); on Windows, REAPER does not have the same camera-permission constraint and the camera happy path applies.
**Requirements**: CAM-01, CAM-02, CAM-03, PKG-04 (entitlements portion)
**Success Criteria** (what must be TRUE):

  1. User can launch JamWide standalone on macOS, see the OS camera permission prompt, grant it, and see their own webcam preview in the plugin UI within 3 seconds
  2. User can load JamWide as an AU plugin in Logic Pro (which requests `com.apple.security.device.camera` for itself), grant permission via the host, and see the local camera preview
  3. User loading JamWide as a VST3 plugin in REAPER on macOS (which does NOT request the camera entitlement per SPARTA Issue #82) sees a graceful "Camera unavailable" dialog with text explaining the host limitation — no crash, no silent freeze, audio still works for the session
  4. User can revoke camera permission via macOS System Settings without crashing JamWide; preview disappears and the fallback UI appears
  5. User can launch JamWide standalone on Windows x86_64, see Windows' camera permission prompt (if applicable per Windows version), grant access, and see their own webcam preview in the UI within 3 seconds (SPARTA Issue #82 does not apply on Windows)

**Plans**: 3 plans
Plans:

- [ ] 19-01-PLAN-capture-pipeline.md — JUCE_USE_CAMERA wiring (Risk E), camera entitlement + Info.plist (PKG-04 entitlements portion), CameraAuthorization TCC pre-check shim (D-03, closes Spike Risk #2), JamWideFrameDistributor (D-02, D-04), JamWideCameraDevice with 7-state machine + retry-backoff worker + watchdog (D-09/12/20), JamWideJuceProcessor ownership wiring, 3 Wave 0 tests (frame_distributor, camera_state_machine, camera_retry_backoff)
- [ ] 19-02-PLAN-ui-and-persistence.md — ConnectionBar Camera button + right-click PopupMenu quality preset (D-06, D-19), CameraPreviewWindow + Tile (juce::DocumentWindow popout with JamWideLookAndFeel chrome, 4:3 aspect, hide-not-destroy on close, D-05/07/08/09), NativeCameraPrivacyDialog (D-22), JamWideJuceEditor FallbackListener wiring, plugin state schema v3→v4 with seven flat camera properties + clamping (D-24/D-25, T-19-03 mitigation), 1 Wave 0 test (test_plugin_state_v3_v4)
- [ ] 19-03-PLAN-fallback-and-verification.md — CameraStatusDialog cause-aware fallback dialog covering 5 causes × 2 platforms (D-13/14/15/16, SPARTA #82 mitigation), platform-conditional deep-links (macOS x-apple.systempreferences URL + Windows ms-settings:privacy-webcam URL), VDO.Ninja coexistence soft warning toast (D-27), license-header sanity check (Risk C — JUCE seat licence compatibility per RESEARCH §13), scripts/verify_camera_entitlement.sh (T-19-05, D-28 verification), docs/UAT/phase-19-camera-uat-checklist.md (9 cells per VALIDATION.md, feedback_uat_scope_redflags compliance), test_camera_cause_mapping unit test (5×2 = 10 cells), CHANGELOG.md entry (D-26)

**UI hint**: yes

### Phase 20: H.264 Encoder & Send Pipeline

**Goal**: Users' webcam frames encode to H.264 via openh264 and broadcast as NinjamZap-compatible video intervals on channel 1, bit-for-bit wire-identical to NinjamZap mobile and the ninjamzap-core reference
**Depends on**: Phase 19 (camera frame source available; entitlements wired)
**Reference**: `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (Item D — port `JamTaba/src/Common/video/FFMpegMuxer.cpp`; Item F.1 — sender state machine; Item F.4 — `Net_Connection::Send` thread-safety per spike Q3). `260515-0pc-RESEARCH-ADDENDUM.md` "Wire format spec (locked)" — fourCC `H264` = `MAKE_NJ_FOURCC('H','2','6','4')`, 24-byte marker `[4B BE prefix=20][4B BE swap_count][16B audio_ch0_guid]`, SPS/PPS as second chunk, per-frame 4-byte BE length prefix. NinjamZap reference: `ninjamzap-core/njclient.cpp:2047-2123` (send API), `:3041-3082` (interval state machine).
**Requirements**: COD-01, COD-02, WIRE-01, WIRE-03
**Success Criteria** (what must be TRUE):

  1. User sees their video broadcast to a NINJAM session at the spike-validated baseline of 320×240 at 10 fps with measured ~98 kbps average bitrate
  2. A second user (or a ninjamzap-core reference receiver) successfully parses the JamWide broadcast: fourCC reads as `H264`, the first per-interval chunk is exactly 24 bytes and matches the marker spec, SPS/PPS appears as the second chunk, and each subsequent frame chunk carries a correct 4-byte BE length prefix
  3. User can run JamWide with audio broadcast AND video broadcast simultaneously on a populated NINJAM server for 5 minutes with no audio glitches and no `Net_Connection::Send` thread-safety races (verifiable under TSan)
  4. User who toggles camera off mid-session sees the sender emit a clean interval END (no truncated stream); receivers stop seeing new video without error

**Plans**: 4 plans
Plans:

- [ ] 20-00-PLAN-substrate-revision.md — Replace `m_rawdata_sendq` SPSC with NinjamZap-literal `WDL_PtrList<RawDataQueueItem> + WDL_Mutex m_rawdata_cs` (D-19); pop-one-unlock-Send-relock drain; retire overflow counter / Pattern C guard / RAWDATA_SEND_QUEUE_CAPACITY; add observability atomics (`m_rawdata_sendq_high_water_mark` + `m_rawdata_cs_contention_count` + `m_rawdata_sendq_total_enqueues` + accessors); strip residual `writeLog` calls in `RawDataSendBegin/Write`; write D-09 audit-allowlist entries to `realtime-audio-reviewer.md`; rewrite `tests/test_rawdata_send.cpp` (multi-producer + drain interleave + BEGIN/marker/SPS/frame ordering); mark RESEARCH.md stale sections with `<!-- STALE — DO NOT PLAN -->` markers; fix CONTEXT.md drain-semantics text per R3 MF2
- [ ] 20-01-PLAN-video-encoder.md — Abstract `VideoEncoder` pure-virtual interface (D-01); `Openh264Encoder` impl owning its own thread + BGRA→YUV420P via `libswscale` (D-02); H.264 Baseline level 3.1 + `RC_BITRATE_MODE` (D-05/D-06); bitrate ladder Low=100/Medium=300/High=800 kbps (D-16); one IDR per interval via `eForceIntraFrame` triggered by `std::atomic<uint64_t> m_audio_interval_seq` change (D-15); drop-oldest backpressure + `m_encoder_input_drops` counter (D-07); reconfigure = tear-down + rebuild + republish SPS/PPS (D-04); encoder lifecycle starts at broadcast-on (D-13)
- [ ] 20-02-PLAN-video-state-machine.md — `NJClient` video state machine: `on_new_interval` END/BEGIN/marker/SPS-PPS sequence under whole-block `m_video_cs` (D-08); `QueueVideoFrame` acquires `m_video_cs` to read `m_video_active`/`m_video_guid`/`m_video_interval_open` (D-11); `SetVideoSPSPPS` under `m_video_spspps_cs` (D-03); per-channel atomic seqlock on `Local_Channel::m_curwritefile.guid` for the D-20 marker read (R3 MF1 — deterministic, not deferred to TSan); cold-start SPS/PPS handled via NinjamZap-literal `if size > 0` gate, marker-only first interval accepted (R3 MF3 option (b)); 4-byte BE length prefix wrapping in `QueueVideoFrame`; introduce `m_sync_interval_cnt` counter
- [ ] 20-03-PLAN-processor-wiring-and-uat.md — `JamWideJuceProcessor` owns the `VideoEncoder` (constructed on camera-open, encoder thread starts on broadcast-on); `ConnectionBar` Broadcast toggle; `NinjamRunThread` connect-up calls BOTH `SetLocalChannelInfo(1, "video", ..., flags=0x10)` AND `SetVideoChannel(1, H264)` + `NotifyServerOfChannelChange` unconditionally per D-18; UAT harness `tests/uat/phase-20-broadcast-uat.sh` for 5-min 2-peer broadcast at each preset on `video.ninjamzap.com:2049`; high-water < 32 items + contention rate < 1% of total enqueues + drops == 0 acceptance thresholds (R3 MF4); audio-thread budget measurement; TSan dual-scope verification on the broadcast happy path

**Wave structure:**

- Wave 0: 20-00 (substrate foundation; blocks all downstream)
- Wave 1: 20-01 *(blocked on Wave 0 completion)*
- Wave 2: 20-02 *(blocked on Wave 1 completion)*
- Wave 3: 20-03 *(blocked on Wave 2 completion; `autonomous: false` — Task 4 is the 5-min populated-server UAT checkpoint)*

**Cross-cutting constraints (must_haves.truths shared across plans):**

- NinjamZap-literal substrate (D-19): `WDL_PtrList + WDL_Mutex m_rawdata_cs`, pop-one-unlock-Send-relock drain
- Whole-block `m_video_cs` (D-08): audio thread holds across END/BEGIN/marker/SPS-PPS in `on_new_interval`; encoder thread waits on same mutex
- Expanded Phase 15.1 audit-allowlist envelope (D-09): mutex acquisitions + RNG + heap-alloc/Resize + memcpy + canonical Local_Channel read; no `writeLog` on audio-thread video path
- Phase 15.1-06 HIGH-2 carve-out (D-20): single-field-single-site `m_curwritefile.guid` read on audio thread, deterministic via per-channel seqlock per R3 MF1
- TSan dual-scope verification (Phase 15.1 D-07): broadcast happy path must be TSan-clean

### Phase 21: H.264 Decoder & Receive Pipeline

**Goal**: Users see remote peers' video decoded to a per-user `juce::Image` and synchronized to audio at interval boundaries via the GUID-pairing decision tree, fixing the "video one interval early" bug at the protocol level
**Depends on**: Phase 20 (send-side wire format is bit-for-bit NinjamZap-compatible; can test receive against the same sender)
**Reference**: `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (Item E.1 — port `JamTaba/src/Common/video/FFMpegDemuxer.cpp`; Item F.2 — `VideoRecvBuffer`/`VideoRecvState` 4-stage pipeline; Item F.3 — GUID-pairing decision tree). `260515-0pc-RESEARCH-ADDENDUM.md` "Receiver decision tree (4-stage pipeline + GUID matching)" — DS match defers 1 swap, PREV match plays immediately, no-match HOLDs with `kHoldCapDrop=4` resync. NinjamZap reference: `ninjamzap-core/njclient.h:334-417` (state structs), `:1300-1550` (WRITE handling), `:3084-3219` (decision tree). Test scenarios: `ninjamzap-core/tests/video-sync/scenarios/` (26 cases).
**Requirements**: COD-03, WIRE-02
**Success Criteria** (what must be TRUE):

  1. User sees a remote peer's video appear in their JamWide UI at the same wall-clock moment as the matching audio interval (no "1 interval early" drift) for 5+ minutes of continuous playback
  2. User joining a session mid-stream sees video appear after at most 2 interval boundaries (SPS/PPS picked up from the next interval's chunk #2)
  3. User on a session where one remote peer's audio stops while video continues sees video freeze gracefully after `kHoldCapDrop=4` consecutive mismatches, then resume cleanly when the peer's audio returns
  4. User running JamWide with 3+ remote peers broadcasting video simultaneously sees each peer's video decoded independently with one decoder + one 4-stage pipeline per peer

**Plans**: 3 plans

  - Plan 21-01: receive-side state machine + WRITE-handler accumulation + audio-thread on_new_interval SWAP + GUID-pair decision tree (DS/PREV/HOLD with kHoldCapDrop=4) + Phase 21 audit-allowlist envelope (WIRE-02)
  - Plan 21-02: VideoDecoder interface + Openh264Decoder (per-peer juce::Thread + libavcodec H.264 + libswscale BGRA + R4 H9 7-step destructor + drop-frame-and-continue error recovery) + AVCC parser wired into audio-thread SWAP via parsePlayingSlotAndEnqueue_ (COD-03)
  - Plan 21-03: JamWideRemoteFrameDistributor + PeerVideoSink (double-buffered juce::Image + atomic generation + AsyncUpdater + listener vector + atomic status fields) + lazy decoder/sink lifecycle wiring + JamWideJuceProcessor integration + 3-peer per-peer isolation integration test + UAT against video.ninjamzap.com:2049 (WIRE-02, COD-03)

### Phase 22: Native Video UI (Grid + Popouts)

**Goal**: Users see remote peers' video in an in-plugin grid and can pop out individual peers to separate `juce::DocumentWindow`s, with grid and popouts active simultaneously and survivable across grid toggles
**Depends on**: Phase 21 (decoded `juce::Image` per remote peer available)
**Reference**: `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (Item E.2 — display widget). `260515-0pc-RESEARCH-ADDENDUM.md` "UI rendering model [LOCKED-2026-05-15]" — native rendering only, grid + popouts both supported. Existing VDO.Ninja popout pattern in `companion/popout.html` and `juce/video/VideoCompanion.cpp` is the UX reference (multi-monitor friendly window detachment).
**Requirements**: DISP-01, DISP-02, DISP-03, DISP-04
**Success Criteria** (what must be TRUE):

  1. User sees a per-remote-user video tile grid inside the main plugin/standalone view, sized appropriately for the number of peers (auto-flow layout)
  2. User can click a "Pop out" affordance on any peer's tile and see the peer's video appear in a separate `juce::DocumentWindow` that can be dragged to a second monitor and resized independently
  3. User can have the grid view open AND one or more popouts open at the same time; closing/reopening the grid does NOT close the popouts
  4. User can toggle the grid view off (without disconnecting from NINJAM) and on again; popouts continue rendering throughout the toggle

**Plans**: 4 plans
Plans:
**Wave 1**

- [ ] 22-01-PLAN.md — Tile substrate: `computeGridLayout` pure helper + `VideoTileBase`/`SelfVideoTile`/`RemotePeerTile` (MEMBER-ORDER CONTRACT verbatim) + Wave 0 unit tests (test_video_grid_layout, test_video_tile_member_order)

**Wave 2** *(blocked on Wave 1 completion)*

- [ ] 22-02-PLAN.md — Grid band container + ConnectionBar Grid button + JamWideJuceEditor integration (resized() insertion between sessionInfoStrip and channelStripArea + 20Hz auto-open-on-first-frame latch per D-05); manual visual checkpoint

**Wave 3** *(blocked on Wave 2 completion)*

- [ ] 22-03-PLAN.md — Popout windows (RemotePeerPopoutWindow + DetachedGridWindow) + placeholder cards (PopoutPlaceholderCard + DetachedGridPlaceholderCard) + editor controller methods (openOrToggleRemotePopout, bringBackRemotePopout, reattachGrid) + Wave 0 popout-lifetime unit test + multi-monitor clamp (T-22-MM mitigation); manual visual checkpoint

**Wave 4** *(blocked on Wave 3 completion)*

- [ ] 22-04-PLAN.md — Plugin state v4→v5 bump (structured `<video>` ValueTree subtree per D-19) + T-22-SP hardening (popout map cap=64 + username cap=256 + jlimit clamping) + Wave 0 test_plugin_state_v4_v5 + manual UAT procedure (13 cells covering DISP-01..04 + 6 manual-only behaviors) + final closure checkpoint

**UI hint**: yes

### Phase 23: macOS Universal + Windows Build & Codesign

**Goal**: JamWide ships a reproducible macOS universal binary (arm64 + x86_64) AND a Windows x86_64 build, both with the native video stack, platform-correct codesigning, camera entitlements (macOS) / signtool signatures (Windows), and CI lanes that verify LGPL discipline and clean platform dependency reports
**Depends on**: Phase 19, Phase 20, Phase 21 (need a fully-functional native video stack to bundle and codesign)
**Reference**: `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (Item B partial — macOS arm64 + universal stitching AND Windows x86_64 (Linux deferred); Item G.2 — per-dylib codesign + `install_name_tool` Frameworks-path rewriting on macOS). `CMakeLists.txt:244-269` (current macOS codesign loop, needs extending). `JamWide.entitlements` (camera entitlement landed in Phase 19; codesign verification lives here). Spike Risk #3 (Cisco openh264 v2.1.1 last mac prebuilt — arm64 may require source build or VideoToolbox fallback per Q13). Spike Risk #4 (libX11 spurious dep mitigated; CI must enforce). Phase 14.3 already produces `libs/ffmpeg/windows-x86_64/` DLLs reproducibly; Phase 23 wires them into the Windows installer/bundle, sets correct PATH/load-path resolution, and applies `signtool` codesigning where a Windows code-signing certificate is available. Memory keys: "Apple Developer Signing — Team ID T3KK66Q67T, notarization via API Key", "Build VST3 with correct target", "Release packaging — macOS/Linux use tar.gz, Windows uses zip" all apply here.
**Requirements**: PKG-01, PKG-02, PKG-03, PKG-04 (codesign + frameworks-path portions), PKG-05, PKG-06, PKG-07
**Success Criteria** (what must be TRUE):

  1. User can install the JamWide universal `.pkg` on macOS arm64 (Apple Silicon) and macOS x86_64 (Intel) machines, launch the standalone, and broadcast/receive video end-to-end on both architectures
  2. User can load the JamWide universal AU/VST3 plugin in at least one DAW on each macOS architecture, grant camera permission, and broadcast video successfully
  3. User running `codesign --verify --deep --strict` against the installed macOS bundle (Standalone, AU, VST3) sees zero errors — all vendored ffmpeg dylibs are individually signed, load paths rewritten via `install_name_tool` to `@loader_path/../Frameworks/`, and the outer bundle passes deep verification
  4. User can install the JamWide Windows x86_64 build (zip or installer), launch the standalone, broadcast/receive video end-to-end, and load the VST3 plugin into at least one Windows DAW (REAPER minimum) with the camera happy path working
  5. Windows installer/bundle contains the vendored ffmpeg DLLs (libavcodec, libavformat, libavutil, libswscale, libopenh264) alongside the executable with correct load-path resolution; `signtool` codesigning is applied when a Windows code-signing certificate is configured (and skipped cleanly with a clear log message when not)
  6. CI gates fail any PR where `strings libs/ffmpeg/*/lib/libavcodec.* | grep -E 'libx264|x264_'` returns non-empty for any architecture (macOS or Windows), OR where `otool -L` reports a dependency outside `@rpath`, `@loader_path`, `/usr/lib`, `/System` for any macOS vendored dylib, OR where `dumpbin /dependents` on a Windows vendored DLL reports a dependency outside the standard Windows system DLLs + the JamWide-bundled DLL set

**Plans**: 3 plans

  - Plan 23-01: macOS universal stitching + per-dylib codesign + `install_name_tool` Frameworks-path rewriting + entitlements + macOS CI gate (`otool -L` + LGPL `strings` check)
  - Plan 23-02: Windows x86_64 build + ffmpeg DLL bundling + correct load-path resolution + `signtool` codesigning (cert-conditional)
  - Plan 23-03: CI lane parity — extend the existing macOS x86_64 lane from Phase 14.3 to also cover macOS arm64, and add a Windows x86_64 lane with the equivalent LGPL discipline + `dumpbin /dependents` gate; both lanes produce shippable beta artifacts

### Phase 24: Beta Validation, Server Docs & Per-DAW UAT

**Goal**: JamWide ships `docs/SERVER.md` framed in two sections — section 1 names the public `video.ninjamzap.com:2049` ninjamzap-server as the recommended v1.3-beta path (community-operated, no SLA; beta testers manually enter the address into JamWide's existing untouched NINJAM server browser); section 2 walks through self-hosting upstream `ninjamzap-server` (Docker Compose example + minimum config + version pin) — ports ≥20 of NinjamZap's 26 video-sync test scenarios so they pass on both macOS and Windows, and validates the beta with the full UAT matrix: macOS standalone + REAPER fallback + Logic Pro happy path + Windows standalone + Windows REAPER VST3 happy path + cross-platform macOS↔Windows interop on `video.ninjamzap.com:2049` + NinjamZap mobile interop check — beta is ready to ship
**Depends on**: Phase 23 (macOS universal + Windows codesigned builds available for distribution to UAT participants)
**Reference**: `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (reduced Item I — macOS + REAPER + Logic Pro + Windows standalone + Windows REAPER, NOT full per-DAW matrix; Item J option (c) — `docs/SERVER.md` linking to the public `video.ninjamzap.com:2049` instance as the recommended v1.3-beta path + upstream `ninjamzap-server` self-host install instructions + the `AllowVideoChannels yes` + `PrivateGroupMode N` minimum config + a JamWide release-notes pin to a known-good ninjamzap-server tag + a one-command Docker Compose example). **No UI work**: JamWide's existing NINJAM server browser (untouched by v1.3) already supports manual server-address entry, so beta testers type `video.ninjamzap.com:2049` themselves — no preset entry needed (SRV-02 removed per user 2026-05-15). NinjamZap test harness at `ninjamzap-core/tests/video-sync/harness/TestClient.{h,cpp}` (port to JamWide `tests/`, ensure they build and pass on both macOS and Windows under `./scripts/build.sh --tests`). 26 scenarios at `ninjamzap-core/tests/video-sync/scenarios/`. Upstream server docs to adapt: `/Users/cell/dev/ninjamzap-server/docs/VIDEO_SUPPORT.md`, `/Users/cell/dev/ninjamzap-server/Dockerfile`, `/Users/cell/dev/ninjamzap-server/configs/`. Memory keys for release packaging: "Release packaging — macOS/Linux use tar.gz, Windows uses zip" and "Update download page on betas — Always update docs/download.md link when tagging a new beta release".
**Requirements**: WIRE-04, BETA-01, BETA-02, BETA-03, BETA-04, BETA-05, BETA-06, SRV-01
**Success Criteria** (what must be TRUE):

  1. JamWide ships `docs/SERVER.md` with two sections: section 1 names the public `video.ninjamzap.com:2049` ninjamzap-server as "Recommended for v1.3 beta — community-operated, no SLA", explaining beta testers enter the address into the existing NINJAM server browser; section 2 documents self-hosting upstream `ninjamzap-server` including a Docker Compose example for one-command deployment, the minimum required server config (`AllowVideoChannels yes` + `PrivateGroupMode N`), latency/privacy rationale, and a JamWide release-notes pin to a known-good `ninjamzap-server` tag (SRV-01)
  2. Two JamWide standalone users on different macOS machines (one arm64, one x86_64) connect to `video.ninjamzap.com:2049` via the existing server browser, join the same room, and successfully broadcast + receive each other's video for at least 5 minutes with no audio glitches and no decoder freezes (BETA-01)
  3. A user loading JamWide VST3 in REAPER on macOS reaches the "Camera unavailable" fallback gracefully per SPARTA Issue #82; the remote peer (a different JamWide standalone user) still hears the REAPER user's audio normally (BETA-02)
  4. A user loading JamWide AU in Logic Pro on macOS broadcasts video successfully to `video.ninjamzap.com:2049` (Logic Pro requests camera for itself); a remote standalone JamWide peer sees the Logic Pro user's video in their grid (BETA-03)
  5. A JamWide user joins a NinjamZap-server-hosted room with at least one NinjamZap mobile (iOS or Android) peer and successfully sees the mobile peer's video decoded and rendered in the JamWide grid (WIRE-04, also acts as live wire-format compatibility evidence)
  6. At least 20 of the 26 NinjamZap video-sync test scenarios at `ninjamzap-core/tests/video-sync/scenarios/` are ported to JamWide `tests/` and pass under `./scripts/build.sh --tests` on **both macOS and Windows** (BETA-04)
  7. JamWide standalone on Windows x86_64 and JamWide REAPER VST3 on Windows x86_64 both reach the camera happy path and successfully broadcast video to `video.ninjamzap.com:2049` (BETA-06)
  8. A macOS user (standalone or DAW-hosted) and a Windows user (standalone or DAW-hosted) both connect to `video.ninjamzap.com:2049` and successfully broadcast + receive each other's video for at least 5 minutes — cross-platform end-to-end gate on the live reference-server instance (BETA-05)

**Plans**: 2 plans

  - Plan 24-01: SRV-01 server docs (`docs/SERVER.md` two-section frame — public `video.ninjamzap.com:2049` recommended path + self-host with Docker Compose + version pin) + port ≥20 of 26 NinjamZap video-sync scenarios to JamWide `tests/` + macOS UAT against `video.ninjamzap.com:2049` (BETA-01, BETA-02, BETA-03, BETA-04 macOS half, WIRE-04 mobile interop)
  - Plan 24-02: Windows standalone + Windows REAPER VST3 UAT against `video.ninjamzap.com:2049` (BETA-06) + cross-platform macOS↔Windows interop on `video.ninjamzap.com:2049` (BETA-05) + finalise BETA-04 Windows half + beta release notes + download page update

## Future Milestones

### v2.0: Codec & Transport Redesign

**Source:** `CODEC_REDESIGN_PLAN.md`
**Depends on:** v1.1 complete
**Goal:** Low-latency codec stack with Opus as real-time default, robust FLAC framing, packetized transport with jitter handling, and backward-compatible capability negotiation.

## Progress

**Execution Order:**
Phases execute in numeric order: 9 -> 10 -> 11 -> 12 -> 13
Note: Phase 11 is independent of Phases 9-10 (OSC and Video are architecturally independent). Phase 13 depends on both Phase 10 and Phase 12.

v1.3 execution order: 19 -> 20 -> 21 -> 22 -> 23 -> 24 (strict dependency chain — capture unblocks encode; encode unblocks receive; receive unblocks UI; UI + send + receive unblock the macOS universal + Windows build + codesign work; both shippable builds unblock the macOS + Windows + cross-platform beta UAT and `docs/SERVER.md` finalisation against upstream ninjamzap-server with `video.ninjamzap.com:2049` as the recommended public instance).

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. FLAC Lossless Codec | v1.0 | 3/3 | Complete | 2026-03-07 |
| 2. JUCE Scaffolding | v1.0 | 2/2 | Complete | 2026-03-07 |
| 3. NJClient Audio Bridge | v1.0 | 2/2 | Complete | 2026-03-07 |
| 4. Core UI Panels | v1.0 | 4/4 | Complete | 2026-04-04 |
| 5. Mixer UI and Channel Controls | v1.0 | 4/4 | Complete | 2026-04-04 |
| 6. Multichannel Output Routing | v1.0 | 2/2 | Complete | 2026-04-04 |
| 7. DAW Sync and Session Polish | v1.0 | 3/3 | Complete | 2026-04-05 |
| 8. JUCE Integration Polish | v1.0 | 1/1 | Complete | 2026-04-05 |
| 9. OSC Server Core | v1.1 | 0/2 | In Progress | - |
| 10. OSC Remote Users and Template | v1.1 | 2/2 | Complete    | 2026-04-07 |
| 11. Video Companion Foundation | v1.1 | 3/3 | Complete    | 2026-04-07 |
| 12. Video Sync and Roster Discovery | v1.1 | 2/2 | Complete   | 2026-04-07 |
| 12.1 Video-Audio Sync Fix | v1.1 | 1/1 | Complete | 2026-04-13 |
| 13. Video Display Modes and OSC Integration | v1.1 | 2/2 | Complete    | 2026-04-07 |
| 14. MIDI Remote Control | v1.1 | 3/3 | Complete   | 2026-04-15 |
| 14.1 Audio Prelisten | v1.1 | 1/2 | In Progress|  |
| 14.2 Instamode Video Sync | v1.1 | 2/2 | Complete   | 2026-04-16 |
| 14.3 Native Video Foundation | v1.1 | 3/3 | Complete | 2026-05-15 |
| 15. Connection Encryption | v1.2 | 2/2 | Complete    | 2026-04-11 |
| 15.1. RT-Safety Hardening | v1.2 | 0/0 | Context gathered | - |
| 16. Opus Codec Integration | v1.2 | 0/0 | Not started | - |
| 17. Network Resilience | v1.2 | 0/0 | Not started | - |
| 18. Testing Infrastructure | v1.2 | 0/0 | Not started | - |
| 19. Camera Capture & Permission UX | v1.3 | 3/3 | Complete   | 2026-05-16 |
| 20. H.264 Encoder & Send Pipeline | v1.3 | 3/4 | In Progress|  |
| 21. H.264 Decoder & Receive Pipeline | v1.3 | 2/3 | In Progress|  |
| 22. Native Video UI (Grid + Popouts) | v1.3 | 0/2 | Not started | - |
| 23. macOS Universal + Windows Build & Codesign | v1.3 | 0/3 | Not started | - |
| 24. Beta Validation, Server Docs & Per-DAW UAT | v1.3 | 0/2 | Not started | - |

## Backlog

### Phase 999.1: Hide Bot Users from NINJAM Mixer Channels (BACKLOG)

**Goal**: Filter known bot usernames (ninbot, jambot, etc.) from the mixer channel strip UI so they don't appear as audio channels. Shared bot detection utility reuses the same bot-name list as Phase 12 roster strip filtering.
**Requirements**: TBD
**Plans**: 0 plans

Plans:

- [ ] TBD (promote with /gsd-review-backlog when ready)
