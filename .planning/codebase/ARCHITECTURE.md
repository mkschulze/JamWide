# Architecture

**Analysis Date:** 2026-03-07

## Pattern Overview

**Overall:** Three-layer plugin architecture with command-based threading model and lock-free inter-thread communication.

**Key Characteristics:**
- Plugin format abstraction (CLAP/VST3/AU) via clap-wrapper
- Three independent execution threads: Audio, UI, and Run (network/NJClient)
- Lock-free SPSC (Single-Producer Single-Consumer) ring buffers for thread-safe message passing
- Direct NINJAM client core (NJClient) port from original Cockos codebase
- Platform-specific GUI abstraction (Metal on macOS, D3D11 on Windows) beneath Dear ImGui

## Layers

**Plugin Layer:**
- Purpose: CLAP plugin interface implementation, parameter handling, lifecycle management
- Location: `src/plugin/clap_entry.cpp`, `src/plugin/jamwide_plugin.h`
- Contains: Plugin descriptor, parameter callbacks, activation/deactivation, audio routing
- Depends on: Core layer, Threading layer, GUI abstractions
- Used by: Host DAW via CLAP API

**Audio/Processing Layer:**
- Purpose: Real-time audio I/O, sample processing, VU meter calculations
- Location: `src/plugin/clap_entry.cpp` (process handler), `src/core/njclient.cpp` (AudioProc)
- Contains: Audio callback handling, sample buffering, soft clipping on master
- Depends on: Core layer for decoded audio
- Used by: Plugin layer's process handler

**Core/Network Layer:**
- Purpose: NINJAM protocol implementation, audio encoding/decoding (OGG Vorbis), network communication
- Location: `src/core/` (njclient.cpp, netmsg.cpp, mpb.cpp, njmisc.cpp)
- Contains: NJClient state machine, remote user/channel management, Vorbis compression pipeline
- Depends on: WDL (jnetlib, SHA), libogg, libvorbis
- Used by: Run thread for network operations

**Run Thread Layer:**
- Purpose: Dedicated thread for NJClient::Run() loop, command processing, event generation
- Location: `src/threading/run_thread.cpp`, `src/threading/ui_command.h`, `src/threading/ui_event.h`
- Contains: Command queue consumer, NJClient lifecycle, network polling, event generation
- Depends on: Core layer (NJClient), Network layer (server list fetcher)
- Used by: Plugin thread for initialization/shutdown, command queue for UI requests

**UI/Rendering Layer:**
- Purpose: Dear ImGui-based user interface panels and state management
- Location: `src/ui/` (ui_main.cpp, ui_*.cpp/h)
- Contains: Status display, connection panel, chat, local/remote channel controls, server browser
- Depends on: UI state (ui_state.h), events from Run thread
- Used by: Platform GUI layer for rendering

**Platform GUI Layer:**
- Purpose: Window management, graphics API initialization, ImGui backend setup
- Location: `src/platform/gui_context.h`, `src/platform/gui_macos.mm`, `src/platform/gui_win32.cpp`
- Contains: Platform-specific window creation, Metal (macOS) or D3D11 (Windows) rendering, event handling
- Depends on: imgui (core and backends), GUI context interface
- Used by: Plugin layer for GUI lifecycle

**Logging/Debug Layer:**
- Purpose: Development-time logging for diagnostics
- Location: `src/debug/logging.h`
- Contains: File-based logging macros (NLOG, NLOG_VERBOSE), conditional compilation
- Depends on: None
- Used by: All layers in development builds

## Data Flow

**Connection/Disconnection Flow:**

1. **User Action:** UI thread renders connection panel, user enters server/username/password
2. **Command Dispatch:** UI thread pushes `ConnectCommand` to `cmd_queue` (lock-free SPSC)
3. **Run Thread Processing:** Run thread pops command from queue, calls `NJClient::Connect()`
4. **Network I/O:** NJClient manages TCP connection, authentication, capability negotiation
5. **Status Event:** NJClient state change triggers `StatusChangedEvent` pushed to `ui_queue`
6. **UI Update:** UI thread drains event queue next frame, updates `ui_state.status`, re-renders panels

**Audio Processing Flow:**

1. **Audio Thread:** Host calls `plugin_process()` with buffer pointers
2. **Parameter Sync:** Read atomic parameter values (master volume, metro volume, mute flags)
3. **Input Buffering:** Consume input samples, queue for encoding to NJClient local channel
4. **Decoding:** NJClient provides decoded remote audio (Vorbis decompressed to PCM)
5. **Mixing:** Combine local input monitor, remote channels, metronome click
6. **Metering:** Calculate RMS for VU display, push to atomic snapshot
7. **Output:** Write mixed audio to output buffer with soft clipping
8. **No Locking:** Audio thread never acquires mutexes; uses only atomics and memory barriers

**Chat/Text Flow:**

1. **User Input:** UI thread captures chat text in ImGui input field
2. **Command:** `SendChatCommand` pushed to `cmd_queue` when user presses Enter
3. **Run Thread:** Processes command, formats message, sends via NJClient
4. **Server Echo:** Server broadcasts chat to all users
5. **Receive:** NJClient's Run() receives chat, parses message, pushes `ChatMessageEvent`
6. **UI Display:** Event queue drained, message appended to `ui_state.chat_history[]`

**Parameter/Control Flow:**

1. **UI Command:** User adjusts slider (volume, pan, mute) in UI panel
2. **Command Queue:** `SetLocalChannelInfoCommand` or `SetUserChannelStateCommand` queued
3. **Run Thread:** Applies via NJClient remote channel state mutations (protected by `client_mutex`)
4. **Audio Thread:** Reads atomic snapshots (bpm, bpi, beat position) without locks
5. **Cached Values:** Master/metro parameters stored in plugin's atomics for audio thread access

**State Management:**

- **Mutable State (Protected by `state_mutex`):** UI input fields, UI state, non-NJClient metadata
- **NJClient State (Protected by `client_mutex`):** Remote user/channel lists, local channel transmit/bitrate, connection settings (except Run thread AudioProc)
- **Audio-Accessible State (Atomic/Lock-Free):** Master/metro volumes and mutes, VU levels, BPM/BPI/beat phase, transient detection

## Key Abstractions

**GuiContext:**
- Purpose: Abstract platform-specific window and graphics management
- Examples: `src/platform/gui_context.h`, `src/platform/gui_macos.mm`, `src/platform/gui_win32.cpp`
- Pattern: Virtual interface with platform-specific concrete implementations; factory pattern for creation

**SpscRing<T, N>:**
- Purpose: Lock-free queue for cross-thread communication
- Examples: `src/threading/spsc_ring.h` (template header)
- Pattern: Template-based circular buffer with atomic head/tail indices; fast memory barriers; no malloc in critical path

**UiCommand Variant:**
- Purpose: Type-safe command encoding from UI to Run thread
- Examples: `src/threading/ui_command.h` (std::variant with 8 command types)
- Pattern: Discriminated union allowing type-safe dispatch via std::visit

**UiEvent Variant:**
- Purpose: Type-safe event encoding from Run thread to UI
- Examples: `src/threading/ui_event.h` (std::variant with 5 event types)
- Pattern: Matches UiCommand pattern; allows UI to respond to network-driven state changes

**NJClient State Machine:**
- Purpose: Core protocol client with built-in state and message parsing
- Examples: `src/core/njclient.h`, `src/core/njclient.cpp` (~2500 lines, original Cockos code)
- Pattern: Monolithic class with public API (Run(), AudioProc(), SetXxx methods); encapsulates NINJAM protocol details

**UiState Snapshot:**
- Purpose: Single unified state object for UI rendering
- Examples: `src/ui/ui_state.h` (struct with ~90 members)
- Pattern: Flat struct with public members; protected by `state_mutex` for mutations; read during render frame
- Includes: Connection settings, status, remote user list, chat history, VU levels, tempo data

## Entry Points

**Plugin Instance Creation:**
- Location: `src/plugin/clap_entry.cpp` (`plugin_create`)
- Triggers: Host instantiates plugin
- Responsibilities: Allocate JamWidePlugin, initialize mutexes, create NJClient, set up GUI context, start Run thread

**Audio Processing:**
- Location: `src/plugin/clap_entry.cpp` (`plugin_process`)
- Triggers: Host's audio thread calls process callback every buffer
- Responsibilities: Read parameters, consume input samples, route to NJClient, pull mixed output, apply soft clipping, write output buffer

**Run Thread Loop:**
- Location: `src/threading/run_thread.cpp` (thread_main in lambda)
- Triggers: Plugin activation
- Responsibilities: Loop `NJClient::Run()` (~50ms intervals), drain command queue, generate events, push to UI queue

**UI Render:**
- Location: `src/ui/ui_main.cpp` (`ui_render_frame`)
- Triggers: Platform GUI layer (platform-specific event loop)
- Responsibilities: Drain event queue, update UiState, call panel render functions, process ImGui input

**Platform GUI Event:**
- Location: `src/platform/gui_macos.mm` or `src/platform/gui_win32.cpp` (event loop / timer callback)
- Triggers: OS event loop or host timer
- Responsibilities: Call ImGui IO, handle native input events, call ui_render_frame, submit ImGui draw data to graphics API

## Error Handling

**Strategy:** Mixed approach with logging, status codes, and error messages.

**Patterns:**
- Network Errors: Propagate through NJClient status codes (NJC_STATUS_*), converted to `StatusChangedEvent` with error message text
- Parameter/State Errors: Logged via NLOG, operation ignored or defaulted
- Audio Thread Errors: Soft clipping prevents distortion; no error reporting (must not block audio thread)
- Thread Lifecycle: Run thread exceptions caught at top level, logged, thread exits gracefully
- License Dialog: Blocking wait with timeout (10 second) using condition variable + atomic; user can accept/reject

**Logging Approach:**
- `NLOG()`: Always-available logging macro; writes to file (/tmp/jamwide.log or ~/Library/Logs/jamwide.log)
- `NLOG_VERBOSE()`: Dev-build only; conditionally compiled out in production
- Session markers: Log file marked with === JamWide Session Started === on first log write

## Cross-Cutting Concerns

**Logging:** File-based via `src/debug/logging.h`; enabled in dev builds via `JAMWIDE_DEV_BUILD` CMake option; outputs to platform-standard log locations

**Validation:** Minimal in hot paths; parameter values range-checked at boundaries (audio thread doesn't validate, UI thread pre-validates before queueing commands)

**Authentication:** Handled by NJClient (username/password sent on Connect command, SHA hashing via WDL)

**Thread Safety:** Three mutexes: `state_mutex` (UI state), `client_mutex` (NJClient API calls except AudioProc), `license_mutex` (license dialog sync); SPSC queues are lock-free

**Serialization:** CMake option `serialize_audio_proc` for diagnostic serialization of audio thread calls under `client_mutex` (default: false for performance)

---

*Architecture analysis: 2026-03-07*
