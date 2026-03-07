# Architecture Patterns

**Domain:** JUCE-based NINJAM client (plugin + standalone)
**Researched:** 2026-03-07

## Recommended Architecture

### System Overview

The JUCE rewrite maps the current three-thread model (Audio / Run / UI) onto JUCE idioms while preserving the battle-tested NJClient core. The key insight: NJClient's `AudioProc()` already takes a raw `float**` buffer interface -- JUCE's `processBlock()` can bridge to it with minimal glue. The Run thread becomes a `juce::Thread` subclass. The UI thread becomes JUCE's message thread with Component-based panels.

```
                         DAW Host / Standalone Wrapper
                        (JUCE handles format: VST3, AU, CLAP*, Standalone)
                                      |
                    +-----------------+-----------------+
                    |                                   |
            Audio Thread                        Message Thread (UI)
        (host-managed callback)              (JUCE MessageManager)
                    |                                   |
        +-----------+-----------+           +-----------+-----------+
        | JamWideProcessor      |           | JamWideEditor         |
        | (AudioProcessor)      |           | (AudioProcessorEditor)|
        |                       |           |                       |
        | processBlock() {      |           | Timer polls:          |
        |   bridge buffers      |           |   - ui event queue    |
        |   -> NJClient::       |           |   - atomic snapshots  |
        |      AudioProc()      |           |   - NJClient state    |
        | }                     |           |     (under mutex)     |
        +-----------+-----------+           +-----------+-----------+
                    |                                   |
                    |   +------ SPSC queues -----+      |
                    |   | cmd_queue (UI -> Run)   |     |
                    |   | event_queue (Run -> UI) |     |
                    |   +-------------------------+     |
                    |               |                   |
                    |    +----------+----------+        |
                    |    | NinjamRunThread      |        |
                    |    | (juce::Thread)       |        |
                    |    |                      |        |
                    |    | run() {              |        |
                    |    |   NJClient::Run()    |        |
                    |    |   drain cmd_queue    |        |
                    |    |   push events        |        |
                    |    | }                    |        |
                    |    +----------+----------+        |
                    |               |                   |
                    +-------+-------+-------+-----------+
                            |               |
                    +-------+-------+ +-----+-------+
                    | NJClient      | | Codec Layer |
                    | (core logic)  | | I_NJEncoder |
                    | njclient.cpp  | | I_NJDecoder |
                    +---------------+ | Vorbis/FLAC |
                                      +-------------+
```

`*` CLAP via clap-juce-extensions today; native in JUCE 9 when released.

### Component Boundaries

| Component | Responsibility | Communicates With |
|-----------|---------------|-------------------|
| **JamWideProcessor** | JUCE AudioProcessor subclass. Owns NJClient, NinjamRunThread, SPSC queues, atomic parameters. Bridges `processBlock()` to `NJClient::AudioProc()`. Declares multi-bus output layout. | DAW host (audio callbacks), NJClient (AudioProc), JamWideEditor (getProcessor()), NinjamRunThread (lifecycle) |
| **JamWideEditor** | JUCE AudioProcessorEditor subclass. Owns all UI Components. Uses Timer to poll event queue and atomic snapshots. Sends commands via SPSC queue. | JamWideProcessor (read state), SPSC cmd_queue (write commands), SPSC event_queue (read events) |
| **NinjamRunThread** | juce::Thread subclass. Runs NJClient::Run() loop at ~50ms intervals. Drains command queue, pushes events to UI. | NJClient (Run(), SetXxx methods under client_mutex), SPSC queues (both directions) |
| **NJClient** | Unchanged Cockos core. NINJAM protocol, network I/O, audio encode/decode, interval management, remote user tracking, metronome. | Codec layer (I_NJEncoder/I_NJDecoder), WDL jnetlib (TCP), called by AudioProc (audio thread) and Run (network thread) |
| **Codec Layer** | I_NJEncoder / I_NJDecoder implementations: VorbisEncoder/Decoder, FlacEncoder/Decoder. Pluggable via FourCC dispatch. | NJClient (created and called by njclient.cpp), libogg/libvorbis/libFLAC (external libs) |
| **SPSC Queues** | Lock-free single-producer single-consumer ring buffers. Existing `SpscRing<T, N>` template carries over unchanged. | UI thread (producer for commands, consumer for events), Run thread (consumer for commands, producer for events) |
| **Atomic Snapshot** | Struct of `std::atomic<>` values for audio-thread-safe reads. VU levels, BPM/BPI, beat phase, interval position. | Audio thread (writes), UI thread (reads for display), no locking required |
| **UI Components** | Individual panels: ConnectionPanel, ChatPanel, MixerPanel, LocalChannelPanel, MasterPanel, ServerBrowser, TimingGuide, StatusBar. Each is a juce::Component. | JamWideEditor (parent), cmd_queue (via parent), event-driven state updates |
| **LookAndFeel** | JamWideLookAndFeel (extends LookAndFeel_V4). Dark theme, custom sliders/knobs/meters. | All UI Components (for rendering) |

### Data Flow

**Audio Processing Flow (Real-Time, Lock-Free):**

```
Host calls JamWideProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&)
    |
    v
1. Read JUCE AudioProcessorParameters (automatable: master vol, metro vol, mutes)
2. Map JUCE AudioBuffer to float** arrays for NJClient:
   - Input: buffer channels 0-1 (stereo input bus)
   - Output: build float* array from all output bus channels
     bus 0 (mix):   outbuf[0], outbuf[1]
     bus 1 (user1): outbuf[2], outbuf[3]
     bus 2 (user2): outbuf[4], outbuf[5]
     ...up to N buses
3. Read transport state from getPlayHead()->getPosition()
   - isPlaying, tempo, timeInSamples
4. Call NJClient::AudioProc(inbuf, 2, outbuf, outnch, frames, srate,
                            justMonitor, isPlaying, isSeek, cursorPos)
   NJClient internally:
   - Encodes local input via I_NJEncoder (Vorbis or FLAC)
   - Decodes remote audio via I_NJDecoder per user
   - Mixes into outbuf[] at each user's out_chan_index
   - Generates metronome clicks
5. Write VU peaks to atomic snapshot (lock-free)
6. Return -- host consumes output buffer
```

**Command Flow (UI to Network, Non-Real-Time):**

```
User clicks "Connect" in ConnectionPanel
    |
    v
ConnectionPanel calls editor->sendCommand(ConnectCommand{host, user, pass})
    |
    v
JamWideEditor pushes to cmd_queue (SPSC, lock-free)
    |
    v
NinjamRunThread::run() drains cmd_queue via std::visit
    |
    v
std::visit dispatches to NJClient::Connect(host, user, pass)
    (under client_mutex)
    |
    v
NJClient begins TCP handshake, state changes
    |
    v
NinjamRunThread detects status change, pushes StatusChangedEvent to event_queue
    |
    v
JamWideEditor::timerCallback() drains event_queue
    |
    v
StatusBar::updateStatus(newStatus) -- repaint()
```

**Parameter Flow (DAW Automation to Audio):**

```
DAW automates "Master Volume" parameter
    |
    v
JUCE AudioProcessorParameter updated (thread-safe, atomic internally)
    |
    v
JamWideProcessor::processBlock() reads parameter value
    |
    v
Sets NJClient::config_mastervolume atomic
    |
    v
NJClient::process_samples() uses atomic value for mixing
```

**State Polling Flow (Audio/Network to UI):**

```
JamWideEditor::timerCallback() @ 30-60 Hz:
    |
    +-- 1. Drain event_queue (SPSC): status changes, chat messages,
    |      user join/leave, config changes
    |
    +-- 2. Read atomic snapshot: VU peaks, BPM/BPI, beat position,
    |      interval progress, transient detection
    |
    +-- 3. If needed: lock client_mutex briefly to read remote user list,
    |      channel names, subscription states (infrequent, ~1-2Hz max)
    |
    +-- 4. Trigger Component::repaint() on changed panels
```

## Threading Model: Current to JUCE Mapping

### Three-Thread Preservation

The existing three-thread model maps cleanly to JUCE. This is intentional -- it is a proven architecture that avoids lock contention on the audio thread.

| Current Thread | JUCE Equivalent | What Changes |
|----------------|-----------------|--------------|
| **Audio Thread** (host callback) | `JamWideProcessor::processBlock()` (host callback) | Buffer format changes from `float**` to `juce::AudioBuffer<float>`. Must bridge via `getWritePointer()` / `getReadPointer()`. Transport info via `getPlayHead()` instead of CLAP transport struct. |
| **Run Thread** (std::thread + lambda) | `NinjamRunThread` (juce::Thread subclass) | Thread lifecycle managed by `startThread()` / `stopThread()` instead of raw `std::thread`. Cooperative shutdown via `threadShouldExit()`. Sleep via `Thread::sleep()` or `wait()`. |
| **UI Thread** (platform event loop + ImGui) | JUCE Message Thread (MessageManager) | Complete replacement. ImGui panels become juce::Component subclasses. Timer-based polling replaces ImGui frame loop. `repaint()` / `paint()` instead of ImGui draw commands. |

### What Does NOT Change

- **SPSC ring buffers**: `SpscRing<UiCommand, N>` and `SpscRing<UiEvent, N>` carry over unchanged. They are header-only templates with no JUCE or ImGui dependency.
- **Lock strategy**: `client_mutex` protects NJClient API calls from Run thread. Audio thread uses only atomics. UI thread briefly acquires `client_mutex` for infrequent state reads.
- **NJClient internals**: AudioProc, process_samples, Run, on_new_interval -- all unchanged.

### What JUCE Replaces

| Current Mechanism | JUCE Replacement | Notes |
|-------------------|------------------|-------|
| `std::thread` + lambda | `juce::Thread` subclass | Cooperative exit via `threadShouldExit()` |
| `std::mutex` (client_mutex, state_mutex) | `juce::CriticalSection` or keep `std::mutex` | Either works; `std::mutex` is fine in JUCE |
| `std::condition_variable` (license dialog) | `juce::WaitableEvent` or `MessageManager::callAsync()` | License acceptance can use async dialog |
| Platform event loop (Metal/D3D11 timer) | JUCE MessageManager + Timer | `startTimer(30)` for ~33Hz UI refresh |
| ImGui render frame | `Component::paint(Graphics& g)` | Retained-mode (repaint on change) vs immediate-mode |
| CLAP param callbacks | `AudioProcessorParameter` + `AudioProcessorValueTreeState` | JUCE manages parameter serialization, automation, undo |
| clap-wrapper (VST3/AU) | JUCE `juce_add_plugin` (VST3/AU/Standalone/CLAP) | Single build produces all formats |

## Multichannel Output Bus Configuration

### The Problem

NINJAM sessions have variable numbers of remote users (1-20+). Each user's audio should be routable to a separate stereo pair in the DAW for independent mixing, EQ, and effects. This is the "ReaNINJAM multichannel" feature.

### The Solution: Fixed Maximum Bus Count

JUCE does not support dynamically changing bus count while the plugin is active. The solution is the same as ReaNINJAM's: declare a fixed maximum number of output buses at construction time.

```cpp
// JamWideProcessor constructor
JamWideProcessor()
    : AudioProcessor(createBusLayout())
{
}

static BusesProperties createBusLayout()
{
    BusesProperties props;

    // Bus 0: always-on stereo mix bus (main output)
    props = props.withOutput("Mix", juce::AudioChannelSet::stereo(), true);

    // Buses 1..N: per-user stereo pairs (disabled by default)
    // Maximum = configurable, default 8 user slots = 16 channels
    constexpr int kMaxUserBuses = 8;
    for (int i = 0; i < kMaxUserBuses; ++i)
    {
        juce::String name = "User " + juce::String(i + 1);
        props = props.withOutput(name, juce::AudioChannelSet::stereo(), false);
    }

    // Single stereo input (local microphone/instrument)
    props = props.withInput("Input", juce::AudioChannelSet::stereo(), true);

    return props;
}
```

### isBusesLayoutSupported

```cpp
bool isBusesLayoutSupported(const BusesLayout& layouts) const override
{
    // Main output (bus 0) must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Main input must be stereo
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // All additional output buses must be stereo (or disabled)
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        auto set = layouts.outputBuses[i];
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}
```

### processBlock Buffer Bridging

The critical bridge between JUCE's `AudioBuffer<float>` and NJClient's `float**`:

```cpp
void JamWideProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi)
{
    // Count active output channels across all enabled buses
    int totalOutCh = getTotalNumOutputChannels();

    // Build float* array for NJClient::AudioProc
    // NJClient expects outbuf[0..outnch-1] as individual channel pointers
    float* outbuf[kMaxOutputChannels]; // e.g., 18 = 1 mix pair + 8 user pairs
    for (int ch = 0; ch < totalOutCh; ++ch)
        outbuf[ch] = buffer.getWritePointer(ch);

    // Input: first 2 channels of the input bus
    float* inbuf[2] = {
        buffer.getReadPointer(0),  // Assumes input mapped to first 2 channels
        buffer.getReadPointer(totalOutCh > 1 ? 1 : 0)
    };

    // Read transport
    bool isPlaying = false;
    bool isSeek = false;
    double cursorPos = -1.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            isPlaying = pos->getIsPlaying();
            // ... extract tempo, time signature for DAW sync
        }
    }

    // Sync JUCE parameters to NJClient atomics
    njClient->config_mastervolume.store(
        *masterVolumeParam, std::memory_order_relaxed);
    // ... other params

    // THE BRIDGE: JUCE buffer pointers -> NJClient AudioProc
    njClient->AudioProc(
        inbuf, 2,
        outbuf, totalOutCh,
        buffer.getNumSamples(),
        static_cast<int>(getSampleRate()),
        !isPlaying,   // justMonitor when not playing
        isPlaying, isSeek, cursorPos
    );
}
```

### Auto-Assignment Modes

NJClient already implements two auto-assignment modes via `config_remote_autochan`:

| Mode | Behavior | JUCE Integration |
|------|----------|------------------|
| `0` | All remote audio to mix bus (bus 0) | Default. `out_chan_index = 0` for all users. Only bus 0 active. |
| `1` | Auto-assign by channel | Each remote channel gets its own stereo pair. Uses `find_unused_output_channel_pair()`. |
| `2` | Auto-assign by user | All channels from one user share a stereo pair. Preferred for typical use. |

The JUCE processor configures NJClient:
```cpp
// In prepareToPlay or when user changes routing mode:
njClient->config_remote_autochan = routingMode;  // 0, 1, or 2
njClient->config_remote_autochan_nch = getTotalNumOutputChannels();
```

## Patterns to Follow

### Pattern 1: Processor Owns Everything, Editor Borrows

**What:** JamWideProcessor owns NJClient, NinjamRunThread, SPSC queues, and all shared state. JamWideEditor gets a reference to the processor and reads state through it. The editor can be created and destroyed multiple times (DAW opens/closes plugin window) without affecting audio or network.

**When:** Always. This is the fundamental JUCE plugin architecture rule.

**Example:**
```cpp
class JamWideProcessor : public juce::AudioProcessor
{
public:
    // Owned state -- persists across editor open/close
    std::unique_ptr<NJClient> njClient;
    std::unique_ptr<NinjamRunThread> runThread;
    SpscRing<UiCommand, 256> cmdQueue;
    SpscRing<UiEvent, 256> eventQueue;
    std::mutex clientMutex;
    AtomicSnapshot snapshot;  // VU, BPM, beat phase

    juce::AudioProcessorEditor* createEditor() override
    {
        return new JamWideEditor(*this);
    }
};

class JamWideEditor : public juce::AudioProcessorEditor,
                      public juce::Timer
{
    JamWideProcessor& processor;

    void timerCallback() override
    {
        // Poll processor's event queue and snapshot
        drainEventQueue();
        updateMeters();
    }
};
```

### Pattern 2: Timer-Based UI Polling (Not AsyncUpdater)

**What:** Use `juce::Timer` at 30-60 Hz to poll SPSC queues and atomic snapshots, rather than `AsyncUpdater` or `MessageManager::callAsync()` triggered from the audio thread.

**When:** Always for audio-to-UI communication. `AsyncUpdater::triggerAsyncUpdate()` posts a message to the system queue, which may block on the calling thread -- unsafe from the audio thread.

**Why:** The audio thread must never call anything that allocates, locks, or blocks. Timer polling is fully decoupled -- the audio thread writes atomics and the UI thread reads them on its own schedule.

**Example:**
```cpp
class JamWideEditor : public juce::AudioProcessorEditor,
                      public juce::Timer
{
    void timerCallback() override
    {
        // Drain all pending events (lock-free)
        UiEvent event;
        while (processor.eventQueue.pop(event))
            std::visit(EventHandler{*this}, event);

        // Read atomic VU meters
        float vuL = processor.snapshot.masterVuLeft.load(
            std::memory_order_relaxed);
        float vuR = processor.snapshot.masterVuRight.load(
            std::memory_order_relaxed);
        masterMeter.setLevels(vuL, vuR);

        // Read atomic beat position for timing guide
        float beatPhase = processor.snapshot.beatPhase.load(
            std::memory_order_relaxed);
        timingGuide.setBeatPhase(beatPhase);
    }

    // Start at construction:
    JamWideEditor(JamWideProcessor& p) : AudioProcessorEditor(p), processor(p)
    {
        startTimerHz(30); // 30 FPS UI refresh
    }
};
```

### Pattern 3: NinjamRunThread as juce::Thread

**What:** Subclass `juce::Thread` for the network/NJClient Run loop. Use `threadShouldExit()` for cooperative shutdown. Use `Thread::sleep()` for the ~50ms polling interval.

**When:** Required. The Run thread must continue operating when the editor is closed.

**Example:**
```cpp
class NinjamRunThread : public juce::Thread
{
    NJClient& client;
    std::mutex& clientMutex;
    SpscRing<UiCommand, 256>& cmdQueue;
    SpscRing<UiEvent, 256>& eventQueue;

public:
    NinjamRunThread(NJClient& c, std::mutex& m,
                    SpscRing<UiCommand, 256>& cmd,
                    SpscRing<UiEvent, 256>& evt)
        : Thread("NinjamRun"), client(c), clientMutex(m),
          cmdQueue(cmd), eventQueue(evt)
    {
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            // Drain command queue
            UiCommand cmd;
            while (cmdQueue.pop(cmd))
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                std::visit(CommandHandler{client, eventQueue}, cmd);
            }

            // Run NJClient network loop
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                int status = client.Run();
                // Push status/event changes to event queue
            }

            Thread::sleep(50); // ~20Hz network polling
        }
    }
};
```

### Pattern 4: JUCE Parameters for DAW-Automatable Values Only

**What:** Use `AudioProcessorValueTreeState` for the small set of DAW-automatable parameters (master volume, master mute, metronome volume, metronome mute). All other state (connection settings, remote user volumes, chat) lives outside APVTS and is communicated via SPSC queues and direct reads under mutex.

**When:** Always. Do not try to map every NJClient setting to a JUCE parameter -- remote user count changes dynamically, parameter IDs must be stable, and most settings are not meaningful for DAW automation.

**Example:**
```cpp
// In JamWideProcessor constructor:
apvts(*this, nullptr, "Parameters",
    {
        std::make_unique<juce::AudioParameterFloat>(
            "masterVol", "Master Volume", 0.0f, 2.0f, 1.0f),
        std::make_unique<juce::AudioParameterBool>(
            "masterMute", "Master Mute", false),
        std::make_unique<juce::AudioParameterFloat>(
            "metroVol", "Metronome Volume", 0.0f, 1.5f, 0.5f),
        std::make_unique<juce::AudioParameterBool>(
            "metroMute", "Metronome Mute", false),
    })

// Remote user volumes, pans, mutes are NOT JUCE parameters.
// They are managed via SPSC commands -> NJClient API.
```

### Pattern 5: State Serialization for Plugin Recall

**What:** Override `getStateInformation()` / `setStateInformation()` to save/restore connection settings, routing preferences, and per-user state between DAW sessions.

**When:** Required for production-quality plugin behavior. Users expect their settings to persist when re-opening a DAW project.

**Example:**
```cpp
void JamWideProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save APVTS (automatable params)
    auto state = apvts.copyState();

    // Add custom properties for non-parameter state
    state.setProperty("lastServer", lastServer, nullptr);
    state.setProperty("lastUsername", lastUsername, nullptr);
    state.setProperty("routingMode", routingMode, nullptr);
    state.setProperty("codecFormat", codecFormat, nullptr);

    juce::MemoryOutputStream stream(destData, true);
    state.writeToStream(stream);
}

void JamWideProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid())
    {
        apvts.replaceState(tree);
        lastServer = tree.getProperty("lastServer", "").toString();
        lastUsername = tree.getProperty("lastUsername", "").toString();
        routingMode = static_cast<int>(tree.getProperty("routingMode", 0));
        codecFormat = static_cast<int>(tree.getProperty("codecFormat", 0));
    }
}
```

## Anti-Patterns to Avoid

### Anti-Pattern 1: Locking in processBlock

**What:** Acquiring `client_mutex` (or any mutex) in `processBlock()` to read NJClient state.

**Why bad:** Any mutex acquisition on the audio thread risks priority inversion with the Run thread or UI thread. If the Run thread holds `client_mutex` during a slow network operation, the audio thread blocks and the DAW glitches. The current codebase already avoids this (audio thread uses only atomics), and the JUCE version must preserve this guarantee.

**Instead:** NJClient::AudioProc() is designed to be called without locks. The existing design already separates audio-thread-safe access (AudioProc, atomic configs) from mutex-protected access (Run, SetXxx methods). Keep this separation.

### Anti-Pattern 2: Dynamic Bus Count Changes

**What:** Trying to add/remove output buses when users join/leave a NINJAM session.

**Why bad:** JUCE does not support changing bus count while the plugin is active. Even in formats that technically support it (VST3), most DAWs do not dynamically rewire mixer tracks. Attempting this requires deactivating the plugin (audio dropout) and hoping the DAW responds correctly (many do not).

**Instead:** Declare a fixed maximum number of output buses at construction. Default 8 user buses (16 channels) covers all realistic sessions. Unused buses output silence. User configures the maximum in preferences before connecting.

### Anti-Pattern 3: Using MessageManager::callAsync from Audio Thread

**What:** Calling `MessageManager::callAsync()` from `processBlock()` to push UI updates.

**Why bad:** `callAsync()` allocates a heap object and posts it to the message queue. This involves a lock (the message queue's internal lock) and allocation -- both forbidden on the audio thread.

**Instead:** Write to `std::atomic<>` snapshot values from the audio thread. The UI thread reads them via Timer polling. For structured events (chat, user join), use the SPSC queue from the Run thread (which is not the audio thread).

### Anti-Pattern 4: Editor Owns Network State

**What:** Putting NJClient, the Run thread, or SPSC queues inside JamWideEditor.

**Why bad:** The editor is created and destroyed every time the user opens/closes the plugin window. If the editor owns network state, closing the window disconnects from the NINJAM server. JUCE explicitly documents that the processor must outlive the editor.

**Instead:** JamWideProcessor owns all persistent state. The editor is a "view" that reads from the processor and sends commands to it.

### Anti-Pattern 5: Replacing SPSC Queues with JUCE Message Passing

**What:** Replacing the existing lock-free SPSC ring buffers with `MessageManager::callAsync()` for UI-to-Run thread communication.

**Why bad:** The Run thread is not the message thread. `callAsync()` dispatches to the message thread, not an arbitrary background thread. There is no built-in JUCE mechanism to post work to a specific `juce::Thread` subclass. The SPSC queue pattern is the correct solution for inter-thread communication with a custom thread.

**Instead:** Keep the existing `SpscRing<UiCommand, N>` and `SpscRing<UiEvent, N>`. They are header-only, tested, lock-free, and have no external dependencies. They work identically under JUCE.

## Scalability Considerations

| Concern | 1-3 users | 5-10 users | 15+ users |
|---------|-----------|------------|-----------|
| Output channels | 2-8 ch (1 mix + 1-3 user buses) | 12-22 ch (1 mix + 5-10 user buses) | 32+ ch (1 mix + 15+ user buses). Some DAWs may not support >32 output channels per plugin. |
| Decode CPU | Negligible | Moderate (5-10 Vorbis/FLAC decoders) | Significant but still manageable. FLAC decode is ~2x CPU of Vorbis. |
| UI complexity | Simple mixer | Scrollable mixer panel | Need efficient Component recycling; only render visible channel strips |
| Network bandwidth | ~50 KB/s per FLAC user | ~500 KB/s total | ~1 MB/s+. FLAC bandwidth may strain some connections. |
| Memory | ~10 MB | ~30 MB (decode buffers per user) | ~60 MB. Each user has interval-sized decode buffers (~8 sec at 44.1kHz). |

## Suggested Build Order

Dependencies between components determine the optimal implementation sequence. Each layer builds on the previous, enabling incremental testing.

### Phase 1: Skeleton (JUCE project + audio passthrough)

**Build:**
- CMakeLists.txt with `juce_add_plugin` (VST3, AU, Standalone)
- Empty JamWideProcessor with passthrough `processBlock()`
- Empty JamWideEditor with placeholder UI
- Verify builds on macOS and Windows
- Verify loads in DAW as VST3/AU and as standalone app

**No NJClient yet.** This validates the JUCE build system, plugin format generation, and basic lifecycle.

**Depends on:** Nothing (fresh start).

### Phase 2: NJClient Integration + Run Thread

**Build:**
- Link NJClient (njclient.cpp, netmsg.cpp, mpb.cpp) + WDL + libogg + libvorbis + libFLAC
- Create NinjamRunThread (juce::Thread subclass)
- Port SPSC queues (header-only, just include)
- Port UiCommand / UiEvent variants
- Wire processBlock() -> NJClient::AudioProc() with stereo-only output (bus 0)
- Wire NinjamRunThread lifecycle to prepareToPlay/releaseResources
- Test: connect to a NINJAM server from standalone, hear audio

**Depends on:** Phase 1 (project builds).

### Phase 3: Core UI Panels

**Build:**
- ConnectionPanel (server/user/pass fields, connect button)
- StatusBar (connection status, BPM/BPI, beat counter)
- ChatPanel (message list, input field)
- Timer-based polling in editor
- Wire SPSC command/event flow through JUCE Components
- LookAndFeel skeleton (dark theme, basic colors)

**Depends on:** Phase 2 (NJClient works, can connect).

### Phase 4: Mixer + Local Channels

**Build:**
- MixerPanel with ChannelStrip components (volume, pan, mute, VU meter)
- LocalChannelPanel (input monitoring controls)
- MasterPanel (master volume/mute, metronome volume/mute)
- Custom VU meter Component
- AudioProcessorValueTreeState for automatable parameters

**Depends on:** Phase 3 (connection works, event flow proven).

### Phase 5: Multichannel Output

**Build:**
- Multi-bus output declaration in JamWideProcessor constructor
- isBusesLayoutSupported() validation
- processBlock() buffer bridging for N output buses
- Wire config_remote_autochan and config_remote_autochan_nch
- Routing mode UI (mix-only, per-user, per-channel)
- Test in DAW with multiple mixer tracks per user

**Depends on:** Phase 4 (mixer UI exists to verify routing).

### Phase 6: Server Browser + State Persistence

**Build:**
- ServerBrowser (juce::TableListBox with server list data)
- Port ServerListFetcher (may switch from WDL jnetlib HTTP to juce::URL for cleaner async)
- getStateInformation / setStateInformation
- Preset save/recall in DAW

**Depends on:** Phase 3 (connection panel, can populate from browser).

### Phase 7: Timing Guide + Polish

**Build:**
- TimingGuide (custom paint() for beat grid visualization)
- Transient detection display (ported from current audio thread code)
- LookAndFeel refinement (custom knobs, hover states)
- Resizable window behavior
- Accessibility (keyboard navigation, screen reader labels)
- CLAP format via clap-juce-extensions (or native if JUCE 9 ships)

**Depends on:** Phase 4-6 (all core features working).

### Phase 8: DAW Sync + Advanced Features

**Build:**
- Live BPM/BPI change handling (no reconnect required)
- Session position tracking across intervals
- Transport sync (start/stop alignment to interval boundaries)
- DAW tempo read via getPlayHead()

**Depends on:** Phase 5 (multichannel output working for full DAW integration testing).

## Sources

- [JUCE AudioProcessor Class Reference](https://docs.juce.com/master/classAudioProcessor.html) -- processBlock, getBusBuffer, BusesProperties
- [JUCE Bus Layout Tutorial](https://juce.com/tutorials/tutorial_audio_bus_layouts/) -- multi-bus configuration, isBusesLayoutSupported
- [JUCE Thread Class Reference](https://docs.juce.com/master/classThread.html) -- run(), threadShouldExit(), stopThread()
- [JUCE Timer Class Reference](https://docs.juce.com/master/classTimer.html) -- timerCallback() polling pattern
- [JUCE AsyncUpdater Class Reference](https://docs.juce.com/master/classAsyncUpdater.html) -- why NOT to use from audio thread
- [JUCE AudioProcessorValueTreeState](https://docs.juce.com/master/classAudioProcessorValueTreeState.html) -- parameter management
- [JUCE MessageManager Class Reference](https://docs.juce.com/master/classMessageManager.html) -- callAsync() threading rules
- [JUCE Roadmap Q3 2025](https://juce.com/blog/juce-roadmap-update-q3-2025/) -- CLAP support planned for JUCE 9
- [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) -- CLAP format via external extension (pre-JUCE 9)
- [JUCE Forum: Lock-free processBlock](https://forum.juce.com/t/reading-writing-values-lock-free-to-from-processblock/50947) -- atomic pattern recommendations
- [JUCE Forum: Multi-bus AU Plugin](https://forum.juce.com/t/multi-bus-au-plugin/53546) -- AU multi-bus caveats
- [timur.audio: Locks in real-time audio](https://timur.audio/using-locks-in-real-time-audio-processing-safely) -- priority inversion analysis
- ReaNINJAM source: `ninjam/njclient.cpp` -- AudioProc, process_samples, find_unused_output_channel_pair, config_remote_autochan
- ReaNINJAM research: `/Users/cell/dev/ninjam/.planning/research/ARCHITECTURE.md` -- multi-output port pattern, out_chan_index routing
- JamWide current: `/Users/cell/dev/JamWide/.planning/codebase/ARCHITECTURE.md` -- three-thread model, SPSC queues, lock strategy
- JamWide JUCE port plan: `/Users/cell/dev/JamWide/memory-bank/plan-juce-port.md` -- initial port planning, component mapping

---
*Architecture research: 2026-03-07*
