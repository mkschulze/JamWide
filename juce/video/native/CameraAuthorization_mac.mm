// Phase 19-01 Task 1 (stub) → Task 2 (full impl: real TCC pre-check via AVFoundation).
// This stub returns Denied unconditionally so any premature caller fails closed
// rather than leaking an unverified Authorized status.
#include "CameraAuthorization.h"

namespace jamwide {

CameraAuthStatus queryCameraAuthorization() {
    return CameraAuthStatus::Denied;
}

void requestCameraAuthorization(std::function<void(CameraAuthStatus)> callback) {
    if (callback) callback(CameraAuthStatus::Denied);
}

} // namespace jamwide
