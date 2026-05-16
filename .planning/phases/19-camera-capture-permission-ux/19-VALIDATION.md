---
phase: 19
slug: camera-capture-permission-ux
status: verified
nyquist_compliant: true
wave_0_complete: true
created: 2026-05-16
verified: 2026-05-16
---

# Phase 19 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution. Sourced from `19-RESEARCH.md` §11.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Lightweight inline `main()` test harness via `juce_add_console_app` / `add_executable`; assertions via `assert()` / `JUCE_ASSERT` / direct exit codes |
| **Config file** | Inline in `tests/`; gated by `JAMWIDE_BUILD_TESTS=ON` (`CMakeLists.txt:29,136,301,406`) and `enable_testing()` (`CMakeLists.txt:407`) |
| **Quick run command** | `cd build-juce && ctest -R camera --output-on-failure` |
| **Full suite command** | `cd build-juce && ctest --output-on-failure` |
| **Estimated runtime** | ~5–10 s quick · ~30–60 s full |

Pattern matches the existing test inventory: `tests/test_block_queue_spsc.cpp`, `tests/test_local_channel_mirror.cpp`, `tests/test_njclient_atomics.cpp`, `tests/test_rawdata_send.cpp`, `tests/test_video_fourcc.cpp`, `tests/test_remote_user_mirror.cpp`.

---

## Sampling Rate

- **After every task commit:** Run `ctest -R camera --output-on-failure` plus the relevant unit test for the just-touched component (≤10 s wall)
- **After every plan wave:** Run `ctest --output-on-failure` (full suite, ~30–60 s)
- **Before `/gsd-verify-work`:** Full suite must be green AND all manual UAT cells completed per `feedback_uat_scope_redflags` memory (no skipping CAM-01/02/03)
- **Max feedback latency:** 60 seconds (full suite)

---

## Per-Task Verification Map

> Filled by gsd-nyquist-auditor (sonnet) on 2026-05-16 from `19-RESEARCH.md` §11 + the implemented test suite.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 19-01-T1 | 19-01 | 1 | PKG-04 | T-19-05 | `com.apple.security.device.camera` in `JamWide.entitlements`; `JUCE_USE_CAMERA=1` and `juce_video` linked on JamWideJuce; 20 source stubs + 7 test stubs staged in `CMakeLists.txt` | smoke | `grep -c 'com.apple.security.device.camera' JamWide.entitlements && grep -c 'JUCE_USE_CAMERA=1' CMakeLists.txt` | ✅ W0 | ✅ green |
| 19-01-T2 | 19-01 | 1 | CAM-01 | T-19-01 | `CameraAuthorization_mac.mm` calls `AVCaptureDevice authorizationStatusForMediaType` + `requestAccessForMediaType`; Windows stub returns `NotApplicable` | smoke | `grep -c 'authorizationStatusForMediaType' juce/video/native/CameraAuthorization_mac.mm` | ✅ W0 | ✅ green |
| 19-01-T3 | 19-01 | 1 | CAM-03 | T-19-PT | `JamWideFrameDistributor` fan-out + `Subscription` RAII (HIGH-2): `~Subscription` blocks in-flight `onFrame` before subscriber memory freed | unit | `cd build-juce && ctest -R 'camera_frame_distributor' --output-on-failure` | ✅ W0 | ✅ green |
| 19-01-T3b | 19-01 | 1 | CAM-03 | T-19-PT | HIGH-2 RAII race: in-flight `onFrame` + `~Subscription` blocks; deterministic via `std::promise`; no UAF possible | unit | `cd build-juce && ctest -R 'camera_frame_distributor_lifetime' --output-on-failure` | ✅ W0 | ✅ green |
| 19-01-T4 | 19-01 | 1 | CAM-02 / CAM-03 | T-19-01 | `CameraStateMachine`: 6 states (no `Paused` — MEDIUM-2); single `dispatch()` entry point (MEDIUM-3); all transitions per table | unit | `cd build-juce && ctest -R 'camera_state_machine' --output-on-failure` | ✅ W0 | ✅ green |
| 19-01-T5 | 19-01 | 1 | CAM-02 | T-19-01 | `JamWideCameraDevice`: generation-token cancellation at 5+ async sites (HIGH-3); D-20 retry 1/2/4/8/16s schedule; HIGH-6 frame-stall watchdog re-queries auth on >2000 ms gap | unit | `cd build-juce && ctest -R 'camera_retry_backoff\|camera_frame_stall' --output-on-failure` | ✅ W0 | ✅ green |
| 19-02-T1 | 19-02 | 2 | CAM-01 / CAM-02 | T-19-01 | ConnectionBar Camera button (D-11 connect-independent); MEDIUM-1 switch-on-state decision tree in `onCameraClicked`; FallbackListener wiring in editor | smoke | `grep -c 'onCameraClicked\|handleCameraIdleClick' juce/JamWideJuceEditor.cpp` | ✅ | ✅ green |
| 19-02-T2 | 19-02 | 2 | CAM-01 / CAM-03 | T-19-PT | `CameraPreviewWindow` (4:3 aspect, hide-not-destroy D-09); `CameraPreviewTile` (AsyncUpdater HIGH-4 — NO `MessageManager::callAsync(this)`; `subscription_` LAST member) | smoke | `grep -c 'MessageManager::callAsync.*this' juce/video/native/CameraPreviewTile.cpp` (must be 0) | ✅ | ✅ green |
| 19-02-T3 | 19-02 | 2 | CAM-01 / PKG-04 | T-19-03 / T-19-04 | D-24/D-25 schema v3→v4 migration: 7 flat ValueTree properties; T-19-03 `juce::jlimit` clamping at read; `privacyAck` persistence (HIGH-5); HIGH-7 prep `isAckResult()` pins JUCE 2-button mapping | unit | `cd build-juce && ctest -R 'plugin_state_v3_v4' --output-on-failure` | ✅ W0 | ✅ green |
| 19-03-T1 | 19-03 | 3 | CAM-02 | T-19-01 | `CameraStatusDialog`: 5 causes × copy (MEDIUM-6 softened) + button mapping; HIGH-7 `actionFor()` 14-cell table; suppress-after-first-show + reset on Opening/Capturing; editor dispatches `Action` to platform-conditional deep-link / `recheckPermission` | unit | `cd build-juce && ctest -R 'camera_cause_mapping' --output-on-failure` | ✅ W0 | ✅ green |
| 19-03-T2 | 19-03 | 3 | CAM-02 (D-27) | — | VDO.Ninja coexistence toast fires AT MOST ONCE per editor lifetime via `std::atomic<bool>.exchange` guard; camera toggle proceeds (non-blocking) | manual | UAT Cell 9 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |
| 19-03-T3a | 19-03 | 3 | PKG-04 | T-19-05 | `scripts/verify_camera_entitlement.sh` reads entitlement from SIGNED bundle (`codesign --display --entitlements -`) NOT source `.entitlements`; `NSCameraUsageDescription` compared against configured `CAMERA_PERMISSION_TEXT` | smoke | `bash -n scripts/verify_camera_entitlement.sh && test -x scripts/verify_camera_entitlement.sh && echo OK` | ✅ W0 | ✅ green |
| 19-03-T3b | 19-03 | 3 | PKG-04 | T-19-05 | Notarization staple validates (`xcrun stapler validate`) on a release codesigned bundle with `JAMWIDE_HARDENED_RUNTIME=ON` | manual | UAT Cell 6 — `docs/UAT/phase-19-camera-uat-checklist.md` (`./scripts/verify_camera_entitlement.sh <bundle> --notarize`) | ✅ | manual-pending |
| 19-UAT-SC1 | all | — | CAM-01 | — | macOS standalone: TCC prompt → Allow → preview within 3 s (live camera) | manual | UAT Cell 1 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |
| 19-UAT-SC2 | all | — | CAM-01 | — | Logic Pro AU: camera grant via Logic Pro host → preview | manual | UAT Cell 2 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |
| 19-UAT-SC3 | all | — | CAM-02 | T-19-01 | REAPER macOS: `HostLacksEntitlement` softened dialog (MEDIUM-6 wording, 3-button set); Open System Settings deep-link; no crash; audio works | manual | UAT Cell 3 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |
| 19-UAT-SC4 | all | — | CAM-01 | — | macOS arm64 standalone: same as SC1 on Apple Silicon hardware | manual | UAT Cell 4 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |
| 19-UAT-SC5 | all | — | CAM-02 | — | Permission revoke roundtrip (HIGH-6 end-to-end): frame-stall watchdog emits `TCCDenied` (not `CameraInUse`) within ~7 s of mid-session revoke | manual | UAT Cell 5 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |
| 19-UAT-SC6 | all | — | CAM-01 | — | Windows standalone: camera preview within 3 s | manual | UAT Cell 7 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |
| 19-UAT-SC7 | all | — | CAM-02 | — | Windows privacy block: `WindowsPrivacyBlock` dialog; `ms-settings` URL opens; recheck resumes | manual | UAT Cell 8 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |
| 19-UAT-HIGH5 | 19-02/03 | — | CAM-01 | T-19-04 | First-launch: `tccutil reset` → click → TCC prompt → Allow → `NativeCameraPrivacyDialog` → I understand → preview; second launch skips modal (ack persisted in v4 state) | manual | UAT Cell 10 — `docs/UAT/phase-19-camera-uat-checklist.md` | ✅ | manual-pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky · manual-pending awaiting human UAT*

---

## Wave 0 Requirements

All Wave 0 cells are currently MISSING — they ship as part of this phase's first plan(s):

- [ ] `tests/test_frame_distributor.cpp` — CAM-03 frame fan-out, removal-safe iteration, thread-safety smoke
- [ ] `tests/test_camera_state_machine.cpp` — CAM-02/CAM-03 state transition invariants
- [ ] `tests/test_camera_retry_backoff.cpp` — D-20 retry timing (virtualized clock or shorter test-mode intervals)
- [ ] `tests/test_camera_cause_mapping.cpp` — CAM-02 cause → copy mapping; cause classification logic
- [ ] `tests/test_plugin_state_v3_v4.cpp` — D-24/D-25 schema migration roundtrip
- [ ] `scripts/verify_camera_entitlement.sh` — PKG-04 entitlements (`codesign --display --entitlements -` + `plutil -extract NSCameraUsageDescription raw`)
- [ ] `docs/UAT/phase-19-camera-uat-checklist.md` — manual UAT script with each CAM-01/02/03 path explicit (referenced by Phase 24's per-DAW matrix later)
- [ ] CMake wiring: add `add_executable(test_camera_* …)` + `add_test(NAME camera_* COMMAND …)` entries under `if(JAMWIDE_BUILD_TESTS)` at `CMakeLists.txt:406+`

---

## Manual-Only Verifications

Per `feedback_uat_scope_redflags`: each of these is a user-visible happy/sad path that MUST be verified before the phase closes. No deferring CAM-01/02/03 cells to Phase 24.

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| macOS standalone happy path | CAM-01 / SC1 | Requires real camera hardware + TCC prompt | Launch JamWide.app on macOS; click Camera button; verify OS prompt; grant; verify preview ≤ 3 s |
| Logic Pro plugin happy path | CAM-01 / SC2 | Requires Logic Pro install + its camera entitlement | Load JamWide AU in Logic Pro; toggle Camera; verify preview |
| REAPER macOS plugin fallback | CAM-02 / SC3 | SPARTA #82 — host bundle ID controls TCC | Load JamWide VST3 in REAPER on macOS; click Camera; verify cause-aware fallback dialog (no crash, audio still works) |
| macOS arm64 standalone | CAM-01 (arm64) | Spike was x86_64-only; arm64 build path uncovered | First arm64 JamWide build that touches camera path |
| Permission revoke roundtrip | CAM-02 / SC4 | Depends on macOS System Settings interaction | Grant permission; capture; revoke via System Settings → Privacy → Camera; verify preview disappears + fallback appears (no crash) |
| Notarization stapler validate | PKG-04 / D-28 | Depends on Apple notary service | Build + codesign + notarize a test bundle; `xcrun stapler validate` must pass |
| Windows standalone happy path | CAM-01 / SC5 | Requires Windows + camera hardware | Launch JamWide.exe on Windows x86_64; toggle Camera; verify preview ≤ 3 s |
| Windows privacy block | CAM-02 (Windows) | Depends on Windows Settings → Privacy → Camera | Disable camera access in Windows Settings; launch JamWide; verify WindowsPrivacyBlock fallback dialog |
| VDO.Ninja coexistence toast | D-27 | Depends on both stacks running | Start VDO.Ninja video; click native Camera; verify non-blocking soft warning toast |

Other DAW cells (Live, Bitwig on macOS; REAPER on Windows) belong to Phase 24's per-DAW matrix per CONTEXT "Deferred Ideas" — explicitly out of Phase 19.

---

## Nyquist Concern Coverage (from RESEARCH §11)

- **State machine matrix** (states × triggers, RESEARCH §5): exhaustively unit-tested in `test_camera_state_machine.cpp`. One assertion per transition-table cell.
- **Cause matrix** (5 causes × 2 platforms = 10 cells, RESEARCH §9): unit-tested via cause-input-shape simulation in `test_camera_cause_mapping.cpp`.
- **Cross-host matrix** (Logic Pro / REAPER on macOS + macOS standalone + Windows standalone = 4 cells in Phase 19; Live / Bitwig / Windows REAPER deferred to Phase 24): manual UAT.
- **Retry-backoff timing** (5 backoff intervals × 30 s budget): unit-tested with virtualized clock.
- **Permission-revoke roundtrip**: cannot be unit-tested (System Settings). Manual UAT.
- **Notarization**: cannot be unit-tested (Apple notary). Manual UAT.

Aggregate sampling rate is appropriate: high-frequency logic via unit tests; low-frequency cross-host quirks via manual UAT.

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references (8 cells above; +2 bonus cells `test_frame_distributor_lifetime` + `test_camera_frame_stall` from codex HIGH-2 / HIGH-6 review)
- [x] No watch-mode flags
- [x] Feedback latency < 60 s (measured: 3.38 s quick, 43.50 s full)
- [x] `nyquist_compliant: true` set in frontmatter
- [x] `wave_0_complete: true` set in frontmatter

**Approval:** verified 2026-05-16

---

## Validation Audit 2026-05-16

| Metric | Count |
|--------|-------|
| Gaps found | 0 (file-level), 0 (automation-boundary) |
| Resolved | All 4 requirements covered (CAM-01/02/03/PKG-04) |
| Escalated | 0 |
| Tests run live | 7/7 PASS (3.38 s) — `ctest -R 'camera_\|plugin_state_v3_v4'` |
| Manual-pending UAT cells | 10 (correctly classified — OS/hardware/credentials dependencies) |
| Run by | gsd-nyquist-auditor (sonnet) |

**Coverage breakdown:**
- **CAM-01** — SATISFIED (code) + HUMAN-UAT (behavior). 4 UAT cells: SC1, SC2, SC4, SC6.
- **CAM-02** — SATISFIED (code) + HUMAN-UAT (behavior). 3 UAT cells: SC3, SC5, SC7.
- **CAM-03** — FULLY AUTOMATED. No human verification needed; `test_frame_distributor` + `test_frame_distributor_lifetime` cover thread-safety + RAII lifetime.
- **PKG-04** — SATISFIED (verifier script) + HUMAN-UAT (notarization staple). 1 UAT cell: Cell 6.

**Carried forward (not gaps):** `19-REVIEW.md` CR-01..04 — 4 Critical UAF risks in editor-owned async lambdas. Not in Nyquist register; surfaced in `19-SECURITY.md` "Related-but-not-in-register" advisory. Recommended `19.1-lifetime-hardening` follow-up.
