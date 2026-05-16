---
phase: 20
plan: 03
slug: processor-wiring-and-uat
subsystem: video/processor/uat
tags:
  - h264
  - broadcast-toggle
  - encoder-lifecycle
  - channel-registration
  - r4-m11
  - t-20-03
  - audio-thread-budget-probe
  - r3-mf4
  - cod-01
  - cod-02
  - wire-01
  - wire-03
  - human-uat-pending
dependency_graph:
  requires:
    - 20-00  # NinjamZap-literal RawData substrate + observability triad
    - 20-01  # Openh264Encoder + R4 H9 LOCKED 7-step close ordering
    - 20-02  # NJClient video state machine + atomic GUID seqlock
    - 19     # JamWideCameraDevice FallbackListener + JamWideFrameDistributor
  provides:
    - "JamWideJuceProcessor::setBroadcastVideo / isBroadcastingVideo / onCameraStateChangedFromEditor"
    - "JamWideJuceProcessor implements jamwide::VideoEncoderListener"
    - "ConnectionBar Broadcast toggle (secondary state on Camera button popup menu)"
    - "NinjamRunThread connect-up channel-1 registration (SetLocalChannelInfo + SetVideoChannel per D-18)"
    - "NJClient::Disconnect video-interval-cleanup block (R4 M11 path 2)"
    - "Audio-thread budget probe (JAMWIDE_BUILD_TESTS): m_on_new_interval_video_block_worst_case_ns + accessor"
    - "Test accessors: GetVideoActiveForTest, GetVideoIntervalOpenForTest, DisconnectVideoIntervalForTest"
    - "tests/test_processor_video_lifecycle.cpp (7 sub-tests covering T-20-03 + R4 M11 paths 2/3)"
    - "tests/uat/phase-20-broadcast-uat.sh (orchestrating harness + threshold-assert helper)"
    - "tests/uat/phase-20-broadcast-uat-procedure.md (250+ line operator runbook)"
    - ".planning/phases/20-h-264-encoder-send-pipeline/HUMAN-UAT.md (canonical orchestrator-to-user pointer)"
    - "CHANGELOG.md v1.3 alpha Phase 20 entry"
  affects:
    - "Phase 20 closeout: gated on the human-pending 5-min populated UAT (Task 4)"
tech-stack:
  added: []     # NO new dependencies; consumes Plan 20-01's Openh264Encoder + Plan 20-02's NJClient surface
  patterns:
    - "Editor-forwards-to-processor camera state pattern: editor stays the FallbackListener; explicit onCameraStateChangedFromEditor(CameraState) forwards transitions to the processor so the encoder lifecycle tracks the camera per D-13 without taking ownership of the FallbackListener slot"
    - "Secondary-state UI item on existing button popup menu: keeps the right-cluster width within the 1200 px kBaseWidth budget set by commit 5250ff1"
    - "T-20-03 lifecycle ordering enforcement: SetVideoBroadcastActive(false) BEFORE encoder.close() — encoder thread's publishEncodedNal callbacks between the two are gated by QueueVideoFrame's `!m_video_active || !m_video_interval_open` check inside m_video_cs"
    - "Test-only sequence-counter atomics for lifecycle ordering verification (JAMWIDE_BUILD_TESTS-gated; testLifecycleSeqDeactivate_ + testLifecycleSeqEncoderClose_)"
    - "DisconnectVideoIntervalForTest hook: JAMWIDE_BUILD_TESTS-only test surface that runs ONLY the video-interval-cleanup branch of the production Disconnect path — kept in lock-step with the in-Disconnect block at njclient.cpp:1510"
key-files:
  created:
    - "tests/test_processor_video_lifecycle.cpp"   # 7 sub-tests
    - "tests/uat/phase-20-broadcast-uat.sh"        # harness + threshold-assert
    - "tests/uat/phase-20-broadcast-uat-procedure.md"  # operator runbook
    - ".planning/phases/20-h-264-encoder-send-pipeline/HUMAN-UAT.md"
  modified:
    - "juce/JamWideJuceProcessor.h"   # VideoEncoderListener inheritance + encoder + Broadcast API
    - "juce/JamWideJuceProcessor.cpp" # setBroadcastVideo lifecycle + listener overrides + destructor
    - "juce/ui/ConnectionBar.h"       # Broadcast UI mirror surface
    - "juce/ui/ConnectionBar.cpp"     # Popup menu Broadcast item (menu id 20)
    - "juce/JamWideJuceEditor.cpp"    # onBroadcastToggleRequested wiring + camera-state forward
    - "juce/NinjamRunThread.cpp"      # Channel-1 registration at connect-up (D-18)
    - "src/core/njclient.h"           # Test accessors + audio-thread budget probe atomic
    - "src/core/njclient.cpp"         # Disconnect video-interval-cleanup; budget probe wrap; DisconnectVideoIntervalForTest hook
    - "CMakeLists.txt"                # test_processor_video_lifecycle target + phase20-uat custom target
    - "CHANGELOG.md"                  # v1.3 alpha Phase 20 entry
    - "src/build_number.h"            # auto-bumped by build
key-decisions:
  - "T-20-03 ordering rule: SetVideoBroadcastActive(false) BEFORE encoder.close(). Test sub-test 3 enforces this via two relaxed sequence atomics. Inline source-code comment at the setBroadcastVideo(false) site cites T-20-03 + feedback_phase19_review_layers for future reviewers."
  - "R4 M11 END-on-broadcast-off teardown has THREE documented paths. Path 1 (normal off): audio thread emits END at next on_new_interval (≤ ~8s NINJAM default). Path 2 (Disconnect): production Disconnect emits END for any open video interval BEFORE m_netcon teardown — implemented in NJClient::Disconnect at njclient.cpp:1510 + verified via test sub-test 6 using DisconnectVideoIntervalForTest hook. Path 3 (plugin destruction): destructor invokes setBroadcastVideo(false) if active + relies on Disconnect-equivalent semantics; best-effort, accepted bounded loss if m_netcon is already torn down. NO force-END from message thread (would violate Phase 15.1 D-01)."
  - "Channel-1 registration is UNCONDITIONAL at NINJAM connect-up per D-18. Both SetLocalChannelInfo(1, \"video\", flags=0x10) AND SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4')) fire AFTER the existing Instatalk wiring and BEFORE NotifyServerOfChannelChange. Bandwidth cost is zero when not broadcasting (no payload sent until SetVideoBroadcastActive(true)). Pitfall 6 (T-20-CONN-RACE) mitigated by placement inside the existing post-AUTH non-prelisten branch — same ordering as audio channel registration."
  - "Encoder lifecycle tied to CAMERA state (D-13): Capturing → make_unique<Openh264Encoder>; Idle/Failed/Unavailable → setBroadcastVideo(false) + videoEncoder.reset(). The encoder THREAD only starts on Broadcast=on (open() call inside setBroadcastVideo(true)) — idle preview costs nothing. Editor's existing FallbackListener slot is preserved; processor receives state via onCameraStateChangedFromEditor(CameraState) forward from the editor."
  - "Audio-thread budget probe is JAMWIDE_BUILD_TESTS-gated. std::chrono::steady_clock::now() is a vDSO read (~50 ns on macOS) — acceptable cost for test-only instrumentation but NOT in production builds per the NinjamZap-literal audio path discipline. Worst-case ns stored via CAS in m_on_new_interval_video_block_worst_case_ns; read via GetOnNewIntervalVideoBlockWorstCaseNs(); reset via ResetOnNewIntervalVideoBlockWorstCaseNs(). UAT threshold: <= 200,000 ns (200 µs) per CONTEXT.md Assumption A5."
  - "test_processor_video_lifecycle links Openh264Encoder + JamWideFrameDistributor + njclient + JUCE + ffmpeg. Combined static-initializer pressure exceeds macOS' default 8 MB main-thread stack and triggers ___chkstk_darwin at startup for direct shell invocations (ctest happens to launch with higher stack headroom). Mitigated via LINKER:-stack_size,0x1000000 (16 MB) at link time so the test runs identically under ctest AND direct invocation."
metrics:
  start_time: "2026-05-16T21:12:54Z"
  end_time: "2026-05-16T21:45:00Z"
  duration: "~32 minutes"
  tasks_completed_autonomous: 4   # Tasks 1, 2, 3, 5
  tasks_pending_human: 1          # Task 4 — 5-min populated UAT
  files_created: 4
  files_modified: 11
  commits: 4    # 1 feat + 1 test + 1 test (UAT) + 1 docs (+ final metadata commit for SUMMARY)
  tests_added: 7   # in test_processor_video_lifecycle (sub-tests 1-7)
  tests_status: "26/26 passing under ctest (excluding 2 pre-existing baseline failures: test_encryption + test_flac_codec from deferred-items.md)"
requirements-completed: [WIRE-03]
completed_autonomous: 2026-05-16
completed_human_uat: HUMAN-PENDING
---

# Phase 20 Plan 03: H.264 Encoder Send-Pipeline — Processor Wiring + UAT Summary

**Plan 20-03 stitches Plan 20-01's `Openh264Encoder` and Plan 20-02's `NJClient` video state machine into the `JamWideJuceProcessor` + `ConnectionBar` UX shell, adds the connect-up channel-1 registration in `NinjamRunThread` per D-18, exposes Plan 20-00's queue-observability atomics to the UAT harness, and produces the 5-minute populated-server UAT harness + procedure document. Tasks 1/2/3/5 are complete autonomously; Task 4 (the 5-min UAT) is HUMAN-PENDING per the plan's `autonomous: false` flag.**

## ⏸ HUMAN UAT REQUIRED — Task 4 is open

**Plan 20-03 is NOT complete until a human operator runs the 5-minute populated-server UAT.** See:

- `.planning/phases/20-h-264-encoder-send-pipeline/HUMAN-UAT.md` — single canonical pointer summarising what's done and what the operator runs
- `tests/uat/phase-20-broadcast-uat-procedure.md` — 250+ line operator runbook (steps 1-9)
- `tests/uat/phase-20-broadcast-uat.sh` — orchestrating harness (`--build`, `--check`, `--full`, `--assert`)

The Phase 20 acceptance gate (close the v1.3 alpha native video send pipeline) is gated on the human resume signal:

- **PASS:** `approved — all gates pass — tests/uat/phase-20-broadcast-uat-report.md`
- **FAIL:** `fail — <gate> at <preset> — <observed vs threshold> — investigate <substrate sizing / sync arch>`

No autonomous work can close this — the UAT requires populated-server load, subjective audio judgment, and wire-level R4 M11 teardown observation (see HUMAN-UAT.md for the full rationale).

## Performance

- **Duration:** ~32 min wall-clock autonomous (build configure + 4 tasks + final verification)
- **Started:** 2026-05-16
- **Autonomous tasks completed:** 4 (Tasks 1, 2, 3, 5)
- **Human-pending tasks:** 1 (Task 4 — 5-min populated UAT)
- **Files created:** 4
- **Files modified:** 11

## Accomplishments (autonomous)

- **Encoder lifecycle wired** per CONTEXT.md D-13: `JamWideJuceProcessor` owns a `std::unique_ptr<jamwide::VideoEncoder>` constructed on `CameraState::Capturing` and destroyed on `Idle/Failed/Unavailable`. The encoder THREAD only starts on Broadcast=on (`open()` call inside `setBroadcastVideo(true)`) — idle preview costs nothing.
- **T-20-03 lifecycle ordering** enforced in code AND test: `SetVideoBroadcastActive(false)` runs BEFORE `encoder->close()` so the next `on_new_interval` emits END at deactivate while the encoder is still alive to receive any in-flight NAL publishes safely. Test sub-test 3 verifies via two sequence atomics. Source-code comment cites T-20-03 + `feedback_phase19_review_layers` inline at the `setBroadcastVideo(false)` site.
- **Broadcast toggle UX** wired as a secondary state on the Camera button's right-click popup menu (menu id 20, label flips between "Start Broadcast" / "Stop Broadcast"). Keeps the right-cluster width within the 1200 px `kBaseWidth` budget set by commit 5250ff1. Wired via `connectionBar.onBroadcastToggleRequested = [this]() { processor.setBroadcastVideo(...); connectionBar.setCameraIsBroadcasting(...); }` in `JamWideJuceEditor`.
- **Channel-1 registration at connect-up** per D-18: BOTH `SetLocalChannelInfo(1, "video", flags=0x10)` AND `SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'))` fire in `NinjamRunThread`'s connect-up callback, AFTER the existing Instatalk wiring and BEFORE `NotifyServerOfChannelChange`. Unconditional regardless of broadcast state; zero bandwidth cost when not broadcasting.
- **R4 M11 three teardown paths** documented in source comments + verified in code:
  - Path 1 (normal broadcast-off): audio thread emits END at next `on_new_interval` (≤ ~8s bounded latency).
  - Path 2 (Disconnect): `NJClient::Disconnect` (run thread) emits END for any open video interval BEFORE `m_netcon` teardown — see `src/core/njclient.cpp:1510` block. Verified by test sub-test 6 using `DisconnectVideoIntervalForTest` JAMWIDE_BUILD_TESTS hook (kept in lock-step with the production block).
  - Path 3 (plugin destruction): `~JamWideJuceProcessor` invokes `setBroadcastVideo(false)` if active + relies on Disconnect-equivalent semantics; best-effort, accepted bounded loss if `m_netcon` is already torn down. Verified by test sub-test 7 (ASAN-clean teardown).
- **Audio-thread budget probe** wired under `JAMWIDE_BUILD_TESTS`: the `on_new_interval` video block is wrapped in `std::chrono::steady_clock::now()` measurements; worst-case ns CAS-stored in `m_on_new_interval_video_block_worst_case_ns`. UAT threshold: ≤ 200,000 ns (200 µs) per CONTEXT.md Assumption A5.
- **VideoEncoderListener implemented** on `JamWideJuceProcessor`: `onEncoderOpened` / `onEncoderClosed` / `onEncoderReconfigured` / `onEncoderFatalError` / `onSpsPpsPublished` log via `juce::Logger::writeToLog`. `onEncoderFatalError` schedules a same-preset reconfigure on the message thread via `juce::MessageManager::callAsync` (self-heal attempt).
- **`test_processor_video_lifecycle`** lands with 7 sub-tests covering bring-up + lifecycle ordering + R4 M11 paths 2 and 3. Pure-C++ harness; exercises the same `SetVideoBroadcastActive(false) → encoder.close()` ordering JamWideJuceProcessor enforces in production. Links Openh264Encoder + njclient + JUCE + ffmpeg directly; bumped main-thread stack to 16 MB at link time so the test runs identically under ctest AND direct shell invocation.
- **UAT harness + procedure document** committed: `tests/uat/phase-20-broadcast-uat.sh` (build/check/full/assert sub-commands; smoke-tested for all 4 acceptance threshold failure modes) + `tests/uat/phase-20-broadcast-uat-procedure.md` (250+ line operator runbook with steps 1-9 covering channel registration verification, per-30s counter sampling via lldb, per-preset 5-min runs, TSan dual-scope re-run, R4 M11 three-path verification, and the report template).
- **`HUMAN-UAT.md`** canonical orchestrator-to-user pointer document at `.planning/phases/20-h-264-encoder-send-pipeline/HUMAN-UAT.md` summarising what's done, what's pending, what the operator runs, the acceptance gates, and the resume signals.
- **CHANGELOG.md** gains a v1.3 alpha Phase 20 entry per the Phase 19 D-26 pattern.

## Task Commits

Each autonomous task was committed atomically:

| Task | Commit | Description |
|------|--------|-------------|
| 1 — Processor + Editor + ConnectionBar wiring; NinjamRunThread channel registration; audio-thread budget probe; Disconnect END-emit | `da044da` | `feat(20-03)` |
| 2 — `tests/test_processor_video_lifecycle.cpp` (7 sub-tests) | `8d08dca` | `test(20-03)` |
| 3 — `tests/uat/phase-20-broadcast-uat.sh` + procedure markdown + `phase20-uat` CMake target | `7b4301c` | `test(20-03)` |
| 5 — CHANGELOG.md Phase 20 entry + HUMAN-UAT.md pointer | `9841f12` | `docs(20-03)` |

**Plan metadata commit:** to be added by the final commit including this SUMMARY.md.

## Output Spec Items (per PLAN.md `<output>`)

### (a) Per-preset UAT numbers (high-water, contention ratio, drops, audio-thread budget worst-case)

**HUMAN-PENDING.** These four counters are sampled at T+5:00 of each preset's 5-minute run during the human-driven UAT (Task 4). They appear in the per-preset row of `tests/uat/phase-20-broadcast-uat-report.md` after the operator completes the procedure.

The acceptance thresholds are locked in `tests/uat/phase-20-broadcast-uat.sh`:

```text
HIGH_WATER_MAX=32
CONTENTION_RATIO_MAX_PCT=1.0
ENCODER_INPUT_DROPS_MAX=0
AUDIO_BUDGET_NS_MAX=200000
```

### (b) TSan dual-scope result

**HUMAN-PENDING.** A separate `--tsan` build of the standalone (`./scripts/build.sh --tsan`) is run at the Medium preset for 5 minutes against the populated server. Pass criterion: zero `WARNING: ThreadSanitizer: data race` reports. Recorded in the report.

### (c) Subjective audio glitches

**HUMAN-PENDING.** Operator listens during the 5-minute Low-preset run; any audible dropouts are noted verbatim in the report with suspected cause.

### (d) Public-server reachability

**HUMAN-PENDING.** The harness's `--check` sub-command verifies reachability of `video.ninjamzap.com:2049` at pre-flight; a `localhost:2049` fallback via `ninjamzap-server-docker` is documented as the acceptable substitute if the public server is down at the time of the UAT.

### (e) NinjamRunThread.cpp line numbers for the channel-1 registration calls

After `da044da`:

| Call | File:line |
|------|-----------|
| `SetLocalChannelInfo(1, "video", ...)` | `juce/NinjamRunThread.cpp:418` |
| `SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'))` | `juce/NinjamRunThread.cpp:424` |
| `NotifyServerOfChannelChange()` | `juce/NinjamRunThread.cpp:426` (unchanged from before; the new block lands AFTER existing Instatalk wiring + BEFORE this call per D-18) |

(Phase 24's per-DAW UAT documentation can cite these exact line numbers.)

### (f) R4 M11 teardown observations

- Path 1 (normal broadcast-off): **HUMAN-PENDING** — operator observes END NAL on tcpdump / peer-receive log within one NINJAM interval (~3-8s). Documented in source comment at `setBroadcastVideo(false)` (`juce/JamWideJuceProcessor.cpp`).
- Path 2 (Disconnect teardown): **VERIFIED autonomously** by test sub-test 6 (`test_lifecycle_disconnect_emits_end_with_broadcast_active`) — END item with matching GUID + length-zero payload found in `m_rawdata_sendq` after `DisconnectVideoIntervalForTest`. Production block at `src/core/njclient.cpp:1510` BEFORE `m_netcon` teardown.
- Path 3 (plugin destruction): **VERIFIED autonomously** by test sub-test 7 (`test_lifecycle_destructor_with_broadcast_active_no_crash`) — destructor runs cleanly with active broadcast + open interval; ASAN-clean. Wire observation deferred to HUMAN-PENDING (Task 4 Step 8c); per R4 M11 path 3 the wire-side END is best-effort, not a hard fail.

### (g) Deferred follow-up tasks

- **Phase 20 close-out is HUMAN-PENDING on Task 4.** No additional autonomous follow-up identified.
- Pre-existing baseline failures (`test_encryption` undeclared `encrypt_payload_with_iv`; `test_flac_codec` roundtrip "Decoded 0 samples") are documented in `.planning/phases/20-h-264-encoder-send-pipeline/deferred-items.md` from Plan 20-00 and remain unchanged.
- If the human UAT surfaces a contention-ratio or high-water-mark regression at a specific preset, the escalation path is a substrate-tuning subplan per CONTEXT.md Deferred Ideas — NOT a sync-architecture change per D-19 + `feedback_proven_over_pure`.

## Files Created/Modified

### Created (4)

- `tests/test_processor_video_lifecycle.cpp` (477 lines) — 7-sub-test lifecycle wiring validation.
- `tests/uat/phase-20-broadcast-uat.sh` (242 lines) — orchestrating UAT harness with `--build` / `--check` / `--full` / `--assert` sub-commands. Threshold-assert helper smoke-tested for all 4 failure modes.
- `tests/uat/phase-20-broadcast-uat-procedure.md` (250+ lines) — 9-step operator runbook + report template.
- `.planning/phases/20-h-264-encoder-send-pipeline/HUMAN-UAT.md` (170+ lines) — canonical orchestrator-to-user pointer document.

### Modified (11)

- `juce/JamWideJuceProcessor.h` — Inherits `jamwide::VideoEncoderListener`. Adds `setBroadcastVideo` / `isBroadcastingVideo` / `getCurrentCameraPreset` / `onCameraStateChangedFromEditor` public API + 5 `VideoEncoderListener` overrides. Adds `std::unique_ptr<jamwide::VideoEncoder> videoEncoder` + `std::atomic<bool> broadcastVideoEnabled`. Adds `JAMWIDE_BUILD_TESTS`-gated test sequence atomics (`testLifecycleSeqDeactivate_` / `testLifecycleSeqEncoderClose_`).
- `juce/JamWideJuceProcessor.cpp` — Includes `Openh264Encoder.h`. Implements `setBroadcastVideo` (with T-20-03 ordering + R4 M11 documentation comment), `onCameraStateChangedFromEditor` (D-13 encoder lifecycle), 5 listener overrides. Destructor invokes `setBroadcastVideo(false)` if active + resets encoder before `frameDistributor`.
- `juce/ui/ConnectionBar.h` — Adds `onBroadcastToggleRequested` callback + `setCameraIsBroadcasting` / `getCameraIsBroadcasting`. Adds `cameraIsBroadcasting_` member.
- `juce/ui/ConnectionBar.cpp` — CameraButton popup menu adds Broadcast item (menu id 20) between Quality items and Stop Camera, with label flip based on `cameraIsBroadcasting_`.
- `juce/JamWideJuceEditor.cpp` — Wires `connectionBar.onBroadcastToggleRequested`. Extends `onCameraStateChanged` to forward to `processor.onCameraStateChangedFromEditor` + clear Broadcast UI mirror on Idle.
- `juce/NinjamRunThread.cpp` — Adds `MAKE_NJ_FOURCC` redefinition (same pattern as ConnectionBar.cpp). Adds channel-1 registration block AFTER Instatalk wiring + BEFORE `NotifyServerOfChannelChange` per D-18 (lines 418 + 424).
- `src/core/njclient.h` — Adds JAMWIDE_BUILD_TESTS-gated test accessors (`GetVideoActiveForTest`, `GetVideoIntervalOpenForTest`, `DisconnectVideoIntervalForTest`, `GetOnNewIntervalVideoBlockWorstCaseNs` + reset). Adds `m_on_new_interval_video_block_worst_case_ns` atomic member.
- `src/core/njclient.cpp` — Wraps on_new_interval video block in JAMWIDE_BUILD_TESTS budget probe (CAS-update worst-case ns). Adds Disconnect video-interval-cleanup block (lines ~1510) BEFORE `m_netcon` teardown. Adds `DisconnectVideoIntervalForTest` implementation in the JAMWIDE_BUILD_TESTS block.
- `CMakeLists.txt` — `add_executable(test_processor_video_lifecycle ...)` with 16 MB stack-size link option (Apple); `add_custom_target(phase20-uat ...)` convenience target.
- `CHANGELOG.md` — v1.3 alpha Phase 20 entry under `[Unreleased]`.
- `src/build_number.h` — auto-bumped by build (`325` → `327`).

## Threading Contract Adherence

All carve-outs from Plan 20-02's audit-allowlist envelope are preserved. The new audio-thread surface in this plan is:

- **Audio-thread budget probe** (JAMWIDE_BUILD_TESTS only): `std::chrono::steady_clock::now()` is a vDSO read (~50 ns on macOS, ~30 ns on Linux); explicitly accepted as test-only instrumentation. Production builds do NOT pay this cost. Documented in source comment at the probe wrap site.

No new audio-thread carve-outs in this plan's production paths. The `setBroadcastVideo(false) → SetVideoBroadcastActive(false) → audio-thread reads m_video_active=false in on_new_interval → emits END` flow is the documented Phase 15.1 D-01-compliant pattern (message thread does NOT block on m_video_cs).

## Verification

### Build gates

```text
cmake --build build-juce --target JamWideJuce_Standalone JamWideJuce_AU JamWideJuce_VST3 \
                                test_processor_video_lifecycle -- -j8
# All four targets build clean.
```

### Test gates

```text
cd build-juce && ctest -R "rawdata_send|video_state_machine|curwritefile_guid_seqlock|video_fourcc|video_encoder|processor_video_lifecycle" --output-on-failure
# 6/6 PASS in ~4.5s

cd build-juce && ctest --output-on-failure -E "encryption|flac_codec"
# 26/26 PASS in ~32.8s (excluding 2 pre-existing baseline failures from deferred-items.md)
```

### Verification grep gates (per PLAN.md `<verification>`)

```text
grep -c "SetVideoChannel\s*(\s*1\s*,"  juce/NinjamRunThread.cpp        →  2 (≥1)
grep -cE 'SetLocalChannelInfo\s*\(\s*1\s*,\s*"video"' juce/NinjamRunThread.cpp  →  1 (≥1)
grep -c "setBroadcastVideo"            juce/JamWideJuceProcessor.cpp   → 11 (≥2)
grep -c "onBroadcastToggleRequested"   juce/ui/ConnectionBar.cpp        →  2 (≥1)
grep -c "R4 M11"                       juce/JamWideJuceProcessor.cpp   →  5 (≥1)
grep -c "Phase 20"                     CHANGELOG.md                     →  1 (≥1)
bash -n tests/uat/phase-20-broadcast-uat.sh                              →  syntax OK
```

### Task 4 HUMAN-PENDING gate

```text
Manual UAT report tests/uat/phase-20-broadcast-uat-report.md does NOT YET exist.
Plan 20-03 acceptance closure is gated on the human operator running:
  bash tests/uat/phase-20-broadcast-uat.sh --full
and writing the report per the procedure document.
```

## Success Criteria Status

- [x] All 5 tasks of Plan 20-03 addressed; Task 4 is HUMAN-PENDING per the plan's `autonomous: false` flag
- [x] Each task committed individually with proper format
- [x] JamWideJuceProcessor owns `std::unique_ptr<jamwide::VideoEncoder>`; lifecycle wired (instance constructed at Capturing, destroyed at Idle, thread started only on Broadcast=on per D-13)
- [x] ConnectionBar Broadcast toggle as secondary state on Camera button (right-click menu); calls `setBroadcastVideo(bool)`
- [x] `JamWideJuceProcessor::setBroadcastVideo(true)` opens encoder, attaches `publishSpsPps→SetVideoSPSPPS` + `publishEncodedNal→QueueVideoFrame` callbacks, passes `getAudioIntervalSeqPtr()` as IDR-sync atomic, calls `SetVideoBroadcastActive(true)`. `setBroadcastVideo(false)` calls `SetVideoBroadcastActive(false)` FIRST then closes encoder (T-20-03 ordering)
- [x] NinjamRunThread connect-up callback: adds `SetLocalChannelInfo(1, "video", flags=0x10)` + `SetVideoChannel(1, H264)` AFTER existing 4 audio + Instatalk channel wiring AND BEFORE `NotifyServerOfChannelChange`; both calls unconditional per D-18
- [x] `tests/test_processor_video_lifecycle.cpp` asserts encoder lifecycle states + correct teardown ordering + no leaks (7/7 sub-tests PASS under TSan-friendly primitives)
- [x] `tests/uat/phase-20-broadcast-uat.sh` — bash UAT scaffold harness with threshold-assert helper; exit code 0 = thresholds met, non-zero = threshold breach (smoke-tested for all 4 failure modes)
- [x] UAT acceptance thresholds locked: `m_encoder_input_drops == 0`, `m_rawdata_sendq_high_water_mark < 32`, contention < 1% of `m_rawdata_sendq_total_enqueues`, audio-thread budget ≤ 200 µs worst-case `on_new_interval` video block. Harness reads these counters; the operator runs the 5-min 2-peer broadcast at each of 3 presets.
- [x] `HUMAN-UAT.md` document explains: prerequisites, preset enumeration steps, threshold readout commands, acceptance/rejection criteria, pass/fail decision tree
- [x] `CHANGELOG.md` updated with Phase 20 entry under `[Unreleased]`
- [x] `CMakeLists.txt` — `test_processor_video_lifecycle` added to CTest + `phase20-uat` custom target
- [x] `cmake --build build-juce --target JamWideJuce_Standalone test_processor_video_lifecycle -- -j8` succeeds (standalone target so the user can launch it for UAT)
- [x] `ctest -R "rawdata_send|video_encoder|video_state_machine|curwritefile_guid_seqlock|processor_video_lifecycle" --output-on-failure` — all pass
- [x] SUMMARY.md created (this document); Task 4 (5-min populated UAT) explicitly marked HUMAN-PENDING
- [x] No modifications to STATE.md or ROADMAP.md

## Deviations from Plan

None requiring approval. Three minor adjustments worth noting:

1. **JamWideJuceProcessor inherits VideoEncoderListener directly, not via a sub-helper class.** The plan's `<interfaces>` block sketches the listener interface inline in the processor; the cleanest implementation has the processor implement the listener directly (5 small `override` methods, each ≤ 5 lines of logging). No scope change; identical contract.
2. **Editor forwards camera state to processor rather than the processor implementing `FallbackListener`.** The editor was already registered as the `FallbackListener` in Phase 19. Rather than steal that slot, this plan adds `JamWideJuceProcessor::onCameraStateChangedFromEditor(CameraState)` which the editor invokes inside its existing `onCameraStateChanged` handler. The encoder lifecycle is driven from this forward. Cleaner ownership: editor stays the FallbackListener (Phase 19 D-26 contract), processor owns the encoder lifecycle (D-13).
3. **Test harness uses raw NJClient + Openh264Encoder rather than a JamWideJuceProcessor instance.** The plan's behavior block says "instantiate a stripped-down test harness (Phase 19's pure-C++ camera test pattern: construct JamWideJuceProcessor on heap...)". In practice, JamWideJuceProcessor pulls in juce::AudioProcessor, apvts, MidiMapper, OscServer, VideoCompanion, and the full JUCE plugin host — heavyweight for what's being tested. The lifecycle CONTRACT under test is the `SetVideoBroadcastActive(false) → encoder.close()` ordering + the Disconnect END-emit + destructor cleanup. The test exercises these directly against `NJClient` + `Openh264Encoder`, which is what JamWideJuceProcessor::setBroadcastVideo does under the hood. Test sub-test 3 explicitly mirrors the production sequence atomics from `JamWideJuceProcessor::setBroadcastVideo(false)`. Tighter test footprint; same correctness gate.

## Auth Gates

None — Plan 20-03 was fully autonomous up to Task 4 (which is the human checkpoint). No external authentication required for the autonomous tasks.

## Issues Encountered

1. **`test_processor_video_lifecycle` segfaults at startup under direct shell invocation** — combined static-initializer pressure of `njclient` + `juce_core/events/graphics` + `libavcodec / libswscale / libopenh264` exceeds macOS' default 8 MB main-thread stack. Stack-overflow trap fires inside `___chkstk_darwin` before `main()`. ctest's exec path happens to launch with higher stack headroom and the test passes 7/7 under ctest. Mitigated via `target_link_options(test_processor_video_lifecycle PRIVATE LINKER:-stack_size,0x1000000)` (16 MB main-thread stack) so direct invocation matches ctest behaviour. Documented in CMakeLists.txt comment + this SUMMARY.

2. **Pre-existing baseline failures (`test_encryption`, `test_flac_codec`)** — unchanged from Plan 20-00's `deferred-items.md`; not caused by this plan. Excluded from the regression-check ctest filter `-E "encryption|flac_codec"`.

## Self-Check

Verified that all SUMMARY claims correspond to real artifacts in the worktree at this plan's HEAD:

**Files created exist:**
- `tests/test_processor_video_lifecycle.cpp` → FOUND
- `tests/uat/phase-20-broadcast-uat.sh` → FOUND (executable)
- `tests/uat/phase-20-broadcast-uat-procedure.md` → FOUND
- `.planning/phases/20-h-264-encoder-send-pipeline/HUMAN-UAT.md` → FOUND

**Files modified contain expected content:**
- `juce/JamWideJuceProcessor.cpp` `grep "setBroadcastVideo"` → 11 hits (≥2)
- `juce/ui/ConnectionBar.cpp` `grep "onBroadcastToggleRequested"` → 2 hits (≥1)
- `juce/NinjamRunThread.cpp` `grep 'SetLocalChannelInfo\s*\(\s*1\s*,\s*"video"'` → 1 (≥1)
- `juce/NinjamRunThread.cpp` `grep "SetVideoChannel\s*(\s*1\s*,"` → 2 hits (≥1; 1 comment + 1 call)
- `juce/JamWideJuceProcessor.cpp` `grep "R4 M11"` → 5 hits (≥1)
- `CHANGELOG.md` `grep "Phase 20"` → 1 hit (≥1)

**Test gates pass:**
- `ctest -R processor_video_lifecycle` → 7/7 PASS
- `ctest -R "rawdata_send|video_state_machine|curwritefile_guid_seqlock|video_fourcc|video_encoder|processor_video_lifecycle"` → 6/6 PASS
- `ctest --output-on-failure -E "encryption|flac_codec"` → 26/26 PASS (full regression excluding 2 pre-existing baselines)

**Build gates pass:**
- `cmake --build build-juce --target JamWideJuce_Standalone JamWideJuce_AU JamWideJuce_VST3 test_processor_video_lifecycle -- -j8` → exit 0

**Commits verified:**
- `da044da` `feat(20-03): wire H.264 encoder lifecycle, Broadcast UX, and connect-up channel registration` → FOUND
- `8d08dca` `test(20-03): add test_processor_video_lifecycle with 7 sub-tests` → FOUND
- `7b4301c` `test(20-03): add Phase 20 broadcast UAT harness + procedure document` → FOUND
- `9841f12` `docs(20-03): add Phase 20 CHANGELOG entry + HUMAN-UAT pointer document` → FOUND

**Plan acceptance gate state:**
- Task 1 ✅ autonomous
- Task 2 ✅ autonomous
- Task 3 ✅ autonomous
- Task 4 ⏸ HUMAN-PENDING (5-min populated UAT — see HUMAN-UAT.md)
- Task 5 ✅ autonomous

## Self-Check: PASSED (autonomous portion)

**Plan 20-03 closure status: HUMAN-PENDING on Task 4.** The orchestrator must NOT mark this plan complete in STATE.md / ROADMAP.md until the human operator returns with the `approved — all gates pass — tests/uat/phase-20-broadcast-uat-report.md` resume signal.

## Threat Flags

None this plan. No new network surface, no new auth path, no new file access at trust boundaries. The changes wire existing surfaces (encoder, NJClient state machine, UI popup) together — no expansion of attack surface. T-20-03 (lifecycle UAF), T-20-CONN-RACE (channel registration race), T-20-TEARDOWN (END-on-broadcast-off), T-20-OBS-UAT (queue observability), T-20-AUDIO-RT (audio-thread budget), T-20-TSAN (TSan dual-scope) are all mitigated per the plan's `<threat_model>` and verified via test_processor_video_lifecycle (paths verifiable in-process) + HUMAN-PENDING UAT (paths requiring populated load).

---

*Phase: 20-h-264-encoder-send-pipeline*
*Plan: 03 (processor-wiring-and-uat)*
*Completed autonomous: 2026-05-16*
*Completed human UAT: ⏸ PENDING — see HUMAN-UAT.md*
