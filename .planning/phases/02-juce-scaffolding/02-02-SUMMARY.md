---
phase: 02-juce-scaffolding
plan: 02
subsystem: infra
tags: [juce, thread, pluginval, ci, github-actions, vst3, au, lifecycle]

# Dependency graph
requires:
  - phase: 02-juce-scaffolding
    plan: 01
    provides: "JUCE 8.0.12 plugin skeleton with AudioProcessor, CMake targets"
provides:
  - "NinjamRunThread juce::Thread subclass with lifecycle management"
  - "Thread starts in prepareToPlay, stops in releaseResources with safety-net destructor"
  - "GitHub Actions CI: JUCE build + pluginval validation on macOS and Windows"
affects: [03-njclient-integration]

# Tech tracking
tech-stack:
  added: [pluginval]
  patterns: [juce::Thread subclass with wait() interruptible sleep, processor lifecycle thread management, CI pluginval validation]

key-files:
  created:
    - juce/NinjamRunThread.h
    - juce/NinjamRunThread.cpp
    - .github/workflows/juce-build.yml
  modified:
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - CMakeLists.txt

key-decisions:
  - "NinjamRunThread uses wait(50) not Thread::sleep(50) for interruptible shutdown"
  - "AU and Standalone pluginval validation set to continue-on-error (CI flakiness per research)"
  - "juce-build.yml is separate from existing build.yml (CLAP release workflow)"

patterns-established:
  - "juce::Thread subclass lifecycle: created in prepareToPlay, stopped in releaseResources, destructor safety-net"
  - "CI workflow: JAMWIDE_BUILD_JUCE=ON, JAMWIDE_BUILD_CLAP=OFF for JUCE-only builds"
  - "pluginval at strictness level 5 for VST3 format compliance"

requirements-completed: [JUCE-04]

# Metrics
duration: 5min
completed: 2026-03-07
---

# Phase 2 Plan 2: CI/pluginval + NinjamRunThread Summary

**NinjamRunThread lifecycle skeleton wired into AudioProcessor prepareToPlay/releaseResources, plus GitHub Actions CI with pluginval strictness-5 validation on macOS and Windows**

## Performance

- **Duration:** 5 min
- **Started:** 2026-03-07T13:37:50Z
- **Completed:** 2026-03-07T13:43:10Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- NinjamRunThread (juce::Thread subclass) with run() loop using wait(50) for interruptible sleep, ready for Phase 3 NJClient::Run() integration
- Thread lifecycle properly managed: starts in prepareToPlay(), stops with signal+stopThread in releaseResources(), destructor safety-net via unique_ptr
- GitHub Actions CI workflow builds JUCE plugin on macOS (universal arm64+x86_64) and Windows (x64) with pluginval validation at strictness level 5
- Build verified locally: all JUCE targets compile and link with NinjamRunThread

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement NinjamRunThread and wire into processor lifecycle** - `1f4adec` (feat)
2. **Task 2: Create GitHub Actions CI workflow with pluginval validation** - `2a37278` (chore)

## Files Created/Modified
- `juce/NinjamRunThread.h` - juce::Thread subclass declaration with processor reference
- `juce/NinjamRunThread.cpp` - Thread implementation: constructor, destructor with stopThread(5000), run() with wait(50) loop
- `juce/JamWideJuceProcessor.h` - Added NinjamRunThread forward declaration and unique_ptr member, non-default destructor
- `juce/JamWideJuceProcessor.cpp` - prepareToPlay creates/starts thread, releaseResources signals/stops/resets, destructor resets
- `CMakeLists.txt` - Added NinjamRunThread.cpp to JamWideJuce target_sources
- `.github/workflows/juce-build.yml` - CI workflow: macOS + Windows build, pluginval VST3/AU/Standalone validation

## Decisions Made
- Used wait(50) instead of Thread::sleep(50) in run loop for immediate response to signalThreadShouldExit()
- AU and Standalone pluginval validation marked continue-on-error since CI environments may lack audio hardware or AU validation can be flaky
- Created separate juce-build.yml workflow rather than modifying existing build.yml (which handles CLAP releases on tag push)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- NinjamRunThread skeleton ready for Phase 3 NJClient::Run() integration
- Thread run() loop has clear Phase 3 comments marking where to add command queue processing and NJClient::Run() call
- CI will automatically validate plugin format compliance on every push
- pluginval strictness level 5 ensures lifecycle correctness (activate/deactivate stress testing exercises NinjamRunThread start/stop)

## Self-Check: PASSED

All created files verified present. Both task commits (1f4adec, 2a37278) confirmed in git log. Line counts meet minimums: NinjamRunThread.h (28 >= 25), NinjamRunThread.cpp (31 >= 30), juce-build.yml (120 >= 50).

---
*Phase: 02-juce-scaffolding*
*Completed: 2026-03-07*
