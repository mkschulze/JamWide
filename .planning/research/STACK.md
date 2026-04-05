# Stack Research: v1.1 OSC + Video Additions

**Domain:** OSC remote control + VDO.Ninja video companion for JUCE audio plugin
**Researched:** 2026-04-05
**Confidence:** HIGH (OSC), MEDIUM (WebSocket/HTTP)

This document covers ONLY the stack additions needed for v1.1. The validated v1.0 stack (JUCE 8.0.12, FLAC, Vorbis, CLAP/VST3/AU, custom LookAndFeel) is not revisited.

## Recommended Stack Additions

### OSC: juce_osc Module (No New Dependencies)

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| juce_osc | 8.0.12 | Bidirectional UDP OSC for TouchOSC remote control | Already bundled with JUCE 8.0.12 in `libs/juce/modules/juce_osc/`. Zero external deps. Battle-tested in IEM Plugin Suite across 20+ plugins. Provides `OSCReceiver`, `OSCSender`, `OSCMessage`, `OSCBundle`, `OSCAddress` with wildcard pattern matching. Only dependency is `juce_events`. |

**Integration point:** Add `juce::juce_osc` to `target_link_libraries(JamWideJuce ...)` in CMakeLists.txt. That is the only build change needed for OSC.

**Key classes (verified from source):**
- `juce::OSCReceiver` -- UDP listener with two callback modes: `RealtimeCallback` (network thread, for low-latency parameter changes) and `MessageLoopCallback` (message thread, for UI updates)
- `juce::OSCSender` -- UDP sender with `connect(host, port)` and variadic `send(address, args...)` template
- `juce::OSCAddress` / `juce::OSCAddressPattern` -- RFC-compliant address matching with wildcard support (`*`, `?`, `[...]`)
- `juce::OSCMessage` -- Typed arguments: `float32`, `int32`, `string`, `blob`

**IEM pattern to follow (verified from `/Users/cell/dev/IEMPluginSuite/resources/OSC/`):**
- `OSCParameterInterface` -- Receives OSC on `RealtimeCallback` thread, maps address to parameter ID, converts value ranges via `NormalisableRange`. Timer-based sender (default 100ms) that only sends changed values using `lastSentValues` dirty tracking.
- `OSCReceiverPlus` / `OSCSenderPlus` -- Thin wrappers around JUCE classes adding `isConnected()`, `getPortNumber()`, `getHostName()` state queries that the base classes lack.
- `OSCMessageInterceptor` -- Virtual interface for custom message handling before/after parameter dispatch. JamWide will use this pattern for session status, user roster, and video control messages that don't map to AudioProcessor parameters.
- `OSCStatus` -- Small footer component with green/red indicators, clickable to open config dialog. Config dialog has receiver port, sender IP/port, OSC address, interval slider.
- `OSCDialogWindow` -- Config persistence via `getConfig()` / `setConfig()` returning `juce::ValueTree`.

**Confidence: HIGH** -- Verified from juce_osc source headers (v8.0.12) and IEM reference implementation on disk.

### Local HTTP + WebSocket Server: cpp-httplib

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| cpp-httplib | v0.41.0 | Local HTTP server (companion page) + WebSocket server (plugin-to-browser sync) + WSS client (VDO.Ninja external API) | Header-only single file. Provides HTTP server, WebSocket server, AND WebSocket client (including `wss://` with OpenSSL). Actively maintained (latest release: 2025-04-04). Thread-per-connection model is fine for localhost with 1-3 connections. No Boost dependency. |

**Why cpp-httplib over alternatives:**

cpp-httplib is the right choice because it solves ALL THREE networking needs in a single header-only dependency:

1. **Local HTTP server** -- Serves the companion HTML page from BinaryData on `127.0.0.1`
2. **Local WebSocket server** -- Pushes interval timing / roster updates to companion page
3. **WSS client** -- Connects to `wss://api.vdo.ninja:443` for room roster discovery and command dispatch

**API patterns (from README-websocket.md, verified):**

```cpp
// WebSocket server (for companion page)
httplib::Server svr;
svr.WebSocket("/ws", [](const httplib::Request& req, httplib::ws::WebSocket& ws) {
    std::string msg;
    while (ws.read(msg)) {
        // handle incoming from browser
    }
    // ws.send("json payload") for outgoing
});

// HTTP server (serves companion page)
svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(BinaryData::companion_html, BinaryData::companion_htmlSize, "text/html");
});
svr.listen("127.0.0.1", port);  // localhost only

// WSS client (for VDO.Ninja API)
httplib::WebSocketClient ws("wss://api.vdo.ninja:443");
ws.set_ca_cert_path("/etc/ssl/certs/ca-certificates.crt");
ws.enable_server_certificate_verification(true);
ws.connect();
ws.send(R"({"join":"APIKEY"})");
```

**Thread model:** Each WebSocket connection occupies one thread. For our use case (1 companion browser tab + 1 VDO.Ninja API connection + maybe 1-2 popout windows), this is 3-5 threads total. Acceptable for a plugin.

**SSL requirement:** WSS to `api.vdo.ninja` requires OpenSSL. Define `CPPHTTPLIB_OPENSSL_SUPPORT` and link OpenSSL. On macOS, use system OpenSSL or brew. On Windows, vcpkg or bundled. On Linux, system libssl-dev.

**Confidence: MEDIUM** -- WebSocket support in cpp-httplib is relatively new (appears in v0.38.0+). The blocking thread-per-connection model is appropriate for our 3-5 connection count, but has not been verified in a JUCE audio plugin context. Need to test that thread creation does not conflict with DAW sandbox restrictions.

### Companion HTML/JS: Embedded via JUCE BinaryData

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| JUCE BinaryData | 8.0.12 | Embed companion HTML/CSS/JS as compiled binary resources in the plugin | No external files to ship. `juce_add_binary_data()` CMake function converts files to C++ byte arrays. Accessed at runtime as `BinaryData::companion_html`. Standard JUCE pattern used by Projucer, AudioPluginHost, and many plugins. |

**Integration:** Add to CMakeLists.txt:
```cmake
juce_add_binary_data(JamWideData SOURCES
    resources/companion.html
    resources/companion.js
)
target_link_libraries(JamWideJuce PRIVATE JamWideData)
```

**Confidence: HIGH** -- `juce_add_binary_data` is a core JUCE feature, verified in JUCE source at `JUCEUtils.cmake:452`.

## Supporting Libraries

| Library | Status | Purpose | Notes |
|---------|--------|---------|-------|
| OpenSSL | System/bundled | TLS for WSS to `api.vdo.ninja` | Required by cpp-httplib for WSS client. macOS: available via Homebrew (`/opt/homebrew/opt/openssl@3`). Windows: vcpkg or static bundle. Linux: `libssl-dev`. |
| juce_events | Already linked | Timer for OSC send interval | juce_osc depends on juce_events, which is already transitively included via juce_audio_utils. |

## What NOT to Add

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| Boost.Beast | Massive dependency (Boost.Asio + Boost headers). Overkill for 3-5 WebSocket connections on localhost. Adds significant compile time and binary size. | cpp-httplib (header-only, single file) |
| IXWebSocket | Good library (BSD-3, minimal deps), but adds a CMake subdirectory with multiple source files and optional zlib/TLS deps. Cannot serve HTTP -- would still need a separate HTTP server for the companion page. | cpp-httplib (handles HTTP + WS + WSS in one dependency) |
| WebSocketPP | Depends on Boost.Asio. Maintenance has slowed. | cpp-httplib |
| Mongoose (Cesanta) | Dual-licensed (GPL + commercial). Embeds MbedTLS which complicates TLS story. C library requiring C++ wrappers. | cpp-httplib |
| JUCE StreamingSocket (raw) | JUCE's `StreamingSocket` is a raw TCP wrapper. No HTTP or WebSocket protocol handling. Building HTTP+WS on raw sockets is error-prone and would take weeks. | cpp-httplib |
| JUCE WebBrowserComponent / WebView | Embeds a full browser in the plugin window. Heavy, platform-specific (WebKit/Edge), sandboxing issues in DAW hosts, video rendering inside plugin is fragile. | External browser companion approach (already decided in PROJECT.md) |
| Gin WebSocket | JUCE-specific but client-only. No server capability. Would still need HTTP server for companion page. Less mature than cpp-httplib. | cpp-httplib for both server and client needs |

## Integration Architecture

### Communication Channels

```
Plugin (JUCE C++)
  |
  |-- [UDP] OSC Receiver (port 9000) <-- TouchOSC / any OSC client
  |-- [UDP] OSC Sender (configurable) --> TouchOSC feedback
  |
  |-- [TCP] HTTP Server (127.0.0.1:port) --> Serves companion.html
  |-- [TCP] WebSocket Server (ws://127.0.0.1:port/ws) <--> Companion browser tab
  |         |  sync: interval timing, roster, buffer delay commands
  |         |  receive: user actions from browser UI
  |
  |-- [TCP] WSS Client (wss://api.vdo.ninja:443) <--> VDO.Ninja signaling
             send: getGuestList, mic, camera, forceKeyframe
             receive: guest-connected, view-connection events
```

### Threading Model

| Thread | Owner | Responsibilities |
|--------|-------|------------------|
| Audio thread | JUCE/DAW | Sample processing. NEVER touches network. |
| Message thread | JUCE | UI rendering, OSC `MessageLoopCallback`, timer-based OSC send |
| Run thread | JamWide | NJClient::Run(), NINJAM networking, command queue |
| OSC receiver thread | juce_osc | Internal thread for UDP receive. Callbacks on this thread (`RealtimeCallback`) or dispatched to message thread (`MessageLoopCallback`). |
| HTTP/WS server thread | cpp-httplib | `svr.listen()` blocks on its own thread. WebSocket handler callbacks execute on per-connection threads. |
| VDO.Ninja API thread | cpp-httplib/manual | WSS client read loop on dedicated thread. Reconnection with exponential backoff (timeout every ~60s). |

**Critical constraint:** OSC and WebSocket operations must NEVER run on the audio thread. The OSC receiver uses `RealtimeCallback` on juce_osc's internal network thread (not the audio thread), which is safe. Parameter changes from OSC flow through the same SPSC command queue as UI commands.

### Data Flow: OSC Parameter Change

```
TouchOSC sends UDP: /JamWide/remote/1/volume 0.75
  --> OSCReceiver (juce_osc network thread, RealtimeCallback)
  --> JamWide OSC handler: parse address, extract index + parameter
  --> Push to SPSC cmd_queue: SetUserChannelStateCommand{idx=0, volume=0.75}
  --> Run thread pops, applies to NJClient
  --> Timer fires on message thread: sendParameterChanges()
  --> OSCSender sends feedback to TouchOSC (only if value changed)
```

### Data Flow: VDO.Ninja Sync

```
NINJAM interval boundary detected (Run thread)
  --> Calculate buffer_delay_ms = (60000/BPM) * BPI * delay_factor
  --> Push IntervalEvent to ui_queue
  --> Message thread drains event, updates state
  --> WebSocket server sends JSON to companion: {"setBufferDelay": 5000}
  --> Companion JS receives, forwards via postMessage to VDO.Ninja iframe
  --> VDO.Ninja adjusts chunked buffer
```

## TouchOSC Protocol Requirements

TouchOSC communicates via standard OSC over UDP. Requirements for JamWide's OSC server:

| Requirement | Detail |
|-------------|--------|
| Transport | UDP (default and recommended). TCP supported by TouchOSC but not needed. |
| Port | Configurable. TouchOSC has no fixed default -- user sets send/receive ports. JamWide default receiver: 9000. |
| Bidirectional feedback | TouchOSC updates fader positions when it receives OSC messages on the SAME address it sends. Send `/JamWide/remote/1/volume 0.75` back to TouchOSC, and fader 1 moves to 75%. |
| Feedback loop prevention | TouchOSC has a `feedback` flag per control (default: off). When enabled, received values update the control visually. JamWide's dirty-tracking sender (IEM pattern) naturally prevents loops by only sending changed values. |
| Label updates | Send `/JamWide/remote/{idx}/name "username"` as string argument. TouchOSC can bind a Label's text to an incoming OSC message. |
| Discovery | TouchOSC supports Zeroconf/Bonjour for finding OSC receivers. Optional for JamWide -- manual IP/port config is standard practice. |
| Message format | OSC 1.0 binary format. Type tags: `f` (float32), `i` (int32), `s` (string). JUCE's juce_osc handles serialization. |

## VDO.Ninja Integration Requirements

| Requirement | Detail |
|-------------|--------|
| External API endpoint | `wss://api.vdo.ninja:443` |
| Authentication | API key passed as `&api=KEY` URL parameter on VDO.Ninja page. Same key used in WebSocket `{"join":"KEY"}` message. Key is any unique string (plugin generates UUID per instance). |
| Reconnection | WebSocket times out every ~60 seconds. Implement reconnect with exponential backoff: 1s, 2s, 4s, 8s, max 30s. Resend `{"join":"KEY"}` after each reconnect. |
| Command format | JSON: `{"action":"getGuestList"}`, `{"action":"mic","target":1}`, `{"action":"volume","target":"streamID","value":80}` |
| Event format | JSON: `{"action":"guest-connected","streamID":"..","label":".."}`, `{"action":"view-connection","value":true/false}` |
| Iframe postMessage | Companion page uses `iframe.contentWindow.postMessage({setBufferDelay: ms}, "*")` for buffer sync. Per-stream: `{setBufferDelay: ms, streamID: "id"}`. |
| URL parameters for music | `&chunked &noaudio &chunkbufferadaptive=0 &chunkbufferceil=180000 &buffer2=0 &lowlatency &cleanoutput &api=KEY &room=ROOM &push=USERNAME` |
| Browser requirement | Chromium-based only (Chrome, Edge, Brave) for `&chunked` transport. Plugin should warn if default browser is not Chromium. |

## Version Compatibility

| Package | Compatible With | Notes |
|---------|-----------------|-------|
| juce_osc 8.0.12 | JUCE 8.0.12 | Bundled module, version-locked. Depends on juce_events (already linked). |
| cpp-httplib v0.41.0 | C++20, OpenSSL 1.1+/3.x | Header-only. Single file include. Requires `CPPHTTPLIB_OPENSSL_SUPPORT` define for WSS. |
| OpenSSL 3.x | cpp-httplib v0.41.0 | macOS: `/opt/homebrew/opt/openssl@3` or system. Windows: vcpkg. Linux: `libssl-dev`. |
| VDO.Ninja | Latest (2025+) | External API at `wss://api.vdo.ninja:443` is stable. Chunked mode and `&api` parameter documented as core features. |

## Installation / Build Changes

```cmake
# === OSC: Add juce_osc module ===
# In target_link_libraries(JamWideJuce ...):
target_link_libraries(JamWideJuce
    PRIVATE
        njclient
        juce::juce_audio_utils
        juce::juce_opengl
        juce::juce_gui_extra
        juce::juce_osc           # ADD THIS
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# === Companion page: Embed HTML as binary data ===
juce_add_binary_data(JamWideResources SOURCES
    resources/companion/companion.html
    resources/companion/companion.js
    resources/companion/companion.css
)
target_link_libraries(JamWideJuce PRIVATE JamWideResources)

# === cpp-httplib: Header-only, just include path + OpenSSL ===
# Vendor single httplib.h file into src/third_party/ (matches picojson.h pattern)
target_compile_definitions(JamWideJuce PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT  # Required for wss:// to api.vdo.ninja
)

# Link OpenSSL
find_package(OpenSSL REQUIRED)
target_link_libraries(JamWideJuce PRIVATE OpenSSL::SSL OpenSSL::Crypto)
```

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| cpp-httplib (HTTP+WS+WSS all-in-one) | IXWebSocket (WS/WSS) + separate HTTP | If cpp-httplib's thread-per-connection model proves problematic with >10 concurrent connections. Unlikely for our use case (3-5 connections). |
| cpp-httplib (HTTP+WS+WSS all-in-one) | JUCE StreamingSocket + manual HTTP/WS | Never. Writing HTTP and WebSocket protocol handlers from scratch is weeks of work for no benefit. |
| Vendored httplib.h (single file) | Git submodule | If we need to track upstream changes closely. For a stable dependency, vendoring a single file is simpler and matches the existing picojson.h pattern in `src/third_party/`. |
| OpenSSL (system) | mbedTLS | If we need to avoid OpenSSL licensing concerns or want smaller binary. cpp-httplib supports `CPPHTTPLIB_MBEDTLS_SUPPORT` as alternative. mbedTLS is Apache-2.0 licensed and often smaller. Worth considering for Windows where bundling OpenSSL is heavier. |

## Stack Patterns by Variant

**If OpenSSL linking proves problematic on all platforms:**
- Use cpp-httplib without SSL for local HTTP/WS server (no change needed -- local is plaintext)
- Use IXWebSocket (BSD-3, optional system TLS) for the WSS client to `api.vdo.ninja` only
- This splits the dependency but avoids mandating OpenSSL for the local server

**If DAW sandboxing blocks localhost binding:**
- Fall back to named pipes / shared memory for plugin-to-browser communication
- This is unlikely (localhost TCP is standard in audio plugins), but worth noting
- Test early in Phase 3 (Video Companion Server)

**If VDO.Ninja external API becomes unreliable:**
- The companion page can still work with iframe-only postMessage communication
- Lose roster discovery (getGuestList) but retain buffer sync
- The external API is the preferred path but not the only path

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| OpenSSL linking on Windows CI | Medium | Use vcpkg or bundle static OpenSSL. Fallback: mbedTLS via `CPPHTTPLIB_MBEDTLS_SUPPORT`. |
| cpp-httplib WebSocket stability (relatively new feature) | Medium | Test early in Phase 3. Fallback: IXWebSocket for WS client, keep cpp-httplib for HTTP only. |
| VDO.Ninja API WebSocket ~60s timeout | Low | Standard exponential backoff reconnection. Well-documented behavior. |
| Firewall prompt on Windows for localhost | Low | Bind to 127.0.0.1 only (not 0.0.0.0). Most firewalls allow loopback. Needs testing. |
| DAW sandbox blocking localhost TCP | Low | JUCE plugins commonly use sockets. Test in REAPER, Logic, Ableton early. |

## Sources

- juce_osc module headers (local, v8.0.12): `libs/juce/modules/juce_osc/` -- HIGH confidence
- IEM Plugin Suite OSC implementation (local): `/Users/cell/dev/IEMPluginSuite/resources/OSC/` -- HIGH confidence
- [cpp-httplib GitHub](https://github.com/yhirose/cpp-httplib) -- v0.41.0, 2025-04-04 -- MEDIUM confidence (WebSocket feature recent)
- [cpp-httplib WebSocket README](https://github.com/yhirose/cpp-httplib/blob/master/README-websocket.md) -- MEDIUM confidence
- [JUCE OSC Tutorial](https://juce.com/tutorials/tutorial_osc_sender_receiver/) -- HIGH confidence
- [TouchOSC OSC Connections Manual](https://hexler.net/touchosc/manual/connections-osc) -- HIGH confidence
- [TouchOSC OSC Messages Manual](https://hexler.net/touchosc/manual/editor-messages-osc) -- HIGH confidence
- [VDO.Ninja API Reference](https://docs.vdo.ninja/advanced-settings/api-and-midi-parameters/api/api-reference) -- MEDIUM confidence
- [VDO.Ninja &api parameter docs](https://docs.vdo.ninja/advanced-settings/api-and-midi-parameters/api) -- MEDIUM confidence
- [VDO.Ninja iframe API Basics](https://docs.vdo.ninja/guides/iframe-api-documentation/iframe-api-basics) -- MEDIUM confidence
- [Companion-Ninja GitHub](https://github.com/steveseguin/Companion-Ninja) -- MEDIUM confidence
- [IXWebSocket GitHub](https://github.com/machinezone/IXWebSocket) -- v11.4.6, BSD-3 -- evaluated as fallback
- v1.1 design spec (local): `docs/superpowers/specs/2026-04-05-v1.1-osc-video-design.md` -- HIGH confidence

---
*Stack research for: JamWide v1.1 OSC + Video additions*
*Researched: 2026-04-05*
