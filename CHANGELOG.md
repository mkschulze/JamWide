# Changelog

All notable changes to JamWide will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 1.1 Beta Series

The 1.1 line is a complete JUCE rewrite (1.0 was CLAP/ImGui). Per-beta release notes for `1.1-beta.1` through `1.1-beta.20` are on the [GitHub Releases page](https://github.com/mkschulze/JamWide/releases) and are not duplicated here. Entries are tracked below starting with `1.1-beta.20.1`.

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
