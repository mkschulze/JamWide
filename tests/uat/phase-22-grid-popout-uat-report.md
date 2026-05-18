# Phase 22 — Native Video UI (Grid + Popouts) UAT Report

**Plan:** 22-04 Task 4 (`checkpoint:human-verify`, gate="blocking")
**Test date:** _(YYYY-MM-DD)_
**Build number:** _(JamWide build #, e.g. v1.1-beta.20.X or local-N)_
**Build commit:** _(git rev-parse --short HEAD on the build branch)_
**Tester:** _(name + role)_
**Environment:** _(macOS x86_64 / macOS arm64 / Windows x86_64; standalone / DAW host)_
**Reference server:** `video.ninjamzap.com:2049`
**Coordinated peers:** _(usernames of remote peers + broadcasting status)_
**Procedure:** [`tests/uat/phase-22-grid-popout-uat-procedure.md`](./phase-22-grid-popout-uat-procedure.md)

---

## Phase Closure Policy

Closure REQUIRES one of:

- **State A — Full PASS:** all 13 cells PASS (or SKIPPED with environmental
  justification). Status: `Phase 22 closed (Cells 1-13 PASS)`.
- **State B — Deferred-risk close:** any BLOCKED cell has a deferred-risk
  record below AND a corresponding entry in `.planning/STATE.md` under
  "Phase 22 deferred risks → Phase 24 follow-up". Status:
  `Phase 22 closed with deferred-risk Cells X, Y tracked in STATE.md → Phase 24`.
- **State C — FAIL:** any cell FAILs due to bug → Phase 22 does NOT close;
  revise plan and re-execute.

NOT acceptable: "BLOCKED-but-closed" without a deferred-risk record
(per `feedback_uat_scope_redflags`).

---

## Cell Status Summary

| Cell | Requirement / Decision  | Status | Cell label                                  | Notes |
| ---- | ----------------------- | ------ | ------------------------------------------- | ----- |
| 1    | DISP-01 happy path      | _(PASS/FAIL/BLOCKED/SKIP)_ | REQUIRES_COORDINATED_PEER (partial EXECUTABLE_NOW) | |
| 2    | DISP-02 popout drag     | _(...)_ | REQUIRES_COORDINATED_PEER                  | |
| 3    | DISP-03 coexistence     | _(...)_ | REQUIRES_COORDINATED_PEER (2 peers)        | |
| 4    | DISP-04 session continuity | _(...)_ | REQUIRES_COORDINATED_PEER                | |
| 5    | D-02 band placement     | _(...)_ | EXECUTABLE_NOW                             | |
| 6    | D-03 placeholders (M7)  | _(...)_ | REQUIRES_COORDINATED_PEER                  | |
| 7    | D-05 auto-open latch    | _(...)_ | REQUIRES_COORDINATED_PEER                  | |
| 8    | D-09 self-tile reuse    | _(...)_ | EXECUTABLE_NOW                             | |
| 9    | D-10 band resizer + persistence | _(...)_ | EXECUTABLE_NOW                     | |
| 10   | D-13 sink-poll Timer    | _(...)_ | REQUIRES_COORDINATED_PEER                  | |
| 11   | H3 4-state walk         | _(...)_ | REQUIRES_COORDINATED_PEER                  | |
| 12   | D-15 popout bounds persistence | _(...)_ | REQUIRES_COORDINATED_PEER           | |
| 13   | T-22-MM multi-monitor   | _(...)_ | REQUIRES_MULTI_MONITOR (+peer for full)    | |

---

## Detailed Results

### Cell 1 — DISP-01 happy path (single peer)

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Result:** _(description)_

**Screenshots:** _(paths or N/A)_

**Issues found:** _(bullet list)_

**L9 glyph check:** _(PASS / TYPOGRAPHY ISSUE — describe platform + glyph behavior — fallback to Path-drawn triangle acceptable for v1.3 beta, mandatory fix for v1.3 release)_

**Failure / BLOCKED record (if applicable):** _(symptom + suspected root cause OR environmental reason + tracked follow-up. If BLOCKED, add a corresponding entry to `.planning/STATE.md` under "Phase 22 deferred risks → Phase 24 follow-up".)_

---

### Cell 2 — DISP-02 popout open + drag (single monitor)

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Result:** _(description)_

**Issues found:** _(bullet list)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 3 — DISP-03 grid + popouts coexist (two peers)

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Result:** _(description)_

**Both popouts decoding simultaneously?** _(Y/N)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 4 — DISP-04 toggle grid without disconnecting NINJAM

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Toggle cycle count completed:** _(integer)_

**Beat counter monotonic across all toggles?** _(Y/N)_

**Peer dropped from ChannelStripArea?** _(Y/N — if Y, DISP-04 hard violation)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 5 — D-02 band placement + ConnectionBar visual

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Right cluster fits at kBaseWidth=1280?** _(Y/N)_

**Layout order verified (ConnectionBar → BeatBar → SessionInfoStrip → Band → ChannelStripArea)?** _(Y/N)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 6 — D-03 placeholder cards (M7 dual-band sync)

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**M7 codex closure verified (detached-grid placeholder mirrors main-band)?** _(Y/N)_

**Click-to-bring-back works as exclusive destroy path (codex H3)?** _(Y/N)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 7 — D-05 auto-open latch (fires once per session)

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Time from peer first-frame to band auto-open:** _(ms)_

**Latch did NOT re-fire after explicit user close + peer restart?** _(Y/N)_

**No JUCE crash from M4 lock-release-then-sync pattern?** _(Y/N)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 8 — D-09 self-tile popout reuses CameraPreviewWindow

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Self-popout `↗` reuses existing CameraPreviewWindow (no second window)?** _(Y/N)_

**M5 typed dispatch verified (no magic string)?** _(Y/N — confirm via source inspection if possible)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 9 — D-10 draggable band resizer + persistence

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Band height clamped to [140, 800] when dragging beyond bounds?** _(Y/N)_

**Height persists across plugin/standalone restart (D-19)?** _(Y/N)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 10 — D-13 sink-poll Timer mount/unmount

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Tile mounted within ~67ms of peer first-frame?** _(Y/N — measured ms if possible)_

**Tile unmounted on peer leave?** _(Y/N)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 11 — H3 4-state truth table walk

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

| Step | Action                            | Expected transition           | Observed             |
|------|-----------------------------------|-------------------------------|----------------------|
| 1    | (initial)                         | (A) absent                    | _(state observed)_  |
| 2    | Click peer A's band-tile `↗`     | A → B (CREATE+SHOW)           | _(...)_              |
| 3    | Click popout's `×`               | B → C (HIDE, no destroy)      | _(...)_              |
| 4    | Click band-tile `↗` (hidden popout) | **C → B (RE-SHOW non-destructive)** | _(...)_       |
| 5    | Click popout's `×` again         | B → C (HIDE)                  | _(...)_              |
| 6    | Click placeholder card            | **C → D (DESTROY)**           | _(...)_              |

**Step 4 specifically RE-SHOWS, NOT destroys?** _(Y/N — codex H3 disambiguation)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 12 — D-15 per-peer popout bounds persistence

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Saved bounds:** `{x: ___, y: ___, w: ___, h: ___}`

**Restored bounds (after plugin restart):** `{x: ___, y: ___, w: ___, h: ___}`

**Bounds keyed correctly by username?** _(Y/N)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

### Cell 13 — T-22-MM multi-monitor

**Status:** _(PASS / FAIL / BLOCKED / SKIP)_

**Popout dragged to secondary display successfully?** _(Y/N)_

**Bounds persisted across plugin restart (secondary display still attached)?** _(Y/N)_

**With secondary display disconnected, popout opens with bounds intersecting primary display (T-22-MM clamp)?** _(Y/N)_

**Failure / BLOCKED record (if applicable):** _(...)_

---

## Closure status

_(Set this AFTER all 13 cells have been exercised. Choose one of the two acceptable templates:)_

```
Phase 22 closed (Cells 1-13 PASS)
```

OR

```
Phase 22 closed with deferred-risk Cells X, Y tracked in STATE.md → Phase 24
```

_(Do NOT write a closure status if any cell is FAIL — the phase is NOT closed until FAILs are resolved.)_

**Recommendation (State A / B / C):** _(A / B / C)_

**Rationale:** _(free text describing why the recommendation is what it is — especially if some cells are BLOCKED on environment vs FAILed on bug)_

---

## Codex review-fix verification (from Plan 22-04)

| Concern | Closure verified by | Operator confirmation |
|---------|---------------------|----------------------|
| H2 (juce::HashMap in production) | Plan 22-04 Task 1 acceptance grep gate (production source) | _(N/A — automated)_ |
| M6 (production-vs-test schema parity) | Plan 22-04 Task 1 acceptance grep gate (all 11 property names in production save AND load blocks) | _(N/A — automated)_ |
| L9 (`↗` glyph rendering — no tofu) | Cell 1 step 5 + report's "L9 glyph check" line | _(PASS / TYPOGRAPHY ISSUE recorded above)_ |
| H3 (4-state truth table) | Cell 11 + tests/test_remote_peer_popout_lifetime Test 7 | _(PASS recorded in Cell 11)_ |
| M7 (dual-band sync) | Cell 6 step 4 explicit verification | _(PASS recorded in Cell 6)_ |
| M5 (typed VideoPopoutTarget) | Cell 8 + source-level grep verification | _(PASS recorded in Cell 8)_ |
| T-22-SP (state-injection hardening) | tests/test_plugin_state_v4_v5 Tests 4/5/6/7 | _(N/A — automated; tests pass)_ |
| T-22-MM (multi-monitor clamp) | Cell 13 + tests/test_remote_peer_popout_lifetime Test 5 | _(PASS recorded in Cell 13)_ |
