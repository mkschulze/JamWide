#pragma once
// Phase 19-01 Task 1 (stub) → Task 4 (full impl).
// Pure-C++ 6-state machine with input event enum + transition table.
// MEDIUM-3 mitigation: single dispatch entry point used by production AND tests.
// MEDIUM-2 mitigation: Paused state REMOVED (returns in Phase 22 for grid view).
#include "CameraFallbackCause.h"
#include <cstdint>
#include <optional>

namespace jamwide {

enum class CameraState : int {
    Idle = 0,
    Opening,
    Capturing,
    Failed,
    Retrying,
    Unavailable,
};

enum class CameraEvent : int {
    UserToggle,
    AuthGranted,
    AuthDenied,
    OpenSucceeded,
    OpenFailed,
    FirstFrameReceived,
    WatchdogFired,
    RuntimeError,
    RetryTick,
    RetryExhausted,
    RecheckPermission,
    Shutdown,
};

class CameraStateMachine;

} // namespace jamwide
