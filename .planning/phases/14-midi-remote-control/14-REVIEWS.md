---
phase: 14
reviewers: [codex]
reviewed_at: 2026-04-11T17:15:00Z
plans_reviewed: [14-01-PLAN.md, 14-02-PLAN.md, 14-03-PLAN.md]
---

# Cross-AI Plan Review — Phase 14

## Codex Review

### Plan 14-01: MIDI Mapper Core

#### Summary
This plan covers most of the phase-critical backend work, but it currently mixes several high-risk changes into one large task and leaves two architectural points under-specified: plugin MIDI I/O enablement and how persistent mappings are serialized into plugin state. The APVTS expansion is consistent with the current codebase, and the separation from audio-thread `cachedUsersMutex` access is correct in intent, but the proposed 100 ms APVTS-to-NJClient sync path looks like a likely responsiveness regression for mixer control compared to the current immediate `cmd_queue` model.

#### Strengths
- Reuses the established OSC pattern: processor-owned service, dirty tracking, feedback suppression, message-thread sync.
- Correctly recognizes that remote mixer controls need APVTS backing if they are to be learnable, automatable, and persistent.
- State version bump is explicitly planned, which matches the current `stateVersion` migration approach.
- Includes tests for the right categories: dispatch, persistence, learn, feedback, echo suppression, and sync.
- Keeps `cachedUsersMutex` out of `processBlock`, which is necessary in this codebase.
- Standalone MIDI lifecycle is called out separately instead of assuming plugin-mode routing covers both environments.

#### Concerns
- **HIGH**: The plan does not explicitly include changing `acceptsMidi()` / `producesMidi()` or otherwise enabling plugin MIDI I/O. The current processor returns `false` for both. Without that, host-routed MIDI CC input and controller feedback will fail in plugin mode.
- **HIGH**: `processBlock` currently ignores the MIDI buffer entirely. The plan says "CC dispatch in processBlock, feedback via processBlock MidiBuffer" but does not mention ordering, buffer iteration safety, or preserving non-CC MIDI events.
- **HIGH**: "APVTS-to-NJClient sync via timerCallback (100ms)" is likely too slow for mixer control. Existing UI remote actions push commands immediately; replacing that with a 100 ms polling bridge risks visible and audible lag.
- **HIGH**: Persistence is a success criterion, but the plan does not define the mapping storage schema. It needs an explicit non-APVTS `ValueTree` layout, migration behavior, validation, and duplicate/conflict handling.
- **MEDIUM**: A fixed "suppress 2 frames" echo suppression policy is brittle. Frame duration varies wildly by host block size, and MIDI/controller loops are usually better handled by source-tagged suppression or per-mapping last-sent suppression.
- **MEDIUM**: "Atomic pointer swap (`shared_ptr`)" is probably more complex than necessary unless reads truly happen concurrently from audio and message threads. It may also be awkward depending on the project's C++ level and ownership model.
- **MEDIUM**: 69 new parameters is acceptable, but it expands host automation/state surface significantly. The plan should call out host-facing naming, ordering, and backwards-compatibility implications.
- **MEDIUM**: Standalone MIDI open/close/error methods are listed, but not the ingestion path into the audio-thread-safe mapper. JUCE device callbacks and plugin `processBlock` MIDI are not interchangeable.

#### Suggestions
- Explicitly add plugin MIDI capability work: update `acceptsMidi()`, `producesMidi()`, bus/config implications, and format-specific validation for VST3/AU/CLAP.
- Replace the 100 ms polling bridge for user-driven remote control with immediate message-thread dispatch to `cmd_queue`, and use APVTS primarily as the canonical UI/automation state.
- Define a concrete mapping persistence schema in plugin state now.
- Make echo suppression mapping-aware: `(channel, cc, direction, lastValue/time)` is safer than a global 2-frame window.
- Split the plan into at least two backend tasks: transport/state plumbing and mapping/learn/feedback logic. The current "single large TDD task" is too failure-prone.
- Add tests for duplicate mapping conflicts, malformed saved mappings, stale remote slot mappings, and "unknown device restored" behavior in standalone mode.
- Specify whether outgoing feedback merges with or replaces host-provided outgoing MIDI, and how sample offsets are assigned.

#### Risk Assessment
**MEDIUM-HIGH**. The plan is directionally correct, but two missing details are phase blockers: actual plugin MIDI enablement and a low-latency state-to-command path. If those are corrected early, the remaining work looks manageable.

---

### Plan 14-02: MIDI Learn UX

#### Summary
This plan is strong on concrete UI decisions and aligns well with the existing JamWide UI architecture, especially the OSC status/config pattern. The main risks are host compatibility around right-click behavior, brittleness in parameter identification/range display, and an over-reliance on manual verification for behavior that will vary across plugin formats and controller types.

#### Strengths
- Matches the project's locked UX decisions closely.
- Reuses existing UI precedent (`OscStatusDot`, config dialog, callout entrypoint), which should keep the feature visually and structurally consistent.
- Separates learn UX from mapper core, which is the right dependency boundary.
- Includes a human checkpoint before completion, which is appropriate for controller hardware workflows.
- Wiring context menus at the control level maps cleanly to the existing `ChannelStrip` / `VbFader` composition.

#### Concerns
- **HIGH**: Right-click behavior in plugin editors is host-sensitive. Some hosts reserve right-click for automation/parameter menus, especially on automatable controls. The plan does not mention host compatibility fallback when the custom MIDI Learn menu cannot appear.
- **MEDIUM**: Intercepting right-clicks for pan/mute/solo via `mouseListener` is fragile in JUCE if child controls consume events or if hit-testing changes. This needs a precise ownership model.
- **MEDIUM**: Deriving the "Range" column from `paramId` strings is brittle. Range/type should come from parameter metadata, not naming conventions.
- **MEDIUM**: Standalone device persistence by display name can be unstable across OSes and duplicate devices. The plan does not state whether it uses stable identifiers.
- **LOW**: The status dot shows three states, but the plan does not define what "red" specifically means when input works but output feedback device open fails, or vice versa.

#### Suggestions
- Add a host-compatibility fallback path for learn initiation if right-click is blocked.
- Prefer explicit parameter metadata for learnability, display name, type, and display range instead of parsing `paramId`.
- Define standalone device persistence as "stable identifier if available, else name with graceful fallback."
- Add automated tests for learn-state transitions and mapping-table CRUD, even if the final acceptance gate is manual.
- Specify exact error semantics for the footer status dot: disabled, healthy, degraded, failed.

#### Risk Assessment
**MEDIUM**. The UI work is well scoped and coherent, but host/plugin UX edge cases could cause a good implementation to fail in real DAWs unless fallback behavior is planned.

---

### Plan 14-03: OSC/UI Remote APVTS Integration

#### Summary
This is the most architecture-sensitive plan and, in broad terms, it is the right bridge to make remote parameters controllable by MIDI. The plan shows good awareness of current threading issues, especially the existing `cachedUsersMutex` usage, but it still risks creating split-brain state between APVTS, `cmd_queue`, and cached remote-user snapshots if ownership and update direction are not made explicit.

#### Strengths
- Targets the two real remote-entry points in the current codebase: OSC receive and UI callbacks.
- Calls out deadlock risk explicitly and proposes reducing mutex scope, which is necessary.
- Respects the existing decision not to APVTS-back sub-channels.
- Recognizes that remote OSC writes must update APVTS too if MIDI feedback is to be coherent.

#### Concerns
- **HIGH**: The plan appears to accept double dispatch as "idempotent," but duplicated `cmd_queue` sends for remote changes are not free. They may reorder, cause redundant host notifications, or produce unnecessary network-side churn.
- **HIGH**: State ownership is still ambiguous. If UI, OSC, and MIDI can all write APVTS and also independently push `cmd_queue`, the system can easily drift into feedback loops or duplicate side effects.
- **MEDIUM**: Remote parameters are slot-based, not identity-based. That is acceptable for current scope, but mappings may appear to control "the wrong person" after roster changes unless the UI makes the slot model explicit.
- **MEDIUM**: Empty remote slot reset is mentioned in decisions, but the plan does not say where that reset is triggered relative to APVTS, UI refresh, and feedback emission.
- **MEDIUM**: Group solo behavior is inherently fan-out to all sub-channels in current architecture. The plan should explicitly test that APVTS remote solo state remains consistent when individual child solos diverge.

#### Suggestions
- Define a single authoritative mutation path for remote parameters.
- Use APVTS as canonical state, but centralize command emission in one observer/bridge so UI/OSC/MIDI do not each emit commands independently.
- Treat duplicate dispatch as a bug to eliminate, not an accepted property.
- Add explicit tests for roster shrink/expand, empty-slot reset, and conflicting updates from OSC and MIDI in the same tick.
- Document slot-based semantics in the UX and mapping table so persistence behavior is predictable.

#### Risk Assessment
**MEDIUM-HIGH**. The plan is necessary and mostly well targeted, but it touches the highest-risk area: remote state ownership across threads and protocols. It will succeed only if mutation flow is tightened further.

---

## Consensus Summary

### Agreed Strengths
- Architecture follows proven OSC pattern — processor-owned service, dirty tracking, echo suppression
- Correct separation of audio thread and message thread concerns (cachedUsersMutex out of processBlock)
- Good test coverage strategy across all critical behaviors
- UI plan reuses existing components (OscStatusDot, CallOutBox) for consistency
- All locked decisions from CONTEXT.md are addressed

### Agreed Concerns
1. **Plugin MIDI I/O enablement** — `acceptsMidi()` / `producesMidi()` changes and processBlock MIDI buffer handling must be explicit in Plan 01 (actually present in plan action Step 5, but reviewer missed it in the summary — this is a FALSE POSITIVE from the review)
2. **100ms APVTS-to-NJClient sync latency** — Timer-based polling may be too slow for real-time mixer control. OSC uses the same approach at 100ms, but this warrants evaluation during implementation.
3. **Remote parameter state ownership** — Multiple writers (UI, OSC, MIDI) to both APVTS and cmd_queue creates risk of duplicate dispatch and split-brain state
4. **Host right-click compatibility** — Some DAW hosts intercept right-click on plugin UI, blocking MIDI Learn menus
5. **Echo suppression brittleness** — Fixed 2-frame window may not adapt to varying block sizes

### Divergent Views
- N/A (single reviewer)

### Reviewer Notes on FALSE POSITIVES
Several HIGH concerns from the Codex review are actually addressed in the detailed plan action text but were missed because the reviewer only saw the summary:
- **acceptsMidi/producesMidi**: Plan 01 Step 5 explicitly changes both to `true`
- **processBlock ordering**: Plan 01 Step 5 explicitly specifies processIncomingMidi FIRST, then appendFeedbackMidi
- **Persistence schema**: Plan 01 Step 6 defines the ValueTree "MidiMappings" schema with paramId/cc/channel entries
- **Plan 01 is too large**: The TDD approach with 11 tests provides the safety net the reviewer wants from splitting

Real concerns worth addressing in replanning:
1. **100ms timer latency** — Consider whether the timer approach is truly sufficient
2. **Double cmd_queue dispatch** — Plan 03 acknowledges this as idempotent but should aim to eliminate
3. **Host right-click fallback** — Should add alternative MIDI Learn entry point (e.g., config dialog button)
4. **Slot-based mapping stability on roster changes** — D-17 handles reset, but mapping table should show slot numbers not usernames

---

*Reviewed: 2026-04-11*
*Reviewer: Codex (OpenAI o3)*
