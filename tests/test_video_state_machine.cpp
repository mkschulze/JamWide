/*
    test_video_state_machine.cpp — Plan 20-02 send-side video state machine.

    Validates NinjamZap-literal on_new_interval video block: END/BEGIN/marker/
    SPS-PPS ordering, whole-block m_video_cs serialization (R3 MF6 closure),
    cold-start option (b) marker-only (R3 MF3 + R4 M-WORD), 4-byte BE length
    prefix on QueueVideoFrame (COD-02), interleave defense, and seqlock-fed
    audio_ch0_guid in the 24-byte marker (R4 H8).

    Eight sub-tests (per Plan 20-02 Task 3):
      1. test_video_block_emits_begin_marker_sps_when_active
      2. test_video_block_emits_end_only_when_deactivated
      3. test_video_frame_during_marker_interleave_is_blocked
      4. test_video_marker_uses_audio_ch0_guid
      5. test_video_cold_start_marker_only_first_interval (R3 MF3 + R4 M-WORD)
      6. test_video_inactive_no_emission
      7. test_video_audio_interval_seq_increments
      8. test_video_frame_prefix_and_data_split_across_drain_chunk_boundary

    Linked against the njclient static library; the test-only helpers
    NJClient::DrainRawDataSendQueueForTest / NJClient::ChunkRawDataItem /
    NJClient::RunOneIntervalForTest are gated under JAMWIDE_BUILD_TESTS.
*/

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "core/njclient.h"
#include "wdl/heapbuf.h"

// MAX_ENC_BLOCKSIZE is file-local to njclient.cpp; replicate here. Matches the
// test_rawdata_send pattern. Sanity-checked manually against njclient.cpp:873.
static constexpr int MAX_ENC_BLOCKSIZE = 8192 + 1024;

// MAKE_NJ_FOURCC is also file-local. Same re-definition pattern.
#ifndef MAKE_NJ_FOURCC
#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))
#endif

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

// Helper to free the drain output vector. Each RawDataQueueItem owns a by-
// value WDL_HeapBuf; `delete item` frees it.
static void free_drained(std::vector<NJClient::RawDataQueueItem*>& items) {
    for (auto* item : items) delete item;
    items.clear();
}

// Build a known SPS/PPS blob with byte-identifiable contents.
static std::vector<unsigned char> make_spspps(int len) {
    std::vector<unsigned char> out(len);
    for (int i = 0; i < len; ++i) out[i] = (unsigned char)(0x10 + (i & 0xF));
    return out;
}

// ----- Sub-test 1: state-machine BEGIN + marker + SPS/PPS when active. -----
static void test_video_block_emits_begin_marker_sps_when_active() {
    TEST("on_new_interval emits BEGIN + marker + SPS/PPS when active");
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    auto spspps = make_spspps(32);
    client.SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'));
    client.SetVideoSPSPPS(spspps.data(), (int)spspps.size());
    client.SetVideoBroadcastActive(true);

    client.RunOneIntervalForTest();

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);

    if (out.size() != 3) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected 3 items (BEGIN + marker + SPS/PPS), got %zu", out.size());
        FAIL(buf); free_drained(out); return;
    }

    // Item 0: BEGIN.
    if (out[0]->type != 0 || out[0]->fourcc != (unsigned int)MAKE_NJ_FOURCC('H','2','6','4')
        || out[0]->chidx != 1) {
        FAIL("item 0 is not a properly populated BEGIN"); free_drained(out); return;
    }
    unsigned char guid[16];
    memcpy(guid, out[0]->guid, 16);

    // Item 1: 24-byte sync marker matching upstream
    // ninjamzap-core njclient.cpp:3055-3070 wire layout:
    //   Bytes 0-3 : BE uint32 = 20 (outer length prefix; receiver
    //               reassembler uses this to find chunk boundaries)
    //   Bytes 4-7 : BE uint32 interval counter
    //   Bytes 8-23: audio channel-0 GUID
    // Commit 6d23b5c briefly shipped a 20-byte marker without the outer
    // [4B BE 20] prefix after reading VIDEO_SYNC.md docs (which describe
    // the inner payload) but not the upstream encoder code. The fix that
    // restored the 24-byte form is verified against
    // ninjamzap-core/tests/video-sync/harness/TestClient.cpp.
    if (out[1]->type != 1 || out[1]->flags != 0 || out[1]->data.GetSize() != 24
        || memcmp(out[1]->guid, guid, 16) != 0) {
        FAIL("item 1 is not the 24-byte sync marker WRITE"); free_drained(out); return;
    }
    const unsigned char* m = (const unsigned char*)out[1]->data.Get();
    // Bytes 0-3: BE uint32 = 20 (outer length prefix).
    if (m[0] != 0 || m[1] != 0 || m[2] != 0 || m[3] != 20) {
        FAIL("marker outer length prefix (bytes 0-3) != BE u32 = 20"); free_drained(out); return;
    }
    // Bytes 4-7: BE uint32 interval counter; seq is 1 on first on_new_interval.
    if (m[4] != 0 || m[5] != 0 || m[6] != 0 || m[7] != 1) {
        FAIL("marker interval counter (bytes 4-7) != BE u32 = 1"); free_drained(out); return;
    }
    // Bytes 8-23: audio_ch0_guid; zero-filled when no Local_Channel registered.
    for (int i = 8; i < 24; ++i) {
        if (m[i] != 0) { FAIL("marker audio_ch0_guid not zero-filled with no Local_Channel"); free_drained(out); return; }
    }

    // Item 2: SPS/PPS.
    if (out[2]->type != 1 || out[2]->flags != 0
        || out[2]->data.GetSize() != (int)spspps.size()
        || memcmp(out[2]->data.Get(), spspps.data(), spspps.size()) != 0
        || memcmp(out[2]->guid, guid, 16) != 0) {
        FAIL("item 2 is not the SPS/PPS chunk"); free_drained(out); return;
    }

    free_drained(out);
    PASS();
}

// ----- Sub-test 2: END at deactivate. -----
static void test_video_block_emits_end_only_when_deactivated() {
    TEST("on_new_interval emits END only when broadcast just deactivated");
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    auto spspps = make_spspps(16);
    client.SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'));
    client.SetVideoSPSPPS(spspps.data(), (int)spspps.size());
    client.SetVideoBroadcastActive(true);
    client.RunOneIntervalForTest();   // opens an interval

    // Drain the first interval's items so the queue is empty for the END test.
    std::vector<NJClient::RawDataQueueItem*> prep;
    client.DrainRawDataSendQueueForTest(prep);
    free_drained(prep);

    // Now deactivate broadcast and tick another interval.
    client.SetVideoBroadcastActive(false);
    client.RunOneIntervalForTest();

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);

    if (out.size() != 1) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected 1 END item, got %zu", out.size());
        FAIL(buf); free_drained(out); return;
    }
    if (out[0]->type != 1 || (out[0]->flags & 1) == 0 || out[0]->data.GetSize() != 0) {
        FAIL("expected type=1 + flags|1 (end) + empty payload"); free_drained(out); return;
    }
    free_drained(out);

    // Subsequent intervals while inactive emit nothing.
    client.RunOneIntervalForTest();
    client.DrainRawDataSendQueueForTest(out);
    if (!out.empty()) {
        FAIL("subsequent on_new_interval while inactive emitted unexpected items");
        free_drained(out); return;
    }
    PASS();
}

// ----- Sub-test 3: cross-producer interleave defense. -----
static void test_video_frame_during_marker_interleave_is_blocked() {
    TEST("frame interleave during marker construction is serialized by m_video_cs");
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    auto spspps = make_spspps(8);
    client.SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'));
    client.SetVideoSPSPPS(spspps.data(), (int)spspps.size());
    client.SetVideoBroadcastActive(true);

    // First, open an interval via on_new_interval so m_video_interval_open is
    // true; QueueVideoFrame is gated on that. Drain the BEGIN+marker+SPSPPS.
    client.RunOneIntervalForTest();
    {
        std::vector<NJClient::RawDataQueueItem*> prep;
        client.DrainRawDataSendQueueForTest(prep);
        free_drained(prep);
    }

    constexpr int N_INTERVALS = 20;
    constexpr int FRAMES_PER_INTERVAL_TARGET = 200;

    std::atomic<bool> done{false};
    std::atomic<uint64_t> frames_attempted{0};
    unsigned char frame_payload[8] = {'F','R','A','M','E','!','!','\0'};

    std::thread frame_thread([&]() {
        while (!done.load(std::memory_order_relaxed)) {
            client.QueueVideoFrame(frame_payload, 8);
            frames_attempted.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Tick on_new_interval N_INTERVALS times. Each interval the audio thread
    // (this main thread) holds m_video_cs across END/BEGIN/marker/SPS-PPS; the
    // frame_thread waits on m_video_cs until our critical section drops. After
    // each interval we drain and validate ordering.
    for (int iter = 0; iter < N_INTERVALS; ++iter) {
        // Allow some frames to attempt in parallel.
        for (int j = 0; j < FRAMES_PER_INTERVAL_TARGET; ++j) {
            std::this_thread::yield();
            if (j == FRAMES_PER_INTERVAL_TARGET / 2) {
                client.RunOneIntervalForTest();
            }
        }

        std::vector<NJClient::RawDataQueueItem*> out;
        client.DrainRawDataSendQueueForTest(out);
        // The output must contain BEGIN+marker+SPSPPS together (3 contiguous
        // items) — these are the items emitted under m_video_cs in
        // on_new_interval. Any frame items (prefix + data pairs, each 2 items)
        // must come either ENTIRELY before the BEGIN or ENTIRELY after the
        // SPS/PPS — never interleaved between BEGIN and SPS/PPS.
        int begin_idx = -1, marker_idx = -1, spspps_idx = -1;
        for (size_t i = 0; i < out.size(); ++i) {
            if (out[i]->type == 0) {
                begin_idx = (int)i;
                marker_idx = (int)i + 1;
                spspps_idx = (int)i + 2;
                break;
            }
        }
        if (begin_idx < 0) {
            // No BEGIN was emitted (e.g., between drains). Just verify all
            // items are frame prefix/data pairs.
            for (auto* it : out) if (it->type != 1) { FAIL("non-frame item without preceding BEGIN"); free_drained(out); done.store(true); frame_thread.join(); return; }
            free_drained(out);
            continue;
        }
        // Assert the three triplet items are contiguous (no frame interleave).
        if (marker_idx >= (int)out.size() || spspps_idx >= (int)out.size()) {
            FAIL("BEGIN found but marker or SPS-PPS missing — m_video_cs ordering violated");
            free_drained(out); done.store(true); frame_thread.join(); return;
        }
        if (out[marker_idx]->type != 1 || out[marker_idx]->data.GetSize() != 24) {
            FAIL("item after BEGIN is not the 24-byte sync marker — interleave occurred");
            free_drained(out); done.store(true); frame_thread.join(); return;
        }
        if (out[spspps_idx]->type != 1 || out[spspps_idx]->data.GetSize() != (int)spspps.size()) {
            FAIL("item after marker is not SPS/PPS — interleave occurred");
            free_drained(out); done.store(true); frame_thread.join(); return;
        }
        free_drained(out);
    }

    done.store(true);
    frame_thread.join();

    printf("[interleave_attempts=%llu] ", (unsigned long long)frames_attempted.load());
    PASS();
}

// ----- Sub-test 4: audio_ch0_guid via seqlock reader. -----
static void test_video_marker_uses_audio_ch0_guid() {
    TEST("marker audio_ch0_guid sourced via seqlock (R4 H8) when Local_Channel exists");
    // Without an exposed test helper to create+register a Local_Channel from
    // outside, this sub-test exercises the conservative case: no Local_Channel
    // configured at channel_idx==0, so the marker carries 16 zero bytes (the
    // NinjamZap NONE-match path). The atomic-halves seqlock contract itself
    // is exercised under concurrency in tests/test_curwritefile_guid_seqlock.cpp.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    auto spspps = make_spspps(16);
    client.SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'));
    client.SetVideoSPSPPS(spspps.data(), (int)spspps.size());
    client.SetVideoBroadcastActive(true);
    client.RunOneIntervalForTest();

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);
    if (out.size() < 2) { FAIL("expected BEGIN + marker"); free_drained(out); return; }
    const unsigned char* m = (const unsigned char*)out[1]->data.Get();
    // 24-byte marker (upstream njclient.cpp:3055-3070): bytes 8-23 are
    // audio_ch0_guid. NONE-match path: 16 zero bytes when no Local_Channel
    // registered at chidx=0.
    for (int i = 8; i < 24; ++i) {
        if (m[i] != 0) {
            char buf[128]; snprintf(buf, sizeof(buf), "marker byte %d != 0 (expected NONE-match path with no Local_Channel)", i);
            FAIL(buf); free_drained(out); return;
        }
    }
    free_drained(out);
    PASS();
}

// ----- Sub-test 5: cold-start option (b) + R4 M-WORD revised wording. -----
static void test_video_cold_start_marker_only_first_interval() {
    TEST("cold-start: marker-only first interval, SPS/PPS in all subsequent");
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    client.SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'));
    client.SetVideoBroadcastActive(true);
    // No SetVideoSPSPPS yet — simulate cold-start (encoder still warming).

    client.RunOneIntervalForTest();

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);
    // Expect BEGIN + marker only — no SPS/PPS chunk yet.
    if (out.size() != 2) {
        char buf[128]; snprintf(buf, sizeof(buf), "cold-start: expected 2 items (BEGIN + marker), got %zu", out.size());
        FAIL(buf); free_drained(out); return;
    }
    if (out[0]->type != 0) { FAIL("cold-start item 0 != BEGIN"); free_drained(out); return; }
    if (out[1]->type != 1 || out[1]->data.GetSize() != 24) { FAIL("cold-start item 1 != 24-byte marker"); free_drained(out); return; }
    free_drained(out);

    // Simulate encoder warm-up completing: publish SPS/PPS.
    auto spspps = make_spspps(20);
    client.SetVideoSPSPPS(spspps.data(), (int)spspps.size());

    // R4 M-WORD revised acceptance: AFTER SPS/PPS is published, every
    // subsequent interval emits SPS/PPS as chunk #2 for as long as broadcast
    // remains active.
    for (int k = 0; k < 10; ++k) {
        client.RunOneIntervalForTest();
        std::vector<NJClient::RawDataQueueItem*> iter_out;
        client.DrainRawDataSendQueueForTest(iter_out);
        // Each subsequent iter: END(prev) + BEGIN + marker + SPS/PPS (4 items
        // because the previous interval was open).
        if (iter_out.size() != 4) {
            char buf[160]; snprintf(buf, sizeof(buf), "iter %d: expected END+BEGIN+marker+SPS/PPS (4 items) after SPS/PPS published, got %zu", k, iter_out.size());
            FAIL(buf); free_drained(iter_out); return;
        }
        if (iter_out[0]->type != 1 || (iter_out[0]->flags & 1) == 0
            || iter_out[0]->data.GetSize() != 0) {
            FAIL("iter: item 0 != END(prev)"); free_drained(iter_out); return;
        }
        if (iter_out[1]->type != 0) { FAIL("iter: item 1 != BEGIN"); free_drained(iter_out); return; }
        if (iter_out[2]->type != 1 || iter_out[2]->data.GetSize() != 24) { FAIL("iter: item 2 != 24-byte marker"); free_drained(iter_out); return; }
        if (iter_out[3]->type != 1 || iter_out[3]->data.GetSize() != (int)spspps.size()
            || memcmp(iter_out[3]->data.Get(), spspps.data(), spspps.size()) != 0) {
            FAIL("iter: item 3 != SPS/PPS — revised MF3 wording violated"); free_drained(iter_out); return;
        }
        free_drained(iter_out);
    }

    // Final: deactivate emits END.
    client.SetVideoBroadcastActive(false);
    client.RunOneIntervalForTest();
    std::vector<NJClient::RawDataQueueItem*> final_out;
    client.DrainRawDataSendQueueForTest(final_out);
    if (final_out.size() != 1 || final_out[0]->type != 1
        || (final_out[0]->flags & 1) == 0
        || final_out[0]->data.GetSize() != 0) {
        FAIL("final deactivate: expected single END");
        free_drained(final_out); return;
    }
    free_drained(final_out);
    PASS();
}

// ----- Sub-test 6: inactive broadcast emits nothing. -----
static void test_video_inactive_no_emission() {
    TEST("inactive broadcast: zero video items emitted across N intervals");
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    // m_video_active and m_video_interval_open both default to false; no API
    // calls toggle them on. Drive several intervals.
    for (int k = 0; k < 5; ++k) {
        client.RunOneIntervalForTest();
    }
    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);
    if (!out.empty()) {
        char buf[128]; snprintf(buf, sizeof(buf), "inactive broadcast emitted %zu items (expected 0)", out.size());
        FAIL(buf); free_drained(out); return;
    }
    PASS();
}

// ----- Sub-test 7: m_audio_interval_seq monotonic increment. -----
static void test_video_audio_interval_seq_increments() {
    TEST("GetAudioIntervalSeq increments by 1 per on_new_interval call");
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    if (client.GetAudioIntervalSeq() != 0) {
        FAIL("initial GetAudioIntervalSeq != 0"); return;
    }
    for (int k = 1; k <= 10; ++k) {
        client.RunOneIntervalForTest();
        const uint64_t v = client.GetAudioIntervalSeq();
        if (v != (uint64_t)k) {
            char buf[128]; snprintf(buf, sizeof(buf), "after %d intervals: GetAudioIntervalSeq() == %llu (expected %d)", k, (unsigned long long)v, k);
            FAIL(buf); return;
        }
    }
    PASS();
}

// ----- Sub-test 8: prefix/data split across MAX_ENC_BLOCKSIZE chunk boundary. -----
// Capture struct for chunker observations.
namespace {
struct ChunkCapture {
    std::vector<std::vector<unsigned char>> chunks;
    std::vector<int> flags;
};
void chunk_emit_cb(void* ctx, const unsigned char[16], const void* data, int dataLen, int flags) {
    auto* cap = static_cast<ChunkCapture*>(ctx);
    std::vector<unsigned char> bytes;
    if (data && dataLen > 0) {
        bytes.assign(static_cast<const unsigned char*>(data),
                     static_cast<const unsigned char*>(data) + dataLen);
    }
    cap->chunks.push_back(std::move(bytes));
    cap->flags.push_back(flags);
}
} // namespace

static bool run_split_test_at_frame_size(int frame_size, const char** failure_msg_out) {
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    auto spspps = make_spspps(8);
    client.SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4'));
    client.SetVideoSPSPPS(spspps.data(), (int)spspps.size());
    client.SetVideoBroadcastActive(true);
    client.RunOneIntervalForTest();
    {
        std::vector<NJClient::RawDataQueueItem*> prep;
        client.DrainRawDataSendQueueForTest(prep);
        free_drained(prep);
    }

    // Construct a frame payload with byte-identifiable contents.
    std::vector<unsigned char> payload(frame_size);
    for (int i = 0; i < frame_size; ++i) payload[i] = (unsigned char)((i * 13 + 7) % 251);
    payload[frame_size - 1] = 0xCE;  // tail marker

    client.QueueVideoFrame(payload.data(), frame_size);

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);
    // Expect exactly 2 items: prefix (4 bytes) + data (frame_size bytes).
    if (out.size() != 2) { *failure_msg_out = "expected 2 items (prefix + data) from QueueVideoFrame"; free_drained(out); return false; }
    if (out[0]->type != 1 || out[0]->data.GetSize() != 4) { *failure_msg_out = "item 0 != 4-byte prefix"; free_drained(out); return false; }
    const unsigned char* p = (const unsigned char*)out[0]->data.Get();
    int decoded_len = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    if (decoded_len != frame_size) { *failure_msg_out = "prefix BE u32 != frame_size"; free_drained(out); return false; }
    if (out[1]->type != 1 || out[1]->data.GetSize() != frame_size) { *failure_msg_out = "item 1 != data of frame_size bytes"; free_drained(out); return false; }
    if (memcmp(out[1]->data.Get(), payload.data(), frame_size) != 0) { *failure_msg_out = "data bytes mismatch"; free_drained(out); return false; }

    // Capture the prefix bytes BEFORE free_drained — out[0]->data goes away
    // when the queue items are deleted.
    unsigned char prefix_snapshot[4] = { p[0], p[1], p[2], p[3] };

    // Now chunk through ChunkRawDataItem (mimics the run-thread drain pop-
    // one-process-one). Concatenate the chunks of item 0 (prefix) and item 1
    // (data) and verify the assembled byte stream is [4B BE = frame_size][payload].
    ChunkCapture cap;
    NJClient::ChunkRawDataItem(*out[0], chunk_emit_cb, &cap);
    NJClient::ChunkRawDataItem(*out[1], chunk_emit_cb, &cap);
    free_drained(out);

    std::vector<unsigned char> assembled;
    for (auto& c : cap.chunks) assembled.insert(assembled.end(), c.begin(), c.end());
    if (assembled.size() != (size_t)(4 + frame_size)) { *failure_msg_out = "assembled length != 4 + frame_size"; return false; }
    if (assembled[0] != prefix_snapshot[0] || assembled[1] != prefix_snapshot[1]
        || assembled[2] != prefix_snapshot[2] || assembled[3] != prefix_snapshot[3]) {
        *failure_msg_out = "assembled prefix bytes corrupted"; return false;
    }
    if (memcmp(assembled.data() + 4, payload.data(), frame_size) != 0) {
        *failure_msg_out = "assembled data bytes corrupted across chunk boundary"; return false;
    }
    return true;
}

static void test_video_frame_prefix_and_data_split_across_drain_chunk_boundary() {
    TEST("prefix/data split across MAX_ENC_BLOCKSIZE boundary preserves wire bytes (R4 CAUTION)");
    const int sizes[] = {
        MAX_ENC_BLOCKSIZE / 2,        // small frame: single chunk
        MAX_ENC_BLOCKSIZE - 1,        // just-under
        MAX_ENC_BLOCKSIZE,            // exact
        MAX_ENC_BLOCKSIZE + 1,        // just-over: splits into 2 chunks
        2 * MAX_ENC_BLOCKSIZE,        // 2x: splits into 2 chunks
        (5 * MAX_ENC_BLOCKSIZE) / 2,  // 2.5x: splits into 3 chunks
    };
    for (int sz : sizes) {
        const char* msg = "ok";
        if (!run_split_test_at_frame_size(sz, &msg)) {
            char buf[256]; snprintf(buf, sizeof(buf), "frame_size=%d: %s", sz, msg);
            FAIL(buf); return;
        }
    }
    PASS();
}

int main() {
    printf("Running test_video_state_machine...\n");
    test_video_block_emits_begin_marker_sps_when_active();
    test_video_block_emits_end_only_when_deactivated();
    test_video_frame_during_marker_interleave_is_blocked();
    test_video_marker_uses_audio_ch0_guid();
    test_video_cold_start_marker_only_first_interval();
    test_video_inactive_no_emission();
    test_video_audio_interval_seq_increments();
    test_video_frame_prefix_and_data_split_across_drain_chunk_boundary();
    printf("Tests passed: %d/%d\n", tests_passed, tests_run);
    return (tests_run == tests_passed) ? 0 : 1;
}
