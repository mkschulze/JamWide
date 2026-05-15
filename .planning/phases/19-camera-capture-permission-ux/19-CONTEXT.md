# Phase 19: Camera Capture & Permission UX - Context

**Gathered:** 2026-05-16
**Status:** Ready for planning

<domain>
## Phase Boundary

First-touch UX for camera capture in JamWide standalone and DAW-hosted plugin (VST3/AU/CLAP). This phase covers:

1. Opening a webcam via JUCE CameraDevice on macOS (arm64 + x86_64) and Windows x86_64
2. Rendering a local camera preview tile inside a JUCE-native popout window
3. Declaring the macOS camera entitlement (`com.apple.security.device.camera`) and the matching `NSCameraUsageDescription` string in Info.plist
4. Detecting and gracefully handling the "camera unavailable" cases (TCC denial, host lacks entitlement, camera in use by another app, no camera hardware, Windows privacy block) with a single DAW-agnostic fallback dialog
5. Persisting popout window state and capture-quality preference across plugin/standalone sessions

**Maps to:** Requirements CAM-01, CAM-02, CAM-03 + the entitlements portion of PKG-04.

**Out of scope:** H.264 encoding (Phase 20), wire transport (Phase 20), GUID-pairing receive pipeline (Phase 21), per-remote-user video grid / popouts (Phase 22), platform packaging + codesign / signtool (Phase 23), per-DAW UAT matrix (Phase 24). Phase 19 ships zero audio-path code — the audio thread remains untouched until Phase 20 introduces the interval-tick `RawDataSendBegin` caller.

</domain>

<decisions>
## Implementation Decisions

### Capture API (Area 1)

- **D-01: Use `juce::CameraDevice` (juce_video module).** Cross-platform single API; spike-validated 320×240@10fps on macOS x86_64. AGPLv3 licence (compatible with JamWide's GPLv2+ via the "or any later version" clause). ~50–100 LOC of integration vs. ~800 LOC for direct AVFoundation + Media Foundation wrappers.
- **D-02: Frame distribution via a JamWideFrameDistributor with subscribers.** A single `CameraDevice::Listener::imageReceived(const Image&)` callback forwards each frame to the distributor. Subscribers register and receive frames in their preferred form. UI subscriber forwards to the preview tile via `juce::MessageManager::callAsync`. Phase 20's encoder subscribes via an SPSC ring. Phase 22's per-remote popouts will subscribe similarly. Extensible architecture; sets the threading contract for the rest of v1.3.
- **D-03: macOS TCC detection via `AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo` pre-check.** Objective-C++ bridge (`.mm` file) called BEFORE `juce::CameraDevice::openDeviceAsync`. Returns `denied` / `restricted` / `authorized` / `notDetermined` synchronously. `requestAccessForMediaType:` is used to trigger the OS prompt when status is `notDetermined`. This closes the spike Risk #2 ambiguity where `openDevice` returns a non-null pointer even when TCC denies frames.
- **D-04: Frame-format conversion lives in each subscriber.** The distributor publishes `juce::Image` (BGRA) verbatim. The UI subscriber renders directly. The Phase 20 encoder subscriber owns its BGRA→YUV420P conversion (via libswscale `sws_scale`) inside its own thread. Clean separation — encoder owns its format; UI keeps the JUCE-native image; no wasted conversion.

### Preview Placement (Area 2)

- **D-05: Floating `juce::DocumentWindow` popout only.** No in-plugin tile in Phase 19. Phase 22 builds the full grid of remote-user tiles separately. Minimal disruption to the existing mixer + ConnectionBar layout (consistent with the user's "existing UI stays untouched" preference for the parallel beta).
- **D-06: New "Camera" button in `ConnectionBar` opens the popout.** Sits next to the existing "Video" button (which still launches VDO.Ninja during the parallel beta — Item H teardown is deferred to post-beta). Beta users can run both stacks side-by-side and compare. Phase 22 consolidates the two buttons when VDO.Ninja is torn out.
- **D-07: Popout window properties — resizable, 4:3 aspect-locked, position persists across sessions.** Default size 320×240 matching spike capture. Aspect ratio enforced so users can't stretch pixels. Window position and size saved to APVTS (per D-21).
- **D-08: Popout chrome matches the JamWide VB-style dark LookAndFeel.** Uses the existing custom `JamWideLookAndFeel`. Custom dark title bar + VB-style accents. ~30 LOC override of `juce::DocumentWindow`'s title bar. Phase 22 popouts reuse the same style.

### Lifecycle (Area 3)

- **D-09: Orthogonal camera state and popout visibility.** The Camera button in ConnectionBar toggles capture ON/OFF. The popout window X just hides the preview — capture continues silently. This anticipates Phase 20's broadcast state as a third independent state. ~10 LOC up front avoids a Phase 20 refactor.
- **D-10: Camera always starts OFF on plugin/standalone launch.** Privacy-default. User must click Camera button after each launch. Matches Zoom / FaceTime / OBS convention. Eliminates the "webcam light on in a DAW session weeks later" surprise mode.
- **D-11: Camera state is fully independent of NINJAM Connect/Disconnect.** User can preview the camera without being connected (useful for pre-session camera check). Camera stays in whatever state user left it across Connect/Disconnect events. Phase 20 will add broadcast-state on top — broadcast naturally ties to "camera ON + connected".
- **D-12: After permission denial, the Camera button label changes to "Recheck permission" and stays clickable.** Click re-queries `authorizationStatusForMediaType`. If authorized, opens camera; otherwise re-shows the fallback dialog. Clear next-step affordance for users who grant permission in System Settings between attempts.

### Permission-Denial Fallback (Area 4) — DAW-Agnostic

- **D-13: Single non-blocking `juce::AlertWindow` dialog handles all "camera unavailable" causes.** Causes are detected by the TCC pre-check (D-03) plus error returns from `openDeviceAsync` and runtime errors. The dialog's copy is cause-aware — TCC denied / host lacks entitlement (REAPER, Live, Bitwig, etc. on macOS) / camera in use by another app / no camera hardware / Windows privacy block. **Important:** the fallback shape is DAW-agnostic — REAPER is one of many hosts that lack the camera entitlement on macOS; the same dialog handles all of them, and the same dialog handles Windows-specific causes via platform-conditional copy. Future hosts that add the entitlement automatically stop triggering the dialog with no JamWide code change.
- **D-14: Dialog suppressed after first show until detected cause changes.** First failure shows the dialog. Subsequent clicks with the same cause just focus the Camera button. If the detected cause changes (e.g., denied → in-use), the dialog re-appears with the new copy. Balances education with non-annoyance.
- **D-15: Platform-aware deep-link.** macOS path includes an "Open System Settings" button that invokes the privacy URL via `NSWorkspace`. Windows path includes an "Open Camera Privacy Settings" button that invokes `juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser()`. One dialog component, platform-conditional content.
- **D-16: Copy-only standalone-as-fallback suggestion.** When the detected cause is "host lacks entitlement", the dialog adds a line: *"Tip: JamWide standalone has direct camera access."* No "Launch Standalone" button — cross-platform process launch is brittle and the new instance can't inherit the current NINJAM session anyway. Educational only.

### Camera Device Selection (Area 5)

- **D-17: Auto-pick `deviceIndex=0` (system default camera), name displayed in popout titlebar.** No multi-camera selection UI in Phase 19. The popout titlebar shows the device name (e.g., "Camera: FaceTime HD") so the user can verify which device is in use. If beta testers ask for multi-camera switching, add a dropdown in a quick task or in Phase 22.

### Capture Parameters (Area 6)

- **D-18: Three capture presets — Low (320×240@10fps) / Medium (640×480@15fps) / High (1280×720@30fps). Default = Medium.** Spike-validated baseline is 320×240@10fps; Medium provides better preview quality for beta testers while staying well within encoder budget (revisited in Phase 20). Presets affect both the capture format and Phase 20's encoder default (Phase 20 may revisit bitrate trade-offs per preset).
- **D-19: Preset selection via Camera button right-click → "Quality ▶ Low / Medium / High" submenu (`juce::PopupMenu`).** Minimal UI footprint, native menu pattern, hidden from casual users but discoverable. Selected preset persists across sessions (per D-21).

### Frame Error Handling (Area 7)

- **D-20: On mid-session camera loss, pause + exponential-backoff retry (1s/2s/4s/8s/16s, give up at 30s).** Triggered by camera-unplug, OS sleep/wake, another app stealing the camera, or runtime errors from the JUCE camera callback. Popout shows "Camera disconnected — retrying..." state during retries. On first successful frame, resume normally. After 30 s of failed retries, transition to the Area 4 fallback dialog with the appropriate detected cause. The retry thread is a separate worker (NOT the camera callback thread, NOT the audio thread, NOT the message thread). Matches Phase 17's planned network-resilience exponential-backoff pattern.

### Test Strategy (Area 8)

- **D-21 (test strategy): Layered — unit tests for logic, manual UAT for permission flows.** Unit tests cover: `JamWideFrameDistributor` fan-out (inject synthetic `juce::Image` frames via Listener), the camera state machine (idle / opening / capturing / paused / failed / unavailable), the retry-backoff timing, and the cause-to-dialog-copy mapping. Manual UAT covers: actual TCC denial via System Settings, REAPER + Live + Bitwig hosting on macOS (camera unavailable expected), Logic Pro on macOS (camera authorized expected), Windows standalone, Windows REAPER, the privacy-modal first-launch flow, popout window resize / position persistence. CI runs only the unit tests; manual UAT is human-driven in Phase 24's per-DAW matrix.

### Privacy Notice (Area 9)

- **D-22: New first-use modal for native-camera broadcast acknowledgment.** Triggered the first time the user clicks Camera button AFTER granting OS permission. Modal text: *"JamWide broadcasts your camera to the NINJAM server and peers in your room. Peers can save or redistribute their view. There's no separate IP exposure beyond what audio already does."* Includes a "Got it / Don't show again" checkbox stored in the new state subtree (per D-25). Distinct from the existing `VideoPrivacyDialog` (which covers VDO.Ninja IP-exposure and stays operational during the parallel beta).

### Logging (Area 10)

- **D-23: Camera events logged via `juce::Logger::writeToLog`.** Camera open success/failure, TCC denial detection, retry attempts, frame-distributor errors. JUCE's standard plugin log — same pattern as OSC and MIDI components. Phase 15.1 removed `wdl::writeLog` from the audio path; this honours that boundary by keeping camera events in the JUCE plugin-side logger. Camera callbacks are on JUCE's message thread (not audio thread), so logging is RT-safe in principle.

### Plugin State Persistence (Area 11)

- **D-24: Bump plugin state schema from version 3 to version 4.** Existing v3 was set by Phase 14 (MIDI Learn mappings); v4 adds the camera subtree. `loadState` handles missing v3 → v4 fields gracefully (defaults applied: popout at OS-chosen position, quality = Medium, privacy not yet acknowledged, no selected device).
- **D-25: New `<camera>` ValueTree subtree with attributes: `popoutX`, `popoutY`, `popoutWidth`, `popoutHeight`, `qualityPreset` (low/medium/high; default medium), `privacyAck` (bool; default false), `selectedDevice` (string; default empty = auto-pick).** All scalar / serializable. The `selectedDevice` field is a forward investment for multi-camera selection — Phase 19 always auto-picks `deviceIndex=0`, but the schema is ready for when a dropdown is added later.

### Documentation (Area 12)

- **D-26: Phase 19 ships tooltip on Camera button + CHANGELOG.md entry.** Tooltip text: "Toggle camera preview (v1.3 beta)". CHANGELOG.md entry describes the new native camera capture stack alongside Phase 14.3's substrate. Full `docs/CAMERA.md` user guide + README updates land in Phase 24 alongside `docs/SERVER.md` and the beta release notes. Keeps Phase 19 focused on capture; user-facing docs ship in the user-facing phase.

### VDO.Ninja Coexistence (Area 13)

- **D-27: Soft warning toast when both native + VDO.Ninja active, no hard block.** When user clicks Camera button while VDO.Ninja is already running, show a non-blocking `juce::AlertWindow` with `alertWindowType = NoIcon`: *"VDO.Ninja video is also active. Bandwidth and CPU may be high — consider stopping one for better quality."* User can ignore and proceed. The two stacks can co-exist precisely so beta testers can A/B compare them.

### macOS Notarization (Area 14)

- **D-28: Phase 19 includes a notarization verification task.** Phase 14.3 substrate already lined up the entitlement plumbing (PKG-04 entitlements portion was mapped to Phase 19 by the roadmapper). Phase 19 (a) adds `com.apple.security.device.camera` to `JamWide.entitlements`, (b) adds `NSCameraUsageDescription` string to the bundle's Info.plist with copy *"JamWide uses your webcam to share video with NINJAM peers."*, (c) builds + codesigns + notarizes a test bundle via the existing API Key workflow (project memory: Team ID T3KK66Q67T), (d) verifies the bundle passes `codesign --verify --deep --strict` and `xcrun stapler validate`. If anything breaks, surface to Phase 23 (Build & Codesign phase) for fixing. The microphone entitlement is the analogous precedent — adding the camera one should be a no-op for the notarization workflow.

### Audio-Thread Coordination Contract (Area 15)

- **D-29: Phase 20 owns ALL audio-thread integration for camera.** Phase 19 ships ZERO audio-path code. Camera state lives entirely in JamWide's message thread (JamWideJuceProcessor + the new camera components). Phase 20 introduces the audio-thread-side flag (likely `std::atomic<bool>` for camera_active + the per-interval `RawDataSendBegin` caller in `on_new_interval`). Clean phase boundary; matches Phase 15.1's "audio path stays simple" principle.

### Onboarding (Area 16)

- **D-30: No onboarding tour — tooltip + CHANGELOG.md entry is sufficient.** JamWide has no onboarding tours today (consistent with the rest of the UI). Adding one for a single beta feature would set an unwelcome precedent. v1.3 beta testers are technical and will read release notes. The button tooltip provides in-context help on hover.

### Claude's Discretion

- Choice of sync vs async `juce::CameraDevice::openDeviceAsync` (planner picks; async is the modern API)
- Exact pixel sizes for the popout default vs minimum vs maximum bounds (planner picks; min ~240×180 makes sense)
- Specific copy strings for the fallback dialog beyond the cause-aware skeleton (planner picks; English-only for v1.3 beta — i18n is a v2+ concern)
- The retry-thread implementation (separate `juce::Thread` vs `juce::TimedCallback` chain — planner picks based on what fits the codebase best)
- File layout for camera code: likely a new `juce/video/native/` subdirectory with `JamWideCameraDevice.h/.cpp`, `JamWideFrameDistributor.h/.cpp`, `CameraPreviewWindow.h/.cpp`, `CameraAuthorization_mac.mm`, `CameraAuthorization_windows.cpp`, `CameraStatusDialog.h/.cpp` (planner refines)
- Whether to expose a `GetCameraPeakFrameRate()` accessor for debugging during beta UAT (planner picks)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Spike research (authoritative for v1.3)

- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-CONTEXT.md` — Locked decisions from the v1.3 spike (Section 3 "Locked Decisions to Honor"). ffmpeg+JUCE CameraDevice stack, standalone+plugin parity, macOS Hardened Runtime entitlement, native-rendering grid+popouts, no browser companion in v1.3.
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-RESEARCH.md` §6 — Integration points and file:line citations (`juce/JamWideJuceProcessor.h:119`, `juce/ui/ConnectionBar.cpp:206-217,512-528,644-650`, `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:78-81,202-206`).
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-spike-results.md` — Measured spike evidence: 320×240@10fps, ~98 kbps encoded, 4% CPU, 5.2 MB ffmpeg per arch. Spike Risk #2 (CameraDevice::openDevice returns non-null when TCC denies).
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` — Item C (JUCE CameraDevice integration), Item G partial (entitlements + plugin plumbing). Q1 (JUCE seat licence) and Q3 (Net_Connection::Send thread safety) — Q1 resolved in this phase by selecting `juce::CameraDevice` under AGPLv3-via-GPLv2+-upgrade.

### NinjamZap and JamTaba references (relevant for downstream phases; informational for 19)

- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:200-236` — Public API surface for the RawData send + callback path (Phase 14.3 already ported this to JamWide; reference here for completeness when the planner connects Phase 19's camera output to Phase 20's encoder).
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:99` — `QThreadPool(1)` worker pattern for the encoder (Phase 20 reference; included here so the Phase 19 planner can mentally place where the encoder will live downstream).

### JamWide codebase

- `juce/ui/ConnectionBar.cpp:207` — Existing "Video" button (currently launches VDO.Ninja); the new "Camera" button lives next to it.
- `juce/JamWideJuceProcessor.h:119,141,177-189,200-204` — Plugin-side owner positions; the new camera components live here.
- `juce/JamWideJuceProcessor.cpp:61,69-72` — Constructor init + destructor cleanup hooks for the new camera owner.
- `juce/video/VideoPrivacyDialog.h` — Existing VDO.Ninja privacy dialog. The new native-camera first-use modal is distinct (per D-22), but the dialog component pattern can be reused.
- `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:78-81,202-206` — JUCE camera API. Note line 202-206 documents that `Listener::imageReceived` is called on "any thread" — informs D-02's frame-distributor design.
- `libs/juce/modules/juce_video/native/juce_CameraDevice_mac.mm` — macOS backend (AVFoundation).
- `libs/juce/modules/juce_video/native/juce_CameraDevice_windows.h` — Windows backend (Media Foundation / DirectShow).
- `JamWide.entitlements:5-10` — Current entitlements; add `com.apple.security.device.camera` (per D-28 + PKG-04 entitlements portion).
- `CMakeLists.txt:145-162` — `juce_add_plugin` block; needs `CAMERA_PERMISSION_ENABLED TRUE` per JUCE convention.

### Phase 14.3 substrate (dependency baseline)

- `.planning/phases/14.3-native-video-foundation/14.3-SPEC.md` — Substrate spec. Phase 19's camera output integrates with the RawDataSendBegin/Write API in Phase 20; the substrate is already complete and Phase 19 doesn't touch it.

### Memory references

- Memory `project_apple_signing` — Team ID T3KK66Q67T, notarization via API Key. Phase 19's D-28 verification leans on this.
- Memory `feedback_ui_preferences` — VB-Audio Voicemeeter Banana style, dark theme, full custom LookAndFeel. D-08 honours this for the popout window chrome.
- Memory `feedback_uat_scope_redflags` — Never let an executor's "verify only X, skip Y" UAT pass when Y is a user-visible happy-path. Phase 19's manual UAT covers CAM-01/02/03 explicitly; do not allow the planner to defer any of them.
- Memory `project_jamtaba_video_port` — Project-level decision log for the v1.3 effort. SUPERSEDED 2026-05-15 by the spike + NinjamZap pivot.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- **JUCE custom LookAndFeel** (`juce/JamWideJuceEditor.cpp` and `juce/ui/`) — VB-style dark theme; reused for the popout window chrome (D-08).
- **`VideoPrivacyDialog` component pattern** (`juce/video/VideoPrivacyDialog.h/.cpp`) — Existing first-use modal pattern; the new native-camera privacy modal (D-22) follows the same shape with different copy.
- **`ConnectionBar` button conventions** (`juce/ui/ConnectionBar.cpp`) — Existing buttons (Connect, Browse, Sync, Route, Fit, Video) demonstrate the `onClick` lambda + button state machine pattern; the new Camera button follows it.
- **JUCE plugin state ValueTree** — `JamWideJuceProcessor` uses a ValueTree alongside APVTS for non-parameter state (e.g., `lastServer`, `oscEnabled`); the new `<camera>` subtree (D-25) plugs in naturally.
- **NSWorkspace bridge for `juce::URL::launchInDefaultBrowser`** — Same mechanism used for the `ms-settings:` deep-link on Windows (D-15).

### Established Patterns

- **JUCE message thread is the canonical UI thread.** All camera-related UI updates use `juce::MessageManager::callAsync` to marshal onto it (D-02 subscriber pattern).
- **Audio thread is sacred.** Per Phase 15.1's hardening, the audio path is free of mutexes, heap deallocations, and logging. Phase 19 honours this rigorously by keeping the camera entirely off the audio thread (D-29).
- **State version bumps for schema additions.** Phase 14 bumped state to v3 for MIDI; Phase 19 bumps to v4 for camera (D-24). `loadState` handles missing fields gracefully via defaults.
- **Per-component status dot indicators.** OSC has `OscStatusDot`, MIDI has `MidiStatusDot`. A `CameraStatusDot` is a natural future-add (Phase 22 timeframe), but Phase 19 keeps the Camera button itself as the status surface (D-12 "Recheck permission" label change).

### Integration Points

- **`JamWideJuceProcessor::JamWideJuceProcessor()`** (`juce/JamWideJuceProcessor.cpp:61`) — Construct the new `JamWideCameraDevice` + `JamWideFrameDistributor` + (initially-closed) `CameraPreviewWindow` owners here.
- **`JamWideJuceProcessor::~JamWideJuceProcessor()`** (`juce/JamWideJuceProcessor.cpp:69-72`) — Tear down camera + distributor + window in the destructor. `releaseResources()` is the right hook for releasing camera hardware specifically (DAW signals "I'm done with you").
- **`ConnectionBar::ConnectionBar()`** (`juce/ui/ConnectionBar.cpp:206-217`) — Add the new Camera button next to the existing Video button. `onClick` lambda toggles camera state via a processor method.
- **`JamWideJuceEditor`** — Owns `ConnectionBar`; passes the camera-toggle lambda through.
- **Plugin state save/restore** — Add `<camera>` ValueTree write + read paths to the existing state save/load methods. Version bump 3 → 4.

</code_context>

<specifics>
## Specific Ideas

- **Camera button label state machine:** "Camera" (idle/off) → "Camera (on)" (capturing) → "Recheck permission" (denial detected) → "Camera (retrying)" (during exponential backoff). Tooltip on hover always shows the current state in English.
- **Popout titlebar format:** `JamWide — Camera: <device name>` (e.g., "JamWide — Camera: FaceTime HD"). Updates if user later switches cameras (multi-camera in a future quick task / Phase 22).
- **Fallback dialog copy skeleton (cause-aware):**
  - TCC denied (macOS): "macOS has denied camera access to JamWide. Grant permission in System Settings → Privacy & Security → Camera, then click Recheck."
  - Host lacks entitlement (macOS): "{HostName} doesn't request camera access for itself, so JamWide can't reach the camera while hosted in it. Tip: JamWide standalone has direct camera access."
  - Camera in use: "Another app is using the camera. Close it and click Recheck."
  - No camera hardware: "No camera detected. Connect a webcam and click Recheck."
  - Windows privacy block: "Windows has blocked camera access. Enable camera access in Settings → Privacy → Camera, then click Recheck."
- **Privacy modal copy (D-22):** "JamWide broadcasts your camera to the NINJAM server and peers in your room. Peers can save or redistribute their view. There's no separate IP exposure beyond what audio already does." With "Got it / Don't show again" checkbox.
- **NSCameraUsageDescription string (D-28):** "JamWide uses your webcam to share video with NINJAM peers."

</specifics>

<deferred>
## Deferred Ideas

These came up during discussion but belong elsewhere — captured here so they're not lost.

- **Multi-camera dropdown UI** — D-17 ships auto-pick + name-in-titlebar. A right-click menu or dropdown for switching cameras was discussed but deferred until beta testers request it (then either a quick task or Phase 22).
- **VST3/AU/CLAP host-specific camera-entitlement behavior** — Beyond REAPER on macOS (SPARTA #82), we know Live and Bitwig also lack the entitlement. Logic Pro carries it. Full DAW matrix verification is Phase 24's BETA-02/03 work.
- **Onboarding tour / coachmark** — Rejected for Phase 19 (D-30). If beta testers report "I didn't notice the new Camera button", revisit as a quick task between Phase 19 and Phase 22.
- **Camera frame rate / bandwidth telemetry** — Anonymous beta usage metrics (open success rate, permission denial rate, retry rate) would inform Phase 22/24 polish. Out of scope for Phase 19; revisit at Phase 24.
- **Privacy notice translations** — D-22 ships English copy only. i18n is a v2+ concern; v1.3 beta is English-only.
- **`docs/CAMERA.md` user guide** — D-26 defers to Phase 24 alongside `docs/SERVER.md`.
- **Audio-thread `camera_active` flag** — D-29 explicitly defers all audio-thread coordination to Phase 20.
- **Capture parameter presets in Phase 20 encoder context** — D-18 ships three presets in Phase 19; Phase 20 may revisit the bitrate trade-offs and add a fourth "Auto" preset that adapts based on network conditions. Deferred to Phase 20 explicitly.
- **Cross-platform launch-Standalone button (Area 4)** — D-16 ships copy-only suggestion. A working button is brittle to implement and adds friction without commensurate value.
- **Linux camera capture (Item K)** — Out of v1.3 entirely per the milestone scope. Phase 19 explicitly does NOT touch Linux (no `juce_CameraDevice_linux.h` exists in JUCE 7).

</deferred>

---

*Phase: 19-camera-capture-permission-ux*
*Context gathered: 2026-05-16*
