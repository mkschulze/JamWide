#pragma once
// Plan 22-01 Task 2 — VideoTileBase: shared tile chrome layer.
//
// Concrete tile classes (SelfVideoTile, RemotePeerTile) inherit from this and
// hold their own Subscription as the LAST declared member (MEMBER-ORDER
// CONTRACT — see SelfVideoTile.h / RemotePeerTile.h). The base owns:
//   - paintCommon: background fill, 4:3 letterbox frame paint, username strip,
//                  popout ↗ icon, Phase 21 status overlays ("video starting...",
//                  "syncing...").
//   - popoutIconHitTest_: hit-test for the top-right ↗ icon.
//   - onPopoutClicked_: typed callback receiving a VideoPopoutTarget (kind +
//                       username) — replaces the legacy magic-string sentinel
//                       (codex M5 closure).
//
// No subscription member at this level — subscriptions belong to derived
// classes per D-08. The base is JUCE-aware (inherits juce::Component) but
// holds no resources that require destructor ordering with subscriptions.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace jamwide {

// M5 typed-callback target (codex review closure) — replaces an earlier
// iteration's magic-string sentinel approach. The enum is the single source
// of truth; downstream consumers (JamWideJuceEditor::openOrToggleRemotePopout)
// switch on `kind` directly. No string-comparison branch exists, so a peer
// whose NINJAM username happens to collide with any sentinel literal cannot
// spoof the Self branch (T-22-MO-4 mitigation).
enum class VideoPopoutTargetKind {
    Self,       // Click on the self-tile's ↗ — reuses Phase 19's CameraPreviewWindow (D-09)
    RemotePeer  // Click on a peer tile's ↗ — opens a fresh RemotePeerPopoutWindow keyed by username
};

struct VideoPopoutTarget {
    VideoPopoutTargetKind kind;
    juce::String          username;   // empty when kind == Self; non-empty when kind == RemotePeer
};

class VideoTileBase : public juce::Component
{
public:
    VideoTileBase() = default;
    ~VideoTileBase() override = default;

    VideoTileBase(const VideoTileBase&) = delete;
    VideoTileBase& operator=(const VideoTileBase&) = delete;

    // Set the typed popout callback. Derived-class mouseDown invokes this
    // when the user clicks the top-right ↗ hit region.
    void setOnPopoutClicked(std::function<void(VideoPopoutTarget)> cb)
    {
        onPopoutClicked_ = std::move(cb);
    }

protected:
    // Shared paint helper. Derived classes call this from their paint() after
    // snapshotting the current frame from their own front-buffer.
    //   - frame:           latest decoded image (may be invalid → blank tile);
    //   - firstFrameSeen:  Phase 21 D-19 — gates "video starting..." overlay;
    //   - holdCount:       Phase 21 D-17 — drives "syncing..." when >= 2 && !synced;
    //   - synced:          Phase 21 — suppresses "syncing..." once aligned;
    //   - username:        rendered in the bottom strip (unless hovering);
    //   - hovering:        when true, suppresses the bottom strip so the full
    //                      frame is visible.
    void paintCommon(juce::Graphics& g,
                     const juce::Image& frame,
                     bool firstFrameSeen,
                     int  holdCount,
                     bool synced,
                     const juce::String& username,
                     bool hovering);

    // Returns true when the mouse event is inside the ~16x16 top-right
    // popout ↗ hit region. Derived classes call this from mouseDown to
    // decide whether to dispatch onPopoutClicked_.
    bool popoutIconHitTest_(const juce::MouseEvent& e) const noexcept;

    // M5 typed callback (codex review closure).
    std::function<void(VideoPopoutTarget)> onPopoutClicked_;
};

} // namespace jamwide
