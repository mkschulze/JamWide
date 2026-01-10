# CLAP NINJAM Client - Functional Design Document

## 1. Project Overview

### 1.1 Purpose
Create a cross-platform CLAP audio plugin that enables musicians to collaborate in real-time over the internet using the NINJAM protocol. The plugin runs inside any CLAP-compatible DAW (Digital Audio Workstation).

### 1.2 Goals
- Port the proven NJClient core from ReaNINJAM to a standalone CLAP plugin
- Support Windows (MSVC/Clang) and macOS (Xcode/Clang)
- Provide a simple, functional UI using Dear ImGui
- Maintain full compatibility with existing NINJAM servers

### 1.3 Non-Goals (MVP)
- Chat functionality
- Multiple local channels (MVP: single stereo)
- Multi-channel I/O beyond stereo
- Session mode / Voice chat mode
- Recording to disk
- Server list fetching

---

## 2. Feature Requirements

### 2.1 Core Features (Must Have)

| ID | Feature | Description |
|----|---------|-------------|
| F01 | Server Connection | Connect to NINJAM servers via hostname:port |
| F02 | Authentication | Username/password authentication (password in-memory only) |
| F03 | License Acceptance | Display and accept server license before joining |
| F04 | Audio I/O | Stereo input/output (2-in, 2-out) |
| F05 | Vorbis Encoding | Encode local audio to OGG Vorbis for transmission |
| F06 | Vorbis Decoding | Decode received audio from remote users |
| F07 | BPM/BPI Sync | Synchronize to server tempo and interval |
| F08 | Local Channel | Single stereo channel with name, transmit toggle, bitrate, volume, pan, mute/solo, VU |
| F09 | Remote Channels | Subscribe, volume, pan, mute/solo, VU per remote user channel |
| F10 | Metronome | Built-in click with volume and mute |
| F11 | Master Output | Master volume and mute |
| F12 | State Persistence | Save/restore settings via CLAP state (no password) |
| F13 | CLAP Parameters | Expose master/metronome volume/mute as automatable params |

### 2.2 UX Features (Should Have)

| ID | Feature | Description |
|----|---------|-------------|
| F14 | VU Metering | Master and per-channel level display |
| F15 | Beat Counter | Visual interval position indicator |
| F16 | Connection Status | Display connection state, errors |
| F17 | Password Memory | Remember password in-memory while connected; cleared on disconnect |

### 2.3 Future Features (Out of Scope)

| ID | Feature | Description |
|----|---------|-------------|
| F18 | Chat | Text messaging (excluded) |
| F19 | Multiple Local Channels | More than one stereo input |
| F20 | Multi-channel I/O | Beyond 2-in/2-out |
| F21 | Session Mode | Project-position sync |
| F22 | Voice Chat Mode | Low-latency transmission |
| F23 | Recording | Save session to disk |
| F24 | Server List | Fetch public servers from ninjam.com |

---

## 3. Architecture Overview

### 3.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         CLAP Host (DAW)                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    CLAP NINJAM Plugin                     │  │
│  ├───────────────────────────────────────────────────────────┤  │
│  │                                                           │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐   │  │
│  │  │ CLAP Wrapper│  │  Dear ImGui │  │   NJClient      │   │  │
│  │  │  - Entry    │  │     UI      │  │   (Core)        │   │  │
│  │  │  - Plugin   │  │             │  │                 │   │  │
│  │  │  - Audio    │  │  ┌───────┐  │  │  ┌───────────┐  │   │  │
│  │  │  - Params   │  │  │Status │  │  │  │ Networking│  │   │  │
│  │  │  - State    │  │  │Connect│  │  │  │ (jnetlib) │  │   │  │
│  │  │  - GUI      │  │  │Local  │  │  │  ├───────────┤  │   │  │
│  │  │             │  │  │Remote │  │  │  │ Vorbis    │  │   │  │
│  │  │             │  │  │License│  │  │  │ Enc/Dec   │  │   │  │
│  │  └──────┬──────┘  └────┬─────┘  │  └──────┬──────┘  │   │  │
│  │         │              │        │         │         │   │  │
│  │         └──────────────┴────────┴─────────┘         │   │  │
│  │                        │                            │   │  │
│  │              ┌─────────┴─────────┐                  │   │  │
│  │              │  Plugin Instance  │                  │   │  │
│  │              │  (JamWidePlugin)   │                  │   │  │
│  │              └───────────────────┘                  │   │  │
│  │                                                     │   │  │
│  └─────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 Layer Responsibilities

| Layer | Responsibility |
|-------|----------------|
| **CLAP Wrapper** | Plugin lifecycle, audio ports, parameters, state, GUI hosting |
| **Dear ImGui UI** | Connection dialog, channel panels, meters, license dialog |
| **NJClient Core** | NINJAM protocol, audio processing, encoding/decoding |
| **WDL Libraries** | Networking (jnetlib), Vorbis codec wrapper, utilities |

---

## 4. Component Specifications

### 4.1 CLAP Wrapper (`src/clap_*.cpp`)

#### 4.1.1 Entry Point (`clap_entry.cpp`)
- Export `clap_entry` with init/deinit
- Provide `clap_plugin_factory` with single plugin descriptor
- Plugin ID: `com.ninjam.clap-client`

#### 4.1.2 Plugin Implementation (`clap_plugin.cpp`)
- Create/destroy `JamWidePlugin` instance
- Implement CLAP plugin interface:
  - `init()` / `destroy()`
  - `activate()` / `deactivate()`
  - `start_processing()` / `stop_processing()`
  - `process()`
  - `get_extension()`

#### 4.1.3 Audio Ports (`clap_audio.cpp`)
- Declare fixed stereo ports:
  - Input: "Audio In" (2 channels, main)
  - Output: "Audio Out" (2 channels, main)

#### 4.1.4 Parameters (`clap_params.cpp`)

| ID | Name | Range | Default | Flags |
|----|------|-------|---------|-------|
| 0 | Master Volume | 0.0 - 2.0 | 1.0 | Automatable |
| 1 | Master Mute | 0/1 | 0 | Automatable, Stepped |
| 2 | Metronome Volume | 0.0 - 2.0 | 0.5 | Automatable |
| 3 | Metronome Mute | 0/1 | 0 | Automatable, Stepped |

#### 4.1.5 State (`clap_state.cpp`)
- Serialize to JSON:
  ```json
  {
    "version": 1,
    "server": "hostname:port",
    "username": "name",
    "master": { "volume": 1.0, "mute": false },
    "metronome": { "volume": 0.5, "mute": false },
    "localChannel": {
      "name": "Channel",
      "transmit": true,
      "bitrate": 64
    }
  }
  ```
- Password NOT saved (security)
- Version field for future migration

#### 4.1.6 GUI (`clap_gui.cpp`)
- Implement `clap_plugin_gui` extension
- Platform backends:
  - Windows: D3D11 + ImGui
  - macOS: Metal + ImGui
- Handle `create()`, `destroy()`, `set_parent()`, `set_size()`, `show()`, `hide()`

### 4.2 UI Components (`src/ui/`)

#### 4.2.1 Main Layout (`ui_main.cpp`)
```
┌─────────────────────────────────────────────────────────────┐
│ ● Connected to ninjam.com:2049 │ 120 BPM │ 16 BPI │ [==  ] │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─ Connection ──────────────────────────────────────────┐  │
│  │ Server: [ninjam.com:2049    ] Username: [player    ]  │  │
│  │ Password: [••••••           ] [Connect] [Disconnect]  │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─ Local Channel ───────────────────────────────────────┐  │
│  │ Name: [Guitar    ] Bitrate: [64 kbps ▼]  [x] Transmit │  │
│  │ Volume: [========|==] Pan: [====|====] [M] [S] [VU  ] │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─ Master ──────────────────────────────────────────────┐  │
│  │ Volume: [========|==] [M]  Metro: [====|==] [M] [VU ] │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─ Remote Users ────────────────────────────────────────┐  │
│  │ ▼ user1@192.168.1.1                              [M]  │  │
│  │   └─ Guitar   [x] Vol: [======|=] Pan: [==|==] [M][S] │  │
│  │   └─ Vocals   [x] Vol: [======|=] Pan: [==|==] [M][S] │  │
│  │ ▼ user2@192.168.1.2                              [M]  │  │
│  │   └─ Bass     [x] Vol: [======|=] Pan: [==|==] [M][S] │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

#### 4.2.2 Status Bar (`ui_status.cpp`)
- Connection indicator (colored dot)
- Server name/address
- Current BPM and BPI
- Beat counter (visual progress through interval)

#### 4.2.3 Connection Panel (`ui_connection.cpp`)
- Server address input
- Username input
- Password input (masked, in-memory only)
- Connect/Disconnect buttons
- Error message display

#### 4.2.4 Local Channel Panel (`ui_local.cpp`)
- Channel name input
- Bitrate dropdown (32, 64, 96, 128, 192, 256 kbps)
- Transmit toggle checkbox
- Volume slider (0-200%, default 100%)
- Pan slider (-100% to +100%, default center)
- Mute/Solo buttons
- VU meter

#### 4.2.5 Remote Channels Panel (`ui_remote.cpp`)
- Collapsible user list
- Per-user mute button
- Per-channel:
  - Subscribe toggle
  - Volume slider
  - Pan slider
  - Mute/Solo buttons
  - VU meter

#### 4.2.6 License Dialog (`ui_license.cpp`)
- Modal popup
- Scrollable license text
- Accept/Reject buttons
- 60-second timeout

### 4.3 NJClient Core (`src/core/`)

#### 4.3.1 Modified Files (from ninjam/)
- `njclient.h` / `njclient.cpp` - Core client logic
- `netmsg.h` / `netmsg.cpp` - Network message framing
- `mpb.h` / `mpb.cpp` - Protocol message builders
- `njmisc.h` / `njmisc.cpp` - Utility functions

#### 4.3.2 Modifications Required

| File | Change | Reason |
|------|--------|--------|
| `njclient.h` | Add `std::atomic<T>` to MVP UI-touched `config_*` fields (master/metronome) | Thread-safe audio reads |
| `njclient.h` | Add `cached_status` atomic | Lock-free status check |
| `njclient.cpp` | Update `cached_status` in `Connect()`, `Disconnect()`, `Run()` | Status sync |
| `njclient.cpp` | Remove REAPER Vorbis callback indirection | Direct libvorbis calls |

### 4.4 WDL Dependencies (`wdl/`)

| Component | Files | Purpose |
|-----------|-------|---------|
| jnetlib | `jnetlib/*.cpp/h` | TCP/IP networking |
| vorbisencdec | `vorbisencdec.h` | Vorbis codec wrapper |
| Containers | `ptrlist.h`, `queue.h`, `heapbuf.h` | Data structures |
| Strings | `wdlstring.h` | String handling |
| Threading | `mutex.h` | Mutex abstraction |
| Crypto | `sha.h`, `rng.h` | Authentication |
| Audio | `pcmfmtcvt.h` | Sample conversion |

### 4.5 External Libraries

| Library | Version | License | Purpose |
|---------|---------|---------|---------|
| CLAP SDK | 1.2+ | MIT | Plugin API |
| Dear ImGui | 1.90+ | MIT | UI framework |
| libogg | 1.3.5+ | BSD | Ogg container |
| libvorbis | 1.3.7+ | BSD | Vorbis codec |

---

## 5. Data Flow

### 5.1 Audio Signal Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                         Audio Thread                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   DAW Input ──►┌─────────────┐                                  │
│   (Stereo)     │  AudioProc  │──► Vorbis Encoder ──► Network    │
│                │             │                                  │
│   Network ──►  │  (NJClient) │──► DAW Output                    │
│   Vorbis Dec   │             │   (Stereo)                       │
│                └─────────────┘                                  │
│                      │                                          │
│                      ▼                                          │
│                ┌───────────┐                                    │
│                │ Metronome │──► Mixed to Output                 │
│                └───────────┘                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 5.2 Network Data Flow

```
┌──────────────────────────────────────────────────────────────────┐
│                         Run Thread                               │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   ┌─────────┐     ┌─────────┐     ┌─────────────────────────┐   │
│   │ jnetlib │◄───►│ NJClient│◄───►│ Protocol Messages (mpb) │   │
│   │  TCP    │     │  Run()  │     │ - Auth, KeepAlive       │   │
│   └─────────┘     └─────────┘     │ - Audio Upload/Download │   │
│        │               │          │ - User/Channel Info     │   │
│        ▼               ▼          └─────────────────────────┘   │
│   NINJAM Server   Callbacks ──► UI Event Queue                   │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 5.3 State Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                         State Flow                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   UI Thread                 Atomic Config                       │
│   ─────────                 ─────────────                       │
│   User adjusts    ──►    std::atomic<float>    ◄──  Audio Thread│
│   slider/button           config_mastervolume       reads during│
│                           config_metronome          AudioProc() │
│                                                                 │
│   UI Thread                 client_mutex                        │
│   ─────────                 ───────────                         │
│   UI sends UiCommand  ──►  Run thread NJClient API  ◄──  Events │
│   (connect, chan)          (serialized access)          + UI     │
│                                                                 │
│   UI Thread                 client_mutex                        │
│   ─────────                 ───────────                         │
│   UI reads NJClient   ──►  lock + getters         ◄──  Run Thread│
│   Remote users/fields      (ReaNINJAM-style)                     │
│                                                                 │
│   DAW                      CLAP State                           │
│   ───                      ──────────                           │
│   Save project    ──►    clap_state.save()                      │
│   Load project    ◄──    clap_state.load()                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 6. User Workflows

### 6.1 Connect to Server

```
1. User opens plugin UI
2. User enters server address (e.g., "ninjam.com:2049")
3. User enters username
4. User enters password (optional, not saved)
5. User clicks "Connect"
6. Plugin initiates connection (Run thread)
7. If license required:
   a. License dialog appears (modal)
   b. User reads and clicks "Accept" or "Reject"
8. On success: Status shows "Connected", remote users appear
9. On failure: Error message displayed
```

### 6.2 Jam Session

```
1. User adjusts local channel settings (name, bitrate)
2. User enables "Transmit" to broadcast audio
3. Audio from DAW input is encoded and sent each interval
4. Remote users' audio is received and mixed to output
5. Metronome clicks on each beat
6. User adjusts remote channel volumes as needed
7. Session continues until disconnect
```

### 6.3 Disconnect

```
1. User clicks "Disconnect"
2. Plugin sends disconnect to server
3. Remote users cleared from UI
4. Status shows "Disconnected"
5. Password cleared on disconnect; user must re-enter to reconnect
```

### 6.4 Save/Load Project

```
Save:
1. DAW triggers clap_state.save()
2. Plugin serializes settings to JSON (no password)
3. State stored in DAW project file

Load:
1. DAW triggers clap_state.load()
2. Plugin deserializes JSON
3. Settings restored (server, username, volumes, etc.)
4. User must re-enter password and reconnect
```

---

## 7. Error Handling

### 7.1 Connection Errors

| Error | User Message | Recovery |
|-------|--------------|----------|
| DNS failure | "Cannot resolve server address" | User corrects address |
| Connection refused | "Cannot connect to server" | User tries again later |
| Invalid auth | "Invalid username or password" | User corrects credentials |
| Server full | "Server is full" | User tries another server |
| License rejected | "License not accepted" | User must accept to join |
| Timeout | "Connection timed out" | User tries again |

### 7.2 Runtime Errors

| Error | Behavior |
|-------|----------|
| Network disconnect | Show "Disconnected" (manual reconnect) |
| Audio underrun | Continue processing, may cause gaps |
| Vorbis decode error | Skip corrupted audio, continue |

---

## 8. Platform Considerations

### 8.1 Windows
- Build with MSVC 2019+ or Clang
- D3D11 for ImGui rendering
- Link libogg/libvorbis statically
- Output: `ninjam.clap` (DLL with .clap extension)

### 8.2 macOS
- Build with Xcode 12+ / Clang
- Metal for ImGui rendering
- Link libogg/libvorbis statically
- Output: `ninjam.clap` (bundle)
- Minimum: macOS 10.15 (Catalina)

### 8.3 Code Signing
- Windows: Optional Authenticode signing
- macOS: Required notarization for distribution

---

## 9. Testing Strategy

### 9.1 Unit Tests
- SPSC ring buffer correctness
- JSON state serialization/deserialization
- Parameter value mapping

### 9.2 Integration Tests
- clap-validator tool (CLAP compliance)
- State roundtrip (save → load → verify)
- Connect/disconnect cycles

### 9.3 Manual Tests
- DAW smoke tests: REAPER, Bitwig, FL Studio (Windows); REAPER, Bitwig, Logic (macOS)
- Multi-user jam session with public server
- Transport start/stop behavior
- License dialog interaction

---

## 10. Glossary

| Term | Definition |
|------|------------|
| BPM | Beats Per Minute - tempo of the session |
| BPI | Beats Per Interval - number of beats in one loop cycle |
| Interval | One complete loop cycle; audio is exchanged at interval boundaries |
| CLAP | CLever Audio Plugin - modern plugin API |
| NJClient | NINJAM client library handling protocol and audio |
| Vorbis | Lossy audio codec used for transmission |
| jnetlib | WDL networking library |
| WDL | Cockos library collection (used by REAPER) |

---

## 11. References

- [CLAP Specification](https://github.com/free-audio/clap)
- [NINJAM Protocol](https://www.cockos.com/ninjam/)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [WDL Library](https://github.com/justinfrankel/WDL)
- [ReaNINJAM Source](jmde/fx/reaninjam/)
