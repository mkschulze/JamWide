---
phase: 21
reviewers: [codex]
reviewed_at: 2026-05-17T14:36:27Z
plans_reviewed: [21-01-PLAN.md, 21-02-PLAN.md, 21-03-PLAN.md]
notes: |
  Only codex was invoked. claude was skipped because the orchestrating CLI is
  Claude Code (CLAUDE_CODE_ENTRYPOINT=cli), so a same-model claude review would
  not have provided independence. Other CLIs (gemini, coderabbit, opencode,
  qwen, cursor) are not installed on this machine.
---

# Cross-AI Plan Review — Phase 21

> **Reviewer count:** 1 (codex). Treat the "Consensus Summary" section below as
> a synthesis of codex's findings, not multi-reviewer agreement. For
> architecturally novel phases (which Phase 21 is — per-peer threaded decoder
> with raw sink pointers and a 4-stage SWAP state machine), a second
> independent reviewer is recommended; install `gemini` or `opencode` and
> re-run `/gsd-review --phase 21` if desired.

## Codex Review

## Plan 21-01 Summary

Plan 21-01 is directionally sound: it correctly treats the receive state machine as a NinjamZap source-port, scopes decoder/render work out of the first wave, and adds focused WIRE-02 tests. The main risk is not intent but mechanical fidelity: several parts duplicate large state-machine blocks into test helpers, and the audio-thread allowlist may normalize expensive work that should remain tightly bounded and measured.

**Strengths**

- Correctly isolates WIRE-02 state-machine work before decoder/UI layers.
- Uses upstream NinjamZap source ranges as the authority, not docs.
- Calls out the Phase 15.1 mirror substitution for `m_remoteuser_mirror[s].next_ds[0]->guid`.
- The nine state-machine tests cover the right core cases: DS defer, PREV immediate, HOLD/drop, split frames, marker parse, burst BEGIN, user leave.

**Concerns**

- **HIGH:** The plan allows `m_video_recv_cs` and up to 4 MB `copyFrom` on the audio thread. Even if audit-allowed, this is still a real audio risk under 3 to 8 peers. The plan should require timing instrumentation around the receive SWAP block from the first implementation wave, not defer measurement to later UAT.
- **MEDIUM:** Duplicating production BEGIN/WRITE/END logic into `DispatchTestVideoRecv*` helpers can let tests drift from production. Prefer extracting shared private helpers and calling them from both production and tests.
- **MEDIUM:** The plan says `m_video_streams.Empty(true)` in destructor, but user-leave keeps `VideoRecvState` alive. That means stale per-peer state can accumulate across many unique usernames. It is accepted, but the memory math should be stated as `4 slots × 4 MB = 16 MB per stale peer`, before decoder/sink memory.
- **LOW:** The grep-based acceptance checks are useful but too weak to prove byte-faithful upstream behavior.

**Suggestions**

- Extract shared private methods such as `handleVideoRecvBegin_`, `handleVideoRecvWrite_`, `handleVideoRecvEnd_`, and `runVideoReceiveBlock_`; have both production and test dispatchers call those.
- Add a `JAMWIDE_BUILD_TESTS` timing counter for `runVideoReceiveBlock_`, recording max duration and peer count.
- Add explicit malformed-length tests: zero-length frame, prefix larger than 4 MB, incomplete marker, SPS/PPS-like chunk with invalid lengths.
- In the audit allowlist, distinguish “accepted for phase parity” from “safe in general”; the envelope should not make future audio-thread allocations acceptable by analogy.

**Risk Assessment: MEDIUM**

The protocol plan is strong, but the audio-thread mutex/copy envelope is inherently risky and needs hard measurement, not just documentation.

---

## Plan 21-02 Summary

Plan 21-02 has the right high-level separation after the D-12 revision: audio thread pushes a whole slot view, decoder thread parses and feeds libavcodec. However, this plan contains the most serious correctness and lifetime hazards. The proposed `VideoRecvBufferView` plus “decoder-owned memcpy” design is underspecified and likely unsafe as written, and `pushSlotView` performs work that is not real-time safe.

**Strengths**

- Correctly moves AVCC parsing off the audio thread.
- Uses libavcodec send/receive API, single-threaded decode, Annex-B SPS/PPS, and `sws_scale`.
- Includes good decoder tests: corrupt NAL recovery, mid-stream SPS/PPS, resolution changes, codec-name verification.
- The parser-thread test is a useful architectural guard.

**Concerns**

- **HIGH:** `pushSlotView` is called from the audio thread and does a bounded 4 MB memcpy plus `pending_event_.signal()`. The memcpy is already expensive; signaling a `juce::WaitableEvent` from the audio thread can enter OS synchronization paths. That violates the project’s RT-safety rule even if the plan tries to include it in the envelope.
- **HIGH:** The “decoder-owned 4 MB `slotCopyBuf_`” is unsafe with `SpscRing<VideoRecvBufferView, 4>` if multiple views are queued. Every queued view may point into the same buffer, and later pushes overwrite earlier queued bytes before the decoder pops them. This can corrupt frames or cross-contaminate peers/intervals. Use a ring of four owned buffers or make the queued object own its payload.
- **HIGH:** `VideoRecvBufferView` includes `frameOffsets` as a raw pointer. If only slot bytes are copied but offsets remain pointing at `vs->playing.frameOffsets`, the decoder thread still depends on mutable receive-state storage after the mutex is released. Copy offsets too, or enqueue an owned snapshot containing bytes plus offsets.
- **MEDIUM:** The wire-format description is inconsistent. In some places the parser treats `frameOffsets` as already stripping the 4-byte outer prefix; elsewhere it describes per-frame `[4B BE len + NAL]`. This is exactly the class of bug Phase 20 hit. The plan must define precisely whether `VideoRecvBuffer::data` stores outer prefixes, payload only, or mixed data.
- **MEDIUM:** Test fixtures are described as Annex-B SPS/PPS and IDR, but the live parser receives NinjamZap slot bytes with no Annex-B start codes in SPS/PPS inner chunks. Tests that mostly use `pushNalChunk` can pass while the actual `pushSlotView` parser is wrong.
- **MEDIUM:** `pollOneFrameForTest` with a raw `juce::Image* test_capture_image_` can race if the test times out while the decoder later writes to that pointer. It needs a lock or an owned callback/result queue.
- **LOW:** `std::atomic<std::thread::id>` is not guaranteed to be supported/lock-free on all standard libraries. For tests this is probably fine, but a simple `std::mutex`-protected value under `JAMWIDE_BUILD_TESTS` is safer.

**Suggestions**

- Replace `SpscRing<VideoRecvBufferView, 4>` with an owned payload type, for example:
  `struct VideoRecvSlotSnapshot { std::array<uint8_t, 4MB> bytes; int size; std::array<int, MaxFrames> offsets; int frameCount; }`, or a fixed ring of four preallocated buffers owned by `VideoRecvState`.
- Do not call `WaitableEvent::signal()` from the audio thread. Use polling on the decoder thread with short wait, an atomic sequence counter, or a non-RT producer notification already accepted elsewhere in the project.
- Add a test that pushes two slot views back-to-back before the decoder pops either; assert the first decoded frame is still the first slot’s content. This catches the single-buffer overwrite bug.
- Add a test using full NinjamZap slot bytes through `pushSlotView`, not just `pushNalChunk`, for SPS/PPS + IDR.
- Pin the parser contract in comments and tests: whether `frameOffsets[i]` points to the frame payload start or to the 4-byte BE prefix.

**Risk Assessment: HIGH**

The decoder architecture is salvageable, but the current audio-to-decoder handoff has likely data-lifetime bugs and RT-safety violations.

---

## Plan 21-03 Summary

Plan 21-03 addresses the right final integration surfaces: distributor, peer sink, processor ownership, lazy lifecycle, and end-to-end tests/UAT. The distributor API is sensible, but the lifecycle ordering is currently unsafe. Decoder threads hold raw `PeerVideoSink*` pointers owned by the distributor; the plan removes/destroys sinks while decoders may still be running or may still have stale sink pointers.

**Strengths**

- Mirrors the proven Phase 19 subscription pattern.
- Correctly supports subscribe-before-peer-exists and multiple subscribers per peer.
- Adds an integration test for three-peer isolation.
- UAT procedure maps cleanly to the four roadmap success criteria.

**Concerns**

- **HIGH:** User-leave teardown order is wrong. The plan calls `removeSink()` before `vs->decoder.reset()`. If the decoder thread is still inside `scaleAndSwapImage_` or about to call `sink_->triggerAsyncUpdate()`, `sink_` is dangling. Reset/stop decoder first, clear its sink pointer, then remove the sink.
- **HIGH:** Processor destructor ordering is unsafe as justified. The plan resets `remoteFrameDistributor` before `client.reset()`, while `NJClient` still owns `VideoRecvState` objects whose decoders hold raw sink pointers. Even if the decoder destructor “should not” call the distributor, it may still have a sink pointer. Destroy `client` before `remoteFrameDistributor`, or make sinks shared/weak and explicitly detach decoders first.
- **HIGH:** `ensureVideoDecoderForPeer_` is called under `m_video_recv_cs` in the BEGIN handler and performs sink creation, decoder allocation, `avcodec_open2`, and thread start. While this is on the run thread, it holds the same mutex the audio thread needs during `on_new_interval`. This can block the audio thread for milliseconds. Move expensive decoder/sink startup outside `m_video_recv_cs`.
- **MEDIUM:** `PeerVideoSink::removeListener` plus `handleAsyncUpdate` needs careful lock ordering. If `removeSink()` destroys a sink while `handleAsyncUpdate` is queued on the message thread, destructor must cancel pending async updates or wait for in-flight dispatch.
- **MEDIUM:** The 3-peer e2e test depends on real asynchronous decoder output but may be flaky unless it has deterministic waits and explicit decoder shutdown ordering.
- **MEDIUM:** UAT is marked blocking but allows Cells 2 and 3 to be BLOCKED while claiming phase closure. Those are explicit Phase 21 success criteria, not optional stretch cases. If blocked, call that out as deferred risk rather than “closed.”
- **LOW:** Using fixed `320×240` sink size at BEGIN is pragmatic, but it conflicts with D-07’s “fixed at first-seen peer resolution.” At BEGIN the first frame resolution is not known. This should be documented as “fixed v1.3 receive surface size,” not first-seen resolution.

**Suggestions**

- Teardown order should be:
  1. Stop and join decoder.
  2. Set decoder sink pointer to null or destroy decoder.
  3. Clear `vs->sink`.
  4. Remove sink from distributor.
- Processor destructor should reset `client` before `remoteFrameDistributor`, unless all decoder/sink pointers are detached earlier.
- Change lazy startup to a two-phase flow: under `m_video_recv_cs`, mark that decoder is needed and capture key; outside the mutex, create decoder/sink; re-enter briefly to install pointers if still valid.
- Make `Openh264Decoder::setSink(nullptr)` available and call it before sink destruction.
- Add a test: peer leave while decoder is actively decoding. Run under ASan/TSan if available.
- Add a test: distributor destroyed while subscriptions still exist, or explicitly document Phase 22 must ensure popouts close before processor teardown.

**Risk Assessment: HIGH**

The final integration has serious use-after-free and audio-thread blocking risks. These need design fixes before execution.

---

## Cross-Plan Concerns

- **HIGH:** The `VideoRecvState` ownership model evolves across plans, but the final destructor contract is not coherent. `VideoRecvState` owns decoder; distributor owns sink; decoder stores raw sink pointer. That triangle needs a formal shutdown protocol.
- **HIGH:** The audio thread still performs lock acquisition, copy work, and possibly decoder wake-up. The allowlist is too broad unless every operation is measured and bounded.
- **HIGH:** The slot handoff between Plan 21-01 and 21-02 is the weakest technical seam. Raw pointers in `VideoRecvBufferView` are not enough unless the queued data and offsets are fully owned until consumed.
- **MEDIUM:** Tests emphasize unit-level mechanisms well, but only UAT validates 5-minute no-drift behavior. Add at least an instrumented integration counter for sender_seq/audio_guid alignment over many simulated intervals.
- **MEDIUM:** Wire-format tests should use byte fixtures copied from or generated by the NinjamZap harness, including the exact 24-byte marker with 4-byte BE prefix value `20`.

## Overall Assessment

Plan 21 is well-researched and mostly decomposed correctly, but I would not execute it unchanged. Plan 21-01 is acceptable with tighter helper extraction and timing. Plan 21-02 needs a redesigned owned slot snapshot or multi-buffer queue before it is safe. Plan 21-03 needs teardown ordering fixed before any decoder/sink integration lands.

Overall risk: **HIGH**, driven by lifetime/use-after-free hazards and RT-safety issues at the audio-to-decoder handoff.

---

## Consensus Summary

Single-reviewer synthesis. The reviewer (codex) found the plans **directionally
sound and well-researched**, but identified **10 HIGH-severity issues** that
should be addressed before execution — concentrated in two areas:

### Highest-priority concerns (must fix before execute-phase)

1. **Audio-thread RT-safety envelope is too broad (Plans 21-01 & 21-02)** —
   `m_video_recv_cs` mutex acquisition + up to 4 MB `copyFrom` + a
   `WaitableEvent::signal()` from the audio thread were all marked acceptable
   under an audit allowlist. The reviewer's verdict: even with allowlist
   coverage, this is real risk under 3–8 peers. **Required mitigation:**
   instrument timing of `runVideoReceiveBlock_` (max duration, peer count)
   from the first implementation wave — do not defer measurement to UAT.

2. **VideoRecvBufferView ownership is unsound (Plan 21-02)** — A
   `SpscRing<VideoRecvBufferView, 4>` where each view points into a single
   shared decoder-owned `slotCopyBuf_` will corrupt frames as soon as two
   pushes happen back-to-back before the decoder pops the first.
   `frameOffsets` is also a raw pointer into mutable receive-state storage
   that the decoder thread continues reading after the mutex is released.
   **Required mitigation:** either (a) a ring of four preallocated owned
   buffers in `VideoRecvState`, or (b) `VideoRecvSlotSnapshot` that owns both
   bytes AND offsets as a single value-type payload.

3. **Lifetime triangle is incoherent (Plan 21-03 + cross-plan)** —
   `VideoRecvState` owns decoder; distributor owns sink; decoder holds a raw
   `PeerVideoSink*`. The plan's teardown order (`removeSink()` before
   `vs->decoder.reset()`) leaves the decoder thread holding a dangling sink
   pointer during its final `triggerAsyncUpdate()`. Same shape of bug in the
   processor destructor (resets `remoteFrameDistributor` before
   `client.reset()`).  **Required mitigation:** formal shutdown protocol —
   stop+join decoder → null the decoder's sink pointer → clear `vs->sink` →
   remove sink from distributor; processor destructor resets `client` before
   `remoteFrameDistributor`.

4. **Lazy decoder startup blocks audio thread (Plan 21-03)** —
   `ensureVideoDecoderForPeer_` runs under `m_video_recv_cs` in BEGIN and
   does sink creation + `avcodec_open2` + thread start — millisecond-scale
   work holding the audio thread's mutex. **Required mitigation:** two-phase
   lazy startup — under mutex, mark "decoder needed" and capture key;
   outside mutex, create decoder/sink; re-acquire briefly to install.

### Lower-priority recommendations

- Plan 21-01 test helpers duplicate production BEGIN/WRITE/END state machine —
  extract shared private helpers, call from both production and tests.
- Plan 21-02 wire-format contract for `VideoRecvBuffer::data` is inconsistent
  between sections (outer-prefix-stripped vs raw `[4B BE len + NAL]`). Pin
  the contract in comments + tests. Phase 20's 24-vs-20-byte marker bug was
  the same shape.
- Plan 21-02 tests use `pushNalChunk` for Annex-B fixtures; live parser sees
  NinjamZap slot bytes. Add at least one full-slot-bytes integration test
  through `pushSlotView`.
- Plan 21-03 UAT allows Cells 2 and 3 (mid-stream join, HOLD→resume) to be
  marked BLOCKED while closing the phase. Those are phase success criteria
  per the roadmap; if blocked they should be tracked as deferred risk, not
  phase closure.
- Cross-plan: add NinjamZap-harness-generated byte fixtures for the 24-byte
  marker (specifically the `[4B BE prefix=20]` outer length) to prevent a
  repeat of the Phase 20 marker-size regression.

### Agreed Strengths (single-reviewer; codex)

- Decomposition into 3 plans is correct: WIRE-02 state machine first,
  decoder substrate second, integration + distributor last.
- Using upstream NinjamZap source ranges (`njclient.h:334-417`,
  `:1300-1550`, `:3084-3219`) as the authority rather than the docs —
  exactly the lesson from Phase 20.
- Phase 15.1 mirror substitution for `next_ds[0]->guid` is correctly
  identified.
- Subscriber/listener pattern in Plan 21-03 mirrors the proven Phase 19
  design.

### Divergent Views

N/A — single reviewer. If a second independent review is added later, this
section should capture disagreements as the highest-signal items to dig
into.

## Recommended Next Step

The reviewer's verdict is "I would not execute it unchanged." The HIGH-severity
findings — particularly the slot-buffer ownership (Plan 21-02) and teardown
ordering (Plan 21-03) — are design-level, not implementation polish. They
warrant a replan pass:

```
/gsd-plan-phase 21 --reviews
```

This will feed `21-REVIEWS.md` back into the planner, which should:

1. Redesign the audio→decoder handoff in Plan 21-02 around an owned-payload
   queue (either preallocated ring of 4 buffers in `VideoRecvState`, or a
   value-type `VideoRecvSlotSnapshot`). Remove `WaitableEvent::signal()` from
   the audio path.
2. Rewrite the teardown protocol in Plan 21-03 (4-step ordered shutdown +
   processor-destructor reorder; add `Openh264Decoder::setSink(nullptr)`
   API).
3. Add timing instrumentation for `runVideoReceiveBlock_` to Plan 21-01,
   not deferred to UAT.
4. Make Plan 21-02's wire-format contract explicit + add a full-slot-bytes
   integration test.
5. Demote Plan 21-03 UAT Cells 2 and 3 from "blocked-but-closed" to "must
   pass or explicitly deferred to a follow-up phase."
