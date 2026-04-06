---
phase: 10
slug: osc-remote-users-and-template
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-06
---

# Phase 10 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CMake CTest + manual verification (C++ plugin, no unit test framework yet) |
| **Config file** | CMakeLists.txt |
| **Quick run command** | `cmake --build build-juce --target JamWideJuce 2>&1 | tail -5` |
| **Full suite command** | `cmake --build build-juce --target JamWideJuce 2>&1` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build-juce --target JamWideJuce 2>&1 | tail -5`
- **After every plan wave:** Run `cmake --build build-juce --target JamWideJuce 2>&1`
- **Before `/gsd-verify-work`:** Full build must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 10-01-01 | 01 | 1 | OSC-04 | — | N/A | build | `cmake --build build-juce` | ✅ | ⬜ pending |
| 10-01-02 | 01 | 1 | OSC-04 | — | N/A | build | `cmake --build build-juce` | ✅ | ⬜ pending |
| 10-01-03 | 01 | 1 | OSC-05 | — | N/A | build+manual | `cmake --build build-juce` | ✅ | ⬜ pending |
| 10-02-01 | 02 | 2 | OSC-08 | — | N/A | build | `cmake --build build-juce` | ✅ | ⬜ pending |
| 10-02-02 | 02 | 2 | OSC-11 | — | N/A | manual | N/A | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- Existing build infrastructure covers all phase requirements (no new test framework needed)
- TouchOSC template (OSC-11) requires manual verification in TouchOSC app

*Existing infrastructure covers all phase requirements.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Remote user volume/pan/mute/solo via OSC | OSC-04 | Requires running NINJAM server + remote client + OSC client | 1. Connect two JamWide instances to same server. 2. Send `/JamWide/remote/1/volume 0.5` via OSC client. 3. Verify remote user volume changes. |
| Roster name broadcast on join/leave | OSC-05 | Requires live NINJAM session with multiple users | 1. Connect to server with OSC enabled. 2. Have second user join. 3. Monitor `/JamWide/remote/1/name` — should receive username string. |
| Connect/disconnect via OSC trigger | OSC-08 | Requires network server availability | 1. Send `/JamWide/session/connect "ninbot.com:2049"` via OSC client. 2. Verify plugin connects. 3. Send `/JamWide/session/disconnect 1.0`. 4. Verify plugin disconnects. |
| TouchOSC template import and use | OSC-11 | Requires TouchOSC app on iPad/phone | 1. Import assets/JamWide.tosc into TouchOSC. 2. Set host IP to plugin machine. 3. Verify faders, buttons, and labels map to correct OSC addresses. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
