---
phase: 4
reviewers: [claude, codex]
reviewed_at: 2026-04-04
plans_reviewed: [04-01-PLAN.md, 04-02-PLAN.md, 04-03-PLAN.md, 04-04-PLAN.md]
---

# Cross-AI Plan Review — Phase 4

## Claude Review

### Plan 04-01: Infrastructure
- **Risk:** MEDIUM
- **HIGH:** License callback mutex mismatch — `ScopedLock` in run loop makes it impossible to release lock from within `client->Run()` callback. Needs run-loop restructuring.
- **HIGH:** `HasUserInfoChanged()` is destructive (clears flag on read). Must only be called once per loop iteration, under client lock.
- **MEDIUM:** Missing `GetRemoteUsersSnapshot()` API — exists on NJClient but plans manually enumerate users instead.
- **MEDIUM:** `kSurfaceScrim` ARGB byte ordering may be wrong (JUCE expects AARRGGBB format).
- **MEDIUM:** Missing shutdown handling for license callback wait.

### Plan 04-02: ConnectionBar + ChatPanel
- **Risk:** MEDIUM
- **HIGH:** `applyScale()` double-scaling — using both `setTransform(scale)` and `setSize(scaled)` creates incorrect behavior. Should use transform only, keeping setSize at base dimensions.
- **MEDIUM:** `static int lastStatus` persists across editor reconstructions — should be a member variable.
- **MEDIUM:** Chat word-wrapping height calculation is non-trivial in JUCE.
- **LOW:** `Viewport` has no `onScrollCallback` — need to subclass with `visibleAreaChanged()` or poll in timer.

### Plan 04-03: VuMeter + ChannelStrip + BeatBar
- **Risk:** LOW
- **MEDIUM:** Phase 4 strips are shells without controls — Phase 5 refactoring risk.
- **MEDIUM:** Remote VU levels hardcoded to 0 — dead meters give broken impression.
- **MEDIUM:** `ChannelStrip::configure()` does too much — consider splitting setup vs updates.
- **LOW:** Per-VuMeter timers wasteful — use single timer in ChannelStripArea.

### Plan 04-04: ServerBrowserOverlay + LicenseDialog + Assembly
- **Risk:** MEDIUM
- **HIGH:** Manual user enumeration instead of `GetRemoteUsersSnapshot()`.
- **HIGH:** `toggleChatSidebar()` calls `setSize()` which is problematic for plugin hosts.
- **MEDIUM:** `cachedUsers` on Processor without explicit synchronization documentation.
- **MEDIUM:** Timing Guide removal may break CLAP build if references exist elsewhere.

### Overall: MEDIUM risk. Plans are well-structured but license callback locking, applyScale double-scaling, and GetRemoteUsersSnapshot need fixing.

---

## Codex Review

### Plan 04-01: Infrastructure
- **Risk:** MEDIUM
- **HIGH:** `ChatMessageModel` mutex not actually synchronized — threading contract not stated.
- **HIGH:** `chat_queue`/`evt_queue` bifurcation may create ordering bugs between topic/status events and chat messages.
- **MEDIUM:** `cachedServerList` and persistent UI state introduced without synchronization model.
- **MEDIUM:** No defined behavior for pending license when editor is closed/no UI active.
- **MEDIUM:** `target_include_directories(PRIVATE ${CMAKE_SOURCE_DIR})` too broad.

### Plan 04-02: ConnectionBar + ChatPanel
- **Risk:** MEDIUM-HIGH
- **HIGH:** Scale implementation fragile — `setTransform` + `setSize` together creates incorrect behavior in plugin hosts.
- **HIGH:** `pollStatus()` reads `client->cached_status` directly, blurring thread-safety model.
- **HIGH:** `userCount` introduced in plan 02 but requires plan 01/NinjamRunThread updates — hidden cross-plan dependency.
- **MEDIUM:** Codec default appears to be FLAC but project says default Vorbis.
- **MEDIUM:** Auto-accepting license as placeholder is dangerous.

### Plan 04-03: VuMeter + ChannelStrip + BeatBar
- **Risk:** MEDIUM-HIGH
- **HIGH:** Partially overlaps Phase 5/6 concerns — routing selectors, subscribe/transmit toggles create behavioral surface area before mixer model finalized.
- **HIGH:** `refreshFromUsers()` expects `cachedUsers` but that doesn't exist until plan 04-04 — dependency mismatch.
- **MEDIUM:** Per-VuMeter 30Hz timers expensive with many strips.
- **MEDIUM:** Remote VU stubbed to zero — central mixer area looks broken.

### Plan 04-04: ServerBrowserOverlay + LicenseDialog + Assembly
- **Risk:** HIGH
- **HIGH:** `cachedUsers` introduced here but required by 04-03 — late architectural addition.
- **HIGH:** Building `cachedUsers` from NJClient underspecified and thread-risky.
- **HIGH:** Double-click auto-connect uses empty password — wrong for passworded servers.
- **MEDIUM:** License dialog should not allow outside-click dismissal while run thread blocked.
- **MEDIUM:** Chat sidebar toggling via `setSize` problematic in plugin hosts.

### Overall: MEDIUM-HIGH risk. Dependency ordering issues, threading contracts implicit, temporary stubs alter behavior.

---

## Consensus Summary

### Agreed Strengths
- **Well-decomposed wave structure** — both reviewers praise the 3-wave dependency graph
- **Strong requirement coverage** — all 6 phase requirements mapped to concrete tasks
- **Processor-owned state** — correctly handles editor destruction/recreation pattern
- **Reuse of existing contracts** — UiEvent, UiCommand, SpscRing patterns carried forward
- **Concrete acceptance criteria** — build-verifiable, specific strings/patterns

### Agreed Concerns (both reviewers flagged)
1. **HIGH — `applyScale()` double-scaling.** Both flag `setTransform` + `setSize` as incorrect for plugin hosts. Fix: use transform only, keep setSize at base dimensions.
2. **HIGH — License callback locking.** Claude identifies `ScopedLock` prevents lock release in callback. Codex flags missing behavior for no-editor case. Both agree this needs structural redesign.
3. **HIGH — `cachedUsers` dependency ordering.** Both note it's introduced in Plan 04-04 but consumed by Plan 04-03. Should be pulled into Plan 04-01 or made an explicit cross-plan contract.
4. **HIGH — Threading contracts implicit.** Both want explicit thread-ownership documentation in Plan 04-01 that later plans conform to.
5. **MEDIUM — Plugin host `setSize` for chat toggle.** Both flag that DAWs may not honor arbitrary size changes.
6. **MEDIUM — Remote VU stubbed to zero.** Both note the mixer looks broken without remote VU data in Phase 4.
7. **MEDIUM — Per-VuMeter timers.** Both suggest centralizing to one timer in ChannelStripArea.

### Divergent Views
- **Manual user enumeration:** Claude strongly recommends `GetRemoteUsersSnapshot()` API. Codex focuses more on the threading risk of the manual approach but doesn't mention the snapshot API.
- **Scope creep risk:** Codex rates Plan 04-03 as MEDIUM-HIGH (too much Phase 5 surface area). Claude rates it LOW (well-specified layout).
- **Plan 04-04 risk:** Codex rates it HIGH (too much late plumbing). Claude rates it MEDIUM (fixable during execution).
- **Codec default:** Codex flags possible FLAC default conflicting with "Vorbis default" in project spec. Claude doesn't mention this.
