/*
    WDL - flacencdec.h
    Copyright (C) 2024 and later, JamWide contributors

    FLAC encoding/decoding classes implementing the I_NJEncoder/I_NJDecoder
    interfaces defined in vorbisencdec.h. Uses libFLAC C API for streaming
    encode/decode with memory callbacks.

    These classes mirror the VorbisEncoder/VorbisDecoder structure:
    - FlacEncoder writes compressed output to WDL_Queue outqueue
    - FlacDecoder reads compressed input from WDL_Queue, outputs float samples
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
    // Stub: constructor/destructor
    FlacEncoder(int srate, int nch, int /*bitrate*/, int /*serno*/) {
        m_nch = nch;
        m_srate = srate;
        m_err = 1; // stub: always error
        m_encoder = nullptr;
    }

    ~FlacEncoder() {}

    void Encode(float *in, int inlen, int advance=1, int spacing=1) override {
        (void)in; (void)inlen; (void)advance; (void)spacing;
    }

    int isError() override { return m_err; }
    int Available() override { return outqueue.Available(); }
    void *Get() override { return outqueue.Get(); }
    void Advance(int amt) override { outqueue.Advance(amt); }
    void Compact() override { outqueue.Compact(); }
    void reinit(int bla=0) override { (void)bla; }

    WDL_Queue outqueue;

private:
    FLAC__StreamEncoder *m_encoder;
    int m_nch, m_srate, m_err;
    std::vector<FLAC__int32> m_intbuf;
};

class FlacDecoder : public VorbisDecoderInterface {
public:
    FlacDecoder() {
        m_srate = 0;
        m_nch = 0;
        m_decoder = nullptr;
    }

    ~FlacDecoder() {}

    int GetSampleRate() override { return m_srate; }
    int GetNumChannels() override { return m_nch ? m_nch : 1; }
    void *DecodeGetSrcBuffer(int srclen) override {
        return m_inbuf.Add(nullptr, srclen);
    }
    void DecodeWrote(int srclen) override { (void)srclen; }
    void Reset() override {}
    int Available() override { return m_outbuf.Available(); }
    float *Get() override { return m_outbuf.Get(); }
    void Skip(int amt) override { m_outbuf.Advance(amt); m_outbuf.Compact(); }
    int GenerateLappingSamples() override { return 0; }

private:
    FLAC__StreamDecoder *m_decoder;
    WDL_Queue m_inbuf;
    WDL_TypedQueue<float> m_outbuf;
    int m_srate, m_nch;
};

#endif // _FLACENCDEC_H_
