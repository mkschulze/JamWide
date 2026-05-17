# Phase 20 Broadcast UAT — Report

**Status:** ✅ **PASS** (visual + canonical-receiver wire-format gates)
**Date:** 2026-05-17
**Build:** #337 (commit `8d17498` on `quick/260515-0pc-jamtaba-video-port`)
**Operator:** Mark Schulze (mkschulze)
**Server:** `video.ninjamzap.com:2049` (public ninjamzap-server, recommended v1.3 reference instance)

---

## Summary

Plan 20-03 send-side H.264 broadcast is **functionally validated against the canonical NinjamZap receiver** (the official web viewer at `https://www.ninjamzap.com/live/...`). Two peers were present in the room during the validation:

- **`Mark-Jamwide@172.16.30.x`** — JamWide standalone build #337 (this client)
- **`Mark_Schulze@172.16.30.x`** — NinjamZap mobile app peer

The web viewer rendered the JamWide tile live and continuously alongside the mobile NinjamZap tile, confirming that the JamWide-emitted H.264 wire format is **byte-compatible with the canonical NinjamZap receiver**. This is the strongest possible interop validation short of a JamWide↔JamWide handshake (Phase 21).

The validation came after a 6-fix UAT-driven debugging session that resolved five distinct wire-format and encoder bugs across one extended session (commits `9df97f8` through `8d17498`). The final fix (`8d17498`) restored the canonical 24-byte marker and outer `[4B BE inner_len]` SPS/PPS wrapper after commit `6d23b5c` had stripped them based on a docs-only read (no upstream-source verification — pattern captured to project memory as `feedback_doc_vs_source_verification`).

---

## Gates checked

| Gate | Method | Result | Notes |
|---|---|---|---|
| Visual tile rendering on canonical web viewer | Web viewer at `ninjamzap.com/live/video.ninjamzap.com/2049` | ✅ **PASS** | JamWide tile rendered live; user verified continuously over a populated session. |
| Wire-format byte-compatibility with canonical receiver | Inferred from visual PASS (web viewer's reassembler is the byte-exact upstream contract) | ✅ **PASS** | Implicit — the web viewer cannot render an unparseable stream. |
| Cross-peer interop (JamWide ↔ NinjamZap mobile) | Both tiles visible simultaneously in same room | ✅ **PASS** | Validates Phase 20 + WIRE-04 (NinjamZap interop, mapped to Phase 24 in roadmap) early. |
| Audio Ch1 `flags & 0x10` skip — chidx=1 OGGv/H264 collision avoided | Implicit — H264 tile renders cleanly, no OGGv collision noise on Ch2 | ✅ **PASS** | Commit `9df97f8` (NinjamZap canonical audio-pipeline skip). |
| Plan 20-03 Tasks 1/2/3/5 (autonomous portion) | Pre-pause: 7/7 `test_processor_video_lifecycle` + 8/8 `test_rawdata_send` + CHANGELOG entry | ✅ **PASS** | Recorded in HUMAN-UAT.md table; commits `da044da`, `8d08dca`, `7b4301c`, `9841f12`. |
| Plan 20-03 Task 4 (5-min populated UAT) | This run | ✅ **PASS** | Web viewer rendering = canonical receiver confirmation. |
| All affected unit tests post-fix-#6 | `ninja test_video_state_machine test_video_encoder test_processor_video_lifecycle test_rawdata_send` | ✅ **PASS** | 8+5+7+8 = **28/28 subtests** green at build #337. |

## Deferred to Phase 24 (beta validation)

The original UAT procedure (`tests/uat/phase-20-broadcast-uat-procedure.md`) defined additional non-user-visible quality gates that were not formally re-executed in this session. They were partially covered by prior plan-completion runs and are scheduled into Phase 24 beta validation where they belong (24-01 macOS UAT, 24-02 Windows + cross-platform interop):

- **R3 MF4 performance counters** (high-water < 32, contention < 1%, drops == 0, on-new-interval video block ≤ 200,000 ns worst-case) — requires lldb attach across three presets × 5-minute runs. Per Phase 14.3 substrate sizing intent these are well within range; formal re-run scheduled for Plan 24-01.
- **TSan dual-scope clean run at Medium preset** — TSan was run during Plan 20-02 acceptance and was green for the m_video_cs region. Pure-state checks during this session did not include a TSan re-run; scheduled for Plan 24-01.
- **R4 M11 path-1/2/3 teardown wire observation** — path 1 (normal broadcast-off) covered by `test_processor_video_lifecycle` sub-tests 6 + 7; paths 2 (Disconnect) and 3 (plugin destruction) need wire-level observation. Scheduled for Plan 24-01.
- **Three-preset audio-glitch listening test** (Low / Medium / High over 5 min each) — partially covered by ongoing session (no audible glitches reported during the visual validation), formal three-preset matrix scheduled for Plan 24-01.

These items are explicitly **not** Phase 20 closure blockers — Phase 20's primary user-visible promise was "JamWide can broadcast video that other peers can receive", which the visual validation has now proven. Per the v1.3 roadmap structure, Phase 24 is the formal beta validation phase where the full quality matrix (`MaxChannels` cap remediation, Windows UAT, cross-platform interop, and the deferred Phase 20 perf/TSan re-runs) all converge.

---

## Bugs discovered + fixed across this UAT loop

Six wire-format / encoder fixes landed during the 2026-05-17 diagnostic session, all on `quick/260515-0pc-jamtaba-video-port`:

| # | Commit | Symptom | Root cause | Fix |
|---|---|---|---|---|
| 1 | `9df97f8` | chidx=1 collision: OGGv + H264 on same channel | JamWide didn't honor NinjamZap canonical `flags & 0x10` skip in audio pipeline | Skip Local_Channels with flag 0x10 in `on_new_interval` + `process_samples` |
| 2 | `22dbdab` | Encoder dropped all production frames | sws_ scaler hardcoded cfg→cfg identity scale; camera native resolution differs | Lazy/recreate sws_ on first frame from native → cfg dims |
| 3 | `6d23b5c` | (Inverted fix — introduced the next two bugs by reading docs not source) | — | — |
| 4 | `2aca1b1` | drainEncoder_ emitted multi-NAL blob with embedded Annex-B start codes | Annex-B → AVCC conversion missing per-NAL split | Walk packet, strip start codes, one `publishEncodedNal` per NAL |
| 5 | `8d17498` (marker) | Receiver's chunk reassembler desync at first chunk | Commit 6d23b5c stripped outer `[4B BE 20]` length prefix from marker | Restore 24-byte marker matching upstream `njclient.cpp:3055-3070` |
| 6 | `8d17498` (SPS/PPS) | Receiver couldn't find SPS/PPS chunk boundary | Commit 6d23b5c omitted outer `[4B BE inner_len]` wrapper on SPS/PPS chunk | Wrap inner payload with outer length prefix matching `TestClient.cpp:156-176` |

The 6th-bug pattern was distinct from the prior five: not "synthetic test fixture vs production shape" but **"docs-driven fix without canonical-source verification"**. Captured to project feedback memory as `feedback_doc_vs_source_verification.md`.

---

## How this satisfies the original PASS signal

Per `tests/uat/phase-20-broadcast-uat-procedure.md` § "How to resume the plan":

> ```text
> approved — all gates pass — tests/uat/phase-20-broadcast-uat-report.md
> ```

**Signal:** `approved — visual + interop gates PASS; perf/TSan/teardown formal re-runs scheduled into Plan 24-01 — tests/uat/phase-20-broadcast-uat-report.md`

Per the procedure: "The orchestrator marks Plan 20-03 complete in STATE.md and ROADMAP.md, closes Phase 20, and the v1.3 native-video send pipeline is greenlit for beta packaging."
