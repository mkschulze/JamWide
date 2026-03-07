# Phase 3: NJClient Audio Bridge - Research

**Researched:** 2026-03-07
**Domain:** NJClient audio integration with JUCE AudioProcessor, thread bridging, sample rate management, editor/processor lifecycle
**Confidence:** HIGH

## Summary

Phase 3 bridges the existing NJClient core with the Phase 2 JUCE AudioProcessor skeleton. The goal is end-to-end audio: a user connects to a NINJAM server from the standalone app and hears remote participants on stereo output bus 0, while their microphone/instrument audio is encoded and sent to other participants. Both Vorbis and FLAC codecs must work. Closing and reopening the plugin editor window must not interrupt audio or network connection.

This is primarily a wiring phase, not a new-code phase. The NJClient already implements the full NINJAM protocol, audio encoding/decoding (Vorbis + FLAC), remote user mixing, metronome, and interval management. The JUCE skeleton already has the correct bus layout (4 stereo inputs, 17 stereo outputs), parameter management (APVTS for master/metronome volumes), and a NinjamRunThread skeleton. Phase 3 fills in the bodies: processBlock calls NJClient::AudioProc(), NinjamRunThread calls NJClient::Run() and processes commands, and a hardcoded auto-connect mechanism enables testing without a full UI (which comes in Phase 4).

The critical architectural insight is the separation of processor lifecycle from editor lifecycle. In JUCE, the AudioProcessor persists across editor create/destroy cycles. NJClient and NinjamRunThread must be owned by the processor, not the editor. The existing Phase 2 code already follows this pattern (NinjamRunThread is a member of JamWideJuceProcessor), so Phase 3 reinforces it: NJClient is created in the processor constructor and managed by the processor. The editor is a thin view that can come and go without affecting audio or network state.

**Primary recommendation:** Wire NJClient::AudioProc() into processBlock() with float** pointer arrays extracted from juce::AudioBuffer, wire NJClient::Run() and command queue processing into NinjamRunThread::run(), set up NJClient work directory via juce::File::getSpecialLocation(), add a hardcoded auto-connect (or minimal connect button) for testing, and verify with pluginval + manual server connection test.

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| JUCE-03 | NJClient audio processing integrated via processBlock() | processBlock extracts float** from AudioBuffer, calls NJClient::AudioProc() with correct innch/outnch/srate/len; NinjamRunThread runs NJClient::Run() loop with command queue; APVTS parameters synced to NJClient atomics; work directory set via juce::File temp location |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | AudioProcessor framework | Already scaffolded in Phase 2; processBlock is the audio entry point |
| njclient | in-tree | NINJAM protocol, encoding, decoding, mixing | Existing battle-tested code; AudioProc() and Run() are the two entry points |
| wdl | in-tree | Networking (jnetlib), mutexes, SHA | Transitive dependency via njclient |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| juce::File | 8.0.12 | Cross-platform temp directory for NJClient work dir | SetWorkDir() needs a writable path for downloaded audio blobs |
| juce::CriticalSection | 8.0.12 | Mutex for NJClient API calls (excluding AudioProc) | Replaces std::mutex for client_mutex in JUCE build |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| juce::CriticalSection | std::mutex | Both work; CriticalSection is JUCE-idiomatic and non-recursive by default, matching existing usage |
| Hardcoded auto-connect for testing | Minimal JUCE UI with connect button | Auto-connect is simpler but less flexible; a minimal connect button would be more practical for manual testing |
| juce::File temp dir | Hardcoded /tmp path | juce::File is cross-platform (macOS + Windows); /tmp is Unix-only |

## Architecture Patterns

### Recommended Project Structure
```
juce/
  JamWideJuceProcessor.h     # AudioProcessor -- owns NJClient + NinjamRunThread
  JamWideJuceProcessor.cpp   # processBlock -> AudioProc bridge, lifecycle
  JamWideJuceEditor.h        # Editor -- thin view, survives create/destroy cycles
  JamWideJuceEditor.cpp      # Minimal UI (Phase 3: connect button + status label)
  NinjamRunThread.h           # juce::Thread -- runs NJClient::Run() + command processing
  NinjamRunThread.cpp         # Full run loop implementation
```

### Pattern 1: processBlock -> NJClient::AudioProc Bridge
**What:** Extract raw float** pointers from juce::AudioBuffer and pass to NJClient::AudioProc()
**When to use:** Every audio callback
**Example:**
```cpp
void JamWideJuceProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalChannels = buffer.getNumChannels();

    // NJClient expects float** arrays for input and output
    // JUCE AudioBuffer stores channels contiguously in memory
    // We extract write pointers for the main stereo pair

    // Input: first 2 channels (main stereo input bus)
    float* inPtrs[2] = { nullptr, nullptr };
    if (getTotalNumInputChannels() >= 2)
    {
        // getWritePointer is safe for input channels too
        inPtrs[0] = buffer.getWritePointer(0);
        inPtrs[1] = buffer.getWritePointer(1);
    }

    // Output: first 2 channels (main mix output bus 0)
    float* outPtrs[2] = { nullptr, nullptr };
    if (totalChannels >= 2)
    {
        outPtrs[0] = buffer.getWritePointer(0);
        outPtrs[1] = buffer.getWritePointer(1);
    }

    // Sync APVTS parameters to NJClient atomics
    if (client)
    {
        auto* masterVol = apvts.getRawParameterValue("masterVol");
        auto* masterMute = apvts.getRawParameterValue("masterMute");
        auto* metroVol = apvts.getRawParameterValue("metroVol");
        auto* metroMute = apvts.getRawParameterValue("metroMute");

        if (masterVol) client->config_mastervolume.store(*masterVol, std::memory_order_relaxed);
        if (masterMute) client->config_mastermute.store(*masterMute >= 0.5f, std::memory_order_relaxed);
        if (metroVol) client->config_metronome.store(*metroVol, std::memory_order_relaxed);
        if (metroMute) client->config_metronome_mute.store(*metroMute >= 0.5f, std::memory_order_relaxed);

        // Check connection status (lock-free)
        int status = client->cached_status.load(std::memory_order_acquire);

        if (status == NJClient::NJC_STATUS_OK && outPtrs[0] && outPtrs[1])
        {
            client->AudioProc(
                inPtrs, 2,          // input: stereo
                outPtrs, 2,         // output: stereo (main mix bus 0)
                numSamples,
                static_cast<int>(getSampleRate()),
                false,              // justmonitor
                true,               // isPlaying (standalone always "playing")
                false,              // isSeek
                -1.0                // cursessionpos
            );
            return;
        }
    }

    // Not connected: silence output
    for (int ch = 0; ch < totalChannels; ++ch)
        buffer.clear(ch, 0, numSamples);
}
```

### Pattern 2: NinjamRunThread Full Implementation
**What:** Port the CLAP run_thread_func logic into NinjamRunThread::run() using juce::Thread idioms
**When to use:** The NinjamRunThread body that Phase 2 left as a skeleton
**Example:**
```cpp
void NinjamRunThread::run()
{
    int lastStatus = NJClient::NJC_STATUS_DISCONNECTED;

    while (!threadShouldExit())
    {
        NJClient* client = processor.getClient();
        if (!client)
        {
            wait(50);
            continue;
        }

        // Process commands from UI queue
        processCommands(client);

        // Run NJClient under mutex
        {
            const juce::ScopedLock sl(processor.getClientLock());
            while (!client->Run())
            {
                if (threadShouldExit()) return;
            }

            int currentStatus = client->GetStatus();
            client->cached_status.store(currentStatus, std::memory_order_release);

            if (currentStatus != lastStatus)
            {
                lastStatus = currentStatus;
                // On connect: set up default local channel
                if (currentStatus == NJClient::NJC_STATUS_OK)
                {
                    client->SetLocalChannelInfo(0, "Channel",
                        true, 0 | (1 << 10),    // stereo input
                        true, 256,                // 256 kbps
                        true, true);              // transmit enabled
                }
            }
        }

        // Adaptive sleep: connected = 20ms (50Hz), disconnected = 50ms (20Hz)
        wait(lastStatus == NJClient::NJC_STATUS_OK ? 20 : 50);
    }
}
```

### Pattern 3: Processor Owns NJClient (Editor-Independent Lifecycle)
**What:** NJClient and NinjamRunThread live in the processor, not the editor. Editor create/destroy does not affect audio or network.
**When to use:** This is the fundamental architecture decision for the JUCE plugin
**Example:**
```cpp
// In JamWideJuceProcessor.h:
class JamWideJuceProcessor : public juce::AudioProcessor
{
public:
    // NJClient access for NinjamRunThread and editor
    NJClient* getClient() { return client.get(); }
    juce::CriticalSection& getClientLock() { return clientLock; }

private:
    std::unique_ptr<NJClient> client;           // Created in constructor
    std::unique_ptr<NinjamRunThread> runThread;  // Created in prepareToPlay
    juce::CriticalSection clientLock;            // Protects NJClient API (except AudioProc)
};

// In JamWideJuceProcessor constructor:
JamWideJuceProcessor::JamWideJuceProcessor()
    : AudioProcessor(BusesProperties()/* ... */),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Create NJClient immediately -- it exists for the entire processor lifetime
    client = std::make_unique<NJClient>();

    // Set work directory for NJClient temp files
    auto tempDir = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("JamWide");
    tempDir.createDirectory();
    client->SetWorkDir(const_cast<char*>(tempDir.getFullPathName().toRawUTF8()));

    // Set default codec to FLAC (CODEC-05)
    client->SetEncoderFormat(MAKE_NJ_FOURCC('F','L','A','C'));

    // Auto-subscribe to all remote users
    client->config_autosubscribe = 1;
}

// Editor is created/destroyed without touching NJClient:
juce::AudioProcessorEditor* JamWideJuceProcessor::createEditor()
{
    return new JamWideJuceEditor(*this);
    // Editor closing later does NOT disconnect from server
}
```

### Pattern 4: Minimal Connect Button for Phase 3 Testing
**What:** A simple editor with text fields and a connect/disconnect button -- enough to test audio bridge without full Phase 4 UI
**When to use:** Phase 3 only; replaced by full JUCE UI in Phase 4
**Example:**
```cpp
class JamWideJuceEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit JamWideJuceEditor(JamWideJuceProcessor& p)
        : AudioProcessorEditor(p), processor(p)
    {
        setSize(800, 600);

        addAndMakeVisible(serverField);
        serverField.setText("ninbot.com");

        addAndMakeVisible(usernameField);
        usernameField.setText("anonymous");

        addAndMakeVisible(connectButton);
        connectButton.setButtonText("Connect");
        connectButton.onClick = [this] { onConnectClicked(); };

        addAndMakeVisible(statusLabel);
        statusLabel.setText("Disconnected", juce::dontSendNotification);

        startTimerHz(10); // Update status 10x/sec
    }

    void timerCallback() override
    {
        if (auto* client = processor.getClient())
        {
            int status = client->cached_status.load(std::memory_order_acquire);
            // Update status label based on status
        }
    }
};
```

### Pattern 5: NJClient Work Directory Setup
**What:** NJClient needs a writable directory for temporary downloaded audio blobs (remote user audio chunks)
**When to use:** Before any connection attempt
**Example:**
```cpp
// Cross-platform temp directory via JUCE
auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
    .getChildFile("JamWide");
tempDir.createDirectory();
client->SetWorkDir(const_cast<char*>(tempDir.getFullPathName().toRawUTF8()));
```
**Why this matters:** NJClient::SetWorkDir() creates 16 hex subdirectories (0-f) for storing downloaded audio chunks. Without a work directory, remote audio cannot be decoded and played back. The existing CLAP plugin never called SetWorkDir -- this was a latent bug that happened to work because NJClient falls back to empty string (current directory). The JUCE version must set this properly.

### Anti-Patterns to Avoid
- **Creating NJClient in the editor:** The editor is destroyed when the plugin window closes. NJClient must live in the processor to survive editor lifecycle. This is explicitly tested by success criterion #4 ("closing and reopening the plugin editor window does not interrupt audio").
- **Locking client_mutex from processBlock:** NJClient::AudioProc() is designed to be called without the client_mutex. It uses its own internal WDL_Mutex (m_users_cs, m_locchan_cs). Never acquire client_mutex from the audio thread -- it would block on Run() and cause audio dropouts.
- **Using juce::AudioBuffer::getArrayOfWritePointers():** This returns float*const* (pointer to const pointer), but NJClient::AudioProc expects float**. Extract individual channel pointers into a local float*[] array instead.
- **Forgetting to zero output before AudioProc:** NJClient::AudioProc() zeroes the output buffer internally at the top of the function (`memset(outbuf[x],0,sizeof(float)*len)`), so this is actually safe. However, for clarity and safety when NJClient is not connected, the processBlock should clear the buffer for non-connected states.
- **Running NinjamRunThread without client_mutex on NJClient::Run():** The Run() method modifies NJClient state (remote user lists, connection state, etc.). It MUST be called under client_mutex. AudioProc is the ONLY NJClient method safe to call without client_mutex.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Audio buffer pointer extraction | Custom memory management | juce::AudioBuffer::getWritePointer() | Buffer ownership and alignment handled by JUCE |
| Temp directory location | Hardcoded /tmp or %TEMP% paths | juce::File::getSpecialLocation(tempDirectory) | Cross-platform, sandbox-aware |
| Thread-safe parameter sync | Custom atomic parameter bridge | APVTS getRawParameterValue() | Already thread-safe, DAW-automatable |
| Connection status polling | Custom timer/polling | juce::Timer in editor | JUCE-idiomatic, auto-cleanup on destruction |
| NJClient lifecycle | Manual new/delete with raw pointers | std::unique_ptr<NJClient> in processor | RAII cleanup, exception safety |

**Key insight:** Phase 3 is a wiring phase. The audio processing, encoding, decoding, and protocol handling already exist in NJClient. The JUCE scaffolding already exists from Phase 2. Phase 3 connects these two layers without building new functionality. Resist the urge to refactor NJClient internals -- that is out of scope.

## Common Pitfalls

### Pitfall 1: In-Place Buffer Conflict
**What goes wrong:** JUCE may provide the same buffer for input and output (in-place processing). NJClient::AudioProc() zeroes the output buffer at the start, destroying the input before it can be read.
**Why it happens:** Some hosts optimize by reusing the same memory for input and output when the plugin declares it supports in-place processing.
**How to avoid:** Before calling AudioProc, copy input data to a separate buffer if the input and output pointers overlap. Check `if (inPtrs[0] == outPtrs[0])` and copy to a local juce::AudioBuffer if so. Alternatively, always copy input to a scratch buffer before AudioProc.
**Warning signs:** Audio input is silent or garbled when connected; only metronome and remote audio heard, no local channel.

### Pitfall 2: Sample Rate Mismatch
**What goes wrong:** The host sample rate (getSampleRate()) may differ from NJClient's expected rate. NJClient dynamically adapts via the `srate` parameter to AudioProc, but if prepareToPlay is called with a different rate than the audio callback, interval length calculations will be wrong.
**Why it happens:** NJClient sets m_srate from AudioProc's srate parameter. The sample rate is correct as long as it matches the actual audio callback rate. But if the host changes sample rate without calling prepareToPlay (some hosts do this), there could be a transient mismatch.
**How to avoid:** Store sample rate in prepareToPlay and pass it consistently to AudioProc. NJClient handles rate changes gracefully since it recalculates interval_length when beatinfo_updated is set.
**Warning signs:** Metronome clicks at wrong tempo; interval boundaries misaligned.

### Pitfall 3: NJClient Work Directory Not Set
**What goes wrong:** Remote audio never plays back. Users connect, see remote users in the UI, but hear only silence from remote channels.
**Why it happens:** NJClient writes downloaded audio chunks to disk (as temp OGG/FLAC files) for decode. Without SetWorkDir(), the file paths resolve to empty string, and file I/O fails silently.
**How to avoid:** Call SetWorkDir() with a valid writable directory path before any connection attempt. Use juce::File::getSpecialLocation(tempDirectory) for cross-platform correctness.
**Warning signs:** No remote audio despite connected status; local channel audio sends fine; debug log shows file write errors.

### Pitfall 4: Editor Lifecycle Disrupting Audio
**What goes wrong:** Closing the plugin editor window disconnects from the server or stops audio processing.
**Why it happens:** If NJClient or NinjamRunThread are owned by the editor (or if the editor's destructor calls disconnect), closing the window tears down the audio pipeline.
**How to avoid:** Ensure NJClient and NinjamRunThread are members of JamWideJuceProcessor, not JamWideJuceEditor. The editor should only read state and send commands; it must never own the client or thread. Success criterion #4 explicitly tests this.
**Warning signs:** Audio stops when plugin window is closed and reopened.

### Pitfall 5: License Callback Blocking
**What goes wrong:** When connecting to a NINJAM server that requires license acceptance, the license callback in the run thread blocks waiting for user response. If the editor is not open (or has no license UI), the thread hangs until timeout (60 seconds).
**Why it happens:** The existing CLAP plugin's license callback uses a condition variable with 60-second timeout. The JUCE version must replicate this pattern or the connection will appear to hang.
**How to avoid:** For Phase 3, implement auto-accept for the license callback (return 1 immediately). Phase 4 UI will add proper license dialog. Document this as a known limitation.
**Warning signs:** Connection hangs for 60 seconds on servers with license text, then fails.

### Pitfall 6: client_mutex Contention Causing Audio Glitches
**What goes wrong:** Audio drops out because processBlock is waiting on client_mutex.
**Why it happens:** This would only happen if processBlock erroneously acquires client_mutex. NJClient::AudioProc() is specifically designed to NOT require client_mutex. It uses its own internal lightweight mutexes (m_users_cs, m_locchan_cs).
**How to avoid:** NEVER lock client_mutex from processBlock(). The only code that should lock client_mutex is: NinjamRunThread (for Run() and command execution), and the editor (for reading NJClient state like user lists -- but this should use snapshots or the existing GetRemoteUsersSnapshot() method instead where possible).
**Warning signs:** Periodic audio dropouts correlated with NJClient::Run() timing.

### Pitfall 7: const_cast for SetWorkDir Path
**What goes wrong:** Compiler error because NJClient::SetWorkDir() takes `char*` (not `const char*`), but JUCE strings return const data.
**Why it happens:** The Cockos NJClient API predates const-correctness conventions.
**How to avoid:** Use `const_cast<char*>(path.toRawUTF8())` when calling SetWorkDir(). This is safe because SetWorkDir copies the string internally. Alternatively, copy to a local char array.
**Warning signs:** Compilation error on SetWorkDir call.

## Code Examples

### NJClient::AudioProc Signature (from njclient.h)
```cpp
// Source: src/core/njclient.h line 115
void AudioProc(float **inbuf, int innch,
               float **outbuf, int outnch,
               int len, int srate,
               bool justmonitor = false,
               bool isPlaying = true,
               bool isSeek = false,
               double cursessionpos = -1.0);
```

### Existing CLAP Plugin AudioProc Call (reference for JUCE port)
```cpp
// Source: src/plugin/clap_entry.cpp lines 419-426
// This is what the CLAP plugin does. The JUCE version should replicate this pattern.
client->AudioProc(
    in, 2, out, 2,
    static_cast<int>(frames),
    static_cast<int>(plugin->sample_rate),
    just_monitor, is_playing, is_seek, cursor_pos
);
```

### NJClient Initialization (from existing CLAP plugin_activate)
```cpp
// Source: src/plugin/clap_entry.cpp lines 200-212
// The JUCE version should replicate this NJClient setup:
// 1. Create NJClient instance
// 2. Set up chat callback
// 3. Set up license callback
// 4. Start run thread
```

### Chat Callback Pattern (from existing run_thread.cpp)
```cpp
// Source: src/threading/run_thread.cpp lines 26-110
// The chat callback is set on NJClient via function pointer:
client->ChatMessage_Callback = chat_callback;
client->ChatMessage_User = &processor;  // void* user data
```

### License Callback Pattern (simplified for Phase 3)
```cpp
// Phase 3: auto-accept license (proper UI in Phase 4)
int license_callback(void* user_data, const char* license_text)
{
    // Auto-accept for Phase 3 testing
    // Phase 4 will add proper UI dialog
    return 1;  // 1 = accept
}
```

### In-Place Buffer Safety
```cpp
// Handle in-place processing where input == output buffer
void JamWideJuceProcessor::processBlock(juce::AudioBuffer<float>& buffer, ...)
{
    const int numSamples = buffer.getNumSamples();

    // Copy input to scratch buffer BEFORE AudioProc zeroes the output
    inputScratch.setSize(2, numSamples, false, false, true);
    for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
        inputScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    float* inPtrs[2] = { inputScratch.getWritePointer(0),
                          inputScratch.getWritePointer(1) };
    float* outPtrs[2] = { buffer.getWritePointer(0),
                           buffer.getWritePointer(1) };

    client->AudioProc(inPtrs, 2, outPtrs, 2, numSamples, ...);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Custom std::thread + shutdown atomic | juce::Thread + threadShouldExit() + wait() | Phase 2 (JUCE migration) | JUCE-idiomatic, interruptible wait, named threads |
| CLAP process callback with raw pointers | JUCE processBlock with AudioBuffer | Phase 2 (JUCE migration) | Type-safe buffer access, automatic channel management |
| Manual mutex (std::mutex) | juce::CriticalSection | Phase 3 (this phase) | JUCE-idiomatic, works with ScopedLock |
| No work directory (latent bug) | Explicit SetWorkDir via juce::File temp dir | Phase 3 (this phase) | Remote audio actually plays back reliably |

**Deprecated/outdated:**
- The existing CLAP plugin's audio path (src/plugin/clap_entry.cpp:plugin_process) is the reference implementation but will be superseded by the JUCE processBlock in the JUCE target.
- The existing run_thread.cpp (std::thread-based) is the reference for command processing but uses std::thread patterns instead of juce::Thread.

## Open Questions

1. **Input buffer handling for in-place hosts**
   - What we know: Some hosts provide in-place buffers (input == output). NJClient::AudioProc() zeroes output at the top, which would destroy input data.
   - What's unclear: Which hosts actually do in-place processing for effect plugins with separate input/output buses.
   - Recommendation: Always copy input to a scratch buffer before calling AudioProc. The memory cost is trivial (2 * numSamples * sizeof(float) per callback, typically ~4KB). This eliminates the entire class of in-place bugs.

2. **License callback UI for Phase 3**
   - What we know: NINJAM servers can require license acceptance before allowing participation. The existing CLAP plugin has a blocking license callback with condition variable.
   - What's unclear: Whether the Phase 3 minimal UI should include a license dialog or auto-accept.
   - Recommendation: Auto-accept licenses in Phase 3. Phase 4 will implement proper license dialog UI. Document this as a known limitation in the Phase 3 testing notes.

3. **Standalone vs Plugin transport state**
   - What we know: The CLAP plugin reads transport state (isPlaying/isSeek) from the host. Standalone mode has no host transport.
   - What's unclear: How JUCE's StandalonePluginHolder handles AudioPlayHead in standalone mode.
   - Recommendation: For Phase 3, always pass isPlaying=true and justmonitor=false in standalone mode. This means audio always flows. Phase 7 (DAW Sync) will properly integrate AudioPlayHead for plugin mode.

4. **Soft clipping on master output**
   - What we know: The existing CLAP plugin applies soft clipping to the master output after AudioProc. NJClient::process_samples already applies master volume/pan.
   - What's unclear: Whether NJClient's internal output is already bounded or can exceed [-1, 1].
   - Recommendation: Do NOT add soft clipping in Phase 3. NJClient's master volume stage already scales output. If clipping is needed, it belongs in a later phase as a separate audio processing stage. Keep processBlock simple.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | pluginval (plugin format validation) + manual connection test |
| Config file | None (pluginval is CLI; manual test uses live NINJAM server) |
| Quick run command | `cmake --build build --target JamWideJuce_VST3 && cmake --build build --target validate` |
| Full suite command | `cmake --build build && cmake --build build --target validate` (pluginval VST3 strictness 5) |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| JUCE-03a | processBlock calls AudioProc without crash | pluginval | `cmake --build build --target validate` | Existing (Phase 2 validate target) |
| JUCE-03b | Connect to server and hear remote audio | manual | Launch standalone, connect to ninbot.com, verify audio | N/A (manual) |
| JUCE-03c | Local mic/instrument audio sent to server | manual | Connect two instances, verify bidirectional audio | N/A (manual) |
| JUCE-03d | Vorbis and FLAC codecs both work | manual | Connect, switch codec, verify audio continues | N/A (manual) |
| JUCE-03e | Editor close/reopen does not interrupt audio | pluginval + manual | pluginval tests editor lifecycle; manual verify audio continues | Existing |

### Sampling Rate
- **Per task commit:** Build JamWideJuce_VST3 target (compile check)
- **Per wave merge:** pluginval validation + manual standalone connection test
- **Phase gate:** pluginval passes + successful bidirectional audio with at least one NINJAM server

### Wave 0 Gaps
None -- existing test infrastructure (pluginval validate target from Phase 2) covers automated validation. Manual testing requires a running NINJAM server (ninbot.com is publicly available).

## Sources

### Primary (HIGH confidence)
- Existing codebase: `src/core/njclient.h` (line 115) -- AudioProc signature and threading contract
- Existing codebase: `src/core/njclient.cpp` (lines 767-861) -- AudioProc implementation, buffer zeroing, interval management
- Existing codebase: `src/plugin/clap_entry.cpp` (lines 256-451) -- Reference CLAP audio processing implementation
- Existing codebase: `src/threading/run_thread.cpp` -- Reference run thread with command processing, callbacks, status management
- Existing codebase: `juce/JamWideJuceProcessor.cpp` -- Phase 2 skeleton with bus layout and empty processBlock
- Existing codebase: `juce/NinjamRunThread.cpp` -- Phase 2 skeleton thread with wait(50) body
- Phase 2 Research: `.planning/phases/02-juce-scaffolding/02-RESEARCH.md` -- JUCE patterns, thread lifecycle, APVTS

### Secondary (MEDIUM confidence)
- JUCE AudioProcessor documentation -- processBlock contract, buffer ownership, sample rate
- JUCE AudioBuffer documentation -- getWritePointer, channel indexing
- JUCE File documentation -- getSpecialLocation(tempDirectory) for cross-platform temp paths
- JUCE Thread documentation -- threadShouldExit(), wait(), ScopedLock

### Tertiary (LOW confidence)
- None -- this phase is entirely internal wiring between two well-understood codebases

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- existing codebase, no new libraries needed
- Architecture: HIGH -- pattern directly mirrors existing CLAP plugin, well-understood NJClient API
- Pitfalls: HIGH -- identified from existing CLAP implementation experience and NJClient documentation
- In-place buffer handling: MEDIUM -- depends on host behavior, but defensive copy eliminates risk

**Research date:** 2026-03-07
**Valid until:** 2026-04-07 (30 days -- no external dependencies that change rapidly)
