#pragma once
// Phase 19-01 Task 1: Camera-fallback cause taxonomy. Own header so 19-03's
// CameraStatusDialog can include without pulling the full JamWideCameraDevice
// declaration (which depends on juce_video).
#include <cstdint>

namespace jamwide {

enum class CameraFallbackCause : int {
    None = 0,
    TCCDenied,             // macOS user denied; macOS Restricted maps here too
    HostLacksEntitlement,  // macOS plugin context, host has no NSCameraUsageDescription
    CameraInUse,           // openDeviceAsync error or watchdog fires after auth=Authorized
    NoHardware,            // getAvailableDevices() empty
    WindowsPrivacyBlock,   // Windows: openDeviceAsync nullptr with devices visible
};

} // namespace jamwide
