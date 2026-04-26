---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: Security & Quality
status: in_progress
stopped_at: Completed 15.1-05-deferred-delete-PLAN.md (Wave 2 first plan — all 7 audio-thread DecodeState delete sites replaced with SPSC try_push to m_deferred_delete_q; drainDeferredDelete wired into NinjamRunThread::run() at 20ms cadence + graceful-shutdown drain; m_deferred_delete_overflows counter exposed via GetDeferredDeleteOverflowCount() per Codex M-8 phase-close gate; test_deferred_delete 3/3 PASSED under both Release and TSan with zero ThreadSanitizer reports)
last_updated: "2026-04-26T15:46:40.000Z"
last_activity: 2026-04-26
progress:
  total_phases: 10
  completed_phases: 9
  total_plans: 30
  completed_plans: 23
  percent: 96
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-05)

**Core value:** Musicians can jam together online with lossless audio quality and per-user mixing -- in any DAW or standalone.
**Current focus:** Phase 15.1 — RT-Safety Hardening (planned, ready to execute)

## Current Position

Phase: 15.1 (rt-safety-hardening) — IN PROGRESS (Wave 1 + 15.1-05 complete; Wave 2 in progress)
Plan: 5 of 11 (15.1-01 audit done; 15.1-02 atomic-promotion done; 15.1-03 audio-path-logging done; 15.1-04 SPSC infrastructure done; 15.1-05 deferred-delete done; 15.1-06..10 remaining; 07 split into 07a/07b/07c)
Status: 15.1-05 complete — first audio-thread-mutating plan landed. All 7 audio-thread DecodeState delete sites identified by AUDIT CR-05/06/07 (mixInChannel sites originally at lines 2404, 2414, 2447, 2467, 2667; on_new_interval sites originally at 2742, 2745) replaced with `deferDecodeStateDelete` helper that try_pushes onto NJClient::m_deferred_delete_q (SpscRing<DecodeState*, DEFERRED_DELETE_CAPACITY=256>). Pointer-shuffle ordering preserved at sites 4, 5, 7 per RESEARCH § "Subtle note for the planner" (capture-first, then advance the slot, then defer-delete). drainDeferredDelete wired into NinjamRunThread::run() inside the 20ms ScopedLock block (drains LAST per drain-order rule) AND once after the run-loop exits for graceful shutdown. Codex M-8 overflow counter (m_deferred_delete_overflows std::atomic<uint64_t>) exposed via public GetDeferredDeleteOverflowCount() accessor — 15.1-10 phase verification will assert == 0 post-UAT (non-zero == architectural defect, queue undersized for workload). On overflow, the audio thread leaks the pointer for one tick AND increments the counter (RT-safety > memory hygiene). tests/test_deferred_delete.cpp added: 3 cases (256-burst push+drain, 50Hz audio-rate producer/20ms consumer, Codex M-8 overflow counter mechanism); 3/3 PASSED under both Release (build-test/) and TSan (build-tsan/) with zero ThreadSanitizer reports. JamWideJuce_Standalone build green. test_njclient_atomics + test_spsc_state_updates regression tests still pass. Next plan: 15.1-06 (m_locchan_cs snapshot replacement, CR-02).
Last activity: 2026-04-26

Progress: [#####.....] 45% (v1.2 milestone — 5 of 11 sub-plans complete)

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
| Phase 15.1 P04 | ~1500 | 3 tasks | 4 files |
| Phase 15.1 P05 | 478 | 3 tasks | 4 files |

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
- [Phase 15.1-04]: src/threading/spsc_payloads.h is FINAL after Wave 0. No subsequent plan (15.1-05/06/07a/07b/07c/08/09) modifies this header. DecodeArmRequest landed at Wave 0 (not deferred to 15.1-09); LocalChannelAddedUpdate carries the FULL field set (mute/solo/volume/pan + srcch/bitrate/bcast/outch/flags) so 15.1-06 doesn't extend it. Codex M-9 stability claim recorded.
- [Phase 15.1-04]: Codex HIGH-2 architectural choice — NO raw-pointer escape hatches in any payload. Mirrors are populated by VALUE through the variant-mutation streams. Only pointers crossing thread boundaries are ownership-transfer (DecodeState handover via PeerNextDsUpdate) and deferred-free transports for canonical objects whose audio-thread observation has provably ceased (HIGH-3 generation-gated lifetime contract — implementation in 15.1-06 / 15.1-07a).
- [Phase 15.1-04]: MAX_BLOCK_SAMPLES = 2048 contract is documented at the source (spsc_payloads.h docstring). NJClient::SetMaxAudioBlockSize (15.1-08) MUST assert maxSamplesPerBlock <= MAX_BLOCK_SAMPLES at prepareToPlay time; per-callsite BlockRecord producers (15.1-07b) MUST defensively bounds-check. Two-layer enforcement closes Codex M-7.
- [Phase 15.1-04]: Single TSan target (--tsan flag → build-tsan/, JAMWIDE_TSAN=ON) covers BOTH NJClient core unit tests AND the JUCE callback boundary. macOS codesign block gated `if(APPLE AND NOT JAMWIDE_TSAN)` per RESEARCH macOS caveat #1 — TSan injects a runtime not covered by ad-hoc codesigning, leading to launch failure on macOS without this gate.
- [Phase 15.1-04]: scripts/build.sh was untracked at session start; added to git index as part of this plan. The script is the canonical local build entrypoint (referenced from CLAUDE.md memory).
- [Phase 15.1-05]: All 7 audio-thread DecodeState delete sites factored through a single static helper (deferDecodeStateDelete) that performs the try_push + overflow-counter bump + null-out pattern. Plan's `<action>` block specified the helper verbatim; the literal `m_deferred_delete_q.try_push` grep in the acceptance criteria was looking for the inlined form, but the helper-factored approach is what the plan prescribes and 7 sites do call it. Functionally identical to 7 inline try_pushes.
- [Phase 15.1-05]: Pointer-shuffle ordering at llmode advance sites (mixInChannel sites 4, 5; on_new_interval site 7) preserved per RESEARCH § "Subtle note for the planner" — capture old pointer FIRST into a local, advance the slot (chan->ds = next_ds[0]; next_ds[0] = next_ds[1]; ...), THEN defer-delete the captured pointer. Audio thread retains exclusive ownership during the shuffle; only the orphaned old pointer crosses the SPSC.
- [Phase 15.1-05]: Codex M-8 fallback semantics — when try_push returns false, the audio thread DOES NOT delete (would block on codec/file teardown). It bumps m_deferred_delete_overflows and proceeds. Counter being observable at phase close (15.1-10 asserts == 0) makes silent overflow a phase-failing condition, not a tolerable transient. RT-safety > memory hygiene at the audio callback boundary is the locked architectural choice.
- [Phase 15.1-05]: Run-thread drain is two-stage — in-loop after updateSessionAndVuSnapshot at 20ms cadence (drained LAST per RESEARCH § "Drain order"), AND post-loop graceful-shutdown drain after the while(!threadShouldExit()) exits to prevent leaks on disconnect.

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

Last session: 2026-04-26T15:46:40.000Z
Stopped at: Completed 15.1-05-deferred-delete-PLAN.md. All 7 audio-thread DecodeState delete sites (CR-05/06/07) now defer onto NJClient::m_deferred_delete_q (256-slot SpscRing<DecodeState*>); drainDeferredDelete runs at 20ms run-thread cadence + once at graceful shutdown; Codex M-8 overflow counter (m_deferred_delete_overflows) exposed via GetDeferredDeleteOverflowCount() for 15.1-10 phase-close gate. test_deferred_delete 3/3 PASSED under both Release and TSan with zero ThreadSanitizer reports. JamWideJuce_Standalone build green. Next plan: 15.1-06 (m_locchan_cs snapshot replacement, CR-02 — second audio-thread-mutating plan).
Resume file: None
