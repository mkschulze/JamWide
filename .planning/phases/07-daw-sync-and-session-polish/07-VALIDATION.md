---
phase: 7
slug: daw-sync-and-session-polish
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-05
---

# Phase 7 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Manual verification + JUCE AudioPluginHost |
| **Config file** | none — C++ plugin, no test framework config |
| **Quick run command** | `cmake --build build-clap --target JamWideJuce_VST3 2>&1 | tail -5` |
| **Full suite command** | `cmake --build build-clap --target JamWideJuce_All 2>&1 | tail -20` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build-clap --target JamWideJuce_VST3 2>&1 | tail -5`
- **After every plan wave:** Run `cmake --build build-clap --target JamWideJuce_All 2>&1 | tail -20`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 07-01-01 | 01 | 1 | SYNC-01 | — | N/A | build | `cmake --build build-clap --target JamWideJuce_VST3` | ✅ | ⬜ pending |
| 07-01-02 | 01 | 1 | SYNC-02 | — | N/A | build | `cmake --build build-clap --target JamWideJuce_VST3` | ✅ | ⬜ pending |
| 07-01-03 | 01 | 1 | SYNC-04 | — | N/A | build | `cmake --build build-clap --target JamWideJuce_VST3` | ✅ | ⬜ pending |
| 07-02-01 | 02 | 2 | SYNC-03 | — | N/A | build | `cmake --build build-clap --target JamWideJuce_VST3` | ✅ | ⬜ pending |
| 07-02-02 | 02 | 2 | SYNC-05 | — | N/A | build | `cmake --build build-clap --target JamWideJuce_VST3` | ✅ | ⬜ pending |
| 07-03-01 | 03 | 2 | RES-01 | — | N/A | file | `test -f .planning/references/VIDEO-FEASIBILITY.md` | ❌ W0 | ⬜ pending |
| 07-03-02 | 03 | 2 | RES-02 | — | N/A | file | `test -f .planning/references/OSC-EVALUATION.md` | ❌ W0 | ⬜ pending |
| 07-03-03 | 03 | 2 | RES-03 | — | N/A | file | `test -f .planning/references/MCP-ASSESSMENT.md` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- Existing infrastructure covers all phase requirements. No new test framework needed.
- Research deliverables (RES-01, RES-02, RES-03) verified by file existence.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Transport gating silences send when DAW stops | SYNC-01, SYNC-02 | Requires DAW host with transport controls | Load in REAPER/Logic, connect to server, play/stop transport, verify remote users don't hear audio when stopped |
| Live BPM/BPI change applies at interval boundary | SYNC-04 | Requires server BPM change during session | Connect, vote BPM change, verify BeatBar updates without reconnect |
| Session position displays correctly | SYNC-03 | Visual verification of UI components | Toggle info strip, verify interval count increments, elapsed time advances, beat position matches BeatBar |
| Standalone pseudo-transport works | SYNC-05 | Requires standalone app with server | Launch standalone, connect, verify BeatBar animates and audio flows |
| Sync button aligns interval to DAW measure | SYNC-01 | Requires DAW with matching BPM | Set DAW to server BPM, enable sync, press play, verify interval starts aligned |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
