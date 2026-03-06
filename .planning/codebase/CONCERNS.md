# Codebase Concerns

**Analysis Date:** 2026-03-07

## Tech Debt

**Large monolithic NJClient port:**
- Issue: `src/core/njclient.cpp` is 3502 lines, containing all connection logic, encoding/decoding, and state management in a single file
- Files: `src/core/njclient.cpp`, `src/core/njclient.h` (330 lines)
- Impact: Difficult to modify or understand individual features without grasping entire system; changes risk breaking network functionality
- Fix approach: Consider breaking into logical components (encoder/decoder, message handling, connection state machine) in future refactoring; for now, additions should use wrapper functions to isolate changes

**Mutex deadlock risk in license callback:**
- Issue: `src/threading/run_thread.cpp:131` manually unlocks `client_mutex` before waiting on `license_cv`, then manually relocks it. This pattern requires precise sequencing and is fragile
- Files: `src/threading/run_thread.cpp` (license_callback function, lines 112-150)
- Impact: If UI crashes or event doesn't arrive, mutex stays unlocked; timeout at line 134 can cause subtle hangs
- Fix approach: Replace with RAII lock guard or refactor to avoid cross-thread mutex unlock pattern; consider using condition_variable with lock predicate instead of manual unlock

**Missing error handling for encoder/decoder initialization:**
- Issue: `src/core/njclient.cpp` creates VorbisEncoder and VorbisDecoder via macros but doesn't validate initialization success
- Files: `src/core/njclient.cpp` (lines 81-82 macro definitions and usage throughout)
- Impact: Silent failures if encoder/decoder allocation fails; corrupted audio or network hangs could result
- Fix approach: Add null checks after `CreateNJEncoder()` and `CreateNJDecoder()` calls; return error state to caller

## Known Bugs

**Windows keyboard input duplication (partially addressed):**
- Symptoms: Keyboard input sometimes duplicates or doesn't reach ImGui text input during fast typing
- Files: `src/platform/gui_win32.cpp` (message hook at lines 57-80, dummy EDIT subclass at lines 83-100)
- Trigger: Rapidly typing in chat or connection fields; occurs more often in REAPER than Bitwig
- Current mitigation: Message hook filters WM_CHAR/WM_KEYDOWN to dummy EDIT control; marked as "fixed" in v0.135 but edge cases may remain
- Workaround: Type slowly or paste text instead
- Recommendation: Add telemetry logging to track message flow; consider using Windows TSF (Text Services Framework) instead of manual IME handling

**macOS REAPER keyboard input requires manual setting:**
- Symptoms: Text input doesn't work in REAPER until user enables "Send all keyboard input to plug-in"
- Files: `src/platform/gui_macos.mm` (ImGui NSTextInputClient setup)
- Trigger: Plugin loads in REAPER on macOS; default keyboard routing goes to DAW not plugin
- Current mitigation: UI hint displayed in status bar telling user to enable setting
- Recommendation: Add OS host detection and enable hint only for REAPER; future version could auto-detect and hint when WantTextInput flag set

## Security Considerations

**Password stored in memory only (by design):**
- Risk: Plugin password (for server auth) lives in `src/plugin/jamwide_plugin.h:90` std::string, cleared only on disconnect
- Files: `src/plugin/jamwide_plugin.h`, `src/threading/run_thread.cpp` (password usage)
- Current mitigation: Password not saved to state file; cleared when plugin destroyed; no disk persistence
- Recommendations:
  1. Consider zeroing password memory with memset before deletion (use secure_string wrapper if possible)
  2. Document that passwords should not be sensitive shared credentials
  3. For production use, recommend empty password + public server auth

**HTTP server list fetching uses unencrypted connection:**
- Risk: Server list fetched from `ninbot.com` via HTTP, not HTTPS; could be intercepted or poisoned
- Files: `src/net/server_list.cpp` (HTTP GET at line ~80, hardcoded to http://ninbot.com/serverlist)
- Current mitigation: URL can be overridden via environment variable (not in UI yet)
- Impact: Low — server list is public and doesn't contain sensitive data; misrouting is inconvenient but not dangerous
- Recommendations:
  1. Migrate to HTTPS endpoint if ninbot.com supports it
  2. Add UI config for server list URL
  3. Implement certificate pinning if HTTPS adopted

**Keyboard/IME message hook can miss events (Windows):**
- Risk: Message hook at `src/platform/gui_win32.cpp:57` intercepts all keyboard messages at thread level; if hook unhooks unexpectedly, subsequent input goes to DAW accelerators
- Files: `src/platform/gui_win32.cpp` (lines 320-350 hook install/uninstall)
- Current mitigation: Hook installed in activate, uninstalled in deactivate; DAW accelerators still work if something goes wrong
- Recommendations:
  1. Add assertion that hook is installed when text input active
  2. Log hook install/uninstall for debugging
  3. Consider fallback to Windows TSF for more robust text handling

## Performance Bottlenecks

**Audio encoding at variable bitrate (256 kbps default):**
- Problem: Default quality set to 256 kbps in UI, but no adaptive bitrate based on network conditions
- Files: `src/ui/ui_local.cpp` (kBitrateValues array), `src/core/njclient.cpp` (quality→bitrate conversion)
- Current capacity: Works fine for local/high-bandwidth networks; may cause dropouts on poor connections
- Scaling path: Monitor upstream bandwidth; implement adaptive bitrate selection that reduces quality if packet loss detected
- Mitigation: Allow user to manually adjust bitrate; default to 96 kbps instead of 256 on first connection

**VU meter update every audio frame (many atomic reads):**
- Problem: `src/plugin/clap_entry.cpp` updates `UiAtomicSnapshot` on every call to `process()`, which happens 40-100+ times per second depending on buffer size
- Files: `src/plugin/clap_entry.cpp` (audio_proc function), `src/plugin/jamwide_plugin.h` (UiAtomicSnapshot struct)
- Current capacity: Works fine with ~100 atomics per snapshot; 40+ updates/sec OK on modern CPU
- Improvement path: Throttle snapshot updates to 30 Hz instead of per-frame; batch writes to reduce cache coherency traffic

**Vorbis encoder blocks UI thread briefly:**
- Problem: Encoding happens in run thread under `client_mutex`; large audio frames can cause noticeable latency
- Files: `src/core/njclient.cpp` (Encode() call in process_samples)
- Impact: Minimal in practice (Vorbis is fast); observable if client_mutex heavily contended
- Improvement: Profile under heavy multi-instance load; consider lock-free ringbuffer for audio if needed

## Fragile Areas

**Remote user list access without locking (by design):**
- Files: `src/ui/ui_remote.cpp` (reads `plugin->client->GetNumRemoteUsers()` and `GetRemoteUser()`)
- Why fragile: RemoteUser list in NJClient can be modified by run thread at any time; UI reads without client_mutex (design choice for performance)
- Safe modification: Always read under `std::lock_guard<std::mutex> lock(plugin->client_mutex)` or use snapshot pattern if modifying access
- Test coverage: Manual testing with multi-user servers; no automated test for concurrent user addition/removal
- Recommendation: Add compile-time guard or comment marking this as intentionally unsafe; add integration tests for user join/part during UI render

**ImGui ID collision potential across panels:**
- Files: Multiple `src/ui/*.cpp` files use ImGui widgets with string labels as IDs
- Why fragile: ImGui ID collisions happen silently if two widgets in same ImGui frame share same ID; difficult to detect
- Safe modification: Always wrap dynamic/repetitive IDs in `ImGui::PushID(unique_int)...ImGui::PopID()`
- Test coverage: Manual review via `tools/check_imgui_ids.py` script; no runtime validation
- Recommendation: Add runtime ImGui validation in dev builds; expand ID checker to detect scoped but duplicate IDs

**Client mutex unlock/wait pattern in license callback:**
- Files: `src/threading/run_thread.cpp` (license_callback, lines 131-148)
- Why fragile: Manual mutex management across thread boundary; if UI thread crashes during wait, main thread hangs
- Safe modification: Only change via condition_variable pattern; never manually unlock from different thread
- Test coverage: Tested by declining/accepting license on real servers; no timeout/cancel scenario test
- Recommendation: Add timeout test (JAMWIDE_LICENSE_TIMEOUT env var override); ensure shutdown flag checked in wait condition

## Scaling Limits

**Fixed UI window size (800x1200):**
- Current capacity: Hardcoded to 800x1200 pixels to work with Logic Pro/GarageBand AU resize limitations
- Limit: Small monitors or high DPI displays will find UI cramped; no way to resize in-app
- Scaling path: Add dynamic resize support in CLAP/VST3; keep AU at fixed size; store user preference in plugin state
- Recommendation: Implement variable window size for non-AU formats; remember user's preferred size across sessions

**Server list fetcher JNetLib HTTP (single-threaded, blocking):**
- Current capacity: Works for ~500 server entries from ninbot.com; network latency adds 100-500ms per fetch
- Limit: If ninbot.com slow/down, entire run thread blocks during HTTP GET
- Scaling path: Implement async HTTP with callback; cache server list locally with TTL
- Recommendation: Add UI progress indicator; fall back to cached list if fetch times out; implement retry with exponential backoff

**SPSC ring buffers fixed size:**
- Current capacity: Chat queue 128 messages, UI queue 256 events, command queue 256 commands
- Limit: Rapid chat spam (rare) could overflow chat_queue and drop messages; command queue overflow blocks UI
- Scaling path: Monitor queue fill levels; warn user if approaching limit; consider circular buffer with older item eviction
- Recommendation: Log overflow warnings to debug log; document recommended limits

## Dependencies at Risk

**libvorbis 1.3.7 (audio codec):**
- Risk: Ancient codec from 2020; no active security updates; only maintainer is Xiph and they've moved focus to Opus
- Impact: Security vulnerability in Vorbis decoder could crash plugin or enable RCE (low probability but high impact)
- Migration plan: FLAC integration already planned (`FLAC_INTEGRATION_PLAN.md`); use FLAC as default in future; keep Vorbis for backward compatibility
- Current mitigation: Vorbis isolated in `src/core/` via NJClient interface; decoder runs in safe plugin sandbox
- Timeline: Can stay as-is for v1.0; migrate to FLAC-first in v1.1

**WDL (Cockos library collection):**
- Risk: WDL used for mutexes, networking (JNetLib), SHA, RNG; no package manager, bundled as git submodule
- Impact: Breaking changes in WDL require manual cherry-pick; no security advisory system
- Current mitigation: Frozen submodule version; only use stable APIs (mutex, jnetlib, sha)
- Migration plan: Consider replacing WDL mutexes with C++20 std::mutex (done in plugin layer); JNetLib is harder to replace
- Recommendation: Document which WDL components are used and why; plan replacement during v2.0 refactor

**CLAP 1.2.7 (plugin API):**
- Risk: CLAP is young and evolving; API changes could break builds on new versions
- Impact: Blocking on CLAP upstream changes; frequent rebuilds needed to support new hosts
- Mitigation: Submodule pinned to v1.2.7; can pin longer if needed
- Current status: CLAP API stable for core features used by JamWide; VST3 SDK provides fallback
- Recommendation: Monitor CLAP releases monthly; plan migration to v1.3+ when LTS released

## Missing Critical Features

**No per-channel receive toggle:**
- Problem: Users cannot solo listen to one remote channel without hearing others
- Blocks: Full mixing workflow (engineers need to isolate tracks for processing)
- Recommendation: Add "mute all others" or "listen solo" per remote channel in `src/ui/ui_remote.cpp`

**No recording/export of jam session:**
- Problem: Audio goes directly to DAW output; no built-in way to export as WAV/FLAC
- Blocks: Users must record entire DAW session and then extract audio (inefficient)
- Recommendation: Add "export session audio" feature that saves decoded audio stream to disk; use libsndfile or WAV writer

**No server-side authentication / private rooms:**
- Problem: All NINJAM servers are public; no way to restrict who can join
- Blocks: Private collaborations on public servers not possible
- Note: This is NINJAM server limitation, not JamWide plugin; mention for future consideration
- Recommendation: Document that JamWide relies on server password feature (if supported by server)

**No MIDI input for metronome click toggling:**
- Problem: No way to mute metronome via MIDI learn / CC
- Impact: Mixing engineers want quick metronome toggle without UI click
- Recommendation: Expose metronome mute as CLAP parameter for MIDI mapping

## Test Coverage Gaps

**No automated unit tests:**
- What's not tested: NJClient encoding/decoding, message parsing, remote user list operations
- Files: `src/core/njclient.cpp`, `src/core/netmsg.cpp`, `src/core/mpb.cpp`
- Risk: Silent regressions if network protocol or audio codec changes; no regression tests for edge cases
- Priority: High
- Recommendation: Add CMake test target with gtest; create fixtures for common jam scenarios (user join, solo crash, license accept)

**No CI integration tests:**
- What's not tested: Actual connection to live NINJAM servers (GitHub Actions workflow doesn't test network)
- Files: All files through `src/threading/run_thread.cpp`
- Risk: Changes that break network connectivity only discovered by manual testing
- Priority: Medium
- Recommendation: Add optional nightly CI job that connects to test server; save logs for analysis

**No load testing:**
- What's not tested: Plugin behavior under heavy load (many channels, rapid chat, high audio bitrate)
- Risk: Discover performance/memory issues only in field
- Priority: Low (v1.0 already released and tested manually)
- Recommendation: Create standalone benchmark harness that simulates N remote users; profile memory/CPU

**No platform-specific edge case coverage:**
- What's not tested: Windows IME behavior with rapid input; macOS keyboard focus in Logic/GarageBand; AU resize scenarios
- Files: `src/platform/gui_win32.cpp`, `src/platform/gui_macos.mm`
- Risk: Known bugs like Windows keyboard duplication resurface without automated regression test
- Priority: Medium
- Recommendation: Add manual test checklist in memory-bank; document exact reproduction steps for each known issue

**No thread safety validation:**
- What's not tested: Concurrent access to shared state under stress; mutex deadlock detection
- Files: `src/plugin/jamwide_plugin.h`, `src/threading/run_thread.cpp`, `src/plugin/clap_entry.cpp`
- Risk: Race conditions silent in dev builds, crash in production under high load
- Priority: High (threading is core risk area)
- Recommendation: Run ThreadSanitizer in CI on macOS/Linux; add TSAN annotations to critical sections

---

*Concerns audit: 2026-03-07*
