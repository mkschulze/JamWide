#include "SessionInfoStrip.h"
#include "JamWideLookAndFeel.h"

SessionInfoStrip::SessionInfoStrip()
{
    setOpaque(true);
}

void SessionInfoStrip::update(int intervalCount, unsigned int elapsedMs,
                               int currentBeat, int totalBeats,
                               int syncState, bool isStandalone,
                               int userCount, int maxUsers)
{
    intervalCount_ = intervalCount;
    elapsedMs_ = elapsedMs;
    currentBeat_ = currentBeat;
    totalBeats_ = totalBeats;
    syncState_ = syncState;
    isStandalone_ = isStandalone;
    userCount_ = userCount;
    maxUsers_ = maxUsers;
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

    // Users section — renders "N/M" when the server's max slot count is known
    // (populated from the public server list cache by the editor), "N"
    // otherwise. maxUsers_ == 0 means "unknown" — the NINJAM protocol itself
    // doesn't expose max-slots, we only know it when the user has browsed the
    // public list and the connected server appears there.
    area.removeFromLeft(16);  // gap
    g.setFont(labelFont);
    g.setColour(labelCol);
    g.drawText("Users: ", area.removeFromLeft(35), juce::Justification::centredRight, false);
    g.setFont(valueFont);
    g.setColour(valueCol);
    juce::String usersStr;
    if (!connected)
        usersStr = "--";
    else if (maxUsers_ > 0)
        usersStr = juce::String(userCount_) + "/" + juce::String(maxUsers_);
    else
        usersStr = juce::String(userCount_);
    // 50px fits "NN/NN" comfortably at the 11pt value font.
    g.drawText(usersStr, area.removeFromLeft(50), juce::Justification::centredLeft, false);

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
