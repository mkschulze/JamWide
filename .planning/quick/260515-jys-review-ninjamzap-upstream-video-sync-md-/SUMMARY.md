---
quick_id: 260515-jys
slug: review-ninjamzap-upstream-docs
status: complete
created: 2026-05-15
completed: 2026-05-15
flags: --full
verdict: NO_PLAN_ADJUSTMENT_NEEDED
---

# Summary — Review NinjamZap Upstream Docs

## Outcome

Cross-referenced two upstream NinjamZap documents (`ninjamzap-core/docs/VIDEO_SYNC.md` and `ninjamzap-server/docs/VIDEO_SUPPORT.md`) against Phase 14.3's locked planning artifacts. **No plan adjustment needed.** Plans 14.3-01 / 14.3-02 / 14.3-03 remain ready to execute as written (HEAD `2f253b6`).

## What was checked

- 16 upstream claims (codec set, message types, wire-format, server-side handling, sync mechanism, encoder params, compatibility constraints) cross-referenced against 8 locked decisions, 8 success criteria, and 3 PLAN.md frontmatters
- Per-plan impact assessment for `14.3-01-PLAN.md`, `14.3-02-PLAN.md`, `14.3-03-PLAN.md`

## Findings

| Category | Count |
|----------|-------|
| Upstream claims that CONFIRM our locked decisions | 8 |
| Upstream claims that are COMPATIBLE (orthogonal layer) | 4 |
| Upstream claims correctly DEFERRED to v1.3 | 4 |
| Conflicts / contradictions | 0 |
| New requirements we missed | 0 |
| Subtle observations worth noting (no adjustment) | 5 |

## Why no adjustment

The upstream docs **confirm** the layering Phase 14.3 has been designed around: NINJAM's wire protocol is codec-agnostic at the message layer (`UPLOAD_INTERVAL_BEGIN/WRITE`); all video-specific behavior lives in the application layer. Phase 14.3 ships exactly the codec-agnostic transport (Item 2 of SPEC §"What this phase delivers") plus the receive-dispatch fix (Item 3); v1.3 will ship the application-layer encoder + decoder + state machine that consume this substrate.

Specifically:
- `is_video_fourcc()` covers exactly upstream's 3-codec set (H264, VP8, MJPG)
- Drain loop emits exactly the message types upstream specifies (`mpb_client_upload_interval_begin/write`)
- JamWide's `MAX_ENC_BLOCKSIZE` chunking (9216 bytes) is more conservative than upstream's `NET_MESSAGE_MAX_SIZE` ceiling (16 KB)
- GUID generation via `WDL_RNG_bytes()` matches the RNG source upstream's GUID-matching mechanism requires
- Receive-dispatch INSERT (not REPLACE) at `:2148` matches upstream's "backward-compatible: clients silently fail to decode" architecture
- 4-stage `VideoRecvState` pipeline correctly DEFERRED to v1.3 Items F.1-F.3

## Optional follow-ups recorded (not applied)

- Add upstream URLs as canonical references in `14.3-SPEC.md` (low priority — already linked transitively via the spike substrate)
- File URLs in spike's `260515-0pc-deferred-items.md` as v1.3 design references (medium priority)
- Re-read SYNC §9 "Approaches that failed" as part of v1.3 discuss-phase (high priority for v1.3, not 14.3)

## Files changed

| File | Action |
|------|--------|
| `.planning/quick/260515-jys-review-ninjamzap-upstream-video-sync-md-/REVIEW.md` | created |
| `.planning/quick/260515-jys-review-ninjamzap-upstream-video-sync-md-/PLAN.md` | created |
| `.planning/quick/260515-jys-review-ninjamzap-upstream-video-sync-md-/SUMMARY.md` | created |
| `.planning/STATE.md` | Quick Tasks Completed row added |

No phase artifacts modified.

## Next

Phase 14.3 is ready to execute. Run `/gsd-execute-phase 14.3` (the existing planning is unchanged by this review).
