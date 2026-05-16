// Phase 19-01 Task 5: JamWideCameraDevice implementation.
//
// HIGH-3 sites (generation_.load() before touching this) — 5 enumerated:
//   1. requestCameraAuthorization completion handler
//   2. juce::CameraDevice::openDeviceAsync result callback
//   3. FirstFrameWatchdog timer callback
//   4. FrameStallWatchdog timer callback (HIGH-6 hook)
//   5. CameraListener::imageReceived → onFirstFrame callAsync
//   (Also: juceCamera_->onErrorOccurred lambda → onRuntimeError callAsync,
//   and the RetryWorker reopen callback. Listed below where they fire.)
#include "JamWideCameraDevice.h"

#include "CameraStateMachine.h"
#include "JamWideFrameDistributor.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <utility>

namespace jamwide {

// ─── Inner: CameraListener ─────────────────────────────────────────────────

class JamWideCameraDevice::CameraListener : public juce::CameraDevice::Listener {
public:
    explicit CameraListener(JamWideCameraDevice& owner, std::uint64_t gen)
        : owner_(owner), gen_(gen) {}

    void imageReceived(const juce::Image& image) override {
        // HIGH-6 hot-path update — relaxed store; reads in onFrameStallTick.
        owner_.lastFrameMs_.store(juce::Time::currentTimeMillis(),
                                  std::memory_order_release);

        // Fan out to subscribers (preview tile, encoder). publish() is
        // thread-safe.
        owner_.distributor_.publish(image);

        // First-frame transition — HIGH-3 #5: dispatch onto message thread
        // with generation check.
        if (!owner_.firstFrameSeen_.exchange(true, std::memory_order_acq_rel)) {
            const auto myGen = gen_;
            juce::WeakReference<juce::MessageManager> mm;
            juce::MessageManager::callAsync([this, myGen]() {
                if (owner_.generation_.load(std::memory_order_acquire) != myGen) return;
                owner_.onFirstFrame();
            });
        }
    }

private:
    JamWideCameraDevice& owner_;
    std::uint64_t gen_;
};

// ─── Inner: FirstFrameWatchdog (juce::Timer single-shot) ───────────────────

class JamWideCameraDevice::FirstFrameWatchdog : public juce::Timer {
public:
    FirstFrameWatchdog(JamWideCameraDevice& owner, std::uint64_t gen)
        : owner_(owner), gen_(gen) {}

    void timerCallback() override {
        stopTimer();
        // HIGH-3 #3: generation check on the message-thread timer dispatch.
        if (owner_.generation_.load(std::memory_order_acquire) != gen_) return;
        owner_.onFirstFrameWatchdogFired(gen_);
    }

private:
    JamWideCameraDevice& owner_;
    std::uint64_t gen_;
};

// ─── Inner: FrameStallWatchdog (juce::Timer continuous, HIGH-6) ────────────

class JamWideCameraDevice::FrameStallWatchdog : public juce::Timer {
public:
    FrameStallWatchdog(JamWideCameraDevice& owner, std::uint64_t gen)
        : owner_(owner), gen_(gen) {}

    void timerCallback() override {
        // HIGH-3 #4: generation check (HIGH-6 timer).
        if (owner_.generation_.load(std::memory_order_acquire) != gen_) {
            stopTimer();
            return;
        }
        const auto now_ms = juce::Time::currentTimeMillis();
        const auto last   = owner_.lastFrameMs_.load(std::memory_order_acquire);
        const auto gap    = now_ms - last;
        if (gap > FRAME_STALL_THRESHOLD_MS) {
            // Stop the timer here so we don't refire while the actuator
            // tears down the camera; the state machine will restart things
            // on the retry path.
            stopTimer();
            owner_.onFrameStallTick(gen_);
        }
    }

private:
    JamWideCameraDevice& owner_;
    std::uint64_t gen_;
};

// ─── Inner: RetryWorker (juce::Thread) ─────────────────────────────────────
//
// Owns the 1s/2s/4s/8s/16s backoff schedule. After each delay, posts a
// MessageManager::callAsync to onRetryTick(). After MAX_ATTEMPTS, posts
// onRetryExhausted().

class JamWideCameraDevice::RetryWorker : public juce::Thread {
public:
    RetryWorker(JamWideCameraDevice& owner, std::uint64_t gen)
        : juce::Thread("JamWideCameraRetryWorker"),
          owner_(owner),
          gen_(gen) {}

    void run() override {
        int attempt = 0;
        while (!threadShouldExit() &&
               attempt < CameraStateMachine::RetryBackoff::MAX_ATTEMPTS) {
            const int delayMs = CameraStateMachine::RetryBackoff::delayMs(attempt);
            // Wait in 100 ms slices so threadShouldExit() can interrupt quickly.
            int remaining = delayMs;
            while (remaining > 0 && !threadShouldExit()) {
                const int slice = std::min(100, remaining);
                wait(slice);
                remaining -= slice;
            }
            if (threadShouldExit()) return;

            const auto myGen = gen_;
            juce::MessageManager::callAsync([this, myGen]() {
                // HIGH-3: generation check on retry tick callback.
                if (owner_.generation_.load(std::memory_order_acquire) != myGen) return;
                owner_.onRetryTick(myGen);
            });

            ++attempt;
        }
        if (!threadShouldExit()) {
            const auto myGen = gen_;
            juce::MessageManager::callAsync([this, myGen]() {
                if (owner_.generation_.load(std::memory_order_acquire) != myGen) return;
                owner_.onRetryExhausted();
            });
        }
    }

private:
    JamWideCameraDevice& owner_;
    std::uint64_t gen_;
};

// ─── JamWideCameraDevice public API ────────────────────────────────────────

JamWideCameraDevice::JamWideCameraDevice(JamWideFrameDistributor& distributor,
                                         FallbackListener* listener)
    : distributor_(distributor),
      fallbackListener_(listener) {
    // D-10: ALWAYS Idle at construction, regardless of saved popout state.
    // The state machine default-constructs to Idle; nothing else to do.
    juce::Logger::writeToLog("[JamWideCamera] Constructed (state=Idle, D-10).");
}

JamWideCameraDevice::~JamWideCameraDevice() {
    // Synchronous teardown: bumps generation_, stops timers/threads, releases hardware.
    shutdown();
}

void JamWideCameraDevice::toggle() {
    handleUserToggleInternal();
}

void JamWideCameraDevice::recheckPermission() {
    auto status = jamwide::queryCameraAuthorization();
    if (status == CameraAuthStatus::Authorized || status == CameraAuthStatus::NotApplicable) {
        auto result = stateMachine_.dispatch(CameraEvent::RecheckPermission);
        actuateDispatchResult(result);
        juce::Logger::writeToLog("[JamWideCamera] Recheck OK; transitioning to Opening.");
    } else {
        stateMachine_.setFallbackHint(classifyDenialCause(status));
        auto result = stateMachine_.dispatch(CameraEvent::AuthDenied);
        actuateDispatchResult(result);
        juce::Logger::writeToLog("[JamWideCamera] Recheck DENIED; staying Unavailable.");
    }
}

void JamWideCameraDevice::setQualityPreset(int preset) noexcept {
    const int clamped = std::clamp(preset, 0, 2);
    qualityPreset_.store(clamped, std::memory_order_relaxed);
}

CameraState JamWideCameraDevice::getState() const noexcept {
    // CameraStateMachine::getState() is not thread-safe; we assume callers
    // are on the message thread or accept a relaxed snapshot.
    return const_cast<CameraStateMachine&>(stateMachine_).getState();
}

juce::String JamWideCameraDevice::getDeviceName() const {
    std::lock_guard<std::mutex> lock(deviceNameMu_);
    return deviceName_;
}

float JamWideCameraDevice::getPeakFps() const {
    return distributor_.getPeakFps();
}

void JamWideCameraDevice::shutdown() {
    // HIGH-3: bump generation FIRST so any in-flight async closures bail.
    generation_.fetch_add(1, std::memory_order_release);

    // Stop the retry worker (signals threadShouldExit, waits up to 500 ms).
    if (retryWorker_) {
        retryWorker_->stopThread(500);
        retryWorker_.reset();
    }

    // Stop the watchdog timers. Both must be reset on the message thread
    // (juce::Timer requires this). Caller is responsible for invoking
    // shutdown() from the message thread — the destructor path comes from
    // JamWideJuceProcessor::~JamWideJuceProcessor which IS the message thread.
    firstFrameWatchdog_.reset();
    frameStallWatchdog_.reset();

    // Remove the listener BEFORE releasing the device.
    if (juceCamera_ && listenerForwarder_) {
        juceCamera_->removeListener(listenerForwarder_.get());
    }
    listenerForwarder_.reset();
    juceCamera_.reset();

    // Reset the state machine to Idle via Shutdown dispatch.
    (void)stateMachine_.dispatch(CameraEvent::Shutdown);
    firstFrameSeen_.store(false, std::memory_order_release);
    lastFrameMs_.store(0, std::memory_order_release);

    juce::Logger::writeToLog("[JamWideCamera] Shutdown complete (generation bumped).");
}

// ─── Internal entry points ─────────────────────────────────────────────────

void JamWideCameraDevice::handleUserToggleInternal() {
    const auto current = stateMachine_.getState();
    if (current == CameraState::Capturing || current == CameraState::Opening ||
        current == CameraState::Retrying) {
        // Toggle off.
        auto result = stateMachine_.dispatch(CameraEvent::UserToggle);
        actuateDispatchResult(result);
        juce::Logger::writeToLog("[JamWideCamera] UserToggle OFF (state → Idle).");
        return;
    }

    // From Idle / Unavailable: query auth, then transition.
    auto status = jamwide::queryCameraAuthorization();
    switch (status) {
    case CameraAuthStatus::Authorized:
    case CameraAuthStatus::NotApplicable: {
        // Two-step: UserToggle (Idle → Opening) then AuthGranted (no-op
        // transition; informational).
        auto r1 = stateMachine_.dispatch(CameraEvent::UserToggle);
        actuateDispatchResult(r1);
        auto r2 = stateMachine_.dispatch(CameraEvent::AuthGranted);
        actuateDispatchResult(r2);
        break;
    }
    case CameraAuthStatus::Denied:
    case CameraAuthStatus::Restricted: {
        // Two-step dispatch with the hint set before AuthDenied so the
        // state machine populates emitFallback.
        auto r1 = stateMachine_.dispatch(CameraEvent::UserToggle);
        actuateDispatchResult(r1);
        stateMachine_.setFallbackHint(classifyDenialCause(status));
        auto r2 = stateMachine_.dispatch(CameraEvent::AuthDenied);
        actuateDispatchResult(r2);
        juce::Logger::writeToLog("[JamWideCamera] TCC denied at toggle; emitting fallback.");
        break;
    }
    case CameraAuthStatus::NotDetermined: {
        // Ask. Capture the current generation; on completion, marshal to
        // message thread and dispatch via state machine.
        const auto myGen = generation_.load(std::memory_order_acquire);
        // HIGH-3 #1: TCC completion handler check.
        jamwide::requestCameraAuthorization(
            [this, myGen](CameraAuthStatus s) {
                juce::MessageManager::callAsync([this, myGen, s]() {
                    if (generation_.load(std::memory_order_acquire) != myGen) return;
                    handleAuthResult(s, myGen);
                });
            });
        // Dispatch UserToggle now (state → Opening) so the UI updates while
        // the system prompt is shown; AuthGranted/Denied lands later.
        auto r1 = stateMachine_.dispatch(CameraEvent::UserToggle);
        actuateDispatchResult(r1);
        juce::Logger::writeToLog("[JamWideCamera] TCC NotDetermined; prompting user.");
        break;
    }
    }
}

void JamWideCameraDevice::handleAuthResult(CameraAuthStatus status, std::uint64_t /*myGen*/) {
    if (status == CameraAuthStatus::Authorized ||
        status == CameraAuthStatus::NotApplicable) {
        auto r = stateMachine_.dispatch(CameraEvent::AuthGranted);
        actuateDispatchResult(r);
    } else {
        stateMachine_.setFallbackHint(classifyDenialCause(status));
        auto r = stateMachine_.dispatch(CameraEvent::AuthDenied);
        actuateDispatchResult(r);
    }
}

void JamWideCameraDevice::scheduleOpenDevice() {
    int minW, minH, maxW, maxH;
    getResolution(minW, minH, maxW, maxH);

    const auto myGen = generation_.load(std::memory_order_acquire);
    // HIGH-3 #2: openDeviceAsync result callback.
    juce::CameraDevice::openDeviceAsync(
        0,
        [this, myGen](juce::CameraDevice* dev, const juce::String& error) {
            juce::MessageManager::callAsync([this, myGen, dev, error]() {
                if (generation_.load(std::memory_order_acquire) != myGen) {
                    // Stale callback — delete the device we got (we won't own it)
                    delete dev;
                    return;
                }
                onOpenResult(dev, error, myGen);
            });
        },
        minW, minH, maxW, maxH, true);
}

void JamWideCameraDevice::onOpenResult(juce::CameraDevice* dev,
                                       const juce::String& error,
                                       std::uint64_t myGen) {
    if (dev != nullptr) {
        juceCamera_.reset(dev);
        // Build a fresh listener forwarder with the current generation so
        // imageReceived's callAsync includes the right gen.
        listenerForwarder_ = std::make_unique<CameraListener>(*this, myGen);
        juceCamera_->addListener(listenerForwarder_.get());
        // Hook the JUCE on-error callback. HIGH-3 (extra site): the lambda
        // captures myGen so a stale error from a previous device is ignored.
        juceCamera_->onErrorOccurred = [this, myGen](const juce::String& err) {
            juce::MessageManager::callAsync([this, myGen, err]() {
                if (generation_.load(std::memory_order_acquire) != myGen) return;
                onRuntimeError(err, myGen);
            });
        };
        {
            std::lock_guard<std::mutex> lock(deviceNameMu_);
            deviceName_ = juceCamera_->getName();
        }
        firstFrameSeen_.store(false, std::memory_order_release);
        auto r = stateMachine_.dispatch(CameraEvent::OpenSucceeded);
        actuateDispatchResult(r);
        juce::Logger::writeToLog("[JamWideCamera] Open OK: device=" + deviceName_);
    } else {
        stateMachine_.setFallbackHint(classifyOpenFailure(error));
        auto r = stateMachine_.dispatch(CameraEvent::OpenFailed);
        actuateDispatchResult(r);
        juce::Logger::writeToLog("[JamWideCamera] Open FAILED: err=" + error);
    }
}

void JamWideCameraDevice::onFirstFrame() {
    auto r = stateMachine_.dispatch(CameraEvent::FirstFrameReceived);
    actuateDispatchResult(r);
    juce::Logger::writeToLog("[JamWideCamera] First frame received.");
}

void JamWideCameraDevice::onFirstFrameWatchdogFired(std::uint64_t /*myGen*/) {
    stateMachine_.setFallbackHint(CameraFallbackCause::CameraInUse);
    auto r = stateMachine_.dispatch(CameraEvent::WatchdogFired);
    actuateDispatchResult(r);
    juce::Logger::writeToLog("[JamWideCamera] First-frame watchdog fired after "
                             + juce::String(FIRST_FRAME_WATCHDOG_MS) + " ms.");
}

void JamWideCameraDevice::onFrameStallTick(std::uint64_t /*myGen*/) {
    const auto now_ms = juce::Time::currentTimeMillis();
    const auto last   = lastFrameMs_.load(std::memory_order_acquire);
    const auto gap    = now_ms - last;

    auto status = jamwide::queryCameraAuthorization();
    CameraFallbackCause cause = CameraFallbackCause::CameraInUse;
    if (status == CameraAuthStatus::Denied || status == CameraAuthStatus::Restricted) {
        cause = classifyDenialCause(status);
    }
    stateMachine_.setFallbackHint(cause);

    juce::Logger::writeToLog(
        "[JamWideCamera] Frame stall detected (gap=" + juce::String((juce::int64)gap) +
        " ms, auth=" + juce::String(static_cast<int>(status)) +
        ") — transitioning to Retrying.");

    auto r = stateMachine_.dispatch(CameraEvent::WatchdogFired);
    actuateDispatchResult(r);
}

void JamWideCameraDevice::onRuntimeError(const juce::String& error, std::uint64_t /*myGen*/) {
    stateMachine_.setFallbackHint(classifyOpenFailure(error));
    auto r = stateMachine_.dispatch(CameraEvent::RuntimeError);
    actuateDispatchResult(r);
    juce::Logger::writeToLog("[JamWideCamera] Runtime error: " + error);
}

void JamWideCameraDevice::onRetryTick(std::uint64_t /*myGen*/) {
    auto r = stateMachine_.dispatch(CameraEvent::RetryTick);
    actuateDispatchResult(r);
    juce::Logger::writeToLog("[JamWideCamera] Retry tick — reopening.");
}

void JamWideCameraDevice::onRetryExhausted() {
    stateMachine_.setFallbackHint(CameraFallbackCause::CameraInUse);
    auto r = stateMachine_.dispatch(CameraEvent::RetryExhausted);
    actuateDispatchResult(r);
    juce::Logger::writeToLog("[JamWideCamera] Retry exhausted after "
                             + juce::String(RETRY_MAX_ATTEMPTS)
                             + " attempts; transitioning to Unavailable.");
}

// ─── Actuator ──────────────────────────────────────────────────────────────

void JamWideCameraDevice::closeHardware() {
    if (juceCamera_ && listenerForwarder_) {
        juceCamera_->removeListener(listenerForwarder_.get());
    }
    listenerForwarder_.reset();
    juceCamera_.reset();
    firstFrameSeen_.store(false, std::memory_order_release);
    lastFrameMs_.store(0, std::memory_order_release);
}

void JamWideCameraDevice::actuateDispatchResult(const DispatchResult& result) {
    if (result.closeHardware) closeHardware();
    if (result.stopFirstFrameWatchdog) firstFrameWatchdog_.reset();
    if (result.stopFrameStallWatchdog) frameStallWatchdog_.reset();
    if (result.stopRetryWorker) {
        if (retryWorker_) retryWorker_->stopThread(500);
        retryWorker_.reset();
    }
    if (result.startOpenDevice) {
        scheduleOpenDevice();
    }
    if (result.startFirstFrameWatchdog) {
        firstFrameWatchdog_ = std::make_unique<FirstFrameWatchdog>(
            *this, generation_.load(std::memory_order_acquire));
        firstFrameWatchdog_->startTimer(FIRST_FRAME_WATCHDOG_MS);
    }
    if (result.startFrameStallWatchdog) {
        frameStallWatchdog_ = std::make_unique<FrameStallWatchdog>(
            *this, generation_.load(std::memory_order_acquire));
        lastFrameMs_.store(juce::Time::currentTimeMillis(),
                           std::memory_order_release);
        frameStallWatchdog_->startTimer(FRAME_STALL_POLL_MS);
    }
    if (result.startRetryWorker) {
        retryWorker_ = std::make_unique<RetryWorker>(
            *this, generation_.load(std::memory_order_acquire));
        retryWorker_->startThread();
    }

    if (result.emitFallback.has_value() && fallbackListener_ != nullptr) {
        fallbackListener_->onCameraFallback(*result.emitFallback);
    }
    if (fallbackListener_ != nullptr) {
        fallbackListener_->onCameraStateChanged(result.newState);
    }
}

// ─── Cause classification ──────────────────────────────────────────────────

CameraFallbackCause JamWideCameraDevice::classifyDenialCause(CameraAuthStatus status) const {
    const bool isPlugin = !juce::JUCEApplicationBase::isStandaloneApp();
    if (status == CameraAuthStatus::Restricted) return CameraFallbackCause::TCCDenied;
    if (status == CameraAuthStatus::Denied) {
        return isPlugin ? CameraFallbackCause::HostLacksEntitlement
                        : CameraFallbackCause::TCCDenied;
    }
    return CameraFallbackCause::None;
}

CameraFallbackCause JamWideCameraDevice::classifyOpenFailure(const juce::String& /*error*/) const {
    if (juce::CameraDevice::getAvailableDevices().isEmpty()) {
        return CameraFallbackCause::NoHardware;
    }
   #if JUCE_WINDOWS
    return CameraFallbackCause::WindowsPrivacyBlock;
   #else
    return CameraFallbackCause::CameraInUse;
   #endif
}

void JamWideCameraDevice::getResolution(int& minW, int& minH, int& maxW, int& maxH) const {
    const int preset = qualityPreset_.load(std::memory_order_relaxed);
    switch (preset) {
    case 0: minW = maxW = 320;  minH = maxH = 240;  break;
    case 1: minW = maxW = 640;  minH = maxH = 480;  break;
    case 2: minW = maxW = 1280; minH = maxH = 720;  break;
    default: minW = maxW = 640; minH = maxH = 480;  break;
    }
}

} // namespace jamwide
