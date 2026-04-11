---
phase: 15
slug: connection-encryption
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-11
---

# Phase 15 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (CMake) + custom test harness |
| **Config file** | `CMakeLists.txt` (test targets) |
| **Quick run command** | `cd build && ctest --test-dir . -R encryption --output-on-failure` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest -R encryption --output-on-failure`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 15-01-01 | 01 | 1 | SEC-01 | T-15-01 | Key derived from password+challenge via SHA-256 | unit | `ctest -R encryption_key_derivation` | ❌ W0 | ⬜ pending |
| 15-01-02 | 01 | 1 | SEC-01 | T-15-02 | AES-256-CBC encrypt/decrypt roundtrip | unit | `ctest -R encryption_roundtrip` | ❌ W0 | ⬜ pending |
| 15-01-03 | 01 | 1 | SEC-02 | T-15-03 | Random IV per message (no IV reuse) | unit | `ctest -R encryption_iv_unique` | ❌ W0 | ⬜ pending |
| 15-02-01 | 02 | 2 | SEC-03 | — | Capability negotiation in auth handshake | integration | `ctest -R encryption_negotiation` | ❌ W0 | ⬜ pending |
| 15-02-02 | 02 | 2 | SEC-03 | — | Fallback to unencrypted for legacy servers | integration | `ctest -R encryption_fallback` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_encryption.cpp` — stubs for SEC-01, SEC-02, SEC-03
- [ ] `tests/CMakeLists.txt` — encryption test target linked against OpenSSL
- [ ] OpenSSL dev headers available in build environment

*Existing infrastructure covers basic build tests; encryption-specific tests needed.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Legacy NINJAM server connection | SEC-03 | Requires external NINJAM server without encryption support | Connect to a legacy NINJAM server, verify connection succeeds without encryption |
| Encrypted audio playback quality | SEC-02 | Subjective audio quality check after decrypt | Join encrypted session, play audio, verify no artifacts from encrypt/decrypt cycle |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
