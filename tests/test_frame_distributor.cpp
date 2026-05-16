// Phase 19-01 Task 3 — test_frame_distributor.cpp
//
// Pure-C++ test for JamWideFrameDistributor's basic fan-out semantics.
// Covers Test 1 (basic fan-out), Test 2 (Subscription move semantics),
// Test 3 (concurrent publish stress), Test 4 (no callback while holding lock),
// Test 5 (peakFps tracking).
//
// Build wiring: see CMakeLists.txt block under "Phase 19-01 Task 1: native
// camera capture pipeline tests" (links juce::juce_core + juce::juce_graphics;
// does NOT link against JamWideJuce — MEDIUM-5 mitigation).
#include "juce/video/native/JamWideFrameDistributor.h"
#include <juce_graphics/juce_graphics.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

using jamwide::JamWideFrameDistributor;

namespace {

class CountingSubscriber : public JamWideFrameDistributor::Subscriber {
public:
    void onFrame(const juce::Image& /*image*/) override {
        count.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<int> count{0};
};

juce::Image makeSynthImage() {
    return juce::Image(juce::Image::ARGB, 16, 16, true);
}

void test1_basic_fan_out() {
    JamWideFrameDistributor dist;
    CountingSubscriber a, b, c;
    auto subA = dist.registerSubscriber(&a);
    auto subB = dist.registerSubscriber(&b);
    auto subC = dist.registerSubscriber(&c);

    juce::Image img = makeSynthImage();
    for (int i = 0; i < 5; ++i) dist.publish(img);

    assert(a.count.load() == 5);
    assert(b.count.load() == 5);
    assert(c.count.load() == 5);
}

void test2_move_semantics() {
    JamWideFrameDistributor dist;
    CountingSubscriber a;
    auto subA = dist.registerSubscriber(&a);
    assert(subA.isActive());

    juce::Image img = makeSynthImage();
    dist.publish(img);
    assert(a.count.load() == 1);

    auto moved = std::move(subA);
    assert(!subA.isActive());
    assert(moved.isActive());
    dist.publish(img);
    assert(a.count.load() == 2);

    moved = JamWideFrameDistributor::Subscription{};  // unsubscribe
    assert(!moved.isActive());
    dist.publish(img);
    assert(a.count.load() == 2);  // still 2 — no longer subscribed
}

void test3_concurrent_publish_stress() {
    JamWideFrameDistributor dist;
    CountingSubscriber a;
    auto subA = dist.registerSubscriber(&a);
    std::atomic<bool> stop{false};

    juce::Image img = makeSynthImage();
    std::thread p1([&] { while (!stop.load()) dist.publish(img); });
    std::thread p2([&] { while (!stop.load()) dist.publish(img); });

    // Register/unregister churn from main thread for 1 second.
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        CountingSubscriber temp;
        auto subTemp = dist.registerSubscriber(&temp);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // subTemp dtor unsubscribes — must not deadlock.
    }
    stop.store(true);
    p1.join();
    p2.join();
    // No crash, no deadlock, no UAF.
    assert(a.count.load() > 0);
}

class ReentrantSubscriber : public JamWideFrameDistributor::Subscriber {
public:
    ReentrantSubscriber(JamWideFrameDistributor& d, CountingSubscriber* other)
        : dist(d), b(other) {}
    void onFrame(const juce::Image& /*image*/) override {
        if (!registered && b) {
            // Re-enter the distributor from inside a callback. Must NOT deadlock.
            subB = dist.registerSubscriber(b);
            registered = true;
        }
        count.fetch_add(1, std::memory_order_relaxed);
    }
    JamWideFrameDistributor& dist;
    CountingSubscriber* b;
    JamWideFrameDistributor::Subscription subB;
    std::atomic<int> count{0};
    bool registered{false};
};

void test4_no_callback_while_holding_lock() {
    JamWideFrameDistributor dist;
    CountingSubscriber b;
    ReentrantSubscriber a(dist, &b);

    auto subA = dist.registerSubscriber(&a);
    juce::Image img = makeSynthImage();

    // Use a watchdog thread so we exit 99 on deadlock.
    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        auto start = std::chrono::steady_clock::now();
        while (!done.load() &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!done.load()) {
            std::fprintf(stderr, "test4: deadlock (>5s)\n");
            std::exit(99);
        }
    });

    dist.publish(img);  // a re-registers b inside its onFrame
    dist.publish(img);  // both should now fire
    done.store(true);
    watchdog.join();

    assert(a.count.load() == 2);
    assert(b.count.load() == 1);  // only the second publish hit b
}

void test5_peak_fps() {
    JamWideFrameDistributor dist;
    CountingSubscriber a;
    auto subA = dist.registerSubscriber(&a);
    juce::Image img = makeSynthImage();
    for (int i = 0; i < 10; ++i) {
        dist.publish(img);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    float fps = dist.getPeakFps();
    // 10 ms intervals → 100 FPS expected; allow generous tolerance for scheduling.
    assert(fps >= 50.0f && fps <= 2000.0f);
}

} // namespace

int main() {
    test1_basic_fan_out();
    test2_move_semantics();
    test3_concurrent_publish_stress();
    test4_no_callback_while_holding_lock();
    test5_peak_fps();
    std::printf("test_frame_distributor: PASS (5 scenarios)\n");
    return 0;
}
