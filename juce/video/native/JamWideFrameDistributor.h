#pragma once
// Phase 19-01 Task 3: thread-safe frame fan-out with Subscription RAII (HIGH-2).
//
// The distributor lives on the JamWideJuceProcessor and is fed by the
// JamWideCameraDevice listener forwarder. Subscribers come from the preview
// tile + popout window (Phase 19-02) and the H.264 encoder (Phase 20).
//
// Threading contract:
//   - publish() is called from the camera-callback thread (juce::CameraDevice
//     Listener "any thread"). It snapshots subscribers under a brief mutex,
//     releases the mutex, then fans the image out by calling Subscriber::onFrame
//     OUTSIDE the lock. This avoids the camera thread blocking on UI work.
//   - registerSubscriber() returns a moveable RAII handle. ~Subscription
//     removes the subscriber from the active set AND blocks until any in-flight
//     publish() iteration referring to that subscriber returns. This is the
//     HIGH-2 mitigation that prevents use-after-free when a UI component
//     owning a Subscriber is destroyed mid-callback.
//
// Reviewer note: per the Codex HIGH-2 finding the original design did snapshot
// iteration outside the lock without coordinating with the subscriber's
// lifetime. The Subscription / unregisterAndWait dance closes the race.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace juce { class Image; }

namespace jamwide {

class JamWideFrameDistributor {
public:
    // Subscriber interface. onFrame may be called on the camera-callback thread
    // (per JUCE's "any thread" contract). Implementations MUST be thread-safe
    // and MUST NOT block on UI work (marshal to the message thread via
    // juce::AsyncUpdater or MessageManager::callAsync instead — see HIGH-4 in
    // Phase 19-02's CameraPreviewTile).
    class Subscriber {
    public:
        virtual ~Subscriber() = default;
        virtual void onFrame(const juce::Image& image) = 0;
    };

    // RAII handle returned from registerSubscriber. While alive, the
    // subscriber receives frames. ~Subscription removes the subscriber AND
    // blocks until any in-flight onFrame call referencing it has returned —
    // after destruction the caller may safely destroy the Subscriber object.
    class Subscription {
    public:
        Subscription() = default;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        ~Subscription();
        bool isActive() const noexcept { return owner_ != nullptr; }

    private:
        friend class JamWideFrameDistributor;
        Subscription(JamWideFrameDistributor* owner, std::uint64_t id) noexcept
            : owner_(owner), id_(id) {}
        JamWideFrameDistributor* owner_ = nullptr;
        std::uint64_t id_ = 0;
    };

    JamWideFrameDistributor();
    ~JamWideFrameDistributor();

    JamWideFrameDistributor(const JamWideFrameDistributor&) = delete;
    JamWideFrameDistributor& operator=(const JamWideFrameDistributor&) = delete;

    // Returns a moveable RAII handle. Caller must keep it alive while the
    // subscriber should receive frames.
    [[nodiscard]] Subscription registerSubscriber(Subscriber* s);

    // Publish a frame to all currently-registered subscribers. Safe to call
    // from any thread (camera-callback thread per JUCE).
    void publish(const juce::Image& image);

    // Peak FPS observed since construction (Q3 — debug accessor exposed in
    // Phase 19-02 status UI). Readable from any thread.
    float getPeakFps() const noexcept { return peakFps_.load(std::memory_order_relaxed); }

private:
    // Called by ~Subscription. Removes the entry from the active map and
    // waits for any in-flight publish iterations holding the entry to drop
    // their reference (inFlight counter back to 0).
    void unregisterAndWait(std::uint64_t id) noexcept;

    struct Entry {
        Subscriber* sub = nullptr;
        // In-flight refcount — incremented while a publish snapshot has hold
        // of this entry and is about to call sub->onFrame; decremented after
        // onFrame returns. unregisterAndWait waits until this reaches 0.
        std::atomic<int> inFlight{0};
    };

    mutable std::mutex regMu_;
    std::condition_variable cv_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Entry>> entries_;
    std::uint64_t nextId_ = 1;

    std::atomic<float> peakFps_{0.0f};
    std::atomic<std::int64_t> lastPublishNanos_{0};
};

} // namespace jamwide
