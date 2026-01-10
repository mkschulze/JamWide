# CLAP NINJAM Client - Technical Design Document (Part 1 of 3)

## Part 1: Core Architecture & Threading

---

## 1. Project Structure

```
JamWide/
├── CMakeLists.txt                    # Root build configuration
├── cmake/
│   └── ClapPlugin.cmake              # CLAP bundle helpers
├── libs/
│   ├── clap/                         # CLAP SDK (git submodule)
│   ├── clap-helpers/                 # CLAP C++ helpers (git submodule)
│   ├── imgui/                        # Dear ImGui (git submodule)
│   ├── libogg/                       # Ogg container (git submodule)
│   └── libvorbis/                    # Vorbis codec (git submodule)
├── src/
│   ├── plugin/
│   │   ├── jamwide_plugin.h           # Plugin instance struct
│   │   ├── jamwide_plugin.cpp         # Plugin implementation
│   │   ├── clap_entry.cpp            # clap_plugin_entry export
│   │   ├── clap_audio.cpp            # Audio ports extension
│   │   ├── clap_params.cpp           # Parameters extension
│   │   ├── clap_state.cpp            # State extension
│   │   └── clap_gui.cpp              # GUI extension
│   ├── core/
│   │   ├── njclient.h                # Modified NJClient header
│   │   ├── njclient.cpp              # Modified NJClient implementation
│   │   ├── netmsg.h                  # Network message framing
│   │   ├── netmsg.cpp
│   │   ├── mpb.h                     # Protocol message builders
│   │   ├── mpb.cpp
│   │   ├── njmisc.h                  # Utility functions
│   │   └── njmisc.cpp
│   ├── threading/
│   │   ├── spsc_ring.h               # Lock-free SPSC queue
│   │   ├── run_thread.h              # Network thread wrapper
│   │   └── run_thread.cpp
│   ├── ui/
│   │   ├── ui_state.h                # UI state struct
│   │   ├── ui_main.cpp               # Main UI layout
│   │   ├── ui_status.cpp             # Status bar
│   │   ├── ui_connection.cpp         # Connection panel
│   │   ├── ui_local.cpp              # Local channel panel
│   │   ├── ui_remote.cpp             # Remote channels panel
│   │   ├── ui_license.cpp            # License dialog
│   │   └── ui_meters.cpp             # VU meter widgets
│   ├── platform/
│   │   ├── gui_context.h             # Abstract GUI context
│   │   ├── gui_win32.cpp             # Win32 + D3D11
│   │   └── gui_macos.mm              # Cocoa + Metal
│   └── third_party/
│       └── picojson.h                # JSON parser (single header)
├── wdl/
│   ├── jnetlib/
│   │   ├── asyncdns.cpp
│   │   ├── asyncdns.h
│   │   ├── connection.cpp
│   │   ├── connection.h
│   │   ├── httpget.cpp
│   │   ├── httpget.h
│   │   ├── jnetlib.h
│   │   ├── listen.cpp
│   │   ├── listen.h
│   │   ├── netinc.h
│   │   └── util.cpp
│   ├── vorbisencdec.h
│   ├── ptrlist.h
│   ├── queue.h
│   ├── heapbuf.h
│   ├── wdlstring.h
│   ├── wdltypes.h
│   ├── mutex.h
│   ├── sha.cpp
│   ├── sha.h
│   ├── rng.cpp
│   ├── rng.h
│   ├── pcmfmtcvt.h
│   └── wavwrite.h
└── resources/
    ├── logo.png
    └── Info.plist.in                 # macOS bundle template
```

---

## 2. Build System (CMake)

### 2.1 Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(JamWide VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    option(JAMWIDE_UNIVERSAL "Build universal binary (arm64 + x86_64)" ON)
    if(JAMWIDE_UNIVERSAL)
        if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "")
            set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
        endif()
    endif()
endif()

# Options
option(JAMWIDE_BUILD_TESTS "Build tests" OFF)

# Submodules
add_subdirectory(libs/clap EXCLUDE_FROM_ALL)
add_subdirectory(libs/clap-helpers EXCLUDE_FROM_ALL)
add_subdirectory(libs/libogg EXCLUDE_FROM_ALL)
add_subdirectory(libs/libvorbis EXCLUDE_FROM_ALL)

# Dear ImGui (manual setup - no CMakeLists in repo)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})

# Platform-specific ImGui backends
if(WIN32)
    target_sources(imgui PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR}/backends)
elseif(APPLE)
    target_sources(imgui PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_osx.mm
        ${IMGUI_DIR}/backends/imgui_impl_metal.mm
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR}/backends)
    target_link_libraries(imgui PUBLIC
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework GameController"
    )
endif()

# WDL library (static)
add_library(wdl STATIC
    wdl/jnetlib/asyncdns.cpp
    wdl/jnetlib/connection.cpp
    wdl/jnetlib/httpget.cpp
    wdl/jnetlib/listen.cpp
    wdl/jnetlib/util.cpp
    wdl/sha.cpp
    wdl/rng.cpp
)
target_include_directories(wdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/wdl)
if(WIN32)
    target_link_libraries(wdl PUBLIC ws2_32)
endif()

# NJClient core (static)
add_library(njclient STATIC
    src/core/njclient.cpp
    src/core/netmsg.cpp
    src/core/mpb.cpp
    src/core/njmisc.cpp
)
target_include_directories(njclient PUBLIC src/core)
target_link_libraries(njclient PUBLIC wdl vorbis vorbisenc ogg)

# Main plugin
add_library(JamWide MODULE
    src/plugin/jamwide_plugin.cpp
    src/plugin/clap_entry.cpp
    src/plugin/clap_audio.cpp
    src/plugin/clap_params.cpp
    src/plugin/clap_state.cpp
    src/plugin/clap_gui.cpp
    src/threading/run_thread.cpp
    src/ui/ui_main.cpp
    src/ui/ui_status.cpp
    src/ui/ui_connection.cpp
    src/ui/ui_local.cpp
    src/ui/ui_remote.cpp
    src/ui/ui_license.cpp
    src/ui/ui_meters.cpp
)
target_include_directories(JamWide PRIVATE
    src
    src/third_party
)

# Platform-specific GUI sources
if(WIN32)
    target_sources(JamWide PRIVATE src/platform/gui_win32.cpp)
    target_link_libraries(JamWide PRIVATE d3d11 dxgi)
elseif(APPLE)
    target_sources(JamWide PRIVATE src/platform/gui_macos.mm)
endif()

target_link_libraries(JamWide PRIVATE
    clap
    clap-helpers
    njclient
    imgui
)

# CLAP bundle setup
set_target_properties(JamWide PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION clap
    PREFIX ""
    OUTPUT_NAME "NINJAM"
)

if(APPLE)
    set_target_properties(JamWide PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
    )
endif()

# Install
install(TARGETS JamWide
    LIBRARY DESTINATION lib/clap
    BUNDLE DESTINATION lib/clap
)
```

### 2.2 Git Submodules Setup

```bash
# .gitmodules
[submodule "libs/clap"]
    path = libs/clap
    url = https://github.com/free-audio/clap.git
    branch = main

[submodule "libs/clap-helpers"]
    path = libs/clap-helpers
    url = https://github.com/free-audio/clap-helpers.git
    branch = main

[submodule "libs/imgui"]
    path = libs/imgui
    url = https://github.com/ocornut/imgui.git
    tag = v1.90.1

[submodule "libs/libogg"]
    path = libs/libogg
    url = https://github.com/xiph/ogg.git
    tag = v1.3.5

[submodule "libs/libvorbis"]
    path = libs/libvorbis
    url = https://github.com/xiph/vorbis.git
    tag = v1.3.7
```

---

## 3. Threading Architecture

### 3.1 Thread Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Plugin Instance                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│   ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐  │
│   │  Audio Thread   │   │   Run Thread    │   │   UI Thread     │  │
│   │  (Host-owned)   │   │  (Plugin-owned) │   │  (Host-owned)   │  │
│   ├─────────────────┤   ├─────────────────┤   ├─────────────────┤  │
│   │                 │   │                 │   │                 │  │
│   │ process()       │   │ run_thread_func │   │ ui_render_frame │  │
│   │   │             │   │   │             │   │   │             │  │
│   │   ▼             │   │   ▼             │   │   ▼             │  │
│   │ AudioProc()     │   │ client_mutex    │   │ drain ui_queue  │  │
│   │ (no lock)       │   │   │             │   │   │             │  │
│   │   │             │   │   ▼             │   │   ▼             │  │
│   │   ▼             │   │ Run()           │   │ check license   │  │
│   │ read atomics    │   │   │             │   │   │             │  │
│   │                 │   │   ▼             │   │   ▼             │  │
│   │                 │   │ callbacks       │   │ render ImGui    │  │
│   │                 │   │   │             │   │   │             │  │
│   │                 │   │   ▼             │   │   ▼             │  │
│   │                 │   │ push events     │   │ state_mutex     │  │
│   │                 │   │                 │   │ (UI state only) │  │
│   └─────────────────┘   └─────────────────┘   └─────────────────┘  │
│           │                     │                     │            │
│           │                     │                     │            │
│           ▼                     ▼                     ▼            │
│   ┌───────────────────────────────────────────────────────────┐   │
│   │                    Shared State                            │   │
│   ├───────────────────────────────────────────────────────────┤   │
│   │  std::atomic<T> config_* (MVP UI-touched only)            │   │
│   │  std::atomic<int> cached_status ◄── Run writes, all read  │   │
│   │  UiAtomicSnapshot ui_snapshot  ◄── Run/Audio write, UI read │   │
│   │  std::mutex client_mutex     ◄── All NJClient API calls   │   │
│   │  std::mutex state_mutex      ◄── UI state only            │   │
│   │  SpscRing<UiEvent> ui_queue  ◄── Run writes, UI reads     │   │
│   │  SpscRing<UiCommand> cmd_queue ◄── UI writes, Run reads   │   │
│   │  license_mutex + license_cv  ◄── Run + UI (license only)  │   │
│   └───────────────────────────────────────────────────────────┘   │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 Synchronization Primitives

| Primitive | Type | Owner | Purpose |
|-----------|------|-------|---------|
| `client_mutex` | `std::mutex` | Plugin | Serializes all NJClient API calls (except `AudioProc`) |
| `state_mutex` | `std::mutex` | Plugin | Protects UI/plugin state (not NJClient calls) |
| `ui_queue` | `SpscRing<UiEvent, 256>` | Plugin | Run → UI event delivery |
| `cmd_queue` | `SpscRing<UiCommand, 256>` | Plugin | UI → Run command delivery |
| `license_mutex` | `std::mutex` | Plugin | License dialog sync |
| `license_cv` | `std::condition_variable` | Plugin | License response wait |
| `license_pending` | `std::atomic<bool>` | Plugin | License prompt flag |
| `license_response` | `std::atomic<int>` | Plugin | User response (0/1/-1) |
| `shutdown` | `std::atomic<bool>` | Plugin | Thread termination flag |
| `audio_active` | `std::atomic<bool>` | Plugin | Audio processing flag |
| `config_*` | `std::atomic<T>` | NJClient | Lock-free access (MVP UI-touched only) |
| `cached_status` | `std::atomic<int>` | NJClient | Lock-free status access |
| `ui_snapshot` | `UiAtomicSnapshot` | Plugin | Lock-free UI telemetry snapshot |

### 3.3 Lock Ordering (Deadlock Prevention)

When multiple locks are needed, acquire in this order:
1. `license_mutex` (if needed)
2. `client_mutex` (if needed)
3. `state_mutex` (if needed)

**Never hold `client_mutex` or `state_mutex` while waiting on `license_cv`.**

---

## 4. Data Structures

### 4.1 Plugin Instance (`src/plugin/jamwide_plugin.h`)

```cpp
#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>

#include "clap/clap.h"
#include "core/njclient.h"
#include "threading/spsc_ring.h"
#include "threading/ui_command.h"
#include "ui/ui_state.h"

// Forward declarations
struct GuiContext;

// UI Events (Run → UI)
struct ChatMessageEvent {
    std::string type;
    std::string user;
    std::string text;
};

struct StatusChangedEvent {
    int status;
    std::string error_msg;
};

// Legacy: defined but currently unused (remote users read directly under client_mutex)
struct UserInfoChangedEvent {};

struct TopicChangedEvent {
    std::string topic;
};

using UiEvent = std::variant<
    ChatMessageEvent,
    StatusChangedEvent,
    UserInfoChangedEvent,
    TopicChangedEvent
>;

// Main plugin instance
struct JamWidePlugin {
    // CLAP plugin handle (back-reference)
    const clap_plugin_t* clap_plugin = nullptr;
    const clap_host_t* host = nullptr;

    // NJClient instance
    std::unique_ptr<NJClient> client;

    // Threading
    std::mutex state_mutex;
    std::mutex client_mutex;
    std::thread run_thread;
    std::atomic<bool> shutdown{false};
    std::atomic<bool> audio_active{false};

    // Run → UI event queue
    SpscRing<UiEvent, 256> ui_queue;

    // UI → Run command queue
    SpscRing<UiCommand, 256> cmd_queue;

    // License synchronization (dedicated slot)
    std::mutex license_mutex;
    std::condition_variable license_cv;
    std::atomic<bool> license_pending{false};
    std::string license_text;  // Protected by license_mutex
    std::atomic<int> license_response{0};

    // Connection settings (password in memory only)
    std::string server;
    std::string username;
    std::string password;

    // Audio settings
    double sample_rate{48000.0};
    uint32_t max_frames{512};

    // GUI
    GuiContext* gui_context = nullptr;
    UiState ui_state;
    UiAtomicSnapshot ui_snapshot;
    bool gui_created = false;
    bool gui_visible = false;
    uint32_t gui_width = 600;
    uint32_t gui_height = 400;

    // Parameter cache (for thread-safe access)
    std::atomic<float> param_master_volume{1.0f};
    std::atomic<bool> param_master_mute{false};
    std::atomic<float> param_metro_volume{0.5f};
    std::atomic<bool> param_metro_mute{false};
};

// Plugin lifecycle functions
JamWidePlugin* jamwide_plugin_create(const clap_plugin_t* clap_plugin,
                                    const clap_host_t* host);
void jamwide_plugin_destroy(JamWidePlugin* plugin);
bool jamwide_plugin_activate(JamWidePlugin* plugin, double sample_rate,
                            uint32_t min_frames, uint32_t max_frames);
void jamwide_plugin_deactivate(JamWidePlugin* plugin);
bool jamwide_plugin_start_processing(JamWidePlugin* plugin);
void jamwide_plugin_stop_processing(JamWidePlugin* plugin);
clap_process_status jamwide_plugin_process(JamWidePlugin* plugin,
                                          const clap_process_t* process);
```

### 4.2 SPSC Ring Buffer (`src/threading/spsc_ring.h`)

```cpp
#pragma once

#include <atomic>
#include <array>
#include <optional>
#include <cstddef>

template <typename T, size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be power of 2");

    std::array<T, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

    static constexpr size_t mask_ = Capacity - 1;

public:
    SpscRing() = default;

    // Non-copyable, non-movable
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;
    SpscRing(SpscRing&&) = delete;
    SpscRing& operator=(SpscRing&&) = delete;

    // Producer: returns false if full
    bool try_push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Producer: move version
    bool try_push(T&& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: returns nullopt if empty
    std::optional<T> try_pop() {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        T item = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return item;
    }

    // Consumer: drain all items with handler
    template <typename Func>
    void drain(Func&& handler) {
        while (auto event = try_pop()) {
            handler(std::move(*event));
        }
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & mask_;
    }

    static constexpr size_t capacity() { return Capacity; }
};
```

### 4.3 UI State (`src/ui/ui_state.h`)

```cpp
#pragma once

#include <string>
#include <vector>

struct RemoteChannel {
    std::string name;
    bool subscribed = true;
    float volume = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    float vu_left = 0.0f;
    float vu_right = 0.0f;
};

struct RemoteUser {
    std::string name;
    std::string address;
    bool mute = false;
    std::vector<RemoteChannel> channels;
};

struct UiState {
    // Connection
    char server_input[256] = "";
    char username_input[64] = "";
    char password_input[64] = "";
    std::string connection_error;
    bool connecting = false;

    // Status
    int status = -1;  // NJC_STATUS_DISCONNECTED
    float bpm = 0.0f;
    int bpi = 0;
    int beat_position = 0;
    int interval_position = 0;
    int interval_length = 0;

    // Local channel
    char local_name_input[64] = "Channel";
    int local_bitrate_index = 1;  // 64 kbps
    bool local_transmit = true;
    float local_volume = 1.0f;
    float local_pan = 0.0f;
    bool local_mute = false;
    bool local_solo = false;
    float local_vu_left = 0.0f;
    float local_vu_right = 0.0f;

    // Master
    float master_vu_left = 0.0f;
    float master_vu_right = 0.0f;

    // Remote users (legacy cache; UI reads NJClient directly under client_mutex)
    std::vector<RemoteUser> remote_users;
    bool users_dirty = false;

    // License dialog
    bool show_license_dialog = false;
    std::string license_text;

    // Solo state
    bool any_solo_active = false;
};
```

---

## 5. NJClient Modifications

### 5.1 Header Changes (`src/core/njclient.h`)

```cpp
// Add at top of file
#include <atomic>

class NJClient {
public:
    // ... existing public interface ...

    // ========== MODIFIED: Atomic config fields ==========
    // These are read by audio thread, written by UI thread
    std::atomic<float> config_mastervolume{1.0f};
    std::atomic<float> config_masterpan{0.0f};
    std::atomic<bool>  config_mastermute{false};

    std::atomic<float> config_metronome{0.5f};
    std::atomic<float> config_metronome_pan{0.0f};
    std::atomic<bool>  config_metronome_mute{false};

    std::atomic<int>   config_metronome_channel{-1};
    std::atomic<int>   config_play_prebuffer{8192};

    // ========== NEW: Cached status for lock-free access ==========
    std::atomic<int>   cached_status{NJC_STATUS_DISCONNECTED};

    // ... rest of class ...

private:
    int m_status;  // Keep internal status
    // ... rest of private members ...
};
```

### 5.2 Implementation Changes (`src/core/njclient.cpp`)

#### 5.2.1 Constructor

```cpp
NJClient::NJClient()
    : cached_status(NJC_STATUS_DISCONNECTED)
    // ... other initializers ...
{
    m_status = 1002;  // Internal "disconnected" code mapped by GetStatus()
    // ... existing constructor code ...
}
```

#### 5.2.2 Connect()

```cpp
void NJClient::Connect(const char* host, const char* user, const char* pass) {
    // ... existing connection logic ...

    // After setting m_status:
    cached_status.store(GetStatus(), std::memory_order_release);
}
```

#### 5.2.3 Disconnect()

```cpp
void NJClient::Disconnect() {
    // ... existing disconnect logic ...

    m_status = NJC_STATUS_DISCONNECTED;
    cached_status.store(NJC_STATUS_DISCONNECTED, std::memory_order_release);
}
```

#### 5.2.4 Run()

```cpp
int NJClient::Run() {
    // ... existing Run() logic ...

    // Before each return statement, sync cached status:
    cached_status.store(GetStatus(), std::memory_order_release);
    return result;
}
```

#### 5.2.5 Vorbis Integration

Remove REAPER callback indirection and use libvorbis directly:

```cpp
// Before (ReaNINJAM style with REAPER callbacks):
// if (GetVorbisEncoder) encoder = GetVorbisEncoder(...);

// After (direct libvorbis):
#include <vorbis/vorbisenc.h>
#include "../wdl/vorbisencdec.h"

// In encoding setup:
VorbisEncoder* encoder = new VorbisEncoder();
encoder->Open(nch, samplerate, bitrate, ...);

// In decoding:
VorbisDecoder* decoder = new VorbisDecoder();
decoder->Open(...);
```

### 5.3 Files to Copy from Original

| Source | Destination | Modifications |
|--------|-------------|---------------|
| `ninjam/njclient.h` | `src/core/njclient.h` | Atomic config, cached_status |
| `ninjam/njclient.cpp` | `src/core/njclient.cpp` | Status updates, direct Vorbis |
| `ninjam/netmsg.h` | `src/core/netmsg.h` | None |
| `ninjam/netmsg.cpp` | `src/core/netmsg.cpp` | None |
| `ninjam/mpb.h` | `src/core/mpb.h` | None |
| `ninjam/mpb.cpp` | `src/core/mpb.cpp` | None |
| `ninjam/njmisc.h` | `src/core/njmisc.h` | None |
| `ninjam/njmisc.cpp` | `src/core/njmisc.cpp` | None |

### 5.4 WDL Files to Copy

| Source | Destination | Notes |
|--------|-------------|-------|
| `WDL/jnetlib/*` | `wdl/jnetlib/` | All networking files |
| `WDL/vorbisencdec.h` | `wdl/vorbisencdec.h` | Vorbis wrapper |
| `WDL/ptrlist.h` | `wdl/ptrlist.h` | Pointer list |
| `WDL/queue.h` | `wdl/queue.h` | Byte queue |
| `WDL/heapbuf.h` | `wdl/heapbuf.h` | Heap buffer |
| `WDL/wdlstring.h` | `wdl/wdlstring.h` | String class |
| `WDL/wdltypes.h` | `wdl/wdltypes.h` | Type definitions |
| `WDL/mutex.h` | `wdl/mutex.h` | Mutex wrapper |
| `WDL/sha.h` / `.cpp` | `wdl/sha.h` / `.cpp` | SHA-1 hash |
| `WDL/rng.h` / `.cpp` | `wdl/rng.h` / `.cpp` | Random numbers |
| `WDL/pcmfmtcvt.h` | `wdl/pcmfmtcvt.h` | Sample conversion |
| `WDL/wavwrite.h` | `wdl/wavwrite.h` | WAV file writing |

---

## 6. Run Thread Implementation

### 6.1 Header (`src/threading/run_thread.h`)

```cpp
#pragma once

struct JamWidePlugin;

// Start the Run thread (called from activate)
void run_thread_start(JamWidePlugin* plugin);

// Stop the Run thread (called from deactivate)
void run_thread_stop(JamWidePlugin* plugin);
```

### 6.2 Implementation (`src/threading/run_thread.cpp`)

```cpp
#include "run_thread.h"
#include "plugin/jamwide_plugin.h"
#include <chrono>

namespace {

void run_thread_func(JamWidePlugin* plugin) {
    while (!plugin->shutdown.load(std::memory_order_acquire)) {
        // Always call Run() regardless of connection state
        // This allows Run() to process connection attempts
        plugin->client_mutex.lock();
        while (!plugin->client->Run()) {
            // Run() returns 0 while there's more work
            // Check shutdown between iterations
            if (plugin->shutdown.load(std::memory_order_acquire)) {
                plugin->client_mutex.unlock();
                return;
            }
        }
        plugin->client_mutex.unlock();

        // Adaptive sleep: faster when connected, slower when idle
        int status = plugin->client->cached_status.load(
            std::memory_order_acquire);
        auto sleep_time = (status == NJC_STATUS_DISCONNECTED)
            ? std::chrono::milliseconds(50)
            : std::chrono::milliseconds(20);

        std::this_thread::sleep_for(sleep_time);
    }
}

}  // namespace

void run_thread_start(JamWidePlugin* plugin) {
    plugin->shutdown.store(false, std::memory_order_release);
    plugin->run_thread = std::thread(run_thread_func, plugin);
}

void run_thread_stop(JamWidePlugin* plugin) {
    // Signal shutdown
    plugin->shutdown.store(true, std::memory_order_release);

    // Wake up license wait if blocked
    {
        std::lock_guard<std::mutex> lock(plugin->license_mutex);
        plugin->license_response.store(-1, std::memory_order_release);
    }
    plugin->license_cv.notify_one();

    // Join thread
    if (plugin->run_thread.joinable()) {
        plugin->run_thread.join();
    }
}
```

---

## 7. Callback Implementations

### 7.1 Callback Setup

```cpp
// In jamwide_plugin.cpp, during activate:

static void chat_callback(void* user_data, NJClient* client,
                          const char** parms, int nparms);
static int license_callback(void* user_data, const char* license_text);

void setup_callbacks(JamWidePlugin* plugin) {
    plugin->client->ChatMessage_Callback = chat_callback;
    plugin->client->ChatMessage_User = plugin;

    plugin->client->LicenseAgreementCallback = license_callback;
    plugin->client->LicenseAgreement_User = plugin;
}
```

### 7.2 Chat Callback

```cpp
static void chat_callback(void* user_data, NJClient* client,
                          const char** parms, int nparms) {
    auto* plugin = static_cast<JamWidePlugin*>(user_data);

    if (nparms < 1) return;

    ChatMessageEvent event;
    event.type = parms[0] ? parms[0] : "";
    event.user = (nparms > 1 && parms[1]) ? parms[1] : "";
    event.text = (nparms > 2 && parms[2]) ? parms[2] : "";

    // Best-effort push (drop if queue full)
    plugin->ui_queue.try_push(std::move(event));
}
```

### 7.3 License Callback (Guaranteed Delivery)

```cpp
static int license_callback(void* user_data, const char* license_text) {
    auto* plugin = static_cast<JamWidePlugin*>(user_data);

    // Store license in dedicated slot (guaranteed delivery)
    {
        std::lock_guard<std::mutex> lock(plugin->license_mutex);
        plugin->license_text = license_text ? license_text : "";
    }

    // Signal UI
    plugin->license_response.store(0, std::memory_order_release);
    plugin->license_pending.store(true, std::memory_order_release);

    // Release client_mutex while waiting for UI (ReaNINJAM pattern)
    plugin->client_mutex.unlock();

    // Wait for user response (60 second timeout)
    {
        std::unique_lock<std::mutex> lock(plugin->license_mutex);
        bool ok = plugin->license_cv.wait_for(
            lock,
            std::chrono::seconds(60),
            [&] {
                return plugin->license_response.load(
                    std::memory_order_acquire) != 0;
            });

        plugin->license_pending.store(false, std::memory_order_release);

        if (!ok) {
            plugin->client_mutex.lock();
            return 0;  // Timeout = reject
        }
    }

    plugin->client_mutex.lock();
    return plugin->license_response.load(std::memory_order_acquire) > 0
        ? 1 : 0;
}
```

---

## 8. Summary

Part 1 covers:
- Project structure and file organization
- CMake build system configuration
- Threading architecture and synchronization
- Core data structures
- NJClient modifications for thread safety
- Run thread implementation
- Callback handling

**Next: Part 2 - CLAP Integration** (entry point, lifecycle, audio, params, state, GUI extension)
