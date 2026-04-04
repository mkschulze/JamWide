# Phase 5: Mixer UI and Channel Controls - Context

**Gathered:** 2026-04-04
**Status:** Ready for planning

<domain>
## Phase Boundary

Add per-channel volume/pan/mute/solo controls to the existing Phase 4 channel strips, implement the metronome controls inside the master strip, create the custom VbFader component, and implement state persistence via APVTS. Remote user mixer (UI-04), local channel controls (UI-05), metronome controls (UI-06), live VU meters (UI-08), and state save/restore (JUCE-06) are all in scope.

NOT in scope: Multichannel output routing (Phase 6), DAW sync (Phase 7), new UI panels or layouts.

</domain>

<decisions>
## Implementation Decisions

### Fader Style and Interaction
- **D-01:** VB-Audio Voicemeeter Banana style big vertical fader per UI-SPEC -- 10px track, 44px circular thumb with green border, dB readout ON the thumb, green fill below thumb
- **D-02:** Mouse scroll wheel adjusts volume when hovering over a strip -- 1 step ~0.5 dB, fine-grained control
- **D-03:** Double-click on fader resets to 0 dB (unity gain)
- **D-04:** Double-click on pan knob/slider resets to center (0.0)
- **D-05:** dB readout format: one decimal place (e.g., "-6.0", "+2.5", "-inf")
- **D-06:** Fader range: -inf to +6 dB (0.0 to 2.0 linear), with dB scale ticks at +6, 0, -6, -18, -inf

### Control Layout in Strips
- **D-07:** Footer zone (38px): pan slider on top (18px), Mute + Solo buttons side by side below (18px)
- **D-08:** Pan slider is horizontal, center-notched, spanning full strip width (100px minus padding)
- **D-09:** Mute button: "M", red (#E04040) when active. Solo button: "S", yellow (#CCB833) when active
- **D-10:** VU meter (24px) + gap (6px) + fader track (10px) = 40px centered in 100px strip, per UI-SPEC

### Solo Behavior
- **D-11:** Additive solo -- multiple channels can be soloed simultaneously. Only soloed channels are heard; non-soloed channels are muted. Standard DAW mixer behavior.

### Local Channel Controls
- **D-12:** Expose all 4 NINJAM input channels using the same expand/collapse group pattern as remote multi-channel users
- **D-13:** Local strip uses parent/child logic: collapsed shows first channel, expanded shows all 4 with individual fader/VU/pan/mute/solo
- **D-14:** Each child strip has an input bus selector for its NINJAM channel
- **D-15:** Transmit toggle per local channel

### Metronome Controls
- **D-16:** Metronome controls live inside the master strip, below the master fader
- **D-17:** Horizontal metronome fader with yellow (#CCB833) fill, per UI-SPEC
- **D-18:** Metronome has volume + mute only (no pan control) -- keeps it simple
- **D-19:** NJClient config_metronome and config_metronome_mute atomics are already available

### State Persistence
- **D-20:** Persist via APVTS getStateInformation/setStateInformation (already scaffolded)
- **D-21:** Persist: master vol/mute, metronome vol/mute (already in APVTS), local channel vol/pan/mute/transmit, input selector
- **D-22:** Persist: last server address, last username (not password)
- **D-23:** Persist: UI scale factor (1x/1.5x/2x), chat sidebar visibility
- **D-24:** Do NOT persist remote user mixer state -- users change between sessions, keying by username would go stale
- **D-25:** State version already at 1 with forward-compatible migration pattern (Plan 02-01 decision)

### Claude's Discretion
- Custom VbFader component implementation details (paint, drag, thumb rendering)
- Pan slider implementation (horizontal juce::Slider customized or custom painted)
- How local channel expand/collapse stores expanded state
- Exact APVTS parameter IDs for new parameters (local channel vol/pan/mute)
- How solo logic is implemented (NJClient level vs UI-side mute routing)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### UI Design Contract
- `.planning/phases/04-core-ui-panels/04-UI-SPEC.md` -- Full UI specification: fader design (line 109), control inventory (line 235), strip layout (line 100), color tokens, spacing, component types
- `docs/assets/JamWide Sketch Design/JamWide-UI.sketch` -- Sketch design file (visual reference)

### Phase 4 Context
- `.planning/phases/04-core-ui-panels/04-CONTEXT.md` -- Prior decisions (D-01 through D-31) including visual design, layout, and theming

### Requirements
- `.planning/REQUIREMENTS.md` -- UI-04 (remote mixer), UI-05 (local controls), UI-06 (metronome), UI-08 (VU meters), JUCE-06 (state persistence)

### Existing Code
- `juce/ui/ChannelStrip.h` -- Existing strip with header/VU/footer zones (footer is 38px placeholder)
- `juce/ui/ChannelStripArea.h` -- Container with centralized 30Hz VU timer, strip management
- `juce/ui/VuMeter.h` -- Existing segmented LED VU (timerless, externally driven)
- `juce/JamWideJuceProcessor.h` -- APVTS with masterVol/masterMute/metroVol/metroMute, event queues, cachedUsers
- `juce/JamWideJuceProcessor.cpp` -- createParameterLayout() and getState/setState already scaffolded
- `src/core/njclient.h` -- SetUserChannelState(), SetLocalChannelInfo(), config_metronome atomics, GetUserChannelPeak()

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `VuMeter`: Segmented LED meter, timerless, driven by ChannelStripArea's 30Hz timer -- already working
- `ChannelStrip`: Has StripType enum (Local/Remote/RemoteChild/Master), configure(), header/VU/footer zones. Footer is placeholder.
- `ChannelStripArea`: Manages strips, 30Hz centralized timer, expand/collapse for remote users, Browse Servers button
- `JamWideLookAndFeel`: Full color token set including kAccentDestructive (mute red), kAccentWarning (solo yellow), kVu* colors
- `APVTS` with state version 1: masterVol, masterMute, metroVol, metroMute already defined

### Established Patterns
- Timer-driven updates: ChannelStripArea runs 30Hz timer calling tickVu() on all strips
- Command queue: UI pushes commands to processor via cmd_queue (SpscRing), run thread processes
- Event queue: Run thread pushes events to UI via evt_queue, editor drains at 20Hz
- cachedUsers: Run thread snapshots remote user state including VU levels every iteration

### Integration Points
- ChannelStrip.resized() footer section: currently `(void)footer` placeholder -- Phase 5 fills this
- ChannelStrip needs new VbFader component added to the VU zone alongside existing VuMeter
- Master strip in ChannelStripArea: needs metronome section added below master fader
- createParameterLayout(): needs new params for local channel vol/pan/mute, UI prefs
- NinjamRunThread::processCommands(): needs to apply local channel and metronome parameter changes to NJClient

</code_context>

<specifics>
## Specific Ideas

- VB-Audio Voicemeeter Banana fader style: big vertical fader with circular grab handle showing dB value -- this is the primary interaction element and should feel premium
- Local channels use the SAME expand/collapse group pattern as remote multi-channel users -- parent strip + child strips, not tabs
- Metronome is a horizontal yellow slider inside the master strip, not its own strip

</specifics>

<deferred>
## Deferred Ideas

None -- discussion stayed within phase scope

</deferred>

---

*Phase: 05-mixer-ui-and-channel-controls*
*Context gathered: 2026-04-04*
