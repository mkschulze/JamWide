#include "JamWideJuceEditor.h"

JamWideJuceEditor::JamWideJuceEditor(JamWideJuceProcessor& p)
    : AudioProcessorEditor(p), processor(p)
{
    setSize(800, 600);

    // Phase 2: placeholder label. Real UI in Phase 4.
    addAndMakeVisible(placeholder);
    placeholder.setText("JamWide JUCE - Phase 2 Scaffold", juce::dontSendNotification);
    placeholder.setJustificationType(juce::Justification::centred);
    placeholder.setFont(juce::FontOptions(24.0f));
}

void JamWideJuceEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId));
}

void JamWideJuceEditor::resized()
{
    placeholder.setBounds(getLocalBounds());
}
