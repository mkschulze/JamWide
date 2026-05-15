# JamWide

## What This Is

JamWide is a cross-platform NINJAM client for real-time online music collaboration. Built as a JUCE-based VST3/AU/CLAP audio plugin and standalone application. It connects musicians over the internet using the NINJAM protocol with FLAC lossless and Vorbis encoding, multichannel output routing for per-user DAW mixing, transport sync, and a Voicemeeter Banana-inspired dark UI.

## Core Value

Musicians can jam together online with lossless audio quality and per-user mixing — in any DAW or standalone.

## Current Milestone: v1.3 Native Video

**Goal:** Replace the VDO.Ninja browser companion with a native in-app/in-plugin video stack using NinjamZap-compatible H264 wire format and GUID-pairing audio-video sync. Reach a testable beta on **macOS + Windows** (Apple Silicon + Intel + Windows x86_64), backed by **ninjamzap-server** as the recommended reference server (per-room threading + two-pass audio-priority + per-subscriber video congestion drop).

**Target features:**
- Webcam capture in standalone and DAW-hosted plugin (VST3/AU/CLAP) with permission UX for DAWs that don't carry `com.apple.security.device.camera` themselves (REAPER, Live, Bitwig)
- H.264 encode/decode via vendored LGPL ffmpeg + Cisco openh264 (substrate landed in Phase 14.3)
- NinjamZap-compatible wire format: fourCC `H264`, 24-byte interval marker, 4-stage receive pipeline (`accumulating → next → pending → playing`), GUID-pairing decision tree (`kHoldCapDrop = 4`)
- Native rendering — `juce::Component` grid in main view + `juce::DocumentWindow` per-user popouts (multi-monitor friendly), both active simultaneously
- **macOS arm64 + x86_64 universal binary** with per-dylib codesigning and camera entitlement
- **Windows x86_64 build** with bundled ffmpeg DLLs and signtool codesigning (where applicable)
- **Reference server** — JamWide ships a `docs/SERVER.md` pointing v1.3 beta testers at the public `video.ninjamzap.com:2049` ninjamzap-server (community-operated, no SLA — users manually enter the address into the existing JamWide server browser), plus instructions for self-hosting upstream `ninjamzap-server` (Docker Compose example) for users who want their own instance

**Out of beta scope (deferred to v1.3 post-beta or v1.4):**
- Linux full client (capture + receive) — Linux V4L2 capture is Item K, post-v1; Linux receive can ship in a follow-up
- VDO.Ninja teardown (Item H) — kept operational in parallel until native stack is testable
- JamWide-owned ninjamzap-server fork or upstream contributions — for beta we ship docs pointing at upstream (Q8 = option (c))
- Full per-DAW UAT matrix (Item I) — reduced to macOS standalone + REAPER + Logic + Windows standalone + REAPER for beta

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

### Active (v1.3 — Native Video — beta scope)

- [ ] Webcam capture in standalone + DAW-hosted plugin with permission-denial fallback
- [ ] H.264 encode/decode end-to-end (capture → encode → wire → decode → display)
- [ ] NinjamZap-compatible wire format (`H264` fourCC, 24-byte marker, 4-stage receive, GUID-pairing decision tree)
- [ ] Native rendering: grid view + per-user popout windows (both active simultaneously)
- [ ] macOS arm64 + x86_64 universal binary with camera entitlement and per-dylib codesigning

### In-flight, parallel to v1.3 (not part of beta gate)

- [ ] OSC server for remote control via TouchOSC (v1.1, Phase 9 partially complete) — paused
- [ ] Audio prelisten in server browser (v1.1, Phase 14.1, plan 1 of 2 complete) — paused
- [ ] RT-Safety hardening UAT closure (v1.2, Phase 15.1) — awaiting UAT decision, orthogonal to v1.3

### Out of Scope

- REAPER-specific extension APIs — not portable; OSC is the path
- Capability negotiation for codecs — deferred to v2
- Mobile support — desktop first
- Peer-to-peer audio — NINJAM is server-relayed by design
- MCP for real-time DAW sync — request/response model incompatible with streaming
- ~~Video embedded in plugin~~ — **reversed 2026-05-15**; v1.3 supersedes the VDO.Ninja browser approach with a native in-app/in-plugin stack (see Current Milestone)

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
| VDO.Ninja browser companion (not embedded WebView) | Keeps plugin lightweight, browser handles video rendering | — **Superseded by v1.3 native stack (2026-05-15)** |
| OSC via juce_osc (IEM pattern) | No external deps, proven across 20+ IEM plugins | — Pending (v1.1) |
| Index-based OSC addressing for remote users | Stable fader mapping, name broadcast on roster change | — Pending (v1.1) |
| Native ffmpeg + JUCE CameraDevice (v1.3, replaces VDO.Ninja) | Spike measured 5.2 MB / arch, 4% CPU, ~98 kbps @ 320×240; LGPL discipline via `--disable-gpl --disable-libx264 --enable-libopenh264`; substrate landed as Phase 14.3 | ✓ Spike GREEN, substrate complete |
| NinjamZap wire format (not JamTaba) | fourCC `H264`, 24-byte marker with audio-ch0 GUID, 4-stage pipeline + GUID-pairing decision tree → fixes "video 1 interval ahead of audio" bug that any naive interval-based transport has; gains future NinjamZap-mobile interop, sacrifices JamTaba interop | Locked 2026-05-15 |
| Native rendering: grid + popouts (no browser in v1.3) | Preserves VDO.Ninja's two strengths (multi-monitor grid + per-user windows) without WebSocket/HTML/TS dependency surface | Locked 2026-05-15 |
| macOS+Windows full send+receive parity v1; Linux receive-only v1, capture deferred | JUCE `juce_video` has no `juce_CameraDevice_linux.h`; Linux capture is post-v1 (Item K, single phase) | Locked 2026-05-15 |
| Stay on OGGv (Vorbis-in-Ogg) for v1.3 audio codec | ninjamzap-core has no Opus support (exhaustive grep verified); Opus is tracked separately as Phase 16 in v1.2 | Locked 2026-05-15 |
| Use upstream ninjamzap-server as the reference JamWide server (Q8 = option (c), document-only) | ninjamzap-server is multithreaded (per-room threading + two-pass audio-priority + per-subscriber video-frame congestion drop), already production-quality on iOS/Android. Shipping a JamWide fork or pushing upstream changes adds maintenance burden without proportional value for the beta. Doc-only path is ~1 plan of work vs 2-3 for a fork. | Locked 2026-05-15 (user redirect) |
| Point v1.3 beta testers at `video.ninjamzap.com:2049` via `docs/SERVER.md` — no UI preset entry needed | Public ninjamzap-server is already running. Eliminates the "deploy server before testing video" friction that would otherwise gate every beta tester. JamWide's existing server browser already supports manual server-address entry (the existing NINJAM server browser is untouched by v1.3), so no UI work is required to support `video.ninjamzap.com:2049` — only docs naming it as the recommended v1.3 beta target. Self-host path also documented for users who want their own latency/privacy guarantees. | Locked 2026-05-15 (user redirect, third pass) |
| v1.3 beta scope expanded from macOS-only to macOS + Windows | User flagged Windows is a first-class beta platform. JUCE `juce_CameraDevice_windows.h` exists; ffmpeg builds for Windows-x86_64 with the same `--disable-gpl --disable-libx264 --enable-libopenh264` recipe (Item B.2). Adds Windows packaging + codesign + CI lane; no Linux capture. | Locked 2026-05-15 (user redirect) |

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
*Last updated: 2026-05-15 — milestone v1.3 (Native Video) started; substrate Phase 14.3 (NinjamZap-compatible RawData transport API + cross-platform LGPL ffmpeg vendoring) already complete; beta scope is macOS + Windows on ninjamzap-server*
