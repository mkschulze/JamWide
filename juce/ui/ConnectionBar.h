#pragma once
#include <JuceHeader.h>
#include <functional>

class JamWideJuceProcessor;

class ConnectionBar : public juce::Component
{
public:
    explicit ConnectionBar(JamWideJuceProcessor& processor);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    void updateStatus(int njcStatus, float bpm, int bpi, int beat, int numUsers);
    void setServerAddress(const juce::String& addr);

    juce::String getServerAddress() const;
    juce::String getUsername() const;
    juce::String getPassword() const;
    void setFitHighlight(bool overflow);

    // Callbacks for editor
    std::function<void()> onBrowseClicked;
    std::function<void()> onConnectClicked;
    std::function<void(float)> onScaleChanged;  // D-23
    std::function<void()> onFitClicked;
    std::function<void(int)> onRouteModeChanged;  // 0=manual, 1=by-channel, 2=by-user

    void setRoutingModeHighlight(int mode);  // Updates Route button text color

private:
    void handleConnectClick();
    void handleCodecChange();
    void updateConnectedState(bool connected, bool connecting);

    JamWideJuceProcessor& processorRef;

    juce::TextEditor serverField;
    juce::TextEditor usernameField;
    juce::TextButton passwordToggle;
    juce::TextEditor passwordField;
    bool passwordVisible = false;

    juce::TextButton connectButton;
    juce::TextButton browseButton;

    juce::Label statusLabel;
    juce::Label bpmBpiLabel;
    juce::Label userCountLabel;

    juce::ComboBox codecSelector;
    juce::TextButton fitButton;
    juce::TextButton routeButton;

    int currentStatus = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConnectionBar)
};
