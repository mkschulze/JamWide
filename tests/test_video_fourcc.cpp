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
    NJClient::ClearTestRemoteUsers /
    NJClient::GetRawDataDownloadCount /
    NJClient::GetMDownloadsCount are gated under JAMWIDE_BUILD_TESTS.

    Scaffold lifted verbatim from tests/test_encryption.cpp:26-44 (TEST/PASS/FAIL
    macros + tests_run/tests_passed counters), matching tests/test_rawdata_send.cpp
    sibling test from Phase 14.3-02.
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
// Capture context for the RawData_Callback (3-event dispatch).
//
// Each callback invocation records (eventType, fourcc, guid, dataLen,
// chidx, username copy, payload first/last byte) into a vector for
// later assertion.
// ============================================================

struct CallbackEvent {
    int eventType;
    unsigned int fourcc;
    unsigned char guid[16];
    int chidx;
    char username[256];
    int dataLen;
    unsigned char first_byte;
    unsigned char last_byte;
};

struct CallbackCapture {
    std::vector<CallbackEvent> events;
};

static void capture_callback(void* userData, int eventType,
                             const unsigned char* guid, unsigned int fourcc,
                             const char* username, int chidx,
                             const void* data, int dataLen)
{
    auto* cap = static_cast<CallbackCapture*>(userData);
    CallbackEvent ev{};
    ev.eventType = eventType;
    ev.fourcc = fourcc;
    if (guid) memcpy(ev.guid, guid, 16);
    ev.chidx = chidx;
    if (username) {
        std::strncpy(ev.username, username, sizeof(ev.username) - 1);
        ev.username[sizeof(ev.username) - 1] = 0;
    }
    ev.dataLen = dataLen;
    if (data && dataLen > 0) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        ev.first_byte = p[0];
        ev.last_byte = p[dataLen - 1];
    }
    cap->events.push_back(ev);
}

// ============================================================
// Test scaffold helpers.
//
// NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
// macOS thread stack cannot hold even a single instance. All tests heap-
// allocate via std::unique_ptr<NJClient>. Documented inline in
// tests/test_rawdata_send.cpp from Phase 14.3-02 — same constraint here.
//
// Each test instantiates a stack-local CallbackCapture, registers it via
// NJClient::RawData_Callback + RawData_User, calls AddTestRemoteUser to
// populate the user lookup, then exercises the dispatch helpers.
// ============================================================

static std::unique_ptr<NJClient> make_test_client(CallbackCapture* cap, bool register_callback)
{
    auto client = std::unique_ptr<NJClient>(new NJClient);
    if (register_callback) {
        client->RawData_Callback = capture_callback;
        client->RawData_User = cap;
    } else {
        client->RawData_Callback = nullptr;
        client->RawData_User = nullptr;
    }
    client->AddTestRemoteUser("testuser", 0, /*chflags=*/0);
    return client;
}

// Distinctive 16-byte guid for the H264/VP8/MJPG tests. Bytes are non-zero
// (so we don't accidentally hit the silence-marker branch — which checks
// `!memcmp(dib.guid, zero_guid, 16)`).
static const unsigned char kTestGuid[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

// A second distinctive guid for the OGGv/FLAC test (forced to not collide
// with kTestGuid).
static const unsigned char kAudioGuid[16] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
    0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xA0
};

// ============================================================
// Test bodies (Task 3 GREEN).
// ============================================================

static void test_h264_begin_with_callback_fires_event_0() {
    TEST("H264 BEGIN with RawData_Callback registered fires eventType=0");

    CallbackCapture cap;
    auto client = make_test_client(&cap, /*register_callback=*/true);

    client->DispatchTestServerDownloadIntervalBegin(
        kTestGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, "testuser", 0);

    if (cap.events.size() != 1) {
        FAIL("expected exactly 1 callback invocation on H264 BEGIN");
        return;
    }
    if (cap.events[0].eventType != 0) {
        FAIL("expected eventType=0 (begin) for H264 BEGIN");
        return;
    }
    if (cap.events[0].fourcc != MAKE_NJ_FOURCC('H','2','6','4')) {
        FAIL("captured fourcc mismatch H264");
        return;
    }
    if (memcmp(cap.events[0].guid, kTestGuid, 16) != 0) {
        FAIL("captured guid mismatch with kTestGuid");
        return;
    }
    if (cap.events[0].chidx != 0) {
        FAIL("captured chidx mismatch");
        return;
    }
    if (strcmp(cap.events[0].username, "testuser") != 0) {
        FAIL("captured username mismatch");
        return;
    }
    if (client->GetRawDataDownloadCount() != 1) {
        FAIL("m_rawdata_downloads should have grown to 1");
        return;
    }
    if (client->GetMDownloadsCount() != 0) {
        FAIL("m_downloads should remain empty (H264 dispatched to RawData, not start_decode)");
        return;
    }
    PASS();
}

static void test_h264_begin_no_callback_logs_and_discards() {
    TEST("H264 BEGIN with NO callback logs+discards (no tracker, no m_downloads)");

    CallbackCapture cap;
    auto client = make_test_client(&cap, /*register_callback=*/false);

    client->DispatchTestServerDownloadIntervalBegin(
        kTestGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, "testuser", 0);

    if (!cap.events.empty()) {
        FAIL("RawData_Callback was nullptr; should not have fired (vector should be empty)");
        return;
    }
    if (client->GetRawDataDownloadCount() != 0) {
        FAIL("m_rawdata_downloads should be empty (log+discard branch — no tracker created)");
        return;
    }
    if (client->GetMDownloadsCount() != 0) {
        FAIL("m_downloads should be empty (H264 must NOT route to Vorbis — SC-6)");
        return;
    }
    PASS();
}

static void test_h264_write_fires_event_1() {
    TEST("H264 WRITE with same GUID fires eventType=1 (data)");

    CallbackCapture cap;
    auto client = make_test_client(&cap, /*register_callback=*/true);

    // BEGIN first to create the tracker.
    client->DispatchTestServerDownloadIntervalBegin(
        kTestGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, "testuser", 0);
    // WRITE with the same guid, payload "FRAMEDATA" (9 bytes), flags=0.
    const char* payload = "FRAMEDATA";
    client->DispatchTestServerDownloadIntervalWrite(
        kTestGuid, payload, 9, /*flags=*/0);

    if (cap.events.size() != 2) {
        FAIL("expected 2 callback invocations after BEGIN + WRITE");
        return;
    }
    if (cap.events[0].eventType != 0) {
        FAIL("first event must be eventType=0 (begin)");
        return;
    }
    if (cap.events[1].eventType != 1) {
        FAIL("second event must be eventType=1 (data) for WRITE");
        return;
    }
    if (cap.events[1].dataLen != 9) {
        FAIL("data event dataLen != 9");
        return;
    }
    if (cap.events[1].first_byte != (unsigned char)'F'
        || cap.events[1].last_byte != (unsigned char)'A') {
        FAIL("data payload bytes do not match 'FRAMEDATA' first/last");
        return;
    }
    if (client->GetRawDataDownloadCount() != 1) {
        FAIL("tracker should still exist after non-end WRITE");
        return;
    }
    PASS();
}

static void test_h264_write_with_end_flag_fires_event_2_and_deletes_tracker() {
    TEST("H264 WRITE with flags&1 fires eventType=2 (end) + deletes tracker");

    CallbackCapture cap;
    auto client = make_test_client(&cap, /*register_callback=*/true);

    // BEGIN + non-end WRITE + end WRITE.
    client->DispatchTestServerDownloadIntervalBegin(
        kTestGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, "testuser", 0);
    client->DispatchTestServerDownloadIntervalWrite(
        kTestGuid, "MIDDLE", 6, /*flags=*/0);
    client->DispatchTestServerDownloadIntervalWrite(
        kTestGuid, nullptr, 0, /*flags=*/1);  // end marker, no payload

    if (cap.events.size() != 3) {
        FAIL("expected 3 callback invocations after BEGIN + WRITE + WRITE(end)");
        return;
    }
    if (cap.events[0].eventType != 0
        || cap.events[1].eventType != 1
        || cap.events[2].eventType != 2) {
        FAIL("event sequence mismatch — expected [0, 1, 2] (begin, data, end)");
        return;
    }
    if (cap.events[2].dataLen != 0) {
        FAIL("eventType=2 (end) should carry zero-length payload");
        return;
    }
    if (client->GetRawDataDownloadCount() != 0) {
        FAIL("tracker should have been deleted on flags&1 (end)");
        return;
    }
    if (client->GetMDownloadsCount() != 0) {
        FAIL("m_downloads should still be empty (H264 path)");
        return;
    }
    PASS();
}

static void test_oggv_begin_routes_to_existing_m_downloads() {
    TEST("OGGv BEGIN routes through existing else-if(dib.fourcc) — m_downloads grows");

    CallbackCapture cap;
    // RawData_Callback IS registered — the existing branch must still be
    // selected for OGGv (Landmine L4 exclusion). If the exclusion fails,
    // OGGv would route to RawData_Callback, breaking audio.
    auto client = make_test_client(&cap, /*register_callback=*/true);

    client->DispatchTestServerDownloadIntervalBegin(
        kAudioGuid, NJ_ENCODER_FMT_TYPE, 0, "testuser", 0);

    if (!cap.events.empty()) {
        FAIL("RawData_Callback fired for OGGv — Landmine L4 exclusion FAILED "
             "(would break audio playback for Vorbis streams)");
        return;
    }
    if (client->GetMDownloadsCount() != 1) {
        FAIL("OGGv should have created a RemoteDownload in m_downloads (SC-7 indirect)");
        return;
    }
    if (client->GetRawDataDownloadCount() != 0) {
        FAIL("OGGv must NOT enter the m_rawdata_downloads path");
        return;
    }
    PASS();
}

static void test_flac_begin_routes_to_existing_m_downloads() {
    TEST("FLAC BEGIN routes through existing else-if(dib.fourcc) — m_downloads grows");

    CallbackCapture cap;
    auto client = make_test_client(&cap, /*register_callback=*/true);

    client->DispatchTestServerDownloadIntervalBegin(
        kAudioGuid, NJ_ENCODER_FMT_FLAC, 0, "testuser", 0);

    if (!cap.events.empty()) {
        FAIL("RawData_Callback fired for FLAC — Landmine L4 second exclusion FAILED "
             "(JamWide-specific: FLAC must also be excluded since JamWide ships FLAC)");
        return;
    }
    if (client->GetMDownloadsCount() != 1) {
        FAIL("FLAC should have created a RemoteDownload in m_downloads (SC-7 indirect)");
        return;
    }
    if (client->GetRawDataDownloadCount() != 0) {
        FAIL("FLAC must NOT enter the m_rawdata_downloads path");
        return;
    }
    PASS();
}

static void test_vp8_begin_dispatches_when_callback_registered() {
    TEST("VP8 (trailing space) BEGIN dispatches eventType=0 when callback registered");

    CallbackCapture cap;
    auto client = make_test_client(&cap, /*register_callback=*/true);

    const unsigned int vp8 = MAKE_NJ_FOURCC('V','P','8',' ');
    client->DispatchTestServerDownloadIntervalBegin(
        kTestGuid, vp8, 0, "testuser", 0);

    if (cap.events.size() != 1) {
        FAIL("expected exactly 1 callback invocation on VP8 BEGIN");
        return;
    }
    if (cap.events[0].eventType != 0) {
        FAIL("expected eventType=0 (begin)");
        return;
    }
    if (cap.events[0].fourcc != vp8) {
        FAIL("captured fourcc mismatch VP8");
        return;
    }
    if (client->GetRawDataDownloadCount() != 1) {
        FAIL("m_rawdata_downloads should have grown to 1");
        return;
    }
    if (client->GetMDownloadsCount() != 0) {
        FAIL("m_downloads should be empty (VP8 dispatched to RawData)");
        return;
    }
    PASS();
}

static void test_mjpg_begin_dispatches_when_callback_registered() {
    TEST("MJPG BEGIN dispatches eventType=0 when callback registered");

    CallbackCapture cap;
    auto client = make_test_client(&cap, /*register_callback=*/true);

    const unsigned int mjpg = MAKE_NJ_FOURCC('M','J','P','G');
    client->DispatchTestServerDownloadIntervalBegin(
        kTestGuid, mjpg, 0, "testuser", 0);

    if (cap.events.size() != 1) {
        FAIL("expected exactly 1 callback invocation on MJPG BEGIN");
        return;
    }
    if (cap.events[0].eventType != 0) {
        FAIL("expected eventType=0 (begin)");
        return;
    }
    if (cap.events[0].fourcc != mjpg) {
        FAIL("captured fourcc mismatch MJPG");
        return;
    }
    if (client->GetRawDataDownloadCount() != 1) {
        FAIL("m_rawdata_downloads should have grown to 1");
        return;
    }
    if (client->GetMDownloadsCount() != 0) {
        FAIL("m_downloads should be empty (MJPG dispatched to RawData)");
        return;
    }
    PASS();
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
