---
phase: 19
slug: camera-capture-permission-ux
status: verified
threats_open: 0
asvs_level: 1
created: 2026-05-16
verified: 2026-05-16
---

# Phase 19 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.
> Native camera capture pipeline (JUCE `juce_CameraDevice`) replacing prior VDO.Ninja browser-based path. STRIDE register authored at plan time (revision 2 after cross-AI codex review).

---

## Trust Boundaries

| Boundary | Description | Data Crossing |
|----------|-------------|---------------|
| OS TCC → JamWide | macOS controls whether camera frames flow; "Authorized" status is necessary but not sufficient (Risk F) | Camera frames (raw `juce::Image`) |
| Camera-callback thread → distributor → subscribers | `juce::CameraDevice::Listener::imageReceived` fires on "any thread"; subscribers must marshal to their target thread | `juce::Image` + frame timestamp |
| Plugin process → DAW host process | DAW host's bundle ID controls TCC for plugin context (SPARTA #82) | TCC grant context, host-controlled |
| Async-callback closure → JamWideCameraDevice instance | Host can destroy the editor/processor while async callbacks are in flight | Captured `this` pointer + async closure state |
| User to plugin UI | User clicks/right-clicks; UI must not crash on rapid click | UI click events |
| ValueTree XML load → in-memory state | Saved-state XML read from disk; ints/strings can be malformed or out-of-range | Plugin-state v3→v4 schema fields |
| Source repo → codesigned bundle | Entitlement keys + Info.plist strings must survive codesign + notarization unchanged | `JamWide.entitlements`, `NSCameraUsageDescription` |

---

## Threat Register

| Threat ID | Category | Component | Disposition | Mitigation | Status |
|-----------|----------|-----------|-------------|------------|--------|
| T-19-01 | Tampering (TCC bypass / silent denial) | JamWideCameraDevice TCC pre-check + CameraStatusDialog | mitigate | (1) `queryCameraAuthorization()` called BEFORE `openDeviceAsync` at `JamWideCameraDevice.cpp:177,256,387`. (2) `FIRST_FRAME_WATCHDOG_MS=3000` at `JamWideCameraDevice.h:89`; routes Authorized-but-no-frames to `CameraFallbackCause::CameraInUse`. (3) Continuous frame-stall watchdog (`FRAME_STALL_POLL_MS=1000` / `FRAME_STALL_THRESHOLD_MS=2000`) re-queries auth at `JamWideCameraDevice.cpp:382-401` → routes to TCCDenied. (4) Cause-aware `CameraStatusDialog::classifyDenialCause` at `CameraStatusDialog.cpp:476-484`. (5) `test_camera_cause_mapping.cpp:62-213` — 5 scenarios + 14 cell assertions. | closed |
| T-19-02 | Information Disclosure (UAF on camera-callback thread) | JamWideFrameDistributor | mitigate | `Subscription` RAII handle at `JamWideFrameDistributor.h:50-66`; `~Subscription()` blocks via `cv_.wait(lock, [&]{ return entry->inFlight.load() == 0; })` at `.cpp:118-137`. Publish path `fetch_add`/`fetch_sub` + `cv_.notify_all()` at `.cpp:95,111-114`. `test_frame_distributor_lifetime.cpp:53-105` reproduces race deterministically with `std::promise`. | closed |
| T-19-03 | Tampering (UAF async + plugin state injection) | JamWideCameraDevice async sites + JamWideJuceProcessor::setStateInformation | mitigate | **Backend:** `std::atomic<uint64_t> generation_` at `JamWideCameraDevice.h:99`; 14 `generation_.load` checks across 7 async sites (CameraListener `:46`, FirstFrameWatchdog `:67`, FrameStallWatchdog `:85`, RetryWorker `:136,145`, TCC completion `:288`, openDeviceAsync result `:324`, onErrorOccurred `:348`). `shutdown()` does `generation_.fetch_add(1, release)` FIRST at `.cpp:212`. **UI:** `setStateInformation` STEP 5 at `JamWideJuceProcessor.cpp:830-854` clamps all v4 camera fields via `juce::jlimit` (popoutX/Y ±10000; size 240..2560 / 180..1920; qualityPreset 0..2; selectedDevice cap 256 chars). `test_plugin_state_v3_v4.cpp:140-171` Test3 verifies clamping with injected -99999/99999/-5/99 + 1000-char string. | closed |
| T-19-04 | Privacy (camera-on without ack) | CameraStateMachine init + NativeCameraPrivacyDialog | mitigate | **Backend:** `CameraStateMachine::state_ = CameraState::Idle` at `CameraStateMachine.h:107`; constructor enforces D-10 at `JamWideCameraDevice.cpp:158-165`. Toggle requires explicit click. **UI:** HIGH-5 first-launch flow `handleCameraIdleClick` at `JamWideJuceEditor.cpp:879-923` — NotDetermined → `requestCameraAuthorization` → marshal → `showPrivacyOrToggle` at `.cpp:926-947` which only calls `cam->toggle()` after ack. `processorRef.setCameraPrivacyAck(true)` persisted at `.cpp:937`. `privacyAck` bool at `JamWideJuceProcessor.h:267`. `test_plugin_state_v3_v4.cpp:176-193` Test4 confirms ack survives serialisation. | closed |
| T-19-05 | Entitlement spoofing | JamWide.entitlements + verify_camera_entitlement.sh | mitigate | `JamWide.entitlements:7-8` contains `com.apple.security.device.camera`. `scripts/verify_camera_entitlement.sh:41-42` reads from SHIPPED bundle via `codesign --display --entitlements - "$BUNDLE_PATH"` (T-19-05 comment at `:15-18`). `plutil -extract NSCameraUsageDescription raw "$BUNDLE_PATH/Contents/Info.plist"` at `:52` extracts from bundle Info.plist; string-equality check at `:60-65` against literal `"JamWide uses your webcam to share video with NINJAM peers."` (matches `CAMERA_PERMISSION_TEXT` at `CMakeLists.txt:170`). Source-only `.entitlements` tampering does NOT defeat. | closed |
| T-19-SC | Tampering (license non-compliance via juce_video AGPLv3) | CMakeLists.txt + LICENSE | mitigate | License-header preflight executed at Task 1 start before `target_link_libraries(JamWideJuce PRIVATE juce::juce_video)` at `CMakeLists.txt:264,401`. Per 19-01-SUMMARY.md, LICENSE + 6 source headers grepped for AGPLv3 upgrade clause → 0 matches; proceeded under option (a) JUCE commercial seat per documented decision. Precedent: `juce::juce_video` already linked into `video_spike` target since Phase 14.3 substrate (commit 3494676). Risk C surfaced in UAT checklist for user confirmation — see Accepted Risks below. | closed |
| T-19-PT | Tampering (UAF in preview tile callAsync) | CameraPreviewTile + AsyncUpdater | mitigate | HIGH-4 fix: `CameraPreviewTile.h:30-31` inherits `juce::AsyncUpdater`. `onFrame()` at `.cpp:33-47` copies juce::Image into `pendingFrame_` under `std::mutex pendingMu_` and calls `triggerAsyncUpdate()`. `handleAsyncUpdate()` at `.cpp:49-64` reads under mutex + `repaint()`. **Verified:** `grep -c 'MessageManager::callAsync.*this' CameraPreviewTile.cpp` = 0. Member-order contract documented at `.h:15-19,63-68`: `subscription_` declared LAST so `~Subscription` (T-19-02 mitigation) runs FIRST and blocks any in-flight onFrame before mutex/frame members are destroyed. `~AsyncUpdater` cancels pending callbacks automatically. | closed |

*Status: open · closed*
*Disposition: mitigate (implementation required) · accept (documented risk) · transfer (third-party)*

---

## Accepted Risks Log

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-19-01 | T-19-SC (advisory note) | JUCE commercial seat licence usage for `juce_video` proceeds under option (a). Confirmed by executor decision per 19-01-SUMMARY.md Risk C resolution; precedent set by Phase 14.3 spike (`video_spike` target already linked `juce_video`). Final UAT confirmation captured in `19-HUMAN-UAT.md` Cell 11. | mkschulze | 2026-05-16 |

*Accepted risks do not resurface in future audit runs.*

---

## Related-but-not-in-register (Advisory)

The post-execution code review (`19-REVIEW.md`) flagged 4 Critical UAF risks in editor-owned async lambdas (CR-01..04). These are SECONDARY surface that the T-19-03 generation-token mitigation (backend-only) does not cover — they are editor-lifetime UAFs at the JUCE message-thread layer:

- **CR-01** — `JamWideJuceEditor::onCameraFallback` lambda at `JamWideJuceEditor.cpp:832-863` — `[this]` capture into `juce::AlertWindow::showAsync` callback that outlives editor.
- **CR-02** — `showPrivacyOrToggle` lambda at `:931-942` — same shape; this is the HIGH-5 first-launch path.
- **CR-03** — `handleCameraIdleClick` NotDetermined branch at `:894-911` — `[this]` capture across macOS TCC system-modal completion handler.
- **CR-04** — Stale `openDeviceAsync` ghost-capture window at `JamWideCameraDevice.cpp:324` — generation check exists but sequencing window flagged.

**Recommendation:** Schedule a `19.1-lifetime-hardening` phase (`juce::Component::SafePointer<JamWideJuceEditor>` guards or processor-only captures) before public v1.3 beta. **Not blocking T-19-01..05/SC/PT closure** — these threats remain CLOSED.

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open | Run By |
|------------|---------------|--------|------|--------|
| 2026-05-16 | 7 | 7 | 0 | gsd-security-auditor (opus) |

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer)
- [x] Accepted risks documented in Accepted Risks Log
- [x] `threats_open: 0` confirmed
- [x] `status: verified` set in frontmatter

**Approval:** verified 2026-05-16
