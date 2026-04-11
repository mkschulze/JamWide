#pragma once
#include <JuceHeader.h>

class MidiMapper;
class MidiLearnManager;

class MidiStatusDot : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::Timer
{
public:
    MidiStatusDot(MidiMapper& mapper, MidiLearnManager* learnMgr = nullptr);
    ~MidiStatusDot() override;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void updateTooltip();

    MidiMapper& midiMapper;
    MidiLearnManager* midiLearnMgr = nullptr;
    bool mouseOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiStatusDot)
};
