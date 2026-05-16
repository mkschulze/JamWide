---
phase: 19
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - CMakeLists.txt
  - JamWide.entitlements
  - juce/JamWideJuceProcessor.h
  - juce/JamWideJuceProcessor.cpp
  - juce/video/native/CameraAuthorization.h
  - juce/video/native/CameraAuthorization_mac.mm
  - juce/video/native/CameraAuthorization_windows.cpp
  - juce/video/native/CameraFallbackCause.h
  - juce/video/native/JamWideFrameDistributor.h
  - juce/video/native/JamWideFrameDistributor.cpp
  - juce/video/native/JamWideCameraDevice.h
  - juce/video/native/JamWideCameraDevice.cpp
  - juce/video/native/CameraStateMachine.h
  - juce/video/native/CameraStateMachine.cpp
  - juce/video/native/CameraStatusDialog.h
  - juce/video/native/CameraStatusDialog.cpp
  - juce/video/native/CameraPreviewWindow.h
  - juce/video/native/CameraPreviewWindow.cpp
  - juce/video/native/CameraPreviewTile.h
  - juce/video/native/CameraPreviewTile.cpp
  - juce/video/native/NativeCameraPrivacyDialog.h
  - juce/video/native/NativeCameraPrivacyDialog.cpp
  - tests/test_frame_distributor.cpp
  - tests/test_frame_distributor_lifetime.cpp
  - tests/test_camera_state_machine.cpp
  - tests/test_camera_retry_backoff.cpp
  - tests/test_camera_cause_mapping.cpp
  - tests/test_plugin_state_v3_v4.cpp
  - tests/test_camera_frame_stall.cpp
autonomous: true
requirements:
  - CAM-01
  - CAM-03
  - PKG-04
threat_refs:
  - T-19-01
  - T-19-02
  - T-19-04
  - T-19-05

must_haves:
  truths:
    - "JUCE LICENSE/upgrade-clause preflight check runs BEFORE juce::juce_video is linked into JamWideJuce; Risk C surfaced for user decision if absent"
    - "All source-file stubs that will be referenced by CMake (`*.h`, `*.cpp`, `*.mm`) exist with `#pragma once` or `int main(){return 0;}` skeletons BEFORE `target_sources()` references them, so configure AND initial build both succeed"
    - "Plugin target compiles with juce::CameraDevice declarations visible (JUCE_USE_CAMERA=1 defined on JamWideJuce)"
    - "JamWide.entitlements declares com.apple.security.device.camera"
    - "Built macOS bundle's Info.plist contains NSCameraUsageDescription equal to the configured CAMERA_PERMISSION_TEXT"
    - "Calling jamwide::queryCameraAuthorization() on macOS returns one of {NotDetermined, Restricted, Denied, Authorized}"
    - "JamWideFrameDistributor::registerSubscriber returns a `Subscription` RAII handle whose destructor blocks until in-flight onFrame calls exit (HIGH-2 mitigation)"
    - "JamWideFrameDistributor fans a synthetic juce::Image to all registered subscribers via Subscriber::onFrame"
    - "Concurrent stress test (`test_frame_distributor_lifetime`) demonstrates 'callback in progress, subscriber destroyed' race exits 0 with no crash, no UAF, no deadlock"
    - "CameraStateMachine (pure C++, no JUCE deps) is the single source of truth for state transitions; production code and tests both dispatch through it (MEDIUM-3 mitigation)"
    - "CameraStateMachine has exactly 6 states: Idle, Opening, Capturing, Failed, Retrying, Unavailable (Paused removed per MEDIUM-2)"
    - "JamWideCameraDevice captures `std::atomic<uint64_t> generation_` in every async callback; shutdown() increments it; stale callbacks return early before touching state (HIGH-3 mitigation)"
    - "Five async-callback sites in JamWideCameraDevice (requestCameraAuthorization completion, openDeviceAsync result, watchdog timer, onErrorOccurred, RetryWorker reopen) all check generation token before any state read/write"
    - "Frame-stall watchdog timer fires every 1000 ms while state==Capturing; if no frame for 2000 ms re-queries authorization and emits TCCDenied/CameraInUse (HIGH-6 mitigation)"
    - "First-frame open watchdog fires after WATCHDOG_INTERVAL_MS (3000 ms default; tunable) without a frame and emits CameraInUse fallback cause"
    - "Retry backoff produces the sequence 1s, 2s, 4s, 8s, 16s and gives up at 30s elapsed"
    - "Camera state stays Idle on plugin/standalone launch regardless of saved popout state (D-10)"
    - "Camera tests are pure-C++ executables that compile source files directly via add_executable; NO test links against JamWideJuce (MEDIUM-5 mitigation)"
  artifacts:
    - path: "CMakeLists.txt"
      provides: "JUCE_USE_CAMERA=1 + juce::juce_video link on JamWideJuce; CAMERA_PERMISSION_ENABLED+TEXT; CameraAuthorization platform-conditional sources; 7 new add_test entries (frame_distributor, frame_distributor_lifetime, camera_state_machine, camera_retry_backoff, camera_cause_mapping, plugin_state_v3_v4, camera_frame_stall)"
      contains: "JUCE_USE_CAMERA=1"
    - path: "JamWide.entitlements"
      provides: "Camera entitlement key"
      contains: "com.apple.security.device.camera"
    - path: "juce/video/native/CameraAuthorization.h"
      provides: "Cross-platform CameraAuthStatus enum + queryCameraAuthorization + requestCameraAuthorization functions"
      exports:
        - "enum class CameraAuthStatus"
        - "queryCameraAuthorization"
        - "requestCameraAuthorization"
    - path: "juce/video/native/CameraAuthorization_mac.mm"
      provides: "macOS TCC pre-check via AVCaptureDevice authorizationStatusForMediaType"
    - path: "juce/video/native/CameraAuthorization_windows.cpp"
      provides: "Windows stub returning NotApplicable"
    - path: "juce/video/native/JamWideFrameDistributor.h"
      provides: "Thread-safe frame fan-out with Subscriber interface + Subscription RAII handle"
      exports:
        - "class JamWideFrameDistributor"
        - "class JamWideFrameDistributor::Subscriber"
        - "class JamWideFrameDistributor::Subscription"
    - path: "juce/video/native/CameraStateMachine.h"
      provides: "Pure-C++ 6-state machine with input event enum + transition table; no JUCE dependencies"
      exports:
        - "enum class CameraState"
        - "enum class CameraEvent"
        - "class CameraStateMachine"
    - path: "juce/video/native/JamWideCameraDevice.h"
      provides: "CameraDevice owner + generation-token cancellation + retry-backoff + watchdog + cause-detection"
      exports:
        - "enum class CameraFallbackCause (alias re-export)"
        - "class JamWideCameraDevice"
        - "class JamWideCameraDevice::FallbackListener"
    - path: "juce/video/native/CameraFallbackCause.h"
      provides: "Enum shared between camera device and dialog (own header so 19-03 dialog can include without pulling full camera device header)"
      contains: "enum class CameraFallbackCause"
    - path: "tests/test_frame_distributor.cpp"
      provides: "CAM-03 frame fan-out + removal-safe iteration coverage"
      min_lines: 80
    - path: "tests/test_frame_distributor_lifetime.cpp"
      provides: "HIGH-2 mitigation test: in-flight onFrame + Subscription destructor blocks; no UAF"
      min_lines: 60
    - path: "tests/test_camera_state_machine.cpp"
      provides: "CAM-02/CAM-03 state transition matrix coverage via direct CameraStateMachine class"
      min_lines: 120
    - path: "tests/test_camera_retry_backoff.cpp"
      provides: "D-20 retry timing coverage with virtualized clock"
      min_lines: 60
    - path: "tests/test_camera_frame_stall.cpp"
      provides: "HIGH-6 mitigation test: frame-stall detection with virtualized clock"
      min_lines: 50
  key_links:
    - from: "JamWideJuceProcessor"
      to: "JamWideCameraDevice"
      via: "std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera_ member"
      pattern: "std::unique_ptr<jamwide::JamWideCameraDevice>"
    - from: "JamWideCameraDevice"
      to: "CameraStateMachine"
      via: "Owns a CameraStateMachine instance; all callbacks dispatch CameraEvent into it"
      pattern: "stateMachine_.dispatch"
    - from: "JamWideCameraDevice"
      to: "JamWideFrameDistributor"
      via: "Listener::imageReceived → distributor.publish(image)"
      pattern: "distributor.*publish"
    - from: "JamWideFrameDistributor::Subscription"
      to: "Subscriber lifetime"
      via: "Subscription destructor waits for in-flight publish to exit before returning"
      pattern: "Subscription::~Subscription"
    - from: "JamWideCameraDevice"
      to: "CameraAuthorization"
      via: "queryCameraAuthorization() called BEFORE juce::CameraDevice::openDeviceAsync"
      pattern: "queryCameraAuthorization"
    - from: "JamWideJuceProcessor"
      to: "CMakeLists.txt"
      via: "target_link_libraries JamWideJuce PRIVATE juce::juce_video AND target_compile_definitions JUCE_USE_CAMERA=1"
      pattern: "JUCE_USE_CAMERA=1"
---

<objective>
Stand up the entire backend capture pipeline for Phase 19 with the thread-safety + lifetime guarantees Codex flagged as missing. Specifically: (a) preflight the JUCE seat-licence / upgrade-clause check (MEDIUM-4) BEFORE `juce::juce_video` enters the main target; (b) create ALL source-file stubs before CMake references them so configure + build both succeed (HIGH-1); (c) provide a `Subscription` RAII handle on the frame distributor whose destructor blocks until in-flight `onFrame` exits (HIGH-2); (d) factor the state machine into a pure-C++ class so tests and production share one code path (MEDIUM-3, MEDIUM-2); (e) add generation-token cancellation to every async callback site so destruction-during-flight is safe (HIGH-3); (f) add a continuous frame-stall watchdog that fires while `Capturing` and re-queries authorization (HIGH-6); (g) make every camera test pure-C++ with no JamWideJuce link (MEDIUM-5). Also: macOS entitlement (PKG-04 portion), TCC pre-check shim (D-03), JUCE_USE_CAMERA=1 (Risk E), 7-state→6-state machine, exponential-backoff retry, JamWideJuceProcessor ownership wiring, and all four backend Wave 0 test scaffolds.

Purpose: Without this revised plan, the prior Codex review identified seven HIGH-severity issues — non-buildable task ordering, use-after-free in the distributor, raw-`this` async captures, silent frame-stall failure, late license check, lab-only state-machine tests, and shaky CMake test linkage. This plan resolves HIGH-1/2/3/6 + MEDIUM-2/3/4/5 entirely within Wave 1, leaving 19-02 to address HIGH-4/5/MEDIUM-1 and 19-03 to address HIGH-7/MEDIUM-6/LOW-1.

Output: A buildable JamWideJuce target with a constructed-but-Idle camera pipeline (D-10), six green pure-C++ unit tests (frame distributor, distributor lifetime/race, state machine, retry backoff, frame-stall, plus the two stubs filled by 19-02 / 19-03), and a working TCC pre-check on macOS that returns one of four valid auth states. All async-callback sites are crash-safe under host-driven editor/processor destruction.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md
@.planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md
@.planning/phases/19-camera-capture-permission-ux/19-VALIDATION.md
@.planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md

<interfaces>
<!-- Key contracts the executor needs. Extracted from JUCE sources + JamWide codebase. -->
<!-- Use these directly — no codebase exploration needed for these signatures. -->

From libs/juce/modules/juce_video/capture/juce_CameraDevice.h:38 — visibility gate:
  #if JUCE_USE_CAMERA || DOXYGEN  (must be defined on JamWideJuce target — see Risk E)

From libs/juce/modules/juce_video/capture/juce_CameraDevice.h:62-117 — JUCE API:
  static StringArray CameraDevice::getAvailableDevices();
  static CameraDevice* openDevice(int deviceIndex, int minW, int minH, int maxW, int maxH, bool highQuality = true);
  static void openDeviceAsync(int deviceIndex,
                              std::function<void(CameraDevice*, const String& /*error*/)> resultCallback,
                              int minW, int minH, int maxW, int maxH, bool highQuality = true);
  const String& getName() const noexcept;
  void addListener(Listener*);
  void removeListener(Listener*);
  void onErrorOccurred = std::function<void(const String& error)>;   // public member

From libs/juce/modules/juce_video/capture/juce_CameraDevice.h:200-204 — threading contract:
  // Listener::imageReceived(const Image&)
  // "This may be called by any thread, so be careful about thread-safety,
  //  and make sure that you process the data as quickly as possible to avoid glitching!"

From libs/juce/modules/juce_gui_basics/windows/juce_MessageBoxOptions.h:94 — JUCE button API limit:
  MessageBoxOptions withButton(const String& text);  // text only; NO custom return-code overload exists.
  // JUCE assigns return codes to the callback per the documented mapping at juce_AlertWindow.h:457-466:
  //   1 button:  button[0] -> 0
  //   2 buttons: button[0] -> 1, button[1] -> 0
  //   3 buttons: button[0] -> 1, button[1] -> 2, button[2] -> 0
  // This plan does NOT add buttons; the dialog work is in 19-02 (privacy modal) and 19-03 (status dialog).
  // Listed here so the planner's reference is consistent across plans.

From JamWide codebase (current state - do not assume these exist after this plan):
  juce/JamWideJuceProcessor.h:88   static constexpr int currentStateVersion = 3;   (this plan does NOT bump — 19-02 does)
  juce/JamWideJuceProcessor.h:119  std::unique_ptr<jamwide::VideoCompanion> videoCompanion;
  juce/JamWideJuceProcessor.cpp:61 constructor (videoCompanion init at line ~64)
  juce/JamWideJuceProcessor.cpp:67 destructor (LIFO reset: midiMapper, videoCompanion, oscServer, runThread, client)

From JamWide CMakeLists.txt:
  line 145-168  juce_add_plugin(JamWideJuce ...) — has MICROPHONE_PERMISSION_ENABLED TRUE at line 167
  line 200-205  BrowserDetect_mac.mm vs BrowserDetect_win.cpp platform dispatch pattern
  line 371      JUCE_USE_CAMERA=1 currently scoped to video_spike target only (Risk E)
  line 406-470  Test executable wiring under if(JAMWIDE_BUILD_TESTS)

From JamWide.entitlements (current):
  com.apple.security.device.audio-input, network.client, network.server   (no camera key)

CameraAuthStatus enum (DEFINE IN THIS PLAN at juce/video/native/CameraAuthorization.h):
  namespace jamwide {
    enum class CameraAuthStatus : int {
      NotDetermined = 0,
      Restricted    = 1,
      Denied        = 2,
      Authorized    = 3,
      NotApplicable = 4,   // Windows
    };
    CameraAuthStatus queryCameraAuthorization();
    void requestCameraAuthorization(std::function<void(CameraAuthStatus)> callback);
  }

CameraFallbackCause enum (DEFINE IN THIS PLAN at juce/video/native/CameraFallbackCause.h):
  namespace jamwide {
    enum class CameraFallbackCause : int {
      None = 0,
      TCCDenied,             // macOS user denied; macOS Restricted maps here too
      HostLacksEntitlement,  // macOS plugin context, host has no NSCameraUsageDescription
      CameraInUse,           // openDeviceAsync error or watchdog fires after auth=Authorized
      NoHardware,            // getAvailableDevices() empty
      WindowsPrivacyBlock,   // Windows: openDeviceAsync nullptr with devices visible
    };
  }

CameraState enum (DEFINE IN THIS PLAN at juce/video/native/CameraStateMachine.h — 6 states, Paused REMOVED per MEDIUM-2):
  namespace jamwide {
    enum class CameraState : int {
      Idle = 0,
      Opening,
      Capturing,
      Failed,
      Retrying,
      Unavailable,
    };
    enum class CameraEvent : int {
      UserToggle,           // user clicked Camera button
      AuthGranted,
      AuthDenied,
      OpenSucceeded,
      OpenFailed,
      FirstFrameReceived,
      WatchdogFired,        // first-frame watchdog OR frame-stall watchdog (HIGH-6)
      RuntimeError,
      RetryTick,
      RetryExhausted,
      RecheckPermission,    // D-12
      Shutdown,
    };
  }
</interfaces>
</context>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| OS TCC → JamWide | macOS controls whether camera frames flow; "Authorized" status is necessary but not sufficient (Risk F) |
| Camera-callback thread → distributor → subscribers | imageReceived fires on "any thread"; subscribers must marshal to their target thread |
| Plugin process → DAW host process | DAW host's bundle ID controls TCC for plugin context (SPARTA #82) |
| Async-callback closure → JamWideCameraDevice instance | Host can destroy the editor/processor while async callbacks are in flight |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-19-01 | Tampering (TCC bypass) | JamWideCameraDevice / TCC pre-check | mitigate | jamwide::queryCameraAuthorization() called BEFORE openDeviceAsync; first-frame watchdog (3 s default) detects "Authorized but no frames"; continuous frame-stall watchdog (every 1000 ms while Capturing, fires after 2000 ms gap) catches mid-session revoke (HIGH-6) — emits CameraFallbackCause::TCCDenied or CameraInUse based on re-queried auth status. |
| T-19-02 | Information Disclosure (use-after-free on camera-callback thread) | JamWideFrameDistributor | mitigate | `Subscription` RAII handle returned from `registerSubscriber`. Distributor maintains an in-flight-publish refcount; `Subscription::~Subscription()` removes the subscriber from the active set, then BLOCKS on a condition variable until any in-flight `publish()` returns. Snapshot iteration still happens outside the mutex, but the snapshot now holds `shared_ptr<Subscriber>` entries that protect the underlying object. Test `test_frame_distributor_lifetime` reproduces the race deterministically. Updates per Codex HIGH-2. |
| T-19-03 | Tampering (UAF in async camera callbacks) | JamWideCameraDevice async callsites | mitigate | `std::atomic<uint64_t> generation_` cancellation token. Every async-callback closure (TCC request completion, openDeviceAsync result, watchdog timers, onErrorOccurred, RetryWorker reopen — 5 sites total) captures the generation value at scheduling time; on invocation it loads `currentGeneration_.load()` and bails if mismatched. `shutdown()` increments generation_, signals retry worker exit, removes the JUCE listener, and releases juceCamera_. Effectively all async work after shutdown is a no-op. Documented in JamWideCameraDevice.h. Added per Codex HIGH-3. |
| T-19-04 | Privacy (camera-on without ack) | JamWideCameraDevice initial state | mitigate | D-10 enforced: CameraStateMachine constructor initialises to Idle regardless of stored popout state; nativeCamera_->toggle() requires explicit user click; privacyAck check is 19-02's responsibility (gates broadcast in Phase 20) — Phase 19 enforces idle-on-launch only |
| T-19-05 | Entitlement spoofing (verify in codesigned bundle, not just source file) | JamWide.entitlements + CAMERA_PERMISSION_ENABLED | mitigate | This plan adds the entitlement and the JUCE permission text; verification that they survive codesign is 19-03's verify_camera_entitlement.sh — but this plan's UAT instruction includes running `codesign --display --entitlements - <bundle>` once on the locally-built artifact |
| T-19-SC | Tampering (license non-compliance via juce_video AGPLv3) | CMakeLists.txt + LICENSE | mitigate | Task 1 runs a license-header preflight check BEFORE `target_link_libraries(JamWideJuce PRIVATE juce::juce_video)` lands. If the upgrade-clause grep fails on the LICENSE file AND on representative source headers, Task 1 blocks and emits a CHECKPOINT requiring user decision (commercial seat / add upgrade clause / replan with direct AVFoundation+DirectShow). Moved from 19-03 to 19-01 preflight per Codex MEDIUM-4. |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: License preflight + CMake/entitlements plumbing + ALL source-file stubs (HIGH-1, MEDIUM-4, Risk E, PKG-04)</name>
  <files>
    CMakeLists.txt,
    JamWide.entitlements,
    juce/video/native/CameraAuthorization.h,
    juce/video/native/CameraAuthorization_mac.mm,
    juce/video/native/CameraAuthorization_windows.cpp,
    juce/video/native/CameraFallbackCause.h,
    juce/video/native/JamWideFrameDistributor.h,
    juce/video/native/JamWideFrameDistributor.cpp,
    juce/video/native/CameraStateMachine.h,
    juce/video/native/CameraStateMachine.cpp,
    juce/video/native/JamWideCameraDevice.h,
    juce/video/native/JamWideCameraDevice.cpp,
    juce/video/native/CameraStatusDialog.h,
    juce/video/native/CameraStatusDialog.cpp,
    juce/video/native/CameraPreviewWindow.h,
    juce/video/native/CameraPreviewWindow.cpp,
    juce/video/native/CameraPreviewTile.h,
    juce/video/native/CameraPreviewTile.cpp,
    juce/video/native/NativeCameraPrivacyDialog.h,
    juce/video/native/NativeCameraPrivacyDialog.cpp,
    tests/test_frame_distributor.cpp,
    tests/test_frame_distributor_lifetime.cpp,
    tests/test_camera_state_machine.cpp,
    tests/test_camera_retry_backoff.cpp,
    tests/test_camera_cause_mapping.cpp,
    tests/test_plugin_state_v3_v4.cpp,
    tests/test_camera_frame_stall.cpp
  </files>
  <read_first>
    - CMakeLists.txt (read lines 1-50, 145-210, 365-470 — discover existing target structure, juce_add_plugin block, BrowserDetect dispatch, video_spike pattern, JAMWIDE_BUILD_TESTS block)
    - JamWide.entitlements (current 4-key plist)
    - LICENSE (current GPLv2 boilerplate; check whether "or any later version" appears in header)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §7 (entitlements details), §11 (test infrastructure), §13 Risk C (license) + Risk E (JUCE_USE_CAMERA scope)
    - libs/juce/extras/Build/CMake/JUCEUtils.cmake (skim for CAMERA_PERMISSION_ENABLED handling near line 367-368 — confirms JUCE recognizes this property)
  </read_first>
  <behavior>
    - Behavior 0 (PREFLIGHT — fires FIRST, before any CMake edit): A grep of the LICENSE file AND of representative source headers (`juce/JamWideJuceProcessor.h`, `juce/JamWideJuceEditor.h`, `src/core/njclient.h`) for the upgrade clause `"or (at your option) any later version"` OR `"or any later version"` determines Risk C status. The result is recorded in the SUMMARY but does NOT block this task — see Action step 0 for the exact decision policy.
    - Behavior 1: `cmake -S . -B build-juce -DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_BUILD_TESTS=ON` configures successfully AND `cmake --build build-juce --target JamWideJuce` succeeds AT END OF THIS TASK. Both configure AND build succeed because every `.h/.cpp/.mm` referenced by `target_sources()` exists at end of this task (as empty stubs — subsequent tasks FILL them).
    - Behavior 2: A grep of build artifacts confirms `JUCE_USE_CAMERA=1` is set on the JamWideJuce compile flags, not just the video_spike target.
    - Behavior 3: A built macOS bundle's Info.plist contains both `NSMicrophoneUsageDescription` (existing) AND `NSCameraUsageDescription` with value "JamWide uses your webcam to share video with NINJAM peers."
    - Behavior 4: `JamWide.entitlements` contains `com.apple.security.device.camera = true` in addition to the three existing keys.
    - Behavior 5: All seven new test executable names (`test_frame_distributor`, `test_frame_distributor_lifetime`, `test_camera_state_machine`, `test_camera_retry_backoff`, `test_camera_cause_mapping`, `test_plugin_state_v3_v4`, `test_camera_frame_stall`) are declared in CMakeLists.txt under `if(JAMWIDE_BUILD_TESTS)`, with the corresponding `tests/*.cpp` files existing as `int main(){return 0;}` stubs that will be filled in subsequent tasks (test_frame_distributor + test_frame_distributor_lifetime in Task 4, test_camera_state_machine + test_camera_retry_backoff + test_camera_frame_stall in Task 6, test_plugin_state_v3_v4 in 19-02 Task 3, test_camera_cause_mapping in 19-03 Task 1).
    - Behavior 6: BrowserDetect-style platform dispatch added for CameraAuthorization sources: `juce/video/native/CameraAuthorization_mac.mm` on APPLE, `juce/video/native/CameraAuthorization_windows.cpp` otherwise. Both files exist as stubs at end of this task; Task 2 FILLS them with real implementations.
    - Behavior 7 (HIGH-1 fix): NO `target_sources()` line in CMakeLists.txt references a file that does not exist on disk at end of this task. Verified by `cmake -G Ninja` configure step succeeding without "file not found" errors.
    - Behavior 8 (MEDIUM-5 fix): test executables in CMakeLists.txt are pure-C++ — `add_executable(test_X tests/test_X.cpp <list of .cpp source files>)` — and `target_link_libraries(test_X PRIVATE juce::juce_core juce::juce_graphics)` (or similar minimal JUCE modules). NO test links against the `JamWideJuce` target. Each test compiles whatever subset of `juce/video/native/*.cpp` it actually needs directly via `add_executable`.
  </behavior>
  <action>
    Execute steps in this order — DO NOT skip ahead:

    **Step 0 — License preflight (MEDIUM-4 fix; HIGH RESPONSE if check fails):**

    Run this exact grep ladder (no fallback to broader patterns):
    ```bash
    LICENSE_CLAUSE=$(grep -l 'or (at your option) any later version\|or any later version' LICENSE juce/JamWideJuceProcessor.h juce/JamWideJuceProcessor.cpp juce/JamWideJuceEditor.h juce/JamWideJuceEditor.cpp src/core/njclient.h src/core/njclient.cpp 2>/dev/null | wc -l | tr -d ' ')
    ```

    - **If LICENSE_CLAUSE > 0**: Record in SUMMARY: "Risk C CLOSED — upgrade clause found in N file(s). Proceeding with juce_video integration." Continue to Step 1.
    - **If LICENSE_CLAUSE == 0**: HALT execution before any CMake edit. Surface this to the user via the SUMMARY with EXACT format:
      ```
      RISK C OPEN — JamWide LICENSE + source headers do NOT contain the GPLv2-or-later upgrade clause.
      juce_video is AGPLv3 (only compatible with GPLv2+ via upgrade clause).
      DECISION REQUIRED before juce_video can be linked:
        (a) Confirm JUCE commercial seat covers this work (most likely — JamWide already uses other JUCE modules).
        (b) Add "or (at your option) any later version" upgrade clause to LICENSE + source headers (one-time retroactive opt-in).
        (c) Replan Phase 19 with direct AVFoundation + DirectShow capture (~+800 LOC, ~+1 plan).
      Cannot proceed with juce_video until user resolves.
      ```
      Wait for user response. If user says "proceed" (option (a) — commercial seat) OR fixes the LICENSE file (option (b)), continue to Step 1. If user picks (c), STOP — phase needs replanning.

    **Step 1 — Create ALL source-file stubs (HIGH-1 fix):**

    Create each of the following 20 files with the EXACT stub contents described. CMake will reference all of them in Step 2; without the stubs, configure fails.

    Headers (`#pragma once` + forward decls only; bodies in later tasks):
    - `juce/video/native/CameraAuthorization.h` — `#pragma once\n#include <functional>\n#include <cstdint>\nnamespace jamwide {\nenum class CameraAuthStatus : int { NotDetermined=0, Restricted=1, Denied=2, Authorized=3, NotApplicable=4 };\nCameraAuthStatus queryCameraAuthorization();\nvoid requestCameraAuthorization(std::function<void(CameraAuthStatus)>);\n}\n` (FULL declaration — Task 2 fills the implementations).
    - `juce/video/native/CameraFallbackCause.h` — `#pragma once\n#include <cstdint>\nnamespace jamwide {\nenum class CameraFallbackCause : int { None=0, TCCDenied, HostLacksEntitlement, CameraInUse, NoHardware, WindowsPrivacyBlock };\n}\n` (FULL declaration — no further changes; downstream code includes only this header).
    - `juce/video/native/JamWideFrameDistributor.h` — `#pragma once` plus a minimal class skeleton declaring `class JamWideFrameDistributor`, `class JamWideFrameDistributor::Subscriber`, `class JamWideFrameDistributor::Subscription`, and the public method signatures (`registerSubscriber`, `publish`, `getPeakFps`). Bodies in Task 3.
    - `juce/video/native/JamWideFrameDistributor.cpp` — `#include "JamWideFrameDistributor.h"\n// Task 3 fills this.\n` (no symbols defined yet — but Task 3 lands before any test executable that references the distributor; symbols-resolved is enforced by the test build steps, not the JamWideJuce build).
    - `juce/video/native/CameraStateMachine.h` — `#pragma once\nnamespace jamwide {\nenum class CameraState : int { Idle=0, Opening, Capturing, Failed, Retrying, Unavailable };\nenum class CameraEvent : int { UserToggle, AuthGranted, AuthDenied, OpenSucceeded, OpenFailed, FirstFrameReceived, WatchdogFired, RuntimeError, RetryTick, RetryExhausted, RecheckPermission, Shutdown };\nclass CameraStateMachine; }` plus a forward-decl-only `class CameraStateMachine`. Task 5 fills.
    - `juce/video/native/CameraStateMachine.cpp` — `#include "CameraStateMachine.h"\n// Task 5 fills this.\n`
    - `juce/video/native/JamWideCameraDevice.h` — `#pragma once\n#include "CameraFallbackCause.h"\n#include "CameraStateMachine.h"\nnamespace jamwide {\nclass JamWideFrameDistributor;\nclass JamWideCameraDevice { public: class FallbackListener; JamWideCameraDevice(JamWideFrameDistributor&, FallbackListener*); ~JamWideCameraDevice(); /* Task 6 adds full API */ };\n}` (skeleton — Task 6 fills full API).
    - `juce/video/native/JamWideCameraDevice.cpp` — `#include "JamWideCameraDevice.h"\nnamespace jamwide { JamWideCameraDevice::JamWideCameraDevice(JamWideFrameDistributor&, FallbackListener*) {} JamWideCameraDevice::~JamWideCameraDevice() = default; }` (constructor/destructor stubs so JamWideJuce links — Task 6 fills full impl).
    - `juce/video/native/CameraStatusDialog.h` — `#pragma once\n// Filled in 19-03 Task 1.\n`
    - `juce/video/native/CameraStatusDialog.cpp` — `// Filled in 19-03 Task 1.\n` (no `#include` — CMake only references this from JamWideJuce target_sources after 19-03 lands; we stage the file path here so the CMake plumbing exists).
    - `juce/video/native/CameraPreviewWindow.h` — `#pragma once\n// Filled in 19-02 Task 2.\n`
    - `juce/video/native/CameraPreviewWindow.cpp` — `// Filled in 19-02 Task 2.\n`
    - `juce/video/native/CameraPreviewTile.h` — `#pragma once\n// Filled in 19-02 Task 2.\n`
    - `juce/video/native/CameraPreviewTile.cpp` — `// Filled in 19-02 Task 2.\n`
    - `juce/video/native/NativeCameraPrivacyDialog.h` — `#pragma once\n// Filled in 19-02 Task 3.\n`
    - `juce/video/native/NativeCameraPrivacyDialog.cpp` — `// Filled in 19-02 Task 3.\n`
    - `juce/video/native/CameraAuthorization_mac.mm` — `// Filled in Task 2.\n#include "CameraAuthorization.h"\nnamespace jamwide { CameraAuthStatus queryCameraAuthorization() { return CameraAuthStatus::Denied; } void requestCameraAuthorization(std::function<void(CameraAuthStatus)> cb) { cb(CameraAuthStatus::Denied); } }` (defensive stub returning Denied — Task 2 replaces with real TCC pre-check).
    - `juce/video/native/CameraAuthorization_windows.cpp` — `#include "CameraAuthorization.h"\nnamespace jamwide { CameraAuthStatus queryCameraAuthorization() { return CameraAuthStatus::NotApplicable; } void requestCameraAuthorization(std::function<void(CameraAuthStatus)> cb) { cb(CameraAuthStatus::NotApplicable); } }` (final Windows behavior — Task 2 may refine docs but the values are correct from here).

    Test stubs (each `int main() { return 0; }` skeleton):
    - `tests/test_frame_distributor.cpp`
    - `tests/test_frame_distributor_lifetime.cpp`
    - `tests/test_camera_state_machine.cpp`
    - `tests/test_camera_retry_backoff.cpp`
    - `tests/test_camera_frame_stall.cpp`
    - `tests/test_camera_cause_mapping.cpp` (filled in 19-03 Task 1)
    - `tests/test_plugin_state_v3_v4.cpp` (filled in 19-02 Task 3)

    Use Write tool for ALL stub files. Total: 20 files (13 source + 7 tests). All must exist on disk before Step 2 edits CMakeLists.txt.

    **Step 2 — JamWide.entitlements:**

    Add `<key>com.apple.security.device.camera</key><true/>` as a fifth key, preserving the existing four. Per D-28 + PKG-04 entitlements portion.

    **Step 3 — CMakeLists.txt — juce_add_plugin block (lines ~145-168):**

    Immediately after the existing `MICROPHONE_PERMISSION_TEXT "..."` line (line 167), add two new arguments to `juce_add_plugin(JamWideJuce ...)`:
    `CAMERA_PERMISSION_ENABLED TRUE`
    `CAMERA_PERMISSION_TEXT "JamWide uses your webcam to share video with NINJAM peers."`
    Per D-28 + RESEARCH §7. JUCE will plumb NSCameraUsageDescription into the bundle Info.plist.

    **Step 4 — CMakeLists.txt — JUCE_USE_CAMERA + juce::juce_video link + camera target_sources:**

    Find the `target_sources(JamWideJuce PRIVATE ...)` block right after `juce_generate_juce_header(JamWideJuce)`. After the block ends, add:
    - `target_compile_definitions(JamWideJuce PUBLIC JUCE_USE_CAMERA=1)` — closes Risk E.
    - Add `juce::juce_video` to the existing `target_link_libraries(JamWideJuce PRIVATE ...)` call for the plugin target (find the existing one and append the module).
    - Add CameraAuthorization platform dispatch immediately after the existing BrowserDetect lines at CMakeLists.txt:200-205, following the same `if(APPLE) ... else() ...` pattern:
      ```
      if(APPLE)
          target_sources(JamWideJuce PRIVATE juce/video/native/CameraAuthorization_mac.mm)
      else()
          target_sources(JamWideJuce PRIVATE juce/video/native/CameraAuthorization_windows.cpp)
      endif()
      ```
    - Add a cross-platform `target_sources(JamWideJuce PRIVATE ...)` block listing ALL the new .cpp files (every one of them exists as a stub after Step 1, so configure + build both succeed):
      ```
      target_sources(JamWideJuce PRIVATE
        juce/video/native/JamWideFrameDistributor.cpp
        juce/video/native/CameraStateMachine.cpp
        juce/video/native/JamWideCameraDevice.cpp
        juce/video/native/CameraStatusDialog.cpp
        juce/video/native/CameraPreviewWindow.cpp
        juce/video/native/CameraPreviewTile.cpp
        juce/video/native/NativeCameraPrivacyDialog.cpp
      )
      ```

    **Step 5 — CMakeLists.txt — Pre-stage all 7 test executables (pure-C++; no JamWideJuce link — MEDIUM-5 fix):**

    Under `if(JAMWIDE_BUILD_TESTS)` (line 406+), add 7 blocks AFTER the existing `test_video_fourcc` block. For each test executable, list the test source AND any production .cpp files it needs as compile-time inputs (not link dependencies on JamWideJuce). Common JUCE include dirs (`${CMAKE_CURRENT_SOURCE_DIR}` and `${CMAKE_CURRENT_SOURCE_DIR}/src`) and the `JAMWIDE_BUILD_TESTS=1` compile def follow the existing `test_rawdata_send.cpp` pattern.

    ```
    add_executable(test_frame_distributor
      tests/test_frame_distributor.cpp
      juce/video/native/JamWideFrameDistributor.cpp)
    target_compile_definitions(test_frame_distributor PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_frame_distributor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_frame_distributor PRIVATE juce::juce_core juce::juce_graphics)
    add_test(NAME camera_frame_distributor COMMAND test_frame_distributor)

    add_executable(test_frame_distributor_lifetime
      tests/test_frame_distributor_lifetime.cpp
      juce/video/native/JamWideFrameDistributor.cpp)
    target_compile_definitions(test_frame_distributor_lifetime PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_frame_distributor_lifetime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_frame_distributor_lifetime PRIVATE juce::juce_core juce::juce_graphics)
    add_test(NAME camera_frame_distributor_lifetime COMMAND test_frame_distributor_lifetime)

    add_executable(test_camera_state_machine
      tests/test_camera_state_machine.cpp
      juce/video/native/CameraStateMachine.cpp)
    target_compile_definitions(test_camera_state_machine PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_camera_state_machine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_camera_state_machine PRIVATE juce::juce_core)
    add_test(NAME camera_state_machine COMMAND test_camera_state_machine)

    add_executable(test_camera_retry_backoff
      tests/test_camera_retry_backoff.cpp
      juce/video/native/CameraStateMachine.cpp)
    target_compile_definitions(test_camera_retry_backoff PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_camera_retry_backoff PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_camera_retry_backoff PRIVATE juce::juce_core)
    add_test(NAME camera_retry_backoff COMMAND test_camera_retry_backoff)

    add_executable(test_camera_frame_stall
      tests/test_camera_frame_stall.cpp
      juce/video/native/CameraStateMachine.cpp)
    target_compile_definitions(test_camera_frame_stall PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_camera_frame_stall PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_camera_frame_stall PRIVATE juce::juce_core)
    add_test(NAME camera_frame_stall COMMAND test_camera_frame_stall)

    add_executable(test_camera_cause_mapping
      tests/test_camera_cause_mapping.cpp
      juce/video/native/CameraStatusDialog.cpp)
    target_compile_definitions(test_camera_cause_mapping PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_camera_cause_mapping PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_camera_cause_mapping PRIVATE juce::juce_core juce::juce_gui_basics)
    add_test(NAME camera_cause_mapping COMMAND test_camera_cause_mapping)

    add_executable(test_plugin_state_v3_v4
      tests/test_plugin_state_v3_v4.cpp)
    target_compile_definitions(test_plugin_state_v3_v4 PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_plugin_state_v3_v4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_plugin_state_v3_v4 PRIVATE juce::juce_core juce::juce_data_structures)
    add_test(NAME plugin_state_v3_v4 COMMAND test_plugin_state_v3_v4)
    ```

    CRITICAL: do NOT touch the existing test_video_fourcc / test_rawdata_send / test_video_sync / test_njclient_atomics wiring. The CTest regex `-R camera` matches the 6 camera-prefixed tests; `-R plugin_state` matches the v3_v4 migration test.

    Note: Test stubs were created in Step 1 with `int main(){return 0;}`. The CMake plumbing here references them all; configure + build both succeed at end of this task because every referenced source file exists (even if its main() is empty). Test bodies are FILLED by Tasks 4 (distributor + lifetime), 5 (state machine), 6 (retry backoff + frame stall), 19-02 Task 3 (plugin state), 19-03 Task 1 (cause mapping).
  </action>
  <verify>
    <automated>set -e; cmake -S . -B build-juce-19-test -DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_BUILD_TESTS=ON -G Ninja 2>&amp;1 | tail -10 &amp;&amp; cmake --build build-juce-19-test --target JamWideJuce 2>&amp;1 | tail -10 &amp;&amp; test -f juce/video/native/CameraAuthorization.h &amp;&amp; test -f juce/video/native/CameraAuthorization_mac.mm &amp;&amp; test -f juce/video/native/CameraAuthorization_windows.cpp &amp;&amp; test -f juce/video/native/CameraFallbackCause.h &amp;&amp; test -f juce/video/native/JamWideFrameDistributor.cpp &amp;&amp; test -f juce/video/native/CameraStateMachine.cpp &amp;&amp; test -f juce/video/native/JamWideCameraDevice.cpp &amp;&amp; test -f juce/video/native/CameraStatusDialog.cpp &amp;&amp; test -f juce/video/native/CameraPreviewWindow.cpp &amp;&amp; test -f juce/video/native/CameraPreviewTile.cpp &amp;&amp; test -f juce/video/native/NativeCameraPrivacyDialog.cpp &amp;&amp; test -f tests/test_frame_distributor.cpp &amp;&amp; test -f tests/test_frame_distributor_lifetime.cpp &amp;&amp; test -f tests/test_camera_state_machine.cpp &amp;&amp; test -f tests/test_camera_retry_backoff.cpp &amp;&amp; test -f tests/test_camera_frame_stall.cpp &amp;&amp; test -f tests/test_camera_cause_mapping.cpp &amp;&amp; test -f tests/test_plugin_state_v3_v4.cpp &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'JUCE_USE_CAMERA=1' &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'CAMERA_PERMISSION_ENABLED TRUE' &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'CameraAuthorization_mac.mm' &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'add_test(NAME camera_frame_distributor_lifetime' &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'add_test(NAME camera_frame_stall' &amp;&amp; grep -c 'com.apple.security.device.camera' JamWide.entitlements</automated>
  </verify>
  <done>
    cmake configure AND `cmake --build ... --target JamWideJuce` BOTH succeed (HIGH-1 closure verified — no missing source files); JUCE_USE_CAMERA=1 appears at least once in CMakeLists.txt; CAMERA_PERMISSION_ENABLED TRUE appears at least once; seven new add_test entries are visible; entitlements file contains the camera key; all 13 production source stubs + 7 test stubs exist on disk; Risk C result (open/closed/decision-required) recorded in plan-progress notes for surfacing in SUMMARY.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: CameraAuthorization shim — fill macOS .mm with real TCC pre-check (D-03, Spike Risk #2)</name>
  <files>
    juce/video/native/CameraAuthorization_mac.mm,
    juce/video/native/CameraAuthorization_windows.cpp
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §2 "macOS TCC Pre-Check" (full section, including the .mm skeleton at lines 247-275)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §3 "Windows Camera Backend" (the no-pre-check-on-desktop finding)
    - juce/video/BrowserDetect_mac.mm and juce/video/BrowserDetect_win.cpp (existing precedent — note these already exist in the codebase and demonstrate the .mm/.cpp cross-compilation pattern)
  </read_first>
  <behavior>
    - Behavior 1: `jamwide::queryCameraAuthorization()` on macOS returns one of {NotDetermined, Restricted, Denied, Authorized} based on `[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]`.
    - Behavior 2: `jamwide::queryCameraAuthorization()` on Windows returns `NotApplicable` (Task 1's stub already does this; this task is a no-op on Windows beyond a docs comment).
    - Behavior 3: `jamwide::requestCameraAuthorization(callback)` on macOS invokes `[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL)]`; the callback receives `Authorized` if `BOOL granted == YES`, else `Denied`. The callback runs on an unspecified thread per Apple's contract — callers must `MessageManager::callAsync` if they touch UI.
    - Behavior 4: `jamwide::requestCameraAuthorization(callback)` on Windows synchronously invokes callback with `NotApplicable` (no change from Task 1 stub).
  </behavior>
  <action>
    1. **Replace `juce/video/native/CameraAuthorization_mac.mm` body with real implementation.** Per RESEARCH §2 lines 247-275 verbatim:
       ```objc
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

    2. **Windows stub `juce/video/native/CameraAuthorization_windows.cpp`** — already final after Task 1. Add a header comment explaining "Windows TCC concept does not apply to desktop apps; cause detection happens at openDeviceAsync error time + watchdog. See RESEARCH §3."

    No tests for this task — the .mm code is exercised by manual UAT (cannot mock TCC). The state machine test in Task 5 verifies the *consumers* of `CameraAuthStatus`. JamWideJuce target rebuilds cleanly.
  </action>
  <verify>
    <automated>grep -c 'authorizationStatusForMediaType:AVMediaTypeVideo' juce/video/native/CameraAuthorization_mac.mm &amp;&amp; grep -c 'requestAccessForMediaType:AVMediaTypeVideo' juce/video/native/CameraAuthorization_mac.mm &amp;&amp; grep -c 'NotApplicable' juce/video/native/CameraAuthorization_windows.cpp &amp;&amp; cmake --build build-juce-19-test --target JamWideJuce 2>&amp;1 | tail -10</automated>
  </verify>
  <done>
    CameraAuthorization_mac.mm contains the four-case switch and the requestAccessForMediaType call; JamWideJuce still compiles (the .mm gets built into the bundle on macOS).
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 3: JamWideFrameDistributor with Subscription RAII (HIGH-2, D-02, D-04, T-19-02)</name>
  <files>
    juce/video/native/JamWideFrameDistributor.h,
    juce/video/native/JamWideFrameDistributor.cpp,
    tests/test_frame_distributor.cpp,
    tests/test_frame_distributor_lifetime.cpp
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §4 (full Frame Distributor Architecture, including the header sketch at lines 380-422, the threading analysis at lines 426-441, and the lifetime rule at line 458)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md HIGH-2 finding (the suggested Subscription RAII pattern is option (b) — picked here for cleanest JUCE-style component lifecycle integration)
    - libs/juce/modules/juce_video/capture/juce_CameraDevice.h:200-204 (the "any thread" callback contract — drives this whole design)
    - tests/test_rawdata_send.cpp (existing test pattern in this codebase — main()-style harness with `assert()`/exit-code-driven, JAMWIDE_BUILD_TESTS=1 compile def, juce::juce_core link via CMake)
  </read_first>
  <behavior>
    - Behavior 1: `registerSubscriber(s)` returns a `Subscription` RAII handle. Holding the handle keeps `s` registered. Destroying the handle (going out of scope / being reset) unregisters `s` AND blocks until any in-flight `publish()` for that subscriber returns.
    - Behavior 2: `publish(image)` called from any thread fans the image to all currently-registered subscribers via `Subscriber::onFrame(const juce::Image&)`. Implementation uses snapshot iteration outside the publish mutex.
    - Behavior 3 (HIGH-2 mitigation): Concurrent invariant — if thread A is mid-iteration of the publish snapshot calling `subscriber->onFrame(image)`, and thread B drops the corresponding `Subscription`, thread B BLOCKS in `~Subscription()` until thread A's `onFrame` returns. After `~Subscription()` returns, the subscriber pointer is GUARANTEED not to be dereferenced by any future publish. No use-after-free, ever.
    - Behavior 4: `Subscriber::onFrame` is invoked OUTSIDE the distributor's main mutex — verifiable by a test subscriber that tries to register a 2nd subscriber from inside its own `onFrame` and observes no deadlock.
    - Behavior 5: `peakFps_` atomic is updated by `publish()` based on time-deltas between successive publishes. The accessor is readable from any thread. (This is the Claude's Discretion item Q3 from RESEARCH §12 — INCLUDED in Phase 19 per researcher's recommendation; supports beta UAT.)
    - Behavior 6: Destructor `assert(subscribers_.empty())` is reachable in debug builds when a Subscription is leaked. Test verifies the contract programmatically (no assert in test process; the test asserts that all Subscription handles must be reset before the distributor goes out of scope).
  </behavior>
  <action>
    1. **Fill `juce/video/native/JamWideFrameDistributor.h`** with the full HIGH-2-aware design:
       ```cpp
       #pragma once
       #include <juce_graphics/juce_graphics.h>
       #include <atomic>
       #include <condition_variable>
       #include <memory>
       #include <mutex>
       #include <unordered_map>
       #include <vector>

       namespace jamwide {

       class JamWideFrameDistributor {
       public:
           // Subscriber interface. onFrame may be called on the camera-callback thread
           // ("any thread" per JUCE). Subscribers MUST be thread-safe and MUST NOT block.
           class Subscriber {
           public:
               virtual ~Subscriber() = default;
               virtual void onFrame(const juce::Image& image) = 0;
           };

           // RAII handle returned from registerSubscriber. Holding the handle keeps the
           // subscriber active. ~Subscription() removes the subscriber AND blocks until
           // any in-flight publish() iterating over the subscriber's onFrame returns.
           // This is the HIGH-2 mitigation: callers can construct/destroy components
           // freely; no use-after-free on the camera-callback thread.
           class Subscription {
           public:
               Subscription() = default;
               Subscription(Subscription&& other) noexcept;
               Subscription& operator=(Subscription&& other) noexcept;
               Subscription(const Subscription&) = delete;
               Subscription& operator=(const Subscription&) = delete;
               ~Subscription();
               bool isActive() const noexcept { return owner_ != nullptr; }
           private:
               friend class JamWideFrameDistributor;
               Subscription(JamWideFrameDistributor* owner, std::uint64_t id);
               JamWideFrameDistributor* owner_ = nullptr;
               std::uint64_t id_ = 0;
           };

           JamWideFrameDistributor();
           ~JamWideFrameDistributor();

           // Returns a moveable RAII handle. Caller must keep it alive while the
           // subscriber should receive frames.
           [[nodiscard]] Subscription registerSubscriber(Subscriber* s);

           // Publish a frame to all currently-registered subscribers. Iterates a
           // snapshot outside the lock. Safe to call from any thread.
           void publish(const juce::Image& image);

           // Peak FPS observed since last publish gap reset (Q3 — debug accessor).
           float getPeakFps() const noexcept { return peakFps_.load(std::memory_order_relaxed); }

       private:
           // Called by ~Subscription. Removes the entry and waits for in-flight
           // publish iterations referencing it to exit.
           void unregisterAndWait(std::uint64_t id);

           struct Entry {
               Subscriber* sub = nullptr;
               // In-flight refcount: incremented while a publish snapshot holds this
               // entry and is about to invoke onFrame; decremented after onFrame returns.
               std::atomic<int> inFlight{0};
           };

           mutable std::mutex regMu_;
           std::condition_variable cv_;
           std::unordered_map<std::uint64_t, std::shared_ptr<Entry>> entries_;
           std::uint64_t nextId_ = 1;

           std::atomic<float> peakFps_{0.0f};
           std::atomic<std::int64_t> lastPublishNanos_{0};
       };

       } // namespace jamwide
       ```

    2. **Fill `juce/video/native/JamWideFrameDistributor.cpp`** with:
       - `Subscription` move ctor/op + destructor (destructor calls `owner_->unregisterAndWait(id_)`).
       - `registerSubscriber(Subscriber* s)`:
         - Lock `regMu_`.
         - Generate id = nextId_++.
         - `entries_[id] = std::make_shared<Entry>(); entries_[id]->sub = s;`.
         - Release lock.
         - Return `Subscription(this, id)`.
       - `publish(const juce::Image& image)`:
         a. Take `regMu_`, COPY `entries_` into a local `std::vector<std::shared_ptr<Entry>> snapshot` (note: snapshot holds `shared_ptr` to each `Entry`, NOT to the `Subscriber*` — entries can be removed from the map without invalidating the snapshot).
         b. For each entry in snapshot: `entry->inFlight.fetch_add(1, std::memory_order_acquire);`
         c. Release `regMu_`.
         d. For each entry in snapshot: if `entry->sub != nullptr` (it's been signalled as "ok to call" by unregisterAndWait if it sets sub to nullptr) call `entry->sub->onFrame(image)`. Then `entry->inFlight.fetch_sub(1, std::memory_order_release); cv_.notify_all();`. Note: setting `sub` to nullptr from `unregisterAndWait` is a CHOICE — alternative is to keep it set and rely on the wait-loop alone. We adopt the wait-loop alone (simpler, no atomic load on sub inside publish).
         e. **Refinement (simpler):** Skip the sub-nullable trick. publish() takes snapshot under lock, bumps refcounts, releases lock, calls onFrame, decrements. unregisterAndWait removes from map under lock, then waits on cv_ until that entry's inFlight==0. shared_ptr keeps the Entry alive as long as either the map OR the snapshot holds a reference.
         f. Update `peakFps_` via the inter-publish time delta (using `std::chrono::steady_clock::now().time_since_epoch().count()`). On first publish (lastPublishNanos_==0), set lastPublishNanos_ and skip. Otherwise compute `fps = 1e9 / delta_ns; peakFps_.store(std::max(peakFps_.load(), fps), std::memory_order_relaxed);`.
       - `unregisterAndWait(id)`:
         a. Lock `regMu_`.
         b. Find `entries_[id]`; capture local `std::shared_ptr<Entry> entry = entries_[id]; entries_.erase(id);`.
         c. Wait on `cv_` while `entry->inFlight.load(std::memory_order_acquire) > 0`. Releases lock during wait per `cv_.wait(lock, predicate)` pattern.
         d. Return. shared_ptr destructor cleans up `Entry`.
       - Destructor: under `regMu_`, assert `entries_.empty()` in debug builds (use `JUCE_ASSERT(...)` or a plain `assert(entries_.empty())`).

    3. **Fill `tests/test_frame_distributor.cpp`** as a `int main()` pure-C++ test. Include `<juce_graphics/juce_graphics.h>` for `juce::Image`. Test scenarios:
       - **Test 1 (basic fan-out)**: Register 3 mock subscribers (each holds a `std::atomic<int>` count), publish 5 synthetic images (`juce::Image(juce::Image::ARGB, 16, 16, true)`), assert each subscriber received exactly 5 `onFrame` calls. Drop the Subscriptions, observe distributor empty.
       - **Test 2 (Subscription move semantics)**: Construct subscription S, move-assign to S2, observe S no longer active and S2 receives frames. Drop S2, observe S2 unsubscribed.
       - **Test 3 (concurrent publish stress)**: 2 producer threads call `publish` 1000 times each in parallel. Register/unregister a subscriber from the main thread between publishes (loop 100 times: register → wait briefly → drop subscription). Assert no crash, no deadlock, no use-after-free. Run for 1 second wall clock.
       - **Test 4 (no callback while holding lock)**: Subscriber A's onFrame calls `distributor.registerSubscriber(&B)`. Publish a frame. Assert no deadlock (timeout 5s; if exceeded exit 99). B's subscription is added; subsequent publishes hit B.
       - **Test 5 (peakFps tracking)**: Publish 10 frames with 10ms sleep between publishes; assert getPeakFps() returns a value in [50, 200] (loose tolerance for scheduling jitter).
       Use `assert()` with `std::exit(1)` on failure. Exit code 0 = all pass.

    4. **Fill `tests/test_frame_distributor_lifetime.cpp`** — the HIGH-2 mitigation test:
       - **Scenario**: Reproduce the "callback in progress, subscriber destroyed" race deterministically.
       - **Setup**: Create a custom subscriber whose `onFrame` waits on a `std::promise<void>` so the test can pin the subscriber inside `onFrame` for a known duration.
       - **Test 1 (race: subscriber drop during onFrame)**:
         1. Construct a `JamWideFrameDistributor` and register the slow subscriber, obtaining `Subscription sub`.
         2. Start a producer thread that calls `distributor.publish(synthetic_image)`. The producer enters the subscriber's `onFrame` and blocks on the promise.
         3. From the main thread, `sub = JamWideFrameDistributor::Subscription{};` (resets `sub` and triggers `~Subscription`). Record `t_drop = now()`.
         4. Verify `~Subscription` BLOCKS — within a 100 ms window from the main thread side, the move-assign is observably not complete (use a flag set after the assign returns; verify the flag is still false from a 3rd thread that polls).
         5. Set the promise → subscriber's onFrame returns → producer's publish returns → publish's `inFlight.fetch_sub` notifies cv_ → main thread's `~Subscription` returns.
         6. Verify the main thread's "complete" flag flips. Verify no crash. Verify subsequent publishes do NOT call the destroyed subscriber's onFrame (the subscriber's count remains at the pre-drop value).
       - **Test 2 (no UAF if subscriber object is destroyed AFTER ~Subscription returns)**: Same as Test 1 but additionally destroy the subscriber object itself (which is owned by the test scope) after ~Subscription returns. Verify no crash. This proves "after ~Subscription returns, the subscriber pointer is safe to destroy."
       - **Test 3 (no deadlock under contention)**: Spawn 4 producer threads each publishing 1000 times. Spawn 1 register/unregister thread that registers + immediately resets the subscription 1000 times. Run for 2 seconds. Verify no deadlock (test exits 0 within the budget).
       Use `std::thread`, `std::promise<void>`, `std::chrono::steady_clock` for timing. Use `assert()` with `std::exit(1)` on failure.
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target test_frame_distributor test_frame_distributor_lifetime 2>&amp;1 | tail -10 &amp;&amp; cd build-juce-19-test &amp;&amp; ctest -R 'camera_frame_distributor|camera_frame_distributor_lifetime' --output-on-failure</automated>
  </verify>
  <done>
    Both test_frame_distributor and test_frame_distributor_lifetime exit 0; the 5 fan-out scenarios + 3 lifetime scenarios pass; distributor.cpp compiles cleanly into JamWideJuce alongside the other juce/video/native sources. HIGH-2 mitigated and proved by `test_frame_distributor_lifetime` Test 1.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 4: CameraStateMachine pure-C++ class (MEDIUM-2 Paused removal, MEDIUM-3 single code path)</name>
  <files>
    juce/video/native/CameraStateMachine.h,
    juce/video/native/CameraStateMachine.cpp,
    tests/test_camera_state_machine.cpp
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §5 (state-machine table; note: this plan REMOVES Paused per MEDIUM-2 — see Behavior 1 below)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md MEDIUM-2 + MEDIUM-3 (rationale for both changes)
    - .planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md D-09 (popout hide is independent of capture state; "capture continues silently" — no Paused state needed in Phase 19)
  </read_first>
  <behavior>
    - Behavior 1 (MEDIUM-2 closure): The state machine has exactly 6 states — `Idle, Opening, Capturing, Failed, Retrying, Unavailable`. `Paused` from RESEARCH §5 is REMOVED. Reasoning recorded in this plan's `## Addressed Review Findings`: per D-09, closing the popout does NOT pause capture (capture continues silently feeding the distributor with no consumer); per D-29, audio-thread bandwidth-pause coordination is Phase 20's responsibility. Paused will reappear in Phase 22's grid view when bandwidth-pause becomes meaningful per-remote.
    - Behavior 2 (MEDIUM-3 closure): The state machine is a pure-C++ class with NO JUCE dependencies. It exposes `dispatch(CameraEvent event)` which returns the new state and an optional fallback cause. Production code in `JamWideCameraDevice` calls `stateMachine_.dispatch(...)` from message-thread callsites; tests call the same `dispatch(...)` directly. There are NO test-only injection methods on `JamWideCameraDevice` itself.
    - Behavior 3: All 12 CameraEvent values are accepted from any state; invalid combinations are no-ops (the state stays as it was). The Dispatch table is documented inline.
    - Behavior 4: `dispatch` is NOT thread-safe — the caller (JamWideCameraDevice) is responsible for serializing calls via the JUCE message thread. Tests serialize by being single-threaded.
    - Behavior 5: A `DispatchResult` struct returned from `dispatch` carries `{ CameraState newState; std::optional<CameraFallbackCause> fallback; bool startWatchdog; bool stopWatchdog; bool startRetryWorker; bool stopRetryWorker; }` — JamWideCameraDevice reads this to drive its side effects (watchdog timer, retry worker, hardware cleanup).
  </behavior>
  <action>
    1. **Fill `juce/video/native/CameraStateMachine.h`** with the pure-C++ class:
       ```cpp
       #pragma once
       #include "CameraFallbackCause.h"
       #include <cstdint>
       #include <optional>

       namespace jamwide {

       enum class CameraState : int {
           Idle = 0,
           Opening,
           Capturing,
           Failed,
           Retrying,
           Unavailable,
       };

       enum class CameraEvent : int {
           UserToggle,           // user clicked Camera button
           AuthGranted,          // queryCameraAuthorization == Authorized OR requestAccess granted
           AuthDenied,           // Denied or Restricted
           OpenSucceeded,        // openDeviceAsync returned non-null
           OpenFailed,           // openDeviceAsync returned null + error string (Carries failure cause via setFallbackHint)
           FirstFrameReceived,   // distributor saw first frame after Opening
           WatchdogFired,        // first-frame watchdog OR frame-stall watchdog
           RuntimeError,         // onErrorOccurred OR frame-stall while Capturing
           RetryTick,            // RetryWorker fires a reopen attempt
           RetryExhausted,       // 5 attempts gave up
           RecheckPermission,    // D-12 user click in Unavailable state
           Shutdown,             // processor destruction
       };

       struct DispatchResult {
           CameraState newState;
           std::optional<CameraFallbackCause> emitFallback;
           bool startFirstFrameWatchdog = false;
           bool stopFirstFrameWatchdog = false;
           bool startFrameStallWatchdog = false;
           bool stopFrameStallWatchdog = false;
           bool startRetryWorker = false;
           bool stopRetryWorker = false;
           bool startOpenDevice = false;
           bool closeHardware = false;
       };

       class CameraStateMachine {
       public:
           CameraStateMachine() = default;
           CameraState getState() const noexcept { return state_; }

           // Caller (JamWideCameraDevice) sets the fallback hint BEFORE dispatching
           // OpenFailed / WatchdogFired / RuntimeError; the state machine consults it to populate
           // DispatchResult.emitFallback. Reset to None after each dispatch.
           void setFallbackHint(CameraFallbackCause hint) noexcept { hint_ = hint; }

           // Returns the side-effect spec for the transition. Caller actuates side effects.
           DispatchResult dispatch(CameraEvent event) noexcept;

       private:
           CameraState state_ = CameraState::Idle;
           CameraFallbackCause hint_ = CameraFallbackCause::None;
       };

       } // namespace jamwide
       ```

    2. **Fill `juce/video/native/CameraStateMachine.cpp`** — `dispatch` is a switch-on-current-state + switch-on-event nested table. Implement the 6×12 transition matrix per RESEARCH §5 (adjusted to drop Paused — popout-hide events no longer change state). Key transitions:
       - `Idle + UserToggle → Opening` (startOpenDevice=true; auth check happens inside JamWideCameraDevice before dispatch — see Task 6)
       - `Opening + AuthGranted → Opening` (no-op; state-machine receives AuthGranted after `requestAccessForMediaType` completion; remains Opening to await OpenSucceeded)
       - `Opening + AuthDenied → Unavailable` (emitFallback=hint)
       - `Opening + OpenSucceeded → Opening` (startFirstFrameWatchdog=true; still Opening until FirstFrameReceived)
       - `Opening + OpenFailed → Unavailable` (emitFallback=hint; closeHardware=true)
       - `Opening + FirstFrameReceived → Capturing` (stopFirstFrameWatchdog=true; startFrameStallWatchdog=true)
       - `Opening + WatchdogFired → Unavailable` (emitFallback=CameraInUse; closeHardware=true; stopFirstFrameWatchdog=true)
       - `Capturing + UserToggle → Idle` (closeHardware=true; stopFrameStallWatchdog=true)
       - `Capturing + RuntimeError → Failed` (then immediately Failed→Retrying — caller dispatches RetryTick or the state machine self-advances on RuntimeError to Retrying directly; we self-advance: state = Retrying; closeHardware=true; stopFrameStallWatchdog=true; startRetryWorker=true)
       - `Capturing + WatchdogFired → Failed→Retrying` (HIGH-6 — frame-stall watchdog while Capturing; emitFallback=hint if set, otherwise omit and let caller re-emit later)
       - `Retrying + RetryTick → Opening` (startOpenDevice=true)
       - `Retrying + OpenSucceeded → Opening` (startFirstFrameWatchdog=true)
       - `Retrying + RetryExhausted → Unavailable` (emitFallback=CameraInUse; stopRetryWorker=true)
       - `Unavailable + RecheckPermission → Opening` (startOpenDevice=true)
       - `Failed + RetryTick → Opening` (in case the state machine returns Failed momentarily before Retrying — defensive)
       - `*+Shutdown → Idle` (closeHardware=true; stopRetryWorker=true; stopFirstFrameWatchdog=true; stopFrameStallWatchdog=true)
       - All other (state, event) combinations: return DispatchResult{newState=current, no side effects} (silent no-op; logged at caller).

       After computing DispatchResult: set `state_ = result.newState; hint_ = CameraFallbackCause::None;` and return result.

    3. **Fill `tests/test_camera_state_machine.cpp`** — pure-C++. Include only `"video/native/CameraStateMachine.h"` and `"video/native/CameraFallbackCause.h"`. Tests:
       - **Per-cell coverage**: For each non-trivial cell in the 6×12 matrix, dispatch the event from the expected starting state with the relevant hint set, assert the returned DispatchResult matches the documented transition (newState, emitFallback, and at least one of the boolean side-effect flags). This is the explicit "one assertion per transition-table cell" coverage that VALIDATION.md mandates.
       - **Happy-path round-trip**: Idle + UserToggle → Opening (startOpenDevice). + OpenSucceeded → Opening (startFirstFrameWatchdog). + FirstFrameReceived → Capturing (stopFirstFrameWatchdog, startFrameStallWatchdog). + UserToggle → Idle (closeHardware, stopFrameStallWatchdog).
       - **Auth-denied path**: Idle + UserToggle. setFallbackHint(TCCDenied). + AuthDenied → Unavailable (emitFallback==TCCDenied).
       - **Watchdog-on-first-frame**: Idle + UserToggle. + OpenSucceeded → Opening (startFirstFrameWatchdog). + WatchdogFired → Unavailable (emitFallback==CameraInUse).
       - **Mid-session-stall** (HIGH-6 path): drive to Capturing. setFallbackHint(TCCDenied). + WatchdogFired → Failed/Retrying (newState==Retrying; emitFallback==TCCDenied carried). This validates the frame-stall scenario at state-machine level. JamWideCameraDevice Task 6 will dispatch this when its frame-stall timer fires.
       - **Recheck path**: drive to Unavailable. + RecheckPermission → Opening.
       - **Retry exhaustion**: drive to Retrying. + RetryTick 4 times → still Opening/Retrying (depends on subsequent OpenFailed dispatches). After the 5th attempt → setFallbackHint(CameraInUse) + RetryExhausted → Unavailable.
       - **Shutdown from any state**: assert dispatching Shutdown from Idle/Opening/Capturing/Failed/Retrying/Unavailable returns newState==Idle + closeHardware=true.
       - **Defensive no-ops**: dispatching FirstFrameReceived from Idle returns newState==Idle with no side effects.

       Use `assert()` with `std::exit(1)` on failure. Aim for ~30+ assertions covering one per matrix cell.
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target test_camera_state_machine 2>&amp;1 | tail -10 &amp;&amp; cd build-juce-19-test &amp;&amp; ctest -R camera_state_machine --output-on-failure &amp;&amp; cd .. &amp;&amp; grep -c 'enum class CameraState' juce/video/native/CameraStateMachine.h &amp;&amp; ! grep -c 'Paused' juce/video/native/CameraStateMachine.h | grep -v '^0$' | head -1; true</automated>
  </verify>
  <done>
    test_camera_state_machine exits 0 with one assertion per non-trivial transition cell; CameraStateMachine.h contains exactly 6 CameraState values (no Paused — verified by grep returning 0); CameraStateMachine.cpp builds cleanly into both JamWideJuce (via Task 1's target_sources) and the test executable. MEDIUM-2 and MEDIUM-3 closed.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 5: JamWideCameraDevice with generation tokens + frame-stall watchdog (HIGH-3, HIGH-6, D-09/12/20, D-23, T-19-01, T-19-03)</name>
  <files>
    juce/video/native/JamWideCameraDevice.h,
    juce/video/native/JamWideCameraDevice.cpp,
    juce/JamWideJuceProcessor.h,
    juce/JamWideJuceProcessor.cpp,
    tests/test_camera_retry_backoff.cpp,
    tests/test_camera_frame_stall.cpp
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §1 (JUCE API + openDeviceAsync recommendation + watchdog), §2 (TCC pre-check integration with state machine — lines 304-321 are the canonical control flow), §5 (full State Machine & Retry-Backoff including the 7→6 states + retry policy at 1s/2s/4s/8s/16s/give-up-30s)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md HIGH-3 (generation-token pattern, 5 callsites enumerated) + HIGH-6 (continuous frame-stall watchdog every 1000 ms)
    - juce/JamWideJuceProcessor.h (lines 88-125 — read the member layout to locate where nativeCamera_ and frameDistributor_ get inserted)
    - juce/JamWideJuceProcessor.cpp (lines 60-80 — constructor + destructor; the new owners get added per RESEARCH §6 lines 545-548)
    - libs/juce/modules/juce_video/capture/juce_CameraDevice.h:64-117 (full openDevice/openDeviceAsync signatures + Listener subclass shape)
  </read_first>
  <behavior>
    - Behavior 1 (D-10): `JamWideCameraDevice` constructor builds in `CameraState::Idle` regardless of input. `getState()` returns the current `CameraStateMachine` state (atomic via wrapper).
    - Behavior 2 (HIGH-3 mitigation — generation tokens): `JamWideCameraDevice` owns `std::atomic<std::uint64_t> generation_{1}`. Every async callback scheduled by this class captures the generation value at scheduling time and bails on mismatch:
        ```cpp
        const auto myGen = generation_.load(std::memory_order_acquire);
        auto cb = [this, myGen](Args... args) {
            if (generation_.load(std::memory_order_acquire) != myGen) return;
            // safe to touch *this
            ...
        };
        ```
      `shutdown()` calls `generation_.fetch_add(1, std::memory_order_release);` BEFORE doing any teardown. After shutdown, ALL prior async closures detect the bump and return immediately without touching `this`.
    - Behavior 3 (HIGH-3 — five callsites enumerated): Generation token applied at exactly five sites:
        1. `requestCameraAuthorization` completion handler (TCC grant/deny callback)
        2. `juce::CameraDevice::openDeviceAsync` result callback
        3. First-frame watchdog timer (3000 ms after `openDeviceAsync` callback fires with valid device)
        4. `juce::CameraDevice::onErrorOccurred` lambda
        5. RetryWorker reopen callback (the do-reopen closure passed to `juce::MessageManager::callAsync`)
      Each closure has a comment `// HIGH-3: generation check`.
    - Behavior 4 (HIGH-6 mitigation — continuous frame-stall watchdog): While the state machine is in `Capturing`, a `juce::Timer` fires every 1000 ms on the message thread. At each tick: compute `gap_ms = now_ms - lastFrameMs_.load()`. If `gap_ms > FRAME_STALL_THRESHOLD_MS` (default 2000 ms), the device:
        a. Calls `jamwide::queryCameraAuthorization()` (synchronous on macOS).
        b. If status == Denied/Restricted → setFallbackHint(TCCDenied or HostLacksEntitlement per `classifyDenialCause`); dispatch `CameraEvent::WatchdogFired` to state machine; state machine returns Retrying (or Unavailable for TCC denial); emit fallback to listener.
        c. If status == Authorized → setFallbackHint(CameraInUse); dispatch WatchdogFired; state machine returns Retrying.
        d. If status == NotApplicable (Windows) → setFallbackHint(CameraInUse); dispatch WatchdogFired.
      The frame-stall timer is started when state transitions Capturing-entry and stopped on Capturing-exit (the state machine's startFrameStallWatchdog / stopFrameStallWatchdog flags drive this from Task 4's DispatchResult).
    - Behavior 5: `toggle()` from Idle invokes a) `jamwide::queryCameraAuthorization()`, then b) on Authorized → dispatch UserToggle+AuthGranted → state=Opening + `juce::CameraDevice::openDeviceAsync`; on Denied/Restricted → setFallbackHint(classifyDenialCause) + dispatch UserToggle+AuthDenied → state=Unavailable + emit fallback cause to listener; on NotDetermined → call `requestCameraAuthorization` then re-dispatch via callAsync. NotApplicable → proceed straight to openDeviceAsync.
    - Behavior 6: When `openDeviceAsync` callback fires with a valid `CameraDevice*`, attach as `Listener`, start the 3-second first-frame watchdog timer. First `imageReceived` cancels the watchdog (state machine: stopFirstFrameWatchdog, startFrameStallWatchdog) and transitions to `Capturing`.
    - Behavior 7: When `openDeviceAsync` callback fires with `nullptr` + non-empty error string, classify cause from RESEARCH §9 cause matrix (TCCDenied / HostLacksEntitlement / WindowsPrivacyBlock / NoHardware) and dispatch OpenFailed → state=Unavailable + emit cause.
    - Behavior 8: Retry timer runs on a dedicated `juce::Thread` subclass — NOT the camera-callback thread, NOT the audio thread, NOT the message thread (D-20 + RESEARCH §5 lines 500-507).
    - Behavior 9: All state transitions are serialized through `juce::MessageManager::callAsync` so the state member is touched on a consistent thread (message thread). The retry worker schedules a callback back to message thread to perform the reopen.
    - Behavior 10 (D-23 — locked decision): Camera events are logged via `juce::Logger::writeToLog`. Four event categories emit a log line: (a) camera open success/failure, (b) TCC denial detection, (c) retry attempts (each backoff tick + give-up), (d) frame-distributor errors / state transitions Capturing→Failed/Retrying. Log strings prefixed with `[JamWideCamera]`. All log calls execute on the message thread.
  </behavior>
  <action>
    1. **Fill `juce/video/native/JamWideCameraDevice.h`** — replace Task 1's stub with the full class:
       ```cpp
       #pragma once
       #include "CameraFallbackCause.h"
       #include "CameraStateMachine.h"
       #include "CameraAuthorization.h"
       #include <juce_video/juce_video.h>
       #include <atomic>
       #include <chrono>
       #include <memory>
       #include <mutex>

       namespace jamwide {

       class JamWideFrameDistributor;

       class JamWideCameraDevice {
       public:
           class FallbackListener {
           public:
               virtual ~FallbackListener() = default;
               virtual void onCameraFallback(CameraFallbackCause cause) = 0;
               virtual void onCameraStateChanged(CameraState newState) = 0;
           };

           JamWideCameraDevice(JamWideFrameDistributor& distributor, FallbackListener* listener = nullptr);
           ~JamWideCameraDevice();

           // Public API used by editor + processor
           void toggle();
           void recheckPermission();
           void setQualityPreset(int preset);
           int getQualityPreset() const noexcept { return qualityPreset_.load(std::memory_order_relaxed); }
           CameraState getState() const noexcept;
           juce::String getDeviceName() const;
           float getPeakFps() const;
           void shutdown();   // HIGH-3: increments generation, releases hardware
           void setFallbackListener(FallbackListener* listener);

       private:
           // HIGH-3: generation token. All async closures capture-by-value the current generation
           // and bail if it changes before they run. shutdown() increments via fetch_add.
           std::atomic<std::uint64_t> generation_{1};

           // Last frame timestamp for HIGH-6 frame-stall detection. Updated by CameraListener::imageReceived.
           std::atomic<std::int64_t> lastFrameMs_{0};

           // Constants tunable for UAT (LOW-1).
           static constexpr int FIRST_FRAME_WATCHDOG_MS = 3000;
           static constexpr int FRAME_STALL_THRESHOLD_MS = 2000;
           static constexpr int FRAME_STALL_POLL_MS = 1000;
           static constexpr int RETRY_BUDGET_MS = 30000;
           static constexpr int RETRY_MAX_ATTEMPTS = 5;

           class CameraListener;
           class FirstFrameWatchdog;   // juce::Timer subclass
           class FrameStallWatchdog;   // juce::Timer subclass
           class RetryWorker;          // juce::Thread subclass

           CameraStateMachine stateMachine_;
           std::unique_ptr<juce::CameraDevice> juceCamera_;
           std::unique_ptr<CameraListener> listenerForwarder_;
           std::unique_ptr<FirstFrameWatchdog> firstFrameWatchdog_;
           std::unique_ptr<FrameStallWatchdog> frameStallWatchdog_;
           std::unique_ptr<RetryWorker> retryWorker_;

           JamWideFrameDistributor& distributor_;
           FallbackListener* fallbackListener_ = nullptr;

           std::atomic<int> qualityPreset_{1};   // 0=Low, 1=Medium, 2=High
           juce::String deviceName_;
           mutable std::mutex deviceNameMu_;

           // Implementation entry points (all run on the message thread; serialized via callAsync)
           void handleUserToggle();
           void handleAuthResult(CameraAuthStatus status, std::uint64_t myGen);
           void onOpenResult(juce::CameraDevice* dev, const juce::String& error, std::uint64_t myGen);
           void onFirstFrame();   // dispatched from CameraListener::imageReceived via callAsync
           void onFirstFrameWatchdogFired(std::uint64_t myGen);
           void onFrameStallTick(std::uint64_t myGen);
           void onRuntimeError(const juce::String& error, std::uint64_t myGen);
           void onRetryTick(std::uint64_t myGen);
           void onRetryExhausted();
           void actuateDispatchResult(const DispatchResult& result);
           CameraFallbackCause classifyDenialCause(CameraAuthStatus status) const;
           CameraFallbackCause classifyOpenFailure(const juce::String& error) const;
       };

       } // namespace jamwide
       ```

    2. **Fill `juce/video/native/JamWideCameraDevice.cpp`** — full implementation. Key elements:

       - **CameraListener (inner class)**: `juce::CameraDevice::Listener` subclass. `imageReceived(const juce::Image& img)` does THREE things (in this order):
         a. `lastFrameMs_.store(juce::Time::currentTimeMillis(), std::memory_order_release)` — HIGH-6 hot-path update.
         b. `distributor_.publish(img)` — fan out to subscribers.
         c. If `firstFrameSeen_.exchange(true) == false`: `juce::MessageManager::callAsync([this, myGen=generation_.load()]() { if (generation_.load() != myGen) return; onFirstFrame(); });` — dispatch state machine transition. HIGH-3 generation check applied.

       - **FirstFrameWatchdog (`juce::Timer` subclass)**: `timerCallback()` invokes `parent.onFirstFrameWatchdogFired(myGen)` where myGen is captured at construction. Single-shot via `stopTimer()` inside the callback.

       - **FrameStallWatchdog (`juce::Timer` subclass)**: `timerCallback()` runs every FRAME_STALL_POLL_MS (1000 ms). Each tick checks `gap = now_ms - parent.lastFrameMs_.load()`. If `gap > FRAME_STALL_THRESHOLD_MS` (2000 ms), invokes `parent.onFrameStallTick(myGen)`.

       - **RetryWorker (`juce::Thread` subclass)**: holds `std::atomic<int> attemptIdx_{0}`. `run()` loop: while `! threadShouldExit() && attemptIdx_ < RETRY_MAX_ATTEMPTS`: wait `(1 << attemptIdx_) * 1000` ms (1, 2, 4, 8, 16 seconds), then on wake call `juce::MessageManager::callAsync([this, myGen=generation_]() { if (generation_.load() != myGen) return; parent.onRetryTick(myGen); });`. After RETRY_MAX_ATTEMPTS: `juce::MessageManager::callAsync([this, myGen]() { if (generation_.load() != myGen) return; parent.onRetryExhausted(); });` and exit.

       - **`handleUserToggle()`**: read `stateMachine_.getState()`. If Idle/Unavailable: call `jamwide::queryCameraAuthorization()` synchronously. Switch on status:
         - `Authorized` → `actuateDispatchResult(stateMachine_.dispatch(CameraEvent::UserToggle))` then immediately `actuateDispatchResult(stateMachine_.dispatch(CameraEvent::AuthGranted))` (advance to Opening) then `actuateDispatchResult({startOpenDevice=true})` to schedule openDeviceAsync.
         - `NotDetermined` → call `jamwide::requestCameraAuthorization([this, myGen=generation_.load()](CameraAuthStatus status) { juce::MessageManager::callAsync([this, myGen, status]() { if (generation_.load() != myGen) return; handleAuthResult(status, myGen); }); });` — HIGH-3 generation check on TCC completion handler.
         - `Denied/Restricted` → `stateMachine_.setFallbackHint(classifyDenialCause(status))`; dispatch `UserToggle` then `AuthDenied`. State → Unavailable. Emit fallback to listener.
         - `NotApplicable` → skip pre-check; dispatch UserToggle + AuthGranted; schedule openDeviceAsync.
         If state == Capturing: dispatch UserToggle → Idle (state machine returns closeHardware=true → call internal closeCamera()).

       - **`handleAuthResult(status, myGen)`**: called via callAsync from TCC completion (HIGH-3 already checked at the callAsync site). Dispatch AuthGranted or AuthDenied to state machine; actuate result.

       - **`actuateDispatchResult(result)`**: read the boolean flags + emitFallback + newState:
         - If `startOpenDevice`: call `scheduleOpenDevice(generation_.load())` which calls `juce::CameraDevice::openDeviceAsync(0, [this, myGen](juce::CameraDevice* dev, const juce::String& err) { juce::MessageManager::callAsync([this, myGen, dev, err]() { if (generation_.load() != myGen) return; onOpenResult(dev, err, myGen); }); }, minW, minH, maxW, maxH)` per quality preset.
         - If `startFirstFrameWatchdog`: construct `firstFrameWatchdog_ = std::make_unique<FirstFrameWatchdog>(*this, generation_.load()); firstFrameWatchdog_->startTimer(FIRST_FRAME_WATCHDOG_MS);`.
         - If `stopFirstFrameWatchdog`: `firstFrameWatchdog_.reset();`.
         - If `startFrameStallWatchdog`: `frameStallWatchdog_ = std::make_unique<FrameStallWatchdog>(*this, generation_.load()); lastFrameMs_.store(juce::Time::currentTimeMillis()); frameStallWatchdog_->startTimer(FRAME_STALL_POLL_MS);`.
         - If `stopFrameStallWatchdog`: `frameStallWatchdog_.reset();`.
         - If `startRetryWorker`: ensure retryWorker_ exists; signal it to start its loop.
         - If `stopRetryWorker`: signal exit; reset retryWorker_.
         - If `closeHardware`: remove listener; juceCamera_.reset();
         - If emitFallback is set AND fallbackListener_ != nullptr: `fallbackListener_->onCameraFallback(*emitFallback);` (also log via D-23).
         - Always notify state change: `fallbackListener_->onCameraStateChanged(result.newState);`.

       - **`onOpenResult(dev, err, myGen)`**: HIGH-3 generation already checked at the callAsync site. If `dev != nullptr`:
         - `juceCamera_.reset(dev);`
         - `juceCamera_->addListener(listenerForwarder_.get());`
         - `juceCamera_->onErrorOccurred = [this, myGen](const juce::String& err) { juce::MessageManager::callAsync([this, myGen, err]() { if (generation_.load() != myGen) return; onRuntimeError(err, myGen); }); };` // HIGH-3 site #4
         - Capture device name under deviceNameMu_.
         - `actuateDispatchResult(stateMachine_.dispatch(CameraEvent::OpenSucceeded));` (which sets startFirstFrameWatchdog=true).
         - Log `"[JamWideCamera] Open OK: device=" + getDeviceName()`.
       - Else (dev == nullptr):
         - `stateMachine_.setFallbackHint(classifyOpenFailure(err));`
         - `actuateDispatchResult(stateMachine_.dispatch(CameraEvent::OpenFailed));`
         - Log `"[JamWideCamera] Open FAILED: err=" + err + " cause=" + causeToString(classifyOpenFailure(err))`.

       - **`onFirstFrame()`**: dispatch FirstFrameReceived. State machine transitions Opening→Capturing; actuate stops firstFrameWatchdog, starts frameStallWatchdog. Log `"[JamWideCamera] First frame received"`.

       - **`onFirstFrameWatchdogFired(myGen)`**: HIGH-3 generation check (already done at callsite). Set hint to CameraInUse. Dispatch WatchdogFired → state machine returns Opening→Unavailable. actuateDispatchResult emits fallback. Log `"[JamWideCamera] First-frame watchdog fired after 3000 ms"`.

       - **`onFrameStallTick(myGen)`**: HIGH-3 generation check. This is the HIGH-6 mitigation core:
         - Re-query `jamwide::queryCameraAuthorization()`.
         - If status == Denied/Restricted → `stateMachine_.setFallbackHint(classifyDenialCause(status));` — TCCDenied or HostLacksEntitlement.
         - Else if status == Authorized → `stateMachine_.setFallbackHint(CameraFallbackCause::CameraInUse);`.
         - Else (NotApplicable) → setFallbackHint(CameraInUse).
         - Dispatch CameraEvent::WatchdogFired → state machine returns Capturing→Retrying (or per its table).
         - actuateDispatchResult emits fallback, stops frameStallWatchdog, starts retryWorker.
         - Log `"[JamWideCamera] Frame stall detected (gap=" + juce::String(gap_ms) + " ms, auth=" + authStatusToString(status) + ") — transitioning Capturing → Retrying"`.

       - **`onRuntimeError(err, myGen)`**: Set hint per classifyOpenFailure. Dispatch RuntimeError → state machine returns Capturing→Retrying. Log `"[JamWideCamera] Runtime error: " + err`.

       - **`onRetryTick(myGen)`**: HIGH-3 generation check. Dispatch RetryTick → state machine returns Retrying→Opening; actuate startOpenDevice. Log `"[JamWideCamera] Retry attempt N/5 (delay=Xms, cumulative=Yms)"` per Behavior 10c.

       - **`onRetryExhausted()`**: setFallbackHint(CameraInUse); dispatch RetryExhausted → state machine returns Retrying→Unavailable. Log `"[JamWideCamera] Retry exhausted after 5 attempts; transitioning to Unavailable"`.

       - **`shutdown()`**: `generation_.fetch_add(1, std::memory_order_release);` FIRST. Then stop watchdogs (reset unique_ptrs). Signal retryWorker exit + reset. Remove listener from juceCamera_. Reset juceCamera_. Dispatch Shutdown to state machine (state→Idle). NO MessageManager::callAsync from here — shutdown is synchronous on the message thread. All in-flight async closures whose callAsync has already fired will observe the bumped generation and return immediately.

       - **Destructor**: calls `shutdown()`.

       - **`recheckPermission()`** (D-12): re-query auth + dispatch RecheckPermission → state machine returns Unavailable→Opening; actuate startOpenDevice. Per the same path as handleUserToggle.

       - **`classifyDenialCause(status)`**:
         ```cpp
         bool isPlugin = ! juce::JUCEApplicationBase::isStandaloneApp();
         if (status == CameraAuthStatus::Restricted) return CameraFallbackCause::TCCDenied;
         if (status == CameraAuthStatus::Denied) {
             return isPlugin ? CameraFallbackCause::HostLacksEntitlement
                             : CameraFallbackCause::TCCDenied;
         }
         return CameraFallbackCause::None;
         ```

       - **`classifyOpenFailure(error)`**:
         ```cpp
         if (juce::CameraDevice::getAvailableDevices().isEmpty()) return CameraFallbackCause::NoHardware;
         #if JUCE_WINDOWS
         return CameraFallbackCause::WindowsPrivacyBlock;
         #else
         return CameraFallbackCause::TCCDenied;
         #endif
         ```

       Quality preset → (minW,minH,maxW,maxH) lookup: Low=(320,240,320,240), Medium=(640,480,640,480), High=(1280,720,1280,720).

    3. **Wire into JamWideJuceProcessor**:
       - `juce/JamWideJuceProcessor.h` — add `#include "video/native/JamWideCameraDevice.h"` and `#include "video/native/JamWideFrameDistributor.h"`. Add members right after `videoCompanion` (around line 119):
         `std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor;`
         `std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera;`
         Public accessor: `jamwide::JamWideCameraDevice* getNativeCamera() { return nativeCamera.get(); }` and `jamwide::JamWideFrameDistributor* getFrameDistributor() { return frameDistributor.get(); }` for 19-02's editor wiring.
       - `juce/JamWideJuceProcessor.cpp` — in the constructor (after the existing `videoCompanion = std::make_unique<jamwide::VideoCompanion>(*this);` line):
         `frameDistributor = std::make_unique<jamwide::JamWideFrameDistributor>();`
         `nativeCamera = std::make_unique<jamwide::JamWideCameraDevice>(*frameDistributor, nullptr);` (listener set later by editor)
       - Destructor (LIFO order, BEFORE `videoCompanion.reset();`):
         `if (nativeCamera) nativeCamera->shutdown();`   // HIGH-3: ensure generation bumped before destruction
         `nativeCamera.reset();`
         `frameDistributor.reset();`
       - Add `if (nativeCamera) nativeCamera->shutdown();` call in `releaseResources()` (DAW lifecycle hook — but only if state is Capturing/Opening; releaseResources is called between bypass cycles, NOT shutdown — refine: call shutdown only from destructor; from releaseResources, the camera stays alive).
         **Correction**: do NOT call shutdown from releaseResources — that is for audio resources only. shutdown is destructor-only. releaseResources should be a no-op for the camera.

    4. **Fill `tests/test_camera_retry_backoff.cpp`** — exercises the RetryWorker timing in isolation. Since RetryWorker depends on JUCE Thread + MessageManager, the test cannot run RetryWorker directly without a JUCE runtime. Approach: factor the backoff-schedule into a pure-C++ helper struct inside `CameraStateMachine.h`:
       ```cpp
       struct RetryBackoff {
           static constexpr int MAX_ATTEMPTS = 5;
           int delayMs(int attemptIdx) const noexcept { return (1 << attemptIdx) * 1000; }   // 1000, 2000, 4000, 8000, 16000
           int cumulativeMs(int afterAttempt) const noexcept;   // 1000, 3000, 7000, 15000, 31000
           bool isExhausted(int attemptIdx) const noexcept { return attemptIdx >= MAX_ATTEMPTS; }
       };
       ```
       Test scenarios:
       - **Test 1**: For attemptIdx 0..4, assert delayMs returns 1000/2000/4000/8000/16000 exactly.
       - **Test 2**: cumulativeMs(5) returns 31000 (1+2+4+8+16 in seconds * 1000).
       - **Test 3**: isExhausted(0..4) is false; isExhausted(5) is true.
       - **Test 4**: A successful "reopen mid-retry" simulation: drive CameraStateMachine via dispatch sequence Idle+UserToggle+AuthGranted → Opening + OpenSucceeded + FirstFrameReceived → Capturing + RuntimeError → Retrying + RetryTick (open scheduled) + OpenSucceeded + FirstFrameReceived → Capturing. Assert retry attempts counter resets (i.e., a NEW RuntimeError after recovery starts the backoff from 1000 ms again, not from where it left off). This is a state-machine-level check on retry semantics.

    5. **Fill `tests/test_camera_frame_stall.cpp`** — HIGH-6 mitigation test via the state machine + a virtualized "frame stall" simulation:
       - Cannot drive the actual `FrameStallWatchdog` `juce::Timer` from a pure-C++ test (no JUCE runtime).
       - Instead: drive CameraStateMachine through the path Idle+UserToggle+AuthGranted+OpenSucceeded+FirstFrameReceived → Capturing.
       - Simulate frame-stall detection: set `stateMachine_.setFallbackHint(CameraFallbackCause::TCCDenied);` (the auth re-query returned Denied) and dispatch `CameraEvent::WatchdogFired`.
       - Assert: state machine returns Capturing→Retrying (or Unavailable depending on table — per Task 4's spec); emitFallback==TCCDenied; stopFrameStallWatchdog=true; startRetryWorker=true.
       - Test variant: set hint to CameraInUse (auth still Authorized but no frames) — same path, different cause.
       - Test variant: set hint to HostLacksEntitlement (plugin context, auth re-queried Denied in DAW host) — same path, different cause.
       - Verify a follow-up RetryTick from Retrying restarts Opening (proves the recovery loop is reachable).
       - Test variant: dispatch a second WatchdogFired from Retrying (defensive — should be a no-op since state machine isn't expecting watchdog during Retrying); assert state unchanged.
       This proves the state machine handles the HIGH-6 paths correctly; the actual Timer firing is exercised by the manual UAT cell in 19-03 (Cell 5: permission-revoke roundtrip).
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target test_camera_retry_backoff test_camera_frame_stall JamWideJuce 2>&amp;1 | tail -10 &amp;&amp; cd build-juce-19-test &amp;&amp; ctest -R 'camera_retry_backoff|camera_frame_stall' --output-on-failure &amp;&amp; cd .. &amp;&amp; grep -c 'std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera' juce/JamWideJuceProcessor.h &amp;&amp; grep -c 'std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor' juce/JamWideJuceProcessor.h &amp;&amp; grep -c 'generation_.fetch_add' juce/video/native/JamWideCameraDevice.cpp &amp;&amp; test "$(grep -c 'generation_.load' juce/video/native/JamWideCameraDevice.cpp)" -ge 5 &amp;&amp; grep -c 'FRAME_STALL' juce/video/native/JamWideCameraDevice.h &amp;&amp; test "$(grep -c '\[JamWideCamera\]' juce/video/native/JamWideCameraDevice.cpp)" -ge 5</automated>
  </verify>
  <done>
    Both test_camera_retry_backoff and test_camera_frame_stall exit 0; JamWideJuce target builds cleanly with nativeCamera/frameDistributor members; constructor inits them; destructor calls shutdown() (which bumps generation_) then resets in LIFO order. The five enumerated async-callback sites all check `generation_.load()` before touching state. The continuous frame-stall watchdog timer fires every 1000 ms in Capturing state; on a 2000 ms gap it re-queries auth and transitions to Retrying with the correct fallback cause. HIGH-3 and HIGH-6 closed.
  </done>
</task>

</tasks>

<verification>

## Plan-Level Verification

```bash
# 1. CMake configures, JamWideJuce builds, AND test stubs exist
set -e
cmake -S . -B build-juce-19-01 -DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_BUILD_TESTS=ON -G Ninja
cmake --build build-juce-19-01 --target JamWideJuce 2>&1 | tail -10

# 2. JUCE_USE_CAMERA + entitlement + permission text are set
grep -c 'JUCE_USE_CAMERA=1' CMakeLists.txt          # >= 1
grep -c 'CAMERA_PERMISSION_ENABLED TRUE' CMakeLists.txt   # == 1
grep -c 'com.apple.security.device.camera' JamWide.entitlements   # == 1

# 3. Wave 0 tests green (5 in this plan + 2 stubs for downstream plans)
cd build-juce-19-01
cmake --build . --target test_frame_distributor test_frame_distributor_lifetime test_camera_state_machine test_camera_retry_backoff test_camera_frame_stall 2>&1 | tail -10
ctest -R 'camera_frame_distributor|camera_frame_distributor_lifetime|camera_state_machine|camera_retry_backoff|camera_frame_stall' --output-on-failure

# 4. JamWide.app standalone Info.plist contains NSCameraUsageDescription with configured string
cmake --build . --target JamWideJuce_Standalone 2>&1 | tail -10
plutil -extract NSCameraUsageDescription raw "$(find . -name 'JamWide.app' -type d -print -quit)/Contents/Info.plist"

# 5. Built bundle declares the camera entitlement (locally-built)
codesign --display --entitlements - "$(find . -name 'JamWide.app' -type d -print -quit)" 2>&1 | grep -c 'device.camera'   # == 1

# 6. HIGH-3 generation token applied at 5 sites (count semantically; min 5)
test "$(grep -c 'generation_.load' juce/video/native/JamWideCameraDevice.cpp)" -ge 5

# 7. HIGH-6 frame-stall constants present
grep -c 'FRAME_STALL_THRESHOLD_MS' juce/video/native/JamWideCameraDevice.h   # == 1
grep -c 'FRAME_STALL_POLL_MS' juce/video/native/JamWideCameraDevice.h        # == 1

# 8. MEDIUM-2 Paused removed
test "$(grep -c '\bPaused\b' juce/video/native/CameraStateMachine.h)" -eq 0

# 9. MEDIUM-3 single code path (test_camera_state_machine compiles CameraStateMachine.cpp directly)
grep -c 'CameraStateMachine.cpp' CMakeLists.txt   # >= 3 (state-machine test, retry-backoff test, frame-stall test, JamWideJuce target)
```

</verification>

<success_criteria>

This plan succeeds when:

1. **HIGH-1 closed** — CMake configure AND JamWideJuce build BOTH succeed at end of every task in this plan; no `target_sources()` references a missing file.
2. **HIGH-2 closed** — `Subscription` RAII destructor blocks until in-flight `onFrame` exits; `test_frame_distributor_lifetime` proves the race deterministically.
3. **HIGH-3 closed** — generation token applied at 5 enumerated async-callback sites; `shutdown()` bumps generation BEFORE teardown; all in-flight closures return early.
4. **HIGH-6 closed** — continuous frame-stall watchdog fires every 1000 ms while Capturing; on 2000 ms gap re-queries auth and transitions Capturing → Retrying (or Unavailable) with correct fallback cause.
5. **MEDIUM-2 closed** — CameraStateMachine has exactly 6 states; Paused is removed.
6. **MEDIUM-3 closed** — pure-C++ CameraStateMachine class is the single transition source; production and tests both call `dispatch(...)` directly.
7. **MEDIUM-4 closed** — License preflight runs before juce_video is linked; Risk C surfaces a checkpoint for user decision if absent.
8. **MEDIUM-5 closed** — all 7 test executables are pure-C++ (no link against JamWideJuce); each compiles required production .cpp files directly.
9. **Risk E closed** — `target_compile_definitions(JamWideJuce PUBLIC JUCE_USE_CAMERA=1)`; juce::juce_video linked.
10. **PKG-04 entitlements portion shipped** — JamWide.entitlements + bundle Info.plist contain camera entries.
11. **TCC pre-check available** — `jamwide::queryCameraAuthorization()` returns valid status on macOS; Windows returns NotApplicable.
12. **Frame distributor passes lifetime test** — `test_frame_distributor_lifetime` exits 0, demonstrating no UAF under callback-during-destruction.
13. **State machine exhaustive** — one assertion per non-trivial transition cell in `test_camera_state_machine`.
14. **JamWideJuceProcessor owns the camera** — nativeCamera + frameDistributor unique_ptr members exist; constructor inits; destructor calls shutdown() then LIFO reset.
15. **Camera starts in Idle (D-10)** — default state is Idle; no auto-open.

</success_criteria>

<output>
Create `.planning/phases/19-camera-capture-permission-ux/19-01-SUMMARY.md` summarizing:
- Risk C resolution outcome (one of: closed via upgrade-clause grep / user-confirmed commercial seat / file edit retroactively adding clause / phase replanned)
- All 20 source-file stubs created in Task 1 (list paths + line counts so the executor can verify)
- HIGH-2 mitigation: Subscription RAII implementation choice (option (b)); link to `test_frame_distributor_lifetime` Test 1 results
- HIGH-3 mitigation: 5 generation-token sites enumerated by file:function:line
- HIGH-6 mitigation: FrameStallWatchdog timer parameters (poll 1000 ms, threshold 2000 ms); cite the manual UAT cell that exercises end-to-end
- MEDIUM-2: Paused removed; cite the line count `grep -c Paused CameraStateMachine.h == 0`
- MEDIUM-3: dispatch() is the only state-mutation entry point in production; test calls the same function
- MEDIUM-5: all 7 test executables list their direct `add_executable(... <cpp files>)` inputs (no JamWideJuce link)
- D-23 log lines emitted: list the 4 categories with example log strings
- Any deviations from RESEARCH.md §6 file layout (expect none beyond CameraStateMachine.h being new)
</output>

## Addressed Review Findings

| Codex Finding | Resolution | Task(s) |
|---------------|------------|---------|
| **HIGH-1** (non-buildable CMake order) | Task 1 Step 1 creates ALL source-file stubs (13 production + 7 tests, total 20 files) BEFORE Step 2-5 edit CMakeLists.txt. CMake configure AND `cmake --build ... --target JamWideJuce` both succeed at end of Task 1. | Task 1 (entire) |
| **HIGH-2** (UAF in distributor snapshot iteration) | `Subscription` RAII handle returned from `registerSubscriber`. `~Subscription` calls `unregisterAndWait(id)` which removes the entry from the map under mutex, then waits on a condition variable until that entry's `inFlight` refcount drops to 0. Snapshot iteration in `publish()` increments inFlight per entry while holding lock, releases lock, calls onFrame, decrements + notifies cv on completion. Test `test_frame_distributor_lifetime` reproduces the race deterministically with `std::promise` to pin the subscriber inside onFrame. Codex's option (b) chosen for JUCE-style component lifecycles. | Task 3 (header + impl + 2 tests) |
| **HIGH-3** (raw `this` in async callbacks) | `std::atomic<uint64_t> generation_` cancellation token. Five callsites enumerated in Behavior 3 of Task 5: TCC completion handler, openDeviceAsync result, first-frame watchdog timer, onErrorOccurred, RetryWorker reopen. Each captures `myGen = generation_.load()` at scheduling time and bails on `generation_.load() != myGen` before touching `this`. `shutdown()` bumps `generation_` via `fetch_add` BEFORE teardown. Documented in JamWideCameraDevice.h header. | Task 5 (every async closure) |
| **HIGH-4** (preview tile callAsync UAF) | **Deferred to 19-02** — this finding is about CameraPreviewTile which is created in 19-02 Task 2. The mitigation (juce::AsyncUpdater pattern instead of callAsync) is implemented there. This plan ONLY stages the empty stub for CameraPreviewTile.{h,cpp}. | (19-02 Task 2) |
| **HIGH-5** (privacy modal not fired on real first-launch) | **Deferred to 19-02** — this is about the editor's onCameraClicked lambda gating sequence. Implemented in 19-02 Task 3. | (19-02 Task 3) |
| **HIGH-6** (mid-session revoke not detected) | Continuous FrameStallWatchdog `juce::Timer` fires every 1000 ms while `state==Capturing`. On each tick computes `gap = now_ms - lastFrameMs_`. If `gap > 2000 ms`, re-queries authorization and dispatches `CameraEvent::WatchdogFired` with the correct fallback hint (TCCDenied/HostLacksEntitlement/CameraInUse). State machine returns Capturing→Retrying with emitFallback. `test_camera_frame_stall` validates the state-machine path with virtualized hints. Manual UAT Cell 5 (permission-revoke roundtrip) exercises the Timer firing end-to-end. | Task 5 (FrameStallWatchdog + onFrameStallTick) + Task 4 (state machine handles WatchdogFired from Capturing) + Task 5 test |
| **HIGH-7** (AlertWindow button index mapping wrong) | **Deferred to 19-03** — dialog work is in 19-03 Task 1. Note in interface block of THIS plan documents JUCE's actual button-index mapping (`juce_AlertWindow.h:457-466`: 1→0; 2→{1,0}; 3→{1,2,0}) so 19-03 has the contract handy. | (19-03 Task 1) |
| **MEDIUM-1** (hidden popout cannot be reopened) | **Deferred to 19-02** — Camera button decision tree implemented in 19-02 Task 1. | (19-02 Task 1) |
| **MEDIUM-2** (Paused state underspecified) | Paused REMOVED from CameraStateMachine. 6 states total: Idle, Opening, Capturing, Failed, Retrying, Unavailable. Verified by `grep -c '\bPaused\b' juce/video/native/CameraStateMachine.h == 0`. Rationale recorded in Task 4 Behavior 1: D-09 says popout hide is independent of capture; Paused returns in Phase 22 for grid view per-remote bandwidth-pause. | Task 4 |
| **MEDIUM-3** (state-machine tests bypass production path) | CameraStateMachine extracted as a pure-C++ class with no JUCE deps. `dispatch(CameraEvent)` is the single transition entry point. JamWideCameraDevice calls `stateMachine_.dispatch(...)` from message-thread callsites. Tests call the same `dispatch(...)` directly. NO test-only injection methods on JamWideCameraDevice. | Task 4 (entire) + Task 5 (uses CameraStateMachine internally) |
| **MEDIUM-4** (license check late) | License preflight grep is Task 1 Step 0 — runs BEFORE any CMake edit. Risk C OPEN surfaces a CHECKPOINT requiring user decision (commercial seat / add upgrade clause / replan). The juce_video link in Step 4 only lands AFTER Risk C is closed. Moved from 19-03 to 19-01 preflight. | Task 1 Step 0 |
| **MEDIUM-5** (CMake test linkage shaky) | All 7 test executables are pure-C++ — `add_executable(test_X tests/test_X.cpp juce/video/native/X.cpp ...)`. NO test links against JamWideJuce target. `target_link_libraries` lists only minimal JUCE modules (juce_core / juce_graphics / juce_data_structures / juce_gui_basics as needed). Documented in Task 1 Step 5. | Task 1 Step 5 |
| **MEDIUM-6** (cause detection approximate) | **Deferred to 19-03** — dialog copy softened in 19-03 Task 1 to avoid asserting "Logic Pro doesn't request camera access for itself." | (19-03 Task 1) |
| **LOW-1** (3-second watchdog brittle) | Constants `FIRST_FRAME_WATCHDOG_MS = 3000` and `FRAME_STALL_THRESHOLD_MS = 2000` are exposed in JamWideCameraDevice.h with comments noting UAT tunability. 19-03 UAT checklist Cell 1 (first launch) notes the tuning option. | Task 5 (constants in header) + (19-03 UAT note) |
