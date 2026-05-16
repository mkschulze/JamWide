---
phase: 19-camera-capture-permission-ux
reviewed: 2026-05-16T00:00:00Z
depth: standard
files_reviewed: 28
files_reviewed_list:
  - docs/UAT/phase-19-camera-uat-checklist.md
  - juce/JamWideJuceEditor.cpp
  - juce/JamWideJuceEditor.h
  - juce/JamWideJuceProcessor.cpp
  - juce/JamWideJuceProcessor.h
  - juce/ui/ConnectionBar.h
  - juce/video/native/CameraAuthorization.h
  - juce/video/native/CameraAuthorization_mac.mm
  - juce/video/native/CameraAuthorization_windows.cpp
  - juce/video/native/CameraFallbackCause.h
  - juce/video/native/CameraPreviewTile.cpp
  - juce/video/native/CameraPreviewTile.h
  - juce/video/native/CameraPreviewWindow.h
  - juce/video/native/CameraPreviewWindow.cpp
  - juce/video/native/CameraStateMachine.cpp
  - juce/video/native/CameraStateMachine.h
  - juce/video/native/CameraStatusDialog.cpp
  - juce/video/native/CameraStatusDialog.h
  - juce/video/native/JamWideCameraDevice.cpp
  - juce/video/native/JamWideCameraDevice.h
  - juce/video/native/JamWideFrameDistributor.cpp
  - juce/video/native/JamWideFrameDistributor.h
  - juce/video/native/NativeCameraPrivacyDialog.cpp
  - juce/video/native/NativeCameraPrivacyDialog.h
  - scripts/verify_camera_entitlement.sh
  - tests/test_camera_cause_mapping.cpp
  - tests/test_camera_frame_stall.cpp
  - tests/test_camera_retry_backoff.cpp
  - tests/test_camera_state_machine.cpp
  - tests/test_frame_distributor.cpp
  - tests/test_frame_distributor_lifetime.cpp
  - tests/test_plugin_state_v3_v4.cpp
findings:
  critical: 4
  warning: 7
  info: 5
  total: 16
status: issues_found
---

# Phase 19: Code Review Report

**Reviewed:** 2026-05-16
**Depth:** standard
**Files Reviewed:** 28 (production: 22, tests: 7, script: 1, UAT doc: 1; note `files_reviewed_list` includes `CameraPreviewWindow.cpp` which was loaded for cross-reference)
**Status:** issues_found

## Summary

Phase 19 lands a substantial native-camera pipeline that is generally well-architected. The HIGH-2/HIGH-3/HIGH-4 mitigations (frame-distributor Subscription RAII, generation-token guards inside `JamWideCameraDevice`, AsyncUpdater in `CameraPreviewTile`) are implemented and correctly thought through. The state machine, retry-backoff helper, and v3→v4 schema migration are clean and well-tested.

The **central problem this review surfaces is that the lifetime-safety discipline that codex enforced inside `JamWideCameraDevice` (HIGH-3 generation tokens) is *not* extended to the JUCE message-thread async callsites that the editor owns**. Four async lambdas in `JamWideJuceEditor` capture `this` and can outlive the editor: the TCC completion handler (HIGH-5 path), the privacy dialog ack, the fallback dialog button result, and the editor's own `requestCameraAuthorization` marshaling lambda. None of these use `juce::Component::SafePointer` or `juce::WeakReference`. The dialogs are NOT modal to the editor (they are top-level `juce::AlertWindow::showAsync` windows that survive editor destruction). The TCC system prompt is similarly not bound to the plugin window's lifetime. Closing the plugin while any of these prompts is up and then responding yields a UAF on the captured `this`.

A secondary but real defect lives in `JamWideCameraDevice::onOpenResult`: a stale callback for an open that arrived *after* the state machine moved to Unavailable (via AuthDenied during Opening) will silently re-open the camera and re-attach the listener, leaving the device hot while the UI thinks it is denied.

The shell script `verify_camera_entitlement.sh` and the entitlement plumbing are sound; the state-machine and frame-distributor tests are thorough.

## Critical Issues

### CR-01: Editor captures `this` in `juce::AlertWindow::showAsync` callbacks that outlive the editor (UAF on plugin-window close)

**File:** `juce/JamWideJuceEditor.cpp:832-863` (`onCameraFallback`)
**Issue:** `cameraStatusDialog_.show(...)` is invoked with a result-lambda that captures `[this]`. The underlying `juce::AlertWindow::showAsync` (in `CameraStatusDialog::show`, `CameraStatusDialog.cpp:192`) is a *top-level* asynchronous alert — it is not parented to the editor and is not auto-dismissed when the editor is destroyed by the host. If the user closes the plugin window while the fallback dialog is open and then clicks one of its buttons, the editor's destructor has already run; the lambda fires with a dangling `this`, dereferences `processorRef` (a member reference), and the subsequent `connectionBar.grabKeyboardFocus()` or `cam->recheckPermission()` is reached through freed memory. The HIGH-3 generation-token discipline that the device uses internally is not present here.

Cross-reference: no `juce::Component::SafePointer<JamWideJuceEditor>` or `juce::WeakReference` is used in `JamWideJuceEditor`, `CameraStatusDialog`, or `NativeCameraPrivacyDialog` — confirmed by grep.

**Fix:**
```cpp
// Approach A — SafePointer-guarded:
juce::Component::SafePointer<JamWideJuceEditor> self(this);
cameraStatusDialog_.show(cause, hostName,
    [self, &proc = processorRef](jamwide::CameraStatusDialog::Action action) {
        if (! self) return;                       // editor gone — bail
        auto* cam = proc.getNativeCamera();
        // … existing switch …
    });

// Approach B — capture only what survives the editor:
auto* cam = processorRef.getNativeCamera();       // owned by processor, lives longer
cameraStatusDialog_.show(cause, hostName,
    [cam](jamwide::CameraStatusDialog::Action action) {
        switch (action) {
            case jamwide::CameraStatusDialog::Action::OpenSystemSettings:
                #if JUCE_MAC
                  juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Camera").launchInDefaultBrowser();
                #elif JUCE_WINDOWS
                  juce::URL("ms-settings:privacy-webcam").launchInDefaultBrowser();
                #endif
                return;
            case jamwide::CameraStatusDialog::Action::RecheckPermission:
                if (cam) cam->recheckPermission();
                return;
            case jamwide::CameraStatusDialog::Action::Dismiss:
                return;                            // drop the grabKeyboardFocus side-effect
        }
    });
```
The `grabKeyboardFocus()` side-effect needs `this`; if it must stay, only Approach A works.

---

### CR-02: Privacy dialog ack-lambda captures `this`; same UAF shape as CR-01

**File:** `juce/JamWideJuceEditor.cpp:931-942` (`showPrivacyOrToggle`)
**Issue:** `privacyDialog_->show([this, cam](bool acknowledged) { processorRef.setCameraPrivacyAck(true); cam->toggle(); });`. The `NativeCameraPrivacyDialog::show` (`NativeCameraPrivacyDialog.cpp:24-27`) wraps a `juce::AlertWindow::showAsync` whose result lambda survives editor destruction. `processorRef` is a member reference of the editor — accessing it through a dangling `this` is UB. `cam` is captured by value and points to a processor-owned object, so `cam->toggle()` is independently safe, but the `setCameraPrivacyAck` call is the wedge.

Worse, this dialog is the *first* one the user sees after a TCC grant (HIGH-5 path, UAT Cell 10) — a moment when many users will be slow to read and click.

**Fix:**
```cpp
auto& proc = processorRef;     // capture the processor reference directly
privacyDialog_->show(
    [&proc, cam](bool acknowledged) {
        if (! acknowledged) return;
        proc.setCameraPrivacyAck(true);
        cam->toggle();
    });
```
Or `juce::Component::SafePointer<JamWideJuceEditor>` if other editor state must be touched (it isn't, here).

---

### CR-03: TCC completion-handler lambda captures `this` across a system-modal prompt

**File:** `juce/JamWideJuceEditor.cpp:894-911` (`handleCameraIdleClick` NotDetermined branch)
**Issue:** The editor calls `jamwide::requestCameraAuthorization([this](...) { juce::MessageManager::callAsync([this, result](){ … processorRef.getNativeCamera() … }); })`. The macOS TCC prompt is a *system* dialog (not owned by JamWide), so it does NOT close when the plugin window closes. If the user clicks the JamWide Camera button, the TCC prompt appears, the user then closes/reopens the host project (destroying the editor), and finally clicks Allow on the still-open TCC dialog, the outer lambda fires on Apple's "unspecified thread", schedules the inner lambda via `MessageManager::callAsync`, and the inner lambda dereferences a dangling `this` to reach `processorRef`. The corresponding code inside `JamWideCameraDevice::handleUserToggleInternal` (`.cpp:280-298`) is protected by the generation token because the device outlives the editor; this editor copy is not.

This is also redundant with the device's own NotDetermined path: `JamWideCameraDevice::handleUserToggleInternal` already calls `requestCameraAuthorization` when status is NotDetermined. The editor's separate TCC call therefore double-prompts in some edge cases (e.g., NotDetermined at the click moment, user grants between the editor's pre-check and the device's `cam->toggle()`), but the first prompt is the dangerous one.

**Fix:** Move the privacy-modal-vs-toggle dispatch entirely into the device's own `handleAuthResult`, OR capture only the processor pointer:
```cpp
case jamwide::CameraAuthStatus::NotDetermined: {
    auto* proc = &processorRef;
    jamwide::requestCameraAuthorization(
        [proc](jamwide::CameraAuthStatus result) {
            juce::MessageManager::callAsync(
                [proc, result]() {
                    auto* cam2 = proc->getNativeCamera();
                    if (! cam2) return;
                    if (result == jamwide::CameraAuthStatus::Authorized) {
                        // showPrivacyOrToggle can no longer be called without `this`.
                        // Either move the privacy-ack gating into the processor, or
                        // use SafePointer<Editor> and bail when null.
                        if (! proc->getCameraPrivacyAck()) {
                            // ... defer to a processor-owned privacy modal …
                        } else {
                            cam2->toggle();
                        }
                    } else {
                        cam2->toggle();
                    }
                });
        });
    return;
}
```
The cleanest structural fix is to push the entire HIGH-5 privacy-ack-vs-toggle decision into a method on the processor (which outlives every editor); the editor then only wires the user click into a single processor call.

---

### CR-04: Stale `openDeviceAsync` callback re-opens the camera after AuthDenied → state machine reaches "ghost capture"

**File:** `juce/video/native/JamWideCameraDevice.cpp:335-366` (`onOpenResult`)
**Issue:** `scheduleOpenDevice` is invoked when the state machine enters Opening. If the auth-completion handler arrives *while the open is still in flight* and dispatches AuthDenied (Opening → Unavailable + closeHardware), the in-flight `openDeviceAsync` may still complete with a valid `juce::CameraDevice*`. The generation guard at `JamWideCameraDevice.cpp:323-328` catches *shutdown* (generation bumps), but a regular AuthDenied transition does NOT bump generation. So `onOpenResult` runs with state=Unavailable and `dev != nullptr`. The code then unconditionally:

```cpp
juceCamera_.reset(dev);
listenerForwarder_ = std::make_unique<CameraListener>(*this, myGen);
juceCamera_->addListener(listenerForwarder_.get());
juceCamera_->onErrorOccurred = […];
firstFrameSeen_.store(false, …);
auto r = stateMachine_.dispatch(CameraEvent::OpenSucceeded);
actuateDispatchResult(r);
```

The state-machine dispatch for `Unavailable / OpenSucceeded` is a no-op (no case in the switch), so the state remains Unavailable. But the side-effects above have already attached a listener, the camera will start producing frames, and `imageReceived` will publish them to the distributor. The TCC dialog shown to the user says "denied", but the camera is hot and frames are flowing to any subscriber.

Same shape applies to a RuntimeError during Opening that races with a slow open: `closeHardware()` runs, the state moves to Retrying, then the original open completes — still no generation bump.

**Fix:**
```cpp
void JamWideCameraDevice::onOpenResult(juce::CameraDevice* dev,
                                       const juce::String& error,
                                       std::uint64_t myGen) {
    // Reject results whose target state has moved on. Generation alone is not
    // enough — AuthDenied / RuntimeError during Opening do NOT bump generation
    // but DO move the state machine away from Opening.
    if (stateMachine_.getState() != CameraState::Opening) {
        delete dev;                                  // free the orphan device
        return;
    }
    if (dev != nullptr) { /* existing happy path */ }
    else { /* existing fail path */ }
}
```

## Warnings

### WR-01: `JamWideCameraDevice` listener may be invoked after `removeListener` returns (camera-callback thread UAF window)

**File:** `juce/video/native/JamWideCameraDevice.cpp:228-232`, `427-432`
**Issue:** `closeHardware()` calls `juceCamera_->removeListener(listenerForwarder_.get())` then `listenerForwarder_.reset()`. JUCE's `CameraDevice::removeListener` is documented only as taking an internal lock; it does NOT guarantee that any in-flight `imageReceived` call on the camera-callback thread has returned. If `imageReceived` is in progress when `removeListener` runs, `listenerForwarder_.reset()` can free the listener while the camera thread still holds a pointer to it. The `imageReceived` body then dereferences `owner_` (a reference member of the listener), which is UAF.

This is the JamWideFrameDistributor HIGH-2 scenario, except the protection only exists *downstream* (Subscription RAII for the distributor's subscribers). The upstream listener has no equivalent guard.

In practice the window is narrow (single-image dispatch is short), but it's the same shape as the codex HIGH-2 finding and deserves the same defensive treatment.

**Fix:** Add a similar "unregister-and-wait" inside the listener forwarder, or use a `std::atomic<bool>` flag that the listener checks on entry and a `std::condition_variable`/`std::shared_mutex` that closeHardware waits on. Cheapest option: wrap the listener body in a `std::shared_mutex` reader-lock and have `closeHardware` take the writer-lock before resetting.

---

### WR-02: Watchdog `juce::Timer` is destroyed from within its own `timerCallback` — JUCE footgun

**File:** `juce/video/native/JamWideCameraDevice.cpp:438-440` (in `actuateDispatchResult`), called transitively from `FirstFrameWatchdog::timerCallback` (`.cpp:64-69`) and `FrameStallWatchdog::timerCallback` (`.cpp:83-99`)
**Issue:** Each watchdog's `timerCallback` synchronously dispatches into `JamWideCameraDevice::onFirstFrameWatchdogFired` / `onFrameStallTick`, which dispatches into the state machine, which produces a `DispatchResult` with `stopFirstFrameWatchdog=true` (or `stopFrameStallWatchdog=true`), which causes `actuateDispatchResult` to call `firstFrameWatchdog_.reset()` (or `frameStallWatchdog_.reset()`). At that point we are executing INSIDE the timer callback, and `unique_ptr::reset` invokes `~Timer()` on the same object. JUCE's documentation specifically cautions against this pattern; the safer idiom is `stopTimer()` from within the callback and `reset()` from a deferred `MessageManager::callAsync`.

In current JUCE this usually works because the callback itself defensively calls `stopTimer()` at the top (`FirstFrameWatchdog.cpp:65`, and the FrameStallWatchdog calls it at `:86` and `:96`). But it remains a sharp edge: any future maintenance that removes the early `stopTimer()` re-opens the hazard, and on some JUCE versions/timer-thread implementations a callback dispatcher can re-enter list iteration after the callback returns.

**Fix:** Defer the destruction:
```cpp
if (result.stopFirstFrameWatchdog) {
    if (firstFrameWatchdog_) firstFrameWatchdog_->stopTimer();
    auto stale = std::move(firstFrameWatchdog_);
    juce::MessageManager::callAsync([stale = std::move(stale)]() mutable {
        stale.reset();                               // run on next message-loop turn
    });
}
```

---

### WR-03: New editor doesn't sync camera-button state when reopened mid-Capturing

**File:** `juce/JamWideJuceEditor.cpp:257-261` (constructor's listener registration)
**Issue:** When the editor is reconstructed (host hides/shows the plugin GUI, or the user reopens it), the `JamWideCameraDevice` may already be in `Capturing` state — the camera is owned by the processor and survives editor destruction (correct design). The new editor registers itself as the FallbackListener, but `onCameraStateChanged` is only invoked on subsequent transitions. The initial UI therefore shows the camera button in its default "Camera / gray" state, even though `getState() == Capturing` and frames are flowing.

The user clicks the button expecting to "start the camera"; the switch in `connectionBar.onCameraClicked` hits `case CameraState::Capturing`, sees that `previewWindow_` is hidden (newly constructed), and pops the window — confusing UX, but at least not destructive. Worse: any code that consumes the active flag (e.g., a future "is camera live" indicator wired to `setCameraActive`) will be stale.

**Fix:** Right after `cam->setFallbackListener(this)`, call the listener once with the current state:
```cpp
if (auto* cam = processorRef.getNativeCamera()) {
    cam->setFallbackListener(this);
    connectionBar.setCameraQualityPreset(cam->getQualityPreset());
    // Initial sync — camera may already be in Capturing / Unavailable from a
    // prior editor's session.
    onCameraStateChanged(cam->getState());
}
```

---

### WR-04: `JamWideFrameDistributor` destructor contract relies on declaration order that isn't enforced in `JamWideJuceProcessor`

**File:** `juce/video/native/JamWideFrameDistributor.cpp:43-52`, `juce/JamWideJuceProcessor.cpp:80-91`
**Issue:** The distributor's destructor asserts `entries_.empty()` and in release silently destroys the entries map regardless. Any outstanding `Subscription` then carries a dangling `owner_` pointer; `~Subscription` will call `unregisterAndWait` on freed memory. The codex HIGH-2 mitigation depends on every `Subscription` being released BEFORE the distributor is destroyed.

The processor destructor (`JamWideJuceProcessor::~JamWideJuceProcessor`) calls `nativeCamera->shutdown()` then `nativeCamera.reset()` then `frameDistributor.reset()`. The order is correct for the *internal* subscribers (none — listener_forwarder uses `addListener`, not `registerSubscriber`). External subscribers come from the editor's `CameraPreviewTile`. JUCE normally destroys the editor before the processor, so this is fine in the common case.

But: if a host crashes or shuts down in an unusual order (processor destroyed while editor still alive), the assertion fires in debug and silently UAFs in release. There is no defensive empty-entries scrub in the distributor dtor; the asymmetry with the careful Subscription wait makes this a latent footgun.

**Fix:** In release, walk `entries_` and null out each `Entry::sub` under the lock so any subsequent `~Subscription` calls find `entries_.find(id) == entries_.end()` and return without dereferencing the (now-dead) distributor:
```cpp
JamWideFrameDistributor::~JamWideFrameDistributor() {
    std::lock_guard<std::mutex> lock(regMu_);
    assert(entries_.empty() && "outstanding Subscriptions on dtor");
    // Release-mode defense: clear the entries so a leaked Subscription's
    // unregisterAndWait can no-op rather than UAF on owner_.
    for (auto& kv : entries_) kv.second->sub = nullptr;
    entries_.clear();
}
```
(This still doesn't save the leaked `Subscription` itself — its `owner_` is still dangling — but it removes the visible UAF on `unregisterAndWait`.)

Better: make `Subscription` hold a `std::weak_ptr<Distributor::ControlBlock>` so a dead distributor is observable.

---

### WR-05: Redundant `nativeCamera->shutdown()` call before `nativeCamera.reset()` in processor dtor

**File:** `juce/JamWideJuceProcessor.cpp:83-84`
**Issue:**
```cpp
if (nativeCamera) nativeCamera->shutdown();
nativeCamera.reset();
```
`~JamWideCameraDevice()` itself calls `shutdown()` (`JamWideCameraDevice.cpp:168-170`). The explicit call before reset is redundant. It's idempotent (generation bumps, null resets) so it's safe, but the duplication suggests either the destructor's shutdown is intended to be removed (in which case the explicit call is load-bearing) or the explicit call is redundant scaffolding. Pick one.

Not strictly a bug, but the inline comment on lines 80-82 says "bump camera generation BEFORE destruction so any in-flight async closures … detect the bump and return early" — which is exactly what the destructor does anyway. The explicit call adds nothing.

**Fix:** Either drop the explicit `shutdown()` call here, or drop the `shutdown()` call from `~JamWideCameraDevice`. The destructor-call is more defensive; the explicit call is more discoverable. I'd keep the destructor-call (it's what guards `JamWideCameraDevice` users that don't read the processor) and drop the explicit line.

---

### WR-06: `JamWideCameraDevice::getState() const noexcept` uses `const_cast` to call a `const noexcept` method

**File:** `juce/video/native/JamWideCameraDevice.cpp:196-199`
**Issue:**
```cpp
CameraState JamWideCameraDevice::getState() const noexcept {
    return const_cast<CameraStateMachine&>(stateMachine_).getState();
}
```
`CameraStateMachine::getState()` is declared `const noexcept` (`CameraStateMachine.h:72`). No const_cast is needed; the call site can simply do `stateMachine_.getState()`. The cast is dead code that suggests a comment-vs-code mismatch (the comment claims "CameraStateMachine::getState() is not thread-safe" — but the *method itself* IS const; what's not thread-safe is the broader state machine while transitions are running).

**Fix:**
```cpp
CameraState JamWideCameraDevice::getState() const noexcept {
    return stateMachine_.getState();
}
```

---

### WR-07: `RetryWorker::stopThread(500)` may force-terminate, leaving an in-flight `callAsync` capturing `this` (RetryWorker, not device)

**File:** `juce/video/native/JamWideCameraDevice.cpp:215-218`, `134-148` (worker run loop)
**Issue:** When shutdown fires, `retryWorker_->stopThread(500)` waits 500 ms for the worker to exit. If the worker is mid-`wait(100)` slice it will pick up `threadShouldExit()` and return cleanly. But if 500 ms elapses, JUCE's `stopThread` invokes the OS-level terminate (documented as "not recommended").

Between the `wait(slice)` return and the `if (threadShouldExit()) return;` check, the worker may have *just* posted `juce::MessageManager::callAsync([this, myGen]() { … })` to the message queue, capturing the RetryWorker `this`. If termination happens at that moment, `retryWorker_.reset()` then frees the RetryWorker, and the queued lambda fires later with a dangling RetryWorker `this` pointer. The lambda dereferences `owner_.generation_.load(...)` — to reach `owner_` it must dereference `this`. UAF.

Likelihood is low (500 ms is generous), but the same generation-token discipline that the device uses for its own async work isn't applied to the worker's `this` capture. The fix is to capture `&owner_` instead of `this` so the worker's death is irrelevant:

**Fix:**
```cpp
class RetryWorker : public juce::Thread {
    // …
    void run() override {
        auto* owner = &owner_;          // stable address; owner_ outlives us (we are owned)
        const auto myGen = gen_;        // captured at construction
        int attempt = 0;
        while (!threadShouldExit() && attempt < MAX_ATTEMPTS) {
            // … wait …
            if (threadShouldExit()) return;
            juce::MessageManager::callAsync([owner, myGen]() {
                if (owner->generation_.load(std::memory_order_acquire) != myGen) return;
                owner->onRetryTick(myGen);
            });
            ++attempt;
        }
        // …
    }
};
```
Now the lambda doesn't need RetryWorker at all; it only needs the (stable-address) `JamWideCameraDevice&`.

## Info

### IN-01: Dead variable `juce::WeakReference<juce::MessageManager> mm;` in `CameraListener::imageReceived`

**File:** `juce/video/native/JamWideCameraDevice.cpp:45`
**Issue:** `juce::WeakReference<juce::MessageManager> mm;` is declared and immediately abandoned — never assigned, never read. Either leftover scaffolding or the start of a guard that was abandoned in favor of the generation token. Compiler may emit -Wunused-variable on it.

**Fix:** Delete the line.

---

### IN-02: `std::atomic<bool> coexistenceToastShown_` is overkill on a message-thread-only flag

**File:** `juce/JamWideJuceEditor.h:88-92`, `juce/JamWideJuceEditor.cpp:193-205`
**Issue:** The comment on the member declaration says "Atomic for paranoid safety (the lambda capturing `this` runs on the message thread, but atomic exchange is the canonical idiom)." The flag is read/written from a single message-thread callback — a plain `bool` would do. Not wrong, just over-engineered.

**Fix:** Optional. If kept, the comment should be honest about it being style preference.

---

### IN-03: `verify_camera_entitlement.sh` exit-code 2 message is misleading when bundle isn't signed

**File:** `scripts/verify_camera_entitlement.sh:41-48`
**Issue:** The `codesign --display --entitlements - "$BUNDLE_PATH" 2>/dev/null | grep -q ...` pipeline yields exit code from grep (last command). If codesign fails entirely (e.g., bundle isn't signed at all), stdout is empty, grep returns 1, the script reports "com.apple.security.device.camera not found in bundle entitlements" — true but unhelpful. A signed-but-stripped bundle and an unsigned bundle look identical from this diagnostic.

**Fix:** Capture codesign's exit code into a temporary, distinguish "not signed" from "signed but missing entitlement":
```bash
ENT=$(codesign --display --entitlements - "$BUNDLE_PATH" 2>&1) || {
    echo "FAIL: codesign verify failed — bundle is not signed?" >&2
    echo "      codesign output: $ENT" >&2
    exit 2
}
if ! echo "$ENT" | grep -q 'com.apple.security.device.camera'; then
    echo "FAIL: com.apple.security.device.camera not found in bundle entitlements" >&2
    exit 2
fi
```

---

### IN-04: `JamWideFrameDistributor::Subscription::operator=` is `noexcept` but can in theory throw from the mutex

**File:** `juce/video/native/JamWideFrameDistributor.cpp:21-33`
**Issue:** Move assignment calls `owner_->unregisterAndWait(id_)`, which locks `regMu_`. `std::mutex::lock` is allowed to throw `system_error`. Real-world risk is essentially zero (only on OS-level lock failures), but a strict reading of `noexcept` says any exception escaping calls `std::terminate`.

**Fix:** Either drop the `noexcept` qualifier, or document that the assumption is "mutex lock cannot fail on this platform". Not action-required.

---

### IN-05: `CameraPreviewWindow::CameraPreviewWindow` invokes `setLookAndFeel` with a raw pointer to the editor's member; lifetime is correct but undocumented

**File:** `juce/video/native/CameraPreviewWindow.cpp:18-21`, `CameraPreviewWindow.h:23` (param: `juce::LookAndFeel*`)
**Issue:** The window takes a raw `juce::LookAndFeel*` and calls `setLookAndFeel(lookAndFeel)`. The destructor calls `setLookAndFeel(nullptr)`. This depends on the editor's member-declaration order (`previewWindow_` declared AFTER `lookAndFeel` so it destructs first) — which IS the case in `JamWideJuceEditor.h:66` vs `:81`. Correct, but fragile: a future refactor that moves `lookAndFeel` after `previewWindow_` in the editor would silently turn `~CameraPreviewWindow`'s `setLookAndFeel(nullptr)` into a dereference of a destroyed LookAndFeel. A comment in `JamWideJuceEditor.h` near the member declarations pinning the order would help.

**Fix:** Add a comment:
```cpp
// MEMBER-ORDER CONTRACT: previewWindow_ MUST be declared AFTER lookAndFeel so
// that ~CameraPreviewWindow (which calls setLookAndFeel(nullptr)) runs while
// the LookAndFeel is still alive.
```

---

## Structural Findings (fallow)

No `<structural_findings>` block was supplied with this review (no structural pre-pass was run for Phase 19). The narrative findings above are the only output.

---

_Reviewed: 2026-05-16_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
