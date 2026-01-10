# CLAP NINJAM Client Port - Threading & Sync Design (Final)

## Overview

This document details the threading, callback handling, and config synchronization patterns for the CLAP NINJAM plugin. Addresses data races, callback deadlocks, and transport-stopped audio behavior.

**Requires C++20** (for `std::variant`, `std::optional`, designated initializers).

---

## 1. Thread Roles & Invariants

| Thread | Responsibility | Locking |
|--------|----------------|---------|
| **Audio thread** | Calls `NJClient::AudioProc()` | NO locks (reads atomic fields directly) |
| **Run thread** | Calls `NJClient::Run()`, handles network | Holds `client_mutex` during NJClient calls |
| **UI thread** | Renders ImGui, handles user input | Uses `state_mutex` for UI state only |

### Invariants

1. `client_mutex` protects all NJClient API calls **except** `AudioProc()`.
2. Only the MVP UI-touched config fields (master/metronome) and `cached_status` are `std::atomic`; all other NJClient fields (including other `config_*`) must be accessed under `client_mutex`.
3. Callbacks from `Run()` must **never** take `client_mutex` or touch UI directly.
4. Audio thread never waits on any mutex.
5. Run thread **always ticks**, even when disconnected, to handle connection state transitions.
6. If VU meters are added, read peaks via an audio-thread snapshot (do not read `output_peaklevel` directly from UI).

---

## 2. Config Sync: Atomic Fields in NJClient

### Problem

Original `config_*` fields in NJClient are plain `float`/`int`/`bool`. Concurrent read (audio) and write (UI/Run) is a data race.

### Solution: Modify Copied NJClient

Since we copy `njclient.h/cpp` into `src/core/`, modify only the MVP config fields to be atomic:

```cpp
// src/core/njclient.h (modified copy)

#include <atomic>

class NJClient {
public:
    // ... existing code ...

    // Atomic config fields (thread-safe read/write)
    std::atomic<float> config_mastervolume{1.0f};
    std::atomic<float> config_masterpan{0.0f};
    std::atomic<bool>  config_mastermute{false};

    std::atomic<float> config_metronome{0.5f};
    std::atomic<float> config_metronome_pan{0.0f};
    std::atomic<bool>  config_metronome_mute{false};

    std::atomic<int>   config_metronome_channel{-1};
    std::atomic<int>   config_play_prebuffer{8192};

    // Cached status for lock-free audio thread access
    std::atomic<int>   cached_status{NJC_STATUS_DISCONNECTED};

    // ... rest of class ...
};
```

### Modify Connect()/Disconnect() to Update cached_status

```cpp
// src/core/njclient.cpp (modified copy)

void NJClient::Connect(const char* host, const char* user, const char* pass) {
    // ... existing Connect() logic ...

    // Update cached status immediately
    cached_status.store(GetStatus(), std::memory_order_release);
}

void NJClient::Disconnect() {
    // ... existing Disconnect() logic ...

    // Update cached status immediately
    cached_status.store(NJC_STATUS_DISCONNECTED, std::memory_order_release);
}

int NJClient::Run() {
    // ... existing Run() logic ...

    // At end of Run(), update cached status
    cached_status.store(GetStatus(), std::memory_order_release);

    return result;
}
```

Non-MVP `config_*` fields remain non-atomic and must only be read/written under `client_mutex`.

---

## 3. Data Structures

### Plugin Instance

```cpp
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <variant>
#include <optional>

struct JamWidePlugin {
    NJClient* client = nullptr;

    // Threading
    std::mutex state_mutex;           // Protects UI/plugin state
    std::mutex client_mutex;          // Protects NJClient API (except AudioProc)
    std::thread run_thread;
    std::atomic<bool> shutdown{false};
    std::atomic<bool> audio_active{false};

    // Run → UI event queue (for chat, status, user info)
    SpscRing<UiEvent, 256> ui_queue;

    // License synchronization (dedicated slot - guaranteed delivery)
    std::mutex license_mutex;
    std::condition_variable license_cv;
    std::atomic<bool> license_pending{false};
    std::string license_text;         // Protected by license_mutex
    std::atomic<int> license_response{0};  // 0=pending, 1=accept, -1=reject

    // Connection (password in memory only)
    std::string server;
    std::string username;
    std::string password;  // Cleared on disconnect, not saved to state

    // Sample rate (set in activate)
    double sample_rate = 48000.0;
};
```

### UI Event Types (No License - License Uses Dedicated Slot)

```cpp
#include <variant>
#include <string>

struct ChatMessageEvent {
    std::string type;      // "MSG", "PRIVMSG", "JOIN", "PART", etc.
    std::string user;
    std::string text;
};

struct StatusChangedEvent {
    int status;            // NJC_STATUS_*
    std::string error_msg;
};

struct UserInfoChangedEvent {
    // Signals UI to refresh user/channel list
};

struct TopicChangedEvent {
    std::string topic;
};

// Note: LicenseRequestEvent removed - uses dedicated slot instead
using UiEvent = std::variant<
    ChatMessageEvent,
    StatusChangedEvent,
    UserInfoChangedEvent,
    TopicChangedEvent
>;
```

### SPSC Ring Buffer

```cpp
#include <atomic>
#include <array>
#include <optional>

template <typename T, size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    std::array<T, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};  // Written by producer
    alignas(64) std::atomic<size_t> tail_{0};  // Written by consumer

    static constexpr size_t mask = Capacity - 1;

public:
    // Producer (Run thread) - returns false if full
    bool try_push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask;

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // Full
        }

        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer (UI thread) - returns nullopt if empty
    std::optional<T> try_pop() {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // Empty
        }

        T item = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask, std::memory_order_release);
        return item;
    }

    // Consumer - drain all available items
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
};
```

---

## 4. Callback Strategy

### Rule: Callbacks are non-blocking, lock-free, UI-free

Callbacks invoked from `NJClient::Run()` execute on the Run thread while `client_mutex` is held. They must:
- **NOT** acquire `client_mutex` (deadlock)
- **NOT** call UI functions (wrong thread)
- **ONLY** enqueue events or set atomic flags

### License Callback: Dedicated Slot (Guaranteed Delivery)

License uses a dedicated atomic flag + mutex-protected string, **not** the SPSC queue. This guarantees the prompt always reaches the UI.

**Known behavior**: While the license dialog is open, the Run thread is blocked waiting on `license_cv`. The license callback releases `client_mutex` while waiting (ReaNINJAM pattern) so UI remains responsive.

**Rationale**: This matches ReaNINJAM behavior and is acceptable because:
1. License prompt is modal and requires user attention
2. User cannot meaningfully interact with session before accepting
3. 60-second timeout prevents permanent deadlock

### Callback Setup

```cpp
void setup_callbacks(JamWidePlugin* plugin) {
    plugin->client->ChatMessage_Callback = chat_callback;
    plugin->client->ChatMessage_User = plugin;

    plugin->client->LicenseAgreementCallback = license_callback;
    plugin->client->LicenseAgreement_User = plugin;
}
```

### Chat Callback

```cpp
void chat_callback(void* user_data, NJClient* client,
                   const char** parms, int nparms) {
    auto* plugin = static_cast<JamWidePlugin*>(user_data);

    if (nparms < 1) return;

    ChatMessageEvent event;
    event.type = parms[0] ? parms[0] : "";
    event.user = (nparms > 1 && parms[1]) ? parms[1] : "";
    event.text = (nparms > 2 && parms[2]) ? parms[2] : "";

    // Non-blocking push; drop if full (acceptable for chat)
    plugin->ui_queue.try_push(event);
}
```

### License Callback (Guaranteed Delivery via Dedicated Slot)

```cpp
int license_callback(void* user_data, const char* license_text) {
    auto* plugin = static_cast<JamWidePlugin*>(user_data);

    // Store license text in dedicated slot (guaranteed delivery)
    {
        std::lock_guard<std::mutex> lock(plugin->license_mutex);
        plugin->license_text = license_text ? license_text : "";
    }

    // Signal UI that license is pending
    plugin->license_response.store(0, std::memory_order_release);
    plugin->license_pending.store(true, std::memory_order_release);

    // Wait for UI response
    {
        std::unique_lock<std::mutex> lock(plugin->license_mutex);
        bool ok = plugin->license_cv.wait_for(lock, std::chrono::seconds(60), [&] {
            return plugin->license_response.load(std::memory_order_acquire) != 0;
        });

        // Clear pending flag
        plugin->license_pending.store(false, std::memory_order_release);

        if (!ok) {
            return 0;  // Timeout = reject
        }
    }

    return plugin->license_response.load(std::memory_order_acquire) > 0 ? 1 : 0;
}
```

---

## 5. Thread Implementations

### Run Thread (Always Ticks)

```cpp
void run_thread_func(JamWidePlugin* plugin) {
    while (!plugin->shutdown.load(std::memory_order_acquire)) {

        // Always call Run() - handles all states including disconnected,
        // preconnect, connecting, etc.
        plugin->client_mutex.lock();
        while (!plugin->client->Run()) {
            // Run() returns 0 while there's more work
        }
        plugin->client_mutex.unlock();
        // Note: Run() internally updates cached_status

        // Sleep interval: shorter when potentially connecting, longer when idle
        int status = plugin->client->cached_status.load(std::memory_order_acquire);
        auto sleep_time = (status == NJC_STATUS_DISCONNECTED)
            ? std::chrono::milliseconds(50)
            : std::chrono::milliseconds(20);

        std::this_thread::sleep_for(sleep_time);
    }
}
```

### Audio Thread (Lock-Free)

```cpp
clap_process_status process(const clap_plugin* clap_plugin,
                            const clap_process* process) {
    auto* plugin = static_cast<JamWidePlugin*>(clap_plugin->plugin_data);

    // Read transport (lock-free)
    bool is_playing = false;
    bool is_seek = false;
    double cursor_pos = -1.0;

    if (process->transport) {
        is_playing = (process->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    }

    // Get audio buffers
    float* in[2] = {
        process->audio_inputs[0].data32[0],
        process->audio_inputs[0].data32[1]
    };
    float* out[2] = {
        process->audio_outputs[0].data32[0],
        process->audio_outputs[0].data32[1]
    };
    uint32_t frames = process->frames_count;

    // Check connection status (lock-free atomic read)
    int status = plugin->client->cached_status.load(std::memory_order_acquire);

    if (status == NJC_STATUS_OK) {
        // ReaNINJAM-style: always call AudioProc, use just_monitor when not playing
        bool just_monitor = !is_playing;

        // AudioProc reads atomic config fields directly - no locks needed
        plugin->client->AudioProc(
            in, 2,
            out, 2,
            static_cast<int>(frames),
            static_cast<int>(plugin->sample_rate),
            just_monitor,
            is_playing,
            is_seek,
            cursor_pos
        );
    } else {
        // Not connected: pass-through
        if (in[0] != out[0]) memcpy(out[0], in[0], frames * sizeof(float));
        if (in[1] != out[1]) memcpy(out[1], in[1], frames * sizeof(float));
    }

    return CLAP_PROCESS_CONTINUE;
}
```

### UI Thread

```cpp
void ui_render_frame(JamWidePlugin* plugin) {
    // 1. Drain event queue (lock-free, for chat/status/user events)
    plugin->ui_queue.drain([&](UiEvent&& event) {
        std::visit([&](auto&& e) { handle_event(plugin, std::move(e)); }, std::move(event));
    });

    // 2. Check for license prompt (dedicated slot - guaranteed delivery)
    if (plugin->license_pending.load(std::memory_order_acquire)) {
        ui_state.show_license_dialog = true;
        {
            std::lock_guard<std::mutex> lock(plugin->license_mutex);
            ui_state.license_text = plugin->license_text;
        }
    }

    // 3. Render panels
    render_status_bar(plugin);
    render_connection_panel(plugin);
    render_local_channel(plugin);
    render_remote_channels(plugin);

    // 4. License dialog (modal, guaranteed to appear)
    if (ui_state.show_license_dialog) {
        render_license_dialog(plugin);
    }
}

void render_local_channel(JamWidePlugin* plugin) {
    // Volume slider - writes atomic directly (no lock needed)
    float vol = plugin->client->config_mastervolume.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Master", &vol, 0.0f, 2.0f)) {
        plugin->client->config_mastervolume.store(vol, std::memory_order_relaxed);
    }

    // Mute button
    bool mute = plugin->client->config_mastermute.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Mute", &mute)) {
        plugin->client->config_mastermute.store(mute, std::memory_order_relaxed);
    }

    // Metronome volume
    float metro = plugin->client->config_metronome.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Metronome", &metro, 0.0f, 2.0f)) {
        plugin->client->config_metronome.store(metro, std::memory_order_relaxed);
    }

    // Metronome mute
    bool metro_mute = plugin->client->config_metronome_mute.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Metro Mute", &metro_mute)) {
        plugin->client->config_metronome_mute.store(metro_mute, std::memory_order_relaxed);
    }
}

void render_remote_channels(JamWidePlugin* plugin) {
    // ReaNINJAM-style: read NJClient getters under client_mutex.
    std::unique_lock<std::mutex> lock(plugin->client_mutex);
    NJClient* client = plugin->client.get();
    if (!client) return;

    const int num_users = client->GetNumUsers();
    for (int u = 0; u < num_users; ++u) {
        const char* user_name = client->GetUserState(u, nullptr, nullptr, nullptr);
        for (int i = 0; ; ++i) {
            const int ch = client->EnumUserChannels(u, i);
            if (ch < 0) break;
            client->GetUserChannelState(u, ch, &sub, &vol, &pan, &mute, &solo);
        }
    }
}

void render_license_dialog(JamWidePlugin* plugin) {
    ImGui::OpenPopup("Server License");

    if (ImGui::BeginPopupModal("Server License", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", ui_state.license_text.c_str());

        if (ImGui::Button("Accept")) {
            plugin->license_response.store(1, std::memory_order_release);
            plugin->license_pending.store(false, std::memory_order_release);
            plugin->license_cv.notify_one();
            ui_state.show_license_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reject")) {
            plugin->license_response.store(-1, std::memory_order_release);
            plugin->license_pending.store(false, std::memory_order_release);
            plugin->license_cv.notify_one();
            ui_state.show_license_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
```

---

## 6. NJClient Modifications Summary

Changes to make in the copied `src/core/njclient.h`:

```cpp
// Before (original)
float config_mastervolume;
float config_masterpan;
bool config_mastermute;
float config_metronome;
float config_metronome_pan;
bool config_metronome_mute;
int config_metronome_channel;
int config_play_prebuffer;

// After (modified copy)
#include <atomic>

std::atomic<float> config_mastervolume{1.0f};
std::atomic<float> config_masterpan{0.0f};
std::atomic<bool>  config_mastermute{false};
std::atomic<float> config_metronome{0.5f};
std::atomic<float> config_metronome_pan{0.0f};
std::atomic<bool>  config_metronome_mute{false};
std::atomic<int>   config_metronome_channel{-1};
std::atomic<int>   config_play_prebuffer{8192};

// Add cached status for lock-free access
std::atomic<int>   cached_status{NJC_STATUS_DISCONNECTED};
```

Changes to `src/core/njclient.cpp`:

```cpp
// In Connect():
void NJClient::Connect(const char* host, const char* user, const char* pass) {
    // ... existing logic that sets m_status ...
    cached_status.store(m_status, std::memory_order_release);
}

// In Disconnect():
void NJClient::Disconnect() {
    // ... existing logic ...
    m_status = NJC_STATUS_DISCONNECTED;
    cached_status.store(NJC_STATUS_DISCONNECTED, std::memory_order_release);
}

// At end of Run():
int NJClient::Run() {
    // ... existing logic ...

    // Before return, sync cached status
    cached_status.store(m_status, std::memory_order_release);
    return result;
}

// Constructor:
NJClient::NJClient() : cached_status(NJC_STATUS_DISCONNECTED) {
    // ... existing init ...
}
```

---

## 7. Summary

| Concern | Solution |
|---------|----------|
| Data race on `config_*` | Atomic fields in modified NJClient copy (MVP UI-touched only; other `config_*` under `client_mutex`) |
| Data race on status | `cached_status` atomic, updated in `Connect()`, `Disconnect()`, and `Run()` |
| Run thread not ticking | Always tick, even when disconnected |
| Audio thread locking | None required; reads atomics directly |
| Callback deadlocks | Callbacks only set flags/enqueue; never lock `client_mutex` |
| License guaranteed delivery | Dedicated atomic slot, not SPSC queue |
| License blocking | Acceptable UX; modal dialog, 60s timeout |

---

## 8. Requirements

| Requirement | Value |
|-------------|-------|
| C++ Standard | **C++20** (for `std::variant`, `std::optional`, designated initializers) |
| Compilers | MSVC 2019+, Clang 10+, Xcode 12+ |
| Platforms | Windows 10+, macOS 10.15+ |
