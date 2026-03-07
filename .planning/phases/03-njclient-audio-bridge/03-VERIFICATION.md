---
phase: 03-njclient-audio-bridge
verified: 2026-03-07T16:06:00Z
status: human_needed
score: 11/11 automated must-haves verified
re_verification: false
human_verification:
  - test: "Connect to a public NINJAM server (ninbot.com) from the standalone app and hear remote participants' audio on stereo output bus 0"
    expected: "Status label shows 'Connected'. Metronome is audible. Other participants' audio plays through the stereo output."
    why_human: "Requires live NINJAM server, audio hardware, and a human ear to confirm audio is actually flowing through AudioProc."
  - test: "Play an instrument or speak into a microphone with the standalone app connected — verify remote participants hear you"
    expected: "Your local audio is encoded (FLAC by default) and transmitted to other participants in the session."
    why_human: "Bidirectional audio transmission requires a second client or human verification from the far end."
  - test: "Connect to a session where another participant is using Vorbis codec — verify no decode errors or audio gaps"
    expected: "Both FLAC and Vorbis codecs work transparently through the audio bridge with no interruptions."
    why_human: "Codec interoperability requires a live peer using a different codec."
  - test: "Connect to a server, then close and reopen the plugin editor window. Verify audio and network continue uninterrupted."
    expected: "Closing/reopening the editor does not change the status label or audio playback — NJClient lifecycle is unaffected."
    why_human: "Editor lifecycle safety requires runtime observation; pluginval passes but cannot verify live server connection."
---

# Phase 3: NJClient Audio Bridge Verification Report

**Phase Goal:** Users can connect to a NINJAM server and hear audio flowing end-to-end through the JUCE plugin or standalone app
**Verified:** 2026-03-07T16:06:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

Plan 03-01 and 03-02 each define a `must_haves` section. All automated truths are verified. Four truths require human verification (live server + audio hardware).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | processBlock calls NJClient::AudioProc with correct float** pointers when connected | VERIFIED | `JamWideJuceProcessor.cpp` lines 127-153: checks `cached_status == NJC_STATUS_OK`, copies input to `inputScratch`, extracts `float* inPtrs[2]` and `float* outPtrs[2]`, calls `client->AudioProc(inPtrs, 2, outPtrs, 2, numSamples, storedSampleRate)` |
| 2 | processBlock silences all output channels when disconnected | VERIFIED | `JamWideJuceProcessor.cpp` lines 156-158: `for (int ch = 0; ch < totalChannels; ++ch) buffer.clear(ch, 0, numSamples)` in else branch |
| 3 | NinjamRunThread calls NJClient::Run() under clientLock | VERIFIED | `NinjamRunThread.cpp` lines 74-79: `const juce::ScopedLock sl(processor.getClientLock()); while (!client->Run()) { ... }` |
| 4 | NinjamRunThread processes UiCommand variants from cmd_queue | VERIFIED | `NinjamRunThread.cpp` lines 107-168: `processor.cmd_queue.drain(...)` with `std::visit` dispatching all required command types |
| 5 | APVTS parameters sync to NJClient atomics every audio callback | VERIFIED | `JamWideJuceProcessor.cpp` lines 110-124: `masterVol`, `masterMute`, `metroVol`, `metroMute` synced via `getRawParameterValue()` + `.store()` every `processBlock` call |
| 6 | NJClient work directory is set to a cross-platform temp path before any connection | VERIFIED | `JamWideJuceProcessor.cpp` lines 40-43: `juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("JamWide")` → `createDirectory()` → `SetWorkDir()` in constructor |
| 7 | In-place buffer safety: input is copied to scratch buffer before AudioProc | VERIFIED | `JamWideJuceProcessor.cpp` lines 133-138: `inputScratch.setSize(2, numSamples, false, false, true)` + `copyFrom` loop before `AudioProc` call |
| 8 | User can type a server address and username and click Connect | VERIFIED | `JamWideJuceEditor.cpp` lines 9-28: `serverField` (default "ninbot.com") and `usernameField` (default "anonymous") with `addAndMakeVisible`; button `onClick` calls `onConnectClicked()` |
| 9 | User can see connection status (Disconnected, Connecting, Connected) | VERIFIED | `JamWideJuceEditor.cpp` lines 89-118: `timerCallback()` at 10Hz reads `cached_status.load(acquire)` and maps to "Disconnected"/"Connecting..."/"Connected"/"Cannot connect"/"Invalid auth" |
| 10 | User can click Disconnect to leave a session | VERIFIED | `JamWideJuceEditor.cpp` lines 64-69: when `isConnected()`, pushes `DisconnectCommand` to `cmd_queue` and resets button text |
| 11 | Editor-owned UI components do not own or lifecycle-manage NJClient | VERIFIED | `JamWideJuceEditor.h` contains no `NJClient` pointer. `JamWideJuceEditor.cpp` only calls `processorRef.getClient()` to read `cached_status`. No `Connect()`/`Disconnect()` calls from editor directly. |
| 12 | Closing and reopening the editor window does not interrupt audio or network | HUMAN NEEDED | Structurally safe (editor owns no NJClient state), confirmed by pluginval editor lifecycle test, but requires live server connection to fully verify. |
| 13 | User connects and hears remote audio (end-to-end audio flow) | HUMAN NEEDED | Requires live NINJAM server and audio hardware. Human approved per 03-02-SUMMARY.md "user-approved" checkpoint. |
| 14 | Local audio transmitted to other participants | HUMAN NEEDED | Requires bidirectional test with live peer. |
| 15 | Both Vorbis and FLAC codecs work through the bridge | HUMAN NEEDED | Requires live session with codec interop. |

**Score:** 11/11 automated must-haves verified. 4 truths require human verification (live server + audio hardware).

---

## Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `juce/JamWideJuceProcessor.h` | NJClient ownership, clientLock, cmd_queue, getClient(), getClientLock() | VERIFIED | All declared: `std::unique_ptr<NJClient> client`, `juce::CriticalSection clientLock`, `jamwide::SpscRing<jamwide::UiCommand, 256> cmd_queue`, `juce::AudioBuffer<float> inputScratch`, `getClient()` and `getClientLock()` accessors (lines 46-57) |
| `juce/JamWideJuceProcessor.cpp` | processBlock -> AudioProc bridge, NJClient construction, work dir setup | VERIFIED | Constructor creates NJClient with workdir + FLAC default (lines 38-46). processBlock calls `AudioProc` when connected (line 146). 159 substantive lines. |
| `juce/NinjamRunThread.h` | NinjamRunThread with processCommands declaration | VERIFIED | `void processCommands(NJClient* client)` declared in private section (line 29). 34 lines, substantive with doc comment. |
| `juce/NinjamRunThread.cpp` | Full run loop: Run() + command dispatch + status tracking + callbacks | VERIFIED | 169 lines. `run()` calls `client->Run()` under lock (line 77). `processCommands()` drains 6 command types (lines 107-168). Callbacks set (lines 56-59). |
| `juce/JamWideJuceEditor.h` | Editor with TextEditor fields, TextButton, Label, Timer | VERIFIED | Inherits `private juce::Timer`. Declares `serverField`, `usernameField`, `connectButton`, `statusLabel`, `timerCallback`, `onConnectClicked`, `isConnected`. |
| `juce/JamWideJuceEditor.cpp` | Connect/disconnect button handler, timer-driven status polling | VERIFIED | `onConnectClicked()` pushes `ConnectCommand`/`DisconnectCommand`. `timerCallback()` reads `cached_status` and updates label. 120 substantive lines. |

---

## Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `juce/JamWideJuceProcessor.cpp` | `NJClient::AudioProc` | processBlock calls AudioProc with float** arrays | WIRED | Line 146: `client->AudioProc(inPtrs, 2, outPtrs, 2, numSamples, static_cast<int>(storedSampleRate))` |
| `juce/NinjamRunThread.cpp` | `NJClient::Run()` | run() loop calls Run() under clientLock | WIRED | Lines 76-78: `const juce::ScopedLock sl(processor.getClientLock()); while (!client->Run())` |
| `juce/NinjamRunThread.cpp` | `jamwide::UiCommand` | cmd_queue.drain() dispatches variants via std::visit | WIRED | Lines 108-167: `processor.cmd_queue.drain([&](jamwide::UiCommand&& cmd) { ... std::visit(...) })` |
| `juce/JamWideJuceEditor.cpp` | `processor.cmd_queue` | Connect button pushes ConnectCommand to cmd_queue | WIRED | Lines 73-77: `processorRef.cmd_queue.try_push(std::move(cmd))` for both Connect and Disconnect |
| `juce/JamWideJuceEditor.cpp` | `processor.getClient()->cached_status` | Timer reads cached_status for status label | WIRED | Lines 91-94: `auto* client = processorRef.getClient()` → `client->cached_status.load(std::memory_order_acquire)` in `timerCallback()` |

All 5 key links verified. No broken connections found.

---

## Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| JUCE-03 | 03-01-PLAN.md, 03-02-PLAN.md | NJClient audio processing integrated via processBlock() | SATISFIED | `processBlock` calls `AudioProc` when connected (Processor.cpp:146). Minimal UI enables triggering the connection (Editor.cpp:63-80). Build passes VST3 + pluginval validate target. REQUIREMENTS.md traceability table marks `JUCE-03 | Phase 3 | Complete`. |

**Note on JUCE-04:** JUCE-04 ("NJClient Run() thread operates via juce::Thread") is mapped to Phase 2 in REQUIREMENTS.md and was completed there. Phase 3 plans do NOT claim JUCE-04 — correctly so. The NinjamRunThread infrastructure from Phase 2 is extended in Phase 3 (run loop implementation), but the requirement itself belongs to Phase 2.

**No orphaned requirements:** REQUIREMENTS.md maps only JUCE-03 to Phase 3. Both plans declare `requirements: [JUCE-03]`. Coverage is complete.

---

## Anti-Patterns Found

No anti-patterns detected in any Phase 3 modified files.

| File | Pattern Type | Result |
|------|-------------|--------|
| `juce/JamWideJuceProcessor.h` | TODO/FIXME/placeholder | None |
| `juce/JamWideJuceProcessor.cpp` | TODO/FIXME/placeholder | None |
| `juce/NinjamRunThread.h` | TODO/FIXME/placeholder | None |
| `juce/NinjamRunThread.cpp` | TODO/FIXME/placeholder | None |
| `juce/JamWideJuceEditor.h` | TODO/FIXME/placeholder | None |
| `juce/JamWideJuceEditor.cpp` | TODO/FIXME/placeholder | None |
| All Phase 3 files | Empty implementations (return null/{},[]) | None (getProgramName returns `{}` which is correct JUCE idiom) |
| All Phase 3 files | Stub handlers (onClick/onSubmit only prevents default) | None — all handlers push real commands |

The chat callback in `NinjamRunThread.cpp` is intentionally a no-op stub (documented, planned for Phase 4). This is an `ℹ️ Info` item, not a blocker — the callback must still be set to prevent NJClient's default behavior, and the plan explicitly notes Phase 4 will add chat UI.

---

## Human Verification Required

### 1. End-to-end audio from remote participants

**Test:** Launch `build/JamWideJuce_artefacts/Debug/Standalone/JamWide JUCE.app`. Select an audio output device. Enter server "ninbot.com" and username "anonymous". Click "Connect". Wait for status label to read "Connected".
**Expected:** Metronome is audible on the stereo output. Any remote participants' audio is heard through the output device.
**Why human:** Requires live NINJAM server, audio hardware, and a human ear to confirm AudioProc is actually mixing audio — automated checks only verify the call is made.

### 2. Local audio transmitted to other participants

**Test:** With the standalone app connected to ninbot.com, configure your audio input device (microphone or instrument). Verify that other participants in the session can hear your audio.
**Expected:** Your local input is encoded in FLAC and transmitted to the server. Other participants hear you.
**Why human:** Bidirectional transmission requires a second client instance or a live peer — cannot verify programmatically.

### 3. Codec interoperability (Vorbis + FLAC)

**Test:** Connect to a session that has participants using the legacy Vorbis codec. Alternatively, connect two clients where one uses the default FLAC and the other switches to Vorbis.
**Expected:** Both codecs work transparently. No audio gaps, decode errors, or crashes.
**Why human:** Requires a live session with mixed codec types.

### 4. Editor close/reopen does not interrupt audio or connection

**Test:** Connect to a NINJAM server. While audio is flowing, close the plugin editor window (in standalone: use Window menu, or in DAW: close the plugin GUI). Wait 5 seconds. Reopen the editor.
**Expected:** Status label shows "Connected" immediately after reopening. Audio continues uninterrupted during and after the editor close/reopen cycle.
**Why human:** pluginval exercises editor create/destroy but not while maintaining a live server connection.

**Note:** The 03-02-SUMMARY.md records that Task 2 (the human checkpoint) was "user-approved" during the original execution, confirming the developer already validated end-to-end audio flow.

---

## Commit Verification

All commits mentioned in SUMMARY files exist in git history and contain the correct file changes:

| Commit | Summary Claim | Actual | Status |
|--------|--------------|--------|--------|
| `9a221a7` | Wire NJClient into Processor and processBlock | `JamWideJuceProcessor.cpp` (+82 lines), `JamWideJuceProcessor.h` (+13 lines) | VERIFIED |
| `8ca3204` | Implement NinjamRunThread run loop with command queue | `NinjamRunThread.cpp` (+156 lines), `NinjamRunThread.h` (+10 lines) | VERIFIED |
| `94e5a08` | Replace placeholder editor with minimal connect/disconnect UI | `JamWideJuceEditor.cpp` (+107 lines), `JamWideJuceEditor.h` (+19 lines) | VERIFIED |
| `fd322da` | Rename processor to processorRef to avoid -Wshadow-field | 6 lines changed across editor files | VERIFIED |

---

## Build Verification

```
cmake --build build --target JamWideJuce_VST3
[100%] Built target JamWideJuce_VST3  -- SUCCESS

cmake --build build --target validate
-- pluginval validation PASSED
[100%] Built target validate           -- SUCCESS
```

Plugin builds as VST3 and passes pluginval at strictness 5 (editor lifecycle stress test included).

---

## Gaps Summary

No automated gaps found. All 11 automated must-haves pass. The 4 human verification items cover live server interaction which cannot be verified programmatically. The 03-02-SUMMARY.md documents that human approval was obtained during execution (Task 2 checkpoint "user-approved"), providing reasonable confidence that those truths also hold. Automated verification considers those as `? UNCERTAIN` pending a fresh human test if desired.

---

_Verified: 2026-03-07T16:06:00Z_
_Verifier: Claude (gsd-verifier)_
