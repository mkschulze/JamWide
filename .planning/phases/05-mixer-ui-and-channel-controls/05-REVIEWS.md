---
phase: 5
reviewers: [codex]
reviewed_at: 2026-04-04T12:00:00Z
plans_reviewed: [05-01-PLAN.md, 05-02-PLAN.md, 05-03-PLAN.md]
---

# Cross-AI Plan Review -- Phase 5

## Codex Review

### Cross-plan note
There is one requirement conflict to resolve before implementation: the roadmap says metronome supports volume, pan, and mute, but locked decision `D-18` says volume + mute only. The plans follow `D-18`. That needs an explicit product decision, otherwise Phase 5 can be "complete" while still failing its written success criteria.

### Plan 05-01: VbFader Component + LookAndFeel + APVTS

**Summary:** Reasonable first wave isolating highest-risk custom UI work and parameter plumbing. Main weakness is parameter/persistence model not fully specified.

**Strengths:**
- Building VbFader as custom juce::Component matches design constraints better than forcing juce::Slider
- Putting pan/metronome styling in JamWideLookAndFeel keeps custom painting surface area small
- Starting parameter work early is good since processor only has 4 params currently

**Concerns:**
- `HIGH`: APVTS ownership model underspecified. UI-only state like uiScale and chatVisible should not casually become automatable host parameters -- risks polluting host automation and preset behavior
- `HIGH`: "14 new params" count doesn't align with D-21 which also requires local transmit and input selector persistence -- these are not audio parameters and should not be treated the same way
- `MEDIUM`: VbFader needs both APVTS mode and plain callback mode for remote (non-persistent) strips -- plan should state this explicitly
- `MEDIUM`: dB/UI mapping (linear 0.0-2.0 vs -inf to +6 dB) conversion, snap/reset, and near-zero clamping should be treated as design-critical
- `LOW`: Global drawLinearSlider override can cause style bleed if more JUCE sliders appear later

**Suggestions:**
- Define persistence matrix: APVTS audio params, processor non-param state, non-persistent remote state
- Keep uiScale and chatVisible as processor/editor state properties, not APVTS parameters
- Specify VbFader's full contract: value domain, dB rules, reset points, wheel behavior
- Limit LookAndFeel overrides to specific slider types via IDs/properties

**Risk Assessment:** MEDIUM

### Plan 05-02: ChannelStrip Controls + Remote Wiring

**Summary:** Fits existing code structure well. Main risks are stale index-based callbacks, layout math, and attachment ownership.

**Strengths:**
- Extends current ChannelStrip/ChannelStripArea split instead of inventing new architecture
- Remote control changes aligned with existing command path in NinjamRunThread
- Filling existing footer placeholder is clean incremental move

**Concerns:**
- `HIGH`: ChannelStripArea width/position calculation doesn't distinguish visible from hidden strips -- adding more controls makes this bug more visible
- `HIGH`: Remote callbacks capture user_index at build time. User list reorder (async from run thread) can make click target wrong user/channel
- `MEDIUM`: Only wires local monitoring for ch0 -- partial nature should be called out
- `MEDIUM`: Scroll-wheel forwarding can conflict with horizontal Viewport scrolling -- consumption rules needed
- `MEDIUM`: Attachment lifetime not mentioned -- if strips are rebuilt often, attachment teardown needs definition
- `LOW`: Solo state needs both write-path and restore/display-path coverage

**Suggestions:**
- Introduce lightweight strip view-model with stable identity and visible filtering
- Use stable remote identifier or rebuild commands from fresh lookup at interaction time
- Decide attachment ownership before coding
- Specify wheel precedence: hovered fader consumes vertical wheel, otherwise viewport scrolls

**Risk Assessment:** MEDIUM-HIGH

### Plan 05-03: Local 4-Channel + Metronome + State Persistence

**Summary:** Weakest of the three because it combines several unrelated high-risk changes. Understates core audio-path work needed for 4 local channels.

**Strengths:**
- Recognizes that local channels, metronome, and persistence close the phase against roadmap
- Correctly puts persistence in processor/editor code
- Notes additive solo is already handled in NJClient

**Concerns:**
- `HIGH`: 4-channel local plan is incomplete at audio-thread level. processBlock() currently copies only 2 input channels and calls AudioProc(..., 2, ..., 2, ...). UI and APVTS attachments alone won't make 4 local channels work
- `HIGH`: VU data model only has one local stereo pair in ui_state.h. 4-channel polling needs data-structure change first
- `HIGH`: Persistence via APVTS copyState/replaceState with extra ValueTree properties at state version 1 is risky without explicit schema and restore order
- `MEDIUM`: chatSidebarVisible vs scaleFactor ownership inconsistency -- need one consistent model
- `MEDIUM`: Input-bus selector restore needs validation against actual host bus layout
- `MEDIUM`: Metronome requirement conflict (D-18 vs roadmap text) unresolved
- `LOW`: Missing verification scope (save/load tests, editor reconstruction, reconnect tests)

**Suggestions:**
- Split into two tracks: 05-03a local multi-channel audio/threading, 05-03b persistence/editor/metronome
- Add processor work items for multi-bus input collection and channel-to-NJClient source mapping
- Expand UiAtomicSnapshot or add separate lock-free local-meter structure before 4-channel UI
- Define concrete serialized schema: APVTS for automatable audio, ValueTree subtree for UI/prefs
- Add restore validation rules for input selectors, transmit flags, chat state

**Risk Assessment:** HIGH

### Overall Assessment
The phase breakdown is directionally good. 05-01 and 05-02 are close to executable with tightening. 05-03 carries too much risk and should be decomposed. The biggest technical gap is not styling or APVTS plumbing; it is that the current processor still behaves like a 2-in/2-out path despite the roadmap moving toward multi-channel local control.

---

## Consensus Summary

*Single reviewer (Codex). Cross-reviewer consensus analysis not applicable.*

### Key Concerns (Priority Order)

1. **processBlock() is 2-in/2-out** -- 4 local NINJAM channels need audio-path changes, not just UI wiring (Plan 05-03)
2. **uiScale/chatVisible as APVTS params** -- UI-only state should not be automatable host parameters (Plan 05-01)
3. **State persistence schema** -- extra ValueTree properties need explicit schema, restore order, and defaults (Plan 05-03)
4. **Stale user_index captures** -- remote callbacks may target wrong user after async list reorder (Plan 05-02)
5. **Metronome requirement conflict** -- D-18 says no pan, roadmap success criteria says pan (All plans)

### Agreed Strengths
- VbFader as custom Component is the right choice
- Existing command queue architecture maps cleanly to mixer controls
- NJClient's built-in solo handling avoids unnecessary UI-side complexity
- Sequential wave ordering (01 -> 02 -> 03) respects component dependencies

### Divergent Views
*N/A -- single reviewer*

---

*Reviewed: 2026-04-04*
*Reviewers: Codex CLI (OpenAI)*
