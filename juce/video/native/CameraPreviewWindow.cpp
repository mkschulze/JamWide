#include "CameraPreviewWindow.h"
#include "../../ui/JamWideLookAndFeel.h"

// Phase 19-02 Task 1 baseline: minimal stubs needed for the editor's
// previewWindow_ unique_ptr to instantiate. Task 2 fills in the constructor
// body (LookAndFeel attach, aspect-ratio constrainer, owned tile, bounds
// listener) and the close-button / device-name implementations.

namespace jamwide {

CameraPreviewWindow::CameraPreviewWindow(JamWideFrameDistributor& /*distributor*/,
                                        juce::LookAndFeel* /*lookAndFeel*/,
                                        juce::Rectangle<int> /*initialBounds*/)
    : juce::DocumentWindow("JamWide — Camera",
                           juce::Colour(JamWideLookAndFeel::kSurfaceStrip),
                           juce::DocumentWindow::closeButton)
{
    // Body filled in Task 2 Edit 2.
    setVisible(false);
}

CameraPreviewWindow::~CameraPreviewWindow() = default;

void CameraPreviewWindow::closeButtonPressed()
{
    // Filled in Task 2 Edit 2 (D-09 — hide, do not destroy).
    setVisible(false);
}

void CameraPreviewWindow::componentMovedOrResized(juce::Component& /*which*/,
                                                  bool /*wasMoved*/,
                                                  bool /*wasResized*/)
{
    // Filled in Task 2 Edit 2.
}

void CameraPreviewWindow::setDeviceName(const juce::String& /*name*/)
{
    // Filled in Task 2 Edit 2.
}

} // namespace jamwide
