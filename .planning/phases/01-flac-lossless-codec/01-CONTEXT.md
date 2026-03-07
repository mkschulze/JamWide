# Phase 1: FLAC Lossless Codec - Context

**Gathered:** 2026-03-07
**Status:** Ready for planning

<domain>
## Phase Boundary

Add FLAC lossless encoding/decoding to JamWide's NINJAM client with codec selection UI, compatibility indicators, and session recording support. This is a client-only change within the existing Dear ImGui + CLAP architecture (JUCE migration is Phase 2+). The server relays opaque bytes with FOURCC codec tags — no server changes needed.

</domain>

<decisions>
## Implementation Decisions

### Compatibility Handling
- Chat notification sent automatically every time the codec changes (FLAC→Vorbis or Vorbis→FLAC), so all session participants are informed
- UI indicator on sender side showing which codec is active on local channel
- UI indicator on receiver side showing which codec each remote user is sending (per remote channel)
- Error indicator on remote channels when received codec cannot be decoded (e.g., "Unsupported codec") — not silent failure, so users understand why there's silence and can communicate with the peer
- The FOURCC from incoming messages already tells us the codec — use this to populate receiver-side indicators

### Default Codec
- FLAC is the default codec (CODEC-05) — user explicitly chose this over Vorbis default
- Users can switch to Vorbis for bandwidth-constrained sessions or when peers don't support FLAC

### Claude's Discretion
- Codec selection scope for Phase 1 ImGui UI (per-channel vs global toggle) — UI-09 specifies per-channel for Phase 4 JUCE UI, but Phase 1 ImGui implementation approach is flexible
- Recording format — stick with existing NJClient `config_savelocalaudio` behavior (OGG compressed / WAV) per REC-02; no need to add FLAC recording format in this phase
- Chat notification message format and wording
- Exact visual design of codec indicator badges in ImGui
- libFLAC compression level and block size tuning

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `wdl/vorbisencdec.h`: Defines `VorbisEncoderInterface` (aliased as `I_NJEncoder`) and `VorbisDecoderInterface` (aliased as `I_NJDecoder`) — FlacEncoder/FlacDecoder must implement these exact interfaces
- `src/core/njclient.cpp`: `CreateNJEncoder()`/`CreateNJDecoder()` macros at ~line 80, `start_decode()` at ~line 1783, encoder creation in `Run()` transmit path — all need FLAC branches
- `src/core/njclient.h`: NJClient class — add `m_encoder_fmt_requested` (atomic), `m_encoder_fmt_active`, `SetEncoderFormat()` method
- `src/threading/ui_command.h`: `SetLocalChannelInfoCommand` variant — may need new command for codec selection, or extend existing
- `src/threading/run_thread.cpp`: Command processing at ~line 218 — handles `SetLocalChannelInfoCommand`
- `src/ui/ui_local.cpp`: Local channel panel with bitrate combo — codec toggle goes next to or replaces bitrate selector
- `src/core/njclient.h:120`: `config_savelocalaudio` (1=compressed OGG, 2=+WAV) — existing recording capability to expose via UI

### Established Patterns
- Lock-free SPSC command queue (UI→Run thread) for all state mutations
- Atomic values for audio-thread-accessible state
- `MAKE_NJ_FOURCC()` macro for codec identification
- Encoder/decoder created per-interval in Run() thread
- UI pushes commands via `cmd_queue.try_push()`, Run thread applies them

### Integration Points
- `CMakeLists.txt` (~line 46): Add libFLAC submodule alongside libogg/libvorbis
- `njclient.cpp` encoder creation: Branch on `m_encoder_fmt_active` FOURCC
- `njclient.cpp` `start_decode()`: Branch on incoming FOURCC for decoder creation
- `ui_local.cpp`: Add codec toggle widget
- `run_thread.cpp`: Handle new codec selection command
- Chat system: Send codec change notification via existing `SendChatCommand` pattern

</code_context>

<specifics>
## Specific Ideas

- Detailed FLAC integration plan exists at `FLAC_INTEGRATION_PLAN.md` — covers FlacEncoder/FlacDecoder class design, njclient.cpp modifications, CMake integration, fallback behavior, and bandwidth considerations
- Research recommends libFLAC 1.5.0 standalone (not JUCE bundled FlacAudioFormat) because NINJAM needs raw `FLAC__stream_encoder_init_stream()` with memory callbacks, not file-oriented AudioFormatReader/Writer
- FLAC_INTEGRATION_PLAN.md specifies native FLAC (not OGG FLAC) for simpler API and fewer dependencies
- The plan's "Default encoder = Vorbis" in the fallback section is superseded by CODEC-05 (default FLAC)

</specifics>

<deferred>
## Deferred Ideas

- FLAC capability negotiation (auto-detect peer codec support) — deferred to v2 (ADV-05)
- FLAC as session recording format — may revisit when JUCE's AudioFormatManager is available (Phase 2+)

</deferred>

---

*Phase: 01-flac-lossless-codec*
*Context gathered: 2026-03-07*
