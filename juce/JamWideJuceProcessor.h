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
    float scaleFactor{1.0f};

    // Chat sidebar visibility (persisted via ValueTree, NOT APVTS param per review)
    bool chatSidebarVisible{true};

    // Local channel transmit state (persisted via ValueTree, per D-21 and D-15)
    std::array<bool, 4> localTransmit{true, false, false, false};

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
    double storedSampleRate = 48000.0;

    std::unique_ptr<NinjamRunThread> runThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceProcessor)
};
