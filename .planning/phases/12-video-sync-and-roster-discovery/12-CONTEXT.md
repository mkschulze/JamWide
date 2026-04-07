# Phase 12: Video Sync and Roster Discovery - Context

**Gathered:** 2026-04-07
**Status:** Ready for planning

<domain>
## Phase Boundary

Interval-synced video buffering, roster discovery by username convention, room security via hash fragment, and bandwidth profile selection. Covers VID-08 (buffer sync), VID-09 (room security), VID-10 (roster discovery), VID-12 (bandwidth profiles). Does NOT include popout mode (VID-07), OSC video control (VID-11) — those are Phase 13.

</domain>

<decisions>
## Implementation Decisions

### Interval-Synced Video Buffering (VID-08)
- **D-01:** Plugin calculates video buffer delay from current BPM/BPI and sends it via WebSocket to the companion page. Companion page forwards via postMessage to the VDO.Ninja iframe using setBufferDelay.
- **D-02:** Delay updates automatically on every BPM/BPI change (not on every beat). This is a session-level parameter that changes rarely.
- **D-03:** Buffer delay formula: `delay_ms = (60.0 / bpm) * bpi * 1000`. This matches the full NINJAM interval length so video participants see each other "in time" with the interval audio.
- **D-04:** New WebSocket message type: `{"type":"bufferDelay","delayMs":N}`. Plugin sends this on connect and whenever BPM/BPI changes. Companion page calls `iframe.contentWindow.postMessage({action:"setBufferDelay",value:N}, "*")`.

### Room Security (VID-09)
- **D-05:** Room password derived from NINJAM session password using SHA-256, passed as `&hash=#fragment` in VDO.Ninja URL. The fragment is never sent to the server (browser-only), preventing plaintext credential exposure.
- **D-06:** Hash derivation: `SHA-256(ninjam_password + ":" + room_id)`. Same WDL_SHA1 approach as Phase 11 room ID but with password as additional input. Deterministic — all JamWide users with the same password on the same server get the same hash.
- **D-07:** If no NINJAM password is set (public server), no hash is applied. Public rooms remain open, consistent with Phase 11 D-17 behavior.

### Roster Discovery (VID-10)
- **D-08:** Roster matching uses the username sanitization convention from Phase 11 (D-22, D-23). Each NINJAM user's sanitized name matches their VDO.Ninja push= stream ID. No external API connection needed.
- **D-09:** Companion page receives roster updates via existing WebSocket `roster` message (Phase 11 D-14). For each user in the roster, the companion page shows a name label overlay on or near the corresponding VDO.Ninja video stream.
- **D-10:** If a VDO.Ninja stream ID doesn't match any roster entry, show the stream without a label (graceful degradation — visitor or name mismatch).
- **D-11:** No VDO.Ninja external API (wss://api.vdo.ninja) in this phase. The API is self-labeled DRAFT and username convention is sufficient for the core use case. External API can be added later if needed.

### Bandwidth Profiles (VID-12)
- **D-12:** Quality dropdown in the companion page header, next to the existing effects dropdown. Three presets: Low (360p, 500kbps), Balanced (720p, 1.5Mbps), High (1080p, 3Mbps).
- **D-13:** Selection persists via localStorage (same pattern as effects dropdown). Default: Balanced.
- **D-14:** VDO.Ninja URL parameters per profile: `&quality=0` (low), `&quality=1` (balanced), `&quality=2` (high). Plus `&maxvideobitrate=` for explicit cap.
- **D-15:** Changing the profile requires iframe reload (same as changing effects). Dropdown change triggers `loadVdoNinjaIframe()` with updated URL params.

### Claude's Discretion
- Exact postMessage format for setBufferDelay (VDO.Ninja's expected message structure)
- CSS layout for name label overlays on video streams
- Fallback behavior if VDO.Ninja ignores setBufferDelay (e.g., chunked mode not available)
- Exact VDO.Ninja URL parameters for each bandwidth tier

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### VDO.Ninja Integration
- `.planning/research/FEATURES.md` — VDO.Ninja API commands, setBufferDelay, chunked mode, external API assessment
- `.planning/research/SUMMARY.md` — Dependency decisions, companion page architecture
- `.planning/phases/11-video-companion-foundation/11-CONTEXT.md` — Phase 11 decisions (WebSocket protocol, room ID derivation, username sanitization)
- `.planning/phases/11-video-companion-foundation/11-RESEARCH.md` — VDO.Ninja URL parameters, WebSocket patterns, mixed content constraints

### Existing Implementation
- `juce/video/VideoCompanion.h` — Frozen public interface (do not change)
- `juce/video/VideoCompanion.cpp` — WebSocket server, room ID derivation, roster broadcast
- `companion/src/ui.ts` — VDO.Ninja URL builder, effects dropdown, iframe loading
- `companion/src/main.ts` — WebSocket callback wiring, state management
- `companion/src/ws-client.ts` — WebSocket client with message validation
- `companion/src/types.ts` — ConfigMessage, RosterMessage type definitions

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `VideoCompanion::broadcastRoster()` — Already sends roster JSON via WebSocket (Phase 11)
- `VideoCompanion::sendConfigToClient()` — Config message pattern to extend with bufferDelay
- `companion/src/ui.ts` effects dropdown — Pattern for adding bandwidth dropdown
- `companion/src/types.ts` type guards — Pattern for validating new message types

### Established Patterns
- WebSocket message protocol: `{"type":"...", ...}` with type guard validation on companion side
- localStorage persistence for companion page settings (effects dropdown)
- VDO.Ninja iframe URL construction via `buildVdoNinjaUrl()` in ui.ts
- BPM/BPI available via `NJClient::GetActualBPM()` and `NJClient::GetBPI()`

### Integration Points
- Plugin → Companion: New `bufferDelay` message type via existing WebSocket
- Plugin → Companion: Room hash appended to existing companion URL
- Companion → VDO.Ninja iframe: postMessage for setBufferDelay
- Companion UI: New bandwidth dropdown in header, name labels over video grid

</code_context>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches for postMessage and overlay labels.

</specifics>

<deferred>
## Deferred Ideas

- VDO.Ninja external API (wss://api.vdo.ninja) — deferred until username convention proves insufficient
- Per-user video quality settings — one profile for all streams is sufficient for now
- Video recording integration — out of scope, OBS handles this

</deferred>

---

*Phase: 12-video-sync-and-roster-discovery*
*Context gathered: 2026-04-07*
