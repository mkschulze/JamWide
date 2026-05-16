#pragma once
// Phase 19-02 Task 2 — per-user camera popout window.
//
// Wraps a CameraPreviewTile in a juce::DocumentWindow with the dark-theme
// LookAndFeel chrome (D-08), 4:3 fixed aspect (D-07), and a non-destructive
// close behaviour (D-09: clicking the X HIDES the window without affecting
// the underlying capture state machine).
#include "CameraPreviewTile.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace jamwide {

class CameraPreviewWindow : public juce::DocumentWindow,
                            public juce::ComponentListener {
public:
    CameraPreviewWindow(JamWideFrameDistributor& distributor,
                        juce::LookAndFeel* lookAndFeel,
                        juce::Rectangle<int> initialBounds);
    ~CameraPreviewWindow() override;

    CameraPreviewWindow(const CameraPreviewWindow&) = delete;
    CameraPreviewWindow& operator=(const CameraPreviewWindow&) = delete;

    // D-09: close hides, does NOT destroy.
    void closeButtonPressed() override;

    // juce::ComponentListener — we subscribe to ourselves so we can publish
    // bounds changes back to the processor for persistence.
    void componentMovedOrResized(juce::Component& which,
                                 bool wasMoved,
                                 bool wasResized) override;

    void setDeviceName(const juce::String& name);

    // Fired when the close button is clicked (visibility already flipped).
    // Currently a no-op hook for the editor; future plans can use it.
    std::function<void()> onCloseRequested;

    // Fired when the window is moved or resized; carries the new bounds so
    // the editor can persist them via setCameraPopoutBounds().
    std::function<void(juce::Rectangle<int>)> onBoundsChanged;

private:
    CameraPreviewTile* tilePtr_ = nullptr;   // non-owning; the window owns it via setContentOwned
};

} // namespace jamwide
