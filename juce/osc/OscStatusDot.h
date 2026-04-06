#pragma once
#include <JuceHeader.h>

class OscServer;

class OscStatusDot : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    explicit OscStatusDot(OscServer& server);
    ~OscStatusDot() override;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void updateTooltip();

    OscServer& oscServer;
    bool mouseOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscStatusDot)
};
