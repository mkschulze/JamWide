# Phase 19 - Camera Capture & Permission UX - Manual UAT Checklist

Date: 2026-05-16
Phase: 19 - Camera Capture & Permission UX
Source: `.planning/phases/19-camera-capture-permission-ux/19-VALIDATION.md` "Manual-Only Verifications" table (the 9 original cells) + this plan's new Cell 10 (HIGH-5 first-launch sequence end-to-end).

Per memory `feedback_uat_scope_redflags`: NO skipping CAM-01 / CAM-02 / CAM-03 happy and sad paths. Every cell below MUST be exercised before Phase 19 is declared complete.

Other DAW cells (Live, Bitwig on macOS; REAPER on Windows) belong to Phase 24's per-DAW matrix per CONTEXT "Deferred Ideas" - explicitly out of Phase 19.

---

## Cell 1 - macOS standalone happy path (CAM-01 / SC1)

**Why manual:** Requires real camera hardware + macOS TCC prompt.

**Setup:**
- Fresh macOS (Apple Silicon or Intel) with no prior JamWide camera grant. If JamWide has previously been granted camera access, reset via Terminal: `tccutil reset Camera com.jamwide.standalone` (substitute the correct bundle ID).
- Built JamWide.app (Standalone) signed with `JAMWIDE_HARDENED_RUNTIME=ON`.

**Steps:**
1. Launch `JamWide.app` on macOS standalone.
2. Click the Camera button in ConnectionBar.
3. Verify the macOS TCC prompt appears within 1 second. The prompt body should read "JamWide uses your webcam to share video with NINJAM peers." (the configured `NSCameraUsageDescription`).
4. Click **Allow**.
5. Verify the camera preview popout appears within 3 seconds.
6. Verify a live frame stream visible in the popout (motion test - wave hand in front of camera).

**Expected:** Preview live within 3 s of grant; popout titlebar reads `JamWide - Camera: <device name>`.

**Record:** Tester name, macOS version, camera device name, observed time-to-first-frame in seconds.

**LOW-1 tuning note:** If the first-frame watchdog fires (preview popout never appears AND the CameraStatusDialog shows `CameraInUse` cause), the `FIRST_FRAME_WATCHDOG_MS` constant may be too aggressive for this camera+macOS combination. To tune: edit `juce/video/native/JamWideCameraDevice.h` and change `FIRST_FRAME_WATCHDOG_MS = 3000` to `5000`, rebuild, and re-run this cell. If 5000 ms also fires the watchdog, the camera is genuinely failing - investigate before declaring SC1 passed.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 2 - Logic Pro plugin happy path (CAM-01 / SC2)

**Why manual:** Requires Logic Pro install + Logic Pro's own camera entitlement.

**Setup:**
- Logic Pro installed on the test machine.
- JamWide AU plugin installed (`~/Library/Audio/Plug-Ins/Components/JamWide.component`).

**Steps:**
1. Open Logic Pro.
2. Create a track and insert the JamWide AU plugin.
3. In the JamWide UI, toggle the Camera button.
4. If macOS TCC has not yet prompted for Logic Pro: verify the prompt appears (host-process bundle ID is `com.apple.logic10`).
5. Click **Allow**.
6. Verify the preview popout appears within 3 seconds with a live frame stream.

**Expected:** Logic Pro has `com.apple.security.device.camera` in its own entitlements, so the plugin reaches `Capturing` without falling through to the `HostLacksEntitlement` dialog.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 3 - REAPER macOS plugin fallback (CAM-02 / SC3)

**Why manual:** SPARTA #82 - host bundle ID controls TCC. REAPER does NOT request the `com.apple.security.device.camera` entitlement, so JamWide cannot reach the camera no matter the user's grant state for JamWide itself.

**Setup:**
- REAPER installed on macOS.
- JamWide VST3 plugin installed (`~/Library/Audio/Plug-Ins/VST3/JamWide.vst3`).

**Steps:**
1. Open REAPER.
2. Insert JamWide as a VST3 effect on a track.
3. Click the Camera button.
4. Verify the `CameraStatusDialog` appears with the `HostLacksEntitlement` softened message — body mentions "host application (REAPER)" and "either the host hasn't requested camera permission for itself, or you've denied it for the host". **MEDIUM-6 closure check:** the dialog MUST NOT use language that exclusively blames the host (e.g. NOT "REAPER doesn't request camera access for itself").
5. Verify the dialog has THREE buttons in this order: **Open System Settings** / **Recheck permission** / **OK**. MEDIUM-6 changed this from OK-only to give the user actionable next steps.
6. Click **Open System Settings** — verify the macOS Privacy & Security pane opens to the Camera tab.
7. Dismiss the dialog (close it or click OK). Verify JamWide does NOT crash.
8. Verify audio still works: load a session, play audio, verify peers can hear it (if connected to a NINJAM server).

**Expected:** Softened copy shown; 3-button set; deep-link works; no crash; audio session intact.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 4 - macOS arm64 standalone (CAM-01 arm64)

**Why manual:** The Phase 14.3 video spike was x86_64-only; arm64 build path uncovered until Phase 23 universal-binary stitching.

**Setup:**
- macOS Apple Silicon (M1 / M2 / M3 / M4) build of `JamWide.app` (arm64 slice).

**Steps:** Same as Cell 1 but on the arm64 Mac. Verify that all six steps from Cell 1 produce identical behavior. Pay attention to time-to-first-frame: arm64 cameras (Apple Silicon FaceTime HD or Studio Display webcam) may behave differently from Intel-Mac hardware.

**Expected:** Identical behavior to Cell 1 (same time-to-first-frame within ±500 ms).

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 5 - Permission revoke roundtrip - HIGH-6 mid-session revoke (CAM-02 / SC4)

**Why manual:** Depends on macOS System Settings interaction. This cell is the end-to-end validation of the HIGH-6 mitigation (the `FrameStallWatchdog` from 19-01 Task 5).

**Setup:**
- macOS standalone JamWide.app, with camera permission previously granted (Cell 1 must have run first).
- An active jam session is NOT required; the camera path can be tested standalone.

**Steps:**
1. Launch JamWide.app on macOS standalone.
2. Click the Camera button. Verify the preview popout shows live frames (state = `Capturing`).
3. WITHOUT closing JamWide, open `System Settings → Privacy & Security → Camera`. Toggle JamWide OFF in the camera list.
4. Return focus to JamWide.
5. Within 5 seconds of the toggle-OFF, verify the preview frames stop arriving.
6. Within another ~3 seconds (total ~7 s after the revoke), verify the `CameraStatusDialog` appears with the **TCCDenied** cause (the copy mentions "macOS has denied"). **HIGH-6 closure check:** the dialog must NOT show `CameraInUse` — the `FrameStallWatchdog` re-queries auth on the 2000 ms frame gap and the re-query returns `Denied`, so the routed cause is `TCCDenied`, not the generic stall fallback.
7. Verify no crash.

**Expected:** HIGH-6 closure - the watchdog detects the revoke via the re-query and emits `TCCDenied`. Frame timing: poll = 1000 ms (`FRAME_STALL_POLL_MS`), gap threshold = 2000 ms (`FRAME_STALL_THRESHOLD_MS`), so worst-case dialog latency is ~3 s after the OS stops delivering frames.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 6 - Notarization stapler validate (PKG-04 / D-28)

**Why manual:** Depends on Apple's notary service round-trip. Cannot run in a hermetic CI build (requires keychain credentials).

**Setup:**
- Apple Developer ID Application certificate installed in the local keychain.
- A keychain profile for `notarytool` configured (per memory `project_apple_signing`: Team ID T3KK66Q67T, notarization via API Key).
- A clean `build-juce/` directory with `JAMWIDE_HARDENED_RUNTIME=ON`.

**Steps:**
1. Build a release JamWide bundle: `./scripts/build.sh --reconfigure JamWideJuce_Standalone` configured with `-DJAMWIDE_HARDENED_RUNTIME=ON -DJAMWIDE_CODESIGN_IDENTITY="Developer ID Application: <NAME> (T3KK66Q67T)"`.
2. Verify the bundle is codesigned: `codesign --verify --deep --strict --verbose=2 build-juce/.../JamWide.app`.
3. Run `./scripts/verify_camera_entitlement.sh build-juce/.../JamWide.app` — must exit 0.
4. Zip the bundle: `cd build-juce/.../Standalone && /usr/bin/ditto -c -k --keepParent JamWide.app JamWide.zip`.
5. Submit to notary: `xcrun notarytool submit JamWide.zip --keychain-profile <profile-name> --wait`. Must exit 0 with "status: Accepted".
6. Staple the ticket: `xcrun stapler staple JamWide.app`.
7. Validate the staple: `xcrun stapler validate JamWide.app` (must exit 0).
8. Run `./scripts/verify_camera_entitlement.sh JamWide.app --notarize` — must exit 0.

**Expected:** All 8 steps pass with zero errors.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 7 - Windows standalone happy path (CAM-01 / SC5)

**Why manual:** Requires Windows hardware + a connected webcam. SPARTA #82 does NOT apply on Windows (there's no per-bundle TCC equivalent).

**Setup:**
- Windows 10 / 11 x86_64 build of `JamWide.exe` (Standalone).
- A webcam connected and recognized by the OS (visible in Windows Camera app).
- Windows `Settings → Privacy & Security → Camera → Camera access` ON; "Let desktop apps access your camera" ON.

**Steps:**
1. Launch `JamWide.exe`.
2. Click the Camera button.
3. Verify the camera preview popout appears within 3 seconds with a live frame stream.

**Expected:** Windows allows desktop apps to access the camera by default (provided the user has not toggled the setting OFF). Preview live, no dialog.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 8 - Windows privacy block (CAM-02 Windows)

**Why manual:** Depends on Windows `Settings → Privacy & Security → Camera` toggle.

**Setup:**
- Same as Cell 7, but BEFORE launching JamWide:
- Open Windows `Settings → Privacy & Security → Camera`.
- Toggle "Let desktop apps access your camera" to OFF.

**Steps:**
1. Launch `JamWide.exe`.
2. Click the Camera button.
3. Verify the `CameraStatusDialog` appears with `WindowsPrivacyBlock` cause; copy mentions "Windows has blocked camera access for JamWide or its host application" (MEDIUM-6 softened).
4. Verify the dialog has THREE buttons: **Open Camera Privacy Settings** / **Recheck permission** / **OK**.
5. Click **Open Camera Privacy Settings** - verify Windows `Settings` opens to the Camera page.
6. Re-enable "Let desktop apps access your camera" in Settings.
7. Return focus to JamWide. Click **Recheck** (or re-trigger the Camera button if the dialog has been dismissed).
8. Verify the camera opens normally (preview popout appears).

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 9 - VDO.Ninja coexistence toast (D-27)

**Why manual:** Depends on both video stacks running simultaneously.

**Setup:**
- JamWide on macOS or Windows, with camera permission previously granted (Cell 1 or Cell 7).
- VDO.Ninja browser companion still in the build (parallel v1.3 beta).

**Steps:**
1. Click the **Video** button in ConnectionBar to start the existing VDO.Ninja video companion.
2. Wait for VDO.Ninja to reach the active state (the Video button highlights green).
3. Click the **Camera** button.
4. Verify the non-blocking soft warning toast appears with title "Multiple video stacks active" and the body mentioning "Bandwidth and CPU may be high — consider stopping one for better quality."
5. Click **OK** on the toast.
6. Verify the camera STILL proceeds to `Capturing` — the preview popout opens despite the toast (D-27: the toast does NOT block).
7. Click the Camera button to stop. Click again to start. Verify the toast does NOT re-appear — once-per-session guard via `coexistenceToastShown_.exchange(true)`.

**Expected:** Toast fires exactly once per editor lifetime; camera toggle proceeds in parallel; no spam on repeated toggles.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Cell 10 (NEW) - First-launch privacy modal sequence (HIGH-5 closure validation)

**Why manual:** Validates the HIGH-5 closure end-to-end. The privacy modal MUST fire on the REAL first-launch path (`NotDetermined → requestAccess → grant → modal → openDeviceAsync`), not only when status was already `Authorized` at click time.

**Setup (macOS):**
1. Reset macOS camera permission for JamWide: open Terminal and run `tccutil reset Camera com.jamwide.standalone` (substitute the correct bundle ID — check `Info.plist` `CFBundleIdentifier` to confirm).
2. Reset the `cameraPrivacyAck` flag in the persisted plugin state:
   - For the standalone app: delete `~/Library/Preferences/JamWide.plist` (or the JUCE-generated equivalent).
   - For a hosted plugin: delete the host's saved-session state for the JamWide track.
3. Confirm both resets took effect: macOS TCC entry for JamWide is gone (System Settings → Privacy & Security → Camera does NOT list JamWide), and the persisted `cameraPrivacyAck` is reset to `false` (default).

**Setup (Windows):** This cell is N/A on Windows (no per-app camera permission gate; Cell 8 covers Windows privacy block separately).

**Steps:**
1. Launch `JamWide.app`.
2. Click the Camera button. Note the time of click.
3. Verify the macOS TCC prompt appears (because status is `NotDetermined`). The prompt body should display the configured `NSCameraUsageDescription` text: "JamWide uses your webcam to share video with NINJAM peers."
4. Click **Allow** on the TCC prompt.
5. Verify the JamWide `NativeCameraPrivacyDialog` appears within 2 seconds of the Allow click. The dialog title is "Camera privacy notice"; body mentions "JamWide broadcasts your camera to the NINJAM server and peers in your room"; two buttons "I understand" and "Cancel".
6. Click **I understand**.
7. Verify the camera preview popout appears (camera proceeds to `Capturing` state).
8. CLOSE JamWide completely (quit the app).
9. Re-open JamWide.
10. Click the Camera button. Verify the camera opens DIRECTLY (no privacy modal, no TCC prompt) - the ack was persisted via state schema v4.

**Expected:** HIGH-5 closure - the privacy modal fires on the `NotDetermined → grant` path, NOT only on the "already `Authorized`" path. Second launch skips the modal (ack persisted to `cameraPrivacyAck=true` in the v4 ValueTree, restored by `setStateInformation` STEP 5).

**Failure modes to flag:**
- Modal does NOT appear after TCC grant → HIGH-5 regression; the editor's `handleCameraIdleClick` is checking the WRONG status state (probably the cached state at click time instead of the current `queryCameraAuthorization()` after the TCC completion handler).
- Modal appears on the second launch → privacyAck persistence broken in the v4 schema (check `JamWideJuceProcessor::getStateInformation` and `setStateInformation` STEP 5 for `cameraPrivacyAck` round-trip).
- TCC prompt appears on the second launch → an unrelated bug in JUCE permission storage or in `requestCameraAuthorization`.

- [ ] Pass / Fail / Date / Tester / Notes:

---

## Phase 19 Sign-Off

When all 10 cells are marked **Pass** with date + tester recorded, this checklist is complete and Phase 19 can transition to Phase 20.

Per `feedback_uat_scope_redflags`: NO cell may be deferred to Phase 24. All 10 cells MUST be exercised before Phase 19 is declared complete.

Other DAW cells (Live, Bitwig on macOS; REAPER on Windows) belong to Phase 24's per-DAW matrix per CONTEXT "Deferred Ideas" - explicitly out of Phase 19.

| Cell | CAM Requirement | Status | Date | Tester |
|------|-----------------|--------|------|--------|
| 1    | CAM-01 / SC1    |        |      |        |
| 2    | CAM-01 / SC2    |        |      |        |
| 3    | CAM-02 / SC3    |        |      |        |
| 4    | CAM-01 arm64    |        |      |        |
| 5    | CAM-02 / SC4    |        |      |        |
| 6    | PKG-04 / D-28   |        |      |        |
| 7    | CAM-01 / SC5    |        |      |        |
| 8    | CAM-02 Windows  |        |      |        |
| 9    | D-27            |        |      |        |
| 10   | HIGH-5          |        |      |        |
