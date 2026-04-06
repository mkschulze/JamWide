---
phase: 11
slug: video-companion-foundation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-06
---

# Phase 11 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Custom C++ smoke tests (no framework; return 0/1 pattern) + Vite dev server for companion page |
| **Config file** | CMakeLists.txt `JAMWIDE_BUILD_TESTS` flag |
| **Quick run command** | `cd build-juce && cmake --build . --target JamWide_VST3 --parallel` |
| **Full suite command** | `cd build-juce && cmake --build . --parallel && ctest --test-dir . --output-on-failure` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build-juce && cmake --build . --target JamWide_VST3 --parallel`
- **After every plan wave:** Run `cd build-juce && cmake --build . --parallel && ctest --test-dir . --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 11-01-01 | 01 | 1 | VID-01, VID-02, VID-03 | — | N/A | build | `cmake --build build-juce --target JamWide_VST3 --parallel` | ✅ | ⬜ pending |
| 11-02-01 | 02 | 2 | VID-04 | — | N/A | build+dev | `cd companion && npm run build` | ❌ W0 | ⬜ pending |
| 11-03-01 | 03 | 3 | VID-05, VID-06 | — | N/A | build | `cmake --build build-juce --target JamWide_VST3 --parallel` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

*Existing infrastructure covers JUCE build validation. Companion page Wave 0 creates Vite project with `npm install`.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Video button launches browser with VDO.Ninja grid | VID-01 | Requires running plugin + browser interaction | Click Video button in connected session, verify browser opens |
| Room ID derived from server address | VID-02 | Requires NINJAM connection to verify room name | Connect to server, check VDO.Ninja room parameter matches expected hash |
| No audio from VDO.Ninja | VID-03 | Requires audio playback verification | Join video, confirm no duplicate audio in DAW |
| All participants visible in grid | VID-04 | Requires multiple users connected | Join from 2+ clients, verify all appear in grid |
| Privacy notice shown before first use | VID-05 | Requires UI interaction verification | Click Video for first time, verify modal appears |
| Browser warning for non-Chromium | VID-06 | Requires testing on Safari/Firefox | Set default browser to Safari, click Video, verify warning |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
