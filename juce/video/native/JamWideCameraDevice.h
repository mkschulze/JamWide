#pragma once
// Phase 19-01 Task 1 (stub) → Task 5 (full impl).
// CameraDevice owner + generation-token cancellation + retry-backoff + watchdog + cause-detection.
#include "CameraFallbackCause.h"
#include "CameraStateMachine.h"

namespace jamwide {

class JamWideFrameDistributor;

class JamWideCameraDevice {
public:
    class FallbackListener;
    JamWideCameraDevice(JamWideFrameDistributor& distributor, FallbackListener* listener = nullptr);
    ~JamWideCameraDevice();
    // Task 5 adds full API: toggle(), recheckPermission(), setQualityPreset(), getState(),
    // getDeviceName(), getPeakFps(), shutdown(), setFallbackListener().
};

} // namespace jamwide
