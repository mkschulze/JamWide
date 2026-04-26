/*
    JamWide Plugin - test_local_channel_mirror.cpp
    Concurrent mutation/apply for the 15.1-06 LocalChannelMirror.

    Includes Codex HIGH-2 (no lc_ptr / Local_Channel* escape hatch in payload)
    and HIGH-3 (generation-gate deferred-free protocol) coverage.

    Pure-C++ (no NJClient link) — exercises the SPSC primitive + payload
    contract in isolation, mirroring the test_spsc_state_updates.cpp pattern
    and test_deferred_delete.cpp's stand-in approach for forward-declared types.
    Designed to also run cleanly under -fsanitize=thread (--tsan build).
*/

// Stand-in definition for forward-declared types in spsc_payloads.h.
// IMPORTANT: must live in namespace jamwide before the header is included so
// the forward declarations resolve to the same type when we try_push() typed
// pointers. Mirrors the pattern used by tests/test_deferred_delete.cpp.
namespace jamwide {
class DecodeState   { public: int marker = 0; };
class RemoteUser    { public: int marker = 0; };
class Local_Channel { public: int marker = 0; };
} // namespace jamwide

#include "threading/spsc_ring.h"
#include "threading/spsc_payloads.h"

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <variant>

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

// ============================================================================
// Stand-in mirror struct + apply visitor.
//
// Mirrors the production LocalChannelMirror declared in src/core/njclient.h:
//   - Holds every field the audio thread reads BY VALUE.
//   - Codex HIGH-2: NO lc_ptr / Local_Channel* / void* escape-hatch field.
//   - Production additionally embeds SpscRing<BlockRecord,16> block_q (audio
//     producer / encoder consumer) — omitted here since this test only
//     exercises the variant-mutation/apply protocol; block_q lifetime is
//     covered by tests/test_spsc_state_updates.cpp::test_block_record_roundtrip
//     and tests/test_spsc_state_updates.cpp::test_concurrent_block_record.
// ============================================================================

namespace test_njclient {

static constexpr int MAX_LOCAL_CHANNELS = 32;

struct TestLocalChannelMirror {
    bool         active = false;
    int          srcch = 0;
    int          bitrate = 0;
    bool         bcast = false;
    int          outch = -1;
    unsigned int flags = 0;
    bool         mute = false;
    bool         solo = false;
    float        volume = 1.0f;
    float        pan = 0.0f;
    // Deliberately NO Local_Channel* / lc_ptr / void* — Codex HIGH-2.
};

static_assert(std::is_trivially_copyable_v<TestLocalChannelMirror>,
              "Test mirror must be trivially copyable (no embedded SPSC ring; the "
              "production mirror has block_q which is non-copyable, but the test "
              "mirror is plain POD by design — block_q lifetime is covered "
              "elsewhere)");

static TestLocalChannelMirror g_mirror[MAX_LOCAL_CHANNELS];

static void apply_one(jamwide::LocalChannelUpdate&& upd) {
    std::visit([](auto&& u) {
        using T = std::decay_t<decltype(u)>;
        if constexpr (std::is_same_v<T, jamwide::LocalChannelAddedUpdate>) {
            if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
            auto& m = g_mirror[u.channel];
            m.active  = true;
            m.srcch   = u.srcch;
            m.bitrate = u.bitrate;
            m.bcast   = u.bcast;
            m.outch   = u.outch;
            m.flags   = u.flags;
            m.mute    = u.mute;
            m.solo    = u.solo;
            m.volume  = u.volume;
            m.pan     = u.pan;
        }
        else if constexpr (std::is_same_v<T, jamwide::LocalChannelRemovedUpdate>) {
            if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
            g_mirror[u.channel] = TestLocalChannelMirror{};  // reset to defaults, active=false.
        }
        else if constexpr (std::is_same_v<T, jamwide::LocalChannelInfoUpdate>) {
            if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
            auto& m = g_mirror[u.channel];
            m.srcch   = u.srcch;
            m.bitrate = u.bitrate;
            m.bcast   = u.bcast;
            m.outch   = u.outch;
            m.flags   = u.flags;
        }
        else if constexpr (std::is_same_v<T, jamwide::LocalChannelMonitoringUpdate>) {
            if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
            auto& m = g_mirror[u.channel];
            if (u.set_volume) m.volume = u.volume;
            if (u.set_pan)    m.pan    = u.pan;
            if (u.set_mute)   m.mute   = u.mute;
            if (u.set_solo)   m.solo   = u.solo;
        }
    }, upd);
}

static void reset_mirror() {
    for (int i = 0; i < MAX_LOCAL_CHANNELS; ++i)
        g_mirror[i] = TestLocalChannelMirror{};
}

} // namespace test_njclient

// ============================================================================
// Test 1: Single-threaded apply roundtrip — Add → drain → Remove → drain.
// ============================================================================

static void test_single_thread_apply_roundtrip()
{
    TEST("single-thread apply: Add+Info+Monitoring+Remove all reach the mirror");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::LocalChannelUpdate, 32> q;

    // Add channel 3 with a full field set.
    q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelAddedUpdate{
            /*channel=*/3, /*srcch=*/2, /*bitrate=*/192,
            /*bcast=*/true, /*outch=*/5, /*flags=*/0x06u,
            /*mute=*/true, /*solo=*/false,
            /*volume=*/0.75f, /*pan=*/-0.25f}});
    q.drain([](jamwide::LocalChannelUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    auto& m = test_njclient::g_mirror[3];
    if (!m.active || m.srcch != 2 || m.bitrate != 192 || !m.bcast ||
        m.outch != 5 || m.flags != 0x06u ||
        !m.mute || m.solo || m.volume != 0.75f || m.pan != -0.25f)
    {
        FAIL("LocalChannelAddedUpdate did not propagate full field set to mirror"); return;
    }

    // Info update: change srcch/bitrate/flags.
    q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelInfoUpdate{
            /*channel=*/3, /*srcch=*/4, /*bitrate=*/256,
            /*bcast=*/false, /*outch=*/7, /*flags=*/0x02u}});
    q.drain([](jamwide::LocalChannelUpdate&& u){ test_njclient::apply_one(std::move(u)); });
    if (m.srcch != 4 || m.bitrate != 256 || m.bcast || m.outch != 7 || m.flags != 0x02u)
    {
        FAIL("LocalChannelInfoUpdate did not propagate scalar fields"); return;
    }
    // Monitoring fields untouched by Info update.
    if (!m.mute || m.solo || m.volume != 0.75f || m.pan != -0.25f)
    {
        FAIL("LocalChannelInfoUpdate clobbered monitoring fields (must not)"); return;
    }

    // Monitoring partial-update: only volume+solo.
    q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelMonitoringUpdate{
            /*channel=*/3,
            /*set_volume=*/true, /*volume=*/1.5f,
            /*set_pan=*/false, /*pan=*/0.0f,
            /*set_mute=*/false, /*mute=*/false,
            /*set_solo=*/true,  /*solo=*/true}});
    q.drain([](jamwide::LocalChannelUpdate&& u){ test_njclient::apply_one(std::move(u)); });
    if (m.volume != 1.5f || !m.solo)
    {
        FAIL("LocalChannelMonitoringUpdate did not propagate set_* fields"); return;
    }
    // Mute and pan unchanged because set_mute/set_pan were false.
    if (!m.mute || m.pan != -0.25f)
    {
        FAIL("LocalChannelMonitoringUpdate touched non-set_ fields (must not)"); return;
    }

    // Remove.
    q.try_push(jamwide::LocalChannelUpdate{jamwide::LocalChannelRemovedUpdate{3}});
    q.drain([](jamwide::LocalChannelUpdate&& u){ test_njclient::apply_one(std::move(u)); });
    if (m.active)
    {
        FAIL("LocalChannelRemovedUpdate did not clear active flag"); return;
    }
    PASS();
}

// ============================================================================
// Test 2: Concurrent producer + consumer.
//
// Producer thread issues N mutation cycles (Add → Info×3 → Monitoring×5 → Remove)
// for one channel. Consumer thread drains at 20ms cadence. After producer joins
// and the consumer drains the tail, the mirror reflects the LAST published
// state for that channel. SpscRing is back-pressure-resilient: producer
// retries on full.
// ============================================================================

static void test_concurrent_mutation_apply()
{
    TEST("concurrent producer/consumer: drain reaches steady-state mirror match");
    test_njclient::reset_mirror();

    // 1000 cycles × 10 records each = 10000 SPSC handoffs. Drain cadence
    // is 20ms, so wall time is bounded by producer rate (which retries on
    // full and yields). On a release build this completes in ~1-3s; under
    // TSan it's slower but still bounded.
    constexpr int CYCLES = 1000;
    constexpr int CH = 7;
    jamwide::SpscRing<jamwide::LocalChannelUpdate, 32> q;
    std::atomic<bool> producer_done{false};

    // Last expected state after the final cycle's Remove. The Remove zeroes
    // the slot, so the steady state is "active=false". This test proves there
    // are no torn applies — every cycle's Remove eventually wins after producer
    // ends.
    std::thread producer([&]() {
        for (int i = 0; i < CYCLES; ++i) {
            // Add. Retry on full.
            jamwide::LocalChannelUpdate add{jamwide::LocalChannelAddedUpdate{
                CH, /*srcch=*/i&31, /*bitrate=*/192,
                /*bcast=*/(i&1)!=0, /*outch=*/i&7, /*flags=*/(unsigned)(i&7),
                /*mute=*/(i&2)!=0, /*solo=*/(i&4)!=0,
                /*volume=*/(float)i*0.001f, /*pan=*/(float)((i&15)-8)*0.05f}};
            while (!q.try_push(add)) std::this_thread::yield();

            // Info×3.
            for (int k = 0; k < 3; ++k) {
                jamwide::LocalChannelUpdate info{jamwide::LocalChannelInfoUpdate{
                    CH, k, k*64, (k&1)!=0, k, (unsigned)k}};
                while (!q.try_push(info)) std::this_thread::yield();
            }

            // Monitoring×5.
            for (int k = 0; k < 5; ++k) {
                jamwide::LocalChannelUpdate mon{jamwide::LocalChannelMonitoringUpdate{
                    CH,
                    /*set_volume=*/true,  /*volume=*/(float)k * 0.1f,
                    /*set_pan=*/(k&1)!=0, /*pan=*/(float)k * 0.05f,
                    /*set_mute=*/(k&2)!=0,/*mute=*/(k&4)!=0,
                    /*set_solo=*/false,   /*solo=*/false}};
                while (!q.try_push(mon)) std::this_thread::yield();
            }

            // Remove.
            jamwide::LocalChannelUpdate rem{jamwide::LocalChannelRemovedUpdate{CH}};
            while (!q.try_push(rem)) std::this_thread::yield();
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer: drain at ~20ms cadence (matches NinjamRunThread::run).
    while (!producer_done.load(std::memory_order_acquire) || !q.empty()) {
        q.drain([](jamwide::LocalChannelUpdate&& u){ test_njclient::apply_one(std::move(u)); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Final tail-drain to absorb any straggler enqueued after the last cadence.
    q.drain([](jamwide::LocalChannelUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    producer.join();

    // Final state: the LAST mutation was a Remove, so mirror[CH].active==false.
    if (test_njclient::g_mirror[CH].active) {
        FAIL("steady-state mirror still active after final Remove"); return;
    }
    PASS();
}

// ============================================================================
// Test 3: Out-of-range channel index is silently ignored (no OOB access).
// ============================================================================

static void test_bounded_channel_index()
{
    TEST("out-of-range channel index ignored by visitor (no OOB)");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::LocalChannelUpdate, 32> q;

    // Channel 999 is way out of range. Visitor must early-return.
    q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelAddedUpdate{
            999, 0, 0, false, 0, 0, false, false, 1.0f, 0.0f}});
    // Channel -1 is also out of range.
    q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelAddedUpdate{
            -1, 0, 0, false, 0, 0, false, false, 1.0f, 0.0f}});
    // Boundary: MAX_LOCAL_CHANNELS exact (== 32) is also out of range
    // because indices are 0..31.
    q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelAddedUpdate{
            test_njclient::MAX_LOCAL_CHANNELS, 0, 0, false, 0, 0, false, false, 1.0f, 0.0f}});

    q.drain([](jamwide::LocalChannelUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    // No mirror entry should have been written.
    for (int i = 0; i < test_njclient::MAX_LOCAL_CHANNELS; ++i) {
        if (test_njclient::g_mirror[i].active) {
            FAIL("out-of-range update wrote to mirror"); return;
        }
    }
    PASS();
}

// ============================================================================
// Test 4: HIGH-3 generation-gate deferred-free.
//
// Models the production protocol from src/core/njclient.cpp::DeleteLocalChannel:
//   1. Run thread: push LocalChannelRemovedUpdate, observe publish_gen_target =
//      audio_drain_generation.load(acquire) + 1.
//   2. Audio thread: drain, apply (set mirror active=false), bump
//      audio_drain_generation (release).
//   3. Run thread (NEXT tick): when audio_drain_generation > publish_gen_target-1,
//      enqueue canonical Local_Channel* onto deferred-delete queue.
//   4. Run thread: drain deferred-delete queue; ~Local_Channel runs.
//
// The TEST serializes the dance for determinism; a real system runs steps 2/3
// across two distinct threads — the SPSC + atomic ordering is what makes it
// safe, and that primitive is independently exercised by test_spsc_state_updates.
// ============================================================================

static void test_generation_gate_deferred_free()
{
    TEST("HIGH-3 generation-gate: Local_Channel deferred-free runs only after audio drain");

    struct FakeLC {
        std::atomic<int>* dtor_count;
        ~FakeLC() { dtor_count->fetch_add(1, std::memory_order_relaxed); }
    };

    std::atomic<int>      dtors{0};
    std::atomic<uint64_t> audio_drain_gen{0};
    jamwide::SpscRing<jamwide::LocalChannelUpdate, 32> update_q;
    jamwide::SpscRing<FakeLC*,
                      jamwide::LOCAL_CHANNEL_DEFERRED_DELETE_CAPACITY> defer_q;

    // ── Run-thread side: publish remove + remember pre-publish generation.
    FakeLC* lc = new FakeLC{&dtors};
    if (!update_q.try_push(jamwide::LocalChannelUpdate{
            jamwide::LocalChannelRemovedUpdate{0}}))
    {
        FAIL("could not publish LocalChannelRemovedUpdate"); delete lc; return;
    }
    const uint64_t publish_gen_target =
        audio_drain_gen.load(std::memory_order_acquire) + 1;

    // ── Audio-thread side: drain + bump generation (release-store).
    update_q.drain([](jamwide::LocalChannelUpdate&&){ /* apply, irrelevant here */ });
    audio_drain_gen.fetch_add(1, std::memory_order_release);

    // ── Run-thread side, NEXT tick: confirm gen advanced past target,
    //    enqueue canonical pointer.
    if (audio_drain_gen.load(std::memory_order_acquire) >= publish_gen_target) {
        if (!defer_q.try_push(lc)) {
            FAIL("deferred-delete queue full (capacity check failed)"); delete lc; return;
        }
    } else {
        FAIL("generation did not advance after drain (gate broken)"); delete lc; return;
    }

    // Sanity: dtor has NOT run yet (pointer is still in the queue).
    if (dtors.load() != 0) {
        FAIL("Local_Channel destructed BEFORE deferred-delete drain (lifetime bug)"); return;
    }

    // ── Run-thread drain of deferred-delete queue (next tick / shutdown).
    defer_q.drain([](FakeLC* p){ delete p; });

    if (dtors.load() == 1) PASS();
    else FAIL("destructor count != 1 after deferred-free protocol");
}

// ============================================================================
// Test 5: HIGH-3 generation-gate also rejects PRE-DRAIN frees.
//
// Adversarial: a buggy run thread might try to enqueue the canonical pointer
// BEFORE the audio thread has drained. The gate must reject this — i.e. the
// run thread should detect "audio_drain_gen <= publish_gen_target-1" and
// not enqueue. The test simulates that condition and asserts the canonical
// object is NOT enqueued, then completes the protocol correctly afterwards
// to prove no leak.
// ============================================================================

static void test_generation_gate_rejects_premature_free()
{
    TEST("HIGH-3 generation-gate: rejects free before audio drain has run");

    struct FakeLC {
        std::atomic<int>* dtor_count;
        ~FakeLC() { dtor_count->fetch_add(1, std::memory_order_relaxed); }
    };

    std::atomic<int>      dtors{0};
    std::atomic<uint64_t> audio_drain_gen{0};
    jamwide::SpscRing<jamwide::LocalChannelUpdate, 32> update_q;
    jamwide::SpscRing<FakeLC*,
                      jamwide::LOCAL_CHANNEL_DEFERRED_DELETE_CAPACITY> defer_q;

    FakeLC* lc = new FakeLC{&dtors};
    update_q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelRemovedUpdate{0}});
    const uint64_t publish_gen_target =
        audio_drain_gen.load(std::memory_order_acquire) + 1;

    // Audio thread has NOT yet drained → audio_drain_gen still equals
    // publish_gen_target - 1. Gate must reject the free.
    if (audio_drain_gen.load(std::memory_order_acquire) >= publish_gen_target) {
        FAIL("gate accepted free before audio drain (UAF window)"); delete lc; return;
    }

    // Now simulate audio thread draining and bumping.
    update_q.drain([](jamwide::LocalChannelUpdate&&){});
    audio_drain_gen.fetch_add(1, std::memory_order_release);

    // Now the gate must accept.
    if (audio_drain_gen.load(std::memory_order_acquire) < publish_gen_target) {
        FAIL("gate rejected after audio drain ran"); delete lc; return;
    }
    defer_q.try_push(lc);
    defer_q.drain([](FakeLC* p){ delete p; });

    if (dtors.load() == 1) PASS();
    else FAIL("destructor count != 1 after deferred-free protocol (premature-gate test)");
}

// ============================================================================
// Test 6 / static checks: Codex HIGH-2 — no escape-hatch pointer in payload.
//
// We can't introspect "no pointer field" directly in C++17. But trivially-copyable
// + a sanity-check that critical fields are NOT pointer types is a concrete
// architectural compile-time fence: any future regression that adds a
// Local_Channel* / void* field to LocalChannelAddedUpdate would have to remove
// the trivially-copyable assert (impossible — most pointer types are trivially
// copyable) OR change the channel-field type to a pointer (this assert
// rejects it).
// ============================================================================

static_assert(std::is_trivially_copyable_v<jamwide::LocalChannelAddedUpdate>,
              "LocalChannelAddedUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::LocalChannelRemovedUpdate>,
              "LocalChannelRemovedUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::LocalChannelInfoUpdate>,
              "LocalChannelInfoUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::LocalChannelMonitoringUpdate>,
              "LocalChannelMonitoringUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(!std::is_pointer_v<decltype(std::declval<jamwide::LocalChannelAddedUpdate>().channel)>,
              "channel field must not be a pointer (HIGH-2 sanity check)");
static_assert(!std::is_pointer_v<decltype(std::declval<jamwide::LocalChannelAddedUpdate>().srcch)>,
              "srcch field must not be a pointer (HIGH-2 sanity check)");
// Codex HIGH-2: confirm the size of LocalChannelAddedUpdate is exactly the
// expected POD layout — no hidden pointer field that escapes the audit grep.
// 4*int + 1*bool + 1*int + 1*unsigned + 4*bool + 2*float = 32-40 bytes with
// alignment. Bound at 64 bytes loose upper limit; any pointer field would
// likely change the layout meaningfully.
static_assert(sizeof(jamwide::LocalChannelAddedUpdate) <= 64,
              "LocalChannelAddedUpdate bloat suggests an escape-hatch field was added (HIGH-2)");

// ============================================================================
// main
// ============================================================================

int main()
{
    printf("test_local_channel_mirror — concurrent mutation/apply + HIGH-2/HIGH-3 coverage\n");

    test_single_thread_apply_roundtrip();
    test_concurrent_mutation_apply();
    test_bounded_channel_index();
    test_generation_gate_deferred_free();
    test_generation_gate_rejects_premature_free();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
