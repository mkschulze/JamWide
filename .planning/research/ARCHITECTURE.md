# Architecture Research: OSC + VDO.Ninja Integration

**Domain:** JUCE audio plugin remote control and browser video companion
**Researched:** 2026-04-05
**Confidence:** HIGH (OSC), MEDIUM (VDO.Ninja video companion)

## System Overview

```
                         ┌─────────────────────────────────────┐
                         │          TouchOSC / OSC Client      │
                         │       (iPad, phone, desktop app)    │
                         └──────────────┬──────────────────────┘
                                        │ UDP bidirectional
                                        │ port 9000 (receive)
                                        │ configurable (send)
┌───────────────────────────────────────┼───────────────────────────────────────┐
│  JamWide Plugin Process               │                                       │
│                                       │                                       │
│  ┌────────────────────────────────────┼─────────────────────────────────┐     │
│  │                 OSC Layer (NEW)     │                                 │     │
│  │  ┌──────────────┐  ┌──────────────┐│  ┌──────────────────────────┐  │     │
│  │  │ OSCReceiver  │  │ OSCSender    ││  │ OscParameterBridge      │  │     │
│  │  │ (juce_osc)   │  │ (juce_osc)  ││  │ (maps OSC <-> APVTS +   │  │     │
│  │  │ realtime cb  │  │ timer-based  ││  │  non-APVTS state)       │  │     │
│  │  └──────┬───────┘  └──────┬───────┘│  └─────────┬────────────────┘  │     │
│  │         │                 │        │            │                   │     │
│  └─────────┼─────────────────┼────────┼────────────┼───────────────────┘     │
│            │                 │        │            │                          │
│  ┌─────────┼─────────────────┼────────┼────────────┼───────────────────┐     │
│  │         │    JamWideJuceProcessor  │            │                   │     │
│  │         │                 │        │            │                   │     │
│  │    ┌────▼─────┐     ┌────▼─────┐  │  ┌─────────▼────────┐         │     │
│  │    │ APVTS    │     │cmd_queue │  │  │ cachedUsers      │         │     │
│  │    │(16 params│     │(SPSC 256)│  │  │ uiSnapshot       │         │     │
│  │    │masterVol │     │          │  │  │ (atomics)        │         │     │
│  │    │metroVol  │     │          │  │  │                  │         │     │
│  │    │localVol  │     │          │  │  │                  │         │     │
│  │    │etc.)     │     │          │  │  │                  │         │     │
│  │    └──────────┘     └────┬─────┘  │  └──────────────────┘         │     │
│  │                          │        │                                │     │
│  └──────────────────────────┼────────┼────────────────────────────────┘     │
│                             │        │                                      │
│  ┌──────────────────────────┼────────┼────────────────────────────────┐     │
│  │  NinjamRunThread         │        │                                │     │
│  │  (drains cmd_queue,      ▼        │                                │     │
│  │   NJClient::Run(),   ┌────────┐   │                                │     │
│  │   pushes events)     │NJClient│   │                                │     │
│  │                      └────────┘   │                                │     │
│  └───────────────────────────────────┘                                │     │
│                                                                       │     │
│  ┌─────────────────────────────────────────────────────────────┐     │     │
│  │           Video Companion Layer (NEW)                        │     │     │
│  │                                                              │     │     │
│  │  ┌──────────────────┐  ┌───────────────────────────────┐    │     │     │
│  │  │ CompanionServer  │  │ VDONinjaAPIClient             │    │     │     │
│  │  │ (HTTP + WS on    │  │ (wss://api.vdo.ninja:443)     │    │     │     │
│  │  │  127.0.0.1:PORT) │  │ WebSocket client              │    │     │     │
│  │  │                  │  │ - roster discovery             │    │     │     │
│  │  │ Serves companion │  │ - command dispatch             │    │     │     │
│  │  │ HTML page        │  │ - reconnect on timeout         │    │     │     │
│  │  │                  │  │                                │    │     │     │
│  │  │ WS: relay sync   │  │                                │    │     │     │
│  │  │ data to browser  │  │                                │    │     │     │
│  │  └────────┬─────────┘  └───────────────────────────────┘    │     │     │
│  │           │                                                  │     │     │
│  └───────────┼──────────────────────────────────────────────────┘     │     │
│              │                                                        │     │
└──────────────┼────────────────────────────────────────────────────────┘
               │ HTTP + WebSocket (localhost only)
               │
┌──────────────▼────────────────────────────────────────────────────────┐
│  Browser (Chromium-based)                                             │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  Companion Page (served from plugin)                         │     │
│  │  - Embeds VDO.Ninja iframes (&noaudio &chunked &api=KEY)    │     │
│  │  - Receives sync data via local WebSocket                    │     │
│  │  - Forwards setBufferDelay via postMessage to iframes        │     │
│  │  - Grid mode: single page, responsive grid                   │     │
│  │  - Popout mode: window.open() per-user windows               │     │
│  └──────────────────────────────┬──────────────────────────────┘     │
│                                 │ WebRTC via VDO.Ninja               │
│                                 ▼                                     │
│                       ┌──────────────────┐                           │
│                       │ VDO.Ninja        │                           │
│                       │ Signaling Server │                           │
│                       └──────────────────┘                           │
└───────────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

### New Components

| Component | Responsibility | Thread | Estimated Size |
|-----------|----------------|--------|----------------|
| `OscServer` | Owns juce::OSCReceiver + juce::OSCSender, manages port lifecycle, timer-based send loop with dirty tracking | Message thread (JUCE Timer) | ~300 LOC |
| `OscParameterBridge` | Maps OSC addresses to APVTS params + non-APVTS state, handles receive callbacks, routes to message thread | Message thread (receives via callAsync) | ~500 LOC |
| `OscRemoteUserMapper` | Maintains stable index-to-user mapping, broadcasts roster changes via OSC | Message thread (triggered by UserInfoChangedEvent) | ~200 LOC |
| `OscConfigPanel` | UI for OSC port/IP config, send interval, status indicator in footer bar | Message thread (UI component) | ~200 LOC |
| `CompanionServer` | Local HTTP server (serves companion HTML) + WebSocket server (relays sync data to browser) | Dedicated background thread (cpp-httplib thread pool) | ~400 LOC |
| `VDONinjaAPIClient` | WebSocket client to wss://api.vdo.ninja:443, roster discovery, command dispatch, auto-reconnect with backoff | Dedicated background thread | ~350 LOC |
| `VideoManager` | Coordinates CompanionServer + VDONinjaAPIClient, manages room state, triggers browser launch, calculates sync delay | Message thread (owns lifecycle) | ~300 LOC |
| `VideoConfigPanel` | UI for video room name, display mode, browser status | Message thread (UI component) | ~150 LOC |

### Modified Components

| Component | Modification | Risk |
|-----------|-------------|------|
| `JamWideJuceProcessor` | Add OscServer + VideoManager ownership, OSC/video config persistence in getState/setState, state version bump 1 -> 2 | Low -- additive only, no existing behavior changed |
| `JamWideJuceEditor` | Add OscConfigPanel to footer area, add VideoConfigPanel, wire OSC status indicator | Low -- UI additions in existing layout |
| `ConnectionBar` | Add video toggle button, OSC status dot indicator | Small -- two new child components |
| `CMakeLists.txt` | Add juce_osc module linkage, cpp-httplib to third_party, new source files, juce_add_binary_data for companion HTML/JS/CSS | Build config only |

### Unchanged Components

| Component | Why Unchanged |
|-----------|---------------|
| `NinjamRunThread` | OSC reads APVTS directly (thread-safe), writes via cmd_queue (same path as UI). No run thread modifications needed. |
| `NJClient` / core layer | No protocol changes. OSC and video are purely plugin-layer features. |
| `ChannelStripArea` / `ChannelStrip` | Remote channel UI already works via cachedUsers. OSC controls the same underlying state through cmd_queue. |
| Audio thread (`processBlock`) | OSC and video never touch the audio thread. All interaction is via APVTS params (message-thread safe) and atomics (lock-free reads). |

## Recommended Project Structure

```
juce/
├── JamWideJuceProcessor.h/cpp      # MODIFIED: owns OscServer + VideoManager
├── JamWideJuceEditor.h/cpp         # MODIFIED: adds OSC/video UI panels
├── NinjamRunThread.h/cpp           # UNCHANGED
├── osc/                            # NEW DIRECTORY
│   ├── OscServer.h/cpp             # OSCReceiver + OSCSender lifecycle + timer
│   ├── OscParameterBridge.h/cpp    # OSC address <-> APVTS + cmd_queue mapping
│   ├── OscRemoteUserMapper.h/cpp   # Stable index assignment for remote users
│   └── OscConfigPanel.h/cpp        # Config dialog + footer status indicator
├── video/                          # NEW DIRECTORY
│   ├── CompanionServer.h/cpp       # Local HTTP + WS server (cpp-httplib)
│   ├── VDONinjaAPIClient.h/cpp     # External API WebSocket client
│   ├── VideoManager.h/cpp          # Coordinator: room state, browser launch, sync
│   └── VideoConfigPanel.h/cpp      # UI for video settings
├── ui/                             # EXISTING -- minor additions
│   ├── ConnectionBar.h/cpp         # MODIFIED: video button, OSC status dot
│   └── (all other files unchanged)
resources/
├── companion/                      # NEW DIRECTORY
│   ├── index.html                  # Companion page (embedded via BinaryData)
│   ├── companion.js                # VDO.Ninja iframe management, WS client
│   └── companion.css               # Grid/popout layout styling
```

### Structure Rationale

- **juce/osc/**: Clean separation of OSC concerns. All 4 files are tightly coupled to each other but loosely coupled to the rest of the plugin. Follows the IEM Plugin Suite pattern where OSC is a self-contained module with its own receiver, sender, parameter bridge, and config UI.
- **juce/video/**: Video companion is architecturally independent from OSC (per design spec -- either can ship without the other). Separate directory allows independent development and potential feature-flagging.
- **resources/companion/**: HTML/JS/CSS served to browser. Embedded via JUCE `juce_add_binary_data()` so no external files to manage at runtime. Works identically in standalone and plugin modes.

## Architectural Patterns

### Pattern 1: OSC Parameter Bridge (IEM Pattern)

**What:** Single class bridges the OSC address space to APVTS parameters and non-APVTS state. Receives OSC messages via juce_osc's realtime callback, dispatches to message thread via `callAsync()`, then either sets APVTS params directly or pushes commands to the SPSC queue.

**When to use:** For all OSC-controllable parameters. APVTS params (masterVol, metroVol, localVol, etc.) use `param->setValueNotifyingHost()`. Non-APVTS state (remote user volume/pan/mute/solo) routes through `cmd_queue`.

**Trade-offs:**
- Pro: APVTS parameters automatically propagate to host automation, OSC, and UI simultaneously
- Pro: Non-APVTS state uses the same command path as UI controls (proven, thread-safe)
- Con: Remote user state requires manual bridging (no APVTS param exists for dynamic user count)
- Con: `callAsync()` adds ~1ms latency vs direct callback, but this is imperceptible for fader control

**Example:**
```cpp
// OscParameterBridge receives OSC on juce_osc's internal thread
void OscParameterBridge::oscMessageReceived(const juce::OSCMessage& msg)
{
    // CRITICAL: juce_osc callback is on its own thread, NOT the message thread.
    // Must dispatch to message thread to safely access APVTS and cmd_queue.
    auto address = msg.getAddressPattern().toString();
    float value = msg.size() > 0 ? msg[0].getFloat32() : 0.0f;

    juce::MessageManager::callAsync([this, address, value]()
    {
        // Now on message thread -- safe to access everything

        // APVTS parameters: direct set via parameter pointer
        if (address == "/JamWide/metro/volume")
        {
            if (auto* param = apvts.getParameter("metroVol"))
                param->setValueNotifyingHost(
                    param->getNormalisableRange().convertTo0to1(value));
            return;
        }

        // Non-APVTS: push command to queue (same path as UI controls)
        if (address.startsWith("/JamWide/remote/"))
        {
            int idx = parseRemoteIndex(address);
            if (idx > 0 && idx <= remoteUserMapper.getActiveCount())
            {
                int userIdx = remoteUserMapper.resolveToNJClientIndex(idx);
                auto cmd = buildRemoteUserCommand(userIdx, address, value);
                processor.cmd_queue.try_push(std::move(cmd));
            }
        }
    });
}
```

### Pattern 2: Timer-Based Outgoing OSC with Dirty Tracking

**What:** A juce::Timer fires at configurable interval (default 100ms). Each tick compares current parameter values to last-sent values. Only sends OSC messages for changed values. Reads APVTS params and uiSnapshot atomics -- both lock-free on the message thread.

**When to use:** For all outgoing OSC feedback. This is the IEM Plugin Suite's proven approach.

**Trade-offs:**
- Pro: Bandwidth-efficient; 100ms interval means max 10 UDP packets/sec per address
- Pro: Fully decoupled from parameter change rate (audio thread may change params 1000x/sec)
- Pro: Lock-free reads of atomics for session info and VU levels
- Con: Latency up to one timer interval for feedback; 100ms is standard for TouchOSC

**Example:**
```cpp
void OscServer::timerCallback()
{
    if (!sender.isConnected()) return;

    // APVTS parameters -- direct read, lock-free on message thread
    for (int i = 0; i < paramCount; ++i)
    {
        float current = apvts.getParameter(paramIDs[i])->getValue();
        if (current != lastSentValues[i])
        {
            sender.send(oscAddresses[i], current);
            lastSentValues[i] = current;
        }
    }

    // Session info from atomics (lock-free read)
    float bpm = processor.uiSnapshot.bpm.load(std::memory_order_relaxed);
    if (bpm != lastSentBpm) { sender.send("/JamWide/session/bpm", bpm); lastSentBpm = bpm; }

    int beat = processor.uiSnapshot.beat_position.load(std::memory_order_relaxed);
    if (beat != lastSentBeat) { sender.send("/JamWide/session/beat", beat); lastSentBeat = beat; }

    // Remote user state from cachedUsers snapshot (safe on message thread)
    sendRemoteUserUpdates();
}
```

### Pattern 3: Local WebSocket Relay for Video Sync

**What:** Plugin runs a local HTTP+WS server on 127.0.0.1 using cpp-httplib. Browser connects via WebSocket. Plugin pushes JSON sync messages (setBufferDelay, roster updates) through the WebSocket. Companion page JS forwards to VDO.Ninja iframes via `postMessage()`.

**When to use:** For all plugin-to-browser communication.

**Trade-offs:**
- Pro: No CORS issues (same localhost origin); real-time bidirectional communication
- Pro: Browser handles all video rendering (keeps plugin lightweight, JUCE_WEB_BROWSER=0)
- Pro: cpp-httplib is header-only, MIT licensed, thread-per-connection model is fine for 1-2 connections
- Con: Adds ~15K LOC header to compilation (one-time cost, no runtime overhead)
- Con: Firewall may prompt on some systems (mitigated by binding to 127.0.0.1 only)

**Example:**
```cpp
void CompanionServer::start(int port)
{
    // Serve companion HTML from embedded BinaryData
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(std::string(BinaryData::index_html, BinaryData::index_htmlSize),
                        "text/html");
    });
    server.Get("/companion.js", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(std::string(BinaryData::companion_js, BinaryData::companion_jsSize),
                        "application/javascript");
    });

    // WebSocket endpoint with auth token
    server.WebSocket("/ws", [this](const httplib::Request& req, httplib::ws::WebSocket& ws) {
        if (req.get_param_value("token") != authToken) { ws.close(); return; }

        registerClient(&ws);
        std::string msg;
        while (ws.read(msg)) { handleBrowserMessage(msg); }
        unregisterClient(&ws);
    });

    serverThread = std::thread([this, port] { server.listen("127.0.0.1", port); });
}

void CompanionServer::broadcastSync(const std::string& json)
{
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto* ws : wsClients) ws->send(json);
}
```

### Pattern 4: VDO.Ninja External API with Auto-Reconnect

**What:** Persistent WebSocket client to wss://api.vdo.ninja:443. Sends `{"join":"APIKEY"}` on connect. Polls roster via `getGuestList`. Auto-reconnects with exponential backoff when connection drops (documented ~60s timeout).

**When to use:** For roster discovery and remote control (mic/camera/volume/forceKeyframe) without going through the iframe.

**Trade-offs:**
- Pro: Direct command channel; roster discovery before companion page opens
- Pro: Enables programmatic control of VDO.Ninja streams from plugin
- Con: WebSocket timeout every ~60s requires robust reconnection logic
- Con: External dependency on VDO.Ninja API availability (marked as DRAFT)
- Con: API behavior may change between VDO.Ninja versions

## Data Flow

### OSC Receive Flow (TouchOSC -> Plugin)

```
[TouchOSC fader move]
    |
    v  UDP packet
[juce::OSCReceiver (internal thread, RealtimeCallback)]
    |
    v
[OscParameterBridge::oscMessageReceived()]
    |
    v  juce::MessageManager::callAsync()
[Message thread dispatch]
    |
    ├── APVTS param? ──> param->setValueNotifyingHost()
    |                          |
    |                          v
    |                    [APVTS notifies host + UI automatically]
    |
    ├── Remote user? ──> cmd_queue.try_push(SetUserChannelStateCommand)
    |                          |
    |                          v
    |                    [NinjamRunThread drains, applies to NJClient]
    |
    ├── Connect/disconnect? ──> cmd_queue.try_push(Connect/DisconnectCommand)
    |
    └── Video command? ──> VideoManager::handleOscCommand()
                               |
                               v
                          [CompanionServer::broadcast() or URL::launchInDefaultBrowser()]
```

### OSC Send Flow (Plugin -> TouchOSC)

```
[juce::Timer fires (100ms default)]
    |
    v
[OscServer::timerCallback() on message thread]
    |
    ├── Read APVTS params (lock-free, message thread safe)
    ├── Read uiSnapshot atomics (lock-free)
    ├── Read cachedUsers (message thread, safe after UserInfoChangedEvent)
    |
    v
[Compare with lastSentValues]
    |
    ├── Changed? ──> juce::OSCSender::send(address, value) ──> UDP to TouchOSC
    └── Unchanged? ──> skip (no network traffic)
```

### Roster Change Flow (User joins/leaves NINJAM)

```
[NJClient detects user join/part]
    |
    v
[NinjamRunThread pushes UserInfoChangedEvent to evt_queue]
    |
    v
[JamWideJuceEditor::drainEvents() on message thread]
    |
    v
[refreshChannelStrips() -- existing behavior, unchanged]
    |
    ├── OscRemoteUserMapper::updateFromCachedUsers(cachedUsers)
    |       |
    |       v
    |   [Assigns/frees stable 1-based indices]
    |       |
    |       v
    |   [OscServer broadcasts /JamWide/remote/count + all /name messages]
    |
    └── VideoManager::onRosterChanged(cachedUsers)
            |
            v
        [CompanionServer broadcasts updated user-stream mapping to browser via WS]
        [VDONinjaAPIClient::getGuestList() correlates NINJAM users to VDO streams]
```

### Video Sync Flow (NINJAM interval -> VDO.Ninja buffer delay)

```
[NinjamRunThread increments uiSnapshot.interval_count (atomic)]
    |
    v
[VideoManager timer (~500ms) polls interval_count on message thread]
    |
    ├── New interval detected?
    |       |
    |       v
    |   Calculate target buffer delay:
    |     delay_ms = (60000 / BPM) * BPI * bars_to_delay + extra_offset
    |       |
    |       v
    |   CompanionServer::broadcastSync({"setBufferDelay": delay_ms})
    |       |
    |       v
    |   [Browser WS client receives JSON]
    |       |
    |       v
    |   [companion.js forwards via postMessage to VDO.Ninja iframe]
    |       |
    |       v
    |   [VDO.Ninja adjusts chunked buffer to requested delay]
    |
    └── No change? ──> skip
```

### Video Companion Lifecycle

```
[User clicks "Video" button in ConnectionBar]
    |
    v
[VideoManager::openCompanion()]
    |
    ├── Generate random API key for this session
    ├── Generate random auth token for local WS security
    ├── Start CompanionServer (try port 18080, increment if taken)
    ├── Derive VDO.Ninja room ID from NINJAM server address
    |     e.g., "ninbot.com:2049" -> "jamwide-ninbot-com-2049"
    ├── Build URL: http://127.0.0.1:PORT/?room=ROOM&api=KEY&token=AUTH
    ├── juce::URL(companionUrl).launchInDefaultBrowser()
    |
    v
[Browser loads companion page from local server]
    |
    ├── companion.js connects: ws://127.0.0.1:PORT/ws?token=AUTH
    ├── companion.js creates VDO.Ninja iframe:
    |     https://vdo.ninja/?noaudio&chunked&api=KEY&room=ROOM
    |     &push=<NINJAM_USERNAME>&lowlatency&cleanoutput
    |
    v
[Plugin receives WS connection from browser]
    |
    ├── VDONinjaAPIClient connects: wss://api.vdo.ninja:443
    ├── Sends {"join": KEY}
    ├── Begins roster polling: {"action": "getGuestList"}
    |
    v
[Ongoing: sync data relayed, roster events broadcast, reconnection on timeout]
```

## Integration Points

### OSC Layer Integration with Existing Architecture

| Integration Point | Mechanism | Thread Safety |
|-------------------|-----------|---------------|
| APVTS params (masterVol, metroVol, localVol_0..3, localPan_0..3, localMute_0..3, masterMute, metroMute) | `param->setValueNotifyingHost()` for write, `param->getValue()` for read | Safe: APVTS is message-thread-safe. OSC callback dispatches to message thread via callAsync. |
| Remote user state (volume, pan, mute, solo) | Push `SetUserChannelStateCommand` / `SetUserStateCommand` to cmd_queue | Safe: cmd_queue is SPSC. Message thread is the single producer (both UI and OSC dispatch happen there). |
| Session info (BPM, BPI, beat, interval) | Read `uiSnapshot` atomics | Safe: relaxed atomics, read-only from message thread |
| Connection control | Push `ConnectCommand` / `DisconnectCommand` to cmd_queue | Safe: same SPSC path as UI thread |
| User roster | Read `cachedUsers` after UserInfoChangedEvent on message thread | Safe: same thread that writes cachedUsers has already completed before event is pushed |
| VU levels | Read `uiSnapshot` VU atomics | Safe: relaxed atomics, read-only |
| Plugin state persistence | OSC config saved in ValueTree (same pattern as existing non-APVTS state) | Safe: getState/setState called on message thread |

### Video Layer Integration

| Integration Point | Mechanism | Thread Safety |
|-------------------|-----------|---------------|
| Interval timing | Read `uiSnapshot.interval_count` atomic | Safe: relaxed atomic, read-only from message thread timer |
| BPM/BPI | Read `uiSnapshot.bpm` and `uiSnapshot.bpi` atomics | Safe: relaxed atomics |
| User roster | Read `cachedUsers` on message thread | Safe: message thread only, triggered by UserInfoChangedEvent |
| Browser launch | `juce::URL::launchInDefaultBrowser()` | Safe: message thread; uses Process::openDocument internally |
| Plugin state persistence | Video config saved in ValueTree (room override, mode, enabled) | Safe: getState/setState on message thread |
| HTTP/WS server | cpp-httplib runs on its own thread pool; communication to message thread via thread-safe broadcast queue | Safe: CompanionServer manages its own thread; VideoManager coordinates via message thread timer |

### Critical Constraint: SPSC Single-Producer Invariant

The existing `SpscRing<UiCommand, 256> cmd_queue` assumes a single producer (the message thread). OSC receive callbacks arrive on juce_osc's internal thread. **OSC MUST NOT push directly to cmd_queue from its callback thread.**

**Solution:** Use `juce::MessageManager::callAsync()` in the OSC receive callback to dispatch to the message thread before pushing to cmd_queue. This preserves the single-producer invariant because both the UI and OSC writes happen on the same message thread.

This is the simplest and safest approach. An MPSC queue upgrade is possible but unnecessary for this use case.

## Anti-Patterns

### Anti-Pattern 1: OSC Callback Pushing Directly to SPSC Queue

**What people do:** Let juce_osc's realtime callback push commands to `cmd_queue` directly, bypassing the message thread.

**Why it is wrong:** `SpscRing` uses relaxed memory ordering assuming exactly one producer thread. Two producers (message thread + OSC thread) can corrupt the head pointer, causing lost commands or undefined behavior.

**Do this instead:** Always dispatch OSC receives to the message thread via `juce::MessageManager::callAsync()` before accessing cmd_queue. The message thread is the single producer for all command sources (UI clicks, OSC messages, video events).

### Anti-Pattern 2: OSC Bypassing APVTS to Write Atomics Directly

**What people do:** OSC handler directly writes to `NJClient::config_mastervolume` or similar atomics.

**Why it is wrong:** APVTS already handles thread-safe parameter distribution. Bypassing it means the host never records the change (no automation recording, no undo), and creates a second write path that can race with host automation.

**Do this instead:** Always go through `param->setValueNotifyingHost()` for APVTS parameters. The processBlock() reads APVTS values and propagates to NJClient atomics -- this is the established data flow.

### Anti-Pattern 3: Running HTTP/WebSocket Server on Audio or Run Thread

**What people do:** Starting cpp-httplib or WebSocket connections in processBlock() or NinjamRunThread.

**Why it is wrong:** Audio thread must never block. Run thread blocking delays NJClient::Run(), causing audio dropouts and missed NINJAM intervals.

**Do this instead:** CompanionServer and VDONinjaAPIClient each run on their own dedicated threads. All coordination with the plugin happens through thread-safe mechanisms (message thread timers, atomic reads, std::mutex-protected broadcast queues).

### Anti-Pattern 4: Embedding Video in Plugin WebView

**What people do:** Use JUCE WebBrowserComponent or CEF to show video directly in the plugin window.

**Why it is wrong:** WebView adds 50-100MB memory, causes DAW sandboxing conflicts, and VDO.Ninja chunked mode requires a full Chromium browser engine. The CMakeLists.txt already sets `JUCE_WEB_BROWSER=0`.

**Do this instead:** Serve a companion page from a local HTTP server and open it in the user's default browser. This is lighter, avoids all sandboxing issues, and lets the browser handle video rendering.

## Scaling Considerations

| Scale | Architecture Impact |
|-------|---------------------|
| 1-4 remote users | Default architecture works fine. OSC sends ~30-50 addresses per tick. Single companion page with grid layout. |
| 5-10 remote users | OSC traffic: ~80-120 addresses/tick at 100ms (~1.2KB/tick UDP). Still well within UDP limits. Video: each user encodes N-1 outgoing streams -- document CPU impact to users. |
| 10+ remote users | Consider reducing OSC send rate to 200ms, or throttle VU-only addresses. Video popout mode with 10+ windows is resource-intensive -- document "max 8 recommended" for popout. |

### Scaling Priorities

1. **First bottleneck:** VDO.Ninja per-viewer video encoding. In a 10-person jam, each participant encodes 9 video streams. This is a VDO.Ninja architectural limit, not JamWide. Mitigation: document in user help, offer `&quality=0` for lower resolution.
2. **Not a real concern:** OSC bandwidth. Even 200 addresses at 10Hz is under 20KB/s UDP.

## State Persistence Changes

State version increments from 1 to 2 to accommodate new persistent state:

```
State Version 2 additions (in ValueTree, NOT APVTS):
├── oscReceiverPort    (int, default 9000)
├── oscSenderHost      (string, default "" = disabled)
├── oscSenderPort      (int, default -1 = disabled)
├── oscSendInterval    (int, default 100 ms)
├── oscEnabled         (bool, default false)
├── videoRoomOverride  (string, default "" = auto-generate from NINJAM server)
├── videoMode          (string, default "grid")
├── videoEnabled       (bool, default false)
```

**Forward compatibility:** State version 1 projects load fine in v1.1 (new fields get defaults). State version 2 projects degrade gracefully if opened in v1.0 (APVTS state loads, unknown ValueTree properties are silently ignored by JUCE).

## Suggested Build Order

Based on dependency analysis and integration risk:

### Phase 1: OSC Server Core

Build bidirectional OSC with APVTS parameter mapping.

**Why first:** Self-contained, lowest complexity, proven pattern (IEM reference), no external deps beyond juce_osc. Provides immediate user value (TouchOSC control of master/metro/local).

**Components:** OscServer, OscParameterBridge (APVTS params only), OscConfigPanel, juce_osc CMake linkage.

**Depends on:** Nothing new. Reads existing APVTS params.

### Phase 2: OSC Remote User Mapping

Add dynamic remote user control via OSC with stable index addressing.

**Why second:** Builds on Phase 1 OSC infrastructure. Requires solving the callAsync dispatch pattern and designing index stability across roster changes.

**Components:** OscRemoteUserMapper, extend OscParameterBridge for /remote/{idx}/* addresses, roster broadcast logic, updated OscServer send loop.

**Depends on:** Phase 1 (OscServer operational), existing cachedUsers + UserInfoChangedEvent flow.

### Phase 3: Video Companion Server

Build local HTTP + WebSocket server and companion HTML page.

**Why third:** Independent of OSC (can be developed in parallel after Phase 1). HTTP server is the foundation for all video features. Companion page is pure HTML/JS, testable in browser standalone.

**Components:** CompanionServer, companion HTML/JS/CSS resources, BinaryData integration, browser launch, VideoManager skeleton.

**Depends on:** cpp-httplib addition to project. No dependency on OSC phases.

### Phase 4: Video Sync and Room Management

Connect to VDO.Ninja external API, implement interval-based sync, auto-generate room IDs from NINJAM server address.

**Why fourth:** Requires CompanionServer (Phase 3) to relay sync data. This phase has the highest uncertainty (VDO.Ninja API behavior, reconnection, chunked buffer accuracy).

**Components:** VDONinjaAPIClient, VideoManager sync logic, room ID generation, user-stream mapping.

**Depends on:** Phase 3 (CompanionServer for local relay), existing uiSnapshot atomics for interval timing.

### Phase 5: Video Display Modes and OSC Video Control

Implement grid/popout modes, per-user popout windows, OSC /video/* commands.

**Why last:** Polish and UX. Requires all previous phases working. Popout windows are browser-side JS complexity. OSC video control bridges Phases 1-2 with Phases 3-4.

**Components:** companion.js updates for popout mode via window.open(), OSC /video/* address handling in OscParameterBridge, VideoConfigPanel in editor.

**Depends on:** Phase 2 (OscRemoteUserMapper for index correlation), Phase 4 (VideoManager for stream mapping).

## Sources

- [IEM Plugin Suite OSC reference implementation](/Users/cell/dev/IEMPluginSuite/resources/OSC/) -- LOCAL, HIGH confidence. Battle-tested across 20+ shipping plugins.
- [JUCE juce_osc module](/Users/cell/dev/JamWide/libs/juce/modules/juce_osc/) -- v8.0.12, HIGH confidence. Depends only on juce_events.
- [VDO.Ninja API reference](https://docs.vdo.ninja/advanced-settings/api-and-midi-parameters/api/api-reference) -- MEDIUM confidence. API marked as DRAFT, may change.
- [VDO.Ninja &api parameter](https://docs.vdo.ninja/advanced-settings/api-and-midi-parameters/api) -- MEDIUM confidence. WebSocket to wss://api.vdo.ninja:443 confirmed.
- [Companion-Ninja reference](https://github.com/steveseguin/Companion-Ninja) -- MEDIUM confidence. Community sample app; confirms guest-connected/push-connection events.
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) -- HIGH confidence. MIT, header-only, HTTP+WebSocket server+client, actively maintained.
- [cpp-httplib WebSocket API](https://github.com/yhirose/cpp-httplib/blob/master/README-websocket.md) -- HIGH confidence. Thread-per-connection model, blocking I/O, suitable for 1-2 connections.
- [JamWide v1.1 design spec](/Users/cell/dev/JamWide/docs/superpowers/specs/2026-04-05-v1.1-osc-video-design.md) -- LOCAL, HIGH confidence. Authoritative decisions doc.
- [JamWide existing architecture](/Users/cell/dev/JamWide/.planning/codebase/ARCHITECTURE.md) -- LOCAL, HIGH confidence. Three-thread model, SPSC queues, lock strategy.

---
*Architecture research for: JamWide v1.1 OSC + VDO.Ninja Video Integration*
*Researched: 2026-04-05*
