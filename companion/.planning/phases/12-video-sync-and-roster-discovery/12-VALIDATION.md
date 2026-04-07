---
phase: 12
slug: video-sync-and-roster-discovery
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-07
---

# Phase 12 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Vitest 4.1.3 (already available via npx) |
| **Config file** | None — Wave 0 installs |
| **Quick run command** | `cd companion && npx vitest run --reporter=verbose` |
| **Full suite command** | `cd companion && npx vitest run --reporter=verbose` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd companion && npx vitest run --reporter=verbose`
- **After every plan wave:** Run `cd companion && npx vitest run --reporter=verbose`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 12-01-01 | 01 | 1 | VID-08 | — | N/A | unit | `cd companion && npx vitest run src/__tests__/buffer-delay.test.ts` | ❌ W0 | ⬜ pending |
| 12-01-02 | 01 | 1 | VID-09 | T-12-01 | Hash in URL fragment only, never query string | unit (C++) | `cd build && ctest -R test_video_hash` | ❌ W0 | ⬜ pending |
| 12-02-01 | 02 | 1 | VID-09 | T-12-01 | Hash in URL fragment only | unit (TS) | `cd companion && npx vitest run src/__tests__/url-builder.test.ts` | ❌ W0 | ⬜ pending |
| 12-02-02 | 02 | 1 | VID-10 | T-12-02 | textContent for user names, never innerHTML | unit | `cd companion && npx vitest run src/__tests__/roster-labels.test.ts` | ❌ W0 | ⬜ pending |
| 12-02-03 | 02 | 1 | VID-12 | — | N/A | unit | `cd companion && npx vitest run src/__tests__/bandwidth-profile.test.ts` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `companion/vitest.config.ts` — Vitest config for companion project
- [ ] `companion/src/__tests__/buffer-delay.test.ts` — covers VID-08
- [ ] `companion/src/__tests__/url-builder.test.ts` — covers VID-09 (hash in URL) and VID-12 (quality params)
- [ ] `companion/src/__tests__/roster-labels.test.ts` — covers VID-10
- [ ] `companion/src/__tests__/bandwidth-profile.test.ts` — covers VID-12 (dropdown persistence)
- [ ] Framework install: `cd companion && npm install -D vitest @vitest/runner jsdom`

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Video buffering visually synced to NINJAM interval | VID-08 | Requires live NINJAM session with multiple participants | Connect 2+ clients, verify video buffer aligns with BPM/BPI interval |
| Name labels appear on correct VDO.Ninja streams | VID-10 | Requires live VDO.Ninja streams with multiple participants | Connect 2+ clients, verify labels match NINJAM usernames |
| Room password prevents unauthorized viewers | VID-09 | Requires attempting room access without correct password | Open VDO.Ninja room URL without hash fragment, verify access denied |
| Bandwidth profile visually changes quality | VID-12 | Requires visual inspection of stream quality | Select each profile, verify resolution/bitrate changes |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
