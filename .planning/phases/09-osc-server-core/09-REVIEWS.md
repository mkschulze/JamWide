---
phase: 9
reviewers: [codex, claude]
reviewed_at: 2026-04-06T18:00:00Z
plans_reviewed: [09-01-PLAN.md, 09-02-PLAN.md]
---

# Cross-AI Plan Review — Phase 9

## Codex Review

### Plan 09-01: OSC Server Engine

#### Summary
This plan is directionally solid and covers the core mechanics needed for bidirectional OSC control, but it carries meaningful threading and behavioral risk for a real-time plugin. The overall architecture is plausible, especially the separation between OSC receive, message-thread dispatch, and timer-based feedback sending, yet a few choices need tighter definition to avoid dropped updates, feedback edge cases, and UI/state mismatch once Wave 2 integrates.

#### Strengths
- Clear separation between inbound OSC handling and outbound feedback.
- Uses timer-based dirty flushing, which matches the locked decision set and limits message spam.
- Echo suppression is explicitly planned rather than assumed.
- Correctly avoids forcing solo through APVTS if that would violate existing command-queue constraints.
- Includes loopback testing early, which is a good foundation for integration confidence.
- Scope is mostly aligned with Phase 9 core requirements: mixer control, feedback, telemetry, metronome control.

#### Concerns
- `HIGH` `callAsync()` for every inbound OSC event can become a message-thread flooding path under bursty controller traffic, especially if multiple faders or VU/telemetry-style sources are active. The plan says callbacks dispatch to the message thread, but not how coalescing/backpressure is handled.
- `HIGH` Echo suppression "for one tick" is underspecified and may fail for fast alternating local/remote edits. A single-tick skip may suppress legitimate feedback or still allow oscillation if local state changes again before the next send window.
- `HIGH` The plan does not define ownership and synchronization for the "reads from atomics" sender path. For a large mixed payload set, partial snapshots across multiple atomics can produce inconsistent bundles unless snapshotting semantics are explicit.
- `MEDIUM` VU meters "always-dirty at 100ms" may be acceptable for TouchOSC, but this is a permanent outbound stream regardless of client interest. That can create needless traffic and should be justified against OSC-06/D-15.
- `MEDIUM` The 44-entry address map may be too rigid if user count, metronome endpoints, telemetry expansion, or future video/OSC integration adds addresses. Fixed-count maps are fine if generated or centrally validated, but brittle if handwritten.
- `MEDIUM` The dB conversion choice may create visible mismatch between OSC values and UI behavior if the UI fader law uses a different perceptual curve.
- `MEDIUM` Error handling is not described for bind failures, malformed OSC types, unsupported addresses, or send failures.
- `LOW` Loopback test coverage sounds narrow. It validates basic linkage and transport, but not race conditions, bundle contents, echo suppression, or malformed input behavior.

#### Suggestions
- Define an inbound coalescing layer before `callAsync()`: enqueue latest-value-per-address and schedule at most one pending async drain at a time.
- Specify snapshot semantics for outbound sends: copy all dirty atomics into a local struct on the message thread before building the bundle.
- Replace "one tick" echo suppression with a more exact source-tag or generation-counter rule per parameter.
- Add explicit handling for invalid OSC message shapes: wrong type, wrong arity, out-of-range values, unknown addresses.
- Validate that outbound bundle size stays reasonable when VU meters, telemetry, and dirty mixer values are all present in the same tick.

#### Risk Assessment: MEDIUM-HIGH

### Plan 09-02: OSC UI + Persistence

#### Summary
This plan is appropriately scoped and makes the right ownership decision by keeping `OscServer` in the processor, but it currently assumes the engine state model is already rich enough to support clean UI/error behavior.

#### Strengths
- Correct ownership: processor-level `OscServer` avoids editor-lifetime bugs.
- State version bump with backward-compatible defaults is the right migration approach.
- Threat model inclusion is good; many plans skip this entirely.
- Human verification checkpoint is appropriate for UX-facing work.

#### Concerns
- `HIGH` The plan does not define runtime reconfiguration behavior. Changing enable/ports/IP in the dialog likely requires orderly stop/rebind/restart logic.
- `HIGH` Polling the server state every 500ms may be too slow for an "active/error/off" indicator if bind/send failures happen during setup.
- `MEDIUM` `jlimit(1, 65535)` on load clamps silently, masking corrupted state.
- `MEDIUM` State persistence across DAW sessions is covered, but not across editor reopen, plugin duplication, preset save/load, or standalone app relaunch.
- `LOW` CMake touched in both waves may create avoidable merge friction.

#### Suggestions
- Define live reconfiguration semantics explicitly.
- Consider push-based UI updates for status changes, or at least faster transient refresh around config actions.
- Add explicit tests or verification steps for preset save/load, plugin reopen, duplicated instances, and standalone restart behavior.

#### Risk Assessment: MEDIUM

---

## Claude Review

### Plan 09-01: OSC Server Engine

#### Summary
This is a well-architected plan that correctly adapts the IEM Plugin Suite's bidirectional OSC pattern to JamWide's three-thread model. The key insight — dispatching OSC receive callbacks through `callAsync()` to preserve the SPSC single-producer invariant — is the most important architectural decision and it's handled correctly.

#### Strengths
- **Threading contract is correct**: `RealtimeCallback` listener on the network thread dispatches via `callAsync()` to the message thread. Solo commands go through `cmd_queue`. Timer runs on message thread. No locks acquired.
- **Echo suppression is well-designed**: Per-param `oscSourced` flag set on receive, cleared on next send tick. Simple, effective, and matches the IEM pattern.
- **Address map is clean**: HashMap-based O(1) lookup, enum-based dispatch, dual namespace with dB variants.
- **Bundle mode**: Grouping dirty values into a single OSC bundle per tick reduces UDP packet count.
- **Input validation**: All incoming values are clamped to entry ranges via `juce::jlimit`.

#### Concerns
- **HIGH — Solo feedback is one-directional**: `localSoloBitmask` is only updated when solo changes come FROM OSC. If a user clicks solo in the JamWide UI, the bitmask is not updated, and the control surface shows stale state. This breaks Success Criterion #2 for solo specifically.
- **MEDIUM — `callAsync` use-after-free on shutdown**: `oscMessageReceived` captures `this` in a `callAsync` lambda. If OSC messages are in-flight when `stop()` is called from the destructor, pending lambdas may dereference a destroyed `this`.
- **MEDIUM — Metro pan uses -1..1 range while local pan uses 0..1**: Inconsistent OSC ranges for pan parameters across metro and channel knobs.
- **LOW — Volume normalization semantics**: OSC volume 0-1 maps to APVTS 0-2 (linear), so 50% = unity gain. May surprise users expecting 100% = unity.
- **LOW — String-based pan detection in OscServer**: `getCurrentValue` uses `entry.oscAddress.contains("/pan")` — fragile string matching.

#### Suggestions
- Add a `isPanParam` flag to `OscParamEntry` to avoid string matching in hot-path code
- Document the "50% = unity" volume mapping convention
- Consider adding a brief sleep in the destructor path to flush pending callAsync callbacks

#### Risk Assessment: LOW-MEDIUM

### Plan 09-02: OSC UI + Persistence

#### Summary
A well-structured plan that wires the Plan 01 engine into the plugin lifecycle. The Voicemeeter-themed config dialog, 3-state status dot, and state version 2 migration are all cleanly specified.

#### Strengths
- **Correct ownership**: OscServer lives on the processor and survives editor destruction.
- **Clean destruction order**: oscServer -> runThread -> client.
- **State version 2 migration is backward-compatible**.
- **UI-SPEC compliance**: Precise color tokens, dimensions, and copywriting.
- **Human verification checkpoint** catches integration issues.

#### Concerns
- **MEDIUM — Config persistence coupling is unclear**: The mechanism for OscConfigDialog to update processor config members is underspecified (getProcessor vs direct reference).
- **MEDIUM — Enabling OSC on state restore can fail silently**: User sees grey dot instead of red, may not understand why OSC isn't running.
- **LOW — CallOutBox with nullptr parent**: On some DAWs (Logic), unparented CallOutBoxes can appear behind the plugin window.
- **LOW — Tab order and accessibility**: ToggleButton may not receive focus depending on LookAndFeel.

#### Suggestions
- Firmly specify the config persistence wiring (recommend `OscServer::getProcessor()`)
- On failed state restore, set error state (red dot) rather than silently disabling (grey dot)
- During human verification, specifically test CallOutBox positioning in DAW-hosted modes

#### Risk Assessment: LOW

---

## Consensus Summary

### Agreed Strengths
- Threading model is correct: callAsync dispatch, SPSC invariant preserved, no locks in hot path (both reviewers)
- Echo suppression pattern is well-designed and matches IEM reference (both reviewers)
- Processor-owned OscServer with clean destruction order is the right lifecycle choice (both reviewers)
- State version 2 with backward-compatible defaults is correct migration approach (both reviewers)
- Human verification checkpoint is valuable for UX-facing work (both reviewers)

### Agreed Concerns
- **callAsync flooding / coalescing** — Codex rates HIGH (no coalescing for bursty traffic), Claude identifies a related MEDIUM UAF risk. Both agree the inbound dispatch path needs attention.
- **Echo suppression edge cases** — Codex rates HIGH (may fail under rapid alternation), Claude rates it well-designed but notes the one-tick skip could miss legitimate updates. The implementation is sound but warrants testing under stress.
- **Runtime reconfiguration semantics** — Codex rates HIGH (not defined), Claude notes config persistence coupling is MEDIUM unclear. Both want explicit stop/rebind/restart semantics.
- **Status indicator update latency** — Codex rates HIGH (500ms too slow for errors), Claude rates LOW (acceptable for steady-state). Disagreement on severity but both note it.

### Divergent Views
- **Overall 09-01 risk**: Codex says MEDIUM-HIGH, Claude says LOW-MEDIUM. Codex focuses on theoretical threading risks; Claude validates the implementation against IEM pattern and finds it sound.
- **Solo feedback gap**: Only Claude identifies the one-directional solo bitmask issue (HIGH). Codex did not catch this specific functional gap.
- **VU meter traffic**: Codex flags always-on VU as MEDIUM concern; Claude does not raise it (D-16 explicitly makes VU always-dirty).
- **Pan range inconsistency**: Only Claude catches the metro pan -1..1 vs local pan 0..1 discrepancy.
