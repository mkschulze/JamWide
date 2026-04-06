---
phase: 10
reviewers: [codex]
reviewed_at: 2026-04-06T12:00:00Z
plans_reviewed: [10-01-PLAN.md, 10-02-PLAN.md]
---

# Cross-AI Plan Review -- Phase 10

## Codex Review

### Plan 10-01: Remote User OSC Send/Receive

#### Summary
This plan is directionally solid and maps well to the phase goal: it covers outbound state publishing, inbound remote-user control, roster name updates, VU export, and session connect/disconnect triggers in one coherent extension of Phase 9. The biggest risks are not architectural but behavioral: slot churn under the positional index model, sparse sub-channel indexing, unclear solo semantics at the group-bus level, and the lack of explicit validation/error-handling details for malformed OSC input and connection strings. It should achieve the phase goal if those edge cases are specified and tested up front.

#### Strengths
- Extends the existing Phase 9 patterns instead of inventing a parallel OSC path.
- Correctly recognizes that dynamic `/remote/{idx}/...` parsing cannot live in the static map.
- Includes both send and receive paths, which is necessary for a usable control-surface workflow.
- Accounts for roster-name broadcasting as string OSC, which is required for TouchOSC usability.
- Separates roster dirty behavior from VU always-dirty behavior, which is the right performance tradeoff.
- Calls out the 0-based NJClient vs 1-based OSC mismatch explicitly.
- Preserves the existing command-queue threading model rather than mutating NJClient directly from the OSC callback.
- Includes sub-channel control, which is required by the locked decisions.

#### Concerns
- **HIGH**: Group-bus solo semantics are underspecified and potentially wrong. "Toggle solo on all sub-channels" may not match existing mixer behavior, may create partial-state drift, and may behave badly when channels are added/removed or individually soloed outside OSC.
- **HIGH**: The plan does not define how invalid or stale slot indices are handled when roster membership shifts. Under D-01, slot reassignment is expected, but incoming control during churn could hit the wrong user unless resolution is done strictly against current cached roster at execution time.
- **HIGH**: Sub-channel addressing uses `/ch/{n}`, but research says `channel_index` is sparse. The plan does not clearly state whether `{n}` is sparse NINJAM bit index or a human-visible sequential slot. If docs/template assume sequential while runtime expects sparse, this will fail usability.
- **MEDIUM**: String handling is added only for connect trigger, but roster name send also relies on string OSC support. The receive/send API surface for strings should be reviewed holistically, not as a one-off.
- **MEDIUM**: Connect trigger validation is thin. `"host:port"` parsing needs failure behavior for missing port, invalid port, IPv6 literals, whitespace, and duplicate connect attempts.
- **MEDIUM**: `lastSentRemoteValues` plus per-channel arrays could become brittle if the cached roster mutates frequently. The plan does not say how cached state is reset when a user leaves and another user occupies the same slot.
- **MEDIUM**: Echo suppression via per-slot flags may be too coarse if slot identity changes. A reused slot could inherit suppression state from the previous occupant.
- **MEDIUM**: Extending `RemoteUserInfo/RemoteChannelInfo` in `njclient.h` may be the wrong layer if these are transport/cache models rather than mixer-state models. That can leak UI/control concerns into protocol state.
- **LOW**: `docs/osc.md` is listed, but the plan does not mention updating docs for error cases, value ranges, trigger payloads, or slot-churn semantics.
- **LOW**: Verification is too generic. "full build + ctest" is weak unless there are actual tests covering address parsing, roster hash changes, and command generation.

#### Suggestions
- Specify authoritative solo behavior before implementation. If JamWide has no true remote-user group solo primitive, document the derived behavior and define reconciliation rules.
- Resolve remote slot indices against the live roster at command execution time, not only at OSC parse time, to reduce stale-target errors.
- Make `/remote/{idx}/ch/{n}` semantics explicit in both code and docs:
  - If `{n}` is sparse NINJAM channel index, say so clearly.
  - If usability matters more, introduce a sequential channel slot model and maintain a mapping.
- Add explicit validation rules for all inbound remote OSC:
  - reject out-of-range slot numbers
  - reject invalid channel selectors
  - clamp numeric values
  - ignore malformed addresses without side effects
  - log parse failures at a throttled debug level
- Reset per-slot cached send-state and echo-suppression state whenever slot occupancy changes.
- Add targeted tests for:
  - `/remote/{idx}` address parsing
  - 1-based to 0-based conversion
  - sparse sub-channel lookup
  - roster change detection/hash behavior
  - connect/disconnect trigger dispatch
  - malformed string/numeric payloads
- Consider keeping derived OSC send-state in `OscServer` rather than expanding `njclient` structs unless those fields are already part of authoritative remote mix state.
- Define disconnect trigger payload semantics explicitly: empty message, int `1`, or any message at address. TouchOSC interoperability improves when this is concrete.

#### Risk Assessment
**MEDIUM** -- The design is mostly aligned with the phase requirements and builds on proven infrastructure, but several behavioral details are still ambiguous in places where the user experience will be fragile: slot churn, sparse channel indexing, and group-solo semantics.

---

### Plan 10-02: TouchOSC Template

#### Summary
This plan covers the required artifact and keeps scope bounded to a shippable starter template, which is good. The main weakness is the choice to generate `.tosc` programmatically via Python instead of authoring in TouchOSC Editor as recommended by research. That raises maintenance and validity risk without obvious user benefit. The layout itself appears sufficient for the phase goal, but the zero-config claim is slightly overstated because TouchOSC still needs host/IP setup, and the dependency on exact OSC docs from 10-01 means this plan is only safe if address semantics are frozen first.

#### Strengths
- Keeps the template focused on immediate usability rather than trying to expose every OSC endpoint.
- Correctly depends on 10-01, since address details must be stable first.
- Ships a real artifact in the expected location, matching the requirement.
- Limits remote slots to 8 in the template while allowing 16 in the server, which is a reasonable UX compromise.
- Includes human verification with TouchOSC, which matters more than pure file-generation success.
- Avoids embedding credentials or any sensitive data.

#### Concerns
- **HIGH**: Programmatic `.tosc` generation is likely over-engineered for a mostly static asset. If the XML/schema is imperfect or TouchOSC changes export details, the generated file may be technically valid zlib XML but still not import correctly or behave as intended.
- **HIGH**: The plan does not define how template addresses will stay synchronized with `docs/osc.md` and implementation. "match docs exactly" is a requirement, but there is no source-of-truth strategy.
- **MEDIUM**: Zero-config is overstated. Users still need OSC host/port configuration in TouchOSC unless the platform supports discovery/persistence.
- **MEDIUM**: The template omits remote-user VU even though VU is part of the remote-user OSC feature set. Acceptable for density reasons, but should be intentionally documented.
- **MEDIUM**: Session connect uses a string `"host:port"` payload, but the template plan does not explain how TouchOSC will supply that string. A button alone is insufficient unless paired with a text field.
- **MEDIUM**: No mention of disconnect trigger payload format. TouchOSC control type must match runtime expectations.
- **LOW**: Verification is partly manual, but there is no import test checklist.

#### Suggestions
- Prefer authoring `assets/JamWide.tosc` directly in TouchOSC Editor unless there is a strong need for generation. For a mostly fixed layout, a checked-in binary asset is lower risk.
- If generation is kept, make the script a build-time utility for reproducing the asset, not the primary editable source of truth.
- Define a single source of truth for OSC addresses (generate docs/template constants from code, or keep a manifest that both consume).
- Add a concrete UX for connect: text field for `"host:port"`, button that sends the string, documented examples.
- Document exactly what the user must configure manually in TouchOSC, since "zero config beyond host IP" is not literally zero config.
- Add a minimum acceptance checklist for real TouchOSC import/behavior.
- Decide explicitly whether remote VU is omitted by design and document in docs.

#### Risk Assessment
**MEDIUM** -- The functional scope is reasonable, but the implementation method adds unnecessary fragility. Using programmatic generation instead of the editor is the main risk driver.

---

## Consensus Summary

### Agreed Strengths
- Phase decomposition is well-structured: 10-01 handles protocol/control, 10-02 packages it into a usable surface
- Extends proven Phase 9 patterns rather than inventing new architecture
- Correctly identifies key pitfalls (index mismatch, sparse channels, string handling)
- Threading model preserved (cmd_queue SPSC pattern)
- Roster dirty-tracking separated from always-dirty VU meters

### Agreed Concerns
1. **Group-bus solo semantics** need explicit definition before implementation (HIGH)
2. **Sub-channel `/ch/{n}` semantics** must be frozen: sequential vs sparse NINJAM bit index (HIGH)
3. **Programmatic .tosc generation** adds fragility vs editor-authored approach (HIGH)
4. **Slot churn handling** under positional index model needs concrete error behavior (HIGH)
5. **Connect trigger UX** needs text field, not just button, for host:port entry (MEDIUM)
6. **Echo suppression and cached send-state** must reset on slot occupancy changes (MEDIUM)

### Divergent Views
_(Single reviewer -- no divergence to report. Codex was the sole independent reviewer.)_
