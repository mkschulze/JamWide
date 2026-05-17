---
phase: 21-h-264-decoder-receive-pipeline
plan: 03
subsystem: video-distributor-sink-lifecycle
tags: [h264, juce-async-updater, juce-image, lazy-startup, four-step-shutdown, codex-cluster-3, codex-cluster-4, codex-cluster-9, codex-cluster-10, lifetime-triangle, dtor-reorder, cross-plan-sender-seq-alignment, uat-pending]

# Dependency graph
requires:
  - phase: 21-h-264-decoder-receive-pipeline/21-01
    provides: VideoRecvBuffer + VideoRecvState struct definitions + runVideoReceiveBlock_ audio-thread block + four single-source state-machine helpers
  - phase: 21-h-264-decoder-receive-pipeline/21-02
    provides: VideoDecoder interface + Openh264Decoder + AVCC parser + setSink API + audio-thread snapshot push
  - phase: 19-camera-capture-permission-ux
    provides: JamWideFrameDistributor + Subscription RAII (HIGH-2) + AsyncUpdater pattern (HIGH-4) — mirrored for receive side
provides:
  - juce/video/distributor/PeerVideoSink.{h,cpp} — juce::AsyncUpdater subclass with double-buffered juce::Image (D-08), atomic generation counter (D-01), D-20 atomic status fields (hold_count / decode_error_count / drop_resync_count / synced / first_frame_seen), multi-listener vector (D-06 — DISP-03), and codex Cluster 3 destructor (cancelPendingUpdate + wait for in-flight handleAsyncUpdate via inFlightCv_). Cross-plan codex concern accessor getLastObservedSenderSeqForTest under JAMWIDE_BUILD_TESTS.
  - juce/video/distributor/JamWideRemoteFrameDistributor.{h,cpp} — symmetric inverse of Phase 19's JamWideFrameDistributor. Owns unordered_map<key="username:chidx", unique_ptr<PeerVideoSink>>. Subscription RAII (~Subscription removes listener + blocks in-flight). Includes a JUCE-linked factory createDecoderAndSinkForPeer + tearDownDecoderAndSink + a free function getDefaultVideoDistributorOps() that returns a populated NJClient::VideoDistributorOps function-pointer table.
  - NJClient::SetRemoteFrameDistributor + NJClient::SetVideoDistributorOps — JamWideJuceProcessor injects both at ctor time. The VideoDistributorOps function-pointer table preserves njclient's juce-free link surface (W-2 type-erased deleter resolution).
  - NJClient::ensureVideoDecoderForPeerLater_ (Phase 1 — under m_video_recv_cs, microseconds, flag-only).
  - NJClient::completeVideoDecoderStartup_ (Phase 2+3 — outside m_video_recv_cs on run thread, heavyweight construction + brief re-acquire to install).
  - User-leave four-step shutdown protocol (codex Cluster 3) integrated at the m_remoteusers Delete site inside the run-thread Run() loop.
  - JamWideJuceProcessor.{h,cpp} — remoteFrameDistributor member + getRemoteFrameDistributor accessor + ctor wiring (make_unique + SetRemoteFrameDistributor + SetVideoDistributorOps) + codex Cluster 3 REVERSED dtor order (client.reset() BEFORE remoteFrameDistributor.reset()).
  - juce/video/decoder/Openh264Decoder.cpp — Task 3 cross-plan concern wiring: marker-frame branch in parseSlotAndFeed_ extracts sender_seq and forwards to sink via setLastObservedSenderSeqForTest under JAMWIDE_BUILD_TESTS.
  - tests/test_remote_frame_distributor.cpp — 4 unit sub-tests (D-06 multi-listener + HIGH-2 dtor blocking + RESEARCH OQ2 subscribe-before-peer-exists + codex Cluster 3 NEW sink-dtor-cancels-pending).
  - tests/test_video_sync_e2e.cpp — 1 integration sub-test (3-peer per-peer isolation + cross-plan sender_seq monotonic alignment via the LIVE pushSlotSnapshotForTest path).
  - tests/uat/phase-21-receive-uat-procedure.md — 4-cell UAT procedure mapping 1:1 to ROADMAP success criteria, with codex Cluster 9 explicit Phase Closure Policy section forbidding "BLOCKED-but-closed" wording, codex Cluster 1 timing-instrumentation gate referenced in Cell 1 + Cell 4 acceptance, codex Cluster 10 LOW "v1.3 fixed receive surface size 320x240" wording.
  - tests/uat/phase-21-receive-uat-report.md — empty report template populated during Task 5 checkpoint per codex Cluster 9 closure policy.

affects: [22, 23, 24]

# Tech tracking
tech-stack:
  added: [juce::AsyncUpdater (mirror of Phase 19 HIGH-4), juce::Image double-buffer, juce::CriticalSection, std::condition_variable + std::mutex for inFlight + listenerLock, function-pointer table NJClient::VideoDistributorOps (W-2 link-surface preservation)]
  patterns: [codex Cluster 3 cancelPendingUpdate + inFlightCv_.wait dtor contract, codex Cluster 3 four-step shutdown protocol (close → setSink(nullptr) → vs->sink=nullptr → removeSink), codex Cluster 3 REVERSED processor dtor order (client.reset() BEFORE remoteFrameDistributor.reset()), codex Cluster 4 two-phase lazy startup (flag-under-mutex + heavy-work-outside-mutex + brief-re-acquire-to-install), codex Cluster 10 LOW v1.3 fixed receive surface size wording, function-pointer ops table for cross-library symbol decoupling (W-2 link-surface preservation), cross-plan codex concern sender_seq monotonic alignment counter for slow-drift detection]

key-files:
  created:
    - juce/video/distributor/PeerVideoSink.h
    - juce/video/distributor/PeerVideoSink.cpp
    - juce/video/distributor/JamWideRemoteFrameDistributor.h
    - juce/video/distributor/JamWideRemoteFrameDistributor.cpp
    - tests/test_remote_frame_distributor.cpp
    - tests/test_video_sync_e2e.cpp
    - tests/uat/phase-21-receive-uat-procedure.md
    - tests/uat/phase-21-receive-uat-report.md
    - .planning/phases/21-h-264-decoder-receive-pipeline/21-03-SUMMARY.md
  modified:
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - juce/video/decoder/Openh264Decoder.cpp
    - src/core/njclient.h
    - src/core/njclient.cpp
    - CMakeLists.txt

key-decisions:
  - "Codex Cluster 3 (HIGH × 3) — three independent lifetime mitigations landed: (a) PeerVideoSink dtor calls cancelPendingUpdate() FIRST then waits on inFlightCv_ for inFlightCount_ == 0; (b) JamWideJuceProcessor dtor REVERSED — client.reset() BEFORE remoteFrameDistributor.reset(); (c) four-step shutdown protocol on user-leave (close → setSink(nullptr) → vs->sink=nullptr → distributor->removeSink). All three together close the lifetime triangle (processor → distributor → sinks ← decoders) per codex review."
  - "Codex Cluster 4 (HIGH — lazy startup blocks audio thread): two-phase startup landed. Phase 1 (ensureVideoDecoderForPeerLater_) under m_video_recv_cs is microseconds — just flags vs->decoder_startup_needed. Phase 2 (completeVideoDecoderStartup_) runs OUTSIDE m_video_recv_cs on the run thread — heavyweight construction (avcodec_open2 + thread start). Phase 3 briefly re-acquires the mutex to install pointers IF vs still exists. Audio thread's runVideoReceiveBlock_ no longer blocks against avcodec_open2's 5-50 ms cost."
  - "Codex Cluster 9 (MEDIUM — UAT closure policy): explicit Phase Closure Policy section at the top of tests/uat/phase-21-receive-uat-procedure.md. Closure requires either full PASS (Cells 1-4) OR a deferred-risk record per BLOCKED cell linked from STATE.md → Phase 24. 'BLOCKED-but-closed' wording is explicitly forbidden."
  - "Codex Cluster 10 LOW (320x240 wording): 'v1.3 fixed receive surface size 320x240' used throughout — NOT 'first-seen peer resolution' (impossible to know at BEGIN time; SwsContext recreate inside Openh264Decoder per D-07 + Pitfall 7 handles real peer resolutions)."
  - "W-2 link-surface preservation: njclient.cpp does NOT include Openh264Decoder.h / PeerVideoSink.h / the full JamWideRemoteFrameDistributor.h's method bodies. Calls to distributor + decoder are routed through NJClient::VideoDistributorOps function-pointer table. Table impl lives in JamWideRemoteFrameDistributor.cpp (JUCE-linked TU). njclient's link surface stays free of juce_graphics / juce_events / libavcodec — existing test contract preserved."
  - "Cross-plan codex concern (sender_seq monotonic alignment counter): JAMWIDE_BUILD_TESTS-gated. PeerVideoSink exposes getLastObservedSenderSeqForTest. Openh264Decoder's parseSlotAndFeed_ extracts sender_seq from the 20-byte marker frame (bytes 0..3 BE) and forwards to sink->setLastObservedSenderSeqForTest. test_video_sync_e2e asserts alice/bob/charlie's last observed values equal 119/219/319 after 20 simulated intervals each — catches slow-drift bugs UAT alone is too coarse to detect."

patterns-established:
  - "Codex Cluster 3 cancelPendingUpdate + inFlightCv_.wait dtor contract — drops queued AsyncUpdate dispatches from the message-thread queue, then waits for any handleAsyncUpdate that has already started on the message thread to complete. Applied to PeerVideoSink::~PeerVideoSink; should be applied to any future juce::AsyncUpdater subclass that has multi-thread listener fan-out + a destruction path that races with in-flight dispatch."
  - "Codex Cluster 4 two-phase lazy startup — Phase 1 under audio-thread-shared mutex: flag-only, microseconds. Phase 2 outside the mutex on a non-RT thread: heavy work (allocation + avcodec_open2 + thread start). Phase 3 brief re-acquire: install pointers IF the racing party (m_video_streams entry) still exists. Idempotent. Applies to any lazy lifecycle that must not block an audio thread under a shared mutex."
  - "Codex Cluster 3 four-step shutdown protocol — (1) consumer->close() (joins thread) → (2) consumer->setSink/setSubscriber(nullptr) (clears back-ref) → (3) owner->subscriber = nullptr (clears owner's reference) → (4) publisher->remove(...) (destroys subscriber, dtor cancels pending + waits in-flight). Applies to any shutdown of a lifetime triangle where the publisher owns the subscriber + the consumer has a raw subscriber pointer."
  - "Function-pointer ops table for cross-library link-surface preservation (W-2) — when a library A needs to call methods on type T defined in library B, but A must not link B (to preserve A's test contracts), use a struct of function pointers populated by B's TU. A stores the table by value + calls through. Avoids template + virtual dispatch overhead; trivial to make optional (null function pointers = no-op)."

requirements-completed: [WIRE-02, COD-03]

# Metrics
duration: "~70 min (Tasks 1-4 autonomous; Task 5 human UAT pending)"
completed: 2026-05-17
status: PARTIAL — Task 5 (live UAT) requires human action; Tasks 1-4 complete + committed + green
---

# Phase 21 Plan 03: Distributor + Lifecycle + UAT Procedure Summary

**Codex review redesign landed in full: PeerVideoSink + JamWideRemoteFrameDistributor with codex Cluster 3 dtor contract; JamWideJuceProcessor with reversed dtor order; two-phase lazy decoder startup keeping the audio thread off the avcodec_open2 critical path; four-step user-leave shutdown protocol; 4 distributor unit sub-tests + 1 three-peer e2e integration test (with cross-plan sender_seq monotonic alignment counter); UAT procedure authored with codex Cluster 9 closure policy. The plugin is now FUNCTIONAL — a peer broadcasting H.264 will lazily construct a per-peer decoder + sink, decode frames, and trigger sink AsyncUpdates that Phase 22 tiles will subscribe to. Task 5 (live UAT against video.ninjamzap.com:2049) is human-pending.**

## Status: PARTIAL — UAT pending

**Tasks 1-4 complete + committed + tests green.**

**Task 5 (`checkpoint:human-verify`, gate="blocking"):** requires live UAT against `video.ninjamzap.com:2049` per `tests/uat/phase-21-receive-uat-procedure.md`. See `tests/uat/phase-21-receive-uat-report.md` for the empty template to populate during the live UAT. Per codex Cluster 9 closure policy, the phase is NOT closed until either:

- All 4 cells PASS, **OR**
- All BLOCKED cells have deferred-risk records linked from `.planning/STATE.md` under "Phase 21 deferred risks → Phase 24 follow-up".

## Performance

- **Duration:** ~70 min (Tasks 1-4 autonomous)
- **Started:** 2026-05-17 (worktree spawn at agent-a1a3d97f7fa90569b)
- **Completed (Tasks 1-4):** 2026-05-17
- **Task 5 (live UAT):** human-pending
- **Tasks committed:** 5 (Task 1 + Task 2 + Task 3 + Task 4 + Task 3 link-fix)
- **Files created:** 9 (4 distributor sources, 2 test sources, 2 UAT docs, 1 summary)
- **Files modified:** 6 (JamWideJuceProcessor.h/cpp, Openh264Decoder.cpp, njclient.h/cpp, CMakeLists.txt)

## Accomplishments

- **PeerVideoSink + JamWideRemoteFrameDistributor (Task 1):** symmetric inverse of Phase 19's camera-side distributor. Sink owns double-buffered juce::Image + atomic generation + D-20 status fields + multi-listener vector; distributor owns the per-peer sink map + Subscription RAII handle. **Codex Cluster 3 (a):** sink dtor cancels pending AsyncUpdate AND waits for in-flight handleAsyncUpdate via inFlightCv_. Cross-plan codex concern: JAMWIDE_BUILD_TESTS-gated sender_seq accessor.
- **Codex Cluster 3 (b) processor dtor reorder (Task 1):** JamWideJuceProcessor::~JamWideJuceProcessor now calls `client.reset()` BEFORE `remoteFrameDistributor.reset()` — NJClient drains all VideoRecvStates via the four-step shutdown protocol; by the time the distributor itself is destroyed, sinks_ is empty.
- **Codex Cluster 4 two-phase lazy startup (Task 2):** Phase 1 (`ensureVideoDecoderForPeerLater_`) under `m_video_recv_cs` is microseconds (flag-only). Phase 2 (`completeVideoDecoderStartup_`) runs OUTSIDE the mutex on the run thread — heavyweight `avcodec_open2` + thread start. Phase 3 briefly re-acquires to install pointers IF vs still exists. Audio thread no longer blocks against the 5-50 ms cost.
- **Codex Cluster 3 (c) four-step user-leave shutdown (Task 2):** routed through `JamWideRemoteFrameDistributor`'s function-pointer table impl `op_tear_down_decoder` which executes: (1) decoder->close (join thread) → (2) decoder->setSink(nullptr) (defensive) → (3) vs->sink = nullptr (clear owner ref) → (4) distributor->removeSink (destroy sink — PeerVideoSink dtor runs codex Cluster 3 (a) contract).
- **W-2 link-surface preservation (Task 2):** njclient.cpp does NOT see Openh264Decoder.h or PeerVideoSink.h; calls are routed through `NJClient::VideoDistributorOps` function-pointer table. Existing test contract (njclient + wdl only) preserved.
- **4 distributor unit sub-tests + 1 e2e integration test (Task 3):** all green. Including `test_sink_dtor_cancels_pending_async_update` covering codex Cluster 3 (a) in both scenarios (cancel pending + wait in-flight) and `test_three_peers_isolated_decode_errors` proving per-peer isolation AND cross-plan sender_seq monotonic alignment in-process via the LIVE `pushSlotSnapshotForTest` path.
- **4-cell UAT procedure (Task 4):** maps 1:1 to ROADMAP success criteria 1-4. **Codex Cluster 9:** explicit Phase Closure Policy section at top — closure requires either full PASS or deferred-risk records linked from STATE.md → Phase 24. "BLOCKED-but-closed" wording explicitly forbidden. **Codex Cluster 1:** Cell 1 + Cell 4 acceptance includes `getRunVideoReceiveBlockMaxNanosForTest() < 1,000,000` (1 ms) at steady-state. **Codex Cluster 10 LOW:** "v1.3 fixed receive surface size 320x240" wording throughout.

## Task Commits

1. **Task 1: PeerVideoSink + JamWideRemoteFrameDistributor + JamWideJuceProcessor wiring + codex Cluster 3 dtor reorder** — `fa62224` (feat)
2. **Task 2: two-phase lazy startup + four-step shutdown protocol + function-pointer ops table** — `53d77b3` (feat)
3. **Task 3: 4 distributor unit tests + 3-peer e2e + sender_seq monotonic alignment + decoder marker-seq forwarding** — `d55f3b6` (test)
4. **Task 4: UAT procedure + empty report template + codex Cluster 9 closure policy** — `0e1c5fd` (docs)
5. **Task 3 link-fix: link PeerVideoSink.cpp into test_video_decoder for the cross-plan setLastObservedSenderSeqForTest symbol** — `b495a4b` (fix)

## Files Created/Modified

### Created

- `juce/video/distributor/PeerVideoSink.h` — class declaration: juce::AsyncUpdater subclass, double-buffered juce::Image, atomic generation, D-20 status fields, multi-listener vector, codex Cluster 3 dtor declarations.
- `juce/video/distributor/PeerVideoSink.cpp` — implementation. handleAsyncUpdate iterates listeners under inFlight-tracked snapshot, fans out OUTSIDE locks, decrement + notify cv. Destructor calls cancelPendingUpdate then waits on inFlightCv_ for inFlightCount_ == 0. JAMWIDE_BUILD_TESTS-gated setLastObservedSenderSeqForTest setter + getLastObservedSenderSeqForTest accessor.
- `juce/video/distributor/JamWideRemoteFrameDistributor.h` — distributor class + Subscription RAII handle + factory function declarations (createDecoderAndSinkForPeer + tearDownDecoderAndSink) + free function getDefaultVideoDistributorOps that returns a populated NJClient::VideoDistributorOps table.
- `juce/video/distributor/JamWideRemoteFrameDistributor.cpp` — implementation. findOrCreateSink flushes deferred listeners on sink creation; removeSink moves the unique_ptr out under mu_ then destroys OUTSIDE the lock so the codex Cluster 3 dtor doesn't deadlock against any thread holding mu_. Function-pointer table impl (anonymous namespace) for op_create_decoder / op_install_decoder / op_destroy_decoder / op_remove_sink / op_tear_down_decoder.
- `tests/test_remote_frame_distributor.cpp` — 4 unit sub-tests. juce_add_console_app + ScopedJuceInitialiser_GUI + JUCE_MODAL_LOOPS_PERMITTED=1 for runDispatchLoopUntil access. Tests cover D-06 multi-listener fan-out, HIGH-2 mirror dtor blocking, RESEARCH OQ2 lazy-create on subscribe, and codex Cluster 3 NEW sink-dtor-cancels-pending (two scenarios).
- `tests/test_video_sync_e2e.cpp` — 1 integration sub-test driving 3 DecoderBundles with PeerVideoSink attached via setSink. 20 simulated intervals per peer; alice + bob get valid fixtures; charlie gets corrupt IDR. Asserts per-peer isolation + cross-plan sender_seq monotonic alignment.
- `tests/uat/phase-21-receive-uat-procedure.md` — 4-cell UAT procedure with codex Cluster 9 + 1 + 10 LOW gates.
- `tests/uat/phase-21-receive-uat-report.md` — empty report template populated during Task 5.
- `.planning/phases/21-h-264-decoder-receive-pipeline/21-03-SUMMARY.md` — this file.

### Modified

- `juce/JamWideJuceProcessor.h` — adds `#include "video/distributor/JamWideRemoteFrameDistributor.h"`, remoteFrameDistributor unique_ptr member after frameDistributor at line 132, getRemoteFrameDistributor accessor at line 170.
- `juce/JamWideJuceProcessor.cpp` — ctor: `remoteFrameDistributor = make_unique<...>()` + `client->SetRemoteFrameDistributor(remoteFrameDistributor.get())` + `client->SetVideoDistributorOps(jamwide::getDefaultVideoDistributorOps())` immediately after the existing camera-side frameDistributor lines. Dtor: codex Cluster 3 REVERSED ORDER — `client.reset()` FIRST, then `remoteFrameDistributor.reset()`, then `frameDistributor.reset()` (camera-side independent lifetime).
- `juce/video/decoder/Openh264Decoder.cpp` — Task 3 cross-plan concern wiring: includes `../distributor/PeerVideoSink.h`; marker-frame branch in parseSlotAndFeed_ extracts sender_seq from bytes 0..3 BE and forwards to sink_->setLastObservedSenderSeqForTest under JAMWIDE_BUILD_TESTS + sink_lock_.
- `src/core/njclient.h` — forward decl of JamWideRemoteFrameDistributor; VideoRecvState gains `bool decoder_startup_needed = false`; m_remote_frame_distributor pointer member; m_video_distributor_ops nested struct member; SetRemoteFrameDistributor + SetVideoDistributorOps public setters; ensureVideoDecoderForPeerLater_ + completeVideoDecoderStartup_ private declarations.
- `src/core/njclient.cpp` — SetRemoteFrameDistributor + SetVideoDistributorOps bodies; ensureVideoDecoderForPeerLater_ Phase 1 body (flag-only); completeVideoDecoderStartup_ Phase 2+3 body routing through the ops table's create_decoder / install_decoder / destroy_decoder / remove_sink. BEGIN handler dispatch site adds completeVideoDecoderStartup_ call OUTSIDE m_video_recv_cs after handleVideoRecvBegin_ returns. handleVideoRecvBegin_ body adds the ensureVideoDecoderForPeerLater_ Phase 1 call under the mutex. User-leave block at the m_remoteusers Delete site collects matching VideoRecvStates into a local vector, releases m_video_recv_cs, then calls the ops table's tear_down_decoder for each (which runs four-step shutdown OUTSIDE the mutex).
- `CMakeLists.txt` — adds juce/video/distributor/{*.h,*.cpp} to JamWideJuce target_sources. Adds test_remote_frame_distributor + test_video_sync_e2e as juce_add_console_app targets with JAMWIDE_BUILD_TESTS=1 + JUCE_MODAL_LOOPS_PERMITTED=1 + 16 MB main-thread stack (e2e). Links Openh264Decoder.cpp + PeerVideoSink.cpp + ffmpeg into both new tests. Adds PeerVideoSink.cpp to test_video_decoder for the cross-plan symbol reference.

## Decisions Made

All decisions follow CONTEXT.md D-01..D-08, D-17..D-20 + codex review Cluster 3 (a/b/c), Cluster 4, Cluster 9, Cluster 10 LOW + the cross-plan codex concern. One implementation-level decision deviated from the strict plan text — see Deviations section.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] W-2 link-surface preservation: function-pointer ops table instead of direct distributor + decoder method calls in njclient.cpp**

- **Found during:** Task 2 build (before commit)
- **Issue:** The plan text described `m_remote_frame_distributor->createDecoderAndSinkForPeer(...)` and `m_remote_frame_distributor->removeSink(...)` and `m_remote_frame_distributor->tearDownDecoderAndSink(decoder, ...)` calls directly from njclient.cpp. This produced linker errors in test_rawdata_send / test_video_state_machine / etc. (which link njclient but not the distributor.cpp object file): "undefined symbols for architecture x86_64: jamwide::JamWideRemoteFrameDistributor::createDecoderAndSinkForPeer / removeSink / findSink / tearDownDecoderAndSink".
- **Root cause:** Static-library linker requires the symbol to exist in some linked object. Even though `m_remote_frame_distributor == nullptr` in tests means the calls don't execute, the linker still demands the symbols.
- **Fix:** Introduced `NJClient::VideoDistributorOps` function-pointer struct populated at construction time by JamWideJuceProcessor via `client->SetVideoDistributorOps(jamwide::getDefaultVideoDistributorOps())`. njclient.cpp calls through the table's function pointers; the table's impl lives in JamWideRemoteFrameDistributor.cpp (JUCE-linked TU) which CAN call Openh264Decoder + PeerVideoSink methods on the full type. Tests leave the table null and the calls degrade to no-ops (same as the `m_remote_frame_distributor == nullptr` branch).
- **Files modified:** src/core/njclient.h (struct + setter + member), src/core/njclient.cpp (setter body + helper bodies route through table), juce/video/distributor/JamWideRemoteFrameDistributor.{h,cpp} (function-pointer impls + getDefaultVideoDistributorOps free function)
- **Verification:** All 8 Phase 21 tests still link cleanly; JamWideJuce_Standalone builds cleanly; the function-pointer indirection adds one level of pointer chase per call (microseconds; negligible).
- **Committed in:** `53d77b3` (Task 2)

**2. [Rule 2 - Missing Critical] Cross-plan sender_seq forwarding in Openh264Decoder was attributed to Plan 21-02 but not landed**

- **Found during:** Task 3 (while writing test_video_sync_e2e that asserts the monotonic-alignment counter)
- **Issue:** The plan text attributes the wiring `sink_->setLastObservedSenderSeqForTest(sender_seq)` inside parseSlotAndFeed_'s 20-byte marker branch to Plan 21-02 Task 2. Inspection of Plan 21-02's actual Openh264Decoder.cpp shows the marker branch only discards the 20-byte payload (`if (frameSize == 20) continue;`) — the sender_seq extraction + sink forwarding is absent.
- **Fix:** Added the wiring in Openh264Decoder.cpp (Task 3 commit). Extract sender_seq from bytes 0..3 BE; take sink_lock_; if sink_ != nullptr, call sink_->setLastObservedSenderSeqForTest(sender_seq). Gated under JAMWIDE_BUILD_TESTS — production builds skip the call.
- **Files modified:** juce/video/decoder/Openh264Decoder.cpp (include PeerVideoSink.h + marker-frame branch wiring)
- **Verification:** test_video_sync_e2e's monotonic-alignment assertion (alice/bob/charlie last observed values equal 119/219/319) passes. No production-build regression (the new code is JAMWIDE_BUILD_TESTS-gated).
- **Committed in:** `d55f3b6` (Task 3)

**3. [Rule 3 - Blocking] Test executables need JUCE_MODAL_LOOPS_PERMITTED=1 for runDispatchLoopUntil**

- **Found during:** Task 3 first build
- **Issue:** JUCE's `juce::MessageManager::runDispatchLoopUntil` is gated behind `JUCE_MODAL_LOOPS_PERMITTED` (default 0). The new distributor + e2e tests use it to pump the AsyncUpdater dispatch deterministically inside the test harness. Without the define, the symbol is hidden and the test compile fails.
- **Fix:** CMakeLists.txt sets `JUCE_MODAL_LOOPS_PERMITTED=1` on both new test executables. This is a test-only define and does NOT propagate to the JamWideJuce production target.
- **Files modified:** CMakeLists.txt
- **Verification:** Both new tests build + pass.
- **Committed in:** `d55f3b6` (Task 3, alongside the new test sources)

**4. [Rule 3 - Blocking] test_video_decoder needs PeerVideoSink.cpp link after Task 3 added the cross-plan wiring**

- **Found during:** final full-suite build verification
- **Issue:** Openh264Decoder.cpp now references PeerVideoSink::setLastObservedSenderSeqForTest under JAMWIDE_BUILD_TESTS. test_video_decoder links Openh264Decoder.cpp directly (not via JamWideJuce target) so it must also link PeerVideoSink.cpp.
- **Fix:** Added PeerVideoSink.cpp to test_video_decoder's target_sources.
- **Files modified:** CMakeLists.txt
- **Verification:** test_video_decoder still passes (8/8 sub-tests).
- **Committed in:** `b495a4b` (Task 3 link followup)

---

**Total deviations:** 4 auto-fixed (1 link-surface architectural restructure, 1 missing-critical cross-plan wiring, 1 blocking JUCE define, 1 blocking link fix). All four were necessary for correctness or build infrastructure; no scope creep. The function-pointer ops table is a substantively better design than direct method calls because it cleanly decouples njclient's link surface from JUCE/ffmpeg.

## Issues Encountered

- **None during planned work after the deviations were resolved.** The 8/8 Phase 21 test suite passes consistently; JamWideJuce_Standalone builds cleanly (1 unrelated ld rpath warning).

## Verification Gates (Plan-level)

- **Build gate:** `./scripts/build.sh JamWideJuce_Standalone test_rawdata_send test_video_state_machine test_video_fourcc test_video_encoder test_video_recv_state test_video_decoder test_remote_frame_distributor test_video_sync_e2e` builds cleanly.
- **Test gate:** `ctest --output-on-failure -R "rawdata_send|video_state_machine|video_fourcc|video_encoder|video_recv_state|video_decoder|remote_frame_distributor|video_sync_e2e"` exits 0; 8/8 tests pass.
- **Codex Cluster 3 (a) gate:** `cancelPendingUpdate` + `inFlightCv_.wait` both present in PeerVideoSink.cpp.
- **Codex Cluster 3 (b) dtor reorder gate:** in JamWideJuceProcessor.cpp dtor, `client.reset()` line precedes `remoteFrameDistributor.reset()` line. Verified via awk on the dtor body.
- **Codex Cluster 3 (c) four-step shutdown gate:** `decoder->close()` + `decoder->setSink(nullptr)` + `vs->sink = nullptr` + `removeSink` all present in JamWideRemoteFrameDistributor.cpp op_tear_down_decoder.
- **Codex Cluster 4 two-phase startup gate:** `ensureVideoDecoderForPeerLater_` + `completeVideoDecoderStartup_` both present in njclient.cpp; `decoder_startup_needed` present in njclient.h.
- **Codex Cluster 9 UAT closure policy gate:** `Phase Closure Policy` + `deferred-risk` + `tracked in STATE.md → Phase 24` all present in phase-21-receive-uat-procedure.md.
- **Codex Cluster 1 timing-instrumentation gate:** `getRunVideoReceiveBlockMaxNanosForTest` referenced in phase-21-receive-uat-procedure.md Cell 1 + Cell 4 acceptance.
- **Codex Cluster 10 LOW wording gate:** `v1.3 fixed` appears in phase-21-receive-uat-procedure.md AND in njclient.cpp.
- **Cross-plan sender_seq alignment gate:** `getLastObservedSenderSeqForTest` accessor in PeerVideoSink.h + .cpp; test_video_sync_e2e exercises it with monotonic alice/bob/charlie assertions.
- **Subscription RAII gate:** `class Subscription` present in JamWideRemoteFrameDistributor.h; move-only; ~Subscription calls unsubscribe_ which routes to PeerVideoSink::removeListener (which blocks on inFlightCv_).
- **Multiple subscribers per peer gate:** test_two_listeners_same_peer_both_called passes.

## User Setup Required

For Task 5 (live UAT):

- Connect to `video.ninjamzap.com:2049` (public NinjamZap reference server).
- Coordinate with one collaborator for Cells 1-3; with two collaborators for Cell 4. Out-of-band (Slack / SMS).
- Follow tests/uat/phase-21-receive-uat-procedure.md cell-by-cell.
- Populate tests/uat/phase-21-receive-uat-report.md per codex Cluster 9 closure policy.

## Next Phase Readiness

- **Phase 22 (UI tile + grid + popout):** all the surfaces Phase 22 needs are in place.
  - `processor->getRemoteFrameDistributor()` returns the distributor pointer.
  - `distributor->subscribeToPeer(username, chidx, onRepaint)` returns a `Subscription` RAII handle Phase 22's per-peer tile component stores as a member.
  - `distributor->findSink(username, chidx)` returns the `PeerVideoSink*` Phase 22's `paint()` reads (under brief bufferLock to snapshot `image_front`; lock-free reads of `generation` + D-20 status atomics).
  - On first H.264 BEGIN per peer, the receive pipeline lazy-constructs the decoder + sink (codex Cluster 4 two-phase). On user-leave, the four-step shutdown protocol tears down (codex Cluster 3). Phase 22's tile lifetime is independent — the `Subscription`'s ~Subscription detaches the listener and blocks in-flight without touching the sink.
- **Phase 23 (packaging + signing) + Phase 24 (BETA UAT):** unaffected by Plan 21-03 changes. The new ops table is JUCE-linked TU only; no new packaging surface.
- **Open blocker:** Task 5 live UAT against video.ninjamzap.com:2049 required to close Phase 21 per codex Cluster 9. Until that runs and either PASSes all 4 cells OR documents deferred-risk records per BLOCKED cell, the phase is OPEN.

## Self-Check: PASSED (Tasks 1-4)

- `juce/video/distributor/PeerVideoSink.h` — present
- `juce/video/distributor/PeerVideoSink.cpp` — present, codex Cluster 3 dtor contract
- `juce/video/distributor/JamWideRemoteFrameDistributor.h` — present, Subscription RAII + factory declarations
- `juce/video/distributor/JamWideRemoteFrameDistributor.cpp` — present, op_* function-pointer impls
- `juce/JamWideJuceProcessor.h` — modified, remoteFrameDistributor member + accessor added
- `juce/JamWideJuceProcessor.cpp` — modified, ctor wiring + codex Cluster 3 REVERSED dtor order
- `juce/video/decoder/Openh264Decoder.cpp` — modified, cross-plan sender_seq forwarding wired
- `src/core/njclient.h` — modified, forward decl + setters + ops struct + helpers + decoder_startup_needed flag
- `src/core/njclient.cpp` — modified, helper bodies + BEGIN dispatch wiring + user-leave four-step shutdown
- `tests/test_remote_frame_distributor.cpp` — present, 4 sub-tests pass
- `tests/test_video_sync_e2e.cpp` — present, 1 sub-test passes
- `tests/uat/phase-21-receive-uat-procedure.md` — present, 4 cells + codex Cluster 9 closure policy
- `tests/uat/phase-21-receive-uat-report.md` — present, empty template ready for Task 5
- `CMakeLists.txt` — modified, distributor sources + new tests wired
- Commit `fa62224` (Task 1) — present in `git log`
- Commit `53d77b3` (Task 2) — present in `git log`
- Commit `d55f3b6` (Task 3) — present in `git log`
- Commit `0e1c5fd` (Task 4) — present in `git log`
- Commit `b495a4b` (Task 3 link-fix) — present in `git log`

---

*Phase: 21-h-264-decoder-receive-pipeline*
*Plan: 03*
*Tasks 1-4 completed: 2026-05-17*
*Task 5 (live UAT) human-pending: status will be recorded in `tests/uat/phase-21-receive-uat-report.md` per codex Cluster 9 closure policy*
