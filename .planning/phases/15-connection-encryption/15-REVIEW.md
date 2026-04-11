---
phase: 15-connection-encryption
reviewed: 2026-04-11T00:00:00Z
depth: standard
files_reviewed: 10
files_reviewed_list:
  - .github/workflows/juce-build.yml
  - CMakeLists.txt
  - src/core/mpb.h
  - src/core/netmsg.cpp
  - src/core/netmsg.h
  - src/core/njclient.cpp
  - src/core/njclient.h
  - src/crypto/nj_crypto.cpp
  - src/crypto/nj_crypto.h
  - tests/test_encryption.cpp
findings:
  critical: 2
  warning: 4
  info: 3
  total: 9
status: issues_found
---

# Phase 15: Code Review Report

**Reviewed:** 2026-04-11T00:00:00Z
**Depth:** standard
**Files Reviewed:** 10
**Status:** issues_found

## Summary

This phase adds AES-256-CBC payload encryption to the NINJAM protocol. The crypto primitives (`nj_crypto.cpp`) are cleanly implemented with solid guard rails: size checks, IV prepend, no-partial-plaintext on failure, and key scrubbing. The capability-negotiation protocol in `mpb.h` is clearly designed and the test suite has good coverage.

However, two critical issues require immediate attention: (1) debug `fopen("/tmp/jamwide.log")` calls in the production auth code path are completely unguarded — they expose the username and credential metadata to a world-readable temp file on every auth, and (2) the `memcpy` into `sendm->get_data()` after `set_size()` will silently corrupt or crash if the `WDL_HeapBuf` resize fails, because `set_size()` silently reverts to size 0 on allocation failure and `get_data()` then returns a null or stale pointer.

There are also four warnings: the in-place encrypt-on-send mutates a shared `Net_Message` that may be referenced from multiple call sites, the key-derivation function is SHA-256 without key stretching (a design-level concern documented by the team but worth flagging), the `encrypt_payload_with_iv` test-only function is exposed in the public header without a build-time guard, and the `tests_passed` counter in the test harness is never incremented in the `test_derive_key_known_vector` path on mismatch, causing the final result count to silently misreport failures.

---

## Critical Issues

### CR-01: Unguarded debug log exposes credential metadata to /tmp in production

**File:** `src/core/njclient.cpp:1104-1141`
**Issue:** Multiple `fopen("/tmp/jamwide.log", "a")` calls in the auth challenge handler log the username, `pass_len`, `client_caps` (including the encryption capability bit), and the license callback function pointer — all without any `#ifdef JAMWIDE_DEV_BUILD` guard. These calls execute in every production build (Release CI, notarized distribution). The file `/tmp/jamwide.log` is world-readable on macOS and Linux. On macOS, other processes owned by the same UID can read it without restriction. This violates the SEC-01 intent of the phase, which aims to protect credentials in transit — the password length and negotiation state are leaked locally instead. Similar unguarded log calls exist at lines 1122, 1136, 1190, 1202, and 2108.

**Fix:**
```cpp
// Wrap all debug-log blocks with the existing JAMWIDE_DEV_BUILD guard:
#ifdef JAMWIDE_DEV_BUILD
{
    FILE* lf = fopen("/tmp/jamwide.log", "a");
    if (lf) {
        fprintf(lf, "[NJClient] Auth challenge received\n");
        fprintf(lf, "[NJClient]   user='%s' pass_len=%d\n",
                m_user.Get(), (int)strlen(m_pass.Get()));
        // ... etc
        fclose(lf);
    }
}
#endif
```
Apply the same guard to all six `fopen("/tmp/jamwide.log")` sites and to the two `fprintf(stderr, ...)` calls at lines 916 and 935 in `Connect()` (which log the hostname and username unconditionally).

---

### CR-02: Unsafe memcpy after set_size() on allocation failure in Net_Connection::Run()

**File:** `src/core/netmsg.cpp:140-141`
**Issue:** The encryption path calls `sendm->set_size((int)enc.data.size())` then immediately does `memcpy(sendm->get_data(), enc.data.data(), enc.data.size())`. Looking at `Net_Message::set_size()` (netmsg.h:65-69):

```cpp
void set_size(int newsize) {
    m_hb.Resize(newsize);
    if (m_hb.GetSize() != newsize) m_hb.Resize(0);  // reverts to 0 on OOM
}
```

If `WDL_HeapBuf::Resize()` fails (out of memory), `set_size()` silently reverts the buffer to zero size, and `get_data()` returns either a null pointer or the old (now-invalid) buffer pointer. The subsequent `memcpy` writes `enc.data.size()` bytes into this location, producing a null-pointer dereference or heap corruption crash. An attacker on a resource-constrained host connecting to a server that sends large messages could trigger this.

**Fix:**
```cpp
sendm->set_size((int)enc.data.size());
if (sendm->get_size() != (int)enc.data.size()) {
    // Allocation failed — treat as encryption failure
    m_error = -4;
    break;
}
if (sendm->get_data() == nullptr) {
    m_error = -4;
    break;
}
memcpy(sendm->get_data(), enc.data.data(), enc.data.size());
```

The same pattern must be applied to the decrypt path at lines 236-238 where `retv->set_size((int)dec.data.size())` is followed by `memcpy(retv->get_data(), ...)`.

---

## Warnings

### WR-01: In-place mutation of a shared Net_Message during send

**File:** `src/core/netmsg.cpp:129-142`
**Issue:** The send loop encrypts the payload by mutating `sendm` (resizing its buffer and overwriting its data) before sending. `Net_Message` uses reference counting (`addRef` / `releaseRef`), so the same `Net_Message*` could be held by other code. If the caller retained a reference after `Send()` (e.g., to retry or inspect the message), they now see an encrypted blob instead of their original plaintext. The encryption is also applied every iteration where `m_msgsendpos < 0`, but since `m_msgsendpos` is set to 0 after the header is sent the guard is only effective on the first iteration — however, if the send is interrupted mid-stream and restarts from `m_msgsendpos = 0` (which the loop does not do), this could re-encrypt. More importantly, if `sendm->get_size()` is re-read after the mutation but the mutation failed (CR-02 scenario), the logic proceeds with stale state.

**Fix:** Encrypt into a separate temporary `Net_Message` rather than mutating the queued message in place. Alternatively, maintain an "already encrypted" flag per queued message so the mutation is idempotent and safe for retained references.

---

### WR-02: Key derivation uses single-iteration SHA-256 (no stretching)

**File:** `src/crypto/nj_crypto.cpp:111-121`, `src/crypto/nj_crypto.h:51-53`
**Issue:** `derive_encryption_key()` computes `SHA-256(password + challenge)` directly. SHA-256 is not a key derivation function — it is extremely fast and allows brute-force dictionary attacks at >1 billion guesses/second on commodity hardware. The 8-byte server challenge provides replay protection but is not a substitute for key stretching. A captured AUTH_USER ciphertext (or any session ciphertext) could be subjected to offline dictionary attack. The code comment cites "D-03: SHA-256(password + challenge). No PBKDF2." which suggests this was an intentional deferral, but it represents a meaningful security weakness for any server using common/weak passwords.

**Fix:** Replace with PBKDF2-HMAC-SHA256 (available in OpenSSL as `PKCS5_PBKDF2_HMAC`) with at least 100,000 iterations using the challenge as the salt:
```cpp
PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                  challenge, 8,
                  100000,
                  EVP_sha256(),
                  32, key_out);
```
If latency of PBKDF2 on the connect path is a concern, `scrypt` or `Argon2` are alternatives. This is flagged as a Warning (not Critical) because the primary threat (credential interception over the wire) is already mitigated by encryption, but offline attacks on captured session data remain feasible.

---

### WR-03: Test-only encrypt_payload_with_iv() has no build-time guard in public header

**File:** `src/crypto/nj_crypto.h:38-40`
**Issue:** The `encrypt_payload_with_iv()` function — documented "MUST NOT be used in production code" — is declared in the public header with no `#ifdef` guard. Any translation unit that includes `nj_crypto.h` can call it. The only barrier is a comment. In a production security module, a function that accepts an arbitrary caller-supplied IV for AES-CBC is a known misuse vector (IV reuse or IV-zeroing attacks).

**Fix:**
```cpp
#ifdef JAMWIDE_BUILD_TESTS
// Test-only variant: accepts explicit IV for deterministic known-vector tests.
// MUST NOT be used in production code — IV reuse breaks IND-CPA security.
EncryptedPayload encrypt_payload_with_iv(const unsigned char* plaintext, int plaintext_len,
                                          const unsigned char key[32],
                                          const unsigned char iv[16]);
#endif
```
Guard the definition in `nj_crypto.cpp` with the same `#ifdef`. The `CMakeLists.txt` already passes `-DJAMWIDE_BUILD_TESTS` is available via the option, or a dedicated `NJ_CRYPTO_TESTING` define can be used.

---

### WR-04: test_derive_key_known_vector() never increments tests_passed on mismatch path

**File:** `tests/test_encryption.cpp:75-101`
**Issue:** The `test_derive_key_known_vector()` function at line 96 calls `printf("FAILED: key mismatch\n  got: ...")` and returns without calling `FAIL(msg)`. The `FAIL` macro prints the message but does not increment any counter. The `tests_passed` counter is only incremented via `PASS()`. However, more specifically: there is **no** `tests_passed++` on the failure path — this is fine — but there is also no `return` after the `FAIL()` macro body. The `FAIL` macro is defined as a `do { printf(...); } while(0)` block that does not return. The test function then falls through to `return` at the end of the function naturally. This is subtle but the real issue is that the failure branch of `test_derive_key_known_vector` does not call `FAIL(msg)` at all on the key-mismatch path (lines 95-100); it does a raw `printf` instead of `FAIL(...)`. The result: if the known-vector test fails, the final summary prints "X/N tests passed" where N includes the test but the failure is only visible as a raw printf with no structured test count impact. The exit code at line 741 (`return (tests_passed == tests_run) ? 0 : 1;`) will correctly be non-zero since `tests_passed < tests_run`, but a CI system parsing test output for "FAILED:" lines will not see the structured failure marker.

**Fix:**
```cpp
// Replace the raw printf in test_derive_key_known_vector with FAIL():
if (memcmp(key, expected, 32) != 0) {
    char msg[256];
    snprintf(msg, sizeof(msg), "key mismatch\n  got:      %s\n  expected: %s",
             /* hex strings */);
    FAIL(msg);
    return;
}
PASS();
```

---

## Info

### IN-01: Large commented-out code block left in Net_Connection::Send()

**File:** `src/core/netmsg.cpp:278-328`
**Issue:** A large `#if 0 ... #endif` block (50 lines) containing a duplicate of the send loop remains in `Net_Connection::Send()`. This is dead code and creates confusion about whether it represents intended future behavior or was simply abandoned during refactoring.

**Fix:** Remove the `#if 0` block entirely. If the inline-flush behavior is ever desired it can be recovered from git history.

---

### IN-02: Magic number 32 used in encrypted payload size assertion in test

**File:** `tests/test_encryption.cpp:544`
**Issue:** The assertion `assert((int)enc.data.size() == 48)` at line 544 and related size assertions in the test hardcode numeric sizes (32, 48, etc.) without reference to the `NJ_CRYPTO_OVERHEAD` or `NJ_CRYPTO_IV_LEN` constants. If the IV size or block size constants are ever changed (unlikely but possible), these assertions will silently test the wrong expectation.

**Fix:**
```cpp
// Use constants for clarity:
assert((int)enc.data.size() == NJ_CRYPTO_IV_LEN + 2 * NJ_CRYPTO_BLOCK_LEN);  // 16 + 32 = 48
```

---

### IN-03: Unused CLAP artifact staging in juce-build.yml produces confusing "not found" output

**File:** `.github/workflows/juce-build.yml:117`
**Issue:** The macOS staging step copies `build/JamWideJuce_artefacts/Release/CLAP/JamWide.clap` but the CMake configure step sets `-DJAMWIDE_BUILD_CLAP=OFF`. The CLAP artifact will not exist and this `cp -r` will fail silently (it is not guarded with `|| true` or `if [ -d ]`). This does not affect the build result but will produce misleading log output and could fail the staging step if `cp` exits non-zero.

**Fix:**
```yaml
- name: Stage artifacts
  run: |
    mkdir -p staging
    cp -r "build/JamWideJuce_artefacts/Release/VST3/JamWide.vst3" staging/
    cp -r "build/JamWideJuce_artefacts/Release/AU/JamWide.component" staging/
    # CLAP build is disabled; skip staging until JAMWIDE_BUILD_CLAP=ON
    # cp -r "build/JamWideJuce_artefacts/Release/CLAP/JamWide.clap" staging/
    cp -r "build/JamWideJuce_artefacts/Release/Standalone/JamWide.app" staging/
```

---

_Reviewed: 2026-04-11T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
