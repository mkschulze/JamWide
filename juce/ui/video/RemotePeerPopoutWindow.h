#pragma once
// Plan 22-03 Task 1 — RemotePeerPopoutWindow: per-peer DocumentWindow wrapping
// a RemotePeerTile. Mirrors the Phase 19 CameraPreviewWindow shape but binds
// the inner content to Phase 21's `JamWideRemoteFrameDistributor` instead of
// the Phase 19 self-camera distributor.
//
// Lifecycle (codex H3 4-state truth table) — driven by the editor:
//   (A) absent in remotePopouts_  : editor CREATEs and SHOWs a fresh window
//   (B) visible                   : tile ↗ click HIDES (setVisible(false))
//   (C) hidden                    : tile ↗ click RE-SHOWS (non-destructive)
//   (D) destroyed                 : window's unique_ptr.reset(); the
//                                   Subscription dtor blocks for in-flight
//                                   per Phase 21 D-06. Bring-back via the
//                                   placeholder card is the EXCLUSIVE destroy
//                                   path.
//
// Multi-monitor friendliness (T-22-MM): the constructor clamps the supplied
// `initialBounds` against `Desktop::getInstance().getDisplays()`; when no
// display intersects the requested rectangle we fall back to a primary-
// monitor-centered (320×240) default. This eliminates "off-screen popout"
// surprises after monitor topology changes (laptop undock, etc.).
//
// D-09 self-popout note: the SELF tile reuses Phase 19's CameraPreviewWindow
// directly (the editor's `openOrToggleRemotePopout` switches on
// `VideoPopoutTarget::Kind` and routes Self to `drivePreviewWindowVisibility`).
// This window is REMOTE-PEER ONLY.

#include "RemotePeerTile.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace jamwide {

class RemotePeerPopoutWindow : public juce::DocumentWindow,
                               public juce::ComponentListener {
public:
    // Phase 22-03 Task 1 — constructor. The distributor must outlive this
    // window (the processor owns it as a unique_ptr that is destroyed AFTER
    // the editor per Phase 21 Cluster 3 destruction order). LookAndFeel must
    // also outlive this window (editor destructor tears popouts down BEFORE
    // its own LookAndFeel teardown — see RESEARCH Pitfall 3 + the editor
    // destructor's positional ordering invariant). `initialBounds` is clamped
    // against `Desktop::getInstance().getDisplays()` inside the ctor.
    RemotePeerPopoutWindow(JamWideRemoteFrameDistributor& distributor,
                           const juce::String&            username,
                           juce::LookAndFeel*             lookAndFeel,
                           juce::Rectangle<int>           initialBounds);
    ~RemotePeerPopoutWindow() override;

    RemotePeerPopoutWindow(const RemotePeerPopoutWindow&) = delete;
    RemotePeerPopoutWindow& operator=(const RemotePeerPopoutWindow&) = delete;

    // D-17 — clicking the X HIDES the window. The editor's `onCloseRequested`
    // lambda is invoked AFTER setVisible(false); the destroy path is via the
    // placeholder card click (codex H3 state-(C)→(D) transition).
    void closeButtonPressed() override;

    // juce::ComponentListener — fires for our own moves/resizes (and for the
    // content component as a child). We filter `if (&which != this) return;`
    // so the editor only persists window-level bounds.
    void componentMovedOrResized(juce::Component& which,
                                 bool wasMoved,
                                 bool wasResized) override;

    // Accessor for the bound peer username (immutable after construction).
    const juce::String& getUsername() const noexcept { return username_; }

    // Fired AFTER closeButtonPressed flips visibility. Editor's lambda may
    // use this for diagnostic logging or no-op; bring-back-via-placeholder
    // is the explicit destroy path and lives in the editor controller.
    std::function<void()> onCloseRequested;

    // Fired when the window is moved or resized; carries the new bounds so
    // the editor can publish them via `processorRef.setRemotePopoutBounds`.
    // Plan 22-04 wires the persistence; Plan 22-03 uses a no-op stub.
    std::function<void(juce::Rectangle<int>)> onBoundsChanged;

#ifdef JAMWIDE_BUILD_TESTS
    // Plan 22-03 Task 3 — friend probe for the lifetime test. Exposes the
    // non-owning `tilePtr_` and `username_` accessors without leaking them
    // into the production API surface.
    friend class RemotePeerPopoutTestProbe;
#endif

private:
    juce::String     username_;             // immutable after construction
    RemotePeerTile*  tilePtr_ = nullptr;    // non-owning; window owns via setContentOwned
};

} // namespace jamwide
