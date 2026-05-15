---
phase: 19
reviewers: [codex]
reviewed_at: 2026-05-16T01:45:00Z
plans_reviewed:
  - 19-01-PLAN-capture-pipeline.md
  - 19-02-PLAN-ui-and-persistence.md
  - 19-03-PLAN-fallback-and-verification.md
context_inputs:
  - 19-CONTEXT.md
  - 19-RESEARCH.md
  - 19-VALIDATION.md
  - REQUIREMENTS.md (CAM-01/02/03 + PKG-04)
  - ROADMAP.md (Phase 19 section)
  - PROJECT.md (first 80 lines)
skipped_reviewers:
  claude: "running inside Claude Code (CLAUDE_CODE_ENTRYPOINT=cli) — skipped for independence"
unavailable_reviewers:
  - gemini
  - coderabbit
  - opencode
  - qwen
  - cursor
  - ollama
  - lm_studio
  - llama_cpp
verdict: NEEDS_REVISION
top_blockers: 3
high_concerns: 7
medium_concerns: 6
low_concerns: 1
---

# Cross-AI Plan Review — Phase 19

## Codex Review

**Model:** `gpt-5.5` (default codex model)
**Verdict:** `NEEDS_REVISION`
**Risk Assessment:** `HIGH` as written

### 1. Summary

Phase 19 is broadly well-scoped and the three-plan split is sensible: backend capture, UI/persistence, then fallback/verification. It covers most of the ROADMAP success criteria on paper: macOS standalone, Logic-hosted AU, REAPER fallback, permission revoke, and Windows standalone. However, I would not approve the plans as executable yet. There are several real blockers around CMake/task sequencing, callback lifetime safety across JUCE camera threads, first-use privacy flow, and mid-session permission revoke detection. These are not polish issues; they can produce failed builds, use-after-free crashes, or missed required UX paths.

### 2. Strengths

- **Good phase boundary.** The plans honor D-29 by keeping Phase 19 out of the audio path. No encoder, RawData send, or interval-tick work leaks into this phase.
- **Correctly identifies `JUCE_USE_CAMERA` as a hard gate.** Plan 19-01 Task 1 explicitly closes Risk E by adding `JUCE_USE_CAMERA=1` and linking `juce::juce_video`.
- **TCC pre-check is the right macOS mitigation.** Plan 19-01 Task 2 follows the correct `AVCaptureDevice authorizationStatusForMediaType` / `requestAccessForMediaType` pattern.
- **Validation intent is strong.** The Wave 0 tests for distributor, state machine, retry backoff, state migration, and cause mapping are the right unit-test layer. The UAT checklist in 19-03 correctly refuses to skip CAM-01/02/03 paths.
- **VDO.Ninja coexistence is handled conservatively.** Plan 19-03 Task 2 preserves the old stack, warns once, and does not block beta A/B testing.

### 3. Concerns

#### HIGH severity

- **HIGH-1: 19-01 Task sequencing is not actually buildable.**
  Plan 19-01 Task 1 adds `target_sources` for files that do not exist until later tasks: `CameraAuthorization_*`, `JamWideFrameDistributor.cpp`, `JamWideCameraDevice.cpp`, and several tests. CMake generally fails generation when source files are missing. The plan says "CMake config will succeed but build will fail," but that assumption is likely false. Plan 19-01 Task 2 then tries to build `JamWideJuce` while `JamWideFrameDistributor.cpp` and `JamWideCameraDevice.cpp` are still missing. This makes the task-level verification order unreliable.

- **HIGH-2: `JamWideFrameDistributor` has a use-after-free race.**
  Plan 19-01 Task 3 uses snapshot iteration outside the mutex. That means `unregisterSubscriber()` can return, the subscriber can be destroyed, and a camera callback that already copied the snapshot can still call `Subscriber::onFrame()` on a dangling pointer. The plan acknowledges "at most one in-flight callback may complete," but `CameraPreviewTile` destructor only unregisters; it does not wait for in-flight callbacks. This violates T-19-02's mitigation.

- **HIGH-3: async camera callbacks capture raw `this`.**
  Plan 19-01 Task 4 captures `this` in `requestCameraAuthorization`, `openDeviceAsync`, watchdog callbacks, `onErrorOccurred`, and retry-worker callbacks. DAW plugin editors/processors can be destroyed while these callbacks are pending. Without a generation token, `WeakReference`, `SafePointer`, or explicit cancellation barrier, this is a real crash path.

- **HIGH-4: preview tile `MessageManager::callAsync` likely captures a destructible component.**
  Plan 19-02 Task 2 schedules repaint from arbitrary camera threads. If the tile is destroyed after scheduling but before the lambda runs, any lambda that touches `this` is unsafe. This needs `juce::Component::SafePointer<CameraPreviewTile>` or an `AsyncUpdater` owned by the component.

- **HIGH-5: first-use privacy dialog does not fire on the real first-launch path.**
  Plan 19-02 Task 3 shows `NativeCameraPrivacyDialog` only when `queryCameraAuthorization() == Authorized` before toggling. On first launch the status is `NotDetermined`; the user clicks Camera, macOS prompt appears, user grants, and the camera opens without the D-22 modal. That contradicts "first time the user clicks Camera button AFTER granting OS permission."

- **HIGH-6: mid-session permission revoke is not reliably detected.**
  Success criterion 4 requires revoking permission in macOS System Settings while JamWide is running. The plan has a first-frame watchdog only. If frames stop after capture has already started and JUCE does not surface a useful `onErrorOccurred`, the preview may freeze silently. A continuous frame-stall watchdog or periodic authorization re-check is needed.

- **HIGH-7: `AlertWindow::showAsync` button result handling is likely wrong.**
  Plan 19-03 Task 1 assumes button index `0` is the first button and indexes `labels[btn]`. JUCE message-box callbacks commonly use `0` for dismiss/no button and `1..N` for buttons unless custom return values are assigned. The same issue appears in `NativeCameraPrivacyDialog`, where `buttonChosen == 0` is treated as "I understand." This could invert or disable dialog actions.

#### MEDIUM severity

- **MEDIUM-1: hidden popout cannot be reopened cleanly.**
  D-09 says closing the popout hides preview while capture continues. But the Camera button is also specified as capture ON/OFF. If the window is hidden while `CameraState::Capturing`, clicking Camera turns capture off instead of reopening the preview. The UX needs an explicit reopen path.

- **MEDIUM-2: `Paused` state is underspecified and may contradict D-09.**
  The research says `Paused` means camera open but frames discarded when popout is hidden, while D-09 says capture continues silently. The implementation plan mostly leaves the window hidden and state Capturing. Either remove `Paused` from Phase 19 or define exactly when it is entered and why frames are discarded.

- **MEDIUM-3: state-machine tests risk testing test hooks instead of production behavior.**
  Plan 19-01 Task 4 adds many `JAMWIDE_BUILD_TESTS` injection methods. That is useful, but the core transition logic should be factored into a small pure state-machine component so tests exercise the same code production callbacks call, not parallel test-only entry points.

- **MEDIUM-4: license Risk C is checked too late.**
  Plan 19-03 Task 2 performs the GPLv2+/AGPL compatibility check after `juce_video` has already been integrated in 19-01 and UI built in 19-02. If the project is GPLv2-only and no JUCE commercial seat exists, this should block before implementation.

- **MEDIUM-5: CMake test linkage is shaky.**
  Some tasks propose linking test executables against `JamWideJuce`. JUCE plugin targets are often not cleanly linkable as libraries for unit tests. The plan later recommends pure-C++ duplication for some tests, which is safer. Standardize this rather than mixing approaches.

- **MEDIUM-6: cause detection remains too approximate for "host lacks entitlement."**
  `!JUCEApplicationBase::isStandaloneApp()` + `Denied` maps all plugin denials to `HostLacksEntitlement`. That may be acceptable copy-wise, but it also means a user who explicitly denied Logic Pro camera access may get "host lacks entitlement," which is wrong. The plan should phrase copy less definitively or inspect host bundle usage description/entitlement where possible.

#### LOW severity

- **LOW-1: 3-second watchdog may be brittle on macOS high preset.**
  JUCE's macOS backend uses still-photo capture looping. Some cameras may exceed 3 seconds on first frame, especially under DAW load. This is aligned with the success criterion, but the fallback should include "still opening" tolerance or UAT tuning.

### 4. Suggestions

- **Fix 19-01 sequencing.**
  Either create stub source files before adding them to `target_sources`, or make Task 1 add only CMake entries for files that already exist. Prefer: Task 1 creates all empty `.h/.cpp/.mm` placeholders and stub test files, then CMake configure is valid.

- **Replace raw subscriber pointers with lifetime-safe tokens.**
  Options:
  - Use `std::shared_ptr<SubscriberToken>` / `std::weak_ptr` and snapshot weak refs.
  - Require subscribers to return a `Subscription` RAII object whose destructor blocks until any in-flight publish exits.
  - Keep callbacks under lock and explicitly forbid unregister/register from `onFrame`, but this is less flexible.

- **Use `AsyncUpdater` for preview repaint.**
  `CameraPreviewTile::onFrame()` should copy the latest `juce::Image` under lock and call `triggerAsyncUpdate()`. `handleAsyncUpdate()` repaints on the message thread. This coalesces frame bursts and avoids unbounded `callAsync` queues.

- **Add cancellation generations for camera async operations.**
  Maintain `std::atomic<uint64_t> generation_`. Increment on shutdown/close. Capture generation in async callbacks and ignore callbacks whose generation is stale. Also stop retry worker before destroying the camera object.

- **Correct first-use privacy flow.**
  After `requestCameraAuthorization` returns `Authorized`, show `NativeCameraPrivacyDialog`; only then call `openDeviceAsync`. Do not only check the pre-existing `Authorized` state.

- **Add continuous frame-stall detection.**
  Track `lastFrameTime`. While Capturing, run a message-thread timer every ~1s. If no frame for a threshold, re-query authorization on macOS and classify revoke vs camera-in-use. This directly covers success criterion 4.

- **Assign explicit dialog return values.**
  Build `MessageBoxOptions` with known button return codes or map JUCE's actual `1..N` results correctly. Do the same for `NativeCameraPrivacyDialog`.

- **Move Risk C license check to 19-01 preflight.**
  It should be the first task before `juce_video` enters the main target. If unresolved, mark Phase 19 blocked pending commercial-seat confirmation or license decision.

- **Define reopen behavior for hidden preview.**
  For example: if state is Capturing and preview window is hidden, Camera button shows the popout instead of stopping capture; a second click or a dropdown item stops capture. Alternatively add a separate "Stop Camera" menu item.

### 5. Risk Assessment

Overall risk is **HIGH** as written. The architecture is directionally right, but the execution plan has build-order assumptions that can fail immediately, and multiple async/thread lifetime holes in exactly the area JUCE warns about: camera callbacks can occur on arbitrary threads and plugin/editor lifetimes are host-controlled. The chance of needing a Phase 19 revision late is high unless the plan tightens callback cancellation, subscriber lifetime, privacy sequencing, and revoke detection before implementation.

### 6. Verdict

**NEEDS_REVISION**

Top 3 blockers:

1. **Non-executable CMake/task ordering in 19-01** due missing source files being added before creation.
2. **Thread/lifetime safety gaps** in distributor snapshot callbacks, `callAsync`, `openDeviceAsync`, TCC callbacks, and retry worker.
3. **Required UX paths are missed**: first-use privacy modal after first TCC grant, and reliable mid-session permission revoke fallback.

---

## Reviewer availability note

Only Codex was invoked this pass. The other CLI reviewers in the GSD review menu (Gemini, OpenCode, Qwen, Cursor, CodeRabbit) are not installed on this machine, and Claude was deliberately skipped because we're running inside Claude Code (`CLAUDE_CODE_ENTRYPOINT=cli`) — using the same model for both planner and reviewer would defeat the adversarial-review premise. To broaden the review pool, install at least one of the listed alternatives and re-run `/gsd-review --phase 19 --<reviewer>`.

---

## How to incorporate this review

Three paths, in increasing involvement:

1. **Triage and apply selected fixes manually.** The plan-checker's review-loop is already structured for this — Edit the plan files for the chosen findings, then `/gsd-review --phase 19` again for a second pass.

2. **Run the integrated re-plan loop:** `/gsd-plan-phase 19 --reviews`. This re-spawns the planner with REVIEWS.md as additional context; the planner reconciles its prior plans against the review feedback.

3. **Triage selectively + accept some findings as deferred risk.** Some findings (LOW-1 watchdog tuning, MEDIUM-6 cause-detection approximation) may legitimately be tunable during UAT rather than locked into the plan. The user gets the final word on which findings are blocking.
