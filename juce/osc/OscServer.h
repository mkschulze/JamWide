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

    // Solo state tracking (per Open Question 1 in research)
    // Atomic bitmask: bit 0 = ch0 solo, bit 1 = ch1 solo, etc.
    std::atomic<uint8_t> localSoloBitmask{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscServer)
};
