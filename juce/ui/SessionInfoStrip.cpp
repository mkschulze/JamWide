#include "SessionInfoStrip.h"
#include "JamWideLookAndFeel.h"

SessionInfoStrip::SessionInfoStrip()
{
    setOpaque(true);
}

void SessionInfoStrip::update(int intervalCount, unsigned int elapsedMs,
                               int currentBeat, int totalBeats,
                               int syncState, bool isStandalone,
                               int userCount)
{
    intervalCount_ = intervalCount;
    elapsedMs_ = elapsedMs;
    currentBeat_ = currentBeat;
    totalBeats_ = totalBeats;
    syncState_ = syncState;
    isStandalone_ = isStandalone;
    userCount_ = userCount;
    repaint();
}

void SessionInfoStrip::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kVuBackground));

    auto area = getLocalBounds().reduced(8, 0);  // 8px horizontal padding
    const auto labelCol = juce::Colour(JamWideLookAndFeel::kTextSecondary);
    const auto valueCol = juce::Colour(JamWideLookAndFeel::kTextPrimary);
    const auto labelFont = juce::FontOptions(9.0f);
    const auto valueFont = juce::FontOptions(11.0f);

    bool connected = (intervalCount_ > 0 || elapsedMs_ > 0 || totalBeats_ > 0);

    // Intervals section
    g.setFont(labelFont);
    g.setColour(labelCol);
    g.drawText("Intervals: ", area.removeFromLeft(55), juce::Justification::centredRight, false);
    g.setFont(valueFont);
    g.setColour(valueCol);
    g.drawText(connected ? juce::String(intervalCount_) : "--",
               area.removeFromLeft(35), juce::Justification::centredLeft, false);

    area.removeFromLeft(16);  // gap

    // Elapsed section
    g.setFont(labelFont);
    g.setColour(labelCol);
    g.drawText("Elapsed: ", area.removeFromLeft(45), juce::Justification::centredRight, false);
    g.setFont(valueFont);
    g.setColour(valueCol);
    if (connected)
    {
        int totalSec = static_cast<int>(elapsedMs_ / 1000);
        int min = totalSec / 60;
        int sec = totalSec % 60;
        g.drawText(juce::String::formatted("%d:%02d", min, sec),
                   area.removeFromLeft(35), juce::Justification::centredLeft, false);
    }
    else
    {
        g.drawText("--", area.removeFromLeft(35), juce::Justification::centredLeft, false);
    }

    area.removeFromLeft(16);  // gap

    // Beat section
    g.setFont(labelFont);
    g.setColour(labelCol);
    g.drawText("Beat: ", area.removeFromLeft(30), juce::Justification::centredRight, false);
    g.setFont(valueFont);
    g.setColour(valueCol);
    if (connected && totalBeats_ > 0)
        g.drawText(juce::String(currentBeat_ + 1) + "/" + juce::String(totalBeats_),
                   area.removeFromLeft(40), juce::Justification::centredLeft, false);
    else
        g.drawText("--/--", area.removeFromLeft(40), juce::Justification::centredLeft, false);

    // Users section
    area.removeFromLeft(16);  // gap
    g.setFont(labelFont);
    g.setColour(labelCol);
    g.drawText("Users: ", area.removeFromLeft(35), juce::Justification::centredRight, false);
    g.setFont(valueFont);
    g.setColour(valueCol);
    g.drawText(connected ? juce::String(userCount_) : "--",
               area.removeFromLeft(25), juce::Justification::centredLeft, false);

    // Sync section (hidden in standalone per D-07)
    if (!isStandalone_)
    {
        area.removeFromLeft(16);  // gap
        g.setFont(labelFont);
        g.setColour(labelCol);
        g.drawText("Sync: ", area.removeFromLeft(30), juce::Justification::centredRight, false);
        g.setFont(valueFont);

        juce::String syncText;
        juce::Colour syncCol;
        if (syncState_ == 0)      { syncText = "IDLE";    syncCol = juce::Colour(JamWideLookAndFeel::kTextSecondary); }
        else if (syncState_ == 1) { syncText = "WAITING"; syncCol = juce::Colour(JamWideLookAndFeel::kAccentWarning); }
        else                      { syncText = "ACTIVE";  syncCol = juce::Colour(JamWideLookAndFeel::kAccentConnect); }

        g.setColour(syncCol);
        g.drawText(syncText, area.removeFromLeft(55), juce::Justification::centredLeft, false);
    }

    // Bottom border
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}
