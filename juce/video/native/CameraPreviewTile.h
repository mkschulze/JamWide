#pragma once
// Phase 19-02 Task 2 — in-grid preview tile with juce::AsyncUpdater (HIGH-4).
//
// HIGH-4 mitigation: the tile inherits juce::AsyncUpdater rather than calling
// juce::MessageManager::callAsync(this, ...). onFrame() copies the latest
// juce::Image into pendingFrame_ under std::mutex and calls
// triggerAsyncUpdate(); handleAsyncUpdate() runs on the message thread.
// Multiple triggers between dispatches coalesce to a single callback, so
// frame bursts (e.g. 100 frames pushed in quick succession) result in
// exactly one repaint cycle. juce::AsyncUpdater cancels pending callbacks
// automatically at destruction, so no UAF is possible if the tile is
// destroyed while a publish() is in flight (the Subscription destructor
// blocks until in-flight onFrame returns; see member-order note below).
//
// MEMBER-ORDER CONTRACT: subscription_ MUST be the LAST declared member.
// Members are destroyed in reverse declaration order, so subscription_'s
// dtor runs FIRST, which calls unregisterAndWait — blocking until every
// in-flight onFrame() returns. By the time the mutex and frame members
// are destroyed, no callback can reach them.
#include "JamWideFrameDistributor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>

#include <mutex>

namespace jamwide {

class CameraPreviewTile : public juce::Component,
                          public juce::AsyncUpdater,
                          public JamWideFrameDistributor::Subscriber {
public:
    explicit CameraPreviewTile(JamWideFrameDistributor& distributor);
    ~CameraPreviewTile() override;

    CameraPreviewTile(const CameraPreviewTile&) = delete;
    CameraPreviewTile& operator=(const CameraPreviewTile&) = delete;

    // JamWideFrameDistributor::Subscriber — called on the camera-callback
    // thread (per JUCE's "any thread" contract). MUST NOT block on UI work.
    void onFrame(const juce::Image& image) override;

    // juce::AsyncUpdater — dispatched on the message thread.
    void handleAsyncUpdate() override;

    // juce::Component
    void paint(juce::Graphics& g) override;

private:
    JamWideFrameDistributor& distributor_;

    // Pending frame queued from the camera-callback thread; consumed by
    // handleAsyncUpdate on the message thread.
    std::mutex pendingMu_;
    juce::Image pendingFrame_;

    // Currently-painted frame; mutated on the message thread inside
    // handleAsyncUpdate, read in paint() on the same thread.
    std::mutex currentMu_;
    juce::Image currentFrame_;

    // MEMBER-ORDER CONTRACT: subscription_ MUST stay as the LAST member.
    // Its destructor (unregisterAndWait) blocks any in-flight onFrame()
    // before this destructor returns to the runtime, which means the
    // mutexes + frame members above are still alive while the camera
    // thread is unwinding through the publish() iteration.
    JamWideFrameDistributor::Subscription subscription_;
};

} // namespace jamwide
