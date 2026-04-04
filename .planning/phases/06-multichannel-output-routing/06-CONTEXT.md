# Phase 6: Multichannel Output Routing - Context

**Gathered:** 2026-04-04
**Status:** Ready for planning

<domain>
## Phase Boundary

Route each remote participant to a separate stereo output pair in the DAW for independent mixing and processing. Auto-assign modes (by user, by channel), manual routing via per-strip selector, metronome on dedicated bus, main mix always on bus 0. Requirements: MOUT-01, MOUT-02, MOUT-03, MOUT-04, MOUT-05.

NOT in scope: Local channel routing (stays on Main Mix), DAW sync (Phase 7), new UI panels.

</domain>

<decisions>
## Implementation Decisions

### Routing Mode and Assignment
- **D-01:** Default routing mode is Manual -- all remote users on Main Mix (bus 0). User opts in to multichannel via quick-assign or per-strip selector.
- **D-02:** Quick-assign button in ConnectionBar (near Fit button) with dropdown: "Assign by User" and "Assign by Channel". Assigns all current users to sequential buses in one click.
- **D-03:** In "by user" mode, multi-channel users get one stereo pair per user (all channels mixed to one bus), not one per channel.
- **D-04:** Per-strip routing selector (already exists from Phase 4) overrides auto-assignment for individual strips.
- **D-05:** When a user leaves, their bus stays reserved until manually reassigned. No automatic shifting of other users' buses.
- **D-06:** When all 16 Remote buses are occupied, new users fall back to Main Mix (bus 0). No error, no warning.

### Audio Path
- **D-07:** Expand AudioProc output buffer to capture per-user audio before mixing. Similar to ReaNINJAM approach. Pass larger buffer (2 * numUsers channels) to get individual user audio.
- **D-08:** Main Mix (bus 0) always contains the full mix of all users + metronome + local monitoring, regardless of individual routing. Individual buses get copies.
- **D-09:** Volume/pan from the mixer (Phase 5) applies BEFORE bus routing. Individual buses get the mixed signal. DAW can do its own mixing on the separate tracks.
- **D-10:** Local monitoring audio stays on Main Mix only. Not routable to separate buses (DAW already gets dry input via plugin input).

### Metronome Bus
- **D-11:** Metronome always on the last declared output bus (bus 17 / Remote 16). Fixed, not user-configurable.

### Persistence
- **D-12:** Persist routing mode setting (manual/by-user/by-channel) and metronome bus in plugin state. Individual user-to-bus mappings are NOT persisted (consistent with D-24 from Phase 5 -- users change between sessions).
- **D-13:** Default routing mode on first load: Manual (all on Main Mix).

### DAW Compatibility
- **D-14:** Target DAWs: REAPER, Logic Pro, Bitwig Studio. Multi-output plugin behavior tested in all three.
- **D-15:** Show a tooltip/help text in the routing area: "Enable additional outputs in your DAW's plugin I/O settings". DAW-specific instructions in documentation, not in the plugin UI.

### Bug Note
- **D-16:** User reports username may not persist correctly. Phase 5 implemented D-22 (persist lastUsername as ValueTree property). Verify during Phase 6 development and fix if broken.

### Claude's Discretion
- AudioProc buffer expansion implementation details (how to extract per-user audio from NJClient internals)
- Quick-assign button UI design (dropdown vs split button)
- Bus labeling in the routing selector (current "Out 1/2" naming is fine, or improve)
- Signal routing implementation in processBlock (how to copy per-user audio to output buses)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Audio Architecture
- `juce/JamWideJuceProcessor.cpp` -- processBlock with multi-bus input, BusesProperties with 17 stereo outputs already declared, isBusesLayoutSupported
- `src/core/njclient.h` -- AudioProc signature, per-user audio mixing, m_users internal state
- `src/core/njclient.cpp` -- AudioProc implementation (~line 767), per-user audio mixing loop

### UI Components
- `juce/ui/ChannelStrip.h` -- routingSelector ComboBox (Out 1/2 through Out 16), onRoutingChanged callback
- `juce/ui/ConnectionBar.h` -- fitButton placement (quick-assign goes near here)

### State Persistence
- `juce/JamWideJuceProcessor.cpp` -- getStateInformation/setStateInformation with ValueTree properties

### Reference Implementation
- ReaNINJAM (`/Users/cell/dev/ninjam/ninjam`) -- per-user stereo pair routing, auto-assign by user/channel modes

### Requirements
- `.planning/REQUIREMENTS.md` -- MOUT-01 through MOUT-05

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **17 stereo output buses** already declared in BusesProperties (Main Mix + Remote 1-16)
- **routingSelector ComboBox** on each ChannelStrip (Out 1/2 through Out 16) with onRoutingChanged callback
- **isBusesLayoutSupported()** validates all buses as stereo or disabled
- **processBlock** already handles multi-bus input (4 stereo pairs from Phase 5)

### Established Patterns
- Command queue (SpscRing) for UI-to-audio thread communication
- ValueTree properties for non-APVTS persistent state
- ConnectionBar for global controls (Connect, Browse, Fit, Codec)

### Integration Points
- processBlock: expand output writing from stereo-only to per-bus routing
- NJClient::AudioProc: expand output buffer or post-process per-user audio
- ChannelStripArea: wire routingSelector callbacks to update routing state
- ConnectionBar: add quick-assign button with dropdown

</code_context>

<specifics>
## Specific Ideas

- Quick-assign button near the Fit button in ConnectionBar -- dropdown with "Assign by User" and "Assign by Channel"
- ReaNINJAM is the reference for how multichannel output routing should work
- Main Mix always has everything (no exclusive routing)
- Metronome on fixed last bus -- simple and predictable

</specifics>

<deferred>
## Deferred Ideas

None -- discussion stayed within phase scope

</deferred>

---

*Phase: 06-multichannel-output-routing*
*Context gathered: 2026-04-04*
