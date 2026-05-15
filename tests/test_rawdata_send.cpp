/*
    test_rawdata_send.cpp - Phase 14.3-02 RawData send-side unit tests.

    Covers:
    - RawDataSendBegin -> Write(N) -> Write(isEnd=true) round-trip populates SPSC
    - 2 * MAX_ENC_BLOCKSIZE payload splits at MAX_ENC_BLOCKSIZE in drain chunker
    - is_video_fourcc test vectors (H264, VP8 trailing-space, MJPG, OGGv, FLAC)
    - test_disconnect_drain_and_discard exercises Pattern C discard-on-null-netcon
    - test_send_queue_overflow_counter_increments exercises Codex M-8 counter

    Linked against the njclient static library; the test-only helpers
    NJClient::IsVideoFourcc / NJClient::DrainRawDataSendQueueForTest /
    NJClient::ChunkRawDataItem are gated under JAMWIDE_BUILD_TESTS, defined
    via target_compile_definitions on the njclient target at CMakeLists.txt:137
    and re-asserted on the test target.

    Scaffold lifted verbatim from tests/test_encryption.cpp:26-44 (TEST/PASS/FAIL
    macros + tests_run/tests_passed counters).
*/

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "core/njclient.h"
#include "threading/spsc_payloads.h"
#include "wdl/heapbuf.h"

// MAX_ENC_BLOCKSIZE is file-local to njclient.cpp; replicate the constant here.
// Defined at src/core/njclient.cpp:831 as (8192+1024) = 9216. Any divergence
// between this value and the cpp's #define would silently break the chunking
// assertion; sanity-checked manually 2026-05-15.
static constexpr int MAX_ENC_BLOCKSIZE = 8192 + 1024;

// MAKE_NJ_FOURCC is also file-local to njclient.cpp (line 212). Re-defined
// here using the SAME convention. NOT exported via the public header
// (Landmine L1: byte-order discipline must match).
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

// Local helper to clean up payloads in the destructive-drain output vector.
// DrainRawDataSendQueueForTest hands ownership of the WDL_HeapBuf payloads
// back to the caller — the tests must free them to prevent leaks.
static void free_payloads(std::vector<jamwide::RawDataItem>& items) {
    for (auto& item : items) {
        delete item.payload;
        item.payload = nullptr;
    }
}

// ============================================================
// Test bodies (Task 3 GREEN).
// ============================================================

static void test_rawdata_begin_write_end_roundtrip() {
    TEST("RawDataSendBegin + Write(N) + Write(isEnd=true) populates SPSC");

    // NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
    // macOS thread stack cannot hold even a single instance. Heap-allocate.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;
    unsigned char outGuid[16] = {0};

    client.RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'), 3, 0);
    client.RawDataSendWrite(outGuid, "AAAA", 4, false);
    client.RawDataSendWrite(outGuid, "BB",   2, true);

    if (client.GetRawDataSendQueueOverflowCount() != 0) {
        FAIL("overflow counter bumped during successful round-trip");
        return;
    }

    std::vector<jamwide::RawDataItem> out;
    client.DrainRawDataSendQueueForTest(out);

    if (out.size() != 3) {
        FAIL("queue did not contain 3 items after Begin + 2 Writes");
        free_payloads(out);
        return;
    }

    // Item 0: BEGIN (type=0, fourcc=H264, chidx=3)
    if (out[0].type != 0 || out[0].fourcc != MAKE_NJ_FOURCC('H','2','6','4')
        || out[0].chidx != 3 || out[0].payload != nullptr) {
        FAIL("item 0 is not a properly-populated BEGIN");
        free_payloads(out);
        return;
    }

    // GUID is generated inside RawDataSendBegin and copied to outGuid;
    // the queued item's guid must match.
    if (memcmp(out[0].guid, outGuid, 16) != 0) {
        FAIL("BEGIN item guid mismatch with outGuid produced by SendBegin");
        free_payloads(out);
        return;
    }

    // Item 1: WRITE (type=1, flags=0, payload=4 bytes "AAAA")
    if (out[1].type != 1 || out[1].flags != 0
        || !out[1].payload || out[1].payload->GetSize() != 4
        || memcmp(out[1].payload->Get(), "AAAA", 4) != 0) {
        FAIL("item 1 is not a 4-byte non-end WRITE 'AAAA'");
        free_payloads(out);
        return;
    }
    // Same GUID as the BEGIN.
    if (memcmp(out[1].guid, outGuid, 16) != 0) {
        FAIL("WRITE item 1 guid mismatch with BEGIN");
        free_payloads(out);
        return;
    }

    // Item 2: WRITE (type=1, flags=1, payload=2 bytes "BB", isEnd)
    if (out[2].type != 1 || out[2].flags != 1
        || !out[2].payload || out[2].payload->GetSize() != 2
        || memcmp(out[2].payload->Get(), "BB", 2) != 0) {
        FAIL("item 2 is not a 2-byte end WRITE 'BB' with flags=1");
        free_payloads(out);
        return;
    }
    if (memcmp(out[2].guid, outGuid, 16) != 0) {
        FAIL("WRITE item 2 guid mismatch with BEGIN");
        free_payloads(out);
        return;
    }

    free_payloads(out);
    PASS();
}

// Capture context for the static ChunkRawDataItem helper. Each emit call
// pushes the chunk metadata into the vector for later assertion.
struct ChunkCapture {
    struct Chunk {
        int len;
        int flags;
        // copy of the first byte and last byte for byte-identity sanity check
        unsigned char first_byte;
        unsigned char last_byte;
    };
    std::vector<Chunk> chunks;
};

static void chunk_emit_cb(void* ctx, const unsigned char /*guid*/[16],
                          const void* data, int dataLen, int flags)
{
    auto* cap = static_cast<ChunkCapture*>(ctx);
    ChunkCapture::Chunk ch{};
    ch.len = dataLen;
    ch.flags = flags;
    if (data && dataLen > 0) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        ch.first_byte = p[0];
        ch.last_byte  = p[dataLen - 1];
    }
    cap->chunks.push_back(ch);
}

static void test_payload_chunking_at_max_enc_blocksize() {
    TEST("2*MAX_ENC_BLOCKSIZE payload splits into 3 chunks at drain");

    // Construct a 2 * MAX_ENC_BLOCKSIZE = 18432-byte payload with a
    // distinguishable byte pattern at each chunk boundary so the test can
    // verify the split landed at MAX_ENC_BLOCKSIZE exactly.
    const int total_len = 2 * MAX_ENC_BLOCKSIZE;
    std::vector<unsigned char> payload(total_len);
    for (int i = 0; i < total_len; ++i) {
        // marker bytes: index modulo 251 (prime) so the sequence is
        // deterministic and byte 0 / byte MAX_ENC_BLOCKSIZE / byte
        // total_len-1 are all distinct.
        payload[i] = (unsigned char)((i * 7 + 13) % 251);
    }

    // Wrap into a RawDataItem of type=1 with flags=1 (isEnd). The chunker
    // should split into:
    //   chunk 0: bytes [0, MAX_ENC_BLOCKSIZE),                flags=0
    //   chunk 1: bytes [MAX_ENC_BLOCKSIZE, 2*MAX_ENC_BLOCKSIZE), flags=0 ...
    //   wait — total is exactly 2*MAX_ENC_BLOCKSIZE which means chunk 0 +
    //   chunk 1 covers it. So only 2 chunks. Let me re-read the plan:
    //   "2*MAX_ENC_BLOCKSIZE payload splits into 3 chunks (2 mid + 1 end)."
    //   For 3 chunks we need a payload size that doesn't divide evenly into
    //   MAX_ENC_BLOCKSIZE — let's use 2*MAX_ENC_BLOCKSIZE + 1 to force the
    //   third chunk of 1 byte.
    //
    //   Actually, re-reading the plan: "2*MAX_ENC_BLOCKSIZE payload splits
    //   into 3 chunks". With current chunker (remaining > MAX_ENC_BLOCKSIZE
    //   takes MAX_ENC_BLOCKSIZE, else takes remaining), 2*MAX_ENC_BLOCKSIZE
    //   produces exactly 2 chunks of MAX_ENC_BLOCKSIZE each.
    //
    //   The plan example is ambiguous. The test below adapts to the actual
    //   chunker semantics: for a 2*MAX_ENC_BLOCKSIZE payload with isEnd=true,
    //   the chunker emits 2 chunks (chunk 0 flags=0, chunk 1 flags=1=end).
    //   For a 2*MAX_ENC_BLOCKSIZE + 1 payload, 3 chunks (chunks 0,1 flags=0,
    //   chunk 2 of size 1 flags=1=end).
    //
    //   This test uses 2*MAX_ENC_BLOCKSIZE + 1 to validate the 3-chunk
    //   split as the plan named.

    const int extended_len = 2 * MAX_ENC_BLOCKSIZE + 1;
    payload.resize(extended_len);
    payload[extended_len - 1] = 0xFE;  // tail marker

    // Build a RawDataItem manually with a WDL_HeapBuf payload (owns it for
    // the duration of the test; we delete it at the end).
    jamwide::RawDataItem item{};
    item.type = 1;
    item.flags = 1;  // isEnd
    WDL_HeapBuf buf;
    buf.Resize(extended_len);
    memcpy(buf.Get(), payload.data(), extended_len);
    item.payload = &buf;

    ChunkCapture cap;
    NJClient::ChunkRawDataItem(item, chunk_emit_cb, &cap);

    item.payload = nullptr;  // we own buf on the stack; do not let test mistakenly delete it later.

    if (cap.chunks.size() != 3) {
        FAIL("expected 3 chunks for 2*MAX_ENC_BLOCKSIZE+1 payload");
        return;
    }

    // chunk 0: MAX_ENC_BLOCKSIZE bytes, flags=0
    if (cap.chunks[0].len != MAX_ENC_BLOCKSIZE || cap.chunks[0].flags != 0) {
        FAIL("chunk 0 size/flags incorrect");
        return;
    }
    // chunk 1: MAX_ENC_BLOCKSIZE bytes, flags=0
    if (cap.chunks[1].len != MAX_ENC_BLOCKSIZE || cap.chunks[1].flags != 0) {
        FAIL("chunk 1 size/flags incorrect");
        return;
    }
    // chunk 2: 1 byte (the final tail marker), flags=1
    if (cap.chunks[2].len != 1 || cap.chunks[2].flags != 1) {
        FAIL("chunk 2 (final, 1 byte) size/flags incorrect");
        return;
    }
    if (cap.chunks[2].first_byte != 0xFE || cap.chunks[2].last_byte != 0xFE) {
        FAIL("chunk 2 byte identity check failed");
        return;
    }
    // chunk 0 first byte must match payload[0]; chunk 1 first byte must
    // match payload[MAX_ENC_BLOCKSIZE] — confirms the split point.
    if (cap.chunks[0].first_byte != payload[0]) {
        FAIL("chunk 0 first byte mismatch");
        return;
    }
    if (cap.chunks[1].first_byte != payload[MAX_ENC_BLOCKSIZE]) {
        FAIL("chunk 1 first byte does not match payload[MAX_ENC_BLOCKSIZE]");
        return;
    }

    PASS();
}

static void test_is_video_fourcc_h264() {
    TEST("is_video_fourcc(H264) returns true");

    // NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
    // macOS thread stack cannot hold even a single instance. Heap-allocate.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;
    if (client.IsVideoFourcc(MAKE_NJ_FOURCC('H','2','6','4'))) {
        PASS();
    } else {
        FAIL("expected H264 fourcc to be recognized as video");
    }
}

static void test_is_video_fourcc_vp8_trailing_space() {
    TEST("is_video_fourcc(VP8' ') true; (VP8\\0) false (trailing-space tightening)");

    // NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
    // macOS thread stack cannot hold even a single instance. Heap-allocate.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;
    const bool vp8_space = client.IsVideoFourcc(MAKE_NJ_FOURCC('V','P','8',' '));
    const bool vp8_null  = client.IsVideoFourcc(MAKE_NJ_FOURCC('V','P','8',0));

    if (vp8_space && !vp8_null) {
        PASS();
    } else if (!vp8_space) {
        FAIL("VP8 trailing-space variant should be recognized as video");
        return;
    } else {
        FAIL("VP8 with NUL trailing byte should NOT be video (tightened from NinjamZap)");
        return;
    }
}

static void test_is_video_fourcc_mjpg() {
    TEST("is_video_fourcc(MJPG) returns true");

    // NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
    // macOS thread stack cannot hold even a single instance. Heap-allocate.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;
    if (client.IsVideoFourcc(MAKE_NJ_FOURCC('M','J','P','G'))) {
        PASS();
    } else {
        FAIL("expected MJPG fourcc to be recognized as video");
    }
}

static void test_is_video_fourcc_excludes_oggv_flac() {
    TEST("is_video_fourcc(OGGv|FLAC) returns false (Vorbis/FLAC excluded)");

    // NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
    // macOS thread stack cannot hold even a single instance. Heap-allocate.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;
    const bool oggv = client.IsVideoFourcc(MAKE_NJ_FOURCC('O','G','G','v'));
    const bool flac = client.IsVideoFourcc(MAKE_NJ_FOURCC('F','L','A','C'));
    // Spot-check a few obvious non-video fourCCs too.
    const bool zero = client.IsVideoFourcc(0);
    const bool junk = client.IsVideoFourcc(MAKE_NJ_FOURCC('X','Y','Z','W'));

    if (!oggv && !flac && !zero && !junk) {
        PASS();
    } else {
        FAIL("non-video fourCC was wrongly identified as video");
    }
}

static void test_disconnect_drain_and_discard() {
    TEST("null m_netcon drain-and-discards items + bumps discard counter (not overflow)");

    // NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
    // macOS thread stack cannot hold even a single instance. Heap-allocate.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;
    // client constructed but Connect() never called -> m_netcon == nullptr.
    // Producer pushes three items; the run-thread drain inside NJClient::Run
    // must discard them (Pattern C) and bump the DISCARD counter per item.
    // The overflow counter must NOT be touched — overflow == queue-full only.

    unsigned char outGuid[16] = {0};
    client.RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, 0);
    client.RawDataSendWrite(outGuid, "DATA", 4, false);
    client.RawDataSendWrite(outGuid, NULL,   0, true);

    const uint64_t pre_overflow = client.GetRawDataSendQueueOverflowCount();
    const uint64_t pre_discard  = client.GetRawDataSendQueueDiscardCount();
    if (pre_overflow != 0) {
        FAIL("overflow counter non-zero after 3 successful pushes (queue is "
             "capacity 64 — should fit)");
        return;
    }

    // Manually invoke NJClient::Run; it drains the SPSC. With m_netcon==null
    // the Pattern C branch fires, discarding each item + bumping the discard
    // counter. Run() returns nonzero ("sleep ok"); we only care about the
    // drain side-effect.
    (void)client.Run();

    const uint64_t post_overflow = client.GetRawDataSendQueueOverflowCount();
    const uint64_t post_discard  = client.GetRawDataSendQueueDiscardCount();
    if (post_overflow != pre_overflow) {
        printf("(pre_overflow=%llu post_overflow=%llu) ",
               (unsigned long long)pre_overflow, (unsigned long long)post_overflow);
        FAIL("overflow counter changed during Pattern C discard — must not be touched");
        return;
    }
    if (post_discard != pre_discard + 3) {
        printf("(pre=%llu post=%llu) ",
               (unsigned long long)pre_discard, (unsigned long long)post_discard);
        FAIL("discard counter did not bump by exactly 3 for 3 discarded items");
        return;
    }

    // Re-drain via the test helper: queue must now be empty.
    std::vector<jamwide::RawDataItem> out;
    client.DrainRawDataSendQueueForTest(out);
    if (!out.empty()) {
        FAIL("queue still contains items after Run() drain — discard did not consume");
        free_payloads(out);
        return;
    }

    PASS();
}

static void test_send_queue_overflow_counter_increments() {
    TEST("Capacity+1 push bumps overflow counter; first Capacity succeed");

    // NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
    // macOS thread stack cannot hold even a single instance. Heap-allocate.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;
    unsigned char outGuid[16] = {0};

    // The SPSC has capacity RAWDATA_SEND_QUEUE_CAPACITY. However, the SpscRing
    // implementation reserves one slot to distinguish full from empty (see
    // spsc_ring.h:55: "next_head == tail" -> full). So the usable capacity
    // is N - 1 = 63 for N=64. Push capacity-1 items first (must all succeed),
    // then the next push must fail and bump the overflow counter.
    constexpr int CAP = (int)jamwide::RAWDATA_SEND_QUEUE_CAPACITY;
    constexpr int USABLE = CAP - 1;

    for (int i = 0; i < USABLE; ++i) {
        client.RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, 0);
    }
    if (client.GetRawDataSendQueueOverflowCount() != 0) {
        FAIL("overflow counter bumped during the first USABLE pushes");
        return;
    }

    // The Nth push (N = USABLE + 1) MUST fail because the ring is full.
    client.RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, 0);
    if (client.GetRawDataSendQueueOverflowCount() != 1) {
        printf("(USABLE=%d count=%llu) ", USABLE,
               (unsigned long long)client.GetRawDataSendQueueOverflowCount());
        FAIL("overflow counter did not bump to 1 on the first full-ring push");
        return;
    }

    // One more push for good measure — overflow counter must climb to 2.
    client.RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, 0);
    if (client.GetRawDataSendQueueOverflowCount() != 2) {
        FAIL("overflow counter did not climb to 2 on second full-ring push");
        return;
    }

    // Cleanup: drain the queue so the destructor doesn't leak.
    std::vector<jamwide::RawDataItem> out;
    client.DrainRawDataSendQueueForTest(out);
    free_payloads(out);

    PASS();
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
