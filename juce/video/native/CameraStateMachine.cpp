// Phase 19-01 Task 4: CameraStateMachine.cpp — pure-C++ transition table.
#include "CameraStateMachine.h"

namespace jamwide {

namespace {

// Helper to take a hint atomically (consume it; subsequent reads see None).
CameraFallbackCause takeHint(CameraFallbackCause& hint) noexcept {
    CameraFallbackCause c = hint;
    hint = CameraFallbackCause::None;
    return c;
}

} // namespace

DispatchResult CameraStateMachine::dispatch(CameraEvent event) noexcept {
    DispatchResult r{};
    r.newState = state_;  // default: no-op stays in current state

    // Universal handler: Shutdown from any state collapses to Idle and tears
    // everything down. Done before the state-specific switch so we don't
    // duplicate the case in every arm.
    if (event == CameraEvent::Shutdown) {
        r.newState              = CameraState::Idle;
        r.closeHardware         = true;
        r.stopRetryWorker       = true;
        r.stopFirstFrameWatchdog = true;
        r.stopFrameStallWatchdog = true;
        state_ = r.newState;
        hint_  = CameraFallbackCause::None;
        return r;
    }

    switch (state_) {
    case CameraState::Idle:
        switch (event) {
        case CameraEvent::UserToggle:
            r.newState        = CameraState::Opening;
            r.startOpenDevice = true;
            break;
        default:
            break;  // no-op for all other events
        }
        break;

    case CameraState::Opening:
        switch (event) {
        case CameraEvent::AuthGranted:
            // Remain Opening; JamWideCameraDevice schedules openDeviceAsync
            // separately from its actuateDispatchResult path.
            break;
        case CameraEvent::AuthDenied:
            r.newState      = CameraState::Unavailable;
            r.emitFallback  = takeHint(hint_);
            r.closeHardware = true;
            break;
        case CameraEvent::OpenSucceeded:
            r.startFirstFrameWatchdog = true;
            // Stay Opening until FirstFrameReceived
            break;
        case CameraEvent::OpenFailed:
            r.newState               = CameraState::Unavailable;
            r.emitFallback           = takeHint(hint_);
            r.closeHardware          = true;
            r.stopFirstFrameWatchdog = true;
            break;
        case CameraEvent::FirstFrameReceived:
            r.newState               = CameraState::Capturing;
            r.stopFirstFrameWatchdog = true;
            r.startFrameStallWatchdog = true;
            break;
        case CameraEvent::WatchdogFired:
            // First-frame watchdog fired while still Opening — give up.
            r.newState               = CameraState::Unavailable;
            // If caller set a hint, emit it; otherwise default to CameraInUse
            // (per RESEARCH §5: "Authorized but no frames" is in-use).
            r.emitFallback           = (hint_ != CameraFallbackCause::None)
                ? std::optional<CameraFallbackCause>(takeHint(hint_))
                : std::optional<CameraFallbackCause>(CameraFallbackCause::CameraInUse);
            r.closeHardware          = true;
            r.stopFirstFrameWatchdog = true;
            break;
        default:
            break;
        }
        break;

    case CameraState::Capturing:
        switch (event) {
        case CameraEvent::UserToggle:
            r.newState               = CameraState::Idle;
            r.closeHardware          = true;
            r.stopFrameStallWatchdog = true;
            break;
        case CameraEvent::RuntimeError:
            r.newState               = CameraState::Retrying;
            r.closeHardware          = true;
            r.stopFrameStallWatchdog = true;
            r.startRetryWorker       = true;
            // RuntimeError may or may not carry a fallback cause; if set we
            // forward it so JamWideCameraDevice can surface it through D-23 logs.
            if (hint_ != CameraFallbackCause::None) {
                r.emitFallback = takeHint(hint_);
            }
            break;
        case CameraEvent::WatchdogFired:
            // HIGH-6 path — frame-stall watchdog while Capturing.
            r.newState               = CameraState::Retrying;
            r.closeHardware          = true;
            r.stopFrameStallWatchdog = true;
            r.startRetryWorker       = true;
            r.emitFallback           = (hint_ != CameraFallbackCause::None)
                ? std::optional<CameraFallbackCause>(takeHint(hint_))
                : std::optional<CameraFallbackCause>(CameraFallbackCause::CameraInUse);
            break;
        default:
            break;
        }
        break;

    case CameraState::Retrying:
        switch (event) {
        case CameraEvent::RetryTick:
            r.newState        = CameraState::Opening;
            r.startOpenDevice = true;
            break;
        case CameraEvent::OpenSucceeded:
            // Some impls may dispatch OpenSucceeded directly from Retrying
            // (e.g., the in-flight reopen completes before the state machine
            // observes RetryTick → Opening). Defensive coverage.
            r.newState                = CameraState::Opening;
            r.startFirstFrameWatchdog = true;
            break;
        case CameraEvent::OpenFailed:
            // Retry exhausted before MAX_ATTEMPTS? The retry worker fires
            // RetryExhausted after MAX_ATTEMPTS reaches; an explicit
            // OpenFailed here means the actuator side gave up. Treat as
            // terminal Unavailable.
            r.newState               = CameraState::Unavailable;
            r.emitFallback           = (hint_ != CameraFallbackCause::None)
                ? std::optional<CameraFallbackCause>(takeHint(hint_))
                : std::optional<CameraFallbackCause>(CameraFallbackCause::CameraInUse);
            r.stopRetryWorker        = true;
            r.closeHardware          = true;
            break;
        case CameraEvent::RetryExhausted:
            r.newState        = CameraState::Unavailable;
            r.stopRetryWorker = true;
            r.emitFallback    = (hint_ != CameraFallbackCause::None)
                ? std::optional<CameraFallbackCause>(takeHint(hint_))
                : std::optional<CameraFallbackCause>(CameraFallbackCause::CameraInUse);
            break;
        case CameraEvent::UserToggle:
            // User cancels a retry-in-progress.
            r.newState        = CameraState::Idle;
            r.closeHardware   = true;
            r.stopRetryWorker = true;
            break;
        default:
            // WatchdogFired during Retrying is a no-op (the watchdog should
            // be stopped before Retrying entry; defensive).
            break;
        }
        break;

    case CameraState::Failed:
        switch (event) {
        case CameraEvent::RetryTick:
            r.newState        = CameraState::Opening;
            r.startOpenDevice = true;
            break;
        case CameraEvent::UserToggle:
            r.newState      = CameraState::Idle;
            r.closeHardware = true;
            break;
        default:
            break;
        }
        break;

    case CameraState::Unavailable:
        switch (event) {
        case CameraEvent::RecheckPermission:
            r.newState        = CameraState::Opening;
            r.startOpenDevice = true;
            break;
        case CameraEvent::UserToggle:
            // From Unavailable, UserToggle behaves like a recheck-attempt
            // (D-12) — the user clicked the button again to retry.
            r.newState        = CameraState::Opening;
            r.startOpenDevice = true;
            break;
        default:
            break;
        }
        break;
    }

    state_ = r.newState;
    if (!r.emitFallback.has_value()) {
        // Any hint not consumed by an emit gets discarded so it doesn't leak
        // into a subsequent dispatch.
        hint_ = CameraFallbackCause::None;
    }
    return r;
}

} // namespace jamwide
