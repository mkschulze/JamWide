# Coding Conventions

**Analysis Date:** 2026-03-07

## Language & Standard

**Language:** C++20

**Standard:** CMake enforces C++20 as the minimum standard via `CMAKE_CXX_STANDARD 20`

**Configuration:** `CMakeLists.txt` sets standard as required (`CMAKE_CXX_STANDARD_REQUIRED ON`)

## Naming Patterns

**Classes & Structs:**
- PascalCase for class names: `NJClient`, `JamWidePlugin`, `DecodeState`
- PascalCase with leading `I_` for interface classes: `I_NJEncoder`, `I_NJDecoder`
- Lowercase with underscores for C-style structs: `overlapFadeState`, `DecodeMediaBuffer`

**Functions:**
- snake_case for free/module functions: `ui_render_frame()`, `ui_update_solo_state()`, `render_vu_meter()`
- camelCase for class member functions: `GetStatus()`, `GetErrorStr()`, `AudioProc()`, `GetNumUsers()`
- Static helper functions prefixed with `s_` in file scope: `s_descriptor`, `s_features`
- Getter functions use `Get` prefix: `GetErrorStr()`, `GetWorkDir()`, `GetUser()`
- Setter functions use `Set` prefix: `SetKeepAlive()`, `SetWorkDir()`

**Variables:**
- snake_case for local and member variables: `server_input`, `username_input`, `connection_error`
- Prefix with `m_` for private class members: `m_errstr`, `m_workdir`, `m_user`, `m_audio_enable`, `m_hb`
- Prefix with `s_` for static variables: `s_descriptor`, `s_features`
- All lowercase for simple type members: `refcnt`, `rdpos`, `status`

**Constants & Enums:**
- snake_case with `k` prefix for compile-time constants: `kRemoteNameMax`, `kLatencyHistorySize`, `kChatHistorySize`, `kOnBeatThresholdMs`
- UPPER_CASE for preprocessor constants: `NET_MESSAGE_MAX_SIZE`, `NET_CON_MAX_MESSAGES`, `MESSAGE_KEEPALIVE`
- ALL_CAPS for enum values in older code: `NJC_STATUS_DISCONNECTED`, `NJC_STATUS_OK`, `PARAM_MASTER_VOLUME`
- Modern: Use `enum class` with PascalCase: `ChatMessageType::Message`, `ChatMessageType::PrivateMessage`

**File Names:**
- snake_case for source files: `ui_main.cpp`, `ui_connection.h`, `njclient.cpp`
- Paired headers and implementation files: `ui_main.h` / `ui_main.cpp`
- Module headers typically have suffix `_h`: `logging.h`, `ui_state.h`

**Namespace Names:**
- lowercase: `namespace jamwide`, `namespace logging`
- No nested namespaces in practice (mostly `jamwide` or global)

## Include Organization

**Order:**
1. Own header file (if .cpp): `#include "ui_main.h"`
2. C++ Standard Library: `#include <atomic>`, `#include <mutex>`, `#include <string>`
3. External libraries: `#include "clap/clap.h"`, `#include "wdl/queue.h"`
4. Project includes: `#include "plugin/jamwide_plugin.h"`, `#include "core/njclient.h"`
5. Conditional platform includes at end: `#ifdef _WIN32`, `#ifndef _WIN32`

**Path Style:**
- Use relative paths with project root context: `#include "plugin/jamwide_plugin.h"`
- No relative path traversal (no `../`): All includes assume src/ or project root as base
- Header guards use uppercase with underscores: `#ifndef UI_MAIN_H`, `#define UI_MAIN_H`
- Modern pragma once also used: `#pragma once` (in some files like `platform/gui_context.h`)

**Example from `src/ui/ui_main.cpp`:**
```cpp
#include "ui_main.h"                    // Own header
#include "plugin/jamwide_plugin.h"      // Project - plugin module
#include "core/njclient.h"              // Project - core module
#include "ui_status.h"                  // Project - UI module
#include "debug/logging.h"              // Project - debug module
#include "imgui.h"                      // External
#include <chrono>                       // C++ Standard
#include <ctime>                        // C++ Standard
```

## Code Style

**Formatting:**
- No explicit formatter configured (no .clang-format, .editorconfig, or .prettier file)
- Manually follows common C++ style patterns
- 4-space indentation observed in all files
- Braces on same line for functions: `void foo() {`
- Braces on new line for control flow in some contexts
- Spaces around operators: `a + b`, `a = b`

**Line Length:**
- No strict limit enforced
- Generally 80-120 character lines observed
- Long function calls wrapped with indentation

**Comments:**
- C++ style comments (`//`) for inline comments
- C-style block comments (`/* */`) for license headers and detailed explanations
- Detailed file headers with copyright info present

**Example from `src/ui/ui_latency_guide.cpp`:**
```cpp
/*
    JamWide Plugin - ui_latency_guide.cpp
    Visual latency guide widget

    Shows when your playing lands relative to the beat.
    Green = on beat, Yellow = slightly off, Red = way off
*/

#include "ui_latency_guide.h"

namespace {

constexpr float kOnBeatThresholdMs = 10.0f;
constexpr float kSlightlyOffThresholdMs = 25.0f;

void push_transient(UiState& state, float offset) {
    state.latency_history[state.latency_history_index] = offset;
    // ...
}

}  // namespace
```

## Linting & Static Analysis

**Linting:** Not configured - no .eslintrc or equivalent found

**Static Analysis:** None detected in CMakeLists.txt

**Compiler Flags:**
- C++20 standard enforced
- Position independent code: `CMAKE_POSITION_INDEPENDENT_CODE ON`
- Platform-specific defines:
  - Windows: `NOMINMAX` (prevent min/max macros), `WIN32_LEAN_AND_MEAN`
  - All: `JAMWIDE_DEV_BUILD=1` in development builds (controls verbose logging)

## Type Conventions

**Integers:**
- Use `int` for general purposes: channel counts, indices, status codes
- Use `uint32_t` for unsigned values from CLAP API: `clap_id`, frame counts
- Use `size_t` for container indices and sizes: `std::size_t` in SpscRing

**Floating Point:**
- Use `float` for audio samples and UI values: `float left, float right` in meters
- Use `double` for timing and sample rates: `double sample_rate`, `double beat_phase`

**Strings:**
- Modern: `std::string` preferred: `std::string server`, `std::string username`
- Legacy code: C-strings with fixed buffers: `char server_input[256]`
- Mixed approach: UI state uses both fixed-size arrays and std::string

**Memory:**
- Smart pointers in modern code: `std::unique_ptr<NJClient> client`, `std::shared_ptr<JamWidePlugin>`
- Raw pointers for observer patterns: `const clap_plugin_t* clap_plugin`, `GuiContext* gui_context`
- Manual memory management in legacy code (NJClient): `refcnt` patterns with `AddRef()`/`Release()`

## Error Handling

**Patterns:**
- Boolean return codes: Functions return `bool` for success/failure
  - `bool plugin_init(const clap_plugin_t* plugin)` - returns true if success
  - `bool parse_response(const std::string& data, ServerListResult& result)`

- Status codes via return value:
  - `int Run()` - returns nonzero if sleep is OK (0 = keep processing immediately)
  - `int GetStatus()` - returns `NJC_STATUS_*` enum values

- Error strings for details:
  - `const char *GetErrorStr()` - returns human-readable error message
  - Stored in `std::string connection_error` in UI state

- Early returns for null checks:
  ```cpp
  void ui_render_frame(JamWidePlugin* plugin) {
      if (!plugin) {
          return;  // Guard early return
      }
  ```

- Optional return for optional data:
  - `std::optional<T> try_pop()` in SpscRing for lock-free queues

**Logging:**
- Macro-based: `NLOG()` for important messages, `NLOG_VERBOSE()` for dev builds
- Uses printf-style format strings
- See `src/debug/logging.h` for implementation details

## Concurrency

**Threading Model:**
- Audio thread (real-time): Calls `AudioProc()` - must not block
- UI thread: Reads UI state and renders frames
- Run thread: Processes NJClient state and network I/O
- Main thread: CLAP host interaction

**Synchronization:**
- `std::mutex` for mutual exclusion: `state_mutex`, `client_mutex`
- `std::atomic<T>` for lock-free communication: config parameters, status flags
- Lock-free ring buffer: `SpscRing<T, N>` for thread-safe event queues
- `std::condition_variable` for blocking waits: license dialog synchronization

**Lock-Free Data Structures:**
- `std::atomic<float>` for parameter values: `config_metronome`, `param_master_volume`
- `std::atomic<int>` for status: `cached_status`, `bpi`, `interval_position`
- `SpscRing<UiEvent, 256>` for event queues between threads

**Memory Order:**
- `std::memory_order_relaxed` for independent values that don't synchronize
- `std::memory_order_acquire` for reading synchronization points
- `std::memory_order_release` for writing synchronization points
- Example from `spsc_ring.h`:
  ```cpp
  buffer_[head] = std::move(value);
  head_.store(next_head, std::memory_order_release);
  ```

## Variant & Pattern Matching

**Modern C++ Features:**
- `std::variant` for tagged union of commands/events: `using UiCommand = std::variant<ConnectCommand, DisconnectCommand, ...>`
- `std::visit` with generic lambdas for pattern matching:
  ```cpp
  std::visit([&](auto&& e) {
      using T = std::decay_t<decltype(e)>;
      if constexpr (std::is_same_v<T, StatusChangedEvent>) {
          // Handle StatusChangedEvent
      }
  }, std::move(event));
  ```

## Module Structure

**Plugin Module** (`src/plugin/`):
- Implements CLAP plugin entry point
- Contains main plugin instance definition: `JamWidePlugin`
- No testing code

**Core Module** (`src/core/`):
- NINJAM network protocol implementation (legacy from Cockos)
- Contains `NJClient` class and protocol messages
- Mix of C and C++ styles (original codebase)

**UI Module** (`src/ui/`):
- ImGui-based user interface
- Render functions follow pattern: `void ui_render_*(...)`
- State management in `UiState` struct

**Threading Module** (`src/threading/`):
- Lock-free primitives and event definitions
- `SpscRing` template for queues
- Command/Event types for thread communication

**Network Module** (`src/net/`):
- Server list fetching and parsing
- Separate from core NINJAM client

**Debug Module** (`src/debug/`):
- Logging utilities and configuration
- Conditional compilation for dev builds

## Coding Practices

**Null Pointer Guards:**
- Always check for null before use:
  ```cpp
  if (!plugin) return;
  NJClient* client = plugin->client.get();
  if (client) { /* use client */ }
  ```

**Initialization:**
- Default member initialization in struct definitions:
  ```cpp
  struct UiRemoteChannel {
      std::string name;
      int channel_index = -1;  // Default -1
      bool subscribed = true;  // Default true
      float volume = 1.0f;     // Default 1.0
  };
  ```

**Array Initialization:**
- Use `std::array` for fixed-size arrays: `std::array<ChatMessage, kChatHistorySize> chat_history{};`
- Use `std::vector` for dynamic arrays: `std::vector<UiRemoteUser> remote_users;`

**Move Semantics:**
- Pass temporary objects by rvalue reference for efficiency:
  ```cpp
  bool try_push(T&& value) {
      buffer_[head] = std::move(value);
  }
  ```

**Const Correctness:**
- Const member functions in query methods: `const char *GetErrorStr() const`
- Const references for read-only parameters: `const UiState& state`
- Const pointers where appropriate: `const clap_plugin_t* clap_plugin`

---

*Convention analysis: 2026-03-07*
