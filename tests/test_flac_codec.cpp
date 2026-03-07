/*
    test_flac_codec.cpp - Tests for FlacEncoder and FlacDecoder

    Simple assert-based tests for FLAC codec round-trip encoding/decoding.
    Validates that FlacEncoder/FlacDecoder implement I_NJEncoder/I_NJDecoder
    interfaces correctly and preserve audio within 16-bit quantization tolerance.
*/

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cassert>
#include <cstring>
#include <vector>

// Only pull the interface classes from vorbisencdec.h (no Vorbis codec deps)
// WDL_VORBIS_INTERFACE_ONLY is defined via compile_definitions in CMakeLists.txt
#include "wdl/vorbisencdec.h"
#include "wdl/flacencdec.h"

static const float kTolerance = 1.0f / 32767.0f;
static const int kSampleRate = 44100;
static const int kBlockSize = 1024;

// Generate a sine wave at the given frequency
static void generate_sine(float* buf, int num_samples, int num_channels,
                          float freq, float sample_rate) {
    for (int i = 0; i < num_samples; i++) {
        float val = sinf(2.0f * 3.14159265358979f * freq * (float)i / sample_rate);
        for (int c = 0; c < num_channels; c++) {
            buf[i * num_channels + c] = val;
        }
    }
}

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  TEST: %s ... ", name); \
        fflush(stdout); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASSED\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAILED: %s\n", msg); \
    } while(0)

// ============================================================
// Test 1: FlacEncoder mono - produces output
// ============================================================
static void test_encoder_mono_produces_output() {
    TEST("FlacEncoder mono produces output");

    FlacEncoder enc(kSampleRate, 1, 128, 1);
    assert(enc.isError() == 0);

    float buf[kBlockSize];
    generate_sine(buf, kBlockSize, 1, 440.0f, (float)kSampleRate);

    // Encode with advance=1, spacing=1 for mono
    enc.Encode(buf, kBlockSize, 1, 1);

    // Flush encoder to get all output
    enc.reinit(0);

    if (enc.Available() > 0) {
        PASS();
    } else {
        FAIL("Available() returned 0 after encoding mono audio");
    }
}

// ============================================================
// Test 2: FlacEncoder stereo - produces output
// ============================================================
static void test_encoder_stereo_produces_output() {
    TEST("FlacEncoder stereo produces output");

    FlacEncoder enc(kSampleRate, 2, 128, 1);
    assert(enc.isError() == 0);

    // Interleaved stereo: L R L R ...
    float buf[kBlockSize * 2];
    generate_sine(buf, kBlockSize, 2, 440.0f, (float)kSampleRate);

    // For stereo interleaved: advance=2, spacing=1
    // This matches how NJClient calls Encode for interleaved stereo
    enc.Encode(buf, kBlockSize, 2, 1);

    // Flush
    enc.reinit(0);

    if (enc.Available() > 0) {
        PASS();
    } else {
        FAIL("Available() returned 0 after encoding stereo audio");
    }
}

// ============================================================
// Test 3: FlacEncoder isError() returns 0 after successful init and encode
// ============================================================
static void test_encoder_no_error() {
    TEST("FlacEncoder isError() returns 0 after successful encode");

    FlacEncoder enc(kSampleRate, 1, 128, 1);

    if (enc.isError() != 0) {
        FAIL("isError() != 0 after construction");
        return;
    }

    float buf[kBlockSize];
    generate_sine(buf, kBlockSize, 1, 440.0f, (float)kSampleRate);
    enc.Encode(buf, kBlockSize, 1, 1);

    if (enc.isError() == 0) {
        PASS();
    } else {
        FAIL("isError() != 0 after encoding");
    }
}

// ============================================================
// Test 4: FlacEncoder reinit() clears output and produces fresh headers
// ============================================================
static void test_encoder_reinit() {
    TEST("FlacEncoder reinit() clears output and produces fresh headers");

    FlacEncoder enc(kSampleRate, 1, 128, 1);
    assert(enc.isError() == 0);

    float buf[kBlockSize];
    generate_sine(buf, kBlockSize, 1, 440.0f, (float)kSampleRate);
    enc.Encode(buf, kBlockSize, 1, 1);

    // Consume all output
    int avail1 = enc.Available();
    enc.Advance(avail1);
    enc.Compact();
    assert(enc.Available() == 0);

    // reinit should produce fresh FLAC stream headers
    enc.reinit(0);

    if (enc.Available() > 0 && enc.isError() == 0) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "After reinit: Available()=%d, isError()=%d",
                 enc.Available(), enc.isError());
        FAIL(msg);
    }
}

// ============================================================
// Test 5: FlacDecoder metadata - GetSampleRate and GetNumChannels
// ============================================================
static void test_decoder_metadata() {
    TEST("FlacDecoder metadata after decoding");

    // Encode some audio
    FlacEncoder enc(kSampleRate, 2, 128, 1);
    assert(enc.isError() == 0);

    float buf[kBlockSize * 2];
    generate_sine(buf, kBlockSize, 2, 440.0f, (float)kSampleRate);
    enc.Encode(buf, kBlockSize, 2, 1);

    // Finish the stream so decoder gets complete data
    enc.reinit(0);

    int enc_avail = enc.Available();
    assert(enc_avail > 0);

    // Decode
    FlacDecoder dec;
    void *dst = dec.DecodeGetSrcBuffer(enc_avail);
    assert(dst != nullptr);
    memcpy(dst, enc.Get(), enc_avail);
    dec.DecodeWrote(enc_avail);

    bool ok = (dec.GetSampleRate() == kSampleRate && dec.GetNumChannels() == 2);
    if (ok) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "srate=%d (expected %d), nch=%d (expected 2)",
                 dec.GetSampleRate(), kSampleRate, dec.GetNumChannels());
        FAIL(msg);
    }
}

// ============================================================
// Test 6: Roundtrip mono - encode then decode, compare
// ============================================================
static void test_roundtrip_mono() {
    TEST("Roundtrip mono: encode/decode preserves audio within 16-bit tolerance");

    const int num_samples = kBlockSize * 4; // encode multiple blocks

    FlacEncoder enc(kSampleRate, 1, 128, 1);
    assert(enc.isError() == 0);

    // Generate a sine wave
    std::vector<float> input(num_samples);
    generate_sine(input.data(), num_samples, 1, 440.0f, (float)kSampleRate);

    // Encode in blocks
    for (int offset = 0; offset < num_samples; offset += kBlockSize) {
        enc.Encode(input.data() + offset, kBlockSize, 1, 1);
    }

    // Finish the stream
    enc.reinit(0);

    int enc_avail = enc.Available();
    if (enc_avail <= 0) {
        FAIL("Encoder produced no output");
        return;
    }

    // Decode
    FlacDecoder dec;
    void *dst = dec.DecodeGetSrcBuffer(enc_avail);
    assert(dst != nullptr);
    memcpy(dst, enc.Get(), enc_avail);
    dec.DecodeWrote(enc_avail);

    int dec_avail = dec.Available();
    if (dec_avail < num_samples) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Decoded %d samples, expected >= %d", dec_avail, num_samples);
        FAIL(msg);
        return;
    }

    // Compare
    float *decoded = dec.Get();
    float max_err = 0.0f;
    for (int i = 0; i < num_samples; i++) {
        float err = fabsf(input[i] - decoded[i]);
        if (err > max_err) max_err = err;
    }

    if (max_err <= kTolerance) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Max error %.8f exceeds tolerance %.8f", max_err, kTolerance);
        FAIL(msg);
    }
}

// ============================================================
// Test 7: Roundtrip stereo - encode then decode, compare
// ============================================================
static void test_roundtrip_stereo() {
    TEST("Roundtrip stereo: encode/decode preserves audio within 16-bit tolerance");

    const int num_samples = kBlockSize * 4;

    FlacEncoder enc(kSampleRate, 2, 128, 1);
    assert(enc.isError() == 0);

    // Generate stereo sine wave (interleaved L R L R)
    std::vector<float> input(num_samples * 2);
    for (int i = 0; i < num_samples; i++) {
        float val_l = sinf(2.0f * 3.14159265358979f * 440.0f * (float)i / (float)kSampleRate);
        float val_r = sinf(2.0f * 3.14159265358979f * 880.0f * (float)i / (float)kSampleRate);
        input[i * 2 + 0] = val_l;
        input[i * 2 + 1] = val_r;
    }

    // Encode in blocks. For interleaved stereo: advance=2, spacing=1
    for (int offset = 0; offset < num_samples; offset += kBlockSize) {
        enc.Encode(input.data() + offset * 2, kBlockSize, 2, 1);
    }

    // Finish the stream
    enc.reinit(0);

    int enc_avail = enc.Available();
    if (enc_avail <= 0) {
        FAIL("Encoder produced no output");
        return;
    }

    // Decode
    FlacDecoder dec;
    void *dst = dec.DecodeGetSrcBuffer(enc_avail);
    assert(dst != nullptr);
    memcpy(dst, enc.Get(), enc_avail);
    dec.DecodeWrote(enc_avail);

    int dec_avail = dec.Available();
    if (dec_avail < num_samples * 2) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Decoded %d samples, expected >= %d", dec_avail, num_samples * 2);
        FAIL(msg);
        return;
    }

    // Compare (decoded is also interleaved)
    float *decoded = dec.Get();
    float max_err = 0.0f;
    for (int i = 0; i < num_samples * 2; i++) {
        float err = fabsf(input[i] - decoded[i]);
        if (err > max_err) max_err = err;
    }

    if (max_err <= kTolerance) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Max error %.8f exceeds tolerance %.8f", max_err, kTolerance);
        FAIL(msg);
    }
}

// ============================================================
// Test 8: Encoder advance/spacing convention
// ============================================================
static void test_encoder_advance_spacing() {
    TEST("FlacEncoder advance/spacing matches VorbisEncoder calling convention");

    // Test non-interleaved stereo: channels in separate buffers packed as
    // [L0 R0 L1 R1 ...] with advance=1, spacing=num_samples
    // This is how NJClient may call when channels are in separate blocks

    const int nsamples = kBlockSize;

    // Create planar layout: all L samples then all R samples
    std::vector<float> planar(nsamples * 2);
    for (int i = 0; i < nsamples; i++) {
        float val_l = sinf(2.0f * 3.14159265358979f * 440.0f * (float)i / (float)kSampleRate);
        float val_r = sinf(2.0f * 3.14159265358979f * 880.0f * (float)i / (float)kSampleRate);
        planar[i] = val_l;                  // L channel
        planar[i + nsamples] = val_r;       // R channel
    }

    // Encode with advance=1, spacing=nsamples (planar)
    FlacEncoder enc(kSampleRate, 2, 128, 1);
    assert(enc.isError() == 0);
    enc.Encode(planar.data(), nsamples, 1, nsamples);

    // Also encode same data with interleaved layout for comparison
    std::vector<float> interleaved(nsamples * 2);
    for (int i = 0; i < nsamples; i++) {
        interleaved[i * 2 + 0] = planar[i];
        interleaved[i * 2 + 1] = planar[i + nsamples];
    }
    FlacEncoder enc2(kSampleRate, 2, 128, 2);
    assert(enc2.isError() == 0);
    enc2.Encode(interleaved.data(), nsamples, 2, 1);

    // Finish both
    enc.reinit(0);
    enc2.reinit(0);

    // Both should produce output
    bool ok = (enc.Available() > 0 && enc2.Available() > 0 &&
               enc.isError() == 0 && enc2.isError() == 0);

    if (!ok) {
        FAIL("One or both encoders produced no output or had error");
        return;
    }

    // Decode both and compare -- they should produce identical audio
    FlacDecoder dec1, dec2;

    int avail1 = enc.Available();
    void *d1 = dec1.DecodeGetSrcBuffer(avail1);
    memcpy(d1, enc.Get(), avail1);
    dec1.DecodeWrote(avail1);

    int avail2 = enc2.Available();
    void *d2 = dec2.DecodeGetSrcBuffer(avail2);
    memcpy(d2, enc2.Get(), avail2);
    dec2.DecodeWrote(avail2);

    int da1 = dec1.Available();
    int da2 = dec2.Available();

    if (da1 != da2 || da1 < nsamples * 2) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Decoded sample counts differ: %d vs %d (expected %d)",
                 da1, da2, nsamples * 2);
        FAIL(msg);
        return;
    }

    float *out1 = dec1.Get();
    float *out2 = dec2.Get();
    float max_err = 0.0f;
    for (int i = 0; i < da1; i++) {
        float err = fabsf(out1[i] - out2[i]);
        if (err > max_err) max_err = err;
    }

    if (max_err == 0.0f) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Planar vs interleaved output differs by %.8f", max_err);
        FAIL(msg);
    }
}

// ============================================================
// Main
// ============================================================
int main() {
    printf("=== FLAC Codec Tests ===\n\n");

    test_encoder_mono_produces_output();
    test_encoder_stereo_produces_output();
    test_encoder_no_error();
    test_encoder_reinit();
    test_decoder_metadata();
    test_roundtrip_mono();
    test_roundtrip_stereo();
    test_encoder_advance_spacing();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
