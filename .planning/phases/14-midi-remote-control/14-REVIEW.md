---
phase: 14-midi-remote-control
reviewed: 2026-04-15T00:00:00Z
depth: standard
files_reviewed: 20
files_reviewed_list:
  - CMakeLists.txt
  - juce/JamWideJuceProcessor.cpp
  - juce/JamWideJuceProcessor.h
  - juce/midi/MidiConfigDialog.cpp
  - juce/midi/MidiConfigDialog.h
  - juce/midi/MidiLearnManager.cpp
  - juce/midi/MidiLearnManager.h
  - juce/midi/MidiMapper.cpp
  - juce/midi/MidiMapper.h
  - juce/midi/MidiStatusDot.cpp
  - juce/midi/MidiStatusDot.h
  - juce/midi/MidiTypes.h
  - juce/osc/OscServer.cpp
  - juce/ui/ChannelStrip.cpp
  - juce/ui/ChannelStrip.h
  - juce/ui/ChannelStripArea.cpp
  - juce/ui/ConnectionBar.cpp
  - juce/ui/ConnectionBar.h
  - juce/ui/VbFader.cpp
  - juce/ui/VbFader.h
  - tests/test_midi_mapping.cpp
findings:
  critical: 3
  warning: 6
  info: 4
  total: 13
status: issues_found
---

# Phase 14: Code Review Report

**Reviewed:** 2026-04-15
**Depth:** standard
**Files Reviewed:** 20
**Status:** issues_found

## Summary

Phase 14 adds MIDI remote control to JamWide: a `MidiMapper` (audio-thread MIDI dispatcher, message-thread mapping manager), a `MidiLearnManager` (state machine for CC assignment), a `MidiConfigDialog` (standalone device selector and mapping table UI), and visual MIDI Learn overlays on `VbFader` and `ChannelStrip`. The architecture is well-designed — the published/staging atomic-swap pattern for the `MappingTable` is correct, the APVTS-to-NJClient timer bridge is properly isolated to the message thread, and the `callAsync` hop in `MidiLearnManager::tryLearn` correctly moves callback work off the audio thread.

Three critical issues exist, all thread-safety data races involving shared data structures accessed from both the audio thread and the message thread without synchronization. Two of those affect `echoSuppression_` and `lastSentCcValues_` — unprotected `std::unordered_map` members written by the audio thread but also written or cleared by the message thread. The third is `remoteSlotToUserIndex`, a plain `std::array<int,16>` that the message thread writes and the timer-thread reads without synchronization.

Six warnings cover a use-after-free risk in the 10-second MIDI Learn auto-cancel timer, a double echo-suppression decrement bug in the standalone output path, a missing `visibleRemoteUserCount` atomic update for multi-channel users, and several correctness issues in the config dialog.

---

## Critical Issues

### CR-01: Data race on `echoSuppression_` between audio thread and message thread

**File:** `juce/midi/MidiMapper.cpp:141`, `juce/midi/MidiMapper.cpp:157-160`, `juce/midi/MidiMapper.cpp:260`, `juce/midi/MidiMapper.cpp:396`

**Issue:** `echoSuppression_` is a plain `std::unordered_map<int, int>` member. It is **written** from the audio thread in `processIncomingMidi` (line 141: `echoSuppression_[key] = 2`). It is also **written** from the message thread in `setEchoSuppression` (line 396, called from OSC receive handler and ChannelStripArea UI callbacks) and **cleared** from the message thread in `clearAllMappings` (line 260). The timer callback (lines 704-709) also reads and decrements it, but since JUCE timers fire on the message thread that part is safe. The audio-thread write vs. message-thread write/clear is an unsynchronized concurrent modification of the same `unordered_map` — undefined behaviour, likely crashes or silent state corruption under a busy MIDI+OSC session.

**Fix:** Move suppression state to an `std::atomic<int>` per-slot array (max 4096 slots matching `kMaxMappings`), or protect with a dedicated spinlock. The simplest correct approach given the existing architecture:

```cpp
// In MidiMapper.h — replace unordered_map with fixed array of atomics
std::array<std::atomic<int>, kMaxMappings> echoSuppression_{};

// processIncomingMidi (audio thread) — same index via makeKey()
echoSuppression_[key].store(2, std::memory_order_relaxed);

// setEchoSuppression (message thread)
echoSuppression_[key].store(2, std::memory_order_relaxed);

// appendFeedbackMidi / timerCallback — fetch_sub returns old value
int sup = echoSuppression_[key].load(std::memory_order_relaxed);
if (sup > 0) {
    echoSuppression_[key].fetch_sub(1, std::memory_order_relaxed);
    continue;
}

// clearAllMappings — zero all slots
for (auto& s : echoSuppression_) s.store(0, std::memory_order_relaxed);
```

Alternatively, since `setEchoSuppression` is always called from the message thread shortly before a feedback cycle, a simpler approach is to queue suppression requests via the existing SPSC ring and process them at the start of `processIncomingMidi`.

---

### CR-02: Data race on `lastSentCcValues_` between audio thread and message thread

**File:** `juce/midi/MidiMapper.cpp:178-180` (audio thread), `juce/midi/MidiMapper.cpp:721-723` (message thread timer), `juce/midi/MidiMapper.cpp:261` (message thread clear)

**Issue:** `lastSentCcValues_` is a plain `std::unordered_map<int, int>`. The audio thread reads and writes it in `appendFeedbackMidi` (lines 178-180). The message thread reads and writes it in `timerCallback` (lines 721-723, the standalone MIDI output feedback path). Both paths run concurrently: the audio thread runs every ~5ms and the message thread timer fires every 20ms. This is an unsynchronized concurrent modification — undefined behaviour, potential iterator invalidation, heap corruption.

**Fix:** The simplest fix is to make the standalone output feedback path use a separate tracking map (`lastSentCcValuesTimer_`) owned exclusively by the message thread. The audio-thread feedback path (`appendFeedbackMidi`) and the standalone output path (`timerCallback`) track state independently anyway since they write to different sinks (host MIDI buffer vs. `midiOutput_->sendMessageNow`). Splitting the tracking map eliminates the race without any locking:

```cpp
// In MidiMapper.h — two separate maps, one per thread:
std::unordered_map<int, int> lastSentCcValues_;        // audio thread only
std::unordered_map<int, int> lastSentCcValuesTimer_;   // message thread only (timerCallback)
```

Then in `timerCallback`, use `lastSentCcValuesTimer_` instead of `lastSentCcValues_`. In `clearAllMappings` (message thread), clear both.

---

### CR-03: Data race on `remoteSlotToUserIndex` — plain array written by message thread, read by timer

**File:** `juce/JamWideJuceProcessor.h:133`, `juce/ui/ChannelStripArea.cpp:547,562`, `juce/midi/MidiMapper.cpp:606,645`

**Issue:** `remoteSlotToUserIndex` is declared as a plain `std::array<int, 16>` (not atomic). `ChannelStripArea::refreshFromUsers` writes it from the message thread (lines 547, 562). `MidiMapper::timerCallback` reads it from the message thread timer (lines 606, 645). JUCE timers fire on the message thread, so these two accesses are actually serialized by the message thread's event loop — this race is latent, not currently triggered, **but** there is one dangerous pattern: `refreshFromUsers` does a bulk `fill(-1)` (line 547) followed by individual slot writes. If `timerCallback` fires between the fill and the subsequent writes (which can happen if `refreshFromUsers` is called from inside a JUCE `callAsync` that yields), a slot could be seen as `-1` and silently skipped mid-update. More importantly, the comment on `remoteSlotToUserIndex` says "written by message thread, read by message thread" but the guard (`cachedUsersMutex`) is not held during the array access in `timerCallback`, while it IS held during `refreshFromUsers` for the `cachedUsers` read. The asymmetry is fragile. Additionally, `visibleRemoteUserCount` is updated **after** the fill loop completes (line 732), creating a window where `timerCallback` could iterate based on a stale count while the array is half-populated.

**Fix:** Make `remoteSlotToUserIndex` an `std::array<std::atomic<int>, 16>` to guarantee visibility, and change the fill/write sequence to use a local staging array with a single atomic publication:

```cpp
// In JamWideJuceProcessor.h:
std::array<std::atomic<int>, 16> remoteSlotToUserIndex;

// In ChannelStripArea::refreshFromUsers — write atomically per slot:
for (auto& slot : processorRef.remoteSlotToUserIndex)
    slot.store(-1, std::memory_order_relaxed);
// ... then:
processorRef.remoteSlotToUserIndex[visibleSlot].store(
    static_cast<int>(userIdx), std::memory_order_relaxed);

// Publish count last (release fence ensures array writes visible before count):
processorRef.visibleRemoteUserCount.store(visibleSlot, std::memory_order_release);

// In MidiMapper::timerCallback — acquire:
const int count = juce::jmin(
    processor.visibleRemoteUserCount.load(std::memory_order_acquire), 16);
int njUserIndex = processor.remoteSlotToUserIndex[i].load(std::memory_order_relaxed);
```

---

## Warnings

### WR-01: Use-after-free in 10-second MIDI Learn auto-cancel timer (VbFader)

**File:** `juce/ui/VbFader.cpp:233-243`

**Issue:** The auto-cancel timer at line 233 captures `this` in a lambda:
```cpp
juce::Timer::callAfterDelay(10000, [this]() {
    if (midiLearning_)
    {
        if (midiLearnMgr_ != nullptr && ...)
            midiLearnMgr_->cancelLearning();
        midiLearning_ = false;
        repaint();
    }
});
```
If the `VbFader` is destroyed before 10 seconds elapses (e.g., when `ChannelStripArea::refreshFromUsers` rebuilds remote strips), the pending lambda retains a dangling `this` pointer. When the lambda fires it will write to freed memory. The same pattern exists in `ChannelStrip::showMidiLearnMenu` at line 408.

**Fix:** Use a shared `std::atomic<bool>` alive flag or a `std::weak_ptr` pattern (the same approach OscServer uses for its async lambdas):

```cpp
// In VbFader.h — add:
std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

// In VbFader destructor:
alive_->store(false, std::memory_order_release);

// In the callAfterDelay lambda:
auto alive = alive_;
juce::Timer::callAfterDelay(10000, [this, alive]() {
    if (!alive->load(std::memory_order_acquire)) return;
    // ... rest of cancel logic
});
```

The same fix is needed in `ChannelStrip::showMidiLearnMenu` line 408 where `this`, `target`, and `paramId` are captured.

---

### WR-02: Double decrement of `echoSuppression_` when both host MIDI and standalone MIDI are active

**File:** `juce/midi/MidiMapper.cpp:157-160` and `juce/midi/MidiMapper.cpp:704-709`

**Issue:** Echo suppression is decremented in **two** separate places for the same key:
1. `appendFeedbackMidi` (called from the audio thread, host MIDI path): lines 157-160
2. `timerCallback` (standalone MIDI output path): lines 704-709

Both use the same `echoSuppression_` map. When a standalone MIDI device is open AND the plugin is loaded in a host, a single CC input event sets `echoSuppression_[key] = 2`, then the value gets decremented **twice per "tick"** — once in the audio callback and once in the 20ms timer. This means suppression expires after only half the intended duration (one tick instead of two), potentially allowing echo on the very next feedback cycle.

**Fix:** Use the separate `lastSentCcValuesTimer_` map proposed in CR-02's fix. The standalone path tracks state independently and does not need the suppression counter from the host path. Echo suppression for standalone output should be tracked in a dedicated `echoSuppressionTimer_` map owned by the message thread, while the audio-thread `echoSuppression_` remains audio-thread-only.

---

### WR-03: `visibleRemoteUserCount` not incremented for multi-channel user parent strips

**File:** `juce/ui/ChannelStripArea.cpp` (multi-channel branch, approximately lines 641-730)

**Issue:** In `refreshFromUsers`, the single-channel branch increments `visibleSlot` once per user. The multi-channel branch creates one parent strip plus N child strips. Looking at the code around line 641, the parent strip calls `attachRemoteStripParams(*parentStrip, visibleSlot)`, but `visibleSlot` is only incremented once (for the parent) and the child strips each call `attachRemoteStripParams` with their own sub-slot. However, `processorRef.visibleRemoteUserCount.store(visibleSlot, ...)` at line 732 stores the slot count **after all strips are created**. This part appears correct.

The actual issue is subtler: for a multi-channel user, the parent strip and each child strip each consume a `visibleSlot` from the APVTS parameter namespace (`remoteVol_N`, etc.). This means a 3-channel remote user occupies slots N, N+1, N+2, leaving only 13 slots for remaining users (not 15). `MidiMapper::timerCallback` uses `visibleRemoteUserCount` to bound the loop and maps slot indices directly to NJClient user indices via `remoteSlotToUserIndex`. If child strips consume slots but `remoteSlotToUserIndex` only maps the parent slot to the NJClient user index, the child slots map to `-1` and get silently skipped in the timer. Verify that `remoteSlotToUserIndex` is populated for child slots and that `visibleRemoteUserCount` reflects the total number of parameter slots consumed (not the number of visible users).

**Fix:** Add an assertion or log in `timerCallback` to detect `-1` slots that fall within the count boundary, and confirm in `refreshFromUsers` that `remoteSlotToUserIndex` entries are set for every `visibleSlot` increment including child strips.

---

### WR-04: `MidiLearnManager` state machine has no mutex — `learningParamId_` and `onLearnedCallback_` are not protected

**File:** `juce/midi/MidiLearnManager.cpp:3-48`, `juce/midi/MidiLearnManager.h:18-22`

**Issue:** `learning_` is an `std::atomic<bool>` providing correct visibility, but `learningParamId_` (a `juce::String`) and `onLearnedCallback_` (a `std::function`) are plain non-atomic members. `startLearning` is called from the message thread. `tryLearn` is called from the audio thread. The audio thread reads `onLearnedCallback_` (line 28) while the message thread may be writing `learningParamId_` and `onLearnedCallback_` concurrently (in a subsequent `startLearning` call that replaces one learn with another). This is a data race on the `juce::String` and `std::function` objects. In practice this is unlikely to manifest because `isLearning()` should return false between learns, but a rapid re-learn sequence could expose it.

`cancelLearning` (message thread) writes `onLearnedCallback_ = nullptr` at line 46 while `tryLearn` (audio thread) may be reading it at line 28 — this is an unsynchronized concurrent write+read on `std::function`.

**Fix:** Add a `juce::CriticalSection` or `std::mutex` protecting the non-atomic members, or restrict `tryLearn` to the message thread (by having `processIncomingMidi` post the message number to an atomic and let the message thread complete the learn).

---

### WR-05: `MidiConfigDialog::learnButton` popup menu callback captures `this` — dialog may be destroyed during async result

**File:** `juce/midi/MidiConfigDialog.cpp:84-97`

**Issue:** `showMenuAsync` captures `[this, unmapped]` in the result callback at line 84. `MidiConfigDialog` is launched as a `CallOutBox` (`MidiStatusDot::mouseUp` line 67), and call-out boxes can be dismissed (and the dialog destroyed) before the async menu result fires, particularly if the user presses Escape. If the menu callback fires after the dialog is deleted, `this->midiLearnMgr_` and `this->midiMapper` dereferences on lines 92-95 are use-after-free.

**Fix:** Use the same alive-flag pattern already used in `OscServer`:
```cpp
auto aliveFlag = std::make_shared<std::atomic<bool>>(true);
// store in member, set to false in destructor
menu.showMenuAsync(..., [this, aliveFlag, unmapped](int result) {
    if (!aliveFlag->load(std::memory_order_acquire)) return;
    // safe to use this
});
```

The same issue affects the learn button callback in `rebuildTableRows` at line 392-404 and the delete button at line 413-415, since `rowComponents` are owned by the dialog which can be destroyed during any async operation.

---

### WR-06: `MidiConfigDialog` timer fires `refreshMappingTable()` at 500ms but timer is never stopped if `MidiLearnManager` is mid-learn when dialog closes

**File:** `juce/midi/MidiConfigDialog.cpp:196`, `juce/midi/MidiConfigDialog.h`

**Issue:** The dialog `startTimer(500)` in the constructor and inherits `juce::Timer`. `juce::Timer` stops automatically when the component is destroyed because the JUCE timer list holds a weak reference. However, `refreshMappingTable` calls `rebuildTableRows` which calls `resized()` on the dialog. If the dialog is in the process of being destroyed (e.g., its parent `CallOutBox` is being closed), calling `resized()` from the timer callback could trigger child component operations on a partially-torn-down component tree. The `CallOutBox` destruction sequence is not guaranteed to call `stopTimer()` before triggering the timer's destructor logic.

**Fix:** Override `visibilityChanged()` and call `stopTimer()` when the dialog becomes hidden, or explicitly call `stopTimer()` early in the destructor before any member cleanup.

---

## Info

### IN-01: Debug `DBG()` calls remain in production MIDI paths

**File:** `juce/midi/MidiMapper.cpp:535-543`, `juce/midi/MidiMapper.cpp:629-634`

**Issue:** Three `DBG(...)` calls remain in shipping code paths — two in `handleIncomingMidiMessage` (the hot standalone MIDI device callback) and one in `timerCallback` which fires 50 times/second. These expand to `std::cerr` or `OutputDebugStringA` in debug builds and are no-ops in release, but they document that the code is not yet considered final and represent dead code in release builds.

**Fix:** Remove the `DBG()` calls or wrap them in a `MIDI_DEBUG_LOG` compile-time guard if they are useful for ongoing development.

---

### IN-02: `MidiConfigDialog` column headers are unused dead code

**File:** `juce/midi/MidiConfigDialog.cpp:249-256`

**Issue:** Lines 249-256 allocate `colHeaderRow` from the layout and then immediately call `juce::ignoreUnused(colHeaderRow)` with a comment that column headers "are part of the table display." No actual column header labels are ever created or added to the component. The code block is dead.

**Fix:** Either add proper column header labels (Param, CC#, Ch, Range, etc.) or remove the layout allocation entirely and let the viewport use the full remaining area.

---

### IN-03: `getTrailingIntValue()` used on paramId strings without verifying the prefix match is exact

**File:** `juce/midi/MidiConfigDialog.cpp:471-487`

**Issue:** `getDisplayNameForParam` uses patterns like:
```cpp
if (paramId.startsWith("remoteVol_"))
    return "Remote Slot " + juce::String(paramId.getTrailingIntValue() + 1) + " Volume";
```
`getTrailingIntValue()` returns 0 if no trailing integer is found. A paramId of `"remoteVol_"` (no number) would silently return "Remote Slot 1 Volume" instead of the raw fallback. This is a display-only cosmetic issue but could confuse users if an unexpected paramId reaches this function.

**Fix:** Add a guard before incrementing:
```cpp
int slotNum = paramId.getTrailingIntValue();
// getTrailingIntValue returns 0 both for "_0" and for missing number — check last char
if (paramId.endsWithChar('_')) return paramId;  // malformed
return "Remote Slot " + juce::String(slotNum + 1) + " Volume";
```

---

### IN-04: `VbFader` paint method accesses `midiLearnMgr_->isLearning()` and `getLearningParamId()` without null check separation

**File:** `juce/ui/VbFader.cpp:151-153`

**Issue:** The expression:
```cpp
bool externalLearning = (!midiLearning_ && midiLearnMgr_ != nullptr
    && midiLearnMgr_->isLearning()
    && midiLearnMgr_->getLearningParamId() == midiParamId_);
```
calls two methods on `midiLearnMgr_` in a single expression. Both calls are safe given the `!= nullptr` guard due to short-circuit evaluation, but `getLearningParamId()` returns `learningParamId_` which is a `juce::String` that could be cleared by `cancelLearning` on the message thread between the two calls (since `paint` can be called from the message thread during a repaint). This is a benign TOCTOU in display logic only (worst case the green pulsing border flickers for one frame), but it is worth noting.

**Fix:** Snapshot the result of `isLearning()` and `getLearningParamId()` into local variables at the top of `paint()` before the condition, or add `getLearningSnapshot()` to `MidiLearnManager` that returns both under a single lock.

---

_Reviewed: 2026-04-15_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
