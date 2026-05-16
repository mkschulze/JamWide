# Phase 20 — HUMAN-PENDING UAT

**Status:** ⏸ AWAITING HUMAN UAT (Plan 20-03 Task 4 — `checkpoint:human-verify`, gate="blocking")
**Plan:** `.planning/phases/20-h-264-encoder-send-pipeline/20-03-processor-wiring-and-uat-PLAN.md`
**Procedure:** `tests/uat/phase-20-broadcast-uat-procedure.md`
**Harness:** `tests/uat/phase-20-broadcast-uat.sh`

This file is the single canonical pointer that Plan 20-03 is **not complete** until a human operator executes the 5-minute populated-server UAT and captures the result. The plan is marked `autonomous: false` for this reason. Plans 20-00 / 20-01 / 20-02 are complete and merged. Plan 20-03 Tasks 1, 2, 3, and 5 are complete and committed on the worktree branch. **Task 4 is the open item.**

---

## What's done autonomously (Plan 20-03 Tasks 1, 2, 3, 5)

| Task | Status | Commit | Verification |
|------|--------|--------|--------------|
| 1 — Processor + Editor + ConnectionBar wiring; NinjamRunThread channel registration; audio-thread budget probe; Disconnect END-emit | ✅ | `da044da` | Standalone + AU + VST3 build clean; existing Phase 20 tests still 5/5 green |
| 2 — `tests/test_processor_video_lifecycle.cpp` (7 sub-tests covering T-20-03 ordering + R4 M11 paths 2/3) | ✅ | `8d08dca` | `ctest -R processor_video_lifecycle` → 7/7 PASS |
| 3 — `tests/uat/phase-20-broadcast-uat.sh` + `.../phase-20-broadcast-uat-procedure.md` + CMake `phase20-uat` custom target | ✅ | `7b4301c` | `bash -n` syntax OK; `--assert` helper smoke-tested |
| 5 — `CHANGELOG.md` v1.3 Phase 20 entry | ✅ | _(pending in this SUMMARY commit)_ | grep "Phase 20" CHANGELOG.md ≥ 1 |

## What's blocked on the human (Plan 20-03 Task 4)

**The 5-minute 2-peer populated-server broadcast UAT against `video.ninjamzap.com:2049` (or local `ninjamzap-server-docker` on `localhost:2049` if the public server is down).**

This is **not automatable**. The acceptance gates require:

1. Two real peers exchanging audio + video over a real NINJAM server for at least 5 minutes at each of three presets (Low / Medium / High).
2. A human ear listening for audio glitches during the broadcast (subjective gate).
3. Visual confirmation of the UI: right-click Camera → "Start Broadcast" → menu label flips to "Stop Broadcast" → encoder log lines appear.
4. Wire-format tcpdump (or NinjamZap mobile receive) confirming the 24-byte interval marker structure on channel 1.
5. R4 M11 teardown verification — observing the END NAL on the wire across three independent teardown paths.

---

## What the operator does

### Pre-flight

```sh
cd /Users/cell/dev/JamWide/.claude/worktrees/agent-a72896c7e32bca9b5
bash tests/uat/phase-20-broadcast-uat.sh --build
bash tests/uat/phase-20-broadcast-uat.sh --check
```

The `--build` step produces a `JamWideJuce_Standalone` at `build-juce/JamWideJuce_artefacts/Release/Standalone/JamWide.app` with `JAMWIDE_BUILD_TESTS=ON` (exposes the queue-observability accessors needed at runtime).

The `--check` step confirms the binary exists, the public server is reachable, and `tcpdump` is on PATH (optional).

### Run

Open and follow:

```text
tests/uat/phase-20-broadcast-uat-procedure.md
```

The procedure walks through 9 steps (35-45 minutes total):

1. Launch instance A, connect to `video.ninjamzap.com:2049`, verify channel-1 registration (SetLocalChannelInfo + SetVideoChannel both fire at connect-up per D-18)
2. Open camera + select Low preset + Start Broadcast
3. 5-minute Low-preset run + per-30s lldb readout of the four counters
4. Connect instance B (or NinjamZap mobile peer); verify wire-level reception (Phase 20 = SEND only; receive/decode/render = Phase 21/22)
5. Repeat 2-4 at Medium preset
6. Repeat 2-4 at High preset
7. TSan dual-scope re-run at Medium preset (`./scripts/build.sh --tsan` first; zero `WARNING: ThreadSanitizer: data race` reports)
8. R4 M11 teardown verification (three paths: normal broadcast-off → END within one interval; Disconnect → END before TCP FIN; plugin destruction → best-effort)
9. Capture the final UAT report at `tests/uat/phase-20-broadcast-uat-report.md`

### Acceptance gates (R3 MF4 — must ALL hold for each preset)

```text
m_rawdata_sendq_high_water_mark            < 32 items
contention_count / total_enqueues          < 1%
m_encoder_input_drops                      == 0 at end-of-run
on_new_interval video block worst-case     <= 200,000 ns (200 µs)
```

Plus:

- Subjective audio: no audible glitches.
- TSan dual-scope (Medium): zero races.
- Wire-format check: first chunk of each interval is 24 bytes matching the marker spec.
- R4 M11 path 1 (normal off): END observed within one interval.
- R4 M11 path 2 (Disconnect): END observed before TCP FIN.
- R4 M11 path 3 (plugin destruction): best-effort (NOT a hard fail).

### Threshold-gate helper

The harness includes a scripted threshold check that the operator pipes the lldb readout into:

```sh
# Read the 5 counters via lldb attached to JamWide process at T=5:00, then:
echo "<high_water> <contention> <total_enq> <drops> <audio_ns>" \
    | bash tests/uat/phase-20-broadcast-uat.sh --assert
# → PASS or FAIL with per-gate breakdown
```

---

## How to resume the plan

After completing the UAT and writing the report, return to the orchestrator with **one of two signals**:

### PASS

```text
approved — all gates pass — tests/uat/phase-20-broadcast-uat-report.md
```

The orchestrator marks Plan 20-03 complete in STATE.md and ROADMAP.md, closes Phase 20, and the v1.3 native-video send pipeline is greenlit for beta packaging.

### FAIL

```text
fail — <gate that failed> at <preset> — <observed value vs threshold> — investigate <substrate sizing / sync arch>
```

For example: `fail — high_water hit 47 items at preset=High, contention 0.3%, no glitches — investigate substrate sizing`.

The orchestrator escalates the failure to a substrate-tuning subplan per CONTEXT.md Deferred Ideas (NOT a sync-architecture change per D-19 + `feedback_proven_over_pure`).

---

## Why this is human-gated

Three reasons (all locked into the plan's `autonomous: false` flag):

1. **The UAT requires populated-server load.** The audio-thread budget probe, contention sampling, and wire-format snoop only produce meaningful values under real cross-peer traffic. A mocked server cannot replicate the populated-load distribution that R3 MF4's thresholds are calibrated against.

2. **Subjective audio is a hard gate.** Phase 20's primary user-visible promise is "broadcast video without breaking audio." That promise can only be verified by a human listening. Per `feedback_uat_scope_redflags`, the executor cannot "verify only X, skip Y" when Y is the user-visible happy path.

3. **R4 M11 path-2/3 teardown observation requires real network teardown.** The Disconnect-emit-END path and the plugin-destruction path interact with `m_netcon` lifetime, which a unit test cannot stand up. The wire observation (tcpdump or peer receive log) is the only correct gate.

`tests/test_processor_video_lifecycle` sub-tests 6 and 7 cover the in-process portion of R4 M11 paths 2 and 3 (the cleanup code runs without crashing), but the wire observation is required to confirm the END actually reaches the receiver.

---

## Files of record

- **`tests/uat/phase-20-broadcast-uat.sh`** — orchestrating harness (build + check + assert sub-commands).
- **`tests/uat/phase-20-broadcast-uat-procedure.md`** — 250+ line operator runbook (the canonical procedure document).
- **`tests/uat/phase-20-broadcast-uat-report.md`** — _will be created by the operator during the UAT run_; template embedded at the bottom of the procedure document.
- **`.planning/phases/20-h-264-encoder-send-pipeline/20-03-processor-wiring-and-uat-SUMMARY.md`** — Plan 20-03 summary (Tasks 1-3, 5 complete; Task 4 explicitly marked HUMAN-PENDING).
