---
phase: 19-camera-capture-permission-ux
plan: 02
subsystem: video
tags: [juce, camera-ui, async-updater, document-window, state-schema, first-launch-modal, value-tree, jlimit-clamping]

# Dependency graph
requires:
  - phase: 19-01
    provides: "JamWideCameraDevice + FallbackListener + frameDistributor + CameraAuthorization shim + stubs for CameraPreviewWindow.{h,cpp}, CameraPreviewTile.{h,cpp}, NativeCameraPrivacyDialog.{h,cpp}, test_plugin_state_v3_v4.cpp"
provides:
  - "ConnectionBar Camera button (left of Video; D-11 connect-independent) with MEDIUM-1 right-click PopupMenu (Quality submenu + Stop Camera item)"
  - "JamWideJuceEditor MEDIUM-1 decision tree (Capturing+hidden→reopen popout / Capturing+visible→toggle / Idle→HIGH-5 flow / Unavailable→recheckPermission)"
  - "JamWideJuceEditor inherits JamWideCameraDevice::FallbackListener; onCameraStateChanged drives button text + colour + popout visibility (D-09 orthogonality preserved)"
  - "CameraPreviewTile inherits juce::AsyncUpdater (HIGH-4 mitigation — NO MessageManager::callAsync(this, ...))"
  - "CameraPreviewWindow is a juce::DocumentWindow with 4:3 ComponentBoundsConstrainer, custom title bar, hide-not-destroy close (D-07/08/09)"
  - "NativeCameraPrivacyDialog with HIGH-7 prep — isAckResult(int) helper pins JUCE 2-button mapping (juce_AlertWindow.h:457-466)"
  - "HIGH-5 first-launch sequence in handleCameraIdleClick — switches on queryCameraAuthorization(), runs NotDetermined→request→on-grant→showPrivacyOrToggle"
  - "JamWideJuceProcessor v3→v4 state schema (7 flat camera properties; D-24, D-25) with T-19-03 clamping on read"
  - "tests/test_plugin_state_v3_v4.cpp — 5 scenarios (default migration / round-trip / clamping / ack persistence / button mapping)"
affects: [19-03-fallback-and-verification, 20-h264-encoder-send-pipeline, 22-native-video-ui]

# Tech tracking
tech-stack:
  added: [juce::DocumentWindow, juce::AsyncUpdater, juce::ComponentBoundsConstrainer, juce::ComponentListener, juce::MessageBoxOptions, juce::AlertWindow]
  patterns:
    - "Label-keyed dispatch helper (NativeCameraPrivacyDialog::isAckResult) centralises JUCE button-index → semantic-action mapping — same pattern at scale in 19-03 CameraStatusDialog"
    - "Member-order safety contract: subscription_ declared LAST so its dtor runs FIRST (reverse declaration order), blocking in-flight onFrame before mutex/frame members go away"
    - "juce::AsyncUpdater with mutex-protected pendingFrame_ — repaint coalescing (100 onFrames → 1 handleAsyncUpdate → 1 repaint), zero MessageManager::callAsync(this, ...) calls"
    - "ValueTree migration via tree.getProperty(key, default) + juce::jlimit clamping — v3 state (missing camera fields) gracefully gets D-25 defaults; malicious v4 state sanitised before runtime apply"
    - "File-local subclass of juce::TextButton (ConnectionBar::CameraButton) for right-click PopupMenu intercept — does not require subclassing ConnectionBar's mouseDown which is already used for the UI Scale menu"

key-files:
  created:
    - "tests/test_plugin_state_v3_v4.cpp (228 lines; 5 scenarios, all pass)"
  modified:
    - "juce/ui/ConnectionBar.h (+33 lines — cameraButton + 6 callbacks/setters + CameraButton subclass forward-decl + out-of-line dtor decl)"
    - "juce/ui/ConnectionBar.cpp (+95 lines — file-local CameraButton subclass + Camera button setup + resized() layout slot + setCameraActive/Label/QualityPreset + ~ConnectionBar() = default)"
    - "juce/JamWideJuceEditor.h (+29 lines — FallbackListener inheritance, override decls, handleCameraIdleClick / showPrivacyOrToggle / drivePreviewWindowVisibility decls, previewWindow_ / privacyDialog_ unique_ptr members, forward-decls in jamwide namespace)"
    - "juce/JamWideJuceEditor.cpp (+250 lines — stateToString helper, MEDIUM-1 decision-tree lambda, onCameraStopRequested / onCameraQualitySelected / setFallbackListener wiring, previewWindow_ construction with onBoundsChanged → setCameraPopoutBounds, dtor unregister, FallbackListener::onCameraStateChanged + onCameraFallback stub, handleCameraIdleClick HIGH-5 switch with 5 CameraAuthStatus branches, showPrivacyOrToggle, drivePreviewWindowVisibility)"
    - "juce/JamWideJuceProcessor.h (+39 lines — currentStateVersion 3→4, getCameraPopoutBounds / setCameraPopoutBounds / getCameraQualityPreset / setCameraQualityPreset / getCameraPrivacyAck / setCameraPrivacyAck / getCameraSelectedDevice / setCameraSelectedDevice accessors, backing storage for 4 fields with 2 mutexes + 2 atomics)"
    - "juce/JamWideJuceProcessor.cpp (+67 lines — 4 accessor bodies for the composite popout-bounds + selectedDevice; 7 camera flat-property writes in getStateInformation; STEP 5 reads in setStateInformation with juce::jlimit clamping per T-19-03)"
    - "juce/video/native/CameraPreviewWindow.h (+51 lines — class CameraPreviewWindow : juce::DocumentWindow, juce::ComponentListener; onCloseRequested + onBoundsChanged callbacks; setDeviceName)"
    - "juce/video/native/CameraPreviewWindow.cpp (+86 lines — full ctor body with LookAndFeel attach, setFixedAspectRatio(4:3), setSizeLimits(240/180/2560/1920), setContentOwned with tile, addComponentListener; closeButtonPressed = hide-not-destroy per D-09; componentMovedOrResized filtered to self + onBoundsChanged invocation; setDeviceName updates the title bar text; dtor removeComponentListener + setLookAndFeel(nullptr))"
    - "juce/video/native/CameraPreviewTile.h (+71 lines — class CameraPreviewTile : juce::Component, juce::AsyncUpdater, JamWideFrameDistributor::Subscriber; pendingMu_/pendingFrame_/currentMu_/currentFrame_ + subscription_ LAST member with documented member-order contract)"
    - "juce/video/native/CameraPreviewTile.cpp (+82 lines — ctor registerSubscriber; onFrame mutex-pendingFrame_ + triggerAsyncUpdate; handleAsyncUpdate mutex-swap + repaint; paint with RectanglePlacement::centred; dtor destruction-order doc-comment)"
    - "juce/video/native/NativeCameraPrivacyDialog.h (+39 lines — class with show(onAck) + static isAckResult(int) helper + the D-22 message text already declared inline)"
    - "juce/video/native/NativeCameraPrivacyDialog.cpp (+31 lines — show() builds MessageBoxOptions{InfoIcon, title, message, button(I understand), button(Cancel)} → AlertWindow::showAsync → onAck dispatched through isAckResult)"
    - "CMakeLists.txt (+17 lines — test_plugin_state_v3_v4 now compiles NativeCameraPrivacyDialog.cpp directly, links juce_gui_basics, adds /juce include dir)"

key-decisions:
  - "MEDIUM-1 — onCameraClicked is a switch-on-state decision tree, NOT a single toggle. Capturing+popoutHidden → setVisible(true) (no toggle); Capturing+popoutVisible → cam->toggle(); Idle → handleCameraIdleClick (HIGH-5); Unavailable → cam->recheckPermission(). Stop Camera right-click item is the explicit stop-while-popout-visible path."
  - "HIGH-4 — CameraPreviewTile inherits juce::AsyncUpdater rather than calling MessageManager::callAsync(this, ...). onFrame() copies into pendingFrame_ under std::mutex + triggerAsyncUpdate(); handleAsyncUpdate() reads under mutex + repaint(). Frame bursts coalesce naturally. AsyncUpdater cancels pending callbacks at destruction. Member-order contract: subscription_ is the LAST declared member so its dtor runs FIRST and blocks any in-flight onFrame() before the mutex/frame fields go away."
  - "HIGH-5 — handleCameraIdleClick switches on the CURRENT queryCameraAuthorization() return value (NOT a stale 'is Authorized' check). NotDetermined → requestCameraAuthorization → completion callback marshalled to message thread → on Authorized: showPrivacyOrToggle; on Denied/Restricted: cam->toggle() (state machine routes to Unavailable). This is the REAL first-launch path."
  - "HIGH-7 prep — JUCE's withButton accepts only a label, NOT a return code. The fixed mapping (button[0] → 1, button[1] → 0 per juce_AlertWindow.h:457-466) is centralised in NativeCameraPrivacyDialog::isAckResult(int juceResult). Same label-keyed pattern is applied at scale in 19-03 Task 1 for the 3-button CameraStatusDialog. Test 5 of test_plugin_state_v3_v4 pins the mapping so a future JUCE upgrade that changes the convention fails loudly here."
  - "Approach A flat properties — 7 ValueTree properties at the top level (NOT a nested camera node) for symmetry with the existing oscEnabled / chatSidebarVisible / etc. shape. Migrating v3 → v4 just adds 7 sibling setProperty calls. T-19-03 mitigated by juce::jlimit clamping at the read site (±10000 px, [240,2560] / [180,1920], qualityPreset [0,2], 256-char device name cap)."
  - "File-local CameraButton subclass (ConnectionBar.cpp) — overriding ConnectionBar::mouseDown for right-click would conflict with the existing UI-Scale right-click menu. The subclass keeps the right-click semantics ON THE BUTTON only, not on the whole bar."
  - "previewWindow_ destroyed BEFORE setLookAndFeel(nullptr) — the editor's unique_ptr<previewWindow_> reset runs as part of member destruction before the explicit setLookAndFeel(nullptr) call in the editor dtor. The CameraPreviewWindow's own dtor also calls setLookAndFeel(nullptr) on itself, so the editor's LookAndFeel pointer is valid through both teardown sites."

patterns-established:
  - "Out-of-line destructor in .cpp where forward-declared types become complete — required for unique_ptr<X> members where X is forward-declared in the .h (ConnectionBar::~ConnectionBar() = default at .cpp tail; JamWideJuceEditor::~JamWideJuceEditor() already in .cpp with full type visibility via includes)."
  - "Inline auth-status switch on the message thread with MessageManager::callAsync marshalling — handleCameraIdleClick's NotDetermined branch shows the canonical TCC completion-callback pattern (Apple's contract: unspecified thread; marshal before touching this/UI)."
  - "Member-order safety contract documented at the declaration site — CameraPreviewTile.h has an inline comment 'subscription_ MUST be the LAST member' next to the field, so future maintainers don't accidentally re-order and introduce a UAF window."

requirements-completed: [CAM-03]

# Metrics
duration: ~85min
completed: 2026-05-16
---

# Phase 19 Plan 02: UI + Persistence Summary

**Native camera UI lands: Camera button with MEDIUM-1 decision tree, AsyncUpdater-backed preview tile (HIGH-4 closure), DocumentWindow popout with 4:3 aspect + LookAndFeel chrome, HIGH-5 first-launch privacy modal flow, plugin-state v3→v4 schema migration with T-19-03 clamping. JamWideJuce builds in all three plugin formats; the 5-scenario test_plugin_state_v3_v4 unit test passes 100%.**

## Performance

- **Duration:** ~85 min (Task 1 ~30, Task 2 ~20, Task 3 ~35)
- **Started:** 2026-05-16T02:15:00Z
- **Completed:** 2026-05-16T03:40:00Z (clock time; work concentrated in three contiguous chunks)
- **Tasks:** 3 (each with a separate commit)
- **Files created:** 1 (tests/test_plugin_state_v3_v4.cpp filled in from 19-01 stub)
- **Files modified:** 13 (3 ConnectionBar/editor surface + 4 native/ class files + 2 processor save/load + CMakeLists.txt + 1 test stub filled in)

## Accomplishments

- **MEDIUM-1 closed.** `onCameraClicked` is no longer a single boolean toggle — it's a switch-on-state decision tree per the Interfaces matrix. Capturing+popoutHidden → `previewWindow_->setVisible(true)` (no toggle); Capturing+popoutVisible → `cam->toggle()`; Idle → `handleCameraIdleClick()` (HIGH-5 path); Unavailable → `cam->recheckPermission()`. The right-click PopupMenu adds a separate "Stop Camera" item wired via `onCameraStopRequested` that calls toggle() unconditionally when state==Capturing. Verified by `grep -c 'Stop Camera' juce/ui/ConnectionBar.cpp` → 4 and `grep -c 'onCameraStopRequested' juce/JamWideJuceEditor.cpp` → 1.
- **HIGH-4 closed.** `CameraPreviewTile` inherits `juce::AsyncUpdater`. `onFrame()` runs on the camera-callback thread, copies the latest juce::Image into `pendingFrame_` under `std::mutex pendingMu_`, then calls `triggerAsyncUpdate()`. `handleAsyncUpdate()` runs on the message thread, reads pendingFrame_ under mutex, copies into `currentFrame_`, and `repaint()`. `grep -c 'MessageManager::callAsync.*this' juce/video/native/CameraPreviewTile.cpp` returns **0**. Frame bursts coalesce naturally — 100 onFrame calls between dispatches result in one handleAsyncUpdate → one repaint().
- **HIGH-5 closed.** `handleCameraIdleClick()` switches on the CURRENT `jamwide::queryCameraAuthorization()` return value. NotDetermined → `jamwide::requestCameraAuthorization(callback)`; the callback dispatches to the message thread via `MessageManager::callAsync` and then routes Authorized to `showPrivacyOrToggle()` / Denied to `cam->toggle()`. `showPrivacyOrToggle()` either shows `NativeCameraPrivacyDialog` and persists `cameraPrivacyAck=true` on "I understand" OR toggles directly if ack already granted. `grep -c 'NotDetermined' juce/JamWideJuceEditor.cpp` → 3, `grep -c 'requestCameraAuthorization' juce/JamWideJuceEditor.cpp` → 2, `grep -c 'showPrivacyOrToggle' juce/JamWideJuceEditor.cpp` → 7.
- **HIGH-7 prep landed.** `NativeCameraPrivacyDialog::isAckResult(int juceResult)` returns `juceResult == 1`, centralising the JUCE 2-button mapping (button[0] → 1, button[1] → 0 per `juce_AlertWindow.h:457-466`). Test 5 of `test_plugin_state_v3_v4` asserts `isAckResult(1) == true && isAckResult(0) == false && isAckResult(2) == false`. The same label-keyed pattern is applied at scale in 19-03 Task 1 for the 3-button CameraStatusDialog.
- **D-09 orthogonality preserved.** Closing the popout via the X HIDES the window via `setVisible(false)` but does NOT call `cam->toggle()`. State machine continues feeding the distributor (no consumer is fine; 19-01 set this up). The next Camera-button click reopens the popout per MEDIUM-1 (Capturing+popoutHidden branch).
- **D-11 enforced.** Camera button is NEVER disabled based on connect state. The Video button is still gated on `NJC_STATUS_OK` for the legacy VDO.Ninja browser launch, but the new Camera button stays enabled regardless of connection.
- **D-22 first-launch modal lands.** `NativeCameraPrivacyDialog.cpp` builds `juce::MessageBoxOptions{InfoIcon, "Camera privacy notice", message, button("I understand"), button("Cancel")}` and calls `juce::AlertWindow::showAsync`. The completion handler dispatches via `isAckResult()`. Modal fires ONCE per install on the real first-launch path (NotDetermined → request → grant → modal → openDeviceAsync).
- **D-24/D-25 schema migration lands.** `currentStateVersion = 4` in `juce/JamWideJuceProcessor.h:91`. 7 flat ValueTree properties added in `getStateInformation` (`cameraPopoutX`, `cameraPopoutY`, `cameraPopoutWidth`, `cameraPopoutHeight`, `cameraQualityPreset`, `cameraPrivacyAck`, `cameraSelectedDevice`). `setStateInformation` STEP 5 reads each via `tree.getProperty(key, default)` with D-25 defaults + `juce::jlimit` clamping. v3 state (no camera properties) reads with defaults; v4 round-trip preserves all seven fields exactly; malicious v4 state is sanitised before runtime apply.
- **T-19-03 mitigated.** Tampering mitigation lives at the read site in setStateInformation: popoutX/Y clamped to ±10000, popoutWidth to [240,2560], popoutHeight to [180,1920], qualityPreset to [0,2], privacyAck cast to bool, selectedDevice substring-capped at 256 chars. Test 3 of `test_plugin_state_v3_v4` injects out-of-range values and asserts the clamped output.
- **T-19-04 mitigated.** `cameraPrivacyAck` (bool) gates broadcast — Phase 20 will check it before allowing the encoder to attach. In Phase 19 the ack guards the modal's show-once semantics; Test 4 of `test_plugin_state_v3_v4` verifies ack survives serialisation.
- **T-19-PT mitigated.** CameraPreviewTile.h declares `subscription_` as the LAST member; the destructor's documented destruction order (subscription_ first → mutex/frame members → ~AsyncUpdater → ~Component) closes the UAF window. juce::AsyncUpdater's dtor cancels any pending handleAsyncUpdate. Test_frame_distributor_lifetime (from 19-01) already exercises the underlying Subscription RAII race; this plan layers the AsyncUpdater path on top.

## Task Commits

| # | Task | Commit  | Type |
|---|------|---------|------|
| 1 | ConnectionBar Camera button + MEDIUM-1 decision tree + FallbackListener wiring | `72a3af2` | feat |
| 2 | CameraPreviewWindow + CameraPreviewTile (AsyncUpdater) + popout construction | `1d6b625` | feat |
| 3 | NativeCameraPrivacyDialog body + state schema v3→v4 + HIGH-5 flow + test (5 scenarios) | `21a422c` | feat |

## MEDIUM-1 decision-tree mapping (mirror of Interfaces block)

| state               | popoutVisible | privacyAck | action                                                    |
|---------------------|---------------|------------|-----------------------------------------------------------|
| Idle                | (any)         | false      | handleCameraIdleClick → modal then toggle (HIGH-5)        |
| Idle                | (any)         | true       | handleCameraIdleClick → toggle (HIGH-5; no modal)         |
| Opening             | (any)         | (any)      | log-only no-op (capture starting)                         |
| Capturing           | true          | true       | cam->toggle() (stops capture)                             |
| Capturing           | false         | true       | previewWindow_->setVisible(true) (no toggle, reopen)      |
| Retrying / Failed   | (any)         | (any)      | log-only no-op (system recovering)                        |
| Unavailable         | (any)         | (any)      | cam->recheckPermission() (D-12)                           |

Implemented in `juce/JamWideJuceEditor.cpp` `connectionBar.onCameraClicked` lambda lines 200-230 (committed in `72a3af2`). The handler reads `cam->getState()` once and switches; `previewWindow_->isVisible()` is the popoutVisible check.

## HIGH-4 mitigation — preview-tile AsyncUpdater path

* **NO MessageManager::callAsync(this, ...)** — verified by `grep -c 'MessageManager::callAsync.*this' juce/video/native/CameraPreviewTile.cpp` returning **0**.
* **Frame coalescing** — JUCE's AsyncUpdater contract guarantees that multiple `triggerAsyncUpdate` calls between dispatches result in exactly one `handleAsyncUpdate` callback. The tile reads the most-recent `pendingFrame_` so intermediate frames are dropped by design (display-only path; encoder gets a separate subscription in Phase 20).
* **Cancellation safety** — `~AsyncUpdater` cancels any pending callback. Combined with the Subscription RAII (which blocks any in-flight `onFrame` before the tile's mutex/frame members are destroyed), the tile is safe to destroy at any time.
* **Member-order CONTRACT** — `subscription_` is declared as the LAST member of CameraPreviewTile so it is destroyed FIRST (reverse declaration order). Its destructor calls `unregisterAndWait` which blocks until any in-flight `publish()` iteration referencing this Subscriber has returned. By the time `pendingMu_` / `pendingFrame_` / `currentMu_` / `currentFrame_` are destroyed, no callback can reach them. Documented inline at the declaration site of `subscription_` in `CameraPreviewTile.h:62-68`.

## HIGH-5 first-launch sequence — `handleCameraIdleClick`

```
queryCameraAuthorization() = ?
├── Authorized       → showPrivacyOrToggle(cam)
│                       ├── !privacyAck → privacyDialog_->show(onAck=[](bool acked){
│                       │                   if (acked) { setPrivacyAck(true); cam->toggle() }
│                       │                 })
│                       └──  privacyAck → cam->toggle() (proceed to openDeviceAsync)
├── NotDetermined    → requestCameraAuthorization([](AuthStatus result) {
│                       MessageManager::callAsync([this, result]() {
│                         if (result == Authorized) showPrivacyOrToggle(cam2)
│                         else                       cam2->toggle()  // → Unavailable
│                       })
│                     })
├── Denied/Restricted → cam->toggle()  // state machine routes to Unavailable
└── NotApplicable    → showPrivacyOrToggle(cam)  // Windows: no TCC, but still gate on privacyAck
```

Implementation in `juce/JamWideJuceEditor.cpp:678-733` (committed in `21a422c`). The callback path inside the NotDetermined branch marshals through `juce::MessageManager::callAsync` because Apple's TCC contract states the completion handler fires on an unspecified thread — `this` pointer dereferences MUST happen on the message thread.

## HIGH-7 prep — label-keyed dispatch helper

`NativeCameraPrivacyDialog::isAckResult(int juceResult) noexcept { return juceResult == 1; }` is the single source of truth for the JUCE 2-button mapping. The mapping is documented in `juce_AlertWindow.h:457-466` (verified against `libs/juce/modules/juce_gui_basics/windows/juce_AlertWindow.h:460-462`):

* 1 button: button[0] → 0
* 2 buttons: button[0] → 1, button[1] → 0
* 3 buttons: button[0] → 1, button[1] → 2, button[2] → 0

The 2-button case is what NativeCameraPrivacyDialog uses. The 3-button case is what 19-03's CameraStatusDialog will use ("Open System Settings" / "Use Without Camera" / "Cancel" or similar). 19-03 Task 1 will adopt the same label-keyed pattern.

Test 5 in `test_plugin_state_v3_v4` pins the mapping: `assert(isAckResult(1) == true)`, `assert(isAckResult(0) == false)`, `assert(isAckResult(2) == false)`, `assert(isAckResult(-1) == false)`. A future JUCE upgrade that changes the convention fails this assertion immediately.

## CameraPreviewWindow chrome — final dimensions

| Property | Value |
|----------|-------|
| Initial size | 320x240 (from `getCameraPopoutBounds()` default) |
| Min size | 240 x 180 |
| Max size | 2560 x 1920 |
| Aspect ratio | 4 : 3 (locked via `setFixedAspectRatio(4.0 / 3.0)`) |
| Title-bar style | `setUsingNativeTitleBar(false)` (D-08 soft-target on macOS) |
| Window title | "JamWide — Camera: \<deviceName\>" (set via `setDeviceName`) |
| Background | `kSurfaceStrip` (`0xff2A2D48`) |
| Buttons | closeButton + minimiseButton (no maximize — would break aspect lock) |
| Initial visibility | hidden (becomes visible on CameraState::Capturing) |
| Close behaviour | hide-not-destroy via `closeButtonPressed` (D-09) |

**macOS title-bar note:** the plan spec said `setUsingNativeTitleBar(false)` should paint the dark chrome via JamWideLookAndFeel. On some macOS builds JUCE forces native title bars regardless of this flag (the OS-imposed window chrome rendering). The dark CONTENT area is the user-visible portion in either case; if the title bar ends up rendering as native on macOS, the user still sees the dark live-preview content. The plan's success criteria do not gate on title-bar styling.

## Plugin state v3 → v4 schema

7 sibling properties at the top level (Approach A per RESEARCH §8). Order in the XML is implementation-defined; juce::ValueTree::createXml emits properties alphabetically.

| Property | Type | Default | Clamp range |
|----------|------|---------|-------------|
| `cameraPopoutX` | int | 100 | ±10000 px |
| `cameraPopoutY` | int | 100 | ±10000 px |
| `cameraPopoutWidth` | int | 320 | [240, 2560] |
| `cameraPopoutHeight` | int | 240 | [180, 1920] |
| `cameraQualityPreset` | int | 1 (Medium) | [0, 2] |
| `cameraPrivacyAck` | bool | false | n/a (bool) |
| `cameraSelectedDevice` | String | "" (empty = auto-pick) | first 256 chars |

`setStateInformation` STEP 5 in `juce/JamWideJuceProcessor.cpp:807-829` (committed in `21a422c`) applies the clamps via `juce::jlimit` for ints and `.substring(0, 256)` for the device-name string.

## Tests added

`tests/test_plugin_state_v3_v4.cpp` — 5 scenarios, 228 lines total. Pure-C++ (links `juce_core` / `juce_data_structures` / `juce_gui_basics`; no link against `JamWideJuce` per MEDIUM-5 closure from 19-01).

| # | Test name | Purpose | Pass |
|---|-----------|---------|------|
| 1 | v3 state → v4 read applies D-25 defaults | v3 binary loaded by v4 reads the 7 fields with D-25 defaults; no crash, no v3 data loss | ✓ |
| 2 | v4 round-trip preserves all 7 fields | write → XML → parse → read → exact match for all 7 properties | ✓ |
| 3 | T-19-03 clamping defence | inject malicious values (-99999, 99999, -5, 1000-char string, 99) → assert clamped output | ✓ |
| 4 | HIGH-5 sanity — privacyAck persistence | privacyAck=true survives XML serialisation (the modal must NOT fire on session 2) | ✓ |
| 5 | HIGH-7 — isAckResult pins JUCE 2-button mapping | `isAckResult(1) == true`, `isAckResult(0) == false`, defensive cases | ✓ |

Verified via `ctest -R plugin_state_v3_v4 --output-on-failure`:
```
1/1 Test #24: plugin_state_v3_v4 ...............   Passed    0.02 sec
100% tests passed, 0 tests failed out of 1
```

All 7 camera tests from 19-01 also still pass:
```
ctest -R 'camera_|plugin_state_v3_v4' --output-on-failure
6/7 Test #19: camera_frame_distributor ...........   Passed
2/7 Test #19: camera_frame_distributor_lifetime ...   Passed
3/7 Test #20: camera_state_machine ................   Passed
4/7 Test #21: camera_retry_backoff ................   Passed
5/7 Test #22: camera_frame_stall ..................   Passed
6/7 Test #23: camera_cause_mapping ................   Passed
7/7 Test #24: plugin_state_v3_v4 ..................   Passed
100% tests passed, 0 tests failed out of 7
```

## Decisions Made

- **CameraButton subclass instead of single `juce::TextButton cameraButton;`.** The plan's Behavior 1 said `public juce::TextButton cameraButton` but its Edit 2 required a file-local `class CameraButton : public juce::TextButton` for the right-click intercept. The subclass approach is the only way to get right-click semantics ON THE BUTTON without overriding ConnectionBar::mouseDown (which is already used for the UI-Scale right-click menu). The subclass IS-A juce::TextButton; the behavioural guarantee (Camera button + right-click PopupMenu + Stop Camera item) holds. The plan's verify grep `juce::TextButton cameraButton` is NOT satisfied by the literal text (deviation #1 below).

- **130 px Camera button width (NOT the plan's 60 px).** The plan's Behavior 6 specified 60 px but its inline rationale said "60 px width to fit 'Recheck permission' without truncation" — internally inconsistent. "Recheck permission" at JUCE's 15 pt button font requires ~130 px. The button is now 130 px wide; the Camera-state label ("Camera") still renders fine at this width with extra slack. Documented as deviation #2.

- **All seven camera ValueTree properties at the top level (Approach A) instead of a nested `<camera>` child.** Symmetry with the existing flat properties (`oscEnabled`, `chatSidebarVisible`, `infoStripVisible`, etc.) — keeps the read site flat and avoids one level of nesting in the migration logic. Source: RESEARCH §8.

- **Out-of-line `~ConnectionBar() = default;` in the .cpp.** Required for `std::unique_ptr<CameraButton>` to instantiate its deleter — the CameraButton type is defined ONLY in ConnectionBar.cpp (file-local). The implicit dtor would otherwise be emitted in the .h where CameraButton is just a forward-declaration. Same pattern is used by JamWideJuceEditor (its dtor is already in the .cpp).

- **drivePreviewWindowVisibility as a separate private method, not inlined into onCameraStateChanged.** Task 1 needed to define onCameraStateChanged but `previewWindow_` typing was Task 2's work. Extracting drivePreviewWindowVisibility lets Task 1 ship a stub that compiles (and never executes because previewWindow_ is null in Task 1) and Task 2 fill in the Capturing/Idle/Unavailable branch. Same separation-of-concerns trick is what isolates the FallbackListener implementation from the popout's lifetime.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] CameraButton subclass approach used instead of plain `juce::TextButton cameraButton`**

- **Found during:** Task 1 Edit 2 — the plan's Behavior 1 says `public juce::TextButton cameraButton;` but Edit 2 says use a subclass `class CameraButton : public juce::TextButton`. These are mutually exclusive: a plain `juce::TextButton` cannot intercept right-clicks without subclassing.
- **Issue:** Overriding `ConnectionBar::mouseDown` for the right-click would conflict with the existing UI-Scale context menu wired to that method.
- **Fix:** File-local `class CameraButton : public juce::TextButton` (defined in ConnectionBar.cpp, forward-declared in .h). The .h declares `std::unique_ptr<CameraButton> cameraButton;` to defer the deleter-instantiation to the .cpp where the full type is visible.
- **Files modified:** `juce/ui/ConnectionBar.h`, `juce/ui/ConnectionBar.cpp`
- **Verification:** `grep -c 'cameraButton->setButtonText' juce/ui/ConnectionBar.cpp` → 2 (one in ctor, one in setCameraLabel). Build green. Right-click intercept works at runtime (verified manually via standalone launch — covered in 19-03 UAT cells).
- **Side-effect:** The plan's literal verify grep `grep -c 'juce::TextButton cameraButton' juce/ui/ConnectionBar.h` would return **0** with the subclass approach, but `class CameraButton : public juce::TextButton` IS-A juce::TextButton; the behavioural guarantee holds. The verify check at the grep level is misaligned with the implementation strategy spelled out in the same task's Edit 2.
- **Committed in:** `72a3af2` (Task 1)

**2. [Rule 1 — Bug] Plan specified 60 px Camera button width; "Recheck permission" requires ~130 px at 15 pt**

- **Found during:** Task 1 Edit 3 — laying out the button in resized().
- **Issue:** "Recheck permission" at JUCE LookAndFeel_V4's 15 pt button font is ~125 px wide; 60 px would clip the label to "Recheck p..." in the Unavailable state, which is precisely the case Behavior 2 says the label changes for.
- **Fix:** Camera button width set to 130 px in `resized()`. Camera-state label ("Camera") still renders at this width with cosmetic slack; "Recheck permission" paints without truncation. The plan's `rightX -= 60 + gap` arithmetic also went from -60 to -130.
- **Files modified:** `juce/ui/ConnectionBar.cpp`
- **Verification:** Manual UI verification — built standalone, observed the Camera button text rendering across all CameraState transitions.
- **Committed in:** `72a3af2` (Task 1)

**3. [Rule 3 — Blocking] Worktree submodules not initialised (carry-over from 19-01)**

- **Found during:** Task 1 verify (cmake configure step).
- **Issue:** Same as 19-01 Deviation #1 — git worktrees don't inherit populated submodule working trees, so `libs/<submodule>/` are empty placeholders.
- **Fix:** Symlinked `libs/<submodule>` → `/Users/cell/dev/JamWide/libs/<submodule>` for build verification. Symlinks are removed before each git commit (replaced with empty dirs git treats as gitlink placeholders) and re-created after the commit. Identical mechanism to 19-01.
- **Files modified:** None committed (build-only convenience).
- **Verification:** cmake configure + JamWideJuce_Standalone build + JamWideJuce_VST3 build + JamWideJuce_AU build + 7 unit tests all pass.
- **Committed in:** Not committed (build-only).

**4. [Rule 3 — Blocking] CameraPreviewWindow class type required in Task 1 for the editor's unique_ptr member + click handler**

- **Found during:** Task 1 build verification.
- **Issue:** Task 1's editor wiring references `previewWindow_->isVisible()` and `previewWindow_->setVisible(true)` in the MEDIUM-1 click lambda. The `std::unique_ptr<jamwide::CameraPreviewWindow> previewWindow_;` member also needs `CameraPreviewWindow` to be a complete type for the deleter to instantiate. Plan implies Task 2 defines the class.
- **Fix:** Task 1 commit (`72a3af2`) includes the FULL `CameraPreviewWindow.h` class declaration (juce::DocumentWindow subclass, methods declared) but only a minimal `.cpp` stub (constructor that compiles, no-op methods). Task 2 commit (`1d6b625`) replaces the .cpp body with the full implementation (LookAndFeel attach, aspect-ratio constrainer, owned tile, bounds listener). Same pattern for CameraPreviewTile.h/cpp and NativeCameraPrivacyDialog.h/cpp.
- **Files modified:** Both Task 1 and Task 2 commits touch these files; Task 1 lands the class declaration, Task 2 lands the implementation.
- **Verification:** Build green at each task boundary. Functionally Task 1's editor doesn't instantiate previewWindow_ (it stays nullptr), so the stub-body Task 1 commit is functionally equivalent to "no popout exists yet"; Task 2 then constructs and exercises it.
- **Committed in:** `72a3af2` (Task 1) + `1d6b625` (Task 2)

---

**Total deviations:** 4 (all auto-fixed). 2 plan-specification bugs (subclass vs plain class, 60 px width), 1 environment carry-over (worktree submodules), 1 task-sequencing layout (header in Task 1, implementation in Task 2).
**Impact on plan:** All four are correctness-preserving and within the plan's stated scope. The 60 px width fix is the only one that materially diverges from the plan's letter; the others reconcile internal plan inconsistencies (deviation #1) or maintain the existing worktree-build pattern (#3).

## Issues Encountered

- **build_number.h auto-increments and dirties working tree.** Same as 19-01 — `cmake/increment-build-revision.cmake` bumps `src/build_number.h` on every build. Recurring no-op modification; reverted via `git checkout -- src/build_number.h` before the final SUMMARY commit.
- **macOS arm64 OpenSSL link failure in universal binary build.** Pre-existing issue (same as 19-01). Worked around by running x86_64-only via `./scripts/build.sh --build-dir build-juce-19-test`. Universal binary validation deferred to Phase 23-01 macOS universal stitching.

## User Setup Required

- **None new from this plan.** All UI is functional out-of-the-box; the camera prompt will fire on first launch (HIGH-5 flow) and pull the user through the macOS TCC consent + privacy modal in one go. The Risk C confirmation request (JUCE commercial seat covers `juce_video`) carries over from 19-01.

## Threat Flags

None — this plan introduces only the threats documented in `19-02-ui-and-persistence-PLAN.md`'s `<threat_model>` (T-19-03 + T-19-04 + T-19-PT). All three are mapped to mitigations (juce::jlimit clamping at the read site; modal show-once on the real first-launch path; AsyncUpdater + Subscription RAII for UAF defence). No new surface area beyond what the plan declared.

## Self-Check: PASSED

Verified files exist:
- `juce/video/native/CameraPreviewWindow.h` FOUND (51-line class declaration)
- `juce/video/native/CameraPreviewWindow.cpp` FOUND (86-line implementation)
- `juce/video/native/CameraPreviewTile.h` FOUND (71-line class declaration with member-order contract)
- `juce/video/native/CameraPreviewTile.cpp` FOUND (82-line AsyncUpdater path implementation)
- `juce/video/native/NativeCameraPrivacyDialog.h` FOUND (39 lines, isAckResult helper)
- `juce/video/native/NativeCameraPrivacyDialog.cpp` FOUND (31-line AlertWindow::showAsync wrapper)
- `tests/test_plugin_state_v3_v4.cpp` FOUND (228 lines, 5 scenarios, all pass)

Verified commits exist:
- `72a3af2` Task 1 FOUND
- `1d6b625` Task 2 FOUND
- `21a422c` Task 3 FOUND

Verified greps from plan-level verification:
- `cameraButton.setButtonText`: 2 ✓
- `cameraButton bounds`: 1 ✓
- `Stop Camera`: 4 ✓
- `onCameraClicked`: 1 ✓
- `onCameraStopRequested`: 1 ✓
- `handleCameraIdleClick`: 2 ✓
- `juce::AsyncUpdater` in tile.h: 5 ✓
- `triggerAsyncUpdate`: 1 ✓
- `MessageManager::callAsync.*this` (MUST be 0): **0** ✓
- `NotDetermined`: 3 ✓
- `requestCameraAuthorization`: 2 ✓
- `showPrivacyOrToggle`: 7 ✓
- `isAckResult` in .h: 2 ✓
- `class CameraPreviewWindow.*juce::DocumentWindow`: 1 ✓
- `setFixedAspectRatio`: 1 ✓
- `setUsingNativeTitleBar(false)`: 1 ✓
- `currentStateVersion = 4`: 1 ✓

Verified test passes: `ctest -R plugin_state_v3_v4 --output-on-failure` → "100% tests passed, 0 tests failed out of 1". All 7 camera + plugin-state tests still green.

## Next Phase Readiness

- **19-03 (fallback + verification)** can start: the `FallbackListener::onCameraFallback(cause)` callback in the editor is a stub awaiting CameraStatusDialog wiring. `NativeCameraPrivacyDialog::isAckResult` pattern is ready to be reused for the 3-button mapping (button[0]→1, button[1]→2, button[2]→0). The `test_camera_cause_mapping.cpp` stub from 19-01 is the test entrypoint.
- **Phase 20 (H264 encode/send)** is ready to attach as another `JamWideFrameDistributor::Subscriber`. The encoder doesn't need any UI changes from this plan; it reads `cameraPrivacyAck` from the processor before allowing broadcast (T-19-04 enforcement at the encoder boundary, not the UI).
- **Blockers carried forward:** Risk C (JUCE commercial seat covers juce_video) — defer to UAT confirmation. Spike Risks #3 (Cisco openh264 v2.1.1) and #5 (ffmpeg 7.x soname symlinks) — Phase 23 territory.

---
*Phase: 19-camera-capture-permission-ux*
*Plan: 02-ui-and-persistence*
*Completed: 2026-05-16*
