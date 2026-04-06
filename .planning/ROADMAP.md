# Roadmap: JamWide

## Milestones

- ✅ **v1.0 MVP** -- Phases 1-8 (shipped 2026-04-05) -- see `milestones/v1.0-ROADMAP.md`
- 🚧 **v1.1 OSC + Video** -- Phases 9-13 (in progress)
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
- [ ] **Phase 10: OSC Remote Users and Template** -- Index-based remote user control, roster broadcasts, connect/disconnect, shipped TouchOSC template
- [ ] **Phase 11: Video Companion Foundation** -- One-click VDO.Ninja launch with auto room ID, companion page, local WebSocket server, safety notices
- [ ] **Phase 12: Video Sync and Roster Discovery** -- Interval-synced buffering, VDO.Ninja API roster mapping, room security, bandwidth profiles
- [ ] **Phase 13: Video Display Modes and OSC Integration** -- Per-user popout windows, OSC video control, grid/popout mode switching

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
- [ ] 10-01-PLAN.md — Remote user OSC send/receive: extend RemoteUserInfo snapshot, dynamic address generation, prefix-based dispatch, roster broadcast, connect/disconnect triggers, docs/osc.md update
- [ ] 10-02-PLAN.md — TouchOSC template: generate and ship JamWide.tosc with 8 remote slots, local channels, master, metronome, session info, verification checkpoint

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
- [ ] 11-01-PLAN.md -- VideoCompanion core: IXWebSocket dependency, WebSocket server, room ID derivation (SHA-1), username sanitization, config/roster JSON protocol
- [ ] 11-02-PLAN.md -- Web companion page: Vite/TypeScript project in docs/video/, VDO.Ninja iframe with &noaudio, branded UI, WebSocket client, connection status
- [ ] 11-03-PLAN.md -- Video button + privacy modal: ConnectionBar integration, VideoPrivacyDialog, BrowserDetect, processor/editor wiring, human verification checkpoint
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
**Plans**: TBD

### Phase 13: Video Display Modes and OSC Integration
**Goal**: Users can pop out individual participant video into separate windows and control all video features from their OSC surface
**Depends on**: Phase 10, Phase 12
**Requirements**: VID-07, VID-11
**Success Criteria** (what must be TRUE):
  1. User can pop out an individual participant's video into a separate browser window (multi-monitor support)
  2. User can open video, close video, switch display modes, and trigger popouts via OSC commands from their control surface
**Plans**: TBD
**UI hint**: yes

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
| 10. OSC Remote Users and Template | v1.1 | 0/2 | Planned | - |
| 11. Video Companion Foundation | v1.1 | 0/3 | Planned | - |
| 12. Video Sync and Roster Discovery | v1.1 | 0/0 | Not started | - |
| 13. Video Display Modes and OSC Integration | v1.1 | 0/0 | Not started | - |
