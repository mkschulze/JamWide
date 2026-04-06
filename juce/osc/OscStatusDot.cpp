#include "OscStatusDot.h"
#include "osc/OscServer.h"
#include "osc/OscConfigDialog.h"
#include "ui/JamWideLookAndFeel.h"

OscStatusDot::OscStatusDot(OscServer& server)
    : oscServer(server)
{
    startTimer(500);  // Poll at 500ms for status changes (per OSC-09)
    updateTooltip();
}

OscStatusDot::~OscStatusDot()
{
    stopTimer();
}

void OscStatusDot::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // "OSC" label text: 9px regular, kTextSecondary, left-aligned
    g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
    g.setFont(juce::FontOptions(9.0f));
    const float labelWidth = g.getCurrentFont().getStringWidthFloat("OSC");
    const float labelX = (bounds.getWidth() - labelWidth - 4.0f - 10.0f) * 0.5f;
    const float labelY = (bounds.getHeight() - 9.0f) * 0.5f;
    g.drawText("OSC", juce::Rectangle<float>(labelX, labelY, labelWidth, 9.0f),
               juce::Justification::centredLeft, false);

    // Dot: 10px diameter, 4px gap after "OSC" text, centered vertically
    const float dotX = labelX + labelWidth + 4.0f;
    const float dotY = (bounds.getHeight() - 10.0f) * 0.5f;

    // Color based on OscServer state (per D-09: 3-state indicator)
    juce::Colour dotColour;
    if (!oscServer.isEnabled())
        dotColour = juce::Colour(JamWideLookAndFeel::kTextSecondary);    // Grey: disabled
    else if (oscServer.hasError())
        dotColour = juce::Colour(JamWideLookAndFeel::kAccentDestructive); // Red: error
    else
        dotColour = juce::Colour(JamWideLookAndFeel::kAccentConnect);     // Green: active

    g.setColour(dotColour);
    g.fillEllipse(dotX, dotY, 10.0f, 10.0f);
}

void OscStatusDot::mouseUp(const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && getBounds().contains(e.getPosition() + getPosition()))
    {
        auto dialog = std::make_unique<OscConfigDialog>(oscServer);
        dialog->setSize(200, 300);  // per D-10
        juce::CallOutBox::launchAsynchronously(
            std::move(dialog),
            getScreenBounds(),
            nullptr);  // nullptr parent per IEM pattern / Pitfall 4
    }
}

void OscStatusDot::mouseEnter(const juce::MouseEvent&)
{
    mouseOver = true;
    repaint();
}

void OscStatusDot::mouseExit(const juce::MouseEvent&)
{
    mouseOver = false;
    repaint();
}

void OscStatusDot::timerCallback()
{
    updateTooltip();
    repaint();
}

void OscStatusDot::updateTooltip()
{
    // Tooltips per copywriting contract
    if (!oscServer.isEnabled())
        setTooltip("OSC: Off");
    else if (oscServer.hasError())
        setTooltip("OSC: Error");
    else
        setTooltip("OSC: Active (port " + juce::String(oscServer.getReceivePort()) + ")");
}
