/*
    test_rawdata_send.cpp - Phase 14.3-02 RawData send-side unit tests.

    Covers:
    - RawDataSendBegin → Write(N) → Write(isEnd=true) round-trip populates SPSC
    - 2 * MAX_ENC_BLOCKSIZE payload splits at MAX_ENC_BLOCKSIZE in drain chunker
    - is_video_fourcc test vectors (H264, VP8 trailing-space, MJPG, OGGv, FLAC)
    - test_disconnect_drain_and_discard exercises Pattern C discard-on-null-netcon
    - test_send_queue_overflow_counter_increments exercises Codex M-8 counter

    Linked against the njclient static library; the test-only helpers
    NJClient::IsVideoFourcc / NJClient::DrainRawDataSendQueueForTest /
    NJClient::ChunkRawDataItem are gated under JAMWIDE_BUILD_TESTS, defined
    via target_compile_definitions on the njclient target at CMakeLists.txt:137
    and re-asserted on the test target below.

    Scaffold lifted verbatim from tests/test_encryption.cpp:26-44 (TEST/PASS/FAIL
    macros + tests_run/tests_passed counters).
*/

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/njclient.h"
#include "threading/spsc_payloads.h"

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
// Task 1 scaffold — bodies filled in Task 3.
// Each test currently FAILs by design (RED) so the test executable
// exits non-zero until Task 3 lands the GREEN bodies.
// ============================================================

static void test_rawdata_begin_write_end_roundtrip() {
    TEST("RawDataSendBegin + Write(N) + Write(isEnd=true) populates SPSC");
    FAIL("RED - not yet implemented");
}

static void test_payload_chunking_at_max_enc_blocksize() {
    TEST("2*MAX_ENC_BLOCKSIZE payload splits into 3 chunks at drain");
    FAIL("RED - not yet implemented");
}

static void test_is_video_fourcc_h264() {
    TEST("is_video_fourcc(H264) returns true");
    FAIL("RED - not yet implemented");
}

static void test_is_video_fourcc_vp8_trailing_space() {
    TEST("is_video_fourcc(VP8' ') true; (VP8\\0) false (trailing-space tightening)");
    FAIL("RED - not yet implemented");
}

static void test_is_video_fourcc_mjpg() {
    TEST("is_video_fourcc(MJPG) returns true");
    FAIL("RED - not yet implemented");
}

static void test_is_video_fourcc_excludes_oggv_flac() {
    TEST("is_video_fourcc(OGGv|FLAC) returns false (Vorbis/FLAC excluded)");
    FAIL("RED - not yet implemented");
}

static void test_disconnect_drain_and_discard() {
    TEST("null m_netcon drain-and-discards items + bumps overflow counter");
    FAIL("RED - not yet implemented");
}

static void test_send_queue_overflow_counter_increments() {
    TEST("Capacity+1 push bumps overflow counter; first Capacity succeed");
    FAIL("RED - not yet implemented");
}

int main()
{
    printf("=== test_rawdata_send (Phase 14.3-02) ===\n");

    test_rawdata_begin_write_end_roundtrip();
    test_payload_chunking_at_max_enc_blocksize();
    test_is_video_fourcc_h264();
    test_is_video_fourcc_vp8_trailing_space();
    test_is_video_fourcc_mjpg();
    test_is_video_fourcc_excludes_oggv_flac();
    test_disconnect_drain_and_discard();
    test_send_queue_overflow_counter_increments();

    printf("\nTotal: %d passed, %d failed\n", tests_passed, tests_run - tests_passed);
    return (tests_passed == tests_run) ? 0 : 1;
}
