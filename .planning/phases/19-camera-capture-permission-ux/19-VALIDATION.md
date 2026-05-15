---
phase: 19
slug: camera-capture-permission-ux
status: planned
nyquist_compliant: true
wave_0_complete: true
created: 2026-05-16
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

> Planner fills this after task IDs are assigned. Source the rows from `19-RESEARCH.md` §11 "Phase Requirements → Test Map".

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 19-XX-XX | XX | X | CAM-XX / PKG-04 | — / T-19-XX | {expected behavior} | unit/UAT/smoke | `{command}` | ✅ / ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

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

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (8 cells above)
- [ ] No watch-mode flags
- [ ] Feedback latency < 60 s
- [ ] `nyquist_compliant: true` set in frontmatter
- [ ] `wave_0_complete: true` set in frontmatter

**Approval:** pending
