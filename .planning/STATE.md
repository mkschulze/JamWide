---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: Security & Quality
status: in_progress
stopped_at: Completed 15.1-03-eliminate-audio-path-logging-PLAN.md (CR-04, H-01, H-02, L-02 closed; writeLog/writeUserChanLog/fopen all gone from audio path; writeUserChanLog body+declaration deleted per Codex per-plan delta)
last_updated: "2026-04-26T15:30:00.000Z"
last_activity: 2026-04-26
progress:
  total_phases: 10
  completed_phases: 9
  total_plans: 30
  completed_plans: 21
  percent: 90
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-05)

**Core value:** Musicians can jam together online with lossless audio quality and per-user mixing -- in any DAW or standalone.
**Current focus:** Phase 15.1 — RT-Safety Hardening (planned, ready to execute)

## Current Position

Phase: 15.1 (rt-safety-hardening) — IN PROGRESS (Wave 1 underway)
Plan: 3 of 11 (15.1-01 audit done; 15.1-02 atomic-promotion done; 15.1-03 audio-path-logging done; 15.1-04..10 remaining; 07 split into 07a/07b/07c)
Status: 15.1-03 complete — writeLog/writeUserChanLog/fopen all eliminated from audio path (CR-04, H-01, H-02, L-02 closed). writeUserChanLog body+declaration fully deleted per Codex per-plan delta — no [[maybe_unused]] inert code retained. m_debug_logged_remote member removed. writeLog API itself survives for non-RT callers (Disconnect/sessionlog/chat/local/localsessionlog). Next Wave-1 plans: 15.1-04 (SPSC infrastructure + --tsan flag).
Last activity: 2026-04-26

Progress: [###.......] 27% (v1.2 milestone — 3 of 11 sub-plans complete)

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
| Phase 15.1 P03 | 120 | 1 task  | 2 files |

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
- [Phase 15.1-03]: writeUserChanLog body+declaration deleted entirely (not [[maybe_unused]]'d, not #if 0'd) per Codex per-plan delta — no inert dead code retained; restoration would require an SPSC-mediated logging path, never an in-place audio-thread call.
- [Phase 15.1-03]: guidtostr() retained against the plan's L-02 cleanup instruction because grep audit showed 6 non-audio-path callers (sessionlog, localsessionlog, makeFilenameFromGuid, chat-write paths). Audit's "becomes irrelevant" wording referred to audio-thread reachability only.

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

Last session: 2026-04-26T15:30:00.000Z
Stopped at: Completed 15.1-03-PLAN.md (CR-04, H-01, H-02, L-02 closed; writeLog/writeUserChanLog/fopen all gone from audio path; writeUserChanLog body+declaration deleted entirely per Codex per-plan delta; writeLog API survives for non-RT callers). Next Wave-1 plan: 15.1-04 (SPSC infrastructure + --tsan flag).
Resume file: None
