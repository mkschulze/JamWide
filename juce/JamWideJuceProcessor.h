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
#include "osc/OscServer.h"
#include "video/VideoCompanion.h"
#include "video/native/JamWideFrameDistributor.h"
#include "video/native/JamWideCameraDevice.h"
#include "video/distributor/JamWideRemoteFrameDistributor.h"
#include "video/encoder/VideoEncoder.h"
#include "video/encoder/VideoEncoderConfig.h"
#include "video/encoder/VideoEncoderListener.h"
#include "midi/MidiMapper.h"
#include "midi/MidiLearnManager.h"

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
//    - Reads: pttActive (for Instatalk PTT callback via SetLocalChannelProcessor)
//    - Note: Measurement atomics (t_insta, t_interval) live on NJClient, not Processor
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
                           , public jamwide::VideoEncoderListener
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

    // v4: added camera flat properties (Phase 19-02; D-24, D-25).
    static constexpr int currentStateVersion = 4;
    static constexpr int kTotalOutChannels = 34;  // 17 stereo buses
    static constexpr int kNumOutputBuses = 17;
    static constexpr int kMetronomeBus = 16;      // Last bus (channels 32-33)

    NJClient* getClient() { return client.get(); }
    juce::CriticalSection& getClientLock() { return clientLock; }

    // 2026-05-03: build a multi-line diagnostic report (counters + per-(slot,
    // channel) mirror snapshot + per-peer summary). Acquires clientLock,
    // reads relaxed-load counter / mirror state — same risk profile as
    // GetUserChannelPeak. Returned as a single std::string with "\n"
    // separators. Used by both:
    //  - ChatPanel /rcmstats command (split into System messages for chat)
    //  - ConnectionBar Debug-Snapshot button (written to a log file)
    std::string buildDiagnosticReport() const;

    // Write the diagnostic report to a timestamped file under
    // userLogsDirectory()/JamWide/. Returns the file path (or empty
    // string on failure). Includes build/connection/timing context that
    // the chat-only readout omits.
    juce::File writeDebugSnapshot() const;

    // OSC server (owned by processor, UI accesses via reference)
    std::unique_ptr<OscServer> oscServer;

    // MIDI mapper (owned by processor, UI accesses via reference)
    std::unique_ptr<MidiMapper> midiMapper;
    MidiLearnManager midiLearnManager;

    // Video companion (owned by processor, UI accesses via reference)
    std::unique_ptr<jamwide::VideoCompanion> videoCompanion;

    // Phase 19-01: native camera capture + frame fan-out (owned by processor).
    // frameDistributor MUST outlive nativeCamera_ — the camera publishes into
    // the distributor on the camera-callback thread. Subscribers (preview tile
    // in 19-02, encoder in Phase 20) attach via frameDistributor->registerSubscriber.
    std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor;
    std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera;

    // Phase 21-03 Task 1: receive-side decoded-frame fan-out distributor.
    // Symmetric inverse of the camera-side frameDistributor — owns one
    // PeerVideoSink per (username, chidx); Phase 22 grid tile + popout
    // subscribe via subscribeToPeer. NJClient lazy-creates a sink when a
    // peer's first H.264 BEGIN arrives (Plan 21-03 Task 2 two-phase
    // startup); removes via four-step shutdown protocol on user-leave
    // (codex Cluster 3). MUST outlive NJClient — see ~JamWideJuceProcessor
    // dtor order (client.reset() BEFORE remoteFrameDistributor.reset()
    // per codex Cluster 3 reversed order).
    std::unique_ptr<jamwide::JamWideRemoteFrameDistributor> remoteFrameDistributor;

    // Phase 20-03 — H.264 encoder owned by the processor. Constructed when
    // the camera reaches Capturing (onCameraStateChangedFromEditor); reset
    // when the camera goes Idle/Failed/Unavailable. The encoder THREAD
    // only starts on Broadcast=on (videoEncoder->open() called by
    // setBroadcastVideo(true)) — idle preview costs nothing per D-13.
    // The frameDistributor MUST outlive the encoder — encoder releases
    // its Subscription in step 2 of R4 H9 7-step close(); destructors
    // run in reverse declaration order (frameDistributor declared first)
    // but we also clear videoEncoder BEFORE frameDistributor explicitly
    // in ~JamWideJuceProcessor to remove any ambiguity.
    std::unique_ptr<jamwide::VideoEncoder> videoEncoder;
    std::atomic<bool> broadcastVideoEnabled{false};

#ifdef JAMWIDE_BUILD_TESTS
    // Plan 20-03 Task 2 sub-test 3 (T-20-03 lifecycle-ordering enforcement):
    // increment-and-record the call order of SetVideoBroadcastActive(false)
    // and videoEncoder->close() during setBroadcastVideo(false). The test
    // reads both indices and asserts deactivate_seq < encoder_close_seq.
    mutable std::atomic<int> testLifecycleSequenceCounter_{0};
    mutable std::atomic<int> testLifecycleSeqDeactivate_{-1};
    mutable std::atomic<int> testLifecycleSeqEncoderClose_{-1};
    void testResetLifecycleSequence() noexcept {
        testLifecycleSequenceCounter_.store(0, std::memory_order_relaxed);
        testLifecycleSeqDeactivate_.store(-1, std::memory_order_relaxed);
        testLifecycleSeqEncoderClose_.store(-1, std::memory_order_relaxed);
    }
    int testGetLifecycleSeqDeactivate() const noexcept {
        return testLifecycleSeqDeactivate_.load(std::memory_order_relaxed);
    }
    int testGetLifecycleSeqEncoderClose() const noexcept {
        return testLifecycleSeqEncoderClose_.load(std::memory_order_relaxed);
    }
#endif

    jamwide::JamWideCameraDevice* getNativeCamera() { return nativeCamera.get(); }
    jamwide::JamWideFrameDistributor* getFrameDistributor() { return frameDistributor.get(); }

    // Phase 21-03 Task 1: Phase 22 tiles get a reference to the receive-side
    // distributor via this accessor.
    jamwide::JamWideRemoteFrameDistributor* getRemoteFrameDistributor() {
        return remoteFrameDistributor.get();
    }

    // ─── Phase 20-03 — H.264 broadcast surface ──────────────────────────────
    // The processor owns the Openh264Encoder; the editor's FallbackListener
    // forwards camera-state changes via onCameraStateChangedFromEditor so the
    // encoder lifecycle tracks the camera per CONTEXT.md D-13: encoder
    // instance is constructed when the camera reaches Capturing and destroyed
    // when it goes Idle/Failed/Unavailable. The encoder THREAD only starts on
    // Broadcast=on (open() call inside setBroadcastVideo(true)) — idle CPU
    // cost is zero when previewing without broadcasting.
    //
    // Threading: setBroadcastVideo / isBroadcastingVideo / getCurrentCameraPreset
    // run on the message thread. The VideoEncoderListener overrides run on
    // the encoder thread (the encoder owns its own juce::Thread); they
    // marshal logging/recovery via juce::Logger::writeToLog / callAsync.
    void setBroadcastVideo(bool enabled);
    bool isBroadcastingVideo() const noexcept {
        return broadcastVideoEnabled.load(std::memory_order_acquire);
    }
    int  getCurrentCameraPreset() const noexcept {
        return getCameraQualityPreset();   // Phase 19 D-25 source of truth
    }
    // The editor's FallbackListener calls this from onCameraStateChanged so
    // the processor can drive encoder construction/destruction without
    // taking ownership of the FallbackListener slot from the editor.
    void onCameraStateChangedFromEditor(jamwide::CameraState newState);

    // VideoEncoderListener overrides (Plan 20-01 contract; called on encoder
    // thread). Default implementations log via juce::Logger::writeToLog;
    // onEncoderFatalError schedules a reconfigure on the message thread.
    void onEncoderOpened(const jamwide::VideoEncoderConfig& cfg) override;
    void onEncoderClosed() override;
    void onEncoderReconfigured(const jamwide::VideoEncoderConfig& cfg) override;
    void onEncoderFatalError(const char* reason) override;
    void onSpsPpsPublished(int spsPpsLen) override;

    // Phase 19-02 — persisted camera fields (D-24, D-25). Backed by atomics
    // for the lock-free read paths (UI thread reads + state save block) and
    // a small mutex for the popout bounds + selected device which are
    // composite values. The state schema bumps to v4 in Task 3.
    juce::Rectangle<int> getCameraPopoutBounds() const;
    void setCameraPopoutBounds(juce::Rectangle<int> bounds);

    int  getCameraQualityPreset() const noexcept {
        return cameraQualityPreset_.load(std::memory_order_relaxed);
    }
    void setCameraQualityPreset(int preset) noexcept {
        cameraQualityPreset_.store(juce::jlimit(0, 2, preset),
                                   std::memory_order_relaxed);
    }

    bool getCameraPrivacyAck() const noexcept {
        return cameraPrivacyAck_.load(std::memory_order_relaxed);
    }
    void setCameraPrivacyAck(bool ack) noexcept {
        cameraPrivacyAck_.store(ack, std::memory_order_relaxed);
    }

    juce::String getCameraSelectedDevice() const;
    void setCameraSelectedDevice(const juce::String& name);

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
    // Written by run thread via GetRemoteUsersSnapshot(), read by message thread.
    // ALL access (read or write) must hold cachedUsersMutex. The run thread
    // replaces the vector via std::move on structural changes and also updates
    // per-channel VU levels in place; iterating without the lock from the
    // message thread can race with the structural replacement and access
    // freed memory.
    mutable std::mutex cachedUsersMutex;
    std::vector<NJClient::RemoteUserInfo> cachedUsers;

    // User count (atomic for lock-free UI read)
    std::atomic<int> userCount{0};

    // Visible (non-bot) remote user count and slot-to-NJClient mapping.
    // Written by message thread (refreshFromUsers), read by message thread (timerCallback).
    std::atomic<int> visibleRemoteUserCount{0};
    std::array<int, 16> remoteSlotToUserIndex{}; // APVTS slot → NJClient user_index

    // Last error message from server (written by editor drain, read by ConnectionBar)
    juce::String lastErrorMsg;

    // Persistent UI state (survives editor destruction)
    juce::String lastServerAddress{"ninbot.com"};
    juce::String lastUsername{"anonymous"};
    float scaleFactor{1.0f};

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

    // ── Prelisten state (Phase 14.1 — BROWSE-01) ──
    // Written by NinjamRunThread on PrelistenCommand/StopPrelistenCommand.
    // Read by audio thread (processBlock) and message thread (editor/connection bar).
    std::atomic<bool>  prelisten_mode{false};
    std::atomic<float> prelisten_volume{0.7f};
    std::atomic<float> savedMetronomeVolume_{0.5f}; // saved before prelisten mutes it

    // -- Phase 14.2: PTT + measurement broadcast guard (VID-13) --
    // pttActive: message thread writes (PTT button/key), audio thread reads (via SetLocalChannelProcessor callback)
    // instaMeasurementBroadcast: run thread sets true after caching/broadcasting measured delay to VideoCompanion
    // These are the ONLY Phase 14.2 atomics on the Processor. All measurement state lives in NJClient.
    std::atomic<bool> pttActive{false};
    std::atomic<bool> instaMeasurementBroadcast{false};

    // MIDI standalone device persistence (stable identifiers per review feedback)
    juce::String midiInputDeviceId;
    juce::String midiOutputDeviceId;

    // OSC config (persisted via ValueTree, per D-21)
    bool oscEnabled{false};
    int oscReceivePort{9000};               // per D-17
    juce::String oscSendIP{"127.0.0.1"};    // per D-17
    int oscSendPort{9001};                  // per D-17

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

    // Phase 19-02 — backing storage for the persisted camera fields. These are
    // public-section members so the unit-test in tests/test_plugin_state_v3_v4
    // can replicate the read logic; in production they are accessed only via
    // the get/set wrappers above.
    std::atomic<int>  cameraQualityPreset_{1};
    std::atomic<bool> cameraPrivacyAck_{false};
    juce::String cameraSelectedDevice_;
    mutable std::mutex cameraSelectedDeviceMu_;
    juce::Rectangle<int> cameraPopoutBounds_{100, 100, 320, 240};
    mutable std::mutex cameraPopoutMu_;

private:
    std::unique_ptr<NJClient> client;
    juce::CriticalSection clientLock;
    juce::AudioBuffer<float> inputScratch;
    juce::AudioBuffer<float> outputScratch;
    double storedSampleRate = 48000.0;

    // 15.1-08 M-03: latched copy of the host-promised maximum samplesPerBlock
    // captured in prepareToPlay. The processBlock jassert below catches a
    // host that violates its own getMaximumExpectedSamplesPerBlock contract
    // in Debug builds. In Release the M-7 throw at SetMaxAudioBlockSize +
    // the per-callsite bounds check (15.1-07b pushBlockRecord) backstop.
    int prevPreparedSize = 0;

    // Audio-thread-only edge detection state (no sync primitive needed -- single thread)
    // rawHostPlaying_ stores the ACTUAL host transport state before any overrides.
    // wasPlaying_ could otherwise store the overridden value (false during WAITING),
    // causing spurious edge detection after WAITING->ACTIVE transition.
    // (Addresses Claude MEDIUM review concern: wasPlaying_ set to overridden hostPlaying)
    bool wasPlaying_{false};
    bool rawHostPlaying_{false};  // Raw transport state for edge detection

    // Previous PPQ position for seek/loop detection
    double prevPpqPos_{0.0};

    // Previous sync state for detecting IDLE->WAITING transition in audio thread
    int prevSyncState_{0};  // kSyncIdle

    // processBlock helpers (audio-thread safe, no allocations)
    void syncApvtsToAtomics();
    int  collectInputChannels(juce::AudioBuffer<float>& buffer, float* inPtrs[], int numSamples);
    bool handleTransportSync(int numSamples);
    void accumulateBusesToMainMix(float* outPtrs[], int numSamples);
    void routeOutputsToJuceBuses(juce::AudioBuffer<float>& buffer, int numSamples);
    void measureMasterVu(int numSamples);

    std::unique_ptr<NinjamRunThread> runThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamWideJuceProcessor)
};
