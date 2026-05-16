---
phase: 20
reviewers: [codex]
rounds: 3
reviewed_at_round_1: 2026-05-16T16:30:00Z
reviewed_at_round_2: 2026-05-16T17:00:00Z
reviewed_at_round_3: 2026-05-16T17:34:00Z
plans_reviewed: []
artifacts_reviewed: [20-CONTEXT.md, 20-RESEARCH.md]
review_stage: pre-plan
review_model_round_3: gpt-5.5
notes: |
  Round 1 (commit bce53bb): Pre-plan review of original 18-decision CONTEXT.md + 991-line RESEARCH.md.
  Found 4 HIGH (3 closed by revision, 1 still open) + 3 MEDIUM.

  Round 2 (commit 049e47a): Re-review after substrate revision (CONTEXT.md D-19/D-20 added,
  Path A/B framing dropped, RESEARCH.md gained CRITICAL UPDATE block). Confirmed H1/H4 closed,
  flagged H2 partial + H3 open, surfaced 3 NEW HIGH findings (H5/H6/H7).

  Round 3 (this round, commit TBD): Re-review after strict NinjamZap-literal R2 revision
  (commit a417238: whole-block m_video_cs, Phase 15.1-06 carve-out for marker GUID instead of
  LocalChannelMirror extension, expanded audit envelope). Confirms 4/5 open HIGHs CLOSED;
  H5 PARTIALLY-CLOSED (mirror bug fixed but raw 16B read race remains). Surfaces 4 NEW MEDIUM
  findings. Verdict: PROCEED-WITH-CAUTION with 5 must-fix items.
---

# Cross-AI Plan Review — Phase 20

## Round 1 — Original CONTEXT.md + RESEARCH.md (codex, gpt-5.5, 2026-05-16T16:30)

### Summary
As written, the architecture is close on wire-format intent, but I would not let plans proceed unchanged. It can deliver success criteria 1, 2, and 4 with normal implementation work, but success criterion 3 is at risk because D-08 depends on a substrate that is documented "any thread" but implemented as a true SPSC queue. The current design also puts heap allocation, possible logging, and `WDL_RNG_bytes()` on the audio thread via `RawDataSendBegin/Write`, which conflicts with the Phase 15.1 RT-safety baseline.

### Strengths
- Phase boundary is well-scoped: encode/send only, receive/render/package deferred.
- HYBRID model matches NinjamZap's proven semantic split.
- Path B was the right kind of escape hatch philosophically.
- Wire-format details concrete enough for downstream plans.
- Research correctly flagged the two subtle integration risks (LocalChannelMirror + SPS/PPS pointer lifetime).

### Concerns

- **HIGH: D-08 / WIRE-03 challenges the actual queue implementation.** `m_rawdata_sendq` is `jamwide::SpscRing<RawDataItem, 64>` (`src/core/njclient.h:976`). Ring contract is single-producer only; `try_push()` uses relaxed load/store of `head_` with no CAS (`src/threading/spsc_ring.h:21,51`). D-08 has two producers (audio + encoder) — not MPSC-safe.
- **HIGH: D-08 puts non-RT work on the audio thread.** `RawDataSendBegin()` calls `WDL_RNG_bytes()` (`src/core/njclient.cpp:2994`); `RawDataSendWrite()` allocates `new WDL_HeapBuf`, resizes, copies, may `writeLog()` on failure (`src/core/njclient.cpp:3021`).
- **HIGH: D-08 has a likely `m_video_guid` race.** Audio thread writes 16-byte GUID per interval; encoder thread reads for each frame chunk. `m_video_interval_open` atomic is not enough; GUID itself needs publication/snapshot.
- **HIGH: D-10 scenario names need correction.** Verified directory listing: `04_late_join_midstream.cpp`, `06_audio_video_resync.cpp` do NOT exist. Actual: `02_video_one_interval_early.cpp`, `03_late_join.cpp`, `13_sps_pps_mid_stream.cpp`, `20_drop_resync_recovery.cpp`, `22_audio_then_video.cpp`, `25_no_initial_spspps.cpp`, `26_send_buffer_pressure.cpp`.
- MEDIUM: Pitfall #1 fix directionally sound but underspecified.
- MEDIUM: D-15 one-frame IDR drift acceptable only if tested against short intervals.
- MEDIUM: D-16 bandwidth plausible but queue capacity rationale stale (assumes ~2-4 RawData items/sec).

### Round 1 Risk Assessment
Overall risk: **HIGH** until D-08/WIRE-03 corrected, then MEDIUM.

---

## Round 2 — Revised CONTEXT.md (D-19/D-20 added) + RESEARCH.md (CRITICAL UPDATE banner) (codex, gpt-5.5, 2026-05-16T17:00)

### Summary
The revision fixes the biggest substrate mismatch (H1) by moving to the NinjamZap queue model, but I would not yet call the architecture sound enough for plan writing. Two original HIGHs are only partially closed: `RawDataSendBegin/Write` still do audio-thread allocation/RNG work, and the `m_video_guid` race is not closed unless encoder-side reads also take `m_video_cs` or use a copied atomic/snapshot GUID. I also found one new HIGH: D-20's proposed `curwritefile_guid` write site is on the run thread in JamWide, while `LocalChannelMirror` is explicitly audio-thread-owned.

### Round 1 HIGH Status

| Finding | Status | Assessment |
|---|---:|---|
| H1: SPSC substrate invalid for two producers | **CLOSED** | D-19 matches NinjamZap's actual shape: `WDL_PtrList<RawDataQueueItem>` + `WDL_Mutex m_rawdata_sendq_cs` in `ninjamzap-core/.../njclient.h:310-321`, with producers adding under mutex at `njclient.cpp:2059-2081`. |
| H2: RawData calls allocate/RNG/log on audio thread | **PARTIALLY CLOSED** | D-19 removes bounded-ring overflow logging, but the NinjamZap-style calls still do `WDL_RNG_bytes` in `RawDataSendBegin` and `new RawDataQueueItem` / `WDL_HeapBuf::Resize` in `RawDataSendBegin/Write` (`ninjamzap ... cpp:2047-2076`). D-09 only allowlists mutexes, not allocation/RNG. |
| H3: `m_video_guid` race | **LEAVES OPEN** | D-08/D-11 say `m_video_guid` is under `m_video_cs`, but encoder-thread `QueueVideoFrame`/direct `RawDataSendWrite(m_video_guid,...)` still reads it. A mutex only protects data if both writer and reader use it. NinjamZap reads `m_video_guid` unsynchronized in `QueueVideoFrame` at `cpp:2116-2120`; JamWide needs TSan-clean semantics. |
| H4: nonexistent scenario filenames | **CLOSED** | D-10 now uses actual scenario names: `02`, `03`, `13`, `20`, `22`, `25`, plus stress `18`/`26`. |

### New Concerns (introduced by Round 1's revision)

- **HIGH H5: D-20 writes the GUID mirror from the wrong thread.** JamWide's `src/core/njclient.cpp:2606` is in the run-thread broadcast drain/encode path, while `LocalChannelMirror` is documented as "mutated EXCLUSIVELY by the audio thread" in `src/core/njclient.h:888-890` and applied through `drainLocalChannelUpdates()` at `src/core/njclient.cpp:3817-3831`. A raw `memcpy` to `m_locchan_mirror[0].curwritefile_guid` at line 2606 would violate Phase 15.1-06 and race with `on_new_interval`.

- **HIGH H6: Cross-producer ordering can put a frame before marker/SPS.** The proposed model enqueues `BEGIN`, sets `m_video_interval_open`, then enqueues marker and SPS/PPS as separate queue items. Encoder frames can enqueue between those audio-thread calls. NinjamZap has the same exposure (`cpp:3043-3075` plus `QueueVideoFrame` at `2116-2120`). JamWide's success criteria require marker first and SPS/PPS second; the architecture needs a local ordering guard, not just reference parity.

- **HIGH H7: The audit allowlist is too narrow.** D-09 allowlists three mutex acquisitions, but the audio-thread call chain also includes RNG, heap allocation, `WDL_HeapBuf::Resize`, `memcpy` of SPS/PPS, and possible allocation failure handling. In current JamWide, failure paths also call `writeLog` at `src/core/njclient.cpp:3015`, `3048`, `3063`; D-19 must explicitly remove or make impossible any audio-thread log path.

- **MEDIUM M4: Drain semantics differ from the proposed text.** D-19 says "swaps the list out under the mutex, then processes outside." NinjamZap does not swap the full list; it pops one item, unlocks, sends it, then relocks (`ninjamzap ... cpp:1987-2039`). Full-list swap is probably better for contention, but it is not literal and changes batching/order visibility. Pick one deliberately.

- **MEDIUM M5: Unbounded queue failure mode is under-specified.** D-19 retires capacity/overflow, but if the run thread cannot drain as fast as producers enqueue, memory grows. NinjamZap accepts this, but JamWide should add depth/high-water observability and a video-only shedding policy before OOM. "TCP layer backpressure" does not stop producer-side heap growth because producers enqueue before `m_netcon->Send`.

- **LOW/MEDIUM M6: RESEARCH.md remains internally contradictory.** The critical update supersedes the body, but many sections still specify Path A atomics, SPSC capacity, overflow counters, deferred SPS/PPS pointer deletion, and 3-plan + contingency framing. That is likely to mislead plan authors.

### Round 2 Risk Assessment

Overall risk: **HIGH** until the GUID mirror lifecycle (H5), encoder-side `m_video_guid` synchronization (H3), and BEGIN/marker/SPS/frame ordering (H6) are corrected. After those changes, the architecture drops to **MEDIUM**: still pragmatic and reference-faithful, but carrying intentional audio-thread mutex/allocation carve-outs (H2/H7) that need measurement and explicit audit exceptions.

---

## Round 3 — R2-revised CONTEXT.md (strict NinjamZap-literal: whole-block m_video_cs, Phase 15.1-06 carve-out, expanded audit envelope) (codex, gpt-5.5, 2026-05-16T17:34)

### Summary

The R2 revision materially improves the architecture and closes the main Round 2 design holes around `m_video_guid` synchronization and cross-producer frame interleaving. I would not call it clean enough for an unconditional proceed, though. The plan is now coherent if JamWide intentionally accepts NinjamZap-literal audio-thread carve-outs, but D-20's direct `m_curwritefile.guid` read is still a known non-atomic cross-thread read/write risk, and the artifacts still contain stale contradictory text that can mislead plan authors.

### HIGH Status (H2/H3/H5/H6/H7)

| Finding | Status | Evidence |
|---|---:|---|
| **H2: RawData alloc/RNG/log on audio thread** | **CLOSED, with explicit carve-out** | D-09 now allowlists the actual audio-thread envelope: `m_video_cs`, `m_video_spspps_cs`, `m_rawdata_cs`, `WDL_RNG_bytes`, `new WDL_HeapBuf`, `Resize`, and marker/SPS memcpy. It also explicitly requires no `writeLog` calls on the audio-thread video path and assigns Plan 20-00 to strip residual logging. This is no longer an accidental violation; it is an intentional Phase 15.1 carve-out. |
| **H3: `m_video_guid` race** | **CLOSED** | D-08 and D-11 now state encoder `QueueVideoFrame` acquires `m_video_cs` before reading `m_video_active`, `m_video_interval_open`, and `m_video_guid`, then calls `RawDataSendWrite` while still serialized by that mutex. That closes the prior "mutex protects only one side" issue. |
| **H5: invalid `LocalChannelMirror.curwritefile_guid` extension** | **PARTIALLY CLOSED** | The bad mirror-extension proposal is retired in D-20, so the specific H5 bug is closed. However, the replacement is a direct audio-thread read of `m_locchans[0]->m_curwritefile.guid` while the writer at `src/core/njclient.cpp:2606` does not take `m_video_cs`. D-20 acknowledges this and says "if TSan flags a race, add a per-channel atomic seqlock." That is too conditional for a known 16-byte non-atomic cross-thread field. The carve-out is specifiable as one field/one read site, but TSan-clean semantics are not guaranteed yet. |
| **H6: frame can interleave between BEGIN/marker/SPS** | **CLOSED for cross-producer interleaving** | D-08 now holds `m_video_cs` across the entire audio-thread video block: END → BEGIN → marker → SPS/PPS. Encoder frames cannot enter `QueueVideoFrame` until that block releases. This fixes the original race. Caveat: D-13 allows a marker-only cold-start interval if SPS/PPS is not ready; that is not a cross-producer interleaving bug, but it does weaken the "SPS/PPS is second chunk" invariant unless explicitly tested/accepted. |
| **H7: audit allowlist too narrow** | **CLOSED** | D-09 now covers the full intended exception surface: mutexes, RNG, heap allocation/resize, memcpy, canonical `Local_Channel` read, and no logging. This is the right shape for `realtime-audio-reviewer` allowlisting. |

### New Concerns

- **MEDIUM M7: D-20 direct GUID read should not be left as "if TSan flags."** The design now knowingly reads a 16-byte non-atomic field from the audio thread while the run thread writes it. If the project requires TSan-clean verification, the plan should either implement the seqlock immediately or explicitly document this as a TSan allowlist/expected race. Waiting for TSan to rediscover a known race is weak planning.

- **MEDIUM M8: CONTEXT.md still contradicts itself on drain semantics.** D-19 and the Integration Points section say pop-one-unlock-Send-relock, but Code Context / Reusable Assets still says "run-thread drain swaps the list out under the mutex, processes outside." That stale sentence can cause Plan 20-00 to implement the wrong drain model.

- **MEDIUM M9: Cold-start SPS/PPS behavior conflicts with the simple WIRE-01 success wording.** D-13 says first broadcast interval may be marker-only if encoder init lags. WIRE-01 and success criterion 2 say SPS/PPS appears as the second chunk. Either gate frames until SPS/PPS exists, or explicitly make `25_no_initial_spspps.cpp` the accepted exception and adjust the plan's assertions.

- **MEDIUM M10: Unbounded rawdata queue observability remains under-specified.** D-16 mentions contention/depth tracing "if needed," while the RESEARCH update says Plan 20-03 adds high-water observability and threshold. That should be made concrete in CONTEXT or PLAN so M5 does not reappear during execution.

### RESEARCH.md Staleness Assessment

**MEDIUM blocker risk, not a hard blocker if CONTEXT.md is enforced as authoritative.** The CRITICAL UPDATE at lines 1-40 is clear and correctly says CONTEXT.md supersedes the body. However, stale sections are extensive and operationally dangerous: Summary lines 42-56 still recommend Path A/B and 3 plans plus contingency; Locked Decisions lines 79-105 still specify atomic SPS/PPS, Path A primary, atomic `m_video_active`; Pattern 3 lines 487-510 still gives code for atomic pointer swap/deferred delete; older sections still discuss SPSC capacity and overflow-counter assumptions.

Risk: a planner skimming headings or copying examples can easily generate the wrong plan. Minimum fix: either delete/archive stale body sections or add loud `STALE - DO NOT PLAN FROM THIS SECTION` markers before each stale section heading. The current top banner helps, but 1000 lines of contradictory body is still a real plan-misleading hazard.

### Plan-Phase Readiness Verdict

**PROCEED-WITH-CAUTION.**

Must-fix before or during Plan 20-00 / 20-02:

1. **D-20 determinism:** Implement seqlock/atomic snapshot for `m_curwritefile.guid`, or explicitly document a TSan allowlist for that exact race. Do not leave it as "if TSan flags."
2. **CONTEXT.md drain semantics consistency:** Remove the stale sentence claiming swap-list-out drain semantics.
3. **Cold-start SPS/PPS resolution:** Either enforce marker → SPS/PPS → frames for every active interval or document/test the marker-only first interval as accepted NinjamZap behavior (and reconcile WIRE-01 + success criterion 2 wording).
4. **Queue observability concretization:** Make rawdata queue high-water/depth observability and acceptance threshold concrete in CONTEXT.md or PLAN.md.
5. **RESEARCH.md trace-only treatment:** Treat RESEARCH.md as trace-only, not planning input, unless stale sections are clearly marked or removed.

### Round 3 Risk Assessment

Overall risk: **MEDIUM**. The major wire-ordering and `m_video_guid` synchronization issues are fixed. Remaining risk is concentrated in intentional reference-literal compromises: audio-thread mutex/allocation/RNG carve-outs, the canonical audio GUID read, unbounded queue growth, and stale documentation that could send the planner back toward retired designs.

---

## Consensus Across All Three Rounds

Single reviewer (codex / gpt-5.5) across all three rounds. The review-revise-review loop produced measurable progress:

- **Round 1 → 4 HIGHs identified.**
- **Round 2 (after first revision) → H1 + H4 closed; H2 partial; H3 open; 3 NEW HIGHs surfaced (H5/H6/H7).** Total open going into R3: 5 HIGHs.
- **Round 3 (after strict NinjamZap-literal R2 revision) → H2/H3/H6/H7 CLOSED; H5 PARTIALLY-CLOSED; 4 NEW MEDIUMs (M7-M10).** Total open: 0 HIGH, 4 MEDIUM + 1 partial.

### Architectural Trajectory

The phase moved across three positions:

1. **R1 baseline:** "Phase 15.1-pure with broken substrate" — pure atomics over a single-producer ring with two producers (H1 substrate mismatch + H2/H3 RT-safety violations).
2. **R2 intermediate:** "NinjamZap-literal substrate but partial encoder synchronization + invalid mirror extension" — substrate fixed (H1 closed) but H5/H6/H7 emerged from race-vulnerable patterns inherited from a partial port.
3. **R3 current:** "Strict NinjamZap-literal with explicit Phase 15.1 carve-outs" — whole-block `m_video_cs` enforces wire ordering, encoder reads under mutex, mirror extension retired in favor of a one-field/one-site Phase 15.1-06 carve-out, audit envelope expanded to cover the full audio-thread exception surface.

### Remaining HIGH-equivalent risk: D-20 canonical GUID read (M7)

The single remaining read-vs-write race in the design is the audio thread reading `m_locchans[0]->m_curwritefile.guid` (16 bytes) while the run-thread writer at `src/core/njclient.cpp:2606` regenerates the GUID without holding `m_video_cs`. NinjamZap accepts this race in practice on iOS/Android (temporal separation between writer and reader call sites); JamWide's TSan verification gate would flag it. The R2 fix downgraded H5 from HIGH to PARTIAL by retiring the wrong mirror extension, but did not eliminate the race — only relocated it. M7's recommendation is to decide now (seqlock or explicit TSan allowlist), not defer to UAT discovery.

### Pattern Worth Recording

R2's revision pattern — "go strict NinjamZap-literal and carve out the Phase 15.1 invariants the reference violates" — is the right design move per `feedback_proven_over_pure`. The cost is **intentional, bounded, auditable**: ~50-100 µs of audio-thread work inside the video block per interval, well within the audio-thread budget at typical NINJAM intervals (3-8s). The remaining work is precision: making the carve-out *deterministic* (seqlock for M7) rather than *probabilistic* (TSan discovery).

### Recommended Next Step

Proceed to **plan-phase** with the 5 must-fix items above incorporated either as Plan 20-00 / 20-02 acceptance criteria or as a final CONTEXT.md R3 revision pass before planning starts. Of those:

- Items 1 (D-20 seqlock) and 2 (drain consistency) are CONTEXT.md edits.
- Items 3 (cold-start SPS/PPS) and 4 (queue observability) can land in PLAN.md acceptance criteria.
- Item 5 (RESEARCH.md staleness) can be addressed with `STALE — DO NOT PLAN FROM THIS SECTION` markers in the stale body sections, or by archiving them.

If the planner reads CONTEXT.md as authoritative and treats RESEARCH.md as trace-only (per the CRITICAL UPDATE block), the phase is ready for `/gsd:plan-phase 20 --reviews`.
