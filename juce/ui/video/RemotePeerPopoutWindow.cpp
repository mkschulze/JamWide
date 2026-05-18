#include "RemotePeerPopoutWindow.h"
#include "../JamWideLookAndFeel.h"

namespace jamwide {

namespace {

// T-22-MM mitigation — multi-monitor topology change. Clamps `requested`
// against the union of every visible display's userArea; if no display
// intersects (e.g. the user closed a second monitor whose persisted bounds
// pointed at), recentre on the primary display with a sensible default size.
//
// Implementation per RESEARCH Pattern 3 (lines 467-478):
//   1. Walk `Desktop::getInstance().getDisplays().displays`.
//   2. If any display's `userArea` intersects `requested`, return `requested`
//      unchanged — partial-overlap windows are still user-recoverable
//      (intersectsRectangle accepts ANY non-empty overlap, unlike
//      containsRectangle which would force-recentre multi-monitor
//      straddle windows — bad UX).
//   3. Otherwise, return a primary-monitor-centered fallback at `defaultSize`.
juce::Rectangle<int> clampToVisibleDisplays_(juce::Rectangle<int> requested,
                                              juce::Rectangle<int> primaryFallback)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    if (displays.isEmpty())
        return primaryFallback;    // no displays known — pathological but safe

    for (const auto& d : displays)
    {
        if (d.userArea.intersects(requested))
            return requested;       // ANY overlap = keep user-recoverable
    }

    // No display contains any pixel of `requested` — fall back centered on
    // the primary monitor.
    return primaryFallback;
}

} // anonymous namespace

RemotePeerPopoutWindow::RemotePeerPopoutWindow(JamWideRemoteFrameDistributor& distributor,
                                                const juce::String&            username,
                                                juce::LookAndFeel*             lookAndFeel,
                                                juce::Rectangle<int>           initialBounds)
    : juce::DocumentWindow(juce::String::fromUTF8("JamWide \xE2\x80\x94 ") + username,    // U+2014 EM DASH
                           juce::Colour(JamWideLookAndFeel::kSurfaceStrip),
                           juce::DocumentWindow::closeButton
                             | juce::DocumentWindow::minimiseButton)
    , username_(username)
{
    // D-08 — custom title bar so JamWideLookAndFeel paints dark chrome. On
    // some macOS builds JUCE forces native title bars regardless of this
    // flag; the dark CONTENT area is the user-visible portion in either case,
    // so this is a soft preference rather than a hard guarantee.
    setUsingNativeTitleBar(false);
    if (lookAndFeel) {
        setLookAndFeel(lookAndFeel);
    }

    // D-07 mirror — 4:3 fixed aspect; 240×180 min / 2560×1920 max. Calling
    // setResizable(true, true) BEFORE getConstrainer() makes the constrainer
    // available.
    setResizable(true, true);
    if (auto* constrainer = getConstrainer()) {
        constrainer->setFixedAspectRatio(4.0 / 3.0);
        constrainer->setSizeLimits(240, 180, 2560, 1920);
    }

    // Owned tile — distributor must outlive this window. The processor owns
    // the JamWideRemoteFrameDistributor as a unique_ptr that is destroyed
    // AFTER the editor (Phase 21 Cluster 3 lifetime invariant: client.reset()
    // before remoteFrameDistributor.reset()), so this is safe by construction.
    // chidx=1 mirrors VideoGridBand's default per-peer subscribe key.
    auto tile = std::make_unique<RemotePeerTile>(distributor, username, /*chidx*/ 1);
    tilePtr_ = tile.get();
    setContentOwned(tile.release(), /*resizeToFitWhenContentChangesSize*/ true);

    // T-22-MM multi-monitor clamp — compute the primary-monitor-centered
    // fallback before clamping so we have a sane default if NO display
    // intersects.
    juce::Rectangle<int> primaryFallback{100, 100, 320, 240};
    if (auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto userArea = primary->userArea;
        primaryFallback = juce::Rectangle<int>{
            userArea.getCentreX() - 160,
            userArea.getCentreY() - 120,
            320, 240};
    }
    const auto clamped = clampToVisibleDisplays_(initialBounds, primaryFallback);
    setBounds(clamped);

    // Always start hidden (D-14 mirror). The editor's `openOrToggleRemotePopout`
    // calls setVisible(true) immediately on creation to transition from
    // codex-H3 state (A) → (B).
    setVisible(false);

    // Subscribe to our own bounds changes so we can publish them back to the
    // editor for persistence (Plan 22-04 territory; this plan wires a no-op).
    addComponentListener(this);
}

RemotePeerPopoutWindow::~RemotePeerPopoutWindow()
{
    removeComponentListener(this);
    // Detach LookAndFeel BEFORE the editor's LookAndFeel is torn down. The
    // editor's destructor resets `remotePopouts_` BEFORE clearing its own
    // LookAndFeel (RESEARCH Pitfall 3 positional ordering invariant), so the
    // pointer we hold is valid through this call.
    setLookAndFeel(nullptr);
}

void RemotePeerPopoutWindow::closeButtonPressed()
{
    // D-17 / codex H3 — clicking X does NOT destroy. We just hide; the
    // underlying RemotePeerTile keeps its Subscription alive (so frames keep
    // arriving) and the placeholder card stays mounted (poppedOutPeers_
    // membership is unchanged). The user can re-show via tile ↗
    // (state (C) → (B)) or destroy via placeholder click (state (C) → (D)).
    setVisible(false);
    if (onCloseRequested) onCloseRequested();
}

void RemotePeerPopoutWindow::componentMovedOrResized(juce::Component& which,
                                                     bool /*wasMoved*/,
                                                     bool /*wasResized*/)
{
    // ComponentListener fires for our own moves/resizes AND for the content
    // component (since we are the parent). Filter to events for ourselves so
    // the editor only persists window-level bounds.
    if (&which != this) return;
    if (onBoundsChanged) onBoundsChanged(getBounds());
}

} // namespace jamwide
