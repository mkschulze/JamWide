---
phase: 20
reviewers: [codex]
reviewed_at: 2026-05-16T16:30:00Z
plans_reviewed: []
artifacts_reviewed: [20-CONTEXT.md, 20-RESEARCH.md]
review_stage: pre-plan
notes: Cross-AI peer review BEFORE plan generation. Phase 20 has CONTEXT.md (18 locked decisions) + RESEARCH.md (991 lines, 6 pitfalls, 9 open questions) but no *-PLAN.md yet. Reviewer was asked to challenge architecture-of-record before plans are written.
---

# Cross-AI Plan Review — Phase 20 (PRE-PLAN review)

## Codex Review

(Model: gpt-5.5, sandbox: read-only, reasoning: medium)

**Summary**

As written, the architecture is close on wire-format intent, but I would not let plans proceed unchanged. It can deliver success criteria 1, 2, and 4 with normal implementation work, but success criterion 3 is at risk because D-08 depends on a substrate that is documented "any thread" but implemented as a true SPSC queue. The current design also puts heap allocation, possible logging, and `WDL_RNG_bytes()` on the audio thread via `RawDataSendBegin/Write`, which conflicts with the Phase 15.1 RT-safety baseline.

**Strengths**

- The phase boundary is well-scoped: encode/send only, receive/render/package deferred.
- The HYBRID model matches NinjamZap's proven semantic split: audio thread owns interval framing; encoder thread owns video frames.
- Path B is the right kind of escape hatch philosophically: bounded, explicit, and tied to real sync scenarios.
- The wire-format details are concrete enough for downstream plans: `H264`, 24-byte marker, SPS/PPS chunk, BE frame lengths.
- Research correctly flags the two subtle integration risks around `LocalChannelMirror` and SPS/PPS pointer lifetime.

**Concerns**

- **HIGH: D-08 / WIRE-03 challenges the actual queue implementation.**
  `m_rawdata_sendq` is a `jamwide::SpscRing<RawDataItem, 64>` ([src/core/njclient.h](src/core/njclient.h):976). The ring contract explicitly allows only one producer ([src/threading/spsc_ring.h](src/threading/spsc_ring.h):21), and `try_push()` uses a relaxed load/store of `head_` with no CAS ([src/threading/spsc_ring.h](src/threading/spsc_ring.h):51). D-08 has at least two producers: audio thread and encoder thread. That is not MPSC-safe and can corrupt/drop queue entries under exactly the concurrent audio+video case WIRE-03 is meant to prove.

- **HIGH: D-08 puts non-RT work on the audio thread.**
  `RawDataSendBegin()` calls `WDL_RNG_bytes()` ([src/core/njclient.cpp](src/core/njclient.cpp):2994). `RawDataSendWrite()` allocates `new WDL_HeapBuf`, resizes it, copies payload bytes, and may `writeLog()` on failure ([src/core/njclient.cpp](src/core/njclient.cpp):3021). Since D-08 calls these from `on_new_interval` for END/BEGIN/marker/SPS-PPS, the "Path A lock-free audio path" claim is incomplete: it is lock-free for `m_video_active`, but not allocation-free or logging-free.

- **HIGH: D-08 has a likely `m_video_guid` race.**
  NinjamZap's `RawDataSendBegin(m_video_guid, ...)` mutates the GUID at each interval, and `QueueVideoFrame()` uses `m_video_guid` from the encoder side (ninjamzap-core `njclient.cpp`:2047). Porting this literally means audio thread writes the 16-byte GUID while encoder thread reads it for frame chunks. Making `m_video_interval_open` atomic is not enough; the GUID itself needs a safe publication/snapshot strategy.

- **HIGH: D-10 scenario names need correction before planning.**
  Directory listing verified. These exist: `02_video_one_interval_early.cpp`, `03_late_join.cpp`, `13_sps_pps_mid_stream.cpp`, `20_drop_resync_recovery.cpp`, `22_audio_then_video.cpp`, `25_no_initial_spspps.cpp`, `26_send_buffer_pressure.cpp`. These do NOT exist: `04_late_join_midstream.cpp`, `06_audio_video_resync.cpp`. D-10 is reconcilable, but only if CONTEXT or PLAN uses actual filenames.

- **MEDIUM: Pitfall #1 fix is directionally sound but underspecified.**
  Adding `curwritefile_guid[16]` to `LocalChannelMirror` is the right RT-safe alternative to reading canonical `Local_Channel*`; the mirror already tracks audio-thread-owned broadcast state ([src/core/njclient.h](src/core/njclient.h):155). But the plan must specify exactly where the audio thread writes the new GUID when `m_curwritefile.guid` is generated on the run-thread path today ([src/core/njclient.cpp](src/core/njclient.cpp):2606). Otherwise the marker may carry stale or zero GUIDs.

- **MEDIUM: D-15 one-frame IDR drift is probably acceptable only if tested against short intervals.**
  At normal intervals, 33-100 ms drift is fine. At extreme intervals, it can make the first frame of an interval non-IDR. NinjamZap has `18_extreme_short_intervals.cpp` for this class of stress. Add it as a diagnostic gate, not necessarily a hard zero-drop gate.

- **MEDIUM: D-16 bandwidth is plausible, but the queue capacity rationale is stale.**
  The existing comment assumes "2-4 RawData items/sec" ([src/core/njclient.h](src/core/njclient.h):971). Phase 20 adds 10-30 frame writes/sec plus interval control writes. Capacity 64 may still be enough if the run thread drains every ~20 ms, but the rationale must be recalculated and tested with `26_send_buffer_pressure.cpp`.

**Suggestions**

- **CONTEXT.md edit:** Amend D-08/D-12 to say Phase 20 must first make the RawData producer side genuinely multi-producer-safe, or must split it into separate single-producer queues with deterministic run-thread merge ordering. Do not rely on the current `SpscRing` for audio thread + encoder thread producers.

- **CONTEXT.md edit:** Add an RT-safe audio-thread enqueue requirement: no heap allocation, no `writeLog`, no RNG, no mutex in Path A. `RawDataSendBegin/Write` as currently implemented are not acceptable audio-thread APIs.

- **CONTEXT.md edit:** Add a safe video interval GUID publication decision. Options: pre-generate interval GUID records off the audio thread, publish GUID snapshots atomically/seqlock-style, or move BEGIN construction to the run thread from an audio-thread control event. The encoder must use a stable GUID snapshot per frame.

- **CONTEXT.md edit:** Correct D-10 scenario names to actual files. I recommend hard-gating Path A on `02_video_one_interval_early.cpp`, `03_late_join.cpp`, `13_sps_pps_mid_stream.cpp`, `20_drop_resync_recovery.cpp`, and `22_audio_then_video.cpp`; use `18_extreme_short_intervals.cpp` and `26_send_buffer_pressure.cpp` as stress diagnostics.

- **RESEARCH.md edit:** Downgrade the "Phase 14.3 substrate already resolved any-thread producer" claim. The comments claim this, but the implementation is SPSC. This should be called a blocking substrate mismatch, not accepted as shipped fact.

- **RESEARCH.md edit:** Expand Pitfall #2 into a required generation-gated lifetime protocol. "Copy within call" alone does not solve UAF if the copied bytes are read from a freed pointer; the old `SpsPpsBuffer*` must not be freed until at least one audio generation after the swap.

- **Plan-level concern:** Plan 20-02 should start with a Wave 0 concurrency proof: TSan test with simultaneous audio-thread control writes and encoder-thread frame writes. This should fail on the current SPSC substrate and pass only after the producer-side architecture is fixed.

- **Plan-level concern:** Add objective RT budget instrumentation around the video block and assert worst-case duration, overflow count, discard count, and audio glitch observations for all three presets.

**Risk Assessment**

Overall risk: **HIGH until D-08/WIRE-03 is corrected**, then **MEDIUM**. The codec and wire format are manageable porting work. The real risk is that the architecture assumes "any-thread" semantics that the actual queue implementation does not provide, while also routing allocating send calls through `on_new_interval`. Fix those before writing implementation plans; otherwise Phase 20 is likely to pass simple unit tests and then fail under TSan or populated-session UAT.

---

## Consensus Summary

Only one reviewer (codex) was invoked — claude was self-excluded (running inside Claude Code); other CLIs (gemini, opencode, qwen, cursor, coderabbit) were not installed.

### Agreed Strengths (codex single-reviewer view)
- Phase boundary well-scoped (encode/send only).
- HYBRID model matches NinjamZap's proven semantic split.
- Path B fallback architecture is the right kind of escape hatch.
- Wire-format details are concrete enough for plans.

### Agreed Concerns (codex single-reviewer view, but every HIGH is independently verifiable)

**4 HIGH findings, all blocking PLAN.md generation:**

1. **H1 — Substrate is SPSC, not MPSC.** D-08's HYBRID model puts two producers (audio + encoder threads) into `m_rawdata_sendq`, which is a `SpscRing<RawDataItem, 64>` with single-producer-only contract and no CAS on `try_push`. The "any thread producer" claim in Phase 14.3-02 SUMMARY is semantically misleading vs. the actual implementation. **Blocks D-08 / D-12 / WIRE-03 as written.**

2. **H2 — `RawDataSendBegin/Write` are not RT-safe.** They call `WDL_RNG_bytes()`, allocate `new WDL_HeapBuf`, copy payloads, and may `writeLog()`. Calling them from `on_new_interval` violates Phase 15.1 D-01 (no allocation, no logging on audio thread). **Blocks D-08 audio-thread emission as currently architected.**

3. **H3 — `m_video_guid` race.** Audio thread writes the 16-byte interval GUID at each interval (via `RawDataSendBegin`'s internal `WDL_RNG_bytes`); encoder thread reads `m_video_guid` for each frame's `RawDataSendWrite` call. Not protected by anything in Path A's atomics design. **Requires explicit GUID publication/snapshot decision.**

4. **H4 — D-10 scenario filenames are wrong.** Verified by directory listing: `04_late_join_midstream.cpp` and `06_audio_video_resync.cpp` do NOT exist. Path B trigger is unenforceable until D-10 is amended. **Blocks D-10 trigger objectivity.**

### Divergent Views
N/A — single reviewer.

### Implications for Planning

The current CONTEXT.md cannot be the basis for PLAN.md as-is. Four CONTEXT.md edits are required before `/gsd-plan-phase 20` should proceed:
1. Amend D-08 / D-12 to address the SPSC-vs-MPSC substrate mismatch (route choice).
2. Add an RT-safety constraint on audio-thread enqueue paths (no alloc, no log, no RNG).
3. Add a video-interval GUID publication decision (off-audio-thread generation or atomic snapshot).
4. Correct D-10 scenario filenames to actual files in the NinjamZap test suite.

RESEARCH.md also needs two corrections:
- Downgrade the substrate's "any-thread producer" claim from "shipped fact" to "blocking substrate mismatch."
- Expand Pitfall #2 into a generation-gated lifetime protocol; copy-within-call alone is insufficient.

**Three of the four HIGH findings (H1, H2, H3) trace to the same root cause:** the Phase 14.3-02 substrate was designed and tested for the audio-only producer pattern that existed at the time. Adding a second producer (encoder thread) was never validated under TSan against the substrate. This is exactly the kind of "non-obvious invariant silently broken when a feature gets added" pattern recorded in the user's `feedback_legacy_invariant_audit` memory.
