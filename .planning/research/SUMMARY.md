# Project Research Summary

**Project:** JamWide v1.1 — OSC Remote Control + VDO.Ninja Video Companion
**Domain:** JUCE audio plugin networking — OSC bidirectional control, browser-based video companion
**Researched:** 2026-04-05
**Confidence:** HIGH (OSC layer), MEDIUM (VDO.Ninja video companion)

## Executive Summary

JamWide v1.1 adds two independent feature sets to the existing JUCE NINJAM plugin: OSC remote control via TouchOSC and a browser-based video companion via VDO.Ninja. The OSC layer is well-understood with a battle-tested reference implementation available on disk (IEM Plugin Suite). The entire OSC stack — `juce_osc` module, bidirectional UDP, parameter bridge, timer-based feedback — requires zero new external dependencies beyond adding one JUCE module to CMakeLists.txt. The pattern is adapt-from-reference, not invent.

The video layer is more novel and carries meaningful risk. The core insight driving the architecture is that VDO.Ninja iframes cannot be embedded in a locally-served HTTP page without mixed-content failures. The recommended solution — confirmed by pitfalls research — is to serve the companion page from a publicly-hosted HTTPS URL (e.g., GitHub Pages) and have the plugin communicate via a local WebSocket server only. This avoids TLS certificate complexity in the plugin while satisfying browser security requirements. The `cpp-httplib` library handles the local WebSocket server; the VDO.Ninja external API (`wss://api.vdo.ninja:443`) provides roster discovery and stream control. Both require OpenSSL linkage, which is the main build-system risk (especially on Windows).

The two feature sets are architecturally independent — either can ship without the other. The recommended build order treats them as sequential: OSC first (proven patterns, no new deps, immediate user value), then video companion in two stages (local server foundation, then advanced roster sync and per-user popouts). Three critical constraints must be respected throughout: the SPSC `cmd_queue` has a single-producer invariant that OSC callbacks must not violate, the audio thread must never touch OSC or WebSocket paths, and the VDO.Ninja external API WebSocket disconnects every ~60 seconds and requires a reconnection state machine from day one.

## Key Findings

### Recommended Stack

The OSC stack requires only one addition to the existing build: `juce::juce_osc` added to `target_link_libraries`. Everything else — `OSCReceiver`, `OSCSender`, pattern matching, typed message args — is already bundled with JUCE 8.0.12. The IEM Plugin Suite on disk provides the exact implementation patterns to follow including `OSCParameterInterface`, `OSCReceiverPlus`, and `OSCStatus` components.

The video stack requires `cpp-httplib` (single-file header, MIT, v0.41.0) vendored into `src/third_party/` matching the existing `picojson.h` pattern, plus OpenSSL linkage for WSS to `api.vdo.ninja`. Companion HTML/JS/CSS is embedded via `juce_add_binary_data()` — a core JUCE feature requiring no new dependencies. The companion page itself is served externally (GitHub Pages) to avoid mixed-content issues; the plugin only runs a local WebSocket server.

**Core technologies:**
- `juce_osc` (JUCE 8.0.12): bidirectional UDP OSC — already bundled, zero external deps
- `cpp-httplib` v0.41.0: local WebSocket server for plugin-to-browser sync — header-only, MIT, handles HTTP+WS+WSS in one file
- OpenSSL 3.x: TLS for WSS to `api.vdo.ninja` — system install on macOS/Linux, vcpkg on Windows
- `juce_add_binary_data`: embed companion CSS/JS as C++ resources — core JUCE CMake feature
- VDO.Ninja external API (`wss://api.vdo.ninja:443`): roster discovery and stream control — JSON over WSS

**What to avoid:**
- Boost.Beast, IXWebSocket, WebSocketPP — unnecessary complexity or missing capabilities vs. cpp-httplib
- JUCE WebBrowserComponent — CMakeLists.txt already sets `JUCE_WEB_BROWSER=0`; adds 50-100MB and DAW sandboxing issues
- Serving companion page from local HTTP and embedding VDO.Ninja iframes — mixed-content block

### Expected Features

**Must have (table stakes for v1.1 launch):**
- Bidirectional OSC — receivers expect fader feedback; one-way OSC feels broken to anyone using TouchOSC
- Volume, pan, mute, solo for all channels — the entire point of remote control
- Configurable send/receive ports with persistence — port collisions are common; re-entering every session is unacceptable
- OSC status indicator in plugin UI — visual confirmation OSC is working
- Session state broadcast (BPM, BPI, beat, user count, connection status) — enables TouchOSC dashboards
- One-click VDO.Ninja video launch with auto room ID derived from NINJAM server — zero-config video
- Audio suppression (`&noaudio`) in all generated VDO.Ninja URLs — prevents double audio from WebRTC + NINJAM
- Privacy notice on first video use — WebRTC exposes IP addresses via STUN; every professional tool discloses this
- Chromium browser detection and warning — chunked mode (required for music sync) is Chromium-only

**Should have (differentiators that set JamWide apart):**
- Index-based remote user OSC addressing with stable indices — no other NINJAM client exposes remote users via OSC
- Shipped TouchOSC template (`.tosc` file) — Gig Performer's most-praised OSC feature; dramatically lowers adoption barrier
- Companion HTML page with CSS grid layout — better than raw VDO.Ninja room view
- VDO.Ninja external API roster discovery — enables per-user popout windows and username-to-stream mapping
- NINJAM-interval-synced video buffering via `setBufferDelay` — no other tool combines NINJAM awareness with VDO.Ninja video
- Per-user popout windows — multi-monitor support for individual bandmates
- OSC-controllable video commands (`/JamWide/video/*`) — unified control surface for audio + video
- Connect/disconnect via OSC — full remote operation without touching the laptop

**Defer to v2+:**
- Embedded video via JUCE WebBrowserComponent
- Username-based OSC addressing (index-based is superior for TouchOSC layout stability)
- MIDI remote control (OSC is strictly superior; TouchOSC bridges MIDI natively)
- OSC over TCP
- Self-hosted VDO.Ninja signaling server

### Architecture Approach

The architecture adds two independent layers to the existing three-thread model (audio thread, message thread, NinjamRunThread). The OSC layer lives entirely on the message thread: an `OscServer` owns `OSCReceiver` and `OSCSender`, a `OscParameterBridge` maps addresses to APVTS params and `cmd_queue` commands (using `callAsync()` to preserve the single-producer invariant), and an `OscRemoteUserMapper` maintains stable 1-based user indices. The video layer runs on two dedicated background threads — one for the `CompanionServer` (cpp-httplib local WebSocket) and one for `VDONinjaAPIClient` (WSS to external API) — coordinated by a `VideoManager` on the message thread. The critical constraint is that OSC callbacks must never push directly to `cmd_queue` from juce_osc's internal network thread; they must dispatch to the message thread via `callAsync()` first.

**Major components (new):**
1. `OscServer` — owns `OSCReceiver`/`OSCSender`, manages port lifecycle, runs timer-based dirty-tracking send loop (~300 LOC)
2. `OscParameterBridge` — maps OSC addresses to APVTS params and `cmd_queue` commands, dispatches via `callAsync()` (~500 LOC)
3. `OscRemoteUserMapper` — stable 1-based index assignment for remote users, broadcasts roster changes (~200 LOC)
4. `OscConfigPanel` — port/IP config dialog + footer status indicator (~200 LOC)
5. `CompanionServer` — local WebSocket server via cpp-httplib, relays sync JSON to browser (~400 LOC)
6. `VDONinjaAPIClient` — WSS client to `api.vdo.ninja`, roster discovery, auto-reconnect with exponential backoff (~350 LOC)
7. `VideoManager` — coordinates CompanionServer + VDONinjaAPIClient, manages room state, calculates sync delay, triggers browser launch (~300 LOC)
8. `VideoConfigPanel` — UI for video room name, display mode, status (~150 LOC)

**Modified components:** `JamWideJuceProcessor` (state version 1→2), `JamWideJuceEditor` (adds config panels), `ConnectionBar` (video toggle + OSC status dot), `CMakeLists.txt` (juce_osc, cpp-httplib, BinaryData, OpenSSL).

**Unchanged:** `NinjamRunThread`, `NJClient`/core layer, `ChannelStripArea`/`ChannelStrip`, `processBlock()`.

### Critical Pitfalls

1. **OSC callback thread safety** — juce_osc's `RealtimeCallback` fires on its own internal network thread, not the message thread. Pushing directly to `cmd_queue` from this thread violates the SPSC single-producer invariant and can corrupt the queue head pointer. Always dispatch to the message thread via `juce::MessageManager::callAsync()` before touching `cmd_queue` or APVTS. This must be established in Phase 1 before any parameter mapping is built on top.

2. **OSCReceiver port binding silently succeeds on Windows** — JUCE's `DatagramSocket` calls `SO_REUSEADDR` unconditionally; on Windows this allows multiple binds to the same UDP port, returning `true` from `connect()` even when another instance holds the port. The receiver appears connected but receives nothing. Verify actual receipt with a loopback test message; implement port-probing with auto-increment for multi-instance DAW sessions.

3. **OSC bidirectional feedback loop** — naive bidirectional OSC where the plugin echoes received values back to TouchOSC causes runaway oscillation, especially on toggle buttons and discrete parameters. The IEM pattern prevents this: outgoing OSC fires on a timer (100ms default), only sends values changed since the last tick, and suppresses feedback for parameters just received (200ms debounce window). This is a Phase 1 architecture decision, not a polish item.

4. **VDO.Ninja external API disconnects every ~60 seconds** — the API WebSocket has no browser-side keepalive when driven from native C++. Without a reconnection state machine (exponential backoff: 1s, 2s, 4s, 8s, cap 30s) plus periodic ping frames, the plugin silently loses roster data after one minute. The reconnection state machine must be designed from day one in Phase 4.

5. **Mixed content blocks VDO.Ninja iframes in locally-served HTTP pages** — the plugin cannot serve a `http://127.0.0.1` page that embeds `https://vdo.ninja` iframes; browsers block mixed content and camera/mic permissions fail. The solution is to host the companion page on a public HTTPS URL (GitHub Pages) and have the plugin open that URL with parameters. The plugin communicates exclusively via a local `ws://127.0.0.1` WebSocket (localhost is exempted from mixed-content rules per W3C spec). This architectural decision must be resolved before Phase 3 HTML work begins.

## Implications for Roadmap

Based on research, the natural phase structure follows architectural dependencies. OSC and video are independent and OSC has no external deps, so it goes first. Video builds in two stages: local server infrastructure before advanced roster sync, because roster sync depends on the companion page being available as a relay.

### Phase 1: OSC Server Core

**Rationale:** Self-contained, zero new external dependencies (juce_osc already bundled in JUCE 8.0.12), proven IEM reference implementation on disk. Delivers immediate user value — TouchOSC control of master, metro, and local channel volumes — without touching the video stack. All three foundational OSC pitfalls (thread safety, port binding, feedback loop) must be addressed here before any parameter mapping is layered on top.

**Delivers:** Bidirectional UDP OSC, APVTS parameter mapping (master/metro/local channels), configurable ports with persistence, status indicator in plugin footer, session telemetry broadcast (BPM, BPI, beat, connection status).

**Addresses features:** bidirectional OSC, volume/pan/mute control, configurable ports, config persistence, status indicator, session state broadcast.

**Avoids:** OSC callback thread safety violation (callAsync dispatch pattern established), port binding silent failure on Windows (loopback verification + port probing), feedback loop (timer-based dirty tracking with 200ms debounce).

### Phase 2: OSC Remote User Mapping

**Rationale:** Builds on Phase 1 OSC infrastructure. Requires solving the index stability problem across roster changes — a design decision that affects the TouchOSC template (Phase 5) and OSC video control (Phase 5). Must be complete before remote user commands are possible via OSC.

**Delivers:** Stable 1-based index addressing for remote users (`/JamWide/remote/{idx}/volume` etc.), roster change broadcasts with name label updates, remote user volume/pan/mute/solo control via OSC.

**Addresses features:** index-based remote user addressing, roster change broadcast, remote user OSC control.

**Avoids:** Audio thread contention (remote user state routes through `cmd_queue` not direct mutex access; adds up to 50ms latency which is acceptable for mixing), index instability (stable join-order indices with name label broadcast on change).

### Phase 3: Video Companion Foundation

**Rationale:** Independent of OSC phases; can be developed in parallel after Phase 1. The serving architecture decision (GitHub Pages + local WebSocket only, not local HTTP serving companion HTML) must be made and implemented here before any companion JS work. cpp-httplib is added to the project in this phase; OpenSSL linkage must be validated on all CI platforms early.

**Delivers:** One-click VDO.Ninja browser launch with auto room ID derived from NINJAM server address, audio suppression (`&noaudio`), companion page hosted on GitHub Pages, local WebSocket server for plugin-browser sync (cpp-httplib), privacy notice on first use, Chromium browser detection with actionable warning.

**Addresses features:** one-click video launch, auto room ID, audio suppression, privacy notice, browser requirement warning, basic video UI panel.

**Avoids:** Mixed-content CORS failure (companion page on HTTPS GitHub Pages, plugin only runs local WebSocket), WebSocket server without auth token (random token per session, reject connections without it), localhost binding on `0.0.0.0` (bind `127.0.0.1` only).

### Phase 4: Video Sync and Roster Discovery

**Rationale:** Requires Phase 3 CompanionServer operational to relay sync data to the browser. Highest uncertainty phase — VDO.Ninja external API is documented as DRAFT, the ~60-second WebSocket timeout requires a robust reconnection state machine, and chunked buffer accuracy under real NINJAM session conditions is untested. Design the state machine carefully; do not defer reconnection logic as a "polish item."

**Delivers:** VDO.Ninja external API connection (`wss://api.vdo.ninja:443`), guest roster discovery, NINJAM username to stream ID mapping, NINJAM-interval-synced video buffering via `setBufferDelay`, room password security via `&hash=` fragment.

**Addresses features:** roster discovery, interval-synced video buffering, room password security.

**Avoids:** Silent API disconnect (exponential backoff reconnection state machine: 1s/2s/4s/8s/30s max + 30s keepalive pings), stale roster after reconnect (reconcile fresh `getGuestList` result against cached state), stream ID corruption (sanitize NINJAM usernames to alphanumeric + hyphens).

### Phase 5: Display Modes, Popout Windows, and OSC Video Control

**Rationale:** Polish and UX. Requires Phase 2 (OscRemoteUserMapper for index correlation with per-user popouts) and Phase 4 (VideoManager with stream mapping for popout window URLs). Companion JS complexity peaks here with `window.open()` per-user popout management. The TouchOSC template cannot be finalized until the OSC namespace is stable (locked in Phase 2).

**Delivers:** Per-user popout windows (separate browser windows per stream ID), grid/popout mode switching in companion JS, OSC `/JamWide/video/*` commands, connect/disconnect via OSC (`/JamWide/connect`), shipped TouchOSC template (`.tosc` file), bandwidth-aware video profiles (`&chunkprofile=`).

**Addresses features:** per-user popout, video control via OSC, connect/disconnect via OSC, TouchOSC template, bandwidth video profiles.

**Avoids:** Orphan browser windows without context (window titles set to "JamWide Video — username"), multiple popouts interfering with each other (plugin provides "Close All Popouts" button), OSC video open triggering duplicates (check if companion page already open before launching).

### Phase Ordering Rationale

- OSC before video because OSC has zero new external dependencies, established patterns in the IEM reference, and delivers user value independently.
- OSC Core (Phase 1) before Remote Users (Phase 2) because the threading contract and feedback-loop prevention architecture must exist before dynamic user addressing is layered on top.
- Video Foundation (Phase 3) before Video Sync (Phase 4) because `CompanionServer` is the relay path that `VDONinjaAPIClient` depends on to push sync data to the browser.
- Display Modes (Phase 5) last because it depends on both OSC index mapping (Phase 2) and roster discovery (Phase 4), and represents polish that does not block core functionality.
- Phases 3-5 (video) have no code dependency on Phase 2 (OSC remote users) and can proceed in parallel after Phase 1 if development bandwidth allows.

### Research Flags

Phases needing deeper research during planning:
- **Phase 3 (Video Foundation):** OpenSSL linkage on Windows CI is the primary build risk — the project currently has no OpenSSL dependency. Validate `find_package(OpenSSL REQUIRED)` with vcpkg before writing companion server code. Also validate cpp-httplib WebSocket stability in a JUCE plugin context (WebSocket feature appeared in v0.38.0; not tested in a real DAW host).
- **Phase 4 (Video Sync):** VDO.Ninja external API is self-labeled DRAFT. Behavior of `getGuestList`, `guest-connected`/`push-connection` events, and `setBufferDelay` accuracy under real NINJAM session conditions all need early prototyping before full implementation. Build a standalone test client first.

Phases with standard patterns (skip research-phase):
- **Phase 1 (OSC Core):** IEM Plugin Suite on disk is the reference. Pattern is adapt-from-reference. juce_osc is thoroughly documented.
- **Phase 2 (OSC Remote Users):** Extends Phase 1 with index mapping logic. No new external APIs or libraries. Standard JUCE threading patterns.
- **Phase 5 (Display Modes + Template):** Browser JS popout management is standard `window.open()`. TouchOSC `.tosc` format is documented. No new unknowns beyond Phase 4 stream IDs being available.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack (OSC) | HIGH | juce_osc verified from local source headers; IEM reference implementation verified on disk; JUCE OSC tutorial and TouchOSC manual verified |
| Stack (Video) | MEDIUM | cpp-httplib WebSocket feature is relatively new (v0.38.0+); not tested in JUCE plugin context; OpenSSL on Windows CI unverified |
| Features | MEDIUM-HIGH | OSC features HIGH (IEM/JUCE docs + TouchOSC manual + competitor analysis); VDO.Ninja features MEDIUM (API docs self-labeled DRAFT) |
| Architecture | HIGH (OSC) / MEDIUM (Video) | OSC threading model verified against existing JamWide architecture; SPSC single-producer constraint confirmed from codebase; VDO.Ninja iframe security behavior needs early validation |
| Pitfalls | HIGH | JUCE OSC threading issues verified from JUCE forum threads (2018-present); port binding Windows behavior is a documented known issue; VDO.Ninja ~60s timeout documented in official API docs |

**Overall confidence:** MEDIUM-HIGH

### Gaps to Address

- **cpp-httplib WebSocket in JUCE plugin context:** Thread-per-connection model assumed safe for 3-5 connections but not validated in a real DAW host. Prototype early in Phase 3 and test that `svr.listen()` on a background thread does not conflict with DAW sandbox restrictions on macOS (Logic Pro, GarageBand) and Windows (Ableton).
- **VDO.Ninja chunked buffer sync accuracy:** `setBufferDelay` is accepted by the iframe API, but actual buffer accuracy under real NINJAM timing (variable BPM, BPI changes mid-session) is unknown. Accept that initial implementation may require tuning based on user feedback; do not promise frame-accurate sync.
- **OpenSSL linkage on Windows CI:** The project currently has no OpenSSL dependency. Windows vcpkg integration needs to be validated in CI before Phase 3 begins. Fallback option: `CPPHTTPLIB_MBEDTLS_SUPPORT` (Apache-2.0, smaller binary).
- **VDO.Ninja external API stability:** API marked DRAFT. Events and command format confirmed via Companion-Ninja sample app but subject to change. Design `VDONinjaAPIClient` with a clear JSON parsing layer that is easy to update if the API evolves.
- **Chromium browser detection cross-platform:** Detecting the system default browser is platform-specific (registry on Windows, Launch Services on macOS, `xdg-settings` on Linux). Companion page JS User-Agent detection is simpler and more reliable; implement detection client-side after the page loads rather than in native C++.

## Sources

### Primary (HIGH confidence)
- `libs/juce/modules/juce_osc/` (local, v8.0.12) — OSCReceiver, OSCSender, OSCAddress class API
- `/Users/cell/dev/IEMPluginSuite/resources/OSC/` (local) — OSCParameterInterface, OSCReceiverPlus, OSCStatus reference implementation across 20+ shipping plugins
- `docs/superpowers/specs/2026-04-05-v1.1-osc-video-design.md` (local) — authoritative design decisions for v1.1
- [JUCE OSC tutorial](https://juce.com/tutorials/tutorial_osc_sender_receiver/) — sender/receiver patterns
- [TouchOSC OSC Connections Manual](https://hexler.net/touchosc/manual/connections-osc) — port config, bidirectional behavior expectations
- [TouchOSC OSC Messages Manual](https://hexler.net/touchosc/manual/editor-messages-osc) — layout design patterns, label binding
- [cpp-httplib GitHub](https://github.com/yhirose/cpp-httplib) — HTTP + WebSocket server + WSS client API (v0.41.0, 2025-04-04)
- [JUCE forum: OSCReceiver port binding always returns true](https://forum.juce.com/t/oscreceiver-binding-to-a-port-always-return-true-even-if-port-is-already-taken/25885) — Windows SO_REUSEADDR behavior (known issue since 2018)
- [IEM Plugin Suite OSCParameterInterface.cpp](https://git.iem.at/audioplugins/IEMPluginSuite/-/blob/175-10th-ambisonic-order/resources/OSC/OSCParameterInterface.cpp) — feedback loop prevention pattern

### Secondary (MEDIUM confidence)
- [VDO.Ninja API reference](https://docs.vdo.ninja/advanced-settings/api-and-midi-parameters/api/api-reference) — external API commands (self-labeled DRAFT)
- [VDO.Ninja &api parameter](https://docs.vdo.ninja/advanced-settings/api-and-midi-parameters/api) — WebSocket connection details, reconnection behavior
- [VDO.Ninja iframe API basics](https://docs.vdo.ninja/guides/iframe-api-documentation/iframe-api-basics) — postMessage API for setBufferDelay
- [VDO.Ninja chunked mode](https://docs.vdo.ninja/advanced-settings/settings-parameters/and-chunked) — buffer parameters, Chromium-only requirement
- [Companion-Ninja GitHub](https://github.com/steveseguin/Companion-Ninja) — HTTP/WebSocket API patterns, guest-connected/push-connection events confirmed
- [cpp-httplib WebSocket README](https://github.com/yhirose/cpp-httplib/blob/master/README-websocket.md) — thread-per-connection WebSocket API
- [JamTaba GitHub](https://github.com/elieserdejesus/JamTaba) — H.264 video frame rate benchmarks (0.03-0.13 FPS at NINJAM BPI settings)
- [Gig Performer TouchOSC guide](https://gigperformer.com/using-touch-osc-app-as-a-remote-display-and-a-touch-control-surface-with-gig-performer) — template shipping as highest-value OSC adoption feature

### Tertiary (LOW confidence)
- [Cockos NINJAM + video forum thread](https://forum.cockos.com/showthread.php?t=213553) — community pain points around video sync (anecdotal)
- VDO.Ninja chunked buffer sync accuracy under real-world NINJAM sessions — not yet validated; inferred from API documentation only

---
*Research completed: 2026-04-05*
*Ready for roadmap: yes*
