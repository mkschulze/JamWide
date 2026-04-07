#pragma once
#include <JuceHeader.h>
#include "osc/OscAddressMap.h"
#include <array>
#include <atomic>

class JamWideJuceProcessor;

class OscServer : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>,
                  private juce::Timer
{
public:
    explicit OscServer(JamWideJuceProcessor& proc);
    ~OscServer() override;

    // OSCReceiver::Listener<RealtimeCallback> -- called on juce_osc network thread
    void oscMessageReceived(const juce::OSCMessage& msg) override;
    void oscBundleReceived(const juce::OSCBundle& bundle) override;

    // Lifecycle
    bool start(int receivePort, const juce::String& sendIP, int sendPort);
    void stop();
    bool isEnabled() const { return enabled.load(std::memory_order_relaxed); }
    bool hasError() const { return receiverError.load(std::memory_order_relaxed); }
    juce::String getErrorMessage() const;

    // Config queries (for persistence in Plan 02)
    int getReceivePort() const { return currentReceivePort; }
    juce::String getSendIP() const { return currentSendIP; }
    int getSendPort() const { return currentSendPort; }

    // Processor access (for Plan 02 config persistence)
    JamWideJuceProcessor& getProcessor() { return processor; }

private:
    // Timer -- fires on message thread every 100ms (per D-12, D-20)
    void timerCallback() override;

    // Message thread dispatch (per D-19)
    void handleOscOnMessageThread(const juce::String& address, float value);

    // Send helpers (per D-13: bundle mode)
    void sendDirtyApvtsParams(juce::OSCBundle& bundle, bool& hasContent);
    void sendDirtyNonApvtsParams(juce::OSCBundle& bundle, bool& hasContent);
    void sendDirtyTelemetry(juce::OSCBundle& bundle, bool& hasContent);
    void sendVuMeters(juce::OSCBundle& bundle, bool& hasContent);

    // Phase 10: Remote user send methods (D-01 through D-08, D-17)
    void sendDirtyRemoteUsers(juce::OSCBundle& bundle, bool& hasContent);
    void sendRemoteVuMeters(juce::OSCBundle& bundle, bool& hasContent);
    void sendRemoteRoster(juce::OSCBundle& bundle, bool& hasContent);

    // Phase 10: Remote user receive dispatch (D-18)
    void handleRemoteUserOsc(const juce::String& address, float value);

    // Phase 13: Video OSC dispatch
    void handleVideoOsc(const juce::String& address, float value);
    void sendVideoState(juce::OSCBundle& bundle, bool& hasContent);

    // Phase 10: String-argument OSC handler (D-09, D-10)
    void handleOscStringOnMessageThread(const juce::String& address, const juce::String& value);

    // Get current value for a controllable parameter (for dirty comparison)
    float getCurrentValue(int index);

    JamWideJuceProcessor& processor;
    OscAddressMap addressMap;
    juce::OSCReceiver receiver;
    juce::OSCSender sender;

    // Dirty tracking (per D-12)
    static constexpr int kMaxParams = 64;  // generous upper bound
    std::array<float, kMaxParams> lastSentValues{};
    std::array<bool, kMaxParams> oscSourced{};  // echo suppression (per D-14)

    // Telemetry last-sent cache (per D-15)
    float lastSentBpm = -1.0f;
    int lastSentBpi = -1;
    int lastSentBeat = -1;
    int lastSentStatus = -999;
    int lastSentUsers = -1;
    float lastSentSampleRate = -1.0f;
    juce::String lastSentCodec;

    // ── Phase 10: Remote user dirty tracking ──
    static constexpr int kMaxRemoteSlots = 16;   // per D-05
    static constexpr int kMaxSubChannels = 8;    // practical upper bound per user

    // Group bus last-sent state per remote user
    struct RemoteUserLastSent {
        float volume = -999.0f;
        float pan = -999.0f;
        float mute = -999.0f;
        float solo = -999.0f;  // derived: 1.0 if ALL sub-channels soloed, 0.0 otherwise
    };
    std::array<RemoteUserLastSent, kMaxRemoteSlots> lastSentRemoteUsers{};

    // Sub-channel last-sent state
    struct RemoteChannelLastSent {
        float volume = -999.0f;
        float pan = -999.0f;
        float mute = -999.0f;
        float solo = -999.0f;
    };
    std::array<std::array<RemoteChannelLastSent, kMaxSubChannels>, kMaxRemoteSlots> lastSentRemoteChannels{};

    // Per-slot echo suppression (cleared on roster change to prevent inherited state)
    struct RemoteUserOscSourced {
        bool volume = false;
        bool pan = false;
        bool mute = false;
    };
    std::array<RemoteUserOscSourced, kMaxRemoteSlots> remoteOscSourced{};

    struct RemoteChannelOscSourced {
        bool volume = false;
        bool pan = false;
        bool mute = false;
        bool solo = false;
    };
    std::array<std::array<RemoteChannelOscSourced, kMaxSubChannels>, kMaxRemoteSlots> remoteChOscSourced{};

    // Roster change detection (per D-07 -- do NOT broadcast every tick)
    int lastSentRosterCount = -1;
    int lastSentRosterHash = -1;

    // Phase 13: Video state dirty tracking
    float lastSentVideoActive = -1.0f;

    // Connection state
    std::atomic<bool> enabled{false};
    std::atomic<bool> receiverError{false};
    bool senderConnected = false;
    bool receiverConnected = false;
    juce::String errorMsg;

    // Current config
    int currentReceivePort = 9000;
    juce::String currentSendIP{"127.0.0.1"};
    int currentSendPort = 9001;

    // Shared flag for callAsync UAF safety — set false in destructor
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscServer)
};
