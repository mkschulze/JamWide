#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>
#include "osc/OscStatusDot.h"
#include "midi/MidiStatusDot.h"

class JamWideJuceProcessor;

class ConnectionBar : public juce::Component
{
public:
    explicit ConnectionBar(JamWideJuceProcessor& processor);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    void updateStatus(int njcStatus, int numUsers);
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
    std::function<void()> onVideoClicked;
    std::function<void()> onDebugSnapshotClicked;

    void setVideoActive(bool active);

    void setRoutingModeHighlight(int mode);  // Updates Route button text color
    void updateSyncState(int state);

private:
    void handleConnectClick();
    void handleCodecChange();
    void handleSyncClick();
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

    std::unique_ptr<juce::Drawable> logoDrawable;

    juce::ComboBox codecSelector;
    juce::TextButton fitButton;
    juce::TextButton routeButton;
    juce::TextButton syncButton;
    std::unique_ptr<juce::BubbleMessageComponent> syncMismatchBubble;
    int syncState_ = 0;  // 0=IDLE, 1=WAITING, 2=ACTIVE (mirrors processor syncState_ atomic)

    int currentStatus = -1;

    // Video button (D-01: in ConnectionBar, D-02: toggle with color states)
    juce::TextButton videoButton;

    // Debug snapshot button — writes current /rcmstats data + extra context
    // to a timestamped log file under userApplicationDataDirectory()/JamWide/Logs/.
    // Less intrusive than the chat /rcmstats command; useful for in-session
    // bug capture without spamming the chat panel. Wired by JamWideJuceEditor
    // via onDebugSnapshotClicked.
    juce::TextButton debugButton;

    // OSC status dot (between Sync button and right-aligned controls)
    std::unique_ptr<OscStatusDot> oscStatusDot;

    // MIDI status dot (next to OSC status dot)
    std::unique_ptr<MidiStatusDot> midiStatusDot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConnectionBar)
};
