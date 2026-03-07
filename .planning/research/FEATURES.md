# Feature Landscape

**Domain:** Online music collaboration (NINJAM-protocol client with DAW plugin + standalone modes)
**Researched:** 2026-03-07
**Reference competitors:** ReaNINJAM, JamTaba, Jamulus, SonoBus, FarPlay, JackTrip

## Table Stakes

Features users expect from a modern NINJAM client. Missing any of these means users stay with ReaNINJAM or JamTaba.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| NINJAM protocol connectivity | Core function -- without this it is not a NINJAM client | N/A (exists) | TCP auth, chat, interval exchange. Already implemented. |
| OGG Vorbis encoding/decoding | Mandatory for interop with all existing NINJAM servers and clients | N/A (exists) | Default codec. Must remain even after FLAC is added. |
| Server browser with public list | All clients (ReaNINJAM, JamTaba, standalone NINJAM) have this | N/A (exists) | Already implemented. |
| Text chat | Every NINJAM client and every competitor (Jamulus, SonoBus) includes chat | N/A (exists) | Already implemented. |
| Per-user volume/pan/mute/solo | ReaNINJAM, JamTaba, Jamulus, and SonoBus all offer per-user mixer controls | N/A (exists) | Already implemented (remote channel mixing). |
| VU meters (local + remote) | Visual feedback is universal across all competitors | N/A (exists) | Already implemented. |
| Metronome with controls | Interval-based jamming is metronomic by definition; all NINJAM clients include this | N/A (exists) | Volume, pan, mute. Already implemented. |
| BPM/BPI display | Fundamental to interval-based collaboration -- all NINJAM clients show this | N/A (exists) | Already implemented. |
| Plugin format support (VST3/AU/CLAP) | JamTaba ships VST/AU. ReaNINJAM is REAPER-only. Being a generic plugin is JamWide's core value proposition. | Med | JUCE provides VST3/AU natively. CLAP via juce_clap_extensions or wrapper. Current CLAP support exists via clap-wrapper. |
| Standalone application mode | JamTaba, Jamulus, SonoBus, FarPlay all have standalone. Users without a DAW need this. | Low | JUCE's AudioAppComponent makes standalone trivial -- it is a natural byproduct of the JUCE migration. |
| Cross-platform (macOS + Windows) | All major competitors run on both. Linux is nice-to-have. | Low | JUCE handles this natively. Already cross-platform today. |
| Local channel monitoring | Musicians must hear themselves. Every competitor offers this. | N/A (exists) | Already implemented. |
| Session recording / save to disk | ReaNINJAM saves multitrack OGG/WAV per user. JamTaba saves sessions. SonoBus records multitrack. This is expected. | Med | NJClient already supports `config_savelocalaudio` (compressed OGG files + optional WAV). Needs UI exposure and FLAC format support for saved files. |
| Live BPM/BPI changes without reconnect | ReaNINJAM handles `CONFIG_CHANGE_NOTIFY` seamlessly. Current JamWide requires reconnect -- this is a deal-breaking regression vs ReaNINJAM. | Med | Must handle at interval boundaries. Already identified as active requirement. |

## Differentiators

Features that set JamWide apart from existing NINJAM clients. Not expected by default, but create competitive advantage.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| FLAC lossless audio codec | No NINJAM client currently offers lossless audio. SonoBus and FarPlay offer uncompressed/lossless but they are not NINJAM-compatible. This is JamWide's flagship differentiator. | Med | Detailed plan exists (`FLAC_INTEGRATION_PLAN.md`). Client-only change; server relays opaque bytes with FOURCC tag. Manual codec selection in v1 (no auto-negotiation). ~5-8x bandwidth vs Vorbis 64kbps. |
| Multichannel output routing (per-user stereo pairs) | ReaNINJAM has this but it is REAPER-only. No generic VST/AU NINJAM plugin offers per-user output routing. Lets DAW users mix each remote musician on separate tracks with their own effects chains. | High | Two modes from ReaNINJAM: auto-assign by user, auto-assign by channel. NJClient already has `config_remote_autochan`, `out_chan_index`, `find_unused_output_channel_pair()`. JUCE AudioProcessor supports declaring multiple output buses. |
| DAW transport sync | ReaNINJAM syncs to REAPER transport (isPlaying/isSeek/cursessionpos). No generic plugin does this. Enables interval-aligned playback and session position tracking in any DAW. | Med | NJClient::AudioProc already accepts isPlaying, isSeek, cursessionpos. JUCE AudioPlayHead provides transport state from any host. Wire them together. |
| Session position tracking | ReaNINJAM tracks session position (`GetSessionPosition()`, `GetUserSessionPos()`). Enables timeline-aware playback and recording aligned to intervals. | Low | Already in NJClient API. Needs UI display and integration with DAW transport. |
| Cross-DAW sync via OSC | Many DAWs support OSC (REAPER has first-class OSC with configurable patterns, Bitwig has community extensions). Could send tempo/transport/loop commands to the host DAW, enabling sync behaviors that plugin APIs alone cannot provide. | High | Exploratory. REAPER's OSC surface is well-documented. Bitwig has DrivenByMoss OSC. Ableton and Logic have limited OSC. Not universal but covers power users. |
| Cross-DAW sync via MCP | MCP (Model Context Protocol) servers are emerging for DAW control (e.g., Scythe MCP for REAPER). Could enable AI-assisted session management and cross-DAW communication. | High | Highly experimental. MCP is production-ready as a protocol (2025 spec) but DAW MCP servers are nascent. REAPER has early implementations. Other DAWs do not yet. Research-only for this milestone. |
| NINJAM looper (interval-synced) | JamTaba's built-in looper (1-8 layers, 3 modes: sequence/all-layers/selected) is synced to NINJAM intervals. Valuable for solo practice over backing tracks and layered performances. | Med | JamTaba proves this is popular. Could be a future milestone feature. Not in current scope. |
| Video support (H.264 over NINJAM intervals) | JamTaba has video (320x240 10fps, server-relayed via NINJAM intervals with audio/video boolean flag, FFmpeg encode/decode). Seeing fellow musicians improves the jam experience. FarPlay has built-in low-latency video. | Very High | Research-only this milestone per PROJECT.md. JamTaba's approach is proven but scope is massive (camera capture, FFmpeg dependency, UI layout for video tiles). Video sync is imperfect in JamTaba. |
| Voice chat (non-musical communication) | JamTaba v2.1.11 added voice chat. Useful for quick communication without typing. Jamulus users often run a muted Zoom call alongside for voice/video. | Med | Lower priority than core audio features. Could use a separate low-bitrate Opus stream outside NINJAM intervals, or build on existing chat infrastructure. |
| Input effects processing | SonoBus offers input compression, noise gate, and EQ. Jamulus has reverb. Helps musicians with minimal setups sound better. | Med | JUCE has built-in DSP (IIR filters, compressor). Could add optional input chain. Lower priority -- DAW users already have plugin chains. Mainly benefits standalone mode. |
| Mixer presets (save/load) | Jamulus supports saving/loading mixer configurations. Useful for users who jam with the same people regularly. | Low | Save per-user volume/pan/mute/solo state to JSON/XML. Low implementation effort, high convenience. |

## Anti-Features

Features to explicitly NOT build. These seem useful but would hurt the product.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| Peer-to-peer audio | NINJAM is server-relayed by design. P2P changes the protocol entirely, creates NAT traversal problems, and fractures the ecosystem. SonoBus and FarPlay already own this space. | Stay server-relayed. Interop with existing NINJAM servers is the priority. |
| Low-latency real-time sync (sub-50ms) | NINJAM's interval system is the opposite of real-time -- it embraces latency as a musical concept. Trying to minimize latency would undermine the core interaction model. Jamulus, FarPlay, and JackTrip own this space. | Lean into the interval model. Make it clear in UX. Optimize for musical quality within intervals, not latency reduction. |
| Mobile support | Desktop-first. Mobile audio I/O is unreliable, plugin ecosystems are immature, and screen real estate is insufficient for multichannel mixing UI. SonoBus has iOS but it is a simpler interface. | Focus on macOS and Windows. Linux as nice-to-have. Mobile is a future product, not a feature. |
| Built-in VST/AU hosting (standalone) | JamTaba standalone hosts plugins. This creates massive complexity (plugin scanning, compatibility, UI hosting, crash isolation). A NINJAM client is not a DAW. | In standalone mode, users bring their own audio routing (e.g., Loopback, VoiceMeeter). In plugin mode, the DAW hosts everything. |
| REAPER-specific extension APIs | ReaNINJAM uses REAPER extension APIs for Set Tempo, Set Loop, Start Playback. These are not portable. Building REAPER-specific features fragments the codebase. | Use OSC for DAW-specific control where possible. Standard plugin APIs (AudioPlayHead) for transport reading. |
| Capability negotiation for FLAC (v1) | Auto-detecting whether peers support FLAC adds protocol complexity and server-side changes. Premature optimization before FLAC proves its value. | Manual codec selection. Default Vorbis. User opts into FLAC knowing some peers may not decode it. Revisit negotiation in v2. |
| Video implementation (this milestone) | JamTaba's video is proven but the scope is enormous (camera capture, codec, layout, sync). Building it before core audio features are solid dilutes focus. | Research only this milestone. Document JamTaba's approach. Evaluate feasibility for a future milestone. |
| AAX plugin format | Pro Tools market share in music production is declining. AAX adds Avid's SDK requirements and signing process. JUCE supports it but the overhead is not justified for the target audience. | Ship VST3, AU, and CLAP. If demand emerges, AAX can be added later since JUCE supports it. |
| Livestreaming / broadcasting | JamKazam streams to YouTube/Twitch. FarPlay has broadcast output. This is a different product category (performance vs jam session). | Focus on session recording for post-production. Users who want to stream can use OBS to capture audio output. |

## Feature Dependencies

```
JUCE Migration ──────────────────┬──> Standalone Mode (byproduct)
                                 ├──> VST3/AU Format (JUCE native)
                                 ├──> Multichannel Output (JUCE AudioProcessor buses)
                                 ├──> DAW Transport Sync (JUCE AudioPlayHead)
                                 └──> Input Effects Processing (JUCE DSP)

FLAC Codec ──────────────────────┬──> FLAC Session Recording
                                 └──> (Independent of JUCE -- can ship before migration)

Multichannel Output ─────────────┬──> Per-User DAW Mixing
                                 └──> Metronome Channel Routing (separate output)

DAW Transport Sync ──────────────┬──> Session Position Tracking
                                 └──> Interval-Aligned Playback

Live BPM/BPI Changes ────────────┬──> (Independent -- protocol message handling)
                                 └──> Looper Sync (if looper is built)

OSC Support ─────────────────────┬──> Cross-DAW Tempo Sync
                                 └──> Cross-DAW Transport Control

Video Research ──────────────────┬──> (No code dependencies -- research artifact only)
```

Key dependency insight: FLAC is independent of JUCE and should ship first (as PROJECT.md already identifies). Multichannel output and DAW transport sync both require JUCE migration to be complete, since they depend on JUCE's AudioProcessor bus model and AudioPlayHead respectively.

## MVP Recommendation

### Must ship (table stakes that are missing or broken):

1. **FLAC lossless codec** -- Flagship differentiator. Ships before JUCE migration. Low risk, high value, detailed plan exists.
2. **JUCE migration** -- Foundation for all other differentiators. Unlocks standalone, multichannel, transport sync.
3. **Standalone mode** -- Natural JUCE byproduct. Opens JamWide to non-DAW users.
4. **Live BPM/BPI changes** -- Without this, JamWide is strictly worse than ReaNINJAM.
5. **Session recording UI** -- NJClient supports it; just needs UI exposure.

### Should ship (primary differentiators):

6. **Multichannel output routing** -- The killer feature for DAW users. No generic NINJAM plugin has this.
7. **DAW transport sync** -- Reads host transport via JUCE AudioPlayHead. Makes the plugin feel native.
8. **Session position tracking** -- Already in NJClient API. Small effort, big polish.

### Defer:

- **OSC support** -- Exploratory. Useful for power users but not critical path. Research this milestone, implement in a future one.
- **MCP bridge** -- Too nascent. DAW MCP servers barely exist outside REAPER prototypes. Research only.
- **Video** -- Research only. Massive scope. Document JamTaba's approach for future reference.
- **Looper** -- Nice-to-have. JamTaba has it. Could be a future milestone after core features stabilize.
- **Voice chat** -- Lower priority. Text chat exists. Voice adds codec and UI complexity.
- **Input effects** -- Mainly benefits standalone users. DAW users have their own effects. Future milestone.
- **Mixer presets** -- Low effort but low priority. Add when core features are stable.

## Competitive Feature Matrix

| Feature | ReaNINJAM | JamTaba | Jamulus | SonoBus | FarPlay | JamWide (target) |
|---------|-----------|---------|--------|---------|---------|-------------------|
| NINJAM protocol | Yes | Yes | No (own protocol) | No (own protocol) | No (own protocol) | Yes |
| Plugin format | REAPER-only | VST/AU | Standalone only | VST/AU/AAX/VST3 | Standalone only | VST3/AU/CLAP |
| Standalone | No | Yes | Yes | Yes | Yes | Yes (via JUCE) |
| Lossless audio | No | No | No | Yes (PCM 16/24/32) | Yes (uncompressed PCM) | Yes (FLAC) |
| Multichannel out | Yes (REAPER-only) | No | No | Yes (multi-bus) | No | Yes (generic plugin) |
| DAW transport sync | Yes (REAPER-only) | Partial | N/A | N/A | N/A | Yes (any DAW) |
| Video | No | Yes (320x240) | No | No | Yes (built-in) | Research only |
| Looper | No | Yes (interval-synced) | No | No | No | Deferred |
| Voice chat | No | Yes | No | No | No | Deferred |
| Session recording | Yes (multitrack) | Yes (multitrack) | Server-side | Yes (multitrack) | Yes (per-user) | Yes (needs UI) |
| Per-user mixing | Yes | Yes | Yes | Yes | Limited | Yes |
| Input effects | No | Via hosted plugins | Reverb only | Compressor/Gate/EQ | No | Deferred |
| OSC control | Via REAPER | No | No | No | No | Research phase |
| Cross-platform | Win/Mac/Linux | Win/Mac/Linux | Win/Mac/Linux | Win/Mac/Linux/iOS/Android | Win/Mac/Linux | Win/Mac |
| Open source | Yes (GPL) | Yes (GPL) | Yes (GPL) | Yes (GPL) | No | N/A |

## Sources

- [Cockos NINJAM official site](https://www.cockos.com/ninjam/) -- NINJAM features, interval model, recording capabilities
- [JamTaba GitHub](https://github.com/elieserdejesus/JamTaba) -- Plugin formats, platform support, video, standalone hosting
- [JamTaba Ninjam Looper wiki](https://github.com/elieserdejesus/JamTaba/wiki/Ninjam-Looper) -- Looper modes, layer management, sync behavior
- [JamTaba releases](https://github.com/elieserdejesus/JamTaba/releases) -- Voice chat feature (v2.1.11)
- [Jamulus official site](https://jamulus.io/) -- Low-latency architecture, server features
- [Jamulus software manual](https://jamulus.io/wiki/Software-Manual) -- Audio settings, channel config, mixer features, JSON-RPC API
- [SonoBus official site](https://www.sonobus.net/) -- P2P audio, codec options, effects, platform support
- [FarPlay official site](https://farplay.io/) -- Uncompressed PCM, video, low-latency approach, multitrack recording
- [REAPER OSC documentation](https://www.reaper.fm/sdk/osc/osc.php) -- OSC surface configuration, transport control patterns
- [Wahjam NINJAM Protocol wiki](https://github.com/wahjam/wahjam/wiki/Ninjam-Protocol) -- FOURCC codec identification, message structure
- [Scythe MCP REAPER](https://glama.ai/mcp/servers/@yura9011/scythe_mcp_reaper) -- MCP-based DAW control prototype
- [DrivenByMoss Bitwig extensions](https://github.com/git-moss/DrivenByMoss) -- OSC control for Bitwig
- [ReaNINJAM multichannel forum discussion](https://forum.cockos.com/showthread.php?t=233601) -- Per-user output routing in REAPER context
- [Cockos NINJAM source (local reference)](file:///Users/cell/dev/ninjam/ninjam/njclient.h) -- AudioProc API with isPlaying/isSeek/cursessionpos, config_remote_autochan, multichannel routing internals
