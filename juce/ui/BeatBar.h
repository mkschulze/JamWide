#pragma once
#include <JuceHeader.h>

class BeatBar : public juce::Component
{
public:
    BeatBar();
    void update(int bpi, int currentBeat, int intervalPos, int intervalLen);
    void paint(juce::Graphics& g) override;

private:
    int bpi_ = 0;
    int currentBeat_ = 0;
    int intervalPos_ = 0;
    int intervalLen_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatBar)
};
