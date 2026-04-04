#pragma once
#include <JuceHeader.h>

class VuMeter : public juce::Component
{
public:
    VuMeter();

    // Called by external code to set target levels
    void setLevels(float left, float right);

    // Called by centralized timer to apply ballistics and repaint
    // REVIEW FIX #7: No internal timer -- parent drives updates
    void tick();

    void paint(juce::Graphics& g) override;

private:
    void paintBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level);

    float targetLeft = 0.0f, targetRight = 0.0f;
    float displayLeft = 0.0f, displayRight = 0.0f;

    static constexpr float kAttack = 0.8f;
    static constexpr float kRelease = 0.92f;
    static constexpr int kSegmentHeight = 7;
    static constexpr int kSegmentGap = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VuMeter)
};
