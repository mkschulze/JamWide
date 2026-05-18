#include "SelfVideoTile.h"

namespace jamwide {

SelfVideoTile::SelfVideoTile(JamWideFrameDistributor& d)
    : distributor_(d)
{
    // MEMBER-ORDER CONTRACT — registerSubscriber returns a moveable handle.
    // subscription_ is the LAST declared member so this assignment lands in
    // the last-to-be-destroyed slot; ~Subscription runs FIRST during dtor
    // (reverse declaration order), blocking any in-flight onFrame() before
    // the mutex/frame members go away. Mirrors CameraPreviewTile.cpp:6-15
    // verbatim per PATTERNS.md "exact (same Phase 19 distributor, same
    // MEMBER-ORDER CONTRACT)".
    subscription_ = distributor_.registerSubscriber(this);
}

SelfVideoTile::~SelfVideoTile()
{
    // Member-destruction sequence (per the contract in the header):
    //   1. subscription_ destroyed FIRST (LAST-declared, reverse-order).
    //      Its dtor blocks any in-flight onFrame() referencing `this`. The
    //      camera-callback thread is now unwound for this Subscriber.
    //   2. hovering_ destroyed (trivial).
    //   3. currentMu_ + currentFrame_ destroyed.
    //   4. pendingMu_ + pendingFrame_ destroyed.
    //   5. ~juce::AsyncUpdater (base) destroyed; cancels any pending update
    //      that has not yet been dispatched.
    //   6. ~VideoTileBase / ~juce::Component (bases) destroyed.
}

void SelfVideoTile::onFrame(const juce::Image& img)
{
    // Camera-callback thread. juce::Image copy is a refcounted pointer copy;
    // the brief mutex hold is the only material cost.
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        pendingFrame_ = img;
    }
    // HIGH-4 mirror — uses AsyncUpdater coalescing, NOT raw-this marshalling.
    triggerAsyncUpdate();
}

void SelfVideoTile::handleAsyncUpdate()
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

void SelfVideoTile::paint(juce::Graphics& g)
{
    juce::Image toDraw;
    {
        std::lock_guard<std::mutex> lock(currentMu_);
        toDraw = currentFrame_;
    }
    // Self-tile: Phase 19's camera distributor exposes no first_frame_seen /
    // hold_count / synced atomics — hard-code firstFrameSeen=true and
    // synced=true so the "video starting..." / "syncing..." overlays never
    // appear over the self preview (they are decoder-side semantics only).
    paintCommon(g,
                toDraw,
                /*firstFrameSeen*/ true,
                /*holdCount*/      0,
                /*synced*/         true,
                /*username*/       juce::String("You"),
                /*hovering*/       hovering_);
}

void SelfVideoTile::mouseDown(const juce::MouseEvent& e)
{
    // M5 typed-target dispatch (codex review closure) — emit a Self target
    // (NOT the legacy no-arg form, NOT any sentinel-string-bearing
    // juce::String form). The downstream consumer
    // JamWideJuceEditor::openOrToggleRemotePopout switches on t.kind == Self
    // and routes to drivePreviewWindowVisibility (D-09).
    if (popoutIconHitTest_(e) && onPopoutClicked_) {
        onPopoutClicked_({VideoPopoutTargetKind::Self, juce::String{}});
    }
}

void SelfVideoTile::mouseEnter(const juce::MouseEvent&)
{
    hovering_ = true;
    repaint();
}

void SelfVideoTile::mouseExit(const juce::MouseEvent&)
{
    hovering_ = false;
    repaint();
}

} // namespace jamwide
