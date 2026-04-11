---
phase: 15-connection-encryption
fixed_at: 2026-04-11T00:00:00Z
review_path: .planning/phases/15-connection-encryption/15-REVIEW.md
iteration: 1
findings_in_scope: 6
fixed: 5
skipped: 1
status: partial
---

# Phase 15: Code Review Fix Report

**Fixed at:** 2026-04-11T00:00:00Z
**Source review:** .planning/phases/15-connection-encryption/15-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 6 (2 Critical, 4 Warning)
- Fixed: 5
- Skipped: 1

## Fixed Issues

### CR-01: Unguarded debug log exposes credential metadata to /tmp in production

**Files modified:** `src/core/njclient.cpp`
**Commit:** a22e216
**Applied fix:** Wrapped all 6 `fopen("/tmp/jamwide.log")` blocks and all 3 `fprintf(stderr)` calls in `Connect()` with `#ifdef JAMWIDE_DEV_BUILD` guards. These debug logs previously executed unconditionally in production builds, writing usernames, password lengths, client capability bits, license callback pointers, and encryption negotiation state to a world-readable temp file on every auth attempt.

### CR-02: Unsafe memcpy after set_size() on allocation failure in Net_Connection::Run()

**Files modified:** `src/core/netmsg.cpp`
**Commit:** 9c62ab5
**Applied fix:** Added allocation failure checks after `set_size()` in both the encrypt (send) and decrypt (receive) paths. On the send path: checks `get_size() != expected` or `get_data() == nullptr` before `memcpy`, sets `m_error = -4` and breaks on failure. On the receive path: checks `get_size() != expected` and `get_data() == nullptr`, properly releases the `retv` reference and sets `m_error = -5` before breaking. This prevents null-pointer dereference or heap corruption if `WDL_HeapBuf::Resize()` fails under memory pressure.

### WR-01: In-place mutation of a shared Net_Message during send

**Files modified:** `src/core/netmsg.cpp`
**Commit:** d234f4e
**Applied fix:** Changed the send-path encryption to create a new `Net_Message` for the encrypted payload instead of mutating the queued (refcounted) original in place. The new message is allocated, typed, sized (with allocation check from CR-02), filled with encrypted data, then swapped into the queue slot via pointer replacement. The original message reference is released. This prevents callers who retained a reference from seeing their plaintext replaced with an encrypted blob. The `sendm` local pointer is updated so the rest of the send loop operates on the new encrypted message.

### WR-03: Test-only encrypt_payload_with_iv() has no build-time guard in public header

**Files modified:** `src/crypto/nj_crypto.h`, `src/crypto/nj_crypto.cpp`, `CMakeLists.txt`
**Commit:** 3dfff52
**Applied fix:** Wrapped both the declaration (in `nj_crypto.h`) and definition (in `nj_crypto.cpp`) of `encrypt_payload_with_iv()` with `#ifdef JAMWIDE_BUILD_TESTS`. Added `target_compile_definitions(njclient PRIVATE JAMWIDE_BUILD_TESTS=1)` conditional on the `JAMWIDE_BUILD_TESTS` CMake option in `CMakeLists.txt`, so the function is compiled into `njclient` only when tests are being built. Production builds no longer expose the arbitrary-IV encryption function.

### WR-04: test_derive_key_known_vector() never increments tests_passed on mismatch path

**Files modified:** `tests/test_encryption.cpp`
**Commit:** 8f1250b
**Applied fix:** Replaced raw `printf("FAILED: ...")` calls with the structured `FAIL(msg)` macro in both `test_derive_key_known_vector()` (key mismatch path) and `test_known_vector_with_iv()` (ciphertext mismatch path). Added explicit `return;` after each `FAIL()` to prevent fall-through. The hex string formatting was refactored to build into stack buffers passed to `snprintf` for the `FAIL()` message. This ensures CI systems parsing for "FAILED:" markers see the structured output, and the test summary correctly reflects the failure.

## Skipped Issues

### WR-02: Key derivation uses single-iteration SHA-256 (no stretching)

**File:** `src/crypto/nj_crypto.cpp:111-121`
**Reason:** Intentional design deferral (D-03). The code explicitly documents "Per D-03: SHA-256(password + challenge). No PBKDF2." This was a deliberate trade-off decision made during phase planning. Replacing SHA-256 with PBKDF2 would change the wire protocol (both server and client must agree on derivation), impact connection latency, and break compatibility with any existing sessions. The review correctly flags this as a meaningful security weakness but acknowledges it is a Warning (not Critical) because the primary threat (credential interception over the wire) is already mitigated by encryption. This should be addressed in a dedicated follow-up phase with coordinated server/client protocol changes, not as a code-review fix.
**Original issue:** `derive_encryption_key()` computes `SHA-256(password + challenge)` directly. SHA-256 is not a key derivation function and allows brute-force dictionary attacks at high speed.

---

_Fixed: 2026-04-11T00:00:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
