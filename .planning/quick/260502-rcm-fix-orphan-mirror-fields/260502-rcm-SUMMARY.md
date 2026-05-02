---
status: complete
---

# Quick Task 260502-rcm: Summary

**Task:** Restore canonical→mirror flow for remote per-channel attributes (`flags` / `volume` / `pan` / `out_chan_index`); add diagnostic counters for falsifiable UAT
**Date:** 2026-05-02
**Status:** Complete (UAT pending in JamWide host)
**Build:** 295
**Confidence symptom is fixed:** HIGH

## Why this work happened

User reported: *"multiple issues with remote channels — some channels get cut off after a few seconds, others not"*. `/gsd-debug` diagnosed the root cause at `.planning/debug/remote-channels-cutoff.md`: the 15.1-07a `RemoteUserMirror` migration moved audio-thread reads of four per-channel fields onto `m_remoteuser_mirror[s].chans[ch].*` but **shipped no publisher path** for them. Audio thread mixed every remote channel as if `flags=0, volume=1.0, pan=0.0, out_chan_index=0` — instamode peers (`flags&2`) starved after 1-2 NINJAM intervals; per-peer output routing was silently bypassed.

Same shape as three already-shipped 15.1-07a post-UAT fixes (`b9899a0`, `e827453`, `e151dd8`) — third instance of the legacy-invariant audit pattern documented in memory `feedback_legacy_invariant_audit.md`.

## What Changed

### SPSC schema
- New `PeerChannelInfoUpdate { int slot; int channel; unsigned int flags; float volume; float pan; int out_chan_index; }` variant case in `RemoteUserUpdate`.
- Codex M-9 FINAL marker superseded with explicit deviation block citing the debug session.
- POD-only contract preserved (`std::is_trivially_copyable_v` static_assert covers the new variant).

### Publishers (run thread)
- **A — user-info-change-notify:** `njclient.cpp:1901-1924`, ordered AFTER `publish_added` block (1892-1900) and BEFORE `publish_mask_change` (1925). Comment encodes both the legacy `PeerAddedUpdate-first` invariant and the new `ChannelInfo-before-NextDs` invariant.
- **B — `SetUserChannelState`:** `njclient.cpp:4459-4478`, post-`m_users_cs.Leave()`, before existing `publish_mask` at 4481.
- Both use `try_push` + overflow counter (matching existing `PeerChannelMaskUpdate` pattern); both bump `m_chinfo_publishes_observed[slot][channel]` (relaxed atomic).

### Apply visitor (audio thread, drainRemoteUserUpdates)
- New branch at `njclient.cpp:3371-3389`, mirrors `PeerCodecSwapUpdate` shape: bounds check `u.slot < MAX_PEERS && u.channel < MAX_USER_CHANNELS`, copy four fields onto `m_remoteuser_mirror[u.slot].chans[u.channel]`, bump `m_chinfo_applies_observed[u.slot][u.channel]` (relaxed atomic).

### Diagnostic counters
- `m_chinfo_publishes_observed[MAX_PEERS][MAX_USER_CHANNELS]` and `m_chinfo_applies_observed[MAX_PEERS][MAX_USER_CHANNELS]` declared at `njclient.h:750-751`, brace-zero-initialized.
- Public accessors `GetChannelInfoPublishCount(slot, channel)` and `GetChannelInfoApplyCount(slot, channel)` at `njclient.h:437-438` (`noexcept`, relaxed-load, defined at `njclient.cpp:4554-4566`).
- Cost: one relaxed atomic increment per publish + per apply — well below noise floor; survives the fix (intentional permanent instrumentation).

### Tests
- Four new cases in `tests/test_remote_user_mirror.cpp:497-680`:
  - PeerChannelInfoUpdate apply roundtrip
  - 1000-cycle producer/consumer concurrent
  - Bounds-check: out-of-range slot/channel ignored
  - Order invariant: ChannelInfo published before NextDs reaches mirror first
- Trivial-copyable static_assert at line 696.
- Release: 12/12 PASS. TSan: 12/12 PASS, 0 ThreadSanitizer warnings.

## Files Modified

| File | Change |
|------|---------|
| `src/threading/spsc_payloads.h` | New `PeerChannelInfoUpdate` variant + Codex M-9 FINAL deviation block |
| `src/core/njclient.h` | Counter arrays + public accessor declarations |
| `src/core/njclient.cpp` | Two publishers + apply-visitor branch + accessor definitions + counter bumps |
| `tests/test_remote_user_mirror.cpp` | 4 new tests + static_assert |
| `src/build_number.h` | 291 → 295 (auto-increment past 292 target) |

## Commits

| Hash | Message |
|------|---------|
| `5b745ab` | feat(260502-rcm-fix-orphan-mirror-fields): add PeerChannelInfoUpdate SPSC + diagnostic counters (Task 1) |
| `d838cc2` | feat(260502-rcm-fix-orphan-mirror-fields): wire PeerChannelInfoUpdate publishers + apply visitor (Task 2) |
| `ac04e17` | test(260502-rcm-fix-orphan-mirror-fields): extend test_remote_user_mirror.cpp with PeerChannelInfoUpdate coverage; bump build (Task 3) |

## Plan deviations

- **realtime-audio-reviewer dispatched manually, not via Task tool.** Executor agent lacked Task-tool dispatch in its environment, so it applied the agent's `.claude/agents/realtime-audio-reviewer.md` checklist by hand and captured the audit trail in commit `ac04e17`'s body. Verdict: **ACCEPTED, zero findings.** User can re-run the actual agent post-hoc on the diff if desired; constraint #1 ("do not commit while a HIGH/CRITICAL is outstanding") is satisfied either way since the manual audit found nothing.
- **Test build directory.** Plan referenced `build-juce/` for tests, but project layout puts test binaries in `build-test/` (per `scripts/build.sh` `-DJAMWIDE_BUILD_TESTS=ON`). Doc drift in plan; tests run in correct location.
- **Build number drift.** `scripts/build.sh` auto-incremented to 295 across the build cycle. Plan target was `≥ 292`; satisfied.

## Plan-check audit trail

- Cycle 1 BLOCK on (a) PeerChannelInfoUpdate placed before `publish_added` block — would have written into a slot whose `active=false` had not yet been set; (b) TSan run not chained into `<verify><automated>`. Both patched in cycle 2.
- Cycle 2 PASS on both BLOCKERs + collateral.
- Verifier final goal-backward: PASS on all 10 checks.

## UAT — Pending in JamWide host

Reproduce the original symptom on build 295:
1. Load `JamWide.vst3` (build 295) in Reaper / Bitwig.
2. Connect to a NINJAM server with multiple peers, ideally including at least one peer broadcasting in **instamode** (`flags & 2` = LIVE_PREBUFFER pacing).
3. Wait at least 60 seconds (multiple intervals).
4. **Expected:** all peer channels remain audible continuously; per-peer output routing in mixer takes effect.
5. **Falsifiable check via lldb (or future debug panel):** `((NJClient*)x)->GetChannelInfoPublishCount(slot, channel)` should be ≥ 1 within ~50 ms of a peer arriving and ≥ 1 increment per mute/vol/pan/out_chan toggle. `GetChannelInfoApplyCount(slot, channel)` should track behind it within one audio block.

If cutoffs persist after this fix, two known unaddressed contributors (filed as Future Work below) become the next investigation target.

## Future Work (recorded, NOT addressed in this quick task)

1. `njclient.cpp:3313-3325` "both slots full" branch destroys a real `next_ds[0]` when a silence marker arrives during bursty data flow — silence-marker case should be filtered separately.
2. `njclient.cpp:1906-1916` 200 ms generation-gate spinwait on the run thread leaks slots permanently when the host's audio thread is paused — needs condvar / next-AudioProc-tick mechanism.
3. `njclient.cpp:1995` sessionmode-flags publisher filter — sessionmode (`flags & 4`) peers are silent-by-accident today (publisher filters them); logical state on the mirror is still wrong. Cosmetically broken, not user-visible until session mode ships.

## References

- Diagnosis: `.planning/debug/remote-channels-cutoff.md`
- Plan: `.planning/quick/260502-rcm-fix-orphan-mirror-fields/PLAN.md`
- Verification: `.planning/quick/260502-rcm-fix-orphan-mirror-fields/VERIFICATION.md`
- Related (already fixed): `.planning/debug/build253-vu-and-disconnect.md`
- Memory: `feedback_legacy_invariant_audit.md` (third instance of this pattern in milestone 15.1)
