# Project Research Summary

**Project:** JamWide JUCE Migration
**Domain:** Online music collaboration — NINJAM-protocol client (DAW plugin + standalone)
**Researched:** 2026-03-07
**Confidence:** MEDIUM-HIGH

## Executive Summary

JamWide is a NINJAM-protocol audio collaboration client that ships as a DAW plugin (VST3/AU/CLAP) and standalone application. The research consensus is clear: migrate to JUCE 8 as the single unifying framework, replacing Dear ImGui (UI), clap-wrapper (plugin formats), and platform-specific code. JUCE's `AudioProcessor` model natively covers VST3, AU, and Standalone formats in one build, provides the `AudioPlayHead` API needed for DAW transport sync, and gives the multi-bus output layout required for per-user channel routing — JamWide's primary competitive differentiator over existing NINJAM clients.

The recommended approach is a phased migration that does not break the existing NJClient core. NJClient's `AudioProc()` interface takes a raw `float**` buffer, which maps directly onto JUCE's `AudioBuffer<float>` via pointer extraction — this bridge is the migration's central seam. The existing three-thread model (audio, run, UI) maps cleanly onto JUCE idioms: `processBlock()` for audio, `juce::Thread` subclass for the NJClient run loop, and JUCE's message thread for UI via retained-mode `Component` panels. The FLAC lossless codec — JamWide's flagship feature — is independent of the JUCE migration and should ship first, before any UI or plugin-format work begins.

The top risks are threading violations (putting NJClient::Run() on the message thread freezes the GUI and can cause DAW kill), multichannel bus layout incompatibilities across DAW hosts (Logic Pro in particular caches AU configurations aggressively), and a FLAC symbol collision between JUCE's bundled libFLAC copy and any external libFLAC submodule. All three risks have known mitigations documented in PITFALLS.md and are avoidable with correct design decisions made at project start.

## Key Findings

### Recommended Stack

JUCE 8.0.12 (December 2025) is the recommended core framework, replacing Dear ImGui, clap-wrapper, and all platform-specific GUI code. It is the industry standard for cross-platform audio plugins and the only framework that natively covers VST3, AU, Standalone, and (in JUCE 9) CLAP from a single `AudioProcessor` subclass. C++20 and CMake 3.22+ are already in use and are compatible. For the NINJAM codec, standalone libFLAC 1.5.0 should be used with JUCE's bundled FLAC disabled (`JUCE_USE_FLAC=0`) to avoid linker symbol conflicts — or JUCE's bundled copy used exclusively via its `FlacNamespace::` prefix. OGG/Vorbis (libogg + libvorbis submodules) is retained for backward compatibility with all existing NINJAM clients and servers.

**Core technologies:**
- **JUCE 8.0.12**: Plugin framework, UI, audio, standalone mode — industry standard, replaces three separate systems
- **libFLAC 1.5.0 (standalone)**: Real-time stream encoding/decoding for NINJAM intervals — JUCE's file-oriented `FlacAudioFormat` is the wrong abstraction for streaming
- **libogg + libvorbis**: Retained OGG Vorbis codec — mandatory for interop with all existing NINJAM servers
- **WDL jnetlib**: Retained for NINJAM TCP protocol — too deeply integrated to replace in this milestone; migrate to `juce::StreamingSocket` in a later phase
- **Catch2 3.7.x + pluginval**: Testing — Catch2 for unit tests, pluginval for plugin format compliance in CI

### Expected Features

All table-stakes features (NINJAM connectivity, Vorbis codec, server browser, chat, per-user mixer, VU meters, metronome) are already implemented. Two table-stakes items are currently missing or broken and must be fixed: live BPM/BPI changes without reconnect (currently a regression vs. ReaNINJAM) and session recording UI (NJClient supports it, but there is no UI to expose it).

**Must have (table stakes — missing or broken):**
- **Live BPM/BPI changes without reconnect** — current JamWide requires reconnect; this is a deal-breaking regression vs. ReaNINJAM
- **Session recording UI** — NJClient already supports `config_savelocalaudio`; needs UI exposure and FLAC format support
- **Standalone application mode** — natural JUCE byproduct; opens JamWide to non-DAW users
- **JUCE migration itself** — foundation for all differentiators; unlocks standalone, multichannel, transport sync

**Should have (primary differentiators — nothing like these exists in generic NINJAM clients):**
- **FLAC lossless audio codec** — no NINJAM client offers lossless; JamWide's flagship differentiator; ships before JUCE migration
- **Multichannel output routing (per-user stereo pairs)** — only ReaNINJAM has this, but it is REAPER-only; the killer feature for DAW users
- **DAW transport sync** — reads host transport via JUCE `AudioPlayHead`; makes the plugin feel native in any DAW
- **Session position tracking** — already in NJClient API; small effort, big polish

**Defer (v2+):**
- OSC cross-DAW sync — exploratory; useful for power users but not critical path
- MCP bridge — too nascent; DAW MCP servers barely exist outside REAPER prototypes
- Video support — massive scope; research-only this milestone
- NINJAM looper, voice chat, input effects, mixer presets

### Architecture Approach

The JUCE rewrite preserves the battle-tested NJClient core and maps the existing three-thread model onto JUCE idioms without restructuring the concurrency design. `JamWideProcessor` (AudioProcessor subclass) owns NJClient, the run thread, and all shared state permanently — the editor is a transient view that is created and destroyed by the DAW host without disrupting audio or network. The audio-to-UI communication path uses atomics (for VU, BPM, beat phase) and SPSC ring buffers (for structured events) — the existing `SpscRing<T,N>` templates carry over unchanged.

**Major components:**
1. **JamWideProcessor** — AudioProcessor subclass; owns NJClient, NinjamRunThread, SPSC queues, atomic snapshot; bridges `processBlock()` to `NJClient::AudioProc()`; declares multi-bus output layout
2. **JamWideEditor** — AudioProcessorEditor subclass; Timer-based polling of event queue and atomic snapshot at 30 Hz; all UI Components as children; transient (can be destroyed and recreated without losing state)
3. **NinjamRunThread** — `juce::Thread` subclass; runs `NJClient::Run()` at ~50ms intervals; drains command queue; pushes events to UI queue
4. **NJClient (unchanged)** — Cockos core logic; NINJAM protocol, audio encode/decode, interval management; only `AudioProc()` is safe to call from the audio thread
5. **Codec Layer** — `I_NJEncoder`/`I_NJDecoder` implementations for Vorbis and FLAC; pluggable via FOURCC dispatch; codec switches only at interval boundaries
6. **UI Components** — ConnectionPanel, ChatPanel, MixerPanel, LocalChannelPanel, MasterPanel, ServerBrowser, TimingGuide, StatusBar; each a `juce::Component`

### Critical Pitfalls

1. **NJClient::Run() on the message thread** — Use a dedicated `juce::Thread` subclass; never use `juce::Timer` for network polling. JNetLib performs blocking I/O; putting it on the message thread freezes the GUI and can trigger DAW plugin kill.

2. **Locking in processBlock()** — Only `NJClient::AudioProc()` is audio-thread-safe. All other NJClient methods require `client_mutex`. Use the atomic snapshot pattern for anything `processBlock()` needs to read (BPM, VU, status). Enable ThreadSanitizer in CI.

3. **libFLAC symbol collision with JUCE's bundled FLAC** — JUCE bundles libFLAC source compiled into `juce_FlacAudioFormat.cpp`. Linking an external libFLAC submodule simultaneously produces duplicate symbols. Resolution: either disable JUCE's bundled copy (`JUCE_USE_FLAC=0`) or use only JUCE's namespaced copy exclusively via `FlacNamespace::`. Do not link both.

4. **Editor lifetime breaks network state** — The DAW destroys the editor on window close. All persistent state (chat history, connection info, mixer settings) must live in `JamWideProcessor`, not `JamWideEditor`. The editor reads state from the processor on construction.

5. **Multichannel bus layout rejected by DAW hosts** — Logic Pro caches AU bus configurations aggressively. Declare auxiliary buses as enabled (not optional/disabled) at construction. Do not override `canAddBus()`/`canRemoveBus()`. Increment AU version number on any bus layout change. Run `auval` and test in Logic Pro, REAPER, Ableton, and Bitwig.

## Implications for Roadmap

Based on research, the dependency chain drives phase order: FLAC is independent and ships first; JUCE migration is the foundation for everything else; multichannel and DAW sync require JUCE to be complete.

### Phase 1: FLAC Lossless Codec
**Rationale:** Independent of JUCE migration; detailed plan already exists (`FLAC_INTEGRATION_PLAN.md`); ships the flagship differentiator with minimal risk before the large migration begins. Low-risk, high-value, does not disrupt existing codebase.
**Delivers:** FLAC encoder/decoder for NINJAM intervals; `I_NJEncoder`/`I_NJDecoder` FLAC implementations; manual codec selection UI; session recording with FLAC format.
**Addresses:** FLAC differentiator, session recording UI (table stakes gap).
**Avoids:** libFLAC symbol collision (Pitfall 3); mid-interval codec switch corruption (Pitfall 15); FLAC encoder finish latency (Pitfall 10).

### Phase 2: JUCE Scaffolding + Plugin Skeleton
**Rationale:** The foundation phase; all subsequent phases depend on correct JUCE architecture. Must establish thread boundaries, Processor/Editor state split, bus declaration, and plugin lifecycle before any features are built on top. Critical: get the architecture right first, even if it does nothing useful yet.
**Delivers:** CMakeLists with `juce_add_plugin(VST3 AU Standalone)`; empty `JamWideProcessor`/`JamWideEditor`; `NinjamRunThread` skeleton; pluginval in CI; multi-bus output declaration (even if routing is not wired yet); `getStateInformation()`/`setStateInformation()` schema.
**Uses:** JUCE 8.0.12, CMake 3.22+, Catch2, pluginval.
**Avoids:** Pitfalls 1, 2, 4, 6, 8, 9, 12, 13 — all of which require correct decisions at project start.

### Phase 3: NJClient Integration + Audio Bridge
**Rationale:** Wire the existing NJClient core into the JUCE AudioProcessor. This is the highest-risk implementation step (bridging two threading models and two buffer formats) but has a clear pattern from the architecture research.
**Delivers:** `processBlock()` → `NJClient::AudioProc()` bridge; stereo-only output (bus 0 only); `NinjamRunThread` running `NJClient::Run()`; SPSC queues ported; Vorbis+FLAC codecs linked; connectable to NINJAM servers from standalone app; audio flowing end-to-end.
**Uses:** JUCE AudioBuffer bridging pattern, `juce::Thread`, existing `SpscRing<T,N>` templates.
**Implements:** JamWideProcessor, NinjamRunThread, Codec Layer.

### Phase 4: Core UI Panels (JUCE Components)
**Rationale:** Clean break from Dear ImGui. Rewrite UI panel-by-panel in JUCE Components, using the existing ImGui UI as visual reference only. Do not attempt hybrid ImGui/JUCE approach.
**Delivers:** ConnectionPanel, StatusBar, ChatPanel, Timer-based editor polling, `LookAndFeel_V4` subclass with dark theme.
**Addresses:** Full UI rewrite; enables connection and basic session participation.
**Avoids:** ImGui/JUCE hybrid anti-pattern (Pitfall 14).

### Phase 5: Mixer + Local Channels + Parameter Automation
**Rationale:** Complete the per-user mixing UI and expose DAW-automatable parameters (master volume, metro volume, mutes) via `AudioProcessorValueTreeState`.
**Delivers:** MixerPanel with ChannelStrip components, VU meters, LocalChannelPanel, MasterPanel, APVTS integration, DAW automation support.
**Addresses:** Per-user volume/pan/mute/solo (table stakes), VU meters, metronome controls.
**Avoids:** Parameter callback threading issues (Pitfall 9).

### Phase 6: Multichannel Output Routing
**Rationale:** JamWide's primary differentiator for DAW users. Requires JUCE migration (Phase 2-3) and mixer UI (Phase 5) to be complete for meaningful testing. Bus declarations must be designed in Phase 2 even though routing is wired here.
**Delivers:** Multi-bus `processBlock()` buffer bridging for N output buses; `config_remote_autochan` routing modes (mix-only, per-user, per-channel); routing mode UI; full DAW multi-track per-user routing.
**Addresses:** Multichannel output routing differentiator.
**Avoids:** Dynamic bus count changes (Architecture anti-pattern 2); bus layout rejection by DAWs (Pitfall 5); requires auval + multi-DAW testing.

### Phase 7: Server Browser + State Persistence
**Rationale:** Complete the connection UX with server discovery, and make all settings persist across DAW sessions. Server browser fetcher can migrate from WDL jnetlib HTTP to `juce::URL::createInputStream()` here as a clean incremental networking migration.
**Delivers:** `ServerBrowser` (juce::TableListBox), async server list fetching via JUCE URL, full `getStateInformation()`/`setStateInformation()` covering all user settings (connection, routing, codec, mixer).
**Avoids:** State save/restore dropping connection context (Pitfall 8); JNetLib/JUCE networking conflict for HTTP (Pitfall 11).

### Phase 8: DAW Sync + Live BPM/BPI + Timing Guide
**Rationale:** Polish phase that completes the DAW integration story. Live BPM/BPI is a table-stakes fix (regression vs. ReaNINJAM). DAW transport sync and session position tracking are the remaining differentiators. Full DAW integration testing requires multichannel routing (Phase 6) to be working.
**Delivers:** Live BPM/BPI changes without reconnect; `AudioPlayHead` transport sync; session position tracking; TimingGuide Component (beat grid visualization); standalone pseudo-transport fallback.
**Addresses:** Live BPM/BPI table-stakes fix; DAW transport sync and session position tracking differentiators.
**Avoids:** No AudioPlayHead in standalone (Pitfall 7); sample rate mismatch (Pitfall 6).

### Phase Ordering Rationale

- **FLAC first (Phase 1)** because it is independent, has a detailed plan, and ships the flagship differentiator without touching the migration risk.
- **Scaffolding before NJClient (Phase 2 before Phase 3)** because architectural mistakes made in Phase 2 (thread model, state split, bus declarations) propagate into everything built afterward and are expensive to fix late.
- **UI after audio bridge (Phase 4 after Phase 3)** because JUCE Components require a working JUCE project and the NJClient connection is needed to validate that UI events (connect, chat, status) flow correctly.
- **Multichannel after mixer (Phase 6 after Phase 5)** because per-user routing is meaningless without per-user mixer strips, and the mixer UI is needed to verify that audio routes to the correct output bus.
- **DAW sync last (Phase 8)** because it requires full DAW integration testing that is only possible when multichannel routing is working.

### Research Flags

Phases likely needing `/gsd:research-phase` during planning:
- **Phase 6 (Multichannel Output):** DAW-specific AU multi-bus behavior is inconsistent and poorly documented. Logic Pro caching, VST3 auxiliary bus support, and Ableton behavior all need direct empirical testing. Research host-specific workarounds before implementation.
- **Phase 8 (DAW Sync):** OSC integration and cross-DAW transport sync behavior varies significantly by host. REAPER has first-class OSC; Ableton and Logic have limited support. Needs targeted research into which sync features are feasible per-DAW.

Phases with well-documented patterns (skip research-phase):
- **Phase 1 (FLAC):** Detailed plan in `FLAC_INTEGRATION_PLAN.md`. libFLAC C API is well-documented.
- **Phase 2 (JUCE Scaffolding):** JUCE CMake API and plugin lifecycle are thoroughly documented. Pamplejuce template provides a reference.
- **Phase 3 (NJClient Integration):** The `float**` to `AudioBuffer<float>` bridge pattern is clearly defined in ARCHITECTURE.md.
- **Phase 4-5 (UI + Mixer):** Standard JUCE Component patterns; well-documented.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All core technology choices verified against official JUCE docs, CMake API, libFLAC release notes. No speculative dependencies. |
| Features | HIGH | Competitors researched directly (JamTaba GitHub, Jamulus, SonoBus, FarPlay). NJClient API verified against source. FLAC plan already exists. |
| Architecture | HIGH | JUCE AudioProcessor patterns are well-documented. Threading model validated against JUCE forum and official docs. Buffer bridging pattern is deterministic. |
| Pitfalls | MEDIUM-HIGH | Critical pitfalls verified with JUCE documentation and community sources. DAW-specific multi-bus behavior (Logic Pro, Ableton) is empirically inconsistent — treat as MEDIUM. |

**Overall confidence:** MEDIUM-HIGH

### Gaps to Address

- **libFLAC integration approach:** The PITFALLS research recommends using JUCE's bundled FLAC (via `FlacNamespace::`) to avoid symbol conflicts, but the existing `FLAC_INTEGRATION_PLAN.md` specifies an external submodule. These must be reconciled before Phase 1 implementation begins. Decision: pick one approach and commit to it. Lean toward JUCE's bundled copy for simplicity unless the `FlacNamespace::` API has missing functions required for streaming.
- **CLAP support path:** JUCE 8 requires `clap-juce-extensions` for CLAP output. JUCE 9 adds native CLAP but has no release date. Decision needed on whether to ship CLAP via extensions in Phase 2 or defer until JUCE 9. Not blocking but should be decided before Phase 2.
- **JUCE license:** `JUCE_DISPLAY_SPLASH_SCREEN=0` requires a commercial license. If JamWide is open-source AGPLv3, the splash screen must either remain or the license must be purchased. Validate the open/closed source decision before shipping.
- **Maximum user bus count:** Research suggests 8 user stereo pairs (16 channels + 2 mix = 18 total) as a reasonable default, but the optimal number needs validation against typical NINJAM session sizes and DAW channel limits. Some DAWs cap at 32 total output channels.

## Sources

### Primary (HIGH confidence)
- [JUCE GitHub Releases](https://github.com/juce-framework/JUCE/releases) — JUCE 8.0.12 latest (Dec 2025)
- [JUCE CMake API](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md) — `juce_add_plugin` reference
- [JUCE AudioProcessor docs](https://docs.juce.com/master/classAudioProcessor.html) — processBlock, getBusBuffer, BusesProperties
- [JUCE AudioPlayHead docs](https://docs.juce.com/master/classAudioPlayHead.html) — transport sync API
- [JUCE Bus Layout Tutorial](https://juce.com/tutorials/tutorial_audio_bus_layouts/) — multi-bus configuration
- [JUCE Thread docs](https://docs.juce.com/master/classThread.html) — run(), threadShouldExit()
- [JUCE AudioProcessorValueTreeState](https://docs.juce.com/master/classAudioProcessorValueTreeState.html) — parameter management
- [JUCE FlacAudioFormat docs](https://docs.juce.com/master/classFlacAudioFormat.html) — file-oriented FLAC (not for streaming)
- [FLAC 1.5.0 Release](https://github.com/xiph/flac/releases/tag/1.5.0) — libFLAC latest (Feb 2025)
- [Xiph FLAC Stream Encoder API](https://xiph.org/flac/api/group__flac__stream__encoder.html) — raw stream API for NINJAM codec
- [JamTaba GitHub](https://github.com/elieserdejesus/JamTaba) — competitor feature reference
- [Cockos NINJAM source (local)](file:///Users/cell/dev/ninjam/ninjam/njclient.h) — NJClient AudioProc API, multichannel routing internals
- [JUCE Roadmap Q3 2025](https://juce.com/blog/juce-roadmap-update-q3-2025/) — JUCE 9 CLAP support planned
- [JUCE Get JUCE (Pricing)](https://juce.com/get-juce/) — license tiers

### Secondary (MEDIUM confidence)
- [Pamplejuce template](https://github.com/sudara/pamplejuce) — JUCE 8 + Catch2 + CI reference patterns
- [JUCE Forum: Multi-bus AU plugin](https://forum.juce.com/t/multi-bus-au-plugin/53546) — AU multi-bus caveats
- [JUCE Forum: Lock-free processBlock](https://forum.juce.com/t/reading-writing-values-lock-free-to-from-processblock/50947) — atomic pattern recommendations
- [timur.audio: Locks in real-time audio](https://timur.audio/using-locks-in-real-time-audio-processing-safely) — priority inversion analysis
- [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) — CLAP format pre-JUCE 9
- [Wahjam NINJAM Protocol wiki](https://github.com/wahjam/wahjam/wiki/Ninjam-Protocol) — FOURCC codec identification
- [REAPER OSC documentation](https://www.reaper.fm/sdk/osc/osc.php) — OSC transport control patterns
- [Melatonin Inspector](https://github.com/sudara/melatonin_inspector) — JUCE UI debugging tool

### Tertiary (LOW confidence)
- [foleys_video_engine](https://github.com/ffAudio/foleys_video_engine) — JUCE + FFmpeg for video (research only, not in scope)
- [Scythe MCP REAPER](https://glama.ai/mcp/servers/@yura9011/scythe_mcp_reaper) — MCP DAW control prototype (nascent)
- [DrivenByMoss Bitwig extensions](https://github.com/git-moss/DrivenByMoss) — OSC for Bitwig (community, not official)

---
*Research completed: 2026-03-07*
*Ready for roadmap: yes*
