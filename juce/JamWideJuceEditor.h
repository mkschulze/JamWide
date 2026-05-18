#pragma once
#include <JuceHeader.h>
#include "JamWideJuceProcessor.h"
#include "ui/JamWideLookAndFeel.h"
#include "ui/ConnectionBar.h"
#include "ui/ChatPanel.h"
#include "ui/BeatBar.h"
#include "ui/ChannelStripArea.h"
#include "ui/SessionInfoStrip.h"
#include "ui/ServerBrowserOverlay.h"
#include "ui/LicenseDialog.h"
#include "video/VideoPrivacyDialog.h"
#include "video/native/JamWideCameraDevice.h"
#include "video/native/CameraFallbackCause.h"
#include "video/native/CameraStatusDialog.h"
#include "ui/video/VideoGridBand.h"

namespace jamwide {
    class CameraPreviewWindow;
    class NativeCameraPrivacyDialog;
}

class JamWideJuceEditor : public juce::AudioProcessorEditor,
                           public jamwide::JamWideCameraDevice::FallbackListener,
                           private juce::Timer
{
public:
    explicit JamWideJuceEditor(JamWideJuceProcessor& p);
    ~JamWideJuceEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Phase 19-02 — FallbackListener overrides (registered with the
    // JamWideCameraDevice on the processor; the device outlives the editor).
    void onCameraFallback(jamwide::CameraFallbackCause cause) override;
    void onCameraStateChanged(jamwide::CameraState newState) override;

private:
    // HIGH-5 first-launch flow — switches on the current auth status and
    // either toggles directly or runs request-access → privacy-modal → toggle.
    // Stubbed in Task 1 (delegates straight to toggle()); Task 3 fills in
    // the NotDetermined → request → on-grant → showPrivacyOrToggle path.
    void handleCameraIdleClick();
    // Helper shared between the Authorized and NotApplicable branches.
    void showPrivacyOrToggle(jamwide::JamWideCameraDevice* cam);
    // Task 2 hook — drives previewWindow_ visibility from onCameraStateChanged.
    // Task 1 ships a stub; Task 2 fills in the Capturing→show / Idle→hide path.
    void drivePreviewWindowVisibility(jamwide::CameraState newState);
    void timerCallback() override;

    // Phase 22-02 — toggle the in-main-view VideoGridBand visibility. Wired
    // by ConnectionBar's onGridToggleClicked AND by the D-05 auto-open
    // latch in timerCallback. DISP-04 hard requirement — this method's
    // body MUST NOT touch NJClient (audio session continues across toggle).
    void toggleGridBand(bool visible);
    void drainEvents();
    void pollStatus();
    void showServerBrowser();
    void showLicenseDialog();
    void applyScale(float factor);

    void refreshChannelStrips();
    void handleServerSelected(const juce::String& address);
    void handleServerDoubleClicked(const juce::String& address);
    void handleLicenseResponse(bool accepted);
    void toggleChatSidebar();
    void toggleSessionInfoStrip();
    int getCurrentSyncState() const;

    JamWideJuceProcessor& processorRef;
    JamWideLookAndFeel lookAndFeel;

    ConnectionBar connectionBar;
    BeatBar beatBar;
    SessionInfoStrip sessionInfoStrip;
    ChannelStripArea channelStripArea;
    ChatPanel chatPanel;

    ServerBrowserOverlay serverBrowser;
    LicenseDialog licenseDialog;
    VideoPrivacyDialog videoPrivacyDialog;

    // Phase 19-02 — native-camera UI surface. The popout window is constructed
    // in the editor constructor and torn down by the editor destructor; the
    // privacy dialog is lazily constructed on first need (~24 bytes idle).
    std::unique_ptr<jamwide::CameraPreviewWindow> previewWindow_;
    std::unique_ptr<jamwide::NativeCameraPrivacyDialog> privacyDialog_;

    // Phase 22-02 — in-main-view native video grid band. Inserted between
    // sessionInfoStrip and channelStripArea by resized() when gridBandVisible_.
    // Constructed with Mode::MainBand (codex M7) — the editor explicitly opts
    // in to the main-band mode at construction; Plan 22-03 will construct
    // an inner VideoGridBand with Mode::DetachedBand inside its
    // DetachedGridWindow.
    std::unique_ptr<jamwide::VideoGridBand> gridBand_;
    bool                                    gridBandVisible_ = false;
    int                                     gridBandHeight_  = 280;

    // D-05 auto-open latch — atomic so the once-fire test reads cleanly even
    // if a parallel pollStatus path ever touches it. Per-session sticky:
    // once fired, the band's open is sticky for the editor lifetime; further
    // peer first-frames do not re-open the band even if the user explicitly
    // closed it.
    std::atomic<bool>                       gridAutoOpenLatchFired_{false};

    // Phase 19-03 — cause-aware fallback dialog (D-13..D-16). Owns the
    // suppress-after-first-show state. onCameraFallback delegates here.
    jamwide::CameraStatusDialog cameraStatusDialog_;

    // Phase 19-03 — VDO.Ninja coexistence toast (D-27). Once-per-editor-
    // lifetime guard so we never spam the user. Atomic for paranoid safety
    // (the lambda capturing `this` runs on the message thread, but atomic
    // exchange is the canonical idiom for "fire-at-most-once" flags).
    std::atomic<bool> coexistenceToastShown_{false};

    // Custom arrow button — TextButton truncates to "..." at 16px width
    struct ChatToggleButton : public juce::Component
    {
        bool pointsRight = true;
        bool hovering = false;
        std::function<void()> onClick;

        ChatToggleButton() { setRepaintsOnMouseActivity(true); }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();

            // Background with subtle hover brightening
            auto bgColour = juce::Colour(JamWideLookAndFeel::kBgElevated);
            if (hovering)
                bgColour = bgColour.brighter(0.15f);
            g.setColour(bgColour);
            g.fillRoundedRectangle(bounds, 2.0f);

            // Arrow triangle
            auto b = bounds.reduced(4.0f, 7.0f);
            auto arrowColour = juce::Colour(JamWideLookAndFeel::kTextSecondary);
            if (hovering)
                arrowColour = arrowColour.brighter(0.4f);
            g.setColour(arrowColour);

            juce::Path arrow;
            if (pointsRight)
                arrow.addTriangle(b.getX(), b.getY(),
                                  b.getRight(), b.getCentreY(),
                                  b.getX(), b.getBottom());
            else
                arrow.addTriangle(b.getRight(), b.getY(),
                                  b.getX(), b.getCentreY(),
                                  b.getRight(), b.getBottom());
            g.fillPath(arrow);
        }

        void mouseEnter(const juce::MouseEvent&) override { hovering = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override  { hovering = false; repaint(); }
        void mouseDown(const juce::MouseEvent&) override   { if (onClick) onClick(); }
    } chatToggleButton;

    bool chatSidebarVisible = true;
    bool infoStripVisible = true;
    int prevPollStatus_ = -1;  // REVIEW FIX: member, not static

    // 1280 accommodates the connection bar after the Phase-22 GridButton
    // (60 px) joined the right cluster between Camera and DBG. The Phase-19
    // budget at 1200 left ~85 px slack for the Camera button (130 px), but
    // adding Grid pushed the leftmost right-cluster item under the status
    // label area (overlap of ~77 px at width=1200). Pitfall 7 documents the
    // fallback as a kBaseWidth bump rather than a silent layout collapse.
    // History: 1030 (pre-Phase-19) → 1200 (post-Camera button, post-VDO.Ninja
    // removal 2026-05) → 1280 (post-GridButton, this plan).
    static constexpr int kBaseWidth = 1280;
    static constexpr int kBaseHeight = 700;
    static constexpr int kConnectionBarHeight = 44;
    static constexpr int kBeatBarHeight = 22;
    static constexpr int kChatPanelWidth = 260;
    static constexpr int kChatToggleWidth = 16;
    static constexpr int kChatToggleHeight = 28;
    static constexpr int kSessionInfoStripHeight = 20;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceEditor)
};
