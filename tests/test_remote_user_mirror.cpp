/*
    JamWide Plugin - test_remote_user_mirror.cpp
    Concurrent peer-churn for the 15.1-07a RemoteUserMirror.

    Includes Codex HIGH-2 (no user_ptr / RemoteUser* escape hatch in payload)
    and HIGH-3 (generation-gate deferred-free protocol) coverage.

    Pure-C++ (no NJClient link) — exercises the SPSC primitive + payload
    contract in isolation, mirroring the test_local_channel_mirror.cpp pattern
    and test_deferred_delete.cpp's stand-in approach for forward-declared types.
    Designed to also run cleanly under -fsanitize=thread (--tsan build).
*/

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <variant>

// Stand-in definitions for forward-declared types in spsc_payloads.h.
// IMPORTANT: must live in namespace jamwide before the header is included so
// the forward declarations resolve to the same type when we try_push() typed
// pointers. Mirrors the pattern used by tests/test_local_channel_mirror.cpp.
namespace jamwide {
class DecodeState   { public: int marker = 0; };
class RemoteUser    {
public:
    int marker = 0;
    std::atomic<int>* dtor_count = nullptr;
    ~RemoteUser() { if (dtor_count) dtor_count->fetch_add(1, std::memory_order_relaxed); }
};
class Local_Channel { public: int marker = 0; };
} // namespace jamwide

#include "threading/spsc_ring.h"
#include "threading/spsc_payloads.h"

// ============================================================================
// Test framework — verbatim from tests/test_local_channel_mirror.cpp
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
// Mirrors the production RemoteUserMirror to be defined in src/core/njclient.h:
//   - Holds every field the audio thread reads BY VALUE.
//   - Codex HIGH-2: NO user_ptr / RemoteUser* / void* escape-hatch field.
//   - DecodeState* fields are AUDIO-THREAD-OWNED once published via
//     PeerNextDsUpdate (ownership transfer, NOT a back-reference).
// ============================================================================

namespace test_njclient {

static constexpr int MAX_PEERS = 64;
static constexpr int MAX_USER_CHANNELS = 32;

struct TestRemoteUserChannelMirror {
    bool         present = false;
    bool         muted = false;
    bool         solo = false;
    float        volume = 1.0f;
    float        pan = 0.0f;
    int          out_chan_index = 0;       // 2026-05-02 orphan-fields fix
    unsigned int flags = 0;                // 2026-05-02 orphan-fields fix
    unsigned int codec_fourcc = 0;
    jamwide::DecodeState* ds = nullptr;
    jamwide::DecodeState* next_ds[2] = {nullptr, nullptr};
    // Deliberately NO RemoteUser_Channel* — Codex HIGH-2.
};

struct TestRemoteUserMirror {
    bool active = false;
    int  user_index = 0;
    int  submask = 0;
    int  chanpresentmask = 0;
    int  mutedmask = 0;
    int  solomask = 0;
    bool muted = false;
    float volume = 1.0f;
    float pan = 0.0f;
    TestRemoteUserChannelMirror chans[MAX_USER_CHANNELS];
    // Deliberately NO RemoteUser* / user_ptr / void* — Codex HIGH-2.
};

static TestRemoteUserMirror g_mirror[MAX_PEERS];

static void apply_one(jamwide::RemoteUserUpdate&& upd) {
    std::visit([](auto&& u) {
        using T = std::decay_t<decltype(u)>;
        if constexpr (std::is_same_v<T, jamwide::PeerAddedUpdate>) {
            if (u.slot < 0 || u.slot >= MAX_PEERS) return;
            auto& m = g_mirror[u.slot];
            m.active = true;
            m.user_index = u.user_index;
        }
        else if constexpr (std::is_same_v<T, jamwide::PeerRemovedUpdate>) {
            if (u.slot < 0 || u.slot >= MAX_PEERS) return;
            auto& m = g_mirror[u.slot];
            m.active = false;
            // Reset everything (including any in-flight DecodeState pointers).
            // In production these would be defer-deleted; here we just leak
            // them since they're stand-in objects we don't actually delete.
            for (int ch = 0; ch < MAX_USER_CHANNELS; ++ch) {
                m.chans[ch] = TestRemoteUserChannelMirror{};
            }
            m.user_index = 0;
            m.submask = 0; m.chanpresentmask = 0;
            m.mutedmask = 0; m.solomask = 0;
            m.muted = false; m.volume = 1.0f; m.pan = 0.0f;
        }
        else if constexpr (std::is_same_v<T, jamwide::PeerChannelMaskUpdate>) {
            if (u.slot < 0 || u.slot >= MAX_PEERS) return;
            auto& m = g_mirror[u.slot];
            m.submask = u.submask;
            m.chanpresentmask = u.chanpresentmask;
            m.mutedmask = u.mutedmask;
            m.solomask = u.solomask;
            for (int ch = 0; ch < MAX_USER_CHANNELS; ++ch) {
                m.chans[ch].present = (u.chanpresentmask & (1u << ch)) != 0;
                m.chans[ch].muted   = (u.mutedmask        & (1u << ch)) != 0;
                m.chans[ch].solo    = (u.solomask         & (1u << ch)) != 0;
            }
        }
        else if constexpr (std::is_same_v<T, jamwide::PeerVolPanUpdate>) {
            if (u.slot < 0 || u.slot >= MAX_PEERS) return;
            auto& m = g_mirror[u.slot];
            m.muted = u.muted; m.volume = u.volume; m.pan = u.pan;
        }
        else if constexpr (std::is_same_v<T, jamwide::PeerNextDsUpdate>) {
            if (u.slot < 0 || u.slot >= MAX_PEERS) return;
            if (u.channel < 0 || u.channel >= MAX_USER_CHANNELS) return;
            if (u.slot_idx < 0 || u.slot_idx > 1) return;
            g_mirror[u.slot].chans[u.channel].next_ds[u.slot_idx] = u.ds;
        }
        else if constexpr (std::is_same_v<T, jamwide::PeerCodecSwapUpdate>) {
            if (u.slot < 0 || u.slot >= MAX_PEERS) return;
            if (u.channel < 0 || u.channel >= MAX_USER_CHANNELS) return;
            g_mirror[u.slot].chans[u.channel].codec_fourcc = u.new_fourcc;
        }
        else if constexpr (std::is_same_v<T, jamwide::PeerChannelInfoUpdate>) {
            // 2026-05-02 RemoteUserMirror orphan-fields fix — production
            // parity with njclient.cpp drainRemoteUserUpdates apply visitor.
            if (u.slot < 0 || u.slot >= MAX_PEERS) return;
            if (u.channel < 0 || u.channel >= MAX_USER_CHANNELS) return;
            auto& chan = g_mirror[u.slot].chans[u.channel];
            chan.flags          = u.flags;
            chan.volume         = u.volume;
            chan.pan            = u.pan;
            chan.out_chan_index = u.out_chan_index;
        }
    }, upd);
}

static void reset_mirror() {
    for (int i = 0; i < MAX_PEERS; ++i)
        g_mirror[i] = TestRemoteUserMirror{};
}

} // namespace test_njclient

// ============================================================================
// Test 1: Peer add → remove cycle.
// ============================================================================

static void test_peer_add_remove_cycle()
{
    TEST("peer add/remove cycle: mirror[slot].active toggles correctly");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;

    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{/*slot=*/0, /*user_index=*/42}});
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    auto& m = test_njclient::g_mirror[0];
    if (!m.active || m.user_index != 42) {
        FAIL("PeerAddedUpdate did not propagate slot/user_index"); return;
    }

    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerRemovedUpdate{/*slot=*/0}});
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    if (m.active) {
        FAIL("PeerRemovedUpdate did not clear active flag"); return;
    }
    PASS();
}

// ============================================================================
// Test 2: Channel mask update propagates per-channel flags.
// ============================================================================

static void test_channel_mask_update()
{
    TEST("channel mask update: per-channel present/muted/solo flags align");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{/*slot=*/3, /*user_index=*/7}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelMaskUpdate{
            /*slot=*/3,
            /*submask=*/0x000Fu,           // channels 0..3 subscribed
            /*chanpresentmask=*/0x0007u,   // channels 0..2 present
            /*mutedmask=*/0x0002u,         // channel 1 muted
            /*solomask=*/0x0004u}});       // channel 2 solo
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    auto& m = test_njclient::g_mirror[3];
    if (m.submask != 0x000F || m.chanpresentmask != 0x0007 ||
        m.mutedmask != 0x0002 || m.solomask != 0x0004) {
        FAIL("masks did not propagate"); return;
    }
    if (!m.chans[0].present || !m.chans[1].present || !m.chans[2].present || m.chans[3].present) {
        FAIL("present flags do not align with chanpresentmask"); return;
    }
    if (m.chans[0].muted || !m.chans[1].muted || m.chans[2].muted) {
        FAIL("muted flags do not align with mutedmask"); return;
    }
    if (m.chans[0].solo || m.chans[1].solo || !m.chans[2].solo) {
        FAIL("solo flags do not align with solomask"); return;
    }
    PASS();
}

// ============================================================================
// Test 3: Vol/pan update propagates.
// ============================================================================

static void test_vol_pan_update()
{
    TEST("vol/pan update: mute/volume/pan propagate to mirror");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{5, 11}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerVolPanUpdate{
            /*slot=*/5, /*muted=*/true,
            /*volume=*/0.42f, /*pan=*/-0.75f}});
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    auto& m = test_njclient::g_mirror[5];
    if (!m.muted || m.volume != 0.42f || m.pan != -0.75f) {
        FAIL("vol/pan/mute did not propagate"); return;
    }
    PASS();
}

// ============================================================================
// Test 4: NextDs update transfers ownership of DecodeState* into the mirror.
// ============================================================================

static void test_next_ds_update()
{
    TEST("next_ds update: DecodeState* slotted into mirror.chans[ch].next_ds[idx]");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;
    auto* dummy = new jamwide::DecodeState{};
    dummy->marker = 0xBEEF;

    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{0, 1}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerNextDsUpdate{
            /*slot=*/0, /*channel=*/2, /*slot_idx=*/1, /*ds=*/dummy}});
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    auto& m = test_njclient::g_mirror[0];
    if (m.chans[2].next_ds[1] != dummy || m.chans[2].next_ds[1]->marker != 0xBEEF) {
        FAIL("PeerNextDsUpdate did not slot DecodeState*");
        delete dummy; return;
    }
    delete dummy;
    PASS();
}

// ============================================================================
// Test 5: Concurrent peer churn (mimics real run-thread / audio-thread cadence).
//
// Producer thread cycles peers (Add → ChannelMask → VolPan → NextDs → Remove).
// Consumer (drain) at 1ms cadence. After producer joins, final tail-drain.
// Asserts no crash, mirror consistent (all peers ended with active=false after
// final Remove of each cycle).
// ============================================================================

static void test_concurrent_peer_churn()
{
    TEST("concurrent peer churn: writer+reader, mirror consistent at end");
    test_njclient::reset_mirror();

    constexpr int CYCLES = 1000;
    constexpr int SLOT = 12;
    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;
    std::atomic<bool> producer_done{false};

    std::thread producer([&]() {
        for (int i = 0; i < CYCLES; ++i) {
            jamwide::RemoteUserUpdate add{
                jamwide::PeerAddedUpdate{SLOT, /*user_index=*/i & 0x3F}};
            while (!q.try_push(add)) std::this_thread::yield();

            jamwide::RemoteUserUpdate mask{jamwide::PeerChannelMaskUpdate{
                SLOT, /*submask=*/i & 0xFF, /*chanpresentmask=*/i & 0x0F,
                /*mutedmask=*/(i >> 1) & 0x07, /*solomask=*/0}};
            while (!q.try_push(mask)) std::this_thread::yield();

            jamwide::RemoteUserUpdate vp{jamwide::PeerVolPanUpdate{
                SLOT, (i & 1) != 0, (float)i * 0.001f, (float)((i & 7) - 4) * 0.1f}};
            while (!q.try_push(vp)) std::this_thread::yield();

            jamwide::RemoteUserUpdate nds{jamwide::PeerNextDsUpdate{
                SLOT, /*channel=*/i & 0x1F, /*slot_idx=*/i & 1,
                /*ds=*/nullptr}};
            while (!q.try_push(nds)) std::this_thread::yield();

            jamwide::RemoteUserUpdate rem{jamwide::PeerRemovedUpdate{SLOT}};
            while (!q.try_push(rem)) std::this_thread::yield();
        }
        producer_done.store(true, std::memory_order_release);
    });

    while (!producer_done.load(std::memory_order_acquire) || !q.empty()) {
        q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    producer.join();

    if (test_njclient::g_mirror[SLOT].active) {
        FAIL("steady-state mirror still active after final Remove"); return;
    }
    PASS();
}

// ============================================================================
// Test 6: Out-of-range slot/channel indices are silently ignored (no OOB).
// ============================================================================

static void test_bounded_indices()
{
    TEST("out-of-range slot/channel indices ignored by visitor (no OOB)");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;

    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{/*slot=*/9999, 0}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{/*slot=*/-1, 0}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{test_njclient::MAX_PEERS, 0}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerNextDsUpdate{0, /*channel=*/9999, 0, nullptr}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerNextDsUpdate{0, 0, /*slot_idx=*/9, nullptr}});

    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    for (int i = 0; i < test_njclient::MAX_PEERS; ++i) {
        if (test_njclient::g_mirror[i].active) {
            FAIL("out-of-range update wrote to mirror"); return;
        }
    }
    PASS();
}

// ============================================================================
// Test 7: HIGH-3 generation-gate deferred-free for canonical RemoteUser.
//
// Models the production protocol from src/core/njclient.cpp::removePeer:
//   1. Run thread: push PeerRemovedUpdate, observe publish_gen_target =
//      audio_drain_generation.load(acquire) + 1.
//   2. Audio thread: drain, apply (set mirror active=false), bump
//      audio_drain_generation (release).
//   3. Run thread (NEXT tick): when audio_drain_generation > publish_gen_target-1,
//      enqueue canonical RemoteUser* onto deferred-delete queue.
//   4. Run thread: drain deferred-delete queue; ~RemoteUser runs.
// ============================================================================

static void test_remote_user_generation_gate()
{
    TEST("HIGH-3 generation-gate: RemoteUser deferred-free runs only after audio drain");

    std::atomic<int> dtors{0};
    std::atomic<uint64_t> audio_drain_gen{0};
    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> update_q;
    jamwide::SpscRing<jamwide::RemoteUser*,
                      jamwide::REMOTE_USER_DEFERRED_DELETE_CAPACITY> defer_q;

    auto* victim = new jamwide::RemoteUser{};
    victim->dtor_count = &dtors;

    if (!update_q.try_push(jamwide::RemoteUserUpdate{
            jamwide::PeerRemovedUpdate{0}}))
    {
        FAIL("could not publish PeerRemovedUpdate"); delete victim; return;
    }
    const uint64_t publish_gen_target =
        audio_drain_gen.load(std::memory_order_acquire) + 1;

    update_q.drain([](jamwide::RemoteUserUpdate&&){ /* apply, irrelevant here */ });
    audio_drain_gen.fetch_add(1, std::memory_order_release);

    if (audio_drain_gen.load(std::memory_order_acquire) >= publish_gen_target) {
        if (!defer_q.try_push(victim)) {
            FAIL("deferred-delete queue full (capacity check failed)"); delete victim; return;
        }
    } else {
        FAIL("generation did not advance after drain (gate broken)"); delete victim; return;
    }

    if (dtors.load() != 0) {
        FAIL("RemoteUser destructed BEFORE deferred-delete drain (lifetime bug)"); return;
    }

    defer_q.drain([](jamwide::RemoteUser* p){ delete p; });

    if (dtors.load() == 1) PASS();
    else FAIL("destructor count != 1 after deferred-free protocol");
}

// ============================================================================
// Test 8: HIGH-3 generation-gate also rejects PRE-DRAIN frees (adversarial).
// ============================================================================

static void test_remote_user_gate_rejects_premature_free()
{
    TEST("HIGH-3 generation-gate: rejects RemoteUser free before audio drain has run");

    std::atomic<int> dtors{0};
    std::atomic<uint64_t> audio_drain_gen{0};
    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> update_q;
    jamwide::SpscRing<jamwide::RemoteUser*,
                      jamwide::REMOTE_USER_DEFERRED_DELETE_CAPACITY> defer_q;

    auto* victim = new jamwide::RemoteUser{};
    victim->dtor_count = &dtors;

    update_q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerRemovedUpdate{0}});
    const uint64_t publish_gen_target =
        audio_drain_gen.load(std::memory_order_acquire) + 1;

    if (audio_drain_gen.load(std::memory_order_acquire) >= publish_gen_target) {
        FAIL("gate accepted free before audio drain (UAF window)"); delete victim; return;
    }

    update_q.drain([](jamwide::RemoteUserUpdate&&){});
    audio_drain_gen.fetch_add(1, std::memory_order_release);

    if (audio_drain_gen.load(std::memory_order_acquire) < publish_gen_target) {
        FAIL("gate rejected after audio drain ran"); delete victim; return;
    }
    defer_q.try_push(victim);
    defer_q.drain([](jamwide::RemoteUser* p){ delete p; });

    if (dtors.load() == 1) PASS();
    else FAIL("destructor count != 1 after deferred-free protocol (premature-gate test)");
}

// ============================================================================
// Test 9 (2026-05-02 orphan-fields fix): PeerChannelInfoUpdate roundtrip.
// All four orphan fields (flags, volume, pan, out_chan_index) propagate to
// the mirror through the apply visitor.
// ============================================================================

static void test_chinfo_roundtrip()
{
    TEST("PeerChannelInfoUpdate apply roundtrip: all four fields propagate");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{/*slot=*/3, /*user_index=*/9}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelInfoUpdate{/*slot=*/3, /*channel=*/5,
                                       /*flags=*/2u, /*volume=*/0.7f,
                                       /*pan=*/-0.25f, /*out_chan_index=*/4}});
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    auto& chan = test_njclient::g_mirror[3].chans[5];
    if (chan.flags != 2u) { FAIL("flags did not propagate"); return; }
    if (chan.volume != 0.7f) { FAIL("volume did not propagate"); return; }
    if (chan.pan != -0.25f) { FAIL("pan did not propagate"); return; }
    if (chan.out_chan_index != 4) { FAIL("out_chan_index did not propagate"); return; }
    PASS();
}

// ============================================================================
// Test 10 (2026-05-02 orphan-fields fix): concurrent producer/consumer with
// PeerChannelInfoUpdate mixed into the variant stream. 1000 cycles. Final
// mirror state must reflect last-published-wins semantics.
// ============================================================================

static void test_chinfo_concurrent()
{
    TEST("PeerChannelInfoUpdate concurrent (1000 cycles): no torn fields under producer/consumer");
    test_njclient::reset_mirror();

    constexpr int CYCLES = 1000;
    constexpr int SLOT = 7;
    constexpr int CH = 3;
    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;
    std::atomic<bool> producer_done{false};

    // Pre-add the peer once so the mirror slot is active throughout.
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{SLOT, /*user_index=*/0}});

    std::thread producer([&]() {
        for (int i = 0; i < CYCLES; ++i) {
            // Mix all six (well — five; we keep the peer alive) variant types.
            jamwide::RemoteUserUpdate mask{jamwide::PeerChannelMaskUpdate{
                SLOT, /*submask=*/i & 0xFF, /*chanpresentmask=*/0xFFu,
                /*mutedmask=*/(i >> 1) & 0x07, /*solomask=*/0}};
            while (!q.try_push(mask)) std::this_thread::yield();

            jamwide::RemoteUserUpdate vp{jamwide::PeerVolPanUpdate{
                SLOT, (i & 1) != 0, (float)i * 0.001f, (float)((i & 7) - 4) * 0.1f}};
            while (!q.try_push(vp)) std::this_thread::yield();

            // The new variant — drives the field-by-field copy at every cycle.
            jamwide::RemoteUserUpdate ci{jamwide::PeerChannelInfoUpdate{
                SLOT, CH,
                /*flags=*/(unsigned int)(i & 0x06),  // bits 1 (instamode) and 2 (sessionmode)
                /*volume=*/(float)((i % 100) + 1) * 0.01f,  // 0.01 .. 1.00
                /*pan=*/(float)((i & 0x0F) - 8) * 0.125f,   // -1.0 .. 0.875
                /*out_chan_index=*/i & 0x07}};
            while (!q.try_push(ci)) std::this_thread::yield();

            jamwide::RemoteUserUpdate nds{jamwide::PeerNextDsUpdate{
                SLOT, /*channel=*/CH, /*slot_idx=*/i & 1, /*ds=*/nullptr}};
            while (!q.try_push(nds)) std::this_thread::yield();
        }
        producer_done.store(true, std::memory_order_release);
    });

    while (!producer_done.load(std::memory_order_acquire) || !q.empty()) {
        q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });
    producer.join();

    // Last cycle is i=CYCLES-1=999. Compute expected last-published values.
    const int last = CYCLES - 1;
    const unsigned int exp_flags  = (unsigned int)(last & 0x06);
    const float        exp_volume = (float)((last % 100) + 1) * 0.01f;
    const float        exp_pan    = (float)((last & 0x0F) - 8) * 0.125f;
    const int          exp_outch  = last & 0x07;

    auto& chan = test_njclient::g_mirror[SLOT].chans[CH];
    if (chan.flags != exp_flags) { FAIL("concurrent: flags torn or wrong (last-write-wins broken)"); return; }
    if (chan.volume != exp_volume) { FAIL("concurrent: volume torn or wrong"); return; }
    if (chan.pan != exp_pan) { FAIL("concurrent: pan torn or wrong"); return; }
    if (chan.out_chan_index != exp_outch) { FAIL("concurrent: out_chan_index torn or wrong"); return; }
    PASS();
}

// ============================================================================
// Test 11 (2026-05-02 orphan-fields fix): out-of-range slot/channel indices on
// PeerChannelInfoUpdate are silently ignored. No UB, mirror state untouched.
// ============================================================================

static void test_chinfo_bounds_check()
{
    TEST("PeerChannelInfoUpdate bounds-check: OOB slot/channel ignored, no UB");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;

    // Push four malformed updates with poison values that, if applied, would
    // be detectable in the resulting mirror state.
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelInfoUpdate{
            /*slot=*/test_njclient::MAX_PEERS, /*channel=*/0,
            /*flags=*/0xDEAD, 99.0f, 99.0f, 999}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelInfoUpdate{
            /*slot=*/-1, /*channel=*/0,
            /*flags=*/0xDEAD, 99.0f, 99.0f, 999}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelInfoUpdate{
            /*slot=*/0, /*channel=*/test_njclient::MAX_USER_CHANNELS,
            /*flags=*/0xDEAD, 99.0f, 99.0f, 999}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelInfoUpdate{
            /*slot=*/0, /*channel=*/-1,
            /*flags=*/0xDEAD, 99.0f, 99.0f, 999}});

    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    // Mirror must remain at construction defaults — no slot got the poison.
    for (int s = 0; s < test_njclient::MAX_PEERS; ++s) {
        for (int c = 0; c < test_njclient::MAX_USER_CHANNELS; ++c) {
            auto& chan = test_njclient::g_mirror[s].chans[c];
            if (chan.flags == 0xDEAD || chan.volume == 99.0f ||
                chan.pan == 99.0f || chan.out_chan_index == 999) {
                FAIL("OOB PeerChannelInfoUpdate wrote to mirror"); return;
            }
        }
    }
    PASS();
}

// ============================================================================
// Test 12 (2026-05-02 orphan-fields fix): ordering invariant. PeerChannelInfoUpdate
// published BEFORE PeerNextDsUpdate for the same channel is observed in that
// order on the mirror after a single drain. This is trivially true given SPSC
// FIFO + straight-line drain semantics; the test guards against future
// regressions that reorder or split the drain loop.
// ============================================================================

static void test_chinfo_orders_before_nextds()
{
    TEST("PeerChannelInfoUpdate orders before PeerNextDsUpdate for same channel");
    test_njclient::reset_mirror();

    jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> q;
    auto* fake_ds_marker = new jamwide::DecodeState{};
    fake_ds_marker->marker = 0xCAFE;

    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerAddedUpdate{/*slot=*/2, /*user_index=*/0}});
    // ChannelInfo first — the audio thread MUST observe flags=2 BEFORE the
    // next_ds slot becomes populated, because real-production audio reads
    // mirror.chans[ch].flags on the SAME block it picks up next_ds.
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelInfoUpdate{/*slot=*/2, /*channel=*/1,
                                       /*flags=*/2u, /*volume=*/1.0f,
                                       /*pan=*/0.0f, /*out_chan_index=*/0}});
    q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerNextDsUpdate{/*slot=*/2, /*channel=*/1,
                                  /*slot_idx=*/0, /*ds=*/fake_ds_marker}});
    q.drain([](jamwide::RemoteUserUpdate&& u){ test_njclient::apply_one(std::move(u)); });

    auto& chan = test_njclient::g_mirror[2].chans[1];
    if (chan.flags != 2u) {
        FAIL("ordering: flags=2 not observed (ChannelInfo apply lost)");
        delete fake_ds_marker; return;
    }
    if (chan.next_ds[0] != fake_ds_marker) {
        FAIL("ordering: next_ds[0] not populated (NextDs apply lost)");
        delete fake_ds_marker; return;
    }
    delete fake_ds_marker;
    PASS();
}

// ============================================================================
// Codex HIGH-2 static checks: no escape-hatch pointer in payload.
// ============================================================================

static_assert(std::is_trivially_copyable_v<jamwide::PeerAddedUpdate>,
              "PeerAddedUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::PeerRemovedUpdate>,
              "PeerRemovedUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::PeerChannelMaskUpdate>,
              "PeerChannelMaskUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::PeerVolPanUpdate>,
              "PeerVolPanUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::PeerNextDsUpdate>,
              "PeerNextDsUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::PeerCodecSwapUpdate>,
              "PeerCodecSwapUpdate must be trivially copyable (HIGH-2 contract)");
static_assert(std::is_trivially_copyable_v<jamwide::PeerChannelInfoUpdate>,
              "PeerChannelInfoUpdate must be trivially copyable (HIGH-2 contract)");
// The slot/user_index fields must be by-value scalars, not pointers:
static_assert(!std::is_pointer_v<decltype(std::declval<jamwide::PeerAddedUpdate>().slot)>,
              "PeerAddedUpdate.slot must not be a pointer (HIGH-2 sanity check)");
static_assert(!std::is_pointer_v<decltype(std::declval<jamwide::PeerAddedUpdate>().user_index)>,
              "PeerAddedUpdate.user_index must not be a pointer (HIGH-2 sanity check)");
static_assert(!std::is_pointer_v<decltype(std::declval<jamwide::PeerRemovedUpdate>().slot)>,
              "PeerRemovedUpdate.slot must not be a pointer (HIGH-2 sanity check)");
// PeerNextDsUpdate.ds IS a pointer (DecodeState*) — but this is documented
// ownership-transfer per spsc_payloads.h header comment, NOT a back-reference
// into run-thread-owned state. The variant itself stays trivially copyable.
static_assert(std::is_pointer_v<decltype(std::declval<jamwide::PeerNextDsUpdate>().ds)>,
              "PeerNextDsUpdate.ds is intentionally a DecodeState* (ownership transfer)");
// Bound payload sizes — guard against accidental balloon (suggests escape-hatch field).
static_assert(sizeof(jamwide::PeerAddedUpdate) <= 32,
              "PeerAddedUpdate bloat suggests an escape-hatch field was added (HIGH-2)");
static_assert(sizeof(jamwide::PeerVolPanUpdate) <= 32,
              "PeerVolPanUpdate bloat suggests an escape-hatch field was added (HIGH-2)");

// ============================================================================
// main
// ============================================================================

int main()
{
    printf("test_remote_user_mirror — concurrent peer churn + HIGH-2/HIGH-3 coverage\n");

    test_peer_add_remove_cycle();
    test_channel_mask_update();
    test_vol_pan_update();
    test_next_ds_update();
    test_concurrent_peer_churn();
    test_bounded_indices();
    test_remote_user_generation_gate();
    test_remote_user_gate_rejects_premature_free();
    // 2026-05-02 RemoteUserMirror orphan-fields fix coverage.
    test_chinfo_roundtrip();
    test_chinfo_concurrent();
    test_chinfo_bounds_check();
    test_chinfo_orders_before_nextds();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
