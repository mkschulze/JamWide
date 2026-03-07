#pragma once
#include <JuceHeader.h>
#include "JamWideJuceProcessor.h"

class JamWideJuceEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit JamWideJuceEditor(JamWideJuceProcessor& p);
    ~JamWideJuceEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void onConnectClicked();
    bool isConnected() const;

    JamWideJuceProcessor& processor;

    juce::Label serverLabel;
    juce::TextEditor serverField;

    juce::Label usernameLabel;
    juce::TextEditor usernameField;

    juce::TextButton connectButton;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceEditor)
};
