#include "MidiStatusDot.h"
#include "midi/MidiMapper.h"
#include "midi/MidiLearnManager.h"
#include "midi/MidiConfigDialog.h"
#include "ui/JamWideLookAndFeel.h"

MidiStatusDot::MidiStatusDot(MidiMapper& mapper, MidiLearnManager* learnMgr)
    : midiMapper(mapper), midiLearnMgr(learnMgr)
{
    startTimer(200);  // Poll at 200ms for responsive status updates
    updateTooltip();
}

MidiStatusDot::~MidiStatusDot()
{
    stopTimer();
}

void MidiStatusDot::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // "MIDI" label text: 9px regular, kTextSecondary, centered layout
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
    g.setFont(juce::FontOptions(9.0f));
    const float labelWidth = g.getCurrentFont().getStringWidthFloat("MIDI");
    const float totalContentWidth = labelWidth + 4.0f + 8.0f;  // label + gap + dot
    const float labelX = (bounds.getWidth() - totalContentWidth) * 0.5f;
    const float labelY = (bounds.getHeight() - 9.0f) * 0.5f;
    g.drawText("MIDI", juce::Rectangle<float>(labelX, labelY, labelWidth, 9.0f),
               juce::Justification::centredLeft, false);

    // Dot: 8px diameter, 4px gap after "MIDI" text, centered vertically
    const float dotX = labelX + labelWidth + 4.0f;
    const float dotY = (bounds.getHeight() - 8.0f) * 0.5f;

    // Color based on MidiMapper::getStatus() (4-state semantics per review feedback)
    juce::Colour dotColour;
    auto status = midiMapper.getStatus();
    switch (status)
    {
        case MidiMapper::Status::Disabled:
            dotColour = juce::Colour(0xff8888AA);  // Grey: no mappings
            break;
        case MidiMapper::Status::Healthy:
            dotColour = juce::Colour(0xff40E070);  // Green: active, receiving CC
            break;
        case MidiMapper::Status::Degraded:
            dotColour = juce::Colour(0xff40E070);  // Green steady: mappings exist, no recent CC
            break;
        case MidiMapper::Status::Failed:
            dotColour = juce::Colour(0xffE04040);  // Red: device error
            break;
    }

    if (mouseOver)
        dotColour = dotColour.brighter(0.15f);

    g.setColour(dotColour);
    g.fillEllipse(dotX, dotY, 8.0f, 8.0f);
}

void MidiStatusDot::mouseUp(const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && !e.mods.isPopupMenu())
    {
        auto dialog = std::make_unique<MidiConfigDialog>(midiMapper, midiLearnMgr);
        dialog->setSize(440, 440);
        juce::CallOutBox::launchAsynchronously(
            std::move(dialog), getScreenBounds(), nullptr);
    }
}

void MidiStatusDot::mouseEnter(const juce::MouseEvent&)
{
    mouseOver = true;
    repaint();
}

void MidiStatusDot::mouseExit(const juce::MouseEvent&)
{
    mouseOver = false;
    repaint();
}

void MidiStatusDot::timerCallback()
{
    updateTooltip();
    repaint();
}

void MidiStatusDot::updateTooltip()
{
    auto status = midiMapper.getStatus();
    int count = midiMapper.getMappingCount();
    switch (status)
    {
        case MidiMapper::Status::Disabled:
            setTooltip("MIDI: Off");
            break;
        case MidiMapper::Status::Healthy:
            setTooltip("MIDI: Active (" + juce::String(count) + " mappings)");
            break;
        case MidiMapper::Status::Degraded:
            setTooltip("MIDI: " + juce::String(count) + " mappings (no input)");
            break;
        case MidiMapper::Status::Failed:
            setTooltip("MIDI: Error");
            break;
    }
}
