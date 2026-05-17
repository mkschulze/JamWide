// Plan 21-03 Task 3 — test_video_sync_e2e.cpp
//
// Integration test that ties Plan 21-01 (state machine) + Plan 21-02
// (decoder) + Plan 21-03 (distributor + sink) together.
//
// One sub-test (Plan 21-VALIDATION test-id 21-03-03):
//
//   test_three_peers_isolated_decode_errors:
//     - Instantiate 3 DecoderBundles (one per simulated peer: alice, bob,
//       charlie) each with its own PeerVideoSink wired via setSink.
//     - Drive each peer through 20 simulated intervals.
//       * alice: monotonic sender_seq 100..119, valid SPS+PPS+IDR fixtures
//       * bob:   monotonic sender_seq 200..219, valid SPS+PPS+IDR fixtures
//       * charlie: monotonic sender_seq 300..319, CORRUPT IDR (other frames OK)
//     - Each interval is pushed via pushSlotSnapshotForTest — the LIVE
//       audio-to-decoder path — exercising Plan 21-02's slot-snapshot ring
//       + Plan 21-03's sink update.
//     - After all intervals settle, assert:
//       (a) Per-peer isolation: alice.first_frame_seen == bob.first_frame_seen
//           == true, alice.decode_error_count == bob.decode_error_count == 0,
//           charlie.decode_error_count >= 1 (corrupt IDR triggered errors).
//       (b) Cross-plan codex concern (monotonic sender_seq alignment):
//           alice.sink.getLastObservedSenderSeqForTest() == 119
//           bob.sink.getLastObservedSenderSeqForTest() == 219
//           charlie.sink.getLastObservedSenderSeqForTest() must equal 319
//           IF charlie's marker was parsed (snapshots are processed in order
//           per the W-3 back-to-back regression guard); the per-peer
//           monotonic property is the key assertion.
//
// Build: links Openh264Decoder.cpp + JamWideRemoteFrameDistributor.cpp +
// PeerVideoSink.cpp + njclient (for VideoRecvSlotSnapshot POD). 16 MB
// main-thread stack on macOS for the 4×3 = 12 MB of snapshot buffers
// (each DecoderBundle has 4×4 MB = 16 MB of slot storage on stack-allocated
// std::array — heap-allocate the bundles via unique_ptr per peer).

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "core/njclient.h"
#include "juce/video/decoder/Openh264Decoder.h"
#include "juce/video/decoder/NalChunk.h"
#include "juce/video/decoder/VideoRecvSlotSnapshot.h"
#include "juce/video/distributor/JamWideRemoteFrameDistributor.h"
#include "juce/video/distributor/PeerVideoSink.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

namespace {

using namespace jamwide;

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

// ─── Fixture loaders (mirror tests/test_video_decoder.cpp helpers) ──────

static std::vector<unsigned char> read_fixture(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                       std::istreambuf_iterator<char>());
}

static std::vector<std::vector<unsigned char>>
split_annexb_to_nal_bodies(const std::vector<unsigned char>& annexb)
{
    std::vector<std::vector<unsigned char>> nals;
    std::size_t i = 0;
    while (i + 4 <= annexb.size()) {
        std::size_t starti = i;
        if (i + 4 <= annexb.size()
            && annexb[i] == 0 && annexb[i+1] == 0
            && annexb[i+2] == 0 && annexb[i+3] == 1) {
            starti = i + 4;
        } else if (i + 3 <= annexb.size()
                   && annexb[i] == 0 && annexb[i+1] == 0 && annexb[i+2] == 1) {
            starti = i + 3;
        } else {
            i++; continue;
        }
        std::size_t endi = starti;
        while (endi + 4 <= annexb.size()) {
            if (annexb[endi] == 0 && annexb[endi+1] == 0
                && (annexb[endi+2] == 1
                    || (annexb[endi+2] == 0 && annexb[endi+3] == 1))) break;
            endi++;
        }
        if (endi + 4 > annexb.size()) endi = annexb.size();
        nals.emplace_back(annexb.begin() + (long)starti,
                          annexb.begin() + (long)endi);
        i = endi;
    }
    return nals;
}

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

// Build a 20-byte marker payload: [4B BE sender_seq][16B audio_guid].
static std::vector<unsigned char> make_marker_payload_20(std::uint32_t sender_seq) {
    std::vector<unsigned char> v(20, 0);
    v[0] = (unsigned char)((sender_seq >> 24) & 0xFF);
    v[1] = (unsigned char)((sender_seq >> 16) & 0xFF);
    v[2] = (unsigned char)((sender_seq >> 8)  & 0xFF);
    v[3] = (unsigned char)( sender_seq        & 0xFF);
    // audio_guid (bytes 4..19) left zero — sufficient for sender_seq extraction.
    return v;
}

static VideoRecvSlotSnapshot
build_full_ninjamzap_snapshot(const std::vector<unsigned char>& marker_payload_20,
                              const std::vector<unsigned char>& sps_pps_chunk,
                              const std::vector<unsigned char>& idr_body)
{
    VideoRecvSlotSnapshot snap;
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
    snap.frameOffsets[3]       = totalSize;
    snap.frameCount            = 3;
    return snap;
}

// Variant for charlie: corrupts the IDR body so libavcodec emits errors.
static VideoRecvSlotSnapshot
build_corrupt_idr_snapshot(const std::vector<unsigned char>& marker_payload_20,
                            const std::vector<unsigned char>& sps_pps_chunk,
                            const std::vector<unsigned char>& valid_idr_body)
{
    // Corrupt the IDR: replace the NAL body with garbage that has NAL
    // unit_type 31 (reserved) so libavcodec rejects it.
    std::vector<unsigned char> corrupt_idr(valid_idr_body);
    if (corrupt_idr.empty()) return {};
    corrupt_idr[0] = 0x1F;  // NAL unit_type = 31 (reserved/invalid)
    // Also flip several body bytes so even type-bypass error recovery fails.
    for (std::size_t i = 1; i < corrupt_idr.size() && i < 16; ++i) {
        corrupt_idr[i] = (unsigned char)(corrupt_idr[i] ^ 0xFF);
    }
    return build_full_ninjamzap_snapshot(marker_payload_20, sps_pps_chunk, corrupt_idr);
}

// ─── DecoderBundle (mirror tests/test_video_decoder.cpp) ────────────────

struct DecoderBundle {
    std::array<VideoRecvSlotSnapshot, 4> slots;
    SpscRing<int, 4>                     indexQ;
    std::atomic<std::uint64_t>           producerSeq{0};
    std::unique_ptr<Openh264Decoder>     decoder;
    std::unique_ptr<PeerVideoSink>       sink;

    DecoderBundle()
        : decoder(std::make_unique<Openh264Decoder>(slots, indexQ, producerSeq))
        , sink   (std::make_unique<PeerVideoSink>(320, 240))
    {
        decoder->setSink(sink.get());
    }

    bool open(int w, int h) { return decoder->open(w, h); }
    void close()            { decoder->close(); }
};

// ─── Sub-test ───────────────────────────────────────────────────────────

static void test_three_peers_isolated_decode_errors()
{
    TEST("3 peers per-peer isolated decode + cross-plan sender_seq monotonic alignment");

    auto sps_pps_annexb = read_fixture("tests/fixtures/sps_pps_baseline_320x240.bin");
    auto idr_annexb     = read_fixture("tests/fixtures/idr_baseline_320x240.bin");
    if (sps_pps_annexb.empty() || idr_annexb.empty()) {
        FAIL("missing fixtures"); return;
    }
    auto sps_pps_chunk = build_upstream_sps_pps_chunk(sps_pps_annexb);
    auto idr_body      = extract_idr_body(idr_annexb);
    if (sps_pps_chunk.empty() || idr_body.empty()) {
        FAIL("failed to build sps/pps chunk or extract idr body"); return;
    }

    // Heap-allocate the bundles (each is 16 MB) so the test main thread's
    // stack doesn't blow up.
    auto alice   = std::make_unique<DecoderBundle>();
    auto bob     = std::make_unique<DecoderBundle>();
    auto charlie = std::make_unique<DecoderBundle>();

    if (!alice->open(320, 240))   { FAIL("alice open() failed");   return; }
    if (!bob->open(320, 240))     { FAIL("bob open() failed");     bob.reset(); alice->close(); return; }
    if (!charlie->open(320, 240)) { FAIL("charlie open() failed"); charlie.reset(); bob->close(); alice->close(); return; }

    // Drive 20 intervals per peer with monotonically increasing sender_seq.
    constexpr int kIntervals = 20;
    for (int n = 0; n < kIntervals; ++n) {
        const std::uint32_t alice_seq   = 100 + (std::uint32_t)n;
        const std::uint32_t bob_seq     = 200 + (std::uint32_t)n;
        const std::uint32_t charlie_seq = 300 + (std::uint32_t)n;

        auto alice_snap = build_full_ninjamzap_snapshot(
            make_marker_payload_20(alice_seq), sps_pps_chunk, idr_body);
        auto bob_snap = build_full_ninjamzap_snapshot(
            make_marker_payload_20(bob_seq), sps_pps_chunk, idr_body);
        auto charlie_snap = build_corrupt_idr_snapshot(
            make_marker_payload_20(charlie_seq), sps_pps_chunk, idr_body);

        alice->decoder->pushSlotSnapshotForTest(alice_snap);
        bob->decoder->pushSlotSnapshotForTest(bob_snap);
        charlie->decoder->pushSlotSnapshotForTest(charlie_snap);

        // Give the decoder threads a moment to drain (poll interval = 15 ms).
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    // Wait up to 5 s for alice + bob first_frame_seen to flip (poll-wait).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (alice->decoder->hasFirstFrameSeen() && bob->decoder->hasFirstFrameSeen()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // ── (a) Per-peer isolation assertions ──────────────────────────────
    if (!alice->decoder->hasFirstFrameSeen()) {
        FAIL("alice first_frame_seen never flipped (no valid decode?)"); goto cleanup;
    }
    if (!bob->decoder->hasFirstFrameSeen()) {
        FAIL("bob first_frame_seen never flipped"); goto cleanup;
    }
    // charlie's IDR is corrupt — first_frame_seen may or may not flip
    // depending on whether libavcodec emitted any partial frame; the key
    // assertion is that charlie has decode errors AND alice/bob do not.
    if (alice->decoder->getDecodeErrorCount() != 0) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "alice has decode errors (%d) — cross-peer contamination?",
                      alice->decoder->getDecodeErrorCount());
        FAIL(buf); goto cleanup;
    }
    if (bob->decoder->getDecodeErrorCount() != 0) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "bob has decode errors (%d) — cross-peer contamination?",
                      bob->decoder->getDecodeErrorCount());
        FAIL(buf); goto cleanup;
    }
    if (charlie->decoder->getDecodeErrorCount() < 1) {
        FAIL("charlie has zero decode errors (corrupt IDR not detected?)"); goto cleanup;
    }

    // ── (b) Cross-plan sender_seq monotonic alignment ─────────────────
    {
        const std::int64_t alice_seq_obs   = alice->sink->getLastObservedSenderSeqForTest();
        const std::int64_t bob_seq_obs     = bob->sink->getLastObservedSenderSeqForTest();
        const std::int64_t charlie_seq_obs = charlie->sink->getLastObservedSenderSeqForTest();

        if (alice_seq_obs != 119) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "alice last observed sender_seq=%lld; expected 119",
                          (long long)alice_seq_obs);
            FAIL(buf); goto cleanup;
        }
        if (bob_seq_obs != 219) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "bob last observed sender_seq=%lld; expected 219",
                          (long long)bob_seq_obs);
            FAIL(buf); goto cleanup;
        }
        // charlie's marker is parsed BEFORE the corrupt IDR (the marker is
        // the FIRST frame in the snapshot at offset 0 — parseSlotAndFeed_
        // forwards sender_seq to the sink BEFORE attempting the IDR which
        // generates the decode error). So charlie should reach 319 too.
        if (charlie_seq_obs != 319) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "charlie last observed sender_seq=%lld; expected 319 (marker should still parse despite corrupt IDR)",
                          (long long)charlie_seq_obs);
            FAIL(buf); goto cleanup;
        }
    }

    PASS();

cleanup:
    if (charlie) charlie->close();
    if (bob)     bob->close();
    if (alice)   alice->close();
}

} // anonymous namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("test_video_sync_e2e:\n");
    test_three_peers_isolated_decode_errors();

    std::printf("\nresults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
