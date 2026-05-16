// Phase 19-01 Task 1 (final for Windows — Task 2 only refines the doc comment).
// Windows TCC concept does not apply to desktop apps; cause detection happens
// at openDeviceAsync error time + watchdog. See RESEARCH §3.
#include "CameraAuthorization.h"

namespace jamwide {

CameraAuthStatus queryCameraAuthorization() {
    return CameraAuthStatus::NotApplicable;
}

void requestCameraAuthorization(std::function<void(CameraAuthStatus)> callback) {
    if (callback) callback(CameraAuthStatus::NotApplicable);
}

} // namespace jamwide
