# Codebase Structure

**Analysis Date:** 2026-03-07

## Directory Layout

```
JamWide/
├── src/                    # Source code (C++, Objective-C++, C)
│   ├── core/               # NINJAM client core (protocol, encoding, networking)
│   ├── plugin/             # CLAP plugin entry point and wrapper
│   ├── platform/           # OS-specific GUI (Metal/macOS, D3D11/Windows)
│   ├── threading/          # Inter-thread communication (SPSC ring, commands, events)
│   ├── ui/                 # Dear ImGui UI panels and state
│   ├── net/                # Public server list fetcher
│   ├── debug/              # Logging utilities
│   ├── third_party/        # Embedded third-party headers (picojson)
│   ├── build_number.h      # Generated version info
│   └── [header files]      # Top-level includes
├── wdl/                    # WDL library (Cockos; jnetlib, SHA, RNG, UTF8 wrapper)
├── libs/                   # Git submodules
│   ├── clap/               # CLAP API headers
│   ├── clap-helpers/       # CLAP plugin wrapper helpers
│   ├── clap-wrapper/       # VST3/AU wrapper around CLAP
│   ├── imgui/              # Dear ImGui source
│   ├── libogg/             # OGG container library
│   └── libvorbis/          # Vorbis audio codec
├── cmake/                  # CMake utility modules
├── .github/                # GitHub Actions CI/CD workflows
├── docs/                   # Documentation (Markdown)
├── resources/              # Plugin resources (icons, metadata)
├── tools/                  # Utility scripts (ImGui ID checker, etc.)
├── memory-bank/            # Design docs and planning files
├── .planning/              # GSD codebase mapping output
├── .vscode/                # VS Code settings
├── build/                  # Build artifacts (generated)
├── CMakeLists.txt          # Root CMake configuration
├── README.md               # Project overview
├── CHANGELOG.md            # Release notes
├── LICENSE                 # GPLv2
├── install.sh              # macOS install script
├── install-win.ps1         # Windows install script
└── release.sh              # Release automation script
```

## Directory Purposes

**src/core/**
- Purpose: NINJAM client protocol implementation, OGG Vorbis codec integration, network message handling
- Contains: njclient.cpp/h (~2500 lines, original Cockos code), netmsg.cpp/h, mpb.cpp/h (metronome/beat)
- Key files: `njclient.h` (main client API), `njclient.cpp` (state machine, network loop)
- Dependency: wdl (for networking), libogg, libvorbis

**src/plugin/**
- Purpose: CLAP plugin interface, parameter handling, plugin lifecycle
- Contains: clap_entry.cpp (~1500 lines, full CLAP implementation), jamwide_plugin.h (plugin instance struct)
- Key files: `clap_entry.cpp` (descriptor, callbacks, process loop), `jamwide_plugin.h` (central data structure)
- Exports: Plugin descriptor for clap-wrapper

**src/platform/**
- Purpose: Platform-specific window creation, graphics API binding, ImGui backend selection
- Contains: gui_context.h (abstract interface), gui_macos.mm (Metal on macOS), gui_win32.cpp (D3D11 on Windows)
- Key files: `gui_context.h` (virtual interface), `gui_macos.mm` (~400 lines Objective-C++), `gui_win32.cpp` (~600 lines)
- Dependency: imgui (core + platform backends)

**src/threading/**
- Purpose: Lock-free inter-thread communication, command queue, event queue
- Contains: run_thread.cpp/h (network thread management), ui_command.h (command types), ui_event.h (event types), spsc_ring.h (lock-free queue template)
- Key files: `spsc_ring.h` (template), `ui_command.h` (8 command variants), `ui_event.h` (5 event variants)
- Generated types: std::variant-based enums for type-safe dispatch

**src/ui/**
- Purpose: Dear ImGui UI panels, state management, user interaction
- Contains: 13 UI component files (ui_*.cpp/h), ui_state.h (unified state struct), server_list_types.h
- Key files: `ui_main.cpp` (top-level frame renderer), `ui_state.h` (90-member state struct), `ui_status.cpp`, `ui_connection.cpp`, `ui_chat.cpp`, `ui_local.cpp`, `ui_remote.cpp`, `ui_master.cpp`, `ui_server_browser.cpp`, `ui_latency_guide.cpp`, `ui_meters.cpp`
- Dependency: imgui, threading layer (for events)

**src/net/**
- Purpose: Public server list fetching and parsing
- Contains: server_list.cpp/h (HTTP GET via jnetlib, JSON/NINJAM format parsing)
- Key files: `server_list.h` (ServerListFetcher class), `server_list.cpp` (dual format parser)

**src/debug/**
- Purpose: Development-time logging infrastructure
- Contains: logging.h (header-only logging macros)
- Key files: `logging.h` (NLOG, NLOG_VERBOSE macros, file output to /tmp or ~/Library/Logs)

**src/third_party/**
- Purpose: Lightweight JSON parsing
- Contains: picojson.h (header-only, used for server list parsing)

**wdl/**
- Purpose: Cockos WDL library (networking, hashing, random, UTF8)
- Contains: jnetlib/ (async DNS, TCP connection, HTTP GET), sha.cpp/h, rng.cpp/h, win32_utf8.c/h
- Dependency: Platform APIs (Windows.h on Win32, POSIX on Unix)

**libs/** (Git Submodules)
- clap/ — CLAP 1.1 plugin API headers
- clap-helpers/ — CLAP helper macros and plugin-side utilities
- clap-wrapper/ — Wrapper for VST3/AU from CLAP implementation
- imgui/ — Dear ImGui (core + backends for Win32/D3D11/macOS/Metal)
- libogg/ — OGG container (required by libvorbis)
- libvorbis/ — Vorbis codec (for audio encoding/decoding)

**build/** (Generated at Configure/Build)
- JamWide.clap — CLAP plugin bundle (macOS and Windows)
- JamWide.vst3 — VST3 plugin (generated by clap-wrapper)
- JamWide.component — Audio Unit v2 (macOS only)
- CLAP/ (Windows) — Build directory for CLAP
- Release/ (Windows) — MSBuild output directory

**docs/**
- Purpose: User and developer documentation (Markdown)
- Contents: Feature guides, troubleshooting, API docs, architecture notes

**memory-bank/**
- Purpose: Design documents, planning notes, decision records
- Contents: Plan files (FLAC integration, JUCE porting, etc.), workspace file, notes

**tools/**
- Purpose: Build/development utility scripts
- Contents: check_imgui_ids.py (ImGui label collision detection)

**cmake/**
- Purpose: CMake helper modules (platform detection, compiler flags)

**.planning/codebase/**
- Purpose: Generated by GSD mapping commands
- Contents: ARCHITECTURE.md, STRUCTURE.md, CONVENTIONS.md, TESTING.md, CONCERNS.md, STACK.md, INTEGRATIONS.md

## Key File Locations

**Entry Points:**

- `src/plugin/clap_entry.cpp`: CLAP plugin descriptor and all plugin callbacks (init, destroy, activate, process, parameters, GUI, extensions)
- `src/plugin/clap_entry_export.cpp`: Minimal export wrapper (entry_factory)
- `src/threading/run_thread.cpp`: Run thread loop spawning and shutdown

**Configuration:**

- `CMakeLists.txt`: Root CMake build configuration; defines all targets, submodules, compiler flags, platform-specific sources
- `cmake/` subdirectory: Helper modules for CMake configuration

**Core Logic:**

- `src/core/njclient.cpp`: Main NINJAM client state machine, protocol handler, message parsing, audio mixing
- `src/core/netmsg.cpp`: Network message framing and parsing
- `src/core/mpb.cpp`: Metronome and beat tracking

**Testing:**

- `tests/` directory: None present; JAMWIDE_BUILD_TESTS CMake option available but no test suite implemented

**Plugin State:**

- `src/plugin/jamwide_plugin.h`: Central plugin instance struct; holds all mutexes, queues, NJClient, GUI context, parameters, state

## Naming Conventions

**Files:**

- Source files: snake_case (e.g., `ui_connection.cpp`, `server_list.h`)
- Component headers: ui_*.h for UI panels (one per component)
- Platform headers: gui_*.h/mm/cpp for platform layers
- Core library headers: simple names (njclient.h, netmsg.h)

**Directories:**

- Functional grouping: core/, plugin/, ui/, platform/, threading/, net/, debug/
- Submodules: libs/ (external), wdl/ (vendored Cockos library)
- Build output: build/ (ignored in git)

**Types:**

- Classes/structs: PascalCase (e.g., `NJClient`, `UiState`, `GuiContext`, `SpscRing`)
- Enums: CamelCase members (e.g., `ChatMessageType::Message`, `NJC_STATUS_OK`)
- Namespaces: lowercase (e.g., `jamwide::`)

**Functions/Methods:**

- Public API: snake_case (e.g., `jamwide_plugin_create()`, `ui_render_frame()`)
- Member methods: camelCase (e.g., `setParent()`, `render()`)
- Internal: snake_case prefixed with underscore or `static`

**Identifiers:**

- Parameters: lowercase_snake_case
- Members: lowercase_snake_case or `m_name` (WDL style in core)
- Constants: UPPER_CASE (e.g., `NET_MESSAGE_MAX_SIZE`, `PARAM_MASTER_VOLUME`)
- Atomics: same as regular members; `std::atomic<T>` type signals thread-safe access

## Where to Add New Code

**New Feature (e.g., recording, plugin chaining):**
- Core logic: `src/core/` (extends NJClient if protocol-related) or new `src/feature/` subdirectory
- UI panels: `src/ui/ui_feature.cpp` + `src/ui/ui_feature.h`
- Commands: Add variant to `src/threading/ui_command.h`
- Events: Add variant to `src/threading/ui_event.h` if Run thread generates status
- Tests: `tests/feature_test.cpp` (if test suite is added)

**New Component/Module:**
- Implementation: `src/[category]/module_name.cpp` + `src/[category]/module_name.h`
- Namespace: Place in `namespace jamwide` unless part of core (njclient) or WDL
- Dependencies: List includes in header, link in CMakeLists.txt target

**Utilities:**
- Shared helpers: `src/util/` (not yet present; create if many shared utilities needed)
- Platform-specific: `src/platform/`
- Debug utilities: `src/debug/`

**Platform-Specific Code:**

- macOS: `src/platform/gui_macos.mm`, conditional compilation with `#ifdef __APPLE__`
- Windows: `src/platform/gui_win32.cpp`, conditional compilation with `#ifdef _WIN32`
- Cross-platform fallback: Implement both, build system selects appropriate version

## Special Directories

**wdl/**
- Purpose: Vendored Cockos WDL library
- Generated: No (pre-built, committed to repo)
- Committed: Yes (full source included)
- Note: Not a git submodule; included as source trees; pre-modified for project use

**libs/**
- Purpose: Git submodules (CLAP, ImGui, libogg, libvorbis, etc.)
- Generated: No (external repos)
- Committed: No (references only in .gitmodules; submodule directories are git submodules)
- Update: `git submodule update --init --recursive`

**build/**
- Purpose: CMake build artifacts
- Generated: Yes (cmake --build)
- Committed: No (.gitignore excludes build/)
- Contains: Compiled object files, libraries, plugin bundles

**memory-bank/**
- Purpose: Design documentation and planning
- Generated: No (manually maintained)
- Committed: Yes (tracked in git)
- Contents: Architecture decisions, feature plans, notes

**.planning/codebase/**
- Purpose: GSD mapping output (auto-generated by `/gsd:map-codebase` command)
- Generated: Yes (by mapper agent)
- Committed: Yes (as documentation for consistency)
- Contents: ARCHITECTURE.md, STRUCTURE.md, CONVENTIONS.md, TESTING.md, CONCERNS.md, STACK.md, INTEGRATIONS.md

## CMake Target Structure

```
jamwide                    # Main CLAP plugin (via clap-wrapper)
├── jamwide-impl           # Plugin implementation (static lib)
│   ├── njclient           # NINJAM client core
│   ├── jamwide-threading  # Run thread, command queue
│   ├── jamwide-ui         # ImGui panels
│   ├── imgui              # Dear ImGui
│   ├── clap               # CLAP API
│   ├── clap-helpers       # CLAP helpers
│   └── platform-specific GUI (macOS or Windows)
│       ├── Metal/D3D11 graphics
│       └── ImGui backends
│
├── jamwide_clap           # CLAP plugin bundle (macOS .clap or Windows .clap)
├── jamwide_vst3           # VST3 wrapper (via clap-wrapper)
└── jamwide_au             # Audio Unit v2 (macOS only)

Dependencies (libs):
├── wdl (jnetlib, SHA, RNG, UTF8)
├── libogg
├── libvorbis
├── imgui + backends
├── clap
└── clap-wrapper
```

---

*Structure analysis: 2026-03-07*
