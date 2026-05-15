---
phase: 14.3-native-video-foundation
created: 2026-05-15
---

# Phase 14.3 — Deferred Items

Items discovered during execution that are out-of-scope for the current
plan and were not auto-fixed. These are logged for follow-up.

## Out-of-scope discoveries during 14.3-02 execution

### test_encryption.cpp pre-existing build failure

**Discovered during:** 14.3-02 Task 2 (post-edit smoke build of full test suite)
**Status:** Pre-existing on base commit `dd9f6d1` — NOT introduced by 14.3-02
**File:** `tests/test_encryption.cpp:321`
**Error:**
```
error: use of undeclared identifier 'encrypt_payload_with_iv'
    EncryptedPayload enc = encrypt_payload_with_iv(plaintext, plaintext_len, key, iv);
```
**Root cause hypothesis:** test_encryption.cpp references an API
(`encrypt_payload_with_iv`) that no longer exists in `src/crypto/nj_crypto.h`.
The function was likely renamed or removed in commit `205c831` (Windows
BCrypt migration) without updating the test. Bypass the test target
(`ctest -E encryption`) or fix the test in a follow-up.

**Why deferred:** 14.3-02 plan scope excludes `tests/test_encryption.cpp` and
`src/crypto/`. Fixing this would violate the additive-only invariant and
push beyond the locked plan.

**Recommended fix path:** Either update the test to call the current crypto
API (`encrypt_payload`?) or restore `encrypt_payload_with_iv` if intended.
Track as a follow-up to phase 15 (encryption).

### test_flac_codec roundtrip pre-existing failure

**Discovered during:** 14.3-02 Task 3 (full ctest sweep)
**Status:** Pre-existing on base commit `dd9f6d1` — NOT introduced by 14.3-02
**File:** `tests/test_flac_codec.cpp`
**Result:** `Decoded 0 samples` — encode/decode round-trip produces empty output
**Failing subtests (3 of 8):**
- `Roundtrip mono: encode/decode preserves audio within 16-bit tolerance`
- `Roundtrip stereo: encode/decode preserves audio within 16-bit tolerance`
- `FlacEncoder advance/spacing matches VorbisEncoder calling convention`

**Why deferred:** 14.3-02 plan scope excludes `wdl/flacencdec.h` and FLAC
codec internals. The 5/8 sub-tests that pass exercise FlacEncoder
output/metadata; the 3 that fail involve a decode step that returns 0
samples. Likely a behavior regression introduced by 15.1-08 prealloc
changes (commit 9b9ce95) — last touch on `tests/test_flac_codec.cpp`.

**Recommended fix path:** Investigate FlacDecoder preallocated-queue init
flow under 15.1-08 — the empty-output result suggests the decoder's WDL
queue is not being consumed correctly post-prealloc.
