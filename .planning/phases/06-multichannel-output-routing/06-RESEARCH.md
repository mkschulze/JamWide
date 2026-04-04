# Phase 6: Multichannel Output Routing - Research

**Researched:** 2026-04-04
**Domain:** JUCE multi-bus audio routing, NJClient per-user output separation, DAW multi-output plugin behavior
**Confidence:** HIGH

## Summary

Phase 6 adds per-user audio routing to separate stereo output buses so DAW users can independently mix and process each remote participant. The codebase is well-prepared: 17 stereo output buses are already declared in BusesProperties, the routingSelector ComboBox exists on each ChannelStrip, and NJClient's internal architecture already supports per-channel `out_chan_index` routing with auto-assign modes (`config_remote_autochan` 1=by-channel, 2=by-user).

The primary work is: (1) expanding the processBlock output buffer passed to AudioProc from 2 channels to 34 channels, (2) accumulating per-bus audio into the main mix after AudioProc returns, (3) wiring the routing selector callbacks and quick-assign button through the command queue, (4) setting the metronome channel to the last bus, and (5) persisting routing mode in plugin state.

**Primary recommendation:** Leverage NJClient's existing `out_chan_index` + `m_remote_chanoffs` + `find_unused_output_channel_pair()` infrastructure. Expand the AudioProc output buffer to 34 channels (17 stereo pairs), set `m_remote_chanoffs = 0`, and post-process in processBlock to accumulate individual buses into the main mix. Do NOT modify NJClient internals for the routing logic -- only add `out_chan_index` to the command passthrough and snapshot structures.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Default routing mode is Manual -- all remote users on Main Mix (bus 0). User opts in to multichannel via quick-assign or per-strip selector.
- **D-02:** Quick-assign button in ConnectionBar (near Fit button) with dropdown: "Assign by User" and "Assign by Channel". Assigns all current users to sequential buses in one click.
- **D-03:** In "by user" mode, multi-channel users get one stereo pair per user (all channels mixed to one bus), not one per channel.
- **D-04:** Per-strip routing selector (already exists from Phase 4) overrides auto-assignment for individual strips.
- **D-05:** When a user leaves, their bus stays reserved until manually reassigned. No automatic shifting of other users' buses.
- **D-06:** When all 16 Remote buses are occupied, new users fall back to Main Mix (bus 0). No error, no warning.
- **D-07:** Expand AudioProc output buffer to capture per-user audio before mixing. Similar to ReaNINJAM approach. Pass larger buffer (2 * numUsers channels) to get individual user audio.
- **D-08:** Main Mix (bus 0) always contains the full mix of all users + metronome + local monitoring, regardless of individual routing. Individual buses get copies.
- **D-09:** Volume/pan from the mixer (Phase 5) applies BEFORE bus routing. Individual buses get the mixed signal. DAW can do its own mixing on the separate tracks.
- **D-10:** Local monitoring audio stays on Main Mix only. Not routable to separate buses (DAW already gets dry input via plugin input).
- **D-11:** Metronome always on the last declared output bus (bus 17 / Remote 16). Fixed, not user-configurable.
- **D-12:** Persist routing mode setting (manual/by-user/by-channel) and metronome bus in plugin state. Individual user-to-bus mappings are NOT persisted.
- **D-13:** Default routing mode on first load: Manual (all on Main Mix).
- **D-14:** Target DAWs: REAPER, Logic Pro, Bitwig Studio. Multi-output plugin behavior tested in all three.
- **D-15:** Show a tooltip/help text in the routing area: "Enable additional outputs in your DAW's plugin I/O settings". DAW-specific instructions in documentation, not in the plugin UI.
- **D-16:** User reports username may not persist correctly. Phase 5 implemented D-22 (persist lastUsername as ValueTree property). Verify during Phase 6 development and fix if broken.

### Claude's Discretion
- AudioProc buffer expansion implementation details (how to extract per-user audio from NJClient internals)
- Quick-assign button UI design (dropdown vs split button)
- Bus labeling in the routing selector (current "Out 1/2" naming is fine, or improve)
- Signal routing implementation in processBlock (how to copy per-user audio to output buses)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| MOUT-01 | Each remote user routable to a separate stereo output pair in the DAW | NJClient `out_chan_index` per-channel routing + expanded 34-channel AudioProc output buffer + JUCE getBusBuffer copy in processBlock |
| MOUT-02 | Auto-assign mode: by user (one stereo pair per user) | NJClient `config_remote_autochan = 2` + `config_remote_autochan_nch = 34` -- existing NJClient auto-assign logic handles this |
| MOUT-03 | Auto-assign mode: by channel (one stereo pair per remote channel) | NJClient `config_remote_autochan = 1` + `config_remote_autochan_nch = 34` -- existing NJClient auto-assign logic handles this |
| MOUT-04 | Metronome routable to its own output pair | `config_metronome_channel` set to 32 (channel index for bus 17, the last stereo pair) + accumulate into main mix |
| MOUT-05 | Main mix always on output bus 0 | Post-AudioProc accumulation loop in processBlock: iterate buses 1-16, add their audio to bus 0 with master volume applied |
</phase_requirements>

## Architecture Patterns

### Signal Flow (The Critical Pattern)

The expanded processBlock signal flow is the core of this phase:

```
processBlock():
  1. Collect inputs from 4 stereo input buses into inputScratch (unchanged)
  2. Prepare 34-channel outputScratch buffer (17 stereo pairs)
  3. Set NJClient routing config:
     - config_remote_autochan_nch = 34
     - config_metronome_channel = 32 (bus 17 L channel)
  4. Call AudioProc(inPtrs, innch, outPtrs, 34, numSamples, srate)
     NJClient internally:
       - Local channels -> outbuf[0..1] (main mix, via m_local_chanoffs=0)
       - Remote channels -> outbuf[out_chan_index] per user
       - Master volume applied to outbuf[0..1] only
       - Metronome -> outbuf[32..33]
  5. Post-process: accumulate all individual buses INTO main mix
     For bus 1..16 (channels 2..33):
       Read master volume from APVTS
       mainMix[L] += bus[L] * masterVol
       mainMix[R] += bus[R] * masterVol
  6. Copy outputScratch buses to JUCE output buses via getBusBuffer()
```

**Key insight (D-08 compliance):** NJClient routes each user to their assigned bus ONLY. The main mix accumulation happens in processBlock as a post-processing step. This means routed users appear on both their individual bus AND the main mix.

**Key insight (D-09 compliance):** Per-user volume/pan is applied by NJClient during mixInChannel(). Master volume is applied by NJClient to channels 0/1 only. When accumulating to main mix, we re-apply master volume to the individual bus audio being added.

### Channel Index Mapping

```
Output channels passed to AudioProc (34 total):
  [0,1]   = Main Mix (bus 0) -- local monitoring + unrouted users + master vol
  [2,3]   = Remote 1 (bus 1) -- out_chan_index = 2
  [4,5]   = Remote 2 (bus 2) -- out_chan_index = 4
  ...
  [30,31] = Remote 15 (bus 15) -- out_chan_index = 30
  [32,33] = Remote 16 (bus 16) -- out_chan_index = 32 (metronome per D-11)
```

NJClient maps: `outbuf[out_chan_index + m_remote_chanoffs]`
- Set `m_remote_chanoffs = 0` (default, no offset needed since out_chan_index directly maps)
- Unrouted users have `out_chan_index = 0` -> main mix

### Routing State Management

```
Processor-level state (non-APVTS, persisted in ValueTree):
  routingMode: int (0=manual, 1=by-channel, 2=by-user)
  
Audio-thread state (via NJClient config fields):
  config_remote_autochan: int (0=manual, 1=by-channel, 2=by-user)
  config_remote_autochan_nch: int (34 = 17 stereo pairs)
  config_metronome_channel: int (32 = bus 17 L channel)
  
Per-channel state (already in NJClient):
  RemoteUser_Channel::out_chan_index (set by auto-assign or manual routing)
```

### Command Queue Pattern for Routing

```cpp
// New command variant for routing changes
struct SetRoutingModeCommand {
    int mode;  // 0=manual, 1=by-channel, 2=by-user
};

// Extended existing command for per-strip routing
struct SetUserChannelStateCommand {
    // ... existing fields ...
    bool set_outch = false;
    int outchannel = 0;
};
```

### Recommended Project Structure Changes

```
juce/
  JamWideJuceProcessor.h    # Add: routingMode, outputScratch buffer
  JamWideJuceProcessor.cpp  # Modify: processBlock, state save/restore
  ui/
    ConnectionBar.h          # Add: routeButton member
    ConnectionBar.cpp        # Add: quick-assign button + dropdown
    ChannelStripArea.cpp     # Wire: onRoutingChanged callbacks
  threading/
src/threading/
    ui_command.h             # Add: SetRoutingModeCommand, extend SetUserChannelState
  NinjamRunThread.cpp        # Add: SetRoutingModeCommand dispatch, routing state passthrough
src/core/
    njclient.h               # No changes needed (already has all required fields)
    njclient.cpp             # No changes needed
```

### Anti-Patterns to Avoid
- **Modifying NJClient's mixInChannel for dual-bus output:** This couples JamWide routing logic into upstream NINJAM code. Instead, accumulate in processBlock.
- **Using APVTS for routing mode:** Routing mode is not an automatable parameter and should not appear in DAW automation lanes. Store as ValueTree property (same pattern as scaleFactor, chatSidebarVisible).
- **Persisting per-user bus assignments:** Users change between sessions (D-12, D-24 from Phase 5). Only persist the routing mode, not individual mappings.
- **Calling NJClient API from message thread:** All NJClient state changes must go through the SPSC command queue. The routingSelector callback must push a command, not call SetUserChannelState directly.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Per-user bus auto-assign | Custom bus allocation algorithm | NJClient `config_remote_autochan` + `find_unused_output_channel_pair()` | Already handles user-mode vs channel-mode, tracks used buses, finds unused pairs |
| Per-channel output routing | Custom mixing loop | NJClient `out_chan_index` on RemoteUser_Channel | Already routes each channel to specified output pair in mixInChannel() |
| Metronome bus routing | Custom metronome copier | NJClient `config_metronome_channel` | Already writes metronome to configurable output channel pair |
| Multi-bus JUCE output | Manual channel index arithmetic | `getBusBuffer(buffer, false, busIndex)` | JUCE handles channel mapping for enabled/disabled buses |

**Key insight:** NJClient already has a complete multichannel output routing system (inherited from ReaNINJAM). The only new logic needed is the main-mix accumulation and the JUCE bus copy loop.

## Common Pitfalls

### Pitfall 1: Disabled Buses Have Zero Channels in processBlock
**What goes wrong:** Calling `getBusBuffer(buffer, false, busIndex)` on a disabled bus returns a buffer with 0 channels. Writing to it causes undefined behavior.
**Why it happens:** DAWs may only enable some output buses. Logic Pro requires the user to activate auxiliary outputs manually. REAPER enables all declared buses by default.
**How to avoid:** Always check `getBus(false, busIndex)->isEnabled()` before writing. Only copy audio to enabled buses. The outputScratch buffer still gets the full 34-channel AudioProc output regardless of which JUCE buses are enabled.
**Warning signs:** Crashes in DAWs that don't auto-enable all outputs. Buffer overruns.

### Pitfall 2: Master Volume Double-Application
**What goes wrong:** Main mix accumulation applies master volume to individual bus audio, but master volume was already applied to the main mix by NJClient.
**Why it happens:** NJClient applies master vol/mute to outbuf[0..1] inside process_samples(). If you accumulate individual bus audio into main mix without applying master volume, those users bypass the master fader.
**How to avoid:** Read master volume atomics and apply during accumulation. Do NOT re-apply to audio already on bus 0.
**Warning signs:** Routed users sound louder on main mix than unrouted users. Master fader doesn't affect routed users in main mix.

### Pitfall 3: Auto-Assign Timing with NJClient Run Thread
**What goes wrong:** Setting `config_remote_autochan` from the message thread while the Run thread processes new user connections causes inconsistent routing state.
**Why it happens:** `config_remote_autochan` and `config_remote_autochan_nch` are non-atomic fields accessed by the Run thread.
**How to avoid:** Set these fields via the command queue, dispatched on the Run thread under `clientLock`. The `SetRoutingModeCommand` should update both `config_remote_autochan` and `config_remote_autochan_nch` atomically.
**Warning signs:** Race conditions where some users get auto-assigned and others don't when switching modes.

### Pitfall 4: Quick-Assign Does Not Update Already-Connected Users
**What goes wrong:** Setting `config_remote_autochan` only affects NEW users joining. Users who connected before the mode change retain `out_chan_index = 0`.
**Why it happens:** NJClient's auto-assign runs in the channel announcement handler (`on_new_user_channel`), not retroactively.
**How to avoid:** Quick-assign must iterate all current users and set their `out_chan_index` explicitly via `SetUserChannelState`. The auto-assign mode is set for future users AND a sweep of existing users is performed.
**Warning signs:** Quick-assign button "doesn't work" -- users already connected stay on main mix.

### Pitfall 5: Logic Pro AU Cache Invalidation
**What goes wrong:** Logic Pro caches AU plugin bus configuration. If the plugin previously loaded as stereo-only, Logic may not offer the multi-output version.
**Why it happens:** Logic Pro aggressively caches Audio Unit validation results.
**How to avoid:** After changing bus configuration, clear AU cache: `killall -9 AudioComponentRegistrar` and delete `/Library/Caches/AudioUnitCache/`. Document this in user instructions per D-15.
**Warning signs:** Plugin loads as stereo-only in Logic despite declaring 17 output buses.

### Pitfall 6: outputScratch Buffer Not Cleared Before AudioProc
**What goes wrong:** AudioProc zeroes output channels at the start, but only for `outnch` channels. If the scratch buffer has leftover data from a previous block, it contaminates the output.
**Why it happens:** AudioProc's first operation is `memset(outbuf[x], 0, sizeof(float)*len)` for all `outnch` channels, so this is actually safe. BUT if `outputScratch.setSize()` is called with `keepExisting=true`, old data persists until AudioProc zeros it.
**How to avoid:** Use `outputScratch.setSize(34, numSamples, false, false, true)` -- third parameter `false` means don't keep existing content. Or simply trust AudioProc's internal zeroing.
**Warning signs:** Stale audio data on buses, ghost audio from previous blocks.

## Code Examples

### Expanded processBlock (Core Pattern)

```cpp
// Source: JamWideJuceProcessor.cpp -- processBlock modification
void JamWideJuceProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // ... input collection unchanged ...

    // Expanded output: 17 stereo pairs = 34 mono channels
    static constexpr int kTotalOutChannels = 34;  // 17 buses * 2
    outputScratch.setSize(kTotalOutChannels, numSamples, false, false, true);

    float* outPtrs[kTotalOutChannels];
    for (int ch = 0; ch < kTotalOutChannels; ++ch)
        outPtrs[ch] = outputScratch.getWritePointer(ch);

    client->AudioProc(inPtrs, numInputChannels, outPtrs, kTotalOutChannels,
                      numSamples, static_cast<int>(storedSampleRate));

    // Accumulate individual buses into main mix (D-08)
    float masterVol = client->config_mastermute.load(std::memory_order_relaxed)
                      ? 0.0f
                      : client->config_mastervolume.load(std::memory_order_relaxed);
    float masterPan = client->config_masterpan.load(std::memory_order_relaxed);
    float mvL = masterVol, mvR = masterVol;
    if (masterPan > 0.0f) mvL *= 1.0f - masterPan;
    else if (masterPan < 0.0f) mvR *= 1.0f + masterPan;

    float* mainL = outPtrs[0];
    float* mainR = outPtrs[1];
    for (int bus = 1; bus < 17; ++bus)
    {
        const float* busL = outPtrs[bus * 2];
        const float* busR = outPtrs[bus * 2 + 1];
        for (int s = 0; s < numSamples; ++s)
        {
            mainL[s] += busL[s] * mvL;
            mainR[s] += busR[s] * mvR;
        }
    }

    // Copy to JUCE output buses
    const int numOutputBuses = getBusCount(false);
    for (int bus = 0; bus < numOutputBuses && bus < 17; ++bus)
    {
        auto* outputBus = getBus(false, bus);
        if (outputBus == nullptr || !outputBus->isEnabled())
            continue;
        auto busBuffer = getBusBuffer(buffer, false, bus);
        int busCh = busBuffer.getNumChannels();
        if (busCh >= 1)
            busBuffer.copyFrom(0, 0, outputScratch, bus * 2, 0, numSamples);
        if (busCh >= 2)
            busBuffer.copyFrom(1, 0, outputScratch, bus * 2 + 1, 0, numSamples);
    }

    // Master VU from main mix
    float masterPeakL = outputScratch.getMagnitude(0, 0, numSamples);
    float masterPeakR = outputScratch.getMagnitude(1, 0, numSamples);
    uiSnapshot.master_vu_left.store(masterPeakL, std::memory_order_relaxed);
    uiSnapshot.master_vu_right.store(masterPeakR, std::memory_order_relaxed);
}
```

### SetUserChannelStateCommand Extension

```cpp
// Source: src/threading/ui_command.h
struct SetUserChannelStateCommand {
    int user_index = 0;
    int channel_index = 0;
    bool set_sub = false;
    bool subscribed = false;
    bool set_vol = false;
    float volume = 1.0f;
    bool set_pan = false;
    float pan = 0.0f;
    bool set_mute = false;
    bool mute = false;
    bool set_solo = false;
    bool solo = false;
    // NEW: output channel routing
    bool set_outch = false;
    int outchannel = 0;  // stereo pair channel index (0, 2, 4, ..., 32)
};

// Dispatch in NinjamRunThread.cpp:
client->SetUserChannelState(
    c.user_index, c.channel_index,
    c.set_sub, c.subscribed,
    c.set_vol, c.volume,
    c.set_pan, c.pan,
    c.set_mute, c.mute,
    c.set_solo, c.solo,
    c.set_outch, c.outchannel);
```

### SetRoutingModeCommand (New)

```cpp
// Source: src/threading/ui_command.h
struct SetRoutingModeCommand {
    int mode;  // 0=manual, 1=by-channel, 2=by-user
};

// Dispatch in NinjamRunThread.cpp:
client->config_remote_autochan = c.mode;
client->config_remote_autochan_nch = (c.mode > 0) ? 34 : 0;

// For quick-assign: also iterate all existing users and assign buses
if (c.mode > 0) {
    // Reset all existing channels to out_chan_index = 0 first
    // Then re-auto-assign using find_unused_output_channel_pair() logic
}
```

### Quick-Assign Sweep (Run Thread)

```cpp
// When quick-assign triggered, after setting config_remote_autochan:
// Reset all users to bus 0, then re-assign
for (int u = 0; u < client->GetNumUsers(); ++u) {
    int chIdx = 0;
    while ((chIdx = client->EnumUserChannels(u, chIdx)) >= 0) {
        // Reset to 0
        client->SetUserChannelState(u, chIdx,
            false, false, false, 0.0f, false, 0.0f,
            false, false, false, false,
            true, 0);  // set_outch=true, outchannel=0
    }
}
// Then trigger auto-assign by temporarily toggling user subscription
// OR implement explicit assign loop using find_unused_output_channel_pair()
```

### RemoteChannelInfo Extension for UI

```cpp
// Source: src/core/njclient.h -- RemoteChannelInfo struct
struct RemoteChannelInfo {
    // ... existing fields ...
    int out_chan_index = 0;  // NEW: current output routing
};

// Source: src/core/njclient.cpp -- GetRemoteUsersSnapshot
ch_info.out_chan_index = chan->out_chan_index;  // Add to snapshot
```

### Routing Selector Callback Wiring

```cpp
// Source: juce/ui/ChannelStripArea.cpp -- in refreshFromUsers/rebuildStrips
strip->onRoutingChanged = [this, uName, cName](int busIndex) {
    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
    if (uIdx < 0) return;
    jamwide::SetUserChannelStateCommand cmd;
    cmd.user_index = uIdx;
    cmd.channel_index = cIdx;
    cmd.set_outch = true;
    cmd.outchannel = busIndex * 2;  // Convert bus index to channel pair offset
    processorRef.cmd_queue.try_push(cmd);
};
```

### State Persistence

```cpp
// In getStateInformation:
state.setProperty("routingMode", routingMode, nullptr);

// In setStateInformation:
routingMode = tree.getProperty("routingMode", 0);  // 0 = manual (D-13)
// Apply on next connect via command queue
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Logic Pro AU cache issues | Fixed in Logic Pro 11.0.1 | 2024 | No workaround needed for host type detection |
| JUCE multi-bus with disabled aux | All aux buses enabled in constructor | JUCE 7+ | Ensures DAW sees all possible outputs |
| ReaNINJAM per-user routing | Same NJClient internals used by JamWide | Since NINJAM inception | Full reuse of proven routing logic |

## JUCE Multi-Bus DAW Behavior

| DAW | Output Activation | Expected Behavior |
|-----|-------------------|-------------------|
| **REAPER** | VST3: all declared outputs available automatically. User enables tracks via "Build multichannel routing for output" in FX chain | Simplest -- works out of the box |
| **Logic Pro** | AU: User must click "+" under the plugin to add auxiliary outputs. VST3 not supported for instruments in Logic | Requires user action; document in tooltip per D-15 |
| **Bitwig Studio** | VST3: all outputs auto-available. User enables via device panel "Show extra outputs" | Works with minimal user action |

**Critical note:** The existing `isBusesLayoutSupported()` in JamWideJuceProcessor already validates all output buses as stereo-or-disabled. This is correct and does NOT need modification. The `withOutput("Remote N", stereo, false)` declaration (default disabled) is correct -- DAWs enable buses as the user requests them.

## Open Questions

1. **Quick-assign sweep implementation on Run thread**
   - What we know: `config_remote_autochan` only affects new channel announcements. Existing users need explicit `SetUserChannelState` calls to update `out_chan_index`.
   - What's unclear: The most efficient way to reset-and-reassign all users. NJClient's `find_unused_output_channel_pair()` is available but was designed for incremental assignment during connection, not bulk reassignment.
   - Recommendation: Implement a dedicated sweep in the `SetRoutingModeCommand` handler on the Run thread. Reset all `out_chan_index` to 0, then iterate users and call `find_unused_output_channel_pair()` for each. This reuses existing NJClient logic without modification.

2. **Username persistence bug (D-16)**
   - What we know: Phase 5 implemented D-22 (persist lastUsername as ValueTree property). Code exists in getStateInformation/setStateInformation.
   - What's unclear: Whether the bug is in save or restore. The code looks correct.
   - Recommendation: Add a diagnostic log during setStateInformation to verify the restored value. Test round-trip save/load.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | No project-level test framework exists. CI uses pluginval for AU/VST3 validation. |
| Config file | `.github/workflows/juce-build.yml` (pluginval step) |
| Quick run command | `cmake --build build --config Release -j $(sysctl -n hw.ncpu)` |
| Full suite command | Build + pluginval validation via CI |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| MOUT-01 | Each remote user routable to separate stereo output pair | manual (DAW) | Build + load in DAW, verify separate tracks | N/A |
| MOUT-02 | Auto-assign by user mode | manual (DAW) | Quick-assign "By User", verify bus assignment | N/A |
| MOUT-03 | Auto-assign by channel mode | manual (DAW) | Quick-assign "By Channel", verify bus assignment | N/A |
| MOUT-04 | Metronome on own output pair | manual (DAW) | Verify metronome audio on bus 17 in DAW | N/A |
| MOUT-05 | Main mix always on bus 0 | manual (DAW) | Verify bus 0 contains all users + metronome | N/A |

### Sampling Rate
- **Per task commit:** `cmake --build build --config Release -j $(sysctl -n hw.ncpu)` (compile check)
- **Per wave merge:** Build succeeds + pluginval passes (CI)
- **Phase gate:** Manual DAW testing in REAPER, Logic Pro, and Bitwig

### Wave 0 Gaps
- No automated unit tests exist for audio routing logic. This is consistent with the project's established pattern (no test framework, relies on pluginval + manual DAW testing).
- Audio routing correctness must be verified by loading the plugin in target DAWs and confirming per-user separation.

## Sources

### Primary (HIGH confidence)
- **JamWide source code** -- JamWideJuceProcessor.cpp (processBlock, BusesProperties, state), njclient.h/cpp (AudioProc, out_chan_index, config_remote_autochan, find_unused_output_channel_pair, mixInChannel, metronome routing), ChannelStrip.h (routingSelector), ChannelStripArea.cpp (callback wiring pattern), ui_command.h (SetUserChannelStateCommand)
- **ReaNINJAM reference** -- `/Users/cell/dev/ninjam/ninjam/njclient.cpp` (identical auto-assign logic, same mixInChannel routing)
- **JUCE docs** -- [Bus layouts tutorial](https://juce.com/tutorials/tutorial_audio_bus_layouts) (isBusesLayoutSupported, getBusBuffer)

### Secondary (MEDIUM confidence)
- **JUCE Forum** -- [Multi-output AU/VST3 compatibility](https://forum.juce.com/t/getting-multiple-buses-working-for-au-and-vst3/60078) -- confirmed multi-out works in REAPER, Logic, Bitwig
- **JUCE Forum** -- [Logic Pro host type fix](https://forum.juce.com/t/pluginhosttype-logic-and-multi-output/62082) -- confirmed fix in Logic 11.0.1

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- No new libraries needed, entire implementation uses existing NJClient + JUCE APIs
- Architecture: HIGH -- Pattern directly follows ReaNINJAM reference implementation, adapted for JUCE multi-bus
- Pitfalls: HIGH -- Identified from direct code analysis of NJClient routing internals and JUCE forum reports

**Research date:** 2026-04-04
**Valid until:** 2026-05-04 (stable domain, no moving targets)
