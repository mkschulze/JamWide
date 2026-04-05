# Pitfalls Research

**Domain:** OSC remote control + VDO.Ninja video companion for JUCE audio plugin (JamWide v1.1)
**Researched:** 2026-04-05
**Confidence:** HIGH (JUCE OSC, threading), MEDIUM (VDO.Ninja API lifecycle, WebSocket server in plugin), LOW (chunked mode sync accuracy, multi-popout browser stability)

## Critical Pitfalls

### Pitfall 1: OSC Receiver Callback Mutates Shared State Without Thread Safety

**What goes wrong:**
JUCE's `OSCReceiver` delivers incoming messages on its internal network thread (when using `RealtimeCallback`) or the message thread (when using `MessageLoopCallback`). JamWide's existing architecture uses three threads: audio, UI (message thread / render), and run (NJClient). Adding the OSC receiver introduces a fourth thread. When an OSC message like `/JamWide/remote/3/volume 0.7` arrives, the handler must update the remote user's volume -- but this requires calling NJClient methods protected by `client_mutex`. If the handler acquires `client_mutex` on the OSC network thread, it contends with the run thread, which holds `client_mutex` during encoding and network I/O (potentially 10-50ms). The OSC thread blocks, incoming OSC messages queue up, and the bidirectional feedback loop to TouchOSC stalls.

**Why it happens:**
Developers treat OSC message handlers like UI event handlers -- "just set the value." But the NJClient API is mutex-protected, and the OSC network thread has no business waiting on network I/O locks. The IEM Plugin Suite avoids this because it maps OSC directly to JUCE AudioProcessorParameters (which are atomic/lock-free). JamWide does not use JUCE's parameter system for NJClient state -- it uses a command queue.

**How to avoid:**
- Route all OSC-triggered state changes through the existing `cmd_queue` (SPSC ring buffer, UI to Run thread). The OSC handler pushes a command variant; the run thread processes it on its next cycle. This is the same pattern the UI already uses.
- For parameters that need audio-thread-level latency (volume, pan, mute), write directly to `std::atomic` members that the audio thread already reads. Do not acquire any mutex from the OSC callback.
- Use `RealtimeCallback` for parameter-setting messages (fast, no allocations) and `MessageLoopCallback` for configuration messages (port changes, connect/disconnect) that can safely allocate.
- Never call NJClient methods directly from the OSC callback thread.

**Warning signs:**
OSC fader movements feel sluggish (>200ms round trip). TouchOSC faders snap back to old values before settling. Audio glitches correlate with rapid OSC fader automation.

**Phase to address:**
Phase 1 (OSC Server Core) -- the threading contract must be established before any parameter mapping.

---

### Pitfall 2: OSCReceiver Port Binding Silently Succeeds on Windows When Port Is Taken

**What goes wrong:**
JUCE's `OSCReceiver::connect(portNumber)` returns `true` on Windows even when another application (or another JamWide instance) already holds the port. This is because JUCE's underlying `DatagramSocket` calls `SO_REUSEADDR` unconditionally on Windows, which permits multiple binds to the same UDP port. The receiver appears connected but never receives any messages. On macOS, the behavior is correct -- `connect()` returns `false` when the port is taken.

**Why it happens:**
JUCE's `DatagramSocket` constructor calls `SocketHelpers::makeReusable(handle)` on all platforms, but the Windows implementation of `SO_REUSEADDR` for UDP allows multiple processes to bind the same port (unlike macOS/Linux where `SO_REUSEPORT` is separate from `SO_REUSEADDR`). This is a known JUCE issue reported since 2018 and still present.

**How to avoid:**
- After calling `OSCReceiver::connect(port)`, send a test OSC message to localhost on that port and verify receipt within 100ms. If no message arrives, the port is occupied.
- Implement a port-probing strategy for multi-instance: try the configured port, then increment and retry (up to N attempts). Display the actual bound port in the UI status bar.
- On Windows specifically, manually test port availability by creating a `DatagramSocket` without `SO_REUSEADDR` before passing to `OSCReceiver`. If the raw bind fails, the port is taken.
- Persist the allocated port per plugin instance in state save/restore so that reloading a DAW session does not cause port conflicts between instances.

**Warning signs:**
OSC status shows "connected" but TouchOSC commands have no effect. Works in one DAW instance but not when a second instance is loaded. Works on macOS but fails on Windows.

**Phase to address:**
Phase 1 (OSC Server Core) -- port binding validation is foundational; everything else depends on actually receiving messages.

---

### Pitfall 3: OSC Feedback Loop Floods the Network When Bidirectional Sync Is Naive

**What goes wrong:**
Bidirectional OSC means: (1) TouchOSC sends a fader value to the plugin, (2) the plugin updates the parameter, (3) the plugin sends the new value back to TouchOSC as feedback, (4) TouchOSC receives the feedback and sends the value again (because its fader "changed"). This creates an infinite feedback loop where a single fader touch generates hundreds of messages per second. For simple continuous faders this may appear to work (the value converges), but for toggle buttons, exclusive multi-toggles, or discrete parameters (codec select), the loop never converges and causes runaway oscillation.

**Why it happens:**
OSC has no built-in echo suppression or source identification. The plugin cannot distinguish "this value came from TouchOSC" versus "this value came from the plugin's own outgoing feedback." The IEM Plugin Suite handles this by only sending feedback for values that actually changed -- but if floating-point rounding causes the echoed value to differ by epsilon, it triggers another round.

**How to avoid:**
- Implement a "change source" flag per parameter: when a value arrives from OSC, mark it as "externally set" and suppress outgoing feedback for that parameter for a configurable debounce period (100-200ms, matching the configured send interval).
- Only send outgoing OSC values that have actually changed since the last send cycle. Use exact value comparison for integers/booleans, and an epsilon threshold (1e-6) for floats.
- Implement the IEM pattern: outgoing OSC is sent on a timer (default 100ms). The timer collects all changed parameters since the last tick and sends them in a single burst. A parameter changed by incoming OSC within the current timer period is excluded from the outgoing batch.
- For multi-toggle and exclusive parameters, add a 200ms suppression window after receiving an OSC change before sending feedback.

**Warning signs:**
Network traffic spikes when touching a single fader. CPU usage increases when TouchOSC is connected. Toggle buttons flicker rapidly between states. Parameter values oscillate visibly in the plugin UI.

**Phase to address:**
Phase 1 (OSC Server Core) -- feedback loop prevention is part of the send/receive architecture, not a polish item.

---

### Pitfall 4: VDO.Ninja External API WebSocket Disconnects Every ~60 Seconds

**What goes wrong:**
The VDO.Ninja external API WebSocket (`wss://api.vdo.ninja:443`) disconnects every ~60 seconds if there is no activity, and even with activity the connection can drop. The official documentation states: "it will timeout every minute or so by default otherwise." After disconnection, the plugin loses the ability to discover room participants, control streams, or receive join/leave events. Without robust reconnection logic, the plugin silently loses contact with VDO.Ninja and the video companion page becomes stale.

**Why it happens:**
WebSocket connections from native C++ code (not running in a browser) lack the browser's built-in reconnection heuristics. VDO.Ninja's signaling server is optimized for browser clients that maintain the connection through the JavaScript runtime. A native WebSocket client must implement its own keepalive and reconnection.

**How to avoid:**
- Implement automatic reconnection with exponential backoff: 1s, 2s, 4s, 8s, cap at 30s. On successful reconnect, re-send the `{"join": APIKEY}` message and re-query room state with `getGuestList`.
- Send periodic ping/keepalive frames (every 30s) to prevent idle timeout. Standard WebSocket ping frames suffice.
- After reconnecting, reconcile room state: compare the fresh `getGuestList` result against the locally cached roster and emit appropriate join/leave events to the companion page and OSC clients.
- Design the WebSocket client as a state machine: CONNECTING, CONNECTED, DISCONNECTING, RECONNECTING. All outbound commands queue until CONNECTED state is reached.
- Log all disconnection events with timestamps for debugging. Track reconnection count and expose it in a debug/status view.

**Warning signs:**
Video companion page shows stale user list after ~1 minute. New users joining the NINJAM session do not appear in the video grid. Plugin logs show WebSocket close events without corresponding reconnection.

**Phase to address:**
Phase 3 (Video Companion Server) -- but the WebSocket client library choice and reconnection architecture should be designed in Phase 1 planning.

---

### Pitfall 5: Audio Thread Blocked by OSC Parameter Changes via Mutex

**What goes wrong:**
If OSC-driven parameter changes (volume, pan, mute) go through the `client_mutex` path (UI command queue -> run thread -> NJClient API under lock), and the audio thread uses `serialize_audio_proc` mode (which acquires `client_mutex` in `AudioProc`), then rapid OSC fader movements cause contention between the run thread processing OSC commands and the audio thread processing samples. Even without `serialize_audio_proc`, if the developer mistakenly routes OSC volume changes through a path that touches `client_mutex`, the audio thread can be indirectly starved.

**Why it happens:**
The existing JamWide architecture uses `std::atomic<float>` for master/metro volumes that the audio thread reads directly. But remote user volumes are stored in NJClient's internal structures, which require `client_mutex` to modify. OSC-driven remote user volume changes must go through the mutex-protected path -- there is no lock-free alternative for NJClient's per-user state.

**How to avoid:**
- For master volume, master mute, metro volume, metro mute: write directly to the existing `std::atomic` members from the OSC callback. These bypass `client_mutex` entirely and the audio thread reads them lock-free. This is the fast path.
- For remote user volume/pan/mute: route through the `cmd_queue` command queue. The run thread applies these under `client_mutex` during its normal ~50ms polling cycle. This adds up to 50ms latency on remote user volume changes from OSC, which is acceptable for mixing.
- Never attempt to modify NJClient per-user state from the OSC callback thread directly.
- Ensure `serialize_audio_proc` is OFF in production builds (it is a diagnostic option). With it off, `AudioProc` never acquires `client_mutex` and is immune to OSC-induced contention.
- Consider adding atomic shadow copies of remote user volumes that the audio thread reads, mirroring the pattern used for master volume. This would eliminate the latency for remote user OSC fader movements at the cost of maintaining a parallel atomic array.

**Warning signs:**
Audio dropouts when rapidly moving remote user faders in TouchOSC. No dropouts when moving master fader (atomic path). xrun count increases during OSC automation.

**Phase to address:**
Phase 2 (OSC Remote User Mapping) -- the remote user parameter path must be designed with the audio thread in mind.

---

### Pitfall 6: Companion Page Served from Plugin Fails Due to Mixed Content / CORS

**What goes wrong:**
The plugin serves a companion HTML page from a local HTTP server (e.g., `http://127.0.0.1:PORT/companion`). This page embeds VDO.Ninja iframes from `https://vdo.ninja/`. Browsers block mixed content: an HTTP page cannot embed HTTPS iframes, and `postMessage` between HTTP and HTTPS origins may be restricted depending on the browser's security policy. Additionally, VDO.Ninja requires SSL sitewide -- embedding it from an HTTP origin causes camera/microphone permission failures.

**Why it happens:**
Running an HTTPS server from a plugin requires TLS certificates. Generating or bundling self-signed certificates is complex, and browsers will show security warnings. Developers initially prototype with HTTP because it is simpler, then discover the mixed-content wall when adding VDO.Ninja iframes.

**How to avoid:**
- Do NOT embed VDO.Ninja iframes in a locally-served HTTP page. Instead, use a two-part architecture:
  1. **Companion page**: A static HTML page hosted on a public HTTPS URL (e.g., a GitHub Pages site at `https://jamwide.github.io/companion/`) that embeds VDO.Ninja iframes. The plugin opens this URL in the system browser with query parameters encoding the room name, API key, and local WebSocket port.
  2. **Local WebSocket**: The plugin runs a WebSocket server on `ws://127.0.0.1:PORT` that the companion page connects to for receiving interval timing and roster updates. Browsers allow `ws://` connections from `https://` pages to `127.0.0.1` (localhost exemption per W3C spec).
- Alternatively, use the `file://` protocol by writing the companion HTML to a temp file and opening it. However, `file://` origins have even stricter iframe restrictions in modern browsers.
- The cleanest approach: make the companion page a standalone static site. Embed all control logic in JavaScript. Plugin communicates exclusively via the local WebSocket. No HTTP server needed in the plugin at all.

**Warning signs:**
Companion page loads but VDO.Ninja iframe shows "blocked by content security policy" or "insecure content blocked." Camera permission dialog never appears. Browser console shows mixed-content errors.

**Phase to address:**
Phase 3 (Video Companion Server) -- the serving architecture must be resolved before any HTML/JS work begins.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Hardcoded OSC port (9000) without multi-instance handling | Simpler config, fewer UI fields | Second plugin instance silently fails to receive OSC on Windows | Never -- multi-instance is common in DAW workflows |
| Polling VDO.Ninja API state instead of event-driven updates | Simpler implementation, no reconnection state machine | Unnecessary network traffic, stale data between polls, 60s timeout still requires reconnection | Early prototype only; replace with event-driven before release |
| Single thread for OSC + WebSocket + HTTP serving | Fewer threads to manage | One slow WebSocket reconnection blocks OSC processing; one OSC flood blocks companion page updates | Never -- these are independent I/O streams with different latency requirements |
| Sending all OSC parameters on every timer tick (no dirty tracking) | Simple implementation | Network flood with 50+ parameters * 10Hz = 500 messages/sec; TouchOSC becomes sluggish | Never -- dirty tracking is trivial and essential |
| Embedding companion HTML as BinaryData and serving via HTTP | No external hosting dependency | Mixed content blocks HTTPS iframe embedding; must solve TLS or move to external hosting anyway | Acceptable if serving over `data:` URI or `file://`, but not HTTP |

## Integration Gotchas

Common mistakes when connecting to external services.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| JUCE `OSCReceiver` | Trusting `connect()` return value as proof of binding success | Verify with a loopback test message; implement port-probing for multi-instance |
| JUCE `OSCSender` | Sending to `255.255.255.255` for discovery | Send to configured unicast IP only; broadcast causes firewall prompts and is unreliable across subnets |
| VDO.Ninja External API | Assuming WebSocket stays connected indefinitely | Implement reconnection state machine with exponential backoff; re-query room state after reconnect |
| VDO.Ninja iframe | Using `postMessage(data, "*")` without origin check | Always specify target origin in `postMessage`; always verify `event.origin` in the message handler |
| VDO.Ninja `&push=` stream ID | Using display names with special characters or spaces | Sanitize NINJAM username to alphanumeric + hyphens for stream ID; display names can differ |
| VDO.Ninja chunked mode | Assuming Firefox/Safari support | Chunked mode is Chromium-only (Chrome, Edge, Brave); detect browser and warn user or fall back to non-chunked |
| VDO.Ninja room password | Passing `&password=` in URL query string | Use `&hash=` with credentials after `#` fragment to prevent server logging |
| TouchOSC bidirectional | Sending feedback for values just received from TouchOSC | Implement debounce/suppression window; only send changed values on timer; exclude recently-received parameters |
| Local WebSocket server | Binding to `0.0.0.0` for convenience | Bind to `127.0.0.1` only; add random auth token in URL to prevent other tabs/processes from connecting |
| Plugin state save | Saving the OSC port but not the allocated port (after conflict resolution) | Save the actual bound port, not the requested port; on restore, re-probe if port is taken |

## Performance Traps

Patterns that work at small scale but fail as usage grows.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Sending OSC feedback for all remote users on every timer tick | Network saturated, TouchOSC lag | Dirty-flag per parameter; only send changed values | >4 remote users (4 users * 5 params * 10Hz = 200 msgs/sec) |
| Creating a new WebSocket connection for each companion page command | Connection overhead, port exhaustion | Single persistent WebSocket with message framing | >1 command/sec (interval boundary updates) |
| VDO.Ninja peer-to-peer video with many participants | Upload bandwidth exhaustion (each user sends to N-1 peers) | Warn users at 6+ participants; suggest SFU mode for 8+ | >5 participants on residential internet (5 upstream video feeds) |
| Allocating std::string for OSC address patterns on every message | GC pressure, memory fragmentation in real-time context | Pre-allocate OSC address pattern strings at startup; use string_view or static patterns | >100 OSC messages/sec (rapid fader automation) |
| Rebuilding the entire OSC namespace on every remote user roster change | CPU spike, outgoing OSC burst | Incremental updates: only send changed indices; batch roster broadcasts | >8 remote users joining/leaving frequently |

## Security Mistakes

Domain-specific security issues.

| Mistake | Risk | Prevention |
|---------|------|------------|
| OSC server bound to `0.0.0.0` instead of `127.0.0.1` | Anyone on the LAN can control the plugin (mute users, disconnect, adjust volumes) | Bind to `127.0.0.1` by default; add explicit "LAN mode" toggle with warning if user enables network OSC |
| WebSocket server without auth token | Any browser tab or local process can connect and inject commands | Generate a random token on each plugin start; include it in the companion page URL; reject connections without valid token |
| VDO.Ninja room ID derived predictably from NINJAM server address | Outsiders can guess room IDs and join video sessions without being in the NINJAM session | Append a random suffix or use room password; derive `&hash=` from NINJAM server password if set |
| Logging VDO.Ninja API key or room password to debug log | Credentials leak via log files shared for bug reports | Redact API keys and passwords in all log output; use placeholder `[REDACTED]` |
| OSC port exposed to WAN via UPnP or port forwarding | Remote attackers can send arbitrary OSC commands to the plugin | Document that OSC is for local/LAN use only; do not implement UPnP; warn if configured send target is a non-private IP |

## UX Pitfalls

Common user experience mistakes in this domain.

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| No visual indicator of OSC connection status | User cannot tell if TouchOSC is communicating | IEM-style footer status LED: green = receiving, yellow = sending only, red = port error, grey = disabled |
| Companion page requires manual URL copy-paste | Friction prevents adoption; users skip video | One-click "Open Video" button that launches browser with pre-configured URL including all parameters |
| Video companion shows Chromium requirement error after launch in Firefox | User wasted time opening the page, confused by technical error | Detect browser via User-Agent on companion page load; show a clear "Please open in Chrome/Edge" message with copy-link button |
| OSC port conflict shown as cryptic error code | User does not understand "Failed to bind port 9000" | Show: "Port 9000 is in use by another application. JamWide will use port 9001 instead." with option to change |
| Remote user indices shift when someone disconnects | TouchOSC faders suddenly control different users; user adjusts wrong person's volume | Broadcast updated name labels immediately on roster change; consider "stable index" approach where disconnected indices are reused by next joiner (already in the design spec) |
| Video popout windows not associated with plugin | macOS Mission Control / Windows Task Manager shows orphan browser windows with no context | Set window title to "JamWide Video - [username]" so each popout is identifiable; provide "Close All Popouts" button |
| No feedback when VDO.Ninja API is unreachable | User thinks video feature is broken | Show connection status in video section: "Connecting to VDO.Ninja...", "Connected (3 users)", "Reconnecting..." |

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces.

- [ ] **OSC Server:** Port binding succeeds but verify actual receipt with loopback test -- silent failure on Windows is the default behavior
- [ ] **OSC Bidirectional:** Fader moves work once but verify no feedback loop under sustained automation -- test with TouchOSC connected for 5+ minutes with continuous fader movement
- [ ] **OSC Multi-Instance:** Works with one plugin instance but verify port allocation when 2+ instances are loaded in the same DAW session
- [ ] **OSC State Restore:** Works on first load but verify that DAW session save/restore correctly restores OSC port, send target, and send interval without port conflicts
- [ ] **Video Companion:** Page loads and shows VDO.Ninja but verify `postMessage` communication works -- iframe security policies silently block cross-origin messages
- [ ] **Video Sync:** Buffer delay is set once but verify it is refreshed on BPM/BPI changes and interval boundary transitions -- stale delay causes drift
- [ ] **Video Roster:** Users appear in grid but verify join/leave events propagate correctly -- test by having a user disconnect and reconnect rapidly
- [ ] **WebSocket Reconnection:** Works on first connect but verify the reconnection state machine handles 10+ consecutive disconnects without resource leaks or state corruption
- [ ] **Browser Detection:** Companion page warns about Chromium requirement but verify the warning actually appears on Firefox/Safari and provides actionable guidance
- [ ] **Popout Windows:** Individual popout opens but verify multiple popouts can be opened simultaneously and that closing one does not affect others
- [ ] **OSC Video Control:** `/JamWide/video/open` triggers browser launch but verify it works when the companion page is already open (should focus existing window, not open duplicate)

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| OSC feedback loop flooding network | LOW | Add emergency rate limiter (cap at 50 msgs/sec per parameter); debounce window solves root cause |
| Port binding failure (multi-instance) | LOW | Increment port number; update TouchOSC config; save new port to state |
| VDO.Ninja WebSocket disconnect loop | LOW | Cap reconnection attempts; show "Video unavailable" status; allow manual reconnect button |
| Mixed content blocking companion page | MEDIUM | Redesign serving architecture to use external HTTPS host + local WebSocket; requires companion page rewrite |
| Audio thread contention from OSC | MEDIUM | Refactor OSC parameter path to use atomics instead of command queue; may require adding atomic shadow state for remote user volumes |
| OSC namespace design wrong (e.g., wrong index scheme) | HIGH | Breaking change for all TouchOSC layouts; must communicate to users and provide migration path; version the namespace (`/JamWide/v1/...`) from the start to allow future changes |
| Companion page iframe security breaks on browser update | MEDIUM | Move all VDO.Ninja communication to the external API WebSocket (plugin-side); companion page becomes a pure video viewer with no control logic |

## Pitfall-to-Phase Mapping

How roadmap phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| OSC callback thread safety (Pitfall 1) | Phase 1: OSC Server Core | OSC fader automation while connected to NINJAM server with audio playing; no glitches or sluggishness |
| Port binding silent failure (Pitfall 2) | Phase 1: OSC Server Core | Load 2 plugin instances on Windows; both show correct port status; second instance auto-increments port |
| Feedback loop (Pitfall 3) | Phase 1: OSC Server Core | Connect TouchOSC; move fader rapidly for 30 seconds; network traffic stays under 200 msgs/sec; no oscillation on toggles |
| VDO.Ninja API disconnects (Pitfall 4) | Phase 3: Video Companion Server | Leave plugin running for 10 minutes; API reconnects automatically; roster stays accurate; companion page reflects current state |
| Audio thread contention (Pitfall 5) | Phase 2: OSC Remote User Mapping | Rapid OSC automation of all remote user faders simultaneously; zero xruns; audio thread profiler shows no mutex waits |
| Mixed content / CORS (Pitfall 6) | Phase 3: Video Companion Server | Companion page opens in Chrome, Edge, and Brave; VDO.Ninja iframe loads; camera permission dialog appears; postMessage communication works |
| OSC namespace design | Phase 1: OSC Server Core | Namespace documented; version prefix included; tested with TouchOSC template import |
| WebSocket auth token | Phase 3: Video Companion Server | Open companion URL without token in a separate browser tab; connection rejected |
| Multi-popout window management | Phase 5: Video Display Modes | Open 4 popout windows; move to different monitors; close plugin; all popout browser windows remain functional (or close gracefully) |
| State save/restore with ports | Phase 1: OSC Server Core | Save DAW session with OSC configured; reload; OSC port binds correctly; TouchOSC reconnects without reconfiguration |
| Chunked mode browser compatibility | Phase 3: Video Companion Server | Open companion page in Firefox; clear warning message displayed; copy-link button works; chunked mode active in Chrome |

## Sources

- [JUCE OSCReceiver::RealtimeCallback documentation](https://docs.juce.com/master/structOSCReceiver_1_1RealtimeCallback.html)
- [JUCE OSCReceiver class reference](https://docs.juce.com/master/classOSCReceiver.html)
- [JUCE OSCReceiver::Listener class reference](https://docs.juce.com/master/classjuce_1_1OSCReceiver_1_1Listener.html)
- [JUCE forum: OSCReceiver binding always returns true even if port is taken](https://forum.juce.com/t/oscreceiver-binding-to-a-port-always-return-true-even-if-port-is-already-taken/25885)
- [JUCE forum: OSC receiver different behaviour between VST3 and AU](https://forum.juce.com/t/osc-receiver-different-behaviour-between-vst3-and-au/32708)
- [JUCE forum: Multiple OSCReceivers binding to a single DatagramSocket](https://forum.juce.com/t/multiple-oscreceivers-binding-to-a-single-datagramsocket-not-working-as-expected/38607)
- [JUCE forum: OSCReceiver port reuse](https://forum.juce.com/t/oscreceiver-port-reuse/25430)
- [JUCE forum: Parameter changes and thread safety](https://forum.juce.com/t/parameter-changes-thread-safety/50098)
- [JUCE forum: Popup windows in plugins](https://forum.juce.com/t/popup-windows-in-plugins/17013)
- [JUCE forum: Secondary window launched from plugin gets incorrect monitor scaling](https://forum.juce.com/t/secondary-window-launched-from-plugin-gets-incorrect-monitor-scaling/51319)
- [JUCE forum: macOS plugins in sandboxed DAW](https://forum.juce.com/t/macos-plugins-in-sandboxed-daw/36999)
- [JUCE forum: Building a simple HTTP server using JUCE sockets](https://forum.juce.com/t/building-a-simple-http-server-using-juce-sockets/20217)
- [Cycling '74 forum: TouchOSC feedback loop](https://cycling74.com/forums/touchosc-feedback-loop)
- [TouchOSC manual: OSC messages editor](https://hexler.net/touchosc/manual/editor-messages-osc)
- [VDO.Ninja iframe API documentation](https://docs.vdo.ninja/guides/iframe-api-documentation)
- [VDO.Ninja iframe API basics](https://docs.vdo.ninja/guides/iframe-api-documentation/iframe-api-basics)
- [VDO.Ninja external API reference](https://docs.vdo.ninja/advanced-settings/api-and-midi-parameters/api/api-reference)
- [VDO.Ninja detecting user joins/disconnects](https://docs.vdo.ninja/guides/iframe-api-documentation/detecting-user-joins-disconnects)
- [VDO.Ninja known issues](https://docs.vdo.ninja/common-errors-and-known-issues/known-issues)
- [VDO.Ninja chunked mode documentation](https://docs.vdo.ninja/advanced-settings/settings-parameters/and-chunked)
- [VDO.Ninja Companion-Ninja sample app (GitHub)](https://github.com/steveseguin/Companion-Ninja)
- [IEM Plugin Suite (GitLab)](https://git.iem.at/audioplugins/IEMPluginSuite)
- [IEM Plugin Suite OSCParameterInterface.cpp](https://git.iem.at/audioplugins/IEMPluginSuite/-/blob/175-10th-ambisonic-order/resources/OSC/OSCParameterInterface.cpp)
- [Surge Synthesizer OSC support discussion (GitHub #2355)](https://github.com/surge-synthesizer/surge/issues/2355)
- [JUCE PR #543: Bidirectional OSC communication](https://github.com/juce-framework/JUCE/pull/543/files)

---
*Pitfalls research for: JamWide v1.1 OSC + VDO.Ninja Video*
*Researched: 2026-04-05*
