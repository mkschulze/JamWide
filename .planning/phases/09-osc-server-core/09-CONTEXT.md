# Phase 9: OSC Server Core - Context

**Gathered:** 2026-04-05
**Status:** Ready for planning

<domain>
## Phase Boundary

Bidirectional OSC server with parameter mapping for local channels and metronome, session telemetry broadcast, config UI with status indicator, and state persistence. Covers OSC-01, OSC-02, OSC-03, OSC-06, OSC-07, OSC-09, OSC-10. Remote user addressing (OSC-04, OSC-05) and TouchOSC template (OSC-11) are Phase 10.

</domain>

<decisions>
## Implementation Decisions

### OSC Namespace Design
- **D-01:** Hierarchical address space rooted at `/JamWide/` — mirrors the mixer layout and maps cleanly to TouchOSC
- **D-02:** Local channels: `/JamWide/local/{1-4}/volume`, `/pan`, `/mute`, `/solo`
- **D-03:** Metronome: `/JamWide/metro/volume`, `/pan`, `/mute`
- **D-04:** Master: `/JamWide/master/volume`
- **D-05:** Session telemetry: `/JamWide/session/bpm`, `/bpi`, `/beat`, `/status`, `/users`, `/codec`, `/samplerate`
- **D-06:** Dual value namespace: primary 0-1 normalized (`/JamWide/local/1/volume`), secondary dB scale (`/JamWide/local/1/volume/db`). TouchOSC faders map to 0-1 by default; dB namespace for power users.
- **D-07:** VU meters: `/JamWide/master/vu/left`, `/vu/right` + `/JamWide/local/{1-4}/vu/left`, `/vu/right`. Phase 10 extends to remote users.

### Config UI
- **D-08:** IEM-style footer status dot + popup dialog. Click the dot to open config. Minimal UI footprint, always-visible status.
- **D-09:** 3-state status indicator: green = active, red = error (port bind failed), grey = disabled
- **D-10:** Dialog contains: enable toggle, receive port (default 9000), send IP (default 127.0.0.1), send port (default 9001), feedback interval display (100ms fixed). Dark Voicemeeter theme. ~200x300px popup.
- **D-11:** Explicit enable/disable toggle in dialog. Users may want to disable OSC without clearing port settings.

### Feedback Behavior
- **D-12:** 100ms timer-based dirty-flag sender (IEM pattern). Only sends values that changed since last tick.
- **D-13:** OSC bundle mode — group all dirty values into a single OSC bundle per timer tick. Atomic updates, fewer UDP packets.
- **D-14:** Echo suppression — when a value changes from OSC input, mark it as 'OSC-sourced' and skip sending it back for one tick. Prevents feedback loop with TouchOSC.

### Telemetry Scope
- **D-15:** Full telemetry: BPM, BPI, beat position, connection status, user count, codec name, sample rate, and VU meters for all channels (master + local + remote when available).
- **D-16:** VU meters sent at 100ms rate for all channels. VU is always-dirty by nature — acceptable bandwidth on UDP.

### Port Defaults & Error Handling
- **D-17:** Default ports: receive 9000, send to 9001. Matches TouchOSC convention (TouchOSC sends on 9000, receives on 9001).
- **D-18:** On port bind failure: status dot turns red, config dialog shows error message ("Port 9000 in use"). OSC stays disabled until user changes port. No auto-increment — user's TouchOSC must match.

### Threading Contract
- **D-19:** OSC receive callbacks dispatch via `juce::MessageManager::callAsync()` to message thread, then push to `cmd_queue`. Preserves SPSC single-producer invariant. Same latency path as UI interactions.
- **D-20:** OSC sender timer runs on message thread (juce::Timer). Reads parameter state from atomics and `uiSnapshot`. No lock acquisition needed.

### State Persistence
- **D-21:** Bump state version from 1 to 2. New fields: oscEnabled (bool), oscReceivePort (int), oscSendIP (string), oscSendPort (int). Version 1 states load with OSC defaults (disabled, ports 9000/9001).

### Claude's Discretion
- Exact power curve mapping for dB namespace (linear dB or matched to VbFader's 2.5 exponent)
- Internal dirty-flag data structure (bitfield, array of bools, etc.)
- Exact error message wording in config dialog
- Timer implementation (juce::Timer subclass or lambda-based)
- Dialog component layout details within the ~200x300px constraint

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### OSC Implementation Reference
- `/Users/cell/dev/IEMPluginSuite/resources/OSC/OSCParameterInterface.h` — IEM bidirectional OSC pattern: timer-based sender, receiver callback, dirty-flag tracking
- `/Users/cell/dev/IEMPluginSuite/resources/OSC/OSCParameterInterface.cpp` — Full implementation: 100ms timer, bundle construction, echo suppression
- `/Users/cell/dev/IEMPluginSuite/resources/OSC/OSCStatus.h` — Status indicator component (footer dot + dialog trigger)
- `/Users/cell/dev/IEMPluginSuite/resources/OSC/OSCStatus.cpp` — Status display implementation with 3-state coloring

### JamWide Architecture
- `juce/JamWideJuceProcessor.h` — Threading contract, cmd_queue (SPSC), apvts, state version, uiSnapshot atomics
- `src/threading/ui_command.h` — Command variants for run thread dispatch (SetLocalChannelMonitoringCommand, etc.)
- `src/threading/spsc_ring.h` — SPSC queue implementation (single-producer invariant)
- `juce/JamWideJuceProcessor.cpp` — getStateInformation/setStateInformation (version 1 schema)

### Research
- `.planning/research/STACK.md` — juce_osc module details, no new deps needed
- `.planning/research/ARCHITECTURE.md` — OSC component integration, threading patterns
- `.planning/research/PITFALLS.md` — Port binding bug on Windows, feedback loop prevention, fourth-thread contract
- `.planning/research/FEATURES.md` — Table stakes and differentiators, IEM and Gig Performer comparison

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `juce::juce_osc` module (bundled in `libs/juce/modules/juce_osc/`) — OSCSender, OSCReceiver, OSCMessage, OSCBundle
- `jamwide::SpscRing<UiCommand, 256>` — existing command queue for thread-safe UI→Run thread dispatch
- `UiAtomicSnapshot` — atomic snapshot struct with beat, VU, BPM/BPI for lock-free reads
- `JamWideJuceProcessor::apvts` — APVTS for local channel params (volumes, pans, mutes)
- Existing `SetLocalChannelMonitoringCommand` and `SetUserChannelStateCommand` — direct mapping targets for OSC parameter changes
- `VoicemeeterLookAndFeel` — dark theme LookAndFeel for dialog styling

### Established Patterns
- SPSC command queue: message thread → cmd_queue → run thread (single producer invariant)
- Atomic snapshot: run thread writes `uiSnapshot` atomics, message thread reads without lock
- 30Hz centralized timer in `ChannelStripArea` for VU meter updates — similar timer pattern for OSC
- Non-APVTS state stored on processor (chatSidebarVisible, scaleFactor, etc.) — OSC config follows same pattern
- State version migration: version check in setStateInformation with defaults for new fields

### Integration Points
- `JamWideJuceProcessor` — OSC server component lives here (or as a member). Survives editor destruction.
- `JamWideJuceEditor` — Footer bar gets OSC status dot component. Dialog opens from dot click.
- `CMakeLists.txt` — Add `juce::juce_osc` to `target_link_libraries`
- `getStateInformation` / `setStateInformation` — Add OSC fields, bump version to 2

</code_context>

<specifics>
## Specific Ideas

- IEM Plugin Suite is the reference implementation — adapt the `OSCParameterInterface` pattern directly
- Voicemeeter Banana dark theme must extend to the OSC dialog (same colors, fonts, component styles)
- TouchOSC sends on port 9000 and receives on 9001 by default — our defaults match this convention for zero-config pairing

</specifics>

<deferred>
## Deferred Ideas

- Remote user index-based addressing (`/JamWide/remote/{idx}/volume`) — Phase 10
- TouchOSC template (`.tosc` file) — Phase 10
- Connect/disconnect via OSC trigger — Phase 10
- Roster change broadcast (user names) — Phase 10
- Video control via OSC namespace (`/JamWide/video/*`) — Phase 13

</deferred>

---

*Phase: 09-osc-server-core*
*Context gathered: 2026-04-05*
