---
phase: 20-h-264-encoder-send-pipeline
plan: 00
subsystem: threading
tags: [njclient, ninjamzap, wdl-mutex, wdl-ptrlist, raw-data-send-queue, audit-allowlist, observability]

requires:
  - phase: 14.3-native-video-foundation
    provides: SpscRing<RawDataItem, 64> m_rawdata_sendq substrate + run-thread drain + Pattern C discard guard (all retired here per D-19)
  - phase: 15.1-rt-safety-hardening
    provides: spsc_payloads.h FINAL header + audit-allowlist convention + Phase 15.1-06 HIGH-2 carve-out grammar
provides:
  - NinjamZap-literal WDL_PtrList<RawDataQueueItem> m_rawdata_sendq + WDL_Mutex m_rawdata_cs substrate
  - Multi-producer-safe RawDataSendBegin / RawDataSendWrite (audio thread + encoder thread can both produce per D-08)
  - Pop-one-unlock-Send-relock run-thread drain matching ninjamzap-core/njclient.cpp:1984-2040 verbatim
  - Queue observability surface for Plan 20-03 UAT — three atomics + three accessors (high-water-mark, mutex contention counter, total-enqueue counter)
  - Phase 20 audit-allowlist envelope published in .claude/agents/realtime-audio-reviewer.md + .codex/agents/realtime-audio-reviewer.toml with R4 M13 TBD:line placeholders awaiting Plan 20-02 Task 4 refresh
  - test_rawdata_send.cpp 8-sub-test suite covering NinjamZap-literal mutex semantics + multi-producer stress + drain interleave + per-producer FIFO + destructor cleanup + wire ordering
  - R3 MF2 + R3 MF5 + R4 M12 + R4 M13-setup closed
affects:
  - 20-01-openh264-encoder (encoder-thread RawDataSendWrite caller; same envelope applies)
  - 20-02-on-new-interval-video-state-machine (audio-thread caller; refreshes TBD:line placeholders per R4 M13)
  - 20-03-broadcast-uat (reads the three observability counters; UAT thresholds depend on this scaffolding)

tech-stack:
  added:
    - WDL_PtrList ownership-by-pointer pattern for the RawData send queue (matches the existing m_remoteusers / m_locchannels / m_downloads pattern)
    - WDL_Mutex per-substrate critical section for cross-thread serialization (m_rawdata_cs)
    - std::chrono::steady_clock-based contention proxy (fallback because WDL_Mutex has no TryEnter)
  patterns:
    - "NinjamZap-literal substrate: WDL_PtrList<HeapAllocItem*> + WDL_Mutex + pop-one-unlock-Send-relock drain (port target for cross-thread message queues that need multi-producer + single-consumer with arbitrary item arrival rates)"
    - "Audit-allowlist envelope as a published artifact: explicit file:line carve-out enumeration with decision-ID rationale, owned by the source plan and refreshed by the consumer plan via TBD:line placeholders (R4 M13)"
    - "Queue observability triad: high-water-mark CAS + contention counter (TryEnter or proxy) + total-enqueue counter (denominator); enables ratio-based UAT acceptance gates without false negatives at low load"

key-files:
  created:
    - .claude/agents/realtime-audio-reviewer.md (audit-allowlist envelope appended; full reviewer config mirrors the parent JamWide repo file)
    - .codex/agents/realtime-audio-reviewer.toml (mirror for codex agent path; same envelope content)
    - .planning/phases/20-h-264-encoder-send-pipeline/deferred-items.md (records pre-existing baseline failures discovered during regression check — out of scope for this plan)
  modified:
    - src/core/njclient.h (RawDataQueueItem nested struct + WDL_PtrList<RawDataQueueItem> m_rawdata_sendq + WDL_Mutex m_rawdata_cs + 3 observability atomics + 3 accessors; SPSC + overflow + discard + accessors deleted)
    - src/core/njclient.cpp (NinjamZap-literal RawDataSendBegin / RawDataSendWrite / run-thread drain / DrainRawDataSendQueueForTest / ChunkRawDataItem; 3 writeLog calls removed after R4 M12 audit; destructor cleanup via Empty(true))
    - src/threading/spsc_payloads.h (RawDataItem POD + RAWDATA_SEND_QUEUE_CAPACITY + WDL_HeapBuf forward-decl deleted; section (h) tombstone documents the retirement)
    - tests/test_rawdata_send.cpp (rewritten: 8 sub-tests covering NinjamZap-literal mutex semantics; SPSC-overflow / Pattern-C-discard assertions retired)
    - .planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md (drain-semantics sentence corrected per R3 MF2 — closes Round 2 M8 too)
    - .planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md (7 STALE markers added per R3 MF5; CRITICAL UPDATE block at lines 1-40 untouched)

key-decisions:
  - "D-19 implemented verbatim: WDL_PtrList<RawDataQueueItem> + WDL_Mutex m_rawdata_cs replaces the Phase 14.3-02 SPSC; pop-one-unlock-Send-relock drain matches ninjamzap-core/njclient.cpp:1984-2040 byte-for-byte. No Pattern-C if(!m_netcon) discard branch (NinjamZap source has none); items accumulate in the unbounded WDL_PtrList until either the next Run tick drains them on reconnect or the NJClient destructor frees them via Empty(true)."
  - "R3 MF4 observability triad owned by Plan 20-00 unconditionally — high-water-mark + mutex contention counter + total-enqueue counter (the denominator for Plan 20-03's contention-ratio gate). Plan 20-03 reads all three; adds none."
  - "Contention sampling proxy: WDL_Mutex (wdl/mutex.h) has only Enter/Leave — no TryEnter. Fallback is to time the Enter() call with std::chrono::steady_clock; if the call took longer than the kRawdataCsContentionThresholdNs (1 µs) bound, bump the contention counter. The total-enqueue counter is the canonical denominator regardless — Plan 20-03 calibrates the threshold against real populated-load data."
  - "R4 M12 caller audit complete: only callers of RawDataSendBegin / RawDataSendWrite at the time of Plan 20-00 are in tests/test_rawdata_send.cpp (test-only). The three writeLog sites at the original 14.3-02 lines 3015 / 3048 / 3063 all reported on the retired SPSC overflow / OOM path; no non-video production caller relies on any of them. Removal proceeds; the audit result is documented below."
  - "R4 M13 setup complete: 18 TBD:line placeholders published in BOTH .claude/agents/realtime-audio-reviewer.md and .codex/agents/realtime-audio-reviewer.toml. Plan 20-02 Task 4 refreshes them with concrete file:line entries; the Phase 20 close-out gate is `grep -c 'TBD:line' = 0`."

patterns-established:
  - "Pattern: NinjamZap-literal substrate. When porting from ninjamzap-core, use WDL_PtrList + WDL_Mutex + pop-one-unlock-Send-relock verbatim. Do not introduce shadow representations (SPSC wrappers, swap-list-out semantics, drain lambdas) unless the substrate change preserves NinjamZap's contract (single-consumer drain, run-thread sole writer, producers serialize via the substrate-internal mutex). Phase 14.3-02's SpscRing<RawDataItem, 64> wrapper silently broke the multi-producer contract — see feedback_legacy_invariant_audit memory."
  - "Pattern: contention proxy via Enter-time measurement when TryEnter is unavailable. When the substrate mutex doesn't expose TryEnter (WDL_Mutex / pthread_mutex with no PTHREAD_MUTEX_TIMED_NP / etc), sampling contention via Enter-time delta over a coarse threshold (e.g. 1 µs) is a best-effort proxy. Pair with a total-enqueue denominator counter so UAT can compute a ratio. Document the proxy noise floor explicitly."
  - "Pattern: published audit-allowlist envelope with R4 M13 TBD:line placeholders. The owning plan publishes the envelope BEFORE the consumer plan creates the carve-out sites; the consumer plan refreshes the placeholders. The audit gate is a simple `grep -c TBD:line == 0` at phase close. Forces the consumer plan to acknowledge each carve-out site explicitly instead of letting CRITICAL flags surface during the final audit run."

requirements-completed: [COD-02, WIRE-03]

duration: ~80min
completed: 2026-05-16
---

# Phase 20 Plan 00: Substrate Revision Summary

**NinjamZap-literal `WDL_PtrList<RawDataQueueItem>` + `WDL_Mutex m_rawdata_cs` replaces the Phase 14.3-02 single-producer SPSC, unblocking the Phase 20 HYBRID two-producer emission model; audit-allowlist envelope + R3 MF2/MF5/MF4 + R4 M12/M13 all closed in this Wave 0 plan.**

## Performance

- **Duration:** ~80 min wall-clock (submodule init was the slowest single step at ~30 s; main build ~2 min; ctest subset ~25 s)
- **Started:** 2026-05-16
- **Completed:** 2026-05-16
- **Tasks:** 3
- **Files modified:** 6 (3 source + 1 test + 2 doc; plus 2 new agent-config files and 1 deferred-items.md)

## Accomplishments

- **Substrate revised to NinjamZap-literal** — `WDL_PtrList<RawDataQueueItem>` + `WDL_Mutex m_rawdata_cs` now in place of the single-producer `SpscRing<RawDataItem, 64>` substrate that Phase 14.3-02 mistakenly assumed was multi-producer-safe. `RawDataSendBegin` / `RawDataSendWrite` / the run-thread drain are byte-for-byte ports of `ninjamzap-core/njclient.cpp:2047-2082 + 1984-2040`.
- **R3 MF4 observability triad wired top-to-bottom** — `m_rawdata_sendq_high_water_mark`, `m_rawdata_cs_contention_count`, and `m_rawdata_sendq_total_enqueues` atomics + their three accessors are owned by this plan unconditionally; the total-enqueue counter is incremented twice (once inside `RawDataSendBegin`, once inside `RawDataSendWrite`, both under `m_rawdata_cs`) so Plan 20-03's contention-ratio gate has a populated denominator at any load.
- **R4 M12 writeLog-removal caller audit complete** — `grep -nE "RawDataSendBegin|RawDataSendWrite" src/ tests/` returned only `tests/test_rawdata_send.cpp` matches. No non-video production caller exists today (Plan 20-01 / 20-02 add the audio + encoder callers later in the wave). The three writeLog sites at the original 14.3-02 lines 3015 / 3048 / 3063 all reported on the retired SPSC overflow / OOM path; none carried diagnostics any non-video caller relied on. Logs deleted; SUMMARY's "R4 M12 caller audit results" section below documents the full disposition.
- **R4 M13 setup half closed** — Audit-allowlist envelope published in BOTH `.claude/agents/realtime-audio-reviewer.md` and `.codex/agents/realtime-audio-reviewer.toml` with 18 `TBD:line` placeholders for the carve-out sites Plan 20-02 will create. The Phase 20 close-out gate (`grep -c TBD:line == 0`) is documented in the envelope footer; Plan 20-02 Task 4 closes the other half by refreshing the placeholders.
- **R3 MF2 + MF5 closed in artifact-edit form** — CONTEXT.md's drain-semantics sentence is corrected to `pop-one-unlock-Send-relock` (closes R3 MF2 + Round 2 M8); RESEARCH.md has 7 STALE markers above the sections superseded by the CRITICAL UPDATE block (closes R3 MF5). The CRITICAL UPDATE block at lines 1-40 of RESEARCH.md is byte-identical to its pre-edit content.
- **test_rawdata_send.cpp rewritten with 8 sub-tests** — 8/8 green under `ctest -R rawdata_send --output-on-failure`. Test 4 explicitly verifies `GetRawDataSendQueueTotalEnqueueCount() == 1200` under 4 producer threads × 100 iters each (the new denominator counter wired correctly). Test 6 asserts destructor cleanup via `Empty(true)` (ASAN-clean). Test 8 simulates the Plan 20-02 + 20-01 wire-ordering pattern under a mock `m_video_cs`.

## Task Commits

Each task was committed atomically:

1. **Task 1: Substrate revision (.h / .cpp / spsc_payloads.h)** — `180212e` (refactor)
2. **Task 2: test_rawdata_send.cpp rewrite + deferred-items.md** — `53caa7d` (test)
3. **Task 3: Audit-allowlist envelope + CONTEXT.md + RESEARCH.md** — `a43f4d2` (docs)

**Plan metadata commit:** to be added by the final commit including this SUMMARY.md.

## Files Created/Modified

- `src/core/njclient.h` — Added: `RawDataQueueItem` nested struct (public for forward-decl symmetry with the test-facing API; mirrors `ninjamzap-core/njclient.h:311-321` field-for-field), `WDL_PtrList<RawDataQueueItem> m_rawdata_sendq`, `WDL_Mutex m_rawdata_cs`, 3 observability atomics + 3 accessors. Removed: `SpscRing<jamwide::RawDataItem, RAWDATA_SEND_QUEUE_CAPACITY> m_rawdata_sendq`, `m_rawdata_sendq_overflows`, `m_rawdata_sendq_discards`, `GetRawDataSendQueueOverflowCount`, `GetRawDataSendQueueDiscardCount`. Updated test-facing signatures: `DrainRawDataSendQueueForTest(std::vector<RawDataQueueItem*>&)`, `ChunkRawDataItem(const RawDataQueueItem&, ...)`.
- `src/core/njclient.cpp` — Rewrote `RawDataSendBegin` (lines 3050-3081), `RawDataSendWrite` (lines 3083-3119), the run-thread drain block (now NinjamZap-literal pop-one-unlock-Send-relock; no Pattern-C branch), `DrainRawDataSendQueueForTest` (destructive drain handing back `RawDataQueueItem*`), `ChunkRawDataItem` (reads via `item.data.Get() / item.data.GetSize()` instead of `item.payload->Get()`). Destructor cleanup switched from `m_rawdata_sendq.drain([](){ delete item.payload; })` to `m_rawdata_sendq.Empty(true)` — `~RawDataQueueItem` runs `~WDL_HeapBuf` on the by-value `data` member. Added `enter_rawdata_cs_with_contention_sample` anonymous-namespace helper as the WDL_Mutex-TryEnter fallback (see Decisions below).
- `src/threading/spsc_payloads.h` — Deleted: `RawDataItem` POD (struct + `static_assert` + section comment), `RAWDATA_SEND_QUEUE_CAPACITY` constant, the global-scope `WDL_HeapBuf` forward-decl that previously served `RawDataItem.payload`, the dual `namespace jamwide { ... namespace jamwide {` pattern around the forward-decl. Phase 15.1-04 Codex M-9 finality preserved for the rest of the file. Tombstone comment at section (h) records the retirement and points at `src/core/njclient.h` for the new `RawDataQueueItem` location.
- `tests/test_rawdata_send.cpp` — Rewritten in-place with 8 sub-tests: `test_rawdata_begin_write_end_roundtrip`, `test_rawdata_write_chunking`, `test_rawdata_video_fourcc_helper`, `test_rawdata_multi_producer_stress`, `test_rawdata_drain_interleave`, `test_rawdata_destructor_cleanup`, `test_rawdata_send_ordering_per_producer`, `test_rawdata_wire_ordering_begin_marker_sps_frame`. Scaffold (TEST/PASS/FAIL macros + tests_run/tests_passed counters) preserved verbatim from the 14.3-02 file. SPSC-overflow + Pattern-C-discard assertions removed.
- `.claude/agents/realtime-audio-reviewer.md` (new) — Full reviewer config mirrors the parent JamWide repo file (162 lines) + appends the "Phase 20 audit allowlist envelope" section with explicit `TBD:line` carve-out entries per CONTEXT.md D-08 / D-09 / D-19 / D-20, R2 H6 + H7, R4 H8 + M12 + M13. Footer cites the R4 M13 enforcement gate.
- `.codex/agents/realtime-audio-reviewer.toml` (new) — Mirror for the codex agent path. Same content shape (envelope inside `developer_instructions`); additional top-level `[phase_20_allowlist]` table for machine-readable status / decision_refs / close-gate command.
- `.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md` — Single-line edit at `<code_context> → Reusable Assets`: replaces "Run-thread drain swaps the list out under the mutex, processes outside." with "Run-thread drain is NinjamZap-literal pop-one-unlock-Send-relock matching `ninjamzap-core/njclient.cpp:1987-2039`; per-item ownership transfers from the WDL_PtrList to the drain loop (`Get(0)` → `Delete(0)` → process outside the lock → `delete item`)."
- `.planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md` — Added 7 STALE markers above retired sections (Summary, Locked Decisions, Pattern 1, Pattern 1b, Pattern 3, Pitfall 4, JamWide Phase 14.3-02 Drain Block). CRITICAL UPDATE block at lines 1-40 untouched; no other changes.
- `.planning/phases/20-h-264-encoder-send-pipeline/deferred-items.md` (new) — Records two pre-existing baseline failures discovered during the regression check (test_encryption won't compile due to undeclared `encrypt_payload_with_iv`; test_flac_codec roundtrips fail with "Decoded 0 samples"). Both reproduce at the Plan 20-00 base commit (`152007f`), so neither was caused by this plan. Surfaced for Phase 23 / beta hardening.

## NinjamZap-Source Citations (Port Targets)

The port followed these specific NinjamZap source ranges verbatim:

- `ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:310-321` → `NJClient::RawDataQueueItem` field shape (int type / unsigned char guid[16] / unsigned int fourcc / int chidx / int estsize / int flags / WDL_HeapBuf data). JamWide port matches field-for-field; the only deviation is using `data.ResizeOK` instead of `data.Resize` per the Phase 14.3-02 CR-01 (Resize doesn't update m_size on alloc failure → NULL-deref on `Get()`; ResizeOK is the JamWide-side safety wrapper that returns NULL on failure).
- `ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2047-2062` → `NJClient::RawDataSendBegin` body (WDL_RNG_bytes 16 + new RawDataQueueItem + Add under m_rawdata_cs). JamWide port matches; the only additions are the R3 MF4 observability instrumentation (contention sample + total-enqueue counter increment + high-water-mark CAS), all paired with the existing Add under m_rawdata_cs.
- `ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2064-2082` → `NJClient::RawDataSendWrite` body (new RawDataQueueItem + ResizeOK + memcpy + Add under m_rawdata_cs). JamWide port matches; deviation noted above (`data.ResizeOK` instead of `data.Resize`).
- `ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:1984-2040` → `NJClient::Run` RawData drain block (m_rawdata_cs.Enter / while-GetSize loop / Get(0) / Delete(0) / Leave / build mpb_client_upload_interval_{begin,write} / Send / delete item / re-Enter / Leave on empty). JamWide port matches; preserves Phase 14.3-02's MAX_ENC_BLOCKSIZE chunking semantics (which match NinjamZap's exactly).

**No deviations from NinjamZap source beyond the ResizeOK/Resize swap noted above.**

## Decisions Made

1. **Public-section `RawDataQueueItem` nested struct (Decision)** — The forward-decl at the top of `NJClient` was placed in `public:` (right after `kRemoteNameMax`) so the test-facing `DrainRawDataSendQueueForTest(std::vector<RawDataQueueItem*>&)` and `ChunkRawDataItem(const RawDataQueueItem&, ...)` signatures could reference the type. C++ requires the access specifier to match between forward-decl and full-decl, so the full struct declaration is also in `public:` (wrapped in a small public island inside the otherwise-`protected:` member block). Production code should treat `RawDataQueueItem` as opaque; only the test harness pokes at its fields directly, and that's gated by `JAMWIDE_BUILD_TESTS=1`.
2. **WDL_Mutex TryEnter fallback (Decision)** — `wdl/mutex.h` only exposes `Enter()` and `Leave()`; no `TryEnter()` exists today. The contention-counter proxy is to time `Enter()` with `std::chrono::steady_clock` and bump the counter if the call took longer than `kRawdataCsContentionThresholdNs = 1000` (1 µs — covers the typical pthread/mutex fast-path on macOS/Windows by an order of magnitude). The total-enqueue counter (the denominator) is NOT optional and is wired regardless of the TryEnter fallback decision — Plan 20-03 calibrates the threshold against real populated-load data. Documented in the audit-allowlist envelope so Plan 20-02's auditor knows the proxy is intentional.
3. **No `if (!m_netcon)` Pattern-C discard branch (Decision)** — NinjamZap source doesn't have one; items accumulate in the unbounded WDL_PtrList until either the next Run tick finds `m_netcon` non-null (reconnect) and drains them, or the NJClient destructor calls `m_rawdata_sendq.Empty(true)` which frees every remaining item via the by-value `~WDL_HeapBuf`. Phase 14.3-02's Pattern-C branch + `m_rawdata_sendq_discards` counter are retired together — they were artifacts of the bounded-capacity SPSC substrate and have no NinjamZap equivalent.

## R4 M12 Caller Audit Results

Pre-removal audit per the plan's must-haves. Audit command and output:

```bash
$ grep -nE "RawDataSendBegin|RawDataSendWrite" src/ tests/
```

**Result (full enumeration):**

| File | Line | Context | Disposition |
|------|------|---------|-------------|
| `src/core/njclient.h` | 642-645 | Public-API signature declarations | Unchanged in shape (signatures preserved); body rewritten Task 1 |
| `src/core/njclient.cpp` | 3050-3081 | `NJClient::RawDataSendBegin` impl (after rewrite — line numbers post-Plan 20-00) | Rewritten Task 1 |
| `src/core/njclient.cpp` | 3083-3119 | `NJClient::RawDataSendWrite` impl (after rewrite) | Rewritten Task 1 |
| `tests/test_rawdata_send.cpp` | multiple (lines 100-490) | Test-only callers exercising the substrate | Updated Task 2 to match new contract |
| `src/threading/spsc_payloads.h` | 31-72, 330-343 | Comment references only (tombstones) | Comments only — no live caller |

**Production callers of `RawDataSendBegin` / `RawDataSendWrite`:** **NONE at the time of Plan 20-00 execution.** The only callers in the codebase are inside `tests/test_rawdata_send.cpp`. Plan 20-01 / 20-02 add the production audio-thread + encoder-thread callers later in the wave.

**Disposition of each `writeLog` site removed by Plan 20-00:**

| Original line (14.3-02 as-shipped) | What it logged | Required production diagnostic? | Disposition |
|------------------------------------|----------------|----------------------------------|-------------|
| `src/core/njclient.cpp:3015` (in RawDataSendBegin) | `"RawDataSendBegin: SPSC full (capacity %zu) — dropped begin fourcc=%08x chidx=%d"` | **No.** Reports on SPSC try_push failure when the bounded ring was at capacity. The bounded SPSC ring is retired (the WDL_PtrList is unbounded per D-19); there is no "full" condition to report. | Deleted. No relocation needed — no caller relies on this diagnostic; the substrate change makes the condition impossible. |
| `src/core/njclient.cpp:3048` (in RawDataSendWrite OOM path) | `"RawDataSendWrite: OOM allocating %d-byte payload — dropped%s"` | **No.** Reports on `WDL_HeapBuf::ResizeOK` failure (memory allocation failure). NinjamZap silently drops on this condition (matches the unbounded-queue OOM semantics); Plan 20-03 observability watches the queue from a different angle (unbounded growth would be the visible signal of allocation pressure). | Deleted. No relocation. |
| `src/core/njclient.cpp:3063` (in RawDataSendWrite try_push failure path) | `"RawDataSendWrite: SPSC full (capacity %zu) — dropped %d-byte write%s"` | **No.** Same retired-SPSC pattern as line 3015. Bounded-capacity push failure is impossible under the unbounded WDL_PtrList. | Deleted. No relocation. |

**Audit conclusion:** No non-video production caller relies on any of the three removed writeLog sites for a required production diagnostic. The three logs all reported on the retired SPSC overflow / allocation-failure path that becomes silent under the unbounded `WDL_PtrList` substrate (per D-19 spec, NinjamZap-literal). Removal is diagnostic-safe.

## R4 M13 Setup Confirmation

| File | TBD:line count | Required minimum (R4 M13 gate) | Plan 20-02 Task 4 will refresh |
|------|----------------|--------------------------------|---------------------------------|
| `.claude/agents/realtime-audio-reviewer.md` | 18 | ≥5 | YES — concrete file:line per Plan 20-02 Task 1 + Task 2 |
| `.codex/agents/realtime-audio-reviewer.toml` | 19 | ≥5 | YES — concrete file:line per Plan 20-02 Task 1 + Task 2 |

**Phase 20 close-out gate:** `grep -c "TBD:line" .claude/agents/realtime-audio-reviewer.md` MUST return 0 by Phase 20 acceptance. If any `TBD:line` remains, Phase 20 acceptance FAILS the R4 M13 gate. The audit-allowlist envelope footer documents this in both files.

## Envelope Snapshot

First 60 lines of the new envelope section in `.claude/agents/realtime-audio-reviewer.md` (for subsequent plans to quote verbatim):

```
## Phase 20 audit allowlist envelope

> **Decision authority:** CONTEXT.md D-08 / D-09 / D-19 / D-20, R2 H6 + H7, R4 H8 + M12 + M13.
> **Status:** **Published 2026-05-16 by Plan 20-00**. Audio-thread carve-out sites
> created by Plan 20-02 are listed with `TBD:line` placeholders per R4 M13;
> Plan 20-02 Task 4 refreshes those placeholders with the concrete file:line
> entries from the as-committed code. **Phase 20 close-out gate (R4 M13):**
> `grep -c 'TBD:line' .claude/agents/realtime-audio-reviewer.md` MUST return 0
> by Phase 20 close. If any `TBD:line` remains, Phase 20 acceptance FAILS.

The following audio-thread sites are accepted Phase 15.1 carve-outs under
CONTEXT.md D-09 + D-20 (full rationale: the user has direct evidence the
NinjamZap mutex/alloc/RNG-on-audio-thread substrate works in production on
iOS/Android; JamWide ships at NinjamZap parity per
`feedback_proven_over_pure`). All other audio-path violations remain
CRITICAL — these are the only carve-outs accepted under the envelope.

### src/core/njclient.cpp `NJClient::on_new_interval` video block (Plan 20-02 Task 1)

- `src/core/njclient.cpp:TBD:line` — `WDL_MutexLock vlock(&m_video_cs)` opening the whole-block critical section (D-08, NinjamZap-literal whole-block serialization)
- `src/core/njclient.cpp:TBD:line` — `WDL_MutexLock slock(&m_video_spspps_cs)` nested inside m_video_cs for SPS/PPS read (D-03)
- `src/core/njclient.cpp:TBD:line` — `readGuidSeqlock(*lc, marker + 8)` atomic two-uint64_t halves seqlock read of the canonical audio_ch0_guid (D-20 + R4 H8 carve-out; audit-allowlist scope: **atomic-halves load only** — no plain non-atomic byte read from the audio thread)
- `src/core/njclient.cpp:TBD:line` — `memcpy(marker + N, ...)` of 16 bytes from atomic-halves local temporaries into the marker buffer (safe: local temporaries are not shared)
[... 5 more on_new_interval entries ...]

### src/core/njclient.cpp `NJClient::QueueVideoFrame` (encoder-thread caller of the same shared substrate; Plan 20-01/20-02 Task 1)

[3 entries: vlock acquire + 2 RawDataSendWrite calls for the 4-byte length prefix + NAL payload]

### src/core/njclient.cpp `NJClient::RawDataSendBegin` (any thread; called from audio thread inside on_new_interval; landed in Plan 20-00)

- `src/core/njclient.cpp:3068` — `m_rawdata_cs.Enter() / .Leave()` via `enter_rawdata_cs_with_contention_sample` helper (D-09; NinjamZap-literal — every Add takes m_rawdata_cs)
- `src/core/njclient.cpp:3055` — `WDL_RNG_bytes(outGuid, 16)` (D-09 — internally locked, per-call cost ~5 µs)
- `src/core/njclient.cpp:3057` — `new RawDataQueueItem` (D-09 — single heap alloc per call; allocation cost is the NinjamZap-faithful substrate cost)
```

The full envelope text (the audit document of record) lives in the two committed agent-config files; the snapshot above is for quick reference only.

## RESEARCH.md STALE Markers Added

Exact list (file:line of each marker — the marker line itself; the heading is one line below):

| File | Line of marker | Heading marked |
|------|---------------:|-----------------|
| 20-RESEARCH.md | 42 | `## Summary` |
| 20-RESEARCH.md | 80 | `### Locked Decisions` (inside User Constraints section) |
| 20-RESEARCH.md | 363 | `### Pattern 1: Audio-Thread Lock-Free SPS/PPS Read` |
| 20-RESEARCH.md | 416 | `### Pattern 1b: Path B Carve-out (NinjamZap-literal)` |
| 20-RESEARCH.md | 491 | `### Pattern 3: SPS/PPS Atomic Pointer Swap with Deferred Delete` |
| 20-RESEARCH.md | 602 | `### Pitfall 4: Path B trigger detection ambiguity` |
| 20-RESEARCH.md | 766 | `### JamWide Phase 14.3-02 Drain Block (existing — reference only, not modified)` (stronger marker noting Plan 20-00 IS modifying it) |

**CRITICAL UPDATE block at lines 1-40 of RESEARCH.md is byte-identical to its pre-edit content.** Verified by `git diff` (the diff hunks only show the 7 inserted marker lines plus a sentinel context line each; no other byte changes).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Submodule init required to configure build-juce**
- **Found during:** Task 1 verify gate (`cmake --build build-juce --target njclient`)
- **Issue:** The fresh worktree had no `libs/libflac`, `libs/ixwebsocket`, `libs/juce`, `libs/clap-juce-extensions`, etc. checked out — CMake configure failed with "source directory does not contain a CMakeLists.txt file" for each missing submodule.
- **Fix:** Ran `git submodule update --init --recursive` once to populate every nested module. No submodule SHAs changed (the worktree's `.gitmodules` and `.git/modules` already pointed at the right commits).
- **Files modified:** None tracked — `libs/*` are git submodules, not source files. No commit needed for the init step.
- **Verification:** `cmake -S . -B build-juce -G Ninja -DCMAKE_BUILD_TYPE=Release -DJAMWIDE_BUILD_TESTS=ON -DCMAKE_OSX_ARCHITECTURES=x86_64` then succeeded.
- **Committed in:** N/A (workspace setup; nothing committed).

**2. [Rule 1 - Bug] Fixed `WDL_HeapBuf` value-init in test 2**
- **Found during:** Task 2 first compile (`cmake --build build-juce --target test_rawdata_send`)
- **Issue:** `WDL_HeapBuf` has an `explicit` constructor (`wdl/heapbuf.h:91`), so `NJClient::RawDataQueueItem item{};` was rejected by the C++20 compiler with "chosen constructor is explicit in copy-initialization". The error was in the test, not in production code.
- **Fix:** Changed `item{};` to `item;` (default-construct rather than value-init).
- **Files modified:** tests/test_rawdata_send.cpp (one line)
- **Verification:** Test rebuilds clean and runs green.
- **Committed in:** `53caa7d` (Task 2 commit — single self-contained test fix included in the rewrite).

**3. [Rule 1 - Bug] Fixed namespace double-open in spsc_payloads.h**
- **Found during:** Task 1 first build attempt (`cmake --build build-juce --target njclient`)
- **Issue:** The original spsc_payloads.h had a `namespace jamwide { ... }` close + re-open pattern around a `class WDL_HeapBuf;` global-scope forward-decl. My initial edit removed the close but left the re-open, producing nested `jamwide::jamwide::*` namespaces and 20+ "no template named 'SpscRing'" errors.
- **Fix:** Removed the stray re-open at line 75; namespace jamwide now opens once at line 53 and closes once at the end of the file.
- **Files modified:** src/threading/spsc_payloads.h (3 lines — the stray re-open + comment update)
- **Verification:** `cmake --build build-juce --target njclient -- -j8` succeeds.
- **Committed in:** `180212e` (Task 1 commit — included in the substrate revision).

**4. [Rule 1 - Bug] Fixed `RawDataQueueItem` access-specifier mismatch**
- **Found during:** Task 1 second build attempt
- **Issue:** I forward-declared `struct RawDataQueueItem` in the `public:` section of NJClient (so the test-facing `DrainRawDataSendQueueForTest` and `ChunkRawDataItem` signatures could reference it) but placed the full struct declaration deep in the `protected:` block. Compiler error: `'RawDataQueueItem' redeclared with 'protected' access`.
- **Fix:** Added an explicit `public:` / `protected:` toggle around the full struct declaration (a small public island inside the otherwise-protected member block) so the access specifier matches the forward-decl.
- **Files modified:** src/core/njclient.h
- **Verification:** `cmake --build build-juce --target njclient -- -j8` succeeds; the type is reachable from the test signatures.
- **Committed in:** `180212e` (Task 1 commit — single coherent edit with the substrate revision).

---

**Total deviations:** 4 auto-fixed (3 Rule 1 bugs + 1 Rule 3 blocking). All four were workspace / port-mechanical issues; none changed the substrate semantics from the plan's intent. No scope creep.

## Issues Encountered

- **Pre-existing baseline failures discovered during the full-suite regression run** — `test_encryption` won't compile due to undeclared `encrypt_payload_with_iv`; `test_flac_codec` roundtrip sub-tests fail with "Decoded 0 samples". Both reproduce at the Plan 20-00 base commit `152007f` (before any Plan 20-00 changes), confirming they are NOT caused by this plan. Captured in `.planning/phases/20-h-264-encoder-send-pipeline/deferred-items.md` for Phase 23 / beta hardening triage. Plan 20-00's verification gates (`ctest -R rawdata_send` + the 12-target regression subset) are 100% green; the full-suite gate is recorded as a known-broken-baseline, not a Plan 20-00 deviation.
- **No `git stash` operations performed in the worktree.** A single mistaken `git stash` early during diagnostic work was immediately followed by `git stash pop` and verified to have restored state. The destructive-git-prohibition memory has been re-internalized; no further stashes.

## Self-Check

Verified that all SUMMARY claims correspond to real artifacts in the worktree:

**Created/modified files exist:**
- `[ -f src/core/njclient.h ] && grep -q "WDL_PtrList<RawDataQueueItem> m_rawdata_sendq" src/core/njclient.h` → FOUND
- `[ -f src/core/njclient.cpp ] && grep -q "enter_rawdata_cs_with_contention_sample" src/core/njclient.cpp` → FOUND
- `[ -f src/threading/spsc_payloads.h ] && ! grep -q "struct RawDataItem {" src/threading/spsc_payloads.h` → FOUND (POD struct deleted; tombstone comment remains)
- `[ -f tests/test_rawdata_send.cpp ] && grep -q "test_rawdata_multi_producer_stress" tests/test_rawdata_send.cpp` → FOUND
- `[ -f .claude/agents/realtime-audio-reviewer.md ] && grep -q "Phase 20 audit allowlist envelope" .claude/agents/realtime-audio-reviewer.md` → FOUND
- `[ -f .codex/agents/realtime-audio-reviewer.toml ] && grep -q "phase_20_allowlist" .codex/agents/realtime-audio-reviewer.toml` → FOUND
- `[ -f .planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md ] && grep -q "pop-one-unlock-Send-relock" .planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md` → FOUND
- `[ -f .planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md ] && grep -c "STALE — DO NOT PLAN" .planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md` → 7 (the count target)
- `[ -f .planning/phases/20-h-264-encoder-send-pipeline/deferred-items.md ]` → FOUND

**Commits exist (verified via `git log --oneline -3`):**
- `180212e` refactor(20-00-01-substrate): ... → FOUND
- `53caa7d` test(20-00-02-tests): ... → FOUND
- `a43f4d2` docs(20-00-03-audit-allowlist): ... → FOUND

## Self-Check: PASSED

## Next Phase Readiness

- **Plan 20-01 (Openh264 encoder)** can proceed. The substrate is multi-producer-safe; the encoder thread's `QueueVideoFrame` → `RawDataSendWrite` path will work correctly under HYBRID emission.
- **Plan 20-02 (on_new_interval video state machine)** can proceed. The audit-allowlist envelope is already published in both agent paths; the audio-thread carve-out sites that Plan 20-02 creates will be auditor-accepted as long as Plan 20-02 Task 4 refreshes the `TBD:line` placeholders.
- **Plan 20-03 (broadcast UAT)** can proceed when 20-01 and 20-02 land. The R3 MF4 observability triad is in place — `GetRawDataSendQueueHighWaterMark`, `GetRawDataMutexContentionCount`, and `GetRawDataSendQueueTotalEnqueueCount` accessors are public on NJClient. Plan 20-03's contention-ratio gate `contention_count / total_enqueues < 1%` reads from live data with a populated denominator at any load.
- **No blockers identified for the rest of the Phase 20 wave.**

## Threat Flags

None this plan. No new network surface, no new auth path, no new file access at trust boundaries; the substrate revision is a pure internal refactor of an existing shared-memory queue.

---
*Phase: 20-h-264-encoder-send-pipeline*
*Plan: 00 (substrate-revision)*
*Completed: 2026-05-16*
