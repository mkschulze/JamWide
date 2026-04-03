---
phase: 04
slug: core-ui-panels
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-04
---

# Phase 04 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CMake CTest + pluginval + manual verification |
| **Config file** | CMakeLists.txt (JAMWIDE_BUILD_TESTS option) |
| **Quick run command** | `cmake --build build --target JamWideJuce_Standalone` |
| **Full suite command** | `cmake --build build && cmake --build build --target validate` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target JamWideJuce_Standalone` (build succeeds)
- **After every plan wave:** Build + launch standalone + manual smoke test
- **Before `/gsd:verify-work`:** Full build + pluginval + manual verification of all 6 requirements
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 04-01-XX | 01 | 1 | JUCE-05 | build | `cmake --build build --target JamWideJuce_Standalone` | N/A | ⬜ pending |
| 04-XX-XX | TBD | TBD | UI-01 | manual | Launch standalone, enter server, click connect | N/A | ⬜ pending |
| 04-XX-XX | TBD | TBD | UI-02 | manual | Connect, send message, verify chat display | N/A | ⬜ pending |
| 04-XX-XX | TBD | TBD | UI-03 | manual | Connect, verify status bar BPM/BPI/users | N/A | ⬜ pending |
| 04-XX-XX | TBD | TBD | UI-07 | manual | Click Browse, verify server list populates | N/A | ⬜ pending |
| 04-XX-XX | TBD | TBD | UI-09 | manual | Change codec dropdown, verify command sent | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- Existing infrastructure covers all phase requirements (pluginval CI, standalone build)
- No new test framework needed — UI phases are primarily manual verification

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Connection panel connect/disconnect | UI-01 | Interactive UI flow | Launch standalone, enter ninbot.com/anonymous, click Connect, verify status changes |
| Chat send/receive | UI-02 | Requires live NINJAM server | Connect to server, type message, verify display in chat panel |
| Status BPM/BPI display | UI-03 | Requires live server connection | Connect, verify BPM/BPI/user count shown in status bar |
| Server browser list | UI-07 | Requires network fetch | Click Browse Servers, verify list populates with server entries |
| Codec selector | UI-09 | Interactive UI + protocol | Select FLAC/Vorbis, verify SetEncoderFormatCommand dispatched |
| Custom LookAndFeel | JUCE-05 | Visual verification | Verify dark theme, custom-drawn components, no stock JUCE appearance |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
