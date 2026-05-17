// Plan 21-03 Task 3 — test_remote_frame_distributor.cpp
//
// 4 unit sub-tests for JamWideRemoteFrameDistributor + PeerVideoSink:
//
//   1. test_two_listeners_same_peer_both_called (D-06)
//      Subscribe two listeners on the same peer; triggerAsyncUpdate; pump
//      the message loop; assert both callbacks fired.
//
//   2. test_subscription_dtor_blocks_in_flight (HIGH-2 mirror)
//      Subscribe with a sleep-injecting callback; trigger; destroy the
//      Subscription on another thread; assert dtor blocks until callback
//      returns (HIGH-2 mirror of Phase 19's unregisterAndWait).
//
//   3. test_subscribe_before_peer_exists_then_create (RESEARCH §OQ2)
//      Subscribe before any sink exists for the key. Distributor lazy-
//      creates a sink with default v1.3 fixed dimensions (320x240).
//      Trigger; assert callback fired.
//
//   4. test_sink_dtor_cancels_pending_async_update (codex Cluster 3 NEW)
//      Two scenarios:
//      (a) trigger asyncUpdate, then destroy sink BEFORE message loop pumps;
//          assert callback NOT called (cancelPendingUpdate prevented dispatch).
//      (b) trigger asyncUpdate, pump loop briefly to start the dispatch
//          (callback sleeps 200 ms), destroy sink on another thread; assert
//          destruction does NOT crash AND callback completed.
//
// Build: links JamWideRemoteFrameDistributor.cpp + PeerVideoSink.cpp +
// juce_core/juce_graphics/juce_events. NO njclient linkage (the tests
// exercise the distributor in isolation; e2e wiring lives in
// test_video_sync_e2e).

#include "juce/video/distributor/JamWideRemoteFrameDistributor.h"
#include "juce/video/distributor/PeerVideoSink.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

using namespace jamwide;

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        std::printf("  TEST: %s ... ", name);                              \
        std::fflush(stdout);                                               \
    } while (0)

#define PASS()                                                             \
    do {                                                                   \
        tests_passed++;                                                    \
        std::printf("PASSED\n");                                           \
    } while (0)

#define FAIL(msg)                                                          \
    do {                                                                   \
        std::printf("FAILED: %s\n", msg);                                  \
    } while (0)

// Drain the JUCE message loop for `ms` milliseconds. The distributor's
// triggerAsyncUpdate dispatches handleAsyncUpdate on the message thread; we
// must pump the loop in the test (which is the message thread) for the
// dispatched message to fire.
static void pumpMessageLoopFor(int ms) {
    auto* mm = juce::MessageManager::getInstance();
    if (!mm) return;
    mm->runDispatchLoopUntil(ms);
}

// ─── Sub-test 1: D-06 — two listeners on the same peer ────────────────

static void test_two_listeners_same_peer_both_called()
{
    TEST("two listeners on same (username, chidx) both fire on triggerAsyncUpdate");
    auto dist = std::make_unique<JamWideRemoteFrameDistributor>();

    std::atomic<int> cnt1{0}, cnt2{0};
    auto sub1 = dist->subscribeToPeer("alice", 1, [&]{ cnt1.fetch_add(1); });
    auto sub2 = dist->subscribeToPeer("alice", 1, [&]{ cnt2.fetch_add(1); });

    PeerVideoSink* sink = dist->findSink("alice", 1);
    if (!sink) { FAIL("findSink returned null"); return; }

    sink->triggerAsyncUpdate();
    pumpMessageLoopFor(200);

    if (cnt1.load() != 1) { FAIL("listener 1 did not fire"); return; }
    if (cnt2.load() != 1) { FAIL("listener 2 did not fire"); return; }
    PASS();
}

// ─── Sub-test 2: HIGH-2 mirror — Subscription dtor blocks in-flight ─

static void test_subscription_dtor_blocks_in_flight()
{
    TEST("~Subscription blocks until any in-flight handleAsyncUpdate returns");
    auto dist = std::make_unique<JamWideRemoteFrameDistributor>();

    std::atomic<int> entered{0};
    std::atomic<int> exited{0};

    auto sub = dist->subscribeToPeer("bob", 1, [&]{
        entered.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        exited.fetch_add(1);
    });

    PeerVideoSink* sink = dist->findSink("bob", 1);
    if (!sink) { FAIL("findSink returned null"); return; }
    sink->triggerAsyncUpdate();

    // Spawn a thread to wait briefly, then destroy the Subscription.
    // The dtor MUST block until handleAsyncUpdate's listener invocation
    // (which is sleeping for 150 ms) completes.
    std::thread destroyer([&]{
        // Give the message loop a moment to start the dispatch.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // Destroy the Subscription. If the dtor does not wait for in-flight,
        // we'd return before exited.load() == 1.
        sub = JamWideRemoteFrameDistributor::Subscription{};  // move-assign nulls it out via dtor
    });

    // Pump the loop so the dispatched handleAsyncUpdate actually runs.
    pumpMessageLoopFor(400);
    destroyer.join();

    if (entered.load() != 1) { FAIL("callback never entered"); return; }
    if (exited.load() != 1)  { FAIL("callback never exited (dtor did not wait?)"); return; }
    PASS();
}

// ─── Sub-test 3: RESEARCH §OQ2 — subscribe before peer exists ────────

static void test_subscribe_before_peer_exists_then_create()
{
    TEST("subscribeToPeer before any sink exists lazy-creates the sink");
    auto dist = std::make_unique<JamWideRemoteFrameDistributor>();

    std::atomic<int> cnt{0};
    auto sub = dist->subscribeToPeer("carol", 1, [&]{ cnt.fetch_add(1); });

    // Verify lazy-create: a sink should now exist with default dimensions.
    PeerVideoSink* sink = dist->findSink("carol", 1);
    if (!sink) { FAIL("subscribe did not lazy-create sink"); return; }
    if (sink->image_front.getWidth() != 320 || sink->image_front.getHeight() != 240) {
        FAIL("lazy-created sink not at v1.3 fixed 320x240"); return;
    }

    sink->triggerAsyncUpdate();
    pumpMessageLoopFor(200);

    if (cnt.load() != 1) { FAIL("subscribed callback did not fire"); return; }
    PASS();
}

// ─── Sub-test 4 (codex Cluster 3 NEW) — sink dtor cancels + waits ───

static void test_sink_dtor_cancels_pending_async_update()
{
    TEST("~PeerVideoSink cancels pending + waits for in-flight handleAsyncUpdate (codex Cluster 3)");
    // ── Scenario (a): cancel pending BEFORE dispatch ───────────────────
    {
        auto dist = std::make_unique<JamWideRemoteFrameDistributor>();
        std::atomic<int> cnt{0};
        auto sub = dist->subscribeToPeer("dave", 1, [&]{ cnt.fetch_add(1); });

        PeerVideoSink* sink = dist->findSink("dave", 1);
        if (!sink) { FAIL("(a) findSink returned null"); return; }
        sink->triggerAsyncUpdate();

        // DO NOT pump the message loop. Destroy the sink directly via
        // removeSink — the sink's dtor must call cancelPendingUpdate()
        // which drops the queued dispatch from the message-thread queue.
        dist->removeSink("dave", 1);

        // Now pump the loop briefly to see if any pending message slipped
        // through despite cancelPendingUpdate.
        pumpMessageLoopFor(200);

        if (cnt.load() != 0) {
            FAIL("(a) callback fired despite sink destruction (cancelPendingUpdate failed?)");
            return;
        }
    }

    // ── Scenario (b): wait for in-flight ───────────────────────────────
    {
        auto dist = std::make_unique<JamWideRemoteFrameDistributor>();
        std::atomic<int> entered{0}, exited{0};

        // Sink listener sleeps 150 ms so we can race the sink destruction
        // against the in-flight handleAsyncUpdate.
        auto sub = dist->subscribeToPeer("erin", 1, [&]{
            entered.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            exited.fetch_add(1);
        });

        PeerVideoSink* sink = dist->findSink("erin", 1);
        if (!sink) { FAIL("(b) findSink returned null"); return; }
        sink->triggerAsyncUpdate();

        // Spawn destroyer: wait 50 ms so message loop starts the dispatch,
        // then destroy the sink. The sink's dtor must wait for the in-flight
        // listener invocation to finish via inFlightCv_.
        std::thread destroyer([dist = dist.get()]{
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            dist->removeSink("erin", 1);
        });

        // Pump the loop so handleAsyncUpdate actually runs.
        pumpMessageLoopFor(400);
        destroyer.join();

        if (entered.load() != 1) { FAIL("(b) listener never entered"); return; }
        if (exited.load()  != 1) { FAIL("(b) listener never exited (sink dtor crashed?)"); return; }
    }

    PASS();
}

} // anonymous namespace

int main()
{
    // Mirror tests/test_osc_loopback.cpp: ScopedJuceInitialiser_GUI sets up
    // the JUCE message manager on the main thread so AsyncUpdater dispatches
    // can be pumped via runDispatchLoopUntil().
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("test_remote_frame_distributor:\n");
    test_two_listeners_same_peer_both_called();
    test_subscription_dtor_blocks_in_flight();
    test_subscribe_before_peer_exists_then_create();
    test_sink_dtor_cancels_pending_async_update();

    std::printf("\nresults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
