---
phase: 3
slug: njclient-audio-bridge
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-07
---

# Phase 3 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | pluginval (plugin format validation) + manual connection test |
| **Config file** | None (pluginval is CLI; manual test uses live NINJAM server) |
| **Quick run command** | `cmake --build build --target JamWideJuce_VST3 && cmake --build build --target validate` |
| **Full suite command** | `cmake --build build && cmake --build build --target validate` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target JamWideJuce_VST3 && cmake --build build --target validate`
- **After every plan wave:** Run `cmake --build build && cmake --build build --target validate`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 03-01-xx | 01 | 1 | JUCE-03a | pluginval | `cmake --build build --target validate` | ✅ | ⬜ pending |
| 03-01-xx | 01 | 1 | JUCE-03b | manual | Launch standalone, connect to ninbot.com | N/A | ⬜ pending |
| 03-01-xx | 01 | 1 | JUCE-03c | manual | Two instances, verify bidirectional audio | N/A | ⬜ pending |
| 03-01-xx | 01 | 1 | JUCE-03d | manual | Connect, switch codec, verify audio | N/A | ⬜ pending |
| 03-01-xx | 01 | 1 | JUCE-03e | pluginval + manual | pluginval editor lifecycle + manual verify | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

Existing infrastructure covers all phase requirements. Phase 2 already provides:
- pluginval validate target in CMake
- VST3 build target

*No additional Wave 0 setup needed.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Connect to server and hear remote audio | JUCE-03b | Requires live NINJAM server and audio hardware | Launch standalone → connect to ninbot.com → verify remote audio on output |
| Local audio sent to server | JUCE-03c | Requires two instances and audio I/O | Run two standalone instances → connect both → verify bidirectional audio |
| Vorbis and FLAC codecs work | JUCE-03d | Requires codec switching during live session | Connect → change codec → verify audio continues without interruption |
| Editor close/reopen preserves audio | JUCE-03e | Requires interaction with plugin window | Connect → close editor → reopen → verify audio and network uninterrupted |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
