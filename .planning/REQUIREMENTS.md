# Requirements: JamWide v1.1

**Defined:** 2026-04-05
**Core Value:** Musicians can jam together online with lossless audio quality and per-user mixing -- in any DAW or standalone.

## v1.1 Requirements

Requirements for OSC remote control and VDO.Ninja video companion.

### OSC Server

- [ ] **OSC-01**: User can receive OSC messages to control any mixer parameter (volume, pan, mute, solo)
- [ ] **OSC-02**: User can send OSC feedback to control surfaces reflecting current parameter state
- [ ] **OSC-03**: User can configure OSC send/receive ports and target IP via a settings dialog
- [ ] **OSC-04**: User can control remote users via index-based OSC addressing (`/remote/{idx}/volume`)
- [ ] **OSC-05**: User can see remote user names update on their control surface when the roster changes
- [ ] **OSC-06**: User can monitor session state (BPM, BPI, beat position, connection status) via OSC
- [ ] **OSC-07**: User can control metronome volume, pan, and mute via OSC
- [ ] **OSC-08**: User can connect/disconnect from a NINJAM server via OSC trigger
- [ ] **OSC-09**: User can see an OSC status indicator in the plugin UI (active/error/off)
- [ ] **OSC-10**: User's OSC configuration persists across DAW sessions
- [ ] **OSC-11**: User can load a shipped TouchOSC template for immediate use with JamWide

### Video Companion

- [ ] **VID-01**: User can launch VDO.Ninja video with one click from the plugin UI
- [ ] **VID-02**: User's video room ID is auto-generated from the NINJAM server address
- [ ] **VID-03**: User hears no duplicate audio from VDO.Ninja (audio suppressed automatically)
- [ ] **VID-04**: User sees all session participants in a video grid layout
- [ ] **VID-05**: User receives a privacy notice about IP exposure before first video use
- [ ] **VID-06**: User is warned if their default browser is not Chromium-based
- [ ] **VID-07**: User can pop out individual participant video into separate windows
- [ ] **VID-08**: User's video buffering syncs to NINJAM interval timing via setBufferDelay
- [ ] **VID-09**: User's video room is secured with a password derived from the NINJAM session
- [ ] **VID-10**: User can see which VDO.Ninja streams map to which NINJAM users (roster discovery)
- [ ] **VID-11**: User can control video features (open, close, mode switch, popout) via OSC
- [ ] **VID-12**: User can select a bandwidth-aware video profile (mobile/balanced/desktop)

## v2 Requirements

Deferred to future release. Tracked but not in current roadmap.

### Advanced Integration

- **ADV-01**: User can see video embedded directly in the plugin window (JUCE WebBrowserComponent)
- **ADV-02**: User can address remote users by name in OSC (`/remote/Dave/volume`)
- **ADV-03**: User can use MIDI controllers for remote control (MIDI-to-parameter mapping)
- **ADV-04**: User can connect to a self-hosted VDO.Ninja signaling server for privacy

## Out of Scope

| Feature | Reason |
|---------|--------|
| Embedded video in plugin window (WebView) | Adds 50-100MB to plugin, massive build complexity; browser companion is superior |
| Username-based OSC addressing | Names contain special chars, change mid-session, break TouchOSC layouts; index-based is stable |
| OSC auto-discovery (mDNS/Bonjour) | No standard, platform-specific, unreliable; one-time manual config is fine |
| Video recording from plugin | Browser renders video, not plugin; OBS handles this well |
| MIDI remote control | 7-bit resolution, no bidirectional feedback standard; OSC is strictly superior |
| Embedded TURN server | Only ~10% of users need TURN; VDO.Ninja's free servers are sufficient |
| H.264-over-NINJAM video (JamTaba approach) | 0.03-0.13 FPS at typical BPI; VDO.Ninja WebRTC is 30fps at 100-300ms |
| Real-time video sync with interval audio | NINJAM audio is 8-32s delayed by design; sub-second video sync is fundamentally incompatible |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| OSC-01 | Phase 9 | Pending |
| OSC-02 | Phase 9 | Pending |
| OSC-03 | Phase 9 | Pending |
| OSC-04 | Phase 10 | Pending |
| OSC-05 | Phase 10 | Pending |
| OSC-06 | Phase 9 | Pending |
| OSC-07 | Phase 9 | Pending |
| OSC-08 | Phase 10 | Pending |
| OSC-09 | Phase 9 | Pending |
| OSC-10 | Phase 9 | Pending |
| OSC-11 | Phase 10 | Pending |
| VID-01 | Phase 11 | Pending |
| VID-02 | Phase 11 | Pending |
| VID-03 | Phase 11 | Pending |
| VID-04 | Phase 11 | Pending |
| VID-05 | Phase 11 | Pending |
| VID-06 | Phase 11 | Pending |
| VID-07 | Phase 13 | Pending |
| VID-08 | Phase 12 | Pending |
| VID-09 | Phase 12 | Pending |
| VID-10 | Phase 12 | Pending |
| VID-11 | Phase 13 | Pending |
| VID-12 | Phase 12 | Pending |

**Coverage:**
- v1.1 requirements: 23 total
- Mapped to phases: 23
- Unmapped: 0

---
*Requirements defined: 2026-04-05*
*Last updated: 2026-04-05 after roadmap creation*
