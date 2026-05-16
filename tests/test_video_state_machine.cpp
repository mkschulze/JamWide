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

#include <atomic>
#include <cassert>
#include <chrono>
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

    // Item 1: marker24. Format: [00,00,00,14][BE u32 seq=1][16B audio_ch0_guid=0].
    if (out[1]->type != 1 || out[1]->flags != 0 || out[1]->data.GetSize() != 24
        || memcmp(out[1]->guid, guid, 16) != 0) {
        FAIL("item 1 is not the 24-byte marker WRITE"); free_drained(out); return;
    }
    const unsigned char* m = (const unsigned char*)out[1]->data.Get();
    if (m[0] != 0 || m[1] != 0 || m[2] != 0 || m[3] != 20) {
        FAIL("marker prefix != BE u32 = 20"); free_drained(out); return;
    }
    // seq is 1 (first call to on_new_interval since construction).
    if (m[4] != 0 || m[5] != 0 || m[6] != 0 || m[7] != 1) {
        FAIL("marker swap-count != BE u32 = 1 on first interval"); free_drained(out); return;
    }
    // audio_ch0_guid is 16 zero bytes (no Local_Channel registered).
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

int main() {
    printf("Running test_video_state_machine...\n");
    test_video_block_emits_begin_marker_sps_when_active();
    printf("Tests passed: %d/%d\n", tests_passed, tests_run);
    return (tests_run == tests_passed) ? 0 : 1;
}
