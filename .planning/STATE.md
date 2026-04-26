---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: Security & Quality
status: in_progress
stopped_at: Completed 15.1-02-atomic-promotion-PLAN.md (CR-03 closed, m_interval_pos race closed, edge-triggered semantics documented per L-10, test_njclient_atomics scaffolded with 4 sub-tests)
last_updated: "2026-04-26T14:42:51.000Z"
last_activity: 2026-04-26
progress:
  total_phases: 10
  completed_phases: 9
  total_plans: 30
  completed_plans: 20
  percent: 90
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-05)

**Core value:** Musicians can jam together online with lossless audio quality and per-user mixing -- in any DAW or standalone.
**Current focus:** Phase 15.1 — RT-Safety Hardening (planned, ready to execute)

## Current Position

Phase: 15.1 (rt-safety-hardening) — IN PROGRESS (Wave 1 underway)
Plan: 2 of 11 (15.1-01 audit done; 15.1-02 atomic-promotion done; 15.1-03..10 remaining; 07 split into 07a/07b/07c)
Status: 15.1-02 complete — m_misc_cs eliminated from audio thread, m_bpm/m_bpi/m_beatinfo_updated/m_interval_pos atomic-promoted, edge-triggered best-effort semantics documented per Codex L-10, test_njclient_atomics scaffold passes 4/4 sub-tests. Next Wave-1 plans: 15.1-03 (eliminate audio-path logging), 15.1-04 (SPSC infrastructure + --tsan flag).
Last activity: 2026-04-26

Progress: [##........] 18% (v1.2 milestone — 2 of 11 sub-plans complete)

## Performance Metrics

**Velocity:**

- Total plans completed: 32 (v1.0)
- v1.1 plans completed: 0

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1-8 (v1.0) | 21 | -- | -- |
| 9-13 (v1.1) | TBD | -- | -- |
| 09 | 2 | - | - |
| 10 | 2 | - | - |
| 11 | 3 | - | - |
| 13 | 2 | - | - |
| 15 | 2 | - | - |

*Updated after each plan completion*
| Phase 14 P02 | 788 | 2 tasks | 19 files |
| Phase 14 P03 | 601 | 2 tasks | 2 files |
| Phase 14.2 P01 | 957 | 2 tasks | 8 files |
| Phase 14.2 P02 | 221 | 2 tasks | 5 files |
| Phase 15.1 P02 | 578 | 2 tasks | 5 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- VDO.Ninja browser companion (not embedded WebView) -- keeps plugin lightweight
- OSC via juce_osc (IEM pattern) -- no external deps, proven across 20+ IEM plugins
- Index-based OSC addressing for remote users -- stable fader mapping, name broadcast on roster change
- OSC before Video in v1.1 -- zero new deps, proven patterns, immediate user value
- Phase 11 (Video) independent of OSC phases -- architecturally decoupled
- Companion page on GitHub Pages HTTPS, plugin runs local WebSocket only (mixed-content constraint)
- OSC callbacks must dispatch via callAsync() to preserve SPSC cmd_queue single-producer invariant
- State version bump 1 to 2 for OSC config persistence
- [Phase 14]: Green/mint MIDI Learn feedback instead of yellow to avoid solo button color conflict
- [Phase 14]: Note On/Off MIDI mapping support added beyond original CC-only plan scope
- [Phase 14]: APVTS centralization: MidiMapper timerCallback is sole APVTS-to-NJClient bridge for remote group controls; OscServer and ChannelStripArea update APVTS only
- [Phase 14.2]: Measurement state machine consolidated in NJClient (single owner); RemoteUser* pointer comparison for identity; syncMode JSON field for three-tier delay priority
- [Phase 14.2]: Global overlay over #main-area instead of per-tile: VDO.Ninja iframe is cross-origin, per-tile positioning impossible
- [Phase 15.1-02]: m_beatinfo_updated publication is edge-triggered best-effort (NOT last-value latch) — writer's last store wins, reader sees latest payload, intermediate updates dropped by design (BPM/BPI are config values; only most recent matters). Documented in njclient.h header comment per Codex L-10.
- [Phase 15.1-02]: AudioProc m_interval_pos uses local-cache pattern (load once, store once back) to clarify same-thread relaxed semantics and minimize atomic ops; m_misc_cs eliminated entirely from audio thread.

### Pending Todos

(Carried from v1.0)

- Phase 3 audio transmission not working end-to-end -- needs debugging

### Known Issues (v1.1 pre-release)

- OSC control not yet working -- needs debugging
- FLAC audio not yet working -- needs debugging
- MIDI Learn not working -- currently under investigation

### Blockers/Concerns

- [Phase 11]: OpenSSL linkage on Windows CI unvalidated -- project now has OpenSSL dependency (Phase 15), CI steps added but untested on Windows
- [Phase 12]: VDO.Ninja external API is self-labeled DRAFT -- may require adaptation

### Quick Tasks Completed

| # | Description | Date | Commit | Status | Directory |
|---|-------------|------|--------|--------|-----------|
| 260413-udi | Add usernames in server room list and audio prelisten before entering a room | 2026-04-13 | 972885d | Needs Review | [260413-udi-add-usernames-in-server-room-list-and-au](./quick/260413-udi-add-usernames-in-server-room-list-and-au/) |

## Session Continuity

Last session: 2026-04-26T14:42:51.000Z
Stopped at: Completed 15.1-02-PLAN.md (CR-03 closed, m_interval_pos race closed, edge-triggered semantics documented per Codex L-10, test_njclient_atomics 4/4 passing). Next Wave-1 plans: 15.1-03 (audio-path logging removal) and 15.1-04 (SPSC infrastructure + --tsan flag).
Resume file: None
