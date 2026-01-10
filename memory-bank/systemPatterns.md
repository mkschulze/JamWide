# CLAP NINJAM Client - System Patterns

Architecture patterns, conventions, and implementation guidelines.

---

## 1. Threading Patterns

### 1.1 Thread Ownership

| Thread | Owner | Purpose |
|--------|-------|---------|
| Audio | Host (DAW) | Real-time audio processing |
| Run | Plugin | Network I/O, protocol handling |
| UI | Host (DAW) | GUI rendering, user input |

### 1.2 Lock Hierarchy (Updated r54)

```
Audio thread:  NO LOCKS (reads atomics only, calls AudioProc only)
     │
     ▼
Run thread:    client_mutex (all NJClient API calls)
               m_users_cs → m_remotechannel_rd_mutex (NJClient internal)
               state_mutex (for ui_state updates)
     │
     ▼
UI thread:     state_mutex (for ui_state reads - copy then release)
               license_mutex (for license dialog only)
```

**Rules:**
1. Never hold multiple locks simultaneously (except NJClient internal pair)
2. UI never calls NJClient methods - uses command queue
3. Audio thread only calls `AudioProc()` - everything else via atomics
4. Run thread is sole owner of NJClient interaction

### 1.3 Atomic Access Patterns

```cpp
// UI thread writes, audio thread reads (MVP UI-touched only)
plugin->client->config_mastervolume.store(val, std::memory_order_relaxed);

// Audio thread reads
float vol = client->config_mastervolume.load(std::memory_order_relaxed);

// Run thread updates status, all threads read
cached_status.store(GetStatus(), std::memory_order_release);
int status = cached_status.load(std::memory_order_acquire);

// Run/audio thread writes snapshot, UI reads
ui_snapshot.bpm.store(bpm, std::memory_order_relaxed);
float bpm_ui = ui_snapshot.bpm.load(std::memory_order_relaxed);
```

**Snapshot update point:** Refresh `ui_snapshot` in the Run thread after `NJClient::Run()` and after releasing `client_mutex`. Audio thread only writes VU fields.

**Memory ordering:**
- `relaxed` for config values (no ordering needed)
- `release/acquire` for status (establishes happens-before)

### 1.4 Event Queue Pattern (SPSC)

```cpp
// Run thread produces (never blocks)
if (!ui_queue.try_push(StatusChangedEvent{status, error})) {
    // Queue full - drop event (non-critical)
}

// UI thread consumes (drains all)
ui_queue.drain([&](UiEvent&& event) {
    std::visit([&](auto&& e) { handle(e); }, std::move(event));
});
```

### 1.5 License Callback Pattern (Dedicated Slot)

```cpp
// Run thread (in callback)
{
    std::lock_guard<std::mutex> lock(plugin->license_mutex);
    plugin->license_text = text;
}
plugin->license_pending.store(true, std::memory_order_release);

// Release client_mutex while waiting for UI (ReaNINJAM pattern)
plugin->client_mutex.unlock();

// Wait for response (with timeout)
std::unique_lock<std::mutex> lock(plugin->license_mutex);
plugin->license_cv.wait_for(lock, 60s, [&] {
    return plugin->license_response.load() != 0;
});
plugin->client_mutex.lock();

// UI thread
if (license_pending.load(std::memory_order_acquire)) {
    show_license_dialog();
}
// On accept/reject:
plugin->license_response.store(1, std::memory_order_release);
plugin->license_pending.store(false, std::memory_order_release);
plugin->license_cv.notify_one();
```

---

## 2. CLAP Plugin Patterns

### 2.1 Extension Discovery

```cpp
const void* plugin_get_extension(const clap_plugin_t* plugin, const char* id) {
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &s_audio_ports;
    if (!strcmp(id, CLAP_EXT_PARAMS)) return &s_params;
    if (!strcmp(id, CLAP_EXT_STATE)) return &s_state;
    if (!strcmp(id, CLAP_EXT_GUI)) return &s_gui;
    return nullptr;
}
```

### 2.2 Parameter ID Scheme

| ID | Name | Type |
|----|------|------|
| 0 | Master Volume | float 0.0-2.0 |
| 1 | Master Mute | bool (stepped) |
| 2 | Metronome Volume | float 0.0-2.0 |
| 3 | Metronome Mute | bool (stepped) |

**Future IDs:** Reserve 100+ for local channel, 1000+ for remote channels.

### 2.3 State Serialization

```cpp
// Always include version for migration
{
    "version": 1,
    "server": "...",
    "username": "...",
    // NO password - security
    "master": { "volume": 1.0, "mute": false },
    "metronome": { "volume": 0.5, "mute": false },
    "localChannel": { "name": "...", "transmit": true, "bitrate": 64 }
}
```

### 2.4 Audio Processing Pattern

```cpp
clap_process_status plugin_process(const clap_plugin_t* plugin,
                                    const clap_process_t* process) {
    auto* p = static_cast<NinjamPlugin*>(plugin->plugin_data);

    // 1. Sync params from host events
    sync_params_from_events(p, process->in_events);

    // 2. Get I/O pointers
    float* in[2] = { process->audio_inputs[0].data32[0],
                     process->audio_inputs[0].data32[1] };
    float* out[2] = { process->audio_outputs[0].data32[0],
                      process->audio_outputs[0].data32[1] };

    // 3. Check connection status (lock-free)
    int status = p->client->cached_status.load(std::memory_order_acquire);

    if (status == NJC_STATUS_OK) {
        // is_playing/is_seek/cursor_pos extracted from transport (omitted)
        // 4. Process through NJClient (no locks)
        const bool just_monitor = !is_playing;
    p->client->AudioProc(in, 2, out, 2, process->frames_count,
                         static_cast<int>(p->sample_rate),
                         just_monitor, is_playing, is_seek, cursor_pos);
    } else {
        // 5. Pass-through when disconnected
        memcpy(out[0], in[0], process->frames_count * sizeof(float));
        memcpy(out[1], in[1], process->frames_count * sizeof(float));
    }

    return CLAP_PROCESS_CONTINUE;
}
```

---

## 3. UI Patterns

### 3.1 ImGui Window Setup

```cpp
// Fill entire plugin area
ImGuiViewport* viewport = ImGui::GetMainViewport();
ImGui::SetNextWindowPos(viewport->Pos);
ImGui::SetNextWindowSize(viewport->Size);

ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoCollapse;

### 3.2 ImGui ID Hygiene

Use `##` suffixes or `ImGui::PushID()` when repeating widgets in loops.

Local checker:

```bash
python3 tools/check_imgui_ids.py
```

This flags unscoped widget labels that are not wrapped in `ImGui::PushID(...)`.

ImGui::Begin("NINJAM", nullptr, flags);
// ... content ...
ImGui::End();
```

### 3.2 Collapsible Panels

```cpp
if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();
    // ... panel content ...
    ImGui::Unindent();
}
```

### 3.3 Parameter Binding

```cpp
// Read atomic, modify, write back
float vol = plugin->param_master_volume.load(std::memory_order_relaxed);
ImGui::SetNextItemWidth(200);
if (ImGui::SliderFloat("Master Volume", &vol, 0.0f, 2.0f)) {
    plugin->param_master_volume.store(vol, std::memory_order_relaxed);
    plugin->client->config_mastervolume.store(vol, std::memory_order_relaxed);
}
```

### 3.4 NJClient API Calls from UI

```cpp
// UI never calls NJClient directly; send a command to the Run thread.
if (ImGui::Button("Connect")) {
    ConnectCommand cmd;
    cmd.server = server;
    cmd.username = user;
    cmd.password = pass;
    plugin->cmd_queue.try_push(std::move(cmd));
}
```

### 3.5 VU Meter Colors

| Level | Color | RGB |
|-------|-------|-----|
| < 60% | Green | (50, 200, 50) |
| 60-85% | Yellow | (200, 200, 50) |
| > 85% | Red | (200, 50, 50) |

---

## 4. Naming Conventions

### 4.1 Files

| Pattern | Example |
|---------|---------|
| CLAP extensions | `clap_audio.cpp`, `clap_params.cpp` |
| UI panels | `ui_status.cpp`, `ui_connection.cpp` |
| Platform code | `gui_win32.cpp`, `gui_macos.mm` |
| Core (modified) | `njclient.cpp`, `netmsg.cpp` |

### 4.2 Classes/Structs

```cpp
struct NinjamPlugin;     // Main plugin instance
struct UiState;          // UI-only state
struct RemoteUser;       // Data struct
struct RemoteChannel;    // Data struct

class GuiContext;        // Abstract interface
class GuiContextWin32;   // Platform implementation
```

### 4.3 Functions

```cpp
// CLAP callbacks (C linkage pattern)
static bool plugin_init(const clap_plugin_t* plugin);
static void plugin_destroy(const clap_plugin_t* plugin);

// UI render functions
void ui_render_frame(NinjamPlugin* plugin);
void ui_render_status_bar(NinjamPlugin* plugin);

// Helper functions
static ImU32 get_vu_color(float level);
```

### 4.4 Variables

```cpp
// Atomics
std::atomic<float> config_mastervolume;
std::atomic<int> cached_status;
UiAtomicSnapshot ui_snapshot;
std::atomic<bool> license_pending;

// Mutexes
std::mutex client_mutex;
std::mutex state_mutex;
std::mutex license_mutex;

// UI state
char server_input[256];
bool local_transmit;
float local_volume;
```

---

## 5. Error Handling

### 5.1 Connection Errors

```cpp
// Callback pushes error to UI
void on_disconnect(NinjamPlugin* p, int code) {
    std::string msg = get_error_message(code);
    p->ui_queue.try_push(StatusChangedEvent{NJC_STATUS_DISCONNECTED, msg});
}

// UI displays error
if (!state.connection_error.empty()) {
    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%s", state.connection_error.c_str());
}
```

### 5.2 Graceful Degradation

| Failure | Behavior |
|---------|----------|
| Queue full | Drop event (non-critical data) |
| License timeout | Auto-reject after 60s |
| Network error | Disconnect, show error, allow reconnect |
| Invalid state | Log and continue (don't crash) |

---

## 6. Build Patterns

### 6.1 Platform Detection

```cmake
if(WIN32)
    target_sources(plugin PRIVATE src/platform/gui_win32.cpp)
    target_link_libraries(plugin PRIVATE d3d11 dxgi ws2_32)
elseif(APPLE)
    target_sources(plugin PRIVATE src/platform/gui_macos.mm)
    target_link_libraries(plugin PRIVATE
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa")
endif()
```

### 6.2 CLAP Bundle Structure

```
# Windows
NINJAM.clap/
└── NINJAM.dll

# macOS
NINJAM.clap/
├── Contents/
│   ├── Info.plist
│   └── MacOS/
│       └── NINJAM
```

---

## 7. Testing Patterns

### 7.1 clap-validator

```bash
clap-validator build/NINJAM.clap
```

Must pass all checks before release.

### 7.2 Multi-Instance Test

1. Load plugin on two tracks
2. Connect both to same server
3. Verify independent state
4. Verify no crashes/hangs

### 7.3 State Persistence Test

1. Configure plugin (connect, set levels)
2. Save DAW project
3. Close and reopen project
4. Verify all settings restored (except password)

---

## 9. macOS Embedded View Patterns

### 9.1 NSView in Host Context

When a CLAP plugin creates an NSView that gets embedded in the host's window:

```objc
// Host calls gui_create(), we return our view
// Host adds view to its window hierarchy
[parentView addSubview:ourView];
```

**Known Issues:**
- Host may intercept events before they reach our view
- First responder status may not be retained
- Mouse tracking areas may need explicit setup
- ImGui's OSX backend adds a hidden NSTextInputClient subview; ensure it becomes first responder

### 9.2 Input Event Forwarding

```objc
// Override event methods explicitly
- (void)mouseDown:(NSEvent *)event {
    ImGuiIO& io = ImGui::GetIO();
    NSPoint location = [self convertPoint:[event locationInWindow] fromView:nil];
    io.AddMousePosEvent(location.x, self.bounds.size.height - location.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    [self.delegate render];  // Trigger redraw
}

// Make view focusable
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
```

### 9.3 Tracking Areas

```objc
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    // Remove old tracking areas
    for (NSTrackingArea *area in self.trackingAreas) {
        [self removeTrackingArea:area];
    }
    // Add new tracking area for entire view
    NSTrackingArea *trackingArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds
        options:(NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect)
        owner:self
        userInfo:nil];
    [self addTrackingArea:trackingArea];
}
```

### 9.4 Debug Checklist

When input isn't working in a hosted plugin:

1. **Check event handlers are called**: Add `fprintf(stderr, ...)` to verify
2. **Check responder chain**: Log `[self window]` and `[self.window firstResponder]`
3. **Check view is in window**: `self.window != nil` after embedding
4. **Test in different host**: REAPER vs Bitwig may behave differently
5. **Compare with working plugins**: Surge XT uses ImGui in CLAP successfully

---

## 8. Anti-Patterns (What NOT to Do) - Updated

| ❌ Don't | ✅ Do Instead |
|----------|---------------|
| Lock mutex in audio thread | Use atomics for audio-accessed data |
| Call NJClient from UI thread without `client_mutex` | Lock `client_mutex` for NJClient getters |
| Iterate remote_users without lock | Read users/channels directly under `client_mutex` |
| Hold raw pointer in run thread | Use shared_ptr keepalive |
| Block on UI response while holding `client_mutex` or `state_mutex` | Release lock(s), then wait with timeout |
| Use same mutex name for different data | NJClient fixed: both m_users_cs+m_remotechannel_rd_mutex |
| Save password to state | Keep in memory only, clear on disconnect |
| Use globals for instance data | Store in NinjamPlugin struct |
| Call NJClient API from audio thread | Only call AudioProc() |
| Hold multiple locks | Single lock at a time (except NJClient internal pair) |
| Allocate memory in audio thread | Pre-allocate buffers |

---

## 10. Command Queue Pattern (r36)

### 10.1 UI→Run Thread Commands

```cpp
// ui_command.h - command variants
using UiCommand = std::variant<
    ConnectCommand,
    DisconnectCommand,
    SetLocalChannelInfoCommand,
    SetLocalChannelMonitoringCommand,
    SetUserStateCommand,
    SetUserChannelStateCommand,
    RequestServerListCommand
>;

// UI pushes command (non-blocking)
plugin->cmd_queue.try_push(ConnectCommand{server, username, password});

// Run thread drains and executes
plugin->cmd_queue.drain([&](UiCommand&& cmd) {
    std::visit([&](auto&& c) {
        if constexpr (std::is_same_v<T, ConnectCommand>) {
            client->Connect(c.server.c_str(), c.username.c_str(), c.password.c_str());
        }
    }, std::move(cmd));
});
```

### 10.2 Remote User Direct Read Pattern (ReaNINJAM-style)

```cpp
// UI renders remote users by reading NJClient getters under client_mutex.
std::unique_lock<std::mutex> lock(plugin->client_mutex);
NJClient* client = plugin->client.get();
if (client) {
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
```

### 10.3 Thread Ownership Summary

| Thread | Owns | Never Touches |
|--------|------|---------------|
| Audio | `AudioProc()` only | Mutex locks, NJClient state |
| Run | `NJClient::Run()`, cmd_queue drain, status/BPM updates | UI rendering |
| UI | UI rendering, cmd_queue push, NJClient getters under `client_mutex` | NJClient mutating calls |

---

## 11. Server List Fetcher Pattern

### 11.1 Async HTTP with JNetLib

```cpp
class ServerListFetcher {
    JNL_HTTPGet m_http;
    bool m_active = false;
    
    void request(const std::string& url);
    bool poll(ServerListResult& out);
};
```

### 11.2 Server List Event Flow

```
UI: Click Refresh → push RequestServerListCommand
      ↓
Run Thread: drain() → ServerListFetcher::request(url)
      ↓
Run Thread: poll() each iteration until complete
      ↓
Run Thread: push ServerListEvent{servers, error}
      ↓
UI: drain() → update ui_state.public_servers
```

---

## 12. Plugin Lifetime Pattern (shared_ptr Keepalive)

### 12.1 Problem
Run thread held raw pointer to plugin. If host destroyed plugin while thread running → use-after-free crash.

### 12.2 Solution
```cpp
// run_thread.h
void run_thread_start(NinjamPlugin* plugin, std::shared_ptr<NinjamPlugin> keepalive);

// run_thread.cpp
void run_thread_func(std::shared_ptr<NinjamPlugin> plugin) {
    while (!plugin->shutdown.load()) {
        // ... work with plugin->client, etc.
    }
}
```

### 12.3 Ownership Chain
```
Host owns plugin instance (via CLAP descriptor)
  ↓
Plugin factory creates shared_ptr<NinjamPlugin>
  ↓
Run thread holds shared_ptr copy (keepalive)
  ↓
Plugin cannot be destroyed until run thread exits
```
