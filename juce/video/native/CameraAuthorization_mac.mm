// Phase 19-01 Task 2: real macOS TCC pre-check via AVFoundation.
// Per RESEARCH §2 — does NOT prompt the user; reads cached authorization status
// from the OS. requestCameraAuthorization() is the prompting path; its completion
// handler fires on an unspecified thread per Apple's contract, so callers that
// touch UI/state must marshal to the JUCE message thread via
// MessageManager::callAsync.
#import <AVFoundation/AVFoundation.h>
#include "CameraAuthorization.h"

namespace jamwide {

CameraAuthStatus queryCameraAuthorization() {
    AVAuthorizationStatus s = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    switch (s) {
        case AVAuthorizationStatusNotDetermined: return CameraAuthStatus::NotDetermined;
        case AVAuthorizationStatusRestricted:    return CameraAuthStatus::Restricted;
        case AVAuthorizationStatusDenied:        return CameraAuthStatus::Denied;
        case AVAuthorizationStatusAuthorized:    return CameraAuthStatus::Authorized;
    }
    return CameraAuthStatus::Denied;  // unreachable; defensive default
}

void requestCameraAuthorization(std::function<void(CameraAuthStatus)> callback) {
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                             completionHandler:^(BOOL granted) {
        if (callback) callback(granted ? CameraAuthStatus::Authorized : CameraAuthStatus::Denied);
    }];
}

} // namespace jamwide
