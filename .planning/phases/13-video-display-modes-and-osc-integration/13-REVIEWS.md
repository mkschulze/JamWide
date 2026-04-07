---
phase: 13
reviewers: [codex]
reviewed_at: 2026-04-07
plans_reviewed: [13-01-PLAN.md, 13-02-PLAN.md]
---

# Cross-AI Plan Review — Phase 13

## Codex Review

### Plan 01 Review

#### Summary
Plan 01 is generally well-scoped and aligned with `VID-07`. It decomposes the browser-side work into sensible pieces, includes meaningful tests, and follows the decisions in Phase 13 context closely. The main weakness is that some important lifecycle and trust-boundary details are under-specified: cross-window messaging is only partially validated, window cleanup is not fully addressed, and there is some coupling between UI changes and runtime behavior that may create subtle regressions.

#### Strengths
- Clear scope boundary: it stays focused on `VID-07` and does not drift into C++ or OSC work.
- Good dependency ordering: types and URL builder first, then page entrypoint/build config, then main-page wiring.
- Strong traceability to phase decisions such as `D-05`, `D-07`, `D-10`, and `D-13`.
- Includes backward-compatibility tests for `buildVdoNinjaUrl`, which reduces regression risk on the main companion flow.
- Explicitly handles popup-blocked behavior, which is a realistic browser edge case.
- Uses existing architecture rather than introducing new dependencies or abstractions.

#### Concerns
- **HIGH**: The plan does not fully address message-origin validation for `postMessage`. The threat model mentions validation, but the implementation only checks shape, not `event.origin` or `event.source`.
- **HIGH**: Window lifecycle cleanup is incomplete. `Map<string, Window>` is updated on use and roster relay, but there is no explicit cleanup when a user manually closes a popout until some later event happens.
- **MEDIUM**: `renderRosterStrip` changing from `<span>` to `<button>` could affect existing styling/layout/accessibility behavior if CSS and reset styles are not audited.
- **MEDIUM**: `window.open('popout.html?...')` assumes the relative path works correctly under the built `/video/` base and in local dev. That should be verified explicitly.
- **MEDIUM**: The plan sends full roster payloads to every popout on every roster update. That is acceptable at this scale, but it is still an untested behavior path.
- **MEDIUM**: Tests cover URL building and DOM shape, but not the core popout behaviors: focus-existing, popup-blocked banner timing, roster relay, disconnect overlay toggle, and deactivate-close behavior.
- **LOW**: The popout iframe allows `camera;microphone;display-capture` even though the stated goal is view-only. That permission set may be broader than needed.

#### Suggestions
- Add a concrete requirement to validate `event.source === window.opener` and, if stable in this deployment model, validate same-origin before processing `message` events.
- Add cleanup logic for closed popouts, either via polling, `beforeunload`, or a lightweight periodic sweep.
- Add tests for focus instead of duplicate open, popup-blocked banner, roster relay, disconnect overlay, deactivate-close behavior.
- Verify the production path resolution for `popout.html` under Vite `base: '/video/'`.
- Reconsider iframe permissions in the popout page and remove capabilities not required for view-only playback.

#### Risk Assessment
**MEDIUM**. The plan is directionally correct and likely to achieve `VID-07`, but there are enough lifecycle and trust-boundary gaps that a first-pass implementation could work in happy-path demos while still being brittle in production.

---

### Plan 02 Review

#### Summary
Plan 02 has the right broad responsibilities for `VID-11`, but it carries materially more implementation risk than Plan 01. The main problem is that several core assumptions are not grounded in the stated interfaces: OSC activation depends on processor/session fields that may not exist, privacy-gate state is described but not modeled, and the plan may be forcing behavior into `VideoCompanion` that does not clearly belong there yet.

#### Strengths
- Good separation of browser-side and C++/template work by putting this in wave 2 after Plan 01.
- Reuses established patterns: OSC prefix dispatch, dirty-state feedback, localhost WebSocket broadcast.
- `requestPopout()` and `getStreamIdForUserIndex()` are reasonable extension points for bridging OSC to the companion.
- Deactivate-broadcast-before-stop is the right lifecycle order for closing popouts cleanly.
- Documentation update is included, which helps keep OSC behavior discoverable.
- TouchOSC template update matches the existing index-based control model from earlier phases.

#### Concerns
- **HIGH**: The `/JamWide/video/active 1.0` flow relies on `processor.lastServer`, `lastUsername`, and `lastPassword`, but those fields are not shown in the provided interfaces. This is a likely compile or design failure point.
- **HIGH**: The "privacy previously accepted this session" rule is not backed by any explicit state or API in the plan. The behavior is documented, but not implementable from the described interfaces.
- **HIGH**: Requirement coverage is incomplete or ambiguous. The roadmap success criteria says users can "switch display modes" via OSC, but the plan only implements `active` and `popout`. If "mode switch" is intentionally reduced to "grid vs popout by action," that should be stated explicitly.
- **MEDIUM**: `cachedRoster_` is updated in `broadcastRoster()` and read from OSC handlers, but thread ownership is only implied. If roster updates are not guaranteed on the message thread, this introduces race risk.
- **MEDIUM**: `requestPopout()` broadcasts to all connected companion clients. If multiple companion pages are open, all of them may react and open popouts.
- **MEDIUM**: The popout address bounds are inconsistent. The docs propose `{1-8}`, but the handler accepts `1-16`.
- **MEDIUM**: Build verification hardcodes `JamWide_VST3`; the plan itself admits the target may differ.
- **MEDIUM**: TouchOSC template editing via decompression/recompression is practical, but fragile. No validation step beyond string assertions.
- **LOW**: `docs/osc.md` says popout is receive-only, but if future bidirectional support is desired for button LEDs, this design may paint the project into a corner.

#### Suggestions
- Resolve the activation contract before execution: add explicit processor APIs/state for relaunch, or narrow to "relaunch only if already configured and previously launched."
- Add an explicit note on whether `VID-11` is satisfied without a dedicated "mode switch" OSC address.
- Align `/video/popout/{idx}` bounds across code, docs, and TouchOSC template.
- Clarify thread ownership of `cachedRoster_`.
- Define behavior when more than one companion page is connected.
- Strengthen verification beyond compile checks.

#### Risk Assessment
**HIGH**. The plan is conceptually sound, but it depends on hidden processor state and an undefined privacy-acceptance mechanism.

---

## Consensus Summary

### Agreed Strengths
- Both plans have clear scope boundaries and strong decision traceability (D-01 through D-20)
- Good wave ordering: browser-side first (Wave 1), then C++/OSC (Wave 2)
- Reuses existing architecture without unnecessary dependencies

### Agreed Concerns
- **Plan 02 activation prerequisites**: OSC video/active relies on processor state and privacy-acceptance mechanisms not defined in the plan interfaces (HIGH)
- **Window lifecycle cleanup**: No explicit cleanup for manually closed popout windows (HIGH)
- **postMessage origin validation**: Security threat model mentions it but implementation doesn't check `event.origin` (HIGH)
- **Thread safety of cachedRoster_**: Updated in broadcastRoster and read from OSC handler — thread ownership unclear (MEDIUM)
- **Test coverage gaps**: Core popout behaviors (focus-existing, deactivate-close, roster relay) lack automated tests (MEDIUM)

### Divergent Views
- No divergent views (single reviewer)
