---
phase: 04-core-ui-panels
plan: 01
subsystem: ui
tags: [juce, lookandfeel, spsc-ring, threading, dark-theme, voicemeeter]

# Dependency graph
requires:
  - phase: 03-njclient-audio-bridge
    provides: JamWideJuceProcessor, NinjamRunThread, cmd_queue, NJClient bridge
provides:
  - JamWideLookAndFeel with VB-Audio Voicemeeter Banana dark theme (color tokens + custom draw methods)
  - ChatMessageModel (500-message Processor-owned chat history)
  - Processor event queues (evt_queue, chat_queue) for Run thread to UI communication
  - Processor cachedUsers (RemoteUserInfo snapshot) for lock-free UI reads
  - License sync primitives (mutex, cv, atomic pending/response)
  - UiAtomicSnapshot (BPM, BPI, beat position, local/master VU)
  - Full NinjamRunThread callbacks (chat, license, status, user-info, server-list, topic)
  - Threading contract documentation on Processor header
affects: [04-02, 04-03, 04-04]

# Tech tracking
tech-stack:
  added: []
  patterns: [SPSC event queues for thread-safe UI communication, GetRemoteUsersSnapshot for lock-free user enumeration, clientLock exit/enter for license callback deadlock prevention]

key-files:
  created:
    - juce/ui/JamWideLookAndFeel.h
    - juce/ui/JamWideLookAndFeel.cpp
    - juce/ui/ChatMessageModel.h
  modified:
    - juce/JamWideJuceProcessor.h
    - juce/JamWideJuceProcessor.cpp
    - juce/NinjamRunThread.h
    - juce/NinjamRunThread.cpp
    - CMakeLists.txt

key-decisions:
  - "GetPosition() used instead of protected m_interval_pos/m_interval_length members"
  - "server_list.cpp added directly to JUCE target sources (not via shared library) for link resolution"
  - "License callback uses getClientLock().exit()/enter() pattern (JUCE CriticalSection) instead of mutex unlock/lock"

patterns-established:
  - "Event queue pattern: Run thread pushes UiEvent/ChatMessage to Processor SPSC queues, UI drains on timer"
  - "Snapshot pattern: cachedUsers written under clientLock, read after UserInfoChangedEvent signal"
  - "Color token pattern: static constexpr uint32 on LookAndFeel class for direct use in custom paint()"

requirements-completed: [JUCE-05]

# Metrics
duration: 16min
completed: 2026-04-04
---

# Phase 4 Plan 01: UI Infrastructure Summary

**Custom LookAndFeel with Voicemeeter Banana dark theme, Processor-owned event queues and cachedUsers, full NinjamRunThread callbacks with license deadlock prevention and GetRemoteUsersSnapshot**

## Performance

- **Duration:** 16 min
- **Started:** 2026-04-04T10:04:31Z
- **Completed:** 2026-04-04T10:20:49Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- JamWideLookAndFeel with all UI-SPEC color tokens (backgrounds, borders, text, VU, chat) and 5 custom draw method overrides
- Processor infrastructure: evt_queue (256), chat_queue (128), chatHistory (500 cap), cachedUsers, license sync, UiAtomicSnapshot, userCount atomic
- Threading contract documentation block with explicit thread-ownership rules
- Full NinjamRunThread chat callback ported from CLAP run_thread (MSG, PRIVMSG, JOIN, PART, TOPIC with timestamps)
- License callback with clientLock exit/enter deadlock prevention and 60s timeout
- GetRemoteUsersSnapshot for thread-safe user+VU enumeration (remote VU not stubbed to zero)
- HasUserInfoChanged called exactly once per loop iteration (flag is destructive)
- ServerListFetcher + RequestServerListCommand + SetUserStateCommand + SetLocalChannelMonitoringCommand handling
- UiAtomicSnapshot updates for BPM, BPI, beat position, local VU, master VU

## Task Commits

Each task was committed atomically:

1. **Task 1: LookAndFeel + ChatMessageModel + Processor event queues + cachedUsers + threading contract** - `37f8453` (feat)
2. **Task 2: NinjamRunThread event pushing + callbacks + ServerListFetcher + GetRemoteUsersSnapshot** - `aab9d2c` (feat)

## Files Created/Modified
- `juce/ui/JamWideLookAndFeel.h` - LookAndFeel_V4 subclass with all color tokens (kBgPrimary through kChatSystem) and draw overrides
- `juce/ui/JamWideLookAndFeel.cpp` - Constructor sets 20+ ColourIds, implements drawButtonBackground, drawComboBox, drawTextEditorOutline, drawScrollbar, drawLabel
- `juce/ui/ChatMessageModel.h` - 500-message vector-based chat history model stored on Processor
- `juce/JamWideJuceProcessor.h` - Added threading contract doc, evt_queue, chat_queue, chatHistory, cachedUsers, license primitives, UiAtomicSnapshot, userCount
- `juce/JamWideJuceProcessor.cpp` - Added ui_state.h include, license_cv unblock in releaseResources shutdown path
- `juce/NinjamRunThread.h` - Added ServerListFetcher member, lastStatus_ member (not static/local)
- `juce/NinjamRunThread.cpp` - Full chat/license/status/user-info/server-list callbacks, UiAtomicSnapshot updates, all command handlers
- `CMakeLists.txt` - Added JamWideLookAndFeel.cpp and server_list.cpp to JUCE target sources

## Decisions Made
- Used `GetPosition()` public API instead of protected `m_interval_pos`/`m_interval_length` members for interval position data
- Added `server_list.cpp` directly to JamWideJuce target sources rather than extracting a shared library (simpler, avoids unnecessary refactoring)
- License callback uses JUCE `CriticalSection::exit()`/`enter()` pattern (equivalent to CLAP `mutex.unlock()`/`mutex.lock()`)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added server_list.cpp to JUCE target sources**
- **Found during:** Task 2 (build verification)
- **Issue:** ServerListFetcher symbols were unresolved at link time because server_list.cpp was only compiled as part of CLAP's jamwide-threading library
- **Fix:** Added `src/net/server_list.cpp` to JamWideJuce target_sources in CMakeLists.txt
- **Files modified:** CMakeLists.txt
- **Verification:** Build succeeds, ServerListFetcher constructs and methods resolve
- **Committed in:** aab9d2c (Task 2 commit)

**2. [Rule 1 - Bug] Used GetPosition() instead of protected members**
- **Found during:** Task 2 (build verification)
- **Issue:** m_interval_pos and m_interval_length are protected NJClient members, not accessible from NinjamRunThread
- **Fix:** Used client->GetPosition(&iPos, &iLen) public API instead
- **Files modified:** juce/NinjamRunThread.cpp
- **Verification:** Build succeeds, position data flows correctly
- **Committed in:** aab9d2c (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (1 blocking, 1 bug)
**Impact on plan:** Both auto-fixes necessary for compilation. No scope creep.

## Issues Encountered
- Git worktree was on older commit (pre-Phase 2), required merge from main before files existed
- Git submodules needed initialization in worktree before CMake could configure

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All UI infrastructure ready for Plans 02-04: LookAndFeel theme, event queues, chat model, cached users
- Plans 02-04 can focus on Component layout and logic without threading or theming concerns
- Build system configured and verified

---
*Phase: 04-core-ui-panels*
*Completed: 2026-04-04*
