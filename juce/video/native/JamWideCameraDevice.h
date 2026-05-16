#pragma once
// Phase 19-01 Task 5: JamWideCameraDevice — CameraDevice owner + lifecycle.
//
// HIGH-3 mitigation (generation tokens): every async callback closure captures
// `myGen = generation_.load()` at scheduling time and bails on
// `generation_.load() != myGen` before touching `this`. shutdown() bumps
// generation_ FIRST (release order) so any in-flight closures invalidate.
//
// HIGH-6 mitigation (continuous frame-stall watchdog): while state==Capturing
// a juce::Timer ticks every FRAME_STALL_POLL_MS (1000 ms); on a frame gap
// >FRAME_STALL_THRESHOLD_MS (2000 ms) it re-queries authorization and
// dispatches WatchdogFired with a hint based on the re-query result.
//
// D-10 enforcement: constructor leaves the state machine at Idle regardless
// of saved popout state. Capture only starts after explicit toggle().
//
// Threading: every state-touching method runs on the JUCE message thread,
// enforced by juce::MessageManager::callAsync from every async callsite. The
// only exception is the lock-free frame-stall timestamp update inside
// CameraListener::imageReceived (which runs on the camera-callback thread).
#include "CameraAuthorization.h"
#include "CameraFallbackCause.h"
#include "CameraStateMachine.h"

#include <juce_video/juce_video.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

namespace jamwide {

class JamWideFrameDistributor;

class JamWideCameraDevice {
public:
    // Listener notified when the device transitions state or emits a fallback
    // cause. Phase 19-02's editor implements this to drive the camera status
    // dialog + preview tile visibility.
    class FallbackListener {
    public:
        virtual ~FallbackListener() = default;
        virtual void onCameraFallback(CameraFallbackCause cause) = 0;
        virtual void onCameraStateChanged(CameraState newState) = 0;
    };

    JamWideCameraDevice(JamWideFrameDistributor& distributor,
                        FallbackListener* listener = nullptr);
    ~JamWideCameraDevice();

    JamWideCameraDevice(const JamWideCameraDevice&) = delete;
    JamWideCameraDevice& operator=(const JamWideCameraDevice&) = delete;

    // ─── Public API (UI/processor) ─────────────────────────────────────────
    // All public methods run on the JUCE message thread.

    // Toggle camera on/off. From Idle: query auth → open device → start
    // capture. From Capturing/Opening/Retrying: stop capture → Idle.
    // From Unavailable: re-query auth (D-12).
    void toggle();

    // D-12: explicit re-check after the user opens system Privacy settings.
    void recheckPermission();

    // 0=Low (320x240), 1=Medium (640x480), 2=High (1280x720). Default Medium.
    // Changing the preset while Capturing does NOT re-open the device — takes
    // effect on the next openDevice cycle.
    void setQualityPreset(int preset) noexcept;
    int  getQualityPreset() const noexcept {
        return qualityPreset_.load(std::memory_order_relaxed);
    }

    CameraState getState() const noexcept;
    juce::String getDeviceName() const;
    float getPeakFps() const;

    // HIGH-3: bump generation_ FIRST, then tear down. Safe to call from the
    // message thread; afterward, all pending async closures bail before
    // touching `this`.
    void shutdown();

    void setFallbackListener(FallbackListener* listener) noexcept {
        fallbackListener_ = listener;
    }

    // Tunable constants — exposed for UAT (LOW-1 future tightening).
    static constexpr int FIRST_FRAME_WATCHDOG_MS = 3000;
    static constexpr int FRAME_STALL_THRESHOLD_MS = 2000;
    static constexpr int FRAME_STALL_POLL_MS = 1000;
    static constexpr int RETRY_BUDGET_MS = 30000;
    static constexpr int RETRY_MAX_ATTEMPTS = 5;

private:
    // HIGH-3: generation token. All async closures capture-by-value the
    // current value and bail if it changes before they run. shutdown()
    // increments via fetch_add(release).
    std::atomic<std::uint64_t> generation_{1};

    // HIGH-6: last frame timestamp (ms since epoch) for frame-stall detection.
    // Updated by CameraListener::imageReceived on the camera-callback thread.
    std::atomic<std::int64_t> lastFrameMs_{0};
    std::atomic<bool> firstFrameSeen_{false};

    // ─── Inner classes (defined in .cpp) ───────────────────────────────────
    class CameraListener;
    class FirstFrameWatchdog;
    class FrameStallWatchdog;
    class RetryWorker;

    CameraStateMachine stateMachine_;
    std::unique_ptr<juce::CameraDevice> juceCamera_;
    std::unique_ptr<CameraListener> listenerForwarder_;
    std::unique_ptr<FirstFrameWatchdog> firstFrameWatchdog_;
    std::unique_ptr<FrameStallWatchdog> frameStallWatchdog_;
    std::unique_ptr<RetryWorker> retryWorker_;

    JamWideFrameDistributor& distributor_;
    FallbackListener* fallbackListener_ = nullptr;

    std::atomic<int> qualityPreset_{1};
    juce::String deviceName_;
    mutable std::mutex deviceNameMu_;

    // ─── Implementation entry points (all run on the message thread) ──────
    void handleUserToggleInternal();
    void handleAuthResult(CameraAuthStatus status, std::uint64_t myGen);
    void onOpenResult(juce::CameraDevice* dev, const juce::String& error,
                      std::uint64_t myGen);
    void onFirstFrame();
    void onFirstFrameWatchdogFired(std::uint64_t myGen);
    void onFrameStallTick(std::uint64_t myGen);
    void onRuntimeError(const juce::String& error, std::uint64_t myGen);
    void onRetryTick(std::uint64_t myGen);
    void onRetryExhausted();

    void scheduleOpenDevice();
    void closeHardware();
    void actuateDispatchResult(const DispatchResult& result);

    CameraFallbackCause classifyDenialCause(CameraAuthStatus status) const;
    CameraFallbackCause classifyOpenFailure(const juce::String& error) const;
    void getResolution(int& minW, int& minH, int& maxW, int& maxH) const;
};

} // namespace jamwide
