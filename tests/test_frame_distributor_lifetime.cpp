// Phase 19-01 Task 3 — test_frame_distributor_lifetime.cpp
//
// HIGH-2 mitigation test: ~Subscription must block until in-flight onFrame()
// returns. Reproduces the "callback in progress, subscriber destroyed" race
// deterministically via std::promise to pin a subscriber inside its onFrame.
//
// Build wiring matches test_frame_distributor.cpp (pure-C++, juce::juce_core +
// juce::juce_graphics, no JamWideJuce link — MEDIUM-5 mitigation).
#include "juce/video/native/JamWideFrameDistributor.h"
#include <juce_graphics/juce_graphics.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>

using jamwide::JamWideFrameDistributor;

namespace {

class SlowSubscriber : public JamWideFrameDistributor::Subscriber {
public:
    void onFrame(const juce::Image& /*image*/) override {
        entered.store(true, std::memory_order_release);
        gate.get_future().wait();  // block until promise.set_value()
        exited.store(true, std::memory_order_release);
        count.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<bool> entered{false};
    std::atomic<bool> exited{false};
    std::atomic<int> count{0};
    std::promise<void> gate;
};

class FastSubscriber : public JamWideFrameDistributor::Subscriber {
public:
    void onFrame(const juce::Image& /*image*/) override {
        count.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<int> count{0};
};

juce::Image makeSynthImage() {
    return juce::Image(juce::Image::ARGB, 16, 16, true);
}

// Test 1: subscriber dropped while a publish() is inside its onFrame must
// observably BLOCK the ~Subscription call until onFrame returns.
void test1_drop_during_on_frame() {
    JamWideFrameDistributor dist;
    auto slow = std::make_unique<SlowSubscriber>();
    auto sub = dist.registerSubscriber(slow.get());

    // Producer thread enters slow->onFrame and blocks on the promise.
    juce::Image img = makeSynthImage();
    std::thread producer([&] { dist.publish(img); });

    // Wait until slow->onFrame is observably entered.
    auto t0 = std::chrono::steady_clock::now();
    while (!slow->entered.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(2)) {
            std::fprintf(stderr, "test1: subscriber never entered onFrame\n");
            std::exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Drop the subscription from the main thread. This invokes ~Subscription
    // which MUST block until slow->onFrame exits.
    std::atomic<bool> dropComplete{false};
    std::thread dropper([&] {
        sub = JamWideFrameDistributor::Subscription{};  // ~Subscription on the moved-from slot
        dropComplete.store(true, std::memory_order_release);
    });

    // Verify dropComplete stays false for ~100 ms.
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (dropComplete.load(std::memory_order_acquire)) {
            std::fprintf(stderr, "test1: ~Subscription returned before onFrame exit "
                                 "(HIGH-2 mitigation broken)\n");
            std::exit(1);
        }
    }

    // Release slow->onFrame.
    slow->gate.set_value();

    // Producer should finish; dropper should finish shortly after.
    producer.join();
    dropper.join();

    assert(dropComplete.load());
    assert(slow->exited.load());
    assert(slow->count.load() == 1);

    // Subsequent publishes must NOT call slow->onFrame.
    dist.publish(img);
    dist.publish(img);
    assert(slow->count.load() == 1);
}

// Test 2: after ~Subscription returns, the subscriber object can be safely
// destroyed without UAF on the camera-callback thread.
void test2_no_uaf_after_drop() {
    JamWideFrameDistributor dist;
    auto slow = std::make_unique<SlowSubscriber>();
    auto sub = dist.registerSubscriber(slow.get());

    juce::Image img = makeSynthImage();
    std::thread producer([&] { dist.publish(img); });
    auto t0 = std::chrono::steady_clock::now();
    while (!slow->entered.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(2)) {
            std::fprintf(stderr, "test2: never entered onFrame\n");
            std::exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::thread dropper([&] { sub = JamWideFrameDistributor::Subscription{}; });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    slow->gate.set_value();
    producer.join();
    dropper.join();

    // Now destroy the subscriber. If ~Subscription's contract holds, the
    // distributor is guaranteed not to call slow->onFrame again, so this is safe.
    slow.reset();

    // Publishing more does not crash.
    FastSubscriber sentinel;
    auto subSentinel = dist.registerSubscriber(&sentinel);
    for (int i = 0; i < 10; ++i) dist.publish(img);
    assert(sentinel.count.load() == 10);
}

// Test 3: high-contention register/unregister + publish must not deadlock.
void test3_no_deadlock_contention() {
    JamWideFrameDistributor dist;
    std::atomic<bool> stop{false};
    juce::Image img = makeSynthImage();

    std::thread producers[4];
    for (int i = 0; i < 4; ++i) {
        producers[i] = std::thread([&] { while (!stop.load()) dist.publish(img); });
    }
    std::thread churn([&] {
        while (!stop.load()) {
            FastSubscriber s;
            auto subS = dist.registerSubscriber(&s);
            // Drop immediately — ~Subscription must wait for any in-flight onFrame.
        }
    });

    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        auto start = std::chrono::steady_clock::now();
        while (!done.load() &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!done.load()) {
            std::fprintf(stderr, "test3: deadlock (>5s)\n");
            std::exit(99);
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop.store(true);
    for (auto& p : producers) p.join();
    churn.join();
    done.store(true);
    watchdog.join();
}

} // namespace

int main() {
    test1_drop_during_on_frame();
    test2_no_uaf_after_drop();
    test3_no_deadlock_contention();
    std::printf("test_frame_distributor_lifetime: PASS (3 scenarios — HIGH-2 verified)\n");
    return 0;
}
