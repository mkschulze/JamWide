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
  - juce/video/native/JamWideFrameDistributor.h
  - juce/video/native/JamWideFrameDistributor.cpp
  - juce/video/native/JamWideCameraDevice.h
  - juce/video/native/JamWideCameraDevice.cpp
  - juce/video/native/CameraFallbackCause.h
  - tests/test_frame_distributor.cpp
  - tests/test_camera_state_machine.cpp
  - tests/test_camera_retry_backoff.cpp
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
    - "Plugin target compiles with juce::CameraDevice declarations visible (JUCE_USE_CAMERA=1 defined on JamWideJuce)"
    - "JamWide.entitlements declares com.apple.security.device.camera"
    - "Built macOS bundle's Info.plist contains NSCameraUsageDescription equal to the configured CAMERA_PERMISSION_TEXT"
    - "Calling jamwide::queryCameraAuthorization() on macOS returns one of {NotDetermined, Restricted, Denied, Authorized}"
    - "JamWideFrameDistributor fans a synthetic juce::Image to all registered subscribers via Subscriber::onFrame"
    - "Subscriber unregistered before destruction does not receive any further onFrame calls (removal-safe iteration)"
    - "JamWideCameraDevice state machine transitions Idle → Opening → Capturing on first frame received"
    - "JamWideCameraDevice state machine transitions Opening → Unavailable when queryCameraAuthorization returns Denied"
    - "JamWideCameraDevice watchdog timer fires after 3 seconds without a frame and emits a fallback cause"
    - "Retry backoff produces the sequence 1s, 2s, 4s, 8s, 16s and gives up at 30s elapsed"
    - "Camera state stays Idle on plugin/standalone launch regardless of saved popout state (D-10)"
  artifacts:
    - path: "CMakeLists.txt"
      provides: "JUCE_USE_CAMERA=1 + juce::juce_video link on JamWideJuce; CAMERA_PERMISSION_ENABLED+TEXT; CameraAuthorization platform-conditional sources; 5 new add_test entries"
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
      provides: "Thread-safe frame fan-out with Subscriber interface"
      exports:
        - "class JamWideFrameDistributor"
        - "class JamWideFrameDistributor::Subscriber"
    - path: "juce/video/native/JamWideCameraDevice.h"
      provides: "CameraDevice owner + state machine + retry-backoff + watchdog + cause-detection"
      exports:
        - "enum class CameraState"
        - "enum class CameraFallbackCause"
        - "class JamWideCameraDevice"
        - "class JamWideCameraDevice::FallbackListener"
    - path: "juce/video/native/CameraFallbackCause.h"
      provides: "Enum shared between camera device and dialog (lives in own header so 19-03 dialog can include without pulling full camera device header)"
      contains: "enum class CameraFallbackCause"
    - path: "tests/test_frame_distributor.cpp"
      provides: "CAM-03 frame fan-out + removal-safe iteration coverage"
      min_lines: 80
    - path: "tests/test_camera_state_machine.cpp"
      provides: "CAM-02/CAM-03 state transition matrix coverage"
      min_lines: 120
    - path: "tests/test_camera_retry_backoff.cpp"
      provides: "D-20 retry timing coverage with virtualized clock"
      min_lines: 60
  key_links:
    - from: "JamWideJuceProcessor"
      to: "JamWideCameraDevice"
      via: "std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera_ member"
      pattern: "std::unique_ptr<jamwide::JamWideCameraDevice>"
    - from: "JamWideCameraDevice"
      to: "JamWideFrameDistributor"
      via: "Listener::imageReceived → distributor.publish(image)"
      pattern: "distributor.*publish"
    - from: "JamWideFrameDistributor"
      to: "Subscribers"
      via: "publish() iterates subscribers under brief lock and calls Subscriber::onFrame outside lock"
      pattern: "Subscriber::onFrame|Subscriber\\*"
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
Stand up the entire backend capture pipeline for Phase 19: make the JUCE camera API reachable in the main plugin target (Risk E), declare the macOS camera entitlement (PKG-04 entitlements portion), provide a TCC pre-check shim (D-03, Spike Risk #2), implement the thread-safe frame distributor (D-02, D-04), and ship `JamWideCameraDevice` with the full state machine, exponential-backoff retry, and watchdog timer (D-09, D-12, D-20). Wire the camera owner into `JamWideJuceProcessor`. Land the three Wave 0 test scaffolds that exercise this code.

Purpose: Without this plan, every other Phase 19 deliverable is non-buildable — `juce::CameraDevice` is hidden behind `#if JUCE_USE_CAMERA || DOXYGEN` (Risk E) and the cause-aware fallback dialog (19-03) has no cause-classification source. This plan creates the entire backend so 19-02 (UI) and 19-03 (entitlements + fallback dialog + UAT) can land in parallel in Wave 2.

Output: A buildable JamWideJuce target with a constructed-but-Idle camera pipeline (D-10), three green unit tests, and a working TCC pre-check on macOS that returns one of four valid auth states.
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

CameraState enum (DEFINE IN THIS PLAN at juce/video/native/JamWideCameraDevice.h):
  namespace jamwide {
    enum class CameraState : int {
      Idle = 0,
      Opening,
      Capturing,
      Paused,
      Failed,
      Retrying,
      Unavailable,
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

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-19-01 | Tampering (TCC bypass) | JamWideCameraDevice / TCC pre-check | mitigate | jamwide::queryCameraAuthorization() called BEFORE openDeviceAsync; watchdog timer (3s default) detects "Authorized but no frames" failure mode — emits CameraFallbackCause::CameraInUse so 19-03 dialog can surface |
| T-19-02 | Information Disclosure (use-after-free on camera-callback thread) | JamWideFrameDistributor | mitigate | std::mutex mu_ guards subscribers_ vector; iterators copied under lock, Subscriber::onFrame invoked OUTSIDE lock; documented rule "every Subscriber MUST be unregistered before destruction"; assert(subscribers_.empty()) in distributor destructor |
| T-19-04 | Privacy (camera-on without ack) | JamWideCameraDevice initial state | mitigate | D-10 enforced: CameraState constructor initialises to Idle regardless of stored popout state; nativeCamera_->toggle() requires explicit user click; privacyAck check is 19-02's responsibility (gates broadcast in Phase 20) — Phase 19 enforces idle-on-launch only |
| T-19-05 | Entitlement spoofing (verify in codesigned bundle, not just source file) | JamWide.entitlements + CAMERA_PERMISSION_ENABLED | mitigate | This plan adds the entitlement and the JUCE permission text; verification that they survive codesign is 19-03's verify_camera_entitlement.sh — but this plan's UAT instruction includes running `codesign --display --entitlements - <bundle>` once on the locally-built artifact |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: CMake + entitlements plumbing (Risk E + PKG-04 entitlements portion)</name>
  <files>
    CMakeLists.txt,
    JamWide.entitlements
  </files>
  <read_first>
    - CMakeLists.txt (read lines 1-50, 145-210, 365-470 — discover existing target structure, juce_add_plugin block, BrowserDetect dispatch, video_spike pattern, JAMWIDE_BUILD_TESTS block)
    - JamWide.entitlements (current 4-key plist)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §7 (entitlements details), §11 (test infrastructure), §13 Risk E (JUCE_USE_CAMERA scope)
    - libs/juce/extras/Build/CMake/JUCEUtils.cmake (skim for CAMERA_PERMISSION_ENABLED handling near line 367-368 — confirms JUCE recognizes this property)
  </read_first>
  <behavior>
    - Behavior 1: `cmake -S . -B build-juce -DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_BUILD_TESTS=ON` configures successfully and the JamWideJuce target shows `juce::juce_video` in its link list.
    - Behavior 2: A grep of build artifacts confirms `JUCE_USE_CAMERA=1` is set on the JamWideJuce compile flags, not just the video_spike target.
    - Behavior 3: A built macOS bundle's Info.plist contains both `NSMicrophoneUsageDescription` (existing) AND `NSCameraUsageDescription` with value "JamWide uses your webcam to share video with NINJAM peers."
    - Behavior 4: `JamWide.entitlements` contains `com.apple.security.device.camera = true` in addition to the three existing keys.
    - Behavior 5: All three new test executable names (`test_frame_distributor`, `test_camera_state_machine`, `test_camera_retry_backoff`) are declared in CMakeLists.txt under `if(JAMWIDE_BUILD_TESTS)` (but bodies are stubbed — they get implemented in tasks 3/4); two additional test names (`test_camera_cause_mapping` from 19-03 and `test_plugin_state_v3_v4` from 19-02) are ALSO pre-staged here so Wave 2 plans don't fight over CMakeLists.txt edits.
    - Behavior 6: BrowserDetect-style platform dispatch added for CameraAuthorization sources: `juce/video/native/CameraAuthorization_mac.mm` on APPLE, `juce/video/native/CameraAuthorization_windows.cpp` otherwise. Sources are added even though the files don't exist yet (next task creates them) — CMake config will succeed but build will fail until Task 2 lands. Task 2 must land in same commit OR this task should also create empty stub files in Task 2's locations.
  </behavior>
  <action>
    Make exactly four edits — in this order:

    1. **JamWide.entitlements** — Add `<key>com.apple.security.device.camera</key><true/>` as a fifth key, preserving the existing four. Per D-28 + PKG-04 entitlements portion.

    2. **CMakeLists.txt — juce_add_plugin block (lines ~145-168)** — Immediately after the existing `MICROPHONE_PERMISSION_TEXT "..."` line (line 167), add two new arguments to `juce_add_plugin(JamWideJuce ...)`:
       `CAMERA_PERMISSION_ENABLED TRUE`
       `CAMERA_PERMISSION_TEXT "JamWide uses your webcam to share video with NINJAM peers."`
       Per D-28 + RESEARCH §7. JUCE will plumb NSCameraUsageDescription into the bundle Info.plist.

    3. **CMakeLists.txt — JUCE_USE_CAMERA + juce::juce_video link** — Find the `target_sources(JamWideJuce PRIVATE ...)` block right after `juce_generate_juce_header(JamWideJuce)`. After the block ends, add:
       - `target_compile_definitions(JamWideJuce PUBLIC JUCE_USE_CAMERA=1)` — closes Risk E.
       - Add `juce::juce_video` to the existing `target_link_libraries(JamWideJuce PRIVATE ...)` call for the plugin target (find the existing one and append the module).
       - Add CameraAuthorization platform dispatch immediately after the existing BrowserDetect lines at CMakeLists.txt:200-205, following the same `if(APPLE) ... else() ...` pattern:
         `if(APPLE)`
         `    target_sources(JamWideJuce PRIVATE juce/video/native/CameraAuthorization_mac.mm)`
         `else()`
         `    target_sources(JamWideJuce PRIVATE juce/video/native/CameraAuthorization_windows.cpp)`
         `endif()`
       - Add `target_sources(JamWideJuce PRIVATE juce/video/native/JamWideFrameDistributor.cpp juce/video/native/JamWideCameraDevice.cpp)` in the cross-platform section (siblings to the dispatch).

    4. **CMakeLists.txt — Pre-stage all 5 new test executables under `if(JAMWIDE_BUILD_TESTS)` (line 406+)**. Add five blocks immediately after the existing `test_video_fourcc` block (or before `test_video_sync` — doesn't matter). For each test, follow the test_rawdata_send.cpp pattern (include dirs from `${CMAKE_CURRENT_SOURCE_DIR}` and `${CMAKE_CURRENT_SOURCE_DIR}/src`, JAMWIDE_BUILD_TESTS=1 compile def). Add tests with these EXACT names so the CTest regex `-R camera` matches all four camera-prefixed tests:
       `add_executable(test_frame_distributor tests/test_frame_distributor.cpp)` + `add_test(NAME camera_frame_distributor COMMAND test_frame_distributor)`
       `add_executable(test_camera_state_machine tests/test_camera_state_machine.cpp)` + `add_test(NAME camera_state_machine COMMAND test_camera_state_machine)`
       `add_executable(test_camera_retry_backoff tests/test_camera_retry_backoff.cpp)` + `add_test(NAME camera_retry_backoff COMMAND test_camera_retry_backoff)`
       `add_executable(test_camera_cause_mapping tests/test_camera_cause_mapping.cpp)` + `add_test(NAME camera_cause_mapping COMMAND test_camera_cause_mapping)`
       `add_executable(test_plugin_state_v3_v4 tests/test_plugin_state_v3_v4.cpp)` + `add_test(NAME plugin_state_v3_v4 COMMAND test_plugin_state_v3_v4)`
       Each links against `JamWideJuce` IF and only if the test needs JUCE types. For test_frame_distributor and test_camera_state_machine and test_camera_retry_backoff and test_camera_cause_mapping — these need juce::juce_core (no JUCE plugin link). For test_plugin_state_v3_v4 — link against `JamWideJuce` to access state load/save (this test is implemented in 19-02; in this task its source file is empty and CMake config tolerates that — CMake doesn't require sources to exist at configure time; only at build time).
       Use `target_link_libraries(test_frame_distributor PRIVATE juce::juce_core juce::juce_graphics)` etc., as juce::Image needs juce_graphics.

       CRITICAL: do NOT touch the existing test_video_fourcc / test_rawdata_send / test_video_sync / test_njclient_atomics wiring.

       CRITICAL: this plan must NOT use the `_${PADDED_PHASE}_${PLAN}_PLAN.md`-style command syntax. All edits done with the Edit/Write tools.

    Note: Task 2 will create CameraAuthorization_mac.mm/.cpp and CameraFallbackCause.h. Task 3 creates the distributor (and `tests/test_frame_distributor.cpp`). Task 4 creates the camera device + state machine (and BOTH `tests/test_camera_state_machine.cpp` and `tests/test_camera_retry_backoff.cpp`). The .cpp source files referenced from CMake will exist by end of this plan. The five test .cpp source files: three are created/filled in this plan — `tests/test_frame_distributor.cpp` in Task 3, and `tests/test_camera_state_machine.cpp` + `tests/test_camera_retry_backoff.cpp` in Task 4. The other two (`tests/test_plugin_state_v3_v4.cpp`, `tests/test_camera_cause_mapping.cpp`) are created HERE in Task 1 with `// stub — implemented in 19-02 / 19-03` and a trivial `int main() { return 0; }` so CMake build succeeds at end of THIS plan. 19-02 will FILL `test_plugin_state_v3_v4.cpp` and 19-03 will FILL `test_camera_cause_mapping.cpp`.
  </action>
  <verify>
    <automated>cmake -S . -B build-juce-19-test -DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_BUILD_TESTS=ON -G Ninja 2>&amp;1 | tail -30 &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'JUCE_USE_CAMERA=1' &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'CAMERA_PERMISSION_ENABLED TRUE' &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'CameraAuthorization_mac.mm' &amp;&amp; grep -v '^#' CMakeLists.txt | grep -c 'add_test(NAME camera_frame_distributor' &amp;&amp; grep -c 'com.apple.security.device.camera' JamWide.entitlements</automated>
  </verify>
  <done>
    cmake configure succeeds; JUCE_USE_CAMERA=1 appears at least once in CMakeLists.txt; CAMERA_PERMISSION_ENABLED TRUE appears at least once; the three new add_test entries are visible; entitlements file contains the camera key. (Build will still fail at this point because source files don't exist — but that's fine for this task; Tasks 2-4 land the source files.)
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: CameraAuthorization shim (D-03, Spike Risk #2)</name>
  <files>
    juce/video/native/CameraAuthorization.h,
    juce/video/native/CameraAuthorization_mac.mm,
    juce/video/native/CameraAuthorization_windows.cpp,
    juce/video/native/CameraFallbackCause.h
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §2 "macOS TCC Pre-Check" (full section, including the .mm skeleton at lines 247-275)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §3 "Windows Camera Backend" (the no-pre-check-on-desktop finding)
    - juce/video/BrowserDetect_mac.mm and juce/video/BrowserDetect_win.cpp (existing precedent for cross-platform .mm/.cpp dispatch — these files already exist and demonstrate the pattern)
  </read_first>
  <behavior>
    - Behavior 1: `jamwide::queryCameraAuthorization()` on macOS returns one of {NotDetermined, Restricted, Denied, Authorized} based on `[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]`.
    - Behavior 2: `jamwide::queryCameraAuthorization()` on Windows returns `NotApplicable`.
    - Behavior 3: `jamwide::requestCameraAuthorization(callback)` on macOS invokes `[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL)]`; the callback receives `Authorized` if `BOOL granted == YES`, else `Denied`. The callback runs on an unspecified thread per Apple's contract — callers must `MessageManager::callAsync` if they touch UI.
    - Behavior 4: `jamwide::requestCameraAuthorization(callback)` on Windows synchronously invokes callback with `NotApplicable`.
    - Behavior 5: `CameraFallbackCause.h` defines the enum WITHOUT including any heavy header (only `<cstdint>` if needed) so 19-03's dialog and 19-02's UI code can include it without pulling JUCE camera/video module.
  </behavior>
  <action>
    1. **Create `juce/video/native/CameraAuthorization.h`** — Cross-platform interface header. Declare `namespace jamwide {}` containing the `enum class CameraAuthStatus : int { NotDetermined=0, Restricted=1, Denied=2, Authorized=3, NotApplicable=4 };` (matches `AVAuthorizationStatus` int values 0-3 plus the Windows sentinel 4) and the two free functions: `CameraAuthStatus queryCameraAuthorization();` and `void requestCameraAuthorization(std::function<void(CameraAuthStatus)> callback);`. Include only `<functional>` and `<cstdint>`. No JUCE includes — this header is intended to be cheap to include from anywhere.

    2. **Create `juce/video/native/CameraAuthorization_mac.mm`** — Objective-C++ implementation per RESEARCH §2 lines 247-275. `#import <AVFoundation/AVFoundation.h>` and `#include "CameraAuthorization.h"`. Inside `namespace jamwide { ... }` implement `queryCameraAuthorization()` (switch on `[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]` four cases, defensive default `Denied`) and `requestCameraAuthorization(callback)` (call `[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) { callback(granted ? Authorized : Denied); }]`). Verbatim from RESEARCH §2 lines 254-273.

    3. **Create `juce/video/native/CameraAuthorization_windows.cpp`** — Stubs returning `NotApplicable`. Verbatim from RESEARCH §2 lines 278-285. Include only `CameraAuthorization.h`.

    4. **Create `juce/video/native/CameraFallbackCause.h`** — `#pragma once`, `namespace jamwide {}` containing `enum class CameraFallbackCause : int { None=0, TCCDenied, HostLacksEntitlement, CameraInUse, NoHardware, WindowsPrivacyBlock };`. Include only `<cstdint>`. This is in its own header (not buried inside JamWideCameraDevice.h) so that 19-03's CameraStatusDialog and 19-02's button-label state machine can include it without pulling the heavyweight camera-device header.

    No tests for this task — the .mm code is exercised by manual UAT (cannot mock TCC). The state machine test in Task 4 verifies the *consumers* of `CameraAuthStatus`.
  </action>
  <verify>
    <automated>test -f juce/video/native/CameraAuthorization.h &amp;&amp; test -f juce/video/native/CameraAuthorization_mac.mm &amp;&amp; test -f juce/video/native/CameraAuthorization_windows.cpp &amp;&amp; test -f juce/video/native/CameraFallbackCause.h &amp;&amp; grep -c 'enum class CameraAuthStatus' juce/video/native/CameraAuthorization.h &amp;&amp; grep -c 'authorizationStatusForMediaType:AVMediaTypeVideo' juce/video/native/CameraAuthorization_mac.mm &amp;&amp; grep -c 'NotApplicable' juce/video/native/CameraAuthorization_windows.cpp &amp;&amp; grep -c 'enum class CameraFallbackCause' juce/video/native/CameraFallbackCause.h &amp;&amp; cmake --build build-juce-19-test --target JamWideJuce 2>&amp;1 | tail -40</automated>
  </verify>
  <done>
    Four files exist with the expected enum and function declarations; the JamWideJuce target compiles (linker may still complain about missing JamWideCameraDevice symbols until Task 4 — that's fine; what matters here is the .mm + .cpp + .h compile cleanly).
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 3: JamWideFrameDistributor (D-02, D-04, T-19-02 mitigation)</name>
  <files>
    juce/video/native/JamWideFrameDistributor.h,
    juce/video/native/JamWideFrameDistributor.cpp,
    tests/test_frame_distributor.cpp
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §4 (full Frame Distributor Architecture, including the header sketch at lines 380-422, the threading analysis at lines 426-441, and the lifetime rule at line 458)
    - libs/juce/modules/juce_video/capture/juce_CameraDevice.h:200-204 (the "any thread" callback contract — drives this whole design)
    - tests/test_rawdata_send.cpp (existing test pattern in this codebase — main()-style harness with `assert()`/exit-code-driven, JAMWIDE_BUILD_TESTS=1 compile def, juce::juce_core link via CMake)
  </read_first>
  <behavior>
    - Behavior 1: `publish(image)` called from any thread fans the image to all registered subscribers via `Subscriber::onFrame(const juce::Image&)`.
    - Behavior 2: Calling `unregisterSubscriber(s)` from one thread WHILE `publish(image)` is running on another thread is safe — the unregistered subscriber's `onFrame` is NOT called after `unregisterSubscriber` returns (or at most one in-flight `onFrame` invocation may complete; subscribers must handle that by themselves). The test must demonstrate removal-safe iteration via a stress loop.
    - Behavior 3: `Subscriber::onFrame` is invoked OUTSIDE the distributor's internal mutex — verifiable by a test subscriber that tries to `unregisterSubscriber(this)` from inside its own `onFrame` and observes no deadlock.
    - Behavior 4: Distructor `assert(subscribers_.empty())` is part of the contract (debug-only). Test verifies the assert is reachable when a subscriber leaks (test sets a flag instead of crashing in test builds — implementation detail of the test, not the production code).
    - Behavior 5: `peakFps_` atomic is updated by `publish()` based on time-deltas between successive publishes. The accessor is readable from any thread. (This is the Claude's Discretion item Q3 from RESEARCH §12 — INCLUDED in Phase 19 per researcher's recommendation; supports beta UAT.)
  </behavior>
  <action>
    1. **Create `juce/video/native/JamWideFrameDistributor.h`** following RESEARCH §4 lines 380-422 verbatim with the following refinements:
       - Use the implementation choice of an explicit `std::vector<Subscriber*>` + `std::mutex` (NOT `juce::ListenerList` — researcher's call: explicit vector is one-fewer-JUCE-coupling for the encoder subscriber that's coming in Phase 20 from a worker thread).
       - Add `class Subscriber` as a public nested class with virtual `~Subscriber() = default` and pure virtual `void onFrame(const juce::Image& image) = 0;`. Document at the declaration: "may be called on any thread; MUST NOT block".
       - Add `std::atomic<float> peakFps_{0.0f}` and `float getPeakFps() const { return peakFps_.load(std::memory_order_relaxed); }` (Q3 resolved YES per researcher).
       - Document via `// THREADING:` comments at the class header that producer thread = camera-callback "any thread"; register/unregister happen on message thread.

    2. **Create `juce/video/native/JamWideFrameDistributor.cpp`** implementing:
       - `registerSubscriber(Subscriber*)`: take mutex, push_back if not already present, release.
       - `unregisterSubscriber(Subscriber*)`: take mutex, erase the entry, release.
       - `publish(const juce::Image&)`:
         a. Take mutex, COPY subscribers_ into a local `std::vector<Subscriber*> snapshot`, release mutex.
         b. For each `s` in `snapshot`: call `s->onFrame(image)`. (Snapshot ensures iteration is safe if unregisterSubscriber races; the snapshot may include a subscriber that was just unregistered — the documented contract is the caller of `unregisterSubscriber` synchronizes its own destruction with that possibility.)
         c. Update `peakFps_` via the inter-publish time delta (using `std::chrono::steady_clock`). Use a `std::atomic<int64_t>` for `lastPublishNanos_` member to avoid mutex; compute fps = 1e9 / delta, store via `peakFps_.store(std::max(peakFps_.load(), fps))`. Reset on first publish (delta=0 → skip update).
       - Destructor: `assert(subscribers_.empty()); // unregister all subscribers before destruction`. Wrap in `#ifndef NDEBUG`.

    3. **Create `tests/test_frame_distributor.cpp`** as a `juce_add_console_app`-style `int main()` test. Include `<juce_graphics/juce_graphics.h>` for `juce::Image`. Test scenarios:
       - **Test 1 (basic fan-out)**: Register 3 mock subscribers, publish 5 synthetic images, assert each subscriber received exactly 5 `onFrame` calls.
       - **Test 2 (removal-safe iteration)**: Register 3 subscribers; one subscriber's `onFrame` immediately calls `distributor.unregisterSubscriber(this)`. Publish 5 frames. Assert the self-removing subscriber received >=1 and <=5 onFrame calls (the exact number depends on timing; what matters is no crash and no infinite loop).
       - **Test 3 (concurrent publish + unregister stress)**: 2 producer threads call `publish` 1000 times each. 1 register/unregister thread continuously adds and removes a subscriber. Run for 1 second wall clock. Assert no crash and no asserts (ThreadSanitizer-flag-friendly).
       - **Test 4 (no callback while holding mutex)**: Subscriber's `onFrame` registers a 2nd subscriber. Assert no deadlock (timeout of 5s; if exceeded, exit code 99).
       - **Test 5 (peakFps tracked)**: Publish 10 frames at known 10ms intervals (sleep_for(10ms) between publishes); assert getPeakFps() returns a value in [50, 200] (loose tolerance for scheduling jitter).
       Use `assert()` with `exit(1)` on failure. Exit code 0 = all pass.
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target test_frame_distributor 2>&amp;1 | tail -10 &amp;&amp; cd build-juce-19-test &amp;&amp; ctest -R camera_frame_distributor --output-on-failure</automated>
  </verify>
  <done>
    test_frame_distributor exits 0; the 5 test scenarios pass; the distributor compiles cleanly into JamWideJuce alongside the other juce/video/native sources.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 4: JamWideCameraDevice — state machine + retry-backoff + watchdog (D-09/12/20, T-19-01 mitigation)</name>
  <files>
    juce/video/native/JamWideCameraDevice.h,
    juce/video/native/JamWideCameraDevice.cpp,
    juce/JamWideJuceProcessor.h,
    juce/JamWideJuceProcessor.cpp,
    tests/test_camera_state_machine.cpp,
    tests/test_camera_retry_backoff.cpp
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §1 (JUCE API + openDeviceAsync recommendation + watchdog), §2 (TCC pre-check integration with state machine — lines 304-321 are the canonical control flow), §5 (full State Machine & Retry-Backoff including the 7 states + retry policy at 1s/2s/4s/8s/16s/give-up-30s)
    - juce/JamWideJuceProcessor.h (lines 88-125 — read the member layout to locate where nativeCamera_ and frameDistributor_ get inserted)
    - juce/JamWideJuceProcessor.cpp (lines 60-80 — constructor + destructor; the new owners get added per RESEARCH §6 lines 545-548)
    - libs/juce/modules/juce_video/capture/juce_CameraDevice.h:64-117 (full openDevice/openDeviceAsync signatures + Listener subclass shape)
  </read_first>
  <behavior>
    - Behavior 1: `JamWideCameraDevice` constructor builds in `CameraState::Idle` regardless of input — D-10 enforced. `getState()` is atomic and lock-free.
    - Behavior 2: `toggle()` from Idle invokes a) `jamwide::queryCameraAuthorization()`, then b) on Authorized → state=Opening + `juce::CameraDevice::openDeviceAsync`, on Denied/Restricted → state=Unavailable + emit fallback cause to listener, on NotDetermined → call `requestCameraAuthorization` then re-dispatch via callAsync.
    - Behavior 3: When `openDeviceAsync` callback fires with a valid `CameraDevice*`, attach as `Listener`, start a 3-second `juce::Timer`-style watchdog. First `imageReceived` cancels the watchdog and transitions to `Capturing`. If watchdog fires first, emit `CameraFallbackCause::CameraInUse` and transition to `Unavailable`.
    - Behavior 4: When `openDeviceAsync` callback fires with `nullptr` + non-empty error string, classify cause from RESEARCH §9 cause matrix (TCCDenied / HostLacksEntitlement / WindowsPrivacyBlock / NoHardware) and transition to `Unavailable` + emit cause.
    - Behavior 5: Runtime error during `Capturing` (`onErrorOccurred` callback or distributor-detected frame gap) transitions to `Failed` → `Retrying`. Retrying scheduler fires reopens at +1s, +3s, +7s, +15s, +31s elapsed (1+2+4+8+16 cumulative). After 31s elapsed without success, transitions to `Unavailable` with `CameraFallbackCause::CameraInUse` (best-guess).
    - Behavior 6: `toggle()` from Capturing/Paused goes back to Idle (closes the camera, stops the listener).
    - Behavior 7: `recheckPermission()` from Unavailable re-queries auth and either re-opens (if Authorized) or re-emits the fallback cause (if still Denied) — supports D-12.
    - Behavior 8: Retry timer runs on a dedicated `juce::Thread` subclass — NOT the camera-callback thread, NOT the audio thread, NOT the message thread (D-20 + RESEARCH §5 lines 500-507).
    - Behavior 9: All state transitions are serialized through `juce::MessageManager::callAsync` so the state member is touched on a consistent thread (message thread). The retry worker schedules a callback back to message thread to perform the reopen.
    - Behavior 10 (D-23 — locked decision): Camera events are logged via `juce::Logger::writeToLog`. Four event categories MUST emit a log line: (a) camera open success/failure (from `onOpenResult` — log device name + result), (b) TCC denial detection (from `handleAuthResult` when status ∈ {Denied, Restricted} — log the classified `CameraFallbackCause`), (c) retry attempts (from `RetryWorker` each backoff tick — log attempt index + delay + cumulative elapsed), (d) frame-distributor errors / state transitions Capturing→Failed/Retrying (log the trigger and new state). Log strings prefixed with `[JamWideCamera]` so the events filter cleanly in JUCE's log output. Logging happens on the message thread (same thread as state transitions per Behavior 9) — never on the audio thread, never blocking the camera callback.
  </behavior>
  <action>
    1. **Create `juce/video/native/JamWideCameraDevice.h`** — Class declaration. Members:
       - `enum class CameraState { Idle, Opening, Capturing, Paused, Failed, Retrying, Unavailable };` (also exported via the header)
       - Inner class `FallbackListener` with `virtual void onCameraFallback(CameraFallbackCause cause) = 0;` and `virtual void onCameraStateChanged(CameraState newState) = 0;`. The processor (or 19-03's wiring) implements this to forward causes to the dialog. Phase 19-02 hooks the state changes to update the button label.
       - Inner class `RetryWorker : public juce::Thread` (subclass) holding the backoff schedule. Constructor takes a `std::function<void()>` "do-reopen" callback to invoke on the message thread after each backoff tick.
       - Public methods: `JamWideCameraDevice(JamWideFrameDistributor& dist, FallbackListener* listener = nullptr)`, `~JamWideCameraDevice()`, `void toggle()`, `void recheckPermission()`, `void setQualityPreset(int preset)`, `int getQualityPreset() const`, `CameraState getState() const`, `juce::String getDeviceName() const`, `float getPeakFps() const`, `void shutdown()` (releases hardware before destruction; called from processor destructor + releaseResources).
       - Private members: `std::unique_ptr<juce::CameraDevice> juceCamera_`; `std::unique_ptr<juce::Timer> watchdogTimer_`; `std::unique_ptr<RetryWorker> retryWorker_`; nested `class CameraListener : public juce::CameraDevice::Listener { void imageReceived(const juce::Image&) override; }` forwarder; `std::atomic<CameraState> state_{CameraState::Idle}`; `std::atomic<int> qualityPreset_{1}` (1=Medium); `JamWideFrameDistributor& distributor_`; `FallbackListener* fallbackListener_`; bookkeeping for first-frame detection, retry attempt index, watchdog interval (default 3000ms).
       - Quality preset → (minW,minH,maxW,maxH) lookup: Low=(320,240,320,240), Medium=(640,480,640,480), High=(1280,720,1280,720). RESEARCH §1 line 190 notes the macOS backend's still-photo loop may bound effective FPS regardless of these constraints.

    2. **Create `juce/video/native/JamWideCameraDevice.cpp`** — Implementation:
       - Constructor: distributor_(dist), fallbackListener_(listener), state_(Idle). Starts retryWorker_ thread (it sits idle until `signalRetryStart()` is called).
       - Destructor: calls `shutdown()`. shutdown sets state_=Idle, stops watchdog, signals retry worker exit, removes listener from juceCamera_, releases juceCamera_.
       - `toggle()`:
         - If state == Idle/Unavailable: invoke `jamwide::queryCameraAuthorization()`. Switch on the four CameraAuthStatus values:
           - NotDetermined → call `jamwide::requestCameraAuthorization([this](status) { MessageManager::callAsync([this, status]() { handleAuthResult(status); }); })`.
           - Authorized → handleAuthResult(Authorized) directly.
           - Denied/Restricted → emit FallbackCause classified per `classifyDenialCause(status)` (in plugin context, prefer HostLacksEntitlement via `juce::JUCEApplicationBase::isStandaloneApp() == false`; in standalone context, TCCDenied). Set state_=Unavailable, call fallbackListener_->onCameraFallback.
           - NotApplicable (Windows) → skip pre-check; proceed straight to openDeviceAsync.
         - If state == Capturing/Paused: call internal `closeCamera()` which removes listener, releases juceCamera_, transitions state_=Idle.
       - `handleAuthResult(status)`:
         - state_=Opening
         - `juce::CameraDevice::openDeviceAsync(0, [this](CameraDevice* dev, const String& err) { MessageManager::callAsync([this, dev, err]() { onOpenResult(dev, err); }); }, minW, minH, maxW, maxH)` per preset.
       - `onOpenResult(CameraDevice* dev, const String& err)`:
         - If dev is non-null: store as unique_ptr, attach `CameraListener`, attach `onErrorOccurred` lambda, start 3s watchdog timer via `juce::Timer` subclass.
         - If dev is nullptr: classify cause via cause-matrix heuristic (RESEARCH §9):
           - If `juce::CameraDevice::getAvailableDevices().isEmpty()` → NoHardware.
           - Else on Windows → WindowsPrivacyBlock.
           - Else on macOS → TCCDenied (pre-check should have caught this; this is defensive).
           Emit cause; state_=Unavailable.
       - `CameraListener::imageReceived(const Image&)`:
         - Forward to `distributor_.publish(image)`.
         - On first frame: cancel watchdog (atomic flag check), MessageManager::callAsync to transition state_=Capturing and notify fallbackListener_->onCameraStateChanged.
       - Watchdog timer callback:
         - If first-frame flag is unset: state_=Unavailable, cause=CameraInUse (best-guess), emit fallback.
       - `onErrorOccurred(const String&)`:
         - state_=Failed → call retryWorker_->signalRetryStart() which schedules the first reopen at +1s and increments retry index. After 5 retries (1+2+4+8+16=31s), emit cause=CameraInUse, state_=Unavailable.
       - `RetryWorker::run()`: standard `juce::Thread::run()` loop that calls `wait(delayMs)` between attempts; when the wait returns (or the thread is signalled), invoke the do-reopen callback via `MessageManager::callAsync` on the message thread. Tracks `attemptIdx_` and computes `delayMs = 1000 << attemptIdx_` (1000, 2000, 4000, 8000, 16000). Gives up after attemptIdx_==5.
       - `recheckPermission()` (D-12): re-query auth, dispatch like toggle's auth handling.

    3. **Wire into JamWideJuceProcessor**:
       - `juce/JamWideJuceProcessor.h` — add `#include "video/native/JamWideCameraDevice.h"` and `#include "video/native/JamWideFrameDistributor.h"`. Add members right after `videoCompanion` (around line 119):
         `std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor;`
         `std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera;`
         Also add public accessor: `jamwide::JamWideCameraDevice* getNativeCamera() { return nativeCamera.get(); }` for 19-02's editor wiring.
       - `juce/JamWideJuceProcessor.cpp` — in the constructor (after the existing `videoCompanion = std::make_unique<jamwide::VideoCompanion>(*this);` line), add:
         `frameDistributor = std::make_unique<jamwide::JamWideFrameDistributor>();`
         `nativeCamera = std::make_unique<jamwide::JamWideCameraDevice>(*frameDistributor, /* listener = nullptr until 19-03 wires the dialog */ nullptr);`
       - In the destructor (LIFO order, BEFORE `videoCompanion.reset();`), add:
         `nativeCamera.reset();` then `frameDistributor.reset();` (in that order — destroying the camera unregisters its frame listener before the distributor is destroyed).
       - Add `if (nativeCamera) nativeCamera->shutdown();` call in `releaseResources()` (the DAW lifecycle hook — the host signals "I'm done with you" and we release camera hardware).

    4. **Create `tests/test_camera_state_machine.cpp`** — Exercises the 7-state transition matrix. Cannot drive `juce::CameraDevice::openDeviceAsync` without real hardware, so test through a "test-mode" injection point on JamWideCameraDevice. Approach:
       - Add `#ifdef JAMWIDE_BUILD_TESTS` block in JamWideCameraDevice.h exposing test-injection methods: `void injectAuthStatus(CameraAuthStatus)`, `void injectOpenResult(bool success, juce::String error)`, `void injectFrame()`, `void injectError(juce::String error)`, `void injectWatchdogFire()`. Each posts the corresponding event into the state machine the same way real JUCE/AVF callbacks would, so tests can drive the full state graph without real hardware.
       - Test scenarios (1 per transition table cell):
         - Idle + toggle + injectAuthStatus(Authorized) + injectOpenResult(true) + injectFrame → Capturing
         - Idle + toggle + injectAuthStatus(Denied) → Unavailable, cause=TCCDenied (standalone) / HostLacksEntitlement (plugin context)
         - Idle + toggle + injectAuthStatus(Authorized) + injectOpenResult(true) + injectWatchdogFire → Unavailable, cause=CameraInUse
         - Capturing + toggle → Idle
         - Capturing + injectError → Failed → Retrying (state observable)
         - Retrying (after virtualized 31s) → Unavailable, cause=CameraInUse
         - Unavailable + recheckPermission + injectAuthStatus(Authorized) → Opening
         - At least one scenario per state's outgoing transition (RESEARCH §5 transition table).
       - Use a synthetic FallbackListener that records `(state, cause)` events in a vector; assert on the recorded sequence.

    5. **Create `tests/test_camera_retry_backoff.cpp`** — Targets `RetryWorker` in isolation OR via the same JAMWIDE_BUILD_TESTS injection points. Approach:
       - Use a "test-mode" override on RetryWorker that scales delays by 1/100 (so 1s becomes 10ms). Either a `setTestModeScale(int)` accessor or `#ifdef JAMWIDE_BUILD_TESTS` shortened delays.
       - Test 1: Backoff sequence is 1s/2s/4s/8s/16s (in test mode: 10/20/40/80/160ms). Record fire-times; assert deltas are within ±50% (loose tolerance for scheduling jitter — keep test reliable on CI).
       - Test 2: After 5 attempts (total 31s test = 310ms test mode), RetryWorker signals "give up". Assert exactly 5 attempts before give-up.
       - Test 3: A successful reopen in the middle of retry stops further retries.

    6. **Wire D-23 logging into JamWideCameraDevice.cpp** — Add `juce::Logger::writeToLog("[JamWideCamera] …")` calls at the four event sites mandated by Behavior 10:
       - In `onOpenResult`: log on both branches — success path emits `"[JamWideCamera] Open OK: device=<name>"`; failure path emits `"[JamWideCamera] Open FAILED: err=<err> cause=<classifyDenialCause result>"`.
       - In `handleAuthResult`: when status ∈ {Denied, Restricted}, emit `"[JamWideCamera] TCC denied: status=<status> classified=<CameraFallbackCause>"` BEFORE setting state to Unavailable.
       - In `RetryWorker::run` (each backoff tick): emit `"[JamWideCamera] Retry attempt <N>/5: delay=<ms>ms cumulative=<ms>ms"`. On give-up emit `"[JamWideCamera] Retry exhausted after 5 attempts (31s); transitioning to Unavailable"`.
       - In the Capturing→Failed/Retrying transition (Behavior 5 trigger): emit `"[JamWideCamera] Runtime error during capture: <err> — transitioning Capturing → Failed → Retrying"`.
       All log calls execute on the message thread (state transitions are already serialized through MessageManager::callAsync per Behavior 9). NO log calls in the camera-callback `imageReceived` path (would slow the hot path), and NO log calls reachable from the audio thread (which Phase 19 doesn't touch anyway per D-29).
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target test_camera_state_machine test_camera_retry_backoff JamWideJuce 2>&amp;1 | tail -20 &amp;&amp; cd build-juce-19-test &amp;&amp; ctest -R 'camera_state_machine|camera_retry_backoff' --output-on-failure &amp;&amp; cd .. &amp;&amp; grep -c 'std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera' juce/JamWideJuceProcessor.h &amp;&amp; grep -c 'std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor' juce/JamWideJuceProcessor.h &amp;&amp; grep -c 'frameDistributor = std::make_unique' juce/JamWideJuceProcessor.cpp &amp;&amp; grep -c 'nativeCamera = std::make_unique' juce/JamWideJuceProcessor.cpp &amp;&amp; grep -c 'nativeCamera.reset()' juce/JamWideJuceProcessor.cpp &amp;&amp; test "$(grep -c '\[JamWideCamera\]' juce/video/native/JamWideCameraDevice.cpp)" -ge 5</automated>
  </verify>
  <done>
    The state-machine and retry-backoff tests exit 0; JamWideJuce target builds cleanly with nativeCamera/frameDistributor members; constructor inits them; destructor tears them down in LIFO order; releaseResources() calls shutdown(); processor compiles without modification to existing logic.
  </done>
</task>

</tasks>

<verification>

## Plan-Level Verification

```bash
# 1. CMake configures, JamWideJuce target links juce::juce_video, JUCE_USE_CAMERA=1 visible
cmake -S . -B build-juce-19-01 -DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_BUILD_TESTS=ON -G Ninja
grep -c 'JUCE_USE_CAMERA=1' CMakeLists.txt   # >= 2 (once for plugin, once preserved for video_spike)
grep -c 'CAMERA_PERMISSION_ENABLED TRUE' CMakeLists.txt   # == 1

# 2. Entitlement landed in source file
grep -c 'com.apple.security.device.camera' JamWide.entitlements   # == 1

# 3. Wave 0 tests green
cd build-juce-19-01
cmake --build . --target test_frame_distributor test_camera_state_machine test_camera_retry_backoff JamWideJuce 2>&1 | tail -20
ctest -R 'camera_frame_distributor|camera_state_machine|camera_retry_backoff' --output-on-failure

# 4. JamWide.app standalone builds + Info.plist contains NSCameraUsageDescription with the configured string
cmake --build . --target JamWideJuce_Standalone 2>&1 | tail -10
plutil -extract NSCameraUsageDescription raw "$(find . -name 'JamWide.app' -type d -print -quit)/Contents/Info.plist"
# Expect: JamWide uses your webcam to share video with NINJAM peers.

# 5. Built bundle declares the camera entitlement (locally built; full notarization is 19-03's UAT task)
codesign --display --entitlements - "$(find . -name 'JamWide.app' -type d -print -quit)" 2>&1 | grep -c 'device.camera'   # == 1

# 6. Camera component is constructed but stays Idle (no auto-open on launch — D-10)
# Manual check: launch JamWide standalone, observe that no camera LED activates, no TCC prompt appears
# (Tests in this plan exercise the state machine via injection; UAT cell deferred to Phase 24 per VALIDATION.md)
```

</verification>

<success_criteria>

This plan succeeds when:

1. **Risk E closed** — `target_compile_definitions(JamWideJuce PUBLIC JUCE_USE_CAMERA=1)` is set, and `juce::juce_video` is linked into the plugin target.
2. **PKG-04 entitlements portion shipped** — `JamWide.entitlements` contains `com.apple.security.device.camera`; built macOS bundle's Info.plist contains `NSCameraUsageDescription` with the configured text.
3. **TCC pre-check available (Spike Risk #2 closed)** — `jamwide::queryCameraAuthorization()` returns a valid status on macOS; Windows stub returns NotApplicable cleanly.
4. **Frame distributor passes thread-safety test** — `test_frame_distributor` exits 0 including the concurrent publish + unregister stress scenario.
5. **State machine + retry backoff exhaustive** — `test_camera_state_machine` exercises every transition cell from RESEARCH §5; `test_camera_retry_backoff` confirms 1s/2s/4s/8s/16s sequence (test-mode scaled) and give-up at 31s.
6. **JamWideJuceProcessor owns the camera** — `nativeCamera` and `frameDistributor` unique_ptr members exist; constructor inits them; destructor releases LIFO; `releaseResources()` calls `nativeCamera->shutdown()`.
7. **Camera starts in Idle (D-10)** — No state-restoration mechanism in this plan; default state is Idle regardless of saved popout state. 19-02 will add state persistence but D-10 must hold across that work too.

</success_criteria>

<output>
Create `.planning/phases/19-camera-capture-permission-ux/19-01-SUMMARY.md` summarizing:
- Risk E closure (CMake additions, exact lines/values)
- TCC pre-check shim file paths
- Frame distributor's mutex strategy + peakFps_ accessor (Q3 resolved YES)
- State machine + retry-backoff implementation notes (juce::Thread subclass chosen over juce::TimedCallback per RESEARCH §5 line 500)
- Test-mode injection points (`#ifdef JAMWIDE_BUILD_TESTS` blocks on JamWideCameraDevice)
- Any deviations from RESEARCH.md §6 file layout (none expected)
- Risk C note: GPLv2 LICENSE file lacks "or any later version" upgrade clause at top; surfaced to user — 19-03 contains the license-header sanity check task
</output>
