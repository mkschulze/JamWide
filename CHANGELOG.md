# Changelog

All notable changes to JamWide will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Phase 20 - H.264 native video broadcast (v1.3 alpha)**: Send-side video broadcast over NINJAM, bit-for-bit wire-compatible with NinjamZap mobile and the ninjamzap-core reference. Broadcast lives on channel 1 with fourCC `H264`, a 24-byte interval marker `[00 00 00 14][BE u32 swap_count][16B audio_ch0_guid]`, SPS/PPS as chunk #2, and per-frame `[4B BE length][NAL]` chunking. Highlights:
  - New abstract `jamwide::VideoEncoder` interface + `Openh264Encoder` libavcodec/libopenh264 implementation; per-preset bitrate ladder (Low 100 kbps / Medium 300 kbps / High 800 kbps); one IDR per NINJAM interval via an atomic interval-sync counter (`D-15`).
  - Broadcast toggle on the Camera button's right-click popup menu (Phase 19 UX extension) — secondary state to keep the right-cluster width within the 1200 px `kBaseWidth` budget.
  - Unconditional video-channel registration at NINJAM connect-up — receivers see "user has video capability" with channel name "video" (`SetLocalChannelInfo` + `flags=0x10`) AND fourCC H264 (`SetVideoChannel`) from the moment of connect regardless of broadcast state (`D-18`). Payload only flows after Broadcast is enabled.
  - Phase 14.3-02 RawData send substrate revised to NinjamZap-literal `WDL_PtrList<RawDataQueueItem> + WDL_Mutex m_rawdata_cs` (Plan 20-00) — multi-producer-correct under the HYBRID emission model (audio thread + encoder thread).
  - Per-channel atomic seqlock on `Local_Channel` (atomic two-uint64_t halves for the 16-byte GUID payload + parity counter) for deterministic, TSan-clean audio-thread read of the canonical audio_ch0_guid at marker construction (Plan 20-02 Must-fix 1 + R4 H8 closure).
  - END-on-broadcast-off teardown documented across three paths (R4 M11): normal broadcast-off → END at next interval (~8s bounded); Disconnect → END via audio-channel-cleanup before m_netcon teardown; plugin destruction → best-effort END via Disconnect-equivalent semantics.
  - UAT acceptance gates (5-min 2-peer broadcast at `video.ninjamzap.com:2049` per preset): `m_rawdata_sendq_high_water_mark < 32`, `contention_ratio < 1%`, `m_encoder_input_drops == 0`, audio-thread budget ≤ 200 µs worst-case, TSan dual-scope clean.
  - **Known limitations (v1.3 alpha)**: cold-start may produce a marker-only first interval if encoder warm-up exceeds the broadcast-on→first-interval window (subsequent intervals carry SPS/PPS once published); "Auto" adaptive-bitrate preset deferred to v1.4+; VideoToolbox/MediaFoundation backends architected via the abstract `VideoEncoder` interface but deferred to a follow-up phase. Receive + decode + per-peer tile rendering arrive in Phases 21/22.

- **Phase 19 - Native Camera Capture (v1.3 beta)**: New "Camera" button in ConnectionBar opens a floating preview popout with the local webcam. Quality preset (Low/Medium/High) via right-click menu, with an explicit "Stop Camera" item. Cross-platform: macOS (arm64+x86_64) + Windows x86_64. macOS adds the `com.apple.security.device.camera` entitlement; plugin Info.plist gains `NSCameraUsageDescription`. Cause-aware fallback dialog handles all denial modes including the SPARTA #82 macOS DAW-host-lacks-entitlement case (REAPER, Live, Bitwig) with softened copy that does not blame the host. Plugin state schema bumped v3 → v4 to persist popout bounds + quality preset + privacy ack. Coexists with the existing VDO.Ninja video stack during the parallel v1.3 beta — a non-blocking soft warning toast fires the first time both stacks run simultaneously.

## 1.1 Beta Series

The 1.1 line is a complete JUCE rewrite (1.0 was CLAP/ImGui). Per-beta release notes for `1.1-beta.1` through `1.1-beta.20` are on the [GitHub Releases page](https://github.com/mkschulze/JamWide/releases) and are not duplicated here. Entries are tracked below starting with `1.1-beta.20.1`.

## [1.1.0-beta.20.5] - 2026-05-05 — DIAGNOSTIC BUILD (pool + profiling)

> **Recommended diagnostic build for the multi-peer CPU-spike regression test.** Supersedes `.20.3` and `.20.4`. The `.20.4` ABTEST 1 (ring 256→32) was redesigned because reverting the size re-introduced the original cutoff bug at typical bitrates, which mixed in a different audio-drop mechanism and confused the test signal. This build keeps the 256-chunk capacity (cutoff fix preserved) and instead **pools `DecodeMediaBuffer` instances** so per-interval allocation is eliminated entirely after warm-up.

### Changed (vs .20.4)
- **ABTEST 1 redesigned**: instead of reverting `SpscRing<DecodeChunk, N>` from 256 to 32 chunks, we keep N=256 *and* introduce `DecodeMediaBufferPool`. `RemoteDownload::Open` now acquires from the pool (`new` only on cold path, when the free list is empty); `DecodeMediaBuffer::Release()` returns to the pool instead of `delete this`. `DecodeMediaBuffer::ResetForReuse()` drains any leftover SPSC chunks and zeroes per-instance counters before recycling. Pool capped at 64 instances (~64 MB resident; covers 64-peer rooms).
- **Side effect of the pivot**: cutoff bug from `.20.3`/`.20.4` is **no longer re-introduced**. The pool preserves 0e9cbae's cutoff fix.

### Same as .20.4 (still active)
- **ABTEST 2**: `broadcastBeatHeartbeat` stubbed (heartbeat baseline-CPU test). Side effect: video companion sync indicator stops updating.
- **Profiling**: `client->Run()`, `RemoteDownload::Open` acquisition, and `JamWideJuceEditor::timerCallback` instrumented with count/total_ns/max_ns counters. Surfaced in `/rcmstats` and DBG button output.

### What the profiling now reveals
With the pool, the `decbuf_alloc avg_ns` metric should trend toward **drain-and-reset cost** (sub-µs) not **mach_vm_allocate cost** (often hundreds of µs). If the spike still occurs despite this, the allocation hypothesis is falsified and we'd look elsewhere. If the spike resolves, this build is essentially the production fix candidate.

### Tester protocol
Same as .20.4: DBG at start, jam 30 min with 4+ peers, DBG at end, diff the two log files.

Key signals to look at:
- **`decbuf_alloc avg_ns`** delta across the session: pool hot path should be < 10 µs (was ~hundreds of µs on .20.2 / .20.3 cold path)
- **`client_run max_ns`**: should drop significantly if allocation churn was the spike source
- Audible glitches and remote-peer reports: should be absent if hypothesis confirmed

### v1.1-beta.20.4 status
Tag `v1.1-beta.20.4` exists on the remote but no GitHub release was created — its CI was cancelled before the release job ran, in favor of this redesign. The commit (`f36a4a6`) remains in `main`'s history as the prior diagnostic checkpoint.

## [1.1.0-beta.20.4] - 2026-05-04 — DIAGNOSTIC BUILD (with profiling)

> Same diagnostic intent as `.20.3` (CPU-spike A/B test) plus profiling counters that the log file can carry. **Recommended diagnostic build** — supersedes `.20.3` for testing.

### Added (profiling)
- `client->Run()` cycle time — count, total, max ns (run thread)
- `RemoteDownload::Open` `new DecodeMediaBuffer` allocation time — count, total, max ns (run thread)
- `JamWideJuceEditor::timerCallback` end-to-end time — count, total, max ns (message thread)
- All counters surfaced via `/rcmstats` chat command and DBG button — appended to existing diagnostic report under `--- profiling (cumulative; compare deltas) ---`

### Same as .20.3 (still active)
- ABTEST 1: SPSC ring 256 → 32 (allocation churn test)
- ABTEST 2: `broadcastBeatHeartbeat` stubbed (message-thread baseline test)

### How to use the profiling
1. Connect to a populated server, wait for steady state (4+ peers)
2. Run `/rcmstats` — note the three profiling rows; this is your **start snapshot**
3. Jam for 30 minutes
4. Run `/rcmstats` again — note the three profiling rows; this is your **end snapshot**
5. Compute deltas (end − start). Key metrics:
   - **`decbuf_alloc avg_ns`** — if it's > 100 µs (100,000 ns) on production sizing, libmalloc is going to mach_vm_allocate. Confirms hypothesis.
   - **`decbuf_alloc max_ns`** — peak single-allocation time. > 1 ms means a single alloc was a real spike.
   - **`client_run max_ns`** — peak run-thread cycle. > 5 ms means audio data was withheld from the encoder upload.
   - **`timer_cb avg_ns`** — message-thread baseline cost. Compare ABTEST-2-on vs ABTEST-2-off builds.

### Tester protocol
Side-by-side test: `v1.1-beta.20.2` (no fixes, no profiling) vs `v1.1-beta.20.4` (both fixes + profiling). Both should run against the same room population for the same duration. Note: only .20.4 will show profiling — but the symptom-presence/absence comparison itself is the primary signal.

## [1.1.0-beta.20.3] - 2026-05-04 — DIAGNOSTIC BUILD

> **⚠ This is not a regular beta.** It is a diagnostic build for the multi-peer CPU-spike investigation tracked in `.planning/debug/cpu-spikes-beta12-regression.md`. Two suspect code paths are temporarily neutralized to confirm whether they are the regression source. Do **not** use this build for normal jamming — at high bitrates the original audio-cutoff bug that beta.20.1 fixed is intentionally re-introduced. Use beta.20.2 for normal use.

### Diagnostic changes (NOT shipping fixes)
- **ABTEST 1**: `DecodeMediaBuffer`'s SPSC ring temporarily reverted from 256 → 32 chunks. Confirms whether per-peer-per-interval 1 MB heap allocation churn (introduced by beta.20.1's ring bump) is the cause of audible CPU spikes that grow with peer count. Side effect: original interval-overflow cutoff bug returns at high bitrates (≥192 kbps stereo on 12 s intervals); `decbuf_drops` will climb in `/rcmstats`.
- **ABTEST 2**: `broadcastBeatHeartbeat` call in `JamWideJuceEditor::timerCallback` is stubbed. Confirms whether ~1.5 Hz JSON build + WebSocket send on the message thread is the source of the baseline CPU bump that started showing in beta.12. Side effect: video companion sync indicator stops updating; no other functional impact.

### Notes for testers
- A/B compare baseline (beta.20.2) against this build under the same load (4+ peers, 30+ min session)
- Watch for: (a) audible-glitch cadence change, (b) Activity Monitor CPU% baseline, (c) `decbuf_drops` counter via `/rcmstats`
- Report findings to inform the proper fix (pool DecodeMediaBuffer + rate-limit/move heartbeat off message thread)

## [1.1.0-beta.20.2] - 2026-05-03

### Fixed
- **Chat**: Scrolling back through history is now sticky — new messages no longer yank the viewport down to a hidden caret
- **Windows**: Build unbroken (`std::strftime` collision with WDL macro from beta.20.1's diagnostic-counter additions)

## [1.1.0-beta.20.1] - 2026-05-03

### Added
- **Diagnostics**: DBG button in UI dumps counters to a log file for bug reports
- **Diagnostics**: `/rcmstats` chat command types remote-channel-mirror counters into chat for live triage
- **Diagnostics**: Local-channel mirror snapshot and `IsNetConnected` accessor for UI use without piercing audio-thread state

### Changed
- **Audio**: `DecodeMediaBuffer`'s SPSC ring grew from 32 to 256 chunks for more head-room under codec/network timing variance

### Fixed
- **UI**: VU meter scale now matches the fader scale (was using a different dB-to-pixel mapping)
- **UI**: Chat auto-scrolls on new messages
- **UI**: Local strip TX button visual stays in sync with `localTransmit[0]`
- **UI**: Strip-keyed remote params reset cleanly on plugin load (no more ghost values from prior sessions)
- **Audio**: `dump_samples` skip-debt accumulation removed; codec underruns recover cleanly without biasing subsequent intervals
- **Network**: `PeerChannelInfoUpdate` now wired through the SPSC mirror, fixing stale UI when a remote user renames a channel mid-session

## [1.0.0] - 2026-01-14

### 🎉 First Stable Release

JamWide 1.0 is the first stable release of the NINJAM client plugin.

### Highlights
- Full NINJAM protocol support (connect, transmit, receive audio)
- Cross-platform: macOS (Intel + Apple Silicon) and Windows
- Plugin formats: CLAP, VST3, Audio Unit v2
- Tested in: Ableton Live, REAPER, Bitwig Studio, Logic Pro, GarageBand

### All Features
- Server browser with live user lists (autosong.ninjam.com)
- Real-time chat with message history and timestamps
- Visual timing guide for beat alignment
- Per-channel volume, pan, mute, and solo controls
- VU meters for all channels
- BPM/BPI voting via chat commands
- Anonymous login support
- 256 kbps default audio quality (OGG/Vorbis)
- State persistence (save/load with DAW projects)
- Parameter automation (master volume/mute, metronome volume/mute)

### Changed
- Removed unused REAPER integration code (hwnd_info API)
- Cleaned up final NINJAM→JamWide naming references

### Fixed
- All known issues from beta testing resolved

## [0.133] - 2026-01-14

### Fixed
- **All Platforms**: Solo channel no longer crashes (mutex deadlock fix)
- **Windows**: Fixed keyboard duplication in text fields (keys no longer repeat)

## [0.131] - 2026-01-13

### Added
- **macOS/REAPER**: Show hint to enable "Send all keyboard input to plug-in" for full keyboard support
- **Chat**: Input field now keeps focus after sending a message

### Fixed
- **macOS**: Simplified keyboard handling - removed experimental swizzle/monitor code
- **Chat**: Send button positioning improved (no longer clips at edge)

## [0.119] - 2026-01-12

### Fixed
- **Windows**: Message hook implementation prevents DAW accelerators from triggering during text input
- **Windows**: Spacebar no longer triggers DAW transport when typing in text fields
- **Windows**: Caps Lock now works correctly in text fields (Bitwig/REAPER)

### Changed
- **System Requirements**: Windows 10 or later now required (Windows 7/8 no longer supported)

## [0.117] - 2026-01-12

### Added
- **Windows**: Dummy EDIT control for proper keyboard focus signaling to DAW
- **Windows**: IME support for Japanese/Chinese/Korean keyboard input
- **Windows**: Focus event forwarding (WM_SETFOCUS/WM_KILLFOCUS)

### Fixed
- **Windows**: Keyboard input now works correctly in text fields
- **Windows**: Added null guard for orig_edit_proc_ to prevent crashes if subclassing fails

## [0.116] - 2026-01-12

### Changed
- **Windows**: Initial keyboard focus implementation with dummy EDIT control

## [0.108] - 2026-01-12

### Fixed
- UI: Transmit toggle now visible (layout fix)

## [0.107] - 2026-01-11

### Fixed
- License dialog now responds to single click instead of requiring double-click

## [0.106] - 2026-01-11

### Changed
- Default audio quality increased to 256 kbps (highest quality)

## [0.105] - 2026-01-11

### Added
- Server browser now displays usernames from autosong.ninjam.com

## [0.104] - 2026-01-11

### Changed
- Audio Unit window size fixed at 800x1200 for Logic Pro/GarageBand compatibility
- Implemented setFrameSize handler for AU

## [0.90] - 2026-01-10

### Added
- Visual timing guide with beat grid and transient dots
- Chat room with message history, timestamps, and input field
- ImGui ID collision fixes throughout UI
- Release automation script (release.sh)

### Fixed
- Anonymous login now auto-prefixes "anonymous:" for public servers

## [0.1.0] - 2026-01-07

### Added
- Initial CLAP plugin implementation
- NINJAM client core ported from ReaNINJAM
- Cross-platform GUI (ImGui + Metal/D3D11)
- Server browser with live server list
- Connection management (connect/disconnect)
- Local channel controls (volume/pan/mute/transmit)
- Remote user channels with per-channel controls
- Master and metronome controls
- VU meters for all channels
- License agreement dialog
- State persistence (JSON save/load)
- Parameter automation support (4 params)
- Multi-instance support
- Thread-safe architecture with command queues
- Windows build system (Visual Studio 2022+, PowerShell)
- macOS build system (Xcode, bash)
- GitHub Actions CI/CD for automated builds

### Supported
- Plugin formats: CLAP, VST3, Audio Unit v2
- Platforms: macOS 10.15+, Windows 10+
- DAWs: Logic Pro, GarageBand, Bitwig Studio, REAPER, and more
