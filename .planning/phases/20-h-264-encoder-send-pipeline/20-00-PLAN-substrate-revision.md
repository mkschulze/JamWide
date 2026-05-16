---
phase: 20
plan: 00
slug: substrate-revision
type: execute
wave: 0
depends_on: []
files_modified:
  - src/core/njclient.h
  - src/core/njclient.cpp
  - src/threading/spsc_payloads.h
  - tests/test_rawdata_send.cpp
  - .claude/agents/realtime-audio-reviewer.md
  - .codex/agents/realtime-audio-reviewer.toml
  - .planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md
  - .planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md
autonomous: true
requirements:
  - COD-02
  - WIRE-03
threat_refs:
  - T-20-00
  - T-20-SC
review_refs:
  - R3-MF2-context-drain-semantics
  - R3-MF5-research-staleness-markers

must_haves:
  truths:
    - "m_rawdata_sendq is a WDL_PtrList<RawDataQueueItem> protected by WDL_Mutex m_rawdata_cs, NinjamZap-literal per D-19"
    - "RawDataSendBegin and RawDataSendWrite each acquire m_rawdata_cs once per call, allocate a heap RawDataQueueItem (matching NinjamZap's struct exactly), and append to the WDL_PtrList; no SPSC try_push, no overflow counter, no Pattern C discard guard"
    - "Run-thread drain in NJClient::Run is NinjamZap-literal pop-one-unlock-Send-relock, matching ninjamzap-core/njclient.cpp:1987-2039; no swap-list-out semantics anywhere in the source"
    - "All three writeLog calls on the RawData send path (njclient.cpp lines around 3015, 3048, 3063 in the as-shipped 14.3-02 code) are removed; the audit-allowlist file:line entries in realtime-audio-reviewer.md explicitly state no writeLog calls remain on the audio-thread video send path"
    - "Audit allowlist envelope (D-09) is published in .claude/agents/realtime-audio-reviewer.md AND .codex/agents/realtime-audio-reviewer.toml — covers m_video_cs / m_video_spspps_cs / m_rawdata_cs mutex Enter on audio thread, WDL_RNG_bytes, new WDL_HeapBuf / WDL_HeapBuf::Resize / ResizeOK, marker+SPS/PPS memcpy on audio thread, and the Phase 15.1-06 HIGH-2 carve-out for direct m_locchans[0]->m_curwritefile.guid read at the marker construction site"
    - "tests/test_rawdata_send.cpp replaces SPSC overflow-counter assertions with mutex-correctness assertions: 2+ producer thread stress, run-thread drain interleaved with active producers, BEGIN/marker/SPS/frame wire-ordering, destructor cleanup of pending items, per-producer FIFO preservation"
    - "GetRawDataSendQueueOverflowCount() accessor and m_rawdata_sendq_overflows counter are deleted; m_rawdata_sendq_discards retained renamed-or-not (planner picks; the Pattern C path is also retired since the WDL_PtrList drains in NJClient::Run only when m_netcon is non-null, and the destructor frees remaining items via WDL_PtrList::Empty(true))"
    - "RAWDATA_SEND_QUEUE_CAPACITY constant in spsc_payloads.h is deleted (unbounded WDL_PtrList per D-19); RawDataItem POD type and SpscRing<RawDataItem, ...> static_assert are deleted (RawDataQueueItem is the new nested type living in njclient.h)"
    - "RawDataQueueItem nested in NJClient class (private), mirroring ninjamzap-core/njclient.h:311-321 exactly: int type, unsigned char guid[16], unsigned int fourcc, int chidx, int estsize, int flags, WDL_HeapBuf data; (not RawDataItem; not a POD; lives only inside NJClient)"
    - "Queue observability surface for Plan 20-03 (R3 MF4) is wired UNCONDITIONALLY in Plan 20-00: three atomics + three accessors live in njclient.h — m_rawdata_sendq_high_water_mark (max WDL_PtrList depth ever observed at enqueue), m_rawdata_cs_contention_count (bumped on TryEnter-false in RawDataSendBegin/Write), AND m_rawdata_sendq_total_enqueues (the denominator for the contention-ratio threshold 'contention < 1% of enqueues' in Plan 20-03's UAT acceptance criteria; bumped once per successful Add inside RawDataSendBegin and once per successful Add inside RawDataSendWrite, paired with the high-water-mark update under m_rawdata_cs). All three accessors return atomic loads with relaxed memory ordering. Plan 20-03 reads them; it does not add any of them."
    - "CONTEXT.md '<code_context> → Reusable Assets' line is rewritten to remove 'Run-thread drain swaps the list out under the mutex' and replaced with 'Run-thread drain is NinjamZap-literal pop-one-unlock-Send-relock, matching ninjamzap-core/njclient.cpp:1987-2039', closing R3 must-fix item 2 (M8 from Round 2)"
    - "RESEARCH.md stale sections per R3 MF5 are tagged with `<!-- STALE — DO NOT PLAN FROM THIS SECTION; see CRITICAL UPDATE at top, CONTEXT.md is authoritative -->` markers; CRITICAL UPDATE block at lines 1-40 is preserved unchanged"
    - "tests/test_rawdata_send.cpp 8-test suite is GREEN under `ctest -R rawdata_send`; full suite green under `ctest --output-on-failure`"
  artifacts:
    - path: "src/core/njclient.h"
      provides: "WDL_PtrList-based RawDataQueueItem queue + WDL_Mutex m_rawdata_cs; deletion of SPSC + overflow counter + capacity constant + Pattern C accessor; declaration of m_rawdata_sendq_high_water_mark + m_rawdata_cs_contention_count + m_rawdata_sendq_total_enqueues atomics + their accessors (GetRawDataSendQueueHighWaterMark, GetRawDataMutexContentionCount, GetRawDataSendQueueTotalEnqueueCount) for Plan 20-03 observability — all three counters owned here unconditionally"
      contains: "WDL_PtrList<RawDataQueueItem> m_rawdata_sendq"
    - path: "src/core/njclient.cpp"
      provides: "NinjamZap-literal RawDataSendBegin/RawDataSendWrite + NinjamZap-literal run-thread drain; all writeLog calls on this path deleted; high-water-mark + contention counter + total-enqueue counter increments wired inside RawDataSendBegin and RawDataSendWrite (each paired with the existing Add() call under m_rawdata_cs)"
      contains: "m_rawdata_cs.Enter()"
    - path: "src/threading/spsc_payloads.h"
      provides: "RawDataItem POD + RAWDATA_SEND_QUEUE_CAPACITY constant + SpscRing<RawDataItem, ...> static_assert deleted (lines 31-69 carve-out comment also retired); the rest of spsc_payloads.h MUST remain UNCHANGED per Phase 15.1-04 Codex M-9 Wave-0 finality"
    - path: "tests/test_rawdata_send.cpp"
      provides: "8 sub-tests replacing 14.3-02 SPSC-overflow assertions with NinjamZap-literal mutex semantics"
      min_lines: 350
    - path: ".claude/agents/realtime-audio-reviewer.md"
      provides: "Audit-allowlist envelope per D-09 — explicit accept-list of audio-thread carve-out file:line entries for the on_new_interval video block, with rationale referencing CONTEXT.md decisions"
      contains: "Phase 20 audit allowlist envelope"
    - path: ".codex/agents/realtime-audio-reviewer.toml"
      provides: "Mirror of allowlist envelope for the codex agent path; same content shape"
      contains: "phase = 20"
    - path: ".planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md"
      provides: "Drain-semantics sentence corrected per R3 MF2"
      contains: "pop-one-unlock-Send-relock"
    - path: ".planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md"
      provides: "Stale body sections per R3 MF5 are tagged STALE — DO NOT PLAN FROM THIS SECTION; CRITICAL UPDATE block preserved unchanged"
      contains: "STALE — DO NOT PLAN FROM THIS SECTION"
  key_links:
    - from: "NJClient::RawDataSendBegin/RawDataSendWrite (any thread)"
      to: "NJClient::Run drain block (run thread)"
      via: "WDL_Mutex m_rawdata_cs + WDL_PtrList<RawDataQueueItem> m_rawdata_sendq"
      pattern: "m_rawdata_cs\\.Enter\\(\\)"
    - from: "NJClient::Run drain block (run thread)"
      to: "m_netcon->Send"
      via: "pop one item, release mutex, Send, free, re-acquire mutex"
      pattern: "m_rawdata_sendq\\.Delete\\(0\\)"
    - from: "audio-thread call sites (Plan 20-02 will use)"
      to: ".claude/agents/realtime-audio-reviewer.md allowlist"
      via: "explicit file:line + rationale for each carve-out"
      pattern: "Phase 20 audit allowlist envelope"
    - from: "Plan 20-00 m_rawdata_sendq_total_enqueues counter"
      to: "Plan 20-03 UAT contention-ratio gate (R3 MF4)"
      via: "denominator for `m_rawdata_cs_contention_count / m_rawdata_sendq_total_enqueues < 1%`; counter incremented once per successful Add inside RawDataSendBegin and once per successful Add inside RawDataSendWrite, both under m_rawdata_cs"
      pattern: "m_rawdata_sendq_total_enqueues\\.fetch_add"
---

<objective>
Plan 20-00 corrects the Phase 14.3-02 SPSC substrate that was discovered to violate the multi-producer requirement of the Phase 20 HYBRID emission model (codex Round 1 H1; see 20-REVIEWS.md). The substrate is reverted to NinjamZap-literal `WDL_PtrList<RawDataQueueItem> + WDL_Mutex` per D-19, the audit-allowlist envelope for the audio-thread carve-outs (D-09) is published, residual `writeLog` calls on the send path are stripped, and the rewritten `test_rawdata_send` suite asserts mutex correctness instead of SPSC capacity/overflow semantics. The plan also discharges the two pre-planning artifact-edit items from Round 3: the CONTEXT.md drain-semantics contradiction (MF2) and the RESEARCH.md staleness markers (MF5).

Purpose: this is the Wave-0 substrate that Plans 20-01 / 20-02 / 20-03 depend on. Without it, the audio thread + encoder thread cannot both producer-safely call `RawDataSendWrite` for the same channel within an interval, and the auditor `realtime-audio-reviewer` would CRITICAL-flag every audio-thread carve-out site that Plan 20-02 introduces. Without the artifact-edit cleanup, downstream plan reviewers risk re-introducing retired Path-A / atomic-pointer-swap / swap-list-out designs from stale documentation.

Output: A `WDL_PtrList`-backed `m_rawdata_sendq` whose multi-producer correctness is verified by 8 sub-tests in `test_rawdata_send`. A `realtime-audio-reviewer` config that explicitly accepts the audio-thread mutex/RNG/heap/memcpy carve-out envelope at the file:line level, so Plan 20-02's audit run reports CRITICAL count = 0. A `20-CONTEXT.md` whose `<code_context>` drain-semantics sentence matches D-19 + the source. A `20-RESEARCH.md` whose stale body sections are clearly tagged so plan authors cannot accidentally consume retired designs. Queue observability scaffolding (high-water-mark + contention atomics + total-enqueue counter + accessors — all three counters owned here unconditionally per R3 MF4) is added here so Plan 20-03's UAT thresholds in MF4 have something to read at populated load; runtime increments for the high-water-mark, contention counter, and total-enqueue counter are wired by Plan 20-00 in RawDataSendBegin/RawDataSendWrite directly — Plan 20-03 only reads them.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/REQUIREMENTS.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-VALIDATION.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-REVIEWS.md
@.planning/phases/14.3-native-video-foundation/14.3-02-SUMMARY.md
@src/core/njclient.h
@src/core/njclient.cpp
@src/threading/spsc_payloads.h
@tests/test_rawdata_send.cpp
@.claude/agents/realtime-audio-reviewer.md

<interfaces>
<!-- Key contracts the executor needs. Extracted verbatim from NinjamZap reference (canonical) and JamWide as-shipped (the surface this plan revises). -->
<!-- Use these directly — no codebase exploration needed for these signatures. -->

From ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:310-321 — CANONICAL NinjamZap shape (port target):
  // Raw data send queue (thread-safe: any thread pushes, Run() drains under gsMtx)
  struct RawDataQueueItem {
    int type; // 0=begin, 1=data/end
    unsigned char guid[16];
    unsigned int fourcc;
    int chidx;
    int estsize;
    int flags; // &1 = end
    WDL_HeapBuf data;
  };
  WDL_PtrList<RawDataQueueItem> m_rawdata_sendq;
  WDL_Mutex m_rawdata_sendq_cs;          // JamWide renames to m_rawdata_cs per CONTEXT.md
  // NOTE: NinjamZap uses `WDL_HeapBuf data` (by-VALUE, not pointer). JamWide port adopts the
  // same by-VALUE shape — the heap allocation lives INSIDE the queue item, ResizeOK'd at
  // RawDataSendWrite time. No separate `new WDL_HeapBuf` like 14.3-02's SPSC port; the
  // queue item itself is `new`'d and the inner buffer is value-managed via WDL_HeapBuf's
  // own destructor when the queue item is `delete`d.

From ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2047-2082 — CANONICAL Begin/Write (port target):
  void NJClient::RawDataSendBegin(unsigned char outGuid[16], unsigned int fourcc, int chidx, int estsize) {
    WDL_RNG_bytes(outGuid, 16);
    RawDataQueueItem *item = new RawDataQueueItem;
    item->type = 0; memcpy(item->guid, outGuid, 16);
    item->fourcc = fourcc; item->chidx = chidx; item->estsize = estsize; item->flags = 0;
    m_rawdata_sendq_cs.Enter();
    m_rawdata_sendq.Add(item);
    m_rawdata_sendq_cs.Leave();
  }
  void NJClient::RawDataSendWrite(const unsigned char guid[16], const void *data, int dataLen, bool isEnd) {
    RawDataQueueItem *item = new RawDataQueueItem;
    item->type = 1; memcpy(item->guid, guid, 16);
    item->fourcc = 0; item->chidx = 0; item->estsize = 0;
    item->flags = isEnd ? 1 : 0;
    if (data && dataLen > 0) { item->data.Resize(dataLen); memcpy(item->data.Get(), data, dataLen); }
    m_rawdata_sendq_cs.Enter();
    m_rawdata_sendq.Add(item);
    m_rawdata_sendq_cs.Leave();
  }

From ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:1984-2040 — CANONICAL drain (port target):
  if (m_netcon) {
    m_rawdata_sendq_cs.Enter();
    while (m_rawdata_sendq.GetSize()) {
      RawDataQueueItem *item = m_rawdata_sendq.Get(0);
      m_rawdata_sendq.Delete(0);                 // remove without freeing item (we own it next)
      m_rawdata_sendq_cs.Leave();
      // ... build mpb_client_upload_interval_begin / mpb_client_upload_interval_write
      // ... split type==1 payload at MAX_ENC_BLOCKSIZE; emit one m_netcon->Send per chunk
      delete item;
      m_rawdata_sendq_cs.Enter();
    }
    m_rawdata_sendq_cs.Leave();
  }
  // NOTE: NinjamZap source does NOT have a Pattern-C "if (!m_netcon) drain-and-discard"
  // branch — items accumulate in the unbounded WDL_PtrList until either Run() drains them
  // or NJClient's destructor empties the list via WDL_PtrList::Empty(true). Port matches.

From JamWide src/core/njclient.h as-shipped (BEFORE this plan — deletions listed for transparency):
  line 976-978   jamwide::SpscRing<jamwide::RawDataItem, jamwide::RAWDATA_SEND_QUEUE_CAPACITY> m_rawdata_sendq;   ← DELETE, replace
  line 984       std::atomic<uint64_t> m_rawdata_sendq_overflows{0};                                              ← DELETE
  line 752-755   GetRawDataSendQueueOverflowCount()                                                               ← DELETE accessor + member
  line 992       std::atomic<uint64_t> m_rawdata_sendq_discards{0};                                               ← KEEP or DELETE per planner choice (NinjamZap has no equivalent; the Disconnect-path drain doesn't exist in NinjamZap because the destructor is the only sink besides Run-thread drain); recommended: DELETE for NinjamZap-literal fidelity

From JamWide src/core/njclient.cpp as-shipped (BEFORE this plan — sites being rewritten):
  line 2825-2889  run-thread drain block (Pattern C if(!m_netcon) + drain lambda for connected path)             ← REWRITE pop-one-unlock-Send-relock
  line 2994-3019  RawDataSendBegin (SpscRing try_push + overflow counter + writeLog at line 3015)               ← REWRITE NinjamZap-literal + remove writeLog
  line 3021-3068  RawDataSendWrite (SpscRing try_push + overflow counter + writeLog at lines 3048/3063)         ← REWRITE NinjamZap-literal + remove writeLog
  line 3084-3089  DrainRawDataSendQueueForTest (JAMWIDE_BUILD_TESTS test helper)                                ← REWRITE to drain WDL_PtrList (still hands ownership back to caller)
  line 3091+      ChunkRawDataItem (JAMWIDE_BUILD_TESTS test helper)                                            ← KEEP — chunking logic unchanged; signature may need RawDataItem → RawDataQueueItem rename

From JamWide src/threading/spsc_payloads.h as-shipped (lines being touched):
  line 31-69     RawDataItem POD declaration + RAWDATA_SEND_QUEUE_CAPACITY constant + static_assert             ← DELETE
  KEEP everything else in spsc_payloads.h UNCHANGED — Phase 15.1-04 Codex M-9 declares this header FINAL after Wave 0 of 15.1; non-RawData payloads (RemoteUserUpdate, LocalChannelUpdate, BlockRecord, DecodeChunk, DecodeArmRequest, RemoteUser*/Local_Channel* deferred-free) MUST be untouched.

Observability frontmatter (new, declared here so Plan 20-03 can read — ALL THREE counters are owned here unconditionally per R3 MF4):
  std::atomic<uint64_t> m_rawdata_sendq_high_water_mark{0};   // max WDL_PtrList depth ever observed at enqueue
  std::atomic<uint64_t> m_rawdata_cs_contention_count{0};     // bumped by RawDataSendBegin/Write when Enter() finds the mutex held (TryEnter-false)
  std::atomic<uint64_t> m_rawdata_sendq_total_enqueues{0};    // denominator for contention-ratio gate; bumped once per successful Add() inside RawDataSendBegin and once per successful Add() inside RawDataSendWrite, under m_rawdata_cs
  uint64_t GetRawDataSendQueueHighWaterMark() const noexcept;
  uint64_t GetRawDataMutexContentionCount() const noexcept;
  uint64_t GetRawDataSendQueueTotalEnqueueCount() const noexcept;
  // NOTE: contention sampling on WDL_Mutex requires `TryEnter()` (jnetlib provides) — sample before Enter()
  // and increment on TryEnter-false. Plan 20-00 wires the counter increment INSIDE RawDataSendBegin/Write
  // (the actual contention-sensitive sites); Plan 20-03 establishes the UAT acceptance threshold.
  // The total-enqueue counter is bumped at the same site, paired with the high-water-mark CAS, so the
  // three counters move together under m_rawdata_cs.

Audit-allowlist envelope text (the file content this plan writes into realtime-audio-reviewer.md):
  ## Phase 20 audit allowlist envelope
  The following audio-thread sites are accepted Phase 15.1 carve-outs under CONTEXT.md D-09 + D-20:
    src/core/njclient.cpp `NJClient::on_new_interval` video block (lines TBD when Plan 20-02 lands):
      - `m_video_cs.Enter() / Leave()` — held across whole video block (D-08, NinjamZap-literal)
      - `m_video_spspps_cs.Enter() / Leave()` — nested inside m_video_cs for SPS/PPS read (D-03)
      - direct read of `m_locchans[0]->m_curwritefile.guid` for marker audio_ch0_guid (D-20 Phase 15.1-06 HIGH-2 carve-out)
      - `memcpy(marker+8, ...)` of 16 bytes from the canonical Local_Channel field
    src/core/njclient.cpp `NJClient::RawDataSendBegin` (any thread; called from audio thread inside on_new_interval):
      - `m_rawdata_cs.Enter() / Leave()` (D-09)
      - `WDL_RNG_bytes(outGuid, 16)` (D-09 — internally locked, per-call cost ~5 µs)
      - `new RawDataQueueItem` (D-09 — single heap alloc per call)
    src/core/njclient.cpp `NJClient::RawDataSendWrite` (any thread; called from audio thread inside on_new_interval AND from encoder thread inside QueueVideoFrame):
      - `m_rawdata_cs.Enter() / Leave()` (D-09)
      - `new RawDataQueueItem` + `item->data.Resize(dataLen)` / `ResizeOK` + `memcpy` of payload (D-09)
    src/core/njclient.cpp on the RawDataSendBegin/Write paths: NO `writeLog` calls remain (verify; this plan strips them per D-19 + R2 H7).
  All other audio-path violations remain CRITICAL — these are the only carve-outs accepted under the D-09 envelope.
</interfaces>
</context>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| audio thread → m_rawdata_cs → run thread | mutex-mediated cross-thread queue handoff; audio thread is one of N producers (N=1 in 14.3-02, N≥2 from Plan 20-02 onward) |
| run thread → m_netcon->Send | sole writer to the NINJAM socket per Phase 14.3-02 D-04 invariant |
| `realtime-audio-reviewer` audit → audio-path source | reviewer reports CRITICAL on heap/lock/log violations; this plan publishes the accept-list envelope |
| package supply chain (none added this plan) | this plan does not add or change any npm/pip/cargo packages; T-20-SC remains nominal |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-20-00 | Tampering (race on m_rawdata_sendq) | NJClient RawData send/drain paths | mitigate | WDL_Mutex m_rawdata_cs serialises every Add() and every Get(0)/Delete(0) sequence; run-thread drain pop-one-unlock-Send-relock matches ninjamzap-core/njclient.cpp:1987-2039 verbatim; test_rawdata_send covers 2+ producer-thread stress with TSan-clean expectations under the --tsan build per Phase 15.1 D-07 |
| T-20-RT | Real-time safety (audio-thread budget) | on_new_interval (Plan 20-02 lands the audio-thread caller; this plan publishes the carve-out envelope) | accept | Per D-09: ~50-100 µs of mutex+RNG+alloc+memcpy work per interval is intentional NinjamZap-literal cost; the envelope is published here so the auditor accepts the carve-out; Plan 20-03 measures actual audio-thread budget at populated HD broadcast and escalates if > 200 µs worst-case |
| T-20-SC | Tampering (supply chain) | none added this plan | n/a | No package installs added; Phase 14.3-01 vendored libavcodec/libavutil/libswscale/libopenh264 audit deferred to Plan 23 — this plan touches only existing source |
| T-20-OBS | Information disclosure (silent OOM growth on unbounded queue) | m_rawdata_sendq (WDL_PtrList) | mitigate | Add m_rawdata_sendq_high_water_mark + m_rawdata_cs_contention_count + m_rawdata_sendq_total_enqueues atomics + accessors in this plan; Plan 20-03's UAT acceptance threshold (high-water < 32 items, contention < 1% of enqueues at each preset) is fed by these counters (contention ratio = contention_count / total_enqueues); if exceeded → substrate-tuning subplan per CONTEXT.md Deferred Ideas |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Substrate revision in NJClient (header + cpp) — replace SPSC with WDL_PtrList+WDL_Mutex, NinjamZap-literal</name>
  <files>
    src/core/njclient.h,
    src/core/njclient.cpp,
    src/threading/spsc_payloads.h
  </files>
  <behavior>
    - After this task, `NJClient::m_rawdata_sendq` is `WDL_PtrList<RawDataQueueItem>` (RawDataQueueItem is the new private nested struct inside NJClient, mirroring NinjamZap's ninjamzap-core/njclient.h:311-321 field-for-field). `WDL_Mutex NJClient::m_rawdata_cs` exists alongside it.
    - `RawDataSendBegin(outGuid, fourcc, chidx, estsize)` matches ninjamzap-core/njclient.cpp:2047-2062 verbatim: `WDL_RNG_bytes(outGuid, 16)` → `new RawDataQueueItem` → populate fields → `m_rawdata_cs.Enter() / m_rawdata_sendq.Add(item) / m_rawdata_cs.Leave()`. No `writeLog`. No overflow counter increment. No try_push, no SPSC reference.
    - `RawDataSendWrite(guid, data, dataLen, isEnd)` matches ninjamzap-core/njclient.cpp:2064-2082 verbatim: `new RawDataQueueItem` → populate fields → if `data && dataLen > 0` then `item->data.ResizeOK(dataLen)` (keep JamWide's existing ResizeOK preference over Resize per 14.3-02 CR-01) + `memcpy(item->data.Get(), data, dataLen)` → `m_rawdata_cs.Enter() / m_rawdata_sendq.Add(item) / m_rawdata_cs.Leave()`. No `writeLog`. ResizeOK-failure path: `delete item; return;` (no counter; treat OOM as silent drop in NinjamZap fashion — Plan 20-03 observability will catch it via the unbounded queue growing or the run-thread drain measuring drain rate).
    - Run-thread drain block in NJClient::Run is replaced by NinjamZap-literal pop-one-unlock-Send-relock matching ninjamzap-core/njclient.cpp:1984-2040 verbatim. The full block:
        if (m_netcon) {
          m_rawdata_cs.Enter();
          while (m_rawdata_sendq.GetSize()) {
            RawDataQueueItem *item = m_rawdata_sendq.Get(0);
            m_rawdata_sendq.Delete(0);                 // pointer removed from list, ownership stays with us
            m_rawdata_cs.Leave();
            // ... build mpb_client_upload_interval_begin (type==0) OR mpb_client_upload_interval_write loop
            //     splitting item->data at MAX_ENC_BLOCKSIZE (type==1, EXISTING chunking semantics preserved
            //     from 14.3-02 ChunkRawDataItem — only the queue substrate changes, not the chunking)
            delete item;
            m_rawdata_cs.Enter();
          }
          m_rawdata_cs.Leave();
        }
      The `if (!m_netcon)` Pattern C discard branch is DELETED — NinjamZap-literal does not have it. Items pending at Disconnect are freed by NJClient's destructor via `m_rawdata_sendq.Empty(true)` (WDL_PtrList::Empty(true) frees the items).
    - `GetRawDataSendQueueOverflowCount()` accessor declaration + its underlying `m_rawdata_sendq_overflows` member are DELETED. `GetRawDataSendQueueDiscardCount()` + `m_rawdata_sendq_discards` are DELETED (NinjamZap has no equivalent; Disconnect-drain branch is also retired).
    - New observability surface (per R3 MF4 — Plan 20-03 consumes these but the declarations + increment sites live here so the data path is wired top-to-bottom in one go). ALL THREE counters are owned by this plan unconditionally; Plan 20-03 does not add any of them:
        std::atomic<uint64_t> m_rawdata_sendq_high_water_mark{0};
        std::atomic<uint64_t> m_rawdata_cs_contention_count{0};
        std::atomic<uint64_t> m_rawdata_sendq_total_enqueues{0};   // denominator for the contention-ratio gate in Plan 20-03 UAT
        uint64_t GetRawDataSendQueueHighWaterMark() const noexcept { return m_rawdata_sendq_high_water_mark.load(std::memory_order_relaxed); }
        uint64_t GetRawDataMutexContentionCount() const noexcept { return m_rawdata_cs_contention_count.load(std::memory_order_relaxed); }
        uint64_t GetRawDataSendQueueTotalEnqueueCount() const noexcept { return m_rawdata_sendq_total_enqueues.load(std::memory_order_relaxed); }
      Increment wiring inside RawDataSendBegin and RawDataSendWrite. Pattern is identical at both call sites:
        // Contention sampling BEFORE the Enter():
        if (!m_rawdata_cs.TryEnter()) { m_rawdata_cs_contention_count.fetch_add(1, std::memory_order_relaxed); m_rawdata_cs.Enter(); } else { /* TryEnter already locked */ }
        m_rawdata_sendq.Add(item);
        // Total-enqueue counter (denominator) — paired with high-water-mark update under m_rawdata_cs:
        m_rawdata_sendq_total_enqueues.fetch_add(1, std::memory_order_relaxed);
        const size_t depth = (size_t)m_rawdata_sendq.GetSize();
        uint64_t prev = m_rawdata_sendq_high_water_mark.load(std::memory_order_relaxed);
        while (depth > prev && !m_rawdata_sendq_high_water_mark.compare_exchange_weak(prev, (uint64_t)depth, std::memory_order_relaxed)) {}
        m_rawdata_cs.Leave();
      Confirm WDL_Mutex has TryEnter() in jnetlib/WDL — if not, fall back to a coarser contention proxy (sample WDL_Mutex internal "wait" flag if available, or omit contention counting and rely on the Plan 20-03 high-water threshold alone — note the choice in SUMMARY.md). The total-enqueue counter is NOT optional — it is the denominator for Plan 20-03's contention-ratio gate and must be wired regardless of the TryEnter fallback decision.
    - In spsc_payloads.h: delete RawDataItem POD declaration (current lines ~331-368), delete RAWDATA_SEND_QUEUE_CAPACITY constant (line 387), delete static_assert that RawDataItem is trivially copyable (line 366-368), delete the "Phase 14.3-02 carve-out" header comment block (lines 31-69 that explain the SPSC carve-out for video). Phase 15.1-04 Codex M-9 finality applies to the rest of this file; touch ONLY the RawData lines.
  </behavior>
  <action>
    Apply the canonical NinjamZap substrate verbatim per the `<interfaces>` block above and the behavior list. Field ordering of RawDataQueueItem MUST exactly match ninjamzap-core/njclient.h:311-321 (int type; unsigned char guid[16]; unsigned int fourcc; int chidx; int estsize; int flags; WDL_HeapBuf data;) — do not reorder, do not rename fields, do not introduce additional bookkeeping fields. The chunking helper `NJClient::ChunkRawDataItem` (JAMWIDE_BUILD_TESTS-gated; currently takes `const jamwide::RawDataItem&`) MUST be retained for test_rawdata_send compatibility but its signature changes to take `const RawDataQueueItem&` (the nested type); the static lookup of item.payload becomes `item.data.Get() / item.data.GetSize()` — that is, by-value WDL_HeapBuf instead of pointer. `DrainRawDataSendQueueForTest` becomes a destructive drain that pops every queue item into an output `std::vector<RawDataQueueItem*>` (callers `delete` each pointer when done); this is the standard WDL_PtrList ownership-handoff idiom. Confirm `m_rawdata_cs` is declared NEAR `m_rawdata_sendq` and the destructor calls `m_rawdata_sendq.Empty(true)` (frees each owned item). Verify all three observability accessors (GetRawDataSendQueueHighWaterMark, GetRawDataMutexContentionCount, GetRawDataSendQueueTotalEnqueueCount) are public on NJClient so Plan 20-03's UAT harness can read them.
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target njclient -- -j8 2>&amp;1 | tail -40</automated>
    Confirm no compile errors on njclient static library after the substrate swap. The lib must build before tests can link.
  </verify>
  <done>
    `src/core/njclient.{h,cpp}` builds with `WDL_PtrList<RawDataQueueItem> m_rawdata_sendq + WDL_Mutex m_rawdata_cs` in place of the SPSC. `spsc_payloads.h` no longer declares `RawDataItem` or `RAWDATA_SEND_QUEUE_CAPACITY`. All references to `m_rawdata_sendq_overflows` / `m_rawdata_sendq_discards` / `GetRawDataSendQueueOverflowCount` / `GetRawDataSendQueueDiscardCount` are deleted across both files. No `writeLog` call remains anywhere in `RawDataSendBegin` / `RawDataSendWrite` / the run-thread drain block (verify with `grep -nE 'writeLog' src/core/njclient.cpp | sed -n '1,200p'` showing the existing writeLog sites are all on other paths). New observability atomics + accessors exist: `m_rawdata_sendq_high_water_mark`, `m_rawdata_cs_contention_count`, `m_rawdata_sendq_total_enqueues`, with public accessors `GetRawDataSendQueueHighWaterMark()`, `GetRawDataMutexContentionCount()`, `GetRawDataSendQueueTotalEnqueueCount()`. Total-enqueue counter is incremented inside both RawDataSendBegin and RawDataSendWrite under m_rawdata_cs, paired with the high-water-mark CAS. njclient static library compiles cleanly.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Rewrite tests/test_rawdata_send.cpp — 8 sub-tests covering NinjamZap-literal mutex semantics</name>
  <files>tests/test_rawdata_send.cpp</files>
  <behavior>
    The rewritten test file MUST contain at minimum these 8 sub-tests (each using the existing TEST()/PASS()/FAIL() macros + tests_run/tests_passed counters from the current file's scaffold; preserve the macros verbatim from lines 44-63 of the as-shipped file):
      1. test_rawdata_begin_write_end_roundtrip — Begin + 2 Writes + Drain yields 3 RawDataQueueItem* in FIFO order; fields populated per NinjamZap shape (type/guid/fourcc/chidx/estsize/flags/data). REPLACES the existing test at lines 79+; assertion on `GetRawDataSendQueueOverflowCount() != 0` is REMOVED (the accessor no longer exists).
      2. test_rawdata_write_chunking — Writing a 2*MAX_ENC_BLOCKSIZE payload then exercising ChunkRawDataItem produces ceil(payload / MAX_ENC_BLOCKSIZE) chunks with flags=1 only on the final chunk if item.flags&1. PORTED from the existing chunking sub-test; signature change to `const RawDataQueueItem&`.
      3. test_rawdata_video_fourcc_helper — IsVideoFourcc returns true for H264/VP8 /MJPG, false for OGGv/FLAC. UNCHANGED from the existing sub-test.
      4. test_rawdata_multi_producer_stress — 4 producer threads concurrently call RawDataSendBegin + RawDataSendWrite N=100 times each (M=400 total enqueues across 1200 queue ops). After all producers join, drain the queue and assert:
           - exactly 1200 items present;
           - all items are well-formed (type ∈ {0,1}; data.GetSize() consistent with the producer's intent if type==1; no torn fields);
           - GetRawDataSendQueueHighWaterMark() > 0 (some serialization observed; the test runs without TSan-flagged data races even at this concurrency);
           - GetRawDataSendQueueTotalEnqueueCount() == 1200 (the new denominator counter is incremented exactly once per successful Add).
         REPLACES the SPSC try_push contract assertions from the 14.3-02 suite.
      5. test_rawdata_drain_interleave — Spawn 2 producer threads enqueuing at rate R for T seconds; main thread acts as run-thread drain (pop-one-unlock-process-relock; processing = `delete item`). After T seconds, signal producers to stop and drain remaining; assert no leaked items (use a manual leak count by overriding `delete` accounting OR by checking m_rawdata_sendq.GetSize() == 0 after final drain; preferred: use the existing DrainRawDataSendQueueForTest as the drain so leak-tracking is automatic via the output vector). Asserts: drain semantics interleave correctly with active producers under WDL_Mutex; no item observed twice.
      6. test_rawdata_destructor_cleanup — Construct an NJClient, enqueue ~10 items, then destroy the client WITHOUT draining. The WDL_PtrList::Empty(true) call in the destructor MUST free all items (verified by ASAN under the existing test build flags showing no leaks). REPLACES the 14.3-02 m_rawdata_sendq_discards == N assertion (which depended on the Pattern-C if(!m_netcon) drain that is now deleted).
      7. test_rawdata_send_ordering_per_producer — Single producer thread enqueues Begin + Write(seq=0) + Write(seq=1) + Write(seq=2) + Write(isEnd=true); after drain, the 5 items appear in exactly that order in the WDL_PtrList. NinjamZap's per-producer FIFO is intrinsic (single Enter()/Leave() per call wraps Add); test asserts this invariant under multi-producer concurrent load too (use producer-tagged sequence numbers embedded in the payload so the drain output can be partitioned by producer and each partition checked for monotonic seq).
      8. test_rawdata_wire_ordering_begin_marker_sps_frame — Simulate the Plan 20-02 on_new_interval sequence + a Plan 20-01 encoder-thread frame interleaved against it:
           Producer A (audio-thread role): under a local std::lock_guard mocking m_video_cs, call RawDataSendBegin(g1, H264, 1, 0), RawDataSendWrite(g1, marker24, 24, false), RawDataSendWrite(g1, spspps, S, false), unlock.
           Producer B (encoder-thread role): wait on the same lock, then call RawDataSendWrite(g1, frame, F, false).
         After both producers finish, drain and assert: order in the queue is BEGIN(g1) → marker24 → spspps → frame. The test models the whole-block m_video_cs serialization that Plan 20-02 will land — it doesn't need NJClient's m_video_cs to exist yet because the test owns the mock mutex.
    Test runtime budget: < 5 seconds per sub-test (sub-test 4 may be ~2 s at N=100 × 4 threads; tune if needed).
    Test file structure: keep the existing main() that runs each test, increments tests_run, and exits non-zero if tests_run != tests_passed.
  </behavior>
  <action>
    Replace test bodies that asserted SPSC-only invariants (overflow counter increment, capacity-bound enforcement, Pattern C discard count) with the mutex-correctness assertions above. The existing `free_payloads` helper (lines 68-73) MUST be updated: since RawDataQueueItem now owns its WDL_HeapBuf by value, `delete item;` is the only cleanup required (no separate `delete item.payload`). Update the helper to `delete out[i]` for each `RawDataQueueItem*` returned by the destructive drain. Sub-test 4 additionally asserts the total-enqueue counter (GetRawDataSendQueueTotalEnqueueCount() == 1200) — this is the denominator for Plan 20-03's contention-ratio gate, so the test must verify it increments correctly under multi-producer load.
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target test_rawdata_send -- -j8 &amp;&amp; ctest -R rawdata_send --output-on-failure 2>&amp;1 | tail -40</automated>
    All 8 sub-tests must PASSED, tests_run == tests_passed at end, exit code 0.
  </verify>
  <done>
    `ctest -R rawdata_send` shows 8/8 sub-tests green. No ASAN leak reports under the existing test build (the destructor cleanup test asserts this). Full suite `ctest --output-on-failure` is also green for the other test_rawdata_send* targets and unrelated existing tests (regression check that the substrate swap didn't break test_video_fourcc or test_video_sync). Sub-test 4 explicitly verifies GetRawDataSendQueueTotalEnqueueCount() == 1200 (the denominator counter is wired correctly under multi-producer load).
  </done>
</task>

<task type="auto">
  <name>Task 3: Publish audit-allowlist envelope (.claude + .codex agent configs) + edit 20-CONTEXT.md drain-semantics + tag 20-RESEARCH.md stale sections</name>
  <files>
    .claude/agents/realtime-audio-reviewer.md,
    .codex/agents/realtime-audio-reviewer.toml,
    .planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md,
    .planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md
  </files>
  <action>
    A) Add a `## Phase 20 audit allowlist envelope` section to `.claude/agents/realtime-audio-reviewer.md` between the existing "Important rules" and end-of-file. Content per the `<interfaces>` block above — explicit accept-list of file:line carve-out entries for: `m_video_cs / m_video_spspps_cs / m_rawdata_cs` mutex Enter/Leave on audio thread; `WDL_RNG_bytes`; `new RawDataQueueItem` + `item->data.ResizeOK` + `memcpy`; marker construction memcpy; direct read of `m_locchans[0]->m_curwritefile.guid` (D-20 Phase 15.1-06 HIGH-2 carve-out, audit-allowlist scope: ONE field, ONE read site — the marker construction inside `on_new_interval`'s video block under `m_video_cs`); explicit "NO `writeLog` on this path" reaffirmation. Each carve-out cites the CONTEXT.md decision ID (D-08 / D-09 / D-19 / D-20). Trailing note: "All other audio-path violations remain CRITICAL — these are the only carve-outs accepted under the D-09 envelope. Auditor zero-CRITICAL gate applies OUTSIDE this envelope."
    B) Mirror the same envelope into `.codex/agents/realtime-audio-reviewer.toml` (TOML structure: top-level `[phase_20_allowlist]` table with key/value pairs for each carve-out site; preserve the existing TOML schema fields). If the existing TOML schema doesn't have an extensible structure, append a free-form `[phase_20_allowlist.notes]` table containing the same markdown content as a multi-line string. Goal: codex agent path consumes the same envelope content as the claude agent path.
    C) Edit `.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md` at the `<code_context> → Reusable Assets` block (currently line ~153 "Run-thread drain swaps the list out under the mutex, processes outside.") — REPLACE that sentence with: "Run-thread drain is NinjamZap-literal pop-one-unlock-Send-relock matching `ninjamzap-core/njclient.cpp:1987-2039`; per-item ownership transfers from the WDL_PtrList to the drain loop (Get(0) → Delete(0) → process outside the lock → delete item)." This closes R3 must-fix item 2 + Round 2 M8.
    D) Edit `.planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md` to add `<!-- STALE — DO NOT PLAN FROM THIS SECTION; see CRITICAL UPDATE at top, CONTEXT.md is authoritative -->` markers IMMEDIATELY ABOVE each of these section headings (R3 must-fix item 5):
      • `## Summary` (line 42)
      • `### Locked Decisions` (line 79)
      • `### Pattern 1: Audio-Thread Lock-Free SPS/PPS Read` (line 361)
      • `### Pattern 1b: Path B Carve-out (NinjamZap-literal)` (line 413)
      • `### Pattern 3: SPS/PPS Atomic Pointer Swap with Deferred Delete` (line 487)
      • `### Pitfall 4: Path B trigger detection ambiguity` (line 597)
      • `### JamWide Phase 14.3-02 Drain Block (existing — reference only, not modified)` (line 760) — the "not modified" wording is wrong, this plan modifies it; add a stronger marker: `<!-- STALE — DO NOT PLAN FROM THIS SECTION; this drain block IS modified by Plan 20-00 per D-19. See CONTEXT.md `<integration_points>` for the correct pattern. -->`
    The CRITICAL UPDATE block at lines 1-40 of 20-RESEARCH.md MUST remain unchanged. The Validation Architecture section (line 897+), Code Examples → NinjamZap Send-Side State Machine (line 637), NinjamZap Encoder Thread Call (line 676), JamTaba openh264 Configure Block (line 694), Forcing IDR for Interval-Boundary Keyframes (line 738), Common Pitfalls (other than #4 which IS stale), and Don't Hand-Roll (line 530) sections remain unmarked — these are referenced by Plans 20-01 / 20-02 as port targets and are accurate.
  </action>
  <verify>
    <automated>set -e; grep -c "Phase 20 audit allowlist envelope" .claude/agents/realtime-audio-reviewer.md | grep -vq "^0$"; grep -c "pop-one-unlock-Send-relock" .planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md | grep -vq "^0$"; STALE=$(grep -v '^#' .planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md | grep -c "STALE — DO NOT PLAN"); test "$STALE" -ge 7 || { echo "expected >= 7 STALE markers, found $STALE"; exit 1; }; echo "edits OK"</automated>
    Audit-allowlist section exists in `realtime-audio-reviewer.md`; drain-semantics sentence is corrected in CONTEXT.md; at least 7 STALE markers exist in RESEARCH.md.
  </verify>
  <done>
    All four files edited per the action block. Grep confirms the envelope header exists, the corrected drain-semantics sentence exists, and the 7 stale-section markers are present in RESEARCH.md. CRITICAL UPDATE block at RESEARCH.md lines 1-40 is byte-identical to its pre-edit content (no incidental whitespace changes).
  </done>
</task>

</tasks>

<verification>
- `cd build-juce && cmake --build . --target njclient -- -j8` exits 0
- `ctest -R rawdata_send --output-on-failure` exits 0; 8/8 sub-tests green
- `ctest --output-on-failure` exits 0 (regression check; existing video_fourcc / video_sync / spsc_state_updates and other unrelated tests are unaffected by the substrate swap)
- `grep -c '"writeLog"' src/core/njclient.cpp | awk '{print $1}'` shows reduced count vs the pre-plan baseline by exactly 3 (the three sites removed: previously around lines 3015, 3048, 3063)
- `grep -nE "GetRawDataSendQueueOverflowCount|m_rawdata_sendq_overflows|RAWDATA_SEND_QUEUE_CAPACITY|jamwide::RawDataItem" src/ tests/` returns NO matches (all references removed)
- `grep -nE "WDL_PtrList<RawDataQueueItem>\\s+m_rawdata_sendq" src/core/njclient.h` returns at least one match
- `grep -nE "m_rawdata_sendq_total_enqueues|GetRawDataSendQueueTotalEnqueueCount" src/core/njclient.h` returns at least one match (the new total-enqueue counter + accessor are declared)
- `grep -c "m_rawdata_sendq_total_enqueues.fetch_add" src/core/njclient.cpp` returns at least 2 (incremented inside both RawDataSendBegin and RawDataSendWrite)
- `grep -c "Phase 20 audit allowlist envelope" .claude/agents/realtime-audio-reviewer.md` returns ≥ 1
- `grep -c "pop-one-unlock-Send-relock" .planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md` returns ≥ 1
- `grep -v '^#' .planning/phases/20-h-264-encoder-send-pipeline/20-RESEARCH.md | grep -c "STALE — DO NOT PLAN"` returns ≥ 7
- TSan optional smoke (Plan 20-03 owns the populated-UAT-scale TSan run): if `JAMWIDE_TSAN=ON` build is locally available, `ctest -R rawdata_send` under TSan exits 0 with zero TSan-reported races
</verification>

<success_criteria>
- Plan 20-00 closes R3 must-fix items 2 (drain semantics contradiction) and 5 (RESEARCH.md staleness markers).
- The audit-allowlist envelope is published BEFORE Plan 20-02 lands, so Plan 20-02's audio-thread carve-out sites do not surface as CRITICAL when the audit runs at phase close.
- The substrate is NinjamZap-literal and ready for Plan 20-02 to drive a second producer (encoder thread via Plan 20-01) and a single drain consumer (run thread) without ABA, torn reads, or capacity-bound surprises.
- Queue observability scaffolding (high-water-mark + contention counter + total-enqueue counter atomics + their three accessors) is in place so Plan 20-03's UAT acceptance thresholds (MF4) read live data instead of mocking. The total-enqueue counter is the denominator for the contention-ratio gate; Plan 20-03 reads all three counters and adds none of them.
</success_criteria>

<output>
On completion, write `.planning/phases/20-h-264-encoder-send-pipeline/20-00-SUMMARY.md` per the get-shit-done summary template. Capture in the summary: (a) the exact NinjamZap-source citations the port pulled from (file:line) and any deviations from those; (b) any TryEnter/contention-counter fallback the executor chose if WDL_Mutex does not expose TryEnter (note: the total-enqueue counter is NOT optional and must be wired regardless of TryEnter availability); (c) the final list of stale-section markers added to RESEARCH.md (file:line); (d) one screenshot or `head -60` of the new envelope section in `.claude/agents/realtime-audio-reviewer.md` so subsequent plans can quote it verbatim.
</output>
</content>
</invoke>