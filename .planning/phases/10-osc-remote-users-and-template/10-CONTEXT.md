# Phase 10: OSC Remote Users and Template - Context

**Gathered:** 2026-04-06
**Status:** Ready for planning

<domain>
## Phase Boundary

Extend the Phase 9 OSC server with remote user control (volume, pan, mute, solo per sub-channel), roster name broadcast, connect/disconnect triggers, and a shipped TouchOSC template. Covers OSC-04, OSC-05, OSC-08, OSC-11.

</domain>

<decisions>
## Implementation Decisions

### Remote User Addressing
- **D-01:** Positional index model. Remote slot 1 maps to the first connected remote user, slot 2 to the second, etc. When users leave, subsequent indices shift down. The roster name broadcast tells the control surface who is in each slot.
- **D-02:** Address pattern: `/JamWide/remote/{idx}/volume`, `/pan`, `/mute`, `/solo` (idx 1-based, matching local channel convention from Phase 9). dB variant: `/JamWide/remote/{idx}/volume/db`.
- **D-03:** Per sub-channel control. Multi-channel remote users expose each sub-channel: `/JamWide/remote/{idx}/ch/{n}/volume`, `/pan`, `/mute`, `/solo`. The "group bus" level control at `/JamWide/remote/{idx}/volume` controls the user's combined output (SetUserChannelState on channel 0 / group).
- **D-04:** VU meters per remote user: `/JamWide/remote/{idx}/vu/left`, `/vu/right`. Sent every 100ms tick (same always-dirty pattern as local VU from Phase 9). Per sub-channel VU: `/JamWide/remote/{idx}/ch/{n}/vu/left`, `/vu/right`.
- **D-05:** Max 16 remote user slots (matching the 16 routing bus limit in JamWideJuceProcessor). If more than 16 users connect, slots 17+ are not controllable via OSC (they still appear in-plugin).

### Roster Broadcast
- **D-06:** Per-slot name messages. When roster changes (UserInfoChangedEvent fires), send `/JamWide/remote/{idx}/name` as a string for each active slot. Empty string `""` for empty slots. This lets TouchOSC bind text labels directly to each slot.
- **D-07:** Broadcast triggered by `UserInfoChangedEvent` from NinjamRunThread. Read from `processor.cachedUsers` snapshot (already thread-safe). Also send `/JamWide/session/users` count (already implemented in Phase 9 telemetry).
- **D-08:** Sub-channel names: `/JamWide/remote/{idx}/ch/{n}/name` — sends the channel name string. Broadcast alongside user names on roster change.

### Connect/Disconnect via OSC
- **D-09:** Simple trigger model. `/JamWide/session/connect` with string argument `"host:port"` — uses username/password from last connection config (stored on processor). `/JamWide/session/disconnect` with no args (or float 1.0 as trigger). Minimal for a single button on a control surface.
- **D-10:** Connect dispatches via cmd_queue using existing connection flow. Disconnect dispatches a disconnect command. Both follow the SPSC single-producer pattern from Phase 9 (callAsync to message thread, then cmd_queue push).
- **D-11:** Connection status feedback already exists: `/JamWide/session/status` (Phase 9 telemetry). No new status addresses needed.

### TouchOSC Template
- **D-12:** Ship a `.tosc` template file in the repo at `assets/JamWide.tosc`. Template targets iPad landscape layout (1024x768 base).
- **D-13:** Template includes 8 remote user slots (covers most sessions). Each slot has: name label, volume fader (0-1), pan knob (0-1), mute button, solo button. OSC server supports all 16 slots — template can be extended by the user if needed.
- **D-14:** Template also includes: 4 local channel strips (from Phase 9), master volume + mute, metronome volume + pan + mute, session info panel (BPM, BPI, beat, status, users), connect/disconnect button.
- **D-15:** Template uses the full address namespace documented in docs/osc.md. Zero manual configuration — user imports template, sets host IP in TouchOSC connections, and it works.
- **D-16:** VU meters in template: master L/R and local 1-4 L/R bars. Remote VU optional (may be too dense for 16 slots).

### Dynamic Address Generation
- **D-17:** Remote user addresses are generated dynamically at send time in OscServer::timerCallback(), not added to the static OscAddressMap. This avoids rebuilding the map on roster changes and matches the VU meter pattern from Phase 9 (always-dirty, no dirty tracking).
- **D-18:** Incoming remote user OSC messages are parsed by prefix matching `/JamWide/remote/` in handleOscOnMessageThread(), extracting idx and control name from the path. No static map entry needed for receive either.
- **D-19:** Use `processor.cachedUsers` (std::vector<RemoteUserSnapshot>) for send-time iteration. This is already populated by NinjamRunThread on the run thread and is safe to read from the message thread (atomic swap pattern).

### Claude's Discretion
- TouchOSC template visual design (colors, label fonts, fader styles within TouchOSC editor)
- Exact layout grid for 16 remote slots (rows x columns, spacing)
- Whether to include VU meters for all 16 remote slots or just first 8 in the template
- Exact string format for connect trigger parsing (delimiter between host and port)
- Error handling for malformed connect strings

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 9 OSC Implementation (extend this)
- `juce/osc/OscServer.h` — Current public API, timerCallback, send methods to extend
- `juce/osc/OscServer.cpp` — sendDirtyApvtsParams, sendVuMeters patterns to follow for remote users
- `juce/osc/OscAddressMap.h` — OscParamEntry struct, OscParamType enum (for reference, not for remote user addresses)
- `docs/osc.md` — Current OSC address spec to update with remote user addresses

### Remote User Infrastructure
- `src/core/njclient.h` — RemoteUser struct (line 174), GetRemoteUsersSnapshot (line 200), SetUserChannelState (line 203), GetLocalChannelMonitoring, GetNumUsers
- `src/core/njclient.cpp` — GetRemoteUsersSnapshot implementation (line 2696), SetUserChannelState (line 2811)
- `juce/ui/ChannelStripArea.cpp` — findRemoteIndex pattern (line 282), refreshFromUsers (line 375) — stable identity lookup pattern
- `juce/NinjamRunThread.cpp` — UserInfoChangedEvent dispatch (line 318), cachedUsers update

### Processor Integration
- `juce/JamWideJuceProcessor.h` — cachedUsers, cmd_queue, BusesProperties (16 remote slots), oscServer member
- `src/threading/ui_command.h` — SetUserChannelStateCommand struct for remote control dispatch

### TouchOSC
- `docs/osc.md` — Address reference to update with Phase 10 addresses

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `OscServer::sendVuMeters()` — pattern for always-dirty dynamic address generation (iterate, build address string, add to bundle)
- `OscServer::handleOscOnMessageThread()` — dispatch switch to extend with remote user case
- `OscAddressMap::apvtsPanToOsc()` / `oscPanToApvts()` — pan conversion reusable for remote users
- `processor.cachedUsers` — thread-safe remote user snapshot already available on message thread
- `ChannelStripArea::findRemoteIndex()` — stable identity lookup pattern for resolving user indices

### Established Patterns
- SPSC cmd_queue: message thread is single producer, run thread is consumer (all OSC → cmd_queue dispatches go through message thread via callAsync)
- Dirty-flag sender: 100ms timer tick, only send changed values (for controllable params). VU/telemetry always sent.
- Echo suppression: per-param oscSourced flag, cleared after one send tick
- SetUserChannelStateCommand: existing command struct for remote user control (set_volume, set_pan, set_mute, set_solo fields)

### Integration Points
- `OscServer::timerCallback()` — add `sendDirtyRemoteUsers(bundle)` call
- `OscServer::handleOscOnMessageThread()` — add `/JamWide/remote/` prefix parsing before static map lookup
- `NinjamRunThread` → `UserInfoChangedEvent` — trigger roster broadcast in OscServer
- `docs/osc.md` — update with remote user address tables
- `CMakeLists.txt` — no changes needed (OscServer already compiled)
- `assets/` directory — new directory for TouchOSC template file

</code_context>

<specifics>
## Specific Ideas

- Full 16-slot template despite most sessions being 2-8 people — user wants complete coverage
- Per sub-channel control for multi-channel users — full granularity, not just group bus
- Template should be zero-config: import into TouchOSC, set host IP, done

</specifics>

<deferred>
## Deferred Ideas

- Video control via OSC namespace (`/JamWide/video/*`) — Phase 13
- Phone-optimized template (4 remote slots) — future template variant
- OSC query protocol (OSCQuery) for automatic address discovery — future

</deferred>

---

*Phase: 10-osc-remote-users-and-template*
*Context gathered: 2026-04-06*
