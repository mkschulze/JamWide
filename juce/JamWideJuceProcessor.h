#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "threading/spsc_ring.h"
#include "threading/ui_command.h"
#include "threading/ui_event.h"
#include "ui/ui_state.h"
#include "ui/ChatMessageModel.h"
#include "core/njclient.h"  // For RemoteUserInfo

class JamWideJuceEditor;
class NinjamRunThread;

//==============================================================================
// THREADING CONTRACT
//
// Three threads access this object:
//
// 1. MESSAGE THREAD (JUCE UI / host main thread)
//    - Reads: evt_queue (drain), chat_queue (drain), chatHistory, cachedServerList,
//             cachedUsers, lastServerAddress, lastUsername, scaleFactor, uiSnapshot (atomics),
//             license_pending (atomic), license_text (under license_mutex)
//    - Writes: cmd_queue (try_push), lastServerAddress, lastUsername, scaleFactor,
//              license_response (atomic), license_cv (notify)
//
// 2. RUN THREAD (NinjamRunThread)
//    - Reads: cmd_queue (drain), license_response (atomic), license_cv (wait)
//    - Writes: evt_queue (try_push), chat_queue (try_push), cachedUsers (under clientLock),
//              uiSnapshot (atomics), userCount (atomic), license_pending (atomic),
//              license_text (under license_mutex)
//    - Holds clientLock during NJClient::Run() and command processing
//
// 3. AUDIO THREAD (processBlock)
//    - Reads/writes NJClient audio buffers (AudioProc called without clientLock)
//    - Does NOT touch any UI state
//
// RULES:
// - Message thread NEVER acquires clientLock (use queues and atomics only)
// - cachedUsers is written by run thread under clientLock, read by message thread
//   (safe because writes complete before UserInfoChangedEvent is pushed)
// - All SPSC queues are single-producer single-consumer by design
// - license_mutex protects license_text only; license_pending/response are atomic
//==============================================================================

class JamWideJuceProcessor : public juce::AudioProcessor
{
public:
    JamWideJuceProcessor();
    ~JamWideJuceProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int currentStateVersion = 1;
    static constexpr int kTotalOutChannels = 34;  // 17 stereo buses
    static constexpr int kNumOutputBuses = 17;
    static constexpr int kMetronomeBus = 16;      // Last bus (channels 32-33)

    NJClient* getClient() { return client.get(); }
    juce::CriticalSection& getClientLock() { return clientLock; }

    juce::AudioProcessorValueTreeState apvts;
    jamwide::SpscRing<jamwide::UiCommand, 256> cmd_queue;

    // Event queues (Run thread -> UI)
    jamwide::SpscRing<jamwide::UiEvent, 256> evt_queue;
    jamwide::SpscRing<ChatMessage, 128> chat_queue;

    // Chat history (survives editor destruction, per Pitfall 2)
    ChatMessageModel chatHistory;

    // Server list cache (survives editor destruction)
    std::vector<ServerListEntry> cachedServerList;

    // REVIEW FIX #3: cachedUsers on Processor (not late in Plan 04).
    // Written by run thread under clientLock via GetRemoteUsersSnapshot(),
    // read by message thread after UserInfoChangedEvent arrives.
    std::vector<NJClient::RemoteUserInfo> cachedUsers;

    // User count (atomic for lock-free UI read)
    std::atomic<int> userCount{0};

    // Persistent UI state (survives editor destruction)
    juce::String lastServerAddress{"ninbot.com"};
    juce::String lastUsername{"anonymous"};
    float scaleFactor{1.5f};

    // Routing mode (0=manual, 1=by-channel, 2=by-user) -- persisted per D-12
    // REVIEW FIX: std::atomic<int> to prevent data race between message thread (write)
    // and run thread (read on connect). See threading contract above.
    std::atomic<int> routingMode{0};

    // DAW Sync state (per D-02: 3-state machine IDLE/WAITING/ACTIVE)
    // Single atomic int replaces two-boolean approach to prevent race condition
    // between run thread auto-disable and audio thread WAITING->ACTIVE transition.
    // (Addresses review consensus concern #1: two-boolean sync state machine is racy)
    static constexpr int kSyncIdle = 0;
    static constexpr int kSyncWaiting = 1;
    static constexpr int kSyncActive = 2;
    std::atomic<int> syncState_{kSyncIdle};

    // Last known host BPM (for UI sync validation)
    std::atomic<float> cachedHostBpm_{0.0f};

    // Chat sidebar visibility (persisted via ValueTree, NOT APVTS param per review)
    bool chatSidebarVisible{true};

    // Session info strip visibility (persisted via ValueTree per D-21)
    bool infoStripVisible{true};

    // Local channel transmit state (persisted via ValueTree, per D-21 and D-15)
    std::array<bool, 4> localTransmit{true, true, true, true};

    // Local channel input selector (persisted via ValueTree, per D-21 and D-14)
    // Stores 0-based stereo pair index (0=Input 1-2, 1=Input 3-4, etc.)
    std::array<int, 4> localInputSelector{0, 1, 2, 3};

    // License sync primitives (mirrors CLAP plugin pattern)
    std::mutex license_mutex;
    std::condition_variable license_cv;
    std::atomic<bool> license_pending{false};
    std::atomic<int> license_response{0};
    juce::String license_text;

    // Atomic snapshot for high-frequency UI reads (beat, VU, BPM/BPI)
    UiAtomicSnapshot uiSnapshot;

private:
    std::unique_ptr<NJClient> client;
    juce::CriticalSection clientLock;
    juce::AudioBuffer<float> inputScratch;
    juce::AudioBuffer<float> outputScratch;
    double storedSampleRate = 48000.0;

    // Audio-thread-only edge detection state (no sync primitive needed -- single thread)
    // rawHostPlaying_ stores the ACTUAL host transport state before any overrides.
    // wasPlaying_ could otherwise store the overridden value (false during WAITING),
    // causing spurious edge detection after WAITING->ACTIVE transition.
    // (Addresses Claude MEDIUM review concern: wasPlaying_ set to overridden hostPlaying)
    bool wasPlaying_{false};
    bool rawHostPlaying_{false};  // Raw transport state for edge detection

    // Previous PPQ position for seek/loop detection
    double prevPpqPos_{0.0};

    std::unique_ptr<NinjamRunThread> runThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceProcessor)
};
