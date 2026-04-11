# Phase 15: Connection Encryption - Research

**Researched:** 2026-04-11
**Domain:** AES-256-CBC encryption of NINJAM protocol traffic via OpenSSL EVP
**Confidence:** HIGH

## Summary

This phase adds transparent encryption to all post-auth NINJAM protocol traffic when a session password is set. The implementation has a proven reference in the Ninja-VST3-Plugin (`WebRTCSession.cpp:594-685`) that uses the exact same OpenSSL EVP API pattern (AES-256-CBC, SHA-256 key derivation, random IV per message). The NINJAM protocol's message framing (`[type:1][size:4][payload:N]`) naturally supports payload-only encryption with minimal overhead (16-byte IV + up to 16-byte PKCS#7 padding per message).

The primary complexity is not the cryptography itself -- the reference implementation provides a direct template -- but the integration into `Net_Connection`'s send/receive pipeline, the protocol negotiation via existing capability flags, and cross-platform OpenSSL linking for macOS universal builds, Windows CI, and Linux.

The `server_caps` and `client_caps` fields in the auth handshake have ample unused bits for encryption negotiation. The existing 8-byte server challenge doubles as salt for key derivation (per D-03), eliminating any need for additional key exchange.

**Primary recommendation:** Implement encryption as an optional layer inside `Net_Connection` with encrypt-on-send/decrypt-on-receive hooks, negotiated via a single capability bit exchange during the existing auth handshake, using OpenSSL EVP for AES-256-CBC with `RAND_bytes` for IV generation and `SHA256()` for key derivation.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** All post-auth traffic encrypted. After encryption is negotiated, every subsequent NINJAM message (audio chunks, config/topic changes, chat, user join/leave) is encrypted. No selective encryption -- all or nothing per session.
- **D-02:** AES-256-CBC via OpenSSL EVP API. Same algorithm and API as Ninja-VST3-Plugin reference (`EVP_aes_256_cbc`, `EVP_EncryptInit_ex`, `EVP_EncryptUpdate`, `EVP_EncryptFinal_ex`). PKCS#7 padding automatic via EVP.
- **D-03:** Key derivation: SHA-256(password + salt) -> 32-byte key. Salt derived from the server's 8-byte challenge (from `MESSAGE_SERVER_AUTH_CHALLENGE`). Same pattern as reference plugin. No PBKDF2 -- NINJAM passwords are session-specific, not stored credentials.
- **D-04:** Random IV per message. 16 bytes via OpenSSL `RAND_bytes` for each encrypted message. IV prepended to ciphertext in the payload. Stateless -- no counter sync needed between client and server.
- **D-05:** Capability flag in existing auth handshake. Client sets an 'encryption supported' bit in `client_caps` (in `MESSAGE_CLIENT_AUTH_USER`). Server responds with 'encryption active' bit in `MESSAGE_SERVER_AUTH_REPLY`. If server doesn't understand the bit, it ignores it -- zero overhead for legacy servers.
- **D-06:** Silent fallback to unencrypted for legacy servers. If server doesn't set the encryption bit in its reply, JamWide connects normally without encryption. No warning to user.
- **D-07:** Payload-only encryption. Encrypt the message payload, keep the type (1 byte) and size (4 bytes) headers in cleartext. Net_Connection can still parse message boundaries and route by type.
- **D-08:** IV prepended to payload. Each encrypted message's payload is: [16-byte IV][ciphertext]. Receiver reads first 16 bytes as IV, remainder as ciphertext.
- **D-09:** Both client and server (ninbot) support encryption in this phase. Since encryption lives in `Net_Connection` (shared by client and server code), both sides get it naturally.
- **D-10:** JamWide-to-JamWide encryption only. This is a JamWide-specific NINJAM protocol extension. Other NINJAM clients connect unencrypted via the fallback mechanism.

### Claude's Discretion
- Exact bit position in the capability flags (which bit number for encryption)
- OpenSSL linking strategy per platform (system lib on macOS, bundled on Windows, pkg-config on Linux)
- Whether to add HMAC authentication on top of AES-CBC (encrypt-then-MAC pattern) for tamper detection
- Error handling when decryption fails mid-session (connection drop vs retry)
- Whether the size field in the header includes the IV length or just the ciphertext
- CMakeLists.txt changes needed to link OpenSSL across all build targets

### Deferred Ideas (OUT OF SCOPE)
- HMAC/encrypt-then-MAC for tamper detection -- could be added later if needed
- TLS/DTLS as alternative to application-layer encryption -- would require replacing JNetLib transport
- Per-user encryption keys (each participant derives independently) -- current design uses shared session key from password
- Key rotation during long sessions -- AES-CBC with random IV per message is sufficient for session-length security
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SEC-01 | User connecting with a password has their credentials encrypted via AES-256-CBC (SHA-256 key derivation from password) | OpenSSL EVP API verified, reference implementation at WebRTCSession.cpp:594-685, key derivation pattern SHA256(password + challenge) confirmed |
| SEC-02 | User's audio stream is encrypted end-to-end when a session password is set | Payload-only encryption in Net_Connection::Send()/Run() covers all message types including audio chunks (MESSAGE_SERVER/CLIENT_DOWNLOAD/UPLOAD_INTERVAL_WRITE) |
| SEC-03 | User can still connect to legacy NINJAM servers without encryption (graceful fallback) | Capability bit in client_caps/server flag, unused bits verified available (client_caps bit 2, flag bit 1), legacy servers ignore unknown bits |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| OpenSSL | 3.6.2 (macOS), 3.0+ (CI) | AES-256-CBC encryption, SHA-256, RAND_bytes | Locked decision D-02; same API as reference implementation; industry standard |

[VERIFIED: `openssl version` on dev machine shows OpenSSL 3.6.2]
[VERIFIED: CI Linux installs `libcurl4-openssl-dev` which includes OpenSSL headers]

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| juce_cryptography (SHA256 class) | Already linked | Backup SHA-256 if needed | Only if OpenSSL SHA256() proves problematic in JUCE context |

[VERIFIED: juce_cryptography already in target_link_libraries for JamWideJuce]

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| OpenSSL EVP | Apple CommonCrypto (macOS) + BCrypt (Windows) | No universal build issues, but triples code surface; LOCKED OUT by D-02 |
| OpenSSL EVP | JUCE BlowFish | Wrong algorithm (BlowFish != AES); LOCKED OUT by D-02 |
| OpenSSL RAND_bytes | WDL_RNG | WDL_RNG is not cryptographically secure; MUST use RAND_bytes for IVs |

### Installation

**macOS (dev):**
```bash
brew install openssl@3
```

**CI changes needed:**
```yaml
# macOS CI - add to build-macos steps:
- name: Install OpenSSL
  run: brew install openssl@3

# Windows CI - add to build-windows steps:
- name: Install OpenSSL
  run: choco install openssl --no-progress

# Linux CI - add or verify in build-linux steps:
- name: Install OpenSSL dev
  run: sudo apt-get install -y libssl-dev
```

## Architecture Patterns

### Encryption Layer Integration

```
Net_Connection (netmsg.h/cpp)
  |
  +-- m_encryption_active (bool, set after auth negotiation)
  +-- m_encryption_key[32] (derived from SHA-256(password + challenge))
  |
  +-- Send(msg)
  |     |-- if m_encryption_active:
  |     |     1. Build plaintext payload from msg
  |     |     2. Generate 16-byte random IV via RAND_bytes
  |     |     3. Encrypt payload with AES-256-CBC
  |     |     4. Replace payload with [IV][ciphertext]
  |     |     5. Update size field to encrypted payload size
  |     |-- Send via JNL_Connection (unchanged)
  |
  +-- Run() -> Net_Message*
        |-- Receive message header (type + size, cleartext)
        |-- Receive payload
        |-- if m_encryption_active:
        |     1. Read first 16 bytes as IV
        |     2. Decrypt remaining bytes with AES-256-CBC
        |     3. Replace payload with decrypted plaintext
        |     4. Update size to plaintext size
        |-- Return message (unchanged interface)
```

### Recommended Module Structure

```
src/
  core/
    netmsg.h          # Add encryption state + key to Net_Connection
    netmsg.cpp         # Add encrypt/decrypt hooks in Send()/Run()
    njclient.h         # No changes needed
    njclient.cpp       # Derive key after auth success, set encryption_active
    mpb.h              # Add capability bit defines
    mpb.cpp            # No changes to parse/build (bits already in unused range)
  crypto/
    nj_crypto.h        # encrypt_payload(), decrypt_payload(), derive_key()
    nj_crypto.cpp       # OpenSSL EVP implementation
CMakeLists.txt         # find_package(OpenSSL), link to njclient
```

### Pattern 1: Capability Bit Negotiation
**What:** Client advertises encryption support via a bit in `client_caps`; server responds with activation bit in `flag`.
**When to use:** During the existing auth handshake, before any post-auth traffic.

Current bit usage in `client_caps` (32-bit int):
- Bit 0: License agreement accepted
- Bit 1: Client version field present (server requires this)
- Bits 2-31: UNUSED [VERIFIED: mpb.cpp:550-553 serializes all 32 bits]

Current bit usage in `server_caps` (32-bit int in auth challenge):
- Bit 0: License agreement present
- Bits 8-15: Keepalive interval
- Bits 1-7, 16-31: UNUSED [VERIFIED: mpb.cpp:50-53, njclient.cpp:1093]

Current bit usage in `flag` (char in auth reply):
- Bit 0: Authentication success
- Bits 1-7: UNUSED [VERIFIED: mpb.cpp:134, njclient.cpp:1160]

**Recommendation:** Use `client_caps` bit 2 for "encryption supported" and `flag` bit 1 for "encryption active". [VERIFIED: these bits are unused in the codebase]

```cpp
// In mpb.h
#define CLIENT_CAP_ENCRYPT_SUPPORTED  0x04  // bit 2
#define SERVER_FLAG_ENCRYPT_ACTIVE    0x02  // bit 1 in auth reply flag
```

### Pattern 2: Key Derivation After Auth
**What:** After successful auth handshake, both client and server derive the same 32-byte key from SHA-256(password + challenge).
**When to use:** Immediately after `MESSAGE_SERVER_AUTH_REPLY` with success + encryption active bits.

```cpp
// Source: Ninja-VST3-Plugin WebRTCSession.cpp:605-607, adapted for NINJAM
// Key derivation: SHA-256 of password concatenated with 8-byte challenge as salt
unsigned char key[32];
std::string phrase = std::string(password) + std::string((char*)challenge, 8);
SHA256(reinterpret_cast<const unsigned char*>(phrase.data()), phrase.size(), key);
```

[VERIFIED: Reference implementation at WebRTCSession.cpp:605-607 uses this exact pattern]

### Pattern 3: Encrypt-on-Send / Decrypt-on-Receive
**What:** Transparent encryption inside Net_Connection that is invisible to all message handling code.
**When to use:** All post-auth messages when encryption is negotiated.

The Send() path currently works like this (from netmsg.cpp):
1. Message is queued via `m_sendq.Add(&msg, sizeof(Net_Message*))`
2. In Run(), messages are dequeued, header sent, then payload sent

**Insertion point for encryption:** Before sending payload bytes, encrypt the entire payload in-place (or into a new buffer). The header's size field must reflect the encrypted size.

The Receive path works like this:
1. `parseMessageHeader()` reads type (1 byte) and size (4 bytes) -- 5 bytes total
2. `parseAddBytes()` accumulates payload until `parseBytesNeeded() < 1`
3. Complete message returned

**Insertion point for decryption:** After full message is received (before returning from Run()), decrypt the payload in-place.

### Anti-Patterns to Avoid
- **Encrypting the header:** NEVER encrypt the type or size fields. Net_Connection needs cleartext headers to parse message boundaries. An observer can see message types and sizes but not contents -- this is acceptable per D-07.
- **Shared EVP_CIPHER_CTX:** NEVER reuse a cipher context across messages. Each message gets a fresh context (and fresh IV). The reference implementation creates and destroys context per call.
- **WDL_RNG for IVs:** NEVER use WDL_RNG for cryptographic randomness. Its header explicitly states "we wouldn't consider this RNG to be cryptographically secure." MUST use OpenSSL `RAND_bytes()`. [VERIFIED: wdl/rng.h comment]
- **Encrypting keepalive messages:** Keepalive messages (type 0xFD) have zero-length payloads. Encrypting a zero-length payload would produce 16 bytes of PKCS#7 padding. Either skip encryption for zero-length payloads or handle this edge case explicitly.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| AES-256-CBC encryption | Custom AES implementation | OpenSSL EVP_aes_256_cbc() | Side-channel attacks, padding oracle, block handling -- EVP handles all |
| SHA-256 hashing | Custom hash or extend WDL_SHA1 | OpenSSL SHA256() | WDL_SHA1 is SHA-1 (20-byte output), need SHA-256 (32-byte output) for AES-256 key |
| Random IV generation | WDL_RNG or rand() | OpenSSL RAND_bytes() | Must be cryptographically secure for IV uniqueness guarantees |
| PKCS#7 padding | Manual padding logic | EVP automatic padding | EVP_EncryptFinal_ex() handles PKCS#7 automatically |

**Key insight:** The entire encrypt/decrypt implementation is approximately 80 lines of C++ (reference: WebRTCSession.cpp:594-685). The complexity is in getting the integration points right (Net_Connection hooks, auth negotiation, key lifecycle), not in the crypto itself.

## Common Pitfalls

### Pitfall 1: NET_MESSAGE_MAX_SIZE Overflow
**What goes wrong:** Encrypted payload (original + IV + padding) exceeds 16384 bytes, causing `parseMessageHeader()` to return -1 (error).
**Why it happens:** AES-256-CBC adds up to 32 bytes overhead per message (16 IV + up to 16 PKCS#7 padding). A plaintext message at exactly 16384 bytes becomes up to 16416 bytes encrypted.
**How to avoid:** Increase `NET_MESSAGE_MAX_SIZE` to accommodate encryption overhead. Recommended: `#define NET_MESSAGE_MAX_SIZE_ENCRYPTED (NET_MESSAGE_MAX_SIZE + 32)` and use the encrypted limit in parseMessageHeader when encryption is active. Alternatively, bump NET_MESSAGE_MAX_SIZE to 16416 for all connections (32 bytes is negligible).
**Warning signs:** Connection drops immediately after encryption is negotiated, especially with large audio chunks.
[VERIFIED: netmsg.h:38 defines NET_MESSAGE_MAX_SIZE as 16384, netmsg.cpp:65 rejects messages exceeding this]

### Pitfall 2: Encrypting Before Auth Completes
**What goes wrong:** Encryption is activated too early, encrypting auth messages that the other side doesn't expect encrypted.
**Why it happens:** Setting `m_encryption_active = true` before both sides have completed the auth handshake.
**How to avoid:** On the client side, set encryption active ONLY after receiving `MESSAGE_SERVER_AUTH_REPLY` with success bit AND encryption-active bit. On the server side, set encryption active ONLY after sending the reply. The first encrypted message from client should be `MESSAGE_CLIENT_SET_CHANNEL_INFO` (the first post-auth message).
**Warning signs:** Auth failures when connecting to your own JamWide server.

### Pitfall 3: OpenSSL Universal Build on macOS
**What goes wrong:** `find_package(OpenSSL)` finds single-architecture OpenSSL (e.g., arm64 from Homebrew on Apple Silicon), but build targets both arm64 and x86_64.
**Why it happens:** Homebrew installs arch-specific libraries. CMake's `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` requires libraries for both architectures.
**How to avoid:** Three options in order of simplicity:
1. Link OpenSSL as a shared library (.dylib) and handle architecture at runtime (simplest but requires OpenSSL at runtime)
2. Build only the native architecture in development, universal in CI via separate arch builds + lipo
3. Use a CMake ExternalProject to build OpenSSL from source as a universal static lib
**Recommendation:** Option 1 for development, with CI building two separate architectures and combining. Or consider switching to static linking with architecture-specific build passes.
**Warning signs:** Linker errors about missing architectures during universal build.

### Pitfall 4: Decryption Failure Mid-Session
**What goes wrong:** A single corrupted byte in a TCP stream causes decryption failure (EVP_DecryptFinal_ex returns error due to bad padding), and the connection enters an unrecoverable state.
**Why it happens:** TCP guarantees delivery but if the encryption state gets out of sync (e.g., partial message received), subsequent decryptions fail.
**How to avoid:** On decryption failure, log the error and disconnect cleanly (set `m_error`). Do NOT attempt to recover or skip messages -- the framing is corrupted. AES-CBC with random IV per message means each message is independently decryptable, so this should only happen on genuine corruption.
**Warning signs:** Sudden disconnections after extended sessions.

### Pitfall 5: Zero-Length Payload Encryption
**What goes wrong:** Keepalive messages (type 0xFD) have zero-length payloads. Encrypting zero bytes with AES-CBC produces exactly one block (16 bytes) of PKCS#7 padding plus 16-byte IV = 32 bytes. This is unnecessary overhead sent every 3 seconds.
**Why it happens:** The encryption layer doesn't special-case zero-length payloads.
**How to avoid:** Skip encryption for zero-length payloads. The keepalive message type (0xFD) and size (0) are already in cleartext headers -- encrypting nothing adds no security value.
**Warning signs:** Keepalive messages balloon from 5 bytes (header only) to 37 bytes.

## Code Examples

### AES-256-CBC Encrypt (adapted from reference implementation)
```cpp
// Source: Ninja-VST3-Plugin WebRTCSession.cpp:594-638, adapted for NINJAM binary payloads
// Returns: encrypted buffer = [16-byte IV][ciphertext], or empty on failure
// Caller owns the returned buffer.

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

struct EncryptedPayload {
    std::vector<unsigned char> data;  // [IV:16][ciphertext:N]
    bool ok = false;
};

EncryptedPayload encrypt_payload(const unsigned char* plaintext, int plaintext_len,
                                  const unsigned char key[32])
{
    EncryptedPayload result;
    if (plaintext_len <= 0) { result.ok = true; return result; }  // skip zero-length

    unsigned char iv[16];
    if (RAND_bytes(iv, 16) != 1) return result;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return result;

    // Max ciphertext size: plaintext + one block of padding
    result.data.resize(16 + plaintext_len + EVP_MAX_BLOCK_LENGTH);
    memcpy(result.data.data(), iv, 16);  // prepend IV

    int out1 = 0, out2 = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv) != 1 ||
        EVP_EncryptUpdate(ctx, result.data.data() + 16, &out1, plaintext, plaintext_len) != 1 ||
        EVP_EncryptFinal_ex(ctx, result.data.data() + 16 + out1, &out2) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        result.data.clear();
        return result;
    }
    EVP_CIPHER_CTX_free(ctx);
    result.data.resize(16 + out1 + out2);
    result.ok = true;
    return result;
}
```

### AES-256-CBC Decrypt (adapted from reference implementation)
```cpp
// Source: Ninja-VST3-Plugin WebRTCSession.cpp:640-685, adapted for NINJAM binary payloads
// Input: buffer = [16-byte IV][ciphertext]
// Returns plaintext, or empty on failure

struct DecryptedPayload {
    std::vector<unsigned char> data;
    bool ok = false;
};

DecryptedPayload decrypt_payload(const unsigned char* encrypted, int encrypted_len,
                                  const unsigned char key[32])
{
    DecryptedPayload result;
    if (encrypted_len < 16) return result;  // need at least IV
    if (encrypted_len == 16) { result.ok = true; return result; }  // IV only, no ciphertext

    const unsigned char* iv = encrypted;
    const unsigned char* ciphertext = encrypted + 16;
    int ciphertext_len = encrypted_len - 16;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return result;

    result.data.resize(ciphertext_len + EVP_MAX_BLOCK_LENGTH);
    int out1 = 0, out2 = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv) != 1 ||
        EVP_DecryptUpdate(ctx, result.data.data(), &out1, ciphertext, ciphertext_len) != 1 ||
        EVP_DecryptFinal_ex(ctx, result.data.data() + out1, &out2) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        result.data.clear();
        return result;
    }
    EVP_CIPHER_CTX_free(ctx);
    result.data.resize(out1 + out2);
    result.ok = true;
    return result;
}
```

### Key Derivation
```cpp
// Source: Ninja-VST3-Plugin WebRTCSession.cpp:605-607
// Derives 32-byte AES key from password + 8-byte server challenge
void derive_encryption_key(const char* password, const unsigned char challenge[8],
                            unsigned char key_out[32])
{
    // Concatenate password + challenge bytes (challenge serves as salt)
    std::string phrase(password);
    phrase.append(reinterpret_cast<const char*>(challenge), 8);

    SHA256(reinterpret_cast<const unsigned char*>(phrase.data()),
           phrase.size(), key_out);
}
```

### Net_Connection Integration Point
```cpp
// In netmsg.h, add to Net_Connection class:
private:
    bool m_encryption_active = false;
    unsigned char m_encryption_key[32] = {};

public:
    void SetEncryptionKey(const unsigned char key[32]) {
        memcpy(m_encryption_key, key, 32);
        m_encryption_active = true;
    }
    void ClearEncryption() {
        m_encryption_active = false;
        memset(m_encryption_key, 0, 32);  // scrub key from memory
    }
    bool IsEncryptionActive() const { return m_encryption_active; }
```

### CMake OpenSSL Integration
```cmake
# In CMakeLists.txt, before njclient target:
find_package(OpenSSL REQUIRED)

# Link OpenSSL to njclient (shared by client and server)
target_link_libraries(njclient PUBLIC wdl vorbis vorbisenc ogg FLAC OpenSSL::Crypto)
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| OpenSSL low-level AES_encrypt() | OpenSSL EVP high-level API | OpenSSL 1.1+ (deprecated low-level in 3.0) | EVP is the supported API; low-level functions emit deprecation warnings |
| SHA-1 for key derivation | SHA-256 minimum | Industry standard post-2015 | SHA-1 is 20 bytes (too short for AES-256 key); SHA-256 produces exactly 32 bytes |
| Fixed IV or sequential counter | Random IV per message | Best practice for CBC mode | Prevents IV reuse attacks; eliminates need for counter synchronization |

[CITED: docs.openssl.org/3.0/man3/EVP_EncryptInit/ -- EVP is the recommended API in OpenSSL 3.x]
[CITED: wiki.openssl.org/index.php/EVP_Symmetric_Encryption_and_Decryption -- EVP example for AES-256-CBC]

**Deprecated/outdated:**
- `AES_encrypt()` / `AES_set_encrypt_key()`: Low-level API deprecated in OpenSSL 3.0. Use EVP instead. [CITED: docs.openssl.org/3.0/man7/migration_guide/]
- WDL_SHA1 for encryption keys: SHA-1 produces 20 bytes, insufficient for AES-256's 32-byte key requirement. Still used for NINJAM auth hashing (backward compat), supplemented by SHA-256 for encryption key derivation.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `client_caps` bit 2 is safe to use for encryption (no legacy client sets it) | Architecture Patterns | Legacy NINJAM clients with unknown bit usage could falsely advertise encryption support; mitigated by JamWide-only encryption (D-10) and silent fallback |
| A2 | `flag` bit 1 in auth reply is safe to use (no legacy server sets it) | Architecture Patterns | Legacy servers setting bit 1 would falsely signal encryption active; mitigated by only activating encryption when JamWide server explicitly sets it |
| A3 | Windows CI runner has OpenSSL available via `choco install openssl` or vcpkg | Environment Availability | Could block Windows CI builds; fallback is vcpkg or bundling OpenSSL |
| A4 | macOS CI runner (macos-14) can install OpenSSL via Homebrew for ARM64 architecture | Environment Availability | Could fail for universal builds; fallback is separate arch build passes |

## Open Questions (RESOLVED)

1. **Universal macOS build + OpenSSL static linking** (RESOLVED)
   - What we know: Homebrew OpenSSL is single-architecture. macOS CI builds universal (arm64+x86_64).
   - **Decision:** Link dynamically in development via Homebrew. For CI, pass `-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)` explicitly in the CMake configure step. The CMakeLists.txt also includes a `brew --prefix` fallback for local builds. If universal builds fail due to single-arch OpenSSL, CI can be split into two separate architecture passes with lipo merge as a follow-up.

2. **HMAC for tamper detection (encrypt-then-MAC)** (RESOLVED)
   - What we know: AES-CBC without MAC is vulnerable to padding oracle attacks and bit-flipping. The user explicitly deferred HMAC.
   - **Decision:** Deferred per user decision (see CONTEXT.md Deferred Ideas). Code comments will note that encrypt-then-MAC (HMAC-SHA256) should be added in a future phase for production-grade security. Current mitigation: disconnect on any decryption failure without revealing error details (prevents padding oracle exploitation).

3. **Size field semantics with encryption** (RESOLVED)
   - What we know: Currently, `size` = plaintext payload length. With encryption, `size` must reflect what's on the wire.
   - **Decision:** `size` = IV (16 bytes) + ciphertext length. This is what the receiver needs to read from the socket. The decrypted plaintext length is recovered after decryption via PKCS#7 unpadding. This matches D-07 ("The 'size' field reflects the encrypted payload size") and D-08 ("IV prepended to payload").

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| OpenSSL (libcrypto) | AES-256-CBC, SHA-256, RAND_bytes | Yes (macOS dev) | 3.6.2 | None -- locked dependency |
| OpenSSL headers | Compilation | Yes (macOS dev) | 3.6.2 | None |
| CMake FindOpenSSL | Build system | Yes | Built-in | Manual -I/-L flags |

[VERIFIED: `openssl version` shows 3.6.2, `pkg-config --cflags --libs openssl` resolves correctly]
[VERIFIED: Homebrew openssl@3 installed at /usr/local/Cellar/openssl@3/3.6.2/]

**Missing dependencies with no fallback:**
- None on dev machine

**Missing dependencies with fallback:**
- Windows CI OpenSSL: Not yet set up in `.github/workflows/juce-build.yml`. Fallback: add `choco install openssl` step or use vcpkg.
- Linux CI OpenSSL-dev: `libcurl4-openssl-dev` is installed but `libssl-dev` may be needed explicitly for headers. Fallback: add `libssl-dev` to apt-get install.
- macOS CI OpenSSL for universal builds: Homebrew installs single-arch. Fallback: Dynamic linking or separate arch passes.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | assert-based C++ tests (project convention) |
| Config file | `CMakeLists.txt` JAMWIDE_BUILD_TESTS section |
| Quick run command | `cmake --build build --target test_encryption && ./build/test_encryption` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| SEC-01 | Password-derived key encrypts/decrypts correctly | unit | `./build/test_encryption --test-roundtrip` | Wave 0 |
| SEC-01 | Key derivation produces expected output for known inputs | unit | `./build/test_encryption --test-keyder` | Wave 0 |
| SEC-02 | All message types encrypt/decrypt with payload integrity | unit | `./build/test_encryption --test-alltypes` | Wave 0 |
| SEC-02 | Zero-length payload (keepalive) handled correctly | unit | `./build/test_encryption --test-zerolength` | Wave 0 |
| SEC-03 | Client connects to mock server without encryption bit | integration | Manual test against legacy NINJAM server | Manual-only: requires running NINJAM server |
| SEC-03 | Capability negotiation sets correct bits | unit | `./build/test_encryption --test-capbits` | Wave 0 |

### Sampling Rate
- **Per task commit:** `cmake --build build --target test_encryption && ./build/test_encryption`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/test_encryption.cpp` -- covers SEC-01, SEC-02, SEC-03 (round-trip encrypt/decrypt, key derivation, capability bits, zero-length edge case)
- [ ] CMakeLists.txt test target: `add_executable(test_encryption tests/test_encryption.cpp)` with OpenSSL::Crypto link
- [ ] Framework install: OpenSSL already available, just needs CMake `find_package(OpenSSL)` wired

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | Yes | Existing SHA-1 challenge-response auth (unchanged); SHA-256 key derivation for encryption |
| V3 Session Management | No | NINJAM sessions are connection-scoped, no session tokens |
| V4 Access Control | No | Server password is the only access control; unchanged |
| V5 Input Validation | Yes | Validate encrypted payload length >= 16 (IV); reject invalid ciphertext gracefully |
| V6 Cryptography | Yes | AES-256-CBC via OpenSSL EVP; RAND_bytes for IV; SHA-256 for key derivation |

### Known Threat Patterns for AES-CBC over TCP

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Padding oracle attack | Information Disclosure | Disconnect on decryption failure (don't reveal padding error details); deferred: HMAC-SHA256 encrypt-then-MAC |
| IV reuse | Information Disclosure | RAND_bytes generates cryptographically random 16-byte IV per message |
| Key compromise via weak derivation | Information Disclosure | SHA-256 of password+challenge; challenge is 8 random bytes from server |
| Replay attack | Spoofing | TCP connection scoping provides implicit replay protection; deferred: sequence numbers |
| Bit-flipping (CBC malleability) | Tampering | Deferred: HMAC-SHA256 encrypt-then-MAC would detect; current design trusts TCP integrity |
| Eavesdropping on cleartext headers | Information Disclosure | Accepted risk per D-07: observer sees message types and sizes but not contents |

## Sources

### Primary (HIGH confidence)
- Ninja-VST3-Plugin reference implementation `WebRTCSession.cpp:594-685` -- verified encrypt/decrypt pattern, read directly from filesystem [VERIFIED: local file]
- JamWide codebase `src/core/netmsg.h`, `netmsg.cpp`, `mpb.h`, `mpb.cpp`, `njclient.h`, `njclient.cpp` -- message framing, auth handshake, capability flags [VERIFIED: local files]
- OpenSSL 3.6.2 on dev machine -- `openssl version`, `pkg-config --cflags --libs openssl` [VERIFIED: command output]

### Secondary (MEDIUM confidence)
- [OpenSSL EVP Symmetric Encryption docs](https://wiki.openssl.org/index.php/EVP_Symmetric_Encryption_and_Decryption) -- EVP API usage patterns
- [OpenSSL 3.0 Migration Guide](https://docs.openssl.org/3.0/man7/migration_guide/) -- EVP_EncryptInit_ex remains supported, low-level AES deprecated
- [OpenSSL EVP_EncryptInit man page](https://docs.openssl.org/3.0/man3/EVP_EncryptInit/) -- Function signatures and behavior

### Tertiary (LOW confidence)
- GitHub Actions macos-14 + OpenSSL universal build discussion -- community reports suggest separate arch builds needed [ASSUMED: specific CI behavior may vary]

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- OpenSSL EVP is locked decision, verified on dev machine, reference implementation exists
- Architecture: HIGH -- NINJAM protocol structures fully analyzed, insertion points clearly identified in codebase, reference implementation provides direct template
- Pitfalls: HIGH -- Based on direct codebase analysis (NET_MESSAGE_MAX_SIZE verified at 16384, zero-length keepalive verified, capability bit usage verified)
- CI/cross-platform linking: MEDIUM -- macOS universal build with OpenSSL has known complications; Windows CI OpenSSL not yet validated

**Research date:** 2026-04-11
**Valid until:** 2026-05-11 (stable domain, OpenSSL API unlikely to change)
