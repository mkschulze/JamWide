/*
    test_video_fourcc.cpp - Phase 14.3-03 RawData receive-path dispatch unit tests.

    Covers:
    - H264 BEGIN with RawData_Callback registered fires eventType=0
    - H264 BEGIN with NO callback writes log warning + creates NO tracker
    - H264 WRITE with same GUID fires eventType=1 (data)
    - H264 WRITE with diw.flags & 1 fires eventType=2 (end) + deletes tracker
    - OGGv BEGIN routes through the existing else-if(dib.fourcc) branch
      (m_downloads.GetSize() == 1; vector empty — RawData_Callback NOT invoked)
    - FLAC BEGIN routes through the existing branch (same invariant — verifies
      Landmine L4's second exclusion (NJ_ENCODER_FMT_FLAC) actually works)
    - VP8 (trailing space) BEGIN dispatches when callback registered
    - MJPG BEGIN dispatches when callback registered

    Linked against the njclient static library; the test-only helpers
    NJClient::DispatchTestServerDownloadIntervalBegin /
    NJClient::DispatchTestServerDownloadIntervalWrite /
    NJClient::AddTestRemoteUser /
    NJClient::GetRawDataDownloadCount /
    NJClient::GetMDownloadsCount are gated under JAMWIDE_BUILD_TESTS.

    Scaffold lifted verbatim from tests/test_encryption.cpp:26-44 (TEST/PASS/FAIL
    macros + tests_run/tests_passed counters), matching tests/test_rawdata_send.cpp
    sibling test from Phase 14.3-02.

    Wave 0 status (Task 1): all tests stubbed to FAIL("RED — not yet implemented");
    ctest exits non-zero. Task 3 fills in the bodies + wires JAMWIDE_BUILD_TESTS-
    gated helpers in NJClient to drive the GREEN flip.
*/

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "core/njclient.h"
#include "core/mpb.h"

// MAKE_NJ_FOURCC is file-local to njclient.cpp (line 212). Re-defined here
// using the SAME convention. Landmine L1: byte-order discipline must match.
#ifndef MAKE_NJ_FOURCC
#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))
#endif

// NJ_ENCODER_FMT_TYPE / NJ_ENCODER_FMT_FLAC are file-local to njclient.cpp
// (lines 154-155). Re-defined here using the SAME convention so the test can
// exercise the existing-branch routing invariant explicitly (SC-7 indirect).
#ifndef NJ_ENCODER_FMT_TYPE
#define NJ_ENCODER_FMT_TYPE MAKE_NJ_FOURCC('O','G','G','v')
#endif
#ifndef NJ_ENCODER_FMT_FLAC
#define NJ_ENCODER_FMT_FLAC MAKE_NJ_FOURCC('F','L','A','C')
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

// ============================================================
// Test bodies (Task 1: stubbed RED; Task 3 fills these in GREEN).
// ============================================================

static void test_h264_begin_with_callback_fires_event_0() {
    TEST("H264 BEGIN with RawData_Callback registered fires eventType=0");
    FAIL("RED — not yet implemented");
}

static void test_h264_begin_no_callback_logs_and_discards() {
    TEST("H264 BEGIN with NO callback logs+discards (no tracker, no m_downloads)");
    FAIL("RED — not yet implemented");
}

static void test_h264_write_fires_event_1() {
    TEST("H264 WRITE with same GUID fires eventType=1 (data)");
    FAIL("RED — not yet implemented");
}

static void test_h264_write_with_end_flag_fires_event_2_and_deletes_tracker() {
    TEST("H264 WRITE with flags&1 fires eventType=2 (end) + deletes tracker");
    FAIL("RED — not yet implemented");
}

static void test_oggv_begin_routes_to_existing_m_downloads() {
    TEST("OGGv BEGIN routes through existing else-if(dib.fourcc) — m_downloads grows");
    FAIL("RED — not yet implemented");
}

static void test_flac_begin_routes_to_existing_m_downloads() {
    TEST("FLAC BEGIN routes through existing else-if(dib.fourcc) — m_downloads grows");
    FAIL("RED — not yet implemented");
}

static void test_vp8_begin_dispatches_when_callback_registered() {
    TEST("VP8 (trailing space) BEGIN dispatches eventType=0 when callback registered");
    FAIL("RED — not yet implemented");
}

static void test_mjpg_begin_dispatches_when_callback_registered() {
    TEST("MJPG BEGIN dispatches eventType=0 when callback registered");
    FAIL("RED — not yet implemented");
}

int main()
{
    printf("=== test_video_fourcc (Phase 14.3-03) ===\n");

    test_h264_begin_with_callback_fires_event_0();
    test_h264_begin_no_callback_logs_and_discards();
    test_h264_write_fires_event_1();
    test_h264_write_with_end_flag_fires_event_2_and_deletes_tracker();
    test_oggv_begin_routes_to_existing_m_downloads();
    test_flac_begin_routes_to_existing_m_downloads();
    test_vp8_begin_dispatches_when_callback_registered();
    test_mjpg_begin_dispatches_when_callback_registered();

    printf("\nTotal: %d passed, %d failed\n", tests_passed, tests_run - tests_passed);
    return (tests_passed == tests_run) ? 0 : 1;
}
