---
phase: 20
reviewers: [codex]
rounds: 2
reviewed_at_round_1: 2026-05-16T16:30:00Z
reviewed_at_round_2: 2026-05-16T17:00:00Z
plans_reviewed: []
artifacts_reviewed: [20-CONTEXT.md, 20-RESEARCH.md]
review_stage: pre-plan
notes: |
  Round 1 (commit bce53bb): Pre-plan review of original 18-decision CONTEXT.md + 991-line RESEARCH.md.
  Found 4 HIGH (3 closed by revision, 1 still open) + 3 MEDIUM.

  Round 2 (this file, commit TBD): Re-review after substrate revision (CONTEXT.md D-19/D-20 added,
  Path A/B framing dropped, RESEARCH.md gained CRITICAL UPDATE block). Round 2 confirms H1/H4 closed,
  flags H2 partial + H3 open, and surfaces 3 NEW HIGH findings introduced by the revision.
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

### Remaining Medium/Low Items

- **Pitfall #1 / GUID mirror:** D-20 identifies the right need but not a valid lifecycle. The mirror should either be written on the audio thread when the interval boundary is created, or updated via an SPSC publication that the audio thread drains before marker construction. Writing directly from run thread is not acceptable.
- **IDR drift testing:** D-15 reasonable; plan tests must verify "first frame after observed interval seq is IDR" under BPM/BPI changes. Unaffected by D-19.
- **Queue capacity rationale:** D-19 removes the incorrect fixed capacity but replaces it with unbounded memory risk. Acceptable only with instrumentation and explicit UAT thresholds.

### Round 2 Specific Suggestions

- **CONTEXT.md edit:** Amend D-09 to allowlist the full audio-thread exception envelope, not just mutexes: `m_rawdata_cs`, `m_video_cs`, `m_video_spspps_cs` + `RawDataSendBegin/Write` allocation/RNG/copy cost + no logging on any audio-thread failure path.
- **CONTEXT.md edit (H3 fix):** Encoder-side `QueueVideoFrame` must copy `{active, interval_open, guid}` under `m_video_cs` before unlocking and calling `RawDataSendWrite`. Or use a double-buffered/atomic snapshot struct. Do not let encoder read `m_video_guid` naked.
- **CONTEXT.md edit (D-20 fix):** Do not write `LocalChannelMirror.curwritefile_guid` at `src/core/njclient.cpp:2606` unless that write is routed through the existing audio-thread mirror update mechanism (extend `LocalChannelAddedUpdate` or add a new `LocalChannelGuidUpdate` payload). The cleaner solution is to generate or publish the audio interval GUID on the audio thread before both audio and video boundary messages.
- **Plan-level (H6 ordering):** Add a test that forces encoder enqueue between `BEGIN` and marker/SPS. Expected wire order: `BEGIN → marker → SPS/PPS → frames`. Implementation choice: gate frames until after audio thread has enqueued marker/SPS for the interval (e.g., `m_video_interval_open` is set to `true` AFTER marker+SPS emit, not before).
- **Plan-level (20-00 tests):** Cover 2+ producer threads with per-producer order preservation, drain while producers are active, empty/non-empty transitions, destructor cleanup, OOM/failure behaviour, BEGIN/marker/SPS/frame ordering.
- **RESEARCH.md edit:** Remove or rewrite the stale Path A / SPSC / overflow-counter / deferred-delete / 3-plan-plus-contingency sections instead of relying on the critical-update banner.

### Round 2 Risk Assessment

Overall risk: **HIGH** until the GUID mirror lifecycle (H5), encoder-side `m_video_guid` synchronization (H3), and BEGIN/marker/SPS/frame ordering (H6) are corrected. After those changes, the architecture drops to **MEDIUM**: still pragmatic and reference-faithful, but carrying intentional audio-thread mutex/allocation carve-outs (H2/H7) that need measurement and explicit audit exceptions.

---

## Consensus Across Both Rounds

Single reviewer (codex) across both rounds. The review-revise-review loop produced concrete progress:
- Round 1 → 4 HIGHs identified.
- Revision pass → H1 + H4 fully closed; H2 partially closed; H3 left open (encoder-side reads not under mutex).
- Round 2 → 3 NEW HIGHs surfaced (H5/H6/H7), all introduced by the Round 1 revision.

**Pattern:** the revision swung the architecture from "Phase 15.1-pure with broken substrate" to "NinjamZap-literal with race-vulnerable patterns inherited from the reference." NinjamZap's source has H3/H6/H7 race exposure — works in practice on iOS/Android — but JamWide's TSan verification gate would flag them. Choosing between "ship NinjamZap's race patterns + allowlist them" vs "tighten synchronization where TSan would flag" is the next architectural decision.

### Open HIGHs Going Into Round 3 (or Plan Phase)

1. **H2 (partial):** `RawDataSendBegin/Write` allocates + RNG on audio thread. Either add to audit allowlist (NinjamZap-literal) or refactor (pre-allocate buffers, defer RNG to run thread).
2. **H3 (open):** Encoder-side reads of `m_video_guid` are unsynchronized. Either accept (NinjamZap-literal) or copy under `m_video_cs`.
3. **H5 (new):** D-20's `curwritefile_guid` write site is run-thread; mirror must be written audio-thread or via SPSC publication.
4. **H6 (new):** BEGIN/marker/SPS-PPS/frame wire-ordering is not enforced. Encoder frame can interleave with audio framing.
5. **H7 (new):** Audit allowlist scope insufficient — needs to cover RNG/alloc/log, not just mutex acquisitions.

H5 is unambiguously a bug I introduced (D-20 write site is wrong); H6 is a wire-protocol invariant we should enforce regardless of NinjamZap fidelity. H2/H3/H7 are the "how strictly NinjamZap-literal vs. how Phase 15.1-pragmatic" question.

### Recommended Next Step

Pause the discuss-CONTEXT loop. The remaining HIGHs are a coherent package that warrants either (a) a final discuss-phase pass with specific decisions on each, or (b) absorbing the findings into PLAN.md's Wave 0 for 20-00 and 20-02 with the planner working from a more flexible mandate. Continuing to iterate CONTEXT.md piece-by-piece without resolving the X-vs-Y question (NinjamZap-literal vs Phase-15.1-pragmatic) risks another correction round.
