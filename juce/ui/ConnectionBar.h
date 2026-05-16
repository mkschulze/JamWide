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
    // Out-of-line destructor — unique_ptr<CameraButton> needs the complete
    // CameraButton type (defined in ConnectionBar.cpp) to instantiate its
    // deleter. Declared here so the compiler synthesises the dtor body in
    // the .cpp where the full type is visible.
    ~ConnectionBar() override;

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
    std::function<void()> onDebugSnapshotClicked;

    // Phase 19-02 — native camera UI callbacks (MEDIUM-1 decision tree wiring).
    // Left-click → onCameraClicked. Right-click → quality submenu + Stop Camera
    // item dispatching to onCameraQualitySelected / onCameraStopRequested.
    std::function<void()> onCameraClicked;
    std::function<void(int)> onCameraQualitySelected;
    std::function<void()> onCameraStopRequested;

    // Phase 20-03 — Broadcast toggle. Lives as a secondary state on the Camera
    // button's right-click popup menu so the right-cluster width stays within
    // the 1200 px kBaseWidth budget set by commit 5250ff1.
    std::function<void()> onBroadcastToggleRequested;

    // Phase 19-02 — camera button state (capturing → green; setCameraLabel swaps
    // the text on Unavailable to "Recheck permission"). setCameraQualityPreset
    // updates the right-click menu's checkmark state.
    void setCameraActive(bool active);
    void setCameraLabel(const juce::String& label);
    void setCameraQualityPreset(int preset);

    // Phase 20-03 — Broadcast UI mirror. The editor calls this from its
    // BroadcastToggle callback so the popup menu's label flips between
    // "Start Broadcast" / "Stop Broadcast" coherently.
    void setCameraIsBroadcasting(bool broadcasting) noexcept {
        cameraIsBroadcasting_ = broadcasting;
    }
    bool getCameraIsBroadcasting() const noexcept {
        return cameraIsBroadcasting_;
    }

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

    // Phase 19-02 — native Camera button (D-11: NEVER disabled by Connect state;
    // independent of NJClient connection). The legacy VDO.Ninja Video button
    // was removed in 2026-05; Camera is now the sole video entry point.
    // Backed by a file-local subclass (defined in .cpp) so we can intercept
    // right-click for the quality + Stop Camera popup menu.
    class CameraButton;
    std::unique_ptr<CameraButton> cameraButton;

    // Right-click menu's checkmark state mirrors processor-stored preset.
    // Updated via setCameraQualityPreset(int) from the editor.
    int currentCameraQualityPreset_{1};
    // Tracks whether the right-click "Stop Camera" item should be enabled
    // (only true while state==Capturing per MEDIUM-1 decision tree).
    bool cameraIsActive_{false};

    // Phase 20-03 — current Broadcast state mirror. Drives the menu label flip
    // ("Start Broadcast" vs "Stop Broadcast"). Written by the editor's
    // BroadcastToggle callback via setCameraIsBroadcasting; read in CameraButton::mouseDown.
    bool cameraIsBroadcasting_{false};

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
