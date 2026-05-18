#include "DetachedGridWindow.h"
#include "../JamWideLookAndFeel.h"
#include "../../JamWideJuceProcessor.h"
#include "../../video/native/JamWideFrameDistributor.h"
#include "../../video/distributor/JamWideRemoteFrameDistributor.h"

namespace jamwide {

namespace {

// Same T-22-MM multi-monitor clamp as RemotePeerPopoutWindow but with a
// larger default size (800×450) matching the detached-grid's wider visual
// footprint.
juce::Rectangle<int> clampToVisibleDisplays_(juce::Rectangle<int> requested,
                                              juce::Rectangle<int> primaryFallback)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    if (displays.isEmpty())
        return primaryFallback;

    for (const auto& d : displays)
    {
        if (d.userArea.intersects(requested))
            return requested;
    }
    return primaryFallback;
}

} // anonymous namespace

DetachedGridWindow::DetachedGridWindow(JamWideJuceProcessor&            processor,
                                       ConnectionBar&                   connectionBar,
                                       JamWideFrameDistributor*         selfDistributor,
                                       JamWideRemoteFrameDistributor*   remoteDistributor,
                                       juce::LookAndFeel*               lookAndFeel,
                                       juce::Rectangle<int>             initialBounds)
    : juce::DocumentWindow("JamWide \xE2\x80\x94 Video Grid",   // D-16 fixed title; U+2014 EM DASH
                           juce::Colour(JamWideLookAndFeel::kSurfaceStrip),
                           juce::DocumentWindow::closeButton
                             | juce::DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar(false);
    if (lookAndFeel) {
        setLookAndFeel(lookAndFeel);
    }

    // Detached grid is NOT 4:3 aspect-locked (it hosts an N×M tile flow that
    // adapts to the wider window). Size limits 320×240 .. 4096×4096.
    setResizable(true, true);
    if (auto* constrainer = getConstrainer()) {
        constrainer->setSizeLimits(320, 240, 4096, 4096);
    }

    // M7 codex closure — construct the inner band with `Mode::DetachedBand`.
    // The inner band's `↗` / `×` icons are SUPPRESSED in paint(); the
    // wrapping DocumentWindow handles close. The editor calls
    // `getGridBand()->setPeerPoppedOut(...)` to keep placeholder state
    // consistent across main and detached bands.
    auto grid = std::make_unique<VideoGridBand>(
        processor, connectionBar, selfDistributor, remoteDistributor,
        VideoGridBand::Mode::DetachedBand);
    gridPtr_ = grid.get();
    setContentOwned(grid.release(), /*resizeToFitWhenContentChangesSize*/ true);

    // T-22-MM multi-monitor clamp with detached-grid default size.
    juce::Rectangle<int> primaryFallback{200, 200, 800, 450};
    if (auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto userArea = primary->userArea;
        primaryFallback = juce::Rectangle<int>{
            userArea.getCentreX() - 400,
            userArea.getCentreY() - 225,
            800, 450};
    }
    const auto clamped = clampToVisibleDisplays_(initialBounds, primaryFallback);
    setBounds(clamped);

    setVisible(false);
    addComponentListener(this);
}

DetachedGridWindow::~DetachedGridWindow()
{
    removeComponentListener(this);
    setLookAndFeel(nullptr);
}

void DetachedGridWindow::closeButtonPressed()
{
    // Mirror of RemotePeerPopoutWindow::closeButtonPressed — HIDE not destroy.
    // The editor's `onCloseRequested` lambda typically calls
    // `detachedGrid_.reset()` (full destroy per D-18 default policy).
    setVisible(false);
    if (onCloseRequested) onCloseRequested();
}

void DetachedGridWindow::componentMovedOrResized(juce::Component& which,
                                                  bool /*wasMoved*/,
                                                  bool /*wasResized*/)
{
    if (&which != this) return;
    if (onBoundsChanged) onBoundsChanged(getBounds());
}

} // namespace jamwide
