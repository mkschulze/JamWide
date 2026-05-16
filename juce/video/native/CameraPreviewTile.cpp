#include "CameraPreviewTile.h"
#include "../../ui/JamWideLookAndFeel.h"

namespace jamwide {

CameraPreviewTile::CameraPreviewTile(JamWideFrameDistributor& d)
    : distributor_(d)
{
    // Member-order contract — registerSubscriber returns a moveable handle.
    // subscription_ is the LAST declared member so this assignment lands in
    // the last-to-be-destroyed slot; ~Subscription runs FIRST during dtor
    // (reverse declaration order), blocking any in-flight onFrame() before
    // the mutex/frame members go away.
    subscription_ = distributor_.registerSubscriber(this);
}

CameraPreviewTile::~CameraPreviewTile()
{
    // Member-destruction sequence (per the contract in the header):
    //   1. subscription_ destroyed FIRST (LAST-declared, reverse-order).
    //      Its dtor blocks any in-flight onFrame() referencing `this`. The
    //      camera-callback thread is now unwound for this Subscriber.
    //   2. currentMu_ + currentFrame_ destroyed.
    //   3. pendingMu_ + pendingFrame_ destroyed.
    //   4. ~juce::AsyncUpdater (base) destroyed; cancels any pending update
    //      that has not yet been dispatched (juce::AsyncUpdater's dtor calls
    //      cancelPendingUpdate).
    //   5. ~juce::Component (base) destroyed.
    // No explicit work needed in this body; the destruction order IS the
    // safety contract.
}

void CameraPreviewTile::onFrame(const juce::Image& img)
{
    // Camera-callback thread. juce::Image copy is a refcounted pointer copy;
    // the brief mutex hold is the only material cost.
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        pendingFrame_ = img;
    }
    // HIGH-4 closure — uses AsyncUpdater, NOT the raw-this marshalling
    // pattern that would otherwise be needed. juce::AsyncUpdater coalesces
    // multiple triggers between dispatches into a single handleAsyncUpdate
    // callback. 100 onFrame calls → 1 handleAsyncUpdate → 1 repaint().
    // Cancellation on destruction is automatic (handled by ~AsyncUpdater).
    triggerAsyncUpdate();
}

void CameraPreviewTile::handleAsyncUpdate()
{
    // Message thread.
    juce::Image latest;
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        latest = pendingFrame_;
    }
    if (latest.isValid()) {
        {
            std::lock_guard<std::mutex> lock(currentMu_);
            currentFrame_ = latest;
        }
        repaint();
    }
}

void CameraPreviewTile::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kSurfaceStrip));

    juce::Image toDraw;
    {
        std::lock_guard<std::mutex> lock(currentMu_);
        toDraw = currentFrame_;
    }
    if (toDraw.isValid()) {
        g.drawImage(toDraw, getLocalBounds().toFloat(),
                    juce::RectanglePlacement::centred);
    }
}

} // namespace jamwide
