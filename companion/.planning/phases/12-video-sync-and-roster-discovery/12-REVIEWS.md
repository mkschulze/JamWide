---
phase: 12
reviewers: [codex]
reviewed_at: 2026-04-07T21:45:00Z
plans_reviewed: [12-01-PLAN.md, 12-02-PLAN.md]
---

# Cross-AI Plan Review -- Phase 12

## Codex Review

### Plan 12-01: C++ Plugin Side

#### Summary
Plan 12-01 is focused and appropriately scoped for the plugin side. It covers the core mechanics needed for VID-08 and VID-09: deriving a session-bound room password, constructing the companion URL correctly, calculating interval-based delay, and pushing delay updates to connected WebSocket clients. The plan looks implementable in one wave and avoids unnecessary coupling to the companion UI. The main risks are around correctness of event wiring, edge-case handling for invalid tempo state, and whether truncating the SHA-256 output to 16 hex chars is an intentional security tradeoff or an accidental weakening.

#### Strengths
- Clear separation of responsibilities between plugin and companion page.
- Good alignment with the roadmap goal for synced buffering and room security.
- Caches delay for new WebSocket clients, which prevents stale or missing initial state.
- Uses hash fragment semantics so the derived password is not sent to the server.
- BPM/BPI-driven updates are constrained to configuration changes, not every beat, which avoids unnecessary traffic.
- Threat model exists and appears to have been considered up front.
- Zero file overlap with the TypeScript plan reduces execution risk for parallel work.

#### Concerns
- **HIGH**: Truncating SHA-256 to 16 hex chars materially reduces password entropy. That may still be acceptable for this use case, but the plan does not justify why truncation is safe or required.
- **MEDIUM**: The formula assumes valid `bpm > 0` and sensible `bpi > 0`. The plan does not mention guards for malformed or transitional state.
- **MEDIUM**: "Wire BPM/BPI change events" is underspecified. If the editor only updates while visible, delay sync may fail for hidden/minimized UI or standalone/plugin state transitions.
- **MEDIUM**: Cached delay for new WS clients is good, but the plan does not state whether the current room URL/password state is also resent or rebuilt consistently on reconnect.
- **LOW**: No mention of rounding strategy for `delay_ms`. Small inconsistencies between float calculation and integer transport could cause test fragility or off-by-one behavior.
- **LOW**: If no NINJAM password is set, plan says no hash is applied, but it does not mention whether stale password fragments are cleared when switching from private to public sessions.

#### Suggestions
- Keep the full SHA-256 hex unless VDO.Ninja imposes a practical limit; if truncation is retained, document the rationale explicitly.
- Add explicit guards for invalid `bpm`, `bpi`, NaN, and zero values, with a no-op or fallback behavior.
- Confirm the delay broadcast source is tied to session state changes, not only visible editor UI events.
- Define reconnect behavior clearly: what state is resent to a newly connected companion page and in what order.
- Specify integer conversion and rounding behavior for `delay_ms`.
- Add one explicit case for clearing/removing derived password state when session security changes.

#### Risk Assessment
**MEDIUM**. The plan is well-scoped and likely to work, but correctness depends on a few unstated details: event source reliability, invalid tempo handling, and the password truncation decision. Those are manageable, but they are real implementation risks.

---

### Plan 12-02: Companion Page TypeScript

#### Summary
Plan 12-02 is ambitious but generally strong. It covers all companion-side requirements in one place: message handling, URL generation, profile persistence, buffer delay relay, and roster display, plus test infrastructure. The design choices are mostly grounded in the research findings, especially around VDO.Ninja quirks. The main concern is that the plan bundles several behaviors into only two tasks, which increases coordination risk and makes it easier to miss state-transition edge cases such as profile changes, reconnects, and dynamic roster churn.

#### Strengths
- Directly addresses all four phase requirements on the web side.
- Incorporates critical research findings correctly: inverted quality scale, chunked mode, and proper `setBufferDelay` postMessage shape.
- Includes automated tests instead of treating the companion page as a manual-only surface.
- Uses `textContent` for roster names, which is the right XSS mitigation.
- Uses localStorage persistence for bandwidth profile, matching the product decision.
- Accepts iframe reload for profile changes, which is a pragmatic constraint instead of overengineering hot reconfiguration.
- Roster strip is a good compromise given cross-origin iframe limitations.

#### Concerns
- **HIGH**: The plan does not clearly describe how roster discovery updates over time. If NINJAM roster changes after initial page load, labels may become stale unless there is explicit update/removal logic.
- **HIGH**: Rebuilding/reloading the iframe on profile change can disrupt current state. The plan does not say how existing room/password/buffer-delay state is preserved immediately after reload.
- **MEDIUM**: The two-task structure is dense. Vitest setup, URL builder expansion, relay logic, roster UI, HTML/CSS, and app wiring in one wave may conceal missing integration edges.
- **MEDIUM**: The plan does not say what happens if `postMessage` fires before the iframe is ready to receive `setBufferDelay`.
- **MEDIUM**: localStorage persistence needs defensive handling for invalid or obsolete stored values.
- **MEDIUM**: The plan mentions room hash handling, but research says `#password=` is required for compatibility. If naming stays "hash" in code and tests, that semantic mismatch can create bugs.
- **LOW**: Roster matching based on sanitization convention from Phase 11 can be collision-prone if two usernames sanitize to the same value.
- **LOW**: CSS/HTML/UI changes and test setup in the same plan may be slightly broader than necessary for one phase, though still reasonable.

#### Suggestions
- Explicitly define lifecycle behavior for roster updates: add, remove, rename, and unmatched-stream cases.
- Specify iframe readiness handling for buffer-delay delivery, such as replaying the latest delay after iframe load/reload.
- After profile-triggered reload, resend or reconstruct the full current state in a deterministic order: room/password, quality params, then latest buffer delay.
- Validate stored profile values before use and fall back to `Balanced` on parse failure.
- Rename implementation concepts consistently around `password` rather than `hash` to match the actual VDO.Ninja fragment contract.
- Add at least one test for dynamic reconnect/reload behavior and one for stale roster cleanup.
- Consider splitting "test infra + core state utilities" from "DOM wiring + styling" if execution starts slipping.

#### Risk Assessment
**MEDIUM**. The plan is directionally correct and should achieve the phase goals, but it is carrying more integration complexity than the C++ side. The main risks are state synchronization across iframe reloads and live roster updates rather than raw implementation difficulty.

---

### Cross-Plan Review

#### Summary
Together, the two plans cover the phase goal well and the parallelization strategy is sound. Responsibilities are separated cleanly, and there is no obvious file overlap or dependency cycle at the code ownership level. The biggest shared risk is not build conflict but protocol/state contract drift between the plugin and companion implementations: message schema, timing of state delivery, and terminology around password/hash handling need to be nailed down precisely or the two independently implemented halves can both be "correct" locally and still fail in integration.

#### Strengths
- Parallel execution is sensible because the file sets do not overlap.
- Responsibilities are split along a clean boundary: plugin owns session-derived state, web layer owns VDO.Ninja/UI behavior.
- Research findings are reflected in both plans instead of being left as implicit knowledge.
- Phase goals map cleanly across the two plans with little obvious scope creep.
- Security considerations were considered in both plans.

#### Concerns
- **HIGH**: The shared contract is underspecified in the review text. There is one explicit new WS message for buffer delay, but not a clear end-to-end contract for roster data shape, reconnect semantics, or reload recovery.
- **MEDIUM**: Password terminology is inconsistent across the materials: roadmap says derived password hash, research says use `#password=`, plan text still refers to "hash fragment." That inconsistency can leak into code/tests.
- **MEDIUM**: There is no explicit integration test plan across C++ and companion boundaries.
- **MEDIUM**: Both plans assume Phase 11 username sanitization is stable and sufficient; if that assumption is wrong, VID-10 degrades noticeably.
- **LOW**: The plans do not mention observability/debugging aids for cross-process issues, which can slow validation.

#### Suggestions
- Write a short shared interface contract before implementation starts:
  - WebSocket message schemas
  - field names and units
  - reconnect behavior
  - iframe reload replay behavior
  - roster payload semantics
- Standardize terminology now: "derived room password" should be the canonical term, even if internally derived from SHA-256.
- Add one manual integration checklist or automated smoke test covering:
  - session join
  - companion connect
  - profile change reload
  - tempo change delay update
  - public vs private room behavior
- Decide explicitly whether username sanitization collisions are ignored, prevented, or surfaced.

#### Risk Assessment
**MEDIUM**. The plans are broadly complete and should deliver the phase if implemented carefully. The remaining risk is mostly integration precision, not architecture. If the shared contract is tightened before coding, risk drops meaningfully.

---

## Consensus Summary

### Agreed Strengths
- Clean separation between C++ plugin and TypeScript companion with zero file overlap
- Research findings (inverted quality scale, chunked mode, #password= format) correctly incorporated
- Security considerations addressed upfront with threat models in both plans
- Pragmatic design choices (iframe reload vs hot reconfiguration, roster strip vs per-tile labels)

### Agreed Concerns
- SHA-256 truncation to 16 hex chars needs explicit justification or should use full hash
- State preservation across iframe reloads (buffer delay, room/password) needs clearer specification
- Shared WebSocket protocol contract between C++ and TypeScript should be documented explicitly
- Roster lifecycle (dynamic updates, stale removal) underspecified
- Terminology inconsistency (hash/password/hashFragment) risks leaking into code

### Divergent Views
No divergent views (single reviewer).
