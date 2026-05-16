#pragma once
// Phase 19-01 Task 1: Cross-platform camera authorization shim.
// Full implementation lives in CameraAuthorization_mac.mm (real TCC pre-check)
// and CameraAuthorization_windows.cpp (stub returning NotApplicable).
#include <functional>
#include <cstdint>

namespace jamwide {

// Auth status returned from the OS TCC layer. Maps directly to AVAuthorizationStatus
// on macOS; "NotApplicable" represents Windows (no TCC equivalent on desktop —
// errors are detected via openDeviceAsync + watchdog instead).
enum class CameraAuthStatus : int {
    NotDetermined = 0,
    Restricted    = 1,
    Denied        = 2,
    Authorized    = 3,
    NotApplicable = 4,   // Windows
};

// Synchronous pre-check. On macOS reads AVCaptureDevice::authorizationStatusForMediaType
// (does NOT prompt the user). On Windows returns NotApplicable.
CameraAuthStatus queryCameraAuthorization();

// Asynchronous request. On macOS may prompt the user via TCC. The completion handler
// fires on an unspecified thread per Apple's contract — callers must marshal to the
// JUCE message thread via MessageManager::callAsync if they touch UI/state. On
// Windows synchronously invokes the callback with NotApplicable.
void requestCameraAuthorization(std::function<void(CameraAuthStatus)> callback);

} // namespace jamwide
