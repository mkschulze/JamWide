#pragma once
#include <JuceHeader.h>

class SessionInfoStrip : public juce::Component
{
public:
    SessionInfoStrip();
    void update(int intervalCount, unsigned int elapsedMs, int currentBeat,
                int totalBeats, int syncState, bool isStandalone,
                int userCount, int maxUsers);
    void paint(juce::Graphics& g) override;

private:
    int intervalCount_ = 0;
    unsigned int elapsedMs_ = 0;
    int currentBeat_ = 0;
    int totalBeats_ = 0;
    int syncState_ = 0;
    bool isStandalone_ = false;
    int userCount_ = 0;
    int maxUsers_ = 0;  // 0 = unknown (not in server list cache) → show "N" only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionInfoStrip)
};
