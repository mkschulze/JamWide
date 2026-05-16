---
phase: 19-camera-capture-permission-ux
verified: 2026-05-16T19:00:00Z
status: human_needed
score: 5/5 truths verified (automated portion); 5/5 success-criteria require manual UAT execution
overrides_applied: 0
re_verification:
  previous_status: none
  previous_score: n/a
  gaps_closed: []
  gaps_remaining: []
  regressions: []
human_verification:
  - test: "Cell 1 — macOS standalone happy path (CAM-01 / SC1)"
    expected: "macOS TCC prompt appears; Allow yields preview popout within 3s with live frames"
    why_human: "Requires real camera hardware, real macOS TCC permission dialog, visual confirmation of preview"
  - test: "Cell 2 — Logic Pro AU plugin happy path (CAM-01 / SC2)"
    expected: "Logic Pro hosts JamWide AU; camera grant produces preview within 3s"
    why_human: "Requires Logic Pro install on macOS and the host's own camera entitlement"
  - test: "Cell 3 — REAPER macOS plugin fallback (CAM-02 / SC3)"
    expected: "CameraStatusDialog shows HostLacksEntitlement (softened, MEDIUM-6 wording); 3-button set; Open System Settings opens privacy pane; no crash; audio still works"
    why_human: "Requires REAPER install, SPARTA #82 host-context behavior, visual verification of softened copy"
  - test: "Cell 4 — macOS arm64 standalone (CAM-01 arm64)"
    expected: "Apple Silicon native build works identically to Cell 1"
    why_human: "Apple Silicon hardware required; arm64 build path was uncovered until Phase 23"
  - test: "Cell 5 — Permission revoke roundtrip (CAM-02 / SC4) — HIGH-6 end-to-end"
    expected: "Mid-session toggling JamWide OFF in System Settings stops frames within ~5s; FrameStallWatchdog re-queries auth; CameraStatusDialog appears with TCCDenied cause (NOT CameraInUse); no crash"
    why_human: "Depends on real macOS System Settings toggle interaction and timing of watchdog re-query"
  - test: "Cell 6 — Notarization stapler validate (PKG-04 / D-28)"
    expected: "Release build with JAMWIDE_HARDENED_RUNTIME=ON, codesigned with Team T3KK66Q67T, notarized, stapled, and verify_camera_entitlement.sh --notarize exits 0"
    why_human: "Requires Apple Developer notary credentials (API Key keychain profile)"
  - test: "Cell 7 — Windows standalone happy path (CAM-01 / SC5)"
    expected: "JamWide.exe on Windows x86_64 grants camera preview within 3s"
    why_human: "Requires Windows x86_64 hardware with webcam"
  - test: "Cell 8 — Windows privacy block (CAM-02 Windows)"
    expected: "CameraStatusDialog shows WindowsPrivacyBlock with softened copy and 3 buttons; ms-settings:privacy-webcam URL opens; recheck resumes capture"
    why_human: "Requires Windows Settings -> Privacy -> Camera toggle interaction"
  - test: "Cell 9 — VDO.Ninja coexistence toast (D-27)"
    expected: "Once-per-session non-blocking 'Multiple video stacks active' toast appears when starting native camera while VDO.Ninja is active; toggling repeatedly does not re-show"
    why_human: "Requires both video stacks running concurrently"
  - test: "Cell 10 — First-launch privacy modal sequence (HIGH-5 end-to-end)"
    expected: "tccutil reset + privacyAck delete; click -> TCC prompt with correct NSCameraUsageDescription -> Allow -> NativeCameraPrivacyDialog -> I understand -> preview; second launch skips both modals"
    why_human: "Requires fresh permission state (tccutil reset) and visual confirmation of the dialog sequence in a real run"
  - test: "Risk C — JUCE commercial seat licence confirmation"
    expected: "User confirms JUCE commercial seat covers juce_video for JamWideJuce target"
    why_human: "Legal/licensing decision, not codebase-verifiable"
---

# Phase 19: Camera Capture & Permission UX — Verification Report

**Phase Goal:** Users can grant camera access in JamWide standalone and DAW-hosted plugin and see their local preview rendered on both macOS and Windows, with a graceful fallback when the DAW host does not request camera permission for itself.

**Verified:** 2026-05-16T19:00:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

**Phase Requirements (from PLAN frontmatter + REQUIREMENTS.md):** CAM-01, CAM-02, CAM-03, PKG-04 (entitlements portion)

## Goal Achievement

### Observable Truths (from ROADMAP.md Success Criteria)

The five Success Criteria are all behavioral end-to-end claims that require real hardware + real OS permission dialogs + visual confirmation. The verification splits each into (a) the codebase wiring needed to make the behavior possible (automated) and (b) the user-facing observation (human UAT).

| #   | Truth | Status     | Evidence       |
| --- | ----- | ---------- | -------------- |
| SC1 | macOS standalone: TCC prompt -> grant -> preview within 3s | VERIFIED (code) + HUMAN-UAT (behavior) | NSCameraUsageDescription="JamWide uses your webcam to share video with NINJAM peers." in built `Info.plist` (verified live via `plutil`); CameraAuthorization_mac.mm calls `[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]` + `[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo]`; HIGH-5 first-launch flow in JamWideJuceEditor.cpp `handleCameraIdleClick` dispatches NotDetermined -> requestAccess -> Authorized -> showPrivacyOrToggle -> toggle -> openDeviceAsync. **UAT Cell 1** required for "within 3s" + visual preview |
| SC2 | Logic Pro AU plugin: camera grant via host -> preview | VERIFIED (code) + HUMAN-UAT (behavior) | JUCE_USE_CAMERA=1 verified in 100 compile_commands entries (covers JamWideJuce target). HIGH-5 first-launch flow runs the same path under AU as Standalone. **UAT Cell 2** required |
| SC3 | REAPER macOS: CameraStatusDialog graceful fallback, no crash, audio works | VERIFIED (code) + HUMAN-UAT (behavior) | CameraStatusDialog with HostLacksEntitlement softened copy (no DAW-blame; grep `doesn't request camera access for itself` == 0); 3-button set with Open System Settings deep-link (x-apple.systempreferences URL grep matches in editor); editor's onCameraFallback delegates to dialog with `juce::PluginHostType().getHostDescription()` for {HostName} substitution. **UAT Cell 3** required |
| SC4 | Revoke permission mid-session: preview disappears, fallback shown, no crash | VERIFIED (code) + HUMAN-UAT (behavior) | FrameStallWatchdog (juce::Timer ticking every 1000 ms while Capturing) re-queries `queryCameraAuthorization()` on >2000 ms frame gap (HIGH-6); classifyDenialCause routes to TCCDenied when re-query returns Denied; CameraStatusDialog::reset() called on Opening/Capturing transitions so the next denial re-shows. **UAT Cell 5** required for the end-to-end observation |
| SC5 | Windows standalone: camera prompt -> grant -> preview within 3s | VERIFIED (code) + HUMAN-UAT (behavior) | Windows `CameraAuthorization_windows.cpp` returns NotApplicable (no TCC on desktop); WindowsPrivacyBlock cause routed via openDevice failure with devices visible; `ms-settings:privacy-webcam` deep-link grep matches in editor. **UAT Cells 7 and 8** required |

**Score:** 5/5 truths VERIFIED at the codebase level (automated portion). All 5 require **human UAT** for end-to-end behavioral confirmation per the human verification section. Phase 19 build/test gate was skipped at the orchestrator level (no auto-detected project build command); my live re-run confirmed:

- `cmake --build build-juce --target test_camera_cause_mapping test_frame_distributor test_frame_distributor_lifetime test_camera_state_machine test_camera_retry_backoff test_camera_frame_stall test_plugin_state_v3_v4` -> all 7 tests pass on the main worktree (not just executor self-test)
- `cmake --build build-juce --target JamWideJuce_Standalone` -> builds clean (1 unrelated warning in non-Phase-19 code)
- `plutil -extract NSCameraUsageDescription raw build-juce/.../JamWide.app/Contents/Info.plist` -> exact match to configured CAMERA_PERMISSION_TEXT

### Required Artifacts

Aggregating artifacts across all three plans' must_haves blocks. All paths checked for existence + substantive content + wiring.

| Artifact | Expected | Status | Details |
| -------- | -------- | ------ | ------- |
| `JamWide.entitlements` | com.apple.security.device.camera key | VERIFIED | Contains the key (line 7-8); also has audio-input, network.client, network.server |
| `CMakeLists.txt` (camera section) | JUCE_USE_CAMERA=1, juce_video link, CAMERA_PERMISSION_TEXT, 7 test executables, platform-conditional CameraAuthorization sources | VERIFIED | `JUCE_USE_CAMERA=1` at line 242 and 407; `juce::juce_video` linked at line 264 and 401; CAMERA_PERMISSION_TEXT at line 170; all 7 add_executable test entries present (lines 631-696); CameraAuthorization_mac.mm + windows.cpp dispatched by platform |
| `juce/video/native/CameraAuthorization.h` | enum CameraAuthStatus + queryCameraAuthorization() + requestCameraAuthorization() | VERIFIED | 5 enum values (NotDetermined/Restricted/Denied/Authorized/NotApplicable); both function decls present |
| `juce/video/native/CameraAuthorization_mac.mm` | macOS AVCaptureDevice TCC pre-check + requestAccess | VERIFIED | 30 lines; `[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]` and `[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo]` both present |
| `juce/video/native/CameraAuthorization_windows.cpp` | Windows stub returning NotApplicable | VERIFIED | Present (1143 bytes) |
| `juce/video/native/CameraFallbackCause.h` | enum with 5 causes (None + TCCDenied + HostLacksEntitlement + CameraInUse + NoHardware + WindowsPrivacyBlock) | VERIFIED | Exactly the expected 6 enum members (5 + None defensive) |
| `juce/video/native/JamWideFrameDistributor.{h,cpp}` | Thread-safe pub/sub with Subscription RAII (HIGH-2) | VERIFIED | 17/20 mentions of Subscription/registerSubscriber/unregisterAndWait across the pair; test_frame_distributor_lifetime exercises the race |
| `juce/video/native/CameraStateMachine.{h,cpp}` | Pure-C++ 6-state machine, NO Paused state (MEDIUM-2), single dispatch entry (MEDIUM-3) | VERIFIED | 6 states (Idle/Opening/Capturing/Failed/Retrying/Unavailable); `grep Paused` returns 0; tested via test_camera_state_machine (66 assertions) |
| `juce/video/native/JamWideCameraDevice.{h,cpp}` | Owns juce::CameraDevice + generation-token cancellation at 5+ async sites (HIGH-3) + frame-stall watchdog (HIGH-6) + retry backoff (D-20) | VERIFIED | generation_.load count = 15 (well above the documented "5+ sites"); FRAME_STALL_THRESHOLD_MS=2000, POLL=1000, FIRST_FRAME_WATCHDOG_MS=3000 all constexpr; recheckPermission() at .cpp:176 |
| `juce/video/native/CameraStatusDialog.{h,cpp}` | Cause-aware dialog with HIGH-7 actionFor() helper + MEDIUM-6 softened copy + suppress-after-first-show + reset() | VERIFIED | Action enum (OpenSystemSettings/RecheckPermission/Dismiss); actionFor switches on cause for button count; copyFor has softened HostLacksEntitlement template with {HostName} placeholder substituted via String::replace; show() suppresses on identical cause; grep `doesn't request camera access for itself` == 0 confirms MEDIUM-6 wording |
| `juce/video/native/CameraPreviewWindow.{h,cpp}` | juce::DocumentWindow with 4:3 aspect, hide-not-destroy, JamWideLookAndFeel, ComponentListener for bounds persistence | VERIFIED | 51-line header, 86-line impl; setFixedAspectRatio(4.0/3.0); closeButtonPressed = hide; componentMovedOrResized -> onBoundsChanged callback |
| `juce/video/native/CameraPreviewTile.{h,cpp}` | juce::Component + JamWideFrameDistributor::Subscriber + juce::AsyncUpdater (HIGH-4) + member-order contract (subscription_ LAST) | VERIFIED | 9 AsyncUpdater/triggerAsyncUpdate/handleAsyncUpdate mentions per file; `grep MessageManager::callAsync.*this` == 0; subscription_ member-order-contract comment present |
| `juce/video/native/NativeCameraPrivacyDialog.{h,cpp}` | D-22 first-use modal + isAckResult(int) HIGH-7 prep helper | VERIFIED | 39-line header with `static bool isAckResult(int) noexcept`; 31-line impl with AlertWindow::showAsync 2-button dispatch |
| `juce/JamWideJuceProcessor.{h,cpp}` | Frame distributor + native camera ownership; state schema v4 (cameraPopoutX/Y/W/H + cameraQualityPreset + cameraPrivacyAck + cameraSelectedDevice) with jlimit clamping | VERIFIED | currentStateVersion=4 at .h:91; `std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor` + `std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera`; 7 setProperty calls in getStateInformation; jlimit clamping at lines 836-846; `shutdown()` before reset in dtor |
| `juce/JamWideJuceEditor.{h,cpp}` | FallbackListener implementation; MEDIUM-1 decision-tree onCameraClicked; HIGH-5 handleCameraIdleClick; cameraStatusDialog_ delegation; coexistence toast (D-27); platform-conditional URL launching | VERIFIED | `public jamwide::JamWideCameraDevice::FallbackListener` inheritance (.h:23); `onCameraFallback`/`onCameraStateChanged` overrides; switch-on-state decision tree in onCameraClicked lambda; `cameraStatusDialog_.show(cause, hostName, lambda)` in onCameraFallback; #if JUCE_MAC/JUCE_WINDOWS deep-link URL branches; coexistenceToastShown_ atomic exchange guard |
| `juce/ui/ConnectionBar.{h,cpp}` | cameraButton (TextButton subclass) left of Video, D-11 connect-independent, right-click PopupMenu (quality presets + Stop Camera) | VERIFIED | `class CameraButton : public juce::TextButton` (file-local subclass in .cpp); cameraButton setBounds at line 365 (130px wide); never disabled by Connect state |
| `scripts/verify_camera_entitlement.sh` | PKG-04 codesign-entitlement + Info.plist NSCameraUsageDescription verifier reading from SIGNED bundle (T-19-05) | VERIFIED | Executable; passes `bash -n`; 5 exit codes (1-5); my live re-run against the dev ad-hoc-signed bundle correctly diagnosed missing entitlement (exit 2) — matches expected behavior for unsigned/ad-hoc builds. Release builds with JAMWIDE_HARDENED_RUNTIME=ON expected to pass per UAT Cell 6 |
| `docs/UAT/phase-19-camera-uat-checklist.md` | 10 cells covering CAM-01/02/03 + PKG-04 + HIGH-5 + HIGH-6 + LOW-1 tuning note | VERIFIED | Exactly 10 `^## Cell ` headings; 39 mentions of CAM-01/CAM-02/PKG-04/HIGH-5/HIGH-6/REAPER/VDO.Ninja/LOW-1/FIRST_FRAME_WATCHDOG_MS/stapler validate/coexistence; LOW-1 tuning note present in Cell 1 lines 33-34; Cell 10 covers HIGH-5 end-to-end |
| `tests/test_camera_cause_mapping.cpp` | 5 causes x copy mapping + 5 causes x button mapping + 14 (cause, juceResult) actionFor cells (HIGH-7) + host-name substitution | VERIFIED | 58 assertion calls; Test 3 has the documented 14 explicit assertions for actionFor; `assert(host == tcc)` pins MEDIUM-6 button-set equality; `assert(! host.contains("doesn't request camera access for itself"))` pins MEDIUM-6 copy wording. **Live re-run on main worktree: PASSED 0.38s** |
| `tests/test_frame_distributor.cpp` | CAM-03 frame fan-out + removal-safe iteration | VERIFIED | 14 assertion calls. **Live re-run: PASSED 1.41s** |
| `tests/test_frame_distributor_lifetime.cpp` | HIGH-2 mitigation: in-flight onFrame + ~Subscription blocks; no UAF | VERIFIED | 5 assertion calls. **Live re-run: PASSED 2.47s** |
| `tests/test_camera_state_machine.cpp` | 6-state x 12-event transition matrix coverage | VERIFIED | 66 assertion calls. **Live re-run: PASSED 0.22s** |
| `tests/test_camera_retry_backoff.cpp` | D-20 1/2/4/8/16s schedule + 30s give-up | VERIFIED | 25 assertion calls. **Live re-run: PASSED 0.23s** |
| `tests/test_camera_frame_stall.cpp` | HIGH-6 mitigation: re-queries auth on frame gap | VERIFIED | 18 assertion calls. **Live re-run: PASSED 0.23s** |
| `tests/test_plugin_state_v3_v4.cpp` | v3->v4 schema migration + T-19-03 clamping + privacyAck persistence + HIGH-7 prep isAckResult | VERIFIED | 42 assertion calls; 5 scenarios (defaults / round-trip / clamping / ack persistence / button mapping). **Live re-run: PASSED 0.31s** |
| `CHANGELOG.md` | Phase 19 [Unreleased] entry per D-26 | VERIFIED | Line 12 describes the full Phase 19 feature set (Camera button, popout, MEDIUM-6 softened copy, schema v3->v4, VDO.Ninja coexistence) |

### Key Link Verification

| From | To | Via | Status | Details |
| ---- | -- | --- | ------ | ------- |
| JamWideJuceProcessor (constructor) | JamWideFrameDistributor + JamWideCameraDevice | unique_ptr ownership wired in correct order (distributor before camera) | WIRED | `frameDistributor = make_unique<JamWideFrameDistributor>()` then `nativeCamera = make_unique<JamWideCameraDevice>(*frameDistributor, nullptr)` at .cpp:65-67 |
| JamWideJuceProcessor (destructor) | shutdown-before-reset | `nativeCamera->shutdown(); nativeCamera.reset(); frameDistributor.reset()` order | WIRED | .cpp:83-85; HIGH-3 generation bump occurs in shutdown() |
| JamWideJuceEditor | JamWideCameraDevice::FallbackListener | Inherits public FallbackListener; setFallbackListener(this) in ctor, setFallbackListener(nullptr) in dtor | WIRED | .h:23 inheritance; .cpp:258 register; .cpp:335 unregister |
| Editor::onCameraFallback | CameraStatusDialog::show | `cameraStatusDialog_.show(cause, hostName, [this](Action){...})` | WIRED | .cpp:835 — host name supplied via `juce::PluginHostType().getHostDescription()` (MEDIUM-5 pure-C++ test path preserved) |
| CameraStatusDialog::Action::OpenSystemSettings | macOS System Settings | `juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser()` (single-line per `c016885`) | WIRED | .cpp:843 inside #if JUCE_MAC branch |
| CameraStatusDialog::Action::OpenSystemSettings | Windows Settings | `juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser()` | WIRED | .cpp:845 inside #elif JUCE_WINDOWS branch |
| ConnectionBar Camera button | Editor::onCameraClicked | TextButton::onClick lambda invokes `onCameraClicked()` callback set by editor | WIRED | ConnectionBar.cpp:271; editor's lambda at JamWideJuceEditor.cpp:183 |
| Editor::onCameraClicked | VDO.Ninja coexistence toast | `if (videoCompanion && videoCompanion->isActive() && !coexistenceToastShown_.exchange(true)) { ... AlertWindow::showAsync ... }` | WIRED | .cpp:193-205; atomic once-per-session guard |
| CameraPreviewTile | JamWideFrameDistributor::Subscriber | `registerSubscriber(this)` in ctor returns Subscription RAII held LAST member; ~Subscription blocks in-flight onFrame | WIRED | CameraPreviewTile.cpp; member-order contract documented in .h |
| CameraPreviewTile::onFrame | juce::AsyncUpdater | `triggerAsyncUpdate()` from onFrame; `handleAsyncUpdate()` reads under mutex + `repaint()` (HIGH-4) | WIRED | 9 AsyncUpdater mentions per file; `grep MessageManager::callAsync.*this` returns 0 |
| Editor (handleCameraIdleClick) | jamwide::requestCameraAuthorization | Switch on `queryCameraAuthorization()` -> NotDetermined dispatches requestAuth + MessageManager::callAsync marshal | WIRED | .cpp:879-924; NotDetermined branch lines 890-911 |
| JamWideCameraDevice (FrameStallWatchdog) | classifyDenialCause | Timer ticks every 1000ms while Capturing; on >2000ms gap re-queries auth and dispatches WatchdogFired | WIRED | watchdog timer constants confirmed in .h; classifyDenialCause grep matches in .cpp |
| scripts/verify_camera_entitlement.sh | Signed JamWide.app | `codesign --display --entitlements -` on the bundle (NOT source .entitlements) + `plutil -extract NSCameraUsageDescription raw Info.plist` | WIRED | T-19-05 mitigation pinned to shipped artifact; my live re-run confirmed Info.plist NSCameraUsageDescription exact match |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| -------- | ------------- | ------ | ------------------ | ------ |
| CameraPreviewTile | currentFrame_ (juce::Image) | JamWideFrameDistributor::publish() -> Subscriber::onFrame -> pendingFrame_ -> handleAsyncUpdate copies to currentFrame_ | YES (when JamWideCameraDevice is in Capturing state; runtime-verifiable via UAT Cell 1) | FLOWING (data pipeline wired end-to-end; observation requires running app) |
| CameraStatusDialog | message string | copyFor(cause, hostName) at runtime; juce::PluginHostType().getHostDescription() supplies hostName at the editor's onCameraFallback call site | YES (substitution via juce::String::replace at runtime; pure-C++ test exercises with "TestHost" / "REAPER") | FLOWING |
| ConnectionBar (cameraButton text) | label string | onCameraStateChanged drives setCameraLabel() based on CameraState (Camera / Recheck permission) | YES | FLOWING (state-driven label change; behavioral check is UAT Cell 5/Cell 8 visual confirmation of "Recheck permission" appearing) |
| JamWideJuceProcessor (state v4) | 7 camera properties | getStateInformation writes ValueTree properties from accessor calls (atomic + mutex-protected backing storage); setStateInformation reads with jlimit clamping | YES (test_plugin_state_v3_v4 exercises round-trip + clamping + ack persistence) | FLOWING |

### Behavioral Spot-Checks

Ran live on the verifier machine (macOS x86_64 standalone build):

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| All 7 camera/state tests pass | `cmake --build build-juce --target test_* && cd build-juce && ctest -R 'camera_|plugin_state_v3_v4' --output-on-failure` | 7/7 passed in 4.91s | PASS |
| test_camera_cause_mapping pinpoint | `ctest -R camera_cause_mapping --output-on-failure` | Passed 0.38s | PASS |
| JamWideJuce_Standalone builds | `cmake --build build-juce --target JamWideJuce_Standalone` | Build succeeded; 1 unrelated warning (not Phase-19 code) | PASS |
| NSCameraUsageDescription present in built bundle | `plutil -extract NSCameraUsageDescription raw build-juce/.../JamWide.app/Contents/Info.plist` | "JamWide uses your webcam to share video with NINJAM peers." (exact match) | PASS |
| verify_camera_entitlement.sh runs and diagnoses correctly | `./scripts/verify_camera_entitlement.sh build-juce/.../JamWide.app` | Returns "FAIL: ... not found ... Hint: rebuild ... with JAMWIDE_HARDENED_RUNTIME=ON" (correct diagnostic for ad-hoc build) | PASS (behavior correct; release-build verification is UAT Cell 6) |
| JUCE_USE_CAMERA=1 propagated | `grep -c "JUCE_USE_CAMERA=1" build-juce/compile_commands.json` | 100 (covers all JamWideJuce TUs) | PASS |
| MEDIUM-6 no DAW-blame copy | `grep -c "doesn't request camera access for itself" juce/video/native/CameraStatusDialog.cpp` | 0 | PASS |

### Probe Execution

Phase 19 declared no `scripts/*/tests/probe-*.sh` probes. The verification script (`scripts/verify_camera_entitlement.sh`) was treated as a PKG-04 verifier, not a probe, and was executed live above (see Behavioral Spot-Checks). No additional probes apply.

### Requirements Coverage

Cross-reference of plan-declared requirement IDs against REQUIREMENTS.md:

| Requirement | Source Plan(s) | Description | Status | Evidence |
| ----------- | -------------- | ----------- | ------ | -------- |
| CAM-01 | 19-01, 19-03 | User can grant camera access via OS prompt in standalone + DAW-hosted plugin | SATISFIED (code) + NEEDS HUMAN (behavior) | CameraAuthorization_mac.mm wires AVCaptureDevice; NSCameraUsageDescription configured + emitted in built bundle (live verified); HIGH-5 first-launch flow in editor. **UAT Cells 1/2/4/7 cover the behavioral observation** |
| CAM-02 | 19-03 | Graceful "Camera unavailable" fallback when host lacks entitlement; no crash, audio works | SATISFIED (code) + NEEDS HUMAN (behavior) | CameraStatusDialog with HostLacksEntitlement softened copy + 3-button set + System Settings deep-link; FallbackListener wired in editor. **UAT Cells 3/5/8 cover REAPER / revoke / Windows-block** |
| CAM-03 | 19-01, 19-02 | User sees own local camera preview when access granted | SATISFIED (code) + NEEDS HUMAN (behavior) | CameraPreviewWindow (juce::DocumentWindow, 4:3, hide-not-destroy) + CameraPreviewTile (AsyncUpdater + Subscription RAII) + frame distributor pub/sub; constructed in editor. **UAT Cells 1/2/4/7 cover preview observation** |
| PKG-04 (entitlements portion) | 19-01, 19-03 | JamWide.entitlements declares com.apple.security.device.camera; verifier script reads from signed bundle | SATISFIED | Entitlement key present in JamWide.entitlements; CMake wires CAMERA_PERMISSION_ENABLED + CAMERA_PERMISSION_TEXT into Info.plist (verified live); verify_camera_entitlement.sh ready for release-build verification. **Notarization staple is UAT Cell 6** (Phase 23 codesign portion is a separate scope per REQUIREMENTS.md) |

No requirement IDs orphaned from this phase (REQUIREMENTS.md only assigns CAM-01/02/03 + PKG-04 entitlements to Phase 19). No additional IDs from other phases erroneously claimed.

### Anti-Patterns Found

Scanned all Phase-19-touched files for debt markers and stub patterns.

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |

**Result: 0 debt markers (TODO/FIXME/XXX/TBD/HACK) in any Phase 19 file.** Clean.

**Stub scan:**
- `grep MessageManager::callAsync.*this` in CameraPreviewTile.cpp -> 0 (HIGH-4 satisfied — no this-captures in repaint scheduling)
- `grep doesn't request camera access for itself` in CameraStatusDialog.cpp -> 0 (MEDIUM-6 softened copy confirmed)
- `grep Paused` in CameraStateMachine.h -> 0 (MEDIUM-2 satisfied)
- No `return null`/empty fallthrough patterns in CameraStatusDialog (every case returns a populated string or button set)
- No stub method bodies in JamWideCameraDevice (recheckPermission, shutdown, toggle all have substantive implementations)

### Human Verification Required

See `human_verification:` section in the frontmatter for the 11 items (10 UAT cells + Risk C licence confirmation) that require manual testing. Detailed test steps live in `docs/UAT/phase-19-camera-uat-checklist.md`. This list is the formal hand-off to UAT — none of these can be verified programmatically.

Additional context per `feedback_uat_scope_redflags`: every CAM-01/02/03 happy and sad path is explicitly enumerated; no cell may be deferred to Phase 24 except the per-DAW matrix (Live, Bitwig macOS; REAPER Windows) already documented as out-of-scope.

### Code Review Findings — Post-Merge Observations (19-REVIEW.md)

The post-execution code review (separate from the cross-AI plan review) found 4 Critical UAF risks (CR-01..04) and 7 Warnings. These are **post-merge findings** and were **NOT fixed** as part of Phase 19 — they are recorded for follow-up. Per the verifier task instructions:

> "Factor them into your verification: they relate to safety but the phase goal is 'users can use the camera' — broken-edge cases vs core happy path."

Analysis per finding:

- **CR-01 / CR-02 / CR-03 (UAF in async lambdas capturing `this`):** The dialog/TCC/privacy lambdas capture `this` without `juce::Component::SafePointer`. The UAF requires: dialog open AND plugin/standalone window closed before user clicks. For the **happy path goal** (grant -> preview, fallback for REAPER) the lambdas complete during normal click-response time — typically under a few seconds — and the editor/standalone window stays alive. **Does NOT block phase goal achievement**, but is a real edge-case crash risk that should be addressed in a follow-up plan before public beta.
- **CR-04 (Stale openDeviceAsync callback after AuthDenied during Opening -> ghost capture):** This is more serious — frames could flow to subscribers while the UI shows "denied". Mitigation: AuthDenied during Opening is a narrow race window (between TCC completion fire and openDeviceAsync result fire). Cell 5 (revoke roundtrip) is the closest UAT to this scenario but does not specifically exercise the in-flight-open race. **Does NOT block phase goal achievement** for the happy-path scenarios (grant -> preview, host-denial -> fallback dialog) — those don't hit the race window. Should be fixed before public beta.
- **Warnings WR-01..07:** Various defensive-coding gaps (listener-removal race, timer self-destruction, editor state-sync on reopen, redundant shutdown call, dead const_cast, RetryWorker termination). None block the phase goal.

**Verifier disposition:** CR-01..04 are real bugs but they live in **edge-case lifetime paths** that the 10 UAT cells do not exercise as adversarial scenarios. The phase goal is "users can grant camera access and see their preview" + "graceful fallback when DAW host lacks entitlement" — both achievable via the happy paths even with the UAFs present. **Recommend a follow-up plan (call it 19.1-lifetime-hardening or include in the Phase 24 beta hardening pass) before public release.** Surfacing here so the developer can decide whether to (a) gate Phase 19 on closing CR-01..04 first, (b) proceed to Phase 20 in parallel with a closure plan, or (c) accept the risk for the v1.3 beta.

### Gaps Summary

**No gaps block the phase goal at the codebase level.** All five Success Criteria are fully wired in code, all 23 declared artifacts exist and have substantive implementations, all 13 key links pass grep + structural inspection, all 7 unit tests pass on my live re-run (not just executor self-test), the JamWideJuce_Standalone target builds cleanly, and the built bundle carries the correct NSCameraUsageDescription.

**The phase is in `human_needed` status because the Success Criteria are behavioral end-to-end claims that require:**
- Real camera hardware
- Real OS permission prompts (macOS TCC + Windows Privacy)
- Real host DAW (Logic Pro, REAPER) for the host-context paths
- Apple notarization for Cell 6
- Visual confirmation of dialogs, copy wording, preview rendering, button labels
- Mid-session state transitions (revoke roundtrip)

None of these can be verified programmatically from grep + tests + a single build.

**Two non-blocking concerns surfaced for developer decision:**
1. **Code review CR-01..04** — 4 Critical UAF risks in editor-owned async lambdas. Real bugs but they require adversarial close-during-prompt sequences to trigger; happy paths complete safely.
2. **Risk C — JUCE commercial seat licence** — flagged OPEN by 19-01 SUMMARY for user confirmation. Cannot be answered by inspecting code.

**Recommended next step:** Execute the 10-cell UAT checklist (`docs/UAT/phase-19-camera-uat-checklist.md`) — once human UAT passes, Phase 19 can be marked verified. Address CR-01..04 in a hardening pass before public v1.3 beta release.

---

_Verified: 2026-05-16T19:00:00Z_
_Verifier: Claude (gsd-verifier)_
