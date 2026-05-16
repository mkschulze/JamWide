#include "CameraPreviewWindow.h"
#include "../../ui/JamWideLookAndFeel.h"

namespace jamwide {

CameraPreviewWindow::CameraPreviewWindow(JamWideFrameDistributor& distributor,
                                        juce::LookAndFeel* lookAndFeel,
                                        juce::Rectangle<int> initialBounds)
    : juce::DocumentWindow("JamWide — Camera",
                           juce::Colour(JamWideLookAndFeel::kSurfaceStrip),
                           juce::DocumentWindow::closeButton
                             | juce::DocumentWindow::minimiseButton)
{
    // D-08 — custom title bar so JamWideLookAndFeel paints dark chrome.
    // Note: on some macOS builds JUCE forces native title bars regardless of
    // this flag; the dark CONTENT area is the user-visible portion in either
    // case, so this is a soft preference rather than a hard guarantee.
    setUsingNativeTitleBar(false);
    if (lookAndFeel) {
        setLookAndFeel(lookAndFeel);
    }

    // D-07 — 4:3 fixed aspect; 240x180 min / 2560x1920 max. Calling
    // setResizable(true, true) BEFORE getConstrainer() makes the constrainer
    // available (otherwise it is null).
    setResizable(true, true);
    if (auto* constrainer = getConstrainer()) {
        constrainer->setFixedAspectRatio(4.0 / 3.0);
        constrainer->setSizeLimits(240, 180, 2560, 1920);
    }

    // Owned tile — distributor must outlive this window. The processor's
    // unique_ptr<JamWideFrameDistributor> is constructed BEFORE the editor
    // and destroyed AFTER it, so this is safe by construction.
    auto tile = std::make_unique<CameraPreviewTile>(distributor);
    tilePtr_ = tile.get();
    setContentOwned(tile.release(), /*resizeToFitWhenContentChangesSize*/ true);

    // Default size 320x240 unless the caller overrides via initialBounds.
    setBounds(initialBounds);

    // Start hidden — onCameraStateChanged(Capturing) makes the window visible
    // via JamWideJuceEditor::drivePreviewWindowVisibility (D-09 orthogonality).
    setVisible(false);

    // Subscribe to our own bounds changes so we can publish them back to the
    // editor for persistence (D-25 — popout X/Y/W/H).
    addComponentListener(this);
}

CameraPreviewWindow::~CameraPreviewWindow()
{
    removeComponentListener(this);
    // Detach LookAndFeel BEFORE the editor's LookAndFeel is torn down. The
    // editor's destructor resets previewWindow_ BEFORE clearing its own
    // LookAndFeel, so the pointer we hold is valid through this call.
    setLookAndFeel(nullptr);
}

void CameraPreviewWindow::closeButtonPressed()
{
    // D-09 — clicking X does NOT stop capture. We just hide; the state
    // machine continues feeding the distributor (no consumer is fine);
    // the next Camera-button click reopens the popout per MEDIUM-1.
    setVisible(false);
    if (onCloseRequested) onCloseRequested();
}

void CameraPreviewWindow::componentMovedOrResized(juce::Component& which,
                                                  bool /*wasMoved*/,
                                                  bool /*wasResized*/)
{
    // ComponentListener fires for our own moves/resizes AND for the content
    // component (since we are the parent). Filter to events for ourselves
    // so the editor only persists window-level bounds.
    if (&which != this) return;
    if (onBoundsChanged) onBoundsChanged(getBounds());
}

void CameraPreviewWindow::setDeviceName(const juce::String& name)
{
    setName("JamWide — Camera: " + name);
}

} // namespace jamwide
