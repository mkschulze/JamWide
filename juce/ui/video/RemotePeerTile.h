#pragma once
// Plan 22-01 Task 2 — RemotePeerTile: per-peer decoded-video tile. Subscribes
// to Phase 21's JamWideRemoteFrameDistributor via the RAII Subscription handle
// (D-08); destruction follows the MEMBER-ORDER CONTRACT inherited from Phase
// 19's CameraPreviewTile.
//
// MEMBER-ORDER CONTRACT: subscription_ MUST be the LAST declared member.
// Members are destroyed in reverse declaration order; subscription_'s dtor
// runs FIRST and blocks any in-flight handleAsyncUpdate via Phase 21's
// removeListener + inFlightCv_ wait (HIGH-2 mirror). This contract is
// mechanically verified by tests/test_video_tile_member_order.cpp.
//
// Sink resolution: per RESEARCH Pitfall 6 we DO NOT cache the PeerVideoSink*
// — the peer may leave and rejoin during the tile's lifetime, invalidating
// any cached pointer. Re-query findSink in handleAsyncUpdate / paint.

#include "VideoTileBase.h"
#include "../../video/distributor/JamWideRemoteFrameDistributor.h"

#include <juce_events/juce_events.h>

namespace jamwide {

class RemotePeerTile : public VideoTileBase,
                      public juce::AsyncUpdater
{
public:
    RemotePeerTile(JamWideRemoteFrameDistributor& distributor,
                   juce::String                   username,
                   int                            chidx = 1);
    ~RemotePeerTile() override;

    RemotePeerTile(const RemotePeerTile&) = delete;
    RemotePeerTile& operator=(const RemotePeerTile&) = delete;

    // juce::AsyncUpdater — dispatched on the message thread when the
    // distributor's onRepaint lambda fires. Body is repaint() only;
    // paint() handles the actual front-buffer snapshot.
    void handleAsyncUpdate() override;

    // juce::Component
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // Accessor for the bound username (used by callers like the grid container
    // to key tiles and by the popout-target dispatch to identify the peer).
    const juce::String& getUsername() const noexcept { return username_; }

#ifdef JAMWIDE_BUILD_TESTS
    // Plan 22-01 Task 3 — runtime offset-comparison probe for the
    // MEMBER-ORDER CONTRACT. See SelfVideoTile.h for the full rationale.
    friend class VideoTileMemberOrderProbe;
#endif

private:
    JamWideRemoteFrameDistributor& distributor_;
    juce::String                   username_;
    int                            chidx_;
    bool                           hovering_ = false;

    // MEMBER-ORDER CONTRACT: subscription_ MUST stay as the LAST member.
    // Its destructor (Phase 21's removeListener + inFlightCv_ wait) blocks
    // any in-flight onRepaint callback before this destructor returns.
    JamWideRemoteFrameDistributor::Subscription subscription_;
};

} // namespace jamwide
