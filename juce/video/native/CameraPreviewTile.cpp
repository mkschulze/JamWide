#include "CameraPreviewTile.h"

// Phase 19-02 Task 1 baseline: minimal stubs needed for the CameraPreviewWindow
// owner type to be complete (its setContentOwned call constructs a tile).
// Task 2 Edit 1 fills in the AsyncUpdater dispatch path, repaint coalescing,
// and the Subscription registration in the ctor.

namespace jamwide {

CameraPreviewTile::CameraPreviewTile(JamWideFrameDistributor& d)
    : distributor_(d)
{
    // Task 2 attaches: subscription_ = distributor_.registerSubscriber(this).
}

CameraPreviewTile::~CameraPreviewTile() = default;

void CameraPreviewTile::onFrame(const juce::Image& /*image*/)
{
    // Task 2 implements: copy under pendingMu_ + triggerAsyncUpdate().
}

void CameraPreviewTile::handleAsyncUpdate()
{
    // Task 2 implements: marshal pendingFrame_ → currentFrame_ + repaint().
}

void CameraPreviewTile::paint(juce::Graphics& /*g*/)
{
    // Task 2 implements: drawImage(currentFrame_, ...) centred.
}

} // namespace jamwide
