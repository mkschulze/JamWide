/*
    WDL - flacencdec.h
    Copyright (C) 2024 and later, JamWide contributors

    FLAC encoding/decoding classes implementing the I_NJEncoder/I_NJDecoder
    interfaces defined in vorbisencdec.h. Uses libFLAC C API for streaming
    encode/decode with memory callbacks.

    These classes mirror the VorbisEncoder/VorbisDecoder structure:
    - FlacEncoder writes compressed output to WDL_Queue outqueue
    - FlacDecoder reads compressed input from WDL_Queue, outputs float samples

    Usage: #include this file after vorbisencdec.h has been included
    (or after VorbisEncoderInterface/VorbisDecoderInterface are declared).

    Configuration:
    - Bits per sample: 16 (CD quality, lossless within 16-bit precision)
    - Compression level: 5 (libFLAC default, good balance of ratio vs CPU)
    - Block size: 1024 samples (23ms at 44.1kHz, ensures multiple frames per
      NINJAM interval even at high BPMs)
*/

#ifndef _FLACENCDEC_H_
#define _FLACENCDEC_H_

#include "FLAC/stream_encoder.h"
#include "FLAC/stream_decoder.h"
#include "queue.h"

#include <cmath>
#include <cstring>
#include <vector>

class FlacEncoder : public VorbisEncoderInterface {
public:
    FlacEncoder(int srate, int nch, int /*bitrate*/, int /*serno*/)
    {
        m_nch = nch;
        m_srate = srate;
        m_err = 0;
        m_encoder = nullptr;
        initEncoder();
    }

    ~FlacEncoder()
    {
        if (m_encoder) {
            FLAC__stream_encoder_finish(m_encoder);
            FLAC__stream_encoder_delete(m_encoder);
        }
    }

    void Encode(float *in, int inlen, int advance=1, int spacing=1) override
    {
        if (m_err || !m_encoder) return;

        if (inlen == 0) {
            // End-of-stream: flush remaining buffered samples via write_cb
            FLAC__stream_encoder_finish(m_encoder);
            FLAC__stream_encoder_delete(m_encoder);
            m_encoder = nullptr;
            return;
        }

        // Convert float -> FLAC__int32 interleaved.
        // advance = stride between successive samples in the input buffer
        // spacing = stride between channels for the same sample
        // Matches the VorbisEncoder calling convention:
        //   mono:               advance=1, spacing=1
        //   stereo interleaved: advance=2, spacing=1 (L0 R0 L1 R1 ...)
        //   stereo planar:      advance=1, spacing=N (L0 L1...LN R0 R1...RN)
        m_intbuf.resize(inlen * m_nch);

        for (int i = 0, idx = 0; i < inlen; i++, idx += advance) {
            for (int c = 0; c < m_nch; c++) {
                float s = in[idx + c * spacing];
                if (s > 1.0f) s = 1.0f;
                if (s < -1.0f) s = -1.0f;
                m_intbuf[i * m_nch + c] = (FLAC__int32)lrintf(s * 32767.0f);
            }
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

    void reinit(int bla=0) override
    {
        if (!bla) {
            // Finish current stream (flushes remaining samples via write_cb)
            if (m_encoder) {
                FLAC__stream_encoder_finish(m_encoder);
                FLAC__stream_encoder_delete(m_encoder);
                m_encoder = nullptr;
            }

            // Clear outqueue before creating new encoder, matching VorbisEncoder
            // behavior. Without this, leftover bytes from finish() remain in the
            // queue and get sent as part of the NEXT interval. The receiving
            // FlacDecoder expects the stream to start with a "fLaC" STREAMINFO
            // header; the stale tail bytes cause immediate decode failure → silence.
            outqueue.Advance(outqueue.Available());
            outqueue.Compact();

            // Create fresh encoder (STREAMINFO header written via write_cb)
            m_err = 0;
            initEncoder();
        }
    }

    WDL_Queue outqueue;

private:
    void initEncoder()
    {
        m_encoder = FLAC__stream_encoder_new();
        if (!m_encoder) { m_err = 1; return; }

        FLAC__stream_encoder_set_channels(m_encoder, m_nch);
        FLAC__stream_encoder_set_bits_per_sample(m_encoder, 16);
        FLAC__stream_encoder_set_sample_rate(m_encoder, m_srate);
        FLAC__stream_encoder_set_compression_level(m_encoder, 5);
        FLAC__stream_encoder_set_blocksize(m_encoder, 1024);

        FLAC__StreamEncoderInitStatus status = FLAC__stream_encoder_init_stream(
            m_encoder, write_cb, /*seek*/nullptr, /*tell*/nullptr,
            /*metadata*/nullptr, this);
        if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
            m_err = 1;
    }

    static FLAC__StreamEncoderWriteStatus write_cb(
        const FLAC__StreamEncoder * /*encoder*/,
        const FLAC__byte buffer[],
        size_t bytes,
        uint32_t /*samples*/,
        uint32_t /*current_frame*/,
        void *client_data)
    {
        FlacEncoder *self = (FlacEncoder *)client_data;
        self->outqueue.Add(buffer, (int)bytes);
        return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
    }

    FLAC__StreamEncoder *m_encoder;
    int m_nch, m_srate, m_err;
    std::vector<FLAC__int32> m_intbuf;
};


class FlacDecoder : public VorbisDecoderInterface {
public:
    FlacDecoder()
    {
        m_srate = 0;
        m_nch = 0;
        m_err = 0;
        m_decoder = nullptr;
        initDecoder();
    }

    ~FlacDecoder()
    {
        if (m_decoder) {
            FLAC__stream_decoder_finish(m_decoder);
            FLAC__stream_decoder_delete(m_decoder);
        }
    }

    int GetSampleRate() override { return m_srate; }
    int GetNumChannels() override { return m_nch ? m_nch : 1; }

    void *DecodeGetSrcBuffer(int srclen) override
    {
        return m_inbuf.Add(nullptr, srclen);
    }

    void DecodeWrote(int srclen) override
    {
        (void)srclen;
        if (!m_decoder) return;

        // process_single() decodes one metadata block or one audio frame.
        // libFLAC buffers read data internally, so even when m_inbuf is
        // empty, the decoder may still have buffered data to process.
        // Keep calling until the decoder needs more data (read_cb ABORTs),
        // reaches END_OF_STREAM, or enters an error state.
        for (;;) {
            FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(m_decoder);
            if (state == FLAC__STREAM_DECODER_END_OF_STREAM ||
                state == FLAC__STREAM_DECODER_ABORTED)
                break;

            if (!FLAC__stream_decoder_process_single(m_decoder))
                break;
        }
    }

    void Reset() override
    {
        m_inbuf.Advance(m_inbuf.Available());
        m_inbuf.Compact();
        m_outbuf.Clear();
        m_srate = 0;
        m_nch = 0;
        m_err = 0;

        if (m_decoder) {
            FLAC__stream_decoder_finish(m_decoder);
            FLAC__stream_decoder_delete(m_decoder);
            m_decoder = nullptr;
        }
        initDecoder();
    }

    int Available() override { return m_outbuf.Available(); }
    float *Get() override { return m_outbuf.Get(); }

    void Skip(int amt) override
    {
        m_outbuf.Advance(amt);
        m_outbuf.Compact();
    }

    int GenerateLappingSamples() override
    {
        // FLAC has no lapping/overlap mechanism like Vorbis.
        // NINJAM's overlapFadeState handles cross-fade at DecodeState level.
        return 0;
    }

private:
    void initDecoder()
    {
        m_decoder = FLAC__stream_decoder_new();
        if (!m_decoder) { m_err = 1; return; }

        // 15.1-08 CR-11 mitigation: pre-grow input compressed-byte and output
        // sample queues. m_inbuf is byte-typed (WDL_Queue) — 16384 bytes
        // covers a NINJAM compressed frame plus headroom. m_outbuf is
        // float-typed (WDL_TypedQueue<float>) — Prealloc takes element count;
        // 8192 * 2 = 16384 floats = 64KB covers stereo at peak frame size.
        // Both Prealloc calls delegate to WDL_HeapBuf::Prealloc (single-thread
        // contract; safe at construction). Since FlacDecoder is constructed
        // on the run thread per 15.1-09 (Codex HIGH-1 invariant), and Reset()
        // also runs on the run thread, the audio-thread paths
        // (DecodeGetSrcBuffer / write_cb's m_outbuf.Add) do NOT realloc in
        // steady state. Algorithmic libFLAC residual (first 3-5 frames per
        // stream) is bounded; deferred to 15.1-10 Instruments UAT for full
        // CR-11 closure measurement.
        m_inbuf.Prealloc(16384);
        m_outbuf.Prealloc(8192 * 2);

        FLAC__StreamDecoderInitStatus status = FLAC__stream_decoder_init_stream(
            m_decoder,
            read_cb, /*seek*/nullptr, /*tell*/nullptr,
            /*length*/nullptr, /*eof*/nullptr,
            write_cb, metadata_cb, error_cb, this);
        if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK)
            m_err = 1;
    }

    static FLAC__StreamDecoderReadStatus read_cb(
        const FLAC__StreamDecoder * /*decoder*/,
        FLAC__byte buffer[],
        size_t *bytes,
        void *client_data)
    {
        FlacDecoder *self = (FlacDecoder *)client_data;
        int avail = self->m_inbuf.Available();
        if (avail <= 0) {
            *bytes = 0;
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        int toread = (int)*bytes;
        if (toread > avail) toread = avail;
        memcpy(buffer, self->m_inbuf.Get(), toread);
        self->m_inbuf.Advance(toread);
        self->m_inbuf.Compact();
        *bytes = (size_t)toread;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    static FLAC__StreamDecoderWriteStatus write_cb(
        const FLAC__StreamDecoder * /*decoder*/,
        const FLAC__Frame *frame,
        const FLAC__int32 *const buffer[],
        void *client_data)
    {
        FlacDecoder *self = (FlacDecoder *)client_data;
        int nch = (int)frame->header.channels;
        int samples = (int)frame->header.blocksize;
        int bps = (int)frame->header.bits_per_sample;
        // Scale must match encoder: encoder uses int = lrintf(float * (2^(bps-1) - 1))
        // so decoder uses float = int / (2^(bps-1) - 1). For 16-bit: 1/32767.
        float scale = 1.0f / (float)((1 << (bps - 1)) - 1);

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

    static void metadata_cb(
        const FLAC__StreamDecoder * /*decoder*/,
        const FLAC__StreamMetadata *metadata,
        void *client_data)
    {
        FlacDecoder *self = (FlacDecoder *)client_data;
        if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
            self->m_srate = (int)metadata->data.stream_info.sample_rate;
            self->m_nch = (int)metadata->data.stream_info.channels;
        }
    }

    static void error_cb(
        const FLAC__StreamDecoder * /*decoder*/,
        FLAC__StreamDecoderErrorStatus /*status*/,
        void *client_data)
    {
        FlacDecoder *self = (FlacDecoder *)client_data;
        self->m_err = 1;
    }

    FLAC__StreamDecoder *m_decoder;
    WDL_Queue m_inbuf;
    WDL_TypedQueue<float> m_outbuf;
    int m_srate, m_nch, m_err;
};

#endif // _FLACENCDEC_H_
