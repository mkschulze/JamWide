---
phase: 19
plan: 03
type: execute
wave: 3
depends_on:
  - 19-01
  - 19-02
files_modified:
  - juce/video/native/CameraStatusDialog.h
  - juce/video/native/CameraStatusDialog.cpp
  - juce/JamWideJuceEditor.cpp
  - juce/JamWideJuceEditor.h
  - juce/ui/ConnectionBar.cpp
  - scripts/verify_camera_entitlement.sh
  - docs/UAT/phase-19-camera-uat-checklist.md
  - tests/test_camera_cause_mapping.cpp
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
    - "CameraStatusDialog displays cause-specific copy for all 5 CameraFallbackCause values (TCCDenied, HostLacksEntitlement, CameraInUse, NoHardware, WindowsPrivacyBlock) - softened HostLacksEntitlement copy per MEDIUM-6 (no DAW-specific blame)"
    - "CameraStatusDialog dispatches on a label-keyed action map (HIGH-7 fix), not on raw juce::AlertWindow button-index"
    - "The dialog's actionFor(cause, juceButtonResult) helper translates JUCE's documented button-index to return-code mapping (1-btn: 0; 2-btn: 1,0; 3-btn: 1,2,0) into a semantic CameraStatusDialog::Action enum - tested explicitly in test_camera_cause_mapping"
    - "Dialog includes platform-conditional deep-link buttons: 'Open System Settings' on macOS launching x-apple.systempreferences URL; 'Open Camera Privacy Settings' on Windows launching ms-settings:privacy-webcam URL"
    - "First denial trigger shows the dialog; subsequent triggers with the same cause focus the Camera button without re-showing"
    - "Cause change re-shows the dialog with the new copy"
    - "VDO.Ninja coexistence soft warning toast appears when user toggles native camera while videoCompanion->isActive() is true (D-27)"
    - "scripts/verify_camera_entitlement.sh exits 0 against a codesigned JamWide.app bundle when the bundle contains com.apple.security.device.camera AND NSCameraUsageDescription = configured string"
    - "docs/UAT/phase-19-camera-uat-checklist.md enumerates 10 manual UAT cells: the original 9 from VALIDATION.md PLUS one new cell explicitly exercising the HIGH-5 first-launch privacy-modal sequence (NotDetermined to grant to modal to camera)"
    - "test_camera_cause_mapping covers all 5 causes x 2 platforms = 10 input/output cells AND the HIGH-7 button-index to action mapping for each cause's button count"
  artifacts:
    - path: "juce/video/native/CameraStatusDialog.h"
      provides: "Cause-aware fallback dialog component + Action enum + actionFor() helper"
      exports:
        - "class CameraStatusDialog"
        - "enum class CameraStatusDialog::Action"
    - path: "juce/video/native/CameraStatusDialog.cpp"
      provides: "Dialog implementation with cause classification, platform-conditional deep-links, suppress-after-first-show, cause-change re-show, label-keyed return-code dispatch (HIGH-7), softened HostLacksEntitlement copy (MEDIUM-6)"
      contains: "x-apple.systempreferences:com.apple.preference.security?Privacy_Camera"
    - path: "scripts/verify_camera_entitlement.sh"
      provides: "CI-runnable PKG-04 entitlement + Info.plist verifier"
      contains: "codesign --display --entitlements"
    - path: "docs/UAT/phase-19-camera-uat-checklist.md"
      provides: "Manual UAT checklist covering 10 cells (9 from VALIDATION.md + 1 new HIGH-5 first-launch cell), with LOW-1 tuning note"
    - path: "tests/test_camera_cause_mapping.cpp"
      provides: "Unit test for cause classification + cause-to-copy mapping + HIGH-7 button-mapping"
      min_lines: 100
    - path: "CHANGELOG.md"
      provides: "Phase 19 user-facing change note per D-26"
  key_links:
    - from: "JamWideJuceEditor::onCameraFallback"
      to: "CameraStatusDialog::show"
      via: "FallbackListener override delegates to the dialog; result returned as Action enum"
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

# Phase 19-03 Plan: Cause-Aware Fallback + PKG-04 Verification + UAT

> NOTE: All XML-style task blocks below use `task`, `read_first`, `behavior`, `action`, `verify`, `automated`, `done` element names per the execute-plan template. This plan rewrites the prior pass to address Codex review HIGH-7 (button-index mapping) + MEDIUM-6 (softened copy) + LOW-1 (UAT tuning note), plus adds a NEW UAT cell explicitly exercising the HIGH-5 first-launch path (the dedicated cell is Task 3's Cell 10).

<objective>

Land the SAD-path UX and the verification layer with the dialog-correctness fixes Codex flagged. Specifically:
(a) the cause-aware fallback dialog (D-13..D-16) that uses a LABEL-KEYED action map instead of raw button-index dispatch (HIGH-7);
(b) softened HostLacksEntitlement copy that does not asymmetrically blame the host (MEDIUM-6);
(c) VDO.Ninja coexistence soft-warning toast (D-27);
(d) the PKG-04 entitlement verification script + macOS notarization UAT instructions (D-28);
(e) the unit test for cause classification + HIGH-7 button-mapping;
(f) a comprehensive UAT checklist with 10 cells - the 9 from VALIDATION.md plus 1 new dedicated cell for the HIGH-5 first-launch privacy-modal sequence - including the LOW-1 watchdog-tuning note;
(g) the user-facing CHANGELOG entry (D-26).

Risk C is NOT addressed here - 19-01 Task 1 Step 0 already gated the license preflight before juce_video was linked (MEDIUM-4 resolution).

Purpose: 19-01 ships the backend; 19-02 ships the UI happy-path + first-launch sequence. This plan ships the SAD-path UX and the verification layer. Without it, users with denied permission see no helpful message; without the UAT checklist, executors could "verify happy path only, skip REAPER fallback" (the bug shape feedback_uat_scope_redflags exists to prevent).

Output: A complete cause-aware fallback dialog routed via the editor's FallbackListener implementation; a coexistence toast that fires correctly; a CI-runnable entitlement verifier script; a manual UAT checklist enumerating every cell + LOW-1 tuning note; a unit test covering all 5 cause-classification cells + the HIGH-7 button-mapping; a CHANGELOG note.

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
@.planning/phases/19-camera-capture-permission-ux/19-02-SUMMARY.md
@.planning/phases/19-camera-capture-permission-ux/19-VALIDATION.md
@.planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md

## Interfaces

Key contracts from 19-01 (use directly):

From juce/video/native/CameraFallbackCause.h (created by 19-01 Task 1):
```cpp
namespace jamwide {
  enum class CameraFallbackCause : int {
    None = 0, TCCDenied, HostLacksEntitlement, CameraInUse, NoHardware, WindowsPrivacyBlock
  };
}
```

From juce/video/native/JamWideCameraDevice.h (created by 19-01 Task 5):
```cpp
class FallbackListener {
  virtual void onCameraFallback(CameraFallbackCause cause) = 0;
  virtual void onCameraStateChanged(CameraState newState) = 0;
};
void recheckPermission();
```

From juce/video/native/CameraAuthorization.h (created by 19-01 Task 1):
```cpp
enum class CameraAuthStatus { NotDetermined, Restricted, Denied, Authorized, NotApplicable };
CameraAuthStatus queryCameraAuthorization();
```

From juce/JamWideJuceEditor.h/.cpp (after 19-02):
```cpp
class JamWideJuceEditor : public juce::AudioProcessorEditor, public jamwide::JamWideCameraDevice::FallbackListener {
  void onCameraFallback(jamwide::CameraFallbackCause cause) override;  // <- this plan IMPLEMENTS the body
  void onCameraStateChanged(jamwide::CameraState) override;
};
```

From juce/video/VideoCompanion.h (existing - used by Task 2 coexistence toast):
```cpp
bool isActive() const { return active_.load(std::memory_order_relaxed); }
```

### JUCE button-index to return-code mapping (HIGH-7 ground truth)

Verified at libs/juce/modules/juce_gui_basics/windows/juce_AlertWindow.h:457-466. Showing via juce::AlertWindow::showAsync(MessageBoxOptions, callback), the callback receives an int per:

- 1 button:  button[0] returns 0
- 2 buttons: button[0] returns 1, button[1] returns 0
- 3 buttons: button[0] returns 1, button[1] returns 2, button[2] returns 0

Note: MessageBoxOptions::withButton(label) takes label TEXT ONLY - JUCE does NOT support withButton(label, returnCode). Therefore CameraStatusDialog MUST translate the int return code to a semantic Action via a per-cause table that knows the button order at construction time. The label-keyed dispatch pattern is verified in 19-02 Task 3 (NativeCameraPrivacyDialog::isAckResult) and reused here at scale.

### Softened cause-to-copy mapping (MEDIUM-6 fix - no DAW-specific blame)

- TCCDenied: "macOS has denied camera access to JamWide. Grant permission in System Settings -> Privacy & Security -> Camera, then click Recheck."
- HostLacksEntitlement: "The host application ({HostName}) can't access the camera right now. This usually means either the host hasn't requested camera permission for itself, or you've denied it for the host in System Settings.\n\nCheck System Settings -> Privacy & Security -> Camera and confirm {HostName} is in the list and enabled.\n\nTip: JamWide standalone has direct camera access."
- CameraInUse: "Another app appears to be using the camera, or no frames are reaching JamWide. Close other apps that use the camera, then click Recheck."
- NoHardware: "No camera detected. Connect a webcam and click Recheck."
- WindowsPrivacyBlock: "Windows has blocked camera access for JamWide or its host application. Enable camera access for desktop apps in Settings -> Privacy & Security -> Camera, then click Recheck."

### Cause -> button set

- TCCDenied: ["Open System Settings", "Recheck permission", "OK"] (3 buttons; JUCE returns 1,2,0)
- HostLacksEntitlement: ["Open System Settings", "Recheck permission", "OK"] (3 buttons; same set as TCCDenied per MEDIUM-6 - System Settings is also the correct action for host-denial)
- CameraInUse: ["Recheck permission", "OK"] (2 buttons; JUCE returns 1,0)
- NoHardware: ["Recheck permission", "OK"] (2 buttons; JUCE returns 1,0)
- WindowsPrivacyBlock: ["Open Camera Privacy Settings", "Recheck permission", "OK"] (3 buttons; JUCE returns 1,2,0)
- None (defensive): ["OK"] (1 button; JUCE returns 0)

### Deep-link URLs (D-15)

- macOS: `juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser()`
- Windows: `juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser()`

### Host detection (for HostLacksEntitlement copy interpolation)

- `juce::PluginHostType().getHostDescription()`

CMake from 19-01: test_camera_cause_mapping executable already declared; this plan fills tests/test_camera_cause_mapping.cpp body.

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
| T-19-01 | Tampering (TCC bypass / silent denial) | CameraStatusDialog cause classification + dialog | mitigate | Dialog shows specific cause to user with actionable next step (open System Settings / wait for another app / connect a camera). Suppress-after-first-show with cause-change re-show prevents user confusion without dialog spam. Verifiable: test_camera_cause_mapping unit asserts each cause produces the expected copy + deep-link button set + HIGH-7 button-index to Action mapping. |
| T-19-05 | Entitlement spoofing (verify in codesigned bundle, not just source file) | scripts/verify_camera_entitlement.sh | mitigate | The script reads the entitlement from the SHIPPED bundle via `codesign --display --entitlements -`, NOT from the source `.entitlements` file. Same script also extracts NSCameraUsageDescription from the bundle's Info.plist via `plutil`. Per RESEARCH section 7 lines 654-670 verbatim commands. Result: a tampered or stripped bundle fails the check; the source .entitlements file alone cannot satisfy it. |

</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name> Task 1: CameraStatusDialog with HIGH-7 label-keyed dispatch + MEDIUM-6 softened copy + cause-mapping unit test (D-13..D-16, T-19-01) </name>
  <files>
    juce/video/native/CameraStatusDialog.h,
    juce/video/native/CameraStatusDialog.cpp,
    juce/JamWideJuceEditor.cpp,
    juce/JamWideJuceEditor.h,
    tests/test_camera_cause_mapping.cpp
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md section 9 "Permission-Denial Fallback Dialog" (full section - cause matrix at lines 786-792, distinguishing TCCDenied vs HostLacksEntitlement at lines 796-810, AlertWindow pattern at lines 822-832, suppress-after-show logic at lines 836-844, copy strings at lines 866-880, action-button mapping at lines 888-893)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md HIGH-7 + MEDIUM-6 (the two findings this task addresses)
    - juce/video/native/JamWideCameraDevice.h + CameraFallbackCause.h (created by 19-01)
    - juce/video/native/NativeCameraPrivacyDialog.h (created by 19-02 - the isAckResult pattern that this dialog generalises)
    - juce/midi/MidiConfigDialog.cpp:115 (existing juce::AlertWindow::showAsync pattern in this codebase)
    - tests/test_video_fourcc.cpp (test harness pattern - JAMWIDE_BUILD_TESTS-gated, pure-C++)
    - libs/juce/modules/juce_gui_basics/windows/juce_AlertWindow.h lines 457-466 (the EXACT JUCE button-index to return-code mapping)
  </read_first>
  <behavior>
    - Behavior 1: `class CameraStatusDialog` exposes a single public method `void show(CameraFallbackCause cause, std::function<void(Action)> onResult)` that fires juce::AlertWindow::showAsync with cause-specific message + button set + icon (WarningIcon for TCCDenied/HostLacksEntitlement/WindowsPrivacyBlock; InfoIcon for CameraInUse/NoHardware). The completion callback translates JUCE's int return code through `actionFor(cause, juceResult)` to produce a semantic `Action` value.
    - Behavior 2 (HIGH-7 closure - the Action enum + actionFor table): `enum class Action { OpenSystemSettings, RecheckPermission, Dismiss };` and `static Action actionFor(CameraFallbackCause cause, int juceResult) noexcept;`. The `actionFor` implementation switches on cause to know the button count, then maps JUCE's int per the documented table: for 2-button causes (CameraInUse, NoHardware) juceResult==1 returns RecheckPermission and juceResult==0 returns Dismiss; for 3-button causes (TCCDenied, HostLacksEntitlement, WindowsPrivacyBlock) juceResult==1 returns OpenSystemSettings, ==2 returns RecheckPermission, ==0 returns Dismiss; for 1-button (None defensive) juceResult==0 returns Dismiss.
    - Behavior 3 (MEDIUM-6 closure - softened copy): For HostLacksEntitlement, the dialog message says "The host application ({HostName}) can't access the camera right now. This usually means either the host hasn't requested camera permission for itself, or you've denied it for the host in System Settings." - no DAW-specific blame. The "Open System Settings" button is included so the user can verify in the privacy pane (instead of OK-only previously, which was useless). Same approach for WindowsPrivacyBlock copy: "Windows has blocked camera access for JamWide or its host application" - generic, not blaming.
    - Behavior 4: On macOS, when cause is TCCDenied or HostLacksEntitlement, the dialog includes an "Open System Settings" button. The actionFor maps the click to OpenSystemSettings. The editor's onCameraFallback handler then calls `juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser()`.
    - Behavior 5: On Windows, when cause is WindowsPrivacyBlock, dialog includes "Open Camera Privacy Settings"; the editor launches `juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser()`.
    - Behavior 6: A "Recheck permission" button maps to RecheckPermission; the editor calls `nativeCamera->recheckPermission()`.
    - Behavior 7 (suppress-after-show): the dialog remembers lastShownCause_; if `show(cause, ...)` is called again with the same cause, the dialog does NOT re-show; it returns immediately with `onResult(Action::Dismiss)`. If a different cause is passed, the dialog re-shows.
    - Behavior 8: For HostLacksEntitlement, the dialog substitutes `juce::PluginHostType().getHostDescription()` into the {HostName} placeholders in the copy template at runtime.
    - Behavior 9 (HIGH-7 testability): A pure-C++ unit test exercises both the cause-to-copy-and-buttons mapping AND the actionFor mapping for every (cause, juceResult) cell.
  </behavior>
  <action>

Edit 1 - Create juce/video/native/CameraStatusDialog.h. Forward-declare juce::AlertWindow. Include CameraFallbackCause.h only. Declare the class with public methods (show, reset), the Action enum, and the static helpers (actionFor, copyFor, buttonsFor, iconFor) needed for unit testing. Header structure:

```cpp
#pragma once
#include "CameraFallbackCause.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jamwide {

class CameraStatusDialog {
public:
    enum class Action {
        OpenSystemSettings,
        RecheckPermission,
        Dismiss,
    };

    void show(CameraFallbackCause cause, std::function<void(Action)> onResult);
    void reset() noexcept;

    // === Static helpers (HIGH-7 - testable in isolation) ===
    // Maps JUCE's int return code to a semantic Action per cause's button count.
    // Per juce_AlertWindow.h:457-466:
    //   1-button:  button[0] -> 0
    //   2-button:  button[0] -> 1, button[1] -> 0
    //   3-button:  button[0] -> 1, button[1] -> 2, button[2] -> 0
    static Action actionFor(CameraFallbackCause cause, int juceResult) noexcept;
    static juce::String copyFor(CameraFallbackCause cause, const juce::String& hostName);
    static juce::StringArray buttonsFor(CameraFallbackCause cause);
    static juce::MessageBoxIconType iconFor(CameraFallbackCause cause) noexcept;

private:
    CameraFallbackCause lastShownCause_ = CameraFallbackCause::None;
};

} // namespace jamwide
```

Edit 2 - Create juce/video/native/CameraStatusDialog.cpp. Implementation outline:

- `copyFor(cause, hostName)` switches on cause and returns the softened copy per the MEDIUM-6 table in the Interfaces block. Substitute {HostName} via `juce::String::replace("{HostName}", hostName)`.
- `buttonsFor(cause)` switches on cause and returns juce::StringArray per the table in Interfaces. HostLacksEntitlement uses the SAME 3-button set as TCCDenied (MEDIUM-6 - System Settings is the correct action for both).
- `iconFor(cause)` - TCCDenied/HostLacksEntitlement/WindowsPrivacyBlock map to WarningIcon; CameraInUse/NoHardware map to InfoIcon; None maps to NoIcon.
- `actionFor(cause, juceResult)` switches on cause to know button count, then maps per JUCE's documented table:
  - 3-button (TCCDenied, HostLacksEntitlement, WindowsPrivacyBlock): juceResult==1 returns OpenSystemSettings, ==2 returns RecheckPermission, otherwise Dismiss.
  - 2-button (CameraInUse, NoHardware): juceResult==1 returns RecheckPermission, otherwise Dismiss.
  - 1-button (None) or unrecognised values: returns Dismiss.
- `show(cause, onResult)`:
  - If `cause == lastShownCause_` and lastShownCause_ != None: call `onResult(Action::Dismiss)` and return immediately (suppression rule).
  - Otherwise: `lastShownCause_ = cause`. Get hostName via `juce::PluginHostType().getHostDescription()`. Build MessageBoxOptions with iconFor + title="Camera unavailable" + copyFor + iterate buttonsFor adding each via `.withButton(label)`. Call `juce::AlertWindow::showAsync(options, [cause, onResult](int juceResult) { onResult(actionFor(cause, juceResult)); });`.
- `reset()` - clear lastShownCause_ to None.

Edit 3 - juce/JamWideJuceEditor.h: add `jamwide::CameraStatusDialog cameraStatusDialog_;` as a private member.

Edit 4 - juce/JamWideJuceEditor.cpp: replace the empty `onCameraFallback(cause)` stub from 19-02 Task 1 with the real implementation. The implementation calls `cameraStatusDialog_.show(cause, lambda)`, and the lambda switches on the returned Action: OpenSystemSettings launches the platform-conditional URL via `juce::URL(...).launchInDefaultBrowser()` (`#if JUCE_MAC` branch uses x-apple URL; `#elif JUCE_WINDOWS` branch uses ms-settings URL); RecheckPermission calls `processorRef.getNativeCamera()->recheckPermission()`; Dismiss focuses the camera button via `connectionBar.grabKeyboardFocus()`.

Edit 5 - juce/JamWideJuceEditor.cpp: extend `onCameraStateChanged` (from 19-02) - when state transitions OUT of Unavailable (to Opening or Capturing), call `cameraStatusDialog_.reset();` so the next denial re-shows the dialog.

Edit 6 - Fill tests/test_camera_cause_mapping.cpp. Pure-C++ test using only the JAMWIDE_BUILD_TESTS-exposed static functions from CameraStatusDialog. CMakeLists.txt (from 19-01) already builds this with juce::juce_core + juce::juce_gui_basics + CameraStatusDialog.cpp direct compile. Tests:

- Test 1 - 5 causes x copy mapping: For each CameraFallbackCause value, call `CameraStatusDialog::copyFor(cause, "TestHost")` and assert the returned string contains a cause-specific substring:
  - TCCDenied: "macOS has denied"
  - HostLacksEntitlement: contains "TestHost" AND "host application" AND "System Settings"
  - CameraInUse: "Another app"
  - NoHardware: "No camera detected"
  - WindowsPrivacyBlock: "Windows has blocked"
- Test 2 - 5 causes x button mapping: For each cause, call `buttonsFor(cause)` and assert the StringArray matches:
  - TCCDenied: 3 buttons "Open System Settings", "Recheck permission", "OK"
  - HostLacksEntitlement: 3 buttons SAME as TCCDenied (verify identical to TCCDenied via direct equality)
  - CameraInUse: 2 buttons "Recheck permission", "OK"
  - NoHardware: 2 buttons "Recheck permission", "OK"
  - WindowsPrivacyBlock: 3 buttons "Open Camera Privacy Settings", "Recheck permission", "OK"
- Test 3 - HIGH-7 button-index to Action mapping for every cell. For each cause, exercise every legal JUCE button-index value:
  - TCCDenied + juceResult=1 returns OpenSystemSettings
  - TCCDenied + juceResult=2 returns RecheckPermission
  - TCCDenied + juceResult=0 returns Dismiss
  - HostLacksEntitlement + 1 returns OpenSystemSettings
  - HostLacksEntitlement + 2 returns RecheckPermission
  - HostLacksEntitlement + 0 returns Dismiss
  - WindowsPrivacyBlock + 1 returns OpenSystemSettings
  - WindowsPrivacyBlock + 2 returns RecheckPermission
  - WindowsPrivacyBlock + 0 returns Dismiss
  - CameraInUse + 1 returns RecheckPermission
  - CameraInUse + 0 returns Dismiss
  - NoHardware + 1 returns RecheckPermission
  - NoHardware + 0 returns Dismiss
  - None + 0 returns Dismiss
  Total: 14 assertions. This is the explicit HIGH-7 fix validation - if a future JUCE upgrade changes the button-index mapping, this test fires loudly.
- Test 4 - HostLacksEntitlement template interpolation: `copyFor(HostLacksEntitlement, "REAPER")` must contain "REAPER" (host name substitutes correctly).
- Test 5 - None defensive fallback: `copyFor(None, "")` returns a non-empty string OR documented empty; `buttonsFor(None)` returns at minimum `["OK"]`; `actionFor(None, 0)` returns Dismiss.

Use the assert()/exit(1) pattern from tests/test_video_fourcc.cpp.

  </action>
  <verify>
    <automated> cmake --build build-juce-19-test --target test_camera_cause_mapping JamWideJuce 2>&amp;1 | tail -10 &amp;&amp; cd build-juce-19-test &amp;&amp; ctest -R camera_cause_mapping --output-on-failure &amp;&amp; cd .. &amp;&amp; test -f juce/video/native/CameraStatusDialog.h &amp;&amp; grep -c 'enum class Action' juce/video/native/CameraStatusDialog.h &amp;&amp; grep -c 'actionFor' juce/video/native/CameraStatusDialog.h &amp;&amp; grep -c 'x-apple.systempreferences' juce/video/native/CameraStatusDialog.cpp &amp;&amp; grep -c 'ms-settings:privacy-webcam' juce/JamWideJuceEditor.cpp &amp;&amp; grep -c 'cameraStatusDialog_.show' juce/JamWideJuceEditor.cpp &amp;&amp; grep -c 'recheckPermission' juce/JamWideJuceEditor.cpp </automated>
  </verify>
  <done> test_camera_cause_mapping exits 0 with all 5 scenarios + 14 button-mapping assertions passing; CameraStatusDialog exists with the Action enum + actionFor helper; the editor's onCameraFallback delegates via the dialog and routes the Action to the correct platform-conditional deep-link / recheck call. JamWideJuce builds cleanly including CameraStatusDialog.cpp. The HostLacksEntitlement copy is softened (does not say "doesn't request camera access for itself"). </done>
</task>

<task type="auto" tdd="true">
  <name> Task 2: VDO.Ninja coexistence toast + CHANGELOG entry (D-27, D-26) </name>
  <files>
    juce/JamWideJuceEditor.cpp,
    juce/ui/ConnectionBar.cpp,
    CHANGELOG.md
  </files>
  <read_first>
    - juce/video/VideoCompanion.h line 77 (the isActive() lock-free accessor used for coexistence detection)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md section 10 "VDO.Ninja Coexistence" (full section - coexistence detection at lines 902-917, toast pattern at lines 922-928, "informational only, does not block the camera toggle" note at line 931)
    - CHANGELOG.md if exists (check repo root); if not, create with a standard "Keep a Changelog" header
  </read_first>
  <behavior>
    - Behavior 1: When the user clicks the Camera button AND `processorRef.videoCompanion->isActive()` returns true, a non-blocking juce::AlertWindow toast appears with NoIcon, title="Multiple video stacks active", message="VDO.Ninja video is also active. Bandwidth and CPU may be high - consider stopping one for better quality.", button="OK".
    - Behavior 2: The toast is INFORMATIONAL - the camera toggle proceeds in parallel (D-27 "User can ignore and proceed"). The toast and the camera state machine run concurrently.
    - Behavior 3: The toast is suppressed after first show per session - a `static std::atomic<bool> coexistenceToastShown{false}` inside the editor's onCameraClicked lambda (reset on editor reconstruction = next plugin load); only the FIRST simultaneous-active triggers the toast. Subsequent toggles do NOT spam.
    - Behavior 4: CHANGELOG.md gains a new section under "Unreleased" describing Phase 19 features per D-26: native camera capture (macOS + Windows), preview popout, permission UX with cause-aware fallback dialog, plugin-state schema v3 to v4.

    NOTE: Risk C (license sanity check) is NOT part of this task - it was moved to 19-01 Task 1 Step 0 per MEDIUM-4. This task is purely the coexistence toast + CHANGELOG entry.
  </behavior>
  <action>

Edit 1 - juce/JamWideJuceEditor.cpp: extend the existing onCameraClicked lambda from 19-02 Task 1. The lambda body already implements the MEDIUM-1 decision tree; this task adds the coexistence-toast check at the TOP of the lambda, BEFORE the state-switch. Insert:

```cpp
static std::atomic<bool> coexistenceToastShown{false};
if (processorRef.videoCompanion && processorRef.videoCompanion->isActive()
    && !coexistenceToastShown.exchange(true)) {
    auto opts = juce::MessageBoxOptions{}
        .withIconType(juce::MessageBoxIconType::NoIcon)
        .withTitle("Multiple video stacks active")
        .withMessage("VDO.Ninja video is also active. Bandwidth and CPU may be high - "
                     "consider stopping one for better quality.")
        .withButton("OK");
    juce::AlertWindow::showAsync(opts, [](int){});
    // Fall through - D-27 says the toast does NOT block.
}
```

The exchange() pattern ensures the toast fires AT MOST ONCE per editor lifetime even if the user toggles repeatedly with VDO.Ninja active.

Edit 2 - CHANGELOG.md: if file does not exist, create with a "Keep a Changelog" header (date 2026-05-16; format `## [Unreleased]`). Add a new entry under `## [Unreleased]` -> `### Added`:

```
- **Phase 19 - Native Camera Capture (v1.3 beta)**: New "Camera" button in ConnectionBar opens a floating preview popout with the local webcam. Quality preset (Low/Medium/High) via right-click menu, with an explicit "Stop Camera" item. Cross-platform: macOS (arm64+x86_64) + Windows x86_64. macOS adds the com.apple.security.device.camera entitlement; plugin Info.plist gains NSCameraUsageDescription. Cause-aware fallback dialog handles all denial modes including the SPARTA #82 macOS DAW-host-lacks-entitlement case (REAPER, Live, Bitwig). Plugin state schema bumped v3 to v4 to persist popout bounds + quality preset + privacy ack. Coexists with the existing VDO.Ninja video stack during the parallel v1.3 beta.
```

If CHANGELOG.md exists with prior phase entries, add the Phase 19 bullet under the same "Unreleased" -> "Added" section, mirroring the existing prose style.

  </action>
  <verify>
    <automated> cmake --build build-juce-19-test --target JamWideJuce_Standalone 2>&amp;1 | tail -10 &amp;&amp; grep -c "Multiple video stacks active" juce/JamWideJuceEditor.cpp &amp;&amp; grep -c "videoCompanion" juce/JamWideJuceEditor.cpp &amp;&amp; grep -c "coexistenceToastShown" juce/JamWideJuceEditor.cpp &amp;&amp; test -f CHANGELOG.md &amp;&amp; grep -c "Phase 19" CHANGELOG.md &amp;&amp; grep -c "Native Camera" CHANGELOG.md </automated>
  </verify>
  <done> JamWideJuce_Standalone builds; the coexistence toast is wired with a once-per-session guard; CHANGELOG.md contains the Phase 19 entry. </done>
</task>

<task type="auto" tdd="true">
  <name> Task 3: PKG-04 entitlement verification script + manual UAT checklist (10 cells, includes HIGH-5 + HIGH-6 + LOW-1) (D-28, T-19-05, feedback_uat_scope_redflags) </name>
  <files>
    scripts/verify_camera_entitlement.sh,
    docs/UAT/phase-19-camera-uat-checklist.md
  </files>
  <read_first>
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md section 7 lines 654-670 (the verification commands - codesign --display --entitlements, plutil --extract NSCameraUsageDescription, xcrun notarytool, xcrun stapler validate)
    - .planning/phases/19-camera-capture-permission-ux/19-VALIDATION.md lines 65-83 (the 9 manual UAT cells per feedback_uat_scope_redflags)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md HIGH-5 (the new dedicated UAT cell explicitly exercises the NotDetermined-to-grant-to-modal-to-camera path) + HIGH-6 (Cell 5 explicitly tests mid-session revoke - the frame-stall watchdog from 19-01 Task 5) + LOW-1 (UAT tuning note about FIRST_FRAME_WATCHDOG_MS)
    - JamWide.entitlements (verifies the source state - should contain device.camera after 19-01 Task 1)
    - Project memory: project_apple_signing (Team ID T3KK66Q67T, notarization via API Key)
    - Project memory: feedback_uat_scope_redflags (no "verify only X, skip Y" UAT; all CAM-01/02/03 cells must be explicit)
  </read_first>
  <behavior>
    - Behavior 1: scripts/verify_camera_entitlement.sh accepts a single argument $BUNDLE_PATH (e.g., /Applications/JamWide.app or a build-tree path). Returns exit 0 on success, non-zero with a clear message on failure.
    - Behavior 2: The script checks four conditions, ANDed:
      a. The bundle exists and is a directory.
      b. `codesign --display --entitlements - "$BUNDLE_PATH"` succeeds AND output contains `com.apple.security.device.camera`.
      c. `plutil -extract NSCameraUsageDescription raw "$BUNDLE_PATH/Contents/Info.plist"` succeeds AND outputs "JamWide uses your webcam to share video with NINJAM peers." (the configured CAMERA_PERMISSION_TEXT).
      d. (Optional, only if --notarize flag is passed) `xcrun stapler validate "$BUNDLE_PATH"` exits 0.
    - Behavior 3: On any failure, the script prints a clear diagnostic naming which check failed and what the expected vs. actual values are.
    - Behavior 4: docs/UAT/phase-19-camera-uat-checklist.md enumerates 10 cells: the 9 from VALIDATION.md "Manual-Only Verifications" PLUS one new Cell 10 dedicated to the HIGH-5 first-launch flow. LOW-1 tuning note appears in Cell 1.
  </behavior>
  <action>

Edit 1 - Create scripts/verify_camera_entitlement.sh. Make it executable (chmod +x). Skeleton:

```bash
#!/usr/bin/env bash
# verify_camera_entitlement.sh - PKG-04 verification gate for Phase 19
# Usage: ./scripts/verify_camera_entitlement.sh <BUNDLE_PATH> [--notarize]
# Returns 0 on success; non-zero with diagnostic on failure.
set -euo pipefail

BUNDLE_PATH="${1:?Usage: $0 <BUNDLE_PATH> [--notarize]}"
NOTARIZE=${2:-}
EXPECTED_USAGE='JamWide uses your webcam to share video with NINJAM peers.'

if [[ ! -d "$BUNDLE_PATH" ]]; then
    echo "FAIL: $BUNDLE_PATH not found or not a directory" >&2
    exit 1
fi

# Check 1: entitlement present in codesigned bundle
if ! codesign --display --entitlements - "$BUNDLE_PATH" 2>/dev/null | grep -q 'com.apple.security.device.camera'; then
    echo "FAIL: com.apple.security.device.camera not found in bundle entitlements" >&2
    echo "      Hint: rebuild JamWide with JamWide.entitlements containing the camera key and JAMWIDE_HARDENED_RUNTIME=ON" >&2
    exit 2
fi

# Check 2: Info.plist NSCameraUsageDescription matches
if ! ACTUAL=$(plutil -extract NSCameraUsageDescription raw "$BUNDLE_PATH/Contents/Info.plist" 2>/dev/null); then
    echo "FAIL: NSCameraUsageDescription missing from Info.plist" >&2
    exit 3
fi
if [[ "$ACTUAL" != "$EXPECTED_USAGE" ]]; then
    echo "FAIL: NSCameraUsageDescription mismatch" >&2
    echo "      Expected: $EXPECTED_USAGE" >&2
    echo "      Actual:   $ACTUAL" >&2
    exit 4
fi

# Check 3 (optional): notarization staple is valid
if [[ "$NOTARIZE" == "--notarize" ]]; then
    if ! xcrun stapler validate "$BUNDLE_PATH"; then
        echo "FAIL: stapler validation failed (bundle not notarized or staple is missing)" >&2
        exit 5
    fi
fi

echo "OK: $BUNDLE_PATH passes PKG-04 entitlement + NSCameraUsageDescription verification"
if [[ "$NOTARIZE" == "--notarize" ]]; then
    echo "OK: notarization staple is valid"
fi
exit 0
```

Edit 2 - Create docs/UAT/phase-19-camera-uat-checklist.md. Header naming the phase, the date, and the link to VALIDATION.md. Then a checklist enumerating ALL 10 cells with explicit step-by-step instructions for each.

The 10 cells are:

- **Cell 1**: macOS standalone happy path (CAM-01 / SC1) - includes LOW-1 tuning note
- **Cell 2**: Logic Pro plugin happy path (CAM-01 / SC2)
- **Cell 3**: REAPER macOS plugin fallback (CAM-02 / SC3)
- **Cell 4**: macOS arm64 standalone (CAM-01 arm64)
- **Cell 5**: Permission revoke roundtrip (CAM-02 / SC4) - exercises HIGH-6 mid-session revoke detection end-to-end
- **Cell 6**: Notarization stapler validate (PKG-04 / D-28)
- **Cell 7**: Windows standalone happy path (CAM-01 / SC5)
- **Cell 8**: Windows privacy block (CAM-02 Windows)
- **Cell 9**: VDO.Ninja coexistence toast (D-27)
- **Cell 10 (NEW)**: First-launch privacy modal sequence (HIGH-5 closure validation)

Skeleton structure for the document - full content with steps for each cell:

```markdown
# Phase 19 - Camera Capture & Permission UX - Manual UAT Checklist

Date: 2026-05-16
Phase: 19 - Camera Capture & Permission UX
Source: .planning/phases/19-camera-capture-permission-ux/19-VALIDATION.md "Manual-Only Verifications" table

Per memory feedback_uat_scope_redflags: NO skipping CAM-01 / CAM-02 / CAM-03 happy and sad paths. Every cell below MUST be exercised before Phase 19 is declared complete.

## Cell 1 - macOS standalone happy path (CAM-01 / SC1)

**Why manual:** Requires real camera hardware + TCC prompt.

**Steps:**
1. Launch JamWide.app on macOS standalone (FRESH install - never granted camera permission before).
2. Click the Camera button in ConnectionBar.
3. Verify the macOS TCC prompt appears within 1 second.
4. Click Allow.
5. Verify the camera preview popout appears within 3 seconds.
6. Verify a live frame stream visible in the popout (motion test - wave hand in front of camera).

**Expected:** Preview live within 3 s of grant; popout titlebar reads "JamWide - Camera: <device name>".

**Record:** Tester name, macOS version, camera device name, observed time-to-first-frame in seconds.

**LOW-1 tuning note:** If the first-frame watchdog fires (preview popout never appears AND the CameraStatusDialog shows CameraInUse cause), the FIRST_FRAME_WATCHDOG_MS constant may be too aggressive for this camera+macOS combination. To tune: edit `juce/video/native/JamWideCameraDevice.h` and change `FIRST_FRAME_WATCHDOG_MS = 3000` to `5000`, rebuild, and re-run this cell. If 5000 ms also fires the watchdog, the camera is genuinely failing - investigate before declaring SC1 passed.

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 2 - Logic Pro plugin happy path (CAM-01 / SC2)

**Why manual:** Requires Logic Pro install + its camera entitlement.

**Steps:**
1. Load JamWide AU in Logic Pro on macOS.
2. Toggle Camera button.
3. Verify the macOS TCC prompt appears (if Logic Pro has not yet asked for camera).
4. Click Allow.
5. Verify preview appears within 3 seconds.

**Expected:** Logic Pro has com.apple.security.device.camera so plugin reaches Capturing without falling through to the HostLacksEntitlement dialog.

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 3 - REAPER macOS plugin fallback (CAM-02 / SC3)

**Why manual:** SPARTA #82 - host bundle ID controls TCC. REAPER does NOT request the camera entitlement.

**Steps:**
1. Load JamWide VST3 in REAPER on macOS.
2. Click Camera button.
3. Verify the CameraStatusDialog appears with the HostLacksEntitlement softened message (mentioning "host application (REAPER)" and "either the host hasn't requested camera permission for itself, or you've denied it for the host"). MEDIUM-6 closure check: the dialog must NOT use language that exclusively blames the host (e.g., "REAPER doesn't request camera access for itself").
4. Verify dialog has THREE buttons: "Open System Settings", "Recheck permission", "OK". MEDIUM-6 changed this from OK-only to give the user actionable next steps.
5. Click "Open System Settings" - verify the macOS Privacy & Security pane opens to the Camera tab.
6. Dismiss dialog. Verify JamWide does NOT crash.
7. Verify audio still works: load a session, play audio, verify peers can hear it.

**Expected:** Softened copy shown; 3-button set; deep-link works; no crash; audio session intact.

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 4 - macOS arm64 standalone (CAM-01 arm64)

**Why manual:** Spike was x86_64-only; arm64 build path uncovered.

**Steps:** Same as Cell 1 but on an arm64 Mac (M1/M2/M3/M4).

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 5 - Permission revoke roundtrip - HIGH-6 mid-session revoke (CAM-02 / SC4)

**Why manual:** Depends on macOS System Settings interaction. This cell is the end-to-end validation of the HIGH-6 mitigation (FrameStallWatchdog).

**Steps:**
1. Launch JamWide.app on macOS standalone. Grant camera permission (Cell 1 prerequisite).
2. Click Camera button. Verify preview is Capturing.
3. WITHOUT closing JamWide, open System Settings -> Privacy & Security -> Camera. Toggle JamWide OFF.
4. Return to JamWide.
5. Within 5 seconds, verify the preview frames stop. Within another 3 seconds (total ~7s after revoke), verify the CameraStatusDialog appears with TCCDenied cause (NOT CameraInUse - the frame-stall watchdog re-queries auth and detects the revoke).
6. Verify no crash.

**Expected:** HIGH-6 closure - frame-stall watchdog detects the revoke via re-querying auth and correctly routes to TCCDenied (not generic CameraInUse).

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 6 - Notarization stapler validate (PKG-04 / D-28)

**Why manual:** Depends on Apple notary service.

**Steps:**
1. Build a release JamWide bundle with JAMWIDE_HARDENED_RUNTIME=ON.
2. Codesign with the configured Team ID (T3KK66Q67T per project memory).
3. Submit to notary: `xcrun notarytool submit <zip> --keychain-profile <profile> --wait`.
4. Staple: `xcrun stapler staple <bundle>`.
5. Validate: `xcrun stapler validate <bundle>` (must exit 0).
6. Run `./scripts/verify_camera_entitlement.sh <bundle> --notarize` (must exit 0).

**Expected:** All 6 steps pass.

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 7 - Windows standalone happy path (CAM-01 / SC5)

**Why manual:** Requires Windows + camera hardware; SPARTA #82 does NOT apply on Windows.

**Steps:**
1. Launch JamWide.exe on Windows x86_64.
2. Toggle Camera button.
3. Verify preview appears within 3 seconds.

**Expected:** Windows allows desktop apps to access camera by default (depending on Settings -> Privacy -> Camera). Preview live.

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 8 - Windows privacy block (CAM-02 Windows)

**Why manual:** Depends on Windows Settings -> Privacy -> Camera.

**Steps:**
1. Open Windows Settings -> Privacy & Security -> Camera.
2. Disable "Camera access" for desktop apps.
3. Launch JamWide.exe and click Camera.
4. Verify the CameraStatusDialog appears with WindowsPrivacyBlock cause; softened MEDIUM-6 copy ("Windows has blocked camera access for JamWide or its host application").
5. Click "Open Camera Privacy Settings" - verify Windows Settings opens to the Camera page.
6. Click Recheck (after re-enabling in Settings) - verify camera opens.

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 9 - VDO.Ninja coexistence toast (D-27)

**Why manual:** Depends on both stacks running.

**Steps:**
1. Click Video button to start the existing VDO.Ninja video companion.
2. Wait for VDO.Ninja to reach the active state.
3. Click the Camera button.
4. Verify the non-blocking soft warning toast appears ("Multiple video stacks active...").
5. Click OK on the toast. Verify the camera still proceeds to Capturing (D-27 - the toast does NOT block).
6. Click the Camera button again to stop. Click again to start. Verify the toast does NOT re-appear (once-per-session guard).

- [ ] Pass / Fail / Date / Tester / Notes:

## Cell 10 (NEW) - First-launch privacy modal sequence (HIGH-5)

**Why manual:** Validates the HIGH-5 closure: the privacy modal MUST fire on the real first-launch path (NotDetermined -> requestAccess -> grant -> modal -> openDeviceAsync), not only when status was already Authorized.

**Setup:**
1. Reset camera permission for JamWide via macOS Terminal: `tccutil reset Camera com.jamwide.standalone` (replace bundle ID if needed). On Windows: this cell is N/A (no per-app permission gate).
2. Reset the cameraPrivacyAck flag in the plugin state: delete the JamWide preset (or hand-edit the saved state XML to set `cameraPrivacyAck="0"`). Confirm `tccutil reset` cleared the macOS TCC entry.

**Steps:**
1. Launch JamWide.app.
2. Click the Camera button. Note the time of click.
3. Verify the macOS TCC prompt appears (because status is NotDetermined). The prompt should display the NSCameraUsageDescription string "JamWide uses your webcam to share video with NINJAM peers."
4. Click Allow on the TCC prompt.
5. Verify the JamWide NativeCameraPrivacyDialog appears within 2 seconds of the Allow click. The dialog title is "Camera privacy notice"; body mentions "JamWide broadcasts your camera to the NINJAM server and peers in your room"; two buttons "I understand" and "Cancel".
6. Click "I understand".
7. Verify the camera preview popout appears (camera proceeds to Capturing).
8. CLOSE JamWide. Re-open.
9. Click the Camera button. Verify the camera opens DIRECTLY (no privacy modal, no TCC prompt) - the ack was persisted via state v4.

**Expected:** HIGH-5 closure - the privacy modal fires on the NotDetermined-to-grant path, NOT only on the "already Authorized" path. Second launch skips the modal (ack persisted).

**Failure modes to flag:**
- Modal does NOT appear after TCC grant => HIGH-5 regression; the editor's handleCameraIdleClick is checking the WRONG status state.
- Modal appears on the second launch => privacyAck persistence broken in v4 schema.
- TCC prompt appears on the second launch => an unrelated bug in JUCE permission storage.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Phase 19 Sign-Off

When all 10 cells are marked Pass with date + tester recorded, this checklist is complete and Phase 19 can transition to Phase 20.

Per `feedback_uat_scope_redflags`: NO cell may be deferred to Phase 24. All 10 cells MUST be exercised before Phase 19 is declared complete.

Other DAW cells (Live, Bitwig on macOS; REAPER on Windows) belong to Phase 24's per-DAW matrix per CONTEXT "Deferred Ideas" - explicitly out of Phase 19.
```

The 10 cells above are the FULL document content. Write the entire document via the Write tool. Note that the file must read out to roughly 200+ lines including the markdown formatting.

  </action>
  <verify>
    <automated> test -x scripts/verify_camera_entitlement.sh &amp;&amp; bash -n scripts/verify_camera_entitlement.sh &amp;&amp; test -f docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "CAM-01" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "CAM-02" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "CAM-03\|CameraStatusDialog" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "PKG-04" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "HIGH-5" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "HIGH-6" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "LOW-1\|FIRST_FRAME_WATCHDOG_MS" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "REAPER" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "stapler validate" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; grep -c "VDO.Ninja\|coexistence" docs/UAT/phase-19-camera-uat-checklist.md &amp;&amp; test "$(grep -c '^## Cell ' docs/UAT/phase-19-camera-uat-checklist.md)" -eq 10 </automated>
  </verify>
  <done> Script exists, is executable, passes `bash -n` syntax check. UAT checklist enumerates all 10 cells with explicit steps; every CAM-01/02/03 / PKG-04 cell is named; SPARTA #82 + REAPER + VDO.Ninja + stapler all appear; HIGH-5 has its own Cell 10; HIGH-6 is explicitly the end-to-end coverage of Cell 5; LOW-1 tuning note appears in Cell 1. </done>
</task>

</tasks>

<verification>

## Plan-Level Verification

```bash
# 1. CameraStatusDialog test green
cmake --build build-juce-19-03 --target test_camera_cause_mapping JamWideJuce 2>&1 | tail -10
cd build-juce-19-03 && ctest -R camera_cause_mapping --output-on-failure && cd ..

# 2. Editor wires the dialog + deep links + Action enum
grep -c 'cameraStatusDialog_.show' juce/JamWideJuceEditor.cpp                                # >= 1
grep -c 'x-apple.systempreferences:com.apple.preference.security' juce/JamWideJuceEditor.cpp # >= 1
grep -c 'ms-settings:privacy-webcam' juce/JamWideJuceEditor.cpp                              # >= 1
grep -c 'recheckPermission' juce/JamWideJuceEditor.cpp                                       # >= 1
grep -c 'CameraStatusDialog::Action' juce/JamWideJuceEditor.cpp                              # >= 1

# 3. HIGH-7 helper present
grep -c 'enum class Action' juce/video/native/CameraStatusDialog.h                           # >= 1
grep -c 'actionFor' juce/video/native/CameraStatusDialog.h                                   # >= 1

# 4. MEDIUM-6 softened copy
grep -c 'host application' juce/video/native/CameraStatusDialog.cpp                          # >= 1
test "$(grep -c "doesn.t request camera access for itself" juce/video/native/CameraStatusDialog.cpp)" -eq 0

# 5. Coexistence toast wired
grep -c 'Multiple video stacks active' juce/JamWideJuceEditor.cpp                            # >= 1
grep -c 'coexistenceToastShown' juce/JamWideJuceEditor.cpp                                   # >= 1

# 6. Entitlement verification script exists and runs
test -x scripts/verify_camera_entitlement.sh
bash -n scripts/verify_camera_entitlement.sh

# 7. UAT checklist enumerates all 10 cells (9 from VALIDATION.md + 1 new HIGH-5 cell)
grep -c '^## Cell ' docs/UAT/phase-19-camera-uat-checklist.md                                # == 10
grep -c 'HIGH-5' docs/UAT/phase-19-camera-uat-checklist.md                                   # >= 1
grep -c 'HIGH-6' docs/UAT/phase-19-camera-uat-checklist.md                                   # >= 1
grep -c 'FIRST_FRAME_WATCHDOG_MS\|LOW-1' docs/UAT/phase-19-camera-uat-checklist.md           # >= 1

# 8. CHANGELOG entry exists
grep -c 'Phase 19' CHANGELOG.md                                                              # >= 1
grep -c 'Native Camera' CHANGELOG.md                                                         # >= 1
```

</verification>

<success_criteria>

This plan succeeds when:

1. **HIGH-7 closed** - CameraStatusDialog::actionFor maps JUCE's int button-result to a semantic Action enum per cause's button count; test_camera_cause_mapping has 14 explicit assertions covering every (cause, juceResult) cell.
2. **MEDIUM-6 closed** - HostLacksEntitlement copy is softened (no DAW-specific blame); the dialog now includes 3 buttons including "Open System Settings" (was OK-only). Same approach for WindowsPrivacyBlock.
3. **LOW-1 documented** - UAT Cell 1 contains the tuning note about FIRST_FRAME_WATCHDOG_MS being editable from 3000 to 5000 if cameras are slow.
4. **Fallback dialog ships** - CameraStatusDialog handles all 5 causes; editor delegates onCameraFallback via the dialog; cause-change re-show + suppress-after-show work per D-14.
5. **Platform deep-links work** - macOS button launches x-apple.systempreferences URL; Windows button launches ms-settings:privacy-webcam URL.
6. **VDO.Ninja coexistence toast** - Once-per-session non-blocking warning when user starts native camera while VDO.Ninja is active (D-27).
7. **PKG-04 verification script** - scripts/verify_camera_entitlement.sh exists, is executable, validates entitlement + NSCameraUsageDescription + (optional) notarization staple.
8. **Manual UAT checklist** - docs/UAT/phase-19-camera-uat-checklist.md enumerates 10 cells (9 from VALIDATION.md + 1 new HIGH-5 cell); per feedback_uat_scope_redflags every CAM-01/02/03 path is explicit.
9. **HIGH-5 end-to-end UAT** - Cell 10 explicitly exercises the NotDetermined-to-grant-to-modal-to-camera path including the second-launch persistence check.
10. **HIGH-6 end-to-end UAT** - Cell 5 explicitly exercises the mid-session permission revoke detection (the FrameStallWatchdog from 19-01 Task 5).
11. **Cause-mapping unit test** - test_camera_cause_mapping exits 0; all 5 causes x copy mapping + 5 button mapping + 14 button-index-to-Action cells covered.
12. **CHANGELOG note** - Phase 19 user-facing entry added under Unreleased (D-26).

</success_criteria>

<output>

Create `.planning/phases/19-camera-capture-permission-ux/19-03-SUMMARY.md` summarizing:
- CameraStatusDialog cause-to-copy-and-buttons mapping (verbatim final strings used in production, after MEDIUM-6 softening)
- HIGH-7 fix: actionFor table for each cause's button count; cite the test_camera_cause_mapping Test 3 line numbers
- MEDIUM-6 fix: comparison of OLD vs NEW HostLacksEntitlement copy + buttons (was OK-only, now 3-button with Open System Settings)
- LOW-1 documentation: where in Cell 1 the tuning note appears
- Editor's onCameraFallback dispatch logic (switch on Action enum)
- VDO.Ninja coexistence toast once-per-session guard implementation
- PKG-04 verification: result of running scripts/verify_camera_entitlement.sh against the locally-built JamWide.app (success/failure with reason)
- UAT checklist: 10 cells enumerated; per-DAW UAT (Live, Bitwig, Windows REAPER) explicitly deferred to Phase 24 per VALIDATION.md (this plan covers macOS standalone + Logic Pro + REAPER macOS + macOS arm64 + permission revoke + notarization + Windows standalone + Windows privacy block + VDO.Ninja coexistence + HIGH-5 first-launch = 10 total in Phase 19 scope)
- HIGH-5 verification status: Cell 10 result (pass/fail/notes)
- HIGH-6 verification status: Cell 5 result (pass/fail/notes - especially whether TCCDenied was correctly emitted by the FrameStallWatchdog re-query rather than CameraInUse)
- D-28 notarization smoke: was a test bundle built + codesigned + notarized + stapled in this plan execution? If yes, record outcome. If no (because notarization requires the API Key keychain profile not present in CI), record as a deferred manual UAT cell.
- Risk C status: confirm it was resolved in 19-01 Task 1 Step 0 (cite the 19-01-SUMMARY.md outcome).

</output>

## Addressed Review Findings

| Codex Finding | Resolution | Task(s) |
|---------------|------------|---------|
| **HIGH-1** | (Resolved in 19-01 Task 1.) | (19-01) |
| **HIGH-2** | (Resolved in 19-01 Task 3.) | (19-01) |
| **HIGH-3** | (Resolved in 19-01 Task 5.) | (19-01) |
| **HIGH-4** | (Resolved in 19-02 Task 2.) | (19-02) |
| **HIGH-5** | (Resolved in 19-02 Task 3 editor sequence; end-to-end UAT in this plan's Task 3 Cell 10.) | (19-02 Task 3) + Task 3 Cell 10 |
| **HIGH-6** | (Resolved in 19-01 Task 5 FrameStallWatchdog; end-to-end UAT in this plan's Task 3 Cell 5.) | (19-01 Task 5) + Task 3 Cell 5 |
| **HIGH-7** (AlertWindow button index) | CameraStatusDialog uses `actionFor(cause, juceResult)` static helper that switches on cause to know button count, then maps JUCE's int return-code (per `juce_AlertWindow.h:457-466`) to a semantic `Action` enum. Editor's onCameraFallback handler dispatches on Action, not on raw int. 14 explicit assertions in test_camera_cause_mapping Test 3 cover every (cause, juceResult) cell. If a future JUCE upgrade changes the mapping, this test fires loudly. | Task 1 (entire) |
| **MEDIUM-1** | (Resolved in 19-02 Task 1 decision tree.) | (19-02) |
| **MEDIUM-2** | (Resolved in 19-01 Task 4.) | (19-01) |
| **MEDIUM-3** | (Resolved in 19-01 Task 4.) | (19-01) |
| **MEDIUM-4** | (Resolved in 19-01 Task 1 Step 0.) | (19-01) |
| **MEDIUM-5** | (Resolved in 19-01 Task 1 Step 5.) | (19-01) |
| **MEDIUM-6** (cause-detection approximate) | HostLacksEntitlement copy softened to neither blame the host nor the user definitively: "The host application ({HostName}) can't access the camera right now. This usually means either the host hasn't requested camera permission for itself, or you've denied it for the host in System Settings." Action-button set ALSO changed from OK-only to 3 buttons (Open System Settings, Recheck permission, OK) so the user has actionable next steps. Same softening + 3-button approach applied to WindowsPrivacyBlock. Test_camera_cause_mapping Test 2 verifies HostLacksEntitlement buttons are identical to TCCDenied buttons (3-button set), pinning the new behavior. | Task 1 (copy + buttons + tests) + UAT Cell 3 verifies the softened copy is shown in REAPER |
| **LOW-1** (3-second watchdog brittle) | LOW-1 tuning note appears in UAT Cell 1 documenting that `FIRST_FRAME_WATCHDOG_MS` in `JamWideCameraDevice.h` can be changed from 3000 to 5000 if a slow camera + macOS high preset combo trips the watchdog spuriously. The constant is exposed as a class-static `constexpr int` in 19-01 Task 5 specifically so testers can find and tune it. | Task 3 Cell 1 (LOW-1 tuning note) |
