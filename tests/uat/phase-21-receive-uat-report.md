# Phase 21 Receive UAT Report

**Plan:** 21-03 Task 5 (`checkpoint:human-verify`, gate="blocking")
**Test date:** _(YYYY-MM-DD)_
**Build number:** _(JamWide build #, e.g. v1.1-beta.20.6 or local-N)_
**Tester:** _(name + role)_
**Reference server:** `video.ninjamzap.com:2049`
**Procedure:** [`tests/uat/phase-21-receive-uat-procedure.md`](./phase-21-receive-uat-procedure.md)

---

## Phase Closure Policy (codex review Cluster 9)

Closure REQUIRES one of:

- **State A — Full PASS:** All 4 cells (Cell 1, 2, 3, 4) marked PASS. Status: `Phase 21 closed (Cells 1-4 PASS)`.
- **State B — Deferred-risk close:** Any BLOCKED cell has a deferred-risk record in this report AND a corresponding entry in `.planning/STATE.md` under "Phase 21 deferred risks → Phase 24 follow-up". Status: `Phase 21 closed with deferred-risk Cells X, Y tracked in STATE.md → Phase 24`.

NOT acceptable: "BLOCKED-but-closed" without a deferred-risk record.

---

## Cell 1 — Wall-clock audio-video alignment (5+ min)

**ROADMAP success criterion 1.**
**Status:** _(PASS / FAIL / BLOCKED)_

### Diagnostic counter readings

| Sample | T+1:00 | T+2:00 | T+3:00 | T+4:00 | T+5:00 |
|--------|--------|--------|--------|--------|--------|
| `getRunVideoReceiveBlockMaxNanosForTest()` ns | | | | | |
| Peer A `hold_count` | | | | | |
| Peer A `drop_resync_count` | | | | | |
| Peer A `decode_error_count` | | | | | |

### Observation notes

_(Free text. Note any drift, glitches, or unexpected behavior.)_

### Failure / BLOCKED record (if applicable)

_(Symptom + suspected root cause + observed counter readings. If BLOCKED, add a corresponding entry to `.planning/STATE.md`.)_

---

## Cell 2 — Mid-session join sees video within ≤2 NINJAM intervals

**ROADMAP success criterion 2.**
**Status:** _(PASS / FAIL / BLOCKED)_

### Diagnostic counter readings

| Field | Value |
|-------|-------|
| Time-to-`first_frame_seen` (s from Connect click) | |
| Number of NINJAM intervals elapsed before first frame | |
| `getRunVideoReceiveBlockMaxNanosForTest()` at first frame ns | |

### Observation notes

_(Free text.)_

### Failure / BLOCKED record (if applicable)

_(Symptom + suspected root cause + observed counter readings.)_

---

## Cell 3 — Peer audio stops → freeze → kHoldCapDrop=4 → resume cleanly

**ROADMAP success criterion 3.**
**Status:** _(PASS / FAIL / BLOCKED)_

### Diagnostic counter readings

| Field | Before mute | At mute T+3s | At mute T+6s | At mute T+9s | At mute T+12s | After unmute |
|-------|-------------|--------------|--------------|--------------|---------------|--------------|
| Peer A `hold_count` | | | | | | |
| Peer A `drop_resync_count` | | | | | | |
| Tile visible? (frozen / black / decoding) | | | | | | |

### Observation notes

_(Did the `'syncing…'` overlay appear at `hold_count >= 2`? Did the tile rejoin cleanly after unmute?)_

### Failure / BLOCKED record (if applicable)

_(Symptom + suspected root cause + observed counter readings.)_

---

## Cell 4 — 3+ peers simultaneously, per-peer isolation, ≥5 min

**ROADMAP success criterion 4.**
**Status:** _(PASS / FAIL / BLOCKED)_

### Diagnostic counter readings

| Sample | T+1:00 | T+2:00 | T+3:00 | T+4:00 | T+5:00 |
|--------|--------|--------|--------|--------|--------|
| Peer A `decode_error_count` | | | | | |
| Peer A `hold_count` | | | | | |
| Peer A `drop_resync_count` | | | | | |
| Peer C `decode_error_count` | | | | | |
| Peer C `hold_count` | | | | | |
| Peer C `drop_resync_count` | | | | | |
| `getRunVideoReceiveBlockMaxNanosForTest()` ns | | | | | |

### Observation notes

_(Per-peer counters move independently? Any cross-contamination signs?)_

### Failure / BLOCKED record (if applicable)

_(Symptom + suspected root cause + observed counter readings.)_

---

## Closure status

_(Set this AFTER all 4 cells have been exercised. Choose one of the two acceptable templates:)_

```
Phase 21 closed (Cells 1-4 PASS)
```

OR

```
Phase 21 closed with deferred-risk Cells X, Y tracked in STATE.md → Phase 24
```

_(Do NOT write a closure status if any cell is FAIL — the phase is NOT closed until FAILs are resolved.)_
