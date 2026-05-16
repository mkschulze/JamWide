#pragma once
// Phase 19-01 Task 4: pure-C++ CameraStateMachine.
//
// MEDIUM-2 mitigation: exactly 6 states — Idle, Opening, Capturing, Failed,
// Retrying, Unavailable. The "pause" state from RESEARCH §5 was REMOVED
// because per D-09 closing a popout does NOT pause capture (capture
// continues silently feeding the distributor with no UI consumer), and per
// D-29 audio-thread bandwidth-pause coordination is Phase 20's responsibility.
// A per-remote pause state will reappear in Phase 22 for the grid view.
//
// MEDIUM-3 mitigation: this class is the SINGLE source of truth for
// transitions. JamWideCameraDevice (Task 5) dispatches every event through
// stateMachine_.dispatch(...) and reacts to DispatchResult; tests call the
// same dispatch() directly. There are no test-only injection points and no
// duplicated transition logic.
//
// Threading: dispatch() is NOT thread-safe. The caller is responsible for
// serializing calls via the JUCE message thread (JamWideCameraDevice does
// this via MessageManager::callAsync from every async callback).
#include "CameraFallbackCause.h"

#include <cstdint>
#include <optional>

namespace jamwide {

enum class CameraState : int {
    Idle        = 0,
    Opening     = 1,
    Capturing   = 2,
    Failed      = 3,
    Retrying    = 4,
    Unavailable = 5,
};

enum class CameraEvent : int {
    UserToggle,           // user clicked Camera button
    AuthGranted,          // queryCameraAuthorization == Authorized OR requestAccess granted
    AuthDenied,           // Denied or Restricted
    OpenSucceeded,        // openDeviceAsync returned non-null
    OpenFailed,           // openDeviceAsync returned null + error string
    FirstFrameReceived,   // CameraDevice::Listener saw its first frame
    WatchdogFired,        // first-frame watchdog OR frame-stall watchdog (HIGH-6)
    RuntimeError,         // onErrorOccurred OR frame-stall while Capturing
    RetryTick,            // RetryWorker fires a reopen attempt
    RetryExhausted,       // 5 attempts gave up
    RecheckPermission,    // D-12 user click in Unavailable state
    Shutdown,             // processor destruction
};

// Side-effect descriptor returned from dispatch(). JamWideCameraDevice reads
// the boolean flags + emitFallback + newState and actuates them
// (start/stop timers, schedule openDeviceAsync, etc.). The state machine
// itself does NOT call into any platform code — pure data.
struct DispatchResult {
    CameraState newState;
    std::optional<CameraFallbackCause> emitFallback;
    bool startFirstFrameWatchdog = false;
    bool stopFirstFrameWatchdog  = false;
    bool startFrameStallWatchdog = false;
    bool stopFrameStallWatchdog  = false;
    bool startRetryWorker        = false;
    bool stopRetryWorker         = false;
    bool startOpenDevice         = false;
    bool closeHardware           = false;
};

class CameraStateMachine {
public:
    CameraStateMachine() = default;

    CameraState getState() const noexcept { return state_; }

    // Caller (JamWideCameraDevice) sets the fallback hint BEFORE dispatching
    // OpenFailed / WatchdogFired / RuntimeError / AuthDenied / RetryExhausted;
    // the state machine consults it to populate DispatchResult.emitFallback.
    // Reset to None after each dispatch returns.
    void setFallbackHint(CameraFallbackCause hint) noexcept { hint_ = hint; }

    // Compute the transition + side effects for `event` from the current
    // state. After this call, getState() reflects the new state and the
    // hint is cleared.
    DispatchResult dispatch(CameraEvent event) noexcept;

    // For tests / debug: retry-attempt counter exposed through a pure-C++
    // helper struct (no JUCE deps so tests can pull it).
    struct RetryBackoff {
        static constexpr int MAX_ATTEMPTS = 5;
        // 1000, 2000, 4000, 8000, 16000 ms
        static constexpr int delayMs(int attemptIdx) noexcept {
            return (1 << attemptIdx) * 1000;
        }
        // cumulativeMs(N) = sum of delayMs(0..N-1)
        static constexpr int cumulativeMs(int afterAttempt) noexcept {
            int sum = 0;
            for (int i = 0; i < afterAttempt && i < MAX_ATTEMPTS; ++i) {
                sum += delayMs(i);
            }
            return sum;
        }
        static constexpr bool isExhausted(int attemptIdx) noexcept {
            return attemptIdx >= MAX_ATTEMPTS;
        }
    };

private:
    CameraState state_ = CameraState::Idle;
    CameraFallbackCause hint_ = CameraFallbackCause::None;
};

} // namespace jamwide
