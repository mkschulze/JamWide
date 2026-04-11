# Phase 15: Connection Encryption - Context

**Gathered:** 2026-04-11
**Status:** Ready for planning

<domain>
## Phase Boundary

AES-256-CBC encryption of all post-auth NINJAM traffic (audio, config, chat) using password-derived session keys, with transparent fallback for legacy unencrypted servers. Covers SEC-01, SEC-02, SEC-03. Both client and server (ninbot) sides.

</domain>

<decisions>
## Implementation Decisions

### Encryption Scope & Algorithm
- **D-01:** All post-auth traffic encrypted. After encryption is negotiated, every subsequent NINJAM message (audio chunks, config/topic changes, chat, user join/leave) is encrypted. No selective encryption — all or nothing per session.
- **D-02:** AES-256-CBC via OpenSSL EVP API. Same algorithm and API as Ninja-VST3-Plugin reference (`EVP_aes_256_cbc`, `EVP_EncryptInit_ex`, `EVP_EncryptUpdate`, `EVP_EncryptFinal_ex`). PKCS#7 padding automatic via EVP.
- **D-03:** Key derivation: SHA-256(password + salt) → 32-byte key. Salt derived from the server's 8-byte challenge (from `MESSAGE_SERVER_AUTH_CHALLENGE`). Same pattern as reference plugin. No PBKDF2 — NINJAM passwords are session-specific, not stored credentials.
- **D-04:** Random IV per message. 16 bytes via OpenSSL `RAND_bytes` for each encrypted message. IV prepended to ciphertext in the payload. Stateless — no counter sync needed between client and server.

### Protocol Negotiation & Fallback
- **D-05:** Capability flag in existing auth handshake. Client sets an 'encryption supported' bit in `client_caps` (in `MESSAGE_CLIENT_AUTH_USER`). Server responds with 'encryption active' bit in `MESSAGE_SERVER_AUTH_REPLY`. If server doesn't understand the bit, it ignores it — zero overhead for legacy servers.
- **D-06:** Silent fallback to unencrypted for legacy servers. If server doesn't set the encryption bit in its reply, JamWide connects normally without encryption. No warning to user. Matches success criterion 3 — transparent, no extra configuration.

### Encryption Layer Placement
- **D-07:** Payload-only encryption. Encrypt the message payload, keep the type (4 bytes) and size (4 bytes) headers in cleartext. Net_Connection can still parse message boundaries and route by type. The 'size' field reflects the encrypted payload size (ciphertext + prepended IV). An observer can see message types and sizes but not contents.
- **D-08:** IV prepended to payload. Each encrypted message's payload is: [16-byte IV][ciphertext]. Receiver reads first 16 bytes as IV, remainder as ciphertext. No separate IV field in the protocol.

### Server-Side Support
- **D-09:** Both client and server (ninbot) support encryption in this phase. Since encryption lives in `Net_Connection` (shared by client and server code), both sides get it naturally. When ninbot hosts a session with a password, all connected JamWide clients get encrypted traffic.
- **D-10:** JamWide-to-JamWide encryption only. This is a JamWide-specific NINJAM protocol extension. Other NINJAM clients connect unencrypted via the fallback mechanism. No cross-client encryption interop needed — the reference Ninja-VST3-Plugin uses WebRTC (completely different transport).

### Claude's Discretion
- Exact bit position in the capability flags (which bit number for encryption)
- OpenSSL linking strategy per platform (system lib on macOS, bundled on Windows, pkg-config on Linux)
- Whether to add HMAC authentication on top of AES-CBC (encrypt-then-MAC pattern) for tamper detection
- Error handling when decryption fails mid-session (connection drop vs retry)
- Whether the size field in the header includes the IV length or just the ciphertext
- CMakeLists.txt changes needed to link OpenSSL across all build targets

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Reference Implementation
- `/Users/cell/dev/Ninja-VST3-Plugin/webrtc_vst/src/WebRTCSession.cpp` lines 594-685 — AES-256-CBC encrypt/decrypt pattern using OpenSSL EVP. Direct template for JamWide's implementation.

### NINJAM Protocol & Connection
- `src/core/netmsg.h` — `Net_Connection` class: message send/receive, the transport layer where encryption hooks in
- `src/core/netmsg.cpp` — Message framing, `Send()` and `Run()` methods — insertion points for encrypt/decrypt
- `src/core/njclient.h` — NJClient: `Connect()`, `m_user`, `m_pass`, auth handling
- `src/core/njclient.cpp` lines 912-944 — `Connect()` implementation; lines 1076-1152 — auth challenge/response with SHA1 hash
- `src/core/mpb.h` — Message structures including `mpb_server_auth_challenge`, `mpb_client_auth_user`, `mpb_server_auth_reply` — capability flags live here
- `src/core/mpb.cpp` — Auth message parse/build implementations

### Crypto
- `wdl/sha.h` — Existing WDL_SHA1 used for current auth hashing

### Prior Phase Patterns
- `.planning/phases/09-osc-server-core/09-CONTEXT.md` — Echo suppression and threading patterns (may be relevant for encryption state management)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `WDL_SHA1` in `wdl/sha.h` — current auth hash; will be supplemented (not replaced) by SHA-256 for key derivation
- `WDL_RNG` — random number generation already imported in njclient.h
- `juce_cryptography` module — already linked, has SHA256 class (backup if OpenSSL SHA256 preferred elsewhere)
- Auth capability flags — 32-bit `server_caps` and `client_caps` fields already in the protocol with room for new bits

### Established Patterns
- Challenge-response auth: Server sends 8-byte challenge, client hashes password+challenge. Encryption negotiation piggybacks on this existing flow.
- Message framing: [type:4][size:4][payload:N] — encryption wraps payload only, preserving the existing framing.
- `Net_Connection` is shared by client and server code — changes there benefit both sides automatically.

### Integration Points
- `Net_Connection::Send()` / `Run()` in `netmsg.cpp` — encrypt before send, decrypt after receive
- `mpb_server_auth_challenge` / `mpb_client_auth_user` / `mpb_server_auth_reply` — add encryption capability bits
- `NJClient::Connect()` — derive encryption key after successful auth
- `CMakeLists.txt` — link OpenSSL (find_package or system lib)

</code_context>

<specifics>
## Specific Ideas

- Follow the Ninja-VST3-Plugin reference implementation closely for the encrypt/decrypt functions
- The 8-byte server challenge doubles as salt for key derivation — no need for a separate salt exchange
- Encryption should be "invisible" to the user — if a password is set, encryption happens; if not, no encryption
- ninbot (server) should advertise encryption capability so JamWide clients can negotiate encrypted sessions

</specifics>

<deferred>
## Deferred Ideas

- HMAC/encrypt-then-MAC for tamper detection — could be added later if needed
- TLS/DTLS as alternative to application-layer encryption — would require replacing JNetLib transport
- Per-user encryption keys (each participant derives independently) — current design uses shared session key from password
- Key rotation during long sessions — AES-CBC with random IV per message is sufficient for session-length security

</deferred>

---

*Phase: 15-connection-encryption*
*Context gathered: 2026-04-11*
