# Domain Pitfalls

**Domain:** NINJAM client JUCE migration (plugin + standalone, multichannel, FLAC, DAW sync)
**Researched:** 2026-03-07

## Critical Pitfalls

Mistakes that cause rewrites, data loss, or fundamental architecture rework.

### Pitfall 1: NJClient::Run() Blocking the JUCE Message Thread

**What goes wrong:** The current architecture runs `NJClient::Run()` in a dedicated `std::thread` that polls every ~50ms. During JUCE migration, developers instinctively reach for `juce::Timer` (which fires on the message thread) or try to call NJClient methods from the message thread directly. NJClient::Run() performs blocking network I/O via JNetLib, DNS resolution, and Vorbis/FLAC encoding -- all of which can stall for 100ms+. Putting this on the message thread freezes the GUI.

**Why it happens:** JUCE's programming model pushes everything toward the message thread (Timer, AsyncUpdater, callAsync). NJClient was designed for a dedicated thread, not for message-thread polling.

**Consequences:** GUI freezes during connection, encoding spikes, or DNS lookups. On macOS, the system may show the spinning beachball. DAW hosts may report the plugin as unresponsive and kill it.

**Prevention:**
- Keep a dedicated `juce::Thread` subclass for NJClient::Run() -- do not use `juce::Timer` for network polling
- Use `juce::MessageManager::callAsync()` or `juce::AsyncUpdater` to push events from the run thread to the message thread for UI updates
- Never acquire `MessageManagerLock` from the run thread while holding `client_mutex` (deadlock risk)
- Use the existing SPSC ring buffer pattern (or `juce::AbstractFifo`) for lock-free audio-thread to run-thread communication

**Detection:** GUI becomes unresponsive during connection or when remote users join/leave. Profile with Instruments (macOS) or ETW (Windows) showing message thread blocked on network I/O.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- get the threading architecture right before building anything on top.

---

### Pitfall 2: Calling NJClient API from the Audio Thread

**What goes wrong:** NJClient's `AudioProc()` is the only method safe to call from the audio thread. All other NJClient methods (Connect, Run, SetLocalChannelInfo, GetRemoteUser, etc.) require `client_mutex`. In the current codebase, the audio thread carefully avoids this mutex. During JUCE migration, it is tempting to call NJClient state-query methods from `processBlock()` to read remote user info, BPM, or connection status -- this introduces mutex contention on the real-time audio thread.

**Why it happens:** JUCE's `processBlock()` is the natural place to "do everything audio-related," and developers may not realize which NJClient methods are mutex-protected versus which are safe (atomic/lock-free).

**Consequences:** Audio dropouts, glitches, and priority inversion. The audio thread blocks waiting for `client_mutex` held by the run thread during encoding or network operations. Worst case: audio thread starves and DAW reports xruns.

**Prevention:**
- Document clearly which NJClient methods are audio-thread-safe (only `AudioProc`)
- Continue the atomic snapshot pattern (`UiAtomicSnapshot`) for any data processBlock needs: BPM, BPI, beat position, connection status
- Never acquire `client_mutex` in `processBlock()` -- not even "just for a quick read"
- Use `std::atomic` or lock-free queues for all audio-thread communication
- Enable ThreadSanitizer in CI to catch violations early

**Detection:** Intermittent audio glitches under load, especially when many remote users are connected. ThreadSanitizer reports. Audio dropout counters in DAW increase.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- establish the boundary contract for thread safety before implementing features.

---

### Pitfall 3: libFLAC Symbol Collision with JUCE's Bundled FLAC

**What goes wrong:** JUCE includes a complete copy of libFLAC source code in `juce_audio_formats/codecs/flac/`, compiled directly into `juce_FlacAudioFormat.cpp`. The current FLAC integration plan adds an external libFLAC git submodule at `libs/libflac/`. Linking both produces duplicate symbol errors for `FLAC__stream_encoder_init_stream`, `FLAC__stream_decoder_init_stream`, and dozens of other FLAC API functions.

**Why it happens:** JUCE wraps its FLAC copy in `FlacNamespace` to isolate symbols, but the external libFLAC uses the global namespace. If `JUCE_USE_FLAC=1` (the default), both copies compile into the same binary. Even if JUCE's copy is namespaced, the external libFLAC's C symbols in the global namespace create linker ambiguity.

**Consequences:** Build failure (duplicate symbols) or, worse, silent symbol resolution to the wrong copy causing crashes or codec corruption.

**Prevention:**
Two viable approaches:
1. **Use JUCE's bundled FLAC:** Set `JUCE_USE_FLAC=1`, use `juce::FlacNamespace::FLAC__*` APIs directly in `FlacEncoder`/`FlacDecoder` wrappers. No external submodule needed. Requires adapting the FLAC integration plan to use JUCE's namespaced API instead of raw libFLAC.
2. **Use external libFLAC only:** Set `JUCE_USE_FLAC=0` to disable JUCE's bundled copy, link your own libFLAC submodule. Loses `juce::FlacAudioFormat` for file I/O (not needed for NINJAM streaming, but worth noting).

Recommendation: Option 1 -- use JUCE's bundled FLAC. It simplifies the build, avoids submodule management, and JUCE maintains the FLAC version.

**Detection:** Linker errors mentioning duplicate FLAC symbols. If somehow it links, runtime crashes in FLAC encode/decode with corrupted function pointers.

**Phase relevance:** FLAC phase -- must decide before implementing `FlacEncoder`/`FlacDecoder`. Affects the FLAC integration plan directly.

---

### Pitfall 4: Editor Lifetime Assumptions Break Network State

**What goes wrong:** In JUCE, the `AudioProcessorEditor` can be created and destroyed at any time by the host. Some DAWs destroy the editor when the plugin window is closed, then recreate it when reopened. If the editor holds references to NJClient state, chat history, remote user lists, or UI state, these get wiped on editor destruction. The NJClient connection continues running headless (processor stays alive), but the UI state is lost.

**Why it happens:** The current Dear ImGui architecture has a persistent UI state object (`UiState`) that lives in `JamWidePlugin` -- it never gets destroyed while the plugin is alive. JUCE separates Processor (persistent) and Editor (transient). Developers naturally put state in the Editor because that is where it renders.

**Consequences:** Chat history disappears when user closes/reopens plugin window. Connection settings reset. Remote user mixer positions lost. Server browser results gone.

**Prevention:**
- All persistent state (chat history, connection info, remote user mixer settings, server browser cache) must live in the `AudioProcessor` subclass, not the `Editor`
- The Editor reads state from the Processor on construction and writes changes back via the command queue
- Use `juce::ValueTree` for observable state that the Editor can listen to via `ValueTree::Listener`
- Test by repeatedly opening/closing the plugin window while connected to a server

**Detection:** Close and reopen the plugin window -- if chat history or mixer settings disappear, state is in the wrong place.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- get the Processor/Editor state boundary right from day one.

---

### Pitfall 5: Multichannel Bus Layout Rejected by DAW Hosts

**What goes wrong:** JamWide needs per-user stereo output pairs (e.g., 8 users = 16 channels = 8 stereo buses). JUCE's bus system lets you declare multiple output buses, but DAWs are inconsistent about supporting them. Logic Pro caches bus configurations by AU subtype code and requires a version number change to recognize new configurations. Some DAWs only see the main stereo bus. VST3 hosts may ignore auxiliary buses entirely.

**Why it happens:** The plugin is passive -- it cannot force a bus layout. It can only accept or reject what the host proposes. Each DAW has its own multi-bus support quirks. Logic Pro's AudioComponentRegistrar caches AU configurations aggressively.

**Consequences:** Plugin loads in stereo-only mode despite declaring 8 output buses. Users cannot route individual remote users to separate mixer channels. The core value proposition of per-user mixing in the DAW is lost.

**Prevention:**
- Declare all auxiliary buses as enabled stereo outputs in the constructor (not optional/disabled)
- Do NOT override `canAddBus()` or `canRemoveBus()` -- this breaks Logic Pro
- Implement `isBusesLayoutSupported()` to accept both stereo-only (fallback) and full multi-bus configurations
- Allocate resources in `prepareToPlay()`, not in `isBusesLayoutSupported()` (called many times during scanning)
- Change the AU version number any time the bus configuration changes -- Logic Pro requires this for re-scanning
- Run `auval` on every build to catch AU validation failures before they reach users
- Test in at least Logic Pro, REAPER, Ableton Live, and Bitwig
- Run `killall -9 AudioComponentRegistrar` during development to clear stale AU caches

**Detection:** Plugin shows only stereo output in DAW mixer. `auval -a` reports validation failure. Logic Pro shows wrong channel count.

**Phase relevance:** Multichannel phase -- but bus declarations must be designed in Phase 1 scaffolding even if routing is implemented later.

---

### Pitfall 6: processBlock Sample Rate Mismatch with NJClient

**What goes wrong:** NJClient encodes and decodes audio at a fixed sample rate set during `AudioProc` configuration. JUCE's `prepareToPlay()` can be called multiple times with different sample rates, and `prepareToPlay()` can be called again without `releaseResources()` in between. If NJClient's encoder is initialized at 48000 Hz but the DAW switches to 44100 Hz, the encoded audio plays at the wrong speed for all remote users, and decoded remote audio plays at the wrong pitch locally.

**Why it happens:** The current plugin activates at a fixed sample rate and never changes. JUCE plugins must handle dynamic sample rate changes gracefully. Some standalone audio device changes also trigger `prepareToPlay()` with a new rate.

**Consequences:** Remote users hear pitch-shifted audio. Metronome timing drifts. Interval boundaries misalign. Audio artifacts at rate-change boundaries.

**Prevention:**
- In `prepareToPlay()`, always reinitialize NJClient's encoder/decoder configuration with the new sample rate
- If connected to a server, trigger a codec reinit at the next interval boundary (not mid-interval)
- Consider resampling if the DAW rate differs from the desired network rate (e.g., always transmit at 48000 Hz regardless of local rate)
- Store the current sample rate atomically so the run thread knows when it has changed
- Handle `prepareToPlay()` being called multiple times in succession without panicking

**Detection:** Audio sounds pitch-shifted after changing sample rate in DAW preferences. Metronome clicks at wrong intervals.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- the `prepareToPlay()`/`releaseResources()` lifecycle must be correct before anything else works.

---

## Moderate Pitfalls

### Pitfall 7: Standalone Mode Has No AudioPlayHead

**What goes wrong:** In standalone mode, `getPlayHead()` returns nullptr or a stub that provides no meaningful BPM, time signature, or transport state. JamWide's DAW sync feature (reading host tempo/transport to sync NINJAM intervals) silently fails in standalone. If the code assumes `getPlayHead()` always returns valid data, it crashes with a null dereference or uses uninitialized BPM values.

**Prevention:**
- Implement a "pseudo transport" class that provides a fallback internal clock when no host transport is available
- In standalone, use the server-provided BPM/BPI as the authoritative tempo source (this is already the NINJAM model)
- Guard every `getPlayHead()` call with null checks and `std::optional` handling for BPM values
- Only call `getPlayHead()` from within `processBlock()` -- calling it elsewhere produces undefined behavior
- Cache transport values in atomics per-buffer rather than calling `getPlayHead()` per-sample

**Detection:** Standalone app crashes on launch or shows BPM as 0/NaN. DAW sync feature does nothing in standalone mode.

**Phase relevance:** DAW sync phase and standalone phase -- but the null-safe pattern should be established in Phase 1.

---

### Pitfall 8: JUCE State Save/Restore Drops Connection Context

**What goes wrong:** JUCE's `getStateInformation()`/`setStateInformation()` serializes plugin state for DAW session save/recall. If connection parameters (server address, username), codec selection, or mixer settings are not included in the state, reopening a DAW session loses all configuration. Conversely, if the state includes "connected" status, the plugin may try to auto-reconnect on session load, which may not be desired.

**Prevention:**
- Save connection parameters, codec selection, per-user mixer settings, and UI preferences in `getStateInformation()` via `AudioProcessorValueTreeState` or custom XML
- Do NOT save "currently connected" status -- reconnection should be explicit user action
- Do NOT save passwords to the DAW session file (security risk -- session files are shared)
- Handle `setStateInformation()` being called before `prepareToPlay()` -- buffer the state and apply it when the plugin activates
- Test with DAW session save/load cycle: settings should persist, connection should not auto-start

**Detection:** Open a saved DAW session -- server address, username, and mixer settings are blank. Or worse: plugin auto-connects to a server on session load without user consent.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- state serialization should be designed early to avoid retrofitting.

---

### Pitfall 9: Parameter Callbacks Fire on Unpredictable Threads

**What goes wrong:** JUCE's `parameterChanged()` and `parameterValueChanged()` callbacks can be called from any thread -- the audio thread, the message thread, or an internal host thread. Developers treat these callbacks as "UI events" and call `repaint()`, acquire mutexes, or allocate memory, causing crashes or deadlocks.

**Prevention:**
- Treat parameter callbacks as if they are on the audio thread: no allocations, no locks, no UI calls
- Use a `juce::Timer` or `juce::VBlankAttachment` with an `std::atomic<bool>` dirty flag to defer UI updates
- For parameters that control NJClient state (volume, mute, pan), write to atomics in the callback, read from atomics in the run thread
- Use `AudioProcessorValueTreeState` (APVTS) for thread-safe parameter management -- do not roll custom parameter systems

**Detection:** Random crashes during automation playback. Deadlocks when adjusting parameters while the plugin is processing audio. ThreadSanitizer reports in CI.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- parameters must be set up correctly from the start.

---

### Pitfall 10: FLAC Encoder Finish Blocks at Interval Boundaries

**What goes wrong:** `FLAC__stream_encoder_finish()` flushes buffered audio and writes final metadata. This call can block for a significant duration (especially with larger block sizes or if multithreaded encoding is enabled). In NINJAM, each interval is a complete FLAC stream -- so `finish()` is called at every interval boundary. If this happens on the run thread while it holds `client_mutex`, the audio thread's `AudioProc()` may be blocked (depending on serialization settings).

**Why it happens:** NINJAM intervals create a new encoder per interval and finalize the previous one. FLAC's finalization writes the STREAMINFO metadata block (which requires the final frame count), flushing all remaining data.

**Prevention:**
- Use small FLAC block sizes (1024 samples = ~23ms at 44.1kHz) to minimize buffered data at finalization
- Profile `FLAC__stream_encoder_finish()` latency -- if >5ms, consider double-buffering (finalize previous interval's encoder in a separate thread while the new interval's encoder is already running)
- Do not enable libFLAC's multithreading option -- it makes `finish()` wait for worker threads
- Keep `client_mutex` hold time minimal during interval transitions
- Set FLAC compression level to 0-3 (fast) rather than 5-8 (slow) for real-time use

**Detection:** Audio glitches at interval boundaries when using FLAC. Profiler shows `FLAC__stream_encoder_finish()` taking >5ms.

**Phase relevance:** FLAC phase -- must profile during implementation.

---

### Pitfall 11: WDL JNetLib Conflicts with JUCE Networking

**What goes wrong:** The current codebase uses WDL's JNetLib for all TCP networking (NINJAM protocol) and HTTP (server list fetching). JUCE provides its own networking classes (`juce::URL`, `juce::StreamingSocket`, `juce::WebInputStream`). Using both simultaneously can cause issues: conflicting socket initialization on Windows (`WSAStartup`/`WSACleanup` reference counting), conflicting DNS resolution, and confused error handling.

**Prevention:**
- Keep JNetLib for the NINJAM TCP protocol -- it is deeply integrated with NJClient and replacing it is high-risk with no benefit
- Replace JNetLib's HTTP fetcher (server list) with JUCE's `juce::URL::createInputStream()` for async HTTP -- this is a clean boundary
- On Windows, ensure `WSAStartup()` is called before any socket use and `WSACleanup()` only on final shutdown -- both JNetLib and JUCE may try to manage this
- Do not mix JUCE and JNetLib sockets in the same thread without verifying initialization order

**Detection:** Server list fetch fails silently. NINJAM connection drops randomly on Windows. `WSAGetLastError()` returns unexpected values.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- decide which networking stack handles what before implementation begins.

---

### Pitfall 12: Static Singletons and Multiple Plugin Instances

**What goes wrong:** Some DAWs load multiple instances of the same plugin in one process. If JamWide uses static variables, singletons, or global state (e.g., a shared logger, a global server list cache, or static NJClient configuration), multiple instances will corrupt each other's state. The current codebase avoids this for NJClient (each plugin instance owns its own `NJClient`), but JUCE's `SharedResourcePointer` or static initializers could introduce new singletons during migration.

**Prevention:**
- Audit all static/global state during migration -- each plugin instance must be fully independent
- Do not use `juce::SharedResourcePointer` for per-instance state
- Logger should use per-instance file names or a shared file with instance-id prefixes
- Server list cache can be shared (read-only after fetch) but must use thread-safe access
- Test with 2+ plugin instances in the same DAW session, connected to different servers

**Detection:** Two plugin instances interfere -- one connects and the other disconnects. Logs from different instances interleave without identification. Chat messages appear in the wrong instance.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- verify instance isolation early.

---

## Minor Pitfalls

### Pitfall 13: pluginval Failures Block Release

**What goes wrong:** `pluginval` is the industry-standard validation tool for JUCE plugins. It tests edge cases like rapid open/close of editor, processBlock with zero samples, bus layout changes, state save/restore, and threading violations. Many developers discover 5+ unknown bugs when first running pluginval. Failing pluginval means the plugin will crash in real DAWs under normal conditions.

**Prevention:**
- Run pluginval in CI from the first buildable plugin binary
- Start with strictness level 5, work up to level 10
- Common failures: processBlock called with 0-sample buffer (must handle gracefully), getStateInformation with no prior setStateInformation, editor created before prepareToPlay
- Fix pluginval failures before adding features -- they indicate fundamental lifecycle bugs

**Detection:** CI pluginval step fails. Users report crashes on session load or when opening/closing plugin windows rapidly.

**Phase relevance:** Phase 1 (Core JUCE scaffolding) -- integrate pluginval into CI immediately.

---

### Pitfall 14: Dear ImGui Rendering Assumptions Incompatible with JUCE Components

**What goes wrong:** During an incremental migration, developers sometimes try to embed Dear ImGui inside a JUCE Component (rendering ImGui to a texture and displaying it in a JUCE component). This creates a Frankenstein UI where neither framework has full control: ImGui expects to own the input/rendering pipeline, JUCE expects its Component tree to handle painting. Mouse events, keyboard focus, and accessibility all break.

**Prevention:**
- Do a clean break -- rewrite the UI in JUCE Components from scratch, using the ImGui UI as a visual reference only
- Do not attempt to embed ImGui inside JUCE or vice versa
- Port UI panel-by-panel: connection panel first (most critical), then chat, then local/remote channels, then server browser
- Use JUCE's `LookAndFeel` for consistent styling rather than trying to replicate ImGui's immediate-mode aesthetic

**Detection:** Focus issues -- clicking in the ImGui area does not release JUCE keyboard focus. Mouse coordinates are wrong. Accessibility features (screen readers) do not see ImGui elements.

**Phase relevance:** Phase 1 (UI rewrite) -- establish the UI framework decision cleanly at the start.

---

### Pitfall 15: Codec Switch Mid-Interval Corrupts Stream

**What goes wrong:** The FLAC integration plan correctly specifies codec switching at interval boundaries only. But during implementation, it is easy to accidentally switch the encoder mid-interval (e.g., user toggles FLAC in the UI and the atomic is read immediately by the encoder). A mid-interval switch produces a stream where the first half is Vorbis and the second half is FLAC -- no decoder can handle this.

**Prevention:**
- The `m_encoder_fmt_requested` atomic is written by the UI thread at any time
- The `m_encoder_fmt_active` is only read at the start of a new interval and never changes mid-interval
- Add an assertion in debug builds that `m_encoder_fmt_active` does not change between interval start and end
- The FOURCC sent in `UPLOAD_INTERVAL_BEGIN` must match the encoder that produced the data

**Detection:** Remote users hear silence or static from the switching user. Decoder errors in logs. FOURCC mismatch between header and data.

**Phase relevance:** FLAC phase -- the interval boundary guard is the critical correctness invariant.

---

### Pitfall 16: macOS AU Cache Staleness During Development

**What goes wrong:** During active development, Logic Pro and other AU hosts cache the plugin's capabilities (bus layout, parameters, version). After changing bus configurations, adding parameters, or modifying the AU manifest, the host continues using the cached version. The developer thinks their code changes have no effect.

**Prevention:**
- After any change to bus layout, parameters, or plugin metadata, run: `killall -9 AudioComponentRegistrar`
- Increment the AU version number when bus configurations change
- Clear `~/Library/Caches/AudioUnitCache/` periodically
- Use REAPER (which does not aggressively cache AU metadata) for rapid iteration; validate in Logic Pro less frequently
- Add a build step that auto-increments a dev build number

**Detection:** Changes to bus layout or parameters have no effect in Logic Pro. `auval` passes but Logic Pro shows old configuration.

**Phase relevance:** All phases -- ongoing development workflow issue.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| Core JUCE scaffolding | Threading model wrong (Pitfalls 1, 2) | Keep dedicated run thread; establish audio/UI/network thread boundaries before features |
| Core JUCE scaffolding | Editor/Processor state split wrong (Pitfall 4) | All persistent state in Processor; Editor is a transient view |
| Core JUCE scaffolding | State save/restore incomplete (Pitfall 8) | Design state schema early; test DAW session save/load from day one |
| Core JUCE scaffolding | pluginval failures (Pitfall 13) | Integrate pluginval into CI before adding features |
| FLAC integration | libFLAC symbol collision (Pitfall 3) | Use JUCE's bundled FLAC or disable it; do not link both |
| FLAC integration | Mid-interval codec switch (Pitfall 15) | Guard format switch at interval boundary only |
| FLAC integration | Encoder finish latency (Pitfall 10) | Profile; use small block sizes and low compression levels |
| Multichannel output | Bus layout rejected by hosts (Pitfall 5) | Fixed enabled buses; no canAddBus/canRemoveBus; test across DAWs |
| DAW sync | No AudioPlayHead in standalone (Pitfall 7) | Pseudo transport with server BPM fallback |
| DAW sync | Sample rate mismatch (Pitfall 6) | Reinit encoder on prepareToPlay; consider fixed-rate encoding with resampling |
| UI rewrite | ImGui/JUCE hybrid (Pitfall 14) | Clean break -- do not embed ImGui in JUCE |
| All development | AU cache staleness (Pitfall 16) | Kill AudioComponentRegistrar; increment version numbers |
| All development | Multiple instances (Pitfall 12) | No singletons; test multi-instance scenarios |

## Sources

- [JUCE AudioProcessor documentation](https://docs.juce.com/master/classAudioProcessor.html)
- [JUCE bus layout tutorial](https://docs.juce.com/master/tutorial_audio_bus_layouts.html)
- [JUCE MessageManager documentation](https://docs.juce.com/master/classMessageManager.html)
- [JUCE Thread class documentation](https://docs.juce.com/master/classThread.html)
- [JUCE Timer class documentation](https://docs.juce.com/master/classTimer.html)
- [JUCE AudioPlayHead documentation](https://docs.juce.com/master/classAudioPlayHead.html)
- [JUCE FlacAudioFormat documentation](https://docs.juce.com/master/classFlacAudioFormat.html)
- [Multi-AU bus count issues (getdunne/multi-au)](https://github.com/getdunne/multi-au)
- [JUCE forum: Getting multiple buses working for AU and VST3](https://forum.juce.com/t/getting-multiple-buses-working-for-au-and-vst3/60078)
- [JUCE forum: AudioPlayHead best practices](https://forum.juce.com/t/best-practice-on-plugins-which-require-audioplayhead-information-and-transport-management/60489)
- [JUCE forum: Real-time threading](https://forum.juce.com/t/real-time-thread-in-juce/43361)
- [JUCE forum: MessageManager thread safety](https://forum.juce.com/t/updating-gui-from-other-threads/20906)
- [JUCE forum: Multi-bus AU plugin](https://forum.juce.com/t/multi-bus-au-plugin/53546)
- [Melatonin JUCE tips and tricks](https://melatonin.dev/blog/big-list-of-juce-tips-and-tricks/)
- [JamTaba freezing during plugin loading (GitHub #241)](https://github.com/elieserdejesus/JamTaba/issues/241)
- [JamTaba encoder compatibility issues (GitHub #1075)](https://github.com/elieserdejesus/JamTaba/issues/1075)
- [FLAC stream encoder API documentation](https://xiph.org/flac/api/group__flac__stream__encoder.html)
- [JUCE BREAKING_CHANGES.md](https://github.com/juce-framework/JUCE/blob/master/BREAKING_CHANGES.md)
- [NINJAM protocol documentation (wahjam wiki)](https://github.com/wahjam/wahjam/wiki/Ninjam-Protocol)

---

*Pitfalls audit: 2026-03-07*
