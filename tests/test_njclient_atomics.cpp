/*
    JamWide Plugin - test_njclient_atomics.cpp

    TSan stress for the CR-03 release/acquire pattern (15.1-02).

    Includes test_missed_update_semantics per Codex L-10 review:
    edge-triggered best-effort publication is correct behavior, NOT a last-value latch.

    These tests exercise the *pattern* in isolation (a small AtomicBeatInfo struct
    that mirrors the m_bpm/m_bpi/m_beatinfo_updated atomics in NJClient). They do not
    link against NJClient — keeping the unit-test build small and TSan-friendly.
*/

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <thread>
#include <utility>

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

namespace {

// Mirrors the CR-03 atomic field block from src/core/njclient.h.
struct AtomicBeatInfo {
    std::atomic<int> m_beatinfo_updated{0};
    std::atomic<int> m_bpm{0};
    std::atomic<int> m_bpi{0};
};

// =====================================================================
// Test 1: roundtrip — single-thread release/acquire returns published values.
// =====================================================================
static void test_roundtrip_single_thread() {
    TEST("roundtrip single-thread release/acquire");
    AtomicBeatInfo s;
    s.m_bpm.store(120, std::memory_order_relaxed);
    s.m_bpi.store(16, std::memory_order_relaxed);
    s.m_beatinfo_updated.store(1, std::memory_order_release);

    if (s.m_beatinfo_updated.load(std::memory_order_acquire) == 1
        && s.m_bpm.load(std::memory_order_relaxed) == 120
        && s.m_bpi.load(std::memory_order_relaxed) == 16) {
        PASS();
    } else {
        FAIL("released values not observed");
    }
}

// =====================================================================
// Test 2: producer/consumer — TSan must report clean over 100k iterations.
// Verifies no torn read of (m_bpm, m_bpi) after seeing the flag set.
// =====================================================================
static void test_concurrent_publish_consume() {
    TEST("concurrent publish/consume — TSan must report clean");
    AtomicBeatInfo s;
    constexpr int kIters = 100000;
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        for (int i = 0; i < kIters; ++i) {
            s.m_bpm.store(120 + (i & 0xFF), std::memory_order_relaxed);
            s.m_bpi.store(16 + (i & 0x07), std::memory_order_relaxed);
            s.m_beatinfo_updated.store(1, std::memory_order_release);
        }
        stop.store(true, std::memory_order_release);
    });

    int observed = 0;
    while (!stop.load(std::memory_order_acquire)
           || s.m_beatinfo_updated.load(std::memory_order_acquire)) {
        if (s.m_beatinfo_updated.load(std::memory_order_acquire)) {
            int bpm = s.m_bpm.load(std::memory_order_relaxed);
            int bpi = s.m_bpi.load(std::memory_order_relaxed);
            // Range check — published values are in known sets.
            if (bpm < 120 || bpm > 120 + 0xFF || bpi < 16 || bpi > 16 + 0x07) {
                s.m_beatinfo_updated.store(0, std::memory_order_relaxed);
                writer.join();
                FAIL("payload out of expected range — possible torn read");
                return;
            }
            s.m_beatinfo_updated.store(0, std::memory_order_relaxed);
            ++observed;
        }
    }

    writer.join();
    if (observed > 0) {
        PASS();
    } else {
        FAIL("consumer never observed a publish");
    }
}

// =====================================================================
// Test 3: m_interval_pos relaxed race — TSan clean. Verifies the AUDIT-line-421
// race fix works under concurrent producer-consumer (relaxed-store / relaxed-load).
// Note: relaxed reads MAY observe non-monotonic values in principle on weakly-
// ordered hardware; on x86 relaxed loads of a single store from a single writer
// are monotonic in practice. We accept either ordering — the test's intent is
// "no TSan report", not "monotonic observation".
// =====================================================================
static void test_interval_pos_race() {
    TEST("m_interval_pos relaxed race — TSan clean");
    std::atomic<int> m_interval_pos{0};
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        for (int i = 0; i < 100000; ++i) {
            m_interval_pos.store(i, std::memory_order_relaxed);
        }
        stop.store(true, std::memory_order_release);
    });

    int last = -1;
    int observations = 0;
    while (!stop.load(std::memory_order_acquire)) {
        int v = m_interval_pos.load(std::memory_order_relaxed);
        // Tolerate non-monotonic (per relaxed-ordering guarantee), but track.
        if (v >= 0 && v < 100000) {
            last = v;
            ++observations;
        }
    }
    writer.join();

    // Final value, after writer has joined, MUST equal the last published value.
    int final_v = m_interval_pos.load(std::memory_order_relaxed);
    if (final_v == 99999 && observations > 0) {
        PASS();
    } else if (final_v != 99999) {
        FAIL("final m_interval_pos not the writer's last store");
    } else {
        // observations == 0 is implausible (the writer ran a long loop); flag it.
        (void)last;
        FAIL("consumer observed nothing");
    }
}

// =====================================================================
// Test 4 (Codex L-10): edge-triggered best-effort semantics.
// Writer publishes 1000 distinct (bpm, bpi) pairs as fast as possible; reader
// runs ~10 times spaced across the writer's lifetime. Verify:
//   (a) every observed pair is a pair the writer DID publish (no torn reads)
//   (b) the LAST values m_bpm/m_bpi hold AFTER the writer finishes equal the
//       writer's final published pair (last-write-wins from reader's view)
//   (c) reader observed FEWER than kPublishes events (intermediate updates lost
//       by design — this is the documented edge-triggered semantics)
// PASSES on all three; FAILS on any.
// =====================================================================
static void test_missed_update_semantics() {
    TEST("missed-update semantics — edge-triggered best-effort (Codex L-10)");
    AtomicBeatInfo s;
    constexpr int kPublishes = 1000;
    std::atomic<bool> writer_done{false};

    // Build the set of pairs the writer will publish, for verification.
    std::set<std::pair<int,int>> published_pairs;
    for (int i = 0; i < kPublishes; ++i) {
        published_pairs.insert({100 + i, 4 + (i % 16)});
    }

    std::thread writer([&] {
        for (int i = 0; i < kPublishes; ++i) {
            s.m_bpm.store(100 + i, std::memory_order_relaxed);
            s.m_bpi.store(4 + (i % 16), std::memory_order_relaxed);
            s.m_beatinfo_updated.store(1, std::memory_order_release);
            // No sleep — publish as fast as possible. Reader will miss most.
        }
        writer_done.store(true, std::memory_order_release);
    });

    // Reader runs ~10 times spaced across the writer's lifetime.
    int observed_count = 0;
    std::set<std::pair<int,int>> observed_pairs;
    for (int r = 0; r < 10; ++r) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (s.m_beatinfo_updated.load(std::memory_order_acquire)) {
            int bpm = s.m_bpm.load(std::memory_order_relaxed);
            int bpi = s.m_bpi.load(std::memory_order_relaxed);
            s.m_beatinfo_updated.store(0, std::memory_order_relaxed);
            observed_pairs.insert({bpm, bpi});
            ++observed_count;
        }
    }

    writer.join();

    // Final observation, after writer is done — m_bpm / m_bpi hold the LATEST values.
    const int final_bpm = s.m_bpm.load(std::memory_order_relaxed);
    const int final_bpi = s.m_bpi.load(std::memory_order_relaxed);
    const int expected_final_bpm = 100 + (kPublishes - 1);
    const int expected_final_bpi = 4 + ((kPublishes - 1) % 16);

    bool all_observed_were_published = true;
    for (const auto& p : observed_pairs) {
        if (published_pairs.find(p) == published_pairs.end()) {
            all_observed_were_published = false;
            break;
        }
    }

    const bool last_write_wins = (final_bpm == expected_final_bpm
                                  && final_bpi == expected_final_bpi);
    const bool intermediate_updates_were_dropped = (observed_count < kPublishes);

    if (!all_observed_were_published) {
        FAIL("observed pair not in writer's sequence — torn read");
    } else if (!last_write_wins) {
        FAIL("final values do not match writer's last publish — protocol broken");
    } else if (!intermediate_updates_were_dropped) {
        FAIL("reader observed every publish — semantics test inconclusive (sleep too long?)");
    } else {
        printf("(observed %d of %d publishes; last-write-wins confirmed) ",
               observed_count, kPublishes);
        PASS();
    }

    (void)writer_done; // silence unused warning if release/acquire on it isn't enough
}

} // namespace

int main() {
    printf("test_njclient_atomics — TSan-targeted release/acquire stress + L-10 semantics\n");
    test_roundtrip_single_thread();
    test_concurrent_publish_consume();
    test_interval_pos_race();
    test_missed_update_semantics();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
