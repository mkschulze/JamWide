# Phase 19: Camera Capture & Permission UX — Research

**Researched:** 2026-05-16
**Status:** Ready for planning
**Author:** gsd-phase-researcher
**Honors locked decisions:** D-01 through D-30 (`.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md`)

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Capture API (Area 1):**
- D-01: Use `juce::CameraDevice` (juce_video module). AGPLv3 (compatible with JamWide's GPLv2+ via "or any later version" clause).
- D-02: Frame distribution via a `JamWideFrameDistributor` with subscribers. UI subscriber forwards via `juce::MessageManager::callAsync`. Phase 20 encoder subscribes via SPSC ring. Phase 22 per-remote popouts will subscribe similarly.
- D-03: macOS TCC detection via `AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo` pre-check in an Objective-C++ bridge, called BEFORE `juce::CameraDevice::openDeviceAsync`. Closes Spike Risk #2.
- D-04: Frame-format conversion lives in each subscriber. Distributor publishes `juce::Image` verbatim. UI renders directly; Phase 20 encoder owns its BGRA→YUV420P conversion.

**Preview Placement (Area 2):**
- D-05: Floating `juce::DocumentWindow` popout only in Phase 19. No in-plugin tile.
- D-06: New "Camera" button in `ConnectionBar`, next to existing "Video" button (VDO.Ninja stays operational during parallel beta).
- D-07: Popout resizable, 4:3 aspect-locked, position persists. Default 320×240.
- D-08: Popout chrome uses existing `JamWideLookAndFeel` (VB-style dark theme).

**Lifecycle (Area 3):**
- D-09: Orthogonal camera-state vs popout-visibility. Closing popout hides preview; capture continues.
- D-10: Camera always starts OFF on launch.
- D-11: Camera state fully independent of NINJAM Connect/Disconnect.
- D-12: After denial, Camera button label changes to "Recheck permission" and stays clickable.

**Permission-Denial Fallback (Area 4) — DAW-Agnostic:**
- D-13: Single non-blocking `juce::AlertWindow` dialog; cause-aware copy (TCC denied / host lacks entitlement / camera in use / no hardware / Windows privacy block). DAW-agnostic.
- D-14: Dialog suppressed after first show until detected cause changes.
- D-15: Platform-aware deep-link button (macOS: `x-apple.systempreferences:com.apple.preference.security?Privacy_Camera` via NSWorkspace; Windows: `juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser()`).
- D-16: Copy-only standalone-as-fallback suggestion (no "Launch Standalone" button).

**Camera Device Selection (Area 5):**
- D-17: Auto-pick `deviceIndex=0`, name in popout titlebar.

**Capture Parameters (Area 6):**
- D-18: Three presets — Low (320×240@10fps) / Medium (640×480@15fps) / High (1280×720@30fps). Default = Medium.
- D-19: Preset selection via Camera button right-click → "Quality ▶ Low/Medium/High" submenu (`juce::PopupMenu`).

**Frame Error Handling (Area 7):**
- D-20: On mid-session camera loss, pause + exponential-backoff retry (1s/2s/4s/8s/16s, give up at 30s). Separate worker thread (NOT camera callback, NOT audio, NOT message). After 30s of failures, transition to Area 4 fallback dialog.

**Test Strategy (Area 8):**
- D-21 (test strategy): Layered — unit tests for logic, manual UAT for permission flows.

**Privacy Notice (Area 9):**
- D-22: New first-use modal for native-camera broadcast acknowledgment. Distinct from existing `VideoPrivacyDialog`.

**Logging (Area 10):**
- D-23: Camera events via `juce::Logger::writeToLog`. Same pattern as OSC and MIDI components.

**Plugin State Persistence (Area 11):**
- D-24: Bump plugin state schema 3 → 4. `loadState` handles missing v3→v4 fields gracefully.
- D-25: New `<camera>` ValueTree subtree with attributes: `popoutX`, `popoutY`, `popoutWidth`, `popoutHeight`, `qualityPreset` (low/medium/high; default medium), `privacyAck` (bool; default false), `selectedDevice` (string; default empty = auto-pick).

**Documentation (Area 12):**
- D-26: Tooltip on Camera button + CHANGELOG.md entry. Full `docs/CAMERA.md` is Phase 24.

**VDO.Ninja Coexistence (Area 13):**
- D-27: Soft warning toast when both native + VDO.Ninja active, no hard block.

**macOS Notarization (Area 14):**
- D-28: Phase 19 includes notarization verification task. Adds `com.apple.security.device.camera` + `NSCameraUsageDescription`, builds + codesigns + notarizes test bundle, verifies via `codesign --verify --deep --strict` + `xcrun stapler validate`.

**Audio-Thread Coordination Contract (Area 15):**
- D-29: Phase 20 owns ALL audio-thread integration for camera. Phase 19 ships ZERO audio-path code.

**Onboarding (Area 16):**
- D-30: No onboarding tour — tooltip + CHANGELOG.md entry is sufficient.

### Claude's Discretion

- Choice of sync vs async `juce::CameraDevice::openDeviceAsync` (planner picks; async is the modern API)
- Exact pixel sizes for the popout default vs minimum vs maximum bounds (planner picks; min ~240×180 makes sense)
- Specific copy strings for the fallback dialog beyond the cause-aware skeleton (planner picks; English-only for v1.3 beta — i18n is a v2+ concern)
- The retry-thread implementation (separate `juce::Thread` vs `juce::TimedCallback` chain — planner picks based on what fits the codebase best)
- File layout for camera code: likely a new `juce/video/native/` subdirectory with `JamWideCameraDevice.h/.cpp`, `JamWideFrameDistributor.h/.cpp`, `CameraPreviewWindow.h/.cpp`, `CameraAuthorization_mac.mm`, `CameraAuthorization_windows.cpp`, `CameraStatusDialog.h/.cpp` (planner refines)
- Whether to expose a `GetCameraPeakFrameRate()` accessor for debugging during beta UAT (planner picks)

### Deferred Ideas (OUT OF SCOPE)

- Multi-camera dropdown UI — deferred until beta testers request it
- VST3/AU/CLAP host-specific camera-entitlement matrix — Phase 24 BETA-02/03
- Onboarding tour / coachmark — rejected for Phase 19 (D-30)
- Camera frame rate / bandwidth telemetry — Phase 24 polish
- Privacy notice translations — v2+ concern
- `docs/CAMERA.md` user guide — Phase 24
- Audio-thread `camera_active` flag — Phase 20 (D-29)
- "Auto" preset adapting to network — Phase 20 encoder
- Cross-platform launch-Standalone button (Area 4) — copy-only per D-16
- Linux camera capture — out of v1.3 entirely (no `juce_CameraDevice_linux.h`)
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CAM-01 | User can grant the JamWide standalone application and DAW-hosted plugin (VST3/AU, CLAP best-effort) access to their webcam via the OS permission prompt | §2 macOS TCC pre-check, §3 Windows backend, §7 entitlements/Info.plist |
| CAM-02 | User sees a graceful "Camera unavailable" fallback in the UI (no crash, audio still works) when the DAW host (e.g., REAPER, Live, Bitwig) does not request `com.apple.security.device.camera` for itself | §9 cause-detection matrix, §5 state machine, §2 watchdog |
| CAM-03 | User sees their own local camera preview rendered in the plugin/standalone UI whenever camera access has been granted | §1 JUCE CameraDevice API, §4 frame distributor, §6 CameraPreviewWindow |
| PKG-04 (entitlements portion only) | `JamWide.entitlements` declares `com.apple.security.device.camera` and `NSCameraUsageDescription` is in the bundle's Info.plist | §7 entitlements + Info.plist plumbing (codesign + frameworks-path portions deferred to Phase 23) |
</phase_requirements>

---

## Summary

Phase 19 has a clean architectural shape because the v1.3 spike already proved the end-to-end LGPL ffmpeg + openh264 + `juce::CameraDevice` stack composes (spike GREEN-with-caveats, 320×240@10fps, 4% CPU, 98 kbps; spike artifacts in `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/`). The phase boundary is also clean: this phase ships ZERO audio-path code (D-29) and ZERO encode/decode code (D-04 punts BGRA→YUV420P to Phase 20). The pure deliverable is camera capture, frame fan-out, preview UI, permission UX, plugin-state persistence, and macOS entitlement plumbing.

Three risks dominate the planning:

1. **Spike Risk #2 — `juce::CameraDevice::openDevice` returns non-null even when TCC denies frames on macOS.** CONTEXT D-03 locks an `AVCaptureDevice authorizationStatusForMediaType` Objective-C++ pre-check as the resolution. Research below specifies the file layout, function signatures, and CMake wiring. We also recommend a watchdog timer (belt-and-braces) for the case where TCC says "authorized" but frames don't arrive within N seconds (e.g., camera physically disconnected after TCC grant but before openDevice).

2. **Plugin Info.plist is NOT the host's Info.plist.** macOS TCC reads the usage description from the *host process* bundle, not the loaded plugin bundle. JUCE generates an `NSCameraUsageDescription` for the plugin Info.plist via `CAMERA_PERMISSION_ENABLED TRUE`, but that string is ineffective when the plugin loads inside a DAW that lacks its own camera entitlement (Logic Pro has it; REAPER, Live, Bitwig don't — SPARTA Issue #82). The locked solution is the DAW-agnostic fallback dialog (D-13), which our research maps to a concrete cause-detection state machine.

3. **`juce::CameraDevice::Listener::imageReceived` fires on "any thread."** This is verbatim from `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:202-206`. D-02's `JamWideFrameDistributor` is the contract enforcer: producer is camera-callback thread, UI consumer marshals via `MessageManager::callAsync`, Phase 20's encoder will subscribe via SPSC ring (consistent with Phase 15.1 RT-safety architecture).

**Primary recommendation:** the planner should treat the file layout in §6 as the canonical decomposition. A natural 3-plan split is (a) capture pipeline + state machine + frame distributor + macOS TCC pre-check, (b) UI surface (popout window, ConnectionBar Camera button, right-click preset menu, plugin-state persistence v3→v4), and (c) entitlements + Info.plist + cause-aware fallback dialog + Windows privacy block + VDO.Ninja coexistence toast + notarization verification. The planner has discretion on the actual split.

---

## 1. JUCE CameraDevice API Deep-Dive

### Open/close lifecycle

| API | When to use | Citation |
|-----|-------------|----------|
| `static CameraDevice* openDevice(int deviceIndex, int minW, int minH, int maxW, int maxH, bool highQuality = true)` | Synchronous; "should not be used on iOS or Android" | `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:64-81` |
| `static void openDeviceAsync(int deviceIndex, OpenCameraResultCallback resultCallback, int minW, int minH, int maxW, int maxH, bool highQuality = true)` | Asynchronous; "preferred method ... works on all platforms" | `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:109-113` |

**Recommendation: use `openDeviceAsync`.** Rationale:
1. JUCE's own docs label it "the preferred method".
2. Avoids blocking the message thread while macOS prompts for camera permission. On a first launch where the OS shows the TCC modal, the synchronous call would freeze the editor until the user clicks Allow/Deny.
3. The `OpenCameraResultCallback` shape (`std::function<void(CameraDevice*, const String& /*error*/)>`) lets us hand a non-empty error string through to the fallback dialog cause-detection (§9) — the synchronous path returns `nullptr` with no diagnostic.

The spike used `openDevice` synchronously and confirmed it returns a non-null `unique_ptr` on macOS x86_64 standalone (`spike-results.md:85`). The async path provides the same surface for Phase 19 with strictly better error reporting.

[CITED: `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:64-113`]

### `Listener::imageReceived` threading contract

> "This may be called by any thread, so be careful about thread-safety, and make sure that you process the data as quickly as possible to avoid glitching!"

[CITED: `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:200-204`]

This is the *enabling* finding for D-02. The frame distributor MUST treat its `imageReceived` callback as untrusted-thread and route into thread-safe state. On macOS in JUCE's `PostCatalinaPhotoOutput` path (`juce_CameraDevice_mac.h:286-295`), the delivery happens via `AVCapturePhotoOutput.capturePhotoWithSettings:delegate:` whose delegate method runs on AVFoundation's internal queue — NOT the JUCE message thread. On Windows the delegate is `ISampleGrabberCB::BufferCB` which runs on DirectShow's filter-graph worker thread (`juce_CameraDevice_windows.h:39-41`).

### Device enumeration and naming

- `static StringArray CameraDevice::getAvailableDevices()` — list of device names. [`juce_CameraDevice.h:62`]
- `const String& getName() const noexcept` — instance accessor for the device's display name. [`juce_CameraDevice.h:117`]

For D-17 (auto-pick deviceIndex=0, name in titlebar):

```cpp
auto names = juce::CameraDevice::getAvailableDevices();
if (names.isEmpty()) { /* trigger "no hardware" fallback path */ }
juce::CameraDevice::openDeviceAsync(0, [self](juce::CameraDevice* dev, const juce::String& err) {
    if (dev != nullptr) {
        const juce::String label = "JamWide — Camera: " + dev->getName();
        previewWindow.setName(label);  // titlebar
    } else { /* err contains a non-empty string — feed to cause-detection */ }
}, minW, minH, maxW, maxH);
```

### Image format on macOS vs Windows

| Platform | JUCE backend | Image::PixelFormat delivered |
|----------|-------------|------------------------------|
| macOS (Catalina+) | `AVCapturePhotoOutput` → `[photo fileDataRepresentation]` → `ImageFileFormat::loadFrom(...)` | Decoded from JPEG container; ends up as `juce::Image::ARGB` after JUCE's ImageFileFormat path |
| Windows | `ISampleGrabber` with `MEDIASUBTYPE_RGB24` configured | `juce::Image::RGB` (24-bit packed BGR; see `juce_CameraDevice_windows.h:316`) |

[CITED: `libs/juce/modules/juce_video/native/juce_CameraDevice_mac.h:316-319`, `libs/juce/modules/juce_video/native/juce_CameraDevice_windows.h:316`]

**Action for D-04 / Phase 20 encoder contract:** the distributor publishes `juce::Image` verbatim and subscribers handle format quirks themselves. The UI (preview tile) calls `Graphics::drawImage` which renders any JUCE image format natively, so the preview path is one-line. Phase 20's encoder will need libswscale anyway (BGRA→YUV420P), and the input pixel format input parameter (`AV_PIX_FMT_BGRA` on macOS-Catalina-photo vs `AV_PIX_FMT_BGR24` on Windows-DirectShow) is something the encoder will discover via `juce::Image::BitmapData::pixelFormat` at conversion time. The Phase 19 distributor stays format-agnostic.

**Important macOS finding (verified by reading the backend):** the macOS backend uses `AVCapturePhotoOutput` ("still picture") with a re-trigger loop, not continuous `AVCaptureVideoDataOutput`. `triggerImageCapture()` is called on the start of capture and re-invoked from `handleImageCapture` after each delivery (`juce_CameraDevice_mac.h:530-547`). This explains the spike's "JUCE console app gets ZERO frames within 2 seconds" finding — without `NSCameraUsageDescription` the first `triggerImageCapture` silently fails and no re-trigger happens. The watchdog timer (§2) is necessary because this failure mode is observable as "frame interval gap" rather than a discrete error event.

### Frame-rate ceiling

`CameraDevice::openDevice` has no framerate parameter. The macOS backend re-triggers after each delivery — effective FPS is bounded by the still-photo capture cost (much slower than `AVCaptureVideoDataOutput` would be). The spike measured ~10 fps at 320×240 (`spike-results.md:71`). For D-18's Medium (640×480@15fps) and High (1280×720@30fps) presets, Phase 19 should set the min/max size constraints on `openDeviceAsync` to request the preset's resolution from JUCE's backend, but real-world frame delivery will be backend-bound. Recommend planning to (a) measure actual FPS via the optional `GetCameraPeakFrameRate()` accessor (Claude's Discretion item — see §12), (b) treat the preset's "fps" number as a *target ceiling*, not a guarantee.

---

## 2. macOS TCC Pre-Check (Closes Spike Risk #2)

### Why `juce::CameraDevice::openDevice` returns non-null when TCC denies

The spike documented this verbatim:

> `juce::CameraDevice::openDevice(0, 320, 240, 320, 240, false)` **succeeded** (returned a non-null `unique_ptr`), proving JUCE's camera vendoring works in this build. **However, the camera produced ZERO frames within 2 seconds.** Root cause: JUCE's macOS camera implementation uses `AVCapturePhotoOutput.triggerImageCapture()` in a feedback loop. `triggerImageCapture` requires the host process to have `NSCameraUsageDescription` in its Info.plist for the OS TCC subsystem to grant access. ... macOS TCC silently denies camera access without a usage-description plist key (no permission prompt, no error log — the AVCaptureSession just never delivers frames).

[CITED: `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-spike-results.md:84-94`]

This is a JUCE API gap — there is no `CameraDevice::onErrorOccurred` callback for "session opened but TCC blocks all frames." The async-open callback returns a valid `CameraDevice*` and an empty error string.

### Objective-C++ pre-check (D-03)

The Apple-supported approach is to call `AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo` synchronously, and `requestAccessForMediaType:completionHandler:` to trigger the OS prompt when status is `notDetermined`.

**The four enum values** (`AVAuthorizationStatus`):
- `AVAuthorizationStatusNotDetermined` (0) — user has not yet been asked
- `AVAuthorizationStatusRestricted` (1) — parental controls / MDM blocks the choice
- `AVAuthorizationStatusDenied` (2) — user actively said No (or no `NSCameraUsageDescription` in host plist on macOS)
- `AVAuthorizationStatusAuthorized` (3) — granted

[CITED: https://developer.apple.com/documentation/avfoundation/avcapturedevice/1624613-authorizationstatusformediatype, https://developer.apple.com/documentation/avfoundation/avcapturedevice/1624584-requestaccessformediatype]

### Minimal `.mm` interface

Recommended file: `juce/video/native/CameraAuthorization_mac.mm` (mirroring the existing `juce/video/BrowserDetect_mac.mm` pattern in this codebase). Recommended header `juce/video/native/CameraAuthorization.h` (cross-platform C++).

```cpp
// CameraAuthorization.h  (cross-platform interface)
namespace jamwide {

enum class CameraAuthStatus : int {
    NotDetermined = 0,
    Restricted    = 1,
    Denied        = 2,
    Authorized    = 3,
    NotApplicable = 4,  // Windows: TCC concept doesn't apply at this layer
};

// Synchronous status query. macOS: AVCaptureDevice authorizationStatusForMediaType.
// Windows: returns NotApplicable (Windows uses error code at openDevice time, not pre-check).
CameraAuthStatus queryCameraAuthorization();

// Asynchronously request access. macOS: calls requestAccessForMediaType.
// Windows: synchronously calls callback with NotApplicable.
// The callback is invoked on an unspecified thread; marshal to MessageManager if needed.
void requestCameraAuthorization(std::function<void(CameraAuthStatus)> callback);

} // namespace jamwide
```

**macOS `.mm` implementation skeleton:**

```objc
// CameraAuthorization_mac.mm
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
        callback(granted ? CameraAuthStatus::Authorized : CameraAuthStatus::Denied);
    }];
}

} // namespace jamwide
```

**Windows stub (`CameraAuthorization_windows.cpp`):**

```cpp
#include "CameraAuthorization.h"
namespace jamwide {
CameraAuthStatus queryCameraAuthorization() { return CameraAuthStatus::NotApplicable; }
void requestCameraAuthorization(std::function<void(CameraAuthStatus)> cb) {
    cb(CameraAuthStatus::NotApplicable);
}
}
```

### CMake wiring

Mirror the existing `BrowserDetect_mac.mm`/`BrowserDetect_win.cpp` pattern (`CMakeLists.txt:200-205`):

```cmake
if(APPLE)
    target_sources(JamWideJuce PRIVATE juce/video/native/CameraAuthorization_mac.mm)
else()
    target_sources(JamWideJuce PRIVATE juce/video/native/CameraAuthorization_windows.cpp)
endif()
```

The `.mm` file gets compiled as Objective-C++ by virtue of the extension; no extra `set_source_files_properties` call is needed because JUCE's CMake already configures Apple `.mm` handling for the bundled project.

### `notDetermined` flow

```cpp
auto status = jamwide::queryCameraAuthorization();
if (status == jamwide::CameraAuthStatus::NotDetermined) {
    jamwide::requestCameraAuthorization([self](jamwide::CameraAuthStatus result) {
        juce::MessageManager::callAsync([self, result]() {
            if (result == jamwide::CameraAuthStatus::Authorized) {
                self->openCamera();  // proceeds to juce::CameraDevice::openDeviceAsync
            } else {
                self->triggerFallback(jamwide::CameraFallbackCause::TCCDenied);
            }
        });
    });
    return;
}
if (status == jamwide::CameraAuthStatus::Authorized) { self->openCamera(); return; }
// Restricted or Denied — go straight to fallback dialog with appropriate cause
self->triggerFallback(self->classifyMacOSDenialCause(status));
```

### Watchdog timer recommendation

Even after a clean `Authorized` pre-check, the camera can fail to deliver frames for non-TCC reasons (camera physically disconnected after status query, hardware locked by Zoom, driver hang). Recommendation: after `openDeviceAsync` callback fires with a valid `CameraDevice*`, start a `juce::Timer` that fires once after a watchdog interval (suggest 3 seconds — matches the success criterion 1 "within 3 seconds" wording in the roadmap). If no frame has been received by the watchdog tick, treat as "camera-in-use or hardware failure" and trigger the fallback path with `cause = CameraInUse` (best-guess) or a generic `cause = NoFrames`. The watchdog cancels itself on first frame received via the distributor.

This is belt-and-braces with the TCC pre-check: pre-check catches the TCC-denied case before openDevice is even called; watchdog catches everything else.

[VERIFIED via direct inspection: `libs/juce/modules/juce_video/capture/juce_CameraDevice.h`, `libs/juce/modules/juce_video/native/juce_CameraDevice_mac.h`]

---

## 3. Windows Camera Backend

### Which JUCE backend

**DirectShow.** JUCE 8's Windows backend is `juce_CameraDevice_windows.h` and it uses DirectShow primitives: `CLSID_FilterGraph`, `ICaptureGraphBuilder2`, `IBaseFilter`, `IGraphBuilder`, `ISampleGrabber`, `CLSID_NullRenderer`, `CLSID_SampleGrabber`. No Media Foundation code path exists in the file. [VERIFIED by reading `libs/juce/modules/juce_video/native/juce_CameraDevice_windows.h:35-200`]

This contradicts the canonical CONTEXT line ("native backend: Windows uses `juce_CameraDevice_windows.h` (Media Foundation / DirectShow)") which is technically imprecise — only DirectShow is used. Both APIs ride on top of the same Windows camera privacy system at the OS level, so the user-facing privacy behaviour is identical regardless of which API is used.

### Windows privacy-block detection (D-15)

Modern Windows (10 / 11) ships a system-wide camera privacy switch (`Settings → Privacy & Security → Camera`). When the user toggles it off (or it's off by default for a non-Store app):

- DirectShow's filter-graph initialization will succeed at the `IFilterGraph::AddFilter` step but the device enumeration returns an empty list, OR
- For some Windows versions, the device shows up in enumeration but `IMediaControl::Run` returns `E_ACCESSDENIED` (HRESULT `0x80070005`).

[CITED: https://learn.microsoft.com/en-us/windows/apps/develop/camera/camera-privacy-setting — "The Windows camera capture APIs will return the error E_ACCESSDENIED when apps attempt to access the camera capture device if the user has disabled camera in the camera privacy Settings page."]

JUCE's Windows backend doesn't surface `E_ACCESSDENIED` in a typed way; it just returns nullptr from `CameraDevice::openDevice`. The async path's error string MAY be populated by the JUCE implementation when the filter graph fails, but the JUCE Windows backend doesn't currently set `openingError` on a per-error-code basis. The most robust Windows fallback detection is:

1. `getAvailableDevices().isEmpty()` → `NoHardware` (or possibly `WindowsPrivacyBlock` — indistinguishable at this layer)
2. `openDeviceAsync` callback returns `nullptr` with non-empty error → `WindowsPrivacyBlock` (most-likely guess given grant flow assumptions)
3. `openDeviceAsync` returns valid device but watchdog fires with zero frames → `CameraInUse` (another app holds the device)

There is NO Windows analogue of macOS's `AVCaptureDevice authorizationStatusForMediaType` pre-check that's accessible from a non-UWP (Win32 desktop) app. The `AppCapability` API exists in WinRT but requires a packaged AppContainer (`.msix` / Store) — JamWide is a desktop binary, so `AppCapability` is not available at this layer.

[CITED: https://learn.microsoft.com/en-us/windows/apps/develop/camera/camera-privacy-setting]

### `ms-settings:privacy-webcam` deep-link verification (D-15)

`ms-settings:privacy-webcam` is the official Windows 10/11 deep-link to the Camera privacy settings page. Invoked via `juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser()` — JUCE will hand the URI to ShellExecute which the Windows shell recognises as a Settings URI. [CITED: https://learn.microsoft.com/en-us/windows/apps/develop/camera/camera-privacy-setting recommends `ShellExecute` of `"ms-settings:privacy-webcam"`; `juce::URL::launchInDefaultBrowser` calls `ShellExecute` internally on Windows.]

### Cross-platform parity issues for the planner

| Concern | macOS | Windows | Implication |
|---------|-------|---------|-------------|
| Auth pre-check | Available via `AVCaptureDevice authorizationStatusForMediaType` | Not available for desktop apps | Windows path skips pre-check; relies on `openDeviceAsync` error + watchdog |
| Permission prompt UX | OS-native modal triggered by `requestAccessForMediaType` | No app-triggered prompt; user must enable in Settings | Windows "denied" path goes straight to fallback dialog with deep-link |
| Image format delivered | ARGB (from JPEG decode via `ImageFileFormat::loadFrom`) | RGB24 (DirectShow `MEDIASUBTYPE_RGB24` packed BGR) | D-04 holds — distributor stays format-agnostic |
| Error codes | Untyped — `openingError` populated as String | `E_ACCESSDENIED` semantically present but not exposed by JUCE | Cause-detection (§9) relies on error-string heuristics + watchdog |
| Capture backend | `AVCapturePhotoOutput` (still-photo loop) | DirectShow `ISampleGrabber` (continuous) | Windows likely delivers frames faster than macOS for same resolution |
| Device names | Localized, e.g. "FaceTime HD Camera" | Plain, e.g. "Integrated Webcam" | Both fine for titlebar (D-17) |
| Privacy-Settings deep-link | `x-apple.systempreferences:com.apple.preference.security?Privacy_Camera` via NSWorkspace | `ms-settings:privacy-webcam` via `juce::URL::launchInDefaultBrowser` | Two-branch implementation in the dialog |

---

## 4. Frame Distributor Architecture (D-02, D-04)

### Header sketch for `JamWideFrameDistributor`

```cpp
// juce/video/native/JamWideFrameDistributor.h
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include <mutex>

namespace jamwide {

class JamWideFrameDistributor {
public:
    /// Subscriber interface. `onFrame` may be called on the camera-callback thread
    /// (which JUCE documents as "any thread") — subscribers MUST be thread-safe.
    /// Subscribers MUST NOT block; the camera-callback thread should not stall.
    class Subscriber {
    public:
        virtual ~Subscriber() = default;
        virtual void onFrame(const juce::Image& image) = 0;
    };

    void registerSubscriber(Subscriber* s);
    void unregisterSubscriber(Subscriber* s);

    /// Called by JamWideCameraDevice's `Listener::imageReceived` from the
    /// JUCE camera-callback thread. Forwards to all registered subscribers
    /// under a brief shared lock; subscribers are responsible for their own
    /// thread-marshalling and copy strategy.
    void publish(const juce::Image& image);

    /// Optional: peak FPS observed since last reset (for §12 debug accessor).
    /// Atomic so safe to read from message thread.
    std::atomic<float> peakFps_{0.0f};

private:
    /// Removal-safe iteration: copy-on-write list of subscribers behind a mutex.
    /// Mutex is brief; no subscriber callback is invoked while holding it.
    mutable std::mutex mu_;
    std::vector<Subscriber*> subscribers_;
};

} // namespace jamwide
```

### Thread-safety analysis

Three threads touch the distributor in steady state:

| Actor | Operation | Thread |
|-------|-----------|--------|
| Producer | `publish(image)` | Camera-callback thread (JUCE's `imageReceived`, "any thread") |
| UI consumer | `onFrame(image)` → marshal to message thread via `MessageManager::callAsync` | Camera-callback thread → message thread |
| Future Phase 20 encoder | `onFrame(image)` → SPSC `try_push` | Camera-callback thread → encoder worker thread (via SPSC) |

**Locking discipline:**
- `mu_` is held during the brief copy of the subscriber list inside `publish()` (or equivalently inside `publish()` we iterate a copy taken under a shared mutex; or we use `juce::ListenerList::call` which has a documented copy-on-iterate safety property).
- Subscriber callbacks run *outside* the mutex to avoid the subscriber's marshalling blocking the producer.
- Subscriber `registerSubscriber`/`unregisterSubscriber` happens from message thread (UI registration on editor construction; Phase 20 encoder registers from its worker on activation). Both writes hold `mu_`.

**Alternative implementation: `juce::ListenerList<Subscriber>`** — JUCE's `ListenerList` already provides removal-safe iteration via its `BailOutChecker` pattern (`juce_ListenerList.h`). Using it is one less concurrency primitive to maintain. The planner should choose `juce::ListenerList` unless a stricter contract is needed (e.g., if subscribers need to attach metadata like priority, in which case the bespoke vector is fine).

### Subscribers own format conversion (D-04)

- **UI subscriber** (Phase 19): receives `juce::Image`, copies a reference (JUCE's `Image` is reference-counted internally — `Image image2 = image;` is cheap), schedules `MessageManager::callAsync([image]() { previewTile.setImage(image); previewTile.repaint(); });`. No conversion.
- **Phase 20 encoder subscriber**: receives `juce::Image`, copies into an SPSC ring entry containing the `juce::Image` (or a raw byte buffer view), and the encoder worker thread does `sws_scale` BGRA→YUV420P inside its own thread.
- **Phase 22 per-remote popouts**: not relevant to Phase 19; ignore.

**No wasted conversion** — exactly what D-04 prescribes.

### Lifetime / "no dangling subscriber" rule

- Distributor is owned by `JamWideJuceProcessor` (constructed alongside `JamWideCameraDevice`).
- UI subscriber (the preview-tile component inside `CameraPreviewWindow`) registers in its constructor and unregisters in its destructor.
- The processor must outlive the editor (always true in JUCE plugins).
- Phase 20 encoder subscriber registers when the camera transitions to capturing-and-broadcasting and unregisters when broadcast stops.

**Documented rule for the planner to enforce:** every subscriber MUST be unregistered before being destroyed. Violation = use-after-free on the camera-callback thread. Plan should add an `assert(subscribers_.empty())` in the distributor destructor to catch leaks at debug time.

---

## 5. State Machine & Retry-Backoff (D-09, D-12, D-20)

### States

| State | Meaning | Entry transitions | Exit transitions |
|-------|---------|------------------|------------------|
| `Idle` | Camera off (D-10 startup) | startup, user-clicked-stop, fallback-acknowledged | →`Opening` on user click |
| `Opening` | TCC pre-check + `openDeviceAsync` in flight | user click from Idle, Retrying success-attempt | →`Capturing` on first frame, →`Failed` on err, →`Unavailable` on TCC denial |
| `Capturing` | Frames flowing to distributor | first frame arrives in Opening | →`Paused` (popout closed but camera-state ON per D-09 keeps capturing), →`Failed` on runtime error, →`Idle` on user click |
| `Paused` | Camera open, frames discarded (popout hidden) | popout X clicked while Capturing | →`Capturing` on popout reopened, →`Idle` on user click camera-off |
| `Failed` | Mid-session loss detected | runtime error during Capturing/Paused | →`Retrying` immediately |
| `Retrying` | Exponential backoff worker scheduled | from `Failed` | →`Opening` on backoff tick, →`Unavailable` after 30s budget exhausted |
| `Unavailable` | TCC denied OR retry-give-up | from Opening (TCC denied), Retrying (timeout 30s) | →`Opening` on user clicks "Recheck permission" (D-12) |

### Transition table

Implementable as a `std::atomic<CameraState>` on the processor, with all transitions marshalled through `MessageManager::callAsync` to serialize them on the message thread. The audio thread never reads or writes this — per D-29, audio-thread integration is Phase 20's territory.

### Trigger sources

| Trigger | State machine input | Owner |
|---------|--------------------|-------|
| User clicks Camera button (Idle→Opening) | UI click | message thread |
| User clicks Camera button (Capturing→Idle) | UI click | message thread |
| First frame arrives | distributor's first publish | message thread (via callAsync) |
| `openDeviceAsync` callback fires with error | OS / JUCE backend | unspecified thread (marshal to message) |
| `CameraDevice::onErrorOccurred` fires | JUCE backend | unspecified thread (marshal) |
| Watchdog (§2) fires without first frame | `juce::Timer` | message thread |
| OS sleep/wake (camera disconnected then reconnected) | observed via `onErrorOccurred` (or via macOS `AVCaptureSessionRuntimeError` fed back through JUCE) | unspecified thread |
| Camera unplug | `onErrorOccurred` | unspecified thread |
| Host steals camera (another app opens it) | macOS: `AVCaptureSessionInterruptionNotification` (not exposed by JUCE); on JUCE end, `onErrorOccurred` or simply zero-frame stall | unspecified thread |
| Retry backoff tick | retry worker | retry worker thread (marshal back) |

### Retry policy: 1s/2s/4s/8s/16s, give-up at 30s

Cumulative: 1+2+4+8+16 = 31s. The "give-up-at-30s" in D-20 effectively means "fire the 16s retry then bail at the 30s mark" — the 16s retry attempts to fire at the 15s elapsed mark and if it fails the cumulative is 31s elapsed when we give up. Plan to fire `Retrying` budget as "5 attempts max with backoff" and add a budget guard that the total elapsed at give-up is reported via `juce::Logger::writeToLog`.

### Which thread runs the retry timer

**Recommendation: `juce::Thread` subclass.** Rationale:
- Plan a dedicated worker that owns the backoff loop without competing for the message thread's runloop.
- Matches D-20's explicit "NOT the camera callback thread, NOT the audio thread, NOT the message thread" constraint.
- Allows clean `signalThreadShouldExit()` on phase-19 component destruction.
- The retry worker doesn't compete with audio (Phase 15.1 RT-safety stays intact — audio thread is untouched).

**Alternative considered: `juce::TimedCallback` (or a chain of `Timer::startTimer(ms)`)** — would run on the message thread. Tempting because it's simpler, but a chain of message-thread timers entangles UI responsiveness with retry timing and could cause subtle UI hitches if the message thread is busy. Not recommended.

### "Recheck permission" button label state (D-12)

After Unavailable transition (TCC denied), the Camera button:
- Label changes from "Camera" to "Recheck permission"
- Tooltip updates: "Click to re-check macOS camera permission. Grant access in System Settings → Privacy & Security → Camera, then click here."
- Click invokes `jamwide::queryCameraAuthorization()`:
  - If now `Authorized`: state → Opening, proceed to `openDeviceAsync`. Button label flips back to "Camera".
  - If still `Denied`: re-show fallback dialog with the appropriate cause (D-14 suppression rule: if cause same as before, focus button; if cause changed, show dialog).

**Important nuance:** the macOS user can grant permission in System Settings without an app restart; the next `authorizationStatusForMediaType` call will reflect the new status. This is well-tested OS behavior.

---

## 6. UI Components — File Layout and JUCE Patterns

### Recommended file layout

```
juce/video/native/                                ← new subdirectory
├── JamWideCameraDevice.h / .cpp                  ← JUCE CameraDevice owner + state machine
├── JamWideFrameDistributor.h / .cpp              ← thread-safe fan-out (§4)
├── CameraPreviewWindow.h / .cpp                  ← juce::DocumentWindow popout (D-05/07/08)
├── CameraPreviewTile.h / .cpp                    ← the Component that renders the juce::Image
├── CameraAuthorization.h                         ← cross-platform interface (§2)
├── CameraAuthorization_mac.mm                    ← TCC pre-check + access request
├── CameraAuthorization_windows.cpp               ← stubbed to NotApplicable
├── CameraStatusDialog.h / .cpp                   ← cause-aware fallback (§9)
└── NativeCameraPrivacyDialog.h / .cpp            ← D-22 first-use modal (NEW — distinct from juce/video/VideoPrivacyDialog.h)
```

**Naming note:** the existing `juce/video/VideoPrivacyDialog.{h,cpp}` covers VDO.Ninja IP-exposure (`VID-05`) and **stays operational during the parallel beta** (D-22 explicitly preserves it). The new modal is distinct — we name it `NativeCameraPrivacyDialog` to disambiguate.

### Integration points into `JamWideJuceProcessor` and `ConnectionBar`

| File:line | Today | After Phase 19 |
|-----------|-------|----------------|
| `juce/JamWideJuceProcessor.h:119` | `std::unique_ptr<jamwide::VideoCompanion> videoCompanion;` (kept for parallel beta) | Add `std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera;` and `std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor;` near line 119. VideoCompanion stays. |
| `juce/JamWideJuceProcessor.h:88` | `static constexpr int currentStateVersion = 3;` | Bump to `= 4;` (D-24) |
| `juce/JamWideJuceProcessor.cpp:61-64` | constructor inits `oscServer`, `videoCompanion`, `midiMapper` | Add `frameDistributor = std::make_unique<JamWideFrameDistributor>();` and `nativeCamera = std::make_unique<JamWideCameraDevice>(*frameDistributor);` AFTER videoCompanion init. |
| `juce/JamWideJuceProcessor.cpp:67-76` | destructor tears down in reverse | Add `nativeCamera.reset(); frameDistributor.reset();` BEFORE `videoCompanion.reset()` (LIFO destruction order). |
| `juce/JamWideJuceProcessor.cpp:622-672` | getStateInformation — writes APVTS + lastServer + OSC + MIDI | Add `<camera>` subtree write (popout bounds, qualityPreset, privacyAck, selectedDevice) per D-25 |
| `juce/JamWideJuceProcessor.cpp:674-...` | setStateInformation — reads APVTS + non-APVTS state | Add `<camera>` subtree read with defaults (D-25 default values applied for missing v3-state) |
| `juce/ui/ConnectionBar.h` | declares `juce::TextButton videoButton;`, `std::function<void()> onVideoClicked;` | Add `juce::TextButton cameraButton;`, `std::function<void()> onCameraClicked;`, `std::function<void()> onCameraRightClicked;` |
| `juce/ui/ConnectionBar.cpp:206-217` | "Video" button setup | Add IDENTICAL pattern for "Camera" button immediately after (D-06 — Camera lives next to Video) |
| `juce/ui/ConnectionBar.cpp:232-...` | `resized()` layout | Add a slot for the Camera button in the layout's button row |
| `juce/ui/ConnectionBar.cpp:644-650` | `setVideoActive(bool)` toggles videoButton color | Add `setCameraActive(bool)` for cameraButton following identical pattern |
| `juce/JamWideJuceEditor.cpp:127-144` | `connectionBar.onVideoClicked` lambda launches VDO.Ninja | Add `connectionBar.onCameraClicked` lambda invoking `processorRef.nativeCamera->toggle()` |

**Critical:** the existing `videoButton` and `videoCompanion` are **NOT removed** in Phase 19 (D-06 "parallel beta"). VDO.Ninja teardown is Item H, deferred to post-beta per `260515-0pc-deferred-items.md`.

### Right-click PopupMenu for quality preset selection (D-19)

The JUCE pattern is to wire `juce::Button::onClick` to the left-click handler and use `juce::Component::mouseDown` override + `e.mods.isPopupMenu()` to handle right-click. Then construct a `juce::PopupMenu` with the three preset items + a `juce::PopupMenu::Options` and call `showMenuAsync(...)`.

Concrete pattern (write into `ConnectionBar`'s custom button subclass, or attach a `mouseListener` to the button):

```cpp
void CameraButton::mouseDown(const juce::MouseEvent& e) override {
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addSectionHeader("Quality");
        const int current = static_cast<int>(processor.cameraQualityPreset());
        menu.addItem(1, "Low (320x240, 10fps)",  true, current == 0);
        menu.addItem(2, "Medium (640x480, 15fps)", true, current == 1);
        menu.addItem(3, "High (1280x720, 30fps)",  true, current == 2);
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
            [&](int result) { if (result > 0) processor.setCameraQualityPreset(result - 1); });
        return;
    }
    juce::TextButton::mouseDown(e);  // pass-through for left-click
}
```

This pattern is consistent with the existing MIDI/OSC dialog launching pattern in this codebase (e.g., `MidiConfigDialog.cpp:115` uses `juce::AlertWindow::showAsync`).

### Popout window chrome (D-08)

`juce::DocumentWindow` supports custom title-bar styling by overriding `LookAndFeel::drawDocumentWindowTitleBar`. The existing `JamWideLookAndFeel` (`juce/ui/JamWideLookAndFeel.{h,cpp}`) defines the colour palette (`kSurfaceStrip = 0xff2A2D48`, `kTextPrimary = 0xffE0E0E0`, `kAccentConnect = 0xff40E070` — `JamWideLookAndFeel.h:12-26`). The popout should:

1. Construct with `juce::DocumentWindow::closeButton | minimiseButton` (no maximise — capturing UI doesn't need maximise; resize handle covers it).
2. Call `setUsingNativeTitleBar(false)` for VB-Audio Voicemeeter Banana parity.
3. Override `closeButtonPressed()` to hide-not-destroy (state machine handles "hidden popout, camera still capturing" per D-09).
4. `setResizable(true, true);` and apply aspect-ratio constraint via `getConstrainer()->setFixedAspectRatio(4.0 / 3.0)` (4:3 per D-07).
5. Default size 320×240; min 240×180 (recommended per Claude's Discretion); max bounded by current screen.

The custom titlebar font/colour selection can either reuse the global `JamWideLookAndFeel` (the popout inherits it from the editor) OR install a local override via `setLookAndFeel(&jamwideLnF)` on the popout. The former is cleaner.

[CITED: `libs/juce/modules/juce_gui_basics/windows/juce_DocumentWindow.h`; existing pattern at `juce/JamWideJuceEditor.cpp:20` (`setResizable(false, false);`)]

---

## 7. Entitlements + Info.plist (D-28, PKG-04 entitlements portion)

### File changes to `JamWide.entitlements`

Current content (`JamWide.entitlements:1-12`):

```xml
<key>com.apple.security.device.audio-input</key>
<true/>
<key>com.apple.security.network.client</key>
<true/>
<key>com.apple.security.network.server</key>
<true/>
```

Add:
```xml
<key>com.apple.security.device.camera</key>
<true/>
```

The file is already plumbed into the codesign step via `CMakeLists.txt:257-258` (`list(APPEND _sign_flags --entitlements "${CMAKE_SOURCE_DIR}/JamWide.entitlements")`). No CMake change is needed for entitlements file delivery; just the file edit.

### `NSCameraUsageDescription` via `juce_add_plugin`

JUCE generates `NSCameraUsageDescription` in the bundle's Info.plist when `CAMERA_PERMISSION_ENABLED TRUE` is passed to `juce_add_plugin`. [VERIFIED at `libs/juce/extras/Build/juce_build_tools/utils/juce_PlistOptions.cpp:149-150` (`if (cameraPermissionEnabled) addPlistDictionaryKey(*dict, "NSCameraUsageDescription", cameraPermissionText);`) and at `libs/juce/extras/Build/CMake/JUCEUtils.cmake:367-368` (`CAMERA_PERMISSION_ENABLED` and `CAMERA_PERMISSION_TEXT` are JUCE-defined target properties).]

The exact CMake parameter names are confirmed:
- `CAMERA_PERMISSION_ENABLED TRUE`
- `CAMERA_PERMISSION_TEXT "JamWide uses your webcam to share video with NINJAM peers."`

These slot into the existing `juce_add_plugin` block at `CMakeLists.txt:151-168`, immediately after the existing `MICROPHONE_PERMISSION_*` lines:

```cmake
MICROPHONE_PERMISSION_ENABLED TRUE
MICROPHONE_PERMISSION_TEXT "JamWide needs microphone access for standalone mode"
CAMERA_PERMISSION_ENABLED TRUE                                               # NEW
CAMERA_PERMISSION_TEXT "JamWide uses your webcam to share video with NINJAM peers."  # NEW
```

### Hardened-runtime + sandbox interaction reminder

JamWide opts into hardened runtime conditionally:
- `JAMWIDE_HARDENED_RUNTIME=OFF` by default (dev builds use ad-hoc signing — `CMakeLists.txt:251-252`).
- `JAMWIDE_HARDENED_RUNTIME=ON` for release builds. When enabled, `--options runtime --timestamp --entitlements JamWide.entitlements` are added to the codesign invocation (`CMakeLists.txt:255-259`).

For Phase 19 entitlement verification, the planner should ensure that the camera entitlement test build uses `JAMWIDE_HARDENED_RUNTIME=ON` — without hardened runtime, the entitlement is silently ignored. This is the difference between "entitlement was declared correctly" and "the entitlement actually does anything."

### App Sandbox not in scope

JamWide does NOT currently enable App Sandbox (`com.apple.security.app-sandbox`) — it's not in `JamWide.entitlements:1-12` and the JUCE `APP_SANDBOX_ENABLED` property is not set in `juce_add_plugin`. The camera entitlement works without sandbox; the sandbox would add additional constraints (e.g., user-selected-files vs implicit access) that don't apply here.

### Verification commands (per D-28)

The notarization verification task must run:

```bash
# 1. Verify codesign with hardened runtime + camera entitlement
codesign --verify --deep --strict "$BUNDLE_PATH"
codesign --display --entitlements - "$BUNDLE_PATH" | grep "device.camera"

# 2. Notarize (uses project memory project_apple_signing API Key flow)
xcrun notarytool submit "$ZIPPED_BUNDLE" --keychain-profile "<configured-profile>" --wait

# 3. Staple ticket onto bundle
xcrun stapler staple "$BUNDLE_PATH"
xcrun stapler validate "$BUNDLE_PATH"

# 4. Sanity check: try to launch standalone, verify TCC prompt appears
open "$BUNDLE_PATH"
```

### Phase 23 boundary

Phase 23 owns:
- Per-dylib codesigning of vendored ffmpeg dylibs in the bundle's `Contents/Frameworks/` (PKG-04 codesign + frameworks-path portions).
- `install_name_tool -change` rewriting load paths to `@loader_path/../Frameworks/`.
- Universal-binary stitching (arm64 + x86_64 via `lipo`).
- Windows signtool integration (PKG-07).
- CI lanes for both platforms (PKG-05 LGPL discipline checks).

Phase 19 only verifies that the entitlement plumbing produces a launchable, TCC-prompting bundle on macOS. If anything breaks during Phase 19's verification, the issue is surfaced for Phase 23 to fix — Phase 19 does not block on Phase 23 completion.

---

## 8. Plugin State Schema v3 → v4 (D-24, D-25)

### Where ValueTree state lives

The ValueTree save/load lives in `JamWideJuceProcessor::getStateInformation` (`juce/JamWideJuceProcessor.cpp:622-672`) and `setStateInformation` (`juce/JamWideJuceProcessor.cpp:674-...`).

The schema-version field is at `JamWideJuceProcessor.cpp:628`:
```cpp
state.setProperty("stateVersion", currentStateVersion, nullptr);
```
where `currentStateVersion` is `static constexpr int currentStateVersion = 3;` at `juce/JamWideJuceProcessor.h:88`.

### v3 → v4 migration

Bump:
```cpp
static constexpr int currentStateVersion = 4;  // v4: added <camera> subtree (Phase 19)
```

On load, `setStateInformation` reads `int version = tree.getProperty("stateVersion", 0);` (`juce/JamWideJuceProcessor.cpp:681`). The pattern already handles missing fields gracefully via `getProperty(key, defaultValue)` per the existing OSC v1→v2 migration comment (`juce/JamWideJuceProcessor.cpp:682`). Phase 19 follows that pattern exactly: read each new camera-subtree property with its D-25 default.

### `<camera>` subtree XML shape

JUCE ValueTree supports nested subtrees. Two viable approaches:

**Approach A — flat properties on root state (mirrors existing OSC pattern):**
```cpp
state.setProperty("cameraPopoutX",       popoutBounds.getX(), nullptr);
state.setProperty("cameraPopoutY",       popoutBounds.getY(), nullptr);
state.setProperty("cameraPopoutWidth",   popoutBounds.getWidth(), nullptr);
state.setProperty("cameraPopoutHeight",  popoutBounds.getHeight(), nullptr);
state.setProperty("cameraQualityPreset", static_cast<int>(qualityPreset), nullptr);
state.setProperty("cameraPrivacyAck",    privacyAcknowledged, nullptr);
state.setProperty("cameraSelectedDevice", selectedDeviceName, nullptr);
```

**Approach B — nested `<camera>` child element (mirrors MIDI mapping pattern):**
```cpp
auto cameraTree = juce::ValueTree("camera");
cameraTree.setProperty("popoutX",       popoutBounds.getX(), nullptr);
cameraTree.setProperty("popoutY",       popoutBounds.getY(), nullptr);
// ... etc
state.appendChild(cameraTree, nullptr);
```

**Recommendation: Approach A** (flat properties). Rationale:
1. Mirrors the existing OSC v2 fields at `JamWideJuceProcessor.cpp:653-657` (`oscEnabled`, `oscReceivePort`, `oscSendIP`, `oscSendPort`) — consistency with neighbours.
2. Defaults-handling is more straightforward — `tree.getProperty("cameraPopoutX", 100)` reads a fallback default in one line.
3. The MIDI mapper nested pattern is only used because MIDI has variable-length data (per-CC mappings); camera state is fixed-shape.

The XML schema becomes:

```xml
<APVTSTree stateVersion="4" lastServer="ninbot.com" lastUsername="anonymous" ...
           cameraPopoutX="100" cameraPopoutY="100" cameraPopoutWidth="320"
           cameraPopoutHeight="240" cameraQualityPreset="1" cameraPrivacyAck="0"
           cameraSelectedDevice="">
  ...
</APVTSTree>
```

### Defaults from D-25

| Field | Default | Notes |
|-------|---------|-------|
| `cameraPopoutX` | 100 | Or use `getDesktopRect().getCentreX() - 160` to centre. JUCE-default is `-1` for "OS chooses". |
| `cameraPopoutY` | 100 | Similar. |
| `cameraPopoutWidth` | 320 | Matches Low preset / spike baseline. |
| `cameraPopoutHeight` | 240 | 4:3 to match width. |
| `cameraQualityPreset` | 1 (Medium per D-18) | enum 0=Low, 1=Medium, 2=High |
| `cameraPrivacyAck` | false | D-22 first-use modal triggers on first AFTER-permission-grant click |
| `cameraSelectedDevice` | "" (empty) | D-25: "default empty = auto-pick" |

### Order of operations

**Save (`getStateInformation`):**
1. Capture current state from running components (popout bounds via `getBounds()` on the popout window if visible; quality preset from a `std::atomic<int>` on the processor; privacy-ack from a `bool` on the processor; selected device from the camera-device wrapper).
2. Write to ValueTree per Approach A.
3. Existing `state.createXml() + copyXmlToBinary()` chain serializes.

**Load (`setStateInformation`):**
1. Existing v1→v2/v3 reads happen first.
2. Read camera-subtree properties with defaults.
3. Apply to processor state (atomics + popout window's initial bounds + privacy-ack member).
4. Camera state machine starts in `Idle` regardless (D-10).

### State-version logging recommendation

Add to setStateInformation immediately after reading version:
```cpp
juce::Logger::writeToLog("Plugin state version on load: " + juce::String(version)
                       + " (current: " + juce::String(currentStateVersion) + ")");
```
This catches "user opened a v4-saved state in an old v3 binary" scenarios that beta testers might report.

---

## 9. Permission-Denial Fallback Dialog (D-13..D-16)

### Cause detection matrix

| Cause enum | Input that distinguishes | Platform |
|-----------|--------------------------|----------|
| `TCCDenied` | `queryCameraAuthorization() == Denied`<br>OR `Restricted` (parental controls) | macOS |
| `HostLacksEntitlement` | (Inside DAW plugin) `queryCameraAuthorization() == Denied` AND `JUCEApplicationBase::isStandaloneApp() == false` AND host bundle ID is a known no-camera-entitlement DAW | macOS plugin |
| `CameraInUse` | `openDeviceAsync` callback fires with non-empty error string mentioning "in use" / "busy" / device-locked; OR watchdog (§2) fires with no frames after auth=Authorized | macOS + Windows |
| `NoHardware` | `getAvailableDevices().isEmpty()` | macOS + Windows |
| `WindowsPrivacyBlock` | (Windows only) `openDeviceAsync` callback fires with `nullptr` AND `getAvailableDevices().isEmpty() == false` | Windows |

**Distinguishing TCCDenied vs HostLacksEntitlement:** the user-visible behaviour is identical on macOS (both produce `Denied` from `authorizationStatusForMediaType`). The dialog copy is what differs. The distinguishing input is the running context:

```cpp
bool isPlugin = ! juce::JUCEApplicationBase::isStandaloneApp();
auto status = jamwide::queryCameraAuthorization();
if (status == jamwide::CameraAuthStatus::Denied) {
    if (isPlugin) {
        // Highly likely: host lacks the entitlement (e.g., REAPER, Live, Bitwig)
        // Logic Pro is the exception, but its entitlement grant would have
        // resulted in Authorized, not Denied
        cause = CameraFallbackCause::HostLacksEntitlement;
    } else {
        // Standalone — user actively said No, or the entitlement is missing
        // from the bundle (build configuration bug)
        cause = CameraFallbackCause::TCCDenied;
    }
}
```

The "HostLacksEntitlement vs user-said-no-in-standalone" disambiguation is approximate. In practice, the user is more likely to be confused by the wrong copy than offended by it, so this heuristic is acceptable.

### `juce::AlertWindow` non-blocking pattern

Use `juce::AlertWindow::showAsync` (mirroring `juce/midi/MidiConfigDialog.cpp:115`). The synchronous `AlertWindow::show*` blocks the message thread — unacceptable for a plugin runtime.

Skeleton:

```cpp
auto options = juce::MessageBoxOptions{}
    .withIconType(juce::MessageBoxIconType::WarningIcon)
    .withTitle("Camera unavailable")
    .withMessage(causeAwareMessageFor(cause))
    .withButton("Open System Settings")  // platform-aware, only when cause supports deep-link
    .withButton("Recheck permission")
    .withButton("OK");
juce::AlertWindow::showAsync(options, [self, cause](int buttonChosen) {
    self->handleFallbackResponse(cause, buttonChosen);
});
```

### Suppress-after-first-show + re-show-on-cause-change (D-14)

On the processor, maintain:
```cpp
std::atomic<int> lastShownCauseEpoch_{-1};  // -1 = never shown
CameraFallbackCause lastShownCause_;
```
Logic:
- On trigger, compute current `cause`.
- If `cause == lastShownCause_ && lastShownCauseEpoch_ != -1`: suppress dialog, focus the Camera button (`connectionBar.cameraButton.grabKeyboardFocus()`).
- Else: show dialog, set `lastShownCause_ = cause; lastShownCauseEpoch_ = std::chrono::system_clock::now().time_since_epoch().count();`.

### Deep-link patterns

**macOS:**
```cpp
juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser();
```
This URL is a documented macOS Preference URI scheme. JUCE's `launchInDefaultBrowser` calls `NSWorkspace openURL:` internally on macOS, which the Preferences app recognises and opens the matching Privacy & Security → Camera pane.

[CITED: macOS preference URL schemes; pattern used widely in macOS apps for "open Privacy Settings" deep-link.]

**Windows:**
```cpp
juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser();
```

### Copy strings (from CONTEXT specifics)

The dialog text skeleton lives in CONTEXT.md `<specifics>` section (lines 191-195). The dialog implementation just needs:

```cpp
juce::String causeAwareMessageFor(CameraFallbackCause cause) {
    switch (cause) {
        case CameraFallbackCause::TCCDenied:
            return "macOS has denied camera access to JamWide. Grant permission in System Settings → Privacy & Security → Camera, then click Recheck.";
        case CameraFallbackCause::HostLacksEntitlement:
            return juce::String(getHostName()) + " doesn't request camera access for itself, so JamWide can't reach the camera while hosted in it.\n\nTip: JamWide standalone has direct camera access.";
        case CameraFallbackCause::CameraInUse:
            return "Another app is using the camera. Close it and click Recheck.";
        case CameraFallbackCause::NoHardware:
            return "No camera detected. Connect a webcam and click Recheck.";
        case CameraFallbackCause::WindowsPrivacyBlock:
            return "Windows has blocked camera access. Enable camera access in Settings → Privacy → Camera, then click Recheck.";
    }
    return "Camera unavailable.";
}
```

`getHostName()` for the `HostLacksEntitlement` case can be sourced from `juce::PluginHostType().getHostDescription()` — JUCE has built-in DAW detection.

### Action-button mapping

| Cause | Buttons (in order) |
|-------|--------------------|
| TCCDenied | "Open System Settings" → macOS deep-link; "Recheck permission" → re-query auth; "OK" → dismiss |
| HostLacksEntitlement | "OK" → dismiss (no useful action; copy-only standalone hint per D-16) |
| CameraInUse | "Recheck permission" → retry open; "OK" → dismiss |
| NoHardware | "Recheck permission" → re-enumerate devices; "OK" → dismiss |
| WindowsPrivacyBlock | "Open Camera Privacy Settings" → Windows deep-link; "Recheck permission" → retry open; "OK" → dismiss |

---

## 10. VDO.Ninja Coexistence (D-27)

### How to detect "VDO.Ninja is active"

The existing VideoCompanion exposes a lock-free atomic `isActive()`:

```cpp
bool jamwide::VideoCompanion::isActive() const { return active_.load(std::memory_order_relaxed); }
```
[CITED: `juce/video/VideoCompanion.h:77` + comment at line 29: "lock-free atomic read, safe from any thread"]

So inside the Camera button onClick handler:

```cpp
connectionBar.onCameraClicked = [this]() {
    if (processorRef.videoCompanion && processorRef.videoCompanion->isActive()) {
        // VDO.Ninja currently active — show coexistence toast first
        showCoexistenceToast();
    }
    processorRef.nativeCamera->toggle();
};
```

### Toast pattern using `juce::AlertWindow::NoIcon`

```cpp
auto options = juce::MessageBoxOptions{}
    .withIconType(juce::MessageBoxIconType::NoIcon)
    .withTitle("Multiple video stacks active")
    .withMessage("VDO.Ninja video is also active. Bandwidth and CPU may be high — consider stopping one for better quality.")
    .withButton("OK");
juce::AlertWindow::showAsync(options, [](int){});
```

Note: this dialog is informational only — it does NOT block the camera toggle. The toggle happens after (or in parallel with) the dialog show. The user can ignore the toast and proceed (per D-27 "User can ignore and proceed").

---

## 11. Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Lightweight inline `main()` test harness compiled via `juce_add_console_app` + `add_executable`; assertion via `assert()`/`JUCE_ASSERT` / direct exit codes |
| Config file | Inline in `tests/` directory; gated by `JAMWIDE_BUILD_TESTS=ON` (`CMakeLists.txt:29,136,301,406`) and `enable_testing()` (`CMakeLists.txt:407`) |
| Quick run command | `cd build-juce && ctest -R camera --output-on-failure` (after `cmake --build . --target test_camera_state_machine test_frame_distributor test_camera_cause_mapping`) |
| Full suite command | `cd build-juce && ctest --output-on-failure` |

The repo already has a rich test inventory in this style: `tests/test_block_queue_spsc.cpp`, `tests/test_local_channel_mirror.cpp`, `tests/test_njclient_atomics.cpp`, `tests/test_rawdata_send.cpp`, `tests/test_video_fourcc.cpp`, `tests/test_remote_user_mirror.cpp`, etc. The new camera tests follow this pattern exactly.

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CAM-01 (macOS standalone happy path) | OS prompt appears; permission grant leads to capturing | manual UAT | Manual on macOS arm64+x86_64 standalone | ❌ Wave 0 — UAT script doc |
| CAM-01 (DAW plugin happy path) | Plugin in Logic Pro reaches Capturing | manual UAT | Manual in Logic Pro on macOS | ❌ Wave 0 — UAT script doc |
| CAM-02 (host lacks entitlement) | REAPER on macOS shows fallback dialog without crash | manual UAT | Manual in REAPER on macOS | ❌ Wave 0 — UAT script doc |
| CAM-02 (cause detection logic) | Correct CameraFallbackCause selected for each input matrix row | unit | `ctest -R camera_cause_mapping --output-on-failure` | ❌ Wave 0 — `tests/test_camera_cause_mapping.cpp` |
| CAM-03 (preview rendered) | Frame distributor publishes synthetic `juce::Image` to a synthetic subscriber; subscriber's `onFrame` receives equal-pixel buffer | unit | `ctest -R frame_distributor --output-on-failure` | ❌ Wave 0 — `tests/test_frame_distributor.cpp` |
| CAM-03 (state machine) | All transitions in §5 exercise the state machine; assert invariants | unit | `ctest -R camera_state_machine --output-on-failure` | ❌ Wave 0 — `tests/test_camera_state_machine.cpp` |
| CAM-03 (retry backoff timing) | Sequence 1s/2s/4s/8s/16s observed; bail at 30s budget | unit (with virtualized clock) | `ctest -R camera_retry_backoff --output-on-failure` | ❌ Wave 0 — `tests/test_camera_retry_backoff.cpp` |
| CAM-02 (permission-revoke roundtrip) | Revoke camera permission in System Settings while app is running; assert preview disappears, fallback appears | manual UAT | Manual on macOS | ❌ Wave 0 — UAT script doc |
| PKG-04 (entitlements) | `codesign --display --entitlements - "$BUNDLE"` shows `com.apple.security.device.camera` | smoke (CI-runnable) | `cd build-juce && ./scripts/verify_camera_entitlement.sh "$BUNDLE"` | ❌ Wave 0 — `scripts/verify_camera_entitlement.sh` |
| PKG-04 (Info.plist) | `plutil -extract NSCameraUsageDescription raw "$BUNDLE/Contents/Info.plist"` returns the configured string | smoke (CI-runnable) | Same script as above (combined) | ❌ Wave 0 — same script |
| D-28 (notarization) | `xcrun stapler validate "$BUNDLE"` passes | manual (needs API key) | Manual on macOS with notarization keychain profile | ❌ Wave 0 — UAT script doc |
| Windows happy path (success criterion 5) | Windows standalone opens camera, sees preview | manual UAT | Manual on Windows x86_64 | ❌ Wave 0 — UAT script doc |
| Windows privacy block | Windows with camera disabled in Settings shows WindowsPrivacyBlock dialog | manual UAT | Manual on Windows | ❌ Wave 0 — UAT script doc |
| Plugin state schema v3→v4 roundtrip | Save state on v3 binary, load on v4 binary; assert all v3 fields preserved + camera fields default-applied | unit | `ctest -R plugin_state_v3_v4 --output-on-failure` | ❌ Wave 0 — `tests/test_plugin_state_v3_v4.cpp` |

### Sampling Rate

- **Per task commit:** `ctest -R camera --output-on-failure` (~5-10s wall) plus the relevant unit test for the just-touched component
- **Per wave merge:** `ctest --output-on-failure` (full suite, ~30-60s)
- **Phase gate:** Full suite green + all manual UAT cells completed per `feedback_uat_scope_redflags` memory (no skipping CAM-01/02/03)

### Wave 0 Gaps

- [ ] `tests/test_frame_distributor.cpp` — covers CAM-03 (frame fan-out, removal-safe iteration, thread-safety smoke)
- [ ] `tests/test_camera_state_machine.cpp` — covers CAM-02/CAM-03 (state transition invariants)
- [ ] `tests/test_camera_retry_backoff.cpp` — covers D-20 retry timing (use a virtualized clock or shorter test-mode intervals)
- [ ] `tests/test_camera_cause_mapping.cpp` — covers CAM-02 (cause → copy mapping; cause classification logic)
- [ ] `tests/test_plugin_state_v3_v4.cpp` — covers D-24/D-25 (schema migration)
- [ ] `scripts/verify_camera_entitlement.sh` — covers PKG-04 entitlements (CI-runnable codesign+plist inspection)
- [ ] `docs/UAT/phase-19-camera-uat-checklist.md` — manual UAT script with each CAM-01/02/03 path explicit (referenced by Phase 24's per-DAW matrix later)
- [ ] CMake wiring: add `add_executable(test_camera_* ...)` + `add_test(NAME camera_* COMMAND ...)` entries under `if(JAMWIDE_BUILD_TESTS)` at `CMakeLists.txt:406+`

### Nyquist concern coverage

- **State machine matrix** (states × triggers, §5): exhaustively tested in `test_camera_state_machine.cpp`. The sample rate is "one assertion per cell" of the transition table.
- **Cause matrix** (5 causes × 2 platforms = 10 cells, §9): unit-tested via cause-input-shape simulation in `test_camera_cause_mapping.cpp`. Cells map cleanly to assertions.
- **Cross-host matrix** (Logic Pro / REAPER / Live / Bitwig on macOS + Standalone / REAPER on Windows = 6 cells minimum): manual UAT. Phase 19 covers the **2 macOS cells** (Logic Pro happy path, REAPER fallback) + **the 1 macOS standalone cell** + **the 1 Windows standalone cell** explicitly. Other DAW cells (Live, Bitwig) are Phase 24 territory per CONTEXT "Deferred Ideas" + STATE roadmap.
- **Retry-backoff timing** (5 backoff intervals × 30s budget): unit-tested with virtualized clock; CI fast. Empirical retry timing under load is manual UAT.
- **Permission-revoke roundtrip**: cannot be unit-tested (depends on macOS System Settings). Manual UAT cell.
- **Notarization**: cannot be unit-tested (depends on Apple notary service). Manual UAT cell.

The aggregate sampling rate is appropriate: high-frequency state-machine and distributor logic gets unit tests; low-frequency cross-host quirks get manual UAT. The `feedback_uat_scope_redflags` memory mandates that all of CAM-01/02/03 receive UAT-time verification — the planner should refuse any plan that defers a CAM-* UAT cell.

---

## 12. Open Questions for the Planner / User

These are items where CONTEXT.md leaves discretion or where the spike's evidence partially conflicts with the locked design. The planner should resolve each before plans land.

1. **Sync vs async `juce::CameraDevice::openDeviceAsync`.** [CONTEXT: Claude's Discretion] Research §1 recommends async. **Risk if wrong:** sync path freezes the message thread during the TCC prompt on a first launch, which is briefly user-visible but not a correctness issue. Easy to revisit.

2. **Retry thread implementation.** [CONTEXT: Claude's Discretion] Research §5 recommends `juce::Thread` subclass over `juce::TimedCallback` chain. **Risk if wrong:** `TimedCallback` chain on the message thread could cause subtle UI hitches during retry. Easy to revisit.

3. **`GetCameraPeakFrameRate()` debug accessor in Phase 19 vs Phase 24.** [CONTEXT: Claude's Discretion] Research recommends shipping it in Phase 19 because the spike's measured-FPS evidence is the only objective evidence beta testers will have for "is the camera actually working?" Adding it in Phase 24 would be late.

4. **Distinguishing `TCCDenied` vs `HostLacksEntitlement` heuristic.** Research §9 uses `JUCEApplicationBase::isStandaloneApp() == false` as the proxy for "plugin context" — this is approximate. **Risk if wrong:** wrong copy in the fallback dialog. Cosmetic; not blocking.

5. **Watchdog timer interval.** Research §2 suggests 3 seconds (matches success criterion 1 "within 3 seconds"). **Risk if wrong:** watchdog fires too soon on a slow camera startup, or too late after a real failure. Tune during UAT.

6. **State-version logging on load.** Research §8 suggests adding `juce::Logger::writeToLog` when a v3 state is loaded by a v4 binary. The CONTEXT doesn't mandate this; planner discretion.

7. **Min/max popout bounds.** [CONTEXT: Claude's Discretion] Research suggests min 240×180 (per Claude's Discretion line). Max could be the user's display dimensions. Planner picks.

8. **Cause-aware deep-link button label.** Research §9 suggests "Open System Settings" for macOS and "Open Camera Privacy Settings" for Windows. Could also use a single platform-conditional label like "Open Camera Privacy Settings" everywhere. Cosmetic.

9. **AppCapability fallback for packaged Windows app.** Research §3 documents that `AppCapability` is unavailable for Win32 desktop apps. If Phase 23 eventually ships a `.msix` packaged JamWide for Windows Store, the Windows cause detection should be re-evaluated. Not blocking for Phase 19.

10. **Empirical macOS PhotoOutput max FPS.** JUCE's macOS backend uses `AVCapturePhotoOutput` which is slower than `AVCaptureVideoDataOutput` would be. The 30-fps target of D-18's High preset may not be achievable on macOS at all. **Risk if wrong:** users select High preset and see ~15 fps actual. Mitigate by tooltip / status display. Phase 20's encoder will measure and report actual fps anyway.

---

## 13. Risks / Surprises

### Risk A — Spike Risk #2 (TCC pre-check needed) [INHERITED]

Already resolved via D-03 and §2. Concrete implementation paths documented. **Confidence:** HIGH.

### Risk B — Spike Risk #5 (ffmpeg 7.x soname symlinks) [Phase 23 territory]

Documented in `260515-0pc-spike-results.md:206-212`. Not applicable to Phase 19 (no ffmpeg consumption in Phase 19). Flagged here so the planner knows it's Phase 23's problem.

### Risk C — JUCE seat licence for `juce_video` (Q1) [INHERITED, NEEDS-USER-DECISION]

`juce_video` ships with the same dual licence as the rest of JUCE: AGPLv3 OR Commercial. CONTEXT.md D-01 states: "AGPLv3 licence (compatible with JamWide's GPLv2+ via the 'or any later version' clause)." This works ONLY if JamWide is licensed as "GPLv2 or any later version" — the upgrade clause is essential.

**Action for planner:** verify by inspecting JamWide's LICENSE file or top-of-source-file headers that the "or any later version" clause is consistently used. If JamWide is licensed as "GPLv2 only" (no upgrade clause), the AGPLv3 compatibility argument fails and the project must either (a) hold a JUCE commercial seat covering `juce_video` or (b) replan with direct AVFoundation + DirectShow capture (~+1 plan, ~+800 LOC per `260515-0pc-deferred-items.md:173`).

[VERIFIED: AGPLv3 licence header at `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:25` ("You may also use this code under the terms of the AGPLv3").]

### Risk D — macOS arm64 `juce_video` build artifact [VERIFIED low-risk]

The current JamWide CMakeLists builds for whatever architecture is configured (`x86_64` locally per the memory; universal in CI). The `juce_video` module compiles cleanly for both architectures because it's pure source — there's no prebuilt binary to vendor. The macOS backend is `juce_CameraDevice_mac.h` (header-only, included from `juce_video.cpp`) which is Objective-C++ and compiles for both architectures via Apple's clang. **No blocker for arm64.**

The Phase 19 caveat is that the spike was run on x86_64 only (`spike-results.md:24` — "Vendored x86_64 only"). The first arm64 build of JamWide that touches the camera path will be a new test surface. The planner should ensure that one of the manual UAT cells is "macOS arm64 JamWide standalone, fresh camera open" specifically.

### Risk E — `JUCE_USE_CAMERA` not currently defined for the main plugin

`CMakeLists.txt:371` defines `JUCE_USE_CAMERA=1` only for the `video_spike` console app (the spike's test executable). The main `JamWideJuce` plugin target does NOT currently define `JUCE_USE_CAMERA`. Without this define, `juce::CameraDevice` declarations are wrapped in `#if JUCE_USE_CAMERA || DOXYGEN` (`juce_CameraDevice.h:38`) and become unavailable.

**Action for planner:** Phase 19 must add to the main plugin target:
```cmake
target_compile_definitions(JamWideJuce PUBLIC JUCE_USE_CAMERA=1)
target_link_libraries(JamWideJuce PRIVATE juce::juce_video)
```
This is a strict prerequisite. Plan should sequence the CMake change FIRST, before any code that references `juce::CameraDevice`.

### Risk F — Plugin Info.plist may be ignored by macOS TCC

Documented in spike RESEARCH §7 ("JUCE camera permission may not exist for plugins on macOS"). The macOS TCC system tracks camera permission by the **host process's** bundle ID, not the loaded plugin's bundle. The `NSCameraUsageDescription` we add via `CAMERA_PERMISSION_ENABLED TRUE` lands in the plugin bundle's Info.plist — which TCC may or may not read.

Behaviour: in standalone, JamWide IS the host process and Info.plist is read correctly. In a DAW plugin, the DAW is the host process — Logic Pro has its own `NSCameraUsageDescription`, REAPER does not. This is exactly the SPARTA Issue #82 scenario.

**Action:** the cause-aware fallback (§9) is the user-facing mitigation. There is no engineering fix for "REAPER doesn't request the entitlement" short of asking the REAPER developers — out of scope for JamWide.

### Risk G — Windows MediaFoundation expectation vs JUCE's DirectShow reality

CONTEXT and STATE both say "Windows uses `juce_CameraDevice_windows.h` (Media Foundation / DirectShow)". The actual JUCE 8 backend is DirectShow-only (§3). This doesn't break anything — DirectShow works at the Windows API level — but the planner should not assume Media Foundation features are accessible.

### Risk H — JUCE's macOS still-photo polling pipeline

The macOS backend uses `AVCapturePhotoOutput` with a re-trigger loop (§1), not continuous `AVCaptureVideoDataOutput`. This:
- Caps effective FPS at the still-photo capture cost
- May fail to reach D-18's High preset's 30 fps target on macOS
- Makes "no frames after grant" indistinguishable from "low FPS but actually working" until a frame arrives

The watchdog timer (§2) mitigates "no frames" detection. The FPS ceiling is a beta-acceptable limitation per CONTEXT's quality-preset locked-decision (D-18 sets explicit ceilings).

### Risk I — VDO.Ninja IP detection

D-22's privacy modal copy includes: "There's no separate IP exposure beyond what audio already does." This is true for the native-camera path (frames flow through NINJAM, same as audio). The existing VDO.Ninja path DOES expose IP because VDO.Ninja is WebRTC peer-to-peer. Both modals coexist (§10); each owns its scope.

---

## 14. References

### Spike artifacts
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-CONTEXT.md` (locked decisions §3)
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-RESEARCH.md` (§5 macOS Hardened Runtime, §6 integration points, §7 pitfalls)
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-spike-results.md` (measured numbers, Spike Risk #2, openDevice-returns-non-null-on-TCC-denial)
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` (Item C JUCE CameraDevice integration; Item G partial entitlements + plugin plumbing; Q1 JUCE seat licence)
- `.planning/phases/14.3-native-video-foundation/14.3-SPEC.md` (substrate spec; Phase 19's camera output integrates with Phase 20's RawDataSendBegin/Write API)

### Phase 19 CONTEXT
- `.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md` — 30 locked decisions D-01 through D-30 (referenced throughout this document)

### Requirements + state
- `.planning/REQUIREMENTS.md:52-54` (CAM-01/02/03), `.planning/REQUIREMENTS.md:80-82` (PKG-04)
- `.planning/STATE.md` (v1.3 roadmap PASS 3 final, 6 phases / 16 plans / 28 requirements)
- `.planning/ROADMAP.md:296,305-318` (Phase 19 success criteria; cross-platform backends note)

### JamWide source code (read, not modified)
- `juce/JamWideJuceProcessor.h:88` — `currentStateVersion = 3` (bump to 4 in Phase 19, per D-24)
- `juce/JamWideJuceProcessor.h:119` — `std::unique_ptr<jamwide::VideoCompanion> videoCompanion;` (kept; new camera components live alongside)
- `juce/JamWideJuceProcessor.cpp:61-65` — constructor init (videoCompanion + OscServer + MidiMapper); add camera components here
- `juce/JamWideJuceProcessor.cpp:67-76` — destructor cleanup (LIFO order)
- `juce/JamWideJuceProcessor.cpp:622-672` — `getStateInformation` (add `<camera>` properties per D-25)
- `juce/JamWideJuceProcessor.cpp:674-...` — `setStateInformation` (add camera reads with defaults)
- `juce/ui/ConnectionBar.cpp:206-217` — existing Video button setup (Camera button follows same pattern)
- `juce/ui/ConnectionBar.cpp:512-528` — videoCompanion->isActive() pattern (Camera button follows same)
- `juce/ui/ConnectionBar.cpp:644-650` — `setVideoActive(bool)` (Camera button gets matching `setCameraActive(bool)`)
- `juce/JamWideJuceEditor.cpp:127-144` — onVideoClicked lambda (Camera button gets onCameraClicked)
- `juce/video/VideoPrivacyDialog.h` — existing privacy modal pattern (new native-camera modal mirrors shape, different copy)
- `juce/video/VideoCompanion.h:77` — `isActive()` (used for VDO.Ninja coexistence detection, §10)
- `juce/ui/JamWideLookAndFeel.h:12-26` — color constants (kSurfaceStrip, kTextPrimary, kAccentConnect — used by popout chrome per D-08)
- `JamWide.entitlements:1-12` — current entitlements (add `com.apple.security.device.camera` per §7)
- `CMakeLists.txt:151-168` — `juce_add_plugin` block (add `CAMERA_PERMISSION_ENABLED TRUE` + `CAMERA_PERMISSION_TEXT` per §7)
- `CMakeLists.txt:200-205` — BrowserDetect macOS/Windows source-dispatch pattern (mirror for CameraAuthorization)
- `CMakeLists.txt:250-275` — codesign block (entitlements file already plumbed)
- `CMakeLists.txt:406-...` — `JAMWIDE_BUILD_TESTS` block (new camera unit tests slot here)

### JUCE source code (read for API contracts)
- `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:64-113` — openDevice / openDeviceAsync API
- `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:200-204` — `Listener::imageReceived` "any thread" contract (D-02 design driver)
- `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:117` — `getName()` for device titlebar
- `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:38` — `JUCE_USE_CAMERA` macro guard (Risk E)
- `libs/juce/modules/juce_video/native/juce_CameraDevice_mac.h:230-340` — `ImageOutputBase` / `PostCatalinaPhotoOutput` (still-photo polling architecture; Risk H)
- `libs/juce/modules/juce_video/native/juce_CameraDevice_mac.h:316-319` — macOS image-decode path (JPEG via `ImageFileFormat::loadFrom`)
- `libs/juce/modules/juce_video/native/juce_CameraDevice_mac.h:519-538` — `handleImageCapture` / `triggerImageCapture` loop
- `libs/juce/modules/juce_video/native/juce_CameraDevice_windows.h:35-200` — DirectShow filter graph (Risk G — no Media Foundation)
- `libs/juce/modules/juce_video/native/juce_CameraDevice_windows.h:316` — `Image::RGB` (24-bit packed)
- `libs/juce/extras/Build/juce_build_tools/utils/juce_PlistOptions.cpp:149-150` — `NSCameraUsageDescription` generation
- `libs/juce/extras/Build/CMake/JUCEUtils.cmake:367-368` — `CAMERA_PERMISSION_ENABLED` / `CAMERA_PERMISSION_TEXT` target properties

### External (web sources)
- [Apple — authorizationStatus(for:)](https://developer.apple.com/documentation/avfoundation/avcapturedevice/1624613-authorizationstatusformediatype) — AVCaptureDevice TCC pre-check
- [Apple — requestAccess(for:completionHandler:)](https://developer.apple.com/documentation/avfoundation/avcapturedevice/1624584-requestaccessformediatype) — request access
- [SPARTA Issue #82](https://github.com/leomccormack/SPARTA/issues/82) — REAPER macOS camera-permission documented case (HostLacksEntitlement cause)
- [Microsoft — Handle the Windows camera privacy setting](https://learn.microsoft.com/en-us/windows/apps/develop/camera/camera-privacy-setting) — E_ACCESSDENIED + `ms-settings:privacy-webcam`
- [JUCE — DocumentWindow Class Reference](https://docs.juce.com/master/classDocumentWindow.html) — popout chrome customization
- [JUCE forum — Camera support](https://forum.juce.com/t/new-feature-camera-support-for-ios-and-android/27409) — camera enablement context

### Memory references
- `project_apple_signing` — Team ID T3KK66Q67T, notarization via API Key (D-28 verification)
- `feedback_ui_preferences` — VB-Audio Voicemeeter Banana dark theme (D-08 popout chrome)
- `feedback_uat_scope_redflags` — no skipping CAM-01/02/03 in UAT (cited in §11 / planner enforcement)
- `project_jamtaba_video_port` — milestone-level context, supersedes `project_vdoninja_interop`

---

## Metadata

**Confidence breakdown:**
- JUCE CameraDevice API contracts: HIGH — read JUCE source verbatim
- macOS TCC pre-check pattern: HIGH — Apple API reference + spike confirmation of the failure mode
- Windows backend behaviour: MEDIUM-HIGH — JUCE source read + Microsoft docs (no Windows hardware test in this research session)
- Frame distributor architecture: HIGH — design follows established Phase 15.1 patterns + JUCE Listener idiom
- State machine + retry-backoff: HIGH — transitions enumerated explicitly; backoff matches D-20 verbatim
- File layout / integration points: HIGH — every line citation re-verified during this session
- Entitlements + Info.plist: HIGH — JUCE CMake parameters verified at source
- Plugin state v3→v4: HIGH — followed existing OSC v1→v2 migration pattern
- Fallback dialog cause detection: MEDIUM — heuristic for TCCDenied vs HostLacksEntitlement is approximate
- UAT coverage: HIGH — explicit row per success criterion; `feedback_uat_scope_redflags` enforced

**Research date:** 2026-05-16
**Valid until:** 2026-06-16 (JUCE 8 API stable; macOS TCC behaviour stable; Windows privacy settings stable. If JamWide moves to JUCE 9 before then, re-verify the `juce_video` API surface.)
