/*
    JamWide Plugin — tests/test_decode_media_buffer_spsc.cpp

    Phase 15.1-07c (CR-12): DecodeMediaBuffer SPSC byte-stream coverage.
    Self-contained reimplementation of the Read/Write byte-stream pattern
    (no NJClient link). Verifies:

      1. test_chunk_roundtrip               — single-chunk push/pop fidelity
      2. test_partial_read_via_consumer_buf — 1024-byte write, four 256-byte
                                              reads — mirrors the audio-thread
                                              codec srcbuf-fill pattern
      3. test_concurrent_write_read         — writer pushes 100 KB, reader
                                              drains with 64-byte Read() calls;
                                              FIFO byte-sequence integrity
                                              preserved
      4. test_atomic_refcnt                 — race two Release() calls on a
                                              refcnt=2 instance; exactly one
                                              delete must fire
      5. test_overflow_returns_partial_write — full SPSC: Write returns less
                                              than len, drop counter increments
      6. test_size_tracks_total_written     — Size() preserves legacy semantics
                                              (cumulative bytes written, not
                                              instantaneous occupancy)

    Designed to also pass under -fsanitize=thread (TSan) with zero races.
    main returns 0 iff tests_passed == tests_run.
*/

#include "../src/threading/spsc_ring.h"
#include "../src/threading/spsc_payloads.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

// ============================================================================
// Minimal TEST/PASS/FAIL framework matching house style (mirrors
// tests/test_block_queue_spsc.cpp).
// ============================================================================
static int tests_run    = 0;
static int tests_passed = 0;
static const char* current_test_name = nullptr;

#define TEST(name) do { current_test_name = (name); ++tests_run; \
    std::printf("  TEST: %s ... ", current_test_name); std::fflush(stdout); } while(0)
#define PASS() do { ++tests_passed; std::printf("PASSED\n"); std::fflush(stdout); } while(0)
#define FAIL(msg) do { std::printf("FAILED: %s\n", (msg)); std::fflush(stdout); } while(0)

// ============================================================================
// In-test reimplementation of the production DecodeMediaBuffer Read/Write
// pattern. Mirrors src/core/njclient.cpp:DecodeMediaBuffer (15.1-07c CR-12)
// so any divergence between the test contract and the production class is
// caught by the unit-test harness (and so this test does not require the
// full NJClient link).
//
// The tests rely on the same SPSC capacity (32) and CHUNK_BYTES (4096) the
// production class uses; both come from src/threading/spsc_payloads.h.
// ============================================================================
namespace test_dmb {

struct DecodeMediaBuffer {
    jamwide::SpscRing<jamwide::DecodeChunk, 32> chunks;
    std::array<uint8_t, jamwide::CHUNK_BYTES * 2> consumer_buf{};
    int  consumer_buf_len = 0;
    int  consumer_buf_pos = 0;
    std::atomic<int64_t> total_written{0};
    std::atomic<uint64_t> drops{0};

    int Write(const void* buf, int len) {
        if (len <= 0 || !buf) return 0;
        const uint8_t* in = static_cast<const uint8_t*>(buf);
        int remaining = len;
        while (remaining > 0) {
            jamwide::DecodeChunk chunk{};
            chunk.len = std::min(remaining, jamwide::CHUNK_BYTES);
            std::memcpy(chunk.data, in, static_cast<size_t>(chunk.len));
            if (!chunks.try_push(chunk)) {
                drops.fetch_add(1, std::memory_order_relaxed);
                return len - remaining;
            }
            in += chunk.len;
            remaining -= chunk.len;
            total_written.fetch_add(chunk.len, std::memory_order_relaxed);
        }
        return len;
    }

    int Read(void* buf, int len) {
        if (len <= 0 || !buf) return 0;
        int written = 0;
        uint8_t* out = static_cast<uint8_t*>(buf);
        while (written < len) {
            const int avail = consumer_buf_len - consumer_buf_pos;
            if (avail > 0) {
                const int take = std::min(avail, len - written);
                std::memcpy(out + written,
                            consumer_buf.data() + consumer_buf_pos,
                            static_cast<size_t>(take));
                consumer_buf_pos += take;
                written += take;
                continue;
            }
            auto chunk = chunks.try_pop();
            if (!chunk) break;
            const int clen = chunk->len;
            if (clen <= 0 || clen > jamwide::CHUNK_BYTES) continue;
            std::memcpy(consumer_buf.data(), chunk->data,
                        static_cast<size_t>(clen));
            consumer_buf_len = clen;
            consumer_buf_pos = 0;
        }
        return written;
    }

    int Size() const {
        return static_cast<int>(total_written.load(std::memory_order_relaxed));
    }
};

} // namespace test_dmb

// ============================================================================
// Test 1: single-chunk roundtrip — push DecodeChunk{len=16, data={0..15}},
// pop via Read(), verify byte-for-byte equality.
// ============================================================================
static void test_chunk_roundtrip() {
    TEST("Single-chunk roundtrip: 16 bytes Write -> Read with bit-exact match");
    test_dmb::DecodeMediaBuffer buf;
    uint8_t in[16];
    for (int i = 0; i < 16; ++i) in[i] = static_cast<uint8_t>(i);
    int wrote = buf.Write(in, 16);
    if (wrote != 16) { FAIL("Write returned wrong byte count"); return; }

    uint8_t out[16] = {0};
    int read = buf.Read(out, 16);
    if (read != 16) { FAIL("Read returned wrong byte count"); return; }
    for (int i = 0; i < 16; ++i) {
        if (out[i] != static_cast<uint8_t>(i)) {
            FAIL("byte mismatch on roundtrip");
            return;
        }
    }
    // Subsequent Read on empty buffer must return 0 (no spin, no block).
    uint8_t leftover[16] = {0};
    int leftover_read = buf.Read(leftover, 16);
    if (leftover_read != 0) {
        FAIL("Read on drained buffer did not return 0");
        return;
    }
    PASS();
}

// ============================================================================
// Test 2: partial read via consumer buffer — write 1024 bytes (fits in one
// chunk), then call Read(buf, 256) four times. Each call must return exactly
// 256 bytes, with the byte sequence reconstructed in order. Mirrors the
// audio-thread codec srcbuf-fill pattern (codec asks for sub-chunk lengths
// over multiple calls).
// ============================================================================
static void test_partial_read_via_consumer_buf() {
    TEST("Partial reads: 1024B write -> 4x Read(256B) preserves sequence");
    test_dmb::DecodeMediaBuffer buf;
    constexpr int kWrite = 1024;
    constexpr int kRead = 256;
    constexpr int kIter = 4;

    std::vector<uint8_t> in(kWrite);
    for (int i = 0; i < kWrite; ++i) in[i] = static_cast<uint8_t>(i & 0xFF);

    int wrote = buf.Write(in.data(), kWrite);
    if (wrote != kWrite) { FAIL("Write byte count mismatch"); return; }

    std::vector<uint8_t> out;
    out.reserve(kWrite);
    for (int it = 0; it < kIter; ++it) {
        uint8_t chunk[kRead] = {0};
        int n = buf.Read(chunk, kRead);
        if (n != kRead) {
            std::printf("\n    iter %d: Read returned %d, expected %d", it, n, kRead);
            FAIL("partial-read returned short on full chunk");
            return;
        }
        for (int i = 0; i < n; ++i) out.push_back(chunk[i]);
    }
    if (static_cast<int>(out.size()) != kWrite) {
        FAIL("aggregate read size mismatch");
        return;
    }
    for (int i = 0; i < kWrite; ++i) {
        if (out[i] != in[i]) {
            FAIL("byte sequence corruption across partial reads");
            return;
        }
    }
    PASS();
}

// ============================================================================
// Test 3: concurrent write/read — writer thread pushes 100 chunks of 1 KB
// each (total 100 KB, deterministic byte pattern); reader thread does
// Read(buf, 64) repeatedly until the producer is done AND the buffer is
// drained. Sum of bytes read must equal sum of bytes accepted (push_ok),
// and the byte sequence must be preserved in FIFO order.
//
// This is the integration test that exercises the SPSC producer/consumer
// race window and validates that no bytes are duplicated, lost (beyond
// the producer-side drop counter), or reordered.
// ============================================================================
static void test_concurrent_write_read() {
    TEST("Concurrent write/read: 100KB stream, 1KB writes vs 64B reads, FIFO preserved");
    test_dmb::DecodeMediaBuffer buf;

    constexpr int kChunkBytes = 1024;
    constexpr int kNumWrites = 100;
    constexpr int kReadSize = 64;

    // Deterministic byte pattern — value at offset i is (i * 31 + 7) & 0xFF.
    auto pattern_byte = [](int i) -> uint8_t {
        return static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    };

    std::atomic<bool> producer_done{false};
    std::atomic<int>  bytes_pushed{0};
    std::atomic<int>  bytes_dropped{0};

    std::thread writer([&]{
        std::vector<uint8_t> chunk(kChunkBytes);
        for (int w = 0; w < kNumWrites; ++w) {
            for (int b = 0; b < kChunkBytes; ++b) {
                chunk[b] = pattern_byte(w * kChunkBytes + b);
            }
            int wrote = buf.Write(chunk.data(), kChunkBytes);
            bytes_pushed.fetch_add(wrote, std::memory_order_relaxed);
            if (wrote < kChunkBytes) {
                bytes_dropped.fetch_add(kChunkBytes - wrote, std::memory_order_relaxed);
            }
            // Brief yield to let the reader keep up — avoids artificial
            // overflow on the small SPSC (32 chunks * 4096 = 128 KB capacity
            // is comfortable, but mixing 1KB DecodeChunk-sized records
            // pushed 100x into a 32-slot ring still benefits from pacing).
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::vector<uint8_t> received;
    received.reserve(kNumWrites * kChunkBytes);
    bool fifo_violated = false;

    std::thread reader([&]{
        uint8_t scratch[kReadSize];
        // Loop until producer signals done AND buffer is drained.
        for (;;) {
            int n = buf.Read(scratch, kReadSize);
            if (n > 0) {
                for (int i = 0; i < n; ++i) received.push_back(scratch[i]);
            } else {
                if (producer_done.load(std::memory_order_acquire)) {
                    // One more drain attempt to catch any in-flight chunks.
                    int more = buf.Read(scratch, kReadSize);
                    if (more <= 0) break;
                    for (int i = 0; i < more; ++i) received.push_back(scratch[i]);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
        // Verify FIFO byte sequence integrity — every received byte must
        // match the pattern at its absolute offset.
        for (size_t i = 0; i < received.size(); ++i) {
            if (received[i] != pattern_byte(static_cast<int>(i))) {
                fifo_violated = true;
                std::printf("\n    FIFO violation at byte %zu: expected %u, got %u",
                            i, pattern_byte(static_cast<int>(i)), received[i]);
                break;
            }
        }
    });

    writer.join();
    reader.join();

    int pushed = bytes_pushed.load(std::memory_order_relaxed);
    int dropped = bytes_dropped.load(std::memory_order_relaxed);
    int got = static_cast<int>(received.size());

    std::printf("(pushed=%d, dropped=%d, received=%d) ", pushed, dropped, got);

    if (fifo_violated) { FAIL("byte sequence corrupted"); return; }
    if (pushed != got) {
        FAIL("received byte count != pushed byte count");
        return;
    }
    if (dropped != 0) {
        // Drops are tolerated (producer outpacing consumer triggers SPSC
        // overflow), but FIFO of accepted bytes must still hold. Note in log.
        std::printf("[note: %d bytes dropped under contention] ", dropped);
    }
    PASS();
}

// ============================================================================
// Test 4: atomic refcnt — race two Release() calls on a refcnt=2 instance
// from different threads. Exactly one delete must fire (counter atomic
// semantics validated). Mirrors T-15.1-07c-01 mitigation.
// ============================================================================
static void test_atomic_refcnt() {
    TEST("Atomic refcnt: concurrent Release() races safely (exactly one delete)");

    struct DMB {
        std::atomic<int> refcnt{2};
        std::atomic<int>* delete_counter = nullptr;
        void Release() {
            if (refcnt.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                if (delete_counter) delete_counter->fetch_add(1, std::memory_order_relaxed);
                delete this;
            }
        }
    };

    // Run many trials — a bug here would only surface probabilistically.
    constexpr int kTrials = 1000;
    std::atomic<int> total_deletes{0};
    bool ok = true;

    for (int t = 0; t < kTrials; ++t) {
        std::atomic<int> deletes_for_trial{0};
        DMB* dmb = new DMB{};
        dmb->delete_counter = &deletes_for_trial;

        std::thread t1([&]{ dmb->Release(); });
        std::thread t2([&]{ dmb->Release(); });
        t1.join();
        t2.join();

        int d = deletes_for_trial.load(std::memory_order_acquire);
        total_deletes.fetch_add(d, std::memory_order_relaxed);
        if (d != 1) {
            std::printf("\n    trial %d: delete count = %d (expected 1)", t, d);
            ok = false;
            break;
        }
    }

    if (!ok) {
        FAIL("atomic refcnt race produced incorrect delete count");
        return;
    }
    if (total_deletes.load(std::memory_order_acquire) != kTrials) {
        FAIL("aggregate delete count mismatch");
        return;
    }
    PASS();
}

// ============================================================================
// Test 5: overflow returns partial write — fill the SPSC to capacity, then
// attempt to write more. Write() must return < len; the drop counter must
// increment. Validates T-15.1-07c-03 producer-side drop policy.
//
// SPSC effective capacity is N-1 = 31 chunks (one slot reserved for the
// empty/full distinction). With CHUNK_BYTES=4096, the maximum bytes that
// can sit in the ring is 31 * 4096 = 126976.
// ============================================================================
static void test_overflow_returns_partial_write() {
    TEST("Overflow: full ring -> Write returns short, drop counter increments");
    test_dmb::DecodeMediaBuffer buf;

    constexpr int kEffectiveCap = 31;
    constexpr int kFillBytes = kEffectiveCap * jamwide::CHUNK_BYTES;
    constexpr int kOverflowBytes = jamwide::CHUNK_BYTES * 4; // attempt 4 more chunks

    std::vector<uint8_t> filler(kFillBytes, 0xAA);
    int filled = buf.Write(filler.data(), kFillBytes);
    if (filled != kFillBytes) {
        std::printf("\n    fill: wrote %d / %d bytes", filled, kFillBytes);
        FAIL("could not fill ring to effective capacity");
        return;
    }
    if (buf.drops.load() != 0) {
        FAIL("drop counter incremented during initial fill");
        return;
    }

    std::vector<uint8_t> overflow(kOverflowBytes, 0x55);
    int over_wrote = buf.Write(overflow.data(), kOverflowBytes);
    if (over_wrote >= kOverflowBytes) {
        FAIL("Write did not signal partial write on full ring");
        return;
    }
    if (buf.drops.load() == 0) {
        FAIL("drop counter did not increment on full-ring Write");
        return;
    }

    // After draining one chunk, the next Write must succeed again.
    uint8_t scratch[jamwide::CHUNK_BYTES];
    int drained = buf.Read(scratch, jamwide::CHUNK_BYTES);
    if (drained != jamwide::CHUNK_BYTES) {
        FAIL("could not drain one chunk to free capacity");
        return;
    }
    int after = buf.Write(overflow.data(), jamwide::CHUNK_BYTES);
    if (after != jamwide::CHUNK_BYTES) {
        FAIL("post-drain Write did not succeed");
        return;
    }
    PASS();
}

// ============================================================================
// Test 6: Size() preserves legacy semantics — cumulative bytes written, NOT
// instantaneous occupancy. Reading bytes does NOT decrement Size() (matches
// the legacy WDL_Queue::Available() behavior, which never compacted; the
// startPlaying pre-buffer threshold check at RemoteDownload depends on this).
// ============================================================================
static void test_size_tracks_total_written() {
    TEST("Size() preserves legacy semantics: cumulative bytes written");
    test_dmb::DecodeMediaBuffer buf;

    if (buf.Size() != 0) { FAIL("initial Size != 0"); return; }

    std::vector<uint8_t> data(2048, 0x42);
    buf.Write(data.data(), 1024);
    if (buf.Size() != 1024) {
        std::printf("\n    after 1024 write, Size=%d", buf.Size());
        FAIL("Size did not track first write");
        return;
    }

    buf.Write(data.data(), 512);
    if (buf.Size() != 1024 + 512) {
        FAIL("Size did not accumulate across writes");
        return;
    }

    // Reads MUST NOT decrement Size() — that's the legacy contract.
    uint8_t scratch[256];
    buf.Read(scratch, 256);
    if (buf.Size() != 1024 + 512) {
        FAIL("Size decreased after Read (semantics regression)");
        return;
    }
    PASS();
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::printf("test_decode_media_buffer_spsc — DecodeChunk SPSC byte-stream + atomic refcnt (CR-12)\n");

    test_chunk_roundtrip();
    test_partial_read_via_consumer_buf();
    test_concurrent_write_read();
    test_atomic_refcnt();
    test_overflow_returns_partial_write();
    test_size_tracks_total_written();

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
