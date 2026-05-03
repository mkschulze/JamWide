#include "VuMeter.h"
#include "JamWideLookAndFeel.h"

#include <cmath>

namespace {

// 2026-05-03 VU calibration fix: the fader scale runs 0..2.0 linear (= -inf
// to +6 dB) with a pow(norm, 1/2.5) visual curve. The meter's segments must
// match the fader's tick spacing, otherwise a 0 dBFS input (peak amp = 1.0)
// draws at the top segment where +6 dB should be — observed in UAT as the
// meter clipping over +6 dB on a 0 dB SPAN-Plus reference signal. Mirror the
// VbFader::valueToY transform here so the 0 dB tick on the fader aligns with
// the 0 dB position on the meter.
float linearAmpToMeterPos(float amp)
{
    constexpr float kMaxLinear = 2.0f;  // +6 dB at top, matches VbFader::kMaxLinear
    const float norm = juce::jlimit(0.0f, 1.0f, amp / kMaxLinear);
    return std::pow(norm, 1.0f / 2.5f);
}

} // namespace

VuMeter::VuMeter()
{
    setOpaque(true);
}

void VuMeter::setLevels(float left, float right)
{
    targetLeft  = linearAmpToMeterPos(left);
    targetRight = linearAmpToMeterPos(right);
}

void VuMeter::tick()
{
    // Apply ballistics per UI-SPEC
    if (targetLeft > displayLeft)
        displayLeft = targetLeft * kAttack + displayLeft * (1.0f - kAttack);
    else
        displayLeft = displayLeft * kRelease;

    if (targetRight > displayRight)
        displayRight = targetRight * kAttack + displayRight * (1.0f - kAttack);
    else
        displayRight = displayRight * kRelease;

    repaint();
}

void VuMeter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kVuBackground));

    const int w = getWidth();
    const int barWidth = (w - 6) / 2;
    if (barWidth <= 0) return;

    // Left bar
    paintBar(g, juce::Rectangle<float>(0.0f, 0.0f, (float)barWidth, (float)getHeight()), displayLeft);

    // Right bar
    paintBar(g, juce::Rectangle<float>((float)(barWidth + 6), 0.0f, (float)barWidth, (float)getHeight()), displayRight);
}

void VuMeter::paintBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level)
{
    const int totalSegments = (int)(bounds.getHeight() / (float)(kSegmentHeight + kSegmentGap));
    if (totalSegments <= 0) return;

    for (int i = 0; i < totalSegments; ++i)
    {
        const float segmentY = bounds.getBottom() - (float)(i + 1) * (float)(kSegmentHeight + kSegmentGap);
        const float normalizedPosition = (float)(i + 1) / (float)totalSegments;
        const bool isLit = normalizedPosition <= level;

        juce::Colour segColour;
        if (isLit)
        {
            if (normalizedPosition < 0.7f)
                segColour = juce::Colour(JamWideLookAndFeel::kVuNominal);
            else if (normalizedPosition < 0.9f)
                segColour = juce::Colour(JamWideLookAndFeel::kVuWarm);
            else
                segColour = juce::Colour(JamWideLookAndFeel::kVuClip);
        }
        else
        {
            segColour = juce::Colour(JamWideLookAndFeel::kVuUnlit);
        }

        g.setColour(segColour);
        g.fillRoundedRectangle(bounds.getX(), segmentY,
                               bounds.getWidth(), (float)kSegmentHeight, 1.0f);
    }
}
