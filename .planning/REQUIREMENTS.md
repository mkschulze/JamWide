# Requirements: JamWide

**Defined:** 2026-03-07
**Core Value:** Musicians can jam together online with lossless audio quality and per-user mixing -- in any DAW or standalone.

## v1 Requirements

Requirements for this milestone. Each maps to roadmap phases.

### Audio Codec

- [x] **CODEC-01**: User can send audio using FLAC lossless encoding
- [x] **CODEC-02**: User can receive and decode FLAC audio from other participants
- [x] **CODEC-03**: User can switch between FLAC and Vorbis via UI toggle
- [x] **CODEC-04**: Codec switch applies at interval boundary (no mid-interval glitches)
- [x] **CODEC-05**: Default codec is FLAC (user can switch to Vorbis for bandwidth-constrained sessions)

### JUCE Migration

- [x] **JUCE-01**: Plugin builds as VST3 and AU using JUCE AudioProcessor
- [x] **JUCE-02**: Standalone application mode works with audio device selection
- [x] **JUCE-03**: NJClient audio processing integrated via processBlock()
- [x] **JUCE-04**: NJClient Run() thread operates via juce::Thread
- [x] **JUCE-05**: All UI rebuilt as JUCE Components (no Dear ImGui)
- [x] **JUCE-06**: Plugin state saves/restores via getStateInformation/setStateInformation

### UI Panels

- [x] **UI-01**: Connection panel (server, username, password, connect/disconnect)
- [x] **UI-02**: Chat panel with message history and input
- [x] **UI-03**: Status display (connection state, BPM/BPI, user count)
- [x] **UI-04**: Remote user mixer (volume, pan, mute, solo per channel)
- [x] **UI-05**: Local channel controls (volume, pan, mute, monitoring)
- [x] **UI-06**: Metronome controls (volume, pan, mute)
- [x] **UI-07**: Server browser with public server list
- [x] **UI-08**: VU meters for local and remote channels
- [x] **UI-09**: Codec selector (FLAC/Vorbis toggle per local channel)

### Multichannel Output

- [ ] **MOUT-01**: Each remote user routable to a separate stereo output pair in the DAW
- [ ] **MOUT-02**: Auto-assign mode: by user (one stereo pair per user)
- [ ] **MOUT-03**: Auto-assign mode: by channel (one stereo pair per remote channel)
- [ ] **MOUT-04**: Metronome routable to its own output pair
- [ ] **MOUT-05**: Main mix always on output bus 0

### DAW Sync

- [ ] **SYNC-01**: Plugin reads host transport state (playing/stopped) via AudioPlayHead
- [ ] **SYNC-02**: Broadcasting only occurs when DAW is playing
- [ ] **SYNC-03**: Session position tracked across intervals
- [ ] **SYNC-04**: Live BPM/BPI changes applied at interval boundaries without reconnect
- [ ] **SYNC-05**: Standalone mode provides pseudo-transport with server BPM

### Session Recording

- [x] **REC-01**: User can enable session recording via UI
- [x] **REC-02**: Recorded audio saved as OGG or WAV (existing NJClient capability)

### Research Deliverables

- [ ] **RES-01**: Video feasibility document (JamTaba approach analysis)
- [ ] **RES-02**: OSC cross-DAW sync evaluation (per-DAW support matrix)
- [ ] **RES-03**: MCP bridge feasibility assessment

## v2 Requirements

Deferred to future release. Tracked but not in current roadmap.

### Codec & Transport Redesign

Source: `CODEC_REDESIGN_PLAN.md` — low-latency codec stack with Opus, robust FLAC, and packetized transport.

**Transport Layer:**
- **XPORT-01**: Codec-agnostic packet envelope (version, codec_id, stream_id, sequence, flags, sample_rate, channels, payload)
- **XPORT-02**: Per-remote-channel jitter/playout buffer with adaptive target delay
- **XPORT-03**: Packet loss detection and concealment (bounded mute, never undefined decode state)
- **XPORT-04**: Decoder input via explicit packet push model (not raw byte stream callbacks)

**Opus Integration:**
- **OPUS-01**: libopus dependency and build integration
- **OPUS-02**: Opus encoder/decoder adapters implementing codec pipeline interface
- **OPUS-03**: Mode presets (low-latency voice, music) with sensible defaults (48kHz, 5-10ms packets, constrained VBR)
- **OPUS-04**: Optional in-band FEC toggle

**FLAC Robust Mode:**
- **FLACR-01**: FLAC packetization as frame-aligned complete decodable units
- **FLACR-02**: Robust reset/discontinuity handling per stream (explicit state machine)
- **FLACR-03**: No starvation-driven decoder state transitions — partial delivery never causes silent failure

**Codec Negotiation:**
- **NEG-01**: Client/server codec capability exchange (supported codecs, preferred live codec, FEC support)
- **NEG-02**: Codec preference UI (Auto / Opus / FLAC / Vorbis legacy)
- **NEG-03**: Negotiated codec indicator per remote channel
- **NEG-04**: Bandwidth and latency warning labels per codec mode

**Rollout & Reliability:**
- **ROLL-01**: Telemetry counters (loss, concealment events, jitter buffer depth, codec switches)
- **ROLL-02**: Canary rollout flag for staged deployment
- **ROLL-03**: WAN simulation test suite in CI (jitter 0-120ms, loss 0-5%, reorder, mixed peers)

### Video

- **VID-01**: Camera capture and H.264 encoding for video streaming
- **VID-02**: Video display for remote participants
- **VID-03**: Video transmitted via NINJAM intervals (JamTaba approach)

### Cross-DAW Sync

- **XSYNC-01**: OSC output for DAW tempo sync
- **XSYNC-02**: OSC output for DAW transport control (play/stop/loop)
- **XSYNC-03**: MCP server bridge for DAW control

### Advanced Features

- **ADV-01**: NINJAM interval-synced looper (JamTaba-style)
- **ADV-02**: Voice chat (low-latency Opus stream)
- **ADV-03**: Input effects processing (compressor, gate, EQ) for standalone mode
- **ADV-04**: Mixer presets (save/load per-user volume/pan/mute/solo)
- **ADV-05**: ~~FLAC capability negotiation~~ — superseded by NEG-01 through NEG-04
- **ADV-06**: CLAP plugin format via juce_clap_extensions (or JUCE 9 native)

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Peer-to-peer audio | NINJAM is server-relayed by design; P2P fractures ecosystem |
| Low-latency real-time sync (<50ms) | NINJAM embraces interval-based latency as a musical concept |
| Mobile support | Desktop-first; mobile audio I/O unreliable, plugin ecosystems immature |
| Built-in VST/AU hosting (standalone) | A NINJAM client is not a DAW; massive complexity for marginal value |
| REAPER-specific extension APIs | Not portable; pursue OSC/MCP alternatives instead |
| AAX plugin format | Declining Pro Tools market share; JUCE supports it if demand emerges later |
| Livestreaming / broadcasting | Different product category; users can use OBS to capture output |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| CODEC-01 | Phase 1 | Complete |
| CODEC-02 | Phase 1 | Complete |
| CODEC-03 | Phase 1 | Complete |
| CODEC-04 | Phase 1 | Complete |
| CODEC-05 | Phase 1 | Complete |
| JUCE-01 | Phase 2 | Complete |
| JUCE-02 | Phase 2 | Complete |
| JUCE-03 | Phase 3 | Complete |
| JUCE-04 | Phase 2 | Complete |
| JUCE-05 | Phase 4 | Complete |
| JUCE-06 | Phase 5 | Complete |
| UI-01 | Phase 4 | Complete |
| UI-02 | Phase 4 | Complete |
| UI-03 | Phase 4 | Complete |
| UI-04 | Phase 5 | Complete |
| UI-05 | Phase 5 | Complete |
| UI-06 | Phase 5 | Complete |
| UI-07 | Phase 4 | Complete |
| UI-08 | Phase 5 | Complete |
| UI-09 | Phase 4 | Complete |
| MOUT-01 | Phase 6 | Pending |
| MOUT-02 | Phase 6 | Pending |
| MOUT-03 | Phase 6 | Pending |
| MOUT-04 | Phase 6 | Pending |
| MOUT-05 | Phase 6 | Pending |
| SYNC-01 | Phase 7 | Pending |
| SYNC-02 | Phase 7 | Pending |
| SYNC-03 | Phase 7 | Pending |
| SYNC-04 | Phase 7 | Pending |
| SYNC-05 | Phase 7 | Pending |
| REC-01 | Phase 1 | Complete |
| REC-02 | Phase 1 | Complete |
| RES-01 | Phase 7 | Pending |
| RES-02 | Phase 7 | Pending |
| RES-03 | Phase 7 | Pending |

**Coverage:**
- v1 requirements: 35 total
- Mapped to phases: 35
- Unmapped: 0

---
*Requirements defined: 2026-03-07*
*Last updated: 2026-03-07 after roadmap creation*
