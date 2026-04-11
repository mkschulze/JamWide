---
phase: 14
slug: midi-remote-control
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-11
---

# Phase 14 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Custom C++ test executables (matches existing test_flac_codec, test_osc_loopback pattern) |
| **Config file** | CMakeLists.txt (JAMWIDE_BUILD_TESTS section) |
| **Quick run command** | `cmake --build build-juce --target test_midi_mapping && ./build-juce/test_midi_mapping` |
| **Full suite command** | `cd build-juce && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build-juce --target test_midi_mapping && ./build-juce/test_midi_mapping`
- **After every plan wave:** Run `cd build-juce && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 14-01-01 | 01 | 1 | MIDI-01 | T-14-01 / input validation | CC values clamped 0-127, channel validated 1-16 | unit | `./build-juce/test_midi_mapping` | ❌ W0 | ⬜ pending |
| 14-01-02 | 01 | 1 | MIDI-01 | — | CC input changes APVTS parameter value | unit | `./build-juce/test_midi_mapping` | ❌ W0 | ⬜ pending |
| 14-01-03 | 01 | 1 | MIDI-01 | — | Parameter change produces CC feedback output | unit | `./build-juce/test_midi_mapping` | ❌ W0 | ⬜ pending |
| 14-01-04 | 01 | 1 | MIDI-01 | — | Echo suppression prevents feedback loop | unit | `./build-juce/test_midi_mapping` | ❌ W0 | ⬜ pending |
| 14-01-05 | 01 | 1 | MIDI-01 | T-14-02 / state validation | Mappings persist via state save/load with validation | unit | `./build-juce/test_midi_mapping` | ❌ W0 | ⬜ pending |
| 14-02-01 | 02 | 2 | MIDI-01 | — | MIDI Learn assigns CC to parameter | unit | `./build-juce/test_midi_mapping` | ❌ W0 | ⬜ pending |
| 14-02-02 | 02 | 2 | MIDI-01 | — | pluginval passes with 85 APVTS params | smoke | `cmake --build build-juce --target validate` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_midi_mapping.cpp` — stubs for MIDI-01 (CC dispatch, feedback, learn, persistence, echo suppression)
- [ ] CMakeLists.txt test target: `test_midi_mapping` — add juce_add_console_app similar to test_osc_loopback

*Existing infrastructure covers pluginval smoke test.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| MIDI Learn visual feedback (pulsing border, overlay text) | MIDI-01 | Requires visual inspection of UI rendering | Right-click fader → MIDI Learn → verify colored highlight appears, move CC → verify "CC 7 Ch 1" text shows |
| Standalone MIDI device selector works with real hardware | MIDI-01 | Requires physical MIDI device | Open standalone, connect MIDI controller, open MIDI config → select device → verify CC input works |
| Motorized controller fader tracking (no oscillation) | MIDI-01 | Requires motorized MIDI controller | Map CC to volume, move fader in JamWide → verify controller fader follows without jitter |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
