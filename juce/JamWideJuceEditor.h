#pragma once
#include <JuceHeader.h>
#include "JamWideJuceProcessor.h"

class JamWideJuceEditor : public juce::AudioProcessorEditor
{
public:
    explicit JamWideJuceEditor(JamWideJuceProcessor& p);
    ~JamWideJuceEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    JamWideJuceProcessor& processor;
    juce::Label placeholder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceEditor)
};
