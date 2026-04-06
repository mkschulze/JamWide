---
phase: 10-osc-remote-users-and-template
plan: 01
subsystem: osc
tags: [osc, juce_osc, remote-users, bidirectional, touchosc, ninjam, roster]

# Dependency graph
requires:
  - phase: 09-osc-server-core
    provides: OscServer with APVTS param send/receive, telemetry, VU meters, echo suppression, dirty tracking
provides:
  - Remote user group bus and sub-channel bidirectional OSC control (volume, pan, mute, solo)
  - Roster name broadcast with dirty-flag change detection
  - Remote user VU meter broadcast (aggregate + per-sub-channel)
  - Connect/disconnect session triggers via OSC
  - String argument OSC handling (connect trigger)
  - Extended RemoteUserInfo snapshot with volume/pan fields
affects: [10-02-touchosc-template, 11-video-companion-foundation]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Prefix-based OSC dispatch for dynamic addresses (vs static OscAddressMap)"
    - "Sequential 1-based sub-channel indexing resolved to sparse NINJAM bit index"
    - "Derived group solo from sub-channel state (no native NJClient group solo)"
    - "Roster dirty-flag hash for change detection (not per-tick broadcast)"
    - "Full cache + echo suppression reset on roster change"

key-files:
  created: []
  modified:
    - juce/osc/OscServer.h
    - juce/osc/OscServer.cpp
    - src/core/njclient.h
    - src/core/njclient.cpp
    - docs/osc.md

key-decisions:
  - "Sub-channel {n} uses sequential 1-based indexing, resolved to NINJAM bit index internally"
  - "Group solo implemented as set-all-sub-channels (no native NJClient group solo primitive)"
  - "Roster broadcast uses hash-based dirty flag, not per-tick -- reduces string message volume"
  - "All per-slot cached state and echo suppression reset on roster change to prevent stale inheritance"
  - "Connect trigger uses stored credentials from processor.lastUsername (not sent via OSC)"

patterns-established:
  - "Prefix dispatch: /JamWide/remote/ prefix routes to handleRemoteUserOsc before OscAddressMap lookup"
  - "String OSC: oscMessageReceived detects string args first, dispatches via handleOscStringOnMessageThread"
  - "Roster hash: count + sum of name lengths as cheap change detection"

requirements-completed: [OSC-04, OSC-05, OSC-08]

# Metrics
duration: 6min
completed: 2026-04-06
---

# Phase 10 Plan 01: Remote User OSC Control Summary

**Bidirectional OSC control for 16 remote user slots with group bus, sub-channel, roster broadcast, VU meters, and session connect/disconnect triggers**

## Performance

- **Duration:** 6 min
- **Started:** 2026-04-06T19:52:06Z
- **Completed:** 2026-04-06T19:58:00Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- Extended OscServer with 5 new methods: sendDirtyRemoteUsers, sendRemoteVuMeters, sendRemoteRoster, handleRemoteUserOsc, handleOscStringOnMessageThread
- Full bidirectional remote user control: group bus (volume, pan, mute, solo) and sub-channel (volume, pan, mute, solo) with correct index mapping and echo suppression
- Roster name broadcast with dirty-flag change detection and full cache reset on roster change
- Session connect/disconnect triggers with robust string parsing (IPv6, port validation, length limits)
- docs/osc.md updated as single source of truth with 16 remote user addresses documented

## Task Commits

Each task was committed atomically:

1. **Task 1: Extend RemoteUserInfo snapshot, add remote user OSC send methods, and wire into timerCallback** - `4f7dbd5` (feat)
2. **Task 2: Add remote user receive path with prefix dispatch, string OSC handling, connect/disconnect triggers, and docs update** - `6b22d2e` (feat)

## Files Created/Modified
- `juce/osc/OscServer.h` - Added 5 method declarations, remote user dirty tracking structs, echo suppression arrays, roster hash state
- `juce/osc/OscServer.cpp` - Implemented all 5 new methods, string OSC handling, prefix dispatch, connect validation, wired into timerCallback
- `src/core/njclient.h` - Added volume/pan fields to RemoteUserInfo struct
- `src/core/njclient.cpp` - Populated volume/pan in GetRemoteUsersSnapshot
- `docs/osc.md` - Added remote user, sub-channel, VU meter, roster, session control sections; removed Future section

## Decisions Made
- Sub-channel `{n}` uses sequential 1-based indexing (not sparse NINJAM bit index) -- resolved to NINJAM bit index via `channels[n-1].channel_index` at dispatch time
- Group solo has no native NJClient primitive -- implemented as setting solo on ALL sub-channels simultaneously
- Group solo feedback is derived: 1.0 only when ALL sub-channels are soloed
- Roster broadcast uses simple hash (count + name lengths) for change detection, not per-tick
- All per-slot cached state and echo suppression flags reset on roster change to prevent stale/inherited state
- Connect trigger uses stored credentials from `processor.lastUsername` -- falls back to "anonymous" if empty

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- OscServer now has full remote user control -- ready for Plan 02 (TouchOSC template)
- docs/osc.md is the single source of truth for all OSC addresses -- template must match
- All 16 remote slots supported with bidirectional feedback

## Self-Check: PASSED

All 5 modified files verified present. Both task commits (4f7dbd5, 6b22d2e) verified in git log. Build compiles cleanly.

---
*Phase: 10-osc-remote-users-and-template*
*Completed: 2026-04-06*
