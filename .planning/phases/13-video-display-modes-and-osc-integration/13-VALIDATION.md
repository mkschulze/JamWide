---
phase: 13
slug: video-display-modes-and-osc-integration
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-04-07
---

# Phase 13 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | vitest 4.x (companion), cmake build (C++) |
| **Config file** | companion/vitest.config.ts |
| **Quick run command** | `cd companion && npx vitest run` |
| **Full suite command** | `cd companion && npx vitest run && cd .. && cmake --build build --target JamWideJuce` |
| **Estimated runtime** | ~15 seconds (vitest ~2s, cmake ~12s) |

---

## Sampling Rate

- **After every task commit:** Run `cd companion && npx vitest run`
- **After every plan wave:** Run full suite command
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 13-01-01 | 01 | 1 | VID-07 | T-13-01 | Popout URL uses view-only params (no &push=) | unit | `npx vitest run popout-url` | ❌ W0 | ⬜ pending |
| 13-01-01 | 01 | 1 | VID-07 | — | Disconnect overlay shows/hides on roster message | unit | `npx vitest run popout-page` | ❌ W0 | ⬜ pending |
| 13-01-02 | 01 | 1 | VID-07 | T-13-02 | Window tracking prevents duplicate popouts | unit | `npx vitest run popout-window` | ❌ W0 | ⬜ pending |
| 13-02-01 | 02 | 2 | VID-11 | T-13-03 | OSC popout resolves roster index to streamId | build | `cmake --build build --target JamWideJuce` | ✅ | ⬜ pending |
| 13-02-02 | 02 | 2 | VID-11 | — | TouchOSC template has VIDEO section | manual | Verify .tosc XML contains VIDEO group | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `companion/vitest.config.ts` — existing from Phase 12
- [x] `companion/package.json` — vitest dev dependency already installed
- [ ] `companion/src/__tests__/popout-url.test.ts` — stubs for VID-07 URL builder
- [ ] `companion/src/__tests__/popout-window.test.ts` — stubs for VID-07 window tracking
- [ ] `companion/src/__tests__/popout-page.test.ts` — stubs for VID-07 overlay behavior

*Test infrastructure exists from Phase 12. Only new test files needed.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Popup blocker interaction | VID-07 | Browser popup blocker behavior cannot be simulated in vitest | Open companion page, click roster pill, verify popout opens or banner appears |
| Multi-monitor popout positioning | VID-07 | Requires physical multi-monitor setup | Open popout window, drag to second monitor, verify video displays |
| TouchOSC hardware control | VID-11 | Requires physical OSC control surface | Load .tosc template on iPad, verify VIDEO section buttons trigger popouts |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 15s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
