---
phase: 9
slug: osc-server-core
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-06
---

# Phase 9 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest + custom test executables |
| **Config file** | `CMakeLists.txt` (test targets) |
| **Quick run command** | `cmake --build build --target JamWideJuce` |
| **Full suite command** | `ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target JamWideJuce`
- **After every plan wave:** Run `ctest --test-dir build --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 09-01-01 | 01 | 1 | OSC-01 | T-09-01 | Validate OSC address format, clamp float range | manual | Connect TouchOSC, move fader, verify plugin response | N/A | ⬜ pending |
| 09-01-02 | 01 | 1 | OSC-02 | T-09-02 | Only send changed values, echo suppression | manual | Move plugin fader, verify TouchOSC fader updates | N/A | ⬜ pending |
| 09-01-03 | 01 | 1 | OSC-06 | — | N/A | manual | Connect to NINJAM, verify BPM/BPI/beat on TouchOSC | N/A | ⬜ pending |
| 09-01-04 | 01 | 1 | OSC-07 | — | N/A | manual | Send OSC to metro addresses, verify plugin changes | N/A | ⬜ pending |
| 09-02-01 | 02 | 1 | OSC-03 | — | N/A | manual | Click status dot, change ports, verify connection | N/A | ⬜ pending |
| 09-02-02 | 02 | 1 | OSC-09 | — | N/A | manual | Visual inspection of footer dot in 3 states | N/A | ⬜ pending |
| 09-02-03 | 02 | 1 | OSC-10 | T-09-03 | Version migration preserves existing state | manual | Set OSC config, save DAW, reload, verify config | N/A | ⬜ pending |
| 09-00-01 | W0 | 0 | — | — | N/A | unit | `ctest --test-dir build -R osc_loopback --output-on-failure` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_osc_loopback.cpp` — basic OSCSender/OSCReceiver loopback on localhost (validates juce_osc linkage and port binding)
- [ ] CMakeLists.txt — add test_osc_loopback target linked against juce::juce_osc

*OSC testing is inherently manual (requires UDP network interaction with TouchOSC). The loopback test validates juce_osc linkage and basic send/receive only.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Fader in TouchOSC moves JamWide fader | OSC-01 | Requires physical TouchOSC device/app connected via UDP | 1. Open TouchOSC on phone/tablet. 2. Set host to 127.0.0.1:9000. 3. Move fader mapped to `/JamWide/local/1/volume`. 4. Verify JamWide fader moves within 100ms. |
| JamWide fader change reflects in TouchOSC | OSC-02 | Requires bidirectional UDP link with TouchOSC | 1. Open TouchOSC receiving on port 9001. 2. Move JamWide local ch1 fader. 3. Verify TouchOSC fader updates without oscillation. |
| Config dialog ports/IP persist | OSC-03, OSC-10 | Requires DAW session save/reload cycle | 1. Open OSC config, set non-default ports. 2. Save DAW session. 3. Close and reopen DAW. 4. Verify OSC config preserved. |
| Session telemetry broadcast | OSC-06 | Requires NINJAM connection + TouchOSC | 1. Connect to NINJAM server. 2. Verify BPM/BPI/beat/status update on TouchOSC. |
| Metro control via OSC | OSC-07 | Requires TouchOSC sending to metro addresses | 1. Send OSC to `/JamWide/metro/volume` from TouchOSC. 2. Verify metro volume changes in plugin. |
| Status indicator states | OSC-09 | Visual inspection of UI states | 1. Disable OSC → grey dot. 2. Enable with valid port → green dot. 3. Bind to used port → red dot. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
