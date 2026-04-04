---
phase: 6
slug: multichannel-output-routing
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-04
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | JUCE AudioProcessorTests + manual DAW validation |
| **Config file** | none — Wave 0 installs |
| **Quick run command** | `cd build && cmake --build . --target JamWide_VST3 2>&1 | tail -5` |
| **Full suite command** | `cd build && cmake --build . 2>&1` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && cmake --build . --target JamWide_VST3 2>&1 | tail -5`
- **After every plan wave:** Run `cd build && cmake --build . 2>&1`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 06-01-01 | 01 | 1 | MOUT-01 | build + grep | `grep -c "out_chan_index" juce/JamWideJuceProcessor.cpp` | ⬜ W0 | ⬜ pending |
| 06-01-02 | 01 | 1 | MOUT-01 | build | `cd build && cmake --build . --target JamWide_VST3` | ✅ | ⬜ pending |
| 06-02-01 | 02 | 1 | MOUT-02 | grep | `grep -c "SetRoutingMode\|quick.*assign" juce/ui/ConnectionBar.cpp` | ⬜ W0 | ⬜ pending |
| 06-02-02 | 02 | 2 | MOUT-03 | grep | `grep -c "config_metronome_channel\|metronome.*bus" juce/JamWideJuceProcessor.cpp` | ⬜ W0 | ⬜ pending |
| 06-02-03 | 02 | 2 | MOUT-04 | grep | `grep "bus.*0\|Main Mix" juce/JamWideJuceProcessor.cpp` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] Build system compiles with 17 stereo output buses active
- [ ] Existing routingSelector ComboBox renders without crash

*Existing infrastructure covers core build validation. DAW-specific multi-output testing is manual.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Per-user audio on separate DAW tracks | MOUT-01 | Requires DAW host with multi-output routing | Load in REAPER, enable outputs, verify separate tracks |
| Quick-assign updates routing display | MOUT-02 | UI interaction + DAW output verification | Click quick-assign, verify strips update and DAW tracks change |
| Metronome on dedicated output | MOUT-03 | Requires DAW playback and output monitoring | Enable metronome, verify only on last output bus in DAW |
| Main mix always on bus 0 | MOUT-04 | Requires DAW monitoring during routing changes | Change routing modes, verify bus 0 always has full mix |
| Multi-DAW compatibility | MOUT-05 | Requires testing in Logic Pro, REAPER, Bitwig | Load plugin in each DAW, enable multi-output, verify routing |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
