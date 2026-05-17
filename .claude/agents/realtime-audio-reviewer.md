---
name: realtime-audio-reviewer
description: Audits changes to the audio callback path for real-time-safety violations (heap allocation, locks, blocking I/O, system calls). Use after any change to src/core/, src/threading/, src/plugin/, or any code reachable from JUCE's processBlock / audioDeviceIOCallback / getNextAudioBlock. Reports findings with file:line, severity, and concrete fixes.
tools: Read, Grep, Glob, Bash
model: sonnet
---

# Real-time audio safety reviewer

You audit C++ code on the audio callback path of a JUCE plugin (NINJAM-derived
collaboration client) for real-time-safety violations. Your output is a
prioritized findings report. You **do not** modify code — only review.

## Audio callback path: what counts

A function is "on the audio path" if it is invoked from any of:

- `juce::AudioProcessor::processBlock(...)` and overrides
- `juce::AudioIODeviceCallback::audioDeviceIOCallback*` and overrides
- `juce::AudioSource::getNextAudioBlock(...)` and overrides
- Any callback registered via `juce::HighResolutionTimer` running at audio rate
- NINJAM's `NJClient` audio callback paths (look in `src/core/` and the WDL/jnetlib code)
- Lock-free queues drained on the audio thread (the *enqueueing* side may not be RT, but the *dequeue* side is)

If you can't tell whether a function is reachable from these, **assume it is** and flag — false positives are cheaper than a missed real-time violation that causes audio dropouts in production.

## Violation categories — flag any of these in audio-path code

### CRITICAL: Allocations

- `new T(...)` / `new T[]`, `delete`, `delete[]`, `std::malloc`, `std::free`
- `std::make_unique`, `std::make_shared`
- `juce::String s = "..."` or `juce::String x = a + b` (allocates internally)
- `std::vector::push_back`, `::emplace_back`, `::resize`, `::reserve`, `::insert` (may allocate)
- `std::map`, `std::unordered_map`, `std::set` mutations (allocate nodes)
- `juce::Array::add`, `juce::OwnedArray::add` (may allocate)
- `std::function` construction with a non-trivial closure
- Any `printf`-family with a `%s` of a `std::string::c_str()` from a fresh string (the fresh string allocated)

### CRITICAL: Locks (anything that can block the audio thread)

- `std::mutex`, `std::recursive_mutex`, `std::lock_guard`, `std::unique_lock`, `std::scoped_lock`
- `juce::CriticalSection`, `juce::ScopedLock`
- `pthread_mutex_lock` / unlock
- Condition variables (`std::condition_variable`, `wait_for`, `wait_until`)

**Acceptable lock-free alternatives:**
- `std::atomic<T>` for primitive types (load/store/CAS) — flag CAS *loops* that may spin too long
- `juce::SpinLock` — only OK if the critical section is provably bounded and very short (~10s of cycles)
- `juce::AbstractFifo` — single-producer-single-consumer ring buffer, RT-safe
- Custom SPSC queues following the same pattern

### CRITICAL: Blocking I/O

- File I/O: `juce::File::loadFileAsString`, `::create`, `::deleteFile`, raw `fopen/fread/fwrite/fclose`, `std::ifstream`, `std::ofstream`, `read()`, `write()`, `open()`, `close()`
- Network: `socket`, `recv`, `send`, `connect`, `accept`, `select`, `poll`, `kqueue`, ixwebsocket calls
- DNS: `getaddrinfo`, `gethostbyname`
- `std::cout`, `std::cerr`, `printf`, `fprintf`, `puts`
- `juce::Logger::writeToLog`, `DBG(...)`, `JUCE_DBG`

### HIGH: System calls and time

- `sleep`, `usleep`, `nanosleep`, `std::this_thread::sleep_for`
- `gettimeofday`, `clock_gettime` — usable but expensive; worth noting
- `time(NULL)` — fine but date formatting (`strftime`) usually allocates

### MEDIUM: Hidden allocations

- String concatenation: `juce::String x = "foo" + bar`
- Format functions: `juce::String::formatted(...)`, `std::format`, `std::ostringstream`
- Lambda captures by value of non-trivial types
- `std::function` parameters that aren't `std::move`d in
- Implicit conversions to `juce::String` from `const char*` (allocates a heap buffer)

### MEDIUM: Exceptions

- `throw` in audio-path code
- Functions called that may throw (most STL allocators throw `std::bad_alloc`)

### LOW: Worth noting but not always bugs

- Virtual dispatch — not RT-unsafe per se, but inhibits inlining and may cache-miss
- Dynamic casts in audio path — `dynamic_cast<T*>` walks the RTTI table

## How to investigate

1. **Identify the audio path**:
   ```bash
   git -C "${CLAUDE_PROJECT_DIR:-.}" grep -nE 'processBlock|audioDeviceIOCallback|getNextAudioBlock' src/ | head
   ```
   Note the files. For each function on the path, identify its callees by reading the code.

2. **Identify recently changed files** (focus the audit):
   ```bash
   git -C "${CLAUDE_PROJECT_DIR:-.}" diff --name-only HEAD~5...HEAD -- 'src/**/*.cpp' 'src/**/*.h'
   ```
   Or, if invoked with a specific file/directory in context, focus there.

3. **Grep for violations**:
   ```bash
   git grep -nE '\bnew\b|std::make_(unique|shared)|push_back|emplace_back|std::mutex|std::lock_guard|juce::CriticalSection|DBG\(|juce::Logger::|sleep|fopen|fread|fwrite|throw ' <files>
   ```
   Refine the regex per category. Match against your understanding of the audio path.

4. **For each hit, decide**:
   - Is this on the audio path? (If unsure → flag.)
   - Is it actually unsafe? (E.g. a `juce::String` *member* that's never re-assigned in `processBlock` is fine; a `juce::String` *constructed* in `processBlock` is not.)
   - What's the suggested fix?

## Output format

Produce a markdown report with this structure. If invoked from a phase-review
flow, write to `${CLAUDE_PROJECT_DIR}/.planning/phases/<active>/<phase>-RT-AUDIO-REVIEW.md`;
otherwise return the report directly.

```markdown
# Real-time audio safety review

**Scope**: <what you reviewed — files, commits, or "all of src/core + src/threading">
**Audio path entry points identified**: <list>

## Findings

### CRITICAL (audio dropouts / xruns guaranteed if hit)

- **`src/core/foo.cpp:142`** — `juce::String log = "received " + msg;` allocates on every audio block.
  - *Why*: `juce::String` heap-allocates when concatenating.
  - *Fix*: pre-allocate `juce::String log;` as a member; use `log.preallocateBytes(N)`; or remove the log entirely and emit via a lock-free FIFO consumed by a non-RT thread.

### HIGH

(...)

### MEDIUM

(...)

### LOW / informational

(...)

## Files audited

- src/core/njclient.cpp ✓
- src/threading/audio_thread.cpp ✓
- ...

## What I did NOT cover

- <gaps in your audit, e.g. "I didn't trace the OSC receiver into the audio path">
```

If there are zero findings, say so explicitly with a short note on what you checked.

## Important rules

- **Never edit code.** You are read-only.
- **Be specific.** Every finding must have file:line and a one-line *why*. Vague findings get ignored.
- **Distinguish "allocates" from "may allocate".** A `std::vector::push_back` only allocates if size == capacity; if you can see capacity is reserved upstream, downgrade severity.
- **Pre-existing patterns aren't automatic passes.** If `nj_crypto.cpp` allocates on the audio path, flag it even if it's been there for years — it just means nobody checked yet.
- **Recommend lock-free patterns from JUCE.** This codebase already uses JUCE; suggest `juce::AbstractFifo`, `juce::SpinLock`, `juce::ReadWriteLock` over rolling custom primitives.
- **Note when you can't trace the call graph.** If a virtual function or callback obscures whether something is RT-reachable, say so in "What I did NOT cover."

## Phase 20 audit allowlist envelope

> **Decision authority:** CONTEXT.md D-08 / D-09 / D-19 / D-20, R2 H6 + H7, R4 H8 + M12 + M13.
> **Status:** **Published 2026-05-16 by Plan 20-00; line-number-refreshed
> 2026-05-16 by Plan 20-02 Task 4 (R4 M13 gate closed — all per-site entries
> below carry concrete `src/core/njclient.cpp:<line>` references from the
> as-committed Plan 20-02 Task 1 + Task 2 code, no placeholder tokens
> remain).** The phase-close grep gate for unreplaced placeholder tokens
> still applies to future audit-allowlist edits.

The following audio-thread sites are accepted Phase 15.1 carve-outs under
CONTEXT.md D-09 + D-20 (full rationale: the user has direct evidence the
NinjamZap mutex/alloc/RNG-on-audio-thread substrate works in production on
iOS/Android; JamWide ships at NinjamZap parity per
`feedback_proven_over_pure`). All other audio-path violations remain
CRITICAL — these are the only carve-outs accepted under the envelope.

### src/core/njclient.cpp `NJClient::on_new_interval` video block (Plan 20-02 Task 1)

- `src/core/njclient.cpp:5186` — `WDL_MutexLock vlock(&m_video_cs)` opening the whole-block critical section (D-08, NinjamZap-literal whole-block serialization)
- `src/core/njclient.cpp:5228` — `WDL_MutexLock slock(&m_video_spspps_cs)` nested inside m_video_cs for SPS/PPS read (D-03)
- `src/core/njclient.cpp:5215` — `readGuidSeqlock(*lc, marker + 8)` atomic two-uint64_t halves seqlock read of the canonical audio_ch0_guid (D-20 + R4 H8 carve-out; audit-allowlist scope: **atomic-halves load only** — no plain non-atomic byte read from the audio thread)
- `src/core/njclient.cpp:5202-5209` — `marker[N]=...` writes followed by `std::memset(marker + 8, 0, 16)` zero-init of the audio_ch0_guid slot of the 24-byte marker on stack (safe: stack-local, not shared)
- `src/core/njclient.cpp:5192` — `RawDataSendWrite(m_video_guid, NULL, 0, true)` END-previous call inside on_new_interval video block
- `src/core/njclient.cpp:5194` — `RawDataSendBegin(m_video_guid, m_video_fourcc, m_video_chidx, 0)` BEGIN-new call
- `src/core/njclient.cpp:5218` — `RawDataSendWrite(m_video_guid, marker, 24, false)` marker write
- `src/core/njclient.cpp:5230` — `RawDataSendWrite(m_video_guid, m_video_spspps.Get(), m_video_spspps.GetSize(), false)` SPS/PPS write
- `src/core/njclient.cpp:5235` — `RawDataSendWrite(m_video_guid, NULL, 0, true)` END-at-deactivate call

### src/core/njclient.cpp `NJClient::QueueVideoFrame` (encoder-thread caller of the same shared substrate; Plan 20-01/20-02 Task 1)

- `src/core/njclient.cpp:3287` — `WDL_MutexLock vlock(&m_video_cs)` opening the critical section (audio-thread cohabitant of m_video_cs — same envelope applies)
- `src/core/njclient.cpp:3296` — `RawDataSendWrite(m_video_guid, prefix, 4, false)` 4-byte BE length-prefix write (COD-02 two-call split, first call)
- `src/core/njclient.cpp:3297` — `RawDataSendWrite(m_video_guid, data, len, false)` NAL data write (COD-02 two-call split, second call)

### src/core/njclient.cpp `NJClient::SetVideoChannel` / `SetVideoBroadcastActive` / `SetVideoSPSPPS` (run/message/encoder-thread; not audio-thread, listed for completeness as cohabitants of m_video_cs / m_video_spspps_cs)

- `src/core/njclient.cpp:3264` — `WDL_MutexLock vlock(&m_video_cs)` in `SetVideoChannel` (run thread, connect-up; not audio-thread)
- `src/core/njclient.cpp:3272` — `WDL_MutexLock vlock(&m_video_cs)` in `SetVideoBroadcastActive` (message thread, Broadcast button; not audio-thread)
- `src/core/njclient.cpp:3303` — `WDL_MutexLock slock(&m_video_spspps_cs)` in `SetVideoSPSPPS` (encoder thread, Plan 20-01 publishSpsPps callback; not audio-thread)

### src/core/njclient.cpp `Local_Channel` seqlock writer call sites (Plan 20-02 Task 2)

- `src/core/njclient.cpp:2681` — `writeGuidSeqlock(*lc, zero16)` at the interval-boundary memset-to-zero broadcast-deactivate path (run thread; not audio-thread; publishes 16 zero bytes via atomic halves so audio-thread reader observes consistent NONE-match state)
- `src/core/njclient.cpp:2721` — `writeGuidSeqlock(*lc, lc->m_curwritefile.guid)` at the `m_need_header` WDL_RNG_bytes site (run thread; not audio-thread; publishes freshly-generated 16-byte GUID via atomic halves — closes R3 MF1 + R4 H8 by writing to the atomic halves the audio thread will read via readGuidSeqlock)

### src/core/njclient.cpp `NJClient::RawDataSendBegin` (any thread; called from audio thread inside on_new_interval; landed in Plan 20-00)

- `src/core/njclient.cpp:3181` — `m_rawdata_cs.Enter() / .Leave()` via `enter_rawdata_cs_with_contention_sample` helper (D-09; NinjamZap-literal — every Add takes m_rawdata_cs)
- `src/core/njclient.cpp:3170` — `WDL_RNG_bytes(outGuid, 16)` (D-09 — internally locked, per-call cost ~5 µs)
- `src/core/njclient.cpp:3172` — `new RawDataQueueItem` (D-09 — single heap alloc per call; allocation cost is the NinjamZap-faithful substrate cost)

### src/core/njclient.cpp `NJClient::RawDataSendWrite` (any thread; called from audio thread inside on_new_interval AND from encoder thread inside QueueVideoFrame; landed in Plan 20-00)

- `src/core/njclient.cpp:3225` — `m_rawdata_cs.Enter() / .Leave()` via `enter_rawdata_cs_with_contention_sample` helper (D-09)
- `src/core/njclient.cpp:3200` — `new RawDataQueueItem` + `item->data.ResizeOK(dataLen)` + `memcpy(item->data.Get(), data, dataLen)` of payload (D-09)

### src/core/njclient.cpp on the RawDataSendBegin / RawDataSendWrite paths

**No `writeLog` calls remain.** Plan 20-00 stripped the three writeLog calls
at the original 14.3-02 lines 3015 / 3048 / 3063 per R2 H7 + R4 M12. The pre-
removal caller audit (R4 M12) confirmed those three logs all reported on the
retired SPSC overflow / OOM path and no non-video production caller relies on
any of them for a required production diagnostic. NinjamZap source does not
have those writeLog calls; the JamWide port now matches.

### R4 M13 enforcement (audit-allowlist line-number refresh) — CLOSED

Plan 20-02 Task 4 (commit refreshing this file) replaced every placeholder
above with the concrete `src/core/njclient.cpp:<line>` reference for the
on_new_interval video block (Task 1), the QueueVideoFrame two-call split
(Task 1), the SetVideo* API mutex sites (Task 1), the seqlock writer call
sites at lines 2681 + 2721 (Task 2), and the inherited Plan 20-00
RawDataSendBegin / RawDataSendWrite carve-out lines. The phase-close grep
gate confirms zero unreplaced placeholder tokens remain.

### Auditor zero-CRITICAL gate scope

All other audio-path violations remain CRITICAL — these are the only carve-
outs accepted under the **D-09 (Phase 20)** + **D-16 (Phase 21, parity-
only)** envelopes. The auditor zero-CRITICAL gate applies OUTSIDE these
envelopes. Phase 21's envelope explicitly forbids generalization-by-analogy
to other subsystems.

## Phase 21 audit allowlist envelope

> **Decision authority:** CONTEXT.md D-14 / D-15 / D-16 / D-20-mirror +
> REVIEWS.md codex Cluster 1.
> **Status:** Published 2026-05-17 by Plan 21-01 Task 4; line-number-
> refreshed by the same commit (Task 2 + Task 3 lines are stable after
> this commit lands).

> **The Phase 21 envelope is accepted ONLY as parity with the upstream NinjamZap source port** (`ninjamzap-core/njclient.cpp:3084-3256`). The envelope MUST NOT be treated as a general permission for similar audio-thread work in unrelated subsystems. Specifically:
>
> - The `m_video_recv_cs` acquire on the audio thread is accepted BECAUSE
>   the upstream NinjamZap audio-thread `on_new_interval` does the
>   equivalent acquire and JamWide is a byte-for-byte port. Any FUTURE
>   audio-thread mutex acquire in another subsystem requires a SEPARATE
>   design review — it does not inherit acceptance from Phase 21.
> - The `VideoRecvBuffer::copyFrom` up to 4 MB on the audio thread is
>   accepted BECAUSE it is bounded (one per peer per swap; pre-allocated;
>   realloc-free per D-11) AND because upstream does the equivalent. Any
>   FUTURE audio-thread memcpy in another subsystem requires its own
>   bounded-cost analysis.
> - The `m_remoteuser_mirror[s].chans[0].next_ds[0]->guid` read is accepted
>   via the Phase 15.1-07a HIGH-2 carve-out which already underwent its
>   own design review.
>
> **Timing instrumentation requirement (codex Cluster 1):** the
> `runVideoReceiveBlock_` helper carries a JAMWIDE_BUILD_TESTS-gated
> `std::chrono::steady_clock` timing wrapper (Plan 21-01 Task 2). Tests
> are required to read `getRunVideoReceiveBlockMaxNanosForTest()` and
> assert sane bounds under simulated peer load. Phase 21-03 UAT MUST
> report a 3-peer max-nanos number; if it exceeds 1 ms (1,000,000 ns)
> per swap under steady-state, escalate to a follow-up design review.

### src/core/njclient.cpp `NJClient::runVideoReceiveBlock_()` (Plan 21-01 Task 2 — single source of truth for audio-thread video receive)

- `src/core/njclient.cpp:3723` — `WDL_MutexLock vrlock(&m_video_recv_cs)` opening the whole-block critical section (D-15, NinjamZap-literal whole-block serialization parallel to D-08 send-side)
- `src/core/njclient.cpp:3724` — `m_video_streams.GetSize()` iteration entry — read-only structural traversal under `m_video_recv_cs`
- `src/core/njclient.cpp:3735` — `vs->playing.copyFrom(vs->pending)` STAGE-1 promote (D-16 accepts Resize + memcpy up to 4 MB per peer per swap; cost ~0.4 ms per peer at HD per RESEARCH §Pitfall 6)
- `src/core/njclient.cpp:3754-3784` — iteration over `m_remoteuser_mirror[s]` reading `chans[0].next_ds[0]->guid` for the GUID-pair comparison (Phase 15.1-07a HIGH-2 carve-out reused; ONLY the `.guid` field is read, no other DecodeState member)
- `src/core/njclient.cpp:3796` — `const int kHoldCapDrop = 4` literal — no allocation, scalar comparisons only
- `src/core/njclient.cpp:3826` — `vs->pending.copyFrom(vs->next)` DS-match defer path (1-swap defer; copyFrom carve-out)
- `src/core/njclient.cpp:3834` — `vs->playing.copyFrom(vs->next)` PREV-match immediate play path (copyFrom carve-out)
- `src/core/njclient.cpp:3840` — `memset(vs->prev_ds_guid, ...)` 16-byte scalar memcpy/memset of GUID — bounded, no realloc
- scalar reads/writes throughout: `vs->next.active`, `vs->next.frameCount`, `vs->next.audio_guid` (16-byte memcmp), `vs->next.sender_seq`, `vs->frameOffsets` reads, `vs->hold_count`, `vs->empty_count`, `vs->synced`, `vs->last_played_sender_seq`, `vs->last_played_audio_guid`, `vs->drop_resync_count`, `vs->append_active`, `vs->append_to_next`, `vs->append_to_pending` — all bounded, no allocation
- **(JAMWIDE_BUILD_TESTS-only)** `src/core/njclient.cpp:3870-3879` — `std::chrono::steady_clock` measurement around the inner scope; CAS-update of `m_run_video_receive_block_max_nanos_` via `compare_exchange_weak`; relaxed-order store on `m_run_video_receive_block_last_peer_count_`. **Zero overhead in production builds — entire block compiles out (codex Cluster 1).**
- **(Forward reference)** Plan 21-02 (next wave) will push a slot snapshot onto a per-peer SPSC at the end of the SWAP block. Per CONTEXT.md D-12 revised + codex Cluster 2 Option A: snapshot lives in a `VideoRecvState`-owned ring of 4 pre-allocated 4 MB buffers; the audio-thread cost is one bounded memcpy + one integer push (within the copyFrom allowlist).

### src/core/njclient.cpp on_new_interval call site

- `src/core/njclient.cpp:6010` — `runVideoReceiveBlock_();` invocation; ADJACENT to the Phase 20 send-side video block (different mutex — `m_video_recv_cs` vs `m_video_cs`).

### src/core/njclient.cpp on the audio-thread receive path

**No `writeLog` / `SYNCLOG` / `printf` calls remain.** Plan 21-01 Task 2
stripped upstream SYNCLOG sites. NinjamZap source has `SYNCLOG` which is
a no-op-able macro; JamWide port does NOT carry over the macro nor any
equivalent — symmetric to Phase 20 R2 H7 + R4 M12.

**No `WaitableEvent::signal()` calls remain (codex Cluster 1).** Plan
21-02 redesign moves the decoder-thread wake-up off the audio thread
entirely — Plan 21-02's `pushSlotSnapshot` will use an atomic sequence
counter that the decoder thread polls with a short timed wait, NOT a
`juce::WaitableEvent::signal()` from the audio thread. See Plan 21-02
for the implementation when it lands.

### src/core/njclient.cpp on the run-thread BEGIN/WRITE/END dispatch sites (Plan 21-01 Task 2 — augmentations of existing Phase 14.3-03 handlers; NOT audio-thread, listed for envelope completeness)

- `src/core/njclient.cpp:2415` — `handleVideoRecvBegin_(dib.guid, dib.fourcc, dib.username, dib.chidx)` invocation inside the BEGIN raw-data branch (run thread; helper acquires `m_video_recv_cs` internally — not audio thread)
- `src/core/njclient.cpp:2540` — `handleVideoRecvWrite_(diw.guid, diw.audio_data, diw.audio_data_len)` invocation inside the WRITE raw-data branch (run thread)
- `src/core/njclient.cpp:2555` — `handleVideoRecvEnd_(diw.guid)` invocation inside the END raw-data branch (run thread)

### src/core/njclient.cpp on the user-leave video-state reset path (Plan 21-01 Task 2 — augmentation of the m_remoteusers Delete site; run thread, NOT audio thread)

- `src/core/njclient.cpp:2187-2202` — `m_video_recv_cs.Enter()` + iteration over `m_video_streams` resetting matching VideoRecvState's `prev_ds_guid` + slot members + `m_video_recv_cs.Leave()`. Run thread inside `m_users_cs`-protected branch; verbatim port of upstream `:1306-1322`. Not audio thread; listed for envelope completeness.

### Phase 21 timing instrumentation surface (codex Cluster 1)

`getRunVideoReceiveBlockMaxNanosForTest()` (JAMWIDE_BUILD_TESTS-only,
declared in `src/core/njclient.h` under the JAMWIDE_BUILD_TESTS guard,
body at `src/core/njclient.cpp:3889-3897`) is the required UAT
measurement surface. Plan 21-03 UAT MUST report a 3-peer max-nanos
number and record it in the UAT report. If 3-peer max-nanos exceeds
1 ms (1,000,000 ns) per swap under steady-state, escalate to a
follow-up design review — Plan 21-02's slot-handoff redesign may need
a smaller per-slot cap or the parser may need to move out of D-16's
envelope.
