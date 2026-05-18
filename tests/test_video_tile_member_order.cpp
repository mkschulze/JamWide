// Plan 22-01 Task 3 — test_video_tile_member_order.cpp
//
// Runtime offset-comparison proof for the MEMBER-ORDER CONTRACT (T-22-MO-1
// mitigation). The contract: in both SelfVideoTile and RemotePeerTile, the
// Subscription handle MUST be the LAST declared private member, so its
// destructor runs FIRST during reverse-declaration-order destruction and
// blocks any in-flight callback before the upstream mutex/frame/string
// members are torn down.
//
// Why runtime offset comparison instead of std::is_standard_layout +
// offsetof? Both tile classes inherit from juce::Component (and other JUCE
// bases) which makes them non-standard-layout — offsetof is conditionally
// supported by the standard, and clang/libc++ emits a non-fatal warning. The
// runtime pointer-subtraction probe is portable and equally precise.
//
// VideoTileMemberOrderProbe is declared `friend class` inside each tile
// header under #ifdef JAMWIDE_BUILD_TESTS, so private members are visible
// here without touching production ABI.
//
// Sub-tests:
//   1. SelfVideoTile member-order — subscription_ offset > all other members.
//   2. RemotePeerTile member-order — subscription_ offset > all other members.
//   3. SelfVideoTile ctor + dtor without crash (Subscription teardown clean).
//   4. RemotePeerTile ctor + dtor without crash (Subscription teardown clean).
//
// Build wiring: console-app variant with JUCE message thread, mirrors
// test_remote_frame_distributor. Links the tile sources + distributors + decoder
// (transitively required by JamWideRemoteFrameDistributor.cpp).

#include "juce/ui/video/SelfVideoTile.h"
#include "juce/ui/video/RemotePeerTile.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace jamwide {

// Friend probe class — defined in the test TU but befriended by both tile
// headers under #ifdef JAMWIDE_BUILD_TESTS. Member access is direct (private
// members visible to friends).
class VideoTileMemberOrderProbe {
public:
    // Compute byte-offset of an object's member from the object base.
    static std::size_t selfSubscriptionOffset(const SelfVideoTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.subscription_)
             - reinterpret_cast<const char*>(&t);
    }
    static std::size_t selfPendingFrameOffset(const SelfVideoTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.pendingFrame_)
             - reinterpret_cast<const char*>(&t);
    }
    static std::size_t selfCurrentFrameOffset(const SelfVideoTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.currentFrame_)
             - reinterpret_cast<const char*>(&t);
    }
    static std::size_t selfPendingMuOffset(const SelfVideoTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.pendingMu_)
             - reinterpret_cast<const char*>(&t);
    }
    static std::size_t selfCurrentMuOffset(const SelfVideoTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.currentMu_)
             - reinterpret_cast<const char*>(&t);
    }
    static std::size_t selfHoveringOffset(const SelfVideoTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.hovering_)
             - reinterpret_cast<const char*>(&t);
    }

    static std::size_t remoteSubscriptionOffset(const RemotePeerTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.subscription_)
             - reinterpret_cast<const char*>(&t);
    }
    static std::size_t remoteUsernameOffset(const RemotePeerTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.username_)
             - reinterpret_cast<const char*>(&t);
    }
    static std::size_t remoteChidxOffset(const RemotePeerTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.chidx_)
             - reinterpret_cast<const char*>(&t);
    }
    static std::size_t remoteHoveringOffset(const RemotePeerTile& t) noexcept {
        return reinterpret_cast<const char*>(&t.hovering_)
             - reinterpret_cast<const char*>(&t);
    }
};

} // namespace jamwide

namespace {

static int tests_passed = 0;
static int tests_failed = 0;

#define EXPECT(cond, msg) do {                                                \
    if (!(cond)) {                                                            \
        std::fprintf(stderr, "FAIL [" msg "] at %s:%d\n", __FILE__, __LINE__); \
        ++tests_failed; return;                                                \
    }                                                                          \
} while (0)

#define PASS(...) do {                                                        \
    ++tests_passed; std::printf("PASS [" __VA_ARGS__ "]\n");                  \
} while (0)

using jamwide::VideoTileMemberOrderProbe;
using jamwide::JamWideFrameDistributor;
using jamwide::JamWideRemoteFrameDistributor;
using jamwide::SelfVideoTile;
using jamwide::RemotePeerTile;

// ─── Sub-test 1: SelfVideoTile member-order ──────────────────────────────

void test_self_video_tile_member_order()
{
    JamWideFrameDistributor selfDist;
    SelfVideoTile tile(selfDist);

    const auto subscriptionOff = VideoTileMemberOrderProbe::selfSubscriptionOffset(tile);
    const auto pendingFrameOff = VideoTileMemberOrderProbe::selfPendingFrameOffset(tile);
    const auto currentFrameOff = VideoTileMemberOrderProbe::selfCurrentFrameOffset(tile);
    const auto pendingMuOff    = VideoTileMemberOrderProbe::selfPendingMuOffset(tile);
    const auto currentMuOff    = VideoTileMemberOrderProbe::selfCurrentMuOffset(tile);
    const auto hoveringOff     = VideoTileMemberOrderProbe::selfHoveringOffset(tile);

    EXPECT(subscriptionOff > pendingMuOff,    "SelfVideoTile: subscription_ > pendingMu_");
    EXPECT(subscriptionOff > pendingFrameOff, "SelfVideoTile: subscription_ > pendingFrame_");
    EXPECT(subscriptionOff > currentMuOff,    "SelfVideoTile: subscription_ > currentMu_");
    EXPECT(subscriptionOff > currentFrameOff, "SelfVideoTile: subscription_ > currentFrame_");
    EXPECT(subscriptionOff > hoveringOff,     "SelfVideoTile: subscription_ > hovering_");

    std::printf("PASS [SelfVideoTile subscription_ is LAST member at offset=%zu]\n",
                subscriptionOff);
    ++tests_passed;
}

// ─── Sub-test 2: RemotePeerTile member-order ─────────────────────────────

void test_remote_peer_tile_member_order()
{
    JamWideRemoteFrameDistributor remoteDist;
    RemotePeerTile tile(remoteDist, juce::String("test_user"), 1);

    const auto subscriptionOff = VideoTileMemberOrderProbe::remoteSubscriptionOffset(tile);
    const auto usernameOff     = VideoTileMemberOrderProbe::remoteUsernameOffset(tile);
    const auto chidxOff        = VideoTileMemberOrderProbe::remoteChidxOffset(tile);
    const auto hoveringOff     = VideoTileMemberOrderProbe::remoteHoveringOffset(tile);

    EXPECT(subscriptionOff > usernameOff, "RemotePeerTile: subscription_ > username_");
    EXPECT(subscriptionOff > chidxOff,    "RemotePeerTile: subscription_ > chidx_");
    EXPECT(subscriptionOff > hoveringOff, "RemotePeerTile: subscription_ > hovering_");

    std::printf("PASS [RemotePeerTile subscription_ is LAST member at offset=%zu]\n",
                subscriptionOff);
    ++tests_passed;
}

// ─── Sub-test 3: SelfVideoTile ctor + dtor clean ─────────────────────────

void test_self_video_tile_ctor_dtor_clean()
{
    // Construct + destruct in a tight scope. The Subscription's destructor
    // calls unregisterAndWait via Phase 19's distributor — with no in-flight
    // onFrame the wait is a no-op. Crash here would indicate either a bad
    // initializer-list ordering OR a broken MEMBER-ORDER CONTRACT.
    {
        JamWideFrameDistributor dist;
        SelfVideoTile tile(dist);
        // No frames published — Subscription teardown is a clean no-wait.
    }
    PASS("SelfVideoTile ctor+dtor clean (no in-flight)");
}

// ─── Sub-test 4: RemotePeerTile ctor + dtor clean ────────────────────────

void test_remote_peer_tile_ctor_dtor_clean()
{
    // Phase 21's distributor: subscribeToPeer with no existing sink stores
    // the listener in deferredListeners_ and lazy-creates a 320x240 sink
    // (see test_remote_frame_distributor.cpp sub-test 3). ~Subscription on a
    // sink that was created by lazy-create still flows through removeListener
    // → inFlightCv_ wait (a no-op when no handleAsyncUpdate is pending).
    {
        JamWideRemoteFrameDistributor dist;
        RemotePeerTile tile(dist, juce::String("solo_peer"), 1);
    }
    PASS("RemotePeerTile ctor+dtor clean (no peers, no in-flight)");
}

} // anonymous namespace

int main()
{
    // JUCE init for the message thread — distributors / sinks dispatch via
    // AsyncUpdater; even if no events fire in these tests, JUCE init is
    // required for the framework's static state to be valid.
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("test_video_tile_member_order:\n");
    test_self_video_tile_member_order();
    test_remote_peer_tile_member_order();
    test_self_video_tile_ctor_dtor_clean();
    test_remote_peer_tile_ctor_dtor_clean();

    std::printf("\nresults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
