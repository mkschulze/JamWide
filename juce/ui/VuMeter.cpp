#include "VuMeter.h"
#include "JamWideLookAndFeel.h"

VuMeter::VuMeter()
{
    setOpaque(true);
}

void VuMeter::setLevels(float left, float right)
{
    targetLeft = juce::jlimit(0.0f, 1.0f, left);
    targetRight = juce::jlimit(0.0f, 1.0f, right);
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
