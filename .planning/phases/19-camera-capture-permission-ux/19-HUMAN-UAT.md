---
status: partial
phase: 19-camera-capture-permission-ux
source: [19-VERIFICATION.md]
started: 2026-05-16T19:00:00Z
updated: 2026-05-16T19:00:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. Cell 1 — macOS standalone happy path (CAM-01 / SC1)
expected: macOS TCC prompt appears; Allow yields preview popout within 3s with live frames
result: [pending]

### 2. Cell 2 — Logic Pro AU plugin happy path (CAM-01 / SC2)
expected: Logic Pro hosts JamWide AU; camera grant produces preview within 3s
result: [pending]

### 3. Cell 3 — REAPER macOS plugin fallback (CAM-02 / SC3)
expected: CameraStatusDialog shows HostLacksEntitlement (softened, MEDIUM-6 wording); 3-button set; Open System Settings opens privacy pane; no crash; audio still works
result: [pending]

### 4. Cell 4 — macOS arm64 standalone (CAM-01 arm64)
expected: Apple Silicon native build works identically to Cell 1
result: [pending]

### 5. Cell 5 — Permission revoke roundtrip (CAM-02 / SC4) — HIGH-6 end-to-end
expected: Mid-session toggling JamWide OFF in System Settings stops frames within ~5s; FrameStallWatchdog re-queries auth; CameraStatusDialog appears with TCCDenied cause (NOT CameraInUse); no crash
result: [pending]

### 6. Cell 6 — Notarization stapler validate (PKG-04 / D-28)
expected: Release build with JAMWIDE_HARDENED_RUNTIME=ON, codesigned with Team T3KK66Q67T, notarized, stapled, and verify_camera_entitlement.sh --notarize exits 0
result: [pending]

### 7. Cell 7 — Windows standalone happy path (CAM-01 / SC5)
expected: JamWide.exe on Windows x86_64 grants camera preview within 3s
result: [pending]

### 8. Cell 8 — Windows privacy block (CAM-02 Windows)
expected: CameraStatusDialog shows WindowsPrivacyBlock with softened copy and 3 buttons; ms-settings:privacy-webcam URL opens; recheck resumes capture
result: [pending]

### 9. Cell 9 — VDO.Ninja coexistence toast (D-27)
expected: Once-per-session non-blocking "Multiple video stacks active" toast appears when starting native camera while VDO.Ninja is active; toggling repeatedly does not re-show
result: [pending]

### 10. Cell 10 — First-launch privacy modal sequence (HIGH-5 end-to-end)
expected: tccutil reset + privacyAck delete; click -> TCC prompt with correct NSCameraUsageDescription -> Allow -> NativeCameraPrivacyDialog -> I understand -> preview; second launch skips both modals
result: [pending]

### 11. Risk C — JUCE commercial seat licence confirmation
expected: User confirms JUCE commercial seat covers juce_video for JamWideJuce target
result: [pending]

## Summary

total: 11
passed: 0
issues: 0
pending: 11
skipped: 0
blocked: 0

## Gaps
