#pragma once
#include <JuceHeader.h>
#include "JamWideJuceProcessor.h"
#include "ui/JamWideLookAndFeel.h"
#include "ui/ConnectionBar.h"
#include "ui/ChatPanel.h"
#include "ui/BeatBar.h"
#include "ui/ChannelStripArea.h"
#include "ui/ServerBrowserOverlay.h"
#include "ui/LicenseDialog.h"

class JamWideJuceEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit JamWideJuceEditor(JamWideJuceProcessor& p);
    ~JamWideJuceEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

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

    JamWideJuceProcessor& processorRef;
    JamWideLookAndFeel lookAndFeel;

    ConnectionBar connectionBar;
    BeatBar beatBar;
    ChannelStripArea channelStripArea;
    ChatPanel chatPanel;

    ServerBrowserOverlay serverBrowser;
    LicenseDialog licenseDialog;

    // Custom arrow button — TextButton truncates to "..." at 16px width
    struct ChatToggleButton : public juce::Component
    {
        bool pointsRight = true;
        std::function<void()> onClick;

        void paint(juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced(3.0f, 6.0f);
            g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
            juce::Path arrow;
            if (pointsRight)
            {
                arrow.addTriangle(b.getX(), b.getY(),
                                  b.getRight(), b.getCentreY(),
                                  b.getX(), b.getBottom());
            }
            else
            {
                arrow.addTriangle(b.getRight(), b.getY(),
                                  b.getX(), b.getCentreY(),
                                  b.getRight(), b.getBottom());
            }
            g.fillPath(arrow);
        }

        void mouseDown(const juce::MouseEvent&) override
        {
            if (onClick) onClick();
        }
    } chatToggleButton;

    bool chatSidebarVisible = true;
    int prevPollStatus_ = -1;  // REVIEW FIX: member, not static

    static constexpr int kBaseWidth = 1000;
    static constexpr int kBaseHeight = 700;
    static constexpr int kConnectionBarHeight = 44;
    static constexpr int kBeatBarHeight = 22;
    static constexpr int kChatPanelWidth = 260;
    static constexpr int kChatToggleWidth = 16;
    static constexpr int kChatToggleHeight = 28;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceEditor)
};
