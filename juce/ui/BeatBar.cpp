#include "BeatBar.h"
#include "JamWideLookAndFeel.h"

BeatBar::BeatBar()
{
    setOpaque(true);
}

void BeatBar::update(int bpi, int currentBeat, int intervalPos, int intervalLen)
{
    bpi_ = bpi;
    currentBeat_ = currentBeat;
    intervalPos_ = intervalPos;
    intervalLen_ = intervalLen;
    repaint();
}

void BeatBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kVuBackground));

    if (bpi_ <= 0)
        return;

    const float segWidth = (float)getWidth() / (float)bpi_;
    const float h = (float)getHeight();
    const auto green = juce::Colour(JamWideLookAndFeel::kAccentConnect);

    for (int i = 0; i < bpi_; ++i)
    {
        const float x = (float)i * segWidth;
        auto segBounds = juce::Rectangle<float>(x + 1.0f, 1.0f, segWidth - 2.0f, h - 2.0f);

        if (i < currentBeat_)
        {
            // Past beats: green at 35% opacity
            g.setColour(green.withAlpha(0.35f));
            g.fillRoundedRectangle(segBounds, 2.0f);
        }
        else if (i == currentBeat_)
        {
            // Current beat: full green
            g.setColour(green);
            g.fillRoundedRectangle(segBounds, 2.0f);

            // Beat number centered
            g.setColour(juce::Colour(JamWideLookAndFeel::kBgPrimary));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(juce::String(i + 1), segBounds.toNearestInt(),
                       juce::Justification::centred, false);
        }
        else
        {
            // Future beats: dark background
            g.setColour(juce::Colour(JamWideLookAndFeel::kVuUnlit));
            g.fillRoundedRectangle(segBounds, 2.0f);
        }

        // Downbeat accent (every 4 beats): 2px bar at top, green at 60%
        if (i % 4 == 0)
        {
            g.setColour(green.withAlpha(0.6f));
            g.fillRect(x + 1.0f, 0.0f, segWidth - 2.0f, 2.0f);
        }

        // BPI adaptation: numbering strategy
        // <=8: all numbered, 9-24: current + downbeats, 32+: group markers only
        if (i != currentBeat_) // current beat already has number
        {
            bool showNumber = false;
            if (bpi_ <= 8)
                showNumber = true;
            else if (bpi_ <= 24)
                showNumber = (i % 4 == 0);
            else
                showNumber = (i % 8 == 0);

            if (showNumber)
            {
                g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary).withAlpha(0.6f));
                g.setFont(juce::FontOptions(9.0f));
                g.drawText(juce::String(i + 1), segBounds.toNearestInt(),
                           juce::Justification::centred, false);
            }
        }
    }
}
