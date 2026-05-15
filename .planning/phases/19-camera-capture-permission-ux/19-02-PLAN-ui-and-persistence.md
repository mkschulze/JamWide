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
  - CMakeLists.txt
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
    - "Clicking Camera button invokes processorRef.getNativeCamera()->toggle()"
    - "Right-clicking the Camera button shows a PopupMenu with Low/Medium/High quality items; selecting one persists across plugin reload"
    - "Camera button label changes to 'Recheck permission' when CameraState is Unavailable"
    - "When camera is Capturing, a juce::DocumentWindow popout window appears with the local preview rendered at 4:3 aspect"
    - "Popout window uses JamWideLookAndFeel chrome (dark theme, custom title bar)"
    - "Closing the popout via the X button hides the window but does NOT stop capture (D-09 orthogonality)"
    - "On second click of Camera button after permission granted, the NativeCameraPrivacyDialog appears once and persists privacyAck=true"
    - "Plugin state schema bumps from 3 to 4; reload of a v3-saved-state plus a v4-saved-state both produce expected camera-subtree defaults / persisted values"
    - "Popout window bounds (x, y, w, h) persist across plugin reload"
  artifacts:
    - path: "juce/ui/ConnectionBar.h"
      provides: "cameraButton TextButton + onCameraClicked + onCameraQualitySelected + setCameraActive + setCameraLabel"
      contains: "cameraButton"
    - path: "juce/ui/ConnectionBar.cpp"
      provides: "Camera button setup mirroring Video button + resized layout slot + right-click PopupMenu hookup"
      contains: "cameraButton.setButtonText"
    - path: "juce/JamWideJuceEditor.cpp"
      provides: "onCameraClicked + onCameraQualitySelected lambdas; FallbackListener implementation; previewWindow construction; privacy dialog gate"
      contains: "onCameraClicked"
    - path: "juce/video/native/CameraPreviewWindow.h"
      provides: "juce::DocumentWindow subclass + 4:3 aspect constraint + JamWideLookAndFeel chrome + hide-not-destroy on close"
      exports:
        - "class CameraPreviewWindow"
    - path: "juce/video/native/CameraPreviewTile.h"
      provides: "juce::Component subclass + JamWideFrameDistributor::Subscriber implementation"
      exports:
        - "class CameraPreviewTile"
    - path: "juce/video/native/NativeCameraPrivacyDialog.h"
      provides: "D-22 first-use modal for native camera broadcast (distinct from juce/video/VideoPrivacyDialog.h)"
      exports:
        - "class NativeCameraPrivacyDialog"
    - path: "tests/test_plugin_state_v3_v4.cpp"
      provides: "Save state on v3, load with v4 binary, assert defaults applied; v4 round-trip"
      min_lines: 100
  key_links:
    - from: "ConnectionBar::cameraButton"
      to: "JamWideJuceEditor::connectionBar.onCameraClicked"
      via: "juce::TextButton::onClick lambda + std::function<void()> callback"
      pattern: "onCameraClicked"
    - from: "JamWideJuceEditor::onCameraClicked"
      to: "JamWideJuceProcessor::getNativeCamera()->toggle()"
      via: "lambda body invokes processor accessor + JamWideCameraDevice::toggle()"
      pattern: "getNativeCamera"
    - from: "CameraPreviewTile"
      to: "JamWideFrameDistributor"
      via: "registerSubscriber in constructor; unregisterSubscriber in destructor"
      pattern: "registerSubscriber"
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
Land the entire UI surface for Phase 19's native camera: a Camera button in ConnectionBar (D-06), a right-click quality-preset PopupMenu (D-19), the floating juce::DocumentWindow popout that renders the preview at 4:3 with JamWideLookAndFeel chrome (D-05/07/08), the orthogonal popout-vs-capture state semantics (D-09), the first-use NativeCameraPrivacyDialog (D-22), and the plugin-state schema bump from v3 to v4 with all seven persisted camera fields (D-24/D-25). Wire the editor lambda chain that connects the button click to `processorRef.getNativeCamera()->toggle()`.

Purpose: This plan converts the Wave 1 backend into a user-facing feature. After this plan, a developer can click "Camera" in ConnectionBar, see the preview window, switch quality preset, and observe the state survive a save+reload cycle. The fallback dialog (when permission is denied) is the parallel 19-03 plan's responsibility — until 19-03 lands, denial paths leave the button in the "Recheck permission" state without showing a dialog. That's a deliberate gap: 19-02 and 19-03 are partners.

Output: A clickable Camera UI that drives the JamWideCameraDevice from 19-01; persisted popout bounds + quality preset; tests confirming v3 to v4 schema migration.
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
Key contracts from Plan 19-01 — use these directly:

From juce/video/native/JamWideCameraDevice.h (created by 19-01):
  enum class CameraState : int { Idle, Opening, Capturing, Paused, Failed, Retrying, Unavailable };
  class JamWideCameraDevice {
    JamWideCameraDevice(JamWideFrameDistributor and, FallbackListener pointer);
    void toggle();
    void recheckPermission();
    void setQualityPreset(int preset);
    int getQualityPreset() const;
    CameraState getState() const;
    juce::String getDeviceName() const;
    float getPeakFps() const;
    void shutdown();
    void setFallbackListener(FallbackListener pointer);
    class FallbackListener {
      virtual void onCameraFallback(CameraFallbackCause cause) = 0;
      virtual void onCameraStateChanged(CameraState newState) = 0;
    };
  };

From juce/video/native/JamWideFrameDistributor.h (created by 19-01):
  class JamWideFrameDistributor {
    class Subscriber {
      virtual void onFrame(const juce::Image and) = 0;
    };
    void registerSubscriber(Subscriber pointer);
    void unregisterSubscriber(Subscriber pointer);
  };

From juce/JamWideJuceProcessor.h (members after 19-01):
  std::unique_ptr<jamwide::JamWideFrameDistributor> frameDistributor;
  std::unique_ptr<jamwide::JamWideCameraDevice> nativeCamera;
  jamwide::JamWideCameraDevice pointer getNativeCamera();
  // This plan adds: getFrameDistributor() accessor; setCameraPopoutBounds setter; getCameraPopoutBounds getter
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

D-25 camera ValueTree property defaults (flat schema; Approach A per RESEARCH §8):
  cameraPopoutX        = 100   (int)
  cameraPopoutY        = 100   (int)
  cameraPopoutWidth    = 320   (int)
  cameraPopoutHeight   = 240   (int)
  cameraQualityPreset  = 1     (int; 0=Low, 1=Medium, 2=High)
  cameraPrivacyAck     = false (bool)
  cameraSelectedDevice = ""    (String, empty=auto-pick)
</interfaces>
</context>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| User to plugin UI | User clicks/right-clicks; UI must not crash on rapid click |
| ValueTree XML load to in-memory state | Saved-state XML read from disk; ints/strings can be malformed or out-of-range |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-19-03 | Tampering (plugin state injection) | JamWideJuceProcessor::setStateInformation | mitigate | All v4 camera fields read via tree.getProperty(key, default); ints clamped to valid ranges (popoutX/Y clamped to current desktop bounds with juce::Desktop::getInstance().getDisplays(); popoutWidth/Height clamped to 240..2560; qualityPreset clamped to 0..2; privacyAck cast to bool; selectedDevice length capped at 256 chars). test_plugin_state_v3_v4 validates clamping. |
| T-19-04 | Privacy (camera-on without ack) | NativeCameraPrivacyDialog | mitigate | First-use modal shown ONCE after permission grant and BEFORE Phase 20's broadcast can begin. Phase 19 enforces show-once + persist ack=true; Phase 20 will check ack before allowing broadcast. privacyAck is bool (unambiguous: true=acknowledged, false=not yet). |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: ConnectionBar Camera button + right-click PopupMenu + editor wiring</name>
  <files>
    juce/ui/ConnectionBar.h,
    juce/ui/ConnectionBar.cpp,
    juce/JamWideJuceEditor.cpp,
    juce/JamWideJuceEditor.h
  </files>
  <read_first>
    - juce/ui/ConnectionBar.h (full file — see existing videoButton at line 72, onVideoClicked at line 33, setVideoActive at line 36)
    - juce/ui/ConnectionBar.cpp (lines 200-235 Video button setup; lines 280-310 resized layout; lines 510-530 enable/disable; lines 640-655 setVideoActive)
    - juce/JamWideJuceEditor.cpp (lines 120-150 onVideoClicked lambda — Camera follows same pattern but invokes processor.getNativeCamera()->toggle())
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §6 (file layout table at lines 543-557, right-click PopupMenu pattern at lines 565-580)
  </read_first>
  <behavior>
    - Behavior 1: ConnectionBar declares public juce::TextButton cameraButton (mirrors videoButton at line 72) plus four members: std::function<void()> onCameraClicked, std::function<void(int)> onCameraQualitySelected, void setCameraActive(bool), void setCameraLabel(const juce::String and).
    - Behavior 2: Camera button text is initially "Camera"; label changes to "Recheck permission" when JamWideJuceEditor receives CameraState::Unavailable via FallbackListener::onCameraStateChanged.
    - Behavior 3: Camera button is NEVER disabled based on connection state (D-11: camera is independent of Connect). It is enabled at construction and stays enabled.
    - Behavior 4: Left-click invokes onCameraClicked. Right-click shows a juce::PopupMenu with three items "Low (320x240, 10fps)", "Medium (640x480, 15fps)", "High (1280x720, 30fps)"; the currently-active preset is rendered as checked. Selection invokes onCameraQualitySelected(int preset_index 0/1/2).
    - Behavior 5: In resized(), the Camera button sits at rightX-60, y, 60, h (60px width to fit the wider "Recheck permission" label without truncation), then rightX -= 60 + gap so the Video button still draws to the right.
    - Behavior 6: JamWideJuceEditor's onCameraClicked lambda calls processorRef.getNativeCamera()->toggle(). Editor also implements jamwide::JamWideCameraDevice::FallbackListener and registers itself via setFallbackListener.
  </behavior>
  <action>
    Edit 1 — juce/ui/ConnectionBar.h: in the public section near line 33, declare `std::function<void()> onCameraClicked;` and `std::function<void(int)> onCameraQualitySelected;`. Near line 36, declare `void setCameraActive(bool active);` and `void setCameraLabel(const juce::String& label);`. Near line 72 (next to videoButton), declare `juce::TextButton cameraButton;`. Also store a private `int currentCameraQualityPreset_{1};` for the menu checkmark state, with a public setter `void setCameraQualityPreset(int preset);`.

    Edit 2 — juce/ui/ConnectionBar.cpp constructor: immediately after the existing addAndMakeVisible(videoButton) call (line 217), add identical-shape setup for cameraButton: setButtonText("Camera"), set colours matching videoButton offline palette, setTooltip("Toggle camera preview (v1.3 beta)") per D-26, DO NOT setEnabled(false) per D-11, set onClick lambda that fires onCameraClicked, install a right-click handler. The right-click handler is a small file-local subclass `class CameraButton : public juce::TextButton` whose mouseDown override checks e.mods.isPopupMenu() and shows juce::PopupMenu with the three preset items per RESEARCH §6 lines 565-580. The menu uses currentCameraQualityPreset_ for the checkmark; selection invokes onCameraQualitySelected(selectedIdx - 1). Then addAndMakeVisible(cameraButton).

    Edit 3 — juce/ui/ConnectionBar.cpp resized() (around line 300): immediately after `videoButton.setBounds(rightX - 54, y, 54, h); rightX -= 54 + gap;`, add `cameraButton.setBounds(rightX - 60, y, 60, h); rightX -= 60 + gap;` so Camera draws to the LEFT of Video.

    Edit 4 — juce/ui/ConnectionBar.cpp setCameraActive/setCameraLabel/setCameraQualityPreset implementations: mirror the setVideoActive color-flipping pattern at line 644. setCameraActive(true) sets the cameraButton textColourOffId to kAccentConnect (green=on); setCameraActive(false) reverts. setCameraLabel(s) calls cameraButton.setButtonText(s) and cameraButton.repaint(). setCameraQualityPreset(p) stores into currentCameraQualityPreset_.

    Edit 5 — juce/JamWideJuceEditor.h: change class declaration to `class JamWideJuceEditor : public juce::AudioProcessorEditor, public jamwide::JamWideCameraDevice::FallbackListener` (keep any other existing base classes). Declare the two overrides: `void onCameraFallback(jamwide::CameraFallbackCause cause) override;` and `void onCameraStateChanged(jamwide::CameraState newState) override;`. Add `#include "video/native/JamWideCameraDevice.h"` and `#include "video/native/CameraFallbackCause.h"`. Also declare `std::unique_ptr<jamwide::CameraPreviewWindow> previewWindow_;` (Task 2 creates the class; this header forward-declares it via `namespace jamwide { class CameraPreviewWindow; }`).

    Edit 6 — juce/JamWideJuceEditor.cpp around line 144 (after existing onVideoClicked lambda): add `connectionBar.onCameraClicked = [this]() { auto* cam = processorRef.getNativeCamera(); if (cam) cam->toggle(); };` and `connectionBar.onCameraQualitySelected = [this](int preset) { auto* cam = processorRef.getNativeCamera(); if (cam) cam->setQualityPreset(preset); connectionBar.setCameraQualityPreset(preset); processorRef.setCameraQualityPreset(preset); };`. Also call `if (auto* cam = processorRef.getNativeCamera()) cam->setFallbackListener(this);` to register the editor as the FallbackListener.

    Edit 7 — juce/JamWideJuceEditor.cpp: implement onCameraStateChanged at file scope: switch on newState; if Unavailable then `connectionBar.setCameraLabel("Recheck permission");` and `connectionBar.setCameraActive(false);`; otherwise `connectionBar.setCameraLabel("Camera");` and `connectionBar.setCameraActive(newState == jamwide::CameraState::Capturing);`. Task 2 will extend this method to drive previewWindow_ visibility. Implement onCameraFallback as an empty stub with `juce::ignoreUnused(cause);` and a comment noting "19-03 wires CameraStatusDialog here".

    Edit 8 — JamWideJuceEditor destructor: call `if (auto* cam = processorRef.getNativeCamera()) cam->setFallbackListener(nullptr);` to unregister before the editor is destroyed (the processor outlives the editor, so the camera could try to invoke this->onCameraFallback after the editor is gone — unregister to avoid UAF).
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target JamWideJuce_Standalone 2>&amp;1 | tail -15; grep -c "juce::TextButton cameraButton" juce/ui/ConnectionBar.h; grep -c "onCameraClicked" juce/ui/ConnectionBar.h; grep -c "onCameraQualitySelected" juce/ui/ConnectionBar.h; grep -c "cameraButton.setButtonText" juce/ui/ConnectionBar.cpp; grep -c "cameraButton.setBounds" juce/ui/ConnectionBar.cpp; grep -c "connectionBar.onCameraClicked" juce/JamWideJuceEditor.cpp; grep -c "getNativeCamera" juce/JamWideJuceEditor.cpp; grep -c "public jamwide::JamWideCameraDevice::FallbackListener" juce/JamWideJuceEditor.h</automated>
  </verify>
  <done>
    JamWideJuce_Standalone builds; ConnectionBar exposes cameraButton + four callbacks/setters; JamWideJuceEditor wires the lambda chain and is registered as a FallbackListener; right-click on cameraButton in the running standalone shows the Low/Medium/High PopupMenu (verifiable on launch).
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: CameraPreviewWindow + Tile with JamWideLookAndFeel chrome (D-05, D-07, D-08, D-09)</name>
  <files>
    juce/video/native/CameraPreviewWindow.h,
    juce/video/native/CameraPreviewWindow.cpp,
    juce/video/native/CameraPreviewTile.h,
    juce/video/native/CameraPreviewTile.cpp,
    juce/JamWideJuceEditor.cpp,
    CMakeLists.txt
  </files>
  <read_first>
    - libs/juce/modules/juce_gui_basics/windows/juce_DocumentWindow.h (skim closeButtonPressed override, setUsingNativeTitleBar, ComponentBoundsConstrainer::setFixedAspectRatio)
    - juce/ui/JamWideLookAndFeel.h (lines 12-26 — colour constants)
    - juce/video/VideoPrivacyDialog.h (existing dialog component pattern; new dialogs mirror its shape)
    - juce/video/native/JamWideFrameDistributor.h (Subscriber interface to implement in the Tile)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §6 lines 584-596 (popout chrome implementation notes)
  </read_first>
  <behavior>
    - Behavior 1: CameraPreviewWindow extends juce::DocumentWindow with setUsingNativeTitleBar(false) so JamWideLookAndFeel paints the custom title bar (D-08).
    - Behavior 2: Window is setResizable(true, true) with ComponentBoundsConstrainer setFixedAspectRatio(4.0/3.0) — D-07.
    - Behavior 3: Default size 320x240; min 240x180; max bounded by current display rectangle.
    - Behavior 4: Window starts hidden setVisible(false); becomes visible when onCameraStateChanged(Capturing) fires.
    - Behavior 5: Clicking the X HIDES the window without destroying it and without signaling capture to stop — D-09 orthogonality.
    - Behavior 6: Title bar reads "JamWide — Camera: <device name>" with deviceName from nativeCamera->getDeviceName() (RESEARCH §1 line 167).
    - Behavior 7: CameraPreviewTile extends juce::Component AND implements JamWideFrameDistributor::Subscriber. onFrame copies the juce::Image (ref-counted, cheap) into currentFrame_ under a small mutex; schedules MessageManager::callAsync to repaint().
    - Behavior 8: CameraPreviewTile registers as a subscriber in its constructor; unregisters in its destructor (RESEARCH §4 line 458 lifetime contract).
    - Behavior 9: Window bounds change events fire a callback so the editor publishes the new bounds to the processor for state persistence.
  </behavior>
  <action>
    Edit 1 — Create juce/video/native/CameraPreviewTile.h + .cpp. The class is `class CameraPreviewTile : public juce::Component, public jamwide::JamWideFrameDistributor::Subscriber`. Constructor takes a `jamwide::JamWideFrameDistributor&` reference and stores it; calls distributor.registerSubscriber(this). Destructor calls distributor_.unregisterSubscriber(this). Member `juce::Image currentFrame_` guarded by `std::mutex frameMutex_`. Override `void onFrame(const juce::Image& img) override` — under frameMutex_, assign currentFrame_=img; then juce::MessageManager::callAsync to schedule repaint(). Override `void paint(juce::Graphics& g) override` — fill background with juce::Colour(JamWideLookAndFeel::kSurfaceStrip); under frameMutex_, copy currentFrame_ to a local; unlock; if local.isValid() then g.drawImage(local, getLocalBounds().toFloat(), juce::RectanglePlacement::centred).

    Edit 2 — Create juce/video/native/CameraPreviewWindow.h + .cpp. The class is `class CameraPreviewWindow : public juce::DocumentWindow`. Constructor takes `jamwide::JamWideFrameDistributor& distributor`, the JUCE LookAndFeel pointer for the editor's existing JamWideLookAndFeel instance, and the initial bounds. Construct base with `juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton` (no maximise per RESEARCH §6 line 588). Call setUsingNativeTitleBar(false). Construct a CameraPreviewTile via setContentOwned(new CameraPreviewTile(distributor), true). Call setResizable(true, true); getConstrainer()->setFixedAspectRatio(4.0 / 3.0); getConstrainer()->setSizeLimits(240, 180, 2560, 1920); setBounds(initialBounds). Override closeButtonPressed() to call setVisible(false) and invoke `if (onCloseRequested) onCloseRequested();` (the callback is informational only — D-09: capture continues). Override `void componentMovedOrResized(juce::Component& which, bool wasMoved, bool wasResized) override` (inherited from juce::ComponentListener) to invoke `if (onBoundsChanged) onBoundsChanged(getBounds());` so the editor can persist. Public members: `std::function<void()> onCloseRequested`, `std::function<void(juce::Rectangle<int>)> onBoundsChanged`. Public setter: `void setDeviceName(const juce::String& name)` calls setName("JamWide — Camera: " + name).

    Edit 3 — Update juce/JamWideJuceEditor.h: confirm forward declaration `namespace jamwide { class CameraPreviewWindow; }` (already added in Task 1). Member `std::unique_ptr<jamwide::CameraPreviewWindow> previewWindow_` already declared. Add `#include "video/native/CameraPreviewWindow.h"` to the .cpp file (not the .h, to avoid header bloat).

    Edit 4 — juce/JamWideJuceEditor.cpp constructor after the camera setFallbackListener call: construct previewWindow_ via `previewWindow_ = std::make_unique<jamwide::CameraPreviewWindow>(*processorRef.getFrameDistributor(), &lookAndFeel, processorRef.getCameraPopoutBounds());` (the editor's JamWideLookAndFeel instance is at `lookAndFeel`; if it lives under a different name, use whatever the existing editor exposes). Set callbacks: `previewWindow_->onCloseRequested = [this]() { /* D-09 — no-op; capture continues */ };` and `previewWindow_->onBoundsChanged = [this](juce::Rectangle<int> r) { processorRef.setCameraPopoutBounds(r); };`.

    Edit 5 — Extend juce/JamWideJuceEditor.cpp onCameraStateChanged (added in Task 1): inside the existing implementation, append:
      `if (previewWindow_) {`
      `    if (newState == jamwide::CameraState::Capturing) {`
      `        previewWindow_->setDeviceName(processorRef.getNativeCamera()->getDeviceName());`
      `        previewWindow_->setVisible(true);`
      `    } else if (newState == jamwide::CameraState::Idle || newState == jamwide::CameraState::Unavailable) {`
      `        previewWindow_->setVisible(false);`
      `    }`
      `    // Other states (Opening, Retrying, Paused, Failed): leave previewWindow_ visibility as-is per D-09 orthogonality`
      `}`

    Edit 6 — JamWideJuceProcessor.h: add public accessor `jamwide::JamWideFrameDistributor* getFrameDistributor() { return frameDistributor.get(); }`. Also add `juce::Rectangle<int> getCameraPopoutBounds() const;` and `void setCameraPopoutBounds(juce::Rectangle<int>);` — backed by a `std::atomic` quartet or a single `juce::Rectangle<int>` member guarded by a small mutex (use mutex for simplicity since this is message-thread only). Initial value comes from D-25 defaults (100,100,320,240) or from setStateInformation if applicable. Task 3 plumbs the state save/load.

    Edit 7 — CMakeLists.txt: in the JamWideJuce target_sources block (where 19-01 added JamWideCameraDevice.cpp and JamWideFrameDistributor.cpp), append `juce/video/native/CameraPreviewWindow.cpp` and `juce/video/native/CameraPreviewTile.cpp`. Confirm juce::juce_gui_basics is already linked (it is — JamWide already uses DocumentWindow elsewhere).

    No automated test for visual UI components — verification is build success plus the manual UAT cells in 19-03. The verify command checks file existence and key wiring grep.
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target JamWideJuce_Standalone 2>&amp;1 | tail -15; test -f juce/video/native/CameraPreviewWindow.h; test -f juce/video/native/CameraPreviewWindow.cpp; test -f juce/video/native/CameraPreviewTile.h; test -f juce/video/native/CameraPreviewTile.cpp; grep -c "class CameraPreviewWindow.*juce::DocumentWindow" juce/video/native/CameraPreviewWindow.h; grep -c "setFixedAspectRatio" juce/video/native/CameraPreviewWindow.cpp; grep -c "setUsingNativeTitleBar(false)" juce/video/native/CameraPreviewWindow.cpp; grep -c "registerSubscriber" juce/video/native/CameraPreviewTile.cpp; grep -c "unregisterSubscriber" juce/video/native/CameraPreviewTile.cpp; grep -c "previewWindow_ = std::make_unique" juce/JamWideJuceEditor.cpp; grep -c "CameraPreviewWindow.cpp" CMakeLists.txt</automated>
  </verify>
  <done>
    JamWideJuce_Standalone builds; CameraPreviewWindow + CameraPreviewTile exist with the documented signatures; editor constructs previewWindow_; CMakeLists references the two new .cpp files. Launching standalone, clicking Camera button after granting permission shows the popout with a live preview.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 3: NativeCameraPrivacyDialog + plugin state v3 to v4 schema migration (D-22, D-24, D-25)</name>
  <files>
    juce/video/native/NativeCameraPrivacyDialog.h,
    juce/video/native/NativeCameraPrivacyDialog.cpp,
    juce/JamWideJuceProcessor.h,
    juce/JamWideJuceProcessor.cpp,
    juce/JamWideJuceEditor.cpp,
    tests/test_plugin_state_v3_v4.cpp,
    CMakeLists.txt
  </files>
  <read_first>
    - juce/video/VideoPrivacyDialog.h + .cpp (existing dialog component — the new NativeCameraPrivacyDialog mirrors its component shape but with different copy and different ack-storage)
    - juce/JamWideJuceProcessor.cpp lines 622-790 (getStateInformation + setStateInformation full bodies — see the OSC v1->v2 migration pattern with tree.getProperty(key, default); the camera fields follow Approach A flat properties per RESEARCH §8 lines 711-733)
    - juce/JamWideJuceProcessor.h line 88 (currentStateVersion = 3 — bump to 4)
    - .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §8 (full plugin state schema migration), §11 (test harness inventory)
    - tests/test_rawdata_send.cpp (test harness pattern — main()-style, assert() based)
  </read_first>
  <behavior>
    - Behavior 1: NativeCameraPrivacyDialog is a small class with a `void show(std::function<void(bool)> onAck)` method that displays a juce::AlertWindow with the D-22 copy and an "I understand" button. On click, invokes the callback with true; on dismiss, invokes with false. Distinct class from juce/video/VideoPrivacyDialog (which covers VDO.Ninja IP-exposure).
    - Behavior 2: The dialog is shown FROM the editor's onCameraClicked lambda ONLY when (a) cameraPrivacyAck is currently false AND (b) the camera is transitioning Idle -> Opening with Authorized status. After the user clicks "I understand", the editor calls processorRef.setCameraPrivacyAck(true) to persist the bool.
    - Behavior 3: JamWideJuceProcessor::currentStateVersion bumps from 3 to 4.
    - Behavior 4: getStateInformation writes seven new flat ValueTree properties: cameraPopoutX, cameraPopoutY, cameraPopoutWidth, cameraPopoutHeight, cameraQualityPreset (int), cameraPrivacyAck (bool), cameraSelectedDevice (string). Follows the OSC v2 flat-property pattern at JamWideJuceProcessor.cpp lines 653-657.
    - Behavior 5: setStateInformation reads each new property via tree.getProperty(key, default) with D-25 defaults. Clamps qualityPreset to [0,2] via juce::jlimit. Clamps popoutWidth/Height to [240, 2560] / [180, 1920]. Caps selectedDevice length at 256 chars. Casts privacyAck to bool. Applies values to nativeCamera and processor state.
    - Behavior 6: A saved v3 state loaded by a v4 binary: missing camera fields are read with defaults; no crash, no data loss for existing v3 fields.
    - Behavior 7: A saved v4 state saved and reloaded by the same v4 binary: all seven camera fields round-trip exactly.
  </behavior>
  <action>
    Edit 1 — Create juce/video/native/NativeCameraPrivacyDialog.h + .cpp. Header declares `class NativeCameraPrivacyDialog`. The class has a single method `void show(std::function<void(bool)> onAck)` which constructs a juce::MessageBoxOptions with:
      title: "Camera privacy notice"
      icon: juce::MessageBoxIconType::InfoIcon
      message: "JamWide broadcasts your camera to the NINJAM server and peers in your room. Peers can save or redistribute their view. There's no separate IP exposure beyond what audio already does." (verbatim from D-22 + CONTEXT specifics line 196)
      Button 1: "I understand"
      Button 2: "Cancel"
    Use juce::AlertWindow::showAsync (mirroring RESEARCH §9 lines 822-832) with a callback that invokes onAck(buttonChosen == 0). DO NOT use a synchronous show — would block the message thread.

    Edit 2 — juce/JamWideJuceProcessor.h: change `static constexpr int currentStateVersion = 3;` (line 88) to `static constexpr int currentStateVersion = 4;` with comment `// v4: added camera subtree (Phase 19; D-24)`. Add public accessors: `int getCameraQualityPreset() const`, `void setCameraQualityPreset(int)`, `bool getCameraPrivacyAck() const`, `void setCameraPrivacyAck(bool)`, `juce::String getCameraSelectedDevice() const`, `void setCameraSelectedDevice(const juce::String and)`. Also already declared in Task 2: getCameraPopoutBounds and setCameraPopoutBounds. Backing storage: `std::atomic<int> cameraQualityPreset_{1}`, `std::atomic<bool> cameraPrivacyAck_{false}`, `juce::String cameraSelectedDevice_` (with std::mutex for string access), `juce::Rectangle<int> cameraPopoutBounds_{100,100,320,240}` (with std::mutex).

    Edit 3 — juce/JamWideJuceProcessor.cpp getStateInformation: after the existing OSC and MIDI saves (around line 661), add SEVEN flat property writes using Approach A pattern:
      `auto popoutB = getCameraPopoutBounds();`
      `state.setProperty("cameraPopoutX", popoutB.getX(), nullptr);`
      `state.setProperty("cameraPopoutY", popoutB.getY(), nullptr);`
      `state.setProperty("cameraPopoutWidth", popoutB.getWidth(), nullptr);`
      `state.setProperty("cameraPopoutHeight", popoutB.getHeight(), nullptr);`
      `state.setProperty("cameraQualityPreset", cameraQualityPreset_.load(), nullptr);`
      `state.setProperty("cameraPrivacyAck", cameraPrivacyAck_.load(), nullptr);`
      `state.setProperty("cameraSelectedDevice", getCameraSelectedDevice(), nullptr);`

    Edit 4 — juce/JamWideJuceProcessor.cpp setStateInformation: after the existing v1->v2/v3 reads (around line 760 or wherever the OSC reads land), add SEVEN flat property reads with defaults + clamping:
      `int popX = juce::jlimit(-10000, 10000, (int)tree.getProperty("cameraPopoutX", 100));`
      `int popY = juce::jlimit(-10000, 10000, (int)tree.getProperty("cameraPopoutY", 100));`
      `int popW = juce::jlimit(240, 2560, (int)tree.getProperty("cameraPopoutWidth", 320));`
      `int popH = juce::jlimit(180, 1920, (int)tree.getProperty("cameraPopoutHeight", 240));`
      `setCameraPopoutBounds(juce::Rectangle<int>(popX, popY, popW, popH));`
      `int qp = juce::jlimit(0, 2, (int)tree.getProperty("cameraQualityPreset", 1));`
      `setCameraQualityPreset(qp); if (nativeCamera) nativeCamera->setQualityPreset(qp);`
      `setCameraPrivacyAck((bool)tree.getProperty("cameraPrivacyAck", false));`
      `juce::String sel = tree.getProperty("cameraSelectedDevice", "").toString();`
      `if (sel.length() > 256) sel = sel.substring(0, 256);`
      `setCameraSelectedDevice(sel);`
    Per T-19-03 mitigation, the explicit clamping is the threat-model defense against malformed XML.

    Edit 5 — juce/JamWideJuceEditor.cpp: extend the onCameraClicked lambda from Task 1 to gate the privacy dialog. After the existing `auto* cam = processorRef.getNativeCamera(); if (cam) cam->toggle();`, wrap with a privacy check:
      `if (cam->getState() == jamwide::CameraState::Idle && !processorRef.getCameraPrivacyAck()) {`
      `    // First click after auth; show privacy dialog BEFORE toggling`
      `    auto auth = jamwide::queryCameraAuthorization();`
      `    if (auth == jamwide::CameraAuthStatus::Authorized) {`
      `        nativeCameraPrivacyDialog_.show([this, cam](bool ack) {`
      `            if (ack) { processorRef.setCameraPrivacyAck(true); cam->toggle(); }`
      `        });`
      `        return;  // dialog completion drives toggle`
      `    }`
      `}`
      `cam->toggle();  // pre-auth or already-acked path`
    Declare `jamwide::NativeCameraPrivacyDialog nativeCameraPrivacyDialog_` as a member of the editor (mirror the existing `videoPrivacyDialog` member used by onVideoClicked).

    Edit 6 — CMakeLists.txt: append `juce/video/native/NativeCameraPrivacyDialog.cpp` to the JamWideJuce target_sources block.

    Edit 7 — Create tests/test_plugin_state_v3_v4.cpp. Pure-C++ test using juce::ValueTree directly (no need to instantiate the full processor; the test exercises the serialization shape).
      Test 1 (v3 -> v4 default migration): construct a ValueTree mimicking a v3 save (no camera properties, stateVersion=3). Apply v4 read logic (replicate the seven getProperty calls with defaults inline in the test, since the test does not link the full processor — OR link against JamWideJuce and call setStateInformation directly through a test-only accessor). Assert: cameraQualityPreset reads as 1, cameraPrivacyAck reads as false, cameraSelectedDevice reads as empty, cameraPopout bounds read as (100,100,320,240).
      Test 2 (v4 round-trip): construct a v4 ValueTree with explicit camera properties (qualityPreset=2, privacyAck=true, selectedDevice="MyCam", popout=(50,75,640,480)); serialize to XML; parse back; apply read logic; assert exact match.
      Test 3 (clamping): construct a malicious v4 ValueTree with cameraQualityPreset=99, cameraPopoutWidth=-5, cameraSelectedDevice = 1000-char string. After load, assert qualityPreset clamped to 2, popoutWidth clamped to 240, selectedDevice trimmed to 256.
      Use the assert()/exit(1) pattern from tests/test_rawdata_send.cpp. The test executable was pre-staged in CMakeLists.txt by 19-01 Task 1; this task fills in the implementation. Recommendation: keep the test pure-C++ (no JamWideJuce link); duplicate the relevant read logic inline. This avoids linking the full JUCE plugin into the test executable (faster CI; matches the pattern of test_rawdata_send.cpp keeping pure-C++ scope).
  </action>
  <verify>
    <automated>cmake --build build-juce-19-test --target test_plugin_state_v3_v4 JamWideJuce 2>&amp;1 | tail -20; cd build-juce-19-test; ctest -R plugin_state_v3_v4 --output-on-failure; cd ..; grep -c "currentStateVersion = 4" juce/JamWideJuceProcessor.h; grep -c "cameraPopoutX" juce/JamWideJuceProcessor.cpp; grep -c "cameraPrivacyAck" juce/JamWideJuceProcessor.cpp; grep -c "cameraQualityPreset" juce/JamWideJuceProcessor.cpp; grep -c "juce::jlimit(0, 2" juce/JamWideJuceProcessor.cpp; test -f juce/video/native/NativeCameraPrivacyDialog.h; grep -c "nativeCameraPrivacyDialog_" juce/JamWideJuceEditor.cpp</automated>
  </verify>
  <done>
    test_plugin_state_v3_v4 exits 0 with all 3 scenarios passing; currentStateVersion=4 in processor.h; the seven camera flat properties land in save/load with clamping; NativeCameraPrivacyDialog exists and is invoked from the editor at the "first-click after auth, not yet acked" gate.
  </done>
</task>

</tasks>

<verification>

## Plan-Level Verification

```bash
# 1. Plugin builds cleanly with full UI surface
cmake --build build-juce-19-02 --target JamWideJuce_Standalone JamWideJuce_VST3 JamWideJuce_AU 2>&1 | tail -20

# 2. Camera button visible + at the correct position
# Manual: launch standalone, observe Camera button next to (left of) Video button
# Automated: grep wires
grep -c 'cameraButton.setButtonText' juce/ui/ConnectionBar.cpp   # >= 1
grep -c 'cameraButton.setBounds' juce/ui/ConnectionBar.cpp       # >= 1
grep -c 'onCameraClicked' juce/JamWideJuceEditor.cpp             # >= 1
grep -c 'getNativeCamera' juce/JamWideJuceEditor.cpp             # >= 1

# 3. Popout window class exists with correct chrome
grep -c 'class CameraPreviewWindow.*juce::DocumentWindow' juce/video/native/CameraPreviewWindow.h
grep -c 'setFixedAspectRatio' juce/video/native/CameraPreviewWindow.cpp
grep -c 'setUsingNativeTitleBar(false)' juce/video/native/CameraPreviewWindow.cpp

# 4. State schema bumped, camera properties land, test green
grep -c 'currentStateVersion = 4' juce/JamWideJuceProcessor.h
cd build-juce-19-02 && ctest -R plugin_state_v3_v4 --output-on-failure

# 5. NativeCameraPrivacyDialog exists and is invoked from the editor
test -f juce/video/native/NativeCameraPrivacyDialog.h
grep -c 'nativeCameraPrivacyDialog_.show' juce/JamWideJuceEditor.cpp
```

</verification>

<success_criteria>

This plan succeeds when:

1. **Camera button visible** — ConnectionBar shows a "Camera" button to the left of the existing "Video" button (D-06).
2. **Left-click toggles camera** — onCameraClicked lambda invokes nativeCamera->toggle(); state transitions are visible via the FallbackListener (D-09/D-12).
3. **Right-click shows quality menu** — PopupMenu with Low/Medium/High; selection persists across plugin reload (D-19, persisted via D-25).
4. **Popout renders preview** — When camera reaches Capturing, the juce::DocumentWindow popout appears with 4:3 aspect, JamWideLookAndFeel chrome, and live camera frames via CameraPreviewTile (D-05/07/08, CAM-03).
5. **Popout close is non-destructive** — Clicking the popout's X button hides the window but the camera stays Capturing per D-09 orthogonality.
6. **Privacy dialog on first authorized click** — NativeCameraPrivacyDialog shown ONCE per plugin install at the "first toggle after grant, not yet acked" gate; "I understand" persists cameraPrivacyAck=true (D-22).
7. **State schema v3 -> v4** — currentStateVersion bumped; seven new flat properties added to ValueTree (popoutX/Y/W/H, qualityPreset, privacyAck, selectedDevice); test_plugin_state_v3_v4 exits 0; clamping defense against malformed XML works.
8. **Persistence round-trip** — Popout bounds + quality preset survive plugin reload (verifiable: change preset, close DAW, reopen, observe preset retained).

</success_criteria>

<output>
Create `.planning/phases/19-camera-capture-permission-ux/19-02-SUMMARY.md` summarizing:
- ConnectionBar Camera button wiring (line numbers in ConnectionBar.cpp for the cameraButton.setBounds insertion)
- File-local CameraButton subclass for right-click handling (or alternative if a different pattern was used)
- CameraPreviewWindow chrome details (final size limits, aspect ratio constraint, whether setUsingNativeTitleBar was honored on macOS — sometimes JUCE forces native on Apple)
- Plugin state v3 -> v4 schema (exact property names + defaults + clamping)
- NativeCameraPrivacyDialog gate logic (the editor's check for state==Idle + !privacyAck + auth==Authorized)
- Tests added (1 new file: test_plugin_state_v3_v4.cpp)
- Any deviations from RESEARCH.md §6 file layout or §8 schema approach
</output>
