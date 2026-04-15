# Instamode Video Sync — Design Document

**Date:** 2026-04-15
**Status:** Design approved, not yet planned as a phase

## Problem

Video from VDO.Ninja arrives near-realtime (~100ms via WebRTC), but NINJAM audio is buffered for a full interval (e.g., 4 seconds at 120 BPM / 8 BPI). Remote users' video appears seconds ahead of their audio. The current workaround calculates delay from BPM/BPI (`60/BPM * BPI * 1000ms`) but doesn't account for actual network latency, server relay time, or codec processing.

## Solution

Use NINJAM's instamode channel (flag `0x2`) as a latency probe. Measure the actual time difference between realtime audio arrival and interval-buffered playback. Send this measured delay to the VDO.Ninja companion page, which buffers video by that amount before displaying.

## Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Probe visibility | Hidden channel (`0x80 \| 0x2` = filler + instamode) | Other clients ignore filler channels. No visible new channel. |
| Measurement frequency | Once at video start | Interval timing is deterministic. Re-measure only on BPM/BPI change. |
| Probe signal | Encoded timestamp | Sender embeds interval counter/timestamp in instamode audio data. Receiver compares to own interval position. Most precise. |
| Video buffer UX | Black + fade in | Companion shows loading state while buffering, fades in when sync achieved. |
| Architecture | Plugin-side only | Single measurement point (no coordination bugs). Companion receives delay via existing WebSocket message. Falls back to BPM/BPI calc on failure. |

## Architecture

```
JamWide Plugin (measurement)
├── Sender side:
│   └── Hidden instamode channel (0x80|0x2)
│       └── Encodes timestamp at interval boundary
│
├── Receiver side:
│   ├── Detects instamode data from remote probe channels
│   ├── Decodes timestamp, computes: delay = interval_play_time - instamode_arrival
│   └── Sends {"type":"bufferDelay","delayMs": measured_delay} to companion
│
└── Fallback:
    └── If no instamode data received, use BPM/BPI calculation (current behavior)

VDO.Ninja Companion (display)
├── Receives bufferDelay message (same as today)
├── Holds incoming video in ring buffer
├── Shows black/loading state during initial buffer fill
└── Fades in video when buffer reaches target delay
```

## Measurement Protocol

### Sender (all JamWide clients with video enabled)

1. On video start: add a hidden local channel with flags `0x80 | 0x2`
   - `0x80` = filler/inactive (other clients' UIs ignore it)
   - `0x02` = instamode (bypasses interval buffering)
2. At each interval boundary (`on_new_interval`):
   - Encode the current `m_loopcnt` (local interval counter) + wall-clock timestamp into a small audio frame
   - Send on the instamode channel (64-byte blocks, minimal bandwidth)
3. Between boundaries: send silence (or nothing) to minimize bandwidth

### Receiver (measuring client)

1. On receiving instamode data from a remote user's probe channel:
   - Decode the embedded timestamp/interval counter
   - Record `t_insta` = current wall-clock time
2. On next local interval boundary:
   - Record `t_interval` = current wall-clock time when buffered audio starts playing
3. Compute: `measured_delay = t_interval - t_insta + interval_length`
   - The `+ interval_length` accounts for the fact that the audio you're about to hear was recorded one interval ago
4. Take 2-3 measurements, average, send to companion

### Why This Works

```
Remote user plays note at time T
  │
  ├─► Instamode probe: arrives at T + network_latency (~50-100ms)
  │   Receiver records: t_insta = T + network_latency
  │
  ├─► Interval audio: buffered, arrives at next interval boundary
  │   Receiver plays: t_interval = T + network_latency + interval_buffer_time
  │
  └─► Video (WebRTC): arrives at T + webrtc_latency (~100ms)
      
measured_delay ≈ interval_buffer_time (the dominant factor)
Video delayed by measured_delay → synced with audio playback
```

## Companion Page Changes

### Initial Buffer State

```
User clicks "Start Video"
  → Companion connects to VDO.Ninja room
  → Shows black overlay with "Syncing video..." text
  → Receives bufferDelay from plugin (measured or BPM/BPI fallback)
  → Starts accumulating video frames in a ring buffer
  → After buffer fills to target delay:
    → Fade in video (CSS transition, ~500ms)
    → From this point: video plays from buffer, audio plays from interval
    → Both are aligned
```

### Ring Buffer Design

- Hold `measured_delay_ms / frame_interval_ms` frames per remote user
- At 30fps with 4000ms delay: ~120 frames per user
- Memory: ~120 * 50KB (compressed) ≈ 6MB per user (manageable)
- On BPM/BPI change: re-measure, resize buffer, brief re-sync

## Edge Cases

| Case | Handling |
|------|----------|
| No remote users have probe channels | Fall back to BPM/BPI calculation (current behavior) |
| User starts video before connecting to NINJAM | Show video immediately (no audio to sync to), apply delay once NINJAM connects |
| BPM/BPI changes mid-session | Re-measure delay, resize video buffer, brief fade-out/in |
| Very high BPI (e.g., 64 at 120 BPM = 32 seconds) | Large video buffer needed. May want to cap or warn user. |
| User has no webcam (audio-only participant) | Probe channel still useful for other users' video sync |
| Multiple remote users | Single room-level measurement suffices (interval timing dominates per-user network jitter) |

## NINJAM Protocol Notes

- Channel flags are set via `CLIENT_SET_CHANNEL_INFO` message
- `0x80` flag = filler/inactive channel (server relays but clients ignore in UI)
- `0x02` flag = instamode (128-byte prebuffer, 64-byte encoding blocks)
- Combined `0x82` should work — flags are bitwise, server processes independently
- Instamode data uses same codec (Vorbis) but with much smaller blocks
- No protocol changes needed — uses existing NINJAM features

## Impact on Existing Code

| Component | Change |
|-----------|--------|
| `NJClient` / `NinjamRunThread` | Add hidden probe channel on video start, encode timestamp at interval boundary |
| `JamWideJuceProcessor` | Receive + decode instamode data, compute delay, send to VideoCompanion |
| `VideoCompanion` | Send measured delay (replaces BPM/BPI calc when available) |
| Companion page (JS) | Add ring buffer for video frames, loading state, fade-in transition |
| Existing `bufferDelay` message | No change — same WebSocket message, just more accurate value |

## Open Questions for Implementation

1. **Encoding format:** How to pack timestamp into instamode audio frames? Options: LSB steganography, header bytes, or use the GUID field in upload messages.
2. **Flag compatibility:** Verify that `0x80 | 0x02` works on existing NINJAM servers (ninbot, etc.) — the filler flag should suppress subscription but instamode flag should still relay data.
3. **Bandwidth:** Estimate overhead of probe channel (should be minimal at 64-byte blocks).
4. **Phase 12.1 integration:** Current video sync uses a manual slider override. The measured delay should feed into the same system — auto mode uses measurement, manual mode uses slider.
