#pragma once
// Phase 19-01 Task 1 (stub) → Task 3 (full impl).
// Thread-safe frame fan-out with Subscription RAII lifetime guarantees.
// HIGH-2 mitigation: ~Subscription() blocks until in-flight onFrame() exits.

namespace juce { class Image; }

namespace jamwide {

class JamWideFrameDistributor {
public:
    class Subscriber;
    class Subscription;

    JamWideFrameDistributor();
    ~JamWideFrameDistributor();

    // Returns a moveable RAII handle. Caller must keep it alive while the
    // subscriber should receive frames.
    Subscription registerSubscriber(Subscriber* s);

    // Publish a frame to all currently-registered subscribers. Safe to call
    // from any thread (camera-callback thread per JUCE's "any thread" contract).
    void publish(const juce::Image& image);

    // Peak FPS observed since last publish gap reset (Q3 — debug accessor).
    float getPeakFps() const noexcept;
};

} // namespace jamwide
