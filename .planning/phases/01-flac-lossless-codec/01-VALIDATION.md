---
phase: 1
slug: flac-lossless-codec
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-07
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 single-header (or simple assert harness) |
| **Config file** | CMakeLists.txt — `JAMWIDE_BUILD_TESTS` option (currently OFF) |
| **Quick run command** | `cmake --build build --target jamwide_tests && ctest --test-dir build -R flac --output-on-failure` |
| **Full suite command** | `cmake -DJAMWIDE_BUILD_TESTS=ON -B build && cmake --build build && ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target jamwide_tests && ctest --test-dir build -R flac --output-on-failure`
- **After every plan wave:** Run `cmake -DJAMWIDE_BUILD_TESTS=ON -B build && cmake --build build && ctest --test-dir build --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 01-01-01 | 01 | 0 | CODEC-01, CODEC-02 | unit | `ctest -R flac_encoder` | ❌ W0 | ⬜ pending |
| 01-01-02 | 01 | 0 | CODEC-01, CODEC-02 | unit | `ctest -R flac_roundtrip` | ❌ W0 | ⬜ pending |
| 01-01-03 | 01 | 0 | CODEC-03, CODEC-05 | unit | `ctest -R codec_command` | ❌ W0 | ⬜ pending |
| 01-02-01 | 02 | 1 | CODEC-01 | unit | `ctest -R flac_encoder` | ❌ W0 | ⬜ pending |
| 01-02-02 | 02 | 1 | CODEC-02 | unit | `ctest -R flac_decoder` | ❌ W0 | ⬜ pending |
| 01-03-01 | 03 | 1 | CODEC-03 | unit | `ctest -R codec_command` | ❌ W0 | ⬜ pending |
| 01-03-02 | 03 | 1 | CODEC-04 | integration | manual | N/A | ⬜ pending |
| 01-04-01 | 04 | 2 | CODEC-03 | manual | manual — requires ImGui context | N/A | ⬜ pending |
| 01-04-02 | 04 | 2 | REC-01 | manual | manual — requires NJClient connected | N/A | ⬜ pending |
| 01-04-03 | 04 | 2 | REC-02 | manual | manual — requires NJClient connected | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_flac_codec.cpp` — stubs for CODEC-01, CODEC-02 (encode/decode/roundtrip)
- [ ] `tests/test_codec_command.cpp` — stubs for CODEC-03, CODEC-05 (command dispatch, default format)
- [ ] Test framework header included (Catch2 single-header or custom assert harness)
- [ ] CMakeLists.txt test target setup: `add_executable(jamwide_tests ...)` linking njclient + FLAC

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Codec switch at interval boundary only | CODEC-04 | Requires running plugin in live NINJAM session to observe timing | 1. Connect to session 2. Switch codec 3. Verify switch happens at next interval boundary (no mid-interval glitch) |
| Recording toggle enables OGG/WAV save | REC-01 | Requires NJClient connected to server | 1. Connect to session 2. Enable recording checkbox 3. Play audio 4. Verify files appear in recording directory |
| Recorded audio saved as OGG/WAV | REC-02 | Requires NJClient connected to server | 1. Record session 2. Stop 3. Verify .ogg and .wav files exist and are playable |
| Remote codec indicator shows correct codec | CODEC-02 | Requires two clients in same session | 1. Client A sends FLAC 2. Client B sees [FLAC] indicator on Client A's channel |
| Chat notification on codec change | CODEC-03 | Requires connected session with chat | 1. Switch codec 2. Verify chat message appears for all participants |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
