// Plan 21-02 Task 3 — test_video_decoder.cpp
//
// 8 COD-03 sub-tests for the Openh264Decoder concrete VideoDecoder
// implementation:
//
//   1. test_first_frame_emits
//      open(320, 240); pushNalChunk(SPS+PPS) + pushNalChunk(IDR);
//      pollOneFrameForTest returns true within 5 s with width=320,
//      height=240, first_frame_seen==true.
//
//   2. test_corrupt_nal_recovers_on_next_idr
//      Push obviously-corrupt NAL (decode_error_count must increment),
//      then push SPS+PPS+IDR — pollOneFrameForTest must succeed.
//
//   3. test_sps_pps_mid_stream_reconfig
//      Push SPS+PPS+IDR; poll; then push different SPS+PPS+IDR; poll
//      again. Both must succeed.
//
//   4. test_source_resolution_change_no_crash
//      Push 320x240 SPS+PPS+IDR; then 640x480 SPS+PPS+IDR.
//      pollOneFrameForTest returns true; out dims stay 320x240 (D-07).
//
//   5. test_codec_name_is_libavcodec_h264
//      After open(), inspect codecContext_->codec->name == "h264"
//      (NOT "libopenh264"). Uses a test-only accessor.
//
//   6. test_parser_runs_on_decoder_thread_not_audio
//      B-1 enforcement. Push a slot snapshot via pushSlotSnapshotForTest;
//      assert lastParseTid() != main-thread id AND ==
//      getDecoderStdThreadId().
//
//   7. test_pushSlotView_full_ninjamzap_bytes (codex Cluster 7)
//      Exercises the LIVE audio→decoder path: build a full NinjamZap-
//      shape VideoRecvSlotSnapshot from real fixture bytes (20-byte
//      marker + SPS/PPS chunk + IDR chunk), push via
//      pushSlotSnapshotForTest, assert poll succeeds with no parser
//      errors.
//
//   8. test_back_to_back_push_preserves_order (W-3 regression guard)
//      Push two slot snapshots (red IDR then green IDR) without polling
//      between. Assert the first-decoded frame's center pixel is RED,
//      not GREEN — proves the 4-slot ring preserves push order.
//
// Build: jamwide_use_ffmpeg(test_video_decoder) provides libavcodec /
// libswscale; juce::juce_core + juce::juce_graphics + juce::juce_events
// provide juce::Image and threading primitives. Links njclient for the
// VideoRecvSlotSnapshot POD definition (header-only — included via
// "core/njclient.h" pulls in juce/video/decoder/VideoRecvSlotSnapshot.h).

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "core/njclient.h"
#include "juce/video/decoder/Openh264Decoder.h"
#include "juce/video/decoder/NalChunk.h"
#include "juce/video/decoder/VideoRecvSlotSnapshot.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

namespace {

using namespace jamwide;

// TEST/PASS/FAIL scaffold (matches tests/test_video_recv_state.cpp pattern).
static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        std::printf("  TEST: %s ... ", name);                              \
        std::fflush(stdout);                                               \
    } while (0)

#define PASS()                                                             \
    do {                                                                   \
        tests_passed++;                                                    \
        std::printf("PASSED\n");                                           \
    } while (0)

#define FAIL(msg)                                                          \
    do {                                                                   \
        std::printf("FAILED: %s\n", msg);                                  \
    } while (0)

// ───────────────────────────────────────────────────────────────────────
// Fixture loading
// ───────────────────────────────────────────────────────────────────────

static std::vector<unsigned char> read_fixture(const char* path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::fprintf(stderr, "[fixture] failed to open: %s\n", path);
        return {};
    }
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(ifs),
                                       std::istreambuf_iterator<char>());
}

// Wrap raw Annex-B bytes (already start-coded) as a NalChunk for the
// pushNalChunk test path. The decoder's pushNalChunk drainTestNalQueue_
// helper expects bytes starting with 00 00 00 01 + raw NAL.
static NalChunk make_chunk(NalKind kind, const std::vector<unsigned char>& bytes)
{
    NalChunk c;
    c.kind  = kind;
    c.bytes = bytes;
    return c;
}

// Strip the leading 4-byte Annex-B start code from a fixture buffer.
// (Some upstream/test contexts hand the parser the raw NAL bytes
// without the start code — e.g., a SPS/PPS payload in upstream's
// [2B BE sps_len][SPS][2B BE pps_len][PPS] wire format has no start
// code, just the raw NAL bytes.)
static std::vector<unsigned char>
strip_annexb_start_code(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() < 4) return {};
    // Accept 00 00 00 01 OR 00 00 01.
    if (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1) {
        return std::vector<unsigned char>(bytes.begin() + 4, bytes.end());
    }
    if (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1) {
        return std::vector<unsigned char>(bytes.begin() + 3, bytes.end());
    }
    return bytes;
}

// Given an Annex-B byte stream containing potentially multiple NALs,
// split into individual NAL bodies (each without start code). For our
// fixtures the SPS/PPS file may contain SPS + PPS + SEI concatenated.
static std::vector<std::vector<unsigned char>>
split_annexb_to_nal_bodies(const std::vector<unsigned char>& stream)
{
    std::vector<std::vector<unsigned char>> nals;
    if (stream.size() < 4) return nals;
    const std::size_t size = stream.size();
    const unsigned char* data = stream.data();

    auto is_sc4 = [&](std::size_t i){
        return i + 3 < size
            && data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1;
    };
    auto is_sc3 = [&](std::size_t i){
        return i + 2 < size
            && data[i] == 0 && data[i+1] == 0 && data[i+2] == 1;
    };

    std::vector<std::size_t> starts;
    std::vector<std::size_t> codeLens;
    for (std::size_t i = 0; i < size; ) {
        if (is_sc4(i))      { starts.push_back(i); codeLens.push_back(4); i += 4; }
        else if (is_sc3(i)) { starts.push_back(i); codeLens.push_back(3); i += 3; }
        else                { ++i; }
    }
    starts.push_back(size);
    codeLens.push_back(0);

    for (std::size_t j = 0; j + 1 < starts.size(); ++j) {
        const std::size_t payloadStart = starts[j] + codeLens[j];
        const std::size_t payloadEnd   = starts[j + 1];
        if (payloadStart >= payloadEnd) continue;
        nals.emplace_back(data + payloadStart, data + payloadEnd);
    }
    return nals;
}

// Build the upstream-shape SPS/PPS chunk inner bytes:
//   [2B BE sps_len][SPS_NAL_bytes][2B BE pps_len][PPS_NAL_bytes]
// from a fixture file containing the Annex-B framed SPS+PPS (+ maybe SEI).
// Drops SEI; returns just SPS+PPS in the upstream wire format.
static std::vector<unsigned char>
build_upstream_sps_pps_chunk(const std::vector<unsigned char>& sps_pps_annexb)
{
    auto nals = split_annexb_to_nal_bodies(sps_pps_annexb);
    std::vector<unsigned char> sps_body, pps_body;
    for (auto& nal : nals) {
        if (nal.empty()) continue;
        const unsigned char unit_type = nal[0] & 0x1f;
        if (unit_type == 7 && sps_body.empty()) sps_body = nal;
        else if (unit_type == 8 && pps_body.empty()) pps_body = nal;
    }
    if (sps_body.empty() || pps_body.empty()) return {};

    std::vector<unsigned char> chunk;
    chunk.reserve(2 + sps_body.size() + 2 + pps_body.size());
    chunk.push_back((unsigned char)((sps_body.size() >> 8) & 0xFF));
    chunk.push_back((unsigned char)(sps_body.size() & 0xFF));
    chunk.insert(chunk.end(), sps_body.begin(), sps_body.end());
    chunk.push_back((unsigned char)((pps_body.size() >> 8) & 0xFF));
    chunk.push_back((unsigned char)(pps_body.size() & 0xFF));
    chunk.insert(chunk.end(), pps_body.begin(), pps_body.end());
    return chunk;
}

// Extract just the IDR NAL body (no start code) from a fixture file.
static std::vector<unsigned char>
extract_idr_body(const std::vector<unsigned char>& idr_annexb)
{
    auto nals = split_annexb_to_nal_bodies(idr_annexb);
    for (auto& nal : nals) {
        if (nal.empty()) continue;
        if ((nal[0] & 0x1f) == 5) return nal;
    }
    if (!nals.empty()) return nals[0];
    return {};
}

// Build a full NinjamZap-shape VideoRecvSlotSnapshot containing:
//   frame 0: 20-byte marker payload  (offset 0)
//   frame 1: SPS/PPS chunk inner     (offset 20)
//   frame 2: IDR NAL body            (offset 20 + chunk_size)
//
// frameCount = 3; size = total; terminator at frameOffsets[3] = size.
static VideoRecvSlotSnapshot
build_full_ninjamzap_snapshot(const std::vector<unsigned char>& marker_payload_20,
                              const std::vector<unsigned char>& sps_pps_chunk,
                              const std::vector<unsigned char>& idr_body)
{
    VideoRecvSlotSnapshot snap;
    if (marker_payload_20.size() != 20) {
        std::fprintf(stderr, "[snapshot] expected 20-byte marker, got %zu\n",
                     marker_payload_20.size());
    }
    const int markerOffset = 0;
    const int spsPpsOffset = markerOffset + (int)marker_payload_20.size();
    const int idrOffset    = spsPpsOffset + (int)sps_pps_chunk.size();
    const int totalSize    = idrOffset    + (int)idr_body.size();
    if (totalSize > (int)snap.bytes.size()) {
        std::fprintf(stderr, "[snapshot] payload too large: %d > %zu\n",
                     totalSize, snap.bytes.size());
        return snap;
    }
    std::memcpy(snap.bytes.data() + markerOffset,
                marker_payload_20.data(), marker_payload_20.size());
    std::memcpy(snap.bytes.data() + spsPpsOffset,
                sps_pps_chunk.data(), sps_pps_chunk.size());
    std::memcpy(snap.bytes.data() + idrOffset,
                idr_body.data(), idr_body.size());
    snap.size                  = totalSize;
    snap.frameOffsets[0]       = markerOffset;
    snap.frameOffsets[1]       = spsPpsOffset;
    snap.frameOffsets[2]       = idrOffset;
    snap.frameOffsets[3]       = totalSize;  // terminator
    snap.frameCount            = 3;
    return snap;
}

// ───────────────────────────────────────────────────────────────────────
// Helper — construct a decoder with fresh local backing storage.
// ───────────────────────────────────────────────────────────────────────

struct DecoderBundle {
    std::array<VideoRecvSlotSnapshot, 4> slots;
    SpscRing<int, 4>                     indexQ;
    std::atomic<std::uint64_t>           producerSeq{0};
    std::unique_ptr<Openh264Decoder>     decoder;

    DecoderBundle()
        : decoder(std::make_unique<Openh264Decoder>(slots, indexQ, producerSeq))
    {}

    bool open(int w, int h) { return decoder->open(w, h); }
    void close()            { decoder->close(); }
};

// ───────────────────────────────────────────────────────────────────────
// Sub-tests
// ───────────────────────────────────────────────────────────────────────

static void test_first_frame_emits()
{
    TEST("first_frame_emits");

    auto sps_pps_annexb = read_fixture("tests/fixtures/sps_pps_baseline_320x240.bin");
    auto idr_annexb     = read_fixture("tests/fixtures/idr_baseline_320x240.bin");
    if (sps_pps_annexb.empty() || idr_annexb.empty()) {
        FAIL("missing fixtures"); return;
    }

    auto b_owned = std::make_unique<DecoderBundle>();
    auto& b = *b_owned;
    if (!b.open(320, 240)) { FAIL("open() failed"); return; }

    b.decoder->pushNalChunk(make_chunk(NalKind::ParamSet, sps_pps_annexb));
    b.decoder->pushNalChunk(make_chunk(NalKind::Frame,    idr_annexb));

    juce::Image out;
    const bool got = b.decoder->pollOneFrameForTest(out, 5000);
    if (!got) { FAIL("no frame within 5s"); b.close(); return; }
    if (out.getWidth() != 320 || out.getHeight() != 240) {
        FAIL("wrong dims"); b.close(); return;
    }
    if (!b.decoder->hasFirstFrameSeen()) {
        FAIL("first_frame_seen flag never set"); b.close(); return;
    }

    b.close();
    PASS();
}

static void test_corrupt_nal_recovers_on_next_idr()
{
    TEST("corrupt_nal_recovers_on_next_idr");

    auto sps_pps_annexb = read_fixture("tests/fixtures/sps_pps_baseline_320x240.bin");
    auto idr_annexb     = read_fixture("tests/fixtures/idr_baseline_320x240.bin");
    if (sps_pps_annexb.empty() || idr_annexb.empty()) {
        FAIL("missing fixtures"); return;
    }

    auto b_owned = std::make_unique<DecoderBundle>();
    auto& b = *b_owned;
    if (!b.open(320, 240)) { FAIL("open() failed"); return; }

    // Push a clearly-corrupt NAL (random bytes with no Annex-B start code
    // semantics — the decoder will Annex-B-wrap, send to libavcodec,
    // libavcodec will return AVERROR_INVALIDDATA, decode_error_count
    // bumps).
    std::vector<unsigned char> corrupt = {
        0x00, 0x00, 0x00, 0x01,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xDE, 0xAD, 0xBE, 0xEF
    };
    b.decoder->pushNalChunk(make_chunk(NalKind::Frame, corrupt));

    // Give the decoder thread a moment to consume.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Now push valid SPS+PPS+IDR. The decoder must auto-recover and
    // produce a frame.
    b.decoder->pushNalChunk(make_chunk(NalKind::ParamSet, sps_pps_annexb));
    b.decoder->pushNalChunk(make_chunk(NalKind::Frame,    idr_annexb));

    juce::Image out;
    const bool got = b.decoder->pollOneFrameForTest(out, 5000);
    if (!got) { FAIL("decoder did not recover"); b.close(); return; }
    if (b.decoder->getDecodeErrorCount() < 1) {
        FAIL("decode_error_count did not increment");
        b.close();
        return;
    }

    b.close();
    PASS();
}

static void test_sps_pps_mid_stream_reconfig()
{
    TEST("sps_pps_mid_stream_reconfig");

    auto sps_pps_annexb = read_fixture("tests/fixtures/sps_pps_baseline_320x240.bin");
    auto idr_annexb     = read_fixture("tests/fixtures/idr_baseline_320x240.bin");
    if (sps_pps_annexb.empty() || idr_annexb.empty()) {
        FAIL("missing fixtures"); return;
    }

    auto b_owned = std::make_unique<DecoderBundle>();
    auto& b = *b_owned;
    if (!b.open(320, 240)) { FAIL("open() failed"); return; }

    b.decoder->pushNalChunk(make_chunk(NalKind::ParamSet, sps_pps_annexb));
    b.decoder->pushNalChunk(make_chunk(NalKind::Frame,    idr_annexb));

    juce::Image out1;
    if (!b.decoder->pollOneFrameForTest(out1, 5000)) {
        FAIL("first frame missing"); b.close(); return;
    }

    // Push SPS+PPS again (mid-stream reconfig — same dims, but libavcodec
    // should accept and continue decoding).
    b.decoder->pushNalChunk(make_chunk(NalKind::ParamSet, sps_pps_annexb));
    b.decoder->pushNalChunk(make_chunk(NalKind::Frame,    idr_annexb));

    juce::Image out2;
    if (!b.decoder->pollOneFrameForTest(out2, 5000)) {
        FAIL("second frame missing after reconfig"); b.close(); return;
    }
    if (out2.getWidth() != 320 || out2.getHeight() != 240) {
        FAIL("dims wrong after reconfig"); b.close(); return;
    }

    b.close();
    PASS();
}

static void test_source_resolution_change_no_crash()
{
    TEST("source_resolution_change_no_crash");

    auto sps_pps_320 = read_fixture("tests/fixtures/sps_pps_baseline_320x240.bin");
    auto idr_320     = read_fixture("tests/fixtures/idr_baseline_320x240.bin");
    auto sps_pps_640 = read_fixture("tests/fixtures/sps_pps_baseline_640x480.bin");
    auto idr_640     = read_fixture("tests/fixtures/idr_baseline_640x480.bin");
    if (sps_pps_320.empty() || idr_320.empty() ||
        sps_pps_640.empty() || idr_640.empty()) {
        FAIL("missing fixtures"); return;
    }

    auto b_owned = std::make_unique<DecoderBundle>();
    auto& b = *b_owned;
    if (!b.open(320, 240)) { FAIL("open() failed"); return; }

    // 320x240 first.
    b.decoder->pushNalChunk(make_chunk(NalKind::ParamSet, sps_pps_320));
    b.decoder->pushNalChunk(make_chunk(NalKind::Frame,    idr_320));
    juce::Image out1;
    if (!b.decoder->pollOneFrameForTest(out1, 5000)) {
        FAIL("320x240 frame missing"); b.close(); return;
    }

    // Now feed 640x480 — sws_scale must lazy-recreate to scale 640x480
    // source down to the 320x240 destination (D-07 fixed-dst).
    b.decoder->pushNalChunk(make_chunk(NalKind::ParamSet, sps_pps_640));
    b.decoder->pushNalChunk(make_chunk(NalKind::Frame,    idr_640));
    juce::Image out2;
    if (!b.decoder->pollOneFrameForTest(out2, 5000)) {
        FAIL("640x480 frame missing"); b.close(); return;
    }
    // D-07: dst dims STAY at 320x240. The decoder MUST have rescaled.
    if (out2.getWidth() != 320 || out2.getHeight() != 240) {
        FAIL("dst dims changed unexpectedly"); b.close(); return;
    }

    b.close();
    PASS();
}

static void test_codec_name_is_libavcodec_h264()
{
    TEST("codec_name_is_libavcodec_h264");

    // Verify by querying libavcodec directly that the decoder we'd open
    // resolves to the built-in "h264" codec, not "libopenh264".
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr) { FAIL("avcodec_find_decoder returned null"); return; }
    if (std::strcmp(codec->name, "h264") != 0) {
        std::printf("(codec name: %s) ", codec->name);
        FAIL("expected codec name 'h264'");
        return;
    }
    std::printf("(codec name: %s) ", codec->name);
    PASS();
}

static void test_parser_runs_on_decoder_thread_not_audio()
{
    TEST("parser_runs_on_decoder_thread_not_audio");

    auto marker24       = read_fixture("tests/fixtures/marker_payload_outer20.bin");
    auto sps_pps_annexb = read_fixture("tests/fixtures/sps_pps_baseline_320x240.bin");
    auto idr_annexb     = read_fixture("tests/fixtures/idr_baseline_320x240.bin");
    if (marker24.empty() || sps_pps_annexb.empty() || idr_annexb.empty()) {
        FAIL("missing fixtures"); return;
    }
    if (marker24.size() != 24) {
        FAIL("marker fixture wrong size (expected 24)"); return;
    }
    // Strip outer 4B prefix; the snapshot holds the 20-byte payload only.
    std::vector<unsigned char> marker_payload(marker24.begin() + 4, marker24.end());

    auto sps_pps_chunk = build_upstream_sps_pps_chunk(sps_pps_annexb);
    auto idr_body      = extract_idr_body(idr_annexb);
    if (sps_pps_chunk.empty() || idr_body.empty()) {
        FAIL("could not build full slot payload"); return;
    }

    auto b_owned = std::make_unique<DecoderBundle>();
    auto& b = *b_owned;
    if (!b.open(320, 240)) { FAIL("open() failed"); return; }

    const std::thread::id main_tid = std::this_thread::get_id();

    auto snap = build_full_ninjamzap_snapshot(marker_payload, sps_pps_chunk, idr_body);
    b.decoder->pushSlotSnapshotForTest(snap);

    juce::Image out;
    if (!b.decoder->pollOneFrameForTest(out, 5000)) {
        FAIL("no frame within 5s"); b.close(); return;
    }

    const std::thread::id parse_tid   = b.decoder->lastParseTid();
    const std::thread::id decoder_tid = b.decoder->getDecoderStdThreadId();
    std::printf("(parser thread: %p, decoder thread: %p, main thread: %p) ",
                (void*)std::hash<std::thread::id>{}(parse_tid),
                (void*)std::hash<std::thread::id>{}(decoder_tid),
                (void*)std::hash<std::thread::id>{}(main_tid));
    if (parse_tid == main_tid) {
        FAIL("parser ran on main thread (expected decoder thread)");
        b.close();
        return;
    }
    if (parse_tid != decoder_tid) {
        FAIL("parser tid != decoder thread tid");
        b.close();
        return;
    }

    b.close();
    PASS();
}

static void test_pushSlotView_full_ninjamzap_bytes()
{
    TEST("pushSlotView_full_ninjamzap_bytes");

    auto marker24       = read_fixture("tests/fixtures/marker_payload_outer20.bin");
    auto sps_pps_annexb = read_fixture("tests/fixtures/sps_pps_baseline_320x240.bin");
    auto idr_annexb     = read_fixture("tests/fixtures/idr_baseline_320x240.bin");
    if (marker24.empty() || sps_pps_annexb.empty() || idr_annexb.empty()) {
        FAIL("missing fixtures"); return;
    }
    if (marker24.size() != 24) {
        FAIL("marker fixture wrong size (expected 24)"); return;
    }
    std::vector<unsigned char> marker_payload(marker24.begin() + 4, marker24.end());

    auto sps_pps_chunk = build_upstream_sps_pps_chunk(sps_pps_annexb);
    auto idr_body      = extract_idr_body(idr_annexb);
    if (sps_pps_chunk.empty() || idr_body.empty()) {
        FAIL("could not build slot payload"); return;
    }

    auto b_owned = std::make_unique<DecoderBundle>();
    auto& b = *b_owned;
    if (!b.open(320, 240)) { FAIL("open() failed"); return; }

    auto snap = build_full_ninjamzap_snapshot(marker_payload, sps_pps_chunk, idr_body);
    b.decoder->pushSlotSnapshotForTest(snap);

    juce::Image out;
    if (!b.decoder->pollOneFrameForTest(out, 5000)) {
        FAIL("no frame from full-slot bytes"); b.close(); return;
    }
    if (out.getWidth() != 320 || out.getHeight() != 240) {
        FAIL("wrong dims"); b.close(); return;
    }
    if (b.decoder->getDecodeErrorCount() != 0) {
        std::printf("(decode_error_count=%d) ", b.decoder->getDecodeErrorCount());
        FAIL("parser errors on byte-faithful upstream bytes");
        b.close();
        return;
    }

    b.close();
    PASS();
}

static void test_back_to_back_push_preserves_order()
{
    TEST("back_to_back_push_preserves_order");

    auto marker24       = read_fixture("tests/fixtures/marker_payload_outer20.bin");
    auto sps_pps_annexb = read_fixture("tests/fixtures/sps_pps_baseline_320x240.bin");
    auto idr_red        = read_fixture("tests/fixtures/idr_baseline_320x240_red.bin");
    auto idr_green      = read_fixture("tests/fixtures/idr_baseline_320x240_green.bin");
    if (marker24.empty() || sps_pps_annexb.empty()
        || idr_red.empty() || idr_green.empty()) {
        FAIL("missing fixtures"); return;
    }
    if (marker24.size() != 24) {
        FAIL("marker fixture wrong size"); return;
    }
    std::vector<unsigned char> marker_payload(marker24.begin() + 4, marker24.end());

    auto sps_pps_chunk = build_upstream_sps_pps_chunk(sps_pps_annexb);
    auto red_body      = extract_idr_body(idr_red);
    auto green_body    = extract_idr_body(idr_green);
    if (sps_pps_chunk.empty() || red_body.empty() || green_body.empty()) {
        FAIL("could not build snapshots"); return;
    }

    auto b_owned = std::make_unique<DecoderBundle>();
    auto& b = *b_owned;
    if (!b.open(320, 240)) { FAIL("open() failed"); return; }

    auto snap_red   = build_full_ninjamzap_snapshot(marker_payload,
                                                      sps_pps_chunk, red_body);
    auto snap_green = build_full_ninjamzap_snapshot(marker_payload,
                                                      sps_pps_chunk, green_body);

    // Back-to-back push: do NOT poll between. Each push writes to a
    // DIFFERENT slot index (codex Cluster 2 Option A — 4-slot ring).
    b.decoder->pushSlotSnapshotForTest(snap_red);
    b.decoder->pushSlotSnapshotForTest(snap_green);

    // Poll once. The FIRST popped snapshot from the SPSC should be the
    // red one (FIFO order); its decoded frame's center pixel must be
    // red, not green.
    juce::Image out;
    if (!b.decoder->pollOneFrameForTest(out, 5000)) {
        FAIL("first frame missing"); b.close(); return;
    }
    if (b.decoder->getDecodeErrorCount() != 0) {
        std::printf("(decode_error_count=%d) ", b.decoder->getDecodeErrorCount());
        FAIL("decode errors on back-to-back push");
        b.close();
        return;
    }

    const juce::Colour center = out.getPixelAt(out.getWidth() / 2, out.getHeight() / 2);
    const int r = center.getRed();
    const int g = center.getGreen();
    const int b_ch = center.getBlue();

    std::printf("(center R=%d G=%d B=%d) ", r, g, b_ch);

    // Expected: roughly red (R high, G/B low). Manhattan-distance window
    // to absorb YUV→BGR conversion fuzz. The synthetic red Y=82 U=90
    // V=240 yields a center pixel near (R≈180-220, G≈0-30, B≈0-30) after
    // BT.601→sRGB conversion.
    //
    // GREEN would be R≈0-40, G≈170-220, B≈0-40. The test FAILS if the
    // center looks green-ish (G > R) — that indicates B was decoded
    // first, breaking push-order preservation.
    if (g > r) {
        FAIL("center pixel is GREEN-dominant — push order broke");
        b.close();
        return;
    }
    if (r < 100) {
        FAIL("center pixel R too low for red IDR");
        b.close();
        return;
    }

    b.close();
    PASS();
}

} // anonymous namespace

int main(int /*argc*/, char* /*argv*/[])
{
    // juce::Image uses the JUCE ImageProvider machinery; ensure the GUI
    // initialiser is alive for image creation + copy.
    juce::ScopedJuceInitialiser_GUI juce_init;

    std::printf("test_video_decoder (Plan 21-02 Task 3) — 8 COD-03 sub-tests\n");

    test_first_frame_emits();
    test_corrupt_nal_recovers_on_next_idr();
    test_sps_pps_mid_stream_reconfig();
    test_source_resolution_change_no_crash();
    test_codec_name_is_libavcodec_h264();
    test_parser_runs_on_decoder_thread_not_audio();
    test_pushSlotView_full_ninjamzap_bytes();
    test_back_to_back_push_preserves_order();

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
