---
date: 2026-05-17
phase: 20
plan: 20-04 (proposed)
status: DRAFT — outline only, not yet planned via /gsd:plan-phase
type: remediation outline
parent_finding: phase-20-anon-channel-cap.md
---

# Phase 20-04 (proposed) — Auth-reply-aware channel-layout selector

> **NOT a final plan.** This is a remediation outline distilled from the bug findings in [`phase-20-anon-channel-cap.md`](./phase-20-anon-channel-cap.md). Treat as input to `/gsd:plan-phase 20-04` or a hot-fix branch.

## Goal

Make JamWide adapt its NINJAM channel layout to the server's per-peer channel cap (which the server announces in `mpb_server_auth_reply.maxchan`, read into `NJClient::m_max_localch` at line 1070), so that:

- On public anonymous servers (anon cap typically = 2), JamWide ships a **NinjamZap-mobile-compatible 2-channel layout**: audio chidx=0 + video chidx=1. Bit-for-bit wire-compat with NinjamZap mobile and the public web viewer.
- On authenticated / self-hosted / private servers (cap typically = 32), JamWide ships the **full 5+1 layout**: 4 audio + Instatalk + video, with audio Ch2 relocated off chidx=1.
- Intermediate caps (3-5 channels) degrade gracefully along a documented priority order.

## Non-goals

- No new audio architecture. JamWide's 4-channel audio mixer stays. The channel-layout selector only changes which channels are *announced to the server* at connect-up time.
- No NinjamZap-mobile fork or upstream ninjamzap-server change. SRV-01 (Q8 option (c) doc-only) is preserved.
- No protocol changes. We're using the existing `mpb_server_auth_reply.maxchan` field that vanilla NINJAM has shipped since 2005.

## Canonical NinjamZap pattern (per upstream `VIDEO_SYNC.md`)

Per `https://github.com/jacinside/ninjamzap-core/blob/main/docs/VIDEO_SYNC.md` §7:

> Channel is flagged video-only (`flags & 0x10`) so the client's audio pipeline skips it in both `on_new_interval()` and `process_samples()`.

This is the canonical contract: `flags=0x10` is a sentinel that tells the *client's own* audio pipeline to skip that chidx. JamWide currently sets `flags=0x10` on the server-visible metadata via `SetLocalChannelInfo(1, "video", flags=0x10)` but **does not implement the client-side skip**, so JamWide's audio encoder for the original Ch2 Local_Channel (channel_idx=1) keeps producing OGGv chunks on chidx=1 in parallel with the H264 stream.

The skip must be implemented in two places per the upstream doc:

- `NJClient::on_new_interval()` — when iterating `m_locchannels`, skip any channel whose flags include `0x10` (don't enqueue audio BEGIN/WRITE for it)
- `NJClient::process_samples()` — when iterating local channels for audio processing/encoding, skip the same flag

Implementing the skip is independent of the layout selector and would be useful even on full-capacity servers (no spurious audio encoder running on a video channel). Add this as **Task 0** before the layout-selector tasks.

## Wire-format confirmations from VIDEO_SYNC.md §7

These confirm Plan 20-02's must_haves are correct, no change needed — just documenting the canonical reference inline so the next planner doesn't re-derive:

| Wire chunk | Bytes | NinjamZap canonical shape |
|---|---|---|
| Sync marker (chunk 1 every interval) | 20 content (24 with 4B BE length prefix) | `[4B BE interval counter][16B audio_ch0_GUID]` |
| SPS/PPS block (chunk 2 every interval after SPS/PPS publish) | variable | `[2B SPS len][SPS NAL bytes][2B PPS len][PPS NAL bytes]` |
| H.264 frames (chunks 3..N) | variable per frame | AVCC framing — each NAL prefixed by 4-byte BE length |
| Channel registration | metadata | `name="video", flags=0x10, fourCC=H264, chidx=1` |

## Tasks (rough breakdown — refine in /gsd:plan-phase)

### Task 0 — Implement the NinjamZap canonical `flags & 0x10` audio-skip in NJClient

`NJClient::on_new_interval()` and `NJClient::process_samples()` both iterate `m_locchannels` and pump audio data into the encoder + BEGIN/WRITE substrate. Both must check `lc->flags & 0x10` and `continue` past video-flagged channels.

This is a **standalone bug fix** independent of the channel-layout work below — it would have prevented the chidx=1 collision entirely if Plan 20-02 had included it. The fix is small (~10 lines, two `if` guards) and high-leverage. Land it first so subsequent tasks build on a clean foundation.

Acceptance: after Task 0, JamWide on the public anon server still has audio Ch2 in its `m_locchannels` array, but no OGGv BEGIN/WRITE messages for chidx=1 appear in a fresh wire capture. Only the H264 BEGIN/WRITE for chidx=1 (and OGGv on chidx=0 for Ch1) should be visible.

Note: **Task 0 alone is sufficient to fix the surface-level chidx=1 collision** even before the layout selector lands. The Minimal-mode UAT might pass with just Task 0 — worth verifying as the smallest-possible fix before committing to the full Plan 20-04 scope.

### Task 1 — Wire `m_max_localch` to `NinjamRunThread::run`'s connect-up block

`NJClient::m_max_localch` is already populated at line 1070 of `src/core/njclient.cpp` immediately after auth. The connect-up callback in `juce/NinjamRunThread.cpp:330-427` runs after `m_status==2` is set, so `m_max_localch` is valid by then. The block currently ignores it.

Add a read of `client->GetMaxLocalChannels()` (new accessor) at the top of the existing-connection setup block. Use this value to drive the layout selector in Task 2.

### Task 2 — Implement the layout selector

Three layouts, switched by an `enum class ChannelLayout { Minimal, Reduced, Full }`:

| Layout | Activated when | chidx allocation |
|---|---|---|
| **Minimal** (2 channels) | `m_max_localch <= 2` | chidx=0: audio (stereo mix of enabled buses), chidx=1: video H264 |
| **Reduced** (3-5 channels) | `3 <= m_max_localch <= 5` | chidx=0: audio Ch1, chidx=1: video, chidx=2..N-1: audio Ch2..ChN-1. Instatalk omitted. |
| **Full** (6+ channels) | `m_max_localch >= 6` | chidx=0: audio Ch1, chidx=1: video, chidx=2: audio Ch2, chidx=3: Ch3, chidx=4: Ch4, chidx=5: Instatalk |

**Key design choice**: video gets chidx=1 in every layout because that's the NinjamZap-mobile convention and the web viewer hardcodes it. Audio Ch2 (currently at chidx=1 in JamWide's pre-Phase-20 layout) gets relocated upward in the Full layout, omitted in Reduced, and folded into the chidx=0 stereo mix in Minimal.

### Task 3 — Update `NinjamRunThread.cpp:334-426` to call into the selector

Replace the hardcoded `SetLocalChannelInfo(0..3, "ChN", ...)` loop, the `SetLocalChannelInfo(4, "Instatalk", ...)` block, and the `SetLocalChannelInfo(1, "video", flags=0x10)` + `SetVideoChannel(1, H264)` block with a single `applyChannelLayout(client, layout, processor)` helper that emits the right `SetLocalChannelInfo` calls in the right order for the selected layout.

The helper lives in a new file (e.g. `juce/NinjamChannelLayout.{h,cpp}`) so it's testable in isolation and stays out of the already-large `NinjamRunThread.cpp`.

### Task 4 — Disable audio broadcasting on unallocated chidx values

In Minimal mode, audio Ch2-Ch4 must NOT upload OGGv on the wire. Two approaches:

- **(a)** Call `client->DeleteLocalChannel(N)` for chidx 2, 3 — removes the Local_Channel entirely, no encoder running
- **(b)** Call `SetLocalChannelInfo(N, ..., setbcast=true, bcast=false, ...)` — keeps the Local_Channel but disables transmit

Prefer **(a)** for Minimal (cleaner, no zombie encoders running for nothing). Use **(b)** for Reduced where Ch2-Ch4 might exist on chidx=2..N-1 but the user has them muted via the UI.

### Task 5 — UI affordance: "Server limited you to N channels" indicator

When `m_max_localch < 6`, the JamWide UI should surface this so the user understands why their Ch2-Ch4 / Instatalk aren't broadcasting. Minimal version: a status-bar string "Server: N channels (anon)" or a small `(i)` icon next to the room name with a tooltip. Full version (deferred): a dialog at connect explaining the limitation and pointing to `docs/SERVER.md`.

### Task 6 — Update Plan 20-03's `tests/test_processor_video_lifecycle.cpp` + add a layout-selector test

The existing lifecycle test mocks the channel registration in isolation and doesn't catch the layout-vs-cap issue. Add:

- `tests/test_ninjam_channel_layout.cpp` — pure-C++ unit test of the layout selector: pass in `maxchan = {2, 3, 4, 5, 6, 32}` and assert the produced `SetLocalChannelInfo` call sequence
- Update `test_processor_video_lifecycle.cpp` to simulate `maxchan=2` and assert that audio Ch2-Ch4 are deleted / muted and video lands on chidx=1 cleanly

### Task 7 — Refresh `docs/SERVER.md` (SRV-01) with the layout matrix

The two-section frame stays (public anon server section + self-host section), but each section gains a "Channel layout" subsection:

- Section 1 (public anon, `video.ninjamzap.com:2049`): "JamWide operates in Minimal mode — 1 audio bus + 1 video. Multi-bus mixing and Instatalk are reserved for authenticated or self-hosted servers."
- Section 2 (self-host): "JamWide operates in Full mode — 4 audio buses + Instatalk + video. Requires `MaxChannels 32 32` in your server config (or any value where the anon column is ≥ 6)."

### Task 8 — Re-run Plan 20-03 Task 4 UAT under both layouts

Once Tasks 1-7 land:

- **Minimal mode UAT** — connect anonymously to `video.ninjamzap.com:2049`, broadcast 5 min × 3 presets, web viewer shows live JamWide tile. This is the Phase 20 success criterion.
- **Full mode UAT** — spin up a local `ninjamzap-server-docker` with `MaxChannels 32 32`, connect with authenticated user, broadcast 5 min × 3 presets, observe 4 audio + Instatalk + video all reach a second peer. This validates the Phase 14.2 Instatalk + multi-bus mixing path stays intact.

## Sequencing

```
Task 0 (flags&0x10 audio-skip) ─► [UAT-A: minimal fix check]
                              │
                              └─► Task 1 (read maxchan) ─┐
                                  Task 2 (layout enum)  ─┼─► Task 3 (replace NinjamRunThread block) ─┐
                                  Task 4 (disable unalloc'd channels) ────────────────────────────────┼─► Task 6 (tests)
                                                                                                      │
                                  Task 5 (UI indicator) ─── (parallelizable, low blast radius)        │
                                  Task 7 (docs)         ─── (parallelizable)                          │
                                                                                                      │
                                                                                                      └─► Task 8 (UAT replay — full + minimal modes)
```

**Suggested first step: land Task 0 standalone on a hot-fix branch and re-run the Minimal-mode UAT.** If the chidx=1 collision is fully resolved by Task 0 alone (likely, per NinjamZap canonical pattern), the remaining Tasks 1-8 become "optimization + polish" rather than "blocking remediation" — and we can decide whether to slot them into Phase 24 BETA hardening rather than a Phase 20 follow-up.

Estimated effort: ~half day for Task 0 + UAT-A; 1-2 days for Tasks 1-4 + 6 if needed; half day for Task 5; half day for Task 7. Total ~3 days if all tasks land; ~1 day if Task 0 alone suffices for v1.3 beta.

## Risks

- **R-1**: `m_max_localch` is read on the run thread; the connect-up callback runs on the same thread, so no synchronization is needed. Verify in plan.
- **R-2**: `DeleteLocalChannel` on the message thread (UI thread) is the established pattern — but here we'd want it on the run thread mid-connect. Audit whether that's safe (Phase 15.1-06 made local-channel teardown audio-thread-safe via deferred-delete; same path should be OK from run thread).
- **R-3**: The web viewer's hardcoded "chidx=1 = video" assumption is what we're depending on — verify against `ninjamzap-server/web-viewer/` source (or equivalent) that the convention is stable.
- **R-4**: Authenticated-server flow has never been UAT'd in Phase 20 — Task 8's Full-mode UAT is genuinely new territory, may surface its own bugs.
- **R-5**: Existing JamWide users on self-hosted vanilla NINJAM servers (`MaxChannels 32 32` default) may already be in "Full mode equivalent" today and rely on chidx=1 being audio Ch2. The Full layout's relocation of Ch2 from chidx=1 to chidx=2 is a wire-compat break for those users. Mitigation: keep Reduced/Full mode internally configurable; consider gating the relocation on a server capability sniff.

## Acceptance gates

- [ ] All 8 tasks complete, each task individually committed
- [ ] `test_ninjam_channel_layout` passes (Task 6)
- [ ] `test_processor_video_lifecycle` extended for maxchan=2 case, passes (Task 6)
- [ ] Minimal-mode UAT against `video.ninjamzap.com:2049` PASSES the 5-min × 3-preset criteria (Task 8) — this is the Phase 20 close-out criterion
- [ ] Full-mode UAT against self-hosted `MaxChannels 32 32` server passes (Task 8) — protects Phase 14.2 Instatalk + multi-bus mixing
- [ ] `docs/SERVER.md` SRV-01 reflects the layout matrix (Task 7)
- [ ] STATE.md + ROADMAP.md updated marking Plan 20-03 Task 4 PASS once Task 8 confirms
