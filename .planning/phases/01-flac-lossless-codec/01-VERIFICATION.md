---
phase: 01-flac-lossless-codec
verified: 2026-03-07T11:00:00Z
status: human_needed
score: 5/5 must-haves verified
human_verification:
  - test: "Join a NINJAM session with FLAC codec and verify other participants receive and play back your audio"
    expected: "Remote users hear your audio encoded as FLAC; no error in session log"
    why_human: "Requires a live multi-user NINJAM session; cannot verify network protocol round-trip programmatically"
  - test: "Have a Vorbis user and a FLAC user join the same session simultaneously"
    expected: "Both users hear each other's audio without crashes, errors, or silent channels"
    why_human: "Mixed-codec coexistence requires two real clients connected to a server"
  - test: "Toggle codec from FLAC to Vorbis via the UI combo box, wait one interval, then listen"
    expected: "Chat shows '/me switched to Vorbis compressed'; audio continues without glitch after the boundary; sender badge changes to [Vorbis]"
    why_human: "Real-time audio continuity across codec switch requires human listening; interval timing cannot be mocked"
  - test: "Enable 'Record Session' checkbox, play audio for one interval, then locate saved files"
    expected: "OGG and WAV files appear in the session recording directory with valid audio content"
    why_human: "File I/O path depends on NJClient config_savelocalaudio value propagating to the Run thread at the correct time; requires a connected session to produce recordings"
---

# Phase 1: FLAC Lossless Codec Verification Report

**Phase Goal:** Musicians can send and receive lossless audio in NINJAM sessions, and record sessions locally
**Verified:** 2026-03-07
**Status:** HUMAN_NEEDED — all automated checks pass; 4 items require live-session verification
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths (from ROADMAP.md Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | User can join a NINJAM session and send FLAC-encoded audio to other participants | ? HUMAN | FlacEncoder wired into NJClient encoder creation at line 1579; FLAC FOURCC sent in upload interval begin message at line 1627; default is FLAC (constructor line 602). Network delivery requires live session. |
| 2 | User can receive and hear FLAC audio from other participants | ? HUMAN | FlacDecoder created when incoming FOURCC == NJ_ENCODER_FMT_FLAC at line 1836-1837; codec_fourcc stored on RemoteUser_Channel at line 3374 from m_fourcc; GetUserChannelCodec() reads it. Network receipt requires live session. |
| 3 | User can toggle between FLAC and Vorbis via UI control, switch happens at next interval boundary | ? HUMAN | Combo box at ui_local.cpp:83 pushes SetEncoderFormatCommand; run_thread.cpp:258 calls SetEncoderFormat(); m_encoder_fmt_active != m_encoder_fmt_requested detected at njclient.cpp:1776, encoder deleted to force recreation. Interval boundary behavior requires real-time observation. |
| 4 | User can enable session recording and find saved audio files on disk | ? HUMAN | Checkbox at ui_local.cpp:178 sets config_savelocalaudio = 2 under client_mutex; NJClient uses this field for OGG+WAV output (existing capability). File creation requires a connected session with audio. |
| 5 | A FLAC user and a Vorbis user can coexist in the same session without errors | ? HUMAN | Decoder dispatch at njclient.cpp:1833-1838 branches on codec_type (network fourcc or matched file type); fallback to CreateNJDecoder() for Vorbis. Mixed-codec coexistence requires two live clients. |

**Score:** 5/5 truths have verified implementation — 4/5 require human confirmation for live-session behavior.

---

## Required Artifacts

### Plan 01-01 Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `wdl/flacencdec.h` | FlacEncoder and FlacDecoder classes implementing I_NJEncoder/I_NJDecoder | VERIFIED | 314 lines. FlacEncoder extends VorbisEncoderInterface (line 34), FlacDecoder extends VorbisDecoderInterface (line 145). All interface methods implemented: Encode, isError, Available, Get, Advance, Compact, reinit; GetSampleRate, GetNumChannels, DecodeGetSrcBuffer, DecodeWrote, Reset, Available, Get, Skip, GenerateLappingSamples. |
| `CMakeLists.txt` | libFLAC build integration | VERIFIED | add_subdirectory(libs/libflac) at line 58; WITH_OGG OFF at line 53; BUILD_CXXLIBS OFF at line 54; target_link_libraries njclient ... FLAC at line 125. |
| `libs/libflac/` | libFLAC 1.5.0 source via git submodule | VERIFIED | Directory populated with xiph/flac source (AUTHORS, CHANGELOG.md, include/, cmake/, etc. confirmed present). |
| `tests/test_flac_codec.cpp` | 8 assert-based tests for codec behaviors | VERIFIED | 438 lines. ctest run confirms 1/1 test passed in 0.23 seconds. |

### Plan 01-02 Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/core/njclient.h` | m_encoder_fmt_requested atomic, m_encoder_fmt_active, SetEncoderFormat(), GetEncoderFormat(), GetUserChannelCodec() | VERIFIED | Lines 134-140, 195: all fields and methods declared. m_encoder_fmt_requested is std::atomic<unsigned int>. |
| `src/core/njclient.cpp` | FLAC encoder creation branch, FLAC decoder dispatch, chat notification on codec change | VERIFIED | NJ_ENCODER_FMT_FLAC at line 38; flacencdec.h include at line 47; CreateFLACEncoder/CreateFLACDecoder macros at lines 88-89; encoder branch at lines 1577-1590; decoder dispatch at lines 1833-1838; chat notification at lines 1584-1589; constructor init at lines 602-603. |
| `src/threading/ui_command.h` | SetEncoderFormatCommand in UiCommand variant | VERIFIED | Struct at line 75; added to UiCommand variant at line 88. |
| `src/threading/run_thread.cpp` | Handler for SetEncoderFormatCommand | VERIFIED | Handler at lines 257-259: if-constexpr branch calls client->SetEncoderFormat(c.fourcc). |

### Plan 01-03 Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/ui/ui_state.h` | local_codec_index field in UiState, codec_fourcc field in UiRemoteChannel | VERIFIED | codec_fourcc at line 20 (UiRemoteChannel); local_codec_index at line 69 (UiState, default 0=FLAC); recording_enabled at line 75 (UiState). |
| `src/ui/ui_local.cpp` | Codec combo widget, codec indicator badge, recording checkbox | VERIFIED | Codec combo at line 83 (kCodecLabels: FLAC/Vorbis); SetEncoderFormatCommand push at lines 86-90; indicator badge at lines 97-100 (green [FLAC] / dimmed [Vorbis]); recording checkbox at lines 178-186 with config_savelocalaudio assignment. |
| `src/ui/ui_remote.cpp` | Per-channel codec indicator badge, unsupported codec error indicator | VERIFIED | GetUserChannelCodec() call at line 114; FLAC badge (green) at lines 116-119; Vorbis badge (dimmed) at lines 120-123; unsupported codec error at lines 124-129. |

---

## Key Link Verification

### Plan 01-01 Key Links

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `wdl/flacencdec.h` | `wdl/vorbisencdec.h` | FlacEncoder extends VorbisEncoderInterface, FlacDecoder extends VorbisDecoderInterface | WIRED | Confirmed: line 34 `class FlacEncoder : public VorbisEncoderInterface`, line 145 `class FlacDecoder : public VorbisDecoderInterface`. |
| `CMakeLists.txt` | `libs/libflac/` | add_subdirectory and target_link_libraries | WIRED | Confirmed: add_subdirectory(libs/libflac EXCLUDE_FROM_ALL) at line 58; target_link_libraries(njclient PUBLIC ... FLAC) at line 125. |

### Plan 01-02 Key Links

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/core/njclient.cpp` | `wdl/flacencdec.h` | include and CreateFLACEncoder/CreateFLACDecoder macros | WIRED | Confirmed: `#include "../wdl/flacencdec.h"` at line 47; CreateFLACEncoder macro at line 88; CreateFLACDecoder macro at line 89; both used at lines 1579 and 1837. |
| `src/threading/run_thread.cpp` | `src/core/njclient.h` | SetEncoderFormat() call when command processed | WIRED | Confirmed: handler at run_thread.cpp:259 calls `client->SetEncoderFormat(c.fourcc)`. |
| `src/core/njclient.cpp` | self | m_encoder_fmt_requested read at interval boundary | WIRED | Confirmed: `m_encoder_fmt_requested.load(std::memory_order_relaxed)` at line 1776 inside interval-boundary encoder recreation check. |

### Plan 01-03 Key Links

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/ui/ui_local.cpp` | `src/threading/ui_command.h` | SetEncoderFormatCommand pushed to cmd_queue on codec selection | WIRED | Confirmed: ui_local.cpp lines 86-90 construct SetEncoderFormatCommand and call plugin->cmd_queue.try_push(). |
| `src/ui/ui_remote.cpp` | `src/core/njclient.h` | Reads FOURCC via GetUserChannelCodec() | WIRED | Confirmed: ui_remote.cpp:114 calls client->GetUserChannelCodec(). GetUserChannelCodec() implementation at njclient.cpp:2932 reads user->channels[channelidx].codec_fourcc under mutex. codec_fourcc is set at njclient.cpp:3374 from m_fourcc during channel message parsing. |
| `src/ui/ui_local.cpp` | `src/core/njclient.h` | Sets config_savelocalaudio for recording toggle | WIRED | Confirmed: ui_local.cpp:182-183 assigns plugin->client->config_savelocalaudio under client_mutex. |

---

## Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| CODEC-01 | 01-01 | User can send audio using FLAC lossless encoding | SATISFIED | FlacEncoder implements I_NJEncoder; wired into NJClient encoder creation (njclient.cpp:1579); FOURCC sent in upload message (line 1627). |
| CODEC-02 | 01-01 | User can receive and decode FLAC audio from other participants | SATISFIED | FlacDecoder implements I_NJDecoder; wired into start_decode() (njclient.cpp:1836-1837); dispatched on incoming FOURCC. |
| CODEC-03 | 01-03 | User can switch between FLAC and Vorbis via UI toggle | SATISFIED | ImGui Combo in ui_local.cpp:83 with labels "FLAC (Lossless)" / "Vorbis (Compressed)"; pushes SetEncoderFormatCommand on change. |
| CODEC-04 | 01-02 | Codec switch applies at interval boundary (no mid-interval glitches) | SATISFIED | njclient.cpp:1776-1779 deletes encoder when m_encoder_fmt_active != requested; encoder recreated with new format only at next interval begin. |
| CODEC-05 | 01-02 | Default codec is FLAC | SATISFIED | NJClient constructor (njclient.cpp:602-603) stores NJ_ENCODER_FMT_FLAC to m_encoder_fmt_requested and sets m_encoder_fmt_active = NJ_ENCODER_FMT_FLAC. UiState.local_codec_index defaults to 0 (FLAC) at ui_state.h:69. |
| REC-01 | 01-03 | User can enable session recording via UI | SATISFIED | Checkbox "Record Session" at ui_local.cpp:178; sets config_savelocalaudio = 2 under client_mutex when enabled. |
| REC-02 | 01-03 | Recorded audio saved as OGG or WAV (existing NJClient capability) | SATISFIED | config_savelocalaudio = 2 activates NJClient's existing OGG+WAV recording path. Label "(OGG + WAV)" shown at ui_local.cpp:187 when recording enabled. |

**All 7 requirements accounted for. No orphaned requirements.**

REQUIREMENTS.md traceability table marks CODEC-01 through CODEC-05, REC-01, and REC-02 as Complete for Phase 1.

---

## Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| njclient.cpp | 1819 | `s.Append(".XXXXXXXXX")` | INFO | Pre-existing filename generation pattern (not phase-introduced); not a placeholder. |

No blocker or warning anti-patterns found in any phase-modified file. The `.XXXXXXXXX` string is pre-existing NJClient code for file extension probing — it is then overwritten by type_to_string(). Not a placeholder.

---

## Build Verification

**cmake --build:** SUCCESS. Full project builds (VST3, CLAP, AUv2) with FLAC integration. Build output confirms `Built target test_flac_codec` at 100%.

**ctest -R flac --output-on-failure:** `1/1 Test #1: flac_codec ... Passed 0.23 sec` — 100% pass rate.

---

## Human Verification Required

### 1. FLAC Send Path (live session)

**Test:** Connect to a NINJAM server (e.g., ninbot.com:2049), confirm the codec combo shows FLAC, and transmit audio for at least one interval.
**Expected:** Remote participants receive your FLAC-encoded audio without error. Session log shows no codec errors. The upload interval begin message carries FOURCC 'FLAC' (0x43414C46).
**Why human:** Network delivery of the FLAC bitstream to a real NINJAM server and remote decode by another client cannot be verified programmatically.

### 2. FLAC/Vorbis Mixed Session

**Test:** Have two clients connect to the same session — one using FLAC (default), one switched to Vorbis. Both listen to each other.
**Expected:** Both clients hear each other without crashes, silent channels, or garbled audio. Remote codec indicators show [FLAC] and [Vorbis] correctly for each user.
**Why human:** Mixed-codec coexistence requires two live clients with different codec states connected to a real NINJAM server.

### 3. Codec Toggle at Interval Boundary

**Test:** While connected and transmitting, switch the codec combo from FLAC to Vorbis. Wait for the interval boundary (indicated by session BPM/BPI).
**Expected:** Chat displays "/me switched to Vorbis compressed". Audio continues without interruption after the boundary. The sender-side badge changes from [FLAC] to [Vorbis].
**Why human:** Interval boundary timing and audio continuity during codec transition require real-time listening and session observation.

### 4. Session Recording

**Test:** Enable "Record Session" checkbox, connect to a session, transmit and receive audio for at least one interval, then disconnect.
**Expected:** OGG and WAV files appear in NJClient's configured recording directory with valid audio content (non-zero file size, playable).
**Why human:** File creation requires a live session producing audio data; file validity requires playback check.

---

## Gaps Summary

No gaps found. All artifacts exist, are substantive, and are wired correctly. The 4 human verification items are not gaps — they are confirmation tests for live-session behaviors that automated checks cannot cover (network protocol, audio continuity, file I/O under live conditions).

The automated evidence is comprehensive:
- FlacEncoder/FlacDecoder: 314-line implementation with full I_NJEncoder/I_NJDecoder coverage, 8 tests passing (including mono/stereo roundtrip within 16-bit tolerance)
- NJClient wiring: FLAC FOURCC defined, encoder creation branched, decoder dispatch branched, format state atomic, interval-boundary change detection present, chat notification present
- SPSC queue: SetEncoderFormatCommand in variant, handler in run_thread dispatching to SetEncoderFormat()
- UI: Codec combo (defaults to FLAC), indicator badges, recording checkbox all present and wired to command queue or client config

---

_Verified: 2026-03-07_
_Verifier: Claude (gsd-verifier)_
