---
phase: 02-juce-scaffolding
plan: 01
subsystem: infra
tags: [juce, cmake, vst3, au, clap, standalone, audio-plugin, multi-bus]

# Dependency graph
requires:
  - phase: 01-flac-codec
    provides: "njclient static library with FLAC codec support"
provides:
  - "JUCE 8.0.12 framework submodule at libs/juce"
  - "clap-juce-extensions submodule at libs/clap-juce-extensions"
  - "JamWideJuce plugin target (VST3, AU, Standalone, CLAP formats)"
  - "AudioProcessor with 4 stereo inputs, 17 stereo outputs"
  - "APVTS with masterVol, masterMute, metroVol, metroMute parameters"
  - "Versioned state serialization (stateVersion=1)"
  - "JAMWIDE_BUILD_JUCE / JAMWIDE_BUILD_CLAP CMake guards"
affects: [03-njclient-integration, 04-core-ui-panels, 05-mixer-ui, 06-routing]

# Tech tracking
tech-stack:
  added: [JUCE 8.0.12, clap-juce-extensions]
  patterns: [juce_add_plugin, BusesProperties multi-bus, APVTS parameter management, versioned state serialization, CMake build guards]

key-files:
  created:
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - juce/JamWideJuceEditor.h
    - juce/JamWideJuceEditor.cpp
  modified:
    - CMakeLists.txt

key-decisions:
  - "JUCE 8.0.12 (not JUCE 9 which is unreleased) pinned as git submodule"
  - "Plugin identity: JmWd/JwJc/com.jamwide.juce-client separate from existing CLAP plugin"
  - "State version 1 with forward-compatible migration pattern"
  - "juce_opengl included in link but OpenGLContext NOT attached (deferred to Phase 4)"

patterns-established:
  - "JAMWIDE_BUILD_JUCE / JAMWIDE_BUILD_CLAP guards for coexistence"
  - "BusesProperties multi-bus: 4 stereo in + 17 stereo out (main mix + 16 routing)"
  - "APVTS with ParameterID version 1 for all host-exposed parameters"
  - "Versioned XML state with stateVersion property for migration"
  - "Ad-hoc codesigning post-build for macOS AU validation"

requirements-completed: [JUCE-01, JUCE-02]

# Metrics
duration: 12min
completed: 2026-03-07
---

# Phase 2 Plan 1: JUCE Scaffolding Summary

**JUCE 8.0.12 plugin skeleton with VST3/AU/Standalone/CLAP builds, 4-in/17-out multi-bus layout, APVTS parameters, and versioned state serialization**

## Performance

- **Duration:** 12 min
- **Started:** 2026-03-07T13:22:16Z
- **Completed:** 2026-03-07T13:34:19Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- JUCE 8.0.12 and clap-juce-extensions added as git submodules, CMake configured with JAMWIDE_BUILD_JUCE/JAMWIDE_BUILD_CLAP guards
- JamWideJuce plugin builds as VST3, AU, Standalone, and CLAP from single CMake configuration
- Multi-bus layout: 4 stereo inputs (Local 1-4) + 17 stereo outputs (Main Mix + Remote 1-16)
- APVTS with masterVol, masterMute, metroVol, metroMute parameters and versioned state serialization (stateVersion=1)
- Existing CLAP/ImGui build verified fully functional with JAMWIDE_BUILD_CLAP=ON

## Task Commits

Each task was committed atomically:

1. **Task 1: Add JUCE and clap-juce-extensions submodules, configure CMake** - `2b15f7b` (feat)
2. **Task 2: Create AudioProcessor with multi-bus layout and minimal Editor** - `0b58e2e` (feat)

## Files Created/Modified
- `libs/juce` - JUCE 8.0.12 framework submodule
- `libs/clap-juce-extensions` - CLAP format support for JUCE plugin
- `CMakeLists.txt` - JAMWIDE_BUILD_JUCE option, juce_add_plugin, clap_juce_extensions_plugin, build guards, codesigning
- `juce/JamWideJuceProcessor.h` - AudioProcessor subclass with BusesProperties, APVTS, state serialization declarations
- `juce/JamWideJuceProcessor.cpp` - Full AudioProcessor implementation: processBlock (silence), isBusesLayoutSupported, versioned state
- `juce/JamWideJuceEditor.h` - Minimal AudioProcessorEditor placeholder declaration
- `juce/JamWideJuceEditor.cpp` - Placeholder editor with 800x600 label "Phase 2 Scaffold"

## Decisions Made
- Used JUCE 8.0.12 (latest stable) since JUCE 9 is not yet released, matching research recommendation
- Plugin identity uses separate codes (JmWd/JwJc) from existing CLAP plugin (JMWD/JWAU) for DAW coexistence
- State version initialized at 1 with forward-compatible migration pattern from PhaseGrid reference
- OpenGL module linked but context NOT attached per research anti-pattern (VST3 scaling bugs), deferred to Phase 4
- Shared libraries (wdl, njclient, ogg, vorbis, FLAC) kept outside both build guards for dual-target linking

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- JUCE plugin skeleton ready for NJClient integration (Phase 3)
- AudioProcessor processBlock is empty (produces silence) -- ready for NJClient::AudioProc wiring
- APVTS parameters defined -- ready for UI binding in Phase 4
- Bus layout locked (4 in, 17 out) -- ready for routing wiring in Phase 6
- Existing CLAP build unaffected, both targets can be built simultaneously

## Self-Check: PASSED

All created files verified present. Both task commits (2b15f7b, 0b58e2e) confirmed in git log. All four build artifacts (VST3, AU, Standalone, CLAP) verified on disk.

---
*Phase: 02-juce-scaffolding*
*Completed: 2026-03-07*
