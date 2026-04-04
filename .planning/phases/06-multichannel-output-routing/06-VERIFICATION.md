---
phase: 06-multichannel-output-routing
verified: 2026-04-04T22:30:00Z
status: passed
score: 14/14 must-haves verified
re_verification: false
---

# Phase 6: Multichannel Output Routing Verification Report

**Phase Goal:** Per-user multichannel output routing with main mix accumulation, routing mode controls, and metronome on dedicated bus
**Verified:** 2026-04-04
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| #  | Truth | Status | Evidence |
|----|-------|--------|----------|
| 1  | AudioProc receives a 34-channel output buffer and routes per-user audio to individual stereo pairs via out_chan_index | VERIFIED | `client->AudioProc(inPtrs, numInputChannels, outPtrs, kTotalOutChannels, ...)` at JamWideJuceProcessor.cpp:213; `kTotalOutChannels = 34` in .h:83 |
| 2  | Main mix (bus 0) always contains all users + metronome + local monitoring regardless of routing | VERIFIED | Accumulation loop at .cpp:234-254: remote buses 1..kMetronomeBus-1 added to mainL/mainR, then metronome bus added separately; bus 0 is always the base output |
| 3  | Metronome audio appears on the last bus (channels 32-33) in addition to main mix | VERIFIED | `SetMetronomeChannel(32)` in NinjamRunThread.cpp:305; metronome accumulated via `outPtrs[kMetronomeBus * 2]` at .cpp:247 |
| 4  | Per-user volume/pan from mixer applies before bus routing; master volume applied during main-mix accumulation for remote buses only | VERIFIED | `mainL[s] += busL[s] * mvL` at .cpp:240-241; master vol derived from `config_mastervolume` and `config_mastermute` atomics at .cpp:222-228 |
| 5  | Metronome is NOT attenuated by master volume when accumulated into main mix | VERIFIED | Separate accumulation block at .cpp:245-253 adds metroL/metroR without multiplying by mvL/mvR |
| 6  | Routing mode and metronome bus setting persist across DAW save/load | VERIFIED | `routingMode.load()` in `getStateInformation` at .cpp:366; `routingMode.store()` in `setStateInformation` at .cpp:412 with `jlimit(0,2,...)` validation |
| 7  | outputScratch buffer is pre-allocated in prepareToPlay, never heap-allocated on the audio thread | VERIFIED | `outputScratch.setSize(kTotalOutChannels, samplesPerBlock, ...)` at .cpp:110 in `prepareToPlay`; safety resize at .cpp:205-206 uses `avoidReallocating=true` |
| 8  | Auto-assign search space is limited to channels 2-31 (buses 1-15), excluding metronome bus on channels 32-33 | VERIFIED | `config_remote_autochan_nch = (c.mode > 0) ? 32 : 0` at NinjamRunThread.cpp:474; identical on-connect at .cpp:303 |
| 9  | User can click Route button to switch between Manual, Assign by User, and Assign by Channel modes | VERIFIED | PopupMenu with `"Manual (Main Mix)"`, `"Assign by User"`, `"Assign by Channel"` at ConnectionBar.cpp:130-132; `onRouteModeChanged` callback fires at .cpp:152 |
| 10 | Quick-assign updates all existing remote channel strips' routing selectors to reflect new bus assignments | VERIFIED | `SetRoutingModeCommand` dispatch in NinjamRunThread.cpp:468 executes two-pass sweep; `setRoutingBus(out_chan_index/2)` called per strip in ChannelStripArea.cpp:456-457 and 564-565 |
| 11 | User can override individual strip routing via the per-strip routing selector dropdown | VERIFIED | `onRoutingChanged` callback at ChannelStripArea.cpp:444-453 and 552-560 pushes `SetUserChannelStateCommand` with `set_outch=true` and `outchannel = busIndex * 2` |
| 12 | Route button text turns green (#40E070) when routing is active, secondary (#8888AA) when manual | VERIFIED | `setRoutingModeHighlight`: sets `kAccentConnect` when `mode > 0` at ConnectionBar.cpp:361-362, `kTextSecondary` otherwise at .cpp:366-367; initialized from persisted state at JamWideJuceEditor.cpp:29-30 |
| 13 | Routing selectors show 'Main Mix' as default item, 'Remote 1' through 'Remote 15' as user-assignable bus options | VERIFIED | `routingSelector.addItem("Main Mix", 1)` at ChannelStrip.cpp:33; `for (int i = 0; i < 15; ++i)` adds `"Remote " + juce::String(i + 1)` at .cpp:34-35 |
| 14 | Routing selector text turns green when routed to a non-default bus | VERIFIED | `onChange` lambda at ChannelStrip.cpp:42-47 sets `0xFF40E070` when `busIndex > 0`, `0xFFDDDDEE` otherwise; `setRoutingBus` mirrors same logic at .cpp:244-249 |

**Score:** 14/14 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `juce/JamWideJuceProcessor.h` | kTotalOutChannels/kNumOutputBuses/kMetronomeBus constants, std::atomic<int> routingMode, outputScratch | VERIFIED | All present: kTotalOutChannels=34 (.h:83), kNumOutputBuses=17 (.h:84), kMetronomeBus=16 (.h:85), `std::atomic<int> routingMode{0}` (.h:119), `juce::AudioBuffer<float> outputScratch` (.h:145) |
| `juce/JamWideJuceProcessor.cpp` | 34-channel processBlock, main-mix accumulation, outputScratch pre-alloc, state persistence | VERIFIED | All present: pre-alloc at .cpp:110, 34-channel AudioProc call at .cpp:213, accumulation loops at .cpp:234-253, getBusBuffer copy at .cpp:263-268, routingMode load/store at .cpp:366/412 |
| `src/threading/ui_command.h` | SetRoutingModeCommand struct, set_outch/outchannel on SetUserChannelStateCommand, updated UiCommand variant | VERIFIED | `set_outch` at .h:65, `outchannel` at .h:66, `struct SetRoutingModeCommand` at .h:83, `SetRoutingModeCommand` in UiCommand variant at .h:97 |
| `src/core/njclient.h` | out_chan_index field in RemoteChannelInfo | VERIFIED | `int out_chan_index = 0` at .h:183 |
| `juce/NinjamRunThread.cpp` | SetRoutingModeCommand dispatch with auto-assign sweep (nch=32), SetUserChannelState outch passthrough, out_chan_index in snapshot, SetMetronomeChannel(32) on connect | VERIFIED | SetRoutingModeCommand handler at .cpp:468 with `? 32 : 0` at .cpp:474; set_outch passthrough at .cpp:444; SetMetronomeChannel(32) at .cpp:305; routingMode.load on connect at .cpp:300 |
| `juce/ui/ConnectionBar.h` | routeButton member, onRouteModeChanged callback, setRoutingModeHighlight method | VERIFIED | `juce::TextButton routeButton` at .h:55; `std::function<void(int)> onRouteModeChanged` at .h:29; `void setRoutingModeHighlight(int mode)` at .h:31 |
| `juce/ui/ConnectionBar.cpp` | Route button with PopupMenu, layout in resized(), atomic routingMode access | VERIFIED | `routeButton.setButtonText("Route")` at .cpp:116; tooltip at .cpp:121; `"Assign by User"` at .cpp:131; `routingMode.load` at .cpp:125; `routingMode.store` at .cpp:148; `routeButton.setBounds(rightX - 52, y, 52, h)` at .cpp:200 |
| `juce/ui/ChannelStrip.h` | setRoutingBus public method declaration | VERIFIED | `void setRoutingBus(int busIndex)` at .h:56 |
| `juce/ui/ChannelStrip.cpp` | Updated routing selector items (Main Mix + Remote 1-15), green text feedback, setRoutingBus implementation | VERIFIED | `addItem("Main Mix", 1)` at .cpp:33; 15-item loop at .cpp:34; green text in onChange at .cpp:42-47; `void ChannelStrip::setRoutingBus(int busIndex)` at .cpp:240 |
| `juce/ui/ChannelStripArea.cpp` | onRoutingChanged callback wiring, routing selector refresh from snapshot out_chan_index | VERIFIED | Single-ch strip: onRoutingChanged at .cpp:444-453, setRoutingBus at .cpp:456-457; multi-ch child strips: same pattern at .cpp:552-565 |
| `juce/JamWideJuceEditor.cpp` | onRouteModeChanged callback wiring, initial Route button highlight | VERIFIED | `onRouteModeChanged` lambda with `SetRoutingModeCommand` at .cpp:23-26; `setRoutingModeHighlight(processorRef.routingMode.load(...))` at .cpp:29-30 |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `juce/JamWideJuceProcessor.cpp` | `src/core/njclient.h` | AudioProc call with 34 output channels | WIRED | `AudioProc(inPtrs, numInputChannels, outPtrs, kTotalOutChannels, ...)` at .cpp:213 |
| `juce/NinjamRunThread.cpp` | `src/core/njclient.h` | SetUserChannelState with set_outch=true | WIRED | `c.set_outch, c.outchannel` passed at NinjamRunThread.cpp:444 |
| `juce/JamWideJuceProcessor.cpp` | `juce/JamWideJuceProcessor.h` | routingMode persisted in getStateInformation/setStateInformation | WIRED | `routingMode.load()` at .cpp:366 (save); `routingMode.store(jlimit(0,2,...))` at .cpp:412 (restore) |
| `juce/ui/ConnectionBar.cpp` | `src/threading/ui_command.h` | onRouteModeChanged callback pushes SetRoutingModeCommand through editor | WIRED | Callback fires `onRouteModeChanged(mode)` at ConnectionBar.cpp:151; editor wires it to `cmd_queue.try_push(cmd)` at JamWideJuceEditor.cpp:23-26 |
| `juce/ui/ChannelStripArea.cpp` | `src/threading/ui_command.h` | SetUserChannelStateCommand with set_outch=true pushed to cmd_queue | WIRED | `cmd.set_outch = true` at ChannelStripArea.cpp:450 (and 558); `cmd_queue.try_push(std::move(cmd))` at .cpp:452 (and 560) |
| `juce/ui/ChannelStripArea.cpp` | `src/core/njclient.h` | out_chan_index read from cachedUsers to update routing selector | WIRED | `ch.out_chan_index / 2` at ChannelStripArea.cpp:456 (single-ch) and .cpp:564 (multi-ch child); passed to `strip->setRoutingBus(busIndex)` |

---

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `juce/JamWideJuceProcessor.cpp` processBlock | `outPtrs[0..33]` | `client->AudioProc(...)` populates all 34 channels from NJClient audio processing | Yes — NJClient fills buffers from decoded remote audio, local monitoring, and metronome | FLOWING |
| `juce/ui/ChannelStrip.cpp` routingSelector | `busIndex` from `out_chan_index` in snapshot | `ch.out_chan_index` in `RemoteChannelInfo` filled by `njclient.cpp` `GetRemoteUsersSnapshot` at .cpp:2753 (`ch_info.out_chan_index = chan->out_chan_index`) | Yes — reflects per-channel routing assignment from NJClient internal state | FLOWING |
| `juce/ui/ConnectionBar.cpp` routeButton highlight | `routingMode` | `processorRef.routingMode` (std::atomic<int>) persisted in plugin state, initialized from `setStateInformation` | Yes — loaded from saved DAW state or defaults to 0 | FLOWING |

---

### Behavioral Spot-Checks

Step 7b: SKIPPED — Build verification was not possible due to missing JUCE and libflac submodules in this environment. All acceptance criteria verified via grep pattern matching against actual committed source files. Both summary files document this constraint explicitly.

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| MOUT-01 | 06-01-PLAN.md | Each remote user routable to a separate stereo output pair in the DAW | SATISFIED | `out_chan_index` in `RemoteChannelInfo`; `SetUserChannelState` passthrough with `set_outch`; `outputScratch` routes each user's audio to individual stereo pair via `out_chan_index + m_remote_chanoffs` in NJClient AudioProc |
| MOUT-02 | 06-02-PLAN.md | Auto-assign mode: by user (one stereo pair per user) | SATISFIED | `SetRoutingModeCommand` with `mode=2`; NinjamRunThread.cpp:468 two-pass sweep; by-user logic reuses `assignedBus` across channels of same user at .cpp:526-534 |
| MOUT-03 | 06-02-PLAN.md | Auto-assign mode: by channel (one stereo pair per remote channel) | SATISFIED | `SetRoutingModeCommand` with `mode=1`; NinjamRunThread.cpp two-pass sweep assigns unique bus per channel via `find_unused_output_channel_pair()` at .cpp:511 |
| MOUT-04 | 06-01-PLAN.md | Metronome routable to its own output pair | SATISFIED | `SetMetronomeChannel(32)` on connect at NinjamRunThread.cpp:305; `kMetronomeBus=16` (channels 32-33) in JamWideJuceProcessor.h:85; separate accumulation block without master vol at .cpp:245-253 |
| MOUT-05 | 06-01-PLAN.md | Main mix always on output bus 0 | SATISFIED | Bus 0 (`outPtrs[0]` and `outPtrs[1]`) is the accumulation target; remote buses and metronome are added into it; getBusBuffer copy writes bus 0 first at .cpp:258-268 |

All 5 MOUT requirements satisfied. No orphaned requirements — REQUIREMENTS.md maps exactly MOUT-01 through MOUT-05 to Phase 6, and both plans claim all five IDs collectively.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| None found | — | — | — | — |

No TODO/FIXME/placeholder/stub comments or empty implementations were found in the 11 files modified by this phase. Both summaries confirm "None" for known stubs.

---

### Human Verification Required

#### 1. DAW multichannel output routing — live audio verification

**Test:** Load the JamWide VST3 in a DAW (Reaper, Logic, or Ableton). Connect to a NINJAM server with at least 2 remote users. Enable additional output buses in the plugin I/O settings (configure 17 stereo outputs). Verify that each user's audio appears on its designated stereo bus (not just bus 0).
**Expected:** Remote user 1 audio on outputs 3-4, remote user 2 on outputs 5-6, etc. Bus 1-2 (main mix) contains all users. Buses 33-34 (metronome) contain only metronome.
**Why human:** Cannot verify audio routing correctness programmatically without a running DAW host with multi-bus output capability.

#### 2. Route button quick-assign live behavior

**Test:** Connect to a session with multiple users. Click Route -> "Assign by User". Observe all routing selectors on channel strips update to show "Remote 1", "Remote 2", etc.
**Expected:** Each user's strip shows a unique Remote N bus label, with green text. Clicking Route -> "Manual (Main Mix)" resets all selectors to "Main Mix" with normal text color.
**Why human:** The two-pass sweep result depends on runtime user count and channel assignments; cannot verify selector text update without a running plugin.

#### 3. Routing mode persistence across DAW save/load

**Test:** Set routing mode to "Assign by User" in the plugin. Save the DAW project. Close and reopen the project. Observe the Route button color and routing mode state.
**Expected:** Route button text is green (kAccentConnect), indicating non-manual routing mode was restored. Connecting to a session re-applies auto-assign at the nch=32 boundary.
**Why human:** State persistence through DAW save/load cycle requires a live DAW environment.

#### 4. Metronome bus isolation

**Test:** In the DAW, route metronome (bus 33-34) to a separate track. Adjust master volume to 0. Verify metronome is still audible on its dedicated bus.
**Expected:** Metronome is unaffected by master volume since it is accumulated into main mix without the masterVol coefficient and appears independently on bus 16.
**Why human:** Requires live audio monitoring across independent output buses.

---

### Gaps Summary

No gaps found. All 14 observable truths verified, all 11 artifacts verified at all four levels (exists, substantive, wired, data-flowing), all 5 key links verified wired, all 5 MOUT requirements satisfied. Four items routed to human verification due to requiring a live DAW environment with audio output.

---

_Verified: 2026-04-04_
_Verifier: Claude (gsd-verifier)_
