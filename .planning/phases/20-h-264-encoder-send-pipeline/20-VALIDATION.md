---
phase: 20
slug: h-264-encoder-send-pipeline
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-16
---

# Phase 20 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | JUCE UnitTests + CMake CTest (existing JamWide harness) |
| **Config file** | `tests/CMakeLists.txt`; built via `./scripts/build.sh --tests` |
| **Quick run command** | `cd build-juce && ctest -R "Phase20|rawdata_send|video_encoder|video_state" --output-on-failure` |
| **Full suite command** | `cd build-juce && ctest --output-on-failure` |
| **Estimated runtime** | ~30 seconds for Phase 20 subset; ~3 minutes for full suite |

---

## Sampling Rate

- **After every task commit:** Run quick command (Phase 20 subset)
- **After every plan wave:** Run full suite command
- **Before `/gsd:verify-work`:** Full suite must be green; TSan dual-scope verification (per Phase 15.1 D-07) on the broadcast happy path
- **Max feedback latency:** 30 seconds for the Phase 20 subset

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 20-00-XX | 20-00 | 0 | substrate revision | T-20-00 | mutex-correct multi-producer enqueue; pop-one-unlock-Send-relock drain | unit | `ctest -R rawdata_send` | ❌ W0 | ⬜ pending |
| 20-01-XX | 20-01 | 1 | COD-01 | T-20-01 | openh264 encoder produces NAL + SPS/PPS at spike baseline | unit | `ctest -R video_encoder` | ❌ W0 | ⬜ pending |
| 20-02-XX | 20-02 | 2 | WIRE-01, COD-02 | T-20-02 | END/BEGIN/marker/SPS-PPS/frame wire order under whole-block m_video_cs | unit | `ctest -R video_state_machine` | ❌ W0 | ⬜ pending |
| 20-03-XX | 20-03 | 3 | WIRE-03 | T-20-03 | 5-min 2-peer broadcast: no audio glitches, drop counters zero, TSan clean | manual + perf | `tests/uat/phase-20-broadcast-uat.sh` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_rawdata_send.cpp` (rewritten in-place by Plan 20-00 Task 2) — multi-producer + drain interleave + BEGIN/marker/SPS/frame ordering (replaces SPSC overflow-counter tests; per D-19)
- [ ] `tests/test_video_encoder.cpp` — openh264 encoder bring-up + SPS/PPS publish + IDR-sync counter (per D-15)
- [ ] `tests/test_video_state_machine.cpp` — `on_new_interval` END/BEGIN/marker/SPS-PPS sequence under simulated whole-block `m_video_cs` (per D-08)
- [ ] `tests/uat/phase-20-broadcast-uat.sh` — UAT harness scaffold for the 5-minute 2-peer broadcast acceptance gate

*Cross-reference: NinjamZap video-sync scenarios `02_video_one_interval_early.cpp`, `03_late_join.cpp`, `13_sps_pps_mid_stream.cpp`, `20_drop_resync_recovery.cpp`, `22_audio_then_video.cpp`, `25_no_initial_spspps.cpp` are ported in Phase 21 (per BETA-04); Phase 20 verifies send-side correctness at the unit level and defers cross-peer interop to Phase 24.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Wire-format bit-for-bit compatibility with NinjamZap mobile | WIRE-01 | Requires a NinjamZap iOS/Android peer + reference ninjamzap-server | Connect JamWide standalone to `video.ninjamzap.com:2049`, broadcast video, verify on a NinjamZap mobile peer that JamWide stream renders correctly |
| 5-minute populated-server broadcast at each preset (Low/Medium/High) | WIRE-03, COD-01 | Wall-clock duration + audio-thread budget measurement requires a live server with ≥2 peers | Two JamWide standalone instances + `video.ninjamzap.com:2049`; run each preset for 5 min; verify: zero audio glitches (subjective), `m_encoder_input_drops == 0`, `m_video_cs` contention budget within audio-thread headroom (Plan 20-03 acceptance threshold), TSan clean |
| Audio-thread budget under populated server + HD broadcast | WIRE-03 | Requires real network + CPU under load | Worst-case `on_new_interval` duration including the three mutex acquisitions + Send calls at HD × 6-peer populated server (per CONTEXT.md Deferred Ideas / Plan 20-03 acceptance criterion) |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (test files marked `❌ W0` above)
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s for Phase 20 subset
- [ ] TSan dual-scope verification (per Phase 15.1 D-07) is part of Plan 20-03 acceptance
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
