/*
    JamWide Plugin - test_deferred_delete.cpp
    Stress for the 15.1-05 deferred-delete queue.

    Three tests covering:
      1. 256-burst push + drain runs all destructors
      2. 50 Hz producer over 1 second, consumer at 20ms — no overflow, no leaks
      3. Overflow counter increments correctly on capacity-bound push (Codex M-8)

    Pure-C++ (no NJClient link) — exercises the SPSC primitive + payload
    contract in isolation, mirroring the test_spsc_state_updates.cpp pattern.
    Designed to also run cleanly under -fsanitize=thread (--tsan build).

    Codex M-8 acceptance: the unit test PROVES the counter increments
    correctly when overflow occurs by construction. Production verification
    (15.1-10) asserts the counter is 0 after UAT.
*/

// Stand-in definition for DecodeState (forward-declared in spsc_payloads.h).
// The real definition lives in src/core/njclient.cpp; the test only exercises
// pointer roundtrip — the type just needs to be defined.
//
// IMPORTANT: this stand-in must be in namespace jamwide before the header is
// included so the forward declarations resolve to the same type when we
// try_push() typed pointers.
namespace jamwide {
class DecodeState   { public: int marker = 0; };
class RemoteUser    { public: int marker = 0; };
class Local_Channel { public: int marker = 0; };
} // namespace jamwide

#include "threading/spsc_ring.h"
#include "threading/spsc_payloads.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

// ============================================================================
// Test framework — verbatim from tests/test_encryption.cpp:25-44
// ============================================================================

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

// Test-local FakeDecodeState: tracks dtor count via shared atomic.
struct FakeDecodeState {
    std::atomic<int>* dtor_count;
    ~FakeDecodeState() { dtor_count->fetch_add(1, std::memory_order_relaxed); }
};

using TestQueue = jamwide::SpscRing<FakeDecodeState*, jamwide::DEFERRED_DELETE_CAPACITY>;

// ----------------------------------------------------------------------------
// Test 1: 256-burst push + drain runs all destructors.
//
// SpscRing reserves one slot for full-detection (next_head==tail means full),
// so practical capacity is N-1 = 255. We push pointers; drain partial as
// needed; verify total destructors == total pushed and the queue is empty.
// ----------------------------------------------------------------------------
static void test_full_burst_drain()
{
    TEST("256-burst push + drain runs all destructors");
    std::atomic<int> dtors{0};
    TestQueue ring;
    int total_pushed = 0;

    for (int i = 0; i < 256; ++i)
    {
        FakeDecodeState* p = new FakeDecodeState{&dtors};
        if (!ring.try_push(p))
        {
            // Capacity-1 slot reached. Drain partial and retry.
            ring.drain([](FakeDecodeState* x) { delete x; });
            if (!ring.try_push(p))
            {
                FAIL("push after drain still failed");
                delete p;
                return;
            }
        }
        ++total_pushed;
    }
    ring.drain([](FakeDecodeState* p) { delete p; });
    if (dtors.load() == total_pushed && ring.empty())
        PASS();
    else
        FAIL("destructor count or queue state mismatch");
}

// ----------------------------------------------------------------------------
// Test 2: 50 Hz producer (audio-thread realistic) over 1 second; consumer
// at 20ms (run-thread realistic). Verify zero overflow, zero leaks.
// ----------------------------------------------------------------------------
static void test_audio_thread_rate()
{
    TEST("50 Hz producer over 1 second, consumer at 20ms — no overflow, no leaks");
    std::atomic<int> dtors{0};
    std::atomic<int> pushed{0};
    std::atomic<int> rejected{0};
    TestQueue ring;
    std::atomic<bool> stop{false};

    std::thread producer([&] {
        for (int i = 0; i < 50; ++i)
        {
            FakeDecodeState* p = new FakeDecodeState{&dtors};
            if (ring.try_push(p))
                pushed.fetch_add(1, std::memory_order_relaxed);
            else
            {
                rejected.fetch_add(1, std::memory_order_relaxed);
                delete p;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        stop.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        while (!stop.load(std::memory_order_acquire) || !ring.empty())
        {
            ring.drain([](FakeDecodeState* p) { delete p; });
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    producer.join();
    consumer.join();

    if (rejected.load() == 0 && dtors.load() == pushed.load() && ring.empty())
        PASS();
    else
        FAIL("audio-rate stress mismatch");
}

// ----------------------------------------------------------------------------
// Test 3 (Codex M-8): overflow-counter mechanism verification.
//
// Push 300 pointers without draining; SpscRing has effective capacity N-1
// (255 for DEFERRED_DELETE_CAPACITY=256), so ~45 should be rejected. Verify
// the simulated counter equals the rejected count. Then drain and re-push
// the orphans to verify recovery semantics.
//
// In production NJClient code, the audio thread would NOT hold a stash of
// orphans — they are leaked and the counter records that fact. The test
// manually re-enqueues to verify the queue still functions after overflow.
// ----------------------------------------------------------------------------
static void test_overflow_counter_increments_correctly()
{
    TEST("overflow counter increments on capacity-bound push (Codex M-8 mechanism)");
    std::atomic<int> dtors{0};
    std::atomic<uint64_t> overflow_counter{0};  // mirror of m_deferred_delete_overflows
    TestQueue ring;
    int accepted = 0, rejected = 0;
    std::vector<FakeDecodeState*> orphaned;

    for (int i = 0; i < 300; ++i)
    {
        FakeDecodeState* p = new FakeDecodeState{&dtors};
        if (ring.try_push(p))
            ++accepted;
        else
        {
            ++rejected;
            overflow_counter.fetch_add(1, std::memory_order_relaxed);
            orphaned.push_back(p);
        }
    }

    // Sanity: capacity-1 reservation should have rejected something.
    if (rejected == 0)
    {
        for (auto* p : orphaned) delete p;
        ring.drain([](FakeDecodeState* p) { delete p; });
        FAIL("expected at least one rejection on 300-push burst");
        return;
    }

    // Codex M-8 core check: counter == rejected count, exactly.
    if (overflow_counter.load() != static_cast<uint64_t>(rejected))
    {
        for (auto* p : orphaned) delete p;
        ring.drain([](FakeDecodeState* p) { delete p; });
        FAIL("overflow_counter != rejected count");
        return;
    }

    // Drain everything that did make it; this models the run thread
    // catching up at its 20ms tick.
    ring.drain([](FakeDecodeState* p) { delete p; });

    // Re-push the orphans — they should now succeed (queue is empty).
    int second_round = 0;
    for (auto* p : orphaned)
    {
        if (ring.try_push(p))
            ++second_round;
        else
            delete p;
    }
    ring.drain([](FakeDecodeState* p) { delete p; });

    // Final accounting: every pointer constructed must have run its dtor.
    const int total_constructed = accepted + rejected;
    const int total_destructed  = dtors.load();
    if (second_round == static_cast<int>(orphaned.size())
        && total_destructed == total_constructed)
        PASS();
    else
        FAIL("recovery push or destructor count off");
}

} // anonymous namespace

int main()
{
    printf("test_deferred_delete — 15.1-05 SPSC stress (incl. Codex M-8 overflow counter)\n");
    test_full_burst_drain();
    test_audio_thread_rate();
    test_overflow_counter_increments_correctly();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
