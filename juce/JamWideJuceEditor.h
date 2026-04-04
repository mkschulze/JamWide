#pragma once
#include <JuceHeader.h>
#include "JamWideJuceProcessor.h"
#include "ui/JamWideLookAndFeel.h"
#include "ui/ConnectionBar.h"
#include "ui/ChatPanel.h"

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
    void showServerBrowser();     // placeholder for Plan 04
    void showLicenseDialog();     // placeholder for Plan 04
    void applyScale(float factor);

    JamWideJuceProcessor& processorRef;
    JamWideLookAndFeel lookAndFeel;

    ConnectionBar connectionBar;
    ChatPanel chatPanel;

    juce::Label mixerPlaceholder;

    bool chatSidebarVisible = true;
    int prevPollStatus_ = -1;  // REVIEW FIX: member, not static

    static constexpr int kBaseWidth = 1000;
    static constexpr int kBaseHeight = 700;
    static constexpr int kConnectionBarHeight = 44;
    static constexpr int kChatPanelWidth = 260;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceEditor)
};
