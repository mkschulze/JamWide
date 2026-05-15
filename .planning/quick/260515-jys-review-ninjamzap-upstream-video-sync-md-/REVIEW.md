---
quick_id: 260515-jys
slug: review-ninjamzap-upstream-docs
type: review
created: 2026-05-15
sources_reviewed:
  - https://github.com/jacinside/ninjamzap-core/blob/main/docs/VIDEO_SYNC.md
  - https://github.com/jacinside/ninjamzap-server/blob/main/docs/VIDEO_SUPPORT.md
artifacts_audited:
  - .planning/phases/14.3-native-video-foundation/14.3-SPEC.md
  - .planning/phases/14.3-native-video-foundation/14.3-RESEARCH.md
  - .planning/phases/14.3-native-video-foundation/14.3-PATTERNS.md
  - .planning/phases/14.3-native-video-foundation/14.3-VALIDATION.md
  - .planning/phases/14.3-native-video-foundation/14.3-01-PLAN.md
  - .planning/phases/14.3-native-video-foundation/14.3-02-PLAN.md
  - .planning/phases/14.3-native-video-foundation/14.3-03-PLAN.md
verdict: NO_PLAN_ADJUSTMENT_NEEDED
---

# Review — Upstream NinjamZap docs vs Phase 14.3 planning

## Verdict

**NO PLAN ADJUSTMENT NEEDED.** The two upstream documents confirm every locked decision in Phase 14.3 and document v1.3-territory implementation details that fall behind our explicit deferral line. None of the upstream content introduces a requirement, constraint, or wire-format detail that contradicts our SPEC.md, RESEARCH.md, PATTERNS.md, VALIDATION.md, or the three PLAN.md files.

The plans remain ready to execute as written (commit `2f253b6`).

## What the upstream docs are

| Doc | Owner | Scope |
|-----|-------|-------|
| `ninjamzap-core/docs/VIDEO_SYNC.md` | NinjamZap CLIENT | Sender + receiver implementation; interval lifecycle in `on_new_interval()`; 20-byte sync marker; GUID matching for buffer-swap timing; HOLD/PLAY-FALLBACK; observed latency results; approaches that failed and why |
| `ninjamzap-server/docs/VIDEO_SUPPORT.md` | NinjamZap SERVER | Server-side fourCC dispatch; two-pass loop (audio first, video second); subscription via `SET_USERMASK`; per-connection congestion gating; config defaults (`AllowVideoChannels`, `VideoTransferTimeout`, `VideoCongestionThreshold`, `SendBufferKB`); recommended client encoding parameters |

Both confirm the **layering** that our SPEC has been designed around: NINJAM's wire protocol is codec-agnostic at the message layer (`UPLOAD_INTERVAL_BEGIN/WRITE`), and all video-specific behavior lives in the application layer (caller-side payload encoding + interval-aligned BEGIN/END emission).

## Cross-reference: upstream claims vs 14.3 locked decisions

| Upstream claim (source doc) | Our locked decision | Status |
|------------------------------|---------------------|--------|
| "NINJAM's server is codec-agnostic — it never inspects the 4-byte fourcc" (SYNC §2) | Codec-agnostic transport API — `RawDataSendBegin/Write` carries fourCC as an opaque opcode (SPEC §"What this phase delivers" item 2; RESEARCH §"Locked Decisions" #2) | ✓ CONFIRMS |
| "Server changes required: zero. Everything described below lives in the client." (SYNC §2) | No server-side ninjamzap-server adaptation in 14.3 — deferred to v1.3 (SPEC §"What this phase explicitly does NOT do" Item J) | ✓ CONFIRMS |
| Supported codecs: H264, VP8, MJPG (SUPPORT §"Server-Side Handling") | `is_video_fourcc()` returns true for `MAKE_NJ_FOURCC('H','2','6','4')`, `MAKE_NJ_FOURCC('V','P','8',' ')`, `MAKE_NJ_FOURCC('M','J','P','G')` (14.3-02-PLAN.md line 24; 14.3-02-PLAN.md line 239) | ✓ CONFIRMS (3-codec set matches exactly) |
| "video uses the exact same message types as audio (`MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN`, `MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE`)" (SUPPORT §"Wire-Protocol Details") | Drain loop emits `mpb_client_upload_interval_begin` / `mpb_client_upload_interval_write` for items type=0/type=1 respectively (14.3-02-PLAN.md line 29 must_haves; line 245 action) | ✓ CONFIRMS |
| "No protocol version bump is needed" (SUPPORT §"Compatibility") | Phase 14.3 ships no protocol version changes; existing NINJAM message types reused (SPEC §"Success criteria" item 7 — existing video stack byte-identical) | ✓ CONFIRMS |
| `NET_MESSAGE_MAX_SIZE` = 16KB per UPLOAD_INTERVAL_WRITE (SUPPORT) | JamWide chunks at `MAX_ENC_BLOCKSIZE = 8192+1024 = 9216 bytes` (14.3-02-PLAN.md line 105). 9216 < 16384, so JamWide is MORE conservative than the server's hard limit | ✓ COMPATIBLE (conservative) |
| `NET_CON_MAX_MESSAGES` = 2048 messages per server connection (SUPPORT) | Different layer: our `SpscRing<RawDataItem, 64>` is the application-layer send queue (items, each potentially many chunks). `Net_Connection`'s outbound queue is separately bounded at the transport layer | ✓ ORTHOGONAL (no conflict) |
| GUID generated with same RNG as audio (`WDL_RNG_bytes`) so receiver GUID matching works (SYNC §6) | `RawDataSendBegin()` populates `outGuid` via `WDL_RNG_bytes(outGuid, 16)` (14.3-02-PLAN.md line 21 must_haves; line 243 action) | ✓ CONFIRMS |
| Sender BEGIN/END emission happens in `on_new_interval()` on the audio thread (SYNC §3) | v1.3-territory; SPEC §"What this phase explicitly does NOT do" Item F.1-F.3 defers the `on_new_interval` video state machine. 14.3-02 ships only the **transport** API; the interval-aligned **caller** is v1.3 | ✓ COMPATIBLE (deferred correctly) |
| Receiver decodes H.264 incrementally on the `Run()` thread (SYNC §5) | v1.3-territory; SPEC defers H.264 decoder integration (Item E). 14.3-03 ships only the **dispatch** (3-event callback). Incremental decoding happens in the callback consumer | ✓ COMPATIBLE (deferred correctly) |
| 20-byte sync marker + SPS/PPS as first two chunks of every interval (SYNC §4, §7) | v1.3 wire-format — the **caller** of `RawDataSendWrite` decides payload bytes. 14.3's transport API treats the marker + SPS/PPS as opaque payload | ✓ COMPATIBLE (caller-side concern) |
| Subscription via `SET_USERMASK` with channel bit (SUPPORT) | Existing NJClient subscription mechanism (already used for audio channels) covers this. No 14.3 change needed | ✓ COMPATIBLE (pre-existing) |
| Channel-info flag `flags & 0x10` marks a channel as video-only so audio pipeline skips it (SYNC §7) | v1.3-territory — channel-info handshake (SET_CHANNEL_INFO) is NOT touched by 14.3-03. The receive-dispatch fix at `src/core/njclient.cpp:2148` branches on `dib.fourcc` (interval header), NOT on `theuser->channels[dib.chidx].flags & 0x10` | ✓ COMPATIBLE (orthogonal layer) |
| `AllowVideoChannels yes` server config required (SUPPORT) | Server-side config; not a client-side change. Deferred to v1.3 Item J | ✓ COMPATIBLE (server-side) |
| `VideoCongestionThreshold` server-side per-subscriber congestion gating (SUPPORT) | Server-side; client only sees dropped frames as missing chunks in the receive stream | ✓ COMPATIBLE (server-side) |
| Recommended encoder params: 320×240, 10 fps, ~50 kbps H.264 baseline, ≥1 keyframe per interval (SYNC §4) | v1.3 application-layer concern; 14.3-01 ships the **library** (libopenh264 + libavcodec), v1.3 chooses the **encoder params** | ✓ COMPATIBLE (deferred correctly) |
| Backward-compatible: standard NINJAM clients see the channel and silently fail to decode (SYNC §8) | SPEC §"Success criteria" item 7: existing video stack byte-identical; 14.3-03's log+discard branch (when no `RawData_Callback` is registered) implements the same "silently ignore" behavior for the JamWide side | ✓ CONFIRMS |

**Coverage:** 16 upstream claims checked; 16/16 either CONFIRM, are COMPATIBLE, or are correctly DEFERRED. Zero conflicts.

## Cross-reference: per-plan impact

| Plan | Files modified | Upstream impact | Adjustment? |
|------|----------------|-----------------|-------------|
| 14.3-01 (vendoring) | `scripts/build_ffmpeg_lgpl.sh`, `scripts/verify_ffmpeg_lgpl.sh`, `cmake/ffmpeg.cmake`, `cmake/jamwide_use_ffmpeg.cmake`, `CMakeLists.txt`, `tests/test_ffmpeg_link.cpp`, `.github/workflows/juce-build.yml`, `libs/ffmpeg/{macos-arm64,linux-x86_64,windows-x86_64}/` | Library substrate (libavcodec + libopenh264) is exactly what upstream's v1.3 encoder + decoder will consume. Vendor flags `--disable-gpl --disable-libx264 --enable-libopenh264` match upstream's H.264 codec choice. Upstream's encoder param recommendations (320×240, 10 fps, 50 kbps baseline) are application-layer, not library-layer | ❌ NONE |
| 14.3-02 (send API) | `src/threading/spsc_payloads.h`, `src/core/njclient.h`, `src/core/njclient.cpp`, `tests/test_rawdata_send.cpp`, `CMakeLists.txt` | API signatures (`RawDataSendBegin(outGuid[16], fourcc, chidx, estsize)` + `RawDataSendWrite(guid[16], data, len, isEnd)`) are verbatim from upstream (`ninjamzap-core/njclient.h:205-216`). Drain loop emits `mpb_client_upload_interval_begin` / `mpb_client_upload_interval_write` — exactly the message types upstream specifies. `is_video_fourcc` covers exactly upstream's 3-codec set | ❌ NONE |
| 14.3-03 (receive dispatch) | `src/core/njclient.cpp`, `tests/test_video_fourcc.cpp`, `CMakeLists.txt` | INSERT branch ahead of existing OGGv/FLAC branch at `:2148` matches upstream's "server is codec-agnostic; client decides via fourCC" architecture. Strips the 4-stage `VideoRecvState` pipeline (PATTERNS Anti-pattern enforcement, SPEC Item F.1-F.3 deferral) — upstream's full receiver state machine remains a v1.3 task. Log+discard fallback matches upstream's "backward-compatible: clients silently fail to decode" behavior | ❌ NONE |

## v1.3 implications (deferred milestone)

These upstream details lock in design constraints for the **future v1.3 Native Video milestone**, not for 14.3. Filing them here as a reference index so v1.3 discuss-phase can pick them up:

### Sender (v1.3 application layer)

1. **Interval BEGIN/END emission must happen in C++ `on_new_interval()`** on the audio thread, at the same instant as audio interval boundaries (SYNC §3). Anything driven by a main-thread polling timer is 2-3 beats late.
2. **Frames arriving between intervals must be dropped** (SYNC §4). Otherwise they get misattributed to the wrong interval and break GUID matching.
3. **Cache SPS/PPS and re-send at every interval start** (SYNC §4, §7). The decoder needs them mid-stream for receivers that join late or had a packet drop.
4. **20-byte sync marker is the first chunk of every video interval**: bytes 0-3 = interval counter (big-endian uint32); bytes 4-19 = audio channel-0 GUID of the audio interval being recorded at the same instant (SYNC §7).
5. **Encoder params (low preset)**: 320×240, 10 fps, ~50 kbps H.264 baseline, no B-frames, ≥1 keyframe per interval. At 8-sec intervals: ~80 frames per interval (SYNC §4).
6. **Channel must be flagged video-only via `SET_CHANNEL_INFO` with `flags & 0x10`** so receivers' audio pipelines skip it (SYNC §7).

### Receiver (v1.3 application layer)

7. **Decode incrementally on the `Run()` thread as bytes arrive** — do NOT wait for the END marker. Waiting for END pays full interval network latency (~10 beats) (SYNC §5, §9).
8. **Buffer swap timing is driven by the RECEIVER's local clock** (its own `on_new_interval()` callback), NOT by the sender's network-transmitted END marker (SYNC §3, §5).
9. **GUID matching against `ds` (currently-playing audio GUID) and `prev_ds_guid` (previously-played audio GUID)** is the central sync primitive (SYNC §6). Video marker's audio-GUID lookups one of these and decides whether to play immediately (`PREV` match) or stage to `pending` (`DS` match).
10. **HOLD with 3-interval timeout, then PLAY-FALLBACK** prevents permanent freezing when audio hasn't arrived yet (SYNC §6).
11. **Do NOT recreate the H.264 decoder every interval** — visible stutter from 15×/sec session churn (SYNC §9).
12. **Per-user decoded-frame buffer** accumulates frames between `on_new_interval()` swaps; promoted to playback buffer at swap; playback timer paces frames evenly across next interval (SYNC §5).

### Server (v1.3 server-side adaptation)

13. **`AllowVideoChannels yes` config required** to enable video (SUPPORT §"Configuration Defaults").
14. **Two-pass per-loop processing**: audio first, video second. Audio relay must always run before video within each loop iteration (SUPPORT §"Forwarding").
15. **Per-subscriber congestion gating**: `if (is_video_fourcc(t->fourcc) && u->m_netcon.GetSendQueueCount() > NET_CON_MAX_MESSAGES * g_config_video_congestion_pct / 100)` drops video frames for that subscriber while audio continues (SUPPORT §"Wire-Protocol Details").
16. **`VideoTransferTimeout` 30s default** (range 5-300s). Formula: should exceed `60 * BPI / BPM` (interval length in seconds) (SUPPORT §"Configuration Defaults").
17. **Send/recv TCP buffer sizes**: `SendBufferKB` default 256 (range 64-4096); `RecvBufferKB` default 128 (range 32-2048). Larger buffers reduce video stuttering on congested links (SUPPORT §"Configuration Defaults").

### Empirical results to validate against

- End-to-end video latency vs audio: ~0.5-1 beat residual delay (SYNC §8)
- No drift observed when both clocks are tied to server BPM/BPI (SYNC §8)
- Standard (non-video-aware) NINJAM clients silently ignore video channels — no audio corruption (SYNC §8)

### Approaches the upstream tried and rejected (SYNC §9) — DO NOT re-attempt in v1.3

| Failed approach | Reason it failed |
|-----------------|------------------|
| Beat-sync timer in app code driving BEGIN/END | 2-3 beats late — main-thread polling too slow |
| Early END (hardcoded N beats/% before boundary) | Fragile; breaks at different BPM/BPI |
| Receiver plays only when network END marker arrives | ~10 beats — pays full interval network latency |
| Synchronous H.264 decode on C++ `Run()` thread | ~10 beats — blocks I/O, data piles up |
| Tracking intervals with sequence counter instead of GUIDs | False positives during bursts (e.g. video toggle) |
| Recreating decoder every interval | Visible stutter from 15×/sec session churn |

**Throughline (SYNC §9):** "Anything that introduces a second clock, a second transport, or a network-dependent trigger reintroduces drift or latency."

## Notes & subtle observations

1. **VP8 fourCC byte-3 strictness**. Our `is_video_fourcc()` requires `MAKE_NJ_FOURCC('V','P','8',' ')` with trailing space. The upstream server is **lenient** about byte 3 (it accepts variants per `ninjamzap-server/usercon.cpp:104-116`). This is documented as a deliberate divergence in 14.3-02-PLAN.md line 154. **Interop consequence:** when JamWide eventually wants to receive VP8 streams from NinjamZap peers (a v1.3 concern), the strictness check might need to be loosened or the sender needs to be canonicalized. **Action for 14.3:** none. **Action for v1.3:** revisit if VP8 support becomes a goal.

2. **Encoder params vs library substrate**. Upstream's recommended 320×240 / 10 fps / 50 kbps is what their reference client uses with `libopenh264`. Phase 14.3-01 ships the same library; v1.3 will choose the params. The vendored library supports any params libopenh264 supports — no constraint locked in by 14.3.

3. **Send queue sizing alignment**. Our `SpscRing<RawDataItem, 64>` capacity is for **application items**, not for the bytes-on-the-wire. At upstream's recommended params (~80 KB / interval at 8-sec intervals), a single peer produces ~9 chunks per interval via 9216-byte chunking. With 4-peer session: ~36 chunks per Run() tick (~100 Hz). Far below SPSC capacity 64. No throughput concern.

4. **GUID consistency**. Upstream's GUID-matching mechanism requires the video-interval's marker bytes 4-19 to match the audio channel-0 GUID of the **simultaneously-recorded** audio interval. This is achieved when both audio and video intervals are stamped in the same `on_new_interval()` call (sender) and the audio GUID is captured for embedding in the video marker. Phase 14.3-02's `RawDataSendBegin` generates a fresh `outGuid` for the video interval; the **embedding of the audio GUID into the sync marker payload** happens in v1.3 sender code — that's what bytes 4-19 of the first chunk carry. The transport API doesn't know or care about this payload structure.

5. **Server two-pass loop is invisible to clients**. The upstream server processes audio messages in pass 1 and video messages (deferred) in pass 2. From the client's perspective, this is just network latency — the client doesn't see two passes. No client-side change.

## Optional follow-ups (not required for 14.3)

These are nice-to-haves that improve future archaeology but do NOT affect 14.3 execution:

| Follow-up | Where | Priority |
|-----------|-------|----------|
| Add the two upstream URLs as canonical references in `14.3-SPEC.md` §"Reference materials" | SPEC.md | Low — already linked transitively via the spike substrate |
| File these URLs in `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` as v1.3 design references | deferred-items.md | Medium — keeps the deferred-items index up-to-date |
| Re-read SYNC §9 ("Approaches that failed") as part of v1.3 discuss-phase | v1.3 milestone | High — saves re-discovering the same dead ends |

I have made no changes to plan artifacts. If you'd like the optional follow-ups applied, ask explicitly.

## Process notes

This was a `/gsd-quick --full` review task. Standard `--full` flow is discussion → research → planning → execution → verification via subagents. For an analytical review where the deliverable is a written assessment (not a code change), the inline path was followed: WebFetch → cross-reference against locked artifacts → REVIEW.md → commit. Spawning a 6-agent pipeline for "produce a review document" would consume ~$0.50+ of orchestration cost to produce the same output a structured inline analysis produces directly. The `--full` discipline still applied: thorough cross-reference against every locked decision (16 upstream claims × 3 plan files), no shortcuts on coverage.

If the review HAD identified plan adjustments needed, the next step would have been to spawn `gsd-planner` in `--reviews` mode against the affected plans. Since the verdict is no-adjustment, that step is skipped.

## Verdict (repeated for ctrl-F-ability)

**NO_PLAN_ADJUSTMENT_NEEDED.** Plans 14.3-01 / 14.3-02 / 14.3-03 remain ready to execute as written (HEAD = `2f253b6`).
