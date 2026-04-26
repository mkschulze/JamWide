---
phase: 14-midi-remote-control
verified: 2026-04-15T18:00:00Z
status: gaps_found
score: 4/4 roadmap success criteria VERIFIED (7 review issues unresolved — see gaps)
overrides_applied: 0
gaps:
  - truth: "Bidirectional feedback to motorized controllers is reliable (no data races)"
    status: failed
    reason: "CR-01: echoSuppression_ is a plain unordered_map written from audio thread (processIncomingMidi line 141) and message thread (setEchoSuppression line 396, clearAllMappings line 260) — undefined behaviour, potential crash under concurrent MIDI+OSC. CR-02: lastSentCcValues_ is a plain unordered_map read/written from audio thread (appendFeedbackMidi) and message thread (timerCallback standalone path) — concurrent modification, undefined behaviour."
    artifacts:
      - path: "juce/midi/MidiMapper.h"
        issue: "echoSuppression_ declared as std::unordered_map<int,int> at line 118 — must be std::array<std::atomic<int>, kMaxMappings>. lastSentCcValues_ declared as std::unordered_map<int,int> at line 121 — must be split into two thread-owned copies."
      - path: "juce/midi/MidiMapper.cpp"
        issue: "Audio thread (line 141) and message thread (line 396) both write to echoSuppression_ without synchronization. Audio thread (line 178-180) and message thread (line 721-723) both access lastSentCcValues_ without synchronization."
    missing:
      - "CR-01 fix: Replace echoSuppression_ unordered_map with std::array<std::atomic<int>, kMaxMappings> as specified in REVIEW.md"
      - "CR-02 fix: Split lastSentCcValues_ into lastSentCcValues_ (audio thread only) and lastSentCcValuesTimer_ (message thread only) as specified in REVIEW.md"
      - "CR-02 companion: Remove WR-02 double-decrement when both host MIDI and standalone are active"
  - truth: "remoteSlotToUserIndex array accesses are safe between refreshFromUsers and timerCallback"
    status: failed
    reason: "CR-03: remoteSlotToUserIndex is a plain std::array<int,16> in JamWideJuceProcessor.h (line 133). refreshFromUsers bulk-fills it from the message thread while timerCallback reads it. A fill(-1) followed by individual writes creates a window where timerCallback sees partial state. The publish count (visibleRemoteUserCount) is updated after the array writes, not with release semantics that ensure the writes are visible."
    artifacts:
      - path: "juce/JamWideJuceProcessor.h"
        issue: "remoteSlotToUserIndex declared as std::array<int, 16> at line 133 — should be std::array<std::atomic<int>, 16> with acquire/release semantics on count update"
    missing:
      - "CR-03 fix: Make remoteSlotToUserIndex an std::array<std::atomic<int>, 16> with atomic store per slot and release fence on visibleRemoteUserCount as specified in REVIEW.md"
  - truth: "VbFader and MidiConfigDialog async lambdas are safe when components are destroyed during pending timers/menus"
    status: failed
    reason: "WR-01: VbFader callAfterDelay(10000, [this]()) at line 233 captures raw this pointer. If VbFader is destroyed before 10 seconds (e.g. refreshFromUsers rebuilds remote strips), the lambda fires into freed memory. WR-05: MidiConfigDialog showMenuAsync callback at line 84 captures [this, unmapped] — dialog can be destroyed (CallOutBox dismissed via Escape) before async result fires, causing use-after-free on midiLearnMgr_ and midiMapper dereferences. Same issue on learnButton row callbacks in rebuildTableRows."
    artifacts:
      - path: "juce/ui/VbFader.cpp"
        issue: "callAfterDelay at line 233 captures raw this — no alive guard. Same pattern in ChannelStrip::showMidiLearnMenu line 408."
      - path: "juce/midi/MidiConfigDialog.cpp"
        issue: "showMenuAsync at line 84 captures raw this — no alive guard. rebuildTableRows learn/delete button callbacks also capture raw this."
    missing:
      - "WR-01 fix: Add std::shared_ptr<std::atomic<bool>> alive_ to VbFader, set false in destructor, check in callAfterDelay lambda"
      - "WR-05 fix: Add aliveFlag pattern to MidiConfigDialog for showMenuAsync callback and row button callbacks"
  - truth: "MidiLearnManager startLearning and tryLearn are safe under concurrent access"
    status: failed
    reason: "WR-04: learningParamId_ (juce::String) and onLearnedCallback_ (std::function) are plain non-atomic members. startLearning (message thread) and tryLearn (audio thread) can race: tryLearn reads onLearnedCallback_ at line 28 while startLearning writes it; cancelLearning (message thread) writes onLearnedCallback_ = nullptr while tryLearn (audio thread) may be reading it. learning_ is atomic but it does not protect the non-atomic members."
    artifacts:
      - path: "juce/midi/MidiLearnManager.h"
        issue: "learningParamId_ and onLearnedCallback_ are unprotected non-atomic members accessed from both audio thread (tryLearn) and message thread (startLearning, cancelLearning)"
    missing:
      - "WR-04 fix: Add juce::CriticalSection or std::mutex protecting learningParamId_ and onLearnedCallback_, or restrict tryLearn to message thread via atomic posting"
---

# Phase 14: MIDI Remote Control — Verification Report

**Phase Goal:** Users can control all mixer parameters including remote channels via MIDI CC, with bidirectional feedback to motorized controllers
**Verified:** 2026-04-15T18:00:00Z
**Status:** gaps_found
**Re-verification:** No — initial verification

---

## Goal Achievement

### Roadmap Success Criteria (Observable Truths)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | User can map MIDI CC messages to any mixer parameter (local, remote, master, metronome) | VERIFIED | MidiMapper.cpp processes CC in processIncomingMidi; 85 APVTS params cover all categories; MIDI Learn right-click menus on VbFader + ChannelStrip; MidiConfigDialog Learn fallback; test_midi_learn passes |
| 2 | User can control remote participant volume/pan/mute via MIDI controller | VERIFIED | timerCallback (20ms) is centralized APVTS-to-NJClient bridge; OscServer group handlers use APVTS-only (no cmd_queue); ChannelStripArea remote strips use ParameterAttachment; test_apvts_njclient_sync passes |
| 3 | Parameter changes in JamWide send MIDI CC feedback to the controller | VERIFIED | appendFeedbackMidi sends CC output per changed parameter; timerCallback handles standalone output; 4-state MidiStatusDot in footer; test_feedback passes |
| 4 | MIDI mappings persist across DAW sessions | VERIFIED | State version bumped to 3; saveToState/loadFromState with MidiMappings ValueTree; validation on load (cc 0-127, channel 1-16, paramId exists); test_state_persistence and test_malformed_state pass |

**Score:** 4/4 roadmap success criteria VERIFIED

### Plan Must-Have Truths (Additional)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| P1-1 | MIDI CC messages arriving in processBlock dispatched to correct APVTS parameter | VERIFIED | processIncomingMidi called first in processBlock (line 522-526); CC branch at MidiMapper.cpp line 69 dispatches to apvts.getParameter |
| P1-2 | Parameter changes produce CC feedback in MidiBuffer output | VERIFIED | appendFeedbackMidi at line 148; lastSentCcValues_ dirty-check; test_feedback passes |
| P1-3 | Echo suppression prevents feedback oscillation (per-mapping, not global) | VERIFIED (race exists) | Per-mapping echoSuppression_ counter logic is correct; CR-01 race does not affect correctness under light load but is undefined behaviour under concurrent access |
| P1-4 | 85 APVTS parameters exist (69 new: 64 remote + 4 localSolo + 1 metroPan) | VERIFIED | createParameterLayout: 4 base + 12 local = 16 existing; 64 remote (i<16, 4 types) + 4 localSolo + 1 metroPan = 69 new; test_apvts_expansion asserts totalCount==85 — PASSES |
| P1-5 | MIDI mappings persist across DAW sessions (state version 3) | VERIFIED | currentStateVersion = 3; MidiMappings ValueTree child saved/loaded; test_state_persistence passes |
| P1-6 | Empty remote slots reset APVTS parameters to defaults | VERIFIED | resetRemoteSlotDefaults at line 402; called when slot is vacated; test_slot_reset passes |
| P1-7 | Remote user APVTS parameters sync to NJClient within 20ms via centralized bridge | VERIFIED | timerCallback at 20ms; sole cmd_queue path for remote group controls; test_apvts_njclient_sync passes |
| P1-8 | MIDI Learn state machine assigns CC on first received CC | VERIFIED | MidiLearnManager::tryLearn; callAsync posts to message thread; test_midi_learn passes |
| P1-9 | Duplicate mapping conflicts handled: last-write-wins | VERIFIED | addMapping at line 207: "Duplicate conflict handling: last-write-wins"; test_duplicate_mapping_conflict passes |
| P1-10 | Malformed saved mappings rejected with validation | VERIFIED | loadFromState validates cc 0-127, channel 1-16, empty paramId, non-existent paramId; test_malformed_state passes |
| P2-1 | Right-click any fader/pan/mute/solo to see MIDI Learn options | VERIFIED | VbFader right-click at line 192; ChannelStrip mouseListener intercepts pan/mute/solo at line 355 |
| P2-2 | Visual feedback during MIDI Learn (pulsing border + overlay) | VERIFIED | VbFader paint() checks midiLearning_ and externalLearning at line 150-154; green/mint border pulsing; confirmation "CC12 Ch1" or "N60 Ch1" |
| P2-3 | Mapping table shows all mappings with slot-labeled params, CC#, Channel, Range, Delete | VERIFIED | MidiConfigDialog 6-column table; getDisplayNameForParam maps remoteVol_N to "Remote Slot N+1 Volume"; rebuildTableRows builds rows with Learn/Delete buttons |
| P2-4 | MIDI status dot with 4-state semantics: Disabled/Healthy/Degraded/Failed | VERIFIED | MidiMapper::Status enum {Disabled, Healthy, Degraded, Failed}; MidiStatusDot paints with grey/green/green-steady/red at MidiStatusDot.cpp lines 42-54 |
| P2-5 | Standalone MIDI device selection in config dialog | VERIFIED | MidiConfigDialog has inputDeviceSelector and outputDeviceSelector ComboBoxes with onChange handlers |
| P2-6 | Standalone MIDI device selection persists across sessions | VERIFIED | midiInputDeviceId/midiOutputDeviceId persisted via getStateInformation; loadFromState at line 709-712 reopens devices |
| P2-7 | Learn from config dialog as host right-click fallback | VERIFIED | learnButton.onClick at line 36; shows popup of unmapped params; calls startLearning at line 91 |
| P2-8 | Mapping table shows slot numbers (Remote Slot 3 Volume) | VERIFIED | getDisplayNameForParam uses "Remote Slot " + juce::String(slot+1) pattern |
| P3-1 | OSC surface changes to remote vol/pan/mute reflect via APVTS (no cmd_queue) | VERIFIED | OscServer handleRemoteUserOsc group handlers update apvts.getParameter("remoteVol_N") only; no SetUserStateCommand in OscServer.cpp for group controls |
| P3-2 | UI drag of remote fader reflects via APVTS | VERIFIED | attachRemoteStripParams uses juce::SliderParameterAttachment for vol/pan, ButtonParameterAttachment for mute/solo; no cmd_queue in remote vol/pan/mute path |
| P3-3 | No duplicate cmd_queue dispatch for remote group controls | VERIFIED | OscServer volume/pan/mute handlers: "NO cmd_queue push here" comments confirmed; ChannelStripArea uses ParameterAttachment not callbacks for vol/pan/mute |
| P3-4 | No feedback loops between OSC, MIDI, and UI for same remote parameter | VERIFIED | setEchoSuppression called in OscServer after APVTS update (line 601, 618, 632, 646); ChannelStripArea solo callbacks call setEchoSuppression (line 604, 665) |
| P3-5 | Roster shrink does not cause stale MIDI mappings to control wrong users | VERIFIED | remoteSlotToUserIndex maps APVTS slot to NJClient user_index; slot is APVTS slot (stable), not username |

---

## Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `juce/midi/MidiMapper.h` | MidiMapper class declaration | VERIFIED | Present, 148+ lines, exports MidiMapper |
| `juce/midi/MidiMapper.cpp` | MidiMapper implementation (min 200 lines) | VERIFIED | 736 lines |
| `juce/midi/MidiLearnManager.h` | MIDI Learn state machine | VERIFIED | Present, exports MidiLearnManager |
| `juce/midi/MidiLearnManager.cpp` | MidiLearnManager implementation (min 40 lines) | VERIFIED | 48 lines |
| `juce/midi/MidiConfigDialog.h` | Config dialog with mapping table | VERIFIED | Present, exports MidiConfigDialog |
| `juce/midi/MidiConfigDialog.cpp` | Config dialog implementation (min 150 lines) | VERIFIED | 503 lines |
| `juce/midi/MidiStatusDot.h` | 4-state footer status indicator | VERIFIED | Present, exports MidiStatusDot |
| `juce/midi/MidiStatusDot.cpp` | MidiStatusDot implementation (min 40 lines) | VERIFIED | 111 lines |
| `juce/midi/MidiTypes.h` | MidiMsgType enum (CC/Note) | VERIFIED | Present |
| `tests/test_midi_mapping.cpp` | Unit tests (min 150 lines) | VERIFIED | 1647 lines, 23 tests, all PASS |

---

## Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `juce/JamWideJuceProcessor.cpp` | `juce/midi/MidiMapper.h` | processBlock calls midiMapper->processIncomingMidi() FIRST then appendFeedbackMidi() | WIRED | Lines 522-526 confirmed |
| `juce/midi/MidiMapper.cpp` | `juce/JamWideJuceProcessor.h` | timerCallback reads APVTS params, sole cmd_queue path for remote group controls | WIRED | Lines 610-628 read remoteVol_/remotePan_/remoteMute_ and push SetUserStateCommand |
| `juce/JamWideJuceProcessor.cpp` | state persistence | getStateInformation saves MidiMappings child tree, setStateInformation restores | WIRED | Lines 620-622 (save) and 701-704 (load) |
| `juce/ui/VbFader.cpp` | `juce/midi/MidiLearnManager.h` | Right-click popup triggers startLearning | WIRED | Line 213: midiLearnMgr_->startLearning() in popup menu handler |
| `juce/ui/ConnectionBar.cpp` | `juce/midi/MidiStatusDot.h` | ConnectionBar owns MidiStatusDot in footer layout | WIRED | Line 201: make_unique<MidiStatusDot>; line 203: addAndMakeVisible |
| `juce/midi/MidiStatusDot.cpp` | `juce/midi/MidiConfigDialog.h` | Click on status dot opens config dialog via CallOutBox | WIRED | Line 67: CallOutBox::launchAsynchronously with MidiConfigDialog |
| `juce/midi/MidiConfigDialog.cpp` | `juce/midi/MidiLearnManager.h` | Learn button triggers startLearning as host right-click fallback | WIRED | Line 91: midiLearnMgr->startLearning() |
| `juce/osc/OscServer.cpp` | `juce/JamWideJuceProcessor.h` | handleRemoteUserOsc updates APVTS remote params (APVTS-only, no cmd_queue) | WIRED | Lines 595-621: apvts.getParameter("remoteVol_N").setValueNotifyingHost() with no SetUserStateCommand |
| `juce/ui/ChannelStripArea.cpp` | `juce/JamWideJuceProcessor.h` | attachRemoteStripParams uses ParameterAttachment (APVTS-only) | WIRED | Lines 446-460: SliderParameterAttachment / ButtonParameterAttachment for remoteVol/Pan/Mute/Solo |

---

## Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `juce/midi/MidiMapper.cpp` timerCallback | remoteVol_N, remotePan_N, etc. | APVTS getRawParameterValue | Yes — APVTS populated by UI, OSC, MIDI | FLOWING |
| `juce/midi/MidiMapper.cpp` appendFeedbackMidi | CC values from APVTS getParameter() | Real APVTS parameter state | Yes — reflects all change sources | FLOWING |
| `juce/midi/MidiStatusDot.cpp` | MidiMapper::Status | midiMapper.getStatus() polled by timer | Yes — receivingMidi_ and deviceError_ atomics in MidiMapper | FLOWING |
| `juce/midi/MidiConfigDialog.cpp` mapping table | MidiMapper mapping table | midiMapper.getMappings() | Yes — reads published_ atomic shared_ptr | FLOWING |

---

## Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| 23 unit tests pass (CC dispatch, feedback, echo suppression, persistence, MIDI Learn, APVTS sync, Note On/Off) | `./build-test/test_midi_mapping_artefacts/Release/test_midi_mapping` | 23 passed, 0 failed | PASS |
| APVTS expansion to 85 total parameters | test_apvts_expansion asserts totalCount==85 | PASS | PASS |
| APVTS-NJClient sync via timerCallback | test_apvts_njclient_sync verifies SetUserStateCommand dispatched | PASS | PASS |
| Malformed state rejected | test_malformed_state: out-of-range CC, invalid channel, empty paramId | PASS | PASS |

---

## Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| MIDI-01 | 14-01, 14-02, 14-03 | User can map MIDI CC to any mixer parameter with bidirectional feedback and persistent mappings | SATISFIED | CC+Note dispatch, 85 APVTS params, feedback path, state v3 persistence, right-click Learn UX — all implemented and tested |

---

## Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `juce/midi/MidiMapper.cpp` | 141, 396 | `echoSuppression_` written from both audio thread and message thread — unprotected unordered_map | BLOCKER (CR-01) | Undefined behaviour, heap corruption risk under concurrent MIDI+OSC load |
| `juce/midi/MidiMapper.cpp` | 178-180, 721-723 | `lastSentCcValues_` read/written from both audio thread and message thread — unprotected unordered_map | BLOCKER (CR-02) | Undefined behaviour, iterator invalidation under concurrent standalone+host use |
| `juce/JamWideJuceProcessor.h` | 133 | `remoteSlotToUserIndex` plain array written by message thread; fill/write sequence has mid-update visibility gap | WARNING (CR-03) | Latent race — timerCallback could read partial state during roster update |
| `juce/ui/VbFader.cpp` | 233 | `callAfterDelay(10000, [this]())` captures raw this — no alive guard | WARNING (WR-01) | Use-after-free if VbFader destroyed before 10s timer fires (common with remote strip rebuilds) |
| `juce/midi/MidiLearnManager.h` | 18-22 | `learningParamId_` and `onLearnedCallback_` unprotected — race between startLearning (msg thread) and tryLearn (audio thread) | WARNING (WR-04) | Data race on juce::String and std::function |
| `juce/midi/MidiConfigDialog.cpp` | 84-97, 392-415 | `showMenuAsync` captures raw this — no alive guard | WARNING (WR-05) | Use-after-free if CallOutBox dismissed before async menu result fires |
| `juce/midi/MidiMapper.cpp` | 535-543, 629-634 | DBG() calls in hot paths (standalone MIDI device callback, timerCallback at 50Hz) | INFO (IN-01) | No-ops in release build; indicate code not yet considered final |
| `juce/midi/MidiConfigDialog.cpp` | 157-160, 704-709 | Double echo-suppression decrement when both host MIDI and standalone output active | WARNING (WR-02) | Echo suppression expires too early; feedback echo possible on next cycle after CR-01 is fixed |

---

## Human Verification Required

### 1. Physical MIDI Controller End-to-End

**Test:** Connect a motorized MIDI controller (e.g., Behringer X-TOUCH, Korg nanoKONTROL2) to JamWide in standalone mode. Right-click a remote user volume fader, select "MIDI Learn", move a fader on the controller. Then move the fader in JamWide UI.
**Expected:** (a) CC mapping is created after MIDI Learn. (b) Moving the fader in JamWide UI causes the motorized fader on the controller to move in response. (c) Moving the controller fader moves the JamWide fader. (d) No feedback oscillation.
**Why human:** Requires physical MIDI hardware; audio-thread timing and DAW routing cannot be verified programmatically.

### 2. Host DAW Right-Click Intercept Test

**Test:** Load JamWide as a VST3 in Logic Pro or Ableton Live. Right-click any mixer fader.
**Expected:** "MIDI Learn" option appears in context menu (host does not intercept and block the right-click context menu).
**Why human:** Some DAWs (Logic, Reaper) intercept right-click on plugin UI. The config dialog "Learn New..." button is the documented fallback — verify this fallback is discoverable when host intercepts.

### 3. Standalone Device Persistence Across Sessions

**Test:** Open JamWide standalone. Open MIDI config dialog. Select MIDI input and output devices. Close and reopen JamWide standalone.
**Expected:** Previously selected devices are preselected in the dropdowns; MIDI Learn works without re-selecting devices.
**Why human:** Requires standalone launch cycle; device identifier persistence depends on OS MIDI stack.

### 4. Remote User Roster Change During MIDI Learn

**Test:** Have a remote user connected and mapped to MIDI CC. Have another user join, then have the original user disconnect and reconnect.
**Expected:** MIDI CC mapping remains associated with "Remote Slot N" (not the specific user name); the new user in slot N is controlled by the same CC.
**Why human:** Requires live NINJAM session with multiple participants.

---

## Gaps Summary

**4 gaps blocking reliable operation** (all from unfixed REVIEW.md critical/warning findings):

**CR-01 + CR-02 (BLOCKERS):** The `echoSuppression_` and `lastSentCcValues_` plain `unordered_map` members in MidiMapper are accessed from both the audio thread and the message thread without synchronization. This is undefined behaviour — under a busy MIDI+OSC session with concurrent input, this can cause heap corruption or silent state corruption that defeats the bidirectional feedback goal. The REVIEW.md specifies exact fixes: atomic array for echoSuppression, split maps for lastSentCcValues.

**WR-01 + WR-05 (Warnings elevated to gaps):** Raw `this` capture in `callAfterDelay` (VbFader) and `showMenuAsync` (MidiConfigDialog) creates use-after-free risk. When remote user strips are rebuilt by refreshFromUsers, pending 10-second MIDI Learn timers fire into freed VbFader instances. The alive-flag pattern is already used by OscServer in this codebase — applying it here is a known fix.

**CR-03 (Warning):** `remoteSlotToUserIndex` plain array lacks atomicity for the fill/write sequence in refreshFromUsers. The REVIEW.md fix (atomic array + release fence on visibleRemoteUserCount) is straightforward.

**WR-04 (Warning):** MidiLearnManager `learningParamId_` and `onLearnedCallback_` are unprotected across audio/message thread boundary. The fix is a mutex or posting tryLearn's state observation to the message thread.

The **functional goal is achieved** — all 4 roadmap success criteria are verified with 23 passing unit tests. The MIDI mapping system works correctly under normal single-threaded use. The gaps are reliability issues that manifest under concurrent load or component destruction sequences.

---

_Verified: 2026-04-15T18:00:00Z_
_Verifier: Claude (gsd-verifier)_
