#pragma once
#include <JuceHeader.h>
#include "MidiTypes.h"
#include <unordered_map>
#include <atomic>
#include <memory>
#include <optional>
#include <array>

class JamWideJuceProcessor;

class MidiMapper : private juce::Timer,
                    public juce::MidiInputCallback
{
public:
    explicit MidiMapper(JamWideJuceProcessor& proc);
    ~MidiMapper() override;

    // Must be called from prepareToPlay so the standalone MIDI collector knows the sample rate
    void setSampleRate(double sampleRate, int samplesPerBlock);

    // Audio thread: parse incoming CC from processBlock MidiBuffer (per D-04)
    // numSamples is needed to drain standalone MIDI collector with correct block size.
    void processIncomingMidi(const juce::MidiBuffer& buffer, int numSamples);

    // Audio thread: append CC feedback to processBlock MidiBuffer (per D-11)
    void appendFeedbackMidi(juce::MidiBuffer& buffer);

    // Message thread: mapping management
    bool addMapping(const juce::String& paramId, int ccNumber, int midiChannel,
                    MidiMsgType type = MidiMsgType::CC);
    void removeMapping(const juce::String& paramId);
    void removeMappingByCc(int ccNumber, int midiChannel, MidiMsgType type = MidiMsgType::CC);
    void clearAllMappings();
    int getMappingCount() const;

    // Query
    bool hasMapping(const juce::String& paramId) const;
    bool hasMappingForCc(int ccNumber, int midiChannel, MidiMsgType type = MidiMsgType::CC) const;
    struct MappingInfo { juce::String paramId; int number; int midiChannel; MidiMsgType type; };
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

    // Cap: 16 channels x 128 numbers x 2 types = 4096
    static constexpr int kMaxMappings = 4096;

private:
    void timerCallback() override; // Centralized APVTS-to-NJClient bridge + standalone feedback

    // Composite key: (int(type) << 11) | ((channel-1) << 7) | number
    // 12-bit key distinguishes CC 60 from Note 60 on the same channel.
    static int makeKey(int number, int midiChannel, MidiMsgType type = MidiMsgType::CC)
    {
        return (static_cast<int>(type) << 11) | ((midiChannel - 1) << 7) | number;
    }
    static MidiMsgType keyType(int key)    { return static_cast<MidiMsgType>((key >> 11) & 1); }
    static int         keyChannel(int key) { return ((key >> 7) & 0xF) + 1; }
    static int         keyNumber(int key)  { return key & 0x7F; }

    struct Mapping {
        juce::String paramId;
        int number;
        int midiChannel;
        MidiMsgType type = MidiMsgType::CC;
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
    std::array<bool, 16>  lastSyncedRemoteSolo_{};

    // Local solo and metro pan APVTS-to-NJClient sync
    std::array<bool, 4> lastSyncedLocalSolo_{};
    float lastSyncedMetroPan_ = 0.0f;

    // Activity tracking
    std::atomic<bool> receivingMidi_{false};
    std::atomic<int64_t> lastMidiReceivedTime_{0};

    // MidiInputCallback override: receives messages from standalone MIDI device thread
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    // Standalone device state (per D-05)
    std::unique_ptr<juce::MidiInput> midiInput_;
    std::unique_ptr<juce::MidiOutput> midiOutput_;
    juce::String currentInputDeviceId_;   // stored on successful open, cleared on close
    juce::String currentOutputDeviceId_;  // stored on successful open, cleared on close
    std::atomic<bool> deviceError_{false};

    // Thread-safe collector: standalone MIDI device thread → audio thread
    juce::MidiMessageCollector standaloneMidiCollector_;

    JamWideJuceProcessor& processor;

    // Validation helpers
    static bool isValidCc(int cc) { return cc >= 0 && cc <= 127; }
    static bool isValidChannel(int ch) { return ch >= 1 && ch <= 16; }
};
