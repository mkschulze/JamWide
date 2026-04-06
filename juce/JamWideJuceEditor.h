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

class JamWideJuceEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit JamWideJuceEditor(JamWideJuceProcessor& p);
    ~JamWideJuceEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
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

    static constexpr int kBaseWidth = 1000;
    static constexpr int kBaseHeight = 700;
    static constexpr int kConnectionBarHeight = 44;
    static constexpr int kBeatBarHeight = 22;
    static constexpr int kChatPanelWidth = 260;
    static constexpr int kChatToggleWidth = 16;
    static constexpr int kChatToggleHeight = 28;
    static constexpr int kSessionInfoStripHeight = 20;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceEditor)
};
