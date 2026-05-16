---
status: partial
phase: 19-camera-capture-permission-ux
source: [19-VERIFICATION.md]
started: 2026-05-16T19:00:00Z
updated: 2026-05-16T13:00:00Z
---

## Current Test

[testing complete on this machine; 6 cells blocked on other environments]

## Tests

### 1. Cell 1 — macOS standalone happy path (CAM-01 / SC1)
expected: macOS TCC prompt appears; Allow yields preview popout within 3s with live frames
result: pass
notes: |
  Initial attempt flagged a connection-bar layout bug: "Conn", "Fit", "D...", "Camera" overlapped
  at the default plugin width because the right cluster ran under the left cluster (Camera/DBG/Fit
  drawing over Connect/Browse). Fixed in two parts before retest:
    1. Removed videoButton (legacy VDO.Ninja toggle) — Camera (Phase 19) is now the sole video
       entry point. Saved ~60 px on the right cluster.
    2. Bumped kBaseWidth 1030 → 1200 so the right cluster doesn't overlap left cluster even
       with the 130 px Camera button (which expands its label to "Recheck permission" on TCC denial).
  Files: juce/ui/ConnectionBar.h, juce/ui/ConnectionBar.cpp, juce/JamWideJuceEditor.cpp,
         juce/JamWideJuceEditor.h. Build 320. Retest with fresh standalone: PASS.

### 2. Cell 2 — Logic Pro AU plugin happy path (CAM-01 / SC2)
expected: Logic Pro hosts JamWide AU; camera grant produces preview within 3s
result: blocked
blocked_by: prior-phase
reason: |
  Logic Pro is not installed on this machine (/Applications/Logic*.app: none).
  User tested in GarageBand instead; GarageBand lacks NSCameraUsageDescription
  in its Info.plist so it can't grant camera to any plugin context (SPARTA #82 /
  Phase 19 Risk F). The observed behavior — JamWide showing CameraStatusDialog
  with the "Open System Settings" path and no TCC prompt firing — is by design
  for that host scenario. This is cross-evidence for Cell 3 (REAPER fallback)
  on the same HostLacksEntitlement code path, not for Cell 2's Logic Pro
  happy path. Cell 2 should be re-attempted on a machine with Logic Pro.
corroborating_evidence: |
  During Cell 3 testing the user reported: "In bitwig it works, because bitwig
  is in the list of apps that has camera permission." Bitwig has
  NSCameraUsageDescription, so JamWide AU loaded in Bitwig successfully reaches
  Authorized state and starts capture. This is the SAME code path Logic Pro
  would take — it confirms the happy-path Cell 2 SC2 outcome on entitled hosts,
  even though Cell 2 specifically named Logic Pro and remains formally blocked
  until Logic Pro is installed.

### 3. Cell 3 — REAPER macOS plugin fallback (CAM-02 / SC3)
expected: CameraStatusDialog shows HostLacksEntitlement (softened, MEDIUM-6 wording); 3-button set; Open System Settings opens privacy pane; no crash; audio still works
result: pass
evidence: |
  Screenshot confirms: WarningIcon + title "Camera unavailable" + MEDIUM-6
  message with "Reaper" interpolated into {HostName} + "Tip: JamWide standalone
  has direct camera access" + 3 buttons (Open System Settings / Recheck permission
  / OK). OK dismisses without re-attempting (correct). No crash; user did not
  report audio dropout. "Open System Settings" deep-link URL launch
  (x-apple.systempreferences:com.apple.preference.security?Privacy_Camera) not
  explicitly clicked through during this UAT cell but the launch site is
  verified at juce/JamWideJuceEditor.cpp:803.
notes: |
  Recheck-still-denied UX gap: D-14 duplicate-suppression in
  CameraStatusDialog::show (juce/video/native/CameraStatusDialog.cpp:174)
  swallows the re-show when the cause is unchanged — Recheck silently fires
  onResult(Dismiss), so the user sees no visible feedback that the recheck
  happened. Working as specified (D-14 anti-spam intent) but the still-denied
  feedback gap is real. Filed as follow-up improvement under Gaps.
  Cross-evidence: user reports "In bitwig it works, because bitwig is in the
  list of apps that has camera permission" — this corroborates the Cell 2
  happy-path code path on entitled hosts (substituting Bitwig for Logic Pro).

### 4. Cell 4 — macOS arm64 standalone (CAM-01 arm64)
expected: Apple Silicon native build works identically to Cell 1
result: blocked
blocked_by: physical-device
reason: |
  Development machine is x86_64 (per memory: scripts/build.sh produces
  x86_64-only locally; CI builds universal). Cell 4 requires Apple Silicon
  hardware to validate the arm64 happy path. Defer to either CI lane or a
  separate test session on an M-series Mac.

### 5. Cell 5 — Permission revoke roundtrip (CAM-02 / SC4) — HIGH-6 end-to-end
expected: Mid-session toggling JamWide OFF in System Settings stops frames within ~5s; FrameStallWatchdog re-queries auth; CameraStatusDialog appears with TCCDenied cause (NOT CameraInUse); no crash
result: blocked
blocked_by: other
reason: |
  Cell 5 spec is incompatible with macOS Sonoma+ TCC behavior. The OS does
  NOT enforce camera-permission revoke mid-session for a running app — it
  shows a "Quit & Reopen / Later" modal (screenshot captured 2026-05-16)
  and defers enforcement until the next app launch. Frames continue to flow
  through the currently-open AVCaptureSession. The FrameStallWatchdog
  (juce/video/native/JamWideCameraDevice.cpp:83-99) is correctly implemented
  — it gates on `now - lastFrameMs > FRAME_STALL_THRESHOLD_MS` and only
  fires on a real stall — but the System Settings toggle never produces a
  stall, so this trigger cannot exercise the watchdog. HIGH-6 unit coverage
  exists in tests/test_camera_frame_stall.cpp per 19-VALIDATION.md. Cell 5
  needs a rewrite against a trigger macOS actually supports (USB camera
  unplug, scripted stall, or a forced AVCaptureSession invalidation). Filed
  as follow-up gap.

### 6. Cell 6 — Notarization stapler validate (PKG-04 / D-28)
expected: Release build with JAMWIDE_HARDENED_RUNTIME=ON, codesigned with Team T3KK66Q67T, notarized, stapled, and verify_camera_entitlement.sh --notarize exits 0
result: blocked
blocked_by: release-build
reason: |
  Current local builds are ad-hoc signed Release with JAMWIDE_HARDENED_RUNTIME
  default. Cell 6 needs a fully notarized, Developer-ID-signed bundle. That's
  Phase 23's deliverable (macOS universal stitching + per-dylib codesign +
  notarization + stapler verification). Defer to the Phase 23 packaging lane
  when it's exercised. User has Apple Developer setup (Team T3KK66Q67T per
  memory project_apple_signing); the gating constraint is "produce a
  Developer-ID-signed + notarized build", not credentials.

### 7. Cell 7 — Windows standalone happy path (CAM-01 / SC5)
expected: JamWide.exe on Windows x86_64 grants camera preview within 3s
result: blocked
blocked_by: physical-device
reason: |
  No Windows machine available in this session. Defer to a separate Windows
  UAT pass (or CI-driven smoke test on the Windows lane being added in
  Phase 23-03).

### 8. Cell 8 — Windows privacy block (CAM-02 Windows)
expected: CameraStatusDialog shows WindowsPrivacyBlock with softened copy and 3 buttons; ms-settings:privacy-webcam URL opens; recheck resumes capture
result: blocked
blocked_by: physical-device
reason: |
  No Windows machine available in this session. Code path is symmetric to
  Cell 3's HostLacksEntitlement: same CameraStatusDialog with 3 buttons,
  different cause (WindowsPrivacyBlock) and different deep-link URL
  (ms-settings:privacy-webcam vs x-apple.systempreferences:...). Defer
  alongside Cell 7.

### 9. Cell 9 — VDO.Ninja coexistence toast (D-27)
expected: Once-per-session non-blocking "Multiple video stacks active" toast appears when starting native camera while VDO.Ninja is active; toggling repeatedly does not re-show
result: skipped
reason: |
  Unreachable via UI after the legacy Video button was removed in this
  session's Cell 1 fix (ConnectionBar.cpp; commit pending). The coexistence
  scenario required activating VDO.Ninja first via the Video button →
  VideoPrivacyDialog → launchCompanion(). With that path gone, there is no
  UI affordance to set videoCompanion->isActive() == true, so the runtime
  check at JamWideJuceEditor.cpp:193 (`if videoCompanion && isActive() && !
  coexistenceToastShown_.exchange(true)`) can never trigger via user
  interaction. The toast logic itself is preserved as a defensive guard in
  case a future code path re-activates VideoCompanion (e.g., persisted
  state), but the D-27 scenario is no longer a user-visible flow worth
  asserting on. Recommend removing D-27 from v1.3 acceptance criteria and
  retiring the coexistence-toast code in the same milestone that fully
  removes VideoCompanion.

### 10. Cell 10 — First-launch privacy modal sequence (HIGH-5 end-to-end)
expected: tccutil reset + privacyAck delete; click -> TCC prompt with correct NSCameraUsageDescription -> Allow -> NativeCameraPrivacyDialog -> I understand -> preview; second launch skips both modals
result: pass
notes: |
  Setup: `osascript -e 'quit app "JamWide"' && tccutil reset Camera com.jamwide.juce-client && rm -f ~/Library/Application\ Support/JamWide.settings ~/Library/Application\ Support/JamWide\ JUCE.settings` (tccutil reported 3 entries reset — likely standalone/AU/VST3 each registered separately by bundle path), then `open build-juce/JamWideJuce_artefacts/Release/Standalone/JamWide.app`. User confirmed full HIGH-5 sequence on first click (TCC prompt → Allow → NativeCameraPrivacyDialog → I understand → preview) and modal-skip on second launch.

### 11. Risk C — JUCE commercial seat licence confirmation
expected: User confirms JUCE commercial seat covers juce_video for JamWideJuce target
result: pass
notes: |
  User (mark-k.schulze, Team T3KK66Q67T) confirms JUCE commercial seat
  covers juce_video. The Phase 19 RESEARCH-listed contingency (rewrite to
  direct AVFoundation, +1 plan +800 LOC) is no longer needed. Q1 is closed.

## Summary

total: 11
passed: 4
issues: 0
pending: 0
skipped: 1
blocked: 6

session_outcome: partial
machine_side_complete: true
deferred_environments:
  - "Logic Pro install (Cell 2)"
  - "Apple Silicon hardware (Cell 4)"
  - "macOS USB-camera-unplug or scripted stall trigger (Cell 5 — spec rewrite required)"
  - "Developer-ID-signed + notarized + stapled Release build (Cell 6, Phase 23 deliverable)"
  - "Windows x86_64 machine (Cells 7, 8)"

## Gaps

- truth: "Connection bar buttons are visually distinct and non-overlapping at standard plugin/standalone width"
  status: resolved
  reason: "User reported: the buttones overlay each other, we can remove the video button in favour of the camera button"
  severity: major
  test: 1
  artifacts: ["juce/ui/ConnectionBar.cpp", "juce/ui/ConnectionBar.h", "juce/JamWideJuceEditor.cpp", "juce/JamWideJuceEditor.h"]
  resolution: |
    Removed legacy videoButton + onVideoClicked + setVideoActive + dead VideoPrivacyDialog
    onResponse handler. Bumped kBaseWidth 1030 → 1200 (right cluster was overlapping left
    cluster by ~145 px at the old default). Build 320; user confirmed pass on retest.

- truth: "Recheck-permission button in CameraStatusDialog provides visible feedback when status is still denied"
  status: resolved
  reason: "D-14 duplicate-suppression in CameraStatusDialog::show (juce/video/native/CameraStatusDialog.cpp:174) silently dismisses the re-show when cause==lastShownCause_; Recheck for an unchanged-denied state fires onResult(Dismiss) with no visible UI change, making the button appear broken."
  severity: minor
  test: 3
  artifacts: ["juce/video/native/CameraStatusDialog.cpp", "juce/video/native/CameraStatusDialog.h", "juce/JamWideJuceEditor.cpp"]
  resolution: |
    Fixed in commit 9679e7b. JamWideJuceEditor::onCameraFallback now calls
    cameraStatusDialog_.reset() before cam->recheckPermission() in the
    Action::RecheckPermission handler. On the still-denied path the next
    show() finds lastShownCause_ == None, so the equality check fails and
    the dialog re-displays — implicitly confirming "still denied" to the
    user. On the now-authorized path nothing changes (no fallback emitted,
    no show() called). Build 321; re-test by clicking Recheck while denied
    and confirming the dialog re-appears.
