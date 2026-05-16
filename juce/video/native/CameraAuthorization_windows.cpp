// Phase 19-01 Task 2: Windows camera authorization stub.
//
// Windows TCC concept does not apply to desktop apps. There is no per-app
// per-device authorization API analogous to macOS AVCaptureDevice. Windows
// privacy settings ARE gated at the OS level ("Allow desktop apps to access
// your camera" in Settings → Privacy → Camera), but this state is not surfaced
// to user-space — apps simply fail to enumerate or open devices.
//
// Cause detection on Windows therefore happens reactively:
//   1. juce::CameraDevice::getAvailableDevices() empty → NoHardware.
//   2. openDeviceAsync returns nullptr with devices visible → WindowsPrivacyBlock.
//   3. Authorized + first-frame watchdog fires → CameraInUse (other app holds it).
//
// See RESEARCH §3 and JamWideCameraDevice::classifyOpenFailure (Task 5).
#include "CameraAuthorization.h"

namespace jamwide {

CameraAuthStatus queryCameraAuthorization() {
    return CameraAuthStatus::NotApplicable;
}

void requestCameraAuthorization(std::function<void(CameraAuthStatus)> callback) {
    if (callback) callback(CameraAuthStatus::NotApplicable);
}

} // namespace jamwide
