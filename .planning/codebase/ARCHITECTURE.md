<!-- refreshed: 2026-04-30 -->
# Architecture

**Analysis Date:** 2026-04-30

## System Overview

```text
┌────────────────────────────────────────────────────────────────────────────┐
│                          JUCE PLUGIN HOST                                  │
│            (DAW: REAPER / Logic / Live, or JUCE Standalone)                │
└──────┬───────────────────────────────────────────┬─────────────────────────┘
       │ MESSAGE THREAD (UI/host main)             │ AUDIO THREAD (host RT)
       ▼                                           ▼
┌──────────────────────────┐                ┌─────────────────────────────────┐
│ JamWideJuceEditor        │                │ JamWideJuceProcessor            │
│ `juce/JamWideJuceEditor.*│                │ `juce/JamWideJuceProcessor.*`   │
│  ui/ChannelStripArea.*   │                │ ::processBlock(buffer, midi)    │
│  ui/ConnectionBar.*      │                │   syncApvtsToAtomics()          │
│  ui/ChatPanel.*          │                │   collectInputChannels()        │
│  ui/ServerBrowserOverlay*│                │   handleTransportSync()         │
│  ui/VbFader, VuMeter,    │                │   client->AudioProc(...)        │
│  ui/BeatBar, etc.`       │                │   accumulateBusesToMainMix()    │
│                          │                │   routeOutputsToJuceBuses()     │
│ Timer-driven drain of    │                │   measureMasterVu()             │
│ evt_queue + chat_queue,  │                │                                 │
│ writes cmd_queue.        │                │ ALSO: APVTS, OSC, MIDI mapper,  │
│                          │                │ video companion all owned here. │
└────────┬─────────────────┘                └────────────┬────────────────────┘
         │                                                 │ AudioProc
         │ cmd_queue (UI→Run, SPSC)                        ▼ (no lock)
         │ evt_queue/chat_queue (Run→UI, SPSC)        ┌────────────────────────────────┐
         │ uiSnapshot atomics, APVTS                  │ NJClient (audio + run)         │
         ▼                                            │ `src/core/njclient.{h,cpp}`    │
┌────────────────────────────────────┐                │  AudioProc → drainLocalChannel │
│ NinjamRunThread                    │── clientLock ──┤              drainRemoteUser  │
│ `juce/NinjamRunThread.{h,cpp}`     │  (CritSection) │              process_samples  │
│  run() loop:                       │                │              on_new_interval  │
│   - drain cmd_queue → NJClient API │                │              mixInChannel     │
│   - NJClient::Run() (network I/O)  │                │  Run()    ←─ message dispatch  │
│   - update uiSnapshot atomics      │                │              encoder/decoder  │
│   - push evt_queue / chat_queue    │                │              file I/O         │
│   - drainDeferredDelete +          │                │              SPSC drain (RT→Run│
│     drainBroadcastBlocks +         │                │              for blocks/peers)│
│     drainWaveBlocks +              │                └─────┬──────────┬───────────────┘
│     drainArmRequests +             │                      │          │
│     drainLocalChannelDeferredDelete│                      │ tx        │ rx
│     drainRemoteUserDeferredDelete  │                      ▼          ▼
│   - refillSessionmodeBuffers       │                ┌────────────────────────────────┐
│   - serverListFetcher poll         │                │ Net_Connection (TCP+payload    │
│   - 20ms / 50ms adaptive sleep     │                │ encryption hooks)              │
└────────────────────────────────────┘                │ `src/core/netmsg.{h,cpp}`      │
                                                      │  Run(): send/recv message      │
                                                      │  encrypt_payload on send       │
                                                      │  decrypt_payload on recv       │
                                                      │  m_encryption_active gate      │
                                                      └────────────┬───────────────────┘
                                                                   │ JNL_IConnection
                                                                   ▼
                                                      ┌────────────────────────────────┐
                                                      │ JNetLib (WDL)                  │
                                                      │ `wdl/jnetlib/connection.*      │
                                                      │  wdl/jnetlib/asyncdns.*        │
                                                      │  wdl/jnetlib/httpget.*`        │
                                                      │  Async TCP, async DNS, HTTP    │
                                                      └────────────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| `JamWideJuceProcessor` | JUCE `AudioProcessor` host plumbing; owns `NJClient`, run thread, OSC, MIDI, video companion, APVTS, all SPSC queues. Audio entry point (`processBlock`). | `juce/JamWideJuceProcessor.cpp:486` |
| `JamWideJuceEditor` | UI shell; assembles VB-style mixer panels; drains run-thread events on a JUCE `Timer`; pushes user actions to `cmd_queue`. | `juce/JamWideJuceEditor.cpp:1` |
| `NinjamRunThread` | Dedicated `juce::Thread` running NJClient state machine + protocol I/O; ALL network calls happen here under `clientLock`. | `juce/NinjamRunThread.cpp:1` |
| `NJClient` | Core NINJAM client: peer state, encoder/decoder pipelines, transmit/receive paths, mixer, deferred-delete + SPSC mirrors. | `src/core/njclient.cpp:1087` (`AudioProc`), `src/core/njclient.cpp:2562` (`process_samples`) |
| `Net_Connection` / `Net_Message` | NINJAM message framing + per-payload encryption hook. Ref-counted messages. | `src/core/netmsg.cpp:100` (Run/encrypt), `src/core/netmsg.cpp:235` (decrypt) |
| `mpb_*` builders | Parse/build NINJAM protocol message bodies (auth, config, channel info, blocks, chat). | `src/core/mpb.{h,cpp}` |
| `nj_crypto` | AES-256-CBC `encrypt_payload` / `decrypt_payload`; SHA-256 key derivation from password + 8-byte server challenge. | `src/crypto/nj_crypto.{h,cpp}` |
| Codec abstraction (`I_NJEncoder` / `I_NJDecoder`) | Polymorphic encoder/decoder behind the `VorbisEncoderInterface` / `VorbisDecoderInterface` typedefs. Implementations: Vorbis (current default), FLAC (in development per `CHANGELOG.md`). | `wdl/vorbisencdec.h:57,72,91,265`, `wdl/flacencdec.h:34,158`, codec-select macros at `src/core/njclient.cpp:152-204` |
| `RemoteUser` / `Local_Channel` / `DecodeState` / `BufferQueue` / `DecodeMediaBuffer` | Per-peer / per-local-channel canonical objects (run-thread-owned), plus per-stream decoder state and inter-thread byte buffers. Defined inline in `njclient.cpp`. | `src/core/njclient.cpp` |
| `LocalChannelMirror` / `RemoteUserMirror` | Audio-thread-owned **value-only** snapshots of run-thread-owned channel/peer state. No back-pointers (Codex HIGH-2). | `src/core/njclient.h:155` (`LocalChannelMirror`), `src/core/njclient.h:228,276` (`RemoteUserChannelMirror`, `RemoteUserMirror`) |
| SPSC ring + payload variants | Lock-free single-producer/single-consumer transports for state updates, deferred deletes, encoded blocks, decoder chunks, arm requests. | `src/threading/spsc_ring.h`, `src/threading/spsc_payloads.h` |
| `OscServer`, `MidiMapper`, `MidiLearnManager` | OSC/MIDI control surfaces; live on processor (not on audio thread). | `juce/osc/OscServer.{h,cpp}`, `juce/midi/MidiMapper.{h,cpp}` |
| `VideoCompanion` | Optional embedded WebSocket server for the external video sidecar (`companion/`). | `juce/video/VideoCompanion.{h,cpp}` |
| `ServerListFetcher` | Async HTTP GET against the public server list, parsed into `ServerListEntry`. | `src/net/server_list.{h,cpp}` |
| WDL utility library | Audio buffers, mutexes, queues, RNG, SHA, JNetLib, `vorbisencdec.h`, `flacencdec.h`. Static-linked into `njclient`. | `wdl/`, `wdl/jnetlib/` |

## Pattern Overview

**Overall:** Three-thread plugin architecture (host **audio** thread, dedicated **run/network** thread, JUCE **message** thread) with lock-free SPSC state-mirroring between threads. The legacy NINJAM client (forked from JamTaba/Cockos NINJAM) was a two-thread design with `WDL_Mutex m_users_cs` / `m_locchan_cs` taken from the audio thread; milestone 15.1 has been systematically replacing those locks with audio-thread-owned mirrors fed by SPSC update queues.

**Key Characteristics:**
- **Audio thread is lock-free.** `NJClient::AudioProc` (`src/core/njclient.cpp:1087`) does NOT take `m_users_cs`, `m_locchan_cs`, or `m_misc_cs` (15.1-06/07a). It drains update queues at the top of every callback, then operates exclusively on `m_locchan_mirror[MAX_LOCAL_CHANNELS]` and `m_remoteuser_mirror[MAX_PEERS]`.
- **Single-owner-at-a-time pointer ownership.** `DecodeState*` is constructed on the run thread, transferred to the audio thread via `PeerNextDsUpdate` (SPSC), and freed via the deferred-delete queue back on the run thread (Codex HIGH-3 generation gate, `m_audio_drain_generation`).
- **Run thread is the only NJClient-mutator thread** apart from APVTS atomics. UI never calls NJClient setters directly; it pushes `UiCommand` variants onto `cmd_queue`.
- **Plain JUCE `CriticalSection` (`clientLock`)** still guards run-thread vs message-thread access to `cachedUsers` and to `NJClient::Run` itself; the audio thread NEVER takes it.
- **NINJAM protocol is transmit-mute by default** (the current `bcast` flag must be set for upload). Local audio always renders to the monitoring mix; only flagged channels are encoded and sent.

## Layers

**Audio engine (real-time):**
- Purpose: Mix N input channels and remote streams into the JUCE bus layout (1 main stereo + 16 remote stereo + 1 metronome stereo = 17 stereo / 34 mono channels) with no allocations and no locks.
- Location: `juce/JamWideJuceProcessor.cpp::processBlock` → `NJClient::AudioProc` → `process_samples` / `mixInChannel` / `on_new_interval`
- Constraints: every buffer is preallocated in `prepareToPlay` (`tmpblock` via `NJClient::SetMaxAudioBlockSize`, `outputScratch`, `inputScratch`). `MAX_BLOCK_SAMPLES = 2048` is enforced via `prepareToPlay` throw and per-callsite `pushBlockRecord` bounds-check (`src/core/njclient.cpp:59`).
- Depends on: SPSC queues (`spsc_ring.h`), atomics in `NJClient` (`config_*`, `m_bpm`, `m_bpi`, `m_beatinfo_updated`, `m_interval_pos`, `cached_status`).
- Used by: JUCE host audio callback.

**Run / network layer:**
- Purpose: NINJAM protocol state machine, message send/receive, encoder feeding, decoder arming, peer/channel lifecycle, server-list HTTP, deferred-delete drains.
- Location: `juce/NinjamRunThread.cpp` (loop), `src/core/njclient.cpp` (`NJClient::Run`, `NJClient::Connect`, `NJClient::Disconnect`), `src/core/netmsg.cpp` (`Net_Connection::Run`).
- Cadence: 20 ms when connected, 50 ms when idle (`NinjamRunThread.h:21`).
- Depends on: WDL/JNetLib (`wdl/jnetlib/`), codec libraries (libvorbis, libFLAC), crypto (`nj_crypto`), `processor.cmd_queue`.
- Used by: UI (via `cmd_queue`), audio thread (via SPSC mirrors and atomics).

**Networking I/O layer:**
- Purpose: Async TCP + async DNS + HTTP GET. Plain TCP — no TLS. Per-message AES-256-CBC payload encryption added in milestone 15 sits **between** `Net_Message` and `JNL_Connection`.
- Location: `wdl/jnetlib/connection.{h,cpp}`, `wdl/jnetlib/asyncdns.{h,cpp}`, `wdl/jnetlib/httpget.{h,cpp}`. Encryption hook: `src/core/netmsg.cpp:129` (encrypt) / `src/core/netmsg.cpp:235` (decrypt).
- Depends on: BSD sockets / Winsock; OpenSSL or BCrypt for AES-256-CBC.

**UI / control layer:**
- Purpose: VB-style channel-strip mixer (Voicemeeter Banana inspired), dark theme via `JamWideLookAndFeel`. Persistent state on `JamWideJuceProcessor` survives editor destruction (per JUCE Pitfall: editor reconstructed on close/open).
- Location: `juce/JamWideJuceEditor.cpp`, `juce/ui/*.cpp`.
- Communication: drains `evt_queue` / `chat_queue` on a `juce::Timer`; reads `uiSnapshot` atomics for high-frequency BPM/VU/beat updates; writes `cmd_queue` on user input.

**Persistence layer:**
- APVTS (`juce::AudioProcessorValueTreeState`) — DAW-saved automation params (master vol/mute, metro vol/mute/pan, 4×local vol/pan/mute/solo, 16×remote vol/pan/mute/solo).
- Free-form ValueTree state — `oscEnabled`, `oscReceivePort`, `oscSendIP`, `oscSendPort`, `chatSidebarVisible`, `infoStripVisible`, `localTransmit[4]`, `localInputSelector[4]`, `lastServerAddress`, `lastUsername`, `scaleFactor`, `routingMode`. Saved/loaded via `getStateInformation` / `setStateInformation` (versioned: `currentStateVersion = 3` at `juce/JamWideJuceProcessor.h:88`).
- Recorded local audio: written by `WaveWriter` into the work directory set by `NJClient::SetWorkDir` (defaults to `${TMPDIR}/JamWide`, see `juce/JamWideJuceProcessor.cpp:41`).

**External integration layer:**
- OSC: `juce/osc/OscServer.{h,cpp}` — UDP control surface (TouchOSC layout in `assets/JamWide.tosc`).
- MIDI: `juce/midi/MidiMapper.{h,cpp}` + `MidiLearnManager` — CC mapping, MIDI Learn, MIDI feedback to physical surfaces.
- Video sidecar: `juce/video/VideoCompanion.{h,cpp}` runs an embedded WebSocket server (via `libs/ixwebsocket`) that the static web companion in `companion/` connects to; a separate browser window (Chrome/Safari/Edge detected via `juce/video/BrowserDetect_*`) hosts the TS app.

## Data Flow

### Local-audio uplink: mic → encode → encrypt → network

1. **Audio thread** (`processBlock`, `juce/JamWideJuceProcessor.cpp:486`)
   - JUCE buffer of `numSamples` samples per stereo input bus arrives. `collectInputChannels` deinterleaves into `inputScratch` (mono channels).
2. **`NJClient::AudioProc`** (`src/core/njclient.cpp:1087`)
   - `drainLocalChannelUpdates()` applies any UI-pushed `LocalChannelUpdate` variants (added/removed/info/monitoring) into `m_locchan_mirror[]`. `drainRemoteUserUpdates()` does the same for peers. Generation counter `m_audio_drain_generation` bumped (release-store).
   - Loop calls `process_samples` per interval segment.
3. **`NJClient::process_samples`** (`src/core/njclient.cpp:2562`)
   - Reads each active `LocalChannelMirror`. If `bcast` is set, applies the optional `cbf` processor (e.g. PTT mute lambda from `JamWideJuceProcessor`), pushes a `BlockRecord` via `pushBlockRecord(mirror.block_q, ...)` for the run-thread encoder.
   - Mixes monitor (vol/pan/mute/solo) into local-channel output bus.
   - Iterates `m_remoteuser_mirror[]` slots and calls `mixInChannel` (per-peer, per-channel; reads `RemoteUserChannelMirror.ds`, runs `runDecode`, decays VU, mixes into `outbuf[out_chan_index]`).
4. **Run thread** (`NinjamRunThread::run` → `NJClient::Run`, `NJClient::drainBroadcastBlocks`)
   - Drains each mirror's `block_q` (BlockRecord SPSC, capacity 16) and forwards into the legacy `lc->m_bq.AddBlock` path so the existing encoder consumer (NJClient::Run upload loop, lines ~1626-1840) is untouched.
   - `I_NJEncoder` (Vorbis or FLAC) consumes blocks and produces compressed bytes; bytes are split into NINJAM `MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN` / `WRITE` / `END` messages (see `src/core/mpb.h`).
5. **`Net_Connection::Run`** (`src/core/netmsg.cpp:100`)
   - For each queued `Net_Message`, if `m_encryption_active` and payload non-empty, calls `encrypt_payload` (AES-256-CBC, random per-message IV). Replaces queue slot with new encrypted message.
   - Writes header + payload to `JNL_Connection`. JNetLib does the actual TCP send.

### Remote-audio downlink: network → decrypt → decode → mix → output

1. **Run thread**: `Net_Connection::Run` receives bytes via `m_con->recv_bytes`. `Net_Message` framing reassembles. If `m_encryption_active` and payload non-empty, `decrypt_payload` runs in place; failure triggers generic `m_error = -5` (no padding-oracle leak).
2. **`NJClient::Run`** dispatches the message: `MESSAGE_DOWNLOAD_INTERVAL_BEGIN` / `WRITE` / `END` arrive, the run thread routes bytes into the per-channel `DecodeMediaBuffer` via `Write(...)` (lock-free SPSC chunks, `CHUNK_BYTES = 4096` in `spsc_payloads.h:222`).
3. **Run thread** prepares the **next** decoder when the audio thread finishes the current interval: constructs a `DecodeState` (codec selected by FOURCC: `'OGGv'` → `VorbisDecoder`, `'FLAC'` → `FlacDecoder` per `src/core/njclient.cpp:152-204`), publishes via `PeerNextDsUpdate{slot, channel, slot_idx, ds}` on `m_remoteuser_update_q`. Ownership transfers to audio thread.
4. **Audio thread** (`mixInChannel`, declared `src/core/njclient.h:633`): `runDecode` pulls bytes out of `DecodeMediaBuffer` (audio side, lock-free Read), feeds them into `DecodeState`'s codec, gets PCM, applies vol/pan/mute/solo, accumulates into `outbuf[out_chan_index]`. Old `DecodeState*` is enqueued on `m_deferred_delete_q` for the run thread to delete off-thread.
5. **`process_samples`** writes per-channel `peak_vol_l/r` (relaxed atomic float) — UI reads via `GetUserChannelPeak`.
6. **Audio thread back to JUCE**: `processBlock` calls `accumulateBusesToMainMix` and `routeOutputsToJuceBuses` to fan out 17 stereo buses; `measureMasterVu` updates `uiSnapshot.master_vu_*`.

### UI command path

UI input (e.g. fader) → `processorRef.cmd_queue.try_push(SetUserChannelStateCommand{...})` → run thread `processCommands` drains and calls `client->SetUserChannelState(...)` → that mutator pushes a `PeerVolPanUpdate` / `PeerChannelMaskUpdate` onto `m_remoteuser_update_q` → audio thread drain at next `AudioProc`.

### Connection lifecycle

1. UI: `ConnectCommand{server, username, password}` → `cmd_queue`.
2. Run thread: `NJClient::Connect(host, user, pass)` (`src/core/njclient.cpp:1324`) → constructs `JNL_Connection` and `Net_Connection`, attaches socket, writes auth state.
3. NINJAM handshake (`MESSAGE_SERVER_AUTH_CHALLENGE` → `MESSAGE_CLIENT_AUTH_USER` → `MESSAGE_SERVER_AUTH_REPLY`): server caps bit `SERVER_CAP_ENCRYPT_SUPPORTED` triggers `derive_encryption_key(password, challenge)` → `m_netcon->SetEncryptionKey(...)`; subsequent payload sends/receives are AES-256-CBC encrypted (`src/core/njclient.cpp:1577-1592`).
4. Server sends config: `BPM`, `BPI`, `keepalive` interval; published via `m_beatinfo_updated.store(1, release)`. License agreement (if present) routes through `LicenseAgreementCallback` → JUCE blocks the run thread on `license_cv` until the message-thread editor responds.
5. `Disconnect`: drops mirrors via `LocalChannelRemovedUpdate` / `PeerRemovedUpdate`, waits for `m_audio_drain_generation` to advance past publish moment, then runs canonical `~RemoteUser` / `~Local_Channel` off the audio thread (HIGH-3 generation gate, `src/core/njclient.cpp:1217-1290`).

### State Management

- **DAW-saved automation:** `apvts` (APVTS) — see `createParameterLayout` at `juce/JamWideJuceProcessor.cpp:73`.
- **DAW-saved free-form:** ValueTree branches in `getStateInformation` / `setStateInformation` (`currentStateVersion = 3`).
- **Cross-thread realtime:** SPSC rings (`m_locchan_update_q`, `m_remoteuser_update_q`, `m_locchan_processor_q`, `m_arm_request_q`, `m_locchan_deferred_delete_q`, `m_remoteuser_deferred_delete_q`, `m_deferred_delete_q`, `m_wave_block_q`, per-channel `block_q`, per-stream `DecodeMediaBuffer` chunk ring) + `std::atomic` config fields on NJClient and `UiAtomicSnapshot` on Processor.
- **Run-thread-private:** `m_remoteuser_slot_table`, `m_sessionmode_file_readers`, `lastStatus_` in `NinjamRunThread`.
- **Audio-thread-private:** `wasPlaying_`, `rawHostPlaying_`, `prevPpqPos_`, `prevSyncState_`, the mirror arrays (after drain).

## Key Abstractions

**Codec abstraction (`I_NJEncoder` / `I_NJDecoder`):**
- Purpose: Polymorphic compressed-audio encoder/decoder behind WDL's `VorbisEncoderInterface` / `VorbisDecoderInterface`. Two implementations.
- Examples: `wdl/vorbisencdec.h:91` (`VorbisDecoder`), `wdl/vorbisencdec.h:265` (`VorbisEncoder`), `wdl/flacencdec.h:34` (`FlacEncoder`), `wdl/flacencdec.h:158` (`FlacDecoder`).
- Pattern: Same interface, FOURCC-tagged on the wire (`OGGv`, `FLAC`). Selection via macro: `CreateNJEncoder(srate, ch, br, id)` / `CreateFLACEncoder(...)` / `CreateNJDecoder()` / `CreateFLACDecoder()` (`src/core/njclient.cpp:196-204`). Live codec swap mid-session is handled via `PeerCodecSwapUpdate` (`src/threading/spsc_payloads.h:103`).
- Note: per `CHANGELOG.md` and `FLAC_INTEGRATION_PLAN.md`, the FLAC encoder/decoder pipeline is in development; the production default is Vorbis. Run-thread-side codec selection is gated by `NJClient::SetEncoderFormat` (`src/core/njclient.h:335`).

**Mirror (audio-thread-owned snapshot):**
- Purpose: A by-value copy of run-thread-owned channel/peer state, kept on the audio thread so it never has to lock or dereference the canonical object. Updated by SPSC drain at the top of `AudioProc`.
- Examples: `LocalChannelMirror` (`src/core/njclient.h:155`), `RemoteUserChannelMirror` (`src/core/njclient.h:228`), `RemoteUserMirror` (`src/core/njclient.h:276`).
- Pattern: NO back-pointers (Codex HIGH-2). Identity is a stable slot index, not a list position. Generation gate (`m_audio_drain_generation`) ensures the audio thread has drained at least once before the canonical object is enqueued for deferred delete.

**SPSC ring (`jamwide::SpscRing<T, N>`):**
- Purpose: Lock-free single-producer / single-consumer transport with power-of-2 capacity, acquire/release semantics, fixed-capacity, non-resizable, non-copyable.
- Location: `src/threading/spsc_ring.h:33`.
- Pattern: Producer calls `try_push` (returns false if full → caller bumps an overflow counter); consumer calls `try_pop` / `drain`. Element type must be `std::is_trivially_copyable`. Every payload variant in `spsc_payloads.h` ends in a `static_assert` to lock that contract.

**Plugin host abstraction:**
- Purpose: Same `NJClient` core wraps two host shims.
- Active shim: `JamWideJuceProcessor` (JUCE → VST3, AU, Standalone, CLAP via `clap-juce-extensions`). All five plugin formats are produced by the single CMake target `JamWideJuce` (`CMakeLists.txt:145-234`).
- Inactive shim: `src/plugin/clap_entry.cpp` + `src/plugin/jamwide_plugin.h` + `src/ui/ui_*.cpp` (ImGui-based) — pre-JUCE prototype, NOT compiled by `CMakeLists.txt` today (verified by grepping the CMake file: no reference to `src/plugin/` or `src/ui/ui_main.cpp`). Per `memory-bank` note, the `JAMWIDE_BUILD_JUCE` toggle is **vestigial** since commit `0d82641` removed the parallel CLAP build; it now just gates the `JamWideJuce` target on/off for fast tests.

**State-machine peer model:**
- Purpose: Track NINJAM peer churn (join → channel mask publish → channel state updates → leave) without ever exposing run-thread-owned `RemoteUser*` to the audio thread.
- Pattern: Run thread owns `WDL_PtrList<RemoteUser> m_remoteusers` plus `m_remoteuser_slot_table[MAX_PEERS]` (slot allocator). State transitions emit `PeerAddedUpdate` / `PeerRemovedUpdate` / `PeerChannelMaskUpdate` / `PeerVolPanUpdate` / `PeerNextDsUpdate` / `PeerCodecSwapUpdate` variants. Audio thread sees only the mirror.

## Entry Points

**JUCE plugin entry:**
- Location: `juce/JamWideJuceProcessor.cpp:750` (`createPluginFilter`, JUCE-required factory).
- Triggers: VST3/AU/Standalone/CLAP host load.
- Responsibilities: instantiate `JamWideJuceProcessor` → constructs `NJClient`, OSC server, video companion, MIDI mapper.

**Audio callback entry:**
- Location: `juce/JamWideJuceProcessor.cpp:486` (`processBlock`).
- Triggers: Host audio thread, every block.
- Responsibilities: APVTS sync, input collection, transport sync, `NJClient::AudioProc`, output routing, master VU.

**Audio prepare entry:**
- Location: `juce/JamWideJuceProcessor.cpp:151` (`prepareToPlay`).
- Triggers: Host before audio starts, on sample-rate / block-size change.
- Responsibilities: presize `outputScratch`, call `client->SetMaxAudioBlockSize(samplesPerBlock)` (which throws if `> MAX_BLOCK_SAMPLES`), latch `prevPreparedSize`, start `NinjamRunThread`.

**Run-thread loop:**
- Location: `juce/NinjamRunThread.cpp` (`NinjamRunThread::run`).
- Triggers: Started in `prepareToPlay`, stopped in `releaseResources`.
- Responsibilities: drain `cmd_queue`, call `NJClient::Run` (network), drain deferred-delete queues, push events, update `uiSnapshot`, adaptive sleep.

**Editor entry:**
- Location: `juce/JamWideJuceEditor.cpp:10` (constructor).
- Triggers: Host opens UI window.
- Responsibilities: assemble UI, register callbacks that push `cmd_queue` entries, start `juce::Timer` to drain `evt_queue` / `chat_queue`.

**Vestigial CLAP entry (NOT compiled):**
- Location: `src/plugin/clap_entry.cpp:1` and `src/plugin/clap_entry_export.cpp:1`.
- Status: NOT in the CMake build graph (verified by grep). Kept for reference / potential reuse only. The active CLAP build path is `clap-juce-extensions` invoked at `CMakeLists.txt:230-234`.

## Architectural Constraints

- **Threading:** Three required threads (host audio + JUCE message + dedicated `NinjamRunThread`). Audio thread MUST NOT lock; MUST NOT allocate; MUST NOT log to file (15.1-03 H-01 / H-02 removed the last in-place audio logging path).
- **Audio-thread block bound:** `MAX_BLOCK_SAMPLES = 2048` (`spsc_payloads.h:196`). `prepareToPlay` throws `std::runtime_error` if the host claims a larger block. A debug `jassert` in `processBlock` (`juce/JamWideJuceProcessor.cpp:499`) catches per-callback violations.
- **Encryption envelope:** payload-only. Header (type + size) and zero-length payloads (e.g. keepalives) are NOT encrypted. AES-256-CBC + random per-message IV (per D-04, D-08); decryption returns generic error on padding failure (no oracle).
- **Pointer-ownership invariant:** every cross-thread pointer crosses ONLY via SPSC handoff (single-owner-at-a-time). No `Local_Channel*` / `RemoteUser*` / `RemoteUser_Channel*` is dereferenced from the audio thread (Codex HIGH-2). The deferred-delete generation gate (`m_audio_drain_generation`) closes the lifetime hole (Codex HIGH-3).
- **Global state:** `JamWideJuceProcessor::cachedUsers` is the single mutable structure shared between run and message threads; it is guarded by `cachedUsersMutex` (NOT `clientLock`) per the threading contract at `juce/JamWideJuceProcessor.h:23-54`.
- **No TLS on the audio TCP socket.** Confidentiality and credential-protection rely on the milestone-15 payload encryption, NOT on transport-layer crypto. Anyone observing the wire can still see message types, sizes, and the unencrypted handshake header.
- **Editor lifetime:** JUCE may destroy and recreate the editor at any time. Anything that must survive (chat history, server list, persisted toggles, license response state) lives on `JamWideJuceProcessor`, not on `JamWideJuceEditor`.
- **`JAMWIDE_BUILD_JUCE`** (`CMakeLists.txt:31`): vestigial toggle, defaults `ON`. Setting it `OFF` produces only the `njclient` static lib + tests (used by some CI/test paths). It does NOT switch to a different runtime UI today; the ImGui sources under `src/ui/` and `src/plugin/` are dead code.

## Anti-Patterns

### Holding a back-pointer in a mirror struct

**What happens:** Adding a `Local_Channel*` / `RemoteUser*` / `void*` "convenience" field on `LocalChannelMirror` or `RemoteUserChannelMirror` so the audio thread can "just call" a method on the canonical object.
**Why it's wrong:** Defeats the entire mirror model — the audio thread is back to dereferencing run-thread-owned memory whose lifetime it cannot reason about. Codex flagged this as HIGH-2 in 15.1-06 / 15.1-07a. The earlier draft of the plan had an `lc_ptr` and a `user_ptr`; both were removed before merge.
**Do this instead:** Hold by VALUE in the mirror; if the audio thread needs a callback (e.g. PTT mute), it must be a trivially-copyable function-pointer + opaque void context owned by the audio host (the JUCE processor), not a method on the canonical object — see `LocalChannelMirror::cbf` / `cbf_inst` (`src/core/njclient.h:172`).

### Indexing mirrors by list position

**What happens:** Using `m_remoteusers.GetIndex(user)` (or similar) as the mirror key. When `m_remoteusers.Delete(...)` shifts subsequent indices, every mirror entry past that point silently mis-maps.
**Why it's wrong:** "Bug A shape" identified in 15.1-MIRROR-AUDIT.md. The audio thread's view of peer N is now actually peer N+1.
**Do this instead:** Allocate a STABLE slot at peer-add time via `allocRemoteUserSlot` (`src/core/njclient.h:767`); release via the deferred-free protocol after the audio thread acknowledges the `PeerRemovedUpdate`. Mirror entries are never reindexed.

### Calling `start_decode` from the audio thread

**What happens:** Audio thread observes "DecodeState exhausted, need next" and constructs a fresh `DecodeState` inline.
**Why it's wrong:** `start_decode` allocates, opens files, parses codec headers — all forbidden on the audio thread.
**Do this instead:** Audio thread emits `DecodeArmRequest` on `m_arm_request_q`; run thread drains, calls `start_decode` off-thread, publishes the result via `PeerNextDsUpdate`. Wired in 15.1-09; today the audio-thread emitter is collapsed to a no-op early-return for sessionmode (see `src/core/njclient.h:787-806`).

### Re-normalizing channel indices on `_reinit`

**What happens:** `NJClient::_reinit` (called by `Disconnect`) shifts `Local_Channel::channel_idx` to compact the array.
**Why it's wrong:** The audio thread's `LocalChannelMirror[i]` is keyed by the original `channel_idx`. Renormalizing the canonical side breaks the mirror without publishing an update.
**Do this instead:** Per memory file `feedback_legacy_invariant_audit.md` — when introducing a shadow representation, grep ALL writes to the indexing field (not just the migrated functions). 15.1-06's `_reinit` renormalize and 15.1-07b's `on_new_interval` state machine were both "legacy invariants silently broken" — same shape, both bit us, cost 4 UAT cycles to find.

### Logging from the audio thread

**What happens:** A `fprintf` / `writeUserChanLog` / `printf` call inside `process_samples` or `mixInChannel`.
**Why it's wrong:** `fopen`/`fwrite` block on file I/O and may take a kernel lock; an underrun is guaranteed.
**Do this instead:** Use atomic counters (`m_block_queue_drops`, `m_deferred_delete_overflows`) read by the run thread or UI; for one-off events, push a payload onto an SPSC queue and log from the consumer side. 15.1-03 H-01/H-02 deleted the last two in-place audio-thread file writes.

## Error Handling

**Strategy:** Layered.
- **Network errors** (`Net_Connection::GetStatus()` returns nonzero) → `NJClient::Run` sets `m_status` to a 1000-series code (1000 = can't connect, 1001 = auth fail, 1002 = disconnected) → `cached_status` atomic published → `NinjamRunThread::handleStatusChange` pushes `StatusChangedEvent` to `evt_queue` → `JamWideJuceEditor::drainEvents` displays.
- **Encryption errors** → `Net_Connection` sets generic `m_error = -4` (encrypt fail) or `m_error = -5` (decrypt fail). Plaintext never partially exposed (`nj_crypto.h:48`).
- **Allocation / contract violations** in `prepareToPlay` (e.g. host claims `samplesPerBlock > MAX_BLOCK_SAMPLES`) → `std::runtime_error` thrown synchronously; JUCE logs and continues with previously-allocated buffers. Debug-build `jassert` catches the failure mode if it leaks past prepareToPlay.
- **SPSC overflow** → producer increments a relaxed `std::atomic<uint64_t>` drop counter (`m_deferred_delete_overflows`, `m_locchan_update_overflows`, `m_remoteuser_update_overflows`, `m_block_queue_drops`, `m_arm_request_drops`, `m_sessionmode_refill_drops`). Phase-close gate (15.1-10) asserts every counter == 0 post-UAT.

**Patterns:**
- Two-phase deferred delete with generation gate (HIGH-3) — never free run-thread-owned objects until the audio thread has acknowledged the `Removed*Update`.
- Generic crypto error codes — never reveal padding state (mitigates padding-oracle attacks).
- License agreement uses a manual `std::condition_variable` rendezvous between run thread (waiter) and message thread (responder).

## Cross-Cutting Concerns

**Logging:** `juce::Logger::writeToLog` from non-audio threads; `fprintf(stderr, ...)` from `NJClient::Connect` (`src/core/njclient.cpp:1327`). `JAMWIDE_DEV_BUILD` (compile-time, default `ON`) opens an extra `/tmp/jamwide.log` debug file from the run thread on auth events. NEVER from the audio thread (15.1-03 H-01/H-02 removed the last in-place audio path).

**Validation:** Bounds checks at every SPSC producer (`pushBlockRecord` at `src/core/njclient.cpp:59`, `MAX_BLOCK_SAMPLES` / `MAX_BLOCK_CHANNELS` clamp). Encryption rejects plaintext > `NJ_CRYPTO_MAX_PLAINTEXT` (16384) to bound DoS-via-allocation. Auth challenge length fixed at 8 bytes; protocol version range gated by `PROTO_VER_MIN` / `PROTO_VER_MAX`.

**Authentication:** SHA-1(SHA-1(user:pass) + 8-byte server challenge) for the legacy NINJAM auth response. Encryption key derived **separately** via SHA-256(password + challenge) (`derive_encryption_key` in `nj_crypto.cpp`). NO PBKDF2 / scrypt — explicit per D-03; password strength is the user's problem, not the protocol's.

**Threading discipline:** Documented in two places. (a) `juce/JamWideJuceProcessor.h:23-54` (the per-class threading contract). (b) Inline comments at every `std::atomic` / SPSC ring / mutex declaration in `src/core/njclient.h` citing the specific 15.1-NN sub-plan that wired it.

**Code signing:** macOS post-build `codesign` step inside `CMakeLists.txt:244-269`. Defaults to ad-hoc (`-`); release builds set `JAMWIDE_CODESIGN_IDENTITY` + `JAMWIDE_HARDENED_RUNTIME=ON` for notarization. TSan builds skip codesign (`JAMWIDE_TSAN` guard).

---

*Architecture analysis: 2026-04-30*
