# JamWide

## What This Is

JamWide is a cross-platform NINJAM client for real-time online music collaboration. Built as a JUCE-based VST3/AU/CLAP audio plugin and standalone application. It connects musicians over the internet using the NINJAM protocol with FLAC lossless and Vorbis encoding, multichannel output routing for per-user DAW mixing, transport sync, and a Voicemeeter Banana-inspired dark UI.

## Core Value

Musicians can jam together online with lossless audio quality and per-user mixing — in any DAW or standalone.

## Current Milestone: v1.1 OSC + Video

**Goal:** Add remote control via OSC and video collaboration via VDO.Ninja companion, expanding JamWide from audio-only to a full visual jam experience.

**Target features:**
- OSC server for remote control via TouchOSC (bidirectional, full parameter mapping)
- VDO.Ninja video companion synced to NINJAM interval timing
- Video grid + per-user popout display modes (multi-monitor support)

## Requirements

### Validated

- ✓ FLAC lossless encoding/decoding — v1.0
- ✓ Vorbis encoding/decoding — existing + v1.0
- ✓ JUCE framework migration (VST3, AU, CLAP, Standalone) — v1.0
- ✓ Multichannel output routing (by user, by channel, manual) — v1.0
- ✓ DAW transport sync (read host state, sync intervals) — v1.0
- ✓ Live BPM/BPI changes without reconnect — v1.0
- ✓ Session position tracking — v1.0
- ✓ Full mixer UI (volume, pan, mute, solo per channel) — v1.0
- ✓ Plugin state persistence (save/restore with DAW session) — v1.0
- ✓ Connection panel, chat, server browser, codec selector — v1.0
- ✓ VU meters, metronome controls — v1.0
- ✓ Video feasibility research (VDO.Ninja sidecar recommended) — v1.0
- ✓ OSC evaluation (viable for REAPER/Bitwig/Ableton, ~37% coverage) — v1.0
- ✓ MCP assessment (not viable for transport sync, good for workflow tooling) — v1.0

### Active (v1.1)

- [ ] OSC server for remote control via TouchOSC (bidirectional, full parameter mapping)
- [ ] VDO.Ninja video companion synced to NINJAM interval timing
- [ ] Video grid + per-user popout display modes (multi-monitor support)

### Out of Scope

- Video embedded in plugin (WebView) — browser companion approach instead
- REAPER-specific extension APIs — not portable; OSC is the path
- Capability negotiation for codecs — deferred to v2
- Mobile support — desktop first
- Peer-to-peer audio — NINJAM is server-relayed by design
- MCP for real-time DAW sync — request/response model incompatible with streaming

## Context

### Reference Implementations

- **IEM Plugin Suite** (`/Users/cell/dev/IEMPluginSuite`): Reference for OSC server in JUCE — `OSCParameterInterface`, bidirectional UDP, juce_osc module
- **VDO.Ninja** (`docs.vdo.ninja`): Music sync buffer demo, chunked mode, external WebSocket API, room management

### Codebase Map

Full codebase analysis at `.planning/codebase/`:
- STACK.md, ARCHITECTURE.md, STRUCTURE.md, CONVENTIONS.md, TESTING.md, INTEGRATIONS.md, CONCERNS.md

## Constraints

- **Protocol compatibility**: Must remain compatible with existing NINJAM servers and other clients
- **Codec compatibility**: FLAC clients coexist with Vorbis-only clients
- **Platform**: macOS, Windows, Linux
- **Dependencies**: JUCE, libFLAC, libogg/libvorbis, juce_osc (for v1.1), OpenSSL (for v1.1 encryption)

## Milestones

| Milestone | Focus | Status |
|-----------|-------|--------|
| v1.0: JUCE Migration | FLAC codec, JUCE rewrite, multichannel routing, DAW sync | ✅ Shipped 2026-04-05 |
| v1.1: OSC + Video | OSC remote control, VDO.Ninja video companion, connection encryption | In Progress |
| v2.0: Codec & Transport Redesign | Opus live codec, packetized transport, jitter handling | Future |

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Full JUCE rewrite (not incremental port) | JUCE idioms fundamentally different from Dear ImGui architecture | ✓ Good — clean architecture |
| FLAC before JUCE migration | Self-contained, lower risk, ships value quickly | ✓ Good |
| ReaNINJAM-style multichannel (both modes) | Users expect per-user routing for DAW mixing | ✓ Good |
| Default Vorbis (not FLAC) | Interop with legacy Vorbis-only NINJAM clients | ✓ Good — practical |
| Voicemeeter Banana dark theme | User preference for familiar pro-audio aesthetic | ✓ Good |
| VDO.Ninja browser companion (not embedded WebView) | Keeps plugin lightweight, browser handles video rendering | — Pending (v1.1) |
| OSC via juce_osc (IEM pattern) | No external deps, proven across 20+ IEM plugins | — Pending (v1.1) |
| Index-based OSC addressing for remote users | Stable fader mapping, name broadcast on roster change | — Pending (v1.1) |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-04-11 after Phase 15 (Connection Encryption) complete*
