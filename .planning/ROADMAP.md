# Roadmap: JamWide

## Overview

JamWide evolves from a working NINJAM client into a full JUCE-based application with lossless audio, multichannel output routing, DAW transport sync, and standalone support. The roadmap starts with the independent FLAC codec (ships value before migration risk), moves through the JUCE rewrite in vertical slices (scaffolding, audio bridge, UI panels, mixer), then delivers the DAW-integration differentiators (multichannel routing, transport sync). Research deliverables slot alongside the phases they inform.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [x] **Phase 1: FLAC Lossless Codec** - FLAC encoding/decoding for NINJAM intervals with UI toggle and session recording (completed 2026-03-07)
- [ ] **Phase 2: JUCE Scaffolding** - Plugin skeleton with AudioProcessor, thread architecture, multi-bus declaration, and CI
- [ ] **Phase 3: NJClient Audio Bridge** - Wire NJClient into JUCE processBlock with stereo output and end-to-end audio flow
- [ ] **Phase 4: Core UI Panels** - Connection, chat, status, server browser, and codec selector as JUCE Components
- [ ] **Phase 5: Mixer UI and Channel Controls** - Remote user mixer, local channels, metronome, VU meters, and state persistence
- [ ] **Phase 6: Multichannel Output Routing** - Per-user and per-channel stereo pair routing to DAW output buses
- [ ] **Phase 7: DAW Sync and Session Polish** - Transport sync, live BPM/BPI, session position tracking, and research deliverables

## Phase Details

### Phase 1: FLAC Lossless Codec
**Goal**: Musicians can send and receive lossless audio in NINJAM sessions, and record sessions locally
**Depends on**: Nothing (first phase)
**Requirements**: CODEC-01, CODEC-02, CODEC-03, CODEC-04, CODEC-05, REC-01, REC-02
**Success Criteria** (what must be TRUE):
  1. User can join a NINJAM session and send FLAC-encoded audio to other participants
  2. User can receive and hear FLAC audio from other participants in the same session
  3. User can toggle between FLAC and Vorbis codecs via a UI control, with the switch happening cleanly at the next interval boundary
  4. User can enable session recording and find saved audio files on disk after the session
  5. A FLAC user and a Vorbis user can coexist in the same session without errors
**Plans**: 3 plans

Plans:
- [x] 01-01-PLAN.md -- libFLAC submodule, CMake integration, FlacEncoder/FlacDecoder classes
- [x] 01-02-PLAN.md -- NJClient FLAC encode/decode paths, format state, command queue, chat notification
- [x] 01-03-PLAN.md -- Codec selection UI, sender/receiver indicators, recording toggle, end-to-end verification

### Phase 2: JUCE Scaffolding
**Goal**: The JUCE project skeleton builds, passes pluginval, and runs as VST3, AU, and standalone with correct architecture in place
**Depends on**: Phase 1
**Requirements**: JUCE-01, JUCE-02, JUCE-04
**Success Criteria** (what must be TRUE):
  1. Plugin builds as VST3 and AU, and loads in a DAW without crashing (validated by pluginval)
  2. Standalone application launches with audio device selection and produces no audio (empty processor)
  3. NinjamRunThread starts and stops cleanly with the plugin lifecycle (no zombie threads on unload)
  4. Multi-bus output layout is declared at construction (even though routing is not yet wired)
**Plans**: TBD

Plans:
- [ ] 02-01: TBD
- [ ] 02-02: TBD

### Phase 3: NJClient Audio Bridge
**Goal**: Users can connect to a NINJAM server and hear audio flowing end-to-end through the JUCE plugin or standalone app
**Depends on**: Phase 2
**Requirements**: JUCE-03
**Success Criteria** (what must be TRUE):
  1. User can connect to a NINJAM server from the standalone app and hear remote participants on stereo output bus 0
  2. User's microphone/instrument audio is encoded and sent to other participants
  3. Both Vorbis and FLAC codecs work through the JUCE audio bridge
  4. Closing and reopening the plugin editor window does not interrupt audio or network connection
**Plans**: TBD

Plans:
- [ ] 03-01: TBD
- [ ] 03-02: TBD

### Phase 4: Core UI Panels
**Goal**: Users can connect, chat, browse servers, and manage codec settings entirely through the JUCE UI (no Dear ImGui)
**Depends on**: Phase 3
**Requirements**: JUCE-05, UI-01, UI-02, UI-03, UI-07, UI-09
**Success Criteria** (what must be TRUE):
  1. User can enter server address, username, and password and connect/disconnect via the connection panel
  2. User can send and receive chat messages with visible message history
  3. User can see connection state, BPM/BPI, and user count in a status display
  4. User can browse and select from a list of public NINJAM servers
  5. User can switch codec between FLAC and Vorbis from the UI
**Plans**: TBD

Plans:
- [ ] 04-01: TBD
- [ ] 04-02: TBD

### Phase 5: Mixer UI and Channel Controls
**Goal**: Users can mix remote participants and control local channels with full per-channel controls and visual feedback
**Depends on**: Phase 4
**Requirements**: JUCE-06, UI-04, UI-05, UI-06, UI-08
**Success Criteria** (what must be TRUE):
  1. User can adjust volume, pan, mute, and solo for each remote participant's channels
  2. User can control local channel volume, pan, mute, and monitoring
  3. User can control metronome volume, pan, and mute
  4. VU meters display real-time levels for both local and remote channels
  5. All mixer settings and plugin state persist across DAW save/load cycles
**Plans**: TBD

Plans:
- [ ] 05-01: TBD
- [ ] 05-02: TBD

### Phase 6: Multichannel Output Routing
**Goal**: DAW users can route each remote participant to a separate stereo track for independent mixing and processing
**Depends on**: Phase 5
**Requirements**: MOUT-01, MOUT-02, MOUT-03, MOUT-04, MOUT-05
**Success Criteria** (what must be TRUE):
  1. Each remote user's audio appears on a separate stereo output pair in the DAW mixer
  2. User can switch between auto-assign modes (by user, by channel) and see routing update
  3. Metronome is routable to its own dedicated output pair
  4. Main mix always remains on output bus 0 regardless of routing mode
  5. Multichannel layout works in at least Logic Pro, REAPER, and one other DAW (Ableton or Bitwig)
**Plans**: TBD

Plans:
- [ ] 06-01: TBD
- [ ] 06-02: TBD

### Phase 7: DAW Sync and Session Polish
**Goal**: The plugin feels native in any DAW with transport-aware broadcasting, live tempo changes, and session tracking; research deliverables complete
**Depends on**: Phase 6
**Requirements**: SYNC-01, SYNC-02, SYNC-03, SYNC-04, SYNC-05, RES-01, RES-02, RES-03
**Success Criteria** (what must be TRUE):
  1. Plugin only broadcasts audio when the DAW transport is playing; stopping the DAW silences the send
  2. BPM or BPI changes from the server apply at the next interval boundary without requiring reconnect
  3. Session position (interval count, beat position) is tracked and visible to the user
  4. Standalone mode provides a pseudo-transport driven by the server's BPM
  5. Video feasibility document, OSC evaluation matrix, and MCP assessment are written and available
**Plans**: TBD

Plans:
- [ ] 07-01: TBD
- [ ] 07-02: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. FLAC Lossless Codec | 3/3 | Complete   | 2026-03-07 |
| 2. JUCE Scaffolding | 0/? | Not started | - |
| 3. NJClient Audio Bridge | 0/? | Not started | - |
| 4. Core UI Panels | 0/? | Not started | - |
| 5. Mixer UI and Channel Controls | 0/? | Not started | - |
| 6. Multichannel Output Routing | 0/? | Not started | - |
| 7. DAW Sync and Session Polish | 0/? | Not started | - |
