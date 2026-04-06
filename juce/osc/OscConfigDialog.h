#pragma once
#include <JuceHeader.h>

class OscServer;

class OscConfigDialog : public juce::Component
{
public:
    explicit OscConfigDialog(OscServer& server);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void applyConfig();
    void updateErrorDisplay();

    OscServer& oscServer;

    juce::ToggleButton enableToggle;
    juce::Label receivePortLabel;
    juce::TextEditor receivePortEditor;
    juce::Label sendIpLabel;
    juce::TextEditor sendIpEditor;
    juce::Label sendPortLabel;
    juce::TextEditor sendPortEditor;
    juce::Label feedbackInfoLabel;
    juce::Label errorLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscConfigDialog)
};
