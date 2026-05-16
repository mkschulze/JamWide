---
phase: 19
plan: 02
type: execute
wave: 2
depends_on:
  - 19-01
files_modified:
  - juce/ui/ConnectionBar.h
  - juce/ui/ConnectionBar.cpp
  - juce/JamWideJuceEditor.cpp
  - juce/JamWideJuceEditor.h
  - juce/JamWideJuceProcessor.h
  - juce/JamWideJuceProcessor.cpp
  - juce/video/native/CameraPreviewWindow.h
  - juce/video/native/CameraPreviewWindow.cpp
  - juce/video/native/CameraPreviewTile.h
  - juce/video/native/CameraPreviewTile.cpp
  - juce/video/native/NativeCameraPrivacyDialog.h
  - juce/video/native/NativeCameraPrivacyDialog.cpp
  - tests/test_plugin_state_v3_v4.cpp
autonomous: true
requirements:
  - CAM-03
threat_refs:
  - T-19-03
  - T-19-04

must_haves:
  truths:
    - "ConnectionBar shows a Camera TextButton immediately to the left of the existing Video button"
    - "Camera button is NEVER disabled by Connect state (D-11 independence)"
    - "Camera button click action follows the MEDIUM-1 decision tree: state==Capturing+popoutVisible → Idle; state==Capturing+popoutHidden → SHOW popout (capture stays on); state==Idle → start capture; state==Unavailable → recheckPermission"
    - "Right-click on Camera button shows quality preset PopupMenu + a 'Stop Camera' item that fully stops capture regardless of popout state"
    - "Camera button label changes to 'Recheck permission' when CameraState is Unavailable"
    - "CameraPreviewTile uses juce::AsyncUpdater for repaint (HIGH-4 mitigation), NOT MessageManager::callAsync(this, ...)"
    - "CameraPreviewTile coalesces frame bursts: handleAsyncUpdate runs once on the message thread even if onFrame is called 100 times in quick succession"
    - "On first launch with state==NotDetermined: user click → requestAccess → grant → AuthResult callback observes !privacyAck → shows NativeCameraPrivacyDialog → on Acknowledge: persists privacyAck=true + proceeds to openDeviceAsync (HIGH-5 mitigation)"
    - "When camera is Capturing, a juce::DocumentWindow popout window appears with the local preview rendered at 4:3 aspect"
    - "Popout window uses JamWideLookAndFeel chrome (dark theme, custom title bar)"
    - "Closing the popout via the X button hides the window but does NOT stop capture (D-09 orthogonality); state remains Capturing; popoutHidden flag flips true"
    - "Plugin state schema bumps from 3 to 4; reload of a v3-saved-state plus a v4-saved-state both produce expected camera-subtree defaults / persisted values"
    - "Popout window bounds (x, y, w, h) persist across plugin reload"
    - "NativeCameraPrivacyDialog dispatches based on label-keyed return-code mapping, not raw button index (HIGH-7 prep)"
  artifacts:
    - path: "juce/ui/ConnectionBar.h"
      provides: "cameraButton TextButton + onCameraClicked + onCameraQualitySelected + onCameraStopRequested + setCameraActive + setCameraLabel + setCameraQualityPreset"
      contains: "cameraButton"
    - path: "juce/ui/ConnectionBar.cpp"
      provides: "Camera button setup mirroring Video button + resized layout slot + right-click PopupMenu hookup (quality presets + Stop Camera item)"
      contains: "cameraButton.setButtonText"
    - path: "juce/JamWideJuceEditor.cpp"
      provides: "onCameraClicked decision tree (MEDIUM-1) + onCameraQualitySelected + onCameraStopRequested + first-launch HIGH-5 sequence + FallbackListener implementation + previewWindow construction"
      contains: "onCameraClicked"
    - path: "juce/video/native/CameraPreviewWindow.h"
      provides: "juce::DocumentWindow subclass + 4:3 aspect constraint + JamWideLookAndFeel chrome + hide-not-destroy on close + popoutHidden state surface"
      exports:
        - "class CameraPreviewWindow"
    - path: "juce/video/native/CameraPreviewTile.h"
      provides: "juce::Component subclass + JamWideFrameDistributor::Subscriber implementation using juce::AsyncUpdater (HIGH-4 mitigation)"
      exports:
        - "class CameraPreviewTile"
    - path: "juce/video/native/NativeCameraPrivacyDialog.h"
      provides: "D-22 first-use modal with label-keyed return-code dispatch"
      exports:
        - "class NativeCameraPrivacyDialog"
    - path: "tests/test_plugin_state_v3_v4.cpp"
      provides: "Save state on v3, load with v4 binary, assert defaults applied; v4 round-trip; T-19-03 clamping defense"
      min_lines: 100
  key_links:
    - from: "ConnectionBar::cameraButton"
      to: "JamWideJuceEditor::connectionBar.onCameraClicked"
      via: "juce::TextButton::onClick lambda + std::function<void()> callback"
      pattern: "onCameraClicked"
    - from: "JamWideJuceEditor::onCameraClicked"
      to: "JamWideJuceProcessor::getNativeCamera()->toggle()"
      via: "Decision-tree lambda dispatches per state+popout combination (MEDIUM-1)"
      pattern: "getNativeCamera"
    - from: "CameraPreviewTile"
      to: "JamWideFrameDistributor"
      via: "registerSubscriber in constructor returns Subscription RAII; release in destructor (HIGH-2)"
      pattern: "registerSubscriber"
    - from: "CameraPreviewTile"
      to: "juce::AsyncUpdater"
      via: "onFrame copies juce::Image under mutex + triggerAsyncUpdate; handleAsyncUpdate repaints on message thread (HIGH-4)"
      pattern: "triggerAsyncUpdate"
    - from: "JamWideJuceProcessor::getStateInformation"
      to: "ValueTree APVTSTree"
      via: "Writes cameraPopoutX/Y/Width/Height/QualityPreset/PrivacyAck/SelectedDevice properties at stateVersion=4"
      pattern: "cameraPopout"
    - from: "JamWideJuceProcessor::setStateInformation"
      to: "JamWideCameraDevice + popout bounds"
      via: "Reads camera-subtree fields and applies via setQualityPreset and popout bounds restore"
      pattern: "tree.getProperty"
---

<objective>
Land the UI surface for Phase 19's native camera with the lifetime + first-use UX fixes Codex flagged. Specifically: (a) a Camera button decision tree (MEDIUM-1) — capture toggle is no longer a single boolean, the button does the right thing based on (state, popoutVisible); (b) CameraPreviewTile uses `juce::AsyncUpdater` (HIGH-4) so repaint scheduling is safe even if the tile is destroyed mid-flight; (c) the privacy modal sequence (HIGH-5) fires on the REAL first-launch path — `NotDetermined → requestAccess → grant → modal → openDeviceAsync` — not just when status was already Authorized; (d) right-click menu adds an explicit "Stop Camera" item per MEDIUM-1; (e) plugin-state schema bump v3→v4 with the seven persisted camera fields (D-24/D-25), `juce::jlimit` clamping (T-19-03 mitigation), and the `test_plugin_state_v3_v4` unit test filled.

Purpose: 19-01 stands up the backend (camera owner, frame distributor, state machine with frame-stall watchdog, generation-token-safe async). This plan converts that backend into a clickable feature with correct first-launch UX and lifetime-safe preview rendering. The fallback dialog (when permission is denied) is 19-03's responsibility — until 19-03 lands, denial paths leave the button in "Recheck permission" state without showing a dialog.

Output: A clickable Camera UI driven by 19-01's `JamWideCameraDevice`; persisted popout bounds + quality preset; tests confirming v3→v4 schema migration. The first-launch privacy modal correctly fires on the NotDetermined-grant path.
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
@.planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md

<interfaces>
Key contracts from Plan 19-01 — use these directly:

From juce/video/native/JamWideCameraDevice.h (created by 19-01 Task 5):
  enum class CameraState : int { Idle, Opening, Capturing, Failed, Retrying, Unavailable };   // Paused REMOVED per MEDIUM-2
  class JamWideCameraDevice {
    JamWideCameraDevice(JamWideFrameDistributor&, FallbackListener*);
    void toggle();
    void recheckPermission();
    void setQualityPreset(int preset);
    int getQualityPreset() const;
    CameraState getState() const;
    juce::String getDeviceName() const;
    float getPeakFps() const;
    void shutdown();
    void setFallbackListener(FallbackListener*);
    class FallbackListener {
      virtual void onCameraFallback(CameraFallbackCause cause) = 0;
      virtual void onCameraStateChanged(CameraState newState) = 0;
    };
  };

From juce/video/native/JamWideFrameDistributor.h (created by 19-01 Task 3):
  class JamWideFrameDistributor {
    class Subscriber {
      virtual void onFrame(const juce::Image&) = 0;
    };
    class Subscription { /* RAII move-only handle */ };
    [[nodiscard]] Subscription registerSubscriber(Subscriber* s);
    void publish(const juce::Image&);
  };

From juce/video/native/CameraAuthorization.h (created by 19-01 Task 1):
  enum class CameraAuthStatus : int { NotDetermined, Restricted, Denied, Authorized, NotApplicable };
  CameraAuthStatus queryCameraAuthorization();
  void requestCameraAuthorization(std::function<void(CameraAuthStatus)>);

From juce/JamWideJuceProcessor.h (members after 19-01):
  std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor;
  std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera;
  jamwide::JamWideCameraDevice* getNativeCamera();
  jamwide::JamWideFrameDistributor* getFrameDistributor();
  // This plan adds: setCameraPopoutBounds setter; getCameraPopoutBounds getter; cameraQuality/PrivacyAck/SelectedDevice accessors
  // This plan bumps: currentStateVersion = 3 -> 4

From juce/JamWideJuceProcessor.cpp (current state save block at lines 622-672 — uses flat properties like oscEnabled, oscReceivePort, midiInputDeviceId):
  state.setProperty("stateVersion", currentStateVersion, nullptr);
  state.setProperty("lastServer", lastServerAddress, nullptr);
  state.setProperty("oscEnabled", oscEnabled, nullptr);
  // ... this plan adds camera flat properties using identical pattern (Approach A per RESEARCH §8 lines 711-733)

From juce/ui/ConnectionBar.cpp lines 206-217 (Video button setup, IMITATE):
  videoButton.setButtonText("Video");
  videoButton.setColour(juce::TextButton::buttonColourId, ...);
  videoButton.setTooltip("Open video companion in browser");
  videoButton.setEnabled(false);  // NOTE: Camera button is NOT disabled (D-11: independent of Connect)
  videoButton.onClick = [this]() { if (onVideoClicked) onVideoClicked(); };
  addAndMakeVisible(videoButton);

From juce/ui/ConnectionBar.cpp:300 (resized layout):
  videoButton.setBounds(rightX - 54, y, 54, h);
  rightX -= 54 + gap;

From juce/ui/JamWideLookAndFeel.h:12-26 (colour palette for popout chrome):
  static constexpr uint32_t kSurfaceStrip   = 0xff2A2D48;
  static constexpr uint32_t kTextPrimary    = 0xffE0E0E0;
  static constexpr uint32_t kAccentConnect  = 0xff40E070;

From libs/juce/modules/juce_gui_basics/windows/juce_MessageBoxOptions.h:94 — JUCE button API:
  MessageBoxOptions withButton(const String& text);   // text-only; no return-code overload exists
  // Per juce_AlertWindow.h:457-466, showAsync(options, callback) delivers:
  //   1 button:  button[0] -> 0
  //   2 buttons: button[0] -> 1, button[1] -> 0
  //   3 buttons: button[0] -> 1, button[1] -> 2, button[2] -> 0
  // Dialogs in this plan MUST map JUCE's int return code back to a semantic action via a label-keyed table.

D-25 camera ValueTree property defaults (flat schema; Approach A per RESEARCH §8):
  cameraPopoutX        = 100   (int)
  cameraPopoutY        = 100   (int)
  cameraPopoutWidth    = 320   (int)
  cameraPopoutHeight   = 240   (int)
  cameraQualityPreset  = 1     (int; 0=Low, 1=Medium, 2=High)
  cameraPrivacyAck     = false (bool)
  cameraSelectedDevice = ""    (String, empty=auto-pick)

State+popout decision matrix (MEDIUM-1 — implemented in onCameraClicked):
| state               | popoutVisible | privacyAck | action                                                                     |
|---------------------|---------------|------------|----------------------------------------------------------------------------|
| Idle                | (any)         | false      | query auth; if NotDetermined→request; if Authorized→SHOW privacy modal then toggle |
| Idle                | (any)         | true       | query auth; if NotDetermined→request; toggle (no modal)                    |
| Opening             | (any)         | (any)      | no-op (capture is starting)                                                |
| Capturing           | true          | true       | toggle (stop capture; closes popout via state change)                      |
| Capturing           | false         | true       | SHOW popout (capture stays on)                                             |
| Retrying / Failed   | (any)         | (any)      | no-op (system is recovering)                                               |
| Unavailable         | (any)         | (any)      | recheckPermission() (D-12)                                                 |
</interfaces>
</context>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| User to plugin UI | User clicks/right-clicks; UI must not crash on rapid click |
| ValueTree XML load to in-memory state | Saved-state XML read from disk; ints/strings can be malformed or out-of-range |
| Camera-callback thread → preview tile | onFrame fires on any thread; UI tile must marshal to message thread safely even under destruction |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-19-03 | Tampering (plugin state injection) | JamWideJuceProcessor::setStateInformation | mitigate | All v4 camera fields read via tree.getProperty(key, default); ints clamped to valid ranges (popoutX/Y clamped to ±10000 px; popoutWidth/Height clamped to 240..2560 / 180..1920; qualityPreset clamped to 0..2; privacyAck cast to bool; selectedDevice length capped at 256 chars). test_plugin_state_v3_v4 validates clamping. |
| T-19-04 | Privacy (camera-on without ack) | NativeCameraPrivacyDialog | mitigate | First-use modal shown ONCE after permission grant and BEFORE Phase 20's broadcast can begin. Phase 19 enforces show-once on the REAL first-launch path (HIGH-5 fix): NotDetermined → request → grant → modal → openDeviceAsync. Phase 20 will check ack before allowing broadcast. privacyAck is bool (unambiguous: true=acknowledged, false=not yet). |
| T-19-PT | Tampering (UAF in preview tile callAsync) | CameraPreviewTile + AsyncUpdater | mitigate | HIGH-4 fix: tile inherits `juce::AsyncUpdater`. `onFrame()` copies the latest juce::Image into `pendingFrame_` under `std::mutex`, then calls `triggerAsyncUpdate()`. `handleAsyncUpdate()` runs on the message thread, reads pendingFrame_ under mutex, and `repaint()`. AsyncUpdater is unregistered at component destruction (juce takes care of pending updates being cancelled). No raw-this callAsync; no UAF if tile is destroyed between trigger and dispatch. |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: ConnectionBar Camera button + right-click menu (quality + Stop Camera) + editor decision tree (MEDIUM-1)</name>
  <files>
    juce/ui/ConnectionBar.h,
    juce/ui/ConnectionBar.cpp,
    juce/JamWideJuceEditor.cpp,
    juce/JamWideJuceEditor.h
  </files>
  <read_first>
    - juce/ui/ConnectionBar.h (full file — see existing videoButton at line 72, onVideoClicked at line 33, setVideoActive at line 36)
    - juce/ui/ConnectionBar.cpp (lines 200-235 Video button setup; lines 280-310 resized layout; lines 510-530 enable/disable; lines 640-655 setVideoActive)
    - juce/JamWideJuceEditor.cpp (lines 120-150 onVideoClicked lambda — Camera follows the SAME shape but with a decision tree, NOT a single toggle)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §6 (file layout table at lines 543-557, right-click PopupMenu pattern at lines 565-580)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md MEDIUM-1 (the decision tree replaces the single-toggle semantics; popout reopens via the button click while Capturing)
  </read_first>
  <behavior>
    - Behavior 1: ConnectionBar declares public `juce::TextButton cameraButton`, plus six members: `std::function<void()> onCameraClicked`, `std::function<void(int)> onCameraQualitySelected`, `std::function<void()> onCameraStopRequested`, `void setCameraActive(bool)`, `void setCameraLabel(const juce::String&)`, `void setCameraQualityPreset(int)`. The Stop Camera path is wired via `onCameraStopRequested` so the right-click menu can stop capture independently of the left-click decision tree.
    - Behavior 2: Camera button text is initially "Camera"; label changes to "Recheck permission" when the editor's FallbackListener::onCameraStateChanged observes CameraState::Unavailable.
    - Behavior 3: Camera button is NEVER disabled based on connection state (D-11: camera is independent of Connect). Enabled at construction and stays enabled.
    - Behavior 4: Left-click invokes onCameraClicked. The editor's onCameraClicked lambda implements the MEDIUM-1 decision tree (see Action Edit 6 for the full tree).
    - Behavior 5: Right-click shows a juce::PopupMenu with TWO sections:
      - Section "Quality": three items "Low (320x240, 10fps)", "Medium (640x480, 15fps)", "High (1280x720, 30fps)" with the currently-active preset checked. Selection invokes `onCameraQualitySelected(preset_index 0/1/2)`.
      - Separator.
      - Item "Stop Camera": enabled ONLY when state==Capturing (the editor passes the state in via setCameraActive). Selection invokes `onCameraStopRequested()`. This is the explicit "stop while popout is open" path per MEDIUM-1.
    - Behavior 6: In resized(), the Camera button sits at rightX-60, y, 60, h (60px width to fit "Recheck permission" without truncation), then rightX -= 60 + gap so Video still draws to the right.
    - Behavior 7: JamWideJuceEditor implements `jamwide::JamWideCameraDevice::FallbackListener` and registers itself via `setFallbackListener(this)`. Unregisters in destructor (set listener to nullptr).
    - Behavior 8 (MEDIUM-1 decision tree): the editor's onCameraClicked dispatches per the state+popoutVisible+privacyAck matrix defined in the Interfaces block. Specifically:
      - state==Capturing AND previewWindow_ is hidden → call previewWindow_->setVisible(true), DO NOT toggle camera.
      - state==Capturing AND previewWindow_ is visible → call cam->toggle() (stops capture; state machine handles closeHardware).
      - state==Idle → continue to the auth+privacy sequence (HIGH-5 path, implemented in Task 3 Edit 5).
      - state==Unavailable → call cam->recheckPermission().
      - state==Opening/Retrying/Failed → no-op (log only).
  </behavior>
  <action>
    Edit 1 — juce/ui/ConnectionBar.h: in the public section near line 33, declare `std::function<void()> onCameraClicked;`, `std::function<void(int)> onCameraQualitySelected;`, and `std::function<void()> onCameraStopRequested;`. Near line 36, declare `void setCameraActive(bool active);`, `void setCameraLabel(const juce::String& label);`, `void setCameraQualityPreset(int preset);`. Near line 72 (next to videoButton), declare `juce::TextButton cameraButton;`. Also store private `int currentCameraQualityPreset_{1};` for the menu checkmark state and `bool cameraIsActive_{false};` so the right-click menu's Stop Camera item knows when to be enabled.

    Edit 2 — juce/ui/ConnectionBar.cpp constructor: immediately after `addAndMakeVisible(videoButton)` (line 217), add identical-shape setup for cameraButton: setButtonText("Camera"); set colours matching videoButton offline palette; setTooltip("Toggle camera preview (v1.3 beta)") per D-26; DO NOT setEnabled(false) per D-11; set onClick lambda → fires onCameraClicked; install a right-click handler. The right-click handler is a small file-local subclass `class CameraButton : public juce::TextButton` whose `mouseDown` override checks `e.mods.isPopupMenu()` and shows a juce::PopupMenu per Behavior 5. The menu construction inside the subclass:
       ```cpp
       juce::PopupMenu menu;
       menu.addSectionHeader("Quality");
       const int current = parent.currentCameraQualityPreset_;
       menu.addItem(1, "Low (320x240, 10fps)",  true, current == 0);
       menu.addItem(2, "Medium (640x480, 15fps)", true, current == 1);
       menu.addItem(3, "High (1280x720, 30fps)",  true, current == 2);
       menu.addSeparator();
       const bool stopEnabled = parent.cameraIsActive_;   // only enabled when Capturing
       menu.addItem(10, "Stop Camera", stopEnabled, /*ticked*/ false);
       menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
         [this](int result) {
           if (result >= 1 && result <= 3) {
               if (parent.onCameraQualitySelected) parent.onCameraQualitySelected(result - 1);
           } else if (result == 10) {
               if (parent.onCameraStopRequested) parent.onCameraStopRequested();
           }
         });
       ```
       Then `addAndMakeVisible(cameraButton);`.

    Edit 3 — juce/ui/ConnectionBar.cpp resized() (around line 300): immediately after `videoButton.setBounds(rightX - 54, y, 54, h); rightX -= 54 + gap;`, add `cameraButton.setBounds(rightX - 60, y, 60, h); rightX -= 60 + gap;` so Camera draws to the LEFT of Video.

    Edit 4 — juce/ui/ConnectionBar.cpp setters:
       - `setCameraActive(bool active)`: flip cameraButton textColourOffId to kAccentConnect (green) when active, revert otherwise; `cameraIsActive_ = active;` (so the right-click menu's Stop Camera item enables/disables correctly).
       - `setCameraLabel(s)`: `cameraButton.setButtonText(s); cameraButton.repaint();`
       - `setCameraQualityPreset(p)`: `currentCameraQualityPreset_ = p;`

    Edit 5 — juce/JamWideJuceEditor.h: change class declaration to `class JamWideJuceEditor : public juce::AudioProcessorEditor, public jamwide::JamWideCameraDevice::FallbackListener` (keep any other existing base classes). Declare the two overrides: `void onCameraFallback(jamwide::CameraFallbackCause cause) override;` and `void onCameraStateChanged(jamwide::CameraState newState) override;`. Add `#include "video/native/JamWideCameraDevice.h"` and `#include "video/native/CameraFallbackCause.h"`. Forward-declare `namespace jamwide { class CameraPreviewWindow; class NativeCameraPrivacyDialog; }`. Declare `std::unique_ptr<jamwide::CameraPreviewWindow> previewWindow_;` and `std::unique_ptr<jamwide::NativeCameraPrivacyDialog> privacyDialog_;` (the dialog is constructed via unique_ptr because Task 3 will hold per-show state inside it; using unique_ptr keeps the editor header light).

    Edit 6 — juce/JamWideJuceEditor.cpp around line 144 (after existing onVideoClicked lambda): wire the decision tree for `connectionBar.onCameraClicked`:
       ```cpp
       connectionBar.onCameraClicked = [this]() {
           auto* cam = processorRef.getNativeCamera();
           if (! cam) return;
           const auto state = cam->getState();

           // MEDIUM-1 decision tree:
           switch (state) {
               case jamwide::CameraState::Capturing: {
                   if (previewWindow_ && ! previewWindow_->isVisible()) {
                       previewWindow_->setVisible(true);   // popout was hidden; reopen it
                   } else {
                       cam->toggle();                       // popout visible; stop capture
                   }
                   return;
               }
               case jamwide::CameraState::Idle: {
                   // Continue to HIGH-5 first-launch sequence (Task 3 Edit 5 fills this)
                   handleCameraIdleClick();
                   return;
               }
               case jamwide::CameraState::Unavailable: {
                   cam->recheckPermission();   // D-12
                   return;
               }
               case jamwide::CameraState::Opening:
               case jamwide::CameraState::Retrying:
               case jamwide::CameraState::Failed:
                   juce::Logger::writeToLog("[JamWideEditor] Camera click ignored (state=" + jamwide::cameraStateToString(state) + ")");
                   return;
           }
       };
       ```
       Declare `void handleCameraIdleClick();` as a private member of JamWideJuceEditor; the body is a STUB in this task (`// Task 3 fills this with the HIGH-5 privacy-modal sequence`) and call `cam->toggle();` so this task's UI is functional for the already-acked case. Task 3 replaces the stub with the full HIGH-5 flow.

       Also wire `connectionBar.onCameraStopRequested = [this]() { if (auto* cam = processorRef.getNativeCamera()) { if (cam->getState() == jamwide::CameraState::Capturing) cam->toggle(); } };` — explicit stop path for the right-click menu (MEDIUM-1).

       And `connectionBar.onCameraQualitySelected = [this](int preset) { if (auto* cam = processorRef.getNativeCamera()) { cam->setQualityPreset(preset); processorRef.setCameraQualityPreset(preset); connectionBar.setCameraQualityPreset(preset); } };`.

       Also: `if (auto* cam = processorRef.getNativeCamera()) cam->setFallbackListener(this);` to register the editor as the FallbackListener.

       Also: also declare and provide a small file-scope helper `juce::String jamwide::cameraStateToString(jamwide::CameraState s)` in `juce/video/native/JamWideCameraDevice.cpp` (added in 19-01 Task 5 as part of the log helpers — note in SUMMARY). If not present, declare it inline in JamWideJuceEditor.cpp as `static juce::String stateToString(jamwide::CameraState s) { switch (s) { case jamwide::CameraState::Idle: return "Idle"; ... } return "Unknown"; }`.

    Edit 7 — juce/JamWideJuceEditor.cpp: implement `onCameraStateChanged(jamwide::CameraState newState)` at file scope:
       ```cpp
       void JamWideJuceEditor::onCameraStateChanged(jamwide::CameraState newState) {
           switch (newState) {
               case jamwide::CameraState::Unavailable:
                   connectionBar.setCameraLabel("Recheck permission");
                   connectionBar.setCameraActive(false);
                   break;
               case jamwide::CameraState::Capturing:
                   connectionBar.setCameraLabel("Camera");
                   connectionBar.setCameraActive(true);
                   break;
               case jamwide::CameraState::Idle:
                   connectionBar.setCameraLabel("Camera");
                   connectionBar.setCameraActive(false);
                   break;
               default:
                   // Opening / Retrying / Failed: keep current label
                   break;
           }
           // Task 2 will EXTEND this to drive previewWindow_ visibility.
       }
       ```
       Implement `onCameraFallback` as an empty stub with `juce::ignoreUnused(cause);` and a comment noting "19-03 Task 1 wires CameraStatusDialog here".

    Edit 8 — JamWideJuceEditor destructor: call `if (auto* cam = processorRef.getNativeCamera()) cam->setFallbackListener(nullptr);` to unregister before the editor is destroyed (the processor outlives the editor).
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target JamWideJuce_Standalone 2>&amp;1 | tail -10 &amp;&amp; grep -c "juce::TextButton cameraButton" juce/ui/ConnectionBar.h &amp;&amp; grep -c "onCameraClicked" juce/ui/ConnectionBar.h &amp;&amp; grep -c "onCameraQualitySelected" juce/ui/ConnectionBar.h &amp;&amp; grep -c "onCameraStopRequested" juce/ui/ConnectionBar.h &amp;&amp; grep -c "cameraButton.setButtonText" juce/ui/ConnectionBar.cpp &amp;&amp; grep -c "cameraButton.setBounds" juce/ui/ConnectionBar.cpp &amp;&amp; grep -c "Stop Camera" juce/ui/ConnectionBar.cpp &amp;&amp; grep -c "connectionBar.onCameraClicked" juce/JamWideJuceEditor.cpp &amp;&amp; grep -c "getNativeCamera" juce/JamWideJuceEditor.cpp &amp;&amp; grep -c "public jamwide::JamWideCameraDevice::FallbackListener" juce/JamWideJuceEditor.h &amp;&amp; grep -c "onCameraStopRequested" juce/JamWideJuceEditor.cpp &amp;&amp; grep -c "handleCameraIdleClick" juce/JamWideJuceEditor.cpp</automated>
  </verify>
  <done>
    JamWideJuce_Standalone builds; ConnectionBar exposes cameraButton + six callbacks/setters; right-click shows Quality submenu + Stop Camera item; JamWideJuceEditor implements the MEDIUM-1 decision tree (Capturing+hidden→reopen popout; Capturing+visible→toggle; Idle→handleCameraIdleClick which is a Task-3-filled stub; Unavailable→recheckPermission); editor is registered as a FallbackListener.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: CameraPreviewWindow + CameraPreviewTile with juce::AsyncUpdater (HIGH-4) and JamWideLookAndFeel chrome (D-05/07/08/09)</name>
  <files>
    juce/video/native/CameraPreviewWindow.h,
    juce/video/native/CameraPreviewWindow.cpp,
    juce/video/native/CameraPreviewTile.h,
    juce/video/native/CameraPreviewTile.cpp,
    juce/JamWideJuceEditor.cpp,
    juce/JamWideJuceProcessor.h,
    juce/JamWideJuceProcessor.cpp
  </files>
  <read_first>
    - libs/juce/modules/juce_gui_basics/windows/juce_DocumentWindow.h (skim closeButtonPressed override, setUsingNativeTitleBar, ComponentBoundsConstrainer::setFixedAspectRatio)
    - libs/juce/modules/juce_events/messages/juce_AsyncUpdater.h (the AsyncUpdater contract — `triggerAsyncUpdate` schedules a single `handleAsyncUpdate` call on the message thread; multiple triggers between dispatches coalesce to one)
    - juce/ui/JamWideLookAndFeel.h (lines 12-26 — colour constants)
    - juce/video/VideoPrivacyDialog.h (existing dialog component pattern; new dialogs mirror its shape)
    - juce/video/native/JamWideFrameDistributor.h (Subscriber interface + Subscription RAII handle to use in the Tile)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §6 lines 584-596 (popout chrome implementation notes)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md HIGH-4 (the AsyncUpdater fix; replaces raw-this callAsync; also coalesces frame bursts)
  </read_first>
  <behavior>
    - Behavior 1: CameraPreviewWindow extends `juce::DocumentWindow` with `setUsingNativeTitleBar(false)` so JamWideLookAndFeel paints the custom title bar (D-08).
    - Behavior 2: Window is `setResizable(true, true)` with ComponentBoundsConstrainer `setFixedAspectRatio(4.0/3.0)` — D-07.
    - Behavior 3: Default size 320x240; min 240x180; max 2560x1920.
    - Behavior 4: Window starts hidden (`setVisible(false)`); shown when onCameraStateChanged(Capturing) fires.
    - Behavior 5: Clicking the X HIDES the window without destroying it and without signaling capture to stop — D-09 orthogonality.
    - Behavior 6: Title bar reads "JamWide — Camera: <device name>" with deviceName from `nativeCamera->getDeviceName()` (RESEARCH §1 line 167).
    - Behavior 7 (HIGH-4 mitigation — core): CameraPreviewTile extends `juce::Component` AND `juce::AsyncUpdater` AND `JamWideFrameDistributor::Subscriber`. `onFrame(const juce::Image&)` copies the image (cheap; juce::Image is reference-counted) into `pendingFrame_` under `std::mutex pendingMu_`, then calls `triggerAsyncUpdate()`. `handleAsyncUpdate()` runs on the message thread, copies pendingFrame_ to currentFrame_ under mutex, calls `repaint()`. AsyncUpdater is automatically cancelled when the tile is destroyed (juce::AsyncUpdater handles this safely). NO `juce::MessageManager::callAsync(this, ...)` is used. Frame bursts coalesce naturally: 100 triggerAsyncUpdate calls between dispatches → 1 handleAsyncUpdate call → 1 repaint.
    - Behavior 8: CameraPreviewTile stores the `Subscription` returned from `registerSubscriber` as a member — the Subscription is destroyed (and waits for in-flight publish) BEFORE the tile's other members are destroyed. Member declaration order is important: `Subscription subscription_` declared AFTER the mutex/frame fields so its destruction (which runs FIRST per reverse-declaration-order rules) blocks on in-flight onFrame before the mutex it touches is gone. Actually — to make this safe, declare `subscription_` LAST in the member list so it's destroyed FIRST. Document this in the .h with a `// MEMBER ORDER MATTERS — subscription_ MUST be the last member` comment.
    - Behavior 9: Window bounds change events fire a callback so the editor publishes the new bounds to the processor for state persistence.
  </behavior>
  <action>
    Edit 1 — Create juce/video/native/CameraPreviewTile.h + .cpp. The class:
       ```cpp
       class CameraPreviewTile : public juce::Component,
                                 public juce::AsyncUpdater,
                                 public JamWideFrameDistributor::Subscriber {
       public:
           explicit CameraPreviewTile(JamWideFrameDistributor& distributor);
           ~CameraPreviewTile() override;

           // JamWideFrameDistributor::Subscriber
           void onFrame(const juce::Image& image) override;

           // juce::AsyncUpdater
           void handleAsyncUpdate() override;

           // juce::Component
           void paint(juce::Graphics& g) override;

       private:
           JamWideFrameDistributor& distributor_;

           std::mutex pendingMu_;
           juce::Image pendingFrame_;   // most-recent frame waiting for repaint

           std::mutex currentMu_;
           juce::Image currentFrame_;   // currently-painted frame

           // MEMBER ORDER MATTERS — subscription_ MUST be the LAST member.
           // Its destructor blocks until in-flight onFrame calls return.
           // Declaring it last means it is destroyed FIRST (reverse-declaration order),
           // so the mutexes + frame members are still alive while waiting.
           JamWideFrameDistributor::Subscription subscription_;
       };
       ```

       Implementation:
       ```cpp
       CameraPreviewTile::CameraPreviewTile(JamWideFrameDistributor& d) : distributor_(d) {
           subscription_ = distributor_.registerSubscriber(this);
       }
       CameraPreviewTile::~CameraPreviewTile() {
           // Order: ~juce::AsyncUpdater is invoked LATER (base class destroyed after members in reverse order).
           // subscription_ goes first because it's the last-declared member; its dtor blocks until in-flight
           // onFrame returns, so no onFrame can fire after this line. After subscription_ is destroyed, ~AsyncUpdater
           // cancels any pending callbacks. Safe.
       }
       void CameraPreviewTile::onFrame(const juce::Image& img) {
           {
               std::lock_guard<std::mutex> lock(pendingMu_);
               pendingFrame_ = img;   // juce::Image copy is ref-count; cheap
           }
           triggerAsyncUpdate();      // HIGH-4: NOT MessageManager::callAsync(this, ...); coalesces bursts
       }
       void CameraPreviewTile::handleAsyncUpdate() {
           juce::Image latest;
           {
               std::lock_guard<std::mutex> lock(pendingMu_);
               latest = pendingFrame_;
           }
           if (latest.isValid()) {
               {
                   std::lock_guard<std::mutex> lock(currentMu_);
                   currentFrame_ = latest;
               }
               repaint();
           }
       }
       void CameraPreviewTile::paint(juce::Graphics& g) {
           g.fillAll(juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
           juce::Image toDraw;
           {
               std::lock_guard<std::mutex> lock(currentMu_);
               toDraw = currentFrame_;
           }
           if (toDraw.isValid()) {
               g.drawImage(toDraw, getLocalBounds().toFloat(),
                           juce::RectanglePlacement::centred);
           }
       }
       ```

    Edit 2 — Create juce/video/native/CameraPreviewWindow.h + .cpp. The class:
       ```cpp
       class CameraPreviewWindow : public juce::DocumentWindow {
       public:
           CameraPreviewWindow(JamWideFrameDistributor& distributor,
                                juce::LookAndFeel* lnf,
                                juce::Rectangle<int> initialBounds);
           ~CameraPreviewWindow() override;

           void closeButtonPressed() override;   // hide, do not destroy (D-09)
           void componentMovedOrResized(juce::Component& which, bool wasMoved, bool wasResized) override;

           void setDeviceName(const juce::String& name);

           std::function<void()> onCloseRequested;
           std::function<void(juce::Rectangle<int>)> onBoundsChanged;

       private:
           std::unique_ptr<CameraPreviewTile> tile_;
       };
       ```

       Implementation: in the constructor, `juce::DocumentWindow("JamWide — Camera", juce::Colour(JamWideLookAndFeel::kSurfaceStrip), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)` base init. Then `setUsingNativeTitleBar(false); setLookAndFeel(lnf); setResizable(true, true); getConstrainer()->setFixedAspectRatio(4.0 / 3.0); getConstrainer()->setSizeLimits(240, 180, 2560, 1920); tile_ = std::make_unique<CameraPreviewTile>(distributor); setContentOwned(tile_.release(), true); setBounds(initialBounds); setVisible(false);`. `closeButtonPressed` calls `setVisible(false); if (onCloseRequested) onCloseRequested();`. `componentMovedOrResized` calls `if (onBoundsChanged) onBoundsChanged(getBounds());`. `setDeviceName` calls `setName("JamWide — Camera: " + name);`. Destructor `setLookAndFeel(nullptr);`.

    Edit 3 — Update juce/JamWideJuceEditor.h: forward-decls already present from Task 1.

    Edit 4 — juce/JamWideJuceEditor.cpp constructor (after Task 1's `setFallbackListener` call): construct previewWindow_:
       ```cpp
       previewWindow_ = std::make_unique<jamwide::CameraPreviewWindow>(
           *processorRef.getFrameDistributor(),
           &lookAndFeel,   // the editor's existing JamWideLookAndFeel instance
           processorRef.getCameraPopoutBounds());
       previewWindow_->onCloseRequested = [this]() { /* D-09 — no-op; capture continues */ };
       previewWindow_->onBoundsChanged = [this](juce::Rectangle<int> r) {
           processorRef.setCameraPopoutBounds(r);
       };
       ```

    Edit 5 — Extend juce/JamWideJuceEditor.cpp onCameraStateChanged (added in Task 1): inside the existing implementation, append:
       ```cpp
       if (previewWindow_) {
           if (newState == jamwide::CameraState::Capturing) {
               previewWindow_->setDeviceName(processorRef.getNativeCamera()->getDeviceName());
               previewWindow_->setVisible(true);
           } else if (newState == jamwide::CameraState::Idle ||
                      newState == jamwide::CameraState::Unavailable) {
               previewWindow_->setVisible(false);
           }
           // Other states (Opening, Retrying, Failed): leave previewWindow_ visibility as-is per D-09 orthogonality
       }
       ```

    Edit 6 — JamWideJuceProcessor.h: add public accessor `jamwide::JamWideFrameDistributor* getFrameDistributor() { return frameDistributor.get(); }`. Add `juce::Rectangle<int> getCameraPopoutBounds() const;` and `void setCameraPopoutBounds(juce::Rectangle<int>);`. Backed by `juce::Rectangle<int> cameraPopoutBounds_{100,100,320,240}` guarded by `mutable std::mutex cameraPopoutMu_`. Task 3 plumbs the state save/load through these accessors.

    Edit 7 — JamWideJuceProcessor.cpp: implement getCameraPopoutBounds/setCameraPopoutBounds with the mutex.

    No automated test for visual UI components — verification is build success plus the manual UAT cells in 19-03. The verify command checks file existence + key wiring.

    Edit 8 — Note for the executor: do NOT add `CameraPreviewWindow.cpp` and `CameraPreviewTile.cpp` to CMakeLists.txt — they are ALREADY in the target_sources block 19-01 Task 1 staged. This task FILLS the previously-empty stub files; no CMake edit needed.
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target JamWideJuce_Standalone 2>&amp;1 | tail -10 &amp;&amp; test -f juce/video/native/CameraPreviewWindow.h &amp;&amp; test -f juce/video/native/CameraPreviewWindow.cpp &amp;&amp; test -f juce/video/native/CameraPreviewTile.h &amp;&amp; test -f juce/video/native/CameraPreviewTile.cpp &amp;&amp; grep -c "class CameraPreviewWindow.*juce::DocumentWindow" juce/video/native/CameraPreviewWindow.h &amp;&amp; grep -c "setFixedAspectRatio" juce/video/native/CameraPreviewWindow.cpp &amp;&amp; grep -c "setUsingNativeTitleBar(false)" juce/video/native/CameraPreviewWindow.cpp &amp;&amp; grep -c "juce::AsyncUpdater" juce/video/native/CameraPreviewTile.h &amp;&amp; grep -c "triggerAsyncUpdate" juce/video/native/CameraPreviewTile.cpp &amp;&amp; ! grep -c "MessageManager::callAsync.*this" juce/video/native/CameraPreviewTile.cpp | grep -v '^0$' | head -1; true &amp;&amp; grep -c "registerSubscriber" juce/video/native/CameraPreviewTile.cpp &amp;&amp; grep -c "previewWindow_ = std::make_unique" juce/JamWideJuceEditor.cpp</automated>
  </verify>
  <done>
    JamWideJuce_Standalone builds; CameraPreviewWindow + CameraPreviewTile exist with the documented signatures; tile inherits from juce::AsyncUpdater and uses triggerAsyncUpdate (HIGH-4 closure verified by `grep MessageManager::callAsync.*this` returning 0); editor constructs previewWindow_ and registers onCloseRequested/onBoundsChanged. Launching standalone, clicking Camera button after granting permission shows the popout with a live preview, and frame bursts coalesce to repaint cadence.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 3: NativeCameraPrivacyDialog + first-launch HIGH-5 sequence + plugin-state v3→v4 (D-22, D-24, D-25, T-19-03)</name>
  <files>
    juce/video/native/NativeCameraPrivacyDialog.h,
    juce/video/native/NativeCameraPrivacyDialog.cpp,
    juce/JamWideJuceProcessor.h,
    juce/JamWideJuceProcessor.cpp,
    juce/JamWideJuceEditor.cpp,
    tests/test_plugin_state_v3_v4.cpp
  </files>
  <read_first>
    - juce/video/VideoPrivacyDialog.h + .cpp (existing dialog pattern — the new NativeCameraPrivacyDialog mirrors its component shape but with different copy and different ack-storage)
    - juce/JamWideJuceProcessor.cpp lines 622-790 (getStateInformation + setStateInformation full bodies — see the OSC v1->v2 migration pattern with tree.getProperty(key, default); the camera fields follow Approach A flat properties per RESEARCH §8 lines 711-733)
    - juce/JamWideJuceProcessor.h line 88 (currentStateVersion = 3 — bump to 4)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §8 (plugin state schema migration), §11 (test harness inventory)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md HIGH-5 (first-launch sequence must fire after NotDetermined→grant; the prior plan only fired when already Authorized)
    - .planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md HIGH-7 (button-index mapping; this plan uses label-keyed dispatch so HIGH-7 is partially addressed here too)
    - tests/test_rawdata_send.cpp (test harness pattern — main()-style, assert() based)
    - libs/juce/modules/juce_gui_basics/windows/juce_AlertWindow.h lines 457-466 (the EXACT button-index → return-code mapping)
  </read_first>
  <behavior>
    - Behavior 1: NativeCameraPrivacyDialog is a class with method `void show(std::function<void(bool acknowledged)> onAck)`. Internally builds a juce::MessageBoxOptions with title="Camera privacy notice", icon=InfoIcon, message=D-22 copy, two buttons added in order: "I understand" then "Cancel". Calls `juce::AlertWindow::showAsync(options, [onAck](int result) { ... })`.
    - Behavior 2 (HIGH-7 prep — label-keyed dispatch): The completion handler maps JUCE's documented int return code back to a semantic action via a STATIC table. Per `juce_AlertWindow.h:457-466`, for a 2-button dialog: button[0] (= "I understand") returns 1; button[1] (= "Cancel") returns 0. The dialog's handler checks `if (result == 1) onAck(true); else onAck(false);`. The mapping is hardcoded based on the exact label order added in this code. A unit-test-style check is REQUIRED: the dialog exposes a static helper `static bool isAckResult(int juceResult)` that returns `juceResult == 1` — this lets 19-03 reuse the same pattern and confirms the mapping is centralised. JUCE does NOT support `withButton(label, returnCode)` (verified by inspecting juce_MessageBoxOptions.h:94), so this label-aware-with-known-order pattern is the correct fix.
    - Behavior 3 (HIGH-5 fix — the REAL first-launch flow): The editor's `handleCameraIdleClick()` method (stubbed in Task 1) is now FILLED with the correct sequence:
        1. Query current auth status via `jamwide::queryCameraAuthorization()`.
        2. If status == Authorized AND privacyAck == false: show privacy modal → on "I understand" → set privacyAck=true → call `cam->toggle()` (which proceeds straight to openDeviceAsync since auth is already granted).
        3. If status == Authorized AND privacyAck == true: call `cam->toggle()` directly (no modal).
        4. If status == NotDetermined: call `jamwide::requestCameraAuthorization(callback)` where callback dispatches to message thread, then if granted AND !privacyAck shows modal → on "I understand" persists ack → toggles camera; if granted AND privacyAck → toggles directly; if denied → toggles (state machine produces Unavailable + emits TCCDenied fallback to listener).
        5. If status == Denied/Restricted: call `cam->toggle()` directly (state machine routes to Unavailable; dialog appears via 19-03 wiring).
        6. If status == NotApplicable (Windows): call `cam->toggle()` directly (no TCC pre-check on Windows).
    - Behavior 4: JamWideJuceProcessor::currentStateVersion bumps from 3 to 4.
    - Behavior 5: getStateInformation writes seven new flat ValueTree properties: cameraPopoutX, cameraPopoutY, cameraPopoutWidth, cameraPopoutHeight, cameraQualityPreset, cameraPrivacyAck, cameraSelectedDevice.
    - Behavior 6 (T-19-03 mitigation): setStateInformation reads each via tree.getProperty(key, default) with D-25 defaults. Clamps qualityPreset to [0,2], popoutX/Y to ±10000, popoutWidth to [240,2560], popoutHeight to [180,1920], casts privacyAck to bool, caps selectedDevice length at 256 chars.
    - Behavior 7: v3 state loaded by v4 binary: missing camera fields get D-25 defaults; no crash, no v3 data loss. v4 round-trip preserves all seven fields exactly.
  </behavior>
  <action>
    Edit 1 — Create juce/video/native/NativeCameraPrivacyDialog.h + .cpp. Header:
       ```cpp
       #pragma once
       #include <functional>
       #include <juce_gui_basics/juce_gui_basics.h>

       namespace jamwide {

       class NativeCameraPrivacyDialog {
       public:
           // Shows the modal; invokes onAck(true) if user clicks "I understand", false otherwise.
           void show(std::function<void(bool)> onAck);

           // HIGH-7 prep: the dialog's button order is fixed at construction time:
           //   button[0] = "I understand"   →  JUCE returns 1  (per juce_AlertWindow.h:457-466)
           //   button[1] = "Cancel"         →  JUCE returns 0  (per same docs)
           // This static helper lets callers (and tests) verify the mapping without
           // duplicating the magic number throughout the code.
           static bool isAckResult(int juceResult) noexcept { return juceResult == 1; }
       };

       } // namespace jamwide
       ```

       Implementation:
       ```cpp
       void NativeCameraPrivacyDialog::show(std::function<void(bool)> onAck) {
           auto options = juce::MessageBoxOptions{}
               .withIconType(juce::MessageBoxIconType::InfoIcon)
               .withTitle("Camera privacy notice")
               .withMessage("JamWide broadcasts your camera to the NINJAM server and peers in your room. "
                            "Peers can save or redistribute their view. There's no separate IP exposure "
                            "beyond what audio already does.")
               .withButton("I understand")    // button[0] → JUCE returns 1
               .withButton("Cancel");          // button[1] → JUCE returns 0
           juce::AlertWindow::showAsync(options, [onAck = std::move(onAck)](int juceResult) {
               // Dispatch via the centralised mapping (HIGH-7 prep).
               onAck(NativeCameraPrivacyDialog::isAckResult(juceResult));
           });
       }
       ```

    Edit 2 — juce/JamWideJuceProcessor.h: change `static constexpr int currentStateVersion = 3;` (line 88) to `static constexpr int currentStateVersion = 4;` with comment `// v4: added camera flat properties (Phase 19; D-24)`. Add accessors (cameraPopoutBounds already added in Task 2 Edit 6):
       - `int getCameraQualityPreset() const`, `void setCameraQualityPreset(int)`
       - `bool getCameraPrivacyAck() const`, `void setCameraPrivacyAck(bool)`
       - `juce::String getCameraSelectedDevice() const`, `void setCameraSelectedDevice(const juce::String&)`

       Backing storage: `std::atomic<int> cameraQualityPreset_{1}`, `std::atomic<bool> cameraPrivacyAck_{false}`, `juce::String cameraSelectedDevice_` guarded by `mutable std::mutex cameraSelectedDeviceMu_`.

    Edit 3 — juce/JamWideJuceProcessor.cpp getStateInformation: after the existing OSC and MIDI saves (around line 661), add SEVEN flat property writes:
       ```cpp
       auto popoutB = getCameraPopoutBounds();
       state.setProperty("cameraPopoutX", popoutB.getX(), nullptr);
       state.setProperty("cameraPopoutY", popoutB.getY(), nullptr);
       state.setProperty("cameraPopoutWidth", popoutB.getWidth(), nullptr);
       state.setProperty("cameraPopoutHeight", popoutB.getHeight(), nullptr);
       state.setProperty("cameraQualityPreset", cameraQualityPreset_.load(), nullptr);
       state.setProperty("cameraPrivacyAck", cameraPrivacyAck_.load(), nullptr);
       state.setProperty("cameraSelectedDevice", getCameraSelectedDevice(), nullptr);
       ```

    Edit 4 — juce/JamWideJuceProcessor.cpp setStateInformation: after the existing v1→v2/v3 reads (around line 760), add SEVEN flat property reads with defaults + clamping (T-19-03 mitigation):
       ```cpp
       int popX = juce::jlimit(-10000, 10000, (int) tree.getProperty("cameraPopoutX", 100));
       int popY = juce::jlimit(-10000, 10000, (int) tree.getProperty("cameraPopoutY", 100));
       int popW = juce::jlimit(240, 2560, (int) tree.getProperty("cameraPopoutWidth", 320));
       int popH = juce::jlimit(180, 1920, (int) tree.getProperty("cameraPopoutHeight", 240));
       setCameraPopoutBounds(juce::Rectangle<int>(popX, popY, popW, popH));

       int qp = juce::jlimit(0, 2, (int) tree.getProperty("cameraQualityPreset", 1));
       setCameraQualityPreset(qp);
       if (nativeCamera) nativeCamera->setQualityPreset(qp);

       setCameraPrivacyAck((bool) tree.getProperty("cameraPrivacyAck", false));

       juce::String sel = tree.getProperty("cameraSelectedDevice", "").toString();
       if (sel.length() > 256) sel = sel.substring(0, 256);
       setCameraSelectedDevice(sel);
       ```

    Edit 5 — juce/JamWideJuceEditor.cpp: FILL the `handleCameraIdleClick()` stub from Task 1 with the HIGH-5 first-launch sequence:
       ```cpp
       void JamWideJuceEditor::handleCameraIdleClick() {
           auto* cam = processorRef.getNativeCamera();
           if (! cam) return;

           // HIGH-5 fix: dispatch on the CURRENT auth status, not on a stale "is Authorized" check.
           const auto status = jamwide::queryCameraAuthorization();
           switch (status) {
               case jamwide::CameraAuthStatus::Authorized: {
                   showPrivacyOrToggle(cam);
                   return;
               }
               case jamwide::CameraAuthStatus::NotDetermined: {
                   // OS prompt path. Request access, then on the completion callback
                   // (which fires on an unspecified thread per Apple's contract),
                   // marshal back to the message thread.
                   jamwide::requestCameraAuthorization([this](jamwide::CameraAuthStatus result) {
                       juce::MessageManager::callAsync([this, result]() {
                           auto* cam2 = processorRef.getNativeCamera();
                           if (! cam2) return;
                           if (result == jamwide::CameraAuthStatus::Authorized) {
                               showPrivacyOrToggle(cam2);
                           } else {
                               // Denied / Restricted — let the state machine route to Unavailable
                               // (19-03 will surface the dialog via FallbackListener).
                               cam2->toggle();
                           }
                       });
                   });
                   return;
               }
               case jamwide::CameraAuthStatus::Denied:
               case jamwide::CameraAuthStatus::Restricted: {
                   cam->toggle();   // state machine → Unavailable + emit fallback
                   return;
               }
               case jamwide::CameraAuthStatus::NotApplicable: {
                   // Windows — no TCC; check privacyAck and either show modal or toggle.
                   showPrivacyOrToggle(cam);
                   return;
               }
           }
       }

       void JamWideJuceEditor::showPrivacyOrToggle(jamwide::JamWideCameraDevice* cam) {
           if (! processorRef.getCameraPrivacyAck()) {
               if (! privacyDialog_) {
                   privacyDialog_ = std::make_unique<jamwide::NativeCameraPrivacyDialog>();
               }
               privacyDialog_->show([this, cam](bool acknowledged) {
                   if (acknowledged) {
                       processorRef.setCameraPrivacyAck(true);
                       cam->toggle();   // proceed to openDeviceAsync
                   }
                   // else: user cancelled; camera stays Idle
               });
           } else {
               cam->toggle();   // ack already given on a prior session
           }
       }
       ```
       Add the `void showPrivacyOrToggle(jamwide::JamWideCameraDevice* cam);` declaration as a private member of JamWideJuceEditor.

    Edit 6 — Create tests/test_plugin_state_v3_v4.cpp. Pure-C++ test using juce::ValueTree directly. The test does NOT link JamWideJuce — it replicates the read logic inline to test the schema clamping shape. Tests:
       - **Test 1 (v3 → v4 default migration)**: Construct a ValueTree mimicking a v3 save (no camera properties, stateVersion=3). Apply v4 read logic inline (the seven getProperty calls with defaults + jlimit clamping — copy-pasted from Edit 4). Assert: cameraQualityPreset reads as 1, cameraPrivacyAck reads as false, cameraSelectedDevice reads as empty string, popout bounds read as (100,100,320,240).
       - **Test 2 (v4 round-trip)**: Construct a v4 ValueTree with explicit camera properties (qualityPreset=2, privacyAck=true, selectedDevice="MyCam", popout=(50,75,640,480)); serialize to XML; parse back; apply read logic; assert exact match.
       - **Test 3 (clamping defense)**: Construct a malicious v4 ValueTree with cameraQualityPreset=99, cameraPopoutWidth=-5, cameraPopoutHeight=99999, cameraSelectedDevice = 1000-char string. After load, assert qualityPreset clamped to 2, popoutWidth clamped to 240, popoutHeight clamped to 1920, selectedDevice trimmed to 256 chars. This is the T-19-03 mitigation verification.
       - **Test 4 (HIGH-5 sanity — privacy-ack persistence)**: Construct v4 ValueTree with privacyAck=true; load; assert privacyAck is true (and not flipped to false by a default). This proves the ack survives serialization, which is what the HIGH-5 sequence depends on for "second session, don't show modal again".
       - **Test 5 (HIGH-7 sanity — privacy dialog button mapping)**: `assert(jamwide::NativeCameraPrivacyDialog::isAckResult(1) == true)` and `assert(jamwide::NativeCameraPrivacyDialog::isAckResult(0) == false)`. This pins the JUCE 2-button mapping (button[0]→1, button[1]→0). If a future JUCE upgrade changes the mapping, this test fails loudly. Test #5 requires the test to compile against NativeCameraPrivacyDialog.h — since the dialog uses juce_gui_basics, this test executable must link juce_gui_basics. Update CMakeLists.txt accordingly: the test_plugin_state_v3_v4 entry must link `juce::juce_core juce::juce_data_structures juce::juce_gui_basics` AND include `juce/video/native/NativeCameraPrivacyDialog.cpp` as a direct source. Edit the 19-01-staged CMake entry:
         ```
         add_executable(test_plugin_state_v3_v4
           tests/test_plugin_state_v3_v4.cpp
           juce/video/native/NativeCameraPrivacyDialog.cpp)
         target_link_libraries(test_plugin_state_v3_v4 PRIVATE juce::juce_core juce::juce_data_structures juce::juce_gui_basics)
         ```

       Use the assert()/exit(1) pattern from tests/test_rawdata_send.cpp.
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target test_plugin_state_v3_v4 JamWideJuce 2>&amp;1 | tail -10 &amp;&amp; cd build-juce-19-test &amp;&amp; ctest -R plugin_state_v3_v4 --output-on-failure &amp;&amp; cd .. &amp;&amp; grep -c "currentStateVersion = 4" juce/JamWideJuceProcessor.h &amp;&amp; grep -c "cameraPopoutX" juce/JamWideJuceProcessor.cpp &amp;&amp; grep -c "cameraPrivacyAck" juce/JamWideJuceProcessor.cpp &amp;&amp; grep -c "cameraQualityPreset" juce/JamWideJuceProcessor.cpp &amp;&amp; grep -c "juce::jlimit(0, 2" juce/JamWideJuceProcessor.cpp &amp;&amp; test -f juce/video/native/NativeCameraPrivacyDialog.h &amp;&amp; grep -c "isAckResult" juce/video/native/NativeCameraPrivacyDialog.h &amp;&amp; grep -c "handleCameraIdleClick" juce/JamWideJuceEditor.cpp &amp;&amp; grep -c "showPrivacyOrToggle" juce/JamWideJuceEditor.cpp &amp;&amp; grep -c "requestCameraAuthorization" juce/JamWideJuceEditor.cpp &amp;&amp; grep -c "NotDetermined" juce/JamWideJuceEditor.cpp</automated>
  </verify>
  <done>
    test_plugin_state_v3_v4 exits 0 with all 5 scenarios passing; currentStateVersion=4 in processor.h; the seven camera flat properties land in save/load with clamping; NativeCameraPrivacyDialog exists with the documented `isAckResult` helper; the editor's `handleCameraIdleClick` correctly handles all five CameraAuthStatus cases including NotDetermined→request→on-grant→showPrivacyOrToggle (HIGH-5 closure verified by `grep NotDetermined juce/JamWideJuceEditor.cpp` returning >=1 plus the requestCameraAuthorization callsite in editor).
  </done>
</task>

</tasks>

<verification>

## Plan-Level Verification

```bash
# 1. Plugin builds cleanly with full UI surface
cmake --build build-juce-19-02 --target JamWideJuce_Standalone JamWideJuce_VST3 JamWideJuce_AU 2>&1 | tail -20

# 2. Camera button visible + decision tree wired (MEDIUM-1)
grep -c 'cameraButton.setButtonText' juce/ui/ConnectionBar.cpp                   # >= 1
grep -c 'cameraButton.setBounds' juce/ui/ConnectionBar.cpp                       # >= 1
grep -c 'Stop Camera' juce/ui/ConnectionBar.cpp                                  # >= 1
grep -c 'onCameraClicked' juce/JamWideJuceEditor.cpp                             # >= 1
grep -c 'onCameraStopRequested' juce/JamWideJuceEditor.cpp                       # >= 1
grep -c 'handleCameraIdleClick' juce/JamWideJuceEditor.cpp                       # >= 1

# 3. HIGH-4 AsyncUpdater closure
grep -c 'juce::AsyncUpdater' juce/video/native/CameraPreviewTile.h               # >= 1
grep -c 'triggerAsyncUpdate' juce/video/native/CameraPreviewTile.cpp             # >= 1
grep -c 'handleAsyncUpdate' juce/video/native/CameraPreviewTile.cpp              # >= 1
test "$(grep -c 'MessageManager::callAsync.*this' juce/video/native/CameraPreviewTile.cpp)" -eq 0

# 4. HIGH-5 first-launch sequence
grep -c 'NotDetermined' juce/JamWideJuceEditor.cpp                               # >= 1
grep -c 'requestCameraAuthorization' juce/JamWideJuceEditor.cpp                  # >= 1
grep -c 'showPrivacyOrToggle' juce/JamWideJuceEditor.cpp                         # >= 1

# 5. HIGH-7 prep — label-keyed dispatch helper
grep -c 'isAckResult' juce/video/native/NativeCameraPrivacyDialog.h              # >= 1
grep -c 'isAckResult' juce/video/native/NativeCameraPrivacyDialog.cpp            # >= 1

# 6. Popout chrome
grep -c 'class CameraPreviewWindow.*juce::DocumentWindow' juce/video/native/CameraPreviewWindow.h
grep -c 'setFixedAspectRatio' juce/video/native/CameraPreviewWindow.cpp
grep -c 'setUsingNativeTitleBar(false)' juce/video/native/CameraPreviewWindow.cpp

# 7. State schema bumped + test green
grep -c 'currentStateVersion = 4' juce/JamWideJuceProcessor.h
cd build-juce-19-02 && ctest -R plugin_state_v3_v4 --output-on-failure
```

</verification>

<success_criteria>

This plan succeeds when:

1. **Camera button visible + correctly wired** — ConnectionBar shows "Camera" left of "Video"; left-click follows the MEDIUM-1 decision tree; right-click shows Quality submenu + Stop Camera item.
2. **MEDIUM-1 decision tree functional** — Capturing+hidden popout → reopen popout (no stop); Capturing+visible → stop; Unavailable → recheckPermission. Stop Camera right-click works regardless of popout state.
3. **HIGH-4 mitigated** — CameraPreviewTile uses `juce::AsyncUpdater`; no `MessageManager::callAsync(this, ...)`. Frame bursts coalesce to repaint rate.
4. **HIGH-5 mitigated** — first-launch (NotDetermined→grant) shows the privacy modal correctly; the editor's handleCameraIdleClick handles all five CameraAuthStatus cases.
5. **Popout renders preview** — When camera reaches Capturing, juce::DocumentWindow popout appears with 4:3 aspect, JamWideLookAndFeel chrome, live frames via CameraPreviewTile (CAM-03).
6. **Popout close is non-destructive** — Clicking X hides the window; capture stays Capturing per D-09; subsequent Camera-button click reopens the popout per MEDIUM-1.
7. **HIGH-7 prep — label-keyed return-code dispatch** — NativeCameraPrivacyDialog::isAckResult centralises the JUCE 2-button mapping; test_plugin_state_v3_v4 Test 5 pins the mapping (the same pattern will be used in 19-03 for the multi-button CameraStatusDialog).
8. **Privacy dialog on first authorized click** — fires ONCE per install on the REAL first-launch path; "I understand" persists privacyAck=true (D-22 + HIGH-5).
9. **State schema v3→v4** — currentStateVersion bumped; seven new flat properties added; test_plugin_state_v3_v4 exits 0 with default-migration, round-trip, AND clamping scenarios passing.
10. **Persistence round-trip** — Popout bounds + quality preset survive plugin reload.

</success_criteria>

<output>
Create `.planning/phases/19-camera-capture-permission-ux/19-02-SUMMARY.md` summarizing:
- ConnectionBar Camera button wiring (line numbers in ConnectionBar.cpp for the cameraButton.setBounds insertion)
- File-local CameraButton subclass for right-click handling (or alternative)
- MEDIUM-1 decision tree: state→action mapping table mirrored from this plan's Interfaces block; cite the editor lambda by line number
- HIGH-4 mitigation: CameraPreviewTile.h member-order comment (`subscription_ MUST be last`) + AsyncUpdater dispatch path
- HIGH-5 mitigation: editor.handleCameraIdleClick switch-on-status flow; what fires the modal in each branch
- HIGH-7 prep: NativeCameraPrivacyDialog::isAckResult centralises the JUCE 2-button mapping (button[0]→1, button[1]→0). Cite the JUCE doc reference juce_AlertWindow.h:457-466. 19-03 will adopt the same label-keyed pattern for the 3-button CameraStatusDialog.
- CameraPreviewWindow chrome details (final size limits, aspect ratio constraint, whether setUsingNativeTitleBar was honored on macOS — sometimes JUCE forces native on Apple)
- Plugin state v3→v4 schema (exact property names + defaults + clamping values)
- Tests added (1 new file body: test_plugin_state_v3_v4.cpp with 5 scenarios — default-migration, round-trip, clamping, ack-persistence, button-mapping)
- Any deviations from RESEARCH.md §6 file layout or §8 schema approach
</output>

## Addressed Review Findings

| Codex Finding | Resolution | Task(s) |
|---------------|------------|---------|
| **HIGH-1** | (Resolved in 19-01 Task 1 stubs.) | (19-01) |
| **HIGH-2** | (Resolved in 19-01 Task 3 Subscription RAII.) | (19-01) |
| **HIGH-3** | (Resolved in 19-01 Task 5 generation tokens.) | (19-01) |
| **HIGH-4** (preview tile callAsync UAF) | CameraPreviewTile inherits `juce::AsyncUpdater`. `onFrame()` copies juce::Image under mutex + `triggerAsyncUpdate()`. `handleAsyncUpdate()` runs on message thread, copies to currentFrame_ + `repaint()`. NO `MessageManager::callAsync(this, ...)` anywhere. Member-order rule: `Subscription subscription_` declared LAST so it destructs FIRST, blocking on any in-flight onFrame before mutex/frame members go away. Frame bursts coalesce automatically (100 onFrame calls → 1 handleAsyncUpdate → 1 repaint). Verified by `grep -c 'MessageManager::callAsync.*this' CameraPreviewTile.cpp == 0`. | Task 2 (CameraPreviewTile.{h,cpp}) |
| **HIGH-5** (privacy modal not fired on real first launch) | `handleCameraIdleClick()` switches on `jamwide::queryCameraAuthorization()` return value. For `NotDetermined`, calls `jamwide::requestCameraAuthorization(callback)`. The callback dispatches to message thread via `MessageManager::callAsync`. On Authorized result, calls `showPrivacyOrToggle(cam)` which checks `processorRef.getCameraPrivacyAck()`; if !ack, shows NativeCameraPrivacyDialog; on "I understand", persists ack=true + `cam->toggle()`. This is the REAL first-launch path (NotDetermined → grant → modal → openDeviceAsync) — not the buggy "if status was already Authorized when we entered the lambda" check. Test_plugin_state_v3_v4 Test 4 pins privacyAck persistence. The end-to-end UAT cell that exercises HIGH-5 is added in 19-03 Task 3 (Cell 10 — see 19-03 below). | Task 1 Edit 6 (stub) + Task 3 Edit 5 (full sequence) |
| **HIGH-6** | (Resolved in 19-01 Task 5 FrameStallWatchdog.) | (19-01) |
| **HIGH-7** (button-index mapping) | Partial mitigation in this plan: NativeCameraPrivacyDialog uses `isAckResult(int juceResult)` static helper that returns `juceResult == 1`. The two-button mapping is documented in the helper's doc comment (button[0]→1, button[1]→0 per juce_AlertWindow.h:457-466). Test_plugin_state_v3_v4 Test 5 pins the mapping. The same pattern is applied at scale in 19-03 Task 1 for the 3-button CameraStatusDialog. | Task 3 (isAckResult helper + Test 5) + (19-03 Task 1 for the full mapping) |
| **MEDIUM-1** (hidden popout reopen) | onCameraClicked is no longer a single toggle — it's a switch-on-state decision tree (per the Interfaces block matrix). Capturing+popoutHidden → setVisible(true) (no toggle); Capturing+popoutVisible → toggle (stop). Right-click menu adds explicit "Stop Camera" item via `onCameraStopRequested` that calls toggle() unconditionally when state==Capturing. Documented in Task 1 Edit 6. | Task 1 Edits 1-6 |
| **MEDIUM-2** | (Resolved in 19-01 Task 4.) | (19-01) |
| **MEDIUM-3** | (Resolved in 19-01 Task 4.) | (19-01) |
| **MEDIUM-4** | (Resolved in 19-01 Task 1 license preflight.) | (19-01) |
| **MEDIUM-5** | (Resolved in 19-01 Task 1 CMake test linkage standardisation.) | (19-01) |
| **MEDIUM-6** (cause-detection approximate) | **Deferred to 19-03** — softened dialog copy in 19-03 Task 1. | (19-03 Task 1) |
| **LOW-1** | (UAT note added in 19-03 Task 3 checklist.) | (19-03 Task 3) |
