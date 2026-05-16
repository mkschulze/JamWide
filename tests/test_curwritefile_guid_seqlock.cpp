/*
    test_curwritefile_guid_seqlock.cpp — Plan 20-02 Task 2.

    R3 MF1 + R4 H8: per-channel atomic seqlock on Local_Channel for the canonical
    audio_ch0_guid read from the audio thread. TSan-clean by C++ memory model —
    the GUID payload lives in TWO `std::atomic<uint64_t>` halves
    (m_curwritefile_guid_lo / _hi) framed by an even/odd parity counter
    (m_curwritefile_guid_seq). No plain non-atomic byte read from the audio
    thread is permitted; reader/writer never share a non-atomic byte.

    Test A — concurrency stress: 1 writer thread bumps the GUID via the seqlock
    writer at high frequency; 1 reader thread reads via the seqlock reader at
    higher frequency. Every read must either match one of the published 16-byte
    values OR be 16 zero bytes (retry-cap exhaustion). No torn read (a value
    that is NEITHER in the published set NOR zero) is permitted.

    Test B — retry-cap fallback: writer holds the seqlock at odd parity longer
    than the reader's 4-attempt retry cap; reader returns false + 16 zeros.

    Test C — TSan clean by design (informational assertion): the seqlock atomics
    cannot race by C++ memory model. Under `--tsan` (JAMWIDE_TSAN=ON) this test
    runs the stress loop; under non-tsan, it just PASSes with a note.

    Test D — single-threaded sanity: write 16 bytes, read 16 bytes, identical.

    Linked against njclient static library; the test-only Local_Channel
    construction + readGuidSeqlock / writeGuidSeqlock helpers are declared
    JAMWIDE_BUILD_TESTS-gated in njclient.h.
*/

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/njclient.h"
#include "wdl/heapbuf.h"

// RAII wrapper around the opaque test handle (unique_ptr cannot delete an
// incomplete type; pass a custom deleter that calls the public destructor).
namespace {
struct TestHandleDeleter {
    void operator()(NJClient::TestLocalChannelHandle* h) const {
        NJClient::DestroyTestLocalChannelHandle(h);
    }
};
using TestHandlePtr = std::unique_ptr<NJClient::TestLocalChannelHandle,
                                      TestHandleDeleter>;
inline TestHandlePtr makeTestHandle() {
    return TestHandlePtr(NJClient::CreateTestLocalChannelHandle());
}
} // namespace

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

// ----- Test D: single-thread non-contended sanity ------------------------
static void test_seqlock_single_thread_sanity() {
    TEST("write 16 bytes, read 16 bytes, byte-for-byte identical (single thread)");
    auto lc = makeTestHandle();
    if (!lc) { FAIL("CreateTestLocalChannelHandle returned null"); return; }

    unsigned char in[16];
    for (int i = 0; i < 16; ++i) in[i] = (unsigned char)(0x80 + i);
    NJClient::TestWriteGuidSeqlock(*lc, in);

    unsigned char out[16] = {0};
    bool ok = NJClient::TestReadGuidSeqlock(*lc, out);
    if (!ok) { FAIL("readGuidSeqlock returned false in single-thread case"); return; }
    if (memcmp(in, out, 16) != 0) { FAIL("read bytes do not match written bytes"); return; }
    PASS();
}

// ----- Test A: writer/reader concurrency stress --------------------------
static void test_seqlock_concurrency_stress() {
    TEST("writer at 100Hz, reader at 1kHz, 2-sec window: zero torn reads");
    auto lc = makeTestHandle();
    if (!lc) { FAIL("CreateTestLocalChannelHandle returned null"); return; }

    // Capture the set of published values so the reader can verify
    // post-hoc that every observed value matches one of these (or is zero).
    std::mutex published_mu;
    std::vector<std::array<unsigned char,16>> published;
    published.reserve(4096);
    // Initial zero published value (matches construction).
    std::array<unsigned char,16> zero{}; published.push_back(zero);

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> torn_reads{0};
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> retry_cap_reads{0};

    std::thread writer([&]() {
        unsigned char buf[16];
        uint64_t counter = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            counter++;
            for (int i = 0; i < 16; ++i) buf[i] = (unsigned char)((counter >> (i % 8 * 8)) ^ (i * 31));
            NJClient::TestWriteGuidSeqlock(*lc, buf);
            {
                std::lock_guard<std::mutex> g(published_mu);
                std::array<unsigned char,16> snap;
                memcpy(snap.data(), buf, 16);
                published.push_back(snap);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::thread reader([&]() {
        unsigned char out[16];
        while (!stop.load(std::memory_order_relaxed)) {
            bool ok = NJClient::TestReadGuidSeqlock(*lc, out);
            total_reads.fetch_add(1, std::memory_order_relaxed);
            if (!ok) {
                retry_cap_reads.fetch_add(1, std::memory_order_relaxed);
                bool is_zero = true;
                for (int i = 0; i < 16; ++i) if (out[i] != 0) { is_zero = false; break; }
                if (!is_zero) torn_reads.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            // Compare against the published set under the mutex. The reader
            // might observe a value the writer just published but that's not
            // yet in `published` (we publish AFTER the writeGuidSeqlock call);
            // sample again briefly. The seqlock writer publishes the new
            // bytes-then-marks-published in two non-atomic steps relative to
            // this test, so we accept either "in published set" OR "zero"
            // OR "matches the next value the writer is about to publish"
            // (which we re-check on the next iteration).
            bool found = false;
            {
                std::lock_guard<std::mutex> g(published_mu);
                for (auto& s : published) {
                    if (memcmp(s.data(), out, 16) == 0) { found = true; break; }
                }
            }
            // Note: not finding the value MIGHT mean the writer just published
            // but hasn't yet appended to `published` — retry once with brief
            // pause. If still not found, it's a torn read.
            if (!found) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard<std::mutex> g(published_mu);
                for (auto& s : published) {
                    if (memcmp(s.data(), out, 16) == 0) { found = true; break; }
                }
                if (!found) torn_reads.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(1000));  // 1 kHz
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop.store(true, std::memory_order_relaxed);
    writer.join();
    reader.join();

    printf("[total=%llu retries=%llu torn=%llu] ",
           (unsigned long long)total_reads.load(),
           (unsigned long long)retry_cap_reads.load(),
           (unsigned long long)torn_reads.load());

    if (torn_reads.load() != 0) {
        FAIL("torn reads detected — atomic-halves seqlock invariant broken");
        return;
    }
    if (total_reads.load() < 1000) {
        FAIL("reader did not run enough iterations — check sleep budget");
        return;
    }
    PASS();
}

// ----- Test B: retry-cap fallback --------------------------------------
static void test_seqlock_retry_cap_fallback() {
    TEST("writer holds odd-parity > retry cap → reader returns false + zero out");
    auto lc = makeTestHandle();
    if (!lc) { FAIL("CreateTestLocalChannelHandle returned null"); return; }

    // Publish a known value first.
    unsigned char published[16];
    for (int i = 0; i < 16; ++i) published[i] = (unsigned char)(0xA0 + i);
    NJClient::TestWriteGuidSeqlock(*lc, published);

    // Now simulate a writer that's stuck at odd parity. Use the test helper
    // to bump the seq counter to odd parity once (without completing the
    // matching even-parity bump). This causes the reader to spin to retry cap
    // each attempt.
    NJClient::TestForceOddParityForTest(*lc);

    unsigned char out[16];
    for (int i = 0; i < 16; ++i) out[i] = 0xFF;  // sentinel
    bool ok = NJClient::TestReadGuidSeqlock(*lc, out);
    if (ok) { FAIL("reader returned true while seqlock held odd parity"); return; }
    for (int i = 0; i < 16; ++i) {
        if (out[i] != 0) { FAIL("retry-cap fallback did not zero-fill output"); return; }
    }

    // Restore even parity for cleanup.
    NJClient::TestRestoreEvenParityForTest(*lc);
    PASS();
}

// ----- Test C: TSan informational (the design is race-free by construction) --
static void test_seqlock_tsan_design() {
    TEST("seqlock atomics are TSan-clean by C++ memory model (informational)");
#ifdef __SANITIZE_THREAD__
    printf("(TSan build detected) ");
#elif defined(__has_feature) && __has_feature(thread_sanitizer)
    printf("(TSan build detected) ");
#else
    printf("(non-TSan build — skipping race-stress) ");
#endif
    // Sanity: drive a brief concurrency loop under whichever build flags;
    // the atomic-halves design produces no race regardless. If TSan is on
    // it asserts no race; if not, it just runs to completion.
    auto lc = makeTestHandle();
    if (!lc) { FAIL("CreateTestLocalChannelHandle returned null"); return; }

    std::atomic<bool> stop{false};
    std::thread w([&]() {
        unsigned char buf[16] = {0};
        uint64_t k = 0;
        while (!stop.load()) {
            k++;
            for (int i = 0; i < 16; ++i) buf[i] = (unsigned char)((k >> (i * 4)) & 0xFF);
            NJClient::TestWriteGuidSeqlock(*lc, buf);
        }
    });
    std::thread r([&]() {
        unsigned char buf[16];
        while (!stop.load()) {
            (void)NJClient::TestReadGuidSeqlock(*lc, buf);
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);
    w.join();
    r.join();
    PASS();
}

int main() {
    printf("Running test_curwritefile_guid_seqlock...\n");
    test_seqlock_single_thread_sanity();
    test_seqlock_concurrency_stress();
    test_seqlock_retry_cap_fallback();
    test_seqlock_tsan_design();
    printf("Tests passed: %d/%d\n", tests_passed, tests_run);
    return (tests_run == tests_passed) ? 0 : 1;
}
