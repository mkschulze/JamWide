---
phase: 2
slug: juce-scaffolding
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-07
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | pluginval (plugin format validation) + CMake build verification |
| **Config file** | None needed for pluginval (CLI tool) |
| **Quick run command** | `cmake --build build --target JamWideJuce_VST3` |
| **Full suite command** | `cmake --build build && pluginval --validate-in-process --strictness-level 5 --validate "build/JamWideJuce_artefacts/Release/VST3/JamWide JUCE.vst3" && pluginval --validate-in-process --strictness-level 5 --validate "build/JamWideJuce_artefacts/Release/AU/JamWide JUCE.component"` |
| **Estimated runtime** | ~60 seconds (build) + ~30 seconds (pluginval per format) |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target JamWideJuce_VST3`
- **After every plan wave:** Run full suite command (build all + pluginval VST3 + AU)
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 90 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 02-01-01 | 01 | 1 | JUCE-01 | build | `cmake --build build --target JamWideJuce_VST3 JamWideJuce_AU` | ❌ W0 | ⬜ pending |
| 02-01-02 | 01 | 1 | JUCE-01 | pluginval | `pluginval --validate-in-process --strictness-level 5 --validate "build/JamWideJuce_artefacts/Release/VST3/JamWide JUCE.vst3"` | ❌ W0 | ⬜ pending |
| 02-01-03 | 01 | 1 | JUCE-02 | build | `cmake --build build --target JamWideJuce_Standalone` | ❌ W0 | ⬜ pending |
| 02-01-04 | 01 | 1 | JUCE-04 | pluginval | `pluginval --validate-in-process --strictness-level 5 --validate "..."` (lifecycle test) | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `libs/juce` — JUCE 8.0.12 submodule
- [ ] `libs/clap-juce-extensions` — clap-juce-extensions submodule
- [ ] `juce/JamWideJuceProcessor.h` / `.cpp` — AudioProcessor subclass with multi-bus layout
- [ ] `juce/JamWideJuceEditor.h` / `.cpp` — minimal AudioProcessorEditor placeholder
- [ ] `juce/NinjamRunThread.h` / `.cpp` — juce::Thread subclass
- [ ] CMakeLists.txt additions for JUCE targets (juce_add_plugin + clap_juce_extensions_plugin)
- [ ] `.github/workflows/juce-build.yml` — CI with pluginval

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Standalone launches with audio device selection | JUCE-02 | Requires audio hardware and GUI interaction | Launch standalone app, verify settings dialog appears, select audio device, confirm no audio output |
| Plugin loads in a DAW without crashing | JUCE-01 | DAW hosting is environment-specific | Load VST3/AU in DAW (Logic, Reaper), verify plugin appears and editor opens |
| NinjamRunThread stops cleanly on unload | JUCE-04 | Thread lifecycle tied to host plugin lifecycle | Load/unload plugin in DAW multiple times, verify no hangs or zombie threads |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 90s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
