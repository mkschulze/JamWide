# Phase 14: MIDI Remote Control - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-11
**Phase:** 14-midi-remote-control
**Areas discussed:** MIDI Learn UX, Plugin vs Standalone, CC Resolution & Mapping, Feedback to Controllers

---

## MIDI Learn UX

| Option | Description | Selected |
|--------|-------------|----------|
| MIDI Learn mode | Right-click param, move CC on controller, mapping created. Fast, intuitive, standard DAW convention. | ✓ |
| Manual table only | Open MIDI mapping dialog and manually type CC numbers for each parameter. | |
| Both with Learn as primary | MIDI Learn for quick assignment, plus full mapping table for review/edit/reorder. | |

**User's choice:** MIDI Learn mode
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| Right-click context menu | Right-click any fader/knob/button → context menu with 'MIDI Learn' and 'Clear MIDI'. Standard DAW convention. | ✓ |
| Global Learn toggle button | A 'MIDI Learn' button in toolbar/footer. When active, clicking any parameter enters learn mode. | |
| You decide | Claude picks based on existing UI patterns. | |

**User's choice:** Right-click context menu
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, dialog with mapping list | 'MIDI Mappings' dialog showing all active mappings: Parameter, CC#, Channel, Range. Users can delete or edit. | ✓ |
| No, just Learn + Clear per control | Keep it minimal — right-click to learn, right-click to clear. No overview table. | |
| You decide | Claude picks based on complexity of managing mappings. | |

**User's choice:** Yes, dialog with mapping list
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| Highlight + overlay text | Pulsing colored border on parameter being learned. Overlay shows 'Waiting for CC...' then 'CC 7 Ch 1'. Auto-closes after assignment. | ✓ |
| Status bar message only | Footer shows message. No visual change on the control itself. | |
| You decide | Claude picks based on existing UI patterns. | |

**User's choice:** Highlight + overlay text
**Notes:** None

---

## Plugin vs Standalone

| Option | Description | Selected |
|--------|-------------|----------|
| Host MIDI routing | Plugin receives MIDI through processBlock MidiBuffer. User routes controller in DAW. Set acceptsMidi() to true. | ✓ |
| Direct device access (bypass DAW) | Plugin opens MIDI ports directly using juce_audio_devices, bypassing DAW's MIDI routing. | |
| You decide | Claude picks based on standard plugin behavior. | |

**User's choice:** Host MIDI routing
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, device selector in config | Standalone gets MIDI input/output device dropdown in config dialog. Uses juce_audio_devices. | ✓ |
| Standalone uses same host routing | Rely on JUCE standalone wrapper's built-in MIDI settings. No custom UI. | |
| You decide | Claude picks based on what JUCE standalone wrapper provides. | |

**User's choice:** Yes, device selector in config
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| Separate MIDI status dot | Second status dot in footer next to OSC dot. Same green/grey/red pattern. Click to open MIDI config. | ✓ |
| Combined remote control dialog | One 'Remote Control' button opening a tabbed dialog with OSC and MIDI tabs. | |
| You decide | Claude picks based on footer space and existing patterns. | |

**User's choice:** Separate MIDI status dot
**Notes:** None

---

## CC Resolution & Mapping

| Option | Description | Selected |
|--------|-------------|----------|
| 7-bit standard only | Standard MIDI CC 0-127. Universal, 128 steps. Works with every controller. | ✓ |
| 7-bit + optional 14-bit | Default 7-bit with option for 14-bit CC pairs (CC 0-31 paired with CC 32-63). | |
| You decide | Claude picks based on controller market reality. | |

**User's choice:** 7-bit standard only
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| CC toggle (any value > 0 = toggle) | CC value > 0 toggles state, value 0 ignored (button release). Same CC for feedback (LED). | ✓ |
| CC threshold (>63 = on, <=63 = off) | CC value directly sets state. More explicit but less intuitive for toggle buttons. | |
| Note On/Off for toggles | Use Note messages for mute/solo. Separates buttons from faders in MIDI namespace. | |
| You decide | Claude picks most compatible approach. | |

**User's choice:** CC toggle (any value > 0 = toggle)
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, CC# + Channel | Mapping stores both CC number and MIDI channel. Allows reuse across channels. Standard for multi-channel controllers. | ✓ |
| Omni mode (ignore channel) | Only CC number matters. Simpler but limits to 128 unique mappings. | |
| You decide | Claude picks based on controller compatibility. | |

**User's choice:** Yes, CC# + Channel
**Notes:** None

---

## Feedback to Controllers

| Option | Description | Selected |
|--------|-------------|----------|
| Timer-based dirty-flag | Same pattern as OSC: 100ms timer, send CC for dirty params only. Runs on message thread. | |
| Immediate on change | Send CC feedback instantly on any parameter change. Lower latency but more MIDI traffic. | |
| processBlock output | Write feedback into processBlock MidiBuffer output. Audio-rate timing, plugin mode only. | |
| You decide | Claude picks based on existing patterns. | |

**User's choice:** Deferred — user said "defer for now"
**Notes:** User deferred the feedback mechanism decision. Left to Claude's discretion during planning.

---

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, same pattern as OSC | MIDI-sourced values skip feedback for one tick. Prevents motorized fader oscillation. | ✓ |
| You decide | Claude implements based on existing OSC pattern. | |

**User's choice:** Yes, same pattern as OSC
**Notes:** None

---

## Claude's Discretion

- Feedback sending mechanism (timer-based, immediate, or processBlock) — user deferred
- Feedback timer interval
- Internal mapping data structure
- MIDI config dialog layout
- Whether producesMidi() returns true
- Standalone vs plugin feedback paths
- CC-to-volume power curve
- juce_audio_devices linkage scope

---

## Remote Parameter APVTS Exposure (Update Session)

**Date:** 2026-04-11 (follow-up session)

### Q1: Should remote user faders be exposed as host-automatable APVTS parameters?

| Option | Description | Selected |
|--------|-------------|----------|
| MIDI Learn only | Remote faders learnable via MIDI CC inside JamWide, but not exposed as DAW automation parameters. | |
| Also expose as APVTS | Create fixed APVTS parameters for Remote 1-16. Makes them automatable in the DAW's parameter list. | ✓ |
| Expose limited set | Only Remote 1-8 group-level as APVTS. Compromise between discoverability and parameter bloat. | |

**User's choice:** Also expose as APVTS
**Notes:** User wants remote faders exposed to the host DAW and learnable.

---

### Q2: How many remote user slots should get APVTS parameters?

| Option | Description | Selected |
|--------|-------------|----------|
| 16 slots (Recommended) | Matches NINJAM's max user count. 64 new parameters (16 × 4). | ✓ |
| 8 slots | 32 new parameters. Users beyond 8 not host-automatable. | |

**User's choice:** 16 slots (Recommended)

---

### Q3: Should remote sub-channels also get APVTS parameters?

| Option | Description | Selected |
|--------|-------------|----------|
| Group-level only (Recommended) | Sub-channels remain MIDI-learnable only. Keeps parameter count manageable. | ✓ |
| Group + sub-channels | 512 additional parameters. Major parameter bloat. | |

**User's choice:** Group-level only (Recommended)

---

### Q4: Should local channel solo and metronome pan be promoted to APVTS?

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, promote both | Add localSolo_0..3 and metroPan to APVTS. 5 extra parameters. | ✓ |
| No, leave as-is | Keep as cmd_queue dispatch only. | |

**User's choice:** Yes, promote both

---

### Q5: When a remote slot is empty, what happens to its APVTS parameter?

| Option | Description | Selected |
|--------|-------------|----------|
| Reset to defaults | Volume 1.0, pan center, unmuted, unsoloed. Clean state. | ✓ |
| Retain last values | Preserve levels for reconnection. | |
| You decide | Claude picks based on NJClient behavior. | |

**User's choice:** Reset to defaults

---

## Deferred Ideas

- 14-bit CC pairs / NRPN support
- Note On/Off for toggles
- Remote sub-channel APVTS exposure (group-level covered, sub-channels deferred)
- Phone-optimized template with MIDI fallback
