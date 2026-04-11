# Phase 14: MIDI Remote Control - Research

**Researched:** 2026-04-11
**Domain:** MIDI CC mapping, bidirectional feedback, JUCE MIDI API, APVTS parameter expansion
**Confidence:** HIGH

## Summary

Phase 14 adds MIDI CC control of all mixer parameters with bidirectional feedback and MIDI Learn UX. The implementation closely mirrors the existing OSC server architecture (Phase 9/10) -- same processor-owned component, timer-based dirty-flag sender, echo suppression, and status dot pattern. The key new elements are: (1) 69 new APVTS parameters for remote users, local solo, and metro pan; (2) a MidiMapper component that processes incoming CC from the host's MidiBuffer (plugin mode) or from a directly-opened MidiInput device (standalone mode); (3) a right-click MIDI Learn workflow on faders/knobs/buttons; and (4) CC feedback output via MidiBuffer (plugin mode) or MidiOutput device (standalone mode).

The JUCE framework provides all necessary MIDI primitives. `juce::MidiMessage::isController()`, `getControllerNumber()`, `getControllerValue()`, and `controllerEvent()` handle CC parsing and creation. `juce::MidiInput`/`MidiOutput` in `juce_audio_devices` (already transitively linked via `juce_audio_utils`) provide standalone device enumeration. The processBlock MidiBuffer parameter, currently ignored (`/*midiMessages*/`), becomes the primary MIDI I/O path in plugin mode once `acceptsMidi()` returns true.

**Primary recommendation:** Implement as two plans: Plan 01 = APVTS parameter expansion + MidiMapper core (CC processing, mapping storage, echo suppression, state persistence, APVTS-to-NJClient remote sync) with plugin-mode I/O through processBlock MidiBuffer; Plan 02 = MIDI Learn UX (right-click on components), mapping table dialog, MIDI config dialog for standalone device selection, and MidiStatusDot in footer.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** MIDI Learn via right-click context menu on any fader/knob/button. Right-click -> "MIDI Learn" starts listening, user moves CC on controller, mapping created. Also "Clear MIDI" option in same menu. Standard DAW convention (Ableton, Bitwig, REAPER pattern).
- **D-02:** Visual feedback during learn: parameter gets a colored highlight/pulsing border, small overlay shows "Waiting for CC..." then "CC 7 Ch 1" when received. Auto-closes after successful assignment.
- **D-03:** Mapping table dialog accessible from footer or menu. Shows all active mappings: Parameter | CC# | Channel | Range. Users can delete or edit mappings from this view.
- **D-04:** Plugin mode uses host MIDI routing. Set acceptsMidi() to true, receive MIDI through processBlock MidiBuffer. User routes their MIDI controller to JamWide's input in the DAW.
- **D-05:** Standalone mode gets its own MIDI device selector in a config dialog. Uses juce_audio_devices for device enumeration (MidiInput/MidiOutput). Device selection persists across sessions.
- **D-06:** Separate MIDI status dot in the footer, next to the existing OSC status dot. Same 3-state pattern: green = active (receiving MIDI), grey = off/no mappings, red = error.
- **D-07:** 7-bit standard CC only (0-127). No 14-bit CC pairs or NRPN.
- **D-08:** Mute and solo use CC toggle: any CC value > 0 toggles the state, value 0 is ignored (button release).
- **D-09:** Mappings store CC# + MIDI Channel as the identity key. Same CC number on different channels maps to different parameters. Up to 16 channels x 128 CCs = 2048 unique mappings.
- **D-10:** Volume maps CC 0-127 linearly to the 0.0-1.0 normalized range. Pan maps CC 0-127 to 0.0-1.0 with 64 as center (0.5). Same value space as OSC normalized namespace.
- **D-11:** Echo suppression mirrors OSC pattern (Phase 9 D-14). When a value changes from MIDI input, mark it 'MIDI-sourced' and skip sending CC feedback for one tick.
- **D-14:** Create fixed APVTS parameters for Remote users 1-16 group-level: remoteVol_0..15, remotePan_0..15, remoteMute_0..15, remoteSolo_0..15. 64 new parameters.
- **D-15:** Promote local channel solo to APVTS: localSolo_0..3 (4 new parameters).
- **D-16:** Promote metronome pan to APVTS: metroPan (1 new parameter).
- **D-17:** When a remote slot is empty (no user connected), reset its APVTS parameters to defaults: volume 1.0, pan center, unmuted, unsoloed.
- **D-18:** Remote sub-channel controls remain cmd_queue dispatch only (not APVTS).
- **D-19:** Total new APVTS parameter count: 69 (64 remote group + 4 local solo + 1 metro pan). Combined with existing 16 = 85 total APVTS parameters.
- **D-20:** MIDI mappings persist across DAW sessions via getStateInformation/setStateInformation. Bump state version (from current version 2). Store array of mapping entries.
- **D-21:** Standalone MIDI device selection persists separately (device name string in state).

### Claude's Discretion
- Feedback sending mechanism (timer-based dirty-flag, immediate, or processBlock output)
- Feedback timer interval if timer-based (100ms matching OSC, or different)
- Internal data structure for mapping storage (std::map, std::unordered_map, flat vector)
- MIDI config dialog layout and exact contents
- Whether producesMidi() returns true (depends on feedback mechanism choice)
- How MIDI feedback works in standalone vs plugin (may need different paths)
- Exact power curve for CC-to-volume mapping (linear CC or matched to VbFader's 2.5 exponent)
- Whether to link juce_audio_devices for all builds or standalone-only

### Deferred Ideas (OUT OF SCOPE)
- 14-bit CC pairs / NRPN support
- Note On/Off for toggle parameters
- Remote sub-channel APVTS exposure
- Phone-optimized TouchOSC template variant with MIDI fallback
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| MIDI-01 | User can map MIDI CC to any mixer parameter (local, remote, master, metronome) with bidirectional feedback and persistent mappings | Full architecture documented: MidiMapper component handles CC-to-parameter mapping via APVTS + cmd_queue, bidirectional feedback via dirty-flag timer (OSC pattern), persistence via state version 3 ValueTree serialization. MIDI Learn UX via right-click context menu. All 85 APVTS parameters (existing 16 + 69 new) are mappable, plus cmd_queue-only sub-channel controls. APVTS-to-NJClient sync via MidiMapper::timerCallback ensures remote user MIDI control actually affects audio. |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| juce::MidiMessage | JUCE 8.x (bundled) | CC parsing: isController(), getControllerNumber(), getControllerValue(), controllerEvent() | [VERIFIED: libs/JUCE source juce_MidiMessage.h] JUCE built-in, no external dependency |
| juce::MidiBuffer | JUCE 8.x (bundled) | processBlock MIDI I/O buffer for plugin mode | [VERIFIED: libs/JUCE source juce_MidiBuffer.h] Standard plugin MIDI pathway |
| juce::MidiInput / juce::MidiOutput | JUCE 8.x (bundled) | Standalone device enumeration and direct MIDI I/O | [VERIFIED: libs/JUCE source juce_MidiDevices.h] Part of juce_audio_devices module |
| juce::AudioProcessorValueTreeState | JUCE 8.x (bundled) | 69 new parameters for remote users, local solo, metro pan | [VERIFIED: existing createParameterLayout() in JamWideJuceProcessor.cpp] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| juce::Timer | JUCE 8.x (bundled) | Dirty-flag feedback sender and APVTS-to-NJClient sync (100ms interval) | MIDI feedback output on message thread + remote param sync |
| juce::CallOutBox | JUCE 8.x (bundled) | MIDI config popup dialog | Same pattern as OscConfigDialog |
| juce::PopupMenu | JUCE 8.x (bundled) | Right-click MIDI Learn context menu | On fader/knob/button right-click |
| juce::ValueTree | JUCE 8.x (bundled) | State persistence for MIDI mappings | getStateInformation / setStateInformation |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Timer-based dirty feedback | processBlock MidiBuffer output | processBlock runs on audio thread; feedback must read APVTS state -- safe with atomics but couples audio thread to MIDI feedback timing. Timer-based (message thread) is simpler and matches OSC pattern. |
| std::unordered_map for mappings | Flat sorted vector | Flat vector is cache-friendly for < 2048 entries but O(n) lookup. unordered_map is O(1) lookup. For CC dispatch in processBlock (audio thread), lookup must be fast. Recommend std::unordered_map with key = (channel << 7 | cc). |

**Installation:**
No new dependencies. All MIDI classes are part of JUCE modules already linked. `juce_audio_devices` is transitively available via `juce_audio_utils` which is already in CMakeLists.txt target_link_libraries. [VERIFIED: CMakeLists.txt line 183 links juce::juce_audio_utils; juce_audio_utils.h declares dependency on juce_audio_devices]

## Architecture Patterns

### Recommended Project Structure
```
juce/
  midi/                    # New directory (mirrors juce/osc/)
    MidiMapper.h           # Core: mapping table, CC dispatch, feedback sender, APVTS-NJClient sync
    MidiMapper.cpp
    MidiLearnManager.h     # MIDI Learn state machine (listening, assignment)
    MidiLearnManager.cpp
    MidiConfigDialog.h     # Standalone device selector + mapping table view
    MidiConfigDialog.cpp
    MidiStatusDot.h        # Footer indicator (mirrors OscStatusDot)
    MidiStatusDot.cpp
```

### Pattern 1: MidiMapper (Core Component)
**What:** Processor-owned component that manages CC-to-parameter mappings, processes incoming MIDI, sends feedback, and syncs remote APVTS params to NJClient.
**When to use:** Always -- this is the central MIDI subsystem.

```cpp
// Source: Derived from OscServer pattern [VERIFIED: juce/osc/OscServer.h]
class MidiMapper : private juce::Timer
{
public:
    explicit MidiMapper(JamWideJuceProcessor& proc);

    // Called from processBlock (audio thread) -- parse CC from MidiBuffer
    void processIncomingMidi(const juce::MidiBuffer& buffer);

    // Called from processBlock (audio thread) -- append CC feedback to MidiBuffer
    void appendFeedbackMidi(juce::MidiBuffer& buffer);

    // Mapping management (message thread)
    void addMapping(const juce::String& paramId, int ccNumber, int midiChannel);
    void removeMapping(const juce::String& paramId);
    void clearAllMappings();

    // State persistence
    void saveToState(juce::ValueTree& state);
    void loadFromState(const juce::ValueTree& state);

    // Standalone device management
    void openMidiInput(const juce::String& deviceId);
    void openMidiOutput(const juce::String& deviceId);

    // Status queries
    bool hasActiveMappings() const;
    bool hasError() const;
    bool isReceiving() const;

private:
    void timerCallback() override; // APVTS-to-NJClient sync + standalone feedback

    struct Mapping {
        juce::String paramId;
        int ccNumber;
        int midiChannel;
    };

    // Key: (channel << 7) | ccNumber -- 14-bit composite for O(1) lookup
    std::unordered_map<int, Mapping> ccToParam_;
    // Reverse lookup: paramId -> composite key
    std::unordered_map<juce::String, int> paramToCc_;

    JamWideJuceProcessor& processor;
};
```

### Pattern 2: Audio Thread CC Dispatch
**What:** Process incoming MIDI CC on the audio thread, apply to APVTS parameters immediately.
**When to use:** In processBlock for zero-latency parameter control.

```cpp
// Source: JUCE MidiMessage API [VERIFIED: juce_MidiMessage.h]
void MidiMapper::processIncomingMidi(const juce::MidiBuffer& buffer)
{
    for (const auto metadata : buffer)
    {
        auto msg = metadata.getMessage();
        if (!msg.isController())
            continue;

        int cc = msg.getControllerNumber();
        int ch = msg.getChannel(); // 1-based in JUCE
        int value = msg.getControllerValue(); // 0-127

        int key = ((ch - 1) << 7) | cc; // 0-based channel for storage
        auto it = ccToParam_.find(key);
        if (it == ccToParam_.end())
            continue;

        const auto& mapping = it->second;
        float normalizedValue = static_cast<float>(value) / 127.0f;

        // Apply to APVTS parameter (thread-safe -- setValueNotifyingHost is atomic)
        auto* param = processor.apvts.getParameter(mapping.paramId);
        if (param != nullptr)
        {
            // For bool params (mute/solo): toggle on value > 0, ignore value == 0
            if (dynamic_cast<juce::AudioParameterBool*>(param))
            {
                if (value > 0)
                    param->setValueNotifyingHost(param->getValue() >= 0.5f ? 0.0f : 1.0f);
            }
            else
            {
                param->setValueNotifyingHost(normalizedValue);
            }
        }

        // Mark echo suppression
        midiSourced_[key] = true;
    }
}
```

### Pattern 3: MIDI Learn State Machine
**What:** Transient listening mode where the next CC received creates a mapping.
**When to use:** When user right-clicks a parameter and selects "MIDI Learn".

```cpp
// Source: Standard DAW MIDI Learn pattern [ASSUMED]
class MidiLearnManager
{
public:
    // Start learning for a specific parameter
    void startLearning(const juce::String& paramId,
                       std::function<void(int cc, int ch)> onLearned);

    // Called from MidiMapper when CC received during learn mode
    bool isLearning() const;
    bool tryLearn(int ccNumber, int midiChannel);

    // Cancel learning (timeout or user action)
    void cancelLearning();

private:
    std::atomic<bool> learning_{false};
    juce::String learningParamId_;
    std::function<void(int, int)> onLearnedCallback_;
};
```

### Pattern 4: Feedback via processBlock MidiBuffer (Plugin Mode)
**What:** Append CC messages to the outgoing MidiBuffer in processBlock for motorized controllers.
**When to use:** Plugin mode where host handles MIDI output routing.

```cpp
// Source: JUCE MidiMessage::controllerEvent [VERIFIED: juce_MidiMessage.h]
void MidiMapper::appendFeedbackMidi(juce::MidiBuffer& buffer)
{
    // Iterate all mappings, check for dirty parameters
    for (const auto& [key, mapping] : ccToParam_)
    {
        // Skip if echo-suppressed
        if (midiSourced_.count(key) && midiSourced_[key])
        {
            midiSourced_[key] = false;
            continue;
        }

        auto* param = processor.apvts.getParameter(mapping.paramId);
        if (!param) continue;

        float currentNorm = param->getValue();
        int ccValue = juce::roundToInt(currentNorm * 127.0f);

        // Dirty check
        if (ccValue != lastSentCcValues_[key])
        {
            lastSentCcValues_[key] = ccValue;
            int channel = (key >> 7) + 1; // Back to 1-based
            int cc = key & 0x7F;
            buffer.addEvent(
                juce::MidiMessage::controllerEvent(channel, cc, ccValue),
                0); // sample offset 0 = start of block
        }
    }
}
```

### Pattern 5: APVTS Parameter Expansion
**What:** Add 69 new parameters to createParameterLayout for remote user controls, local solo, and metro pan.
**When to use:** Plan 01 -- foundational change needed before any MIDI mapping can target these parameters.

```cpp
// Source: Existing createParameterLayout() pattern [VERIFIED: JamWideJuceProcessor.cpp line 69]
// Add to createParameterLayout():

// Remote user group controls (D-14): 16 users x 4 params = 64
for (int i = 0; i < 16; ++i)
{
    juce::String suffix = juce::String(i);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"remoteVol_" + suffix, 3},  // version 3
        "Remote " + juce::String(i + 1) + " Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"remotePan_" + suffix, 3},
        "Remote " + juce::String(i + 1) + " Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"remoteMute_" + suffix, 3},
        "Remote " + juce::String(i + 1) + " Mute", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"remoteSolo_" + suffix, 3},
        "Remote " + juce::String(i + 1) + " Solo", false));
}

// Local solo (D-15): 4 channels
for (int ch = 0; ch < 4; ++ch)
{
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"localSolo_" + juce::String(ch), 3},
        "Local Ch" + juce::String(ch + 1) + " Solo", false));
}

// Metro pan (D-16)
params.push_back(std::make_unique<juce::AudioParameterFloat>(
    juce::ParameterID{"metroPan", 3},
    "Metronome Pan",
    juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
```

### Pattern 6: APVTS-to-NJClient Remote Sync (Timer-Based)
**What:** Sync remote user APVTS parameters to NJClient via cmd_queue on the message thread.
**When to use:** In MidiMapper::timerCallback, every 100ms. Critical for making MIDI control of remote users actually affect audio.

```cpp
// Source: Derived from OscServer dirty-flag pattern [VERIFIED: juce/osc/OscServer.cpp]
// Runs on message thread via Timer -- safe for cmd_queue.push (single-producer invariant)
void MidiMapper::timerCallback()
{
    const int count = juce::jmin(processor.userCount.load(std::memory_order_relaxed), 16);
    for (int i = 0; i < count; ++i)
    {
        juce::String suffix = juce::String(i);
        float vol = *processor.apvts.getRawParameterValue("remoteVol_" + suffix);
        float pan = *processor.apvts.getRawParameterValue("remotePan_" + suffix);
        bool mute = *processor.apvts.getRawParameterValue("remoteMute_" + suffix) >= 0.5f;

        bool volChanged = std::abs(vol - lastSyncedRemoteVol_[i]) > 0.001f;
        bool panChanged = std::abs(pan - lastSyncedRemotePan_[i]) > 0.001f;
        bool muteChanged = mute != lastSyncedRemoteMute_[i];

        if (volChanged || panChanged || muteChanged)
        {
            jamwide::SetUserStateCommand cmd;
            cmd.user_index = i;
            cmd.set_vol = volChanged;  cmd.volume = vol;
            cmd.set_pan = panChanged;  cmd.pan = pan;
            cmd.set_mute = muteChanged; cmd.mute = mute;
            processor.cmd_queue.push(std::move(cmd));

            if (volChanged) lastSyncedRemoteVol_[i] = vol;
            if (panChanged) lastSyncedRemotePan_[i] = pan;
            if (muteChanged) lastSyncedRemoteMute_[i] = mute;
        }
    }
    // ... also handle standalone MIDI output feedback here ...
}
```

**IMPORTANT:** Do NOT sync remote APVTS to NJClient in processBlock. The cachedUsersMutex would be locked on the audio thread, risking priority inversion. The timer-based approach on the message thread is safe and matches the OscServer pattern.

### Anti-Patterns to Avoid
- **Allocating in processBlock:** Never create std::string, juce::String, or allocate memory in the audio thread CC dispatch path. Use pre-allocated lookup structures. [VERIFIED: existing processBlock avoids allocation]
- **Locking in processBlock:** The CC lookup map must be read-only during audio processing. Map modifications (add/remove mapping) happen on the message thread; use a swap-on-update pattern or atomic pointer exchange.
- **Direct cmd_queue push from audio thread:** The SPSC cmd_queue has a single-producer invariant (message thread only). MIDI CC from processBlock runs on the audio thread. For remote sub-channel controls (D-18, cmd_queue-only), dispatch via callAsync to message thread first, same as OSC. [VERIFIED: OscServer.cpp uses callAsync for all cmd_queue pushes]
- **Ignoring parameter version IDs:** New APVTS parameters use version 3 in ParameterID. Old DAW states (version 1-2) will load with defaults for these new parameters. Do NOT change existing parameter version IDs -- only new params get version 3.
- **Syncing remote APVTS in processBlock:** Do NOT lock cachedUsersMutex on audio thread -- priority inversion risk. Use timer-based sync on message thread instead.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| MIDI CC parsing | Raw byte manipulation of MIDI status bytes | `juce::MidiMessage::isController()`, `getControllerNumber()`, `getControllerValue()` | Handles running status, system messages, edge cases [VERIFIED: juce_MidiMessage.h] |
| MIDI CC creation | Manual byte construction | `juce::MidiMessage::controllerEvent(channel, cc, value)` | Correct status byte construction guaranteed [VERIFIED: juce_MidiMessage.h] |
| Standalone device enumeration | Platform-specific CoreMIDI/WinMM/ALSA code | `juce::MidiInput::getAvailableDevices()`, `MidiOutput::getAvailableDevices()` | Cross-platform, already linked [VERIFIED: juce_MidiDevices.h] |
| APVTS state persistence | Custom binary serialization | `juce::ValueTree` XML serialization via `getStateInformation` | Matches existing pattern, version-safe [VERIFIED: JamWideJuceProcessor.cpp] |
| Popup dialog | Custom window management | `juce::CallOutBox::launchAsynchronously()` | Same pattern as OscConfigDialog [VERIFIED: OscStatusDot.cpp line 53-58] |

**Key insight:** Every MIDI primitive needed is in JUCE's standard modules. The only custom code is the mapping logic, MIDI Learn state machine, APVTS-NJClient sync, and integration with the existing parameter system.

## Common Pitfalls

### Pitfall 1: Audio Thread Safety for CC Lookup
**What goes wrong:** Modifying the mapping table (add/remove mapping on message thread) while the audio thread is iterating it causes data races.
**Why it happens:** processBlock runs on the audio thread and reads the mapping table. MIDI Learn adds mappings on the message thread.
**How to avoid:** Use an atomic pointer swap or double-buffer pattern. Keep a "live" copy for the audio thread and a "staging" copy for the message thread. After modification, atomically swap pointers. Alternatively, use a lock-free SPSC queue to send mapping updates from message thread to audio thread.
**Warning signs:** Intermittent crashes in processBlock, TSAN violations.

### Pitfall 2: SPSC Single-Producer Invariant for Remote Sub-Channels
**What goes wrong:** MIDI CC for remote sub-channel controls (D-18) must dispatch via cmd_queue, but cmd_queue is single-producer (message thread only). If processBlock pushes directly, the SPSC invariant is violated.
**Why it happens:** processBlock runs on audio thread, not message thread.
**How to avoid:** For cmd_queue-only parameters, buffer the CC value on the audio thread (e.g., an atomic or small lock-free queue), then dispatch via callAsync from a timer on the message thread. This matches the OSC pattern exactly. [VERIFIED: OscServer.cpp dispatches all cmd_queue pushes via callAsync from message thread]
**Warning signs:** Corrupted commands in cmd_queue, run thread processing garbage data.

### Pitfall 3: APVTS Parameter Version and DAW State Migration
**What goes wrong:** Adding 69 new parameters to createParameterLayout changes the parameter list. Old DAW sessions saved with version 2 will not have these parameters in their state.
**Why it happens:** JUCE's replaceState() handles missing parameters by using defaults, but the version number matters for ParameterID.
**How to avoid:** Use version 3 for all new ParameterIDs. Existing parameters keep version 1. In setStateInformation, check stateVersion and handle gracefully. The current code already does this for version 1->2 migration (OSC config). [VERIFIED: JamWideJuceProcessor.cpp line 526-527]
**Warning signs:** Parameters resetting to zero instead of defaults when loading old sessions.

### Pitfall 4: processBlock MidiBuffer is the SAME Buffer for Input and Output
**What goes wrong:** In many JUCE hosts, the MidiBuffer passed to processBlock is both the input and output. If you clear it to write feedback, you lose incoming CC messages.
**Why it happens:** JUCE does not guarantee separate input/output MIDI buffers.
**How to avoid:** Process incoming CC messages FIRST (iterate and consume), THEN append feedback CC messages to the same buffer. Do not clear the buffer. Use `addEvent()` to append.
**Warning signs:** MIDI input not working, or feedback not reaching the controller.

### Pitfall 5: Echo Suppression Timing with processBlock Feedback
**What goes wrong:** If feedback is sent in the same processBlock call as the incoming CC, the echo suppression flag has not been set yet, causing a feedback loop.
**Why it happens:** Feedback generation and input processing happen in the same audio callback.
**How to avoid:** Process input FIRST, set echo suppression flags, THEN generate feedback output. The one-tick suppression should use a frame counter, not a timer, since processBlock frequency varies. Mark a CC as "just received" and skip its feedback for the current AND next processBlock invocation.
**Warning signs:** Motorized faders oscillating or jittering when moved.

### Pitfall 6: MIDI Learn Right-Click Conflict with Existing Scale Menu
**What goes wrong:** Right-click on faders currently forwards up to the editor for the UI scale menu. MIDI Learn needs to intercept right-clicks on specific components.
**Why it happens:** VbFader.mouseDown forwards right-clicks to the top-level component. [VERIFIED: VbFader.cpp line 152-158]
**How to avoid:** Change VbFader (and panSlider, muteButton, soloButton) to show a MIDI Learn popup menu on right-click INSTEAD of forwarding to the editor. The scale menu can be accessed by right-clicking empty space (not on a control). This is the expected DAW behavior -- right-click on a control gives that control's context menu.
**Warning signs:** Cannot MIDI Learn because right-click always shows scale menu.

### Pitfall 7: Remote APVTS Parameters Must Sync Bidirectionally with NJClient
**What goes wrong:** Remote user APVTS parameters (remoteVol_0..15 etc.) are a new layer on top of the existing cmd_queue-based remote user control. The APVTS value and the NJClient actual value can diverge.
**Why it happens:** When a remote user's volume changes via the existing UI (which uses cmd_queue), the APVTS parameter is not updated. When MIDI or DAW automation changes the APVTS parameter, NJClient is not updated.
**How to avoid:** Use MidiMapper::timerCallback (100ms, message thread) to sync APVTS remote params to NJClient via SetUserStateCommand through cmd_queue. This uses the same dirty-flag pattern as OscServer. The timer runs on the message thread, preserving the cmd_queue single-producer invariant. Do NOT sync in processBlock (cachedUsersMutex lock on audio thread = priority inversion risk). For UI-initiated changes, the existing cmd_queue path continues to work; the APVTS params will be updated by MIDI/automation and synced to NJClient by the timer.
**Warning signs:** MIDI-controlled remote volume reverts after one interval, or UI changes not reflected on MIDI controller.

### Pitfall 8: VbFader Uses 2.5 Power Curve but D-10 Specifies Linear CC Mapping
**What goes wrong:** The VbFader maps linear 0-2 to screen position using `pow(norm, 1/2.5)` for better dB resolution at the low end. CC 0-127 maps linearly to 0.0-1.0 normalized (per D-10). This means CC values 0-64 cover the bottom half of fader travel but map to 0-50% of the volume range, which feels like the fader "jumps" when the CC is in the upper range.
**Why it happens:** Linear CC mapping is simple and universal, but VbFader's display curve is non-linear.
**How to avoid:** Accept this as intentional per D-10. The CC value maps to the parameter's normalized range (0-1), which maps to 0-2 linear, which maps to dB via 20*log10. The fader will visually track the CC proportionally in the normalized domain. This is standard behavior -- Ableton and Bitwig both use linear CC-to-normalized mapping. The VbFader's power curve is purely visual.
**Warning signs:** User perceives "dead zone" at top of CC range. This is expected and acceptable per D-07 (7-bit is sufficient).

## Code Examples

### APVTS Remote Parameter Sync via Timer (Message Thread)
```cpp
// Source: Derived from OscServer dirty-flag pattern [VERIFIED: OscServer.cpp]
// In MidiMapper::timerCallback (message thread, 100ms):

const int count = juce::jmin(processor.userCount.load(std::memory_order_relaxed), 16);
for (int i = 0; i < count; ++i)
{
    juce::String suffix = juce::String(i);
    float vol = *processor.apvts.getRawParameterValue("remoteVol_" + suffix);
    float pan = *processor.apvts.getRawParameterValue("remotePan_" + suffix);
    bool mute = *processor.apvts.getRawParameterValue("remoteMute_" + suffix) >= 0.5f;

    // Dirty check against last-synced values
    if (volChanged || panChanged || muteChanged)
    {
        jamwide::SetUserStateCommand cmd;
        cmd.user_index = i;
        cmd.set_vol = volChanged; cmd.volume = vol;
        cmd.set_pan = panChanged; cmd.pan = pan;
        cmd.set_mute = muteChanged; cmd.mute = mute;
        processor.cmd_queue.push(std::move(cmd));
        // Update last-synced tracking arrays
    }
}
```

**IMPORTANT NOTE:** This timer runs on the message thread which is safe for cmd_queue (single-producer invariant preserved). Do NOT attempt this sync in processBlock -- the cachedUsersMutex lock on the audio thread causes priority inversion.

### State Version 3 Migration
```cpp
// Source: Existing state migration pattern [VERIFIED: JamWideJuceProcessor.cpp line 526]
static constexpr int currentStateVersion = 3;  // Bump from 2

// In getStateInformation:
// Add MIDI mapping array
auto midiMappings = juce::ValueTree("MidiMappings");
for (const auto& [key, mapping] : midiMapper->getMappings())
{
    auto entry = juce::ValueTree("Mapping");
    entry.setProperty("paramId", mapping.paramId, nullptr);
    entry.setProperty("cc", mapping.ccNumber, nullptr);
    entry.setProperty("channel", mapping.midiChannel, nullptr);
    midiMappings.addChild(entry, -1, nullptr);
}
state.addChild(midiMappings, -1, nullptr);

// In setStateInformation:
auto midiMappings = tree.getChildWithName("MidiMappings");
if (midiMappings.isValid() && midiMapper)
{
    for (int i = 0; i < midiMappings.getNumChildren(); ++i)
    {
        auto entry = midiMappings.getChild(i);
        midiMapper->addMapping(
            entry.getProperty("paramId").toString(),
            static_cast<int>(entry.getProperty("cc")),
            static_cast<int>(entry.getProperty("channel")));
    }
}
```

### MIDI Learn Right-Click Menu on VbFader
```cpp
// Source: Derived from existing VbFader::mouseDown [VERIFIED: VbFader.cpp line 150]
void VbFader::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        // Check if this parameter already has a MIDI mapping
        bool hasMidiMapping = /* query MidiMapper */;
        menu.addItem(1, "MIDI Learn");
        if (hasMidiMapping)
            menu.addItem(2, "Clear MIDI");
        menu.addSeparator();
        // Optionally include scale menu items too
        menu.showMenuAsync(juce::PopupMenu::Options()
            .withParentComponent(getTopLevelComponent()),
            [this](int result) {
                if (result == 1)
                    /* start MIDI Learn for this parameter's ID */;
                else if (result == 2)
                    /* clear mapping for this parameter */;
            });
        return;
    }
    // ... existing drag logic
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| acceptsMidi() returns false | Must return true for MIDI CC input | Phase 14 | Host will route MIDI to plugin processBlock |
| producesMidi() returns false | Should return true for CC feedback | Phase 14 | Host will pass MIDI output from plugin to downstream |
| 16 APVTS parameters | 85 APVTS parameters | Phase 14 | DAW automation lanes show all mixer params |
| Remote user control via cmd_queue only | APVTS + cmd_queue bridge via timer sync | Phase 14 | Remote users become host-automatable and MIDI-controllable |
| State version 2 | State version 3 | Phase 14 | New MIDI mapping entries in state |

## Discretion Recommendations

Based on the research, here are recommendations for areas left to Claude's discretion:

### Feedback Mechanism: Timer-Based Dirty-Flag (Recommended)
**Rationale:** Matches the proven OSC pattern. Timer fires on message thread every 100ms, checks all mapped parameters for changes, sends CC feedback. In plugin mode, buffer the CC messages and inject them at the start of the next processBlock. In standalone mode, send via MidiOutput::sendMessageNow() directly from the timer. This avoids all audio-thread concerns for feedback generation.

The alternative (generate feedback directly in processBlock) is simpler for plugin mode but couples audio thread timing to feedback rate and requires careful handling of the standalone path. Timer-based is more consistent across both modes.

### producesMidi() Should Return True
**Rationale:** DAW hosts check producesMidi() to determine whether to route MIDI output from the plugin. Returning true enables CC feedback to reach motorized controllers via the DAW's MIDI routing. JUCE's standalone wrapper also uses this flag to show MIDI output device selection in the audio settings dialog. [VERIFIED: juce_StandaloneFilterWindow.h line 475 checks producesMidi() for device selector]

### Data Structure: std::unordered_map with Composite Key
**Rationale:** Key = `(channel << 7) | cc_number` gives a unique 14-bit integer per mapping. With a maximum of 2048 mappings, hash collisions are rare. O(1) lookup in the hot processBlock path. The map can be read concurrently by the audio thread using an atomic-pointer-swap pattern: message thread builds a new map, swaps the shared_ptr atomically.

### CC-to-Volume Mapping: Linear in Normalized Domain
**Rationale:** Per D-10, CC 0-127 maps linearly to 0.0-1.0 normalized. For volume params (range 0-2), normalized 0-1 maps to linear 0-2. No power curve in the CC-to-parameter conversion. The VbFader's 2.5 power curve is purely visual (screen-to-value) and does not affect the normalized parameter value. This is the standard approach used by all major DAWs. [VERIFIED: VbFader.cpp confirms power curve is in valueToY/yToValue display methods only]

### juce_audio_devices: Already Available for All Builds
**Rationale:** `juce_audio_utils` (already linked) transitively depends on `juce_audio_devices`. No CMakeLists change needed. MidiInput/MidiOutput are available in all build targets (VST3, AU, CLAP, Standalone). Standalone-specific code (device enumeration UI) is conditionally compiled with `#if JucePlugin_IsMidiEffect == 0` or similar guards, but the classes are always available. [VERIFIED: CMakeLists.txt line 183, juce_audio_utils.h dependency declaration]

### Feedback Timer Interval: 100ms (Same as OSC)
**Rationale:** 100ms matches the OSC dirty-flag sender interval. For MIDI CC feedback to motorized controllers, 100ms (10 Hz) is more than adequate. Controllers like Mackie MCU update at 10 Hz. Higher rates (e.g., 10ms) would waste CPU for no perceivable benefit since motorized faders have mechanical inertia.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | JUCE's standalone wrapper automatically routes MIDI from opened MidiInput devices to processBlock's MidiBuffer when acceptsMidi() returns true | Architecture Patterns | Would need custom MidiInputCallback to bridge standalone MIDI to the mapper. Low risk -- verified from juce_StandaloneFilterWindow.h that deviceManager routes MIDI to player. |
| A2 | setValueNotifyingHost() is safe to call from the audio thread for APVTS parameters | Pattern 2: Audio Thread CC Dispatch | If not safe, would need to buffer CC values and apply on message thread. JUCE docs state it is thread-safe for atomic float parameters. [VERIFIED: JUCE parameter values are atomic floats] |
| A3 | The existing right-click scale menu on the editor can be moved to empty-space-only without breaking UX | Pitfall 6 | If users rely on right-clicking faders for the scale menu, this change could confuse them. Low risk -- right-click on controls for context menus is universal DAW convention. |

## Open Questions (RESOLVED)

1. **APVTS-to-NJClient Sync Path for Remote Users** (RESOLVED)
   - **Resolution:** APVTS is the source of truth for remote group controls. MidiMapper::timerCallback (100ms, message thread) reads remote APVTS params (remoteVol_N, remotePan_N, remoteMute_N) and dispatches SetUserStateCommand via cmd_queue when values change (dirty-flag pattern). This runs on the message thread, preserving the SPSC single-producer invariant. Do NOT sync in processBlock (cachedUsersMutex lock = priority inversion risk). UI-initiated changes continue via existing cmd_queue path; MIDI/automation changes go through APVTS and are synced to NJClient by the timer. Plan 01 Step 3 timerCallback and Plan 01 behavior Test 11 cover this.

2. **Thread-Safe Map Swap for Audio Thread** (RESOLVED)
   - **Resolution:** Use `std::shared_ptr<const MappingTable>` with `std::atomic_load/store`. Audio thread reads via `std::atomic_load(&published_)`. Message thread writes to `staging_` copy, then publishes via `std::atomic_store(&published_, newSharedPtr)`. Old shared_ptr reference count handles deallocation safely. Implemented in Plan 01 Step 3 MidiMapper class with `published_` and `staging_` members.

3. **Standalone MIDI Output for Feedback** (RESOLVED)
   - **Resolution:** MidiMapper::timerCallback handles standalone feedback. If `midiOutput_` device is open, the timer iterates all mappings, computes dirty CC values, and sends via `midiOutput_->sendMessageNow()`. This runs on the message thread (safe for MidiOutput calls). Plugin mode feedback goes through processBlock's appendFeedbackMidi (audio thread appends to MidiBuffer). Both paths are implemented in Plan 01 Step 3. MidiMapper.h declares `openMidiInput`, `openMidiOutput`, `closeMidiInput`, `closeMidiOutput`, and `hasError` for Plan 02 to consume.

## Environment Availability

Step 2.6: No external dependencies identified. All MIDI functionality comes from JUCE modules already linked. No new CLI tools, services, or runtimes needed.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Custom C++ test executables (no framework -- matches existing test_flac_codec, test_osc_loopback pattern) |
| Config file | CMakeLists.txt (JAMWIDE_BUILD_TESTS section) |
| Quick run command | `cmake --build build --target test_midi_mapping && ./build/test_midi_mapping` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| MIDI-01a | CC input -> parameter value change | unit | `./build/test_midi_mapping` | Wave 0 |
| MIDI-01b | Parameter change -> CC feedback output | unit | `./build/test_midi_mapping` | Wave 0 |
| MIDI-01c | MIDI Learn assigns CC to parameter | unit | `./build/test_midi_mapping` | Wave 0 |
| MIDI-01d | Mappings persist via state save/load | unit | `./build/test_midi_mapping` | Wave 0 |
| MIDI-01e | Echo suppression prevents feedback loop | unit | `./build/test_midi_mapping` | Wave 0 |
| MIDI-01f | APVTS remote params sync to NJClient via timerCallback | unit | `./build/test_midi_mapping` | Wave 0 |
| MIDI-01g | pluginval passes with 85 APVTS params | smoke | `cmake --build build --target validate` | Existing |

### Sampling Rate
- **Per task commit:** Quick unit test run
- **Per wave merge:** Full ctest suite
- **Phase gate:** pluginval green + all unit tests passing

### Wave 0 Gaps
- [ ] `tests/test_midi_mapping.cpp` -- covers MIDI-01a through MIDI-01f (CC dispatch, feedback, learn, persistence, echo suppression, APVTS-NJClient sync)
- [ ] CMakeLists.txt test target: `test_midi_mapping` -- add juce_add_console_app similar to test_osc_loopback

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A -- local MIDI only |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Yes | Clamp CC values 0-127, validate channel 1-16, bounds-check mapping indices |
| V6 Cryptography | No | N/A |

### Known Threat Patterns for MIDI

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Out-of-range CC value | Tampering | Clamp to 0-127 (MidiMessage handles this internally) |
| Invalid MIDI channel | Tampering | Validate 1-16 range before lookup |
| Mapping index out of bounds | Tampering | Bounds-check array indices before access |
| Excessive mappings causing memory growth | Denial of Service | Cap at 2048 mappings (16 channels x 128 CCs) |
| Malformed state data in setStateInformation | Tampering | Validate all deserialized values (cc 0-127, channel 1-16, paramId exists) |

## Sources

### Primary (HIGH confidence)
- JUCE source `libs/JUCE/modules/juce_audio_basics/midi/juce_MidiMessage.h` -- MidiMessage CC API
- JUCE source `libs/JUCE/modules/juce_audio_basics/midi/juce_MidiBuffer.h` -- MidiBuffer iteration
- JUCE source `libs/JUCE/modules/juce_audio_devices/midi_io/juce_MidiDevices.h` -- MidiInput/MidiOutput
- JUCE source `libs/JUCE/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h` -- Standalone MIDI routing
- Project source `juce/osc/OscServer.h`, `OscServer.cpp` -- Echo suppression, dirty-flag, timer patterns
- Project source `juce/JamWideJuceProcessor.h`, `.cpp` -- processBlock, APVTS, state persistence
- Project source `juce/ui/VbFader.h`, `.cpp` -- Right-click handling, power curve, parameter attachment
- Project source `juce/ui/ConnectionBar.h`, `.cpp` -- Footer layout, OscStatusDot placement
- Project source `src/threading/ui_command.h` -- Command variants for cmd_queue (SetUserStateCommand)
- Project source `juce/NinjamRunThread.cpp` -- cmd_queue processing, SetUserState dispatch
- Project source `CMakeLists.txt` -- Build config, linked JUCE modules

### Secondary (MEDIUM confidence)
- Phase 9 CONTEXT.md -- OSC architecture decisions (D-12 dirty-flag, D-14 echo suppression, D-19 callAsync threading)

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all JUCE built-in, verified against source
- Architecture: HIGH -- direct adaptation of proven OSC pattern in same codebase
- Pitfalls: HIGH -- derived from direct code analysis of threading model and existing patterns
- APVTS expansion: HIGH -- verified existing createParameterLayout and state migration
- APVTS-NJClient sync: HIGH -- uses established cmd_queue + timer pattern from OscServer
- Discretion recommendations: MEDIUM -- based on engineering judgment, all supported by codebase patterns

**Research date:** 2026-04-11
**Valid until:** 2026-05-11 (stable -- JUCE MIDI API does not change frequently)
