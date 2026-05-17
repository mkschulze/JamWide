---
phase: 21
slug: h-264-decoder-receive-pipeline
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-17
---

# Phase 21 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from `.planning/phases/21-h-264-decoder-receive-pipeline/21-RESEARCH.md` § "Validation Architecture".

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest + plain C++ test executables (existing pattern from Phase 14.3-03 `test_video_fourcc.cpp` and Phase 20 `test_video_state_machine.cpp` / `test_video_encoder.cpp`) |
| **Config file** | `CMakeLists.txt` `if(JAMWIDE_BUILD_TESTS)` block (existing pattern) |
| **Quick run command** | `./scripts/build.sh --tests test_video_recv_state test_video_decoder test_remote_frame_distributor` |
| **Full suite command** | `cd build-juce && ctest --output-on-failure` |
| **Estimated runtime** | ~60 seconds (matches Phase 20 baseline) |

---

## Sampling Rate

- **After every task commit:** Run the task-relevant test only — `./scripts/build.sh --tests <test_name>` for the specific subsystem being committed. Plan 21-01 commits run `test_video_recv_state`, Plan 21-02 commits run `test_video_decoder`, Plan 21-03 commits run `test_remote_frame_distributor`.
- **After every plan wave:** Run full suite — `cd build-juce && ctest --output-on-failure`.
- **Before `/gsd:verify-work`:** Full suite must be green AND Plan 21-03 UAT cells 1-4 PASS.
- **Max feedback latency:** ~30 seconds per-task (subsystem test only), ~60 seconds per-wave (full suite).

---

## Per-Task Verification Map

| Task ID (placeholder) | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---|---|---|---|---|---|---|---|---|---|
| 21-01-01 | 01 | 1 | WIRE-02 | — | DS-match defers to pending (verbatim port of upstream :3084-3219) | unit | `ctest -R video_recv_state -V` (sub-test `ds_match_defers_to_pending`) | ❌ W0 (`tests/test_video_recv_state.cpp`) | ⬜ pending |
| 21-01-02 | 01 | 1 | WIRE-02 | — | PREV-match plays immediately (no defer) | unit | `ctest -R video_recv_state -V` (sub-test `prev_match_plays_immediately`) | ❌ W0 | ⬜ pending |
| 21-01-03 | 01 | 1 | WIRE-02 | — | No-match HOLDs and drops at `kHoldCapDrop=4` | unit | `ctest -R video_recv_state -V` (sub-test `no_match_holds_then_drops_at_4`) | ❌ W0 | ⬜ pending |
| 21-01-04 | 01 | 1 | WIRE-02 | — | STAGE-1 promote `pending → playing` | unit | `ctest -R video_recv_state -V` (sub-test `pending_promotes_to_playing_on_next_swap`) | ❌ W0 | ⬜ pending |
| 21-01-05 | 01 | 1 | WIRE-02 | — | Mid-download startPlaying (accumulating → next during WRITE) | unit | `ctest -R video_recv_state -V` (sub-test `mid_download_start_playing`) | ❌ W0 | ⬜ pending |
| 21-01-06 | 01 | 1 | WIRE-02 | — | User-leave clears `prev_ds_guid` + `hold_count` + `synced` | unit | `ctest -R video_recv_state -V` (sub-test `user_leave_resets_video_sync_state`) | ❌ W0 | ⬜ pending |
| 21-01-07 | 01 | 1 | WIRE-02 | — | Multi-write reassembly via 4B BE length prefix | unit | `ctest -R video_recv_state -V` (sub-test `split_frame_reassembles_via_length_prefix`) | ❌ W0 | ⬜ pending |
| 21-01-08 | 01 | 1 | WIRE-02 | — | 24B marker parsing extracts `audio_ch0_guid` + `sender_seq` | unit | `ctest -R video_recv_state -V` (sub-test `marker_parse_extracts_guid_and_seq`) | ❌ W0 | ⬜ pending |
| 21-01-09 | 01 | 1 | WIRE-02 | — | Burst BEGIN discards stale `next` | unit | `ctest -R video_recv_state -V` (sub-test `burst_begin_discards_stale_next`) | ❌ W0 | ⬜ pending |
| 21-02-01 | 02 | 2 | COD-03 | — | One independent decoder per peer; YUV→BGRA via sws_scale; juce::Image delivery | unit | `ctest -R video_decoder` | ❌ W0 (`tests/test_video_decoder.cpp`) | ⬜ pending |
| 21-02-02 | 02 | 2 | COD-03 | — | First-frame visible after IDR arrival | unit | `ctest -R video_decoder -V` (sub-test `first_frame_emits`) | ❌ W0 | ⬜ pending |
| 21-02-03 | 02 | 2 | COD-03 | — | Decode-error recovery without thread teardown (corrupt NAL recovers on next IDR) | unit | `ctest -R video_decoder -V` (sub-test `corrupt_nal_recovers_on_next_idr`) | ❌ W0 | ⬜ pending |
| 21-02-04 | 02 | 2 | COD-03 | — | Mid-stream SPS/PPS update (peer preset change) | unit | `ctest -R video_decoder -V` (sub-test `sps_pps_mid_stream_reconfig`) | ❌ W0 | ⬜ pending |
| 21-02-05 | 02 | 2 | COD-03 | — | Resolution change handling (SwsContext recreate) | unit | `ctest -R video_decoder -V` (sub-test `source_resolution_change_no_crash`) | ❌ W0 | ⬜ pending |
| 21-03-01 | 03 | 3 | WIRE-02+COD-03 | — | Two listeners on same peer both receive frame signals (DISP-03 prep) | unit | `ctest -R remote_frame_distributor -V` (sub-test `two_listeners_same_peer_both_called`) | ❌ W0 (`tests/test_remote_frame_distributor.cpp`) | ⬜ pending |
| 21-03-02 | 03 | 3 | WIRE-02+COD-03 | — | `~Subscription` blocks in-flight callback (Phase 19 HIGH-2 mirror) | unit | `ctest -R remote_frame_distributor -V` (sub-test `subscription_dtor_blocks_in_flight`) | ❌ W0 | ⬜ pending |
| 21-03-03 | 03 | 3 | WIRE-02+COD-03 | — | Per-peer isolation: one peer's decode error doesn't affect others (3-peer parallel) | integration | `ctest -R video_sync_e2e -V` (sub-test `three_peers_isolated_decode_errors`) | ❌ W0 (`tests/test_video_sync_e2e.cpp`) | ⬜ pending |
| 21-03-04 | 03 | 3 | success-1 | — | Success criterion 1: video appears at same wall-clock moment as audio (5+ min continuous) | UAT (manual) | `tests/uat/phase-21-receive-uat-report.md` Cell 1 | manual (Plan 21-03 UAT) | ⬜ pending |
| 21-03-05 | 03 | 3 | success-2 | — | Success criterion 2: mid-session joiner sees video ≤2 interval boundaries | UAT (manual) | Cell 2 | manual | ⬜ pending |
| 21-03-06 | 03 | 3 | success-3 | — | Success criterion 3: peer audio stops → freeze after 4 holds → clean rejoin | UAT (manual) | Cell 3 | manual | ⬜ pending |
| 21-03-07 | 03 | 3 | success-4 | — | Success criterion 4: 3+ peers simultaneously, per-peer isolation, ≥5 min | UAT (manual) | Cell 4 | manual | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

*Note: Task IDs above are PLACEHOLDERS for the validation matrix. The planner will assign actual task numbers during plan generation; the Per-Task Verification Map will be re-linked after PLAN.md files land.*

---

## Wave 0 Requirements

- [ ] `tests/test_video_recv_state.cpp` — covers WIRE-02 sub-scenarios via `DispatchTestServerDownloadIntervalBegin/Write/End` test surface (Phase 14.3-03 pattern). Ports upstream sync-test scenarios 02, 03, 20, 22.
- [ ] `tests/test_video_decoder.cpp` — covers COD-03 sub-scenarios via direct `Openh264Decoder` API exercise with synthetic Annex-B fixtures (small H.264 IDR + SPS/PPS from a vendored test asset under `tests/fixtures/`). Ports upstream scenarios 13, 25.
- [ ] `tests/test_remote_frame_distributor.cpp` — covers Subscription RAII + multi-listener + atomic-generation semantics. New Phase 21 component, no upstream reference; mirrors Phase 19's `test_frame_distributor_lifetime.cpp` symmetrically.
- [ ] `tests/test_video_sync_e2e.cpp` — ties Plan 21-01 + 21-02 + 21-03 together in-process via the existing `DispatchTestServer*` substrate. Demonstrates success criterion 1 (wall-clock alignment) via instrumented swap+paint cycle counter.
- [ ] `CMakeLists.txt` — `add_executable` + `add_test` entries for each new test target, mirroring the existing `test_video_fourcc` block. Gated by `if(JAMWIDE_BUILD_TESTS)`.
- [ ] `tests/fixtures/sps_pps_baseline_320x240.bin` + `tests/fixtures/idr_baseline_320x240.bin` — small H.264 test assets generated from a known-good openh264 encoder run (one-time fixture commit; ≤5 KB total).
- [ ] `JAMWIDE_BUILD_TESTS`-gated NJClient helpers: `DispatchTestVideoRecvBegin/Write/End`, `GetVideoStreamForTest`, `DrainDecoderInputQForTest`, `GetPeerSinkForTest` — Phase 14.3-03 test-helper pattern extended for Phase 21.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|---|---|---|---|
| Wall-clock audio-video alignment over 5+ min continuous playback | success-1 | Subjective listening-and-watching gate; mechanical instrumentation can't replace human ear/eye correlation | UAT Cell 1: connect two JamWide instances to `video.ninjamzap.com:2049`; one broadcasts video for 5 minutes; receiver-side user watches/listens; counter readout via lldb at T=5:00 must show zero `prev_ds_guid` mismatches with `hold_count` resets |
| Mid-session join sees video within ≤2 intervals | success-2 | Requires real network + real broadcasting peer + clock-coordinated join | UAT Cell 2: Peer A broadcasts steadily; Peer B joins room mid-stream; B's tile transitions from `'video starting…'` → first decoded frame within ≤2 NINJAM interval ticks (observe interval count via lldb) |
| Hold + freeze + recovery on peer audio stop | success-3 | Requires real audio-stop scenario (sender mute); receiver tile UX visual confirmation | UAT Cell 3: Peer A broadcasts video + audio; Peer A toggles audio Ch1 mute (audio stream stops, video continues); Receiver tile shows last-frame freeze; after 2 holds, `'syncing…'` overlay appears; after 4 holds, `drop_resync_count++`; Peer A unmutes audio; receiver tile rejoins cleanly within 1 interval |
| 3+ peer per-peer isolation, no audio glitches | success-4 | Requires 3 physical machines + populated session; per-peer audio quality is subjective | UAT Cell 4: 3 standalone JamWide instances broadcasting+receiving each other on `video.ninjamzap.com:2049` for ≥5 minutes; no audio glitches reported by any of 3 listeners; per-peer `decode_error_count`/`drop_resync_count`/`slot_drop_count` counters all 0 |

---

## Proof-by-Construction Coverage Summary

For each ROADMAP success criterion, Plan 21-03's audit confirms the listed automated test ASSERTS the mechanism that GUARANTEES the criterion (not just one that detects violations):

- **Success criterion 1:** 1-swap defer through `pending` is the architectural guarantee. `test_video_recv_state` sub-test `ds_match_defers_to_pending` + `pending_promotes_to_playing_on_next_swap` cover the mechanism end-to-end. UAT Cell 1 is the subjective check.
- **Success criterion 2:** SPS/PPS as chunk #2 of every interval (per Phase 20 D-13 + RESEARCH §3.6) is the architectural guarantee. `test_video_decoder` sub-test `first_frame_emits` covers the mechanism. UAT Cell 2 is the integration check.
- **Success criterion 3:** `kHoldCapDrop=4` with `last_played_audio_guid` reset in `STAGE 1` is the architectural guarantee. `test_video_recv_state` sub-test `no_match_holds_then_drops_at_4` + `user_leave_resets_video_sync_state` cover. UAT Cell 3 is the UX visual check.
- **Success criterion 4:** Per-peer threading + per-peer SpscRing + per-peer codec context (no shared state) is the architectural guarantee. `test_video_sync_e2e` sub-test `three_peers_isolated_decode_errors` covers per-peer isolation. UAT Cell 4 is the 3-peer integration check.

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify OR Wave 0 dependencies recorded
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (5 new test files + fixtures + CMakeLists.txt)
- [ ] No watch-mode flags (all commands are one-shot)
- [ ] Feedback latency < 60s per wave
- [ ] `nyquist_compliant: true` set in frontmatter (after Wave 0 completes and Per-Task Verification Map is re-linked to real task IDs)

**Approval:** pending — planner to re-link Per-Task Verification Map to actual task IDs after PLAN.md files land
