# External Integrations

**Analysis Date:** 2026-03-07

## APIs & External Services

**NINJAM Network Protocol:**
- Service: NINJAM (Novel Intervallic Network Jamming Architecture for Music)
- What it's used for: Real-time collaborative audio session hosting and synchronization
  - Server connections via TCP (default port 2049)
  - Audio streaming to/from multiple remote musicians
  - Chat messaging and room status
  - BPM/BPI synchronization
- Protocol: Custom binary protocol (implemented in `src/core/netmsg.h`, `src/core/netmsg.cpp`)
- Client Library: NJClient port from original REAPER extension
  - Location: `src/core/njclient.h`, `src/core/njclient.cpp`
  - Handles encoding/decoding, network I/O, session state

**Server List API:**
- Service: autosong.ninjam.com/serverlist.php
- What it's used for: Public server discovery and user count display
  - Default URL: `http://autosong.ninjam.com/serverlist.php` (defined in `src/ui/ui_state.h:102`)
  - HTTP GET request with response parsing
  - Formats supported: Plain text (NINJAM format) and JSON
- Client: JNL_HTTPGet from WDL jnetlib (`src/net/server_list.cpp`)
- Parser: Custom format detection + picojson for JSON (`src/third_party/picojson.h`)

## Data Storage

**No Persistent Storage:**
- Session data not stored - connects to remote servers only
- State kept in memory only (UiState structs in `src/ui/ui_state.h`)

**Server-Side:**
- NINJAM server maintains:
  - Session tempo (BPM/BPI)
  - User list with connection metadata
  - Chat message history
  - Interval timing and synchronization
  - Recording capabilities (host-dependent)

## Authentication & Identity

**Auth Provider:**
- Custom NINJAM protocol authentication
  - Implementation: `src/core/mpb.h` - Message protocol buffer definitions
  - Classes: `mpb_server_auth_challenge`, `mpb_client_auth_user`, `mpb_server_auth_reply`
  - Method: SHA-1 hash-based challenge-response (legacy NINJAM spec)

**Credentials:**
- Username: User-provided at connection time
  - Stored in UI state: `src/ui/ui_state.h:51` (`username_input[64]`)
- Password: Optional, user-provided at connection time
  - Stored in UI state: `src/ui/ui_state.h:52` (`password_input[64]`)
  - Password hash computed via SHA algorithm (`wdl/sha.cpp`, `wdl/sha.h`)
  - Never transmitted plaintext

**License Agreement:**
- Some NINJAM servers require license acceptance
- Callback mechanism: `license_callback()` in `src/threading/run_thread.cpp:112`
- UI displays license text via `show_license_dialog` in `src/ui/ui_state.h:98`

## Monitoring & Observability

**Error Tracking:**
- None detected - No external error tracking service

**Logs:**
- Development: Console output (conditional `JAMWIDE_DEV_BUILD=1`)
- File logging: Unix systems write to `/tmp/jamwide.log`
- Connection errors: Stored in `connection_error` (UiState)
- Server list errors: Stored in `server_list_error` (UiState)

## CI/CD & Deployment

**Hosting:**
- GitHub repository: https://github.com/mkschulze/JamWide
- Release artifacts: GitHub Releases + Actions artifacts

**CI Pipeline:**
- GitHub Actions (`.github/workflows/build.yml`)
- Triggers:
  - Version tags (v*) create automatic releases
  - Manual workflow dispatch option
- Builds:
  - macOS 14 runner: arm64 + x86_64 universal binaries
  - Windows latest: x64 release builds
- Artifacts:
  - macOS: CLAP, VST3, AU .zip packages
  - Windows: CLAP, VST3 .zip packages

## Environment Configuration

**Required env vars:**
- None - All configuration is user-provided at runtime or compiled in

**Secrets location:**
- None - No API keys, tokens, or secrets used in codebase
- All external communication is:
  - HTTP public server list (no auth)
  - NINJAM protocol with user-provided credentials (no tokens)

## Webhooks & Callbacks

**Incoming:**
- None - Plugin is read-only from network perspective

**Outgoing:**
- Chat callback: `chat_callback()` in `src/threading/run_thread.cpp:26`
  - Triggered by server chat messages
  - Parses NINJAM protocol chat events
  - Updates UI state with message content/sender/type
- License callback: `license_callback()` in `src/threading/run_thread.cpp:112`
  - Triggered by servers requiring license acceptance
  - Blocks Run thread until user accepts/rejects
  - Sets `license_pending` atomic flag for synchronization

## Network Communication

**Outbound:**
- NINJAM servers (user-specified): TCP port 2049 or custom
  - Audio stream (OGG Vorbis encoded)
  - Chat messages
  - User status updates
- Public server list: HTTP GET
  - URL: `autosong.ninjam.com/serverlist.php`
  - User-Agent: "JamWide"
  - Accept header: "text/plain, application/json"

**Inbound:**
- NINJAM server:
  - Remote user audio streams
  - Chat room messages
  - Server timing/synchronization data
  - License agreement text (when required)
- Server list:
  - Server entries with host/port/user count/BPM

---

*Integration audit: 2026-03-07*
