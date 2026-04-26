# Roadmap: JamWide

## Milestones

- ✅ **v1.0 MVP** -- Phases 1-8 (shipped 2026-04-05) -- see `milestones/v1.0-ROADMAP.md`
- 🚧 **v1.1 OSC + Video** -- Phases 9-13 (in progress)
- 📋 **v1.2 Security & Quality** -- Phases 15-18 (planned)
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
- [ ] 15.1-02-atomic-promotion-PLAN.md — m_misc_cs / BPM / BPI / m_interval_pos atomic promotion (CR-03; documents `m_beatinfo_updated` edge-trigger semantics per L-10)
- [ ] 15.1-03-eliminate-audio-path-logging-PLAN.md — remove writeLog / writeUserChanLog / JAMWIDE_DEV_BUILD fopen from audio path (CR-04, H-01, H-02, L-02)
- [ ] 15.1-04-spsc-infrastructure-PLAN.md — SPSC payloads.h (FINAL Wave-0 payloads incl. DecodeArmRequest), `--tsan` flag, JAMWIDE_TSAN option, primitive unit tests, `MAX_BLOCK_SAMPLES` contract
- [ ] 15.1-05-deferred-delete-PLAN.md — DecodeState* deferred-delete SPSC for all 7 audio-thread delete sites (CR-05, CR-06, CR-07) + overflow counter accessor
- [ ] 15.1-06-locchan-cs-snapshot-PLAN.md — m_locchan_cs replaced with audio-thread mirror + LocalChannelUpdate SPSC + Local_Channel deferred-free with generation gate (CR-02)
- [ ] 15.1-07a-remote-user-mirror-PLAN.md — m_users_cs mirror + RemoteUser deferred-free with generation gate (CR-01) — has human-verify checkpoint
- [ ] 15.1-07b-buffer-queue-PLAN.md — BlockRecord SPSC replacing BufferQueue (CR-09, CR-10) with `<5%` CPU perf-budget acceptance criterion
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

## Future Milestones

### v2.0: Codec & Transport Redesign
**Source:** `CODEC_REDESIGN_PLAN.md`
**Depends on:** v1.1 complete
**Goal:** Low-latency codec stack with Opus as real-time default, robust FLAC framing, packetized transport with jitter handling, and backward-compatible capability negotiation.

## Progress

**Execution Order:**
Phases execute in numeric order: 9 -> 10 -> 11 -> 12 -> 13
Note: Phase 11 is independent of Phases 9-10 (OSC and Video are architecturally independent). Phase 13 depends on both Phase 10 and Phase 12.

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
| 15. Connection Encryption | v1.2 | 2/2 | Complete    | 2026-04-11 |
| 15.1. RT-Safety Hardening | v1.2 | 0/0 | Context gathered | - |
| 16. Opus Codec Integration | v1.2 | 0/0 | Not started | - |
| 17. Network Resilience | v1.2 | 0/0 | Not started | - |
| 18. Testing Infrastructure | v1.2 | 0/0 | Not started | - |

## Backlog

### Phase 999.1: Hide Bot Users from NINJAM Mixer Channels (BACKLOG)
**Goal**: Filter known bot usernames (ninbot, jambot, etc.) from the mixer channel strip UI so they don't appear as audio channels. Shared bot detection utility reuses the same bot-name list as Phase 12 roster strip filtering.
**Requirements**: TBD
**Plans**: 0 plans

Plans:
- [ ] TBD (promote with /gsd-review-backlog when ready)
