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

    // 1200 accommodates the connection bar after the Phase-19 Camera button
    // (130px, expandable to "Recheck permission" text) and 2026-05 removal
    // of the legacy VDO.Ninja Video button. At 1030 the right cluster ran
    // under the left cluster (Camera/DBG/Fit drew over Connect/Browse).
    static constexpr int kBaseWidth = 1200;
    static constexpr int kBaseHeight = 700;
    static constexpr int kConnectionBarHeight = 44;
    static constexpr int kBeatBarHeight = 22;
    static constexpr int kChatPanelWidth = 260;
    static constexpr int kChatToggleWidth = 16;
    static constexpr int kChatToggleHeight = 28;
    static constexpr int kSessionInfoStripHeight = 20;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceEditor)
};
