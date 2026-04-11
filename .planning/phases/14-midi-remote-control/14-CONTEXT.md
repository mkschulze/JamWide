# Phase 14: MIDI Remote Control - Context

**Gathered:** 2026-04-11
**Status:** Ready for planning

<domain>
## Phase Boundary

MIDI CC mapping for all mixer parameters (local channels, remote channels, master, metronome), bidirectional feedback where possible, MIDI Learn UX, config UI with status indicator, and persistent mappings. Covers MIDI-01.

</domain>

<decisions>
## Implementation Decisions

### MIDI Learn UX
- **D-01:** MIDI Learn via right-click context menu on any fader/knob/button. Right-click → "MIDI Learn" starts listening, user moves CC on controller, mapping created. Also "Clear MIDI" option in same menu. Standard DAW convention (Ableton, Bitwig, REAPER pattern).
- **D-02:** Visual feedback during learn: parameter gets a colored highlight/pulsing border, small overlay shows "Waiting for CC..." then "CC 7 Ch 1" when received. Auto-closes after successful assignment.
- **D-03:** Mapping table dialog accessible from footer or menu. Shows all active mappings: Parameter | CC# | Channel | Range. Users can delete or edit mappings from this view. Provides overview of all assignments.

### Plugin vs Standalone MIDI Access
- **D-04:** Plugin mode uses host MIDI routing. Set `acceptsMidi()` to `true`, receive MIDI through `processBlock` `MidiBuffer`. User routes their MIDI controller to JamWide's input in the DAW. No device enumeration needed in plugin mode.
- **D-05:** Standalone mode gets its own MIDI device selector in a config dialog. Uses `juce_audio_devices` for device enumeration (MidiInput/MidiOutput). Device selection persists across sessions.
- **D-06:** Separate MIDI status dot in the footer, next to the existing OSC status dot. Same 3-state pattern: green = active (receiving MIDI), grey = off/no mappings, red = error. Click to open MIDI config dialog. MIDI and OSC are independent systems.

### CC Resolution & Value Mapping
- **D-07:** 7-bit standard CC only (0-127). No 14-bit CC pairs or NRPN. Universal compatibility with all controllers. 128 steps is sufficient for mixing parameters.
- **D-08:** Mute and solo use CC toggle: any CC value > 0 toggles the state, value 0 is ignored (button release). Same CC number used for feedback (LED state: 127 = on, 0 = off).
- **D-09:** Mappings store CC# + MIDI Channel as the identity key. Same CC number on different channels maps to different parameters. Enables multi-channel controllers (e.g., Ch1 CC1 = Local Vol 1, Ch2 CC1 = Remote Vol 1). Up to 16 channels × 128 CCs = 2048 unique mappings.
- **D-10:** Volume maps CC 0-127 linearly to the 0.0-1.0 normalized range (matching the APVTS/OSC convention). Pan maps CC 0-127 to 0.0-1.0 with 64 as center (0.5). Same value space as OSC normalized namespace.

### Feedback Behavior
- **D-11:** Echo suppression mirrors OSC pattern (Phase 9 D-14). When a value changes from MIDI input, mark it 'MIDI-sourced' and skip sending CC feedback for one tick. Prevents motorized fader oscillation.

### State Persistence
- **D-12:** MIDI mappings persist across DAW sessions via getStateInformation/setStateInformation. Bump state version (from current version). Store array of mapping entries: each entry has paramId (string), ccNumber (int), midiChannel (int).
- **D-13:** Standalone MIDI device selection persists separately (device name string in state).

### Claude's Discretion
- Feedback sending mechanism (timer-based dirty-flag, immediate, or processBlock output) — user deferred this decision
- Feedback timer interval if timer-based (100ms matching OSC, or different)
- Internal data structure for mapping storage (std::map, std::unordered_map, flat vector)
- MIDI config dialog layout and exact contents
- Whether `producesMidi()` returns true (depends on feedback mechanism choice)
- How MIDI feedback works in standalone vs plugin (may need different paths)
- Exact power curve for CC-to-volume mapping (linear CC or matched to VbFader's 2.5 exponent)
- Whether to link `juce_audio_devices` for all builds or standalone-only

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### OSC Server (parallel pattern to follow)
- `juce/osc/OscServer.h` — Public API, timer callback, send methods. MIDI server should follow same ownership and lifecycle pattern.
- `juce/osc/OscServer.cpp` — Dirty-flag sender, echo suppression, remote user handling. Reference for MIDI feedback implementation.
- `juce/osc/OscConfigDialog.h` / `OscConfigDialog.cpp` — Config dialog pattern to replicate for MIDI config.
- `juce/osc/OscStatusDot.h` / `OscStatusDot.cpp` — Footer status indicator to replicate for MIDI status.

### Processor Integration
- `juce/JamWideJuceProcessor.h` — `processBlock` with MidiBuffer, `acceptsMidi()` stubs, `oscServer` member pattern, state version, cmd_queue
- `juce/JamWideJuceProcessor.cpp` — `acceptsMidi()` returns false (must change to true), `processBlock` ignores MidiBuffer (must process), getStateInformation/setStateInformation for persistence

### Threading & Commands
- `src/threading/ui_command.h` — Command variants for dispatch. May need new MIDI-specific commands or reuse existing SetLocalChannelMonitoringCommand / SetUserChannelStateCommand.
- `src/threading/spsc_ring.h` — SPSC queue (single-producer invariant must be preserved for MIDI dispatch)

### Phase 9/10 Context (prior decisions)
- `.planning/phases/09-osc-server-core/09-CONTEXT.md` — OSC architecture decisions, especially D-12 (dirty-flag), D-14 (echo suppression), D-19 (callAsync threading), D-21 (state version)
- `.planning/phases/10-osc-remote-users-and-template/10-CONTEXT.md` — Remote user addressing pattern, dynamic address generation, cachedUsers snapshot

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `OscServer` pattern — lifecycle (processor member, unique_ptr), start/stop, timer-based sender, echo suppression. Direct template for MidiMapper/MidiServer.
- `OscConfigDialog` / `OscStatusDot` — UI components to replicate for MIDI config and status indicator.
- `OscAddressMap` — mapping table pattern (parameter ID to address/type). Adaptable for CC# + Channel → parameter mapping.
- `SetLocalChannelMonitoringCommand` / `SetUserChannelStateCommand` — existing command structs for dispatching parameter changes to the run thread. MIDI can reuse these directly.
- `processor.cachedUsers` — thread-safe remote user snapshot for mapping remote user indices to MIDI CCs.
- `VoicemeeterLookAndFeel` — dark theme for MIDI config dialog.

### Established Patterns
- SPSC cmd_queue: message thread → cmd_queue → run thread (single producer invariant via callAsync)
- Dirty-flag sender: 100ms timer, only send changed values (OSC pattern)
- Echo suppression: per-param source flag, cleared after one send tick
- State version migration: version check in setStateInformation with defaults for new fields
- Footer component bar: OSC status dot already exists, MIDI dot sits alongside

### Integration Points
- `JamWideJuceProcessor` — MidiMapper/MidiServer component lives here as member (same as OscServer)
- `JamWideJuceProcessor::processBlock()` — Parse incoming MidiBuffer for CC messages, route to mapper
- `JamWideJuceProcessor::acceptsMidi()` — Change from false to true
- `JamWideJuceEditor` — Footer bar gets MIDI status dot, right-click handlers on faders/knobs for MIDI Learn
- `CMakeLists.txt` — May need `juce::juce_audio_devices` for standalone MIDI device enumeration
- `getStateInformation` / `setStateInformation` — Add MIDI mapping entries, bump state version

</code_context>

<specifics>
## Specific Ideas

- Follow the OSC server pattern closely — processor member ownership, status dot in footer, config dialog, echo suppression
- Right-click MIDI Learn is the primary mapping workflow — should feel instant and familiar to DAW users
- Mapping table provides visibility into all assignments but is secondary to the Learn workflow
- 7-bit CC is intentional — the original concern about resolution was acknowledged and accepted as sufficient for mixing

</specifics>

<deferred>
## Deferred Ideas

- 14-bit CC pairs / NRPN support — could be a future enhancement if high-end controller users request it
- Note On/Off for toggle parameters — CC toggle is sufficient, but Note messages could be added later
- MIDI Learn for remote sub-channels — main remote user controls first, per-sub-channel can be added if needed
- Phone-optimized TouchOSC template variant with MIDI fallback — future template work

None — discussion stayed within phase scope

</deferred>

---

*Phase: 14-midi-remote-control*
*Context gathered: 2026-04-11*
