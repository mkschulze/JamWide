#include "RemotePeerTile.h"
#include "../../video/distributor/PeerVideoSink.h"

namespace jamwide {

RemotePeerTile::RemotePeerTile(JamWideRemoteFrameDistributor& d,
                               juce::String                   username,
                               int                            chidx)
    : distributor_(d)
    , username_(std::move(username))
    , chidx_(chidx)
{
    // MEMBER-ORDER CONTRACT — subscribeToPeer returns a moveable RAII handle.
    // subscription_ is the LAST declared member so this assignment lands in
    // the last-to-be-destroyed slot; ~Subscription runs FIRST during dtor
    // and waits for in-flight handleAsyncUpdate via Phase 21's removeListener
    // + inFlightCv_ wait (HIGH-2 mirror).
    //
    // The lambda captures `this` and posts a triggerAsyncUpdate() onto the
    // tile's own AsyncUpdater. Phase 21's listener fan-out runs on the
    // message thread (PeerVideoSink::handleAsyncUpdate), so the callback is
    // also on the message thread — but using AsyncUpdater coalesces multiple
    // back-to-back triggers into a single repaint cycle.
    subscription_ = distributor_.subscribeToPeer(
        username_.toRawUTF8(),
        chidx_,
        [this]{ triggerAsyncUpdate(); });
}

RemotePeerTile::~RemotePeerTile()
{
    // Member-destruction sequence (per the contract in the header):
    //   1. subscription_ destroyed FIRST — removeListener + inFlightCv_ wait.
    //   2. hovering_ destroyed (trivial).
    //   3. chidx_ + username_ destroyed.
    //   4. distributor_ reference released (no-op).
    //   5. ~juce::AsyncUpdater (base) destroyed; cancels any pending update.
    //   6. ~VideoTileBase / ~juce::Component (bases) destroyed.
}

void RemotePeerTile::handleAsyncUpdate()
{
    // Message thread. paint() handles the snapshot under bufferLock — this
    // body is just the AsyncUpdater→repaint trampoline.
    repaint();
}

void RemotePeerTile::paint(juce::Graphics& g)
{
    // Re-query the sink every paint — peer may leave + rejoin during the
    // tile's lifetime, invalidating any cached pointer (RESEARCH Pitfall 6).
    auto* sink = distributor_.findSink(username_.toRawUTF8(), chidx_);

    if (sink == nullptr) {
        // No sink yet (subscribe-before-peer-exists, or peer just left).
        // Render the username strip + "video starting..." overlay.
        paintCommon(g,
                    juce::Image{},
                    /*firstFrameSeen*/ false,
                    /*holdCount*/      0,
                    /*synced*/         false,
                    /*username*/       username_,
                    /*hovering*/       hovering_);
        return;
    }

    // Snapshot the front image under bufferLock (brief — JUCE Image is
    // refcounted so the copy is O(1)). Then release the lock BEFORE the
    // drawImage call inside paintCommon (T-22-MO-2 mitigation).
    juce::Image localFront;
    {
        const juce::ScopedLock sl(sink->bufferLock);
        localFront = sink->image_front;
    }

    // Read atomic status fields lock-free (Phase 21 D-20).
    const bool firstFrame = sink->first_frame_seen.load(std::memory_order_acquire);
    const int  holds      = sink->hold_count.load(std::memory_order_relaxed);
    const bool synced     = sink->synced.load(std::memory_order_relaxed);

    paintCommon(g,
                localFront,
                firstFrame,
                holds,
                synced,
                username_,
                hovering_);
}

void RemotePeerTile::mouseDown(const juce::MouseEvent& e)
{
    // M5 typed-target dispatch (codex review closure) — emit a RemotePeer
    // target with the bound username (NOT the legacy bare-juce::String form).
    // The downstream consumer JamWideJuceEditor::openOrToggleRemotePopout
    // switches on t.kind == RemotePeer and constructs a fresh
    // RemotePeerPopoutWindow keyed by t.username.
    if (popoutIconHitTest_(e) && onPopoutClicked_) {
        onPopoutClicked_({VideoPopoutTargetKind::RemotePeer, username_});
    }
}

void RemotePeerTile::mouseEnter(const juce::MouseEvent&)
{
    hovering_ = true;
    repaint();
}

void RemotePeerTile::mouseExit(const juce::MouseEvent&)
{
    hovering_ = false;
    repaint();
}

} // namespace jamwide
