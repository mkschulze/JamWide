# Feature Research: v1.1 OSC Remote Control + VDO.Ninja Video

**Domain:** OSC remote control and browser-based video companion for a NINJAM audio plugin
**Researched:** 2026-04-05
**Confidence:** MEDIUM-HIGH (OSC: HIGH based on IEM reference + JUCE docs; Video: MEDIUM based on VDO.Ninja API docs which are self-described as DRAFT)

## Feature Landscape

### Table Stakes (Users Expect These)

Features that users of OSC-controllable audio plugins and VDO.Ninja video integrations assume exist. Missing these makes the feature feel broken or incomplete.

#### OSC Remote Control

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Bidirectional OSC (send + receive) | Every OSC-capable audio tool (IEM Suite, Gig Performer, RME TotalMix FX, X32/M32 mixers) sends parameter feedback. One-way OSC feels broken -- faders on TouchOSC must snap to current positions. | MEDIUM | IEM pattern: timer-based sender (100ms default), realtime receiver callback. JUCE `juce_osc` provides both `OSCSender` and `OSCReceiver`. |
| Volume/pan/mute control for all channels | Users expect to control the same parameters via OSC that they see in the plugin UI. This is the entire point of remote control. | MEDIUM | Map to existing UiCommand variants: `SetLocalChannelInfoCommand`, `SetUserChannelStateCommand`. Requires adapter from OSC float values to internal representations. |
| Configurable ports (send + receive) | Every OSC implementation requires user-configurable UDP ports. Default port collisions are extremely common (many apps default to 8000 or 9000). | LOW | Receiver port + sender target IP/port. IEM uses a dialog with text fields. Persist in plugin state. |
| Feedback on state changes (not just parameter echo) | Connection status, user roster changes, beat position -- users expect to see session state on their control surface, not just mirror fader positions. | MEDIUM | Timer-based sender broadcasts changed values. Must include read-only session state (BPM, BPI, beat, user count, connection status). |
| Config persistence across sessions | OSC settings (ports, target IP, interval) must survive DAW save/load. Re-entering ports every session is unacceptable. | LOW | Save/restore via plugin state serialization. IEM stores as `ValueTree` config. JamWide uses `UiState` snapshot -- add OSC fields. |
| Status indicator in UI | IEM Suite, Gig Performer, and RME TotalMix all show OSC connection status. Users need visual confirmation that OSC is active. | LOW | IEM pattern: colored dot in footer (green=connected, red=error, grey=off). Click opens config dialog. Direct port of `OSCStatus` component. |
| Solo control for remote users | Solo is a standard mixer feature. If volume/pan/mute are controllable, solo must be too. Omitting it creates confusion. | LOW | Already exists in UI. Add OSC address mapping. |

#### VDO.Ninja Video Companion

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| One-click video launch | Users expect "click button, video appears." Any multi-step setup (copy URL, open browser, paste) creates friction that kills adoption. | MEDIUM | Plugin generates full VDO.Ninja URL with room ID, username, and all parameters. Opens in system default browser via `juce::URL::launchInDefaultBrowser()`. |
| Automatic room management | Users should not need to manually coordinate VDO.Ninja room IDs. The plugin knows which NINJAM server everyone is on -- use it. | LOW | Auto-generate room ID from NINJAM server address (e.g., `jamwide-ninbot-com-2049`). Override field for custom rooms. |
| Audio suppression in video | VDO.Ninja plays audio by default. Hearing double audio (NINJAM + WebRTC) is immediately confusing and unpleasant. | LOW | Append `&noaudio` to all generated VDO.Ninja URLs. Non-negotiable. |
| Video grid showing all participants | OBS + VDO.Ninja users, Zoom users, Google Meet users -- everyone expects a grid layout as the default video view. | MEDIUM | Companion HTML page with CSS grid layout. VDO.Ninja provides `&view=` parameter to see all room participants. |
| Privacy notice / opt-in | WebRTC exposes IP addresses to peers via STUN. Musicians not expecting this will feel violated. Every professional video tool (Zoom, Meet) explains camera/mic permissions. | LOW | Show a one-time notice explaining IP exposure when video is first enabled. Offer `&relay` mode for privacy-sensitive users (forces TURN, hides IPs). |
| Browser requirement warning | Chunked mode (needed for music sync) requires Chromium-based browsers. Firefox/Safari users will get a broken experience with no explanation. | LOW | Detect default browser on launch. Warn if not Chromium-based. Include fallback URL without `&chunked` for non-Chromium browsers (standard WebRTC, no sync). |

### Differentiators (Competitive Advantage)

Features that set JamWide apart. No existing NINJAM client offers these.

#### OSC Remote Control

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Index-based remote user addressing | TouchOSC layouts can use fixed fader positions (1-8) that map to NINJAM users. Name labels update on roster change. No other NINJAM client exposes remote users via OSC. | MEDIUM | Assign stable 1-based indices in join order. Broadcast `/remote/{idx}/name` on roster change. TouchOSC reads labels, faders stay bound to indices. |
| Shipped TouchOSC template | Most OSC plugins leave template creation to users. Shipping a ready-made `.tosc` file dramatically lowers adoption barrier. Gig Performer ships templates and it is their most-praised OSC feature. | MEDIUM | Design a 2-page layout: Page 1 = mixer (8 remote user faders + local + metro), Page 2 = session info + video controls. Distribute alongside plugin. |
| OSC-controllable video features | No competing product lets you control video companion from a control surface. Opening video, switching grid/popout, triggering per-user popout -- all from an iPad. | LOW | Add `/JamWide/video/*` namespace. Straightforward since video commands are simple triggers/strings. |
| Metronome remote control | Controlling click volume from an iPad while playing is genuinely useful for musicians. Most OSC mixer implementations forget the metronome. | LOW | `/JamWide/metro/volume`, `/metro/pan`, `/metro/mute`. Maps directly to existing atomic parameters. |
| Read-only session telemetry | Beat position, BPM, BPI, user count broadcast via OSC. Enables building custom TouchOSC dashboards showing jam session state. | LOW | Timer-based broadcast of session state atomics. Already accessible in the audio thread via lock-free reads. |
| Connect/disconnect trigger | Ability to connect to or disconnect from a NINJAM server entirely from the control surface. Useful for musicians who keep their laptop out of reach during performance. | LOW | `/JamWide/connect` with 0/1 toggle. Maps to existing `ConnectCommand` / `DisconnectCommand`. |

#### VDO.Ninja Video Companion

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Per-user popout windows | Each remote musician's video can be dragged to a separate monitor. Unique to JamWide -- no other NINJAM client offers this. Lets musicians see specific bandmates on dedicated screens. | HIGH | Each popout is a separate browser window with `&view=streamID`. Requires stream ID discovery via VDO.Ninja external API. Window management is browser-side (plugin just opens URLs). |
| NINJAM-synced video buffering | Video delay matched to NINJAM interval timing via VDO.Ninja's chunked mode `setBufferDelay`. Participants see each other "in time" with the interval audio. No other tool does this. | HIGH | Requires: chunked mode, companion page WebSocket, interval boundary tracking, `postMessage` to VDO.Ninja iframe. Complex chain but each link is documented. |
| Plugin-controlled room security | Room password derived from NINJAM server password. Uses `&hash=` with `#` fragment to prevent plaintext credential exposure. Zero-config security. | MEDIUM | Hash derivation needs a deterministic algorithm shared between all JamWide instances. SHA-256 of NINJAM password + room name as salt. |
| External API roster discovery | Plugin connects to `wss://api.vdo.ninja` for real-time guest list. Maps VDO.Ninja stream IDs to NINJAM usernames for 1:1 video-to-audio correlation. | HIGH | WebSocket client with JSON messaging, reconnection logic (timeout every ~60s), event handling for `guest-connected`/`push-connection`. New dependency: WebSocket client library or JUCE networking. |
| Video control via OSC | Open/close video, switch modes, trigger popouts -- all from TouchOSC. Single control surface for audio + video. | LOW | Thin layer mapping OSC messages to video control commands. |
| Bandwidth-aware video profiles | VDO.Ninja supports `&chunkprofile=mobile|balanced|desktop`. Plugin can auto-select based on detected bandwidth or let users choose. | LOW | Append profile parameter to generated URLs. UI dropdown or auto-detect. |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Embedded video in plugin window | "I want video without opening a browser" | JUCE WebBrowserComponent or CEF adds 50-100MB to plugin size, introduces Chromium dependency, platform-specific WebRTC issues, massive build complexity. Browser does this better and for free. | Browser companion page. Plugin opens a URL. Browser handles all video rendering, WebRTC, and codec work. |
| Username-based OSC addressing (`/remote/Dave/volume`) | "I want to address users by name" | Names can contain spaces, special characters, unicode. Names change mid-session. TouchOSC layouts break when names change. OSC address parsing becomes complex. | Index-based addressing with name broadcast. `/remote/1/volume` is stable. `/remote/1/name` broadcasts "Dave" for label display. |
| Auto-discovery of OSC clients | "Plugin should find my iPad automatically" | OSC has no standard discovery protocol. mDNS/Bonjour is platform-specific and unreliable on many networks. Adds complexity for a one-time setup. | Manual IP/port configuration with sensible defaults. One-time setup per device. |
| Video recording from within plugin | "Record the jam video too" | VDO.Ninja video is rendered in the browser, not the plugin. Recording browser content from a plugin is not feasible. OBS already does this well. | Document how to use OBS to capture VDO.Ninja alongside JamWide audio. Provide OBS integration guide. |
| MIDI-based remote control (instead of or alongside OSC) | "My controller sends MIDI, not OSC" | MIDI has 7-bit resolution (128 steps), limited addressing, no bidirectional feedback standard. OSC is strictly superior for this use case. Adding MIDI remote adds a second control protocol with less capability. | OSC only. TouchOSC handles MIDI-to-OSC bridging natively. Users with hardware MIDI controllers can use TouchOSC Bridge or similar. |
| Embedded TURN server | "I don't want to depend on external TURN servers" | Running a TURN server requires a public IP, bandwidth, and ongoing maintenance. Only ~10% of users need TURN at all. | Use VDO.Ninja's free TURN servers (CA, DE, US, FR/UK). Document self-hosted TURN option for enterprise/privacy setups via `&turn=` parameter. |
| Real-time video sync (sub-second) with interval audio | "Video should be perfectly in sync with audio" | NINJAM audio has 8-32 second interval latency by design. Delaying video 8-32 seconds makes it a slideshow of the past. Fundamentally incompatible time domains. | Accept that video is near-real-time (100-300ms) and audio is interval-delayed. Musicians already understand this -- they see reactions in real-time and hear music on a delay. This is actually useful: visual cues happen before the audio arrives. |
| H.264-over-NINJAM video (JamTaba approach) | "Keep everything in one protocol" | 0.03-0.13 FPS at typical BPI settings. Literally a slideshow. Adds bandwidth pressure to the NINJAM server. JamTaba users universally report sync problems and poor quality. | VDO.Ninja WebRTC: 30fps, 100-300ms latency, peer-to-peer (no server load), hardware-accelerated. Strictly superior in every dimension. |

## Feature Dependencies

```
[OSC Receiver (UDP listen)]
    |
    +--enables--> [OSC Parameter Mapping]
    |                 |
    |                 +--enables--> [Remote User Index Addressing]
    |                                   |
    |                                   +--enables--> [TouchOSC Template]
    |
    +--enables--> [OSC Sender (UDP feedback)]
                      |
                      +--enables--> [Session Telemetry Broadcast]
                      +--enables--> [Roster Change Broadcast]

[VDO.Ninja URL Generation]
    |
    +--enables--> [One-Click Video Launch]
    |                 |
    |                 +--enables--> [Video Grid Mode]
    |
    +--requires--> [Auto Room ID from NINJAM Server]

[VDO.Ninja External API (WebSocket)]
    |
    +--enables--> [Roster Discovery (stream ID mapping)]
    |                 |
    |                 +--enables--> [Per-User Popout Windows]
    |                 +--enables--> [Video Control via OSC]
    |
    +--requires--> [WebSocket Client Library]
    +--requires--> [Reconnection Logic]

[Companion HTML Page (local server)]
    |
    +--enables--> [Video Grid Layout]
    +--enables--> [Interval-Synced Buffering (setBufferDelay)]
    |                 |
    |                 +--requires--> [Chunked Mode (&chunked)]
    |                 +--requires--> [Interval Boundary Tracking (existing)]
    |
    +--requires--> [Local HTTP Server]
    +--requires--> [Local WebSocket Server (plugin <-> page)]

[OSC Video Namespace] --requires--> [Video Control Commands]
                      --requires--> [OSC Server Core]
```

### Dependency Notes

- **OSC Receiver requires nothing new**: JUCE `juce_osc` module is the only dependency. Add to CMake and it works.
- **OSC Sender requires Receiver**: Bidirectional is the IEM pattern -- sender and receiver share the `OSCParameterInterface` lifecycle.
- **TouchOSC Template requires Index Addressing**: Template must be designed after the OSC namespace is finalized. Cannot ship template until namespace is stable.
- **Video Popout requires External API**: Must discover stream IDs to generate per-user `&view=streamID` URLs. Without API, only grid mode works.
- **Interval Sync requires Companion Page + Chunked Mode**: The `setBufferDelay` command must flow through a local WebSocket to the companion page, which forwards via `postMessage` to the VDO.Ninja iframe. Each link in this chain is a dependency.
- **Local HTTP/WebSocket Server is the video foundation**: Without it, the companion page cannot receive commands from the plugin. This is the critical infrastructure piece for all video features beyond basic URL launch.
- **OSC and Video are architecturally independent**: Either feature can ship without the other. No code dependencies between them. Only the OSC video namespace ties them together at the control layer.

## MVP Definition

### Phase 1: OSC Server Core (Launch First)

Minimum viable OSC remote control -- enough to be genuinely useful with TouchOSC.

- [x] Bidirectional UDP OSC (receiver + sender) via `juce_osc` -- core infrastructure
- [x] Parameter mapping for local channel (volume, pan, mute, transmit) -- immediate value
- [x] Parameter mapping for metronome (volume, pan, mute) -- musicians need click control
- [x] Remote user control with index-based addressing (volume, pan, mute, solo) -- main use case
- [x] Roster change broadcast (count + names) -- TouchOSC labels update automatically
- [x] Session state broadcast (BPM, BPI, beat, status, user count) -- dashboard capability
- [x] Configurable ports with UI dialog -- required for any multi-app setup
- [x] Status indicator in plugin footer -- visual confirmation OSC is working
- [x] Config persistence in plugin state -- survives DAW save/load

### Phase 2: Video Companion Foundation (Launch Second)

Minimum viable video -- enough to see bandmates during a jam.

- [x] VDO.Ninja URL generation with auto room ID -- zero-config video
- [x] One-click browser launch with `&noaudio` + `&cleanoutput` -- clean video-only experience
- [x] Audio suppression (`&noaudio`) -- prevents double audio
- [x] Privacy notice on first use -- responsible IP exposure handling
- [x] Browser detection + Chromium warning -- prevents broken experience
- [x] Basic video UI panel in plugin (room ID display, open/close button, status) -- control center

### Phase 3: Video Advanced Features (Add After Validation)

- [ ] Local HTTP + WebSocket server for companion page hosting -- enables layout control
- [ ] Companion HTML page with CSS grid layout -- better than raw VDO.Ninja room view
- [ ] VDO.Ninja external API connection (WebSocket client) -- roster discovery
- [ ] Stream ID ↔ NINJAM username mapping -- video-audio user correlation
- [ ] Per-user popout windows -- multi-monitor support
- [ ] Interval-synced video buffering via `setBufferDelay` -- unique differentiator
- [ ] Room password security (`&hash=` with `#` fragment) -- zero-config security

### Phase 4: Polish and Templates (Add After Core Stable)

- [ ] TouchOSC template (`.tosc` file) -- dramatically lowers adoption barrier
- [ ] Video control via OSC namespace -- unified control surface
- [ ] Connect/disconnect via OSC -- full remote operation
- [ ] Bandwidth-aware video profiles (`&chunkprofile=`) -- adaptive quality

### Future Consideration (v2+)

- [ ] Embedded video via JUCE WebBrowserComponent -- only if browser companion proves inadequate
- [ ] Username-based OSC addressing -- only if users demand it despite index approach
- [ ] OSC over TCP (for firewalled environments) -- niche use case
- [ ] MIDI remote control -- only if significant demand despite OSC superiority
- [ ] Self-hosted VDO.Ninja signaling server -- enterprise/privacy feature
- [ ] OBS integration guide / scene collection -- community content

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| Bidirectional OSC core | HIGH | MEDIUM | P1 |
| Remote user index addressing | HIGH | MEDIUM | P1 |
| OSC config UI + persistence | HIGH | LOW | P1 |
| Status indicator | MEDIUM | LOW | P1 |
| One-click video launch | HIGH | LOW | P1 |
| Auto room ID | HIGH | LOW | P1 |
| Audio suppression | HIGH | LOW | P1 |
| Session telemetry broadcast | MEDIUM | LOW | P1 |
| Roster change broadcast | HIGH | LOW | P1 |
| Privacy notice | MEDIUM | LOW | P1 |
| Chromium browser warning | MEDIUM | LOW | P1 |
| Local HTTP/WS server | HIGH | HIGH | P2 |
| Companion HTML page grid | HIGH | MEDIUM | P2 |
| External API WebSocket client | HIGH | HIGH | P2 |
| Stream ID mapping | MEDIUM | MEDIUM | P2 |
| Per-user popout windows | MEDIUM | MEDIUM | P2 |
| Interval-synced buffering | MEDIUM | HIGH | P2 |
| Room password security | MEDIUM | MEDIUM | P2 |
| TouchOSC template | HIGH | MEDIUM | P2 |
| Video control via OSC | MEDIUM | LOW | P2 |
| Connect/disconnect via OSC | MEDIUM | LOW | P2 |
| Bandwidth video profiles | LOW | LOW | P3 |
| Embedded video (WebView) | LOW | HIGH | P3 (defer) |
| Username-based OSC | LOW | MEDIUM | P3 (defer) |

**Priority key:**
- P1: Must have for v1.1 launch
- P2: Should have, add in later v1.1 phases
- P3: Nice to have, defer to v1.2+

## Competitor Feature Analysis

### OSC Remote Control

| Feature | IEM Plugin Suite | Gig Performer | RME TotalMix FX | JamWide v1.1 Plan |
|---------|-----------------|---------------|-----------------|-------------------|
| Bidirectional OSC | Yes (timer-based sender) | Yes (widget-based) | Partial (fader output limited) | Yes (IEM pattern) |
| Configurable ports | Yes (dialog) | Yes (preferences) | Yes (settings) | Yes (dialog) |
| Status indicator | Yes (footer dot + dialog) | No (settings only) | Yes (LED indicator) | Yes (footer dot) |
| Parameter feedback interval | Configurable (default 100ms) | Fixed | Fixed | Configurable (1-1000ms, default 100ms) |
| Namespace | `/PluginName/paramID` | Widget OSC names (flat) | `/1/volume1` etc (fixed) | Hierarchical `/JamWide/...` |
| Dynamic user addressing | N/A (fixed parameters) | N/A (fixed widgets) | N/A (fixed channels) | Yes (index-based `/remote/{idx}/`) |
| TouchOSC template shipped | No | Yes (praised feature) | Community templates | Yes (planned) |
| Session telemetry | No | No | No | Yes (BPM, BPI, beat, status) |
| Read-only state broadcast | No | No | Limited | Yes (codec, names, connection) |

**JamWide advantage:** Dynamic user addressing and session telemetry are unique. No audio plugin does this because no audio plugin manages a roster of remote musicians.

### VDO.Ninja Video

| Feature | JamTaba H.264 | VDO.Ninja (standalone) | OBS + VDO.Ninja | JamWide v1.1 Plan |
|---------|--------------|----------------------|-----------------|-------------------|
| Frame rate | 0.03-0.13 FPS | 30 FPS | 30 FPS | 30 FPS (WebRTC) |
| Latency | 8-32 sec (interval) | 100-300ms | 100-300ms | 100-300ms (real-time) |
| Audio-video sync | Synced (but unusable FPS) | Real-time only | Real-time only | Configurable: real-time or interval-matched |
| Setup effort | Built-in (auto) | Manual (URL sharing) | Manual (OBS config) | One-click (auto room ID) |
| Grid view | No | Yes (room view) | Yes (scene layout) | Yes (companion page) |
| Per-user popout | No | PiP (limited) | Yes (separate sources) | Yes (dedicated windows) |
| Plugin integration | Native | None | None | Native (OSC + URL generation) |
| Music sync buffer | No | Available (chunked demo) | No | Yes (setBufferDelay via API) |
| Bandwidth control | N/A (tiny data) | Manual parameters | Manual | Auto profiles + manual |
| Privacy controls | N/A (server-relayed) | Manual (&relay) | Manual | Guided (notice + relay option) |

**JamWide advantage:** Zero-setup (auto room ID), plugin-integrated controls, and interval-synced buffering. No other tool combines NINJAM awareness with VDO.Ninja video.

### Online Jam Video Solutions

| Feature | Zoom/Meet | SonoBus (no video) | Jamulus (no video) | FarPlay | JamWide v1.1 Plan |
|---------|-----------|-------------------|-------------------|---------|-------------------|
| Video | Built-in | None | None | Built-in | Browser companion |
| Audio optimization | Poor (voice-optimized) | Excellent | Excellent | Excellent | Excellent (NINJAM) |
| Multi-monitor | Limited | N/A | N/A | No | Yes (popout windows) |
| Interval-based audio | No | No | No | No | Yes (NINJAM core) |
| Remote control | No | No | No | No | Yes (OSC) |
| OSC integration | No | No | No | No | Yes (full bidirectional) |

## Sources

### OSC Implementation
- [IEM Plugin Suite OSCParameterInterface](https://git.iem.at/audioplugins/IEMPluginSuite/-/blob/175-10th-ambisonic-order/resources/OSC/OSCParameterInterface.cpp) - Reference implementation (HIGH confidence)
- [JUCE OSCReceiver documentation](https://docs.juce.com/master/classOSCReceiver.html) - Thread safety patterns (HIGH confidence)
- [JUCE OSC tutorial](https://docs.juce.com/master/tutorial_osc_sender_receiver.html) - Sender/receiver patterns (HIGH confidence)
- [Gig Performer TouchOSC guide](https://gigperformer.com/using-touch-osc-app-as-a-remote-display-and-a-touch-control-surface-with-gig-performer) - Bidirectional feedback expectations (MEDIUM confidence)
- [TouchOSC manual](https://hexler.net/touchosc/manual/complete) - Layout design patterns (HIGH confidence)
- [RME TotalMix OSC forum](https://forum.rme-audio.de/viewtopic.php?pid=238855) - Fader feedback limitations (MEDIUM confidence)

### VDO.Ninja Video
- [VDO.Ninja API reference](https://docs.vdo.ninja/advanced-settings/api-and-midi-parameters/api/api-reference) - External API commands (MEDIUM confidence -- docs self-labeled "DRAFT")
- [VDO.Ninja chunked mode docs](https://docs.vdo.ninja/advanced-settings/settings-parameters/and-chunked) - Buffer parameters and requirements (MEDIUM confidence)
- [Companion-Ninja repo](https://github.com/steveseguin/Companion-Ninja) - HTTP/WebSocket API patterns (MEDIUM confidence)
- [VDO.Ninja GitHub releases](https://github.com/steveseguin/vdo.ninja/releases) - chunkprofile presets (MEDIUM confidence)
- [VDO.Ninja buffer docs](https://docs.vdo.ninja/advanced-settings/video-parameters/buffer) - Chunked buffering behavior (MEDIUM confidence)

### Competitive Analysis
- [JamTaba GitHub](https://github.com/elieserdejesus/JamTaba) - H.264 video limitations (HIGH confidence)
- [Cockos NINJAM + video forum thread](https://forum.cockos.com/showthread.php?t=213553) - Community video sync pain points (MEDIUM confidence)
- [JamTaba wiki](https://github.com/elieserdejesus/JamTaba/wiki/Inside-a-Ninjam-Server-(Overview)) - NINJAM server architecture (HIGH confidence)

---
*Feature research for: JamWide v1.1 OSC Remote Control + VDO.Ninja Video Companion*
*Researched: 2026-04-05*
