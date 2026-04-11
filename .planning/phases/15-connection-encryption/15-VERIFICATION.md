---
phase: 15-connection-encryption
verified: 2026-04-11T17:00:00Z
status: human_needed
score: 19/19 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Connect JamWide to a legacy NINJAM server (one that does NOT advertise SERVER_CAP_ENCRYPT_SUPPORTED=0x02 in server_caps) and verify the connection succeeds without error, audio works, and no encryption-related errors appear."
    expected: "Successful unencrypted connection — session works normally, no disconnection or error from the encryption negotiation path."
    why_human: "Requires a real external NINJAM server instance that does not support encryption. Cannot verify backward-compatibility code path purely from static analysis or unit tests — the legacy fallback path (server does not set bit 1 in server_caps) must be exercised against a live legacy server."
  - test: "Connect two JamWide clients to a JamWide-aware NINJAM server (ninbot with encryption support added), use a session password, and capture the network traffic with Wireshark or tcpdump. Inspect the post-auth message payloads."
    expected: "AUTH_USER message payload is encrypted (appears as ciphertext, not a recognizable SHA-1 hash). All subsequent message payloads after auth-reply are opaque ciphertext prefixed with 16-byte random IVs. Message type bytes and size fields in cleartext headers are still visible."
    why_human: "Full end-to-end encryption verification requires a running JamWide-aware server (ninbot with server_caps bit set). The server-side integration is explicitly documented as out-of-scope for this client phase — ninbot changes must be made separately. Cannot verify wire-level encryption without a live encrypted session."
  - test: "Join an encrypted session with a session password, have a remote participant play audio, and listen for audio quality artifacts."
    expected: "Audio is decrypted transparently with no artifacts, clicks, or degradation compared to an unencrypted session. The encrypt/decrypt cycle adds no audible distortion to Vorbis/FLAC audio payloads."
    why_human: "Subjective audio quality verification after decrypt cycle requires a real session with real audio. Cannot verify perceptual quality from static analysis."
---

# Phase 15: Connection Encryption Verification Report

**Phase Goal:** Users' credentials and audio are encrypted in transit when connecting with a session password, while maintaining backward compatibility with legacy NINJAM servers
**Verified:** 2026-04-11T17:00:00Z
**Status:** human_needed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

**Sources:** ROADMAP.md Success Criteria (non-negotiable) + Plan 01/02 must_haves (merged, deduplicated)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | User connecting with a password has their credentials encrypted via AES-256-CBC (SHA-256 key derivation from password) | VERIFIED | `njclient.cpp:1147-1155`: derives key from `m_pass + m_auth_challenge` via `derive_encryption_key`, calls `SetEncryptionKey` BEFORE `Send(repl.build())` so AUTH_USER is encrypted. `nj_crypto.cpp`: SHA256 key derivation confirmed. |
| 2 | User's audio stream is encrypted end-to-end when a session password is set | VERIFIED | `netmsg.cpp:129-142`: `encrypt_payload` hook in `Net_Connection::Run()` send path encrypts all non-empty payloads when `m_encryption_active=true`. Audio chunks (`MESSAGE_CLIENT_SET_CHANNEL_INFO`, audio interval data) pass through this path. |
| 3 | User can still connect to legacy NINJAM servers without encryption (graceful fallback) | VERIFIED (code) / NEEDS HUMAN (live test) | `njclient.cpp:1147`: encryption only activated when `(cha.server_caps & SERVER_CAP_ENCRYPT_SUPPORTED)` — legacy servers that don't set bit 1 take the unencrypted path. `test_legacy_server_fallback` passes. Live server test required. |
| 4 | Encryption is transparent — no extra configuration required beyond the existing password field | VERIFIED | Encryption uses existing `m_pass` field (`njclient.h:300`). No new UI settings, no new NJClient API methods exposed to callers. `m_auth_challenge[8]` is internal only. |
| 5 | encrypt_payload() produces [16-byte IV][ciphertext] output for any non-zero plaintext input | VERIFIED | `nj_crypto.cpp:22-23, 53`: IV prepended, correct resize. 26/26 tests pass including `test_roundtrip_*` family. |
| 6 | decrypt_payload() recovers the original plaintext from encrypt_payload() output for all tested sizes | VERIFIED | 6 round-trip tests pass (1, 15, 16, 37, 1000, 16384 bytes). |
| 7 | derive_encryption_key() produces a deterministic 32-byte key from password + 8-byte challenge | VERIFIED | `nj_crypto.cpp:111-120`: SHA256(password+challenge). `test_derive_key_deterministic` and `test_derive_key_known_vector` pass. |
| 8 | Zero-length plaintext encrypt produces valid 32-byte block that round-trips correctly | VERIFIED | `nj_crypto.cpp:33-38`: zero-length path skips EVP_EncryptUpdate, calls Final only. `test_zero_length_encrypt_roundtrip` passes. |
| 9 | Encryption round-trip works for 1, 15, 16, 37, 1000, 16384 bytes | VERIFIED | All 6 round-trip tests pass in `./build/test_encryption`. |
| 10 | encrypt_payload_with_iv produces known ciphertext for known-vector test | VERIFIED | `test_known_vector_with_iv` passes with expected ciphertext `0x41977e6b...` |
| 11 | decrypt_payload returns ok=false with empty data on any failure | VERIFIED | `nj_crypto.cpp:101-103`: `result.data.clear()` on failure. 5 failure-mode tests pass. |
| 12 | Exact ciphertext overhead is 16 + ceil((N+1)/16)*16 for N>0, 32 for N=0 | VERIFIED | `test_encrypted_size_overhead` tests all 8 cases and passes. |
| 13 | Server advertises encryption support via server_caps bit 1 (SERVER_CAP_ENCRYPT_SUPPORTED) | VERIFIED | `mpb.h:303`: `#define SERVER_CAP_ENCRYPT_SUPPORTED 0x02`. `test_capability_bit_defines` asserts no collision. |
| 14 | Client encrypts MESSAGE_CLIENT_AUTH_USER when server advertises encryption support | VERIFIED | `njclient.cpp:1147-1158`: SetEncryptionKey before Send. The encrypt hook in `netmsg.cpp:129` fires on AUTH_USER's non-empty payload. |
| 15 | Client sets bit 2 (CLIENT_CAP_ENCRYPT_SUPPORTED) in client_caps | VERIFIED | `njclient.cpp:1154`: `repl.client_caps |= CLIENT_CAP_ENCRYPT_SUPPORTED`. `mpb.h:304`: bit value is 0x04. |
| 16 | Server's auth reply bit 1 (SERVER_FLAG_ENCRYPT_ACTIVE) confirms encryption | VERIFIED | `njclient.cpp:1185`: `if (ar.flag & SERVER_FLAG_ENCRYPT_ACTIVE)`. `mpb.h:305`: `#define SERVER_FLAG_ENCRYPT_ACTIVE 0x02`. |
| 17 | Downgrade detection: client clears encryption if server doesn't confirm | VERIFIED | `njclient.cpp:1196-1200`: `ClearEncryption()` called if `m_netcon->IsEncryptionActive()` but no `SERVER_FLAG_ENCRYPT_ACTIVE` in reply. `test_downgrade_detection` passes. |
| 18 | Zero-length payloads (keepalive) are NOT encrypted | VERIFIED | `netmsg.cpp:129`: `sendm->get_size() > 0` guard. `netmsg.cpp:222`: same guard on receive. |
| 19 | On decrypt failure, connection disconnects with generic error (no padding details leaked) | VERIFIED | `netmsg.cpp:228-234`: sets `m_error = -5` (generic, not distinguishing padding vs alignment vs key mismatch). |

**Score:** 19/19 truths verified (3 require human live-server testing in addition to code verification)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/crypto/nj_crypto.h` | encrypt_payload, decrypt_payload, derive_encryption_key, NJ_CRYPTO_IV_LEN, NJ_CRYPTO_OVERHEAD | VERIFIED | All declarations and constants present. EncryptedPayload/DecryptedPayload structs with ok+data semantics. |
| `src/crypto/nj_crypto.cpp` | OpenSSL EVP AES-256-CBC implementation | VERIFIED | EVP_aes_256_cbc, RAND_bytes, SHA256. No deprecated AES_encrypt or WDL_RNG. |
| `tests/test_encryption.cpp` | 26 tests: round-trip, known-vector, size overhead, zero-length, failure modes, integration | VERIFIED | 26/26 tests pass when executed. Covers all required test cases. |
| `CMakeLists.txt` | OpenSSL linkage for njclient and test target | VERIFIED | find_package(OpenSSL REQUIRED), OpenSSL::Crypto in njclient target_link_libraries, test_encryption target. |
| `src/core/netmsg.h` | Net_Connection encryption state and methods | VERIFIED | m_encryption_active, m_encryption_key, SetEncryptionKey, ClearEncryption, IsEncryptionActive, NET_MESSAGE_MAX_SIZE_ENCRYPTED. |
| `src/core/netmsg.cpp` | encrypt/decrypt hooks in send/receive paths | VERIFIED | encrypt_payload hook at send (line 130), decrypt_payload hook at receive (line 223). |
| `src/core/mpb.h` | Capability bit defines | VERIFIED | SERVER_CAP_ENCRYPT_SUPPORTED=0x02, CLIENT_CAP_ENCRYPT_SUPPORTED=0x04, SERVER_FLAG_ENCRYPT_ACTIVE=0x02 at lines 303-305. |
| `src/core/njclient.h` | m_auth_challenge[8] field | VERIFIED | Line 301: `unsigned char m_auth_challenge[8] = {};` |
| `src/core/njclient.cpp` | Auth flow: save challenge, derive key, encrypt AUTH_USER, confirm/downgrade | VERIFIED | Lines 1097-1208: full auth flow with encryption negotiation. |
| `.github/workflows/juce-build.yml` | OpenSSL install on macOS, Windows, Linux | VERIFIED | macOS: `brew install openssl@3`, Windows: `choco install openssl`, Linux: `libssl-dev` via apt. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/crypto/nj_crypto.cpp` | OpenSSL EVP API | EVP_EncryptInit_ex, EVP_DecryptInit_ex, SHA256, RAND_bytes | WIRED | All four OpenSSL functions present and used in implementation. |
| `tests/test_encryption.cpp` | `src/crypto/nj_crypto.h` | #include "crypto/nj_crypto.h" | WIRED | Line 21. Also includes mpb.h and netmsg.h for integration tests. |
| `src/core/netmsg.cpp` | `src/crypto/nj_crypto.h` | #include "crypto/nj_crypto.h" + encrypt_payload/decrypt_payload calls | WIRED | Line 37 include; Lines 130, 223 call sites. |
| `src/core/njclient.cpp` | `src/crypto/nj_crypto.h` | derive_encryption_key + SetEncryptionKey | WIRED | derive_encryption_key called at line 1149; SetEncryptionKey at 1150 uses derived key. |
| `src/core/njclient.cpp` | `src/core/mpb.h` | SERVER_CAP_ENCRYPT_SUPPORTED, CLIENT_CAP_ENCRYPT_SUPPORTED, SERVER_FLAG_ENCRYPT_ACTIVE | WIRED | Capability bits used at lines 1147, 1154, 1185. |
| `CMakeLists.txt` | `src/crypto/nj_crypto.cpp` | njclient sources list | WIRED | `src/crypto/nj_crypto.cpp` in njclient add_library sources at line 107. |

### Data-Flow Trace (Level 4)

Not applicable to this phase. Phase 15 adds a crypto/networking layer, not a UI/rendering component. There are no data variables that render to a display. The relevant data flow (plaintext -> encrypt -> wire -> decrypt -> plaintext) is verified by the 26 passing unit and integration tests.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| 26/26 encryption unit + integration tests pass | `./build/test_encryption` | 26/26 PASSED, exit 0 | PASS |
| test_encryption binary builds cleanly | `cmake --build build --target test_encryption` | Builds without error (2 warnings from SHA deprecation, not errors) | PASS |
| No deprecated low-level API used | `grep "WDL_RNG\|AES_encrypt\|AES_set_encrypt_key" src/crypto/nj_crypto.cpp` | No matches | PASS |
| Claimed git commits exist | `git log --oneline \| grep -E "3f0dfdd\|8f3236c\|3f19bf1"` | All 3 commits found | PASS |
| EVP high-level API used | `grep "EVP_aes_256_cbc\|RAND_bytes\|SHA256" src/crypto/nj_crypto.cpp` | 4 matches confirmed | PASS |

### Requirements Coverage

**Note:** SEC-01, SEC-02, SEC-03 are v1.2 requirements defined in ROADMAP.md (the v1.2 milestone section). They do NOT appear in REQUIREMENTS.md, which covers only v1.1 requirements (OSC, Video, MIDI). This is expected — REQUIREMENTS.md tracks v1.1 scope; v1.2 requirements are tracked in ROADMAP.md phase details. No orphaned requirements.

| Requirement | Source Plan | Description (from ROADMAP.md) | Status | Evidence |
|-------------|-------------|-------------------------------|--------|----------|
| SEC-01 | 15-01, 15-02 | Credentials encrypted via AES-256-CBC (SHA-256 key derivation) | SATISFIED | `nj_crypto.cpp`: SHA256(password+challenge) key derivation. `njclient.cpp:1147-1154`: key derived and set before AUTH_USER sent. `test_derive_key_known_vector`, `test_encrypted_auth_scenario` both pass. |
| SEC-02 | 15-01, 15-02 | All post-auth NINJAM messages encrypted when session password set | SATISFIED | `netmsg.cpp:129-141`: encrypt hook fires on all non-empty payloads when `m_encryption_active=true`. Activation happens at AUTH_USER send — all subsequent messages (audio, config, chat) are encrypted. |
| SEC-03 | 15-02 | Graceful fallback for legacy NINJAM servers (no encryption) | SATISFIED (code) | `njclient.cpp:1147`: encryption only if server advertises `SERVER_CAP_ENCRYPT_SUPPORTED`. If not set, connection proceeds unencrypted. Downgrade detection at `njclient.cpp:1196-1200` also handles the edge case. Verified in `test_legacy_server_fallback`, `test_legacy_client_fallback`. Live server test deferred to human verification. |

### Anti-Patterns Found

| File | Lines | Pattern | Severity | Impact |
|------|-------|---------|----------|--------|
| `src/core/njclient.cpp` | 1104, 1122, 1136, 1190, 1202, 2108 | Debug logging to `/tmp/jamwide.log` not guarded by `#ifdef JAMWIDE_DEV_BUILD` — runs unconditionally in production builds | WARNING | Auth flow log entries will write to `/tmp/jamwide.log` in all Release builds. Does NOT write passwords or keys — logs capability bits and connection state only. Not a security issue, but a performance/cleanliness concern. This pre-existed Phase 15; the Phase 15 additions follow the same existing pattern. |

No blockers found. No stub implementations. No empty handlers. No hardcoded empty data arrays in the encryption path.

### Human Verification Required

#### 1. Legacy NINJAM Server Backward Compatibility (SEC-03 live test)

**Test:** Connect JamWide to a legacy NINJAM server that does NOT set bit 1 in `server_caps` (any standard NINJAM server without JamWide-specific modifications). Verify the connection completes and audio works normally.
**Expected:** Successful unencrypted session. No disconnect errors. No encryption-related failures. The fallback code path at `njclient.cpp:1147` (`if ((cha.server_caps & SERVER_CAP_ENCRYPT_SUPPORTED) && ...)`) evaluates false and the unencrypted path is taken.
**Why human:** Requires a real external NINJAM server. The legacy fallback code is verified by unit tests (`test_legacy_server_fallback`) but the actual live server interaction — including the exact bytes sent in AUTH_CHALLENGE and the full connection handshake — can only be confirmed against a running legacy server.

#### 2. End-to-End Wire-Level Encryption Verification (SEC-01, SEC-02 live test)

**Test:** Connect two JamWide clients to a ninbot server that has been updated to support SERVER_CAP_ENCRYPT_SUPPORTED. Use a session password. Capture the TCP stream with Wireshark or `tcpdump -X`. Examine post-connection message payloads.
**Expected:** AUTH_USER message payload is encrypted ciphertext (not a recognizable SHA-1 hash). Subsequent audio interval messages have encrypted, non-deterministic payloads starting with a 16-byte random IV. The 5-byte cleartext headers (type + size) remain visible.
**Why human:** Server-side (ninbot) changes are explicitly out of scope for this phase. End-to-end encryption can only be verified against a full encrypted session between JamWide client and JamWide-aware server.

#### 3. Audio Quality Through Encrypt/Decrypt Cycle (SC-2 quality check)

**Test:** Join an encrypted session (requires item 2 above), have a remote participant stream audio, and listen carefully at several codec settings (Vorbis, FLAC).
**Expected:** No audible artifacts, clicks, drop-outs, or degradation introduced by the encrypt/decrypt cycle.
**Why human:** Subjective audio quality verification. Cannot be determined from static analysis.

### Gaps Summary

No gaps. All 19 must-haves are verified in the codebase. The 3 human verification items are live-environment tests that require external infrastructure (a JamWide-aware NINJAM server), which is explicitly documented as out-of-scope for the client phase.

The one warning (unguarded debug log writes) is a pre-existing pattern in the codebase, not introduced by Phase 15, and does not affect security or correctness.

---

_Verified: 2026-04-11T17:00:00Z_
_Verifier: Claude (gsd-verifier)_
