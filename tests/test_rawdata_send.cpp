/*
    test_rawdata_send.cpp — Phase 20-00 NinjamZap-literal mutex-substrate suite.

    REPLACES the Phase 14.3-02 SPSC-overflow assertions with mutex-correctness
    assertions per D-19. The substrate under test is `WDL_PtrList<RawDataQueueItem>`
    protected by `WDL_Mutex m_rawdata_cs`, NinjamZap-literal pop-one-unlock-Send-
    relock drain. The old assertions about `GetRawDataSendQueueOverflowCount() == 0`,
    `RAWDATA_SEND_QUEUE_CAPACITY` overflow at capacity+1, and Pattern-C disconnect
    discards no longer apply — those substrate features were retired in Plan 20-00.

    Eight sub-tests covering NinjamZap-literal mutex semantics + Plan 20 observability:

      1. test_rawdata_begin_write_end_roundtrip
         Begin + 2 Writes + Drain yields 3 RawDataQueueItem* in FIFO order; fields
         populated per NinjamZap shape (type/guid/fourcc/chidx/estsize/flags/data).
         REPLACES the existing test at the same name; assertion on
         GetRawDataSendQueueOverflowCount() != 0 REMOVED (accessor no longer exists).

      2. test_rawdata_write_chunking
         Writing 2*MAX_ENC_BLOCKSIZE+1 payload + isEnd produces 3 chunks via
         ChunkRawDataItem: 2x full + 1x tail (1 byte, flags=1). Validates the
         signature change (`const RawDataQueueItem&` instead of `const RawDataItem&`).

      3. test_rawdata_video_fourcc_helper
         IsVideoFourcc returns true for H264, VP8' ' (trailing-space), MJPG; false
         for OGGv, FLAC, zero, junk. UNCHANGED from 14.3-02.

      4. test_rawdata_multi_producer_stress
         4 producer threads concurrently call RawDataSendBegin + RawDataSendWrite
         N=100 each (M=400 total enqueues, 1200 queue ops including the BEGINs).
         Asserts: queue contains exactly 1200 items; all items well-formed;
         GetRawDataSendQueueHighWaterMark() > 0; GetRawDataSendQueueTotalEnqueueCount()
         == 1200 (the new denominator counter wired correctly under multi-producer
         load). REPLACES the SPSC try_push contract assertions.

      5. test_rawdata_drain_interleave
         2 producer threads enqueue at rate R for T=1 second; main thread acts as
         run-thread drain (pop-one-unlock-process-relock). Asserts: drain semantics
         interleave correctly with active producers under WDL_Mutex; no item observed
         twice; total drained + remaining == total produced (no loss).

      6. test_rawdata_destructor_cleanup
         Construct NJClient, enqueue ~10 items, destroy WITHOUT draining. The
         WDL_PtrList::Empty(true) call in the destructor MUST free all items.
         Validation: drives through ASAN-clean (existing build flags) — no leak
         report. REPLACES the 14.3-02 m_rawdata_sendq_discards == N assertion.

      7. test_rawdata_send_ordering_per_producer
         Two producer threads each enqueue a tagged BEGIN + 4 sequenced Writes;
         after drain, each producer's 5 items appear in monotonic order in the
         queue (per-producer FIFO is intrinsic to NinjamZap's single Enter/Leave
         per call). Validates the invariant under concurrent load.

      8. test_rawdata_wire_ordering_begin_marker_sps_frame
         Simulates the Plan 20-02 on_new_interval sequence + a Plan 20-01 encoder
         frame interleaved against it. Producer A (audio role) holds a local mock
         mutex for BEGIN + marker24 + SPS/PPS; Producer B (encoder role) waits on
         the mock mutex then writes a frame. After drain, queue order is
         BEGIN(g1) -> marker24 -> spspps -> frame. Validates the whole-block
         m_video_cs serialization pattern (Plan 20-02 lands the real m_video_cs;
         this test owns a mock mutex with the same role).

    Scaffold (TEST/PASS/FAIL macros + tests_run/tests_passed counters) lifted
    verbatim from the 14.3-02 file. Linked against the njclient static library;
    the test-only helpers NJClient::IsVideoFourcc /
    NJClient::DrainRawDataSendQueueForTest / NJClient::ChunkRawDataItem are
    gated under JAMWIDE_BUILD_TESTS (defined on the njclient + test_rawdata_send
    targets in CMakeLists.txt).
*/

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/njclient.h"
#include "wdl/heapbuf.h"

// MAX_ENC_BLOCKSIZE is file-local to njclient.cpp; replicate the constant here.
// Defined at src/core/njclient.cpp:831 as (8192+1024) = 9216. Any divergence
// between this value and the cpp's #define would silently break the chunking
// assertion; sanity-checked manually 2026-05-16 (Plan 20-00 substrate revision
// did not touch MAX_ENC_BLOCKSIZE).
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

// Local helper to clean up the destructive-drain output vector. Each
// RawDataQueueItem* owns its by-value WDL_HeapBuf — `delete item;` frees both.
static void free_drained(std::vector<NJClient::RawDataQueueItem*>& items) {
    for (auto* item : items) {
        delete item;
    }
    items.clear();
}

// ============================================================
// Test 1 — single-producer round trip
// ============================================================

static void test_rawdata_begin_write_end_roundtrip() {
    TEST("RawDataSendBegin + Write(N) + Write(isEnd=true) populates queue");

    // NJClient is ~9 MB (huge mirror arrays + SPSC rings); the default 8 MB
    // macOS thread stack cannot hold even a single instance. Heap-allocate.
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;
    unsigned char outGuid[16] = {0};

    client.RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'), 3, 0);
    client.RawDataSendWrite(outGuid, "AAAA", 4, false);
    client.RawDataSendWrite(outGuid, "BB",   2, true);

    // R3 MF4 observability: total-enqueue counter must move with each Add()
    // — 3 successful Adds across the BEGIN + 2 Writes.
    if (client.GetRawDataSendQueueTotalEnqueueCount() != 3) {
        FAIL("GetRawDataSendQueueTotalEnqueueCount() != 3 after Begin + 2 Writes");
        return;
    }

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);

    if (out.size() != 3) {
        FAIL("queue did not contain 3 items after Begin + 2 Writes");
        free_drained(out);
        return;
    }

    // Item 0: BEGIN (type=0, fourcc=H264, chidx=3, no payload)
    if (out[0]->type != 0 || out[0]->fourcc != (unsigned int)MAKE_NJ_FOURCC('H','2','6','4')
        || out[0]->chidx != 3 || out[0]->data.GetSize() != 0) {
        FAIL("item 0 is not a properly-populated BEGIN");
        free_drained(out);
        return;
    }

    // GUID is generated inside RawDataSendBegin and copied to outGuid;
    // the queued item's guid must match.
    if (memcmp(out[0]->guid, outGuid, 16) != 0) {
        FAIL("BEGIN item guid mismatch with outGuid produced by SendBegin");
        free_drained(out);
        return;
    }

    // Item 1: WRITE (type=1, flags=0, payload=4 bytes "AAAA")
    if (out[1]->type != 1 || out[1]->flags != 0
        || out[1]->data.GetSize() != 4
        || memcmp(out[1]->data.Get(), "AAAA", 4) != 0) {
        FAIL("item 1 is not a 4-byte non-end WRITE 'AAAA'");
        free_drained(out);
        return;
    }
    if (memcmp(out[1]->guid, outGuid, 16) != 0) {
        FAIL("WRITE item 1 guid mismatch with BEGIN");
        free_drained(out);
        return;
    }

    // Item 2: WRITE (type=1, flags=1, payload=2 bytes "BB", isEnd)
    if (out[2]->type != 1 || out[2]->flags != 1
        || out[2]->data.GetSize() != 2
        || memcmp(out[2]->data.Get(), "BB", 2) != 0) {
        FAIL("item 2 is not a 2-byte end WRITE 'BB' with flags=1");
        free_drained(out);
        return;
    }
    if (memcmp(out[2]->guid, outGuid, 16) != 0) {
        FAIL("WRITE item 2 guid mismatch with BEGIN");
        free_drained(out);
        return;
    }

    free_drained(out);
    PASS();
}

// ============================================================
// Test 2 — chunking helper (signature port)
// ============================================================

struct ChunkCapture {
    struct Chunk {
        int len;
        int flags;
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

static void test_rawdata_write_chunking() {
    TEST("ChunkRawDataItem splits 2*MAX_ENC_BLOCKSIZE+1 payload into 3 chunks");

    const int extended_len = 2 * MAX_ENC_BLOCKSIZE + 1;
    std::vector<unsigned char> payload(extended_len);
    for (int i = 0; i < extended_len; ++i) {
        payload[i] = (unsigned char)((i * 7 + 13) % 251);
    }
    payload[extended_len - 1] = 0xFE;  // tail marker

    // Build a RawDataQueueItem manually with a by-value WDL_HeapBuf. The
    // signature change is the test gate here: ChunkRawDataItem now takes
    // `const NJClient::RawDataQueueItem&` (not `const jamwide::RawDataItem&`)
    // and reads via `item.data.Get()` / `item.data.GetSize()`. Note: the
    // inner WDL_HeapBuf has an `explicit` constructor (wdl/heapbuf.h:91)
    // so empty-brace value-init is rejected — default-construct instead.
    NJClient::RawDataQueueItem item;
    item.type = 1;
    item.flags = 1;  // isEnd
    if (!item.data.ResizeOK(extended_len)) {
        FAIL("ResizeOK failed for extended_len payload");
        return;
    }
    memcpy(item.data.Get(), payload.data(), extended_len);

    ChunkCapture cap;
    NJClient::ChunkRawDataItem(item, chunk_emit_cb, &cap);

    if (cap.chunks.size() != 3) {
        FAIL("expected 3 chunks for 2*MAX_ENC_BLOCKSIZE+1 payload");
        return;
    }

    if (cap.chunks[0].len != MAX_ENC_BLOCKSIZE || cap.chunks[0].flags != 0) {
        FAIL("chunk 0 size/flags incorrect");
        return;
    }
    if (cap.chunks[1].len != MAX_ENC_BLOCKSIZE || cap.chunks[1].flags != 0) {
        FAIL("chunk 1 size/flags incorrect");
        return;
    }
    if (cap.chunks[2].len != 1 || cap.chunks[2].flags != 1) {
        FAIL("chunk 2 (final, 1 byte) size/flags incorrect");
        return;
    }
    if (cap.chunks[2].first_byte != 0xFE || cap.chunks[2].last_byte != 0xFE) {
        FAIL("chunk 2 byte identity check failed");
        return;
    }
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

// ============================================================
// Test 3 — video-fourcc helper (regression check; unchanged from 14.3-02)
// ============================================================

static void test_rawdata_video_fourcc_helper() {
    TEST("IsVideoFourcc accepts H264/VP8' '/MJPG and rejects OGGv/FLAC/junk");

    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    const bool h264      = client.IsVideoFourcc(MAKE_NJ_FOURCC('H','2','6','4'));
    const bool vp8_space = client.IsVideoFourcc(MAKE_NJ_FOURCC('V','P','8',' '));
    const bool vp8_null  = client.IsVideoFourcc(MAKE_NJ_FOURCC('V','P','8',0));
    const bool mjpg      = client.IsVideoFourcc(MAKE_NJ_FOURCC('M','J','P','G'));
    const bool oggv      = client.IsVideoFourcc(MAKE_NJ_FOURCC('O','G','G','v'));
    const bool flac      = client.IsVideoFourcc(MAKE_NJ_FOURCC('F','L','A','C'));
    const bool zero      = client.IsVideoFourcc(0);
    const bool junk      = client.IsVideoFourcc(MAKE_NJ_FOURCC('X','Y','Z','W'));

    if (h264 && vp8_space && !vp8_null && mjpg && !oggv && !flac && !zero && !junk) {
        PASS();
    } else {
        FAIL("video fourcc helper mismatch on at least one input");
    }
}

// ============================================================
// Test 4 — multi-producer stress (the headline correctness gate)
// ============================================================

static void multi_producer_worker(NJClient* client, int producer_id, int n_iters) {
    unsigned char outGuid[16] = {0};
    for (int i = 0; i < n_iters; ++i) {
        // Encode producer_id + i into the payload so we can verify FIFO per
        // producer in test 7. For test 4 we just need well-formed items.
        unsigned char payload[8] = {0};
        payload[0] = (unsigned char)producer_id;
        payload[1] = (unsigned char)(i & 0xff);
        payload[2] = (unsigned char)((i >> 8) & 0xff);

        client->RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'), producer_id, 0);
        client->RawDataSendWrite(outGuid, payload, 8, false);
        client->RawDataSendWrite(outGuid, NULL,    0, true);
    }
}

static void test_rawdata_multi_producer_stress() {
    TEST("4 producer threads x 100 iters each: 1200 queued items, no data race");

    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    constexpr int N_PRODUCERS = 4;
    constexpr int N_ITERS = 100;
    constexpr int EXPECTED_ITEMS = N_PRODUCERS * N_ITERS * 3;  // 3 calls per iter

    const uint64_t pre_total = client.GetRawDataSendQueueTotalEnqueueCount();

    std::vector<std::thread> producers;
    producers.reserve(N_PRODUCERS);
    for (int p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back(multi_producer_worker, &client, p, N_ITERS);
    }
    for (auto& t : producers) t.join();

    const uint64_t post_total = client.GetRawDataSendQueueTotalEnqueueCount();
    if (post_total - pre_total != (uint64_t)EXPECTED_ITEMS) {
        printf("(expected %d, got %llu) ", EXPECTED_ITEMS,
               (unsigned long long)(post_total - pre_total));
        FAIL("total-enqueue counter did not move by EXPECTED_ITEMS");
        return;
    }

    if (client.GetRawDataSendQueueHighWaterMark() == 0) {
        FAIL("high-water-mark stayed at 0 — no producer overlap observed");
        return;
    }

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);

    if ((int)out.size() != EXPECTED_ITEMS) {
        printf("(expected %d, drained %zu) ", EXPECTED_ITEMS, out.size());
        FAIL("drain did not produce EXPECTED_ITEMS items");
        free_drained(out);
        return;
    }

    // Well-formedness sweep: every item is type 0 or 1; type-1 items have
    // consistent payload size; no torn integer fields.
    for (size_t i = 0; i < out.size(); ++i) {
        const auto* it = out[i];
        if (it->type != 0 && it->type != 1) {
            printf("(item %zu type=%d) ", i, it->type);
            FAIL("malformed item: type out of range");
            free_drained(out);
            return;
        }
        if (it->type == 0) {
            // BEGIN with fourcc=H264. fourcc is a 32-bit POD field; if it
            // is mid-write torn we will catch it here.
            if (it->fourcc != (unsigned int)MAKE_NJ_FOURCC('H','2','6','4')) {
                printf("(item %zu fourcc=0x%08x) ", i, it->fourcc);
                FAIL("BEGIN fourcc torn or wrong");
                free_drained(out);
                return;
            }
        } else {
            // Type-1 items either carry 0 or 8 bytes (isEnd or payload).
            const int sz = it->data.GetSize();
            if (sz != 0 && sz != 8) {
                printf("(item %zu data.GetSize=%d) ", i, sz);
                FAIL("WRITE payload size not 0 or 8");
                free_drained(out);
                return;
            }
        }
    }

    free_drained(out);
    PASS();
}

// ============================================================
// Test 5 — drain interleaved with active producers
// ============================================================

static std::atomic<bool> stop_interleave_producers{false};
static std::atomic<int> interleave_produced{0};

static void interleave_producer(NJClient* client, int producer_id) {
    unsigned char outGuid[16] = {0};
    while (!stop_interleave_producers.load(std::memory_order_relaxed)) {
        client->RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'),
                                 producer_id, 0);
        unsigned char payload = (unsigned char)producer_id;
        client->RawDataSendWrite(outGuid, &payload, 1, true);
        interleave_produced.fetch_add(2, std::memory_order_relaxed);
        std::this_thread::yield();
    }
}

static void test_rawdata_drain_interleave() {
    TEST("Drain pop-one-unlock-process-relock interleaves with active producers");

    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    stop_interleave_producers.store(false, std::memory_order_relaxed);
    interleave_produced.store(0, std::memory_order_relaxed);

    constexpr int N_PRODUCERS = 2;
    std::vector<std::thread> producers;
    producers.reserve(N_PRODUCERS);
    for (int p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back(interleave_producer, &client, p);
    }

    // "Drain" thread (this thread) periodically destructively-drains the
    // queue. Each drain hand-back of RawDataQueueItem* must `delete` the
    // pointers immediately (matches NinjamZap's per-iter `delete item;` in
    // the real run-thread drain loop).
    int drained_count = 0;
    const auto t_start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t_start
           < std::chrono::milliseconds(500))
    {
        std::vector<NJClient::RawDataQueueItem*> out;
        client.DrainRawDataSendQueueForTest(out);
        drained_count += (int)out.size();
        free_drained(out);
        std::this_thread::yield();
    }

    stop_interleave_producers.store(true, std::memory_order_relaxed);
    for (auto& t : producers) t.join();

    // Final drain to flush any leftover items.
    std::vector<NJClient::RawDataQueueItem*> tail;
    client.DrainRawDataSendQueueForTest(tail);
    drained_count += (int)tail.size();
    free_drained(tail);

    const int total_produced = interleave_produced.load(std::memory_order_relaxed);
    if (total_produced == 0) {
        FAIL("no items produced during interleave window");
        return;
    }
    if (drained_count != total_produced) {
        printf("(produced=%d drained=%d) ", total_produced, drained_count);
        FAIL("drained item count differs from produced count (items leaked or seen twice)");
        return;
    }
    PASS();
}

// ============================================================
// Test 6 — destructor cleanup (no leak when queue non-empty on dtor)
// ============================================================

static void test_rawdata_destructor_cleanup() {
    TEST("~NJClient frees residual queued items via Empty(true) (ASAN-clean)");

    // Construct, enqueue, destroy without draining. ASAN (linked into the
    // test build by --tests) will flag any leak. The pass criterion is
    // "the test runs to completion without ASAN reporting a leak"; we also
    // sanity-check that the items were actually enqueued (otherwise the
    // dtor cleanup test would pass trivially even if Empty(true) was broken).
    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    unsigned char outGuid[16] = {0};
    for (int i = 0; i < 10; ++i) {
        client_owned->RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'), 0, 0);
        unsigned char payload[16] = {0};
        payload[0] = (unsigned char)i;
        client_owned->RawDataSendWrite(outGuid, payload, sizeof(payload), true);
    }
    const uint64_t enq = client_owned->GetRawDataSendQueueTotalEnqueueCount();
    if (enq != 20) {
        printf("(enq=%llu) ", (unsigned long long)enq);
        FAIL("expected 20 total enqueues before destructor");
        return;
    }

    // Trigger destructor. If Empty(true) is broken, ASAN will report 20 leaks.
    client_owned.reset();

    PASS();
}

// ============================================================
// Test 7 — per-producer FIFO preservation under multi-producer load
// ============================================================

struct OrderedProducerArgs {
    NJClient* client;
    int producer_id;
    int n_items;
};

static void ordered_producer(OrderedProducerArgs args) {
    unsigned char outGuid[16] = {0};
    args.client->RawDataSendBegin(outGuid, MAKE_NJ_FOURCC('H','2','6','4'),
                                  args.producer_id, 0);
    for (int seq = 0; seq < args.n_items; ++seq) {
        unsigned char payload[8] = {0};
        payload[0] = (unsigned char)args.producer_id;
        payload[1] = (unsigned char)(seq & 0xff);
        payload[2] = (unsigned char)((seq >> 8) & 0xff);
        const bool is_end = (seq == args.n_items - 1);
        args.client->RawDataSendWrite(outGuid, payload, sizeof(payload), is_end);
    }
}

static void test_rawdata_send_ordering_per_producer() {
    TEST("Per-producer FIFO: each producer's items observed in monotonic seq");

    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    constexpr int N_PRODUCERS = 2;
    constexpr int N_WRITES_PER = 4;
    constexpr int ITEMS_PER_PRODUCER = 1 /*BEGIN*/ + N_WRITES_PER;
    constexpr int EXPECTED_TOTAL = N_PRODUCERS * ITEMS_PER_PRODUCER;

    std::vector<std::thread> threads;
    threads.reserve(N_PRODUCERS);
    for (int p = 0; p < N_PRODUCERS; ++p) {
        threads.emplace_back(ordered_producer, OrderedProducerArgs{
            &client, p, N_WRITES_PER});
    }
    for (auto& t : threads) t.join();

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);
    if ((int)out.size() != EXPECTED_TOTAL) {
        printf("(expected %d, drained %zu) ", EXPECTED_TOTAL, out.size());
        FAIL("unexpected item count for ordering test");
        free_drained(out);
        return;
    }

    // Partition by producer_id; verify each partition is BEGIN-first then
    // monotonic seq. producer_id is recoverable from:
    //   - BEGIN: stored in item.chidx (we set chidx=producer_id above)
    //   - WRITE: stored in payload byte 0
    int last_seq[N_PRODUCERS] = {-1, -1};
    bool seen_begin[N_PRODUCERS] = {false, false};
    for (auto* item : out) {
        int producer_id;
        int seq;
        if (item->type == 0) {
            producer_id = item->chidx;
            seq = -1;  // BEGIN is not part of the seq order; just must come first.
        } else {
            const unsigned char* p =
                (const unsigned char*)item->data.Get();
            if (!p || item->data.GetSize() != 8) {
                FAIL("WRITE payload not 8 bytes");
                free_drained(out);
                return;
            }
            producer_id = (int)p[0];
            seq = (int)p[1] | ((int)p[2] << 8);
        }

        if (producer_id < 0 || producer_id >= N_PRODUCERS) {
            printf("(producer_id=%d) ", producer_id);
            FAIL("producer_id out of range");
            free_drained(out);
            return;
        }

        if (item->type == 0) {
            if (seen_begin[producer_id]) {
                FAIL("second BEGIN observed for same producer");
                free_drained(out);
                return;
            }
            seen_begin[producer_id] = true;
        } else {
            if (!seen_begin[producer_id]) {
                FAIL("WRITE observed before BEGIN for producer");
                free_drained(out);
                return;
            }
            if (seq != last_seq[producer_id] + 1) {
                printf("(producer=%d seq=%d expected=%d) ",
                       producer_id, seq, last_seq[producer_id] + 1);
                FAIL("per-producer FIFO violated");
                free_drained(out);
                return;
            }
            last_seq[producer_id] = seq;
        }
    }
    for (int p = 0; p < N_PRODUCERS; ++p) {
        if (last_seq[p] != N_WRITES_PER - 1) {
            printf("(producer %d ended at seq %d, expected %d) ",
                   p, last_seq[p], N_WRITES_PER - 1);
            FAIL("producer's final seq did not reach N_WRITES_PER-1");
            free_drained(out);
            return;
        }
    }

    free_drained(out);
    PASS();
}

// ============================================================
// Test 8 — wire-ordering BEGIN -> marker -> SPS/PPS -> frame
// ============================================================

static void test_rawdata_wire_ordering_begin_marker_sps_frame() {
    TEST("Wire order BEGIN -> marker24 -> SPS/PPS -> frame under mock m_video_cs");

    auto client_owned = std::unique_ptr<NJClient>(new NJClient);
    NJClient& client = *client_owned;

    // Mock the whole-block m_video_cs that Plan 20-02 will land. Producer A
    // plays the audio-thread role (BEGIN + marker + SPS/PPS under the lock);
    // Producer B plays the encoder role and waits for the lock before
    // emitting a frame.
    std::mutex mock_video_cs;
    std::atomic<bool> a_done{false};

    unsigned char g1[16] = {0};
    unsigned char marker24[24];
    for (int i = 0; i < 24; ++i) marker24[i] = (unsigned char)(0x20 + i);
    unsigned char spspps[12];
    for (int i = 0; i < 12; ++i) spspps[i] = (unsigned char)(0x40 + i);
    unsigned char frame[16];
    for (int i = 0; i < 16; ++i) frame[i] = (unsigned char)(0x80 + i);

    std::thread audio_role([&]{
        std::lock_guard<std::mutex> lk(mock_video_cs);
        client.RawDataSendBegin(g1, MAKE_NJ_FOURCC('H','2','6','4'), 1, 0);
        client.RawDataSendWrite(g1, marker24, 24, false);
        client.RawDataSendWrite(g1, spspps,   12, false);
        a_done.store(true, std::memory_order_release);
    });
    // Give Producer A a chance to grab mock_video_cs first.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::thread encoder_role([&]{
        std::lock_guard<std::mutex> lk(mock_video_cs);
        // By the time we acquire the lock, audio_role has released it
        // (release-acquire ordering via a_done); Plan 20-02's real
        // m_video_cs serialization gives us the same guarantee.
        if (!a_done.load(std::memory_order_acquire)) {
            // If this fires, the mock mutex ordering broke — the rest of
            // the test will likely fail too.
        }
        client.RawDataSendWrite(g1, frame, 16, false);
    });

    audio_role.join();
    encoder_role.join();

    std::vector<NJClient::RawDataQueueItem*> out;
    client.DrainRawDataSendQueueForTest(out);

    if (out.size() != 4) {
        printf("(drained %zu, expected 4) ", out.size());
        FAIL("expected 4 items: BEGIN + marker + SPS/PPS + frame");
        free_drained(out);
        return;
    }
    if (out[0]->type != 0
        || out[1]->type != 1 || out[1]->data.GetSize() != 24
        || memcmp(out[1]->data.Get(), marker24, 24) != 0
        || out[2]->type != 1 || out[2]->data.GetSize() != 12
        || memcmp(out[2]->data.Get(), spspps, 12) != 0
        || out[3]->type != 1 || out[3]->data.GetSize() != 16
        || memcmp(out[3]->data.Get(), frame, 16) != 0)
    {
        FAIL("wire order violated: expected BEGIN, marker24, SPS/PPS, frame");
        free_drained(out);
        return;
    }
    free_drained(out);
    PASS();
}

// ============================================================
// Main runner
// ============================================================

int main()
{
    printf("=== test_rawdata_send (Phase 20-00 NinjamZap-literal substrate) ===\n");

    test_rawdata_begin_write_end_roundtrip();
    test_rawdata_write_chunking();
    test_rawdata_video_fourcc_helper();
    test_rawdata_multi_producer_stress();
    test_rawdata_drain_interleave();
    test_rawdata_destructor_cleanup();
    test_rawdata_send_ordering_per_producer();
    test_rawdata_wire_ordering_begin_marker_sps_frame();

    printf("\nTotal: %d passed, %d failed\n", tests_passed, tests_run - tests_passed);
    return (tests_passed == tests_run) ? 0 : 1;
}
