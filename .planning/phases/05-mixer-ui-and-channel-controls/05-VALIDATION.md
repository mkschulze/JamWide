---
phase: 5
slug: mixer-ui-and-channel-controls
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-04
---

# Phase 5 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | JUCE built-in test harness + manual DAW hosting |
| **Config file** | none — JUCE plugin requires DAW host for full validation |
| **Quick run command** | `cmake --build build --target JamWide_VST3 2>&1 | tail -5` |
| **Full suite command** | `cmake --build build --target JamWide_VST3 && pluginval --validate build/JamWide_artefacts/VST3/JamWide.vst3` |
| **Estimated runtime** | ~30 seconds (build) + ~10 seconds (pluginval) |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target JamWide_VST3 2>&1 | tail -5`
- **After every plan wave:** Run full suite command
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 40 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 05-01-xx | 01 | 1 | JUCE-06 | build | `cmake --build build` | ✅ | ⬜ pending |
| 05-01-xx | 01 | 1 | UI-04 | build+visual | `cmake --build build` | ✅ | ⬜ pending |
| 05-01-xx | 01 | 1 | UI-05 | build+visual | `cmake --build build` | ✅ | ⬜ pending |
| 05-02-xx | 02 | 1 | UI-06 | build+visual | `cmake --build build` | ✅ | ⬜ pending |
| 05-02-xx | 02 | 1 | UI-08 | build+state | `cmake --build build` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

*Existing infrastructure covers all phase requirements — JUCE build system and pluginval already configured.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| VU meters display real-time levels | UI-06 | Requires audio signal flow through DAW | Load plugin in DAW, play audio, verify meters animate |
| Fader drag interaction feels correct | UI-04 | Haptic/visual UX quality | Drag faders, verify smooth response and dB readout |
| Pan knob sweeps L-R correctly | UI-05 | Audio routing verification | Pan a channel, verify stereo field changes in DAW |
| Solo/Mute interact correctly | JUCE-06 | Multi-channel audio logic | Solo one channel, verify others mute; unsolo, verify restore |
| State persists across DAW save/load | UI-08 | Requires DAW project save/load cycle | Set mixer state, save DAW project, close/reopen, verify state |
| Metronome controls work | UI-05 | Requires active jam session | Connect to server, verify metronome vol/pan/mute work |

*Note: This phase is heavily UI/interaction — most verifications require visual and auditory confirmation in a DAW host.*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 40s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
