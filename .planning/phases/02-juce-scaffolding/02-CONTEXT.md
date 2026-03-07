# Phase 2: JUCE Scaffolding - Context

**Gathered:** 2026-03-07
**Status:** Ready for planning

<domain>
## Phase Boundary

Plugin skeleton with AudioProcessor, thread architecture, multi-bus declaration, and CI. The JUCE project builds as VST3, AU, CLAP, and standalone, passes pluginval, and runs with correct architecture in place. No audio processing or UI panels yet — those are Phases 3-5.

</domain>

<decisions>
## Implementation Decisions

### JUCE Licensing & Version
- JUCE GPL license — project is already GPLv2+, fully compatible, no splash screen
- Target JUCE 9 (latest release)
- Git submodule in libs/juce, consistent with existing dependency pattern (libogg, libvorbis, libflac)
- Use juce_add_plugin CMake API for format target generation

### Plugin Formats & Identity
- Output formats: VST3, AU, standalone, and CLAP (via juce_clap_extensions)
- New plugin identity during transition: "JamWide JUCE" (temporary name to coexist with current CLAP plugin in DAW plugin lists)
- Different plugin ID from current CLAP plugin so both can be loaded simultaneously
- Rename back to "JamWide" after Phase 5 when old code is removed

### Output Bus Layout
- 16+1 stereo output pairs (34 channels total)
- Bus 0 = main mix (all remote users summed), always present
- Buses 1-16 = individual remote user/channel routing (wired in Phase 6)
- Metronome uses one of the 16 routing slots (bus 16 by default), not a separate dedicated pair

### Input Bus Layout
- 4 stereo input buses (8 channels total)
- Each input bus maps to a separate NINJAM local channel
- Covers typical use case: guitar, mic, keys, aux

### Coexistence with Existing Plugin
- Keep current CLAP/ImGui plugin buildable alongside JUCE version through Phase 5
- JUCE source lives in juce/ at project root (not inside src/)
- Shared CMakeLists.txt with JAMWIDE_BUILD_JUCE / JAMWIDE_BUILD_CLAP options
- Both targets link the same njclient static library (src/core/) — no forking
- Old CLAP/ImGui code removed after Phase 5 (mixer complete, full feature parity)

### Thread Architecture
- NinjamRunThread uses juce::Thread (JUCE-04 requirement)
- JUCE-idiomatic hybrid communication:
  - Run thread → UI: juce::MessageManager::callAsync() / AsyncUpdater (not polled SPSC)
  - UI → Run thread: lock-free queue (SPSC or AbstractFifo)
  - Audio thread ↔ anything: lock-free only (atomics, AbstractFifo) — no MessageManager
  - Parameters: juce::AudioProcessorValueTreeState for host-exposed parameters

### JUCE Module Selection
- Include all likely modules upfront to avoid CMake reconfiguration:
  - juce_core, juce_events, juce_data_structures
  - juce_audio_basics, juce_audio_processors, juce_audio_formats, juce_audio_devices
  - juce_gui_basics, juce_gui_extra
  - juce_opengl (for hardware-accelerated custom vector/SVG UI rendering)
- Skip juce_dsp (NJClient handles all audio processing)

### Custom UI Direction
- Custom vector/SVG-based UI planned (designed via Sketch MCP server)
- juce_opengl included for hardware-accelerated rendering of custom-drawn components
- Custom LookAndFeel deferred to Phase 4 — Phase 2 uses default JUCE look

### CI / Validation
- GitHub Actions CI with pluginval from Phase 2
- Build + pluginval on both macOS and Windows
- Validates VST3, AU (macOS), standalone, and CLAP format compliance

### Standalone Behavior
- Audio device selection via settings dialog / popup window (not in-window panel)
- Remember last-used audio device, sample rate, and buffer size via PropertiesFile
- Auto-select remembered device on launch

### Claude's Discretion
- Exact JUCE 9 tag/version to pin as submodule
- juce_clap_extensions integration approach (submodule vs. vendored)
- Temporary plugin manufacturer code and bundle ID for "JamWide JUCE"
- CMake option defaults (which targets build by default)
- OpenGLContext attachment strategy (top-level vs. per-component)
- PropertiesFile location and format for standalone settings
- CI workflow structure (single workflow vs. separate per-platform)

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/core/njclient.cpp` + `njclient.h`: NJClient core — shared between CLAP and JUCE builds via single njclient static library
- `wdl/`: WDL networking, SHA, RNG — linked transitively via njclient
- `libs/libogg`, `libs/libvorbis`, `libs/libflac`: Audio codec submodules — linked transitively via njclient
- `src/threading/spsc_ring.h`: Lock-free SPSC template — may be reused for audio thread paths (JUCE's AbstractFifo is an alternative)

### Established Patterns
- Git submodules in libs/ with add_subdirectory() and EXCLUDE_FROM_ALL — JUCE submodule follows this pattern
- CMake target structure: static libraries per layer (njclient, jamwide-threading, jamwide-ui, jamwide-impl) — JUCE build adds parallel targets
- CLAP plugin uses make_clapfirst_plugins() — JUCE uses juce_add_plugin() instead
- Universal binary support via CMAKE_OSX_ARCHITECTURES — must also apply to JUCE targets

### Integration Points
- `CMakeLists.txt`: Add JUCE submodule, juce_add_plugin(), conditional JAMWIDE_BUILD_JUCE/CLAP options
- `njclient` static library: Both CLAP and JUCE targets link this — no changes needed to njclient itself
- `.github/workflows/`: New or extended CI workflow for JUCE build + pluginval

</code_context>

<specifics>
## Specific Ideas

- Custom UI will be designed using a Sketch MCP server for layout, with vector/SVG graphics for the interface
- "JamWide JUCE" is a temporary name — revert to "JamWide" after Phase 5 cleanup removes the old CLAP/ImGui code
- Thread communication follows JUCE "from scratch" best practices: MessageManager for non-realtime paths, lock-free for audio thread
- The 16+1 bus layout is conservative compared to ReaNINJAM's 64+1, chosen for better DAW compatibility

</specifics>

<deferred>
## Deferred Ideas

- Custom LookAndFeel with SVG assets — Phase 4 (Core UI Panels)
- CLAP format via native JUCE 9 support (if available) instead of juce_clap_extensions — research during Phase 2 planning
- Old CLAP/ImGui code removal — after Phase 5 (Mixer UI complete)
- FLAC recording format support — revisit when JUCE AudioFormatManager is available

</deferred>

---

*Phase: 02-juce-scaffolding*
*Context gathered: 2026-03-07*
