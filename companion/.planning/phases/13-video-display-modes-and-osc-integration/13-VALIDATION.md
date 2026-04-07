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

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | vitest 4.x (companion), cmake build (C++) |
| **Config file** | companion/vitest.config.ts |
| **Quick run command** | `cd companion && npx vitest run` |
| **Full suite command** | `cd companion && npx vitest run && cd .. && cmake --build build --target JamWideJuce` |
| **Estimated runtime** | ~15 seconds |

## Sampling Rate

- **After every task commit:** Run `cd companion && npx vitest run`
- **After every plan wave:** Run full suite command
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

## Wave 0 Requirements

- [x] `companion/vitest.config.ts` — existing from Phase 12
- [x] `companion/package.json` — vitest dev dependency already installed

## Validation Sign-Off

- [x] All tasks have automated verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] No watch-mode flags
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
