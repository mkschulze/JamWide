---
phase: quick/260502-rcm-fix-orphan-mirror-fields
verified: 2026-05-02T00:00:00Z
status: passed
score: 10/10 must-haves verified
re_verification:
  previous_status: none
  previous_score: n/a
  gaps_closed: []
  gaps_remaining: []
  regressions: []
overrides_applied: 0
gaps: []
human_verification:
  - test: "Connect to a NINJAM server with multiple active peers (3+, ideally one in instamode)"
    expected: "All remote channels remain audible continuously across NINJAM intervals; no cutoffs after a few seconds; user-set per-peer output channel routing takes effect; instamode peer audio is continuous."
    why_human: "Symptom-to-fix link is a runtime audio behavior across a real NINJAM session — cannot be observed without running the JamWide host against a server with at least one instamode peer."
  - test: "While a UAT session is active, attach lldb (or call from debug UI) and read GetChannelInfoPublishCount(slot, ch) and GetChannelInfoApplyCount(slot, ch) for an active remote peer; mutate canonical state (mute/unmute, change vol/pan, change output channel)."
    expected: "Both counters increment per mutation. publish_count > 0 with apply_count == 0 indicates audio thread isn't draining (different bug). Both at 0 after a clear mutation indicates a missed publisher site (regression). Both > 0 means data flow is restored."
    why_human: "Falsifiable readout requires a live process; the diagnostic counters were added precisely so a single UAT cycle confirms or falsifies the symptom-to-defect link."
---

# Quick Task: 260502-rcm-fix-orphan-mirror-fields — Verification Report

**Plan:** `.planning/quick/260502-rcm-fix-orphan-mirror-fields/PLAN.md`
**Commits:** `5b745ab` (T1) → `d838cc2` (T2) → `ac04e17` (T3)
**Build:** 295 (target was ≥ 292; auto-incremented past it as documented in `project_build_number_automation.md`)
**Branch:** main
**Verified:** 2026-05-02

## Goal Achievement

**Stated goal (from PLAN):** Restore the canonical → mirror data flow for remote per-channel attributes (flags / volume / pan / out_chan_index) so the audio thread mixes remote channels with the correct state, and add diagnostic counters to make the symptom-to-defect link falsifiable in one UAT.

The diagnosed gap was: four mirror fields were read by the audio thread on every block but written nowhere except the PeerRemovedUpdate reset path. After this work:

- `PeerChannelInfoUpdate` exists in the SPSC variant and carries all four orphan fields plus `slot` + `channel`.
- Two publishers fire (one when `mpb_server_user_info_change_notify` adds / mutates a channel, one when the UI calls `SetUserChannelState`).
- The apply visitor copies all four fields onto `m_remoteuser_mirror[slot].chans[channel]` with bounds checks.
- Two relaxed-atomic counters + public accessors expose publish/apply counts for runtime falsification.

## Goal-Backward Checks (10/10 PASS)

| #  | Check | Status | Evidence |
|----|-------|--------|----------|
| 1  | PeerChannelInfoUpdate carries the four orphan fields + FINAL marker handled | PASS | `src/threading/spsc_payloads.h:136-143` defines the struct with `slot`, `channel`, `flags` (unsigned int), `volume` (float), `pan` (float), `out_chan_index` (int). Header lines 5-11 explicitly mark M-9 supersession ("Originally FINALIZED at 15.1-04 ... SUPERSEDED 2026-05-02"). Deviation block at lines 114-135 cites `.planning/debug/remote-channels-cutoff.md`. `static_assert(std::is_trivially_copyable_v<RemoteUserUpdate>)` at line 155 still active. |
| 2  | Both publishers wired with capture-inside-lock + try_push-after-Leave | PASS | Publisher A: `njclient.cpp:1881` `m_users_cs.Leave()`; insert lands at lines 1901-1924, between `if (publish_added)` (line 1892, closes 1900) and `if (publish_mask_change)` (line 1925). Publisher B: `njclient.cpp:4459-4478`, post-lock-block (line 4458 closes lock at line 4458 `}`), before `if (publish_mask)` at 4481. Pub-time values captured inside lock at 4454-4457 and 1818-1825 region. Both use try_push + overflow log + counter bump on success only. (Line numbers drifted from plan's "1880/1881" due to insertions; structural ordering is preserved.) |
| 3  | Apply visitor branch added with bounds + four-field copy + apply counter bump | PASS | `njclient.cpp:3371-3389` — `else if constexpr (std::is_same_v<T, jamwide::PeerChannelInfoUpdate>)` branch with bounds checks against `MAX_PEERS` and `MAX_USER_CHANNELS` (matches PeerCodecSwapUpdate shape at lines 3390-3394), four field copies onto `m_remoteuser_mirror[u.slot].chans[u.channel]`, relaxed `fetch_add` on `m_chinfo_applies_observed`. |
| 4  | Diagnostic counters declared and accessible | PASS | `src/core/njclient.h:750-751` declares both `std::atomic<uint64_t> m_chinfo_publishes_observed[MAX_PEERS][MAX_USER_CHANNELS]{}` and `m_chinfo_applies_observed[...][...]{}`. Public accessors declared at lines 437-438 (both `noexcept`); definitions at `njclient.cpp:4554-4566` use bounds check + relaxed load. Brace-init `{}` zeroes the arrays at construction (no constructor body init needed). |
| 5  | Tests verify the fix (4 new test cases, all green Release + TSan) | PASS | `tests/test_remote_user_mirror.cpp:497-680` — four new tests: `test_chinfo_roundtrip` (line 499), `test_chinfo_concurrent` (line 527), `test_chinfo_bounds_check` (line 597), `test_chinfo_orders_before_nextds` (line 646). Local apply visitor extended at line 163. `static_assert(std::is_trivially_copyable_v<jamwide::PeerChannelInfoUpdate>)` at line 696. All four registered in main at lines 733-736. |
| 6  | Both builds GREEN with 12/12 + zero TSan warnings | PASS | `/tmp/rcm-test-release.log` and `/tmp/rcm-test-tsan.log` both end with "12/12 tests passed" and contain all four new test names with "PASSED". `grep -E "WARNING: ThreadSanitizer\|FAILED:\|FAIL:\|assertion failed\|SEGFAULT"` against both logs returns no hits (exit 1, no matches). Re-ran `build-test/test_remote_user_mirror` live — 12/12 PASSED reproduced. |
| 7  | Specialist review captured in commit body | PASS (with documented deviation) | `git show ac04e17` body contains a "Real-time-audio safety review" section noting: scalar-only apply visitor (no alloc/lock/I/O), capture-inside-lock + try_push-outside-lock pattern, relaxed-atomic counter bumps, ordering invariant, Codex M-9 FINAL deviation justified — ACCEPTED with zero findings. The deviation: executor performed a manual audit applying the realtime-audio-reviewer.md checklist rather than dispatching the agent (commit body explicitly states "agent dispatch unavailable from executor"). Constraint #1 ("do not commit while a HIGH/CRITICAL is outstanding") was satisfied via zero findings. Acceptable per prompt instructions. |
| 8  | No scope creep — three Future Work items untouched | PASS | `git diff 5874994..ac04e17 --stat` shows only the planned 5 files modified (spsc_payloads.h +43, njclient.h +15, njclient.cpp +119, test_remote_user_mirror.cpp +209, build_number.h +1). Diff hunk headers (`@@`) show all njclient.cpp inserts in regions 1709, 1803, 1878, 3324, 4294, 4373, 4446 — none overlap (i) silence-marker double-free at the "both slots full" branch (now at 3361-3369 due to insertion drift), (ii) generation-gate spinwait at 1906-1916-equivalent region (line 1936+), or (iii) sessionmode publisher filter region (1995-equivalent). No spinwait/sessionmode/generation_gate keyword hits in the diff. |
| 9  | Build number progression sane | PASS | `src/build_number.h:2` reads `#define JAMWIDE_BUILD_NUMBER 295`. ≥ 292 plan target. The drift past 292 is expected per memory `project_build_number_automation.md` (auto-increment cmake hook bumps over the cycle). |
| 10 | Ordering invariant in code matches plan | PASS | `njclient.cpp:1892` opens `if (publish_added)` block, closes at 1900. Lines 1901-1909 comment explicitly encodes BOTH ordering constraints: "AFTER PeerAddedUpdate (mirror slot is active=true before per-channel attrs land) AND BEFORE PeerChannelMaskUpdate (per-channel attributes precede mask-driven present/mute/solo apply)". Insert is at 1910 (publish_chinfo); next opener at 1925 is `publish_mask_change`. Independently verified via grep — no PeerNextDsUpdate publishes appear inside either user-info-change-notify or SetUserChannelState (those publish from start_decode callsites; SPSC FIFO covers cross-publisher ordering). |

## Anti-Patterns Found

None blocking. The "incoming_ds==nullptr" path at `njclient.cpp:3361-3369` (Future Work item i) is a known defect inherited from prior commits — explicitly recorded as out-of-scope in PLAN and intentionally untouched.

## Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All previously-existing remote-user-mirror tests still pass at HEAD | `build-test/test_remote_user_mirror` | "12/12 tests passed" with all 8 prior tests + 4 new tests reported PASSED | PASS |
| All four new test cases by name appear and pass | `grep -E "PeerChannelInfoUpdate" /tmp/rcm-test-release.log` | All four names present, each with "... PASSED" | PASS |
| TSan run produced zero warnings | `grep "WARNING: ThreadSanitizer" /tmp/rcm-test-tsan.log` | no matches | PASS |
| Public accessor symbols visible | `grep "GetChannelInfoPublishCount\|GetChannelInfoApplyCount" src/core/njclient.{h,cpp}` | 6 hits (decl + defn for both, plus comment refs) | PASS |

## Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| RCM-FIX-01 | PLAN.md | Close 15.1-07a RemoteUserMirror orphan-fields gap | SATISFIED | All 10 goal-backward checks pass; data flow restored end-to-end (publish → SPSC → drain → apply → mirror); TSan-clean. |

## Goal-Backward Summary

The implementation actually fixes the diagnosed defect: the four orphan mirror fields (`flags`, `volume`, `pan`, `out_chan_index`) now have a complete write path from canonical state to audio-thread mirror via the new `PeerChannelInfoUpdate` SPSC payload, two run-thread publishers, and one apply-visitor branch — closing the architectural gap that left them stuck at `PeerRemovedUpdate` reset defaults.

## Confidence the Symptom Is Fixed

**HIGH** — rationale:
- The root-cause diagnosis (`.planning/debug/remote-channels-cutoff.md`) is precise and supported by direct evidence (no other writers found via exhaustive grep, beta-18 baseline confirms the data flow that was lost).
- The code change is minimal, surgical, and matches the exact shape of the working `PeerCodecSwapUpdate` and `PeerChannelMaskUpdate` patterns already in production.
- TSan-green under a 1000-cycle concurrent producer/consumer test gives strong confidence there are no torn-field or race conditions at runtime.
- The diagnostic counters provide a single-cycle UAT readout that will falsify or confirm the fix without ambiguity.

The only residual risk is the medium-confidence note in the debug doc: the four orphan fields ARE a confirmed bug, but whether they are the ONLY mechanism behind the user-reported "cut off after a few seconds" symptom is something only UAT can finalize. The two recorded Future Work items (silence-marker double-free, sessionmode-flags filter mismatch) are plausible additional contributors and remain unaddressed.

## UAT Next Step

Tag a beta build of build 295, install the VST3 in your DAW, connect to a NINJAM server with at least one instamode peer (`flags & 2`), let the session run 60+ seconds across multiple NINJAM intervals, and verify (a) all peer audio remains continuous, (b) per-peer output-channel routing takes effect, (c) `(NJClient*)x->GetChannelInfoApplyCount(slot, ch)` increments in lldb on every mute/vol/pan/out_chan change.

---

_Verified: 2026-05-02_
_Verifier: Claude (gsd-verifier)_
