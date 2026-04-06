---
phase: 11
reviewers: [codex]
reviewed_at: 2026-04-06T21:00:00Z
plans_reviewed: [11-01-PLAN.md, 11-02-PLAN.md, 11-03-PLAN.md]
---

# Cross-AI Plan Review -- Phase 11

## Codex Review

### Plan 11-01: VideoCompanion Core

#### Summary
This plan covers the core backend responsibilities cleanly and mostly aligns with the phase goal. The separation is good: a single VideoCompanion owns localhost WebSocket serving, room derivation, participant serialization, and browser launch. The main risk is not the happy path but lifecycle correctness: connect/disconnect races, stale roster state, browser launch failures, and ambiguity in how usernames and stream identities stay stable across roster updates.

#### Strengths
- Clear ownership boundary: one class handles video companion orchestration.
- Good dependency choice: IXWebSocket is appropriate for localhost WebSocket serving.
- Sensible avoidance of new crypto deps by reusing WDL_SHA1.
- Explicit loopback binding to 127.0.0.1 is the right security default.
- JSON protocol is small and easy for the web client to consume.
- Audio suppression is encoded in config, which keeps the browser client dumb.

#### Concerns
- **HIGH**: Room derivation uses server address + password, but VID-02 says auto-generated from the NINJAM server address. Including password changes semantics and may create different rooms for users on the same server if passwords differ.
- **HIGH**: No explicit plan for thread safety between NINJAM roster updates and WebSocket client access. If roster changes arrive from the run thread, the class needs a clear locking or message-thread handoff strategy.
- **HIGH**: The contract includes `push`, but the plan does not define how `push` is derived, validated, or kept stable. That field appears critical to VDO.Ninja room behavior.
- **MEDIUM**: Fixed port 7170 is simple, but the plan does not say what happens if the port is already occupied.
- **MEDIUM**: Username collision handling is underspecified. If suffixes depend on roster order, names may flap across updates and break UI continuity.
- **MEDIUM**: Browser launch is listed, but there is no explicit failure path if the default browser cannot be opened.
- **LOW**: Public-server salt behavior is mentioned, but the distinction between "public" and non-public server classification is not defined here.

#### Suggestions
- Reconcile room-ID derivation with VID-02. If password is intentional, document why and confirm it does not fragment a shared session into multiple video rooms.
- Define a stable streamId generation rule in this plan, not only username sanitization.
- Add explicit lifecycle/error cases: port bind failure, browser launch failure, client disconnect/reconnect, server start/stop idempotency.
- Specify thread ownership and synchronization for roster mutation versus WebSocket broadcast.
- Make collision resolution deterministic from stable user identity rather than roster position.
- Define how `push` is constructed and whether it is safe to expose to the browser page.

#### Risk Assessment
**MEDIUM-HIGH**. The architecture is sound, but there are unresolved contract and lifecycle details that could cause cross-plan integration issues or incorrect room partitioning.

---

### Plan 11-02: Web Companion Page

#### Summary
This plan is appropriately scoped and matches the phase UX goal well: connect to the local plugin, render a browser-based participant grid, and keep the browser logic thin. The main weakness is that it assumes the protocol and iframe parameters are sufficient without specifying behavior for partial/malformed state, participant churn, or failure to connect on first load.

#### Strengths
- Good parallelization: this can proceed independently once the JSON contract is stable.
- Small front-end surface area; Vite/TypeScript is reasonable for a docs-hosted page.
- Manual reconnect avoids reconnection loops and keeps behavior predictable.
- Status footer is useful for diagnosing plugin/websocket availability.
- Grid-based browser page is directly aligned with VID-04.
- Audio suppression is enforced at iframe construction, which is the right place.

#### Concerns
- **HIGH**: The plan assumes the config/roster schema is sufficient, but it does not define validation or fallback if fields are missing, malformed, or arrive out of order.
- **HIGH**: No handling is described for first-load behavior when the page opens before the plugin WebSocket server is ready.
- **MEDIUM**: "Shows session participants in video grid layout" depends on stable participant identity, but the plan does not say how it maps roster entries to VDO.Ninja embeds if usernames collide or change.
- **MEDIUM**: No explicit empty-state behavior when connected but roster is empty or only contains self.
- **MEDIUM**: &noaudio and &cleanoutput are specified, but if the iframe URL also needs self-view or viewer/push parameters, this plan should define the exact URL builder to avoid drift from Plan 11-01.
- **LOW**: "Dark theme matching plugin visual language" is subjective and can drift into unnecessary design work.

#### Suggestions
- Freeze the JSON contract before implementation and treat it as versioned input.
- Add defensive handling for: invalid JSON, unknown message type, roster arriving before config, missing push or room, disconnected-at-load state.
- Define exact iframe URL construction in the plan, including how room, push, name, and noaudio are applied.
- Add a clear empty state: "Waiting for participants" or equivalent.
- Use stable keys derived from streamId rather than display name when rendering participant tiles.

#### Risk Assessment
**MEDIUM**. The UI scope is controlled, but the plan is under-specified around protocol resilience and participant identity stability.

---

### Plan 11-03: Video Button + Privacy Modal

#### Summary
This plan is the most user-visible and the most integration-heavy. It addresses the missing product behavior around consent, browser compatibility, activation state, and wiring into the processor/editor lifecycle. It is directionally correct, but it is also where most coordination risk sits: platform detection, modal UX rules, active-state semantics, and run-thread-to-UI/backend integration all need sharper definitions to avoid rework.

#### Strengths
- Correctly placed after core backend work.
- Covers the privacy requirement explicitly.
- Reuse of existing dialog styling reduces UI risk and scope.
- Button state requirements are concrete and testable.
- Auto-deactivation on NINJAM disconnect is the right lifecycle behavior.
- Includes run-thread roster forwarding, which is necessary for live updates.

#### Concerns
- **HIGH**: Browser detection is likely brittle. "Chromium-based" is not always reliably inferable from default-handler APIs or registry associations alone.
- **HIGH**: Re-clicking active Video re-opens without modal is fine UX, but the plan does not define whether the server must already be running, whether it relaunches a tab every click, or whether duplicate tabs/windows are prevented.
- **HIGH**: This plan depends on VideoCompanion.h, but no explicit interface contract is listed. That creates a coordination gap between Plans 11-01 and 11-03.
- **MEDIUM**: "Privacy modal shown every video launch session" conflicts slightly with "re-clicking active Video re-opens companion without modal." The activation/session model needs to be precisely defined.
- **MEDIUM**: Roster forwarding from the run thread can become a coupling hotspot if the run thread now owns video-specific transformation logic.
- **MEDIUM**: The file count is high for only three tasks, which suggests hidden integration complexity.
- **LOW**: Browser warning behavior is not fully specified. Warning only? Block launch? Allow continue?

#### Suggestions
- Define the VideoCompanion interface explicitly before implementation.
- Clarify user-flow semantics: first click, click after accepting, click while active, click after disconnect/reconnect.
- Treat browser detection as best-effort advisory, not authoritative gating.
- Ensure the privacy modal has explicit continue/cancel behavior.
- Keep roster transformation close to backend/core code rather than scattering across UI and run thread.

#### Risk Assessment
**HIGH**. This plan touches UI, platform APIs, backend lifecycle, and threading. It can succeed, but only if the interface and activation semantics are nailed down before coding.

---

## Consensus Summary

### Agreed Strengths
- Phase decomposition is well-structured: backend core and web page parallel, then UI integration
- Protocol is intentionally minimal and appropriate for localhost use
- Privacy and browser-compatibility addressed early rather than deferred
- IXWebSocket and WDL_SHA1 are solid dependency choices
- Audio suppression is correctly enforced at both plugin and iframe level

### Agreed Concerns
1. **Room ID derivation mismatch** — VID-02 says "from server address" but plan uses server address + password, potentially fragmenting sessions (HIGH)
2. **`push` and `streamId` undefined** — critical VDO.Ninja protocol fields with no generation/stability rules (HIGH)
3. **Thread safety for roster updates** — run thread to WebSocket broadcast needs explicit synchronization strategy (HIGH)
4. **Browser detection brittleness** — Chromium inference from platform APIs is unreliable (HIGH)
5. **Protocol resilience** — no validation, ordering guarantees, or error handling for WebSocket messages (HIGH)
6. **VideoCompanion interface not frozen** — Plan 03 depends on Plan 01's API but no explicit contract exists (HIGH)
7. **Port bind failure** — no fallback if 7170 is occupied (MEDIUM)
8. **First-load timing** — page opens before plugin WebSocket is ready (MEDIUM)
9. **Username collision instability** — suffix based on roster order may flap across updates (MEDIUM)

### Divergent Views
_(Single reviewer -- no divergence to report. Codex was the sole independent reviewer.)_
