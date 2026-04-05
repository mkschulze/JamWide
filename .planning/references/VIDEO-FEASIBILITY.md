# Video Feasibility: VDO.Ninja WebRTC Sidecar for JamWide

**Date:** April 2026
**Author:** Research deliverable for Phase 7 (DAW Sync and Session Polish)
**Requirements Addressed:** VID-01, VID-02, VID-03 (v2)
**Analysis based on VDO.Ninja as of April 2026**

---

## Goal

Evaluate the feasibility of adding video streaming to JamWide jam sessions, focusing on the VDO.Ninja WebRTC sidecar approach (per D-18). Determine whether video can be integrated alongside NINJAM audio without compromising the core audio collaboration experience, and provide a concrete recommendation for v2 implementation.

## Constraints

1. **NINJAM is audio-only by design.** The NINJAM protocol relays opaque audio bytes between clients via a server. There is no native video channel in the protocol.
2. **Server is a dumb relay.** The NINJAM server does not inspect, transcode, or route individual streams. It relays interval-sized chunks from each client to all others.
3. **Plugin UI has limited real estate.** JamWide runs as a VST3/AU plugin inside a DAW. Plugin windows are typically 800-1200px wide, shared with mixer controls, chat, and beat display.
4. **Latency characteristics differ fundamentally.** NINJAM embraces 8-32 second interval-based latency as a musical concept. Real-time video requires sub-second latency for meaningful visual interaction.

## Findings

### Approach: VDO.Ninja WebRTC Sidecar

VDO.Ninja (formerly OBS.Ninja) is an open-source, browser-based WebRTC video streaming tool designed for low-latency peer-to-peer video. It requires no software installation -- participants join via a URL with a room ID.

- **GitHub:** https://github.com/steveseguin/vdo.ninja
- **How it works:** Each participant opens a VDO.Ninja URL in a browser. WebRTC establishes peer-to-peer video streams between all participants. The VDO.Ninja signaling server handles initial peer discovery; actual video data flows directly between peers.
- **Why sidecar:** Rather than embedding video into the NINJAM protocol (which is fundamentally interval-based), VDO.Ninja runs as a parallel real-time video stream alongside NINJAM audio. Each system operates in its native latency domain.

### Music Sync Buffer Demo

VDO.Ninja includes an experimental music sync buffer demo at:
`https://vdo.ninja/alpha/examples/music-sync-buffer-demo`

- **What it does:** Implements an adaptive playout buffer specifically designed for music collaboration scenarios. It smooths jitter in WebRTC audio/video delivery to reduce glitches during musical performance.
- **How it could sync video to NINJAM intervals:** The adaptive buffer concept could be extended to align video playback with NINJAM interval boundaries. For example, video could be delayed to match the current NINJAM interval latency, so participants see each other "in time" with the interval-based audio they hear. However, this would introduce 8-32 seconds of video delay, which defeats the purpose of real-time video.
- **Practical application:** The music sync buffer is more relevant to a future scenario where NINJAM moves to lower-latency transport (v2 Opus/packetized). For v1/v2, video should remain real-time (100-300ms) and accepted as being in a different time domain than the interval audio.

**Note:** The alpha URL may change or be reorganized as VDO.Ninja development continues. Pin to a specific commit or version if integrating.

### Integration Architecture

```
                    NINJAM Server
                    (audio relay)
                         |
            +-----------+-----------+
            |                       |
     [JamWide Plugin A]      [JamWide Plugin B]
     (NINJAM audio)          (NINJAM audio)
     8-32s intervals         8-32s intervals
            |                       |
            |   VDO.Ninja           |
            |   Signaling           |
            |   Server              |
            |       |               |
     [Browser/Embed A] <--WebRTC--> [Browser/Embed B]
     (real-time video)              (real-time video)
     100-300ms latency              100-300ms latency
```

**Integration options:**

1. **Separate browser window (recommended for v2):** JamWide generates a VDO.Ninja room URL (e.g., `https://vdo.ninja/?room=jamwide-{session-id}&push={username}`) and opens it in the system browser. Room ID is shared via NINJAM chat topic or a dedicated config field. Zero browser dependency in the plugin itself.

2. **Embedded browser (future, v3+):** Use JUCE's WebBrowserComponent or CEF (Chromium Embedded Framework) to embed a VDO.Ninja view directly in the plugin window. Adds significant complexity (browser engine dependency, resource usage, cross-platform WebRTC support).

3. **Room ID sharing:** The simplest coordination mechanism is embedding the VDO.Ninja room URL in the NINJAM server topic or chat. When a user connects to a NINJAM session, the chat or topic contains the VDO.Ninja link. No protocol changes needed.

### Latency Comparison Table

| Method | Typical Latency | Frame Rate | Codec | Notes |
|--------|----------------|------------|-------|-------|
| VDO.Ninja WebRTC | 100-300ms | 30fps | VP8/VP9/H.264 | Real-time, hardware accelerated |
| JamTaba H.264-over-NINJAM | 8-32s (1 interval) | 0.03-0.13fps | H.264 | One frame per interval, unusable for interaction |
| NINJAM audio interval | 8-32s | N/A | FLAC/Vorbis | Interval-based latency is by design |

### Advantages Over JamTaba H.264

JamTaba implements video by encoding H.264 frames and sending them through NINJAM intervals alongside audio. This approach has fundamental limitations:

- **Frame rate:** One video frame per NINJAM interval means 0.03-0.13 FPS at typical BPI settings (8-32 beats per interval at 120 BPM). This is a slideshow, not video.
- **Latency:** Video frames inherit the 8-32 second NINJAM interval latency. Participants see each other with the same delay as audio, which means facial expressions and gestures are severely delayed.
- **Bandwidth competition:** H.264 frames share the NINJAM channel with audio data, potentially impacting audio quality or requiring higher bandwidth.
- **Server load:** The NINJAM server must relay larger interval chunks containing both audio and video data.

VDO.Ninja WebRTC eliminates all of these issues:

- **30fps real-time video** via WebRTC peer-to-peer connections
- **100-300ms latency** -- participants see each other in near-real-time
- **Independent bandwidth** -- video streams do not share the NINJAM channel
- **Hardware-accelerated encoding/decoding** via browser WebRTC stack
- **No server relay** -- video flows peer-to-peer, NINJAM server is unaffected

The tradeoff is that video and audio exist in different time domains: video is near-real-time while audio is interval-delayed. In practice, musicians in NINJAM sessions understand and accept interval-based latency. Having real-time video of bandmates (even if it doesn't sync to the interval audio) provides valuable social connection and visual cues for musical communication.

### Privacy and Network Implications

WebRTC video introduces privacy and network considerations that do not exist in audio-only NINJAM:

- **IP address exposure:** WebRTC uses STUN (Session Traversal Utilities for NAT) servers to discover public IP addresses and establish peer-to-peer connections. VDO.Ninja uses Google STUN servers (`stun:stun.l.google.com:19302`) by default. All participants' IP addresses are visible to other peers.
- **Self-hosted TURN recommended:** For privacy-sensitive deployments, operators should run a self-hosted TURN (Traversal Using Relays around NAT) server. TURN relays video through the server, hiding participant IPs from each other, at the cost of added latency and server bandwidth.
- **Peer-to-peer data flow:** Video streams flow directly between participants, not through the NINJAM server. This means the NINJAM server operator does not see or control video traffic. Participants must trust each other with their IP addresses.
- **Firewall/NAT traversal:** WebRTC requires UDP connectivity between peers (or fallback to TURN relay). Restrictive corporate firewalls or symmetric NATs may prevent peer-to-peer connections. TURN relay is the fallback, but adds latency and requires a relay server.
- **Camera permissions:** Browser-based WebRTC requires explicit camera permission grants. Users must understand they are sharing video with all session participants.

### Challenges

1. **Browser dependency:** The separate-window approach requires users to have a modern browser. Embedded browser adds ~50-100MB to plugin size and significant build complexity.
2. **Signaling server:** VDO.Ninja's signaling server handles WebRTC peer discovery. For production use, a self-hosted signaling server may be needed for reliability and privacy.
3. **UI real estate:** If embedded in the plugin window, video tiles compete with mixer controls, chat, and beat display. A separate window avoids this entirely.
4. **Audio-video sync complexity:** Video is 100-300ms latency; NINJAM audio is 8-32s. They cannot be meaningfully synchronized. This is a conceptual challenge for users, not a technical one.
5. **Alpha URL instability:** The music sync buffer demo is at an alpha URL that may change. Any integration should reference a specific VDO.Ninja version/commit.
6. **Multi-participant bandwidth:** Each WebRTC peer-to-peer connection requires separate upload bandwidth. With N participants, each user uploads video to N-1 peers. For large sessions (8+ users), this can exceed typical residential upload bandwidth. VDO.Ninja supports SFU (Selective Forwarding Unit) mode for larger groups, which requires a server.

## Recommendation

**Feasible as a v2 feature.** VDO.Ninja WebRTC sidecar is the correct approach for adding video to JamWide.

**Recommended implementation path:**

1. **v2 (initial):** Separate browser window. JamWide generates a VDO.Ninja room URL based on the NINJAM session and opens it in the system default browser. Room ID shared via NINJAM chat topic. Zero plugin-side complexity. Users who want video open the link; users who don't, ignore it.

2. **v3+ (future):** Embedded browser via JUCE WebBrowserComponent or CEF. Video tiles displayed in a collapsible panel within the plugin UI. Requires significant engineering investment and testing across platforms.

**Do not pursue JamTaba-style H.264-over-NINJAM.** The frame rate (0.03-0.13 FPS) makes it unusable for any meaningful visual interaction. VDO.Ninja provides 30fps at 100-300ms latency, which is fundamentally superior for the use case of seeing bandmates during a jam session.

## Open Questions

1. **Alpha URL stability:** Will the music sync buffer demo URL remain accessible? Should JamWide pin to a specific VDO.Ninja release?
2. **Self-hosted signaling server:** Is a self-hosted VDO.Ninja signaling server needed for production reliability, or is the public VDO.Ninja infrastructure sufficient?
3. **Bandwidth requirements:** What is the practical upper limit on participants for peer-to-peer video alongside NINJAM audio? When does SFU mode become necessary?
4. **Room ID coordination:** What is the best mechanism for sharing VDO.Ninja room IDs? NINJAM chat topic, a dedicated protocol extension, or a separate coordination channel?
5. **Privacy policy:** Should JamWide warn users about IP exposure when opening a VDO.Ninja link? Should the feature be opt-in with a clear privacy notice?

---

*Research deliverable for JamWide Phase 7. This document informs v2 features VID-01, VID-02, VID-03.*
