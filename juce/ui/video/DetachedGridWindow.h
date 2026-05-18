#pragma once
// Plan 22-03 Task 1 — DetachedGridWindow: singleton DocumentWindow wrapping
// a second VideoGridBand instance constructed with Mode::DetachedBand. The
// editor opens at most one DetachedGridWindow at a time (D-16 singleton);
// the wrapping window's `closeButtonPressed` HIDES, and the editor's
// `reattachGrid` controller is the explicit destroy path.
//
// Codex M7 closure (CRITICAL): the inner VideoGridBand is constructed with
// `Mode::DetachedBand` so:
//   1. The inner band's `↗` detach affordance is SUPPRESSED (no recursive
//      detach — that would make no sense).
//   2. The inner band's `×` close affordance is also suppressed; the
//      wrapping DocumentWindow's title-bar X handles close.
//   3. The editor drives `setPeerPoppedOut(name, true)` on BOTH the main
//      band AND the detached band via `getGridBand()` so placeholder state
//      stays consistent across surfaces. Without this, popping out a peer
//      while the detached grid is open would leave the detached band
//      showing the LIVE tile (broken UX).
//
// Plan 22-03 implements the open path. Plan 22-04 wires the bounds
// persistence (this plan uses a no-op `onBoundsChanged` stub).

#include "VideoGridBand.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

class JamWideJuceProcessor;
class ConnectionBar;

namespace jamwide {

class JamWideFrameDistributor;
class JamWideRemoteFrameDistributor;

class DetachedGridWindow : public juce::DocumentWindow,
                           public juce::ComponentListener {
public:
    DetachedGridWindow(JamWideJuceProcessor&            processor,
                       ConnectionBar&                   connectionBar,
                       JamWideFrameDistributor*         selfDistributor,
                       JamWideRemoteFrameDistributor*   remoteDistributor,
                       juce::LookAndFeel*               lookAndFeel,
                       juce::Rectangle<int>             initialBounds);
    ~DetachedGridWindow() override;

    DetachedGridWindow(const DetachedGridWindow&) = delete;
    DetachedGridWindow& operator=(const DetachedGridWindow&) = delete;

    // D-17 mirror — closing HIDES; the editor's `reattachGrid` is the
    // explicit destroy path. (For the detached-grid the editor's default
    // policy is `detachedGrid_.reset()` on close — see Plan 22-03 Task 2.)
    void closeButtonPressed() override;

    // juce::ComponentListener — filtered to ourselves.
    void componentMovedOrResized(juce::Component& which,
                                 bool wasMoved,
                                 bool wasResized) override;

    // M7 codex closure — the editor needs to drive setPeerPoppedOut /
    // setDetachedActive on the inner band so placeholder state stays
    // synchronized between the main band and the detached band. The window
    // OWNS the band via setContentOwned; this accessor returns a NON-OWNING
    // pointer for editor-side reads only. Use only on the message thread.
    VideoGridBand* getGridBand() const noexcept { return gridPtr_; }

    // Fired AFTER closeButtonPressed flips visibility. Editor's lambda
    // typically calls detachedGrid_.reset() (full destroy per D-18 default).
    std::function<void()> onCloseRequested;

    // Fired when the window is moved or resized; persistence wired in 22-04.
    std::function<void(juce::Rectangle<int>)> onBoundsChanged;

private:
    VideoGridBand* gridPtr_ = nullptr;   // non-owning; window owns via setContentOwned
};

} // namespace jamwide
