/*
    JamWide Plugin — tests/test_block_queue_spsc.cpp

    Phase 15.1-07b: BlockRecord SPSC fill/drain integrity + overflow counter +
    Codex M-7 bounds-check exercise. Replaces BufferQueue audio-thread sites.

    Includes 5 test cases:
      1. test_block_record_size_assertion       — sizeof(BlockRecord) compile/runtime check
      2. test_fill_drain_integrity              — push 16 records, drain, verify integrity
      3. test_overflow_returns_false            — capacity behavior (N=16 → effective 15)
      4. test_concurrent_audio_to_encoder       — 5s wall-time producer/consumer FIFO
      5. test_bounds_check_rejects_oversized    — Codex M-7 bounds-check exercise

    All tests pure-C++, no NJClient link; runs under TSan with zero races.
*/

#include "../src/threading/spsc_ring.h"
#include "../src/threading/spsc_payloads.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

// ---- Minimal TEST/PASS/FAIL framework matching house style ---------------
static int tests_run    = 0;
static int tests_passed = 0;
static const char* current_test_name = nullptr;

#define TEST(name) do { current_test_name = (name); ++tests_run; \
    std::printf("  TEST: %s ... ", current_test_name); std::fflush(stdout); } while(0)
#define PASS() do { ++tests_passed; std::printf("PASSED\n"); std::fflush(stdout); } while(0)
#define FAIL(msg) do { std::printf("FAILED: %s\n", (msg)); std::fflush(stdout); } while(0)

// ============================================================================
// Test 1: BlockRecord size assertion (compile-time + runtime defensive)
// ============================================================================
static_assert(sizeof(jamwide::BlockRecord) <= 16500,
              "BlockRecord size unexpectedly large — header padding budget exceeded");

static void test_block_record_size_assertion() {
    TEST("sizeof(BlockRecord) <= 16500 bytes (compile + runtime)");
    if (sizeof(jamwide::BlockRecord) > 16500) {
        FAIL("BlockRecord size regression");
        return;
    }
    // Also confirm it is trivially copyable — the SPSC ring requires it.
    if (!std::is_trivially_copyable_v<jamwide::BlockRecord>) {
        FAIL("BlockRecord is not trivially copyable");
        return;
    }
    PASS();
}

// ============================================================================
// Test 2: Fill/drain integrity — push 15 BlockRecords with deterministic
// sample patterns, drain, verify each popped record matches the pushed one.
// (Capacity N=16 effective is N-1 = 15 due to one-slot-empty SPSC convention.)
// ============================================================================
static void test_fill_drain_integrity() {
    TEST("Fill/drain integrity: 15 BlockRecords roundtrip with sample-buffer match");
    jamwide::SpscRing<jamwide::BlockRecord, 16> ring;

    constexpr int kCount = 15;  // N-1 effective capacity
    constexpr int kSamples = 256;
    constexpr int kNch = 2;

    // Push 15 records with deterministic content.
    for (int i = 0; i < kCount; ++i) {
        jamwide::BlockRecord br{};
        br.attr = i;
        br.startpos = static_cast<double>(i) * 0.5;
        br.sample_count = kSamples;
        br.nch = kNch;
        for (int s = 0; s < kSamples * kNch; ++s) {
            br.samples[s] = static_cast<float>(i * 1000 + s);
        }
        if (!ring.try_push(std::move(br))) {
            FAIL("try_push returned false before reaching effective capacity");
            return;
        }
    }

    // Drain and verify FIFO order + content integrity.
    int popped = 0;
    bool ok = true;
    ring.drain([&](jamwide::BlockRecord&& br) {
        if (br.attr != popped) {
            ok = false;
            std::printf("\n    expected attr=%d, got attr=%d", popped, br.attr);
            return;
        }
        if (br.startpos != static_cast<double>(popped) * 0.5) {
            ok = false;
            std::printf("\n    startpos mismatch at attr=%d", popped);
            return;
        }
        if (br.sample_count != kSamples || br.nch != kNch) {
            ok = false;
            std::printf("\n    sample_count/nch mismatch at attr=%d", popped);
            return;
        }
        for (int s = 0; s < kSamples * kNch; ++s) {
            if (br.samples[s] != static_cast<float>(popped * 1000 + s)) {
                ok = false;
                std::printf("\n    sample[%d] mismatch at attr=%d", s, popped);
                return;
            }
        }
        ++popped;
    });

    if (!ok) { FAIL("integrity check failed"); return; }
    if (popped != kCount) { FAIL("popped count != pushed count"); return; }
    PASS();
}

// ============================================================================
// Test 3: Overflow returns false — N=16 → effective capacity 15.
// ============================================================================
static void test_overflow_returns_false() {
    TEST("Overflow: N=16 ring rejects 16th push, accepts after one drain");
    jamwide::SpscRing<jamwide::BlockRecord, 16> ring;

    int push_ok = 0;
    int push_fail = 0;

    // Push 16 records — only 15 should succeed.
    for (int i = 0; i < 16; ++i) {
        jamwide::BlockRecord br{};
        br.attr = i;
        if (ring.try_push(std::move(br))) ++push_ok;
        else ++push_fail;
    }

    if (push_ok != 15 || push_fail != 1) {
        std::printf("\n    expected 15 ok + 1 fail, got %d ok + %d fail", push_ok, push_fail);
        FAIL("capacity behavior unexpected");
        return;
    }

    // Pop one; the next push must succeed.
    auto popped = ring.try_pop();
    if (!popped) { FAIL("try_pop on full ring returned nullopt"); return; }

    jamwide::BlockRecord again{};
    again.attr = 99;
    if (!ring.try_push(std::move(again))) {
        FAIL("after pop, push still failed");
        return;
    }
    PASS();
}

// ============================================================================
// Test 4: Concurrent audio-to-encoder — producer pushes ~5s of records at
// 200 Hz; consumer drains at 50 Hz. All records observed exactly once in
// FIFO order (verified by monotonic attr sequence).
// ============================================================================
static void test_concurrent_audio_to_encoder() {
    TEST("Concurrent producer/consumer: ~5s, 200Hz writer + 50Hz reader, FIFO preserved");

    jamwide::SpscRing<jamwide::BlockRecord, 16> ring;
    std::atomic<bool> producer_done{false};
    std::atomic<int>  push_count{0};
    std::atomic<int>  drop_count{0};
    std::atomic<int>  pop_count{0};
    std::atomic<int>  expected_next{0};
    std::atomic<bool> fifo_violated{false};

    auto t0 = std::chrono::steady_clock::now();
    constexpr int kProducerHz = 200;
    constexpr int kRunSeconds = 5;
    const int kTotal = kProducerHz * kRunSeconds;

    std::thread producer([&]() {
        for (int i = 0; i < kTotal; ++i) {
            jamwide::BlockRecord br{};
            br.attr = i;
            br.sample_count = 64;
            br.nch = 1;
            // Tag samples[0] with attr so consumer can verify identity.
            br.samples[0] = static_cast<float>(i);
            if (ring.try_push(std::move(br))) {
                push_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                drop_count.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / kProducerHz));
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        while (!producer_done.load(std::memory_order_acquire) || !ring.empty()) {
            ring.drain([&](jamwide::BlockRecord&& br) {
                int expected = expected_next.load(std::memory_order_relaxed);
                if (br.attr != expected) {
                    fifo_violated.store(true, std::memory_order_relaxed);
                    std::printf("\n    FIFO violation: expected %d, got %d", expected, br.attr);
                }
                if (br.samples[0] != static_cast<float>(br.attr)) {
                    fifo_violated.store(true, std::memory_order_relaxed);
                    std::printf("\n    sample tag mismatch at attr=%d", br.attr);
                }
                expected_next.store(br.attr + 1, std::memory_order_relaxed);
                pop_count.fetch_add(1, std::memory_order_relaxed);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    producer.join();
    consumer.join();

    auto t1 = std::chrono::steady_clock::now();
    auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::printf("(wall=%lldms, push=%d, drop=%d, pop=%d) ",
                static_cast<long long>(wall_ms),
                push_count.load(), drop_count.load(), pop_count.load());

    if (fifo_violated.load()) { FAIL("FIFO order or sample tag broken"); return; }
    if (push_count.load() + drop_count.load() != kTotal) {
        FAIL("producer count mismatch");
        return;
    }
    if (pop_count.load() != push_count.load()) {
        FAIL("popped count != pushed count (consumer missed records)");
        return;
    }
    if (drop_count.load() > 0) {
        // Drops are tolerated under contention but should be rare; do not fail
        // (the OVERFLOW test case explicitly exercises drop semantics).
        std::printf("[note: %d drops under contention, FIFO of accepted records preserved] ",
                    drop_count.load());
    }
    PASS();
}

// ============================================================================
// Test 5 (Codex M-7): bounds-check rejects oversized records.
// Mirror the pushBlockRecord helper inline so the unit test exercises the
// SAME branch logic the production code uses.
// ============================================================================
static void test_bounds_check_rejects_oversized() {
    TEST("M-7 bounds-check: oversized sample_count/nch rejected, drop counter increments");

    jamwide::SpscRing<jamwide::BlockRecord, 16> ring;
    std::atomic<uint64_t> drops{0};

    // Inline mirror of pushBlockRecord helper for unit-test isolation.
    auto push = [&](int sc, int nch, const float* samples_ptr) {
        if (sc > jamwide::MAX_BLOCK_SAMPLES || nch > jamwide::MAX_BLOCK_CHANNELS
            || sc <= 0 || nch <= 0) {
            drops.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        jamwide::BlockRecord br{};
        br.sample_count = sc;
        br.nch = nch;
        std::memcpy(br.samples, samples_ptr, static_cast<size_t>(sc * nch) * sizeof(float));
        if (!ring.try_push(std::move(br))) {
            drops.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<float> samples(jamwide::MAX_BLOCK_SAMPLES * jamwide::MAX_BLOCK_CHANNELS, 1.0f);

    // (a) Within bounds — must succeed.
    push(jamwide::MAX_BLOCK_SAMPLES, jamwide::MAX_BLOCK_CHANNELS, samples.data());
    if (drops.load() != 0) {
        FAIL("rejected within-bounds push");
        return;
    }

    // (b) Just over the sample_count bound — must reject.
    push(jamwide::MAX_BLOCK_SAMPLES + 1, jamwide::MAX_BLOCK_CHANNELS, samples.data());
    if (drops.load() != 1) {
        FAIL("did not reject oversized sample_count");
        return;
    }

    // (c) nch > MAX_BLOCK_CHANNELS — must reject.
    push(256, jamwide::MAX_BLOCK_CHANNELS + 1, samples.data());
    if (drops.load() != 2) {
        FAIL("did not reject oversized nch");
        return;
    }

    // (d) sample_count <= 0 — must reject.
    push(0, 1, samples.data());
    if (drops.load() != 3) {
        FAIL("did not reject sample_count <= 0");
        return;
    }

    // (e) nch <= 0 — must reject.
    push(64, 0, samples.data());
    if (drops.load() != 4) {
        FAIL("did not reject nch <= 0");
        return;
    }

    PASS();
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::printf("test_block_queue_spsc — BlockRecord SPSC fill/drain + integrity + overflow + M-7 bounds\n");

    test_block_record_size_assertion();
    test_fill_drain_integrity();
    test_overflow_returns_false();
    test_concurrent_audio_to_encoder();
    test_bounds_check_rejects_oversized();

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
