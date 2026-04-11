---
phase: 15-connection-encryption
plan: 02
subsystem: crypto, networking
tags: [aes-256-cbc, openssl, ninjam, encryption, protocol-negotiation]

# Dependency graph
requires:
  - phase: 15-01
    provides: "AES-256-CBC crypto module (encrypt_payload, decrypt_payload, derive_encryption_key)"
provides:
  - "Encrypted NINJAM protocol layer: Net_Connection encrypt-on-send/decrypt-on-receive hooks"
  - "Redesigned auth handshake: server advertises encryption in AUTH_CHALLENGE, client encrypts AUTH_USER"
  - "Capability bit defines for encryption negotiation (SERVER_CAP_ENCRYPT_SUPPORTED, CLIENT_CAP_ENCRYPT_SUPPORTED, SERVER_FLAG_ENCRYPT_ACTIVE)"
  - "Downgrade detection: explicit ClearEncryption if server does not confirm"
  - "CI OpenSSL support on macOS, Windows, and Linux"
affects: [ninbot-server, future-opus-codec]

# Tech tracking
tech-stack:
  added: []
  patterns: ["payload-only encryption with cleartext framing headers", "capability bit negotiation for backward compatibility", "key scrubbing on disconnect"]

key-files:
  created: []
  modified:
    - "src/core/netmsg.h"
    - "src/core/netmsg.cpp"
    - "src/core/mpb.h"
    - "src/core/njclient.h"
    - "src/core/njclient.cpp"
    - "tests/test_encryption.cpp"
    - "CMakeLists.txt"
    - ".github/workflows/juce-build.yml"

key-decisions:
  - "Payload-only encryption: message type and size remain cleartext for framing compatibility (accepted trade-off per D-07, T-15-08)"
  - "Zero-length keepalive payloads skip encryption to avoid 32-byte overhead every 3 seconds with zero confidentiality benefit"
  - "Generic error code (-5) on decrypt failure prevents padding oracle information leakage"
  - "Server-side integration deferred to ninbot server repository (out of scope for this client repo)"

patterns-established:
  - "Capability bit negotiation: unused bits in server_caps/client_caps/flag used for feature negotiation"
  - "Key lifecycle: derive on AUTH_CHALLENGE, set before AUTH_USER send, scrub on disconnect"
  - "Encrypt hook placement: before makeMessageHeader so header reflects encrypted payload size"

requirements-completed: [SEC-01, SEC-02, SEC-03]

# Metrics
duration: 8min
completed: 2026-04-11
---

# Phase 15 Plan 02: Protocol Integration Summary

**AES-256-CBC encryption integrated into NINJAM protocol with redesigned auth handshake -- credentials encrypted from AUTH_USER onward, transparent to application layer, backward-compatible with legacy servers**

## Performance

- **Duration:** 8 min
- **Started:** 2026-04-11T16:08:10Z
- **Completed:** 2026-04-11T16:16:17Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Integrated encrypt/decrypt hooks into Net_Connection::Run() send and receive paths, encrypting all non-empty payloads transparently
- Redesigned auth handshake: server advertises encryption in AUTH_CHALLENGE (server_caps bit 1), client derives key and encrypts AUTH_USER before sending (SEC-01: credentials encrypted)
- Added downgrade detection: if server does not confirm encryption in AUTH_REPLY, client explicitly clears encryption state
- Updated CI workflows for all 3 platforms (macOS Homebrew, Windows Chocolatey, Linux apt) with OpenSSL install steps
- All 26 tests pass (18 unit tests from Plan 01 + 8 integration tests from Plan 02)

## Task Commits

Each task was committed atomically:

1. **Task 1: Integrate encryption into Net_Connection and redesigned auth handshake** - `8f3236c` (feat)
2. **Task 2: Update CI workflows for OpenSSL on all platforms** - `3f19bf1` (chore)

## Files Created/Modified
- `src/core/mpb.h` - Added SERVER_CAP_ENCRYPT_SUPPORTED, CLIENT_CAP_ENCRYPT_SUPPORTED, SERVER_FLAG_ENCRYPT_ACTIVE capability bit defines
- `src/core/netmsg.h` - Added encryption state (m_encryption_active, m_encryption_key), SetEncryptionKey/ClearEncryption/IsEncryptionActive methods, NET_MESSAGE_MAX_SIZE_ENCRYPTED constant
- `src/core/netmsg.cpp` - Added encrypt_payload hook in send path, decrypt_payload hook in receive path, updated parseMessageHeader for encrypted size limit, generic error on decrypt failure
- `src/core/njclient.h` - Added m_auth_challenge[8] for key derivation
- `src/core/njclient.cpp` - Redesigned auth flow: save challenge, derive key on server encryption bit, encrypt AUTH_USER, confirm/downgrade on AUTH_REPLY, scrub challenge on disconnect
- `tests/test_encryption.cpp` - Added 8 integration tests for capability bits, auth scenario, legacy fallback, downgrade detection, max size, lifecycle, key scrub
- `CMakeLists.txt` - Updated test_encryption to link against njclient instead of standalone nj_crypto.cpp
- `.github/workflows/juce-build.yml` - Added OpenSSL install steps for macOS (brew), Windows (choco), Linux (apt libssl-dev) with OPENSSL_ROOT_DIR hints

## Decisions Made
- Payload-only encryption with cleartext framing headers (type + size) -- accepted trade-off for backward compatibility with NINJAM protocol framing
- Zero-length keepalive skip -- encrypting empty payloads adds 32 bytes overhead per 3 seconds with zero security value
- Generic decrypt error code (-5) -- prevents padding oracle attacks by not distinguishing padding vs. block alignment vs. key mismatch failures
- Server-side (ninbot) deferred -- the shared Net_Connection hooks and capability defines work for both client and server; server integration is a follow-up in the ninbot repository

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- Git submodules needed initialization in the worktree before building (`git submodule update --init --recursive`)
- OpenSSL on the build machine is x86_64-only (Homebrew /usr/local), requiring explicit `-DCMAKE_OSX_ARCHITECTURES=x86_64` for local test builds. CI universal builds use the OPENSSL_ROOT_DIR hint to find the correct architecture.

## Known Stubs

None - all functionality is fully wired. Server-side integration is explicitly documented as out-of-scope (ninbot server is a separate repository).

## Threat Flags

None - all security surfaces match the plan's threat model (T-15-06 through T-15-13).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Encryption feature is complete on the client side
- Server-side (ninbot) needs: set server_caps bit 1, derive key on seeing client_caps bit 2, call SetEncryptionKey, set flag bit 1 in AUTH_REPLY
- The shared foundation (Net_Connection hooks, capability defines, crypto module) is ready for server integration

## Self-Check: PASSED

All files verified present. All commit hashes found in git log.

---
*Phase: 15-connection-encryption*
*Completed: 2026-04-11*
