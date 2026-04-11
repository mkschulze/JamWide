#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <optional>
#include <array>

class JamWideJuceProcessor;

class MidiMapper : private juce::Timer
{
public:
    explicit MidiMapper(JamWideJuceProcessor& proc);
    ~MidiMapper() override;

    // Audio thread: parse incoming CC from processBlock MidiBuffer (per D-04)
    void processIncomingMidi(const juce::MidiBuffer& buffer);

    // Audio thread: append CC feedback to processBlock MidiBuffer (per D-11)
    void appendFeedbackMidi(juce::MidiBuffer& buffer);

    // Message thread: mapping management
    bool addMapping(const juce::String& paramId, int ccNumber, int midiChannel);
    void removeMapping(const juce::String& paramId);
    void removeMappingByCc(int ccNumber, int midiChannel);
    void clearAllMappings();
    int getMappingCount() const;

    // Query
    bool hasMapping(const juce::String& paramId) const;
    bool hasMappingForCc(int ccNumber, int midiChannel) const;
    struct MappingInfo { juce::String paramId; int ccNumber; int midiChannel; };
    std::vector<MappingInfo> getAllMappings() const;
    std::optional<MappingInfo> getMappingForParam(const juce::String& paramId) const;

    // State persistence (per D-20)
    void saveToState(juce::ValueTree& state) const;
    void loadFromState(const juce::ValueTree& state);

    // Standalone device management (per D-05)
    void openMidiInput(const juce::String& deviceId);
    void openMidiOutput(const juce::String& deviceId);
    void closeMidiInput();
    void closeMidiOutput();

    // Currently-open device identifiers (for persistence and UI selection)
    juce::String getInputDeviceId() const { return currentInputDeviceId_; }
    juce::String getOutputDeviceId() const { return currentOutputDeviceId_; }

    // Cross-system echo suppression API (for OscServer/ChannelStripArea callers)
    // Suppresses MIDI feedback for the given paramId for one timer tick.
    void setEchoSuppression(const juce::String& paramId);

    // Remote slot reset (per D-17)
    void resetRemoteSlotDefaults(int slotIndex);

    // Status
    bool hasActiveMappings() const;
    bool isReceiving() const;
    bool hasError() const;

    // Error state semantics (for footer status dot)
    // disabled = no mappings (grey dot)
    // healthy = mappings exist AND receiving CC within last 2s (green dot)
    // degraded = mappings exist but no recent CC (green dot, steady)
    // failed = standalone device open failure (red dot)
    enum class Status { Disabled, Healthy, Degraded, Failed };
    Status getStatus() const;

    // Cap per D-09: 16 channels x 128 CCs = 2048
    static constexpr int kMaxMappings = 2048;

private:
    void timerCallback() override; // Centralized APVTS-to-NJClient bridge + standalone feedback

    // Composite key: ((channel-1) << 7) | ccNumber, where channel is 1-based JUCE convention
    static int makeKey(int ccNumber, int midiChannel) { return ((midiChannel - 1) << 7) | ccNumber; }

    struct Mapping {
        juce::String paramId;
        int ccNumber;
        int midiChannel;
    };

    // Thread-safe map access: audio thread reads published_, message thread writes staging_
    // After modification on message thread, atomically swap pointer
    struct MappingTable {
        std::unordered_map<int, Mapping> ccToParam;      // key -> mapping
        std::unordered_map<juce::String, int> paramToCc;  // paramId -> key
    };

    std::shared_ptr<const MappingTable> published_;  // read by audio thread via atomic load
    MappingTable staging_;  // written by message thread only

    void publishMappings();  // copy staging_ to published_ (atomic swap)

    // Per-mapping echo suppression
    // Key: composite MIDI key. Value: suppression counter.
    // When CC arrives from MIDI input: set to 2 (suppress current + next appendFeedbackMidi call)
    // When external caller (OSC/UI) sets suppression: set to 2
    // Decremented each appendFeedbackMidi call; feedback suppressed while > 0
    std::unordered_map<int, int> echoSuppression_;

    // Dirty tracking for feedback
    std::unordered_map<int, int> lastSentCcValues_;  // key -> last CC value sent

    // APVTS-to-NJClient sync tracking for remote params
    // THIS IS THE SOLE PATH from APVTS remote params to NJClient cmd_queue.
    // Timer runs at 20ms (not 100ms) for responsive mixer control.
    std::array<float, 16> lastSyncedRemoteVol_{};
    std::array<float, 16> lastSyncedRemotePan_{};
    std::array<bool, 16>  lastSyncedRemoteMute_{};

    // Local solo and metro pan APVTS-to-NJClient sync
    std::array<bool, 4> lastSyncedLocalSolo_{};
    float lastSyncedMetroPan_ = 0.0f;

    // Activity tracking
    std::atomic<bool> receivingMidi_{false};
    std::atomic<int64_t> lastMidiReceivedTime_{0};

    // Standalone device state (per D-05)
    std::unique_ptr<juce::MidiInput> midiInput_;
    std::unique_ptr<juce::MidiOutput> midiOutput_;
    juce::String currentInputDeviceId_;   // stored on successful open, cleared on close
    juce::String currentOutputDeviceId_;  // stored on successful open, cleared on close
    std::atomic<bool> deviceError_{false};

    JamWideJuceProcessor& processor;

    // Validation helpers
    static bool isValidCc(int cc) { return cc >= 0 && cc <= 127; }
    static bool isValidChannel(int ch) { return ch >= 1 && ch <= 16; }
};
