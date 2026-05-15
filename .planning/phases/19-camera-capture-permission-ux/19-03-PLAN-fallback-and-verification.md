---
phase: 19
plan: 03
type: execute
wave: 2
depends_on:
  - 19-01
files_modified:
  - juce/video/native/CameraStatusDialog.h
  - juce/video/native/CameraStatusDialog.cpp
  - juce/JamWideJuceEditor.cpp
  - juce/JamWideJuceEditor.h
  - juce/ui/ConnectionBar.cpp
  - scripts/verify_camera_entitlement.sh
  - docs/UAT/phase-19-camera-uat-checklist.md
  - tests/test_camera_cause_mapping.cpp
  - CMakeLists.txt
  - CHANGELOG.md
autonomous: true
requirements:
  - CAM-01
  - CAM-02
  - PKG-04
threat_refs:
  - T-19-01
  - T-19-05

must_haves:
  truths:
    - "CameraStatusDialog displays cause-specific copy for all 5 CameraFallbackCause values (TCCDenied, HostLacksEntitlement, CameraInUse, NoHardware, WindowsPrivacyBlock)"
    - "Dialog includes platform-conditional deep-link buttons: 'Open System Settings' on macOS launching x-apple.systempreferences URL; 'Open Camera Privacy Settings' on Windows launching ms-settings:privacy-webcam URL"
    - "First denial trigger shows the dialog; subsequent triggers with the same cause focus the Camera button without re-showing"
    - "Cause change re-shows the dialog with the new copy"
    - "VDO.Ninja coexistence soft warning toast appears when user toggles native camera while videoCompanion->isActive() is true (D-27)"
    - "scripts/verify_camera_entitlement.sh exits 0 against a codesigned JamWide.app bundle when the bundle contains com.apple.security.device.camera AND NSCameraUsageDescription = configured string"
    - "License header sanity check confirms JamWide source files use 'or any later version' upgrade clause OR explicitly flags the absence (Risk C)"
    - "docs/UAT/phase-19-camera-uat-checklist.md enumerates all 9 manual UAT cells from VALIDATION.md per feedback_uat_scope_redflags memory"
    - "test_camera_cause_mapping covers all 5 causes x 2 platforms = 10 input/output cells"
  artifacts:
    - path: "juce/video/native/CameraStatusDialog.h"
      provides: "Cause-aware fallback dialog component"
      exports:
        - "class CameraStatusDialog"
    - path: "juce/video/native/CameraStatusDialog.cpp"
      provides: "Dialog implementation with cause classification, platform-conditional deep-links, suppress-after-first-show, cause-change re-show"
      contains: "x-apple.systempreferences:com.apple.preference.security?Privacy_Camera"
    - path: "scripts/verify_camera_entitlement.sh"
      provides: "CI-runnable PKG-04 entitlement + Info.plist verifier"
      contains: "codesign --display --entitlements"
    - path: "docs/UAT/phase-19-camera-uat-checklist.md"
      provides: "Manual UAT checklist covering 9 cells per VALIDATION.md"
    - path: "tests/test_camera_cause_mapping.cpp"
      provides: "Unit test for cause classification + cause-to-copy mapping"
      min_lines: 80
    - path: "CHANGELOG.md"
      provides: "Phase 19 user-facing change note per D-26"
  key_links:
    - from: "JamWideJuceEditor::onCameraFallback"
      to: "CameraStatusDialog::show"
      via: "FallbackListener override delegates to the dialog"
      pattern: "cameraStatusDialog_.show"
    - from: "CameraStatusDialog"
      to: "macOS System Settings"
      via: "juce::URL launchInDefaultBrowser of x-apple.systempreferences URL"
      pattern: "x-apple.systempreferences"
    - from: "CameraStatusDialog"
      to: "Windows Settings"
      via: "juce::URL launchInDefaultBrowser of ms-settings:privacy-webcam"
      pattern: "ms-settings:privacy-webcam"
    - from: "ConnectionBar::onCameraClicked"
      to: "VDO.Ninja coexistence toast"
      via: "Check processorRef.videoCompanion->isActive() before toggle; show juce::AlertWindow NoIcon toast"
      pattern: "isActive\\(\\)"
    - from: "scripts/verify_camera_entitlement.sh"
      to: "JamWide.app codesigned bundle"
      via: "codesign --display + plutil --extract NSCameraUsageDescription"
      pattern: "device.camera"
---

<objective>
Land the user-facing Phase 19 surface that 19-01 and 19-02 deliberately deferred: the cause-aware fallback dialog (D-13..D-16, SPARTA #82 mitigation), VDO.Ninja coexistence soft-warning toast (D-27), the Risk C license header sanity check (per planning context's Risk C — JUCE seat licence compatibility), the PKG-04 entitlement verification script + macOS notarization UAT instructions (D-28), the unit test for cause classification, and the user-facing CHANGELOG entry (D-26). Plus the manual UAT checklist that enforces `feedback_uat_scope_redflags` (no skipping CAM-01/02/03 UAT cells).

Purpose: 19-01 ships the backend (entitlement file + state machine + frame distributor); 19-02 ships the UI happy-path (button, popout, persistence). This plan ships the SAD-path UX (dialog) and the verification layer (script, test, UAT checklist, license check). Without it, users with denied permission see no helpful message; without the UAT checklist, executors could "verify happy path only, skip REAPER fallback" (the bug shape that `feedback_uat_scope_redflags` exists to prevent).

Output: A complete cause-aware fallback dialog routed via the editor's FallbackListener implementation; a coexistence toast that fires correctly; a CI-runnable entitlement verifier script; a manual UAT checklist that enumerates every cell; a unit test covering all 5x2=10 cause-classification cells; a CHANGELOG note.
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
@.planning/phases/19-camera-capture-permission-ux/19-01-SUMMARY.md
@.planning/phases/19-camera-capture-permission-ux/19-VALIDATION.md

<interfaces>
Key contracts from 19-01 (use directly):

From juce/video/native/CameraFallbackCause.h (created by 19-01):
  namespace jamwide {
    enum class CameraFallbackCause : int {
      None = 0, TCCDenied, HostLacksEntitlement, CameraInUse, NoHardware, WindowsPrivacyBlock
    };
  }

From juce/video/native/JamWideCameraDevice.h (created by 19-01):
  class FallbackListener {
    virtual void onCameraFallback(CameraFallbackCause cause) = 0;
    virtual void onCameraStateChanged(CameraState newState) = 0;
  };
  void recheckPermission();

From juce/video/native/CameraAuthorization.h (created by 19-01):
  enum class CameraAuthStatus { NotDetermined, Restricted, Denied, Authorized, NotApplicable };
  CameraAuthStatus queryCameraAuthorization();

From juce/JamWideJuceEditor.h/.cpp (after 19-02 lands):
  class JamWideJuceEditor : public juce::AudioProcessorEditor, public jamwide::JamWideCameraDevice::FallbackListener {
    void onCameraFallback(jamwide::CameraFallbackCause cause) override;  // <- this plan IMPLEMENTS the body
    void onCameraStateChanged(jamwide::CameraState) override;
  };

From juce/video/VideoCompanion.h (existing — used by Task 2 coexistence toast):
  bool isActive() const { return active_.load(std::memory_order_relaxed); }

Cause-to-copy mapping (per RESEARCH §9 lines 866-880; finalize verbatim copy here):
  TCCDenied            -> "macOS has denied camera access to JamWide. Grant permission in System Settings -> Privacy & Security -> Camera, then click Recheck."
  HostLacksEntitlement -> "{HostName} doesn't request camera access for itself, so JamWide can't reach the camera while hosted in it.\\n\\nTip: JamWide standalone has direct camera access."
  CameraInUse          -> "Another app is using the camera. Close it and click Recheck."
  NoHardware           -> "No camera detected. Connect a webcam and click Recheck."
  WindowsPrivacyBlock  -> "Windows has blocked camera access. Enable camera access in Settings -> Privacy -> Camera, then click Recheck."

Cause -> button set (RESEARCH §9 lines 886-893):
  TCCDenied            -> ["Open System Settings", "Recheck permission", "OK"]
  HostLacksEntitlement -> ["OK"]
  CameraInUse          -> ["Recheck permission", "OK"]
  NoHardware           -> ["Recheck permission", "OK"]
  WindowsPrivacyBlock  -> ["Open Camera Privacy Settings", "Recheck permission", "OK"]

Deep-link URLs:
  macOS:    juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser()
  Windows:  juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser()

Host detection (for HostLacksEntitlement copy):
  juce::PluginHostType().getHostDescription()
  juce::JUCEApplicationBase::isStandaloneApp()

CMake from 19-01:
  test_camera_cause_mapping executable already declared at CMakeLists.txt; this plan fills in tests/test_camera_cause_mapping.cpp body
</interfaces>
</context>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| OS TCC -> JamWide | Plugin runs in DAW host process; host's bundle ID controls TCC on macOS |
| Source repo -> codesigned bundle | Entitlement keys + Info.plist strings must survive codesign + notarization unchanged |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-19-01 | Tampering (TCC bypass / silent denial) | CameraStatusDialog cause classification + dialog | mitigate | Dialog shows specific cause to user with actionable next step (open System Settings / wait for another app / connect a camera). Suppress-after-first-show with cause-change re-show prevents user confusion without dialog spam. Verifiable: test_camera_cause_mapping unit asserts each cause produces the expected copy + deep-link button set. |
| T-19-05 | Entitlement spoofing (verify in codesigned bundle, not just source file) | scripts/verify_camera_entitlement.sh | mitigate | The script reads the entitlement from the SHIPPED bundle via `codesign --display --entitlements -`, NOT from the source `.entitlements` file. Same script also extracts NSCameraUsageDescription from the bundle's Info.plist via `plutil`. Per RESEARCH §7 lines 654-670 verbatim commands. Result: a tampered or stripped bundle fails the check; the source .entitlements file alone cannot satisfy it. |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: CameraStatusDialog cause-aware fallback dialog + cause-mapping unit test (D-13..D-16, T-19-01)</name>
  <files>
    juce/video/native/CameraStatusDialog.h,
    juce/video/native/CameraStatusDialog.cpp,
    juce/JamWideJuceEditor.cpp,
    juce/JamWideJuceEditor.h,
    tests/test_camera_cause_mapping.cpp,
    CMakeLists.txt
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §9 "Permission-Denial Fallback Dialog" (full section — cause matrix at lines 786-792, distinguishing TCCDenied vs HostLacksEntitlement at lines 796-810, AlertWindow pattern at lines 822-832, suppress-after-show logic at lines 836-844, copy strings at lines 866-880, action-button mapping at lines 888-893)
    - juce/video/native/JamWideCameraDevice.h + CameraFallbackCause.h (created by 19-01 — get the enum + listener shape)
    - juce/midi/MidiConfigDialog.cpp:115 (existing juce::AlertWindow::showAsync pattern in this codebase)
    - tests/test_video_fourcc.cpp (test harness pattern — JAMWIDE_BUILD_TESTS-gated, pure-C++)
  </read_first>
  <behavior>
    - Behavior 1: `class CameraStatusDialog` exposes a single public method `void show(CameraFallbackCause cause, std::function<void(int /* buttonIndex */)> onResult)` that fires juce::AlertWindow::showAsync with cause-specific message + button set + icon (WarningIcon for all except CameraInUse/NoHardware which use InfoIcon).
    - Behavior 2: On macOS, when cause is TCCDenied, the dialog includes an "Open System Settings" button as button index 0. Clicking it launches `juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser()`.
    - Behavior 3: On Windows, when cause is WindowsPrivacyBlock, dialog includes "Open Camera Privacy Settings" as button index 0; clicking it launches `juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser()`.
    - Behavior 4: A "Recheck permission" button (when present) invokes a callback that triggers `nativeCamera->recheckPermission()` (D-12). The dialog itself doesn't know about the camera; it delegates via the onResult callback's buttonIndex.
    - Behavior 5: Suppress-after-show: the dialog remembers `lastShownCause_`; if `show(cause)` is called again with the same cause, the dialog does NOT re-show; instead it returns immediately (caller can then focus the Camera button — that's the editor's responsibility, not the dialog's). If a different cause is passed, the dialog re-shows.
    - Behavior 6: When the editor's `onCameraFallback(cause)` fires, the editor calls `cameraStatusDialog_.show(cause, [this](int btn) { handleDialogButton(cause, btn); });`. The editor's handleDialogButton handles the deep-link button (calls launchInDefaultBrowser); the recheck button (calls nativeCamera->recheckPermission()); and OK/Cancel (no-op).
    - Behavior 7: For HostLacksEntitlement, the dialog substitutes `juce::PluginHostType().getHostDescription()` into the copy template at runtime.
    - Behavior 8: A pure-C++ unit test exercises the cause classification function (which lives in JamWideCameraDevice.cpp from 19-01, exposed via JAMWIDE_BUILD_TESTS) AND the cause-to-copy-and-buttons mapping function (which lives in CameraStatusDialog.cpp here, exposed via JAMWIDE_BUILD_TESTS).
  </behavior>
  <action>
    Edit 1 — Create juce/video/native/CameraStatusDialog.h. Forward-declare juce::AlertWindow. Include "CameraFallbackCause.h" only. Declare `class CameraStatusDialog`:
      Public: `void show(CameraFallbackCause cause, std::function<void(int)> onResult);` and `void reset();` (clears lastShownCause_ so the next show always displays — used when permission state externally changes).
      Public for testing (under `#ifdef JAMWIDE_BUILD_TESTS`): `static juce::String testCauseToCopy(CameraFallbackCause c, const juce::String& hostName);` and `static juce::StringArray testCauseToButtonLabels(CameraFallbackCause c);` so the unit test can exercise the mapping without launching a dialog.
      Private: `CameraFallbackCause lastShownCause_ = CameraFallbackCause::None;`.

    Edit 2 — Create juce/video/native/CameraStatusDialog.cpp. Implementation:
      - Implement `causeToCopy(cause, hostName)` — switch on cause; return the verbatim copy from RESEARCH §9 lines 866-880 (interpolate hostName into the HostLacksEntitlement template).
      - Implement `causeToButtonLabels(cause)` — switch on cause; return juce::StringArray per RESEARCH §9 lines 888-893.
      - Implement `causeToIcon(cause)` — TCCDenied/HostLacksEntitlement/WindowsPrivacyBlock = WarningIcon; CameraInUse/NoHardware = InfoIcon.
      - Implement `show(cause, onResult)`:
          1. If `cause == lastShownCause_`: call onResult(-1) to signal "suppressed"; return.
          2. lastShownCause_ = cause.
          3. Build juce::MessageBoxOptions: title="Camera unavailable", icon=causeToIcon(cause), message=causeToCopy(cause, juce::PluginHostType().getHostDescription()), buttons=causeToButtonLabels(cause).
          4. juce::AlertWindow::showAsync(options, [onResult](int btn) { onResult(btn); });
      - Implement `reset()` -> lastShownCause_ = CameraFallbackCause::None;
      - In testCauseToCopy / testCauseToButtonLabels: delegate to the static implementation functions for test access.

    Edit 3 — juce/JamWideJuceEditor.h: add `jamwide::CameraStatusDialog cameraStatusDialog_;` as a private member (mirrors `videoPrivacyDialog`).

    Edit 4 — juce/JamWideJuceEditor.cpp: replace the empty `onCameraFallback(cause)` stub from 19-02 Task 1 with the real implementation:
      `void JamWideJuceEditor::onCameraFallback(jamwide::CameraFallbackCause cause) {`
      `    cameraStatusDialog_.show(cause, [this, cause](int btn) {`
      `        if (btn < 0) { /* suppressed; focus the camera button */ connectionBar.cameraButton.grabKeyboardFocus(); return; }`
      `        auto labels = jamwide::CameraStatusDialog::testCauseToButtonLabels(cause);`
      `        if (btn >= labels.size()) return;`
      `        auto label = labels[btn];`
      `        if (label == "Open System Settings") {`
      `            juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser();`
      `        } else if (label == "Open Camera Privacy Settings") {`
      `            juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser();`
      `        } else if (label == "Recheck permission") {`
      `            if (auto* cam = processorRef.getNativeCamera()) cam->recheckPermission();`
      `        }`
      `        /* OK -> no-op */`
      `    });`
      `}`
      Note: the dispatch via label-string comparison is the simplest implementation; alternatives include an enum-keyed action map. Label-comparison is acceptable here because the labels are short, fixed, and the action is platform-conditional anyway.

    Edit 5 — juce/JamWideJuceEditor.cpp: extend `onCameraStateChanged` (from 19-02 Task 1) — when state transitions from Unavailable back to a usable state (e.g., Capturing), call `cameraStatusDialog_.reset();` so the next denial will re-show the dialog (the user has likely been granted permission externally and may want to know if a new denial happens).

    Edit 6 — Create tests/test_camera_cause_mapping.cpp. Pure-C++ test using only JAMWIDE_BUILD_TESTS-exposed static functions from CameraStatusDialog. Include "video/native/CameraStatusDialog.h" and "video/native/CameraFallbackCause.h".
      Test 1 (5 causes x copy mapping): for each CameraFallbackCause value, call CameraStatusDialog::testCauseToCopy(cause, "TestHost") and assert the returned string CONTAINS a cause-specific substring (e.g., TCCDenied -> "macOS has denied"; HostLacksEntitlement -> "TestHost"; CameraInUse -> "Another app"; NoHardware -> "No camera detected"; WindowsPrivacyBlock -> "Windows has blocked").
      Test 2 (5 causes x button mapping): for each cause, call testCauseToButtonLabels(cause); assert the returned StringArray matches the expected set (size + contents):
        TCCDenied -> ["Open System Settings", "Recheck permission", "OK"]
        HostLacksEntitlement -> ["OK"]
        CameraInUse -> ["Recheck permission", "OK"]
        NoHardware -> ["Recheck permission", "OK"]
        WindowsPrivacyBlock -> ["Open Camera Privacy Settings", "Recheck permission", "OK"]
      Test 3 (HostLacksEntitlement template interpolation): testCauseToCopy(HostLacksEntitlement, "REAPER") must contain "REAPER" (the host name substitutes correctly).
      Test 4 (None -> generic fallback): testCauseToCopy(None, "") returns a non-empty generic "Camera unavailable" string; testCauseToButtonLabels(None) returns at minimum ["OK"] (defensive default).
      Use the assert()/exit(1) pattern from tests/test_video_fourcc.cpp.

    Edit 7 — CMakeLists.txt: in the test_camera_cause_mapping block (pre-staged by 19-01 Task 1), update the target_link_libraries to include the source files needed for the test. Since the test exercises CameraStatusDialog.cpp's static functions, the test executable needs to either (a) link against the JamWideJuce target's video/native object files or (b) compile CameraStatusDialog.cpp directly into the test binary. Recommendation: option (b) — add CameraStatusDialog.cpp as a direct source of test_camera_cause_mapping target. This keeps the test self-contained without pulling the full plugin in. The test_camera_cause_mapping line in CMake becomes:
      `add_executable(test_camera_cause_mapping tests/test_camera_cause_mapping.cpp juce/video/native/CameraStatusDialog.cpp)` + the existing include dirs + `target_link_libraries(test_camera_cause_mapping PRIVATE juce::juce_core juce::juce_gui_basics)` (gui_basics needed for juce::PluginHostType — adjust if a juce::JUCEApplicationBase usage forces a different module).

    Also append `juce/video/native/CameraStatusDialog.cpp` to the JamWideJuce target_sources block.
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target test_camera_cause_mapping JamWideJuce 2>&amp;1 | tail -20; cd build-juce-19-test; ctest -R camera_cause_mapping --output-on-failure; cd ..; test -f juce/video/native/CameraStatusDialog.h; grep -c "x-apple.systempreferences" juce/video/native/CameraStatusDialog.cpp; grep -c "ms-settings:privacy-webcam" juce/JamWideJuceEditor.cpp; grep -c "cameraStatusDialog_.show" juce/JamWideJuceEditor.cpp; grep -c "recheckPermission" juce/JamWideJuceEditor.cpp</automated>
  </verify>
  <done>
    test_camera_cause_mapping exits 0 with 4 scenarios passing; CameraStatusDialog exists with the documented signatures; the editor's onCameraFallback delegates to the dialog and routes button clicks to the correct platform-conditional deep-link. JamWideJuce builds cleanly including CameraStatusDialog.cpp.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: VDO.Ninja coexistence toast + license header sanity check (Risk C) + CHANGELOG (D-27, D-26)</name>
  <files>
    juce/JamWideJuceEditor.cpp,
    juce/ui/ConnectionBar.cpp,
    CHANGELOG.md
  </files>
  <read_first>
    - juce/video/VideoCompanion.h:77 (the isActive() lock-free accessor used for coexistence detection)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §10 "VDO.Ninja Coexistence" (full section — coexistence detection at lines 902-917, toast pattern at lines 922-928, "informational only, does not block the camera toggle" note at line 931)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §13 Risk C lines 1034-1039 (JUCE seat licence + GPLv2+ upgrade clause requirement)
    - /Users/cell/dev/JamWide/LICENSE (current GPLv2 file — verify whether the "or any later version" clause is in the file header)
    - CHANGELOG.md if exists (check repo root); if not, create with a standard "Keep a Changelog" header
  </read_first>
  <behavior>
    - Behavior 1: When the user clicks the Camera button AND processorRef.videoCompanion->isActive() returns true, a non-blocking juce::AlertWindow toast appears with NoIcon, title="Multiple video stacks active", message="VDO.Ninja video is also active. Bandwidth and CPU may be high — consider stopping one for better quality.", button="OK".
    - Behavior 2: The toast is INFORMATIONAL — the camera toggle proceeds in parallel (D-27 "User can ignore and proceed"). The toast and the camera state machine run concurrently.
    - Behavior 3: The toast is suppressed after first show per session — flag a static bool inside the editor `static bool coexistenceToastShown = false;` (reset on editor reconstruction = next plugin load); only the FIRST simultaneous-active triggers the toast. Subsequent toggles do NOT spam.
    - Behavior 4: License header sanity check: a grep of the JamWide source headers identifies whether the "or (at your option) any later version" upgrade clause is present. If absent, Risk C is surfaced as an explicit blocker in the SUMMARY (does not block the build but flags for user attention).
    - Behavior 5: CHANGELOG.md gains a new section under "Unreleased" describing Phase 19 features per D-26: native camera capture (macOS + Windows), preview popout, permission UX with cause-aware fallback dialog, plugin-state schema v3->v4.
  </behavior>
  <action>
    Edit 1 — juce/JamWideJuceEditor.cpp: extend the existing onCameraClicked lambda from 19-02 Task 1 + Task 3 to wrap the toggle with a coexistence check. Insert BEFORE the existing privacy-dialog gate:
      `static std::atomic<bool> coexistenceToastShown{false};`
      `if (processorRef.videoCompanion && processorRef.videoCompanion->isActive() && !coexistenceToastShown.exchange(true)) {`
      `    auto opts = juce::MessageBoxOptions{}`
      `        .withIconType(juce::MessageBoxIconType::NoIcon)`
      `        .withTitle("Multiple video stacks active")`
      `        .withMessage("VDO.Ninja video is also active. Bandwidth and CPU may be high - consider stopping one for better quality.")`
      `        .withButton("OK");`
      `    juce::AlertWindow::showAsync(opts, [](int){});`
      `    // Fall through to the toggle — D-27 says the toast does NOT block`
      `}`
      The exchange() pattern ensures the toast fires AT MOST ONCE per editor lifetime even if the user toggles repeatedly with VDO.Ninja active.

    Edit 2 — License header sanity check. This is a verification activity (not a code change unless a fix is needed). Run during execution:
      `grep -l "or (at your option) any later version" juce/JamWideJuceProcessor.* juce/JamWideJuceEditor.* src/core/njclient.h src/core/njclient.cpp 2>/dev/null` -- if zero output, Risk C is OPEN.
      `grep -l "or any later version" juce/JamWideJuceProcessor.* juce/JamWideJuceEditor.* src/core/njclient.h src/core/njclient.cpp 2>/dev/null` -- if zero output, Risk C is OPEN.
      Per RESEARCH §13 line 1037: "if JamWide is licensed as 'GPLv2 only' (no upgrade clause), the AGPLv3 compatibility argument fails and the project must either (a) hold a JUCE commercial seat or (b) replan with direct AVFoundation + DirectShow capture".
      ACTION on this task:
        - If the upgrade clause IS present in JamWide source headers: pass the check, document in SUMMARY.
        - If the upgrade clause is ABSENT: do NOT remove juce_video — surface the finding in SUMMARY with one of three resolutions: (1) user can confirm a JUCE commercial seat covers this work (most likely path; the project ships JUCE under custom terms already); (2) user can add the upgrade clause to JamWide source headers (one-time edit, retroactive opt-in to GPLv2+); (3) user can choose to defer Phase 19 and replan with direct platform capture APIs.
      Since the LICENSE file is GPLv2 (no "or later version" in the boilerplate header of the file itself — verified during planning), the planner notes this as a likely Risk C OPEN. The executor must surface it explicitly in the SUMMARY and request user decision before the plan can be closed. Risk C does not block the build — it blocks SHIP, and the user can resolve at SUMMARY review time.

    Edit 3 — CHANGELOG.md: if file does not exist, create with a "Keep a Changelog" header (date 2026-05-16; format "## [Unreleased]"). Add a new entry under "## [Unreleased]" -> "### Added":
      `- **Phase 19 — Native Camera Capture (v1.3 beta)**: New "Camera" button in ConnectionBar opens a floating preview popout with the local webcam. Quality preset (Low/Medium/High) via right-click menu. Cross-platform: macOS (arm64+x86_64) + Windows x86_64. macOS adds the com.apple.security.device.camera entitlement; plugin Info.plist gains NSCameraUsageDescription. Cause-aware fallback dialog handles all denial modes including the SPARTA #82 macOS DAW-host-lacks-entitlement case (REAPER, Live, Bitwig). Plugin state schema bumped v3 to v4 to persist popout bounds + quality preset + privacy ack. Coexists with the existing VDO.Ninja video stack during the parallel v1.3 beta.`
      If CHANGELOG.md does exist with prior phase entries, add the Phase 19 bullet under the same "Unreleased" -> "Added" section, mirroring the existing prose style.
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target JamWideJuce_Standalone 2>&amp;1 | tail -10; grep -c "Multiple video stacks active" juce/JamWideJuceEditor.cpp; grep -c "videoCompanion->isActive" juce/JamWideJuceEditor.cpp; grep -c "coexistenceToastShown" juce/JamWideJuceEditor.cpp; test -f CHANGELOG.md; grep -c "Phase 19" CHANGELOG.md; grep -c "Native Camera" CHANGELOG.md; (grep -l "or any later version\|or (at your option) any later" juce/*.h juce/*.cpp src/core/njclient.h src/core/njclient.cpp 2>/dev/null | wc -l)</automated>
  </verify>
  <done>
    JamWideJuce_Standalone builds; the coexistence toast is wired with a once-per-session guard; CHANGELOG.md has the Phase 19 entry; the license-header sanity-check result is recorded (either a positive find indicating the upgrade clause is present, or an empty result documented as Risk C OPEN in the SUMMARY for user decision).
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 3: PKG-04 entitlement verification script + manual UAT checklist + notarization smoke (D-28, T-19-05, feedback_uat_scope_redflags)</name>
  <files>
    scripts/verify_camera_entitlement.sh,
    docs/UAT/phase-19-camera-uat-checklist.md
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §7 lines 654-670 (the verification commands — codesign --display --entitlements, plutil --extract NSCameraUsageDescription, xcrun notarytool, xcrun stapler validate)
    - .planning/phases/19-camera-capture-permission-ux/19-VALIDATION.md lines 65-83 (the 9 manual UAT cells per feedback_uat_scope_redflags)
    - JamWide.entitlements (verifies the source state — should contain device.camera after 19-01 Task 1)
    - Project memory: project_apple_signing (Team ID T3KK66Q67T, notarization via API Key)
    - Project memory: feedback_uat_scope_redflags (no "verify only X, skip Y" UAT; all CAM-01/02/03 cells must be explicit)
  </read_first>
  <behavior>
    - Behavior 1: scripts/verify_camera_entitlement.sh accepts a single argument $BUNDLE_PATH (e.g., /Applications/JamWide.app or a build-tree path). The script returns exit 0 on success, non-zero with a clear message on failure.
    - Behavior 2: The script checks four conditions, ANDed:
      a. The bundle exists and is a directory.
      b. `codesign --display --entitlements - "$BUNDLE_PATH"` succeeds AND its output contains the literal string `com.apple.security.device.camera` followed by `<true/>` (or contains the key in a binary entitlement blob).
      c. `plutil -extract NSCameraUsageDescription raw "$BUNDLE_PATH/Contents/Info.plist"` succeeds AND outputs `JamWide uses your webcam to share video with NINJAM peers.` (the configured CAMERA_PERMISSION_TEXT from 19-01 Task 1).
      d. (Optional, only if --notarize flag is passed) `xcrun stapler validate "$BUNDLE_PATH"` exits 0.
    - Behavior 3: On any failure, the script prints a clear diagnostic naming which check failed and what the expected vs. actual values are.
    - Behavior 4: The script is portable to CI: pure bash, uses only macOS-default tools (codesign, plutil, xcrun) — no Homebrew dependencies.
    - Behavior 5: docs/UAT/phase-19-camera-uat-checklist.md enumerates all 9 cells from VALIDATION.md "Manual-Only Verifications" table verbatim, with a checkbox per cell and step-by-step instructions per row.
  </behavior>
  <action>
    Edit 1 — Create scripts/verify_camera_entitlement.sh. Make it executable (chmod +x). Skeleton:
      `#!/usr/bin/env bash`
      `# verify_camera_entitlement.sh — PKG-04 verification gate for Phase 19`
      `# Usage: ./scripts/verify_camera_entitlement.sh <BUNDLE_PATH> [--notarize]`
      `# Returns 0 on success; non-zero with diagnostic on failure.`
      `set -euo pipefail`
      `BUNDLE_PATH="${1:?Usage: $0 <BUNDLE_PATH> [--notarize]}"`
      `NOTARIZE=${2:-}`
      `EXPECTED_USAGE='JamWide uses your webcam to share video with NINJAM peers.'`
      `if [[ ! -d "$BUNDLE_PATH" ]]; then echo "FAIL: $BUNDLE_PATH not found or not a directory" >&2; exit 1; fi`
      `# Check 1: entitlement present in codesigned bundle`
      `if ! codesign --display --entitlements - "$BUNDLE_PATH" 2>/dev/null | grep -q 'com.apple.security.device.camera'; then`
      `    echo "FAIL: com.apple.security.device.camera not found in bundle entitlements" >&2`
      `    echo "      Hint: rebuild JamWide with JamWide.entitlements containing the camera key and JAMWIDE_HARDENED_RUNTIME=ON" >&2`
      `    exit 2`
      `fi`
      `# Check 2: Info.plist NSCameraUsageDescription matches`
      `if ! ACTUAL=$(plutil -extract NSCameraUsageDescription raw "$BUNDLE_PATH/Contents/Info.plist" 2>/dev/null); then`
      `    echo "FAIL: NSCameraUsageDescription missing from Info.plist" >&2; exit 3`
      `fi`
      `if [[ "$ACTUAL" != "$EXPECTED_USAGE" ]]; then`
      `    echo "FAIL: NSCameraUsageDescription mismatch" >&2`
      `    echo "      Expected: $EXPECTED_USAGE" >&2`
      `    echo "      Actual:   $ACTUAL" >&2`
      `    exit 4`
      `fi`
      `# Check 3 (optional): notarization staple is valid`
      `if [[ "$NOTARIZE" == "--notarize" ]]; then`
      `    if ! xcrun stapler validate "$BUNDLE_PATH"; then`
      `        echo "FAIL: stapler validation failed (bundle not notarized or staple is missing)" >&2; exit 5`
      `    fi`
      `fi`
      `echo "OK: $BUNDLE_PATH passes PKG-04 entitlement + NSCameraUsageDescription verification"`
      `if [[ "$NOTARIZE" == "--notarize" ]]; then echo "OK: notarization staple is valid"; fi`
      `exit 0`

    Edit 2 — Create docs/UAT/phase-19-camera-uat-checklist.md. Header naming the phase, the date, and the link to VALIDATION.md. Then a checklist enumerating ALL 9 cells from VALIDATION.md "Manual-Only Verifications" (lines 70-80) with explicit step-by-step instructions for each. Use a `- [ ]` checkbox per cell. For each cell, include: requirement ID, why this cell is manual, exact steps, expected outcome, what to record in this file when passing.

      Skeleton structure:
      `# Phase 19 — Camera Capture & Permission UX — Manual UAT Checklist`
      `Date: 2026-05-16`
      `Phase: 19 — Camera Capture & Permission UX`
      `Source: .planning/phases/19-camera-capture-permission-ux/19-VALIDATION.md "Manual-Only Verifications" table`
      `Per memory feedback_uat_scope_redflags: NO skipping CAM-01 / CAM-02 / CAM-03 happy and sad paths. Every cell below MUST be exercised before Phase 19 is declared complete.`
      ``
      `## Cell 1 — macOS standalone happy path (CAM-01 / SC1)`
      `**Why manual:** Requires real camera hardware + TCC prompt.`
      `**Steps:**`
      `1. Launch JamWide.app on macOS standalone.`
      `2. Click the Camera button in ConnectionBar.`
      `3. Verify the macOS TCC prompt appears within 1 second.`
      `4. Click Allow.`
      `5. Verify the camera preview popout appears within 3 seconds.`
      `6. Verify a live frame stream visible in the popout (motion test — wave hand in front of camera).`
      `**Expected:** Preview live within 3 s of grant; popout titlebar reads "JamWide — Camera: <device name>".`
      `**Record:** Tester name, macOS version, camera device name, observed time-to-first-frame in seconds.`
      `- [ ] Pass / Fail / Date / Tester / Notes:`
      ``
      `## Cell 2 — Logic Pro plugin happy path (CAM-01 / SC2)`
      `[similar structure, instructions per VALIDATION.md row 2]`
      ``
      `## Cell 3 — REAPER macOS plugin fallback (CAM-02 / SC3)`
      `**Why manual:** SPARTA #82 — host bundle ID controls TCC. REAPER does NOT request the camera entitlement.`
      `**Steps:**`
      `1. Load JamWide VST3 in REAPER on macOS.`
      `2. Click Camera button.`
      `3. Verify the CameraStatusDialog appears with the HostLacksEntitlement message mentioning "REAPER".`
      `4. Verify dialog has exactly one "OK" button (D-16 — no useful action).`
      `5. Dismiss dialog. Verify JamWide does NOT crash.`
      `6. Verify audio still works: load a session, play audio, verify peers can hear it.`
      `**Expected:** Dialog shows REAPER-named copy; no crash; audio session intact.`
      `**Record:** REAPER version; verified host name appears in dialog copy.`
      `- [ ] Pass / Fail / Date / Tester / Notes:`
      ``
      `## Cell 4 — macOS arm64 standalone (CAM-01 arm64)`
      `[spike was x86_64-only — first arm64 build path]`
      ``
      `## Cell 5 — Permission revoke roundtrip (CAM-02 / SC4)`
      `[grant + capture + revoke via System Settings + verify fallback appears]`
      ``
      `## Cell 6 — Notarization stapler validate (PKG-04 / D-28)`
      `**Why manual:** Depends on Apple notary service.`
      `**Steps:**`
      `1. Build a release JamWide bundle with JAMWIDE_HARDENED_RUNTIME=ON.`
      `2. Codesign with the configured Team ID (T3KK66Q67T per project memory).`
      `3. Submit to notary: xcrun notarytool submit <zip> --keychain-profile <profile> --wait.`
      `4. Staple: xcrun stapler staple <bundle>.`
      `5. Validate: xcrun stapler validate <bundle> (must exit 0).`
      `6. Run ./scripts/verify_camera_entitlement.sh <bundle> --notarize (must exit 0).`
      `**Expected:** All 6 steps pass.`
      `- [ ] Pass / Fail / Date / Tester / Notes:`
      ``
      `## Cell 7 — Windows standalone happy path (CAM-01 / SC5)`
      `[Windows + camera hardware required; SPARTA #82 does NOT apply on Windows]`
      ``
      `## Cell 8 — Windows privacy block (CAM-02 Windows)`
      `[disable camera access in Settings → Privacy → Camera; verify WindowsPrivacyBlock fallback]`
      ``
      `## Cell 9 — VDO.Ninja coexistence toast (D-27)`
      `[start VDO.Ninja video; toggle native Camera; verify the once-per-session soft warning toast appears and is non-blocking]`

      Each cell follows the same shape: header, why-manual, steps, expected, record. Cells 4, 7, 8, 9 are abbreviated above for brevity in this plan; the actual file should expand each to the same level of detail as Cells 1, 3, 6.

    No automated test for this task — the script itself is the "test" infrastructure. Verification: run the script against the locally-built JamWide.app artifact (if available; otherwise dry-run with a synthetic bundle in test setup to verify the script's argument handling).
  </action>
  <verify>
    <automated>test -x scripts/verify_camera_entitlement.sh; bash -n scripts/verify_camera_entitlement.sh; test -f docs/UAT/phase-19-camera-uat-checklist.md; grep -c "CAM-01" docs/UAT/phase-19-camera-uat-checklist.md; grep -c "CAM-02" docs/UAT/phase-19-camera-uat-checklist.md; grep -c "CAM-03" docs/UAT/phase-19-camera-uat-checklist.md; grep -c "PKG-04" docs/UAT/phase-19-camera-uat-checklist.md; grep -c "SPARTA #82\\|HostLacksEntitlement" docs/UAT/phase-19-camera-uat-checklist.md; grep -c "REAPER" docs/UAT/phase-19-camera-uat-checklist.md; grep -c "stapler validate" docs/UAT/phase-19-camera-uat-checklist.md; grep -c "VDO.Ninja\\|coexistence" docs/UAT/phase-19-camera-uat-checklist.md; (./scripts/verify_camera_entitlement.sh 2>&amp;1 | grep -c "Usage:" || true)</automated>
  </verify>
  <done>
    The verification script exists, is executable, passes a bash -n syntax check, and prints a Usage: message when invoked without arguments. The UAT checklist enumerates all 9 cells with explicit steps; every CAM-01/02/03 cell is named; SPARTA #82 + REAPER + VDO.Ninja + stapler all appear in the document. After a local build, the script can be smoke-tested against the build-tree JamWide.app (informational only — full PKG-04 verification requires the codesigned release bundle and is a manual UAT cell).
  </done>
</task>

</tasks>

<verification>

## Plan-Level Verification

```bash
# 1. CameraStatusDialog test green
cmake --build build-juce-19-03 --target test_camera_cause_mapping JamWideJuce 2>&1 | tail -10
cd build-juce-19-03 && ctest -R camera_cause_mapping --output-on-failure && cd ..

# 2. Editor wires the dialog + deep links
grep -c 'cameraStatusDialog_.show' juce/JamWideJuceEditor.cpp   # >= 1
grep -c 'x-apple.systempreferences:com.apple.preference.security' juce/JamWideJuceEditor.cpp   # >= 1
grep -c 'ms-settings:privacy-webcam' juce/JamWideJuceEditor.cpp   # >= 1
grep -c 'recheckPermission' juce/JamWideJuceEditor.cpp   # >= 1

# 3. Coexistence toast wired
grep -c 'Multiple video stacks active' juce/JamWideJuceEditor.cpp   # >= 1
grep -c 'coexistenceToastShown' juce/JamWideJuceEditor.cpp   # >= 1

# 4. Entitlement verification script exists and runs
test -x scripts/verify_camera_entitlement.sh
bash -n scripts/verify_camera_entitlement.sh   # syntax-check only

# 5. UAT checklist enumerates all 9 cells
grep -c '^## Cell ' docs/UAT/phase-19-camera-uat-checklist.md   # == 9 (exactly)
grep -c 'CAM-01\|CAM-02\|CAM-03\|PKG-04' docs/UAT/phase-19-camera-uat-checklist.md   # >= 4

# 6. CHANGELOG entry exists
grep -c 'Phase 19\|Native Camera' CHANGELOG.md   # >= 1
```

</verification>

<success_criteria>

This plan succeeds when:

1. **Fallback dialog ships** — CameraStatusDialog handles all 5 causes; editor delegates onCameraFallback via the dialog; cause-change re-show + suppress-after-show work per D-14.
2. **Platform deep-links work** — macOS button launches x-apple.systempreferences URL; Windows button launches ms-settings:privacy-webcam URL.
3. **VDO.Ninja coexistence toast** — Once-per-session non-blocking warning when user starts native camera while VDO.Ninja is active (D-27).
4. **Risk C resolved or surfaced** — License header sanity check completed; result documented in SUMMARY; if the upgrade clause is absent, user is asked to resolve before phase closes.
5. **PKG-04 verification script** — scripts/verify_camera_entitlement.sh exists, is executable, validates entitlement + NSCameraUsageDescription + (optional) notarization staple.
6. **Manual UAT checklist** — docs/UAT/phase-19-camera-uat-checklist.md enumerates all 9 cells from VALIDATION.md with explicit steps; per feedback_uat_scope_redflags every CAM-01/02/03 path is present.
7. **Cause-mapping unit test** — test_camera_cause_mapping exits 0; 5 causes x copy mapping + button mapping cells covered (RESEARCH §9).
8. **CHANGELOG note** — Phase 19 user-facing entry added under Unreleased (D-26).

</success_criteria>

<output>
Create `.planning/phases/19-camera-capture-permission-ux/19-03-SUMMARY.md` summarizing:
- CameraStatusDialog cause-to-copy-and-buttons mapping (verbatim final strings used in production)
- Editor's onCameraFallback dispatch logic (the label-string comparison vs. enum-keyed action map decision)
- VDO.Ninja coexistence toast once-per-session guard implementation
- **Risk C resolution status (CRITICAL):** Result of the license-header sanity check. Three possible outcomes — document which one applied:
  - (a) "Upgrade clause present in source headers — Risk C CLOSED via 'GPLv2 or any later version' compatibility with juce_video AGPLv3."
  - (b) "Upgrade clause ABSENT — Risk C OPEN; user must confirm JUCE commercial seat OR add the upgrade clause OR replan with direct AVF + DirectShow capture path (per RESEARCH §13 line 1037)."
  - (c) "Ambiguous — JamWide LICENSE file is GPLv2 boilerplate without 'or later' clause, but individual source files [list them] DO carry the upgrade clause. User decision required on whether this constitutes acceptable opt-in."
- PKG-04 verification: result of running scripts/verify_camera_entitlement.sh against the locally-built JamWide.app (success/failure with reason)
- UAT checklist: total 9 cells enumerated; per-DAW UAT (Live, Bitwig, Windows REAPER) explicitly deferred to Phase 24 per VALIDATION.md (this plan ONLY covers the 4 macOS cells + 2 Windows cells + 1 VDO.Ninja coexistence cell + permission-revoke cell + notarization cell = 9 total in Phase 19 scope)
- D-28 notarization smoke: was a test bundle built + codesigned + notarized + stapled in this plan execution? If yes, record the outcome. If no (because notarization requires the API Key keychain profile not present in CI), record as a deferred manual UAT cell.
</output>
