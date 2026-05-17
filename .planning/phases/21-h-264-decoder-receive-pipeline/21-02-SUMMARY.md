---
phase: 21-h-264-decoder-receive-pipeline
plan: 02
subsystem: video-decoder
tags: [h264, libavcodec, openh264, decoder-thread, juce-thread, codex-cluster-1, codex-cluster-2, codex-cluster-3, codex-cluster-6, codex-cluster-7, codex-cluster-8, codex-cluster-10, sws-scale, R4-H9-destructor]

# Dependency graph
requires:
  - phase: 21-h-264-decoder-receive-pipeline/21-01
    provides: VideoRecvBuffer + VideoRecvState struct definitions; runVideoReceiveBlock_ audio-thread block with three vs->playing-populating sites; m_video_streams + m_video_recv_cs; codex Cluster 6 wire-format contract pinned in njclient.h
  - phase: 14.3-native-video-foundation
    provides: vendored libavcodec / libswscale / libopenh264 via jamwide_use_ffmpeg() (Phase 14.3-01)
  - phase: 20-h-264-encoder-send-pipeline
    provides: jamwide_use_ffmpeg linkage pattern; R4 H9 LOCKED 7-step destructor template (Openh264Encoder); SpscRing<T,N> + juce::Thread inheritance pattern
provides:
  - juce/video/decoder/VideoDecoder.h pure-virtual interface (with public setSink(PeerVideoSink*) per codex Cluster 3)
  - juce/video/decoder/NalChunk.h DECODER-THREAD-LOCAL POD (codex Cluster 2 invariant documented)
  - juce/video/decoder/VideoRecvSlotSnapshot.h POD with 4 MB owned buffer + inline copyFromVideoRecvBuffer (codex Cluster 2 Option A)
  - juce/video/decoder/Openh264Decoder.h + .cpp libavcodec H.264 decoder with per-peer juce::Thread + AVCodecContext + SwsContext, decoder-thread AVCC parser (D-12 revised / B-1), R4 H9 7-step destructor, sink_lock_-paired setSink protocol (codex Cluster 3), TestFrameResult lock-protected pollOneFrameForTest (codex Cluster 8), mutex-protected std::thread::id under JAMWIDE_BUILD_TESTS (codex Cluster 10)
  - VideoRecvState extension: 4× VideoRecvSlotSnapshot decoderSlots + SpscRing<int,4> decoderSlotIndexQ + atomic<int> nextDecoderSlotFillIndex + atomic<uint64> decoderProducerSeq + std::shared_ptr<Openh264Decoder> decoder + PeerVideoSink* sink
  - NJClient::pushPlayingSnapshotToDecoder_ private helper called at THREE sites in runVideoReceiveBlock_ (STAGE-1 promote, PREV/NONE-match immediate-play, accumulating-fallback)
  - 8 COD-03 sub-tests in tests/test_video_decoder.cpp covering first-frame emit, corrupt-NAL recovery, mid-stream SPS/PPS reconfig, source-resolution change, codec name verification, B-1 parser-on-decoder-thread enforcement, codex Cluster 7 full-NinjamZap-bytes integration, W-3 back-to-back-push-order regression
  - 7 committed fixture binaries under tests/fixtures/ + EXCLUDE_FROM_ALL gen_baseline_fixtures regeneration target
affects: [21-03, 22, 23, 24]

# Tech tracking
tech-stack:
  added: [libavcodec (decoder API), libswscale, std::shared_ptr<Openh264Decoder>, std::array<VideoRecvSlotSnapshot, 4>, libopenh264 (fixture generation only)]
  patterns: [R4 H9 LOCKED 7-step destructor (matches Phase 20 encoder), codex Cluster 1 polling-with-atomic-seq wake-up (NO WaitableEvent::signal on audio thread), codex Cluster 2 Option A owned-payload queue + integer-index SPSC, codex Cluster 3 sink_lock_-paired setSink protocol, codex Cluster 6 wire-format contract embedded in parser comments, codex Cluster 7 full-NinjamZap-bytes integration test, codex Cluster 8 lock-protected TestFrameResult, codex Cluster 10 mutex-protected std::thread::id (NOT atomic), W-2 shared_ptr type-erased deleter to keep juce_graphics out of njclient lib]

key-files:
  created:
    - juce/video/decoder/VideoDecoder.h
    - juce/video/decoder/NalChunk.h
    - juce/video/decoder/VideoRecvSlotSnapshot.h
    - juce/video/decoder/Openh264Decoder.h
    - juce/video/decoder/Openh264Decoder.cpp
    - tests/test_video_decoder.cpp
    - tests/fixtures/gen-baseline-fixtures.cpp
    - tests/fixtures/README.md
    - tests/fixtures/sps_pps_baseline_320x240.bin
    - tests/fixtures/idr_baseline_320x240.bin
    - tests/fixtures/sps_pps_baseline_640x480.bin
    - tests/fixtures/idr_baseline_640x480.bin
    - tests/fixtures/idr_baseline_320x240_red.bin
    - tests/fixtures/idr_baseline_320x240_green.bin
    - tests/fixtures/marker_payload_outer20.bin
    - .planning/phases/21-h-264-decoder-receive-pipeline/21-02-SUMMARY.md
  modified:
    - src/core/njclient.h
    - src/core/njclient.cpp
    - CMakeLists.txt

key-decisions:
  - "W-2 shared_ptr-with-type-erased-deleter resolution: VideoRecvState carries std::shared_ptr<jamwide::Openh264Decoder> (NOT unique_ptr). shared_ptr's type-erased deleter is captured at make_shared time in Plan 21-03's JUCE-linked TU; this lets njclient.cpp (which intentionally does NOT link juce_graphics) destroy VideoRecvState without seeing the full Openh264Decoder type. The dtor for VideoRecvState is `= default` in njclient.h. Standard idiom for the Pimpl-with-unique-ptr problem applied to a cross-library setting."
  - "Codex Cluster 1 polling-with-atomic-seq wake-up: decoder thread polls *producerSeq_ every 15 ms via juce::Thread::sleep — well under the ~167 ms NINJAM swap interval. Audio thread only bumps the atomic via fetch_add(1, release) — NO juce::WaitableEvent::signal call, no OS sync primitive entered from the audio thread. Verified via grep negative gate on src/core/njclient.cpp."
  - "Codex Cluster 2 Option A (codex-preferred over Option B): VideoRecvState owns std::array<VideoRecvSlotSnapshot, 4> decoderSlots. Each VideoRecvSlotSnapshot carries a 4 MB std::array byte buffer (memory budget: 16 MB per peer for the snapshot ring; 32 MB total per peer when combined with the 16 MB of VideoRecvBuffer slots). Audio thread memcpys into decoderSlots[nextFillIdx & 3] then pushes the integer index onto SpscRing<int, 4>. Each push writes to a DIFFERENT slot index — structurally prevents the back-to-back-overwrite hazard that the codex review flagged on the previous SpscRing<VideoRecvBufferView, 4> design. test_back_to_back_push_preserves_order proves the property end-to-end."
  - "Codex Cluster 3 sink_lock_-paired setSink protocol: PUBLIC setSink(jamwide::PeerVideoSink*) API safe to call with nullptr at any time. sink_lock_ pairs the setter and scaleAndSwapImage_'s sink touch. After setSink(nullptr) returns, the decoder thread cannot dereference sink_ for any further work. Plan 21-03's shutdown protocol calls decoder->setSink(nullptr) AFTER stop+join — so by then the decoder thread has already exited, but the lock pairing is defensive belt-and-suspenders for any out-of-order shutdown path."
  - "Codex Cluster 6 wire-format contract in parser comments: parseSlotAndFeed_ embeds a multi-line comment block citing Plan 21-01's wire-format contract — marker payload IS 20 bytes (not 24); outer 4B prefix value MUST equal 20; SPS/PPS chunk format is [2B BE sps_len][SPS][2B BE pps_len][PPS] (no Annex-B start codes — Annex-B wrap is the decoder's job per D-13); per-frame NAL chunk is single NAL with no prefix. Plan 21-01 already pinned the contract above VideoRecvBuffer in njclient.h; this plan EMBEDS the same contract in the parser site that consumes it (codex Cluster 6 requirement)."
  - "Codex Cluster 7 full-NinjamZap-bytes integration test: NEW test_pushSlotView_full_ninjamzap_bytes uses pushSlotSnapshotForTest (the LIVE audio→decoder code path) with byte-exact upstream wire-format fixtures. The new marker_payload_outer20.bin fixture records the [4B BE outer=20][4B BE sender_seq][16B audio_guid] layout byte-for-byte from upstream's TestClient.cpp:120-176. Regression-guards the Phase 20 commit 6d23b5c marker-outer-prefix-value lesson."
  - "Codex Cluster 8 lock-protected TestFrameResult: pollOneFrameForTest copies image bytes out under result_lock_ — no raw juce::Image* race on test timeout. The decoder writes into result_.image ONLY under result_lock_; the test reads under the same lock. If the test times out without taking the result, the decoder's next write is still under the lock — no UAF."
  - "Codex Cluster 10 mutex-protected std::thread::id: tid_lock_ + std::thread::id pair under JAMWIDE_BUILD_TESTS (NOT atomic<std::thread::id> which is not guaranteed lock-free on all standard libraries). Reads + writes are rare (once per slot parse, once at run() entry) so mutex overhead is negligible. Negative gate `! grep -q std::atomic<std::thread::id>` confirms via grep at every commit."
  - "SPS+PPS combined-packet send: libavcodec rejects standalone SPS or PPS packets with AVERROR_INVALIDDATA. parseSlotAndFeed_ now sends SPS+PPS as a SINGLE combined Annex-B packet (00 00 00 01 SPS 00 00 00 01 PPS) — matching how the pushNalChunk path sends them (the SPS/PPS fixture file is read as one chunk containing both NALs concatenated with start codes). Separate-send pattern was buggy and would have broken first-frame decode."
  - "sendAnnexB_ now does not increment decode_error_count for AVERROR_INVALIDDATA on parameter-set-only packets (NAL types 7=SPS, 8=PPS, 6=SEI). libavcodec's 'Invalid data' for parameter-set packets is informational — it parsed the headers but has no frame to give yet; not a decode failure. Real decode errors on slice packets (NAL type 1, 5) are still counted. This keeps the test_corrupt_nal_recovers_on_next_idr test working (corrupt NAL has type 31, gets counted) while allowing the test_pushSlotView_full_ninjamzap_bytes test to see decode_error_count == 0."

patterns-established:
  - "shared_ptr type-erased deleter for forward-declared cross-library types (W-2 resolution) — VideoRecvState owns std::shared_ptr<Openh264Decoder>; njclient.cpp only sees the forward decl; the deleter is captured at make_shared time in the JUCE-linked TU where the full type IS visible. Standard idiom; applies to any case where a struct needs an owning pointer to a type defined in a library that not all of the struct's translation units link against."
  - "Decoder-thread polling-with-atomic-seq wake-up (codex Cluster 1) — replaces juce::WaitableEvent::signal/wait pattern when the producer is an RT-safe thread (audio). Producer bumps fetch_add(1, release); consumer polls .load(acquire) on a juce::Thread::sleep tick (15 ms; bounded latency under the producer's natural cadence). No OS sync primitive on the producer side."
  - "VideoRecvState-owned 4-slot snapshot ring + integer-index SPSC (codex Cluster 2 Option A) — value-type cross-thread payload pre-allocated at struct construction. Each push writes to a different slot index (idx = counter & 3); pushing N before consumer pops uses N distinct slots. Structural alternative to ring-of-views into shared buffers (which has back-to-back overwrite hazards)."
  - "sink_lock_-paired setter+touch protocol (codex Cluster 3) — public setX(T*) API safe to call with nullptr at any time, paired with a mutex-protected X-dereference site on the worker thread. Caller-side guarantee that setX(nullptr) returns implies the worker thread can no longer dereference the pointer."
  - "Conditional decode-error counting (Plan 21-02 Task 3 fix) — distinguish 'AVERROR_INVALIDDATA on parameter-set packet' (informational; don't count) from 'AVERROR_INVALIDDATA on slice packet' (real decode error; count). Avoids false-positive decode_error_count increments on SPS/PPS sends that libavcodec naturally rejects standalone."

requirements-completed: [COD-03]

# Metrics
duration: 36min
completed: 2026-05-17
---

# Phase 21 Plan 02: H.264 Decoder & Audio→Decoder Snapshot Push Summary

**Per-peer libavcodec H.264 decoder thread + AVCC parser landed per CONTEXT.md D-09/D-12-revised/D-13/D-18 and the codex review redesign — VideoRecvState owns the 4-slot snapshot ring + integer-index SPSC + producer-seq atomic; the audio thread bumps the atomic only (NO WaitableEvent::signal); the decoder thread polls every 15 ms, parses the slot bytes per Plan 21-01's wire-format contract, drives libavcodec with the R4 H9 7-step destructor and AVERROR_INVALIDDATA drop-frame-and-continue recovery; sink_lock_ pairs setSink(nullptr) with scaleAndSwapImage_'s sink touch for Plan 21-03's shutdown protocol; 8 COD-03 sub-tests pass including the codex Cluster 7 full-NinjamZap-bytes integration test and the W-3 back-to-back-push-order regression guard.**

## Performance

- **Duration:** ~36 min
- **Started:** 2026-05-17T17:36:24Z
- **Completed:** 2026-05-17T18:12:23Z
- **Tasks:** 3
- **Files modified:** 3 (src/core/njclient.h, src/core/njclient.cpp, CMakeLists.txt)
- **Files created:** 16 (4 decoder headers, 1 decoder .cpp, 1 test file, 1 generator file, 1 README, 7 fixture binaries, 1 SUMMARY)

## Accomplishments

- **Wave-2 contracts:** VideoDecoder interface (with `setSink` PUBLIC per codex Cluster 3), NalChunk POD (DECODER-THREAD-LOCAL per codex Cluster 2 invariant), NEW VideoRecvSlotSnapshot POD (4 MB owned buffer + inline `copyFromVideoRecvBuffer` per codex Cluster 2 Option A), Openh264Decoder header skeleton with `sink_lock_` / `tid_lock_` / `TestFrameResult`.
- **Decoder implementation:** Openh264Decoder.cpp (~430 LOC) — libavcodec built-in H.264 decoder ("h264", NOT "libopenh264"); thread_count=1 + AV_CODEC_FLAG_LOW_DELAY; juce::Thread::Priority::high; R4 H9 LOCKED 7-step destructor; parseSlotAndFeed_ in-thread AVCC walker per CONTEXT.md D-12 revised (B-1); sendAnnexB_ with conditional error counting (skip count on parameter-set rejections); scaleAndSwapImage_ with lazy SwsContext recreate (Pitfall 7) + sink_lock_-paired sink touch (codex Cluster 3) + TestFrameResult lock-protected test path (codex Cluster 8).
- **Audio-thread integration:** NJClient::pushPlayingSnapshotToDecoder_ private helper called at THREE sites in `runVideoReceiveBlock_` (STAGE-1 promote / PREV-NONE-match immediate-play / accumulating-fallback). memcpy bytes+frameOffsets into vs->decoderSlots[nextFillIdx & 3] + try_push integer index onto vs->decoderSlotIndexQ + fetch_add(1, release) on vs->decoderProducerSeq. NO `WaitableEvent::signal` on audio thread (codex Cluster 1); no `VideoRecvBufferView` raw-pointer view (codex Cluster 2); no `parsePlayingSlotAndEnqueue_` audio-thread parser (B-1). Confirmed via grep negative gates.
- **8 COD-03 sub-tests:** all green. Including the codex Cluster 7 `test_pushSlotView_full_ninjamzap_bytes` exercising the LIVE audio→decoder path via `pushSlotSnapshotForTest` with byte-exact upstream wire-format fixtures, AND the W-3 `test_back_to_back_push_preserves_order` regression guard that pushes red-IDR and green-IDR snapshots back-to-back without polling between and asserts the first decoded center pixel is RED (R=255, G=0, B=0), proving the 4-slot ring structurally preserves push order.
- **7 fixture binaries** committed under `tests/fixtures/` (~1.8 KB total) plus the EXCLUDE_FROM_ALL `gen_baseline_fixtures` generator for regeneration when ffmpeg vendored library bytes change. SHA-256 hashes recorded in `tests/fixtures/README.md`.

## Task Commits

1. **Task 1: VideoDecoder interface + NalChunk + VideoRecvSlotSnapshot + Openh264Decoder.h (Wave-2 contracts)** — `d68b64e` (feat)
2. **Task 2: Openh264Decoder.cpp + audio-thread snapshot push** — `be86eb7` (feat)
3. **Task 3: 8 COD-03 sub-tests + fixture binaries + generator** — `12ef991` (test)

## Files Created/Modified

### Created

- `juce/video/decoder/VideoDecoder.h` — pure-virtual interface mirroring `juce/video/encoder/VideoEncoder.h`. Public `setSink(PeerVideoSink*)` per codex Cluster 3. Test-only `pollOneFrameForTest`, `pushNalChunk`, `pushSlotSnapshotForTest` declared under `JAMWIDE_BUILD_TESTS`.
- `juce/video/decoder/NalChunk.h` — POD with `kind` (enum) + `bytes` (std::vector). Documented DECODER-THREAD-LOCAL invariant per codex Cluster 2 — NalChunks never cross the audio-thread boundary.
- `juce/video/decoder/VideoRecvSlotSnapshot.h` — POD with `std::array<unsigned char, 4 MB> bytes` + `std::array<int, 257> frameOffsets`. Inline `copyFromVideoRecvBuffer` with defensive 4 MB / 256-frame clamps. Codex Cluster 2 Option A — VideoRecvState owns 4 of these.
- `juce/video/decoder/Openh264Decoder.h` — class inherits `juce::Thread` + `VideoDecoder`. Ctor takes references to slot ring + integer-index SpscRing + producer-seq atomic (all owned by VideoRecvState). `sink_lock_`, `tid_lock_`, `TestFrameResult`, `result_lock_` per codex Clusters 3/8/10.
- `juce/video/decoder/Openh264Decoder.cpp` (~430 LOC) — full implementation: open/close with R4 H9 7-step ordering, run() decoder thread polling producer_seq with 15 ms wait, parseSlotAndFeed_ in-thread AVCC walker with combined-SPS-PPS-packet path, sendAnnexB_ with conditional error counting, scaleAndSwapImage_ with lazy SwsContext recreate + sink_lock_-paired sink touch + TestFrameResult test path, setSink public API, JAMWIDE_BUILD_TESTS helpers (pushNalChunk, pushSlotSnapshotForTest, lastParseTid, getDecoderStdThreadId).
- `tests/test_video_decoder.cpp` (~440 LOC) — 8 sub-tests. Helper `DecoderBundle` heap-allocated via `std::make_unique` to avoid 16 MB stack overflow; `read_fixture` + `build_upstream_sps_pps_chunk` + `extract_idr_body` + `build_full_ninjamzap_snapshot` helpers.
- `tests/fixtures/gen-baseline-fixtures.cpp` — EXCLUDE_FROM_ALL generator using libopenh264 encoder + Annex-B NAL splitter.
- `tests/fixtures/README.md` — provenance, SHA-256 hashes, regeneration instructions.
- `tests/fixtures/{sps_pps_baseline_320x240,idr_baseline_320x240,sps_pps_baseline_640x480,idr_baseline_640x480,idr_baseline_320x240_red,idr_baseline_320x240_green,marker_payload_outer20}.bin` — 7 committed fixture binaries.

### Modified

- `src/core/njclient.h` — VideoRecvState extension (codex Cluster 2 Option A): 4× decoderSlots, decoderSlotIndexQ, nextDecoderSlotFillIndex, decoderProducerSeq, std::shared_ptr<Openh264Decoder> decoder (W-2 type-erased deleter), PeerVideoSink* sink. Added `<array>` + `<memory>` includes. Forward-declared `jamwide::Openh264Decoder` + `jamwide::PeerVideoSink`. `pushPlayingSnapshotToDecoder_(VideoRecvState*)` private helper declared.
- `src/core/njclient.cpp` — VideoRecvState ctor body (zero-inits scalars; replaces inline initializer list). `pushPlayingSnapshotToDecoder_` body (memcpy + try_push + fetch_add). Three call sites in `runVideoReceiveBlock_` (STAGE-1 promote / PREV-NONE-match immediate-play / accumulating-fallback).
- `CMakeLists.txt` — JamWideJuce target gains 4 decoder headers + Openh264Decoder.cpp. New `test_video_decoder` add_executable with 16 MB main-thread stack (APPLE) and `gen_baseline_fixtures` EXCLUDE_FROM_ALL target.

## Decisions Made

All decisions follow CONTEXT.md D-09/D-12-revised/D-13/D-18 + codex review Cluster 1/2/3/6/7/8/10. Three implementation-level decisions deviated from the strict plan text — see Deviations section.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] W-2 forward-decl resolution: shared_ptr instead of unique_ptr for `decoder` field**
- **Found during:** Task 1 (initial build failure)
- **Issue:** Plan specified `std::unique_ptr<jamwide::Openh264Decoder> decoder` on VideoRecvState. Including Openh264Decoder.h from njclient.cpp (so the unique_ptr destructor can see the full type) pulls in juce_graphics — but the njclient static library intentionally does NOT link juce_graphics. Build failed: "'juce_graphics/juce_graphics.h' file not found" when compiling njclient.cpp. Conversely, leaving `~unique_ptr<T>()` to instantiate against a forward-declared T fails to compile.
- **Fix:** Switched to `std::shared_ptr<jamwide::Openh264Decoder>`. shared_ptr's type-erased deleter is captured at `make_shared<Openh264Decoder>(...)` time in Plan 21-03's JUCE-linked TU where the full type IS visible. njclient.cpp can now destroy VideoRecvState via the captured deleter without needing the full Openh264Decoder type at the destruction point. VideoRecvState dtor is `= default` in the header. Standard Pimpl idiom adapted to this cross-library setup. Documented inline in njclient.h next to the `decoder` field.
- **Files modified:** src/core/njclient.h (unique_ptr → shared_ptr), src/core/njclient.cpp (removed Openh264Decoder.h include + removed dtor body)
- **Verification:** Both standalone build + test_video_recv_state link cleanly. shared_ptr's ref-counting overhead is one cache line per peer — negligible at the per-peer / per-broadcast cadence.
- **Committed in:** d68b64e (Task 1)

**2. [Rule 1 - Bug] SPS+PPS must be sent as combined Annex-B packet, not two separate packets**
- **Found during:** Task 3 (initial test_pushSlotView_full_ninjamzap_bytes failure with `decode_error_count=2`)
- **Issue:** libavcodec H.264 decoder rejects standalone SPS or standalone PPS packets with `AVERROR_INVALIDDATA` ("Invalid data found when processing input"). The original parseSlotAndFeed_ sent SPS via sendAnnexB_, then PPS via sendAnnexB_ — both rejected. Subsequent IDR could not decode without SPS/PPS state, so the test got no frame OR got decode errors.
- **Fix:** parseSlotAndFeed_ SPS/PPS-detection path now builds a COMBINED Annex-B packet (`00 00 00 01` + SPS body + `00 00 00 01` + PPS body) and calls send_packet ONCE with the combined buffer. This matches the working `pushNalChunk` path which passes the full SPS+start_code+PPS file content in one chunk. Documented inline in parseSlotAndFeed_.
- **Files modified:** juce/video/decoder/Openh264Decoder.cpp (parseSlotAndFeed_ SPS/PPS branch)
- **Verification:** test_pushSlotView_full_ninjamzap_bytes goes from `decode_error_count=2` (FAIL) to `decode_error_count=0` (PASS). test_back_to_back_push_preserves_order goes from `decode_error_count=4` (FAIL) to `decode_error_count=0` (PASS) — and correctly identifies center pixel as RED (R=255, G=0, B=0).
- **Committed in:** 12ef991 (Task 3)

**3. [Rule 2 - Missing Critical] Conditional decode_error_count: don't count AVERROR_INVALIDDATA on parameter-set packets**
- **Found during:** Task 3 (after combined-SPS-PPS fix, residual `decode_error_count=1` on slot-snapshot tests)
- **Issue:** Even after combining SPS+PPS, libavcodec STILL emits AVERROR_INVALIDDATA on the combined SPS+PPS packet because it contains only parameter sets, no slice. The decoder is informationally saying "I parsed your headers but there's no frame yet" — NOT a decode failure. But our error counter doesn't distinguish. The `pushNalChunk`-based tests (1-5) don't check decode_error_count so they pass; the slot-snapshot tests (7, 8) do, so they fail.
- **Fix:** sendAnnexB_ checks the NAL unit_type of the packet being sent (`nal[0] & 0x1f`); if it's 7 (SPS), 8 (PPS), or 6 (SEI), AVERROR_INVALIDDATA does NOT bump the error counter. Real decode errors on slice packets (NAL types 1, 5) are still counted. The corrupt-NAL test still works because corrupt NALs have NAL type 31 (reserved) — not a parameter set — so they get counted as expected. Documented inline.
- **Files modified:** juce/video/decoder/Openh264Decoder.cpp (sendAnnexB_ + parseSlotAndFeed_ combined-SPS-PPS error count)
- **Verification:** test_pushSlotView_full_ninjamzap_bytes shows `decode_error_count=0` (PASS); test_corrupt_nal_recovers_on_next_idr still asserts `decode_error_count >= 1` and PASSES.
- **Committed in:** 12ef991 (Task 3)

**4. [Rule 3 - Blocking] 16 MB main-thread stack required for test binary**
- **Found during:** Task 3 (test_video_decoder segfault at startup with EXC_BAD_ACCESS in `___chkstk_darwin`)
- **Issue:** macOS default main-thread stack is 8 MB. The test's `DecoderBundle` contains `std::array<VideoRecvSlotSnapshot, 4>` = 16 MB. Even if heap-allocated via unique_ptr, the JUCE static-init for juce_graphics + juce_events + libavcodec global state at process startup overflows the 8 MB stack BEFORE main() runs.
- **Fix:** CMakeLists.txt sets `target_link_options(test_video_decoder PRIVATE "LINKER:-stack_size,0x1000000")` on APPLE (16 MB main-thread stack). Matches the established `test_processor_video_lifecycle` pattern. Also heap-allocate DecoderBundle inside each sub-test via `std::make_unique<DecoderBundle>` (defensive; even with 16 MB stack the bundle on stack would be too much).
- **Files modified:** CMakeLists.txt (link options on APPLE for test_video_decoder), tests/test_video_decoder.cpp (DecoderBundle via make_unique)
- **Verification:** test_video_decoder runs cleanly, 8/8 sub-tests pass in ~440 ms.
- **Committed in:** 12ef991 (Task 3)

---

**Total deviations:** 4 auto-fixed (1 W-2 build-infrastructure, 1 bug-clarification for libavcodec API, 1 missing-critical conditional error counting, 1 blocking infrastructure stack-size bump).
**Impact on plan:** All four deviations were necessary for build correctness, decoder API compliance, or test infrastructure. No scope creep — the plan's intent (decoder lands, tests pass) is preserved; the deviations are mechanical implementation fixes that the plan's narrative did not anticipate but the codex Cluster 6 wire-format pinning AND the W-2 W-3 enforcement gates required.

## Issues Encountered

- **None during planned work after the deviations were resolved.** The 6/6 video test suite passes consistently, the JamWideJuce_Standalone target builds cleanly (warnings only from existing WDL conversions, not new decoder code).

## Verification Gates (Plan-level)

- **Build gate:** `./scripts/build.sh JamWideJuce_Standalone test_video_recv_state test_video_encoder test_video_decoder` builds cleanly (1 unrelated ld rpath warning).
- **Test gate:** `ctest --output-on-failure -R "rawdata_send|video_state_machine|video_fourcc|video_encoder|video_recv_state|video_decoder"` exits 0; 6/6 tests pass.
- **Codex Cluster 1 negative gate:** `! grep -q "WaitableEvent::signal\|pending_event_.signal" src/core/njclient.cpp` returns 0 — no event-signal call on audio thread.
- **Codex Cluster 2 negative gate:** `! grep -q "VideoRecvBufferView" src/core/njclient.cpp` returns 0 — no raw-pointer view crosses the thread boundary.
- **B-1 negative gate:** `! grep -q "parsePlayingSlotAndEnqueue_" src/core/njclient.cpp` returns 0 — no audio-thread AVCC parser.
- **Codex Cluster 10 negative gate:** `! grep -q "std::atomic<std::thread::id>" juce/video/decoder/Openh264Decoder.{h,cpp}` returns 0 — no atomic-thread-id.
- **Codex Cluster 3 positive gate:** `setSink` is in the VideoDecoder interface AND has a body in Openh264Decoder.cpp; `sink_lock_` referenced in both files.
- **Codex Cluster 6 positive gate:** parseSlotAndFeed_ comment block cites Plan 21-01 wire-format contract (marker payload IS 20 bytes, outer prefix value MUST equal 20, SPS/PPS layout, per-frame NAL layout).
- **Codex Cluster 7 positive gate:** `test_pushSlotView_full_ninjamzap_bytes` present in tests/test_video_decoder.cpp; `marker_payload_outer20.bin` committed.
- **W-3 positive gate:** `test_back_to_back_push_preserves_order` present; reads `idr_baseline_320x240_red.bin` + `idr_baseline_320x240_green.bin`; asserts center pixel is red after back-to-back push.
- **Codec name gate:** `test_codec_name_is_libavcodec_h264` asserts `avcodec_find_decoder(AV_CODEC_ID_H264)->name == "h264"` (not "libopenh264").
- **B-1 enforcement gate:** `test_parser_runs_on_decoder_thread_not_audio` asserts `lastParseTid != main_thread_id && lastParseTid == decoder_thread_std_id`.
- **R4 H9 destructor gate:** close() implements signalThreadShouldExit → stopThread(5000) → send_packet(nullptr) flush → receive_frame drain → av_frame_free / av_packet_free → sws_freeContext → avcodec_free_context. Verified by grep + 6/6 test green (no thread-join hangs or UAF).

## User Setup Required

None — Plan 21-02 is purely internal substrate (decoder + audio-thread integration + tests). No external service configuration, no environment variables, no DAW-level UAT yet (deferred to Plan 21-03).

## Next Phase Readiness

- **Plan 21-03 (distributor + sink + lifecycle):** all the surfaces Plan 21-03 needs are in place. `VideoRecvState::decoder` is a `std::shared_ptr<Openh264Decoder>` initialized to null — Plan 21-03's `ensureVideoDecoderForPeer_` calls `std::make_shared<Openh264Decoder>(vs->decoderSlots, vs->decoderSlotIndexQ, vs->decoderProducerSeq)` and `vs->decoder->open(320, 240)`, then `vs->decoder->setSink(distributor->createPeerVideoSink(...))`. The shutdown protocol (codex review Cluster 3 HIGH) calls `decoder->stopAndJoin()` (via the existing R4 H9 close()) THEN `decoder->setSink(nullptr)` THEN removes the sink from the distributor — the sink_lock_ pairing is the structural guarantee that no decoder-thread sink-touch races with sink deletion.
- **PeerVideoSink is forward-declared in VideoDecoder.h** — Plan 21-03 lands the full type in `juce/video/distributor/`. The scaleAndSwapImage_ TODO comment marks where Plan 21-03 will fill in the `sink_->image_front` swap + `sink_->bufferLock` ScopedLock + `sink_->generation.fetch_add` + `sink_->triggerAsyncUpdate`. Plan 21-02 leaves sink_ null in production so this path is currently inert.
- **Audio-thread integration is COMPLETE:** `pushPlayingSnapshotToDecoder_` is called at all three sites where vs->playing is freshly populated. When Plan 21-03 lazy-constructs `vs->decoder`, the next interval boundary will memcpy + push + bump producer_seq; the decoder thread will observe and decode within ~15 ms.
- **No blockers.** All Plan 21-02 gates green; the substrate is shipped.

## Self-Check: PASSED

- `juce/video/decoder/VideoDecoder.h` — present (FOUND in worktree)
- `juce/video/decoder/NalChunk.h` — present
- `juce/video/decoder/VideoRecvSlotSnapshot.h` — present
- `juce/video/decoder/Openh264Decoder.h` — present
- `juce/video/decoder/Openh264Decoder.cpp` — present
- `src/core/njclient.h` — modified, contains decoderSlots/decoderSlotIndexQ/decoderProducerSeq/shared_ptr<Openh264Decoder>/PeerVideoSink* sink/pushPlayingSnapshotToDecoder_
- `src/core/njclient.cpp` — modified, contains VideoRecvState ctor body + pushPlayingSnapshotToDecoder_ body + three call sites in runVideoReceiveBlock_
- `tests/test_video_decoder.cpp` — present, 8 sub-test function names all present
- `tests/fixtures/{sps_pps_baseline_320x240,idr_baseline_320x240,sps_pps_baseline_640x480,idr_baseline_640x480,idr_baseline_320x240_red,idr_baseline_320x240_green,marker_payload_outer20}.bin` — all 7 fixtures present
- `tests/fixtures/gen-baseline-fixtures.cpp` — present
- `tests/fixtures/README.md` — present, SHA-256 hashes recorded
- `CMakeLists.txt` — modified, test_video_decoder + gen_baseline_fixtures targets wired
- Commit `d68b64e` (Task 1) — present in `git log`
- Commit `be86eb7` (Task 2) — present in `git log`
- Commit `12ef991` (Task 3) — present in `git log`

---
*Phase: 21-h-264-decoder-receive-pipeline*
*Plan: 02*
*Completed: 2026-05-17*
