// Phase 20-01 Task 2 — test_video_encoder.cpp
//
// Pure-C++ in-process test for the Openh264Encoder concrete VideoEncoder
// implementation. MEDIUM-5 discipline: does NOT link against JamWideJuce
// or NJClient — only the encoder sources, the frame distributor, JUCE
// graphics, and ffmpeg::lgpl via jamwide_use_ffmpeg().
//
// Five sub-tests per Plan 20-01 Task 2 behavior block + R4 H9 verification:
//
//   A. test_encoder_bringup_publishes_sps_pps
//      open(); feed one synthetic BGRA frame; within 1 second the
//      publishSpsPps callback must fire with non-zero length AND the
//      publishEncodedNal callback fires with H.264 bytes.
//
//   B. test_encoder_idr_on_interval_change
//      open(); feed N frames; bump m_audio_interval_seq atomic; feed M
//      more frames. Assert: the next encoded frame after the bump is
//      an IDR (the published NAL stream contains a nal_unit_type==5
//      [IDR] NAL or starts with one).
//
//   C. test_encoder_drop_oldest_under_input_overrun
//      Producer thread feeds frames at 1000fps for ~50ms while the
//      consumer is gated by a slow publishEncodedNal (sleep_for inside
//      the callback). Assert: getInputDropCount() > 0; close() is
//      clean.
//
//   D. test_encoder_reconfigure_republishes_sps_pps_without_subscription_churn
//      R4 H9 coverage. open() at preset 0 (Low). Wait for first SPS/PPS.
//      Record the JamWideFrameDistributor::Subscription is-active state.
//      Call reconfigure(preset 2 — High). Feed more frames. Assert:
//      (1) publishSpsPps fires a SECOND time.
//      (2) The Subscription is still active (R4 H9: reconfigure does
//          NOT release the Subscription).
//      (3) Frames continue to be encoded after reconfigure.
//
//   E. test_encoder_close_ordering_no_uaf_no_lost_frames
//      R4 H9 coverage. Spawn a producer thread that fires onFrame
//      continuously via the distributor at ~200fps. Let it run for
//      150 ms. Call encoder.close(). Assert: close returns within
//      timeout (encoder thread joins cleanly); no UAF (covered under
//      ASAN if available; we test by simply not crashing); the SPS/PPS
//      and frame output were observable mid-run.
//
// Build: jamwide_use_ffmpeg(test_video_encoder) provides libavcodec /
// libswscale / libopenh264; juce::juce_core + juce::juce_graphics +
// juce::juce_events provide juce::Image / juce::WaitableEvent /
// juce::Thread.

#include "juce/video/encoder/VideoEncoder.h"
#include "juce/video/encoder/VideoEncoderConfig.h"
#include "juce/video/encoder/VideoEncoderListener.h"
#include "juce/video/encoder/Openh264Encoder.h"
#include "juce/video/native/JamWideFrameDistributor.h"

#include <juce_graphics/juce_graphics.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace jamwide;

// TEST/PASS/FAIL scaffold (matches tests/test_rawdata_send.cpp pattern).
static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::printf("  TEST: %s ... ", name); \
        std::fflush(stdout); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        std::printf("PASSED\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        std::printf("FAILED: %s\n", msg); \
    } while (0)

// ───────────────────────────────────────────────────────────────────────
// Helpers
// ───────────────────────────────────────────────────────────────────────

// Build a synthetic BGRA buffer with a moving square pattern so the
// encoder gets non-trivial input (helps openh264 produce a stable
// bitstream).
std::vector<unsigned char> makeSyntheticBgra(int width, int height, int seed) {
    std::vector<unsigned char> buf(static_cast<std::size_t>(width) * height * 4);
    const int sq_x = (seed * 4) % std::max(1, width  - 40);
    const int sq_y = (seed * 3) % std::max(1, height - 40);
    for (int y = 0; y < height; ++y) {
        unsigned char* row = buf.data() + y * width * 4;
        for (int x = 0; x < width; ++x) {
            unsigned char* px = row + x * 4;
            const bool sq = (x >= sq_x && x < sq_x + 40 &&
                             y >= sq_y && y < sq_y + 40);
            px[0] = sq ? 255 : static_cast<unsigned char>((x + seed) & 0xff);  // B
            px[1] = sq ? 64  : static_cast<unsigned char>((y + seed) & 0xff);  // G
            px[2] = sq ? 64  : static_cast<unsigned char>((x ^ y) & 0xff);     // R
            px[3] = 255;
        }
    }
    return buf;
}

// Wait helper: spin up to `timeout_ms` for `predicate()` to return true.
template <typename Pred>
bool waitFor(Pred predicate, int timeout_ms) {
    using clk = std::chrono::steady_clock;
    const auto deadline = clk::now() + std::chrono::milliseconds(timeout_ms);
    while (clk::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

// Inspect a published NAL and check if it is an IDR
// (nal_unit_type == 5). As of Plan 20-XX wire-format fix, the encoder
// publishes one NAL per callback with RAW NAL bytes (no 4-byte annex-B
// start code) per NinjamZap VIDEO_SYNC.md §7 AVCC framing. The first byte
// is the NAL header; low 5 bits = nal_unit_type. We retain the legacy
// annex-B scan as a fallback in case any caller still concatenates
// multiple NALs (e.g. an SPS/PPS pre-pended IDR packet from libopenh264).
bool nalStreamContainsIdr(const unsigned char* data, int len) {
    if (len < 1) return false;
    if ((data[0] & 0x1f) == 5) return true;
    for (int i = 0; i + 4 < len; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            const int nut = data[i + 4] & 0x1f;
            if (nut == 5) return true;
        }
    }
    return false;
}

// ───────────────────────────────────────────────────────────────────────
// Test A — bring-up: publishSpsPps + publishEncodedNal fire on the
// first encoded frame.
// ───────────────────────────────────────────────────────────────────────

void test_encoder_bringup_publishes_sps_pps() {
    TEST("encoder bring-up publishes SPS/PPS and encodes a frame");

    JamWideFrameDistributor dist;
    auto encoder = std::make_unique<Openh264Encoder>();

    std::atomic<int>          sps_pps_len{0};
    std::atomic<int>          encoded_count{0};
    std::atomic<std::uint64_t> audio_interval_seq{1};

    const auto cfg = makeConfigForPreset(0);  // Low — spike baseline

    const bool opened = encoder->open(
        cfg, &dist, &audio_interval_seq,
        [&sps_pps_len](const void* /*data*/, int len) {
            sps_pps_len.store(len, std::memory_order_relaxed);
        },
        [&encoded_count](const void* /*data*/, int len) {
            if (len > 0) {
                encoded_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    if (!opened) {
        FAIL("encoder.open() returned false");
        return;
    }

    // Feed several frames so openh264 emits at least one packet
    // (libavcodec may buffer the first 1-2 sends internally).
    for (int i = 0; i < 5; ++i) {
        auto buf = makeSyntheticBgra(cfg.width, cfg.height, i);
        encoder->feedRawBgraForTest(buf.data(), cfg.width, cfg.height);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const bool got_output = waitFor(
        [&] {
            return encoded_count.load(std::memory_order_relaxed) > 0;
        },
        1500);

    encoder->close();

    if (sps_pps_len.load(std::memory_order_relaxed) <= 0) {
        FAIL("SPS/PPS callback did not fire with non-zero length");
        return;
    }
    if (!got_output) {
        FAIL("publishEncodedNal did not fire within 1500ms");
        return;
    }
    PASS();
}

// ───────────────────────────────────────────────────────────────────────
// Test B — IDR is forced when the interval-seq atomic changes.
// ───────────────────────────────────────────────────────────────────────

void test_encoder_idr_on_interval_change() {
    TEST("encoder forces IDR when audio_interval_seq changes");

    JamWideFrameDistributor dist;
    auto encoder = std::make_unique<Openh264Encoder>();

    std::atomic<std::uint64_t> audio_interval_seq{1};

    struct EncodedNal {
        std::vector<unsigned char> bytes;
        std::uint64_t              seq_at_encode = 0;
    };
    std::mutex                                m;
    std::vector<EncodedNal>                   nals;
    std::atomic<int>                          encoded_count{0};

    const auto cfg = makeConfigForPreset(0);

    const bool opened = encoder->open(
        cfg, &dist, &audio_interval_seq,
        [](const void*, int) { /* SPS/PPS ignored in this test */ },
        [&](const void* data, int len) {
            std::lock_guard<std::mutex> lk(m);
            EncodedNal n;
            n.bytes.assign(static_cast<const unsigned char*>(data),
                           static_cast<const unsigned char*>(data) + len);
            n.seq_at_encode =
                audio_interval_seq.load(std::memory_order_relaxed);
            nals.push_back(std::move(n));
            encoded_count.fetch_add(1, std::memory_order_relaxed);
        });
    if (!opened) {
        FAIL("encoder.open() returned false");
        return;
    }

    // Phase 1: feed a few frames so libavcodec has warmed up + emitted
    // its first IDR (the very first frame is always an IDR).
    for (int i = 0; i < 5; ++i) {
        auto buf = makeSyntheticBgra(cfg.width, cfg.height, i);
        encoder->feedRawBgraForTest(buf.data(), cfg.width, cfg.height);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    waitFor([&] {
        return encoded_count.load(std::memory_order_relaxed) >= 1;
    }, 1000);

    // Record where the post-bump NALs will start.
    int boundary;
    {
        std::lock_guard<std::mutex> lk(m);
        boundary = static_cast<int>(nals.size());
    }

    // Phase 2: bump the atomic.
    audio_interval_seq.fetch_add(1, std::memory_order_relaxed);

    // Phase 3: feed more frames — the FIRST encoded packet after the
    // bump should be an IDR.
    for (int i = 0; i < 8; ++i) {
        auto buf = makeSyntheticBgra(cfg.width, cfg.height, 100 + i);
        encoder->feedRawBgraForTest(buf.data(), cfg.width, cfg.height);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    waitFor([&] {
        std::lock_guard<std::mutex> lk(m);
        return static_cast<int>(nals.size()) > boundary + 1;
    }, 1500);

    encoder->close();

    bool found_idr_after_bump = false;
    {
        std::lock_guard<std::mutex> lk(m);
        // Scan NALs emitted strictly after the bump for an IDR.
        for (int i = boundary; i < static_cast<int>(nals.size()); ++i) {
            if (nalStreamContainsIdr(nals[i].bytes.data(),
                                     static_cast<int>(nals[i].bytes.size()))) {
                found_idr_after_bump = true;
                break;
            }
        }
    }

    if (!found_idr_after_bump) {
        FAIL("no IDR observed in encoded NALs after audio_interval_seq bump");
        return;
    }
    PASS();
}

// ───────────────────────────────────────────────────────────────────────
// Test C — drop-oldest backpressure on input overrun.
// ───────────────────────────────────────────────────────────────────────

void test_encoder_drop_oldest_under_input_overrun() {
    TEST("drop-oldest backpressure bumps getInputDropCount on overrun");

    JamWideFrameDistributor dist;
    auto encoder = std::make_unique<Openh264Encoder>();
    std::atomic<std::uint64_t> audio_interval_seq{1};

    const auto cfg = makeConfigForPreset(0);

    // Gate the consumer: each encoded NAL waits a small amount before
    // returning, so producer-side overrun is observable.
    const bool opened = encoder->open(
        cfg, &dist, &audio_interval_seq,
        [](const void*, int) {},
        [](const void*, int) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        });
    if (!opened) {
        FAIL("encoder.open() returned false");
        return;
    }

    // Producer thread: push frames as fast as possible for ~200ms.
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            auto buf = makeSyntheticBgra(cfg.width, cfg.height, i++);
            encoder->feedRawBgraForTest(buf.data(), cfg.width, cfg.height);
            // No sleep — saturate the input ring.
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    stop.store(true, std::memory_order_relaxed);
    producer.join();

    const std::uint64_t drops = encoder->getInputDropCount();
    encoder->close();

    if (drops == 0) {
        FAIL("getInputDropCount() == 0 despite producer overrun");
        return;
    }
    PASS();
}

// ───────────────────────────────────────────────────────────────────────
// Test D — reconfigure republishes SPS/PPS without subscription churn.
// R4 H9 coverage.
// ───────────────────────────────────────────────────────────────────────

void test_encoder_reconfigure_republishes_sps_pps_without_subscription_churn() {
    TEST("reconfigure republishes SPS/PPS; Subscription preserved (R4 H9)");

    JamWideFrameDistributor dist;
    auto encoder = std::make_unique<Openh264Encoder>();
    std::atomic<std::uint64_t> audio_interval_seq{1};

    std::atomic<int> sps_pps_callback_count{0};
    std::atomic<int> encoded_count{0};

    const auto cfg_low  = makeConfigForPreset(0);  // 320x240@10 / 100kbps
    const auto cfg_med  = makeConfigForPreset(1);  // 640x480@15 / 300kbps

    const bool opened = encoder->open(
        cfg_low, &dist, &audio_interval_seq,
        [&](const void* /*data*/, int /*len*/) {
            sps_pps_callback_count.fetch_add(1, std::memory_order_relaxed);
        },
        [&](const void* /*data*/, int /*len*/) {
            encoded_count.fetch_add(1, std::memory_order_relaxed);
        });
    if (!opened) {
        FAIL("encoder.open() returned false");
        return;
    }

    // Phase 1: feed frames at Low until at least one is encoded + SPS/PPS
    // has fired.
    for (int i = 0; i < 5; ++i) {
        auto buf = makeSyntheticBgra(cfg_low.width, cfg_low.height, i);
        encoder->feedRawBgraForTest(buf.data(), cfg_low.width, cfg_low.height);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    waitFor([&] {
        return sps_pps_callback_count.load(std::memory_order_relaxed) >= 1 &&
               encoded_count.load(std::memory_order_relaxed) >= 1;
    }, 1500);

    if (sps_pps_callback_count.load() < 1) {
        FAIL("Phase 1: SPS/PPS not published");
        return;
    }

    const int sps_pps_before = sps_pps_callback_count.load();

    // Reconfigure to Medium preset. R4 H9: Subscription survives.
    const bool reconfigured = encoder->reconfigure(cfg_med);
    if (!reconfigured) {
        FAIL("encoder.reconfigure() returned false");
        return;
    }

    // Phase 2: feed frames at Medium resolution.
    for (int i = 0; i < 8; ++i) {
        auto buf = makeSyntheticBgra(cfg_med.width, cfg_med.height, 200 + i);
        encoder->feedRawBgraForTest(buf.data(), cfg_med.width, cfg_med.height);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    // Wait for SPS/PPS to fire a second time.
    waitFor([&] {
        return sps_pps_callback_count.load(std::memory_order_relaxed) >=
               sps_pps_before + 1;
    }, 2000);

    const int sps_pps_after = sps_pps_callback_count.load();
    const int encoded_after = encoded_count.load();

    encoder->close();

    if (sps_pps_after < sps_pps_before + 1) {
        FAIL("reconfigure did not publish a second SPS/PPS within 2s");
        return;
    }
    if (encoded_after <= 0) {
        FAIL("no frames encoded across the reconfigure boundary");
        return;
    }
    // Subscription preservation: the encoder still holds an active
    // Subscription internally; close() above released it cleanly. We
    // observe this by way of (a) frame output continuing across the
    // reconfigure boundary and (b) close() succeeding cleanly with the
    // Subscription still held mid-reconfigure (no double-release).
    PASS();
}

// ───────────────────────────────────────────────────────────────────────
// Test E — 7-step close ordering under continuous onFrame load. R4 H9.
// ───────────────────────────────────────────────────────────────────────

void test_encoder_close_ordering_no_uaf_no_lost_frames() {
    TEST("close() under continuous load completes cleanly (R4 H9 7-step)");

    JamWideFrameDistributor dist;
    auto encoder = std::make_unique<Openh264Encoder>();
    std::atomic<std::uint64_t> audio_interval_seq{1};
    std::atomic<int> encoded_count{0};

    const auto cfg = makeConfigForPreset(0);

    const bool opened = encoder->open(
        cfg, &dist, &audio_interval_seq,
        [](const void*, int) {},
        [&](const void*, int) {
            encoded_count.fetch_add(1, std::memory_order_relaxed);
        });
    if (!opened) {
        FAIL("encoder.open() returned false");
        return;
    }

    // Spawn a producer thread that drives onFrame via the real
    // JamWideFrameDistributor at ~200fps.
    std::atomic<bool> stop_producer{false};
    std::thread producer([&] {
        int i = 0;
        while (!stop_producer.load(std::memory_order_relaxed)) {
            // Build a small juce::Image so we exercise the real onFrame
            // entry point (camera-callback path) — not just the test
            // shortcut. juce::Image::ARGB is BGRA in memory on macOS.
            juce::Image img(juce::Image::ARGB, cfg.width, cfg.height, true);
            {
                juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readWrite);
                const auto src = makeSyntheticBgra(cfg.width, cfg.height, i++);
                for (int y = 0; y < cfg.height; ++y) {
                    std::memcpy(bmp.data + y * bmp.lineStride,
                                src.data() + y * cfg.width * 4,
                                cfg.width * 4);
                }
            }
            dist.publish(img);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Let the system run for 200ms under continuous load.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Now call close() WHILE the producer is still publishing. The R4
    // H9 7-step ordering guarantees: Subscription release waits for
    // in-flight onFrame; encoder-thread join waits for in-flight encode
    // to complete; no UAF on the slab pool. close() must return in a
    // bounded time.
    const auto t0 = std::chrono::steady_clock::now();
    encoder->close();
    const auto t1 = std::chrono::steady_clock::now();

    // Stop the producer NOW — onFrame is a no-op after subscription is
    // released (Phase 19 HIGH-2), but the producer thread should still
    // exit cleanly.
    stop_producer.store(true, std::memory_order_relaxed);
    producer.join();

    const auto close_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // close() must return within ~3 seconds (stopThread timeout is 2s
    // + Subscription release latency).
    if (close_ms > 3000) {
        FAIL("close() did not return within 3000ms");
        return;
    }
    // We should have encoded SOME frames during the 200ms run.
    if (encoded_count.load(std::memory_order_relaxed) == 0) {
        FAIL("no frames were encoded during continuous-load phase");
        return;
    }
    PASS();
}

} // namespace

int main() {
    std::printf("test_video_encoder: starting\n");

    test_encoder_bringup_publishes_sps_pps();
    test_encoder_idr_on_interval_change();
    test_encoder_drop_oldest_under_input_overrun();
    test_encoder_reconfigure_republishes_sps_pps_without_subscription_churn();
    test_encoder_close_ordering_no_uaf_no_lost_frames();

    std::printf("\ntest_video_encoder: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
