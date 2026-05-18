#pragma once
// Plan 22-01 Task 2 — SelfVideoTile: self-camera tile in the in-main-view
// grid band. Subscribes to Phase 19's JamWideFrameDistributor via the older
// callback-style Subscriber API (D-08); destruction follows the MEMBER-ORDER
// CONTRACT inherited from Phase 19's CameraPreviewTile.
//
// MEMBER-ORDER CONTRACT: subscription_ MUST be the LAST declared member.
// Members are destroyed in reverse declaration order, so subscription_'s
// dtor runs FIRST, which calls unregisterAndWait — blocking until every
// in-flight onFrame() returns. By the time the mutex and frame members are
// destroyed, no callback can reach them. This contract is mechanically
// verified by tests/test_video_tile_member_order.cpp (offsetof check via
// the VideoTileMemberOrderProbe friend class).
//
// Status overlays — the self-tile hard-codes firstFrameSeen=true and
// synced=true because Phase 19's camera distributor does not expose those
// atomics; "video starting..." is meaningful only for decoder-side state
// machines (Phase 21), not the self preview.

#include "VideoTileBase.h"
#include "../../video/native/JamWideFrameDistributor.h"

#include <juce_events/juce_events.h>

#include <mutex>

namespace jamwide {

class SelfVideoTile : public VideoTileBase,
                     public juce::AsyncUpdater,
                     public JamWideFrameDistributor::Subscriber
{
public:
    explicit SelfVideoTile(JamWideFrameDistributor& distributor);
    ~SelfVideoTile() override;

    SelfVideoTile(const SelfVideoTile&) = delete;
    SelfVideoTile& operator=(const SelfVideoTile&) = delete;

    // JamWideFrameDistributor::Subscriber — called on the camera-callback
    // thread; must not block on UI work.
    void onFrame(const juce::Image& image) override;

    // juce::AsyncUpdater — dispatched on the message thread (HIGH-4 mirror).
    void handleAsyncUpdate() override;

    // juce::Component
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

#ifdef JAMWIDE_BUILD_TESTS
    // Plan 22-01 Task 3 — runtime offset-comparison probe for the
    // MEMBER-ORDER CONTRACT. The probe accesses private members to compute
    // their byte-offset from the object base via pointer subtraction; it
    // asserts subscription_'s offset is strictly greater than every other
    // member's. JAMWIDE_BUILD_TESTS guards the friend out of production
    // builds — ABI unchanged.
    friend class VideoTileMemberOrderProbe;
#endif

private:
    JamWideFrameDistributor& distributor_;

    // Pending frame queued from the camera-callback thread; consumed by
    // handleAsyncUpdate on the message thread.
    std::mutex  pendingMu_;
    juce::Image pendingFrame_;

    // Currently-painted frame; mutated on the message thread inside
    // handleAsyncUpdate, read on the same thread in paint().
    std::mutex  currentMu_;
    juce::Image currentFrame_;

    // Mouse hover state — drives the "hide username strip on hover" affordance.
    bool hovering_ = false;

    // MEMBER-ORDER CONTRACT: subscription_ MUST stay as the LAST member.
    // Its destructor (unregisterAndWait) blocks any in-flight onFrame() before
    // this destructor returns, so the mutex + frame members above are still
    // alive while the camera thread is unwinding through publish().
    JamWideFrameDistributor::Subscription subscription_;
};

} // namespace jamwide
