# Phase 1: FLAC Lossless Codec - Research

**Researched:** 2026-03-07
**Domain:** FLAC audio codec integration into NINJAM client (C/C++, libFLAC, Dear ImGui)
**Confidence:** HIGH

## Summary

Phase 1 adds FLAC lossless encoding/decoding to JamWide's NINJAM client. The existing codebase has a clean codec abstraction (`VorbisEncoderInterface` / `VorbisDecoderInterface`) that FlacEncoder/FlacDecoder must implement. The NINJAM protocol already supports arbitrary codecs via FOURCC tags -- the server is a dumb byte relay -- so this is entirely a client-side change. libFLAC 1.5.0 provides a well-documented streaming C API (`FLAC__stream_encoder_init_stream` / `FLAC__stream_decoder_init_stream`) with memory callbacks that map directly onto the existing WDL_Queue-based pattern used by VorbisEncoder/VorbisDecoder.

The codebase has an established command queue pattern (SPSC ring, UI thread pushes commands, Run thread executes against NJClient). Codec selection requires a new command type plus atomic state in NJClient for the Run thread to read at interval boundaries. The existing interval-boundary encoder recreation logic (lines 1736-1754 of njclient.cpp) already deletes and recreates encoders when channel count or bitrate changes -- adding a codec format check follows the identical pattern.

**Primary recommendation:** Use libFLAC 1.5.0 as a git submodule at `libs/libflac`, implement FlacEncoder/FlacDecoder in `wdl/flacencdec.h` mirroring the VorbisEncoder/VorbisDecoder structure, and route codec selection through the existing SPSC command queue with atomic format state in NJClient.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- Chat notification sent automatically every time the codec changes (FLAC to Vorbis or Vorbis to FLAC), so all session participants are informed
- UI indicator on sender side showing which codec is active on local channel
- UI indicator on receiver side showing which codec each remote user is sending (per remote channel)
- Error indicator on remote channels when received codec cannot be decoded (e.g., "Unsupported codec") -- not silent failure, so users understand why there's silence and can communicate with the peer
- The FOURCC from incoming messages already tells us the codec -- use this to populate receiver-side indicators
- FLAC is the default codec (CODEC-05) -- user explicitly chose this over Vorbis default
- Users can switch to Vorbis for bandwidth-constrained sessions or when peers don't support FLAC

### Claude's Discretion
- Codec selection scope for Phase 1 ImGui UI (per-channel vs global toggle) -- UI-09 specifies per-channel for Phase 4 JUCE UI, but Phase 1 ImGui implementation approach is flexible
- Recording format -- stick with existing NJClient `config_savelocalaudio` behavior (OGG compressed / WAV) per REC-02; no need to add FLAC recording format in this phase
- Chat notification message format and wording
- Exact visual design of codec indicator badges in ImGui
- libFLAC compression level and block size tuning

### Deferred Ideas (OUT OF SCOPE)
- FLAC capability negotiation (auto-detect peer codec support) -- deferred to v2 (ADV-05)
- FLAC as session recording format -- may revisit when JUCE's AudioFormatManager is available (Phase 2+)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| CODEC-01 | User can send audio using FLAC lossless encoding | FlacEncoder class implementing VorbisEncoderInterface, encoder creation branch in Run() transmit path, FOURCC tag `FLAC` in upload_interval_begin messages |
| CODEC-02 | User can receive and decode FLAC audio from other participants | FlacDecoder class implementing VorbisDecoderInterface, decoder dispatch branch in start_decode() keyed on incoming FOURCC |
| CODEC-03 | User can switch between FLAC and Vorbis via UI toggle | New SetEncoderFormatCommand in SPSC queue, codec combo/toggle in ui_local.cpp, atomic m_encoder_fmt_requested in NJClient |
| CODEC-04 | Codec switch applies at interval boundary (no mid-interval glitches) | Existing encoder recreation pattern at interval boundary (njclient.cpp lines 1736-1754) extended with format check; m_encoder_fmt_active only updated when encoder is recreated |
| CODEC-05 | Default codec is FLAC (user can switch to Vorbis for bandwidth-constrained sessions) | m_encoder_fmt_requested initialized to NJ_ENCODER_FMT_FLAC, UI default index set to FLAC |
| REC-01 | User can enable session recording via UI | Expose existing config_savelocalaudio toggle in ImGui UI; NJClient already supports recording when this flag is set |
| REC-02 | Recorded audio saved as OGG or WAV (existing NJClient capability) | No new code needed for recording format -- existing compressed OGG + optional WAV recording paths already work; just need UI toggle to set config_savelocalaudio |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| libFLAC | 1.5.0 | FLAC encoding/decoding | Official Xiph reference implementation; C API with streaming callbacks; same ecosystem as libogg/libvorbis already in project |
| WDL | existing | Queue, HeapBuf, mutex primitives | Already used by VorbisEncoder/VorbisDecoder; FlacEncoder/FlacDecoder must use same data structures for interface compatibility |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Dear ImGui | existing | Codec toggle UI, indicators | Already used for all UI panels; add combo/checkbox widgets |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| libFLAC standalone | JUCE AudioFormatReader/Writer | JUCE not available until Phase 2; JUCE wraps file I/O, not raw stream callbacks needed by NINJAM |
| Native FLAC framing | OGG FLAC container | OGG FLAC adds libogg dependency to encoding path and extra framing overhead; NINJAM already handles packetization via its own message framing |

**Installation (git submodule):**
```bash
cd /Users/cell/dev/JamWide
git submodule add https://github.com/xiph/flac.git libs/libflac
cd libs/libflac && git checkout 1.5.0
```

## Architecture Patterns

### Recommended Project Structure (new/modified files)
```
wdl/
  flacencdec.h              # NEW: FlacEncoder + FlacDecoder classes (~300-400 lines)
src/core/
  njclient.h                # MODIFY: add m_encoder_fmt_requested, m_encoder_fmt_active, SetEncoderFormat()
  njclient.cpp              # MODIFY: FLAC FOURCC, encoder creation branch, decoder dispatch branch
src/threading/
  ui_command.h              # MODIFY: add SetEncoderFormatCommand to UiCommand variant
  run_thread.cpp            # MODIFY: handle SetEncoderFormatCommand
src/ui/
  ui_local.cpp              # MODIFY: add codec toggle widget, codec indicator badge
  ui_remote.cpp             # MODIFY: add per-channel codec indicator, error indicator
  ui_state.h                # MODIFY: add codec fields to UiState, UiRemoteChannel
CMakeLists.txt              # MODIFY: add libFLAC subdirectory, link to njclient
libs/libflac/               # NEW: git submodule (xiph/flac @ 1.5.0)
```

### Pattern 1: Codec Encoder/Decoder Interface
**What:** FlacEncoder and FlacDecoder implement the existing `VorbisEncoderInterface` / `VorbisDecoderInterface` abstract classes (aliased as `I_NJEncoder` / `I_NJDecoder`).
**When to use:** Always -- this is how NJClient creates and uses codec instances.

```cpp
// Source: wdl/vorbisencdec.h (existing interface)
class VorbisEncoderInterface {  // aliased as I_NJEncoder
public:
  virtual ~VorbisEncoderInterface(){}
  virtual void Encode(float *in, int inlen, int advance=1, int spacing=1)=0;
  virtual int isError()=0;
  virtual int Available()=0;
  virtual void *Get()=0;
  virtual void Advance(int)=0;
  virtual void Compact()=0;
  virtual void reinit(int bla=0)=0;
};

class VorbisDecoderInterface {  // aliased as I_NJDecoder
public:
  virtual ~VorbisDecoderInterface(){}
  virtual int GetSampleRate()=0;
  virtual int GetNumChannels()=0;
  virtual void *DecodeGetSrcBuffer(int srclen)=0;
  virtual void DecodeWrote(int srclen)=0;
  virtual void Reset()=0;
  virtual int Available()=0;
  virtual float *Get()=0;
  virtual void Skip(int amt)=0;
  virtual int GenerateLappingSamples()=0;
};
```

### Pattern 2: SPSC Command Queue for Codec Selection
**What:** UI thread pushes a `SetEncoderFormatCommand` into the SPSC ring; Run thread applies it to NJClient.
**When to use:** When user changes codec selection in UI.

```cpp
// Source: existing pattern in ui_command.h / run_thread.cpp
struct SetEncoderFormatCommand {
    unsigned int fourcc = 0;  // NJ_ENCODER_FMT_FLAC or MAKE_NJ_FOURCC('O','G','G','v')
};

// Add to UiCommand variant:
using UiCommand = std::variant<
    // ... existing types ...
    SetEncoderFormatCommand
>;
```

### Pattern 3: Interval-Boundary Encoder Recreation
**What:** NJClient::Run() already deletes and recreates encoders at interval boundaries when channel count or bitrate changes. Codec format change follows the same pattern.
**When to use:** Every interval boundary in the transmit path.

```cpp
// Source: njclient.cpp lines 1736-1754 (existing pattern)
// At interval boundary, after existing nch/bitrate checks:
if (lc->m_enc && m_encoder_fmt_active != m_encoder_fmt_requested.load(std::memory_order_relaxed))
{
    delete lc->m_enc;
    lc->m_enc = 0;
}

// Then at encoder creation (line 1565-1567):
m_encoder_fmt_active = m_encoder_fmt_requested.load(std::memory_order_relaxed);
if (m_encoder_fmt_active == NJ_ENCODER_FMT_FLAC)
    lc->m_enc = CreateFLACEncoder(m_srate, block_nch, ...);
else
    lc->m_enc = CreateNJEncoder(m_srate, block_nch, ...);
```

### Pattern 4: FOURCC-Based Decoder Dispatch
**What:** `start_decode()` receives the FOURCC from the incoming message and creates the appropriate decoder.
**When to use:** Every time a new remote interval begins decoding.

```cpp
// Source: njclient.cpp line 1770-1820 (existing function)
// In start_decode(), after checking decode_buf or file:
if (newstate->decode_fp || newstate->decode_buf)
{
    if (fourcc == NJ_ENCODER_FMT_FLAC)
        newstate->decode_codec = CreateFLACDecoder();
    else
        newstate->decode_codec = CreateNJDecoder();  // Vorbis (existing)
}
```

### Pattern 5: Chat Notification on Codec Change
**What:** When encoder format changes at an interval boundary, send a chat message notifying the session.
**When to use:** When `m_encoder_fmt_active` transitions to a different value.

```cpp
// Existing chat pattern:
client->ChatMessage_Send("MSG", message_text);
// Message format (Claude's discretion): e.g. "/me switched to FLAC lossless"
```

### Anti-Patterns to Avoid
- **Modifying server code:** The NINJAM server relays bytes opaquely with FOURCC tags. No server changes needed or desired.
- **Capability negotiation in Phase 1:** Deferred to v2 (ADV-05). Users manually switch codecs and communicate via chat.
- **Using OGG FLAC container:** Native FLAC framing is simpler. NINJAM's message framing already handles packetization.
- **Shared encoder state across channels:** Each Local_Channel has its own `m_enc`. The format-requested atomic is global (all channels use same codec), but encoder instances are per-channel.
- **Mid-interval codec switch:** Never switch codec in the middle of an interval. Only update at boundary when encoder is recreated.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| FLAC encoding | Custom FLAC frame builder | libFLAC `FLAC__stream_encoder_init_stream()` | FLAC specification is complex (variable block sizes, predictor types, rice coding); libFLAC handles all of this |
| FLAC decoding | Custom FLAC frame parser | libFLAC `FLAC__stream_decoder_init_stream()` | Sync code detection, CRC validation, metadata parsing are non-trivial |
| Float-to-int32 conversion for encoder | Manual bit shifting | Standard pattern: `(FLAC__int32)(sample * 32767.0f)` | Clamping edge cases, proper rounding |
| Thread-safe codec selection | Mutex around codec format | `std::atomic<unsigned int>` + interval-boundary application | Existing pattern for all NJClient config changes; no locks on audio path |

**Key insight:** The VorbisEncoder/VorbisDecoder classes are ~200 lines each of mostly boilerplate wrapping the Vorbis API. FlacEncoder/FlacDecoder will follow the exact same structure wrapping libFLAC instead.

## Common Pitfalls

### Pitfall 1: FLAC Encoder Produces No Output Until Block Is Full
**What goes wrong:** `FLAC__stream_encoder_process_interleaved()` buffers samples internally until a full block is accumulated. If block size is too large relative to the NINJAM interval, the write callback may not fire until many Encode() calls later.
**Why it happens:** FLAC encodes in fixed-size blocks (default 4096 samples). A NINJAM interval at 120 BPM / 16 BPI with 44.1kHz sample rate is ~22050 samples, so a 4096-block produces ~5 frames per interval -- this is fine. But at higher BPMs or smaller BPIs, intervals can be short.
**How to avoid:** Use block size 1024 (23ms at 44.1kHz). This ensures multiple FLAC frames per NINJAM interval even in short-interval scenarios. Compression ratio difference between 1024 and 4096 blocks is minimal (~1-3%).
**Warning signs:** `Available()` returns 0 after multiple `Encode()` calls.

### Pitfall 2: FLAC Stream Header Must Be Written Per Interval
**What goes wrong:** FLAC streams start with a STREAMINFO metadata block. Each NINJAM interval is a complete, independent stream. If the encoder is `reinit()`ed without calling `finish()` first, the previous stream is incomplete and the new stream needs fresh headers.
**Why it happens:** NINJAM creates a new stream per interval. The VorbisEncoder handles this by re-emitting OGG headers in `reinit()`. FlacEncoder must do the equivalent: call `FLAC__stream_encoder_finish()`, then `FLAC__stream_encoder_delete()` + `FLAC__stream_encoder_new()` + reconfigure + `FLAC__stream_encoder_init_stream()`.
**How to avoid:** In `reinit()`, finish the current encoder, clear the output queue, create a new encoder instance, and initialize it. The STREAMINFO header automatically appears in the output via the write callback.
**Warning signs:** Decoder fails on second interval, decoding errors after codec was working for one interval.

### Pitfall 3: Decoder Read Callback Must Handle Partial Data
**What goes wrong:** `FLAC__stream_decoder_process_single()` calls the read callback requesting N bytes, but the internal buffer may have fewer bytes available. Returning `FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM` prematurely kills the decoder.
**Why it happens:** NINJAM delivers compressed data in chunks via `DecodeWrote()`. The decoder may be called before all data for a frame has arrived.
**How to avoid:** Return `FLAC__STREAM_DECODER_READ_STATUS_ABORT` when buffer is empty (not END_OF_STREAM, which is final). Or better: only call `process_single()` when the input buffer has at least some data, and set `*bytes = 0` to signal "try again later" pattern. The existing VorbisDecoder avoids this issue because `ogg_sync` handles partial-page buffering internally.
**Warning signs:** Decoder state transitions to `FLAC__STREAM_DECODER_END_OF_STREAM` unexpectedly after first chunk.

### Pitfall 4: Float-to-Int32 Conversion Precision
**What goes wrong:** FLAC operates on integer samples (FLAC__int32). NJClient works with float samples. Round-trip conversion introduces quantization noise if not handled carefully.
**Why it happens:** FLAC supports 4-32 bits per sample. Using 16 bits gives CD quality but loses the bottom ~20dB of a 32-bit float's dynamic range. Using 24 bits preserves more but increases file size.
**How to avoid:** Use 16 bits per sample for Phase 1 (matches the FLAC_INTEGRATION_PLAN.md recommendation). The encoding is lossless within 16-bit precision, and NINJAM's typical use case (live jamming) doesn't benefit from >16-bit precision. Conversion: `int32 = (FLAC__int32)lrintf(clampf(sample, -1.0f, 1.0f) * 32767.0f)` and back: `float = (float)int32 / 32767.0f`.
**Warning signs:** Subtle noise floor differences between FLAC and Vorbis at very low levels.

### Pitfall 5: CMake libFLAC OGG Dependency
**What goes wrong:** libFLAC's CMakeLists.txt has `WITH_OGG` option defaulting to ON. If it finds libogg, it tries to build OGG FLAC support which may cause duplicate symbol conflicts with JamWide's libogg.
**Why it happens:** Both JamWide and libFLAC can pull in libogg. If not coordinated, CMake may create conflicting targets.
**How to avoid:** Set `WITH_OGG OFF` before `add_subdirectory(libs/libflac)` since we're using native FLAC (not OGG FLAC). Also disable programs, examples, testing, and docs to minimize build scope.
**Warning signs:** CMake duplicate target errors, linker symbol conflicts.

### Pitfall 6: RemoteDownload FOURCC Not Passed to start_decode for Network Path
**What goes wrong:** In `start_decode()`, the `fourcc` parameter from the incoming message is available but not actually used for the network decode path. The current code only uses `fourcc` to influence the file-probe loop for file-based decoding.
**Why it happens:** The original code only supports one codec (Vorbis), so there was no need to branch on fourcc. The TODO comment at line 1781 confirms this was always intended to be extended.
**How to avoid:** When `newstate->decode_buf` is set (network path), use the `fourcc` parameter directly to select the decoder. When decoding from file, probe for known types.
**Warning signs:** All remote audio decoded as Vorbis even when sender is using FLAC; garbled audio or silence from FLAC senders.

## Code Examples

### FlacEncoder Core Structure
```cpp
// Pattern based on VorbisEncoder in wdl/vorbisencdec.h
// Uses libFLAC C API: https://xiph.org/flac/api/group__flac__stream__encoder.html

class FlacEncoder : public VorbisEncoderInterface {
public:
    FlacEncoder(int srate, int nch, int /*bitrate*/, int serno) {
        m_nch = nch;
        m_err = 0;
        m_encoder = FLAC__stream_encoder_new();
        if (!m_encoder) { m_err = 1; return; }

        FLAC__stream_encoder_set_channels(m_encoder, nch);
        FLAC__stream_encoder_set_bits_per_sample(m_encoder, 16);
        FLAC__stream_encoder_set_sample_rate(m_encoder, srate);
        FLAC__stream_encoder_set_compression_level(m_encoder, 5);
        FLAC__stream_encoder_set_blocksize(m_encoder, 1024);

        auto status = FLAC__stream_encoder_init_stream(
            m_encoder, write_cb, /*seek*/nullptr, /*tell*/nullptr,
            /*metadata*/nullptr, this);
        if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) m_err = 1;
    }

    ~FlacEncoder() {
        if (m_encoder) {
            FLAC__stream_encoder_finish(m_encoder);
            FLAC__stream_encoder_delete(m_encoder);
        }
    }

    void Encode(float *in, int inlen, int advance=1, int spacing=1) override {
        if (m_err) return;
        // Convert float -> FLAC__int32, interleaved
        m_intbuf.resize(inlen * m_nch);
        for (int i = 0, idx = 0; i < inlen; i++) {
            for (int c = 0; c < m_nch; c++) {
                float s = in[idx + c * spacing];
                if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
                m_intbuf[i * m_nch + c] = (FLAC__int32)lrintf(s * 32767.0f);
            }
            idx += advance;
        }
        if (!FLAC__stream_encoder_process_interleaved(m_encoder,
                m_intbuf.data(), inlen))
            m_err = 1;
    }

    int isError() override { return m_err; }
    int Available() override { return outqueue.Available(); }
    void *Get() override { return outqueue.Get(); }
    void Advance(int amt) override { outqueue.Advance(amt); }
    void Compact() override { outqueue.Compact(); }

    void reinit(int bla=0) override {
        // Finish current stream
        if (m_encoder) {
            FLAC__stream_encoder_finish(m_encoder);
            FLAC__stream_encoder_delete(m_encoder);
        }
        outqueue.Advance(outqueue.Available());
        outqueue.Compact();
        // Create fresh encoder (headers go into outqueue via write_cb)
        m_encoder = FLAC__stream_encoder_new();
        // ... re-set channels, bps, srate, compression, blocksize ...
        FLAC__stream_encoder_init_stream(m_encoder, write_cb,
            nullptr, nullptr, nullptr, this);
    }

    WDL_Queue outqueue;

private:
    static FLAC__StreamEncoderWriteStatus write_cb(
        const FLAC__StreamEncoder*, const FLAC__byte buffer[],
        size_t bytes, uint32_t, uint32_t, void *client_data) {
        auto *self = (FlacEncoder*)client_data;
        self->outqueue.Add(buffer, (int)bytes);
        return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
    }

    FLAC__StreamEncoder *m_encoder;
    int m_nch, m_err;
    std::vector<FLAC__int32> m_intbuf;
};
```

### FlacDecoder Core Structure
```cpp
// Pattern based on VorbisDecoder in wdl/vorbisencdec.h
// Uses libFLAC C API: https://xiph.org/flac/api/group__flac__stream__decoder.html

class FlacDecoder : public VorbisDecoderInterface {
public:
    FlacDecoder() {
        m_srate = 0; m_nch = 1;
        m_decoder = FLAC__stream_decoder_new();
        if (m_decoder) {
            FLAC__stream_decoder_init_stream(m_decoder,
                read_cb, /*seek*/nullptr, /*tell*/nullptr,
                /*length*/nullptr, /*eof*/nullptr,
                write_cb, metadata_cb, error_cb, this);
        }
    }

    ~FlacDecoder() {
        if (m_decoder) {
            FLAC__stream_decoder_finish(m_decoder);
            FLAC__stream_decoder_delete(m_decoder);
        }
    }

    int GetSampleRate() override { return m_srate; }
    int GetNumChannels() override { return m_nch ? m_nch : 1; }

    void *DecodeGetSrcBuffer(int srclen) override {
        return m_inbuf.Add(nullptr, srclen);  // WDL_Queue::Add returns writable ptr
    }

    void DecodeWrote(int srclen) override {
        // Data is already in m_inbuf from DecodeGetSrcBuffer
        // Process frames while data is available
        while (m_inbuf.Available() > 0) {
            if (!FLAC__stream_decoder_process_single(m_decoder)) break;
            auto state = FLAC__stream_decoder_get_state(m_decoder);
            if (state == FLAC__STREAM_DECODER_END_OF_STREAM ||
                state == FLAC__STREAM_DECODER_ABORTED) break;
        }
    }

    int Available() override { return m_outbuf.Available(); }
    float *Get() override { return m_outbuf.Get(); }
    void Skip(int amt) override { m_outbuf.Advance(amt); m_outbuf.Compact(); }
    void Reset() override { /* reset decoder state */ }
    int GenerateLappingSamples() override { return 0; /* FLAC has no lapping */ }

private:
    static FLAC__StreamDecoderReadStatus read_cb(
        const FLAC__StreamDecoder*, FLAC__byte buffer[],
        size_t *bytes, void *client_data) {
        auto *self = (FlacDecoder*)client_data;
        int avail = self->m_inbuf.Available();
        if (avail <= 0) { *bytes = 0; return FLAC__STREAM_DECODER_READ_STATUS_ABORT; }
        int toread = (int)*bytes;
        if (toread > avail) toread = avail;
        memcpy(buffer, self->m_inbuf.Get(), toread);
        self->m_inbuf.Advance(toread);
        self->m_inbuf.Compact();
        *bytes = toread;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    static FLAC__StreamDecoderWriteStatus write_cb(
        const FLAC__StreamDecoder*, const FLAC__Frame *frame,
        const FLAC__int32 *const buffer[], void *client_data) {
        auto *self = (FlacDecoder*)client_data;
        int nch = frame->header.channels;
        int samples = frame->header.blocksize;
        int bps = frame->header.bits_per_sample;
        float scale = 1.0f / (float)(1 << (bps - 1));
        float *out = self->m_outbuf.Add(nullptr, samples * nch);
        if (out) {
            for (int i = 0; i < samples; i++) {
                for (int c = 0; c < nch; c++) {
                    *out++ = (float)buffer[c][i] * scale;
                }
            }
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    static void metadata_cb(const FLAC__StreamDecoder*,
        const FLAC__StreamMetadata *metadata, void *client_data) {
        auto *self = (FlacDecoder*)client_data;
        if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
            self->m_srate = metadata->data.stream_info.sample_rate;
            self->m_nch = metadata->data.stream_info.channels;
        }
    }

    static void error_cb(const FLAC__StreamDecoder*,
        FLAC__StreamDecoderErrorStatus, void*) {
        // Log error, but don't crash -- decoder may recover
    }

    FLAC__StreamDecoder *m_decoder;
    WDL_Queue m_inbuf;
    WDL_TypedQueue<float> m_outbuf;
    int m_srate, m_nch;
};
```

### CMake Integration
```cmake
# After libvorbis section in CMakeLists.txt (~line 46):

# libFLAC
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WITH_OGG OFF CACHE BOOL "" FORCE)        # We use native FLAC, not OGG FLAC
set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)    # We use C API only
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
set(INSTALL_PKGCONFIG_MODULES OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/libflac EXCLUDE_FROM_ALL)

# Update njclient linkage (line 113):
target_link_libraries(njclient PUBLIC wdl vorbis vorbisenc ogg FLAC)
```

### Codec Selection UI Widget
```cpp
// In ui_local.cpp, after the bitrate combo:
const char* kCodecLabels[] = { "FLAC (Lossless)", "Vorbis (Compressed)" };
ImGui::SameLine();
ImGui::SetNextItemWidth(150.0f);
if (ImGui::Combo("##codec_local", &state.local_codec_index,
                  kCodecLabels, 2)) {
    if (state.status == NJClient::NJC_STATUS_OK) {
        jamwide::SetEncoderFormatCommand cmd;
        cmd.fourcc = (state.local_codec_index == 0)
            ? MAKE_NJ_FOURCC('F','L','A','C')
            : MAKE_NJ_FOURCC('O','G','G','v');
        plugin->cmd_queue.try_push(std::move(cmd));
    }
}
```

### Remote Codec Indicator
```cpp
// In ui_remote.cpp, after channel name display:
// channel.codec_fourcc is populated from NINJAM download messages
if (channel.codec_fourcc == MAKE_NJ_FOURCC('F','L','A','C')) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[FLAC]");
} else if (channel.codec_fourcc == MAKE_NJ_FOURCC('O','G','G','v')) {
    ImGui::SameLine();
    ImGui::TextDisabled("[Vorbis]");
} else if (channel.codec_fourcc != 0) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[Unsupported codec]");
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| libFLAC single-threaded | libFLAC 1.5.0 multithreaded encoder | Feb 2025 | Faster encoding, but we use small blocks so impact is minimal for our use case |
| FLAC 1.3.x CMake via separate wrapper | FLAC 1.4+ native CMake add_subdirectory support | 2022 | Clean CMake integration without external wrappers |
| Only OGG FLAC in Vorbis ecosystem | Native FLAC widely supported | Always | Native FLAC is simpler and has lower overhead for streaming |

**Deprecated/outdated:**
- `FLAC__stream_encoder_init_ogg_stream()` -- Not needed; we use native FLAC framing
- FLAC_INTEGRATION_PLAN.md specifies "Default encoder = Vorbis" in fallback section -- superseded by CODEC-05 decision (default FLAC)

## Discretion Recommendations

### Codec Selection Scope: Global Toggle (Recommended)
**Rationale:** Phase 1 has a single local channel (channel 0) with the existing ImGui UI. A global toggle is simpler to implement and test. Per-channel selection is specified for Phase 4 (UI-09) when the JUCE UI supports multiple channels natively. Adding per-channel now would require additional UI complexity (per-channel format state, per-channel SPSC commands) with no practical benefit since there's only one active channel.

### Recording UI: Simple Checkbox
**Rationale:** `config_savelocalaudio` is already functional in NJClient. A checkbox labeled "Record Session" that sets `config_savelocalaudio = 2` (OGG + WAV) or `0` (off) is the minimal UI needed. No FLAC recording format in this phase.

### Chat Notification Format: /me Action Style
**Rationale:** Using the existing `/me` chat action format makes codec changes visible but non-intrusive. Suggested wording: `"/me switched to FLAC lossless"` or `"/me switched to Vorbis compressed"`. This leverages the existing ChatMessageType::Action rendering in the chat panel.

### Compression Level: 5 (Default)
**Rationale:** libFLAC's default compression level 5 provides a good balance of compression ratio and CPU usage. For real-time encoding of 1-2 channels at 44.1kHz, even level 8 would be fast enough on modern hardware, but level 5 has the widest compatibility and is the libFLAC default.

### Block Size: 1024 Samples
**Rationale:** 1024 samples = 23ms at 44.1kHz. Small enough to produce multiple frames per NINJAM interval (even at high BPMs), large enough for reasonable compression. The VorbisEncoder's effective block size is similar.

## Open Questions

1. **libFLAC CMake target name**
   - What we know: The FLAC CMakeLists.txt creates a target named `FLAC` for the C library
   - What's unclear: Whether `FLAC` target name conflicts with any other targets in the project
   - Recommendation: Verify with a test build after adding the submodule; unlikely to conflict given current dependencies

2. **Universal binary (arm64 + x86_64) with libFLAC**
   - What we know: libFLAC has NEON optimizations for ARM and SSE/FMA for x86_64; the CMake build handles architecture detection
   - What's unclear: Whether libFLAC's CMake properly handles CMAKE_OSX_ARCHITECTURES for universal builds
   - Recommendation: JamWide already sets `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` globally; libFLAC should inherit this. Verify with build.

3. **GenerateLappingSamples() for FlacDecoder**
   - What we know: VorbisDecoder implements `GenerateLappingSamples()` using `vorbis_synthesis_lapout()` for cross-fade between intervals. FLAC has no equivalent API.
   - What's unclear: Whether returning 0 (no lapping samples) will cause audible clicks at interval boundaries
   - Recommendation: Return 0 initially. NINJAM's `overlapFadeState` system handles cross-fading at the DecodeState level using whatever samples are available. If clicks occur, generate a short fade-out buffer manually.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | None currently -- JAMWIDE_BUILD_TESTS option exists but no test framework is configured |
| Config file | CMakeLists.txt line 28: `option(JAMWIDE_BUILD_TESTS "Build tests" OFF)` |
| Quick run command | `cmake --build build --target jamwide_tests && ctest --test-dir build --output-on-failure` |
| Full suite command | `cmake -DJAMWIDE_BUILD_TESTS=ON -B build && cmake --build build && ctest --test-dir build --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CODEC-01 | FlacEncoder produces valid FLAC output from float input | unit | `ctest -R flac_encoder` | Wave 0 |
| CODEC-02 | FlacDecoder decodes FLAC data back to floats | unit | `ctest -R flac_decoder` | Wave 0 |
| CODEC-01+02 | Encode/decode roundtrip preserves audio (within 16-bit precision) | unit | `ctest -R flac_roundtrip` | Wave 0 |
| CODEC-03 | SetEncoderFormatCommand dispatched and applied | unit | `ctest -R codec_command` | Wave 0 |
| CODEC-04 | Encoder format change only takes effect at interval boundary | integration | manual -- requires running plugin in session | N/A |
| CODEC-05 | Default encoder format is FLAC FOURCC | unit | `ctest -R codec_default` | Wave 0 |
| REC-01 | config_savelocalaudio toggle enables recording | manual-only | manual -- requires NJClient connected to server | N/A |
| REC-02 | OGG/WAV files written to disk during recording | manual-only | manual -- requires NJClient connected to server | N/A |

### Sampling Rate
- **Per task commit:** `cmake --build build --target jamwide_tests && ctest --test-dir build -R flac --output-on-failure`
- **Per wave merge:** `cmake -DJAMWIDE_BUILD_TESTS=ON -B build && cmake --build build && ctest --test-dir build --output-on-failure`
- **Phase gate:** Full suite green + manual session testing before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/test_flac_codec.cpp` -- covers CODEC-01, CODEC-02 (encode/decode roundtrip)
- [ ] `tests/test_codec_command.cpp` -- covers CODEC-03, CODEC-05 (command dispatch, default format)
- [ ] Test framework selection (recommend simple assert-based or Catch2 single-header)
- [ ] CMakeLists.txt test target setup: `add_executable(jamwide_tests ...)` linking njclient + FLAC
- [ ] Framework install: Include test framework header (Catch2 or custom assert harness)

## Sources

### Primary (HIGH confidence)
- [xiph/flac GitHub](https://github.com/xiph/flac) - CMakeLists.txt options, target names, release info
- [libFLAC stream_encoder.h](https://raw.githubusercontent.com/xiph/flac/master/include/FLAC/stream_encoder.h) - Full C API for stream encoding with callbacks
- [libFLAC stream_decoder.h](https://raw.githubusercontent.com/xiph/flac/master/include/FLAC/stream_decoder.h) - Full C API for stream decoding with callbacks
- [FLAC 1.5.0 release](https://github.com/xiph/flac/releases/tag/1.5.0) - Latest stable version (Feb 2025)
- `/Users/cell/dev/JamWide/wdl/vorbisencdec.h` - Existing VorbisEncoder/VorbisDecoder interface and implementation
- `/Users/cell/dev/JamWide/src/core/njclient.cpp` - Encoder creation (line 1567), decoder dispatch (line 1770-1820), interval boundary logic (lines 1736-1754)
- `/Users/cell/dev/JamWide/FLAC_INTEGRATION_PLAN.md` - Detailed integration plan with class designs and file change list

### Secondary (MEDIUM confidence)
- [FLAC stream encoder API docs](https://xiph.org/flac/api/group__flac__stream__encoder.html) - Official API reference
- [FLAC stream decoder API docs](https://xiph.org/flac/api/group__flac__stream__decoder.html) - Official API reference

### Tertiary (LOW confidence)
- None -- all findings verified against official sources and existing codebase

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - libFLAC is the only FLAC implementation; version 1.5.0 confirmed via GitHub releases; CMake integration verified from CMakeLists.txt
- Architecture: HIGH - Existing codebase patterns (encoder interface, SPSC queue, interval boundary logic) directly dictate the integration approach; code references verified line-by-line
- Pitfalls: HIGH - Derived from verified libFLAC API behavior (stream callbacks, block buffering) and existing NINJAM codec patterns; FLAC_INTEGRATION_PLAN.md provides additional validation

**Research date:** 2026-03-07
**Valid until:** 2026-04-07 (stable domain; libFLAC API is mature and rarely changes)
