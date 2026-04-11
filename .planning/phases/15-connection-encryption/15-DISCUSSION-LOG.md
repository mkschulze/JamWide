# Phase 15: Connection Encryption - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-11
**Phase:** 15-connection-encryption
**Areas discussed:** Encryption scope & algorithm, Protocol negotiation & fallback, Encryption layer placement, Server-side support

---

## Encryption Scope & Algorithm

| Option | Description | Selected |
|--------|-------------|----------|
| All post-auth traffic | After encryption is negotiated, ALL subsequent messages encrypted. Maximum security. | ✓ |
| Auth credentials only | Only encrypt password/auth exchange. Audio remains cleartext. | |
| Auth + audio, not config | Encrypt credentials and audio, leave control messages cleartext. | |

**User's choice:** All post-auth traffic (Recommended)
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| SHA-256(password + salt) | Same as Ninja-VST3-Plugin. Simple, fast, proven. | ✓ |
| PBKDF2 with iterations | Stronger brute-force resistance but more computational cost. | |
| You decide | Claude picks based on threat model. | |

**User's choice:** SHA-256(password + salt) (Recommended)
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| OpenSSL EVP | Same as Ninja-VST3-Plugin. Battle-tested AES-256-CBC. | ✓ |
| JUCE BlowFish + SHA256 | No new dependency but weaker (64-bit blocks, sweet32 attack). | |
| You decide | Claude picks based on security and build analysis. | |

**User's choice:** OpenSSL EVP (Recommended)
**Notes:** None

---

## Protocol Negotiation & Fallback

| Option | Description | Selected |
|--------|-------------|----------|
| Capability flag in existing auth handshake | Client sets bit in client_caps, server responds. Zero overhead for legacy. | ✓ |
| Separate encryption handshake message | New message type after auth. More explicit but more complex. | |
| You decide | Claude picks cleanest approach. | |

**User's choice:** Capability flag in existing auth handshake (Recommended)
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| Silent fallback to unencrypted | Connect normally without warning. Transparent. | ✓ |
| Fallback with warning | Connect unencrypted but show lock icon indicator. | |
| Refuse connection | Don't connect to unencrypted servers. Breaks compatibility. | |

**User's choice:** Silent fallback to unencrypted (Recommended)
**Notes:** None

---

## Encryption Layer Placement

| Option | Description | Selected |
|--------|-------------|----------|
| Payload-only encryption | Encrypt payload, keep type+size headers clear. Simpler. | ✓ |
| Full-frame encryption | Encrypt everything. Hides message types. More complex. | |
| Stream cipher on TCP | AES-CTR/ChaCha20 stream mode. Different from reference pattern. | |

**User's choice:** Payload-only encryption (Recommended)
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| Random IV per message | Same as Ninja-VST3-Plugin. RAND_bytes 16 bytes. Stateless. | ✓ |
| Counter-based IV | Incrementing counter. More efficient but adds state. | |
| You decide | Claude picks based on protocol characteristics. | |

**User's choice:** Random IV per message (Recommended)
**Notes:** None

---

## Server-Side Support

| Option | Description | Selected |
|--------|-------------|----------|
| Client + server | Both sides. Net_Connection is shared so both get it naturally. | ✓ |
| Client-only for now | Only outgoing connections. Reduces scope. | |
| You decide | Claude assesses code sharing. | |

**User's choice:** Client + server (Recommended)
**Notes:** None

---

| Option | Description | Selected |
|--------|-------------|----------|
| JamWide-to-JamWide only | Encryption is JamWide-specific extension. Others fallback. | ✓ |
| Compatible with Ninja-VST3-Plugin | Same wire format for cross-client encryption. | |
| You decide | Claude determines feasibility. | |

**User's choice:** JamWide-to-JamWide only
**Notes:** User asked about companion app room sharing. Explained that NINJAM TCP encryption and WebRTC video are completely different transports — VDO.Ninja rooms already have built-in WebRTC encryption (DTLS-SRTP).

---

## Claude's Discretion

- Exact bit position in capability flags
- OpenSSL linking strategy per platform
- Whether to add HMAC for tamper detection
- Error handling on decryption failure
- IV size accounting in message size field
- CMakeLists changes for OpenSSL

## Deferred Ideas

- HMAC/encrypt-then-MAC pattern
- TLS/DTLS transport replacement
- Per-user encryption keys
- Key rotation during long sessions
