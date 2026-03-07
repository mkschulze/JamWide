# JamWide

## What This Is

JamWide is a cross-platform NINJAM client for real-time online music collaboration. Currently implemented as a CLAP/VST3/AU audio plugin using Dear ImGui for UI and clap-wrapper for plugin format support. It connects musicians over the internet using the NINJAM protocol (interval-based audio exchange with OGG Vorbis encoding). This milestone evolves JamWide into a full JUCE-based application with lossless audio, multichannel routing, DAW sync, and standalone support.

## Core Value

Musicians can jam together online with lossless audio quality and per-user mixing — in any DAW or standalone.

## Requirements

### Validated

- ✓ NINJAM protocol connectivity (TCP, authentication, chat) — existing
- ✓ OGG Vorbis audio encoding/decoding — existing
- ✓ Three-thread architecture (audio, UI, network/run) — existing
- ✓ CLAP/VST3/AU plugin format support — existing (via clap-wrapper)
- ✓ Cross-platform (macOS, Windows) — existing
- ✓ Server browser with public server list — existing
- ✓ Chat functionality — existing
- ✓ Local channel monitoring with VU meters — existing
- ✓ Remote user channel display and mixing — existing
- ✓ Metronome with volume/pan/mute — existing
- ✓ BPM/BPI display from server — existing

### Active

- [ ] FLAC lossless audio encoding/decoding alongside Vorbis
- [ ] JUCE framework migration (full rewrite — UI, plugin format, threading)
- [ ] Standalone application mode (byproduct of JUCE migration)
- [ ] Multichannel output — per-user stereo pair routing (ReaNINJAM-style, both auto-assign modes: by user and by channel)
- [ ] DAW transport sync — read host transport state, sync intervals to playback
- [ ] Live BPM/BPI changes without reconnecting to server
- [ ] Session position tracking across intervals
- [ ] Evaluate video support (JamTaba-style H.264 over NINJAM intervals — research only)
- [ ] Evaluate MCP server bridge for cross-DAW sync (tempo, transport, loop control)
- [ ] Evaluate OSC support for cross-DAW sync

### Out of Scope

- Video implementation — research only this milestone, no code
- REAPER-specific extension APIs — cannot replicate generically; pursue MCP/OSC alternatives instead
- Capability negotiation for FLAC — v1 uses manual codec selection; auto-negotiation deferred
- Mobile support — desktop first
- Peer-to-peer audio — NINJAM is server-relayed by design

## Context

### Existing Codebase (Brownfield)

JamWide has a working NINJAM client with a three-layer architecture:
- **Plugin layer**: CLAP entry point, parameter handling via clap-wrapper
- **Core/Network layer**: NJClient from Cockos NINJAM (njclient.cpp ~2500 lines), Vorbis codec, TCP networking via WDL jnetlib
- **UI layer**: Dear ImGui with Metal (macOS) / D3D11 (Windows) backends
- **Threading**: Lock-free SPSC ring buffers for UI↔Run thread communication, atomics for audio thread

The JUCE migration is a **full rewrite** — not a port. JUCE idioms (AudioProcessor, Component, MessageManager) replace the current custom threading and plugin abstraction layers. NJClient core logic carries over but gets wrapped in JUCE patterns.

### Reference Implementations

- **ReaNINJAM** (`/Users/cell/dev/ninjam/ninjam`): Reference for multichannel output routing (per-user stereo pairs, auto-assign by user/channel), DAW transport sync (`AudioProc` isPlaying/isSeek), and session position tracking. Sync settings (set tempo, set loop, start playback) use REAPER-only extension APIs.
- **JamTaba** (`https://github.com/elieserdejesus/JamTaba`): Reference for video support — H.264 at 320x240 10FPS, server-relayed via NINJAM intervals with audio/video boolean flag, FFmpeg encode/decode, Qt camera capture. Also built with Qt/JUCE — useful architectural reference.

### FLAC Integration Plan

Detailed plan exists at `FLAC_INTEGRATION_PLAN.md`:
- Client-only change — NINJAM server relays opaque bytes with FOURCC codec tag
- New `FlacEncoder`/`FlacDecoder` classes implementing existing `I_NJEncoder`/`I_NJDecoder` interfaces
- Runtime codec selection via `SetEncoderFormat()` with atomic requested/active pattern
- Codec switch at interval boundaries only
- Default Vorbis, FLAC opt-in via UI toggle
- Mixed-codec sessions work naturally (server is a dumb pipe)

### DAW Sync Gap

Current JamWide requires reconnecting to the server when BPM changes. ReaNINJAM handles live BPM/BPI updates via `MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY` with recalculation at interval boundaries. This needs to work without reconnect.

Cross-DAW sync (tempo/transport/loop control) is not possible via standard plugin APIs. Two exploration paths:
1. **MCP servers**: Each DAW could run an MCP server exposing tempo/transport APIs — plugin communicates via standardized protocol
2. **OSC**: Many DAWs already support OSC — could send tempo/transport commands over OSC to the host

### Codebase Map

Full codebase analysis at `.planning/codebase/`:
- STACK.md, ARCHITECTURE.md, STRUCTURE.md, CONVENTIONS.md, TESTING.md, INTEGRATIONS.md, CONCERNS.md

## Constraints

- **Tech stack**: JUCE framework — replaces Dear ImGui, clap-wrapper, and custom threading
- **Protocol compatibility**: Must remain compatible with existing NINJAM servers and other clients (ReaNINJAM, JamTaba, etc.)
- **Codec compatibility**: FLAC clients must coexist with Vorbis-only clients in same session
- **Platform**: macOS and Windows (Linux nice-to-have but not required)
- **Dependencies**: libFLAC (new), JUCE (replaces ImGui + clap-wrapper), existing libogg/libvorbis retained for Vorbis support

## Milestones

| Milestone | Focus | Status |
|-----------|-------|--------|
| v1: JUCE Migration | FLAC codec, JUCE rewrite, multichannel routing, DAW sync | Active (Phase 2 of 7) |
| v2: Codec & Transport Redesign | Opus live codec, packetized transport, jitter handling, capability negotiation | Future |

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Full JUCE rewrite (not incremental port) | JUCE idioms for threading/UI/plugin are fundamentally different from current architecture — incremental port would create hybrid mess | — Pending |
| FLAC before JUCE migration | FLAC is self-contained, lower risk, ships value quickly while JUCE rewrite is planned | — Pending |
| ReaNINJAM-style multichannel (both modes) | Users expect per-user output routing for DAW mixing — both by-user and by-channel modes cover all workflows | — Pending |
| Video as research only | JamTaba approach is proven but significant scope — evaluate feasibility before committing | — Pending |
| MCP/OSC for cross-DAW sync | REAPER extension APIs aren't portable — MCP servers and OSC offer cross-DAW alternatives | — Pending |
| Default Vorbis, FLAC opt-in | Preserves backward compatibility — old clients can't decode FLAC | — Pending |
| Opus as v2 live default | Opus offers better quality/latency/bitrate than Vorbis; transport refactor needed first (v2 milestone) | — Planned |
| Transport refactor before new codecs | Packet envelope + jitter buffer needed to support Opus FEC and robust FLAC — build foundation first | — Planned |

---
*Last updated: 2026-03-07 after initialization*
