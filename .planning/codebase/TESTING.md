# Testing Patterns

**Analysis Date:** 2026-03-07

## Current Testing Status

**Framework:** CMake option `JAMWIDE_BUILD_TESTS` exists but is set to `OFF` by default

**Status:** No test files detected in the main source tree (`src/` directory)

**Configuration:** `CMakeLists.txt` line 28:
```cmake
option(JAMWIDE_BUILD_TESTS "Build tests" OFF)
```

The option is declared but not acted upon - there are no test targets or test files in the build configuration.

## Testing Infrastructure

**Testing Framework:** Not configured

**Test Runner:** Not configured

**Assertion Library:** Not configured

**Build System Integration:** CMake (primary build system)

## Test Organization Strategy

**Recommended Pattern (not currently implemented):**

Based on the codebase structure, tests should follow this layout if implemented:

```
src/
├── core/
│   ├── njclient.h
│   ├── njclient.cpp
│   └── njclient_test.cpp          # Tests would go here
├── ui/
│   ├── ui_main.h
│   ├── ui_main.cpp
│   └── ui_main_test.cpp
├── threading/
│   ├── spsc_ring.h
│   └── spsc_ring_test.cpp
└── net/
    ├── server_list.h
    ├── server_list.cpp
    └── server_list_test.cpp
```

**Naming Convention:** `[component]_test.cpp` co-located with source files

## Module-by-Module Testing Needs

### Core Module Tests (`src/core/`)

**What exists:**
- `NJClient` class: Main network client for NINJAM protocol
- Protocol message handling (netmsg.h/cpp)
- Audio encoding/decoding (integrates Vorbis codecs)

**What should be tested:**
- Protocol message parsing and building
- Connection state machine
- Audio buffer management and encoding
- Error conditions and recovery

**Current Status:** No tests. Legacy code from Cockos NINJAM reference client.

### UI Module Tests (`src/ui/`)

**What exists:**
- `UiState` struct: State container for rendering
- Multiple render functions: `ui_render_frame()`, `ui_render_connection_panel()`, etc.
- Event handling and UI command processing

**What should be tested:**
- State mutations (channel mute/solo, volume changes)
- Event processing in `ui_render_frame()` (currently uses `std::visit` with variant events)
- Null pointer guards and early returns

**Current Status:** No tests. UI relies on ImGui immediate mode rendering.

### Threading Module Tests (`src/threading/`)

**What exists:**
- `SpscRing<T, N>` template: Lock-free single-producer single-consumer ring buffer
- `UiCommand` variant type: Commands from UI → Run thread
- `UiEvent` variant type: Events from Run → UI thread

**What should be tested:**
- SpscRing capacity and overflow behavior
- Push/pop on empty and full conditions
- Multi-threaded producer/consumer stress tests
- Variant dispatch correctness with std::visit

**Current Status:** No tests. SpscRing is production code with careful memory ordering.

### Network Module Tests (`src/net/`)

**What exists:**
- `ServerList` class: Fetches and parses public NINJAM server lists
- Both NINJAM native format and JSON format parsing

**What should be tested:**
- Parsing of server list responses (valid and malformed)
- Error handling for network failures
- JSON and native format parsers independently

**Current Status:** No tests.

## Testing Patterns (To Be Established)

### Lock-Free Data Structure Testing

**SpscRing Pattern (recommended):**

```cpp
#include <thread>
#include <cassert>
#include "threading/spsc_ring.h"

void test_spsc_ring_basic() {
    SpscRing<int, 4> ring;

    // Test push
    assert(ring.try_push(42));
    assert(!ring.empty());
    assert(ring.size() == 1);

    // Test pop
    auto val = ring.try_pop();
    assert(val.has_value());
    assert(val.value() == 42);
    assert(ring.empty());
}

void test_spsc_ring_overflow() {
    SpscRing<int, 2> ring;

    assert(ring.try_push(1));
    assert(ring.try_push(2));
    assert(!ring.try_push(3));  // Overflow
}

void test_spsc_ring_threaded() {
    SpscRing<int, 256> ring;

    std::thread producer([&]() {
        for (int i = 0; i < 100; ++i) {
            while (!ring.try_push(i)) {
                // Busy-wait if full
            }
        }
    });

    std::thread consumer([&]() {
        int count = 0;
        while (count < 100) {
            if (auto val = ring.try_pop()) {
                count++;
            }
        }
    });

    producer.join();
    consumer.join();
}
```

### State Mutation Testing

**UiState Pattern (recommended):**

```cpp
#include "ui/ui_state.h"

void test_ui_state_initialization() {
    UiState state;

    assert(state.status == -1);  // NJC_STATUS_DISCONNECTED
    assert(state.bpm == 0.0f);
    assert(state.bpi == 0);
    assert(state.local_transmit == true);
    assert(state.local_volume == 1.0f);
    assert(std::string(state.server_input) == "");
}

void test_ui_state_solo_logic() {
    UiState state;
    state.local_solo = true;
    assert(state.any_solo_active == false);  // Updated by ui_update_solo_state()

    // After calling ui_update_solo_state(plugin):
    // assert(state.any_solo_active == true);
}

void test_ui_remote_user_initialization() {
    UiRemoteUser user;

    assert(user.name.empty());
    assert(user.address.empty());
    assert(user.mute == false);
    assert(user.channels.empty());
}
```

### Protocol Parsing Testing

**NetMessage Pattern (recommended):**

Since NJClient and netmsg.h are legacy code, tests should focus on message marshaling:

```cpp
#include "core/netmsg.h"

void test_message_type_roundtrip() {
    Net_Message msg;
    msg.set_type(MESSAGE_KEEPALIVE);
    assert(msg.get_type() == MESSAGE_KEEPALIVE);
}

void test_message_size_management() {
    Net_Message msg;
    msg.set_size(1024);
    assert(msg.get_size() == 1024);
}

void test_message_header_parsing() {
    char buffer[16];
    Net_Message msg;

    // Construct a valid header
    int header_len = msg.makeMessageHeader(buffer);
    assert(header_len > 0);

    // Parse it back
    Net_Message msg2;
    int parsed = msg2.parseMessageHeader(buffer, header_len);
    assert(parsed == header_len);
}
```

### Variant Dispatch Testing

**UiCommand Pattern (recommended):**

```cpp
#include "threading/ui_command.h"
#include <variant>

void test_ui_command_variant() {
    UiCommand cmd = ConnectCommand{
        .server = "example.com",
        .username = "testuser",
        .password = "pass123"
    };

    bool matched = false;
    std::visit([&](auto&& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, ConnectCommand>) {
            assert(c.server == "example.com");
            assert(c.username == "testuser");
            matched = true;
        }
    }, cmd);

    assert(matched);
}

void test_ui_event_variant() {
    UiEvent event = StatusChangedEvent{
        .status = NJClient::NJC_STATUS_OK,
        .error_msg = ""
    };

    bool matched = false;
    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, StatusChangedEvent>) {
            assert(e.status == NJClient::NJC_STATUS_OK);
            matched = true;
        }
    }, event);

    assert(matched);
}
```

## What NOT to Test

**Audio Processing (AudioProc):**
- Called from audio thread with strict real-time constraints
- Integration with host requires live plugin testing
- Use plugin host test suite instead (Waveform, Reaper, etc.)

**CLAP Boilerplate:**
- Parameter marshaling between host and plugin
- Event I/O with host
- Test via compliance checker (clap-validator)

**ImGui Rendering:**
- Immediate mode UI doesn't lend itself to unit tests
- Functional/UI testing requires manual testing or screenshot regression
- Focus tests on state mutations that feed into ImGui

**Network I/O:**
- Server list fetching uses actual HTTP
- Consider mocking external HTTP calls if testing server list parsing

## Coverage Gaps

**Critical missing tests:**
1. **SpscRing thread safety** - Lock-free data structure correctness
2. **NJClient protocol state machine** - Complex network state transitions
3. **Variant dispatch correctness** - Event/command processing
4. **UI state mutations** - Channel muting, soloing, volume changes
5. **Server list parsing** - JSON and native format parsing

**Why not tested:**
- Project is in active development (CMake option exists but disabled)
- Real-time audio plugin constraints make testing complex
- No test infrastructure was bootstrapped initially
- Focus has been on functionality over test coverage

## Recommended First Test Suite

**Priority 1 (Foundation):**
- `threading/spsc_ring_test.cpp` - Lock-free queue correctness
- `ui/ui_state_test.cpp` - State initialization and mutations

**Priority 2 (Core):**
- `core/netmsg_test.cpp` - Message marshaling roundtrips
- `threading/ui_command_test.cpp` - Variant dispatch patterns

**Priority 3 (Integration):**
- `net/server_list_test.cpp` - Parsing edge cases
- `ui/ui_render_test.cpp` - State-to-render correctness (harder due to ImGui)

## Build Integration (Future)

**Recommended CMakeLists addition:**

```cmake
if(JAMWIDE_BUILD_TESTS)
    enable_testing()

    # Test executable
    add_executable(jamwide_tests
        src/core/netmsg_test.cpp
        src/threading/spsc_ring_test.cpp
        src/threading/ui_command_test.cpp
        src/ui/ui_state_test.cpp
        src/net/server_list_test.cpp
    )

    # Link test executable against core libraries
    target_link_libraries(jamwide_tests PRIVATE
        njclient
        jamwide-threading
        jamwide-ui
        wdl
    )

    # Register tests
    add_test(NAME JamWideTests COMMAND jamwide_tests)
endif()
```

**Run tests:**
```bash
cmake -DJAMWIDE_BUILD_TESTS=ON -B build
cmake --build build
ctest --output-on-failure
```

---

*Testing analysis: 2026-03-07*
