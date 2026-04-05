# Phase 9: OSC Server Core - Research

**Researched:** 2026-04-06
**Domain:** Bidirectional OSC remote control for JUCE audio plugin (juce_osc module, IEM pattern)
**Confidence:** HIGH

## Summary

Phase 9 implements a bidirectional OSC server that lets users control local mixer parameters from TouchOSC (or any OSC client) with real-time feedback, plus session telemetry broadcast. The technical approach is well-proven: the IEM Plugin Suite has shipped this exact pattern across 20+ audio plugins using `juce_osc`. JamWide's architecture (APVTS for local params, SPSC command queue for state changes, atomic snapshots for telemetry) maps cleanly to the IEM `OSCParameterInterface` pattern.

The core challenge is adapting the IEM pattern to JamWide's split parameter model: APVTS parameters (masterVol, metroVol, localVol, localPan, localMute) can be set directly via `setValueNotifyingHost()`, but metronome pan requires writing to `NJClient::config_metronome_pan` atomically (no APVTS param exists). The OSC receive path must dispatch to the message thread via `callAsync()` before touching the SPSC queue, preserving the single-producer invariant. The OSC send timer reads APVTS values and `uiSnapshot` atomics -- both lock-free on the message thread.

No new external dependencies are needed. The `juce_osc` module is already bundled in `libs/juce/modules/juce_osc/`. The only build change is adding `juce::juce_osc` to `target_link_libraries` in CMakeLists.txt.

**Primary recommendation:** Adapt the IEM `OSCParameterInterface` pattern directly. Use `RealtimeCallback` for receiving, `callAsync()` for dispatch, 100ms `juce::Timer` for sending with dirty-flag tracking, and OSC bundle mode for grouping outgoing values.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Hierarchical address space rooted at `/JamWide/` -- mirrors the mixer layout and maps cleanly to TouchOSC
- **D-02:** Local channels: `/JamWide/local/{1-4}/volume`, `/pan`, `/mute`, `/solo`
- **D-03:** Metronome: `/JamWide/metro/volume`, `/pan`, `/mute`
- **D-04:** Master: `/JamWide/master/volume`
- **D-05:** Session telemetry: `/JamWide/session/bpm`, `/bpi`, `/beat`, `/status`, `/users`, `/codec`, `/samplerate`
- **D-06:** Dual value namespace: primary 0-1 normalized (`/JamWide/local/1/volume`), secondary dB scale (`/JamWide/local/1/volume/db`). TouchOSC faders map to 0-1 by default; dB namespace for power users.
- **D-07:** VU meters: `/JamWide/master/vu/left`, `/vu/right` + `/JamWide/local/{1-4}/vu/left`, `/vu/right`. Phase 10 extends to remote users.
- **D-08:** IEM-style footer status dot + popup dialog. Click the dot to open config. Minimal UI footprint, always-visible status.
- **D-09:** 3-state status indicator: green = active, red = error (port bind failed), grey = disabled
- **D-10:** Dialog contains: enable toggle, receive port (default 9000), send IP (default 127.0.0.1), send port (default 9001), feedback interval display (100ms fixed). Dark Voicemeeter theme. ~200x300px popup.
- **D-11:** Explicit enable/disable toggle in dialog. Users may want to disable OSC without clearing port settings.
- **D-12:** 100ms timer-based dirty-flag sender (IEM pattern). Only sends values that changed since last tick.
- **D-13:** OSC bundle mode -- group all dirty values into a single OSC bundle per timer tick. Atomic updates, fewer UDP packets.
- **D-14:** Echo suppression -- when a value changes from OSC input, mark it as 'OSC-sourced' and skip sending it back for one tick. Prevents feedback loop with TouchOSC.
- **D-15:** Full telemetry: BPM, BPI, beat position, connection status, user count, codec name, sample rate, and VU meters for all channels (master + local + remote when available).
- **D-16:** VU meters sent at 100ms rate for all channels. VU is always-dirty by nature -- acceptable bandwidth on UDP.
- **D-17:** Default ports: receive 9000, send to 9001. Matches TouchOSC convention (TouchOSC sends on 9000, receives on 9001).
- **D-18:** On port bind failure: status dot turns red, config dialog shows error message ("Port 9000 in use"). OSC stays disabled until user changes port. No auto-increment -- user's TouchOSC must match.
- **D-19:** OSC receive callbacks dispatch via `juce::MessageManager::callAsync()` to message thread, then push to `cmd_queue`. Preserves SPSC single-producer invariant. Same latency path as UI interactions.
- **D-20:** OSC sender timer runs on message thread (juce::Timer). Reads parameter state from atomics and `uiSnapshot`. No lock acquisition needed.
- **D-21:** Bump state version from 1 to 2. New fields: oscEnabled (bool), oscReceivePort (int), oscSendIP (string), oscSendPort (int). Version 1 states load with OSC defaults (disabled, ports 9000/9001).

### Claude's Discretion
- Exact power curve mapping for dB namespace (linear dB or matched to VbFader's 2.5 exponent)
- Internal dirty-flag data structure (bitfield, array of bools, etc.)
- Exact error message wording in config dialog
- Timer implementation (juce::Timer subclass or lambda-based)
- Dialog component layout details within the ~200x300px constraint

### Deferred Ideas (OUT OF SCOPE)
- Remote user index-based addressing (`/JamWide/remote/{idx}/volume`) -- Phase 10
- TouchOSC template (`.tosc` file) -- Phase 10
- Connect/disconnect via OSC trigger -- Phase 10
- Roster change broadcast (user names) -- Phase 10
- Video control via OSC namespace (`/JamWide/video/*`) -- Phase 13
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| OSC-01 | User can receive OSC messages to control any mixer parameter (volume, pan, mute, solo) | IEM `OSCParameterInterface` pattern: `RealtimeCallback` listener + address parsing + `callAsync()` dispatch. 16 APVTS params mapped directly; metro pan via NJClient atomic; solo via cmd_queue. |
| OSC-02 | User can send OSC feedback to control surfaces reflecting current parameter state | 100ms `juce::Timer` with dirty-flag array. APVTS reads + `uiSnapshot` atomics are lock-free on message thread. Bundle mode groups all dirty values per tick. |
| OSC-03 | User can configure OSC send/receive ports and target IP via a settings dialog | IEM `OSCDialogWindow` pattern adapted with Voicemeeter dark theme. `CallOutBox` popup from footer status dot. Fields: enable toggle, receive port, send IP, send port. |
| OSC-06 | User can monitor session state (BPM, BPI, beat position, connection status) via OSC | `uiSnapshot` atomics provide BPM, BPI, beat position lock-free. Connection status from `NJClient::cached_status` atomic. Sent as part of dirty-flag timer loop. |
| OSC-07 | User can control metronome volume, pan, and mute via OSC | metroVol and metroMute are APVTS params (direct `setValueNotifyingHost`). metroPan has no APVTS param -- write to `NJClient::config_metronome_pan` atomic directly. |
| OSC-09 | User can see an OSC status indicator in the plugin UI (active/error/off) | IEM `OSCStatus` component: footer dot with 3 states (green/red/grey). Click opens config dialog via `CallOutBox`. Timer polls connection state at 500ms. |
| OSC-10 | User's OSC configuration persists across DAW sessions | State version bump 1 to 2. New ValueTree properties in `getStateInformation`/`setStateInformation`. Version 1 states load with defaults (disabled, 9000/9001). |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| juce_osc | 8.0.12 (bundled) | OSCReceiver, OSCSender, OSCMessage, OSCBundle, OSCAddress | Already in `libs/juce/modules/juce_osc/`. Zero external deps. Battle-tested in IEM Plugin Suite (20+ shipping plugins). Provides UDP send/receive, typed arguments, address pattern matching with wildcards. [VERIFIED: local source inspection] |
| juce::Timer | 8.0.12 (bundled) | 100ms send loop for outgoing OSC feedback | Part of juce_events. Fires on message thread. IEM uses this exact pattern for `OSCParameterInterface::timerCallback()`. [VERIFIED: IEM source] |
| juce::CallOutBox | 8.0.12 (bundled) | Popup dialog for OSC config | IEM uses `CallOutBox::launchAsynchronously()` for OSC config dialog. Clean popup that stays attached to the status dot. [VERIFIED: IEM OSCStatus.cpp line 370] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| juce::ValueTree | 8.0.12 | OSC config persistence (port, IP, enabled state) | For state save/restore in getStateInformation/setStateInformation. Same pattern as existing non-APVTS state (lastServer, scaleFactor, etc.). [VERIFIED: existing processor code] |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| juce_osc bundled module | liblo (C OSC library) | liblo is mature but adds external dependency, C API requires wrappers, no JUCE integration. juce_osc is zero-cost and already available. |
| juce::Timer for send loop | std::thread + sleep loop | Dedicated thread would run on non-message thread, complicating APVTS reads. Timer on message thread is lock-free and simpler. |
| CallOutBox popup | DialogWindow | DialogWindow creates a separate top-level window, which can cause issues in some DAW hosts. CallOutBox is inline and lightweight -- IEM uses this pattern. |

**Installation:**
```cmake
# In CMakeLists.txt, add juce::juce_osc to target_link_libraries:
target_link_libraries(JamWideJuce
    PRIVATE
        njclient
        juce::juce_audio_utils
        juce::juce_opengl
        juce::juce_gui_extra
        juce::juce_osc           # ADD THIS LINE
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
```

**Version verification:** juce_osc 8.0.12 is bundled with the project's JUCE installation at `libs/juce/modules/juce_osc/`. [VERIFIED: local filesystem]

## Architecture Patterns

### Recommended Project Structure
```
juce/
├── JamWideJuceProcessor.h/cpp    # MODIFIED: owns OscServer, state version bump
├── JamWideJuceEditor.h/cpp       # MODIFIED: adds OscStatusDot to footer area
├── osc/                          # NEW DIRECTORY
│   ├── OscServer.h/cpp           # OSCReceiver + OSCSender + Timer + dirty tracking
│   ├── OscAddressMap.h/cpp       # Address-to-parameter mapping + dispatch logic
│   ├── OscStatusDot.h/cpp        # Footer indicator (3-state dot) + click handler
│   └── OscConfigDialog.h/cpp     # CallOutBox popup: enable, ports, IP, status
└── ui/
    └── ConnectionBar.h/cpp       # MODIFIED: reserves space for OSC status dot
```

### Pattern 1: OSC Parameter Bridge (IEM Pattern, Adapted)
**What:** Single `OscServer` class owns both `OSCReceiver` and `OSCSender`. Receives on `RealtimeCallback` (network thread), dispatches to message thread via `callAsync()`, then either sets APVTS params or pushes to `cmd_queue`. Timer-based sender at 100ms reads APVTS + atomics (lock-free) and sends only changed values.

**When to use:** For all OSC parameter control. This is the proven IEM pattern.

**Key adaptation from IEM:** IEM maps OSC addresses directly to APVTS parameter IDs (flat namespace like `/StereoEncoder/azimuth`). JamWide uses a hierarchical namespace (`/JamWide/local/1/volume`) that maps to APVTS param IDs (`localVol_0`). The `OscAddressMap` translates between these.

**Example:**
```cpp
// Source: Adapted from IEM OSCParameterInterface.cpp (local reference)
// OscServer.h
class OscServer : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>,
                  private juce::Timer
{
public:
    OscServer(JamWideJuceProcessor& proc);

    // OSCReceiver::Listener<RealtimeCallback> -- called on juce_osc network thread
    void oscMessageReceived(const juce::OSCMessage& msg) override;
    void oscBundleReceived(const juce::OSCBundle& bundle) override;

    // Timer -- fires on message thread
    void timerCallback() override;

    // Lifecycle
    bool startReceiver(int port);
    bool startSender(const juce::String& ip, int port);
    void stop();

    // Config persistence
    juce::ValueTree getConfig() const;
    void setConfig(const juce::ValueTree& config);

    // Status queries
    bool isReceiverConnected() const;
    bool isSenderConnected() const;
    bool hasError() const;
    juce::String getErrorMessage() const;

private:
    void handleOscOnMessageThread(const juce::String& address, float value);
    void sendDirtyParameters();
    void sendTelemetry();
    void sendVuMeters();

    JamWideJuceProcessor& processor;
    juce::OSCReceiver receiver;
    juce::OSCSender sender;

    // Dirty tracking
    std::array<float, kParamCount> lastSentValues;
    std::array<bool, kParamCount> oscSourced;  // echo suppression flags

    // Telemetry last-sent cache
    float lastSentBpm = -1.0f;
    int lastSentBpi = -1;
    int lastSentBeat = -1;
    int lastSentStatus = -999;
    // ... etc

    bool enabled = false;
    bool receiverError = false;
    juce::String errorMsg;
};
```

### Pattern 2: Echo Suppression (Feedback Loop Prevention)
**What:** When a value arrives from OSC input, mark that parameter index as "OSC-sourced" for one timer tick. The send loop skips OSC-sourced parameters, preventing the echoed value from being sent back to TouchOSC.

**When to use:** Every bidirectional OSC implementation must have this. Without it, a single fader touch creates an infinite feedback loop.

**Example:**
```cpp
// Source: IEM pattern + D-14 decision
void OscServer::handleOscOnMessageThread(const juce::String& address, float value)
{
    // This runs on message thread (dispatched via callAsync)
    int paramIndex = addressMap.resolve(address);
    if (paramIndex >= 0)
    {
        // Mark as OSC-sourced -- skip sending this back for one tick
        oscSourced[paramIndex] = true;

        // Set APVTS parameter
        if (auto* param = processor.apvts.getParameter(addressMap.apvtsId(paramIndex)))
            param->setValueNotifyingHost(
                param->getNormalisableRange().convertTo0to1(value));
    }
}

void OscServer::sendDirtyParameters()
{
    if (!sender.isConnected()) return;

    juce::OSCBundle bundle;
    bool bundleHasMessages = false;

    for (int i = 0; i < kParamCount; ++i)
    {
        // Skip if this was just set by OSC input (echo suppression)
        if (oscSourced[i])
        {
            oscSourced[i] = false;  // clear for next tick
            continue;
        }

        float current = getCurrentValue(i);
        if (current != lastSentValues[i])
        {
            lastSentValues[i] = current;
            bundle.addElement(juce::OSCMessage(
                addressMap.oscAddress(i), current));
            bundleHasMessages = true;
        }
    }

    if (bundleHasMessages)
        sender.send(bundle);
}
```

### Pattern 3: Dual-Wrapper for OSCReceiver/OSCSender State Queries
**What:** JUCE's `OSCReceiver` and `OSCSender` do not expose `isConnected()`, `getPortNumber()`, or `getHostName()`. The IEM suite wraps them in `OSCReceiverPlus`/`OSCSenderPlus` that track this state. JamWide can either use the same wrapper pattern or track state directly in `OscServer`.

**When to use:** For status UI and config persistence. Without wrappers, you cannot query current port or connection state.

**Recommendation:** Track connection state in `OscServer` directly (simpler than subclassing). Store `receiverPort`, `senderPort`, `senderHost`, `receiverConnected`, `senderConnected` as member variables updated after `connect()`/`disconnect()` calls. [VERIFIED: IEM OSCUtilities.h shows the wrapper approach]

### Pattern 4: MetroPan via NJClient Atomic (Non-APVTS Path)
**What:** The D-03 decision specifies `/JamWide/metro/pan` as a controllable parameter. However, there is no `metroPan` APVTS parameter in the current codebase. Metro pan is controlled via `NJClient::config_metronome_pan` (a `std::atomic<float>`). The OSC handler must write this atomic directly rather than going through APVTS.

**When to use:** For metro pan OSC control specifically. This is the only parameter in the Phase 9 scope that uses a non-APVTS write path.

**Example:**
```cpp
// Metro pan: no APVTS param, write to NJClient atomic directly
if (address == "/JamWide/metro/pan")
{
    float pan = juce::jlimit(-1.0f, 1.0f, value);
    processor.getClient()->config_metronome_pan.store(
        pan, std::memory_order_relaxed);
    return;
}
```

**Note:** For outgoing feedback, read the same atomic: `processor.getClient()->config_metronome_pan.load(std::memory_order_relaxed)`. This is safe on the message thread (relaxed atomic read). [VERIFIED: NJClient::config_metronome_pan declared as std::atomic<float> in njclient.h line 125]

### Anti-Patterns to Avoid
- **Direct cmd_queue push from OSC network thread:** The SPSC queue assumes a single producer (message thread). OSC callbacks on juce_osc's internal thread would create a second producer, corrupting the head pointer. Always `callAsync()` first. [VERIFIED: spsc_ring.h comment at line 27]
- **Bypassing APVTS for volume/mute:** Writing directly to NJClient atomics for parameters that have APVTS equivalents would skip host automation recording and create race conditions with host-driven parameter changes. Always use `setValueNotifyingHost()` for APVTS params. [VERIFIED: IEM pattern]
- **Sending all parameters every tick:** Even 20 parameters at 10Hz is noisy. Always use dirty tracking -- only send changed values. VU meters are the exception (always dirty by nature). [VERIFIED: IEM sendParameterChanges() checks `lastSentValues[i] != normValue`]
- **Using `MessageLoopCallback` for parameter receives:** This adds message thread queue latency. `RealtimeCallback` is faster for fader automation. The `callAsync()` dispatch is needed regardless to reach the message thread, so `RealtimeCallback` gives us the earliest possible notification. [VERIFIED: IEM uses `RealtimeCallback` -- OSCParameterInterface.h line 41]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| OSC message parsing/serialization | Custom UDP parser for OSC wire format | `juce::OSCReceiver` / `juce::OSCSender` | OSC wire format has type tags, alignment padding, bundle framing. juce_osc handles all of this. [VERIFIED: juce_osc module] |
| OSC address pattern matching | String comparison or regex for address routing | `juce::OSCAddressPattern::matches()` | Supports wildcards (`*`, `?`, `[...]`), part matching. RFC-compliant. [VERIFIED: juce_osc headers] |
| UDP socket management | Raw `DatagramSocket` with manual bind/listen | `juce::OSCReceiver::connect(port)` | Handles socket creation, binding, thread management internally. [VERIFIED: juce_osc API] |
| Config dialog popup | Custom window management with `DialogWindow` | `juce::CallOutBox::launchAsynchronously()` | Auto-positions relative to trigger component, handles focus, works in all DAW hosts without top-level window issues. [VERIFIED: IEM OSCStatus.cpp] |
| Value range conversion | Manual linear-to-dB math per parameter | `juce::NormalisableRange::convertTo0to1()` / `convertFrom0to1()` | Already defined for each APVTS parameter. Handles non-linear ranges correctly. [VERIFIED: IEM OSCParameterInterface.cpp line 141] |

**Key insight:** The IEM Plugin Suite already solved every sub-problem in this phase. The adaptation work is mapping their flat-namespace pattern to JamWide's hierarchical namespace and handling the few non-APVTS parameters (metro pan).

## Common Pitfalls

### Pitfall 1: OSC Receiver Port Binding Silently Succeeds on Windows
**What goes wrong:** `OSCReceiver::connect(9000)` returns `true` on Windows even when port 9000 is already taken by another app or another JamWide instance. JUCE's `DatagramSocket` sets `SO_REUSEADDR` unconditionally, which on Windows allows multiple binds to the same UDP port.
**Why it happens:** Windows `SO_REUSEADDR` for UDP permits multiple binds (unlike macOS/Linux). This is a known JUCE issue since 2018.
**How to avoid:** After `connect()`, send a test loopback message to verify actual receipt. On failure, set `receiverError = true` and display error in status dot + config dialog. Per D-18, no auto-increment -- user must change port manually to match TouchOSC.
**Warning signs:** OSC status shows green but TouchOSC commands have no effect. Works on macOS but fails on Windows.
[CITED: https://forum.juce.com/t/oscreceiver-binding-to-a-port-always-return-true-even-if-port-is-already-taken/25885]

### Pitfall 2: Feedback Loop from Bidirectional OSC
**What goes wrong:** Plugin receives fader value from TouchOSC, updates parameter, dirty-flag detects change, sends value back to TouchOSC, TouchOSC detects incoming value as change, sends back again. Infinite loop for toggle buttons; convergent but wasteful for continuous faders.
**Why it happens:** OSC has no built-in source identification or echo suppression.
**How to avoid:** Per D-14, implement per-parameter `oscSourced` flag. When a value is set via OSC receive, mark it as OSC-sourced and skip sending it back on the next timer tick. Use exact float comparison (not epsilon) for the dirty check, since the same value should match exactly.
**Warning signs:** Network traffic spikes when touching a single fader. Toggle buttons flicker.
[CITED: IEM OSCParameterInterface pattern -- timer-based sender naturally debounces]

### Pitfall 3: SPSC Queue Corruption from Multi-Producer
**What goes wrong:** If the OSC receive callback pushes to `cmd_queue` directly from juce_osc's network thread, there are now two producers (message thread from UI + OSC network thread). The SPSC ring uses relaxed memory ordering with a single-producer assumption. Two producers can corrupt the head pointer.
**Why it happens:** Developers treat the OSC callback like a UI callback, not realizing it runs on a different thread.
**How to avoid:** Per D-19, always dispatch via `callAsync()` to the message thread before pushing to `cmd_queue`. Both UI and OSC writes then happen on the same (message) thread -- single producer preserved.
**Warning signs:** Lost commands, parameter changes that don't take effect, random crashes.
[VERIFIED: SpscRing comment at line 27: "One thread may call try_push() (producer)"]

### Pitfall 4: CallOutBox in Plugin Context
**What goes wrong:** `CallOutBox::launchAsynchronously()` needs a valid parent component or `nullptr` for desktop window. In some DAW hosts, plugin editors don't have a desktop peer, causing the callout to appear at wrong position or crash.
**Why it happens:** DAW-specific windowing. Some hosts reparent the editor, breaking JUCE's component hierarchy.
**How to avoid:** Pass `nullptr` as parent (IEM pattern). The CallOutBox creates its own desktop window. Test in REAPER, Logic, and Ableton. If issues arise, fall back to adding the dialog as a child component of the editor with a semi-transparent overlay.
**Warning signs:** Config dialog appears off-screen or behind the plugin window.
[CITED: IEM OSCStatus.cpp line 370: `juce::CallOutBox::launchAsynchronously(std::move(dialogWindow), getScreenBounds()..., nullptr)`]

### Pitfall 5: MetroPan Has No APVTS Parameter
**What goes wrong:** Developer assumes all mixer parameters are APVTS params and tries `apvts.getParameter("metroPan")`, which returns nullptr. OSC metro pan control silently fails.
**Why it happens:** Metro pan exists only as `NJClient::config_metronome_pan` atomic. There is no APVTS parameter for it. Similarly, solo for local channels is handled through `SetLocalChannelMonitoringCommand`, not an APVTS param.
**How to avoid:** The OSC address map must classify each address as either APVTS-backed or non-APVTS. For non-APVTS params, use the appropriate write path (NJClient atomic for metro pan, cmd_queue for local solo).
**Warning signs:** Metro pan OSC address has no effect; local solo OSC address has no effect.
[VERIFIED: createParameterLayout() in JamWideJuceProcessor.cpp has no metroPan or localSolo params]

## OSC Address-to-Parameter Mapping (Complete)

This table documents every OSC address in Phase 9 scope and its write/read path.

### Controllable Parameters (OSC receive + send)

| OSC Address | APVTS Param ID | Write Path | Read Path | Value Range |
|-------------|----------------|------------|-----------|-------------|
| `/JamWide/master/volume` | `masterVol` | `setValueNotifyingHost()` | `param->getValue()` | 0.0-1.0 (normalized) |
| `/JamWide/master/volume/db` | `masterVol` | `setValueNotifyingHost()` | `param->getValue()` -> dB | -inf to +6 dB |
| `/JamWide/master/mute` | `masterMute` | `setValueNotifyingHost()` | `param->getValue()` | 0 or 1 (int) |
| `/JamWide/metro/volume` | `metroVol` | `setValueNotifyingHost()` | `param->getValue()` | 0.0-1.0 (normalized) |
| `/JamWide/metro/pan` | *none* | `client->config_metronome_pan.store()` | `client->config_metronome_pan.load()` | -1.0 to 1.0 |
| `/JamWide/metro/mute` | `metroMute` | `setValueNotifyingHost()` | `param->getValue()` | 0 or 1 (int) |
| `/JamWide/local/{1-4}/volume` | `localVol_{0-3}` | `setValueNotifyingHost()` | `param->getValue()` | 0.0-1.0 (normalized) |
| `/JamWide/local/{1-4}/volume/db` | `localVol_{0-3}` | `setValueNotifyingHost()` | `param->getValue()` -> dB | -inf to +6 dB |
| `/JamWide/local/{1-4}/pan` | `localPan_{0-3}` | `setValueNotifyingHost()` | `param->getValue()` | 0.0-1.0 (center=0.5) |
| `/JamWide/local/{1-4}/mute` | `localMute_{0-3}` | `setValueNotifyingHost()` | `param->getValue()` | 0 or 1 (int) |
| `/JamWide/local/{1-4}/solo` | *none* | `cmd_queue.try_push(SetLocalChannelMonitoringCommand)` | need tracking var | 0 or 1 (int) |

### Read-Only Telemetry (OSC send only)

| OSC Address | Source | Read Path | Value Type |
|-------------|--------|-----------|------------|
| `/JamWide/session/bpm` | `uiSnapshot.bpm` | `load(relaxed)` | float |
| `/JamWide/session/bpi` | `uiSnapshot.bpi` | `load(relaxed)` | int |
| `/JamWide/session/beat` | `uiSnapshot.beat_position` | `load(relaxed)` | int |
| `/JamWide/session/status` | `client->cached_status` | `load(relaxed)` | int (NJC_STATUS enum) |
| `/JamWide/session/users` | `processor.userCount` | `load(relaxed)` | int |
| `/JamWide/session/codec` | derived from encoder format | read from client config | string |
| `/JamWide/session/samplerate` | `processor.getSampleRate()` | direct call on msg thread | float |
| `/JamWide/master/vu/left` | `uiSnapshot.master_vu_left` | `load(relaxed)` | float (0.0-1.0) |
| `/JamWide/master/vu/right` | `uiSnapshot.master_vu_right` | `load(relaxed)` | float (0.0-1.0) |
| `/JamWide/local/{1-4}/vu/left` | `uiSnapshot.local_ch_vu_left[n]` | `load(relaxed)` | float (0.0-1.0) |
| `/JamWide/local/{1-4}/vu/right` | `uiSnapshot.local_ch_vu_right[n]` | `load(relaxed)` | float (0.0-1.0) |

### Value Range Mapping

**Normalized 0-1 to linear 0-2 (APVTS volume params):**
```cpp
// APVTS range for masterVol/metroVol/localVol: NormalisableRange(0.0f, 2.0f, 0.01f)
// OSC sends 0.0-1.0 normalized
// To APVTS: param->setValueNotifyingHost(oscValue)  // APVTS internally maps 0-1 to 0-2
// From APVTS: param->getValue()  // returns 0-1 normalized, ready for OSC
```

**dB namespace mapping (Claude's discretion -- recommendation):**
```cpp
// Match VbFader's 2.5 exponent power curve for consistent feel
// VbFader: linear = pow(curveNorm, 2.5) * 2.0
// dB = 20 * log10(linear)  where linear is 0.0-2.0
// Range: -inf dB (at 0.0 linear) to +6.02 dB (at 2.0 linear)
// Incoming: dB -> linear -> APVTS 0-1
// Outgoing: APVTS 0-1 -> linear -> dB
float linearToDb(float linear) {
    return linear <= 0.0001f ? -100.0f : 20.0f * std::log10(linear);
}
float dbToLinear(float db) {
    return db <= -100.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
}
```
[VERIFIED: VbFader.cpp uses 2.5 exponent at line 35 and line 53]

**Pan normalization:**
```cpp
// APVTS localPan range: NormalisableRange(-1.0f, 1.0f, 0.01f)
// OSC convention: 0.0 (left) to 1.0 (right), center at 0.5
// Conversion: oscPan = (apvtsPan + 1.0f) / 2.0f
// Inverse: apvtsPan = oscPan * 2.0f - 1.0f
```

## Code Examples

### Complete OscServer Timer Callback
```cpp
// Source: Adapted from IEM OSCParameterInterface.cpp timerCallback/sendParameterChanges
void OscServer::timerCallback()
{
    if (!enabled || !senderConnected) return;

    juce::OSCBundle bundle;
    bool hasContent = false;

    // 1. APVTS parameters (dirty-flag check)
    sendDirtyApvtsParams(bundle, hasContent);

    // 2. Non-APVTS params (metro pan)
    sendDirtyNonApvtsParams(bundle, hasContent);

    // 3. Session telemetry (always check, rarely dirty except beat)
    sendDirtyTelemetry(bundle, hasContent);

    // 4. VU meters (always dirty -- D-16)
    sendVuMeters(bundle, hasContent);

    if (hasContent)
        sender.send(bundle);
}
```

### OSC Receive Dispatch
```cpp
// Source: Adapted from IEM OSCParameterInterface.cpp oscMessageReceived
void OscServer::oscMessageReceived(const juce::OSCMessage& msg)
{
    // CRITICAL: We are on juce_osc's internal network thread here.
    // Must dispatch to message thread for APVTS and cmd_queue access.
    auto address = msg.getAddressPattern().toString();
    float value = (msg.size() > 0 && msg[0].isFloat32()) ? msg[0].getFloat32()
                : (msg.size() > 0 && msg[0].isInt32()) ? static_cast<float>(msg[0].getInt32())
                : 0.0f;

    juce::MessageManager::callAsync([this, addr = std::move(address), value]()
    {
        handleOscOnMessageThread(addr, value);
    });
}

void OscServer::oscBundleReceived(const juce::OSCBundle& bundle)
{
    for (int i = 0; i < bundle.size(); ++i)
    {
        auto elem = bundle[i];
        if (elem.isMessage())
            oscMessageReceived(elem.getMessage());
        else if (elem.isBundle())
            oscBundleReceived(elem.getBundle());
    }
}
```

### State Persistence (Version Bump)
```cpp
// Source: JamWideJuceProcessor.cpp getStateInformation, adapted for OSC
// In getStateInformation:
state.setProperty("stateVersion", 2, nullptr);  // bump from 1
state.setProperty("oscEnabled", oscServer->isEnabled(), nullptr);
state.setProperty("oscReceivePort", oscServer->getReceivePort(), nullptr);
state.setProperty("oscSendIP", oscServer->getSendIP(), nullptr);
state.setProperty("oscSendPort", oscServer->getSendPort(), nullptr);

// In setStateInformation:
int version = tree.getProperty("stateVersion", 0);
// OSC fields: default to disabled with standard ports
bool oscEnabled = tree.getProperty("oscEnabled", false);
int oscRecvPort = tree.getProperty("oscReceivePort", 9000);
juce::String oscSendIP = tree.getProperty("oscSendIP", "127.0.0.1").toString();
int oscSendPort = tree.getProperty("oscSendPort", 9001);

if (oscEnabled)
{
    oscServer->setConfig(oscEnabled, oscRecvPort, oscSendIP, oscSendPort);
    oscServer->start();
}
```

### Footer Status Dot
```cpp
// Source: Adapted from IEM OSCStatus.cpp
void OscStatusDot::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(2.0f);

    juce::Colour dotColour;
    if (!oscServer.isEnabled())
        dotColour = juce::Colours::white.withAlpha(0.15f);  // grey = disabled
    else if (oscServer.hasError())
        dotColour = juce::Colours::red;                      // red = error
    else
        dotColour = juce::Colours::limegreen;                // green = active

    float alpha = mouseOver ? 1.0f : 0.6f;
    g.setColour(dotColour.withAlpha(alpha));
    g.fillEllipse(area);
}

void OscStatusDot::mouseUp(const juce::MouseEvent& e)
{
    if (getLocalBounds().contains(e.getPosition()))
    {
        auto dialog = std::make_unique<OscConfigDialog>(oscServer);
        dialog->setSize(200, 300);

        juce::CallOutBox::launchAsynchronously(
            std::move(dialog),
            getScreenBounds(),
            nullptr);
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| IEM uses `juce::Atomic<bool>` for connection state | Modern JUCE prefers `std::atomic<bool>` | JUCE 6+ | Use `std::atomic<bool>` in new code. IEM's `juce::Atomic` is legacy but functionally equivalent. |
| OSC individual messages per parameter | OSC bundles grouping dirty params per tick | IEM pattern | Fewer UDP packets, atomic updates on receiver side. D-13 mandates bundle mode. |
| Flat OSC namespace (`/PluginName/paramID`) | Hierarchical namespace (`/JamWide/local/1/volume`) | JamWide design | Better TouchOSC layout mapping, clearer semantic grouping. Requires custom address parsing (not just paramID substring). |

**Deprecated/outdated:**
- `juce::Atomic<T>`: Still works but `std::atomic<T>` is preferred in C++17/20 code. Use `std::atomic` for new members.
- IEM `OSCReceiverPlus`/`OSCSenderPlus` wrappers: Functional but unnecessary if state tracking is done in the owning class. Recommended to track state in `OscServer` directly.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `CallOutBox::launchAsynchronously()` with `nullptr` parent works in all target DAW hosts (REAPER, Logic, Ableton, standalone) | Architecture Patterns - Pitfall 4 | MEDIUM -- may need fallback to child-component dialog if CallOutBox fails in specific hosts |
| A2 | OSC pan convention is 0.0 (left) to 1.0 (right) with 0.5 center | Address Mapping | LOW -- TouchOSC faders default to 0-1 range, convention is well-established |
| A3 | 100ms timer tick rate is sufficient for acceptable fader feedback latency | Pattern 1 | LOW -- IEM uses this as default, TouchOSC is designed for ~10Hz update rate |
| A4 | `OSCBundle` grouping does not have a practical size limit for ~30 messages per tick | Architecture Patterns | LOW -- UDP datagram limit is 65535 bytes; 30 OSC messages is well under 2KB |

## Open Questions

1. **Local channel solo state tracking for OSC feedback**
   - What we know: Solo is handled via `SetLocalChannelMonitoringCommand` through `cmd_queue`. There is no APVTS param or atomic tracking solo state.
   - What's unclear: How to read current solo state for outgoing OSC feedback. NJClient tracks solo internally via `m_issoloactive` bitmask, but reading it requires `client_mutex`.
   - Recommendation: Add `std::atomic<uint8_t> localSoloBitmask` to processor (4 bits for 4 channels). Update it from the run thread when processing `SetLocalChannelMonitoringCommand`. Read it lock-free in OSC send timer. This mirrors the pattern used for other atomic state (VU, beat).

2. **Codec name for telemetry broadcast**
   - What we know: D-05 specifies `/JamWide/session/codec`. The encoder format is a fourcc value stored on NJClient.
   - What's unclear: Whether codec name should be a string ("FLAC", "Vorbis") or int (fourcc).
   - Recommendation: Send as string for readability on TouchOSC displays. Map fourcc to human-readable string in OscServer.

3. **dB namespace: match VbFader curve or standard linear dB?**
   - What we know: VbFader uses a 2.5 exponent power curve for position-to-value mapping. Standard dB is `20*log10(linear)`.
   - What's unclear: Whether `/volume/db` should send the raw dB value (standard) or a curve-adjusted value matching the fader position.
   - Recommendation: Send standard linear dB values. The `/volume/db` endpoint should be a transparent dB representation, not a display-curve representation. This is Claude's discretion per CONTEXT.md.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| juce_osc module | OSC server core | Yes | 8.0.12 | -- (bundled with JUCE) |
| JUCE framework | Entire plugin | Yes | 8.0.12 | -- |
| CMake | Build system | Yes | 3.x | -- |

No external dependencies needed for Phase 9. Everything is bundled.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CTest + custom test executables (no unit test framework like GTest) |
| Config file | `CMakeLists.txt` (test targets at bottom) |
| Quick run command | `ctest --test-dir build -R flac_codec --output-on-failure` |
| Full suite command | `ctest --test-dir build --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| OSC-01 | Receive OSC message, parameter changes | manual | Connect TouchOSC, move fader, verify plugin response | N/A |
| OSC-02 | Send feedback on parameter change | manual | Move plugin fader, verify TouchOSC fader updates | N/A |
| OSC-03 | Config dialog with port/IP settings | manual | Click status dot, change ports, verify connection | N/A |
| OSC-06 | Session telemetry broadcast | manual | Connect to NINJAM, verify BPM/BPI/beat on TouchOSC | N/A |
| OSC-07 | Metro volume/pan/mute via OSC | manual | Send OSC to metro addresses, verify plugin changes | N/A |
| OSC-09 | Status indicator (green/red/grey) | manual | Visual inspection of footer dot in 3 states | N/A |
| OSC-10 | Config persistence | manual | Set OSC config, save DAW session, reload, verify config | N/A |

### Sampling Rate
- **Per task commit:** Build verification (`cmake --build build --target JamWideJuce`)
- **Per wave merge:** Build + manual TouchOSC loopback test
- **Phase gate:** Full manual test against TouchOSC with all 7 requirements verified

### Wave 0 Gaps
- No automated OSC tests exist. OSC testing is inherently manual (requires UDP network interaction with TouchOSC).
- A loopback smoke test could be added: create an `OSCReceiver` and `OSCSender` on localhost, send a test message, verify receipt. This would validate port binding and basic send/receive.
- [ ] `tests/test_osc_loopback.cpp` -- basic send/receive loopback on localhost (validates juce_osc linkage and port binding)

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | OSC is local/LAN only, no auth needed |
| V3 Session Management | No | Stateless UDP protocol |
| V4 Access Control | Partial | Bind to 127.0.0.1 by default; warn if user enters non-loopback IP |
| V5 Input Validation | Yes | Validate OSC message arguments (float range clamping, address format) |
| V6 Cryptography | No | No secrets in OSC messages |

### Known Threat Patterns for OSC/JUCE

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed OSC packet causing crash | Tampering | juce_osc handles parsing; wrap handler in try/catch for safety |
| OSC bound to 0.0.0.0 exposes control to LAN | Information Disclosure / Elevation | Bind receiver to 127.0.0.1 by default. Config dialog allows changing IP but no network listen by default. |
| Rapid OSC floods causing message thread starvation | Denial of Service | `callAsync()` naturally throttles (message queue). Rate-limit if needed. |
| OSC port scanning reveals plugin presence | Information Disclosure | Low risk -- standard ports 9000/9001, local only. Document as expected behavior. |

## Sources

### Primary (HIGH confidence)
- IEM Plugin Suite OSC implementation (local: `/Users/cell/dev/IEMPluginSuite/resources/OSC/`) -- `OSCParameterInterface.h/cpp`, `OSCStatus.h/cpp`, `OSCUtilities.h`
- JUCE juce_osc module (local: `libs/juce/modules/juce_osc/`) -- v8.0.12. `OSCReceiver`, `OSCSender`, `OSCMessage`, `OSCBundle`, `OSCAddress` APIs
- JamWide source code (local) -- `JamWideJuceProcessor.h/cpp`, `spsc_ring.h`, `ui_command.h`, `VbFader.cpp`, `ConnectionBar.h`, `JamWideJuceEditor.h`, `ui_state.h`
- JamWide v1.1 research (local) -- `.planning/research/STACK.md`, `ARCHITECTURE.md`, `PITFALLS.md`, `FEATURES.md`

### Secondary (MEDIUM confidence)
- JUCE forum: OSCReceiver port binding issue on Windows -- https://forum.juce.com/t/oscreceiver-binding-to-a-port-always-return-true-even-if-port-is-already-taken/25885
- TouchOSC OSC Connections Manual -- https://hexler.net/touchosc/manual/connections-osc

### Tertiary (LOW confidence)
- None -- all findings verified against local source code.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- juce_osc is bundled, verified from source, no external deps
- Architecture: HIGH -- IEM reference implementation read in full, JamWide integration points verified
- Pitfalls: HIGH -- JUCE forum confirms port binding issue, SPSC constraint verified from source, IEM pattern verified
- Address mapping: HIGH -- every APVTS parameter ID verified from createParameterLayout(), NJClient atomics verified from njclient.h

**Research date:** 2026-04-06
**Valid until:** 2026-05-06 (stable domain -- juce_osc API has not changed since JUCE 5)
