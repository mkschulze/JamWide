// Phase 19-01 Task 3: JamWideFrameDistributor implementation.
#include "JamWideFrameDistributor.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <vector>

namespace jamwide {

// ─── Subscription RAII ─────────────────────────────────────────────────────

JamWideFrameDistributor::Subscription::Subscription(Subscription&& other) noexcept
    : owner_(other.owner_), id_(other.id_) {
    other.owner_ = nullptr;
    other.id_    = 0;
}

JamWideFrameDistributor::Subscription&
JamWideFrameDistributor::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        // Release the current registration FIRST (must wait for in-flight
        // onFrame to exit). Only then take over `other`'s slot.
        if (owner_) owner_->unregisterAndWait(id_);
        owner_  = other.owner_;
        id_     = other.id_;
        other.owner_ = nullptr;
        other.id_    = 0;
    }
    return *this;
}

JamWideFrameDistributor::Subscription::~Subscription() {
    if (owner_) owner_->unregisterAndWait(id_);
}

// ─── Distributor ───────────────────────────────────────────────────────────

JamWideFrameDistributor::JamWideFrameDistributor() = default;

JamWideFrameDistributor::~JamWideFrameDistributor() {
    // All Subscription handles MUST be released before the distributor is
    // destroyed — otherwise we'd have a dangling owner_ pointer on the
    // outstanding Subscription. Debug-only assert; in release builds we leak
    // the entries map without a use-after-free hazard (the Subscription
    // destructor will dereference a freed `this` if a handle is leaked — but
    // that's a caller bug we surface via assertion in debug).
    std::lock_guard<std::mutex> lock(regMu_);
    assert(entries_.empty() && "JamWideFrameDistributor destroyed with outstanding Subscriptions");
}

JamWideFrameDistributor::Subscription
JamWideFrameDistributor::registerSubscriber(Subscriber* s) {
    if (s == nullptr) return {};  // defensive — empty (inactive) Subscription
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(regMu_);
        id = nextId_++;
        auto entry = std::make_shared<Entry>();
        entry->sub = s;
        entries_.emplace(id, std::move(entry));
    }
    return Subscription{this, id};
}

void JamWideFrameDistributor::publish(const juce::Image& image) {
    // Update peakFps_ from inter-publish gap (debug accessor only).
    const auto now_ns = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    const auto prev_ns = lastPublishNanos_.exchange(now_ns, std::memory_order_relaxed);
    if (prev_ns != 0) {
        const auto delta_ns = now_ns - prev_ns;
        if (delta_ns > 0) {
            const float fps  = 1e9f / static_cast<float>(delta_ns);
            float current    = peakFps_.load(std::memory_order_relaxed);
            while (fps > current &&
                   !peakFps_.compare_exchange_weak(current, fps,
                                                  std::memory_order_relaxed)) {
                // retry on contention
            }
        }
    }

    // Snapshot of entries with refcount bumped — happens under lock so
    // unregisterAndWait observes the bump before we release the lock.
    std::vector<std::shared_ptr<Entry>> snapshot;
    {
        std::lock_guard<std::mutex> lock(regMu_);
        snapshot.reserve(entries_.size());
        for (auto& kv : entries_) {
            kv.second->inFlight.fetch_add(1, std::memory_order_acq_rel);
            snapshot.push_back(kv.second);
        }
    }

    // Fan out OUTSIDE the lock — subscribers may call back into the
    // distributor (e.g. registerSubscriber from inside onFrame) without
    // deadlocking. Each entry's inFlight counter prevents Subscription
    // destruction during this loop.
    for (auto& entry : snapshot) {
        Subscriber* sub = entry->sub;
        if (sub != nullptr) {
            sub->onFrame(image);
        }
        // Notify waiters AFTER we've decremented — unregisterAndWait wakes
        // and re-checks the counter.
        if (entry->inFlight.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lock(regMu_);
            cv_.notify_all();
        }
    }
}

void JamWideFrameDistributor::unregisterAndWait(std::uint64_t id) noexcept {
    std::shared_ptr<Entry> entry;
    {
        std::unique_lock<std::mutex> lock(regMu_);
        auto it = entries_.find(id);
        if (it == entries_.end()) return;
        entry = it->second;
        // Clear the back-pointer FIRST so any concurrent publish snapshot
        // that already bumped inFlight observes sub==nullptr and skips the
        // callback. (Strictly speaking the cv wait below is what guarantees
        // safety; this is belt-and-braces for the publish loop's `if (sub
        // != nullptr)` check after a remove.)
        entry->sub = nullptr;
        entries_.erase(it);

        // Wait until all in-flight onFrame calls referencing this entry exit.
        cv_.wait(lock, [&] { return entry->inFlight.load(std::memory_order_acquire) == 0; });
    }
    // shared_ptr destructor cleans up Entry now that no thread holds it.
}

} // namespace jamwide
