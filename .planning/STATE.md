---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: Security & Quality
status: in_progress
stopped_at: 15.1-06 implementation complete — awaiting human-verify checkpoint UAT (Task 5). LocalChannelMirror replaces m_locchan_cs at all 4 audit-cited audio-path sites; HIGH-2 (no lc_ptr) + HIGH-3 (Local_Channel deferred-free with generation gate) both closed. Two deviations documented in SUMMARY (cbf preserved via separate ring; VU peak via atomic mirror fields). 5/5 test_local_channel_mirror cases pass under Release + TSan with zero races. Resume signal "approved" advances ROADMAP plan-close.
last_updated: "2026-04-26T17:34:00.000Z"
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

Phase: 15.1 (rt-safety-hardening) — IN PROGRESS (Wave 1 + 15.1-05 complete; 15.1-06 implementation complete — UAT pending)
Plan: 6 of 11 (15.1-01 audit done; 15.1-02 atomic-promotion done; 15.1-03 audio-path-logging done; 15.1-04 SPSC infrastructure done; 15.1-05 deferred-delete done; 15.1-06 implementation done — awaiting UAT; 15.1-07a/07b/07c, 08, 09, 10 remaining)
Status: 15.1-06 implementation complete — second audio-thread-mutating plan landed. m_locchan_cs.Enter/Leave removed from all 4 audit-cited audio-path sites (process_samples 1961+2118, on_new_interval 2698+2721); replaced with LocalChannelMirror[MAX_LOCAL_CHANNELS] (POD-with-embedded-SPSC, owned by audio thread) populated by drainLocalChannelUpdates() at the top of AudioProc. Mirror is BY-VALUE — Codex HIGH-2 closed (no Local_Channel* / lc_ptr / void* escape-hatch field; the per-channel BlockRecord SPSC is stored AS A MEMBER of the mirror entry; encoder consumer side wired in 15.1-07b same wave). Codex HIGH-3 closed via DeleteLocalChannel publish-wait-defer protocol with 200ms timeout: snapshot m_audio_drain_generation+1 → publish RemovedUpdate → yield-spin until audio thread bumps gate (release-store from AudioProc) → enqueue canonical Local_Channel* onto m_locchan_deferred_delete_q → drainLocalChannelDeferredDelete on run thread runs ~Local_Channel() off audio thread. NinjamRunThread::run() drains both queues in-loop AND post-loop. spsc_payloads.h UNTOUCHED — Wave-0 M-9 stability preserved. Two documented deviations: (1) Instatalk PTT cbf is preserved via SEPARATE inline-defined SPSC ring (m_locchan_processor_q) — AUDIT H-03 said zero callers, juce/NinjamRunThread.cpp:374 actually registers one; this is critical Phase 14.2 functionality; cbf+cbf_inst added to mirror by-value (NOT a HIGH-2 violation — cbf_inst is JamWideJuceProcessor-owned, not Local_Channel-owned); (2) VU peak migrated to std::atomic<float> on the mirror — GetLocalChannelPeak now reads lock-free from mirror (was reading lc->decode_peak_vol under m_locchan_cs). m_locchan_update_overflows counter exposed via GetLocalChannelUpdateOverflowCount() (Codex M-8). tests/test_local_channel_mirror.cpp added: 5 cases (Add/Info/Monitoring/Remove apply roundtrip, 1000-cycle concurrent producer/consumer, out-of-range channel ignored, HIGH-3 generation-gate deferred-free, HIGH-3 gate rejects premature free) + 6 static_assert HIGH-2 contract checks; 5/5 PASSED under both Release (build-test/) and TSan (build-tsan/) with zero ThreadSanitizer reports. JamWideJuce_Standalone build green in both build-juce/ and build-tsan/. Next: human-verify checkpoint UAT (populated-server TSan listening for 5+ min, exercise local-channel mutations, verify DeleteLocalChannel ≤ 200ms, confirm no audible glitches, confirm zero TSan reports). Then 15.1-07a (m_users_cs mirror, CR-01).
Last activity: 2026-04-26

Progress: [######....] 55% (v1.2 milestone — 6 of 11 sub-plans implementation-complete; UAT pending for 15.1-06)

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
| Phase 15.1 P06 | 1119 | 4 tasks (impl) + 1 UAT pending | 4 files |

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
- [Phase 15.1-06]: LocalChannelMirror is BY-VALUE — Codex HIGH-2 architectural choice. NO Local_Channel* / lc_ptr / void* escape-hatch field anywhere in the mirror. The per-channel BlockRecord SPSC is stored AS A MEMBER of the mirror entry (block_q owned by the audio-thread mirror, encoder thread becomes the consumer in 15.1-07b — same wave). The original revision of this plan added an `lc_ptr` so the audio thread could call lc_ptr->m_bq.AddBlock(...); this revision eliminates it entirely.
- [Phase 15.1-06]: Codex HIGH-3 deferred-free for canonical Local_Channel — DeleteLocalChannel publish-wait-defer protocol with 200ms synchronous timeout. snapshot publish_gen_target = m_audio_drain_generation.load(acquire) + 1 → publish RemovedUpdate → yield-spin until audio thread bumps gate (release-store from AudioProc, after drainLocalChannelUpdates returns) → enqueue victim onto m_locchan_deferred_delete_q → drainLocalChannelDeferredDelete on run thread runs ~Local_Channel(). On 200ms timeout we LEAK rather than risk UAF (RT-safety > memory hygiene at audio callback boundary).
- [Phase 15.1-06]: Deviation #1 — Instatalk PTT cbf preserved via SEPARATE inline-defined SPSC ring (m_locchan_processor_q with NJClient::LocalChannelProcessorUpdate POD declared INSIDE the class body, NOT in spsc_payloads.h which is FINAL per Wave-0 Codex M-9). AUDIT H-03 / RESEARCH Open Questions #4 said "JamWide doesn't register a callback today" — that was INCORRECT; juce/NinjamRunThread.cpp:374 registers an Instatalk PTT mute lambda for channel 4 at every connect. The audio thread (process_samples) MUST consult cbf for PTT muting. cbf and cbf_inst are added to the mirror BY VALUE (function pointer + opaque void* both trivially copyable; cbf_inst is JamWideJuceProcessor-owned, NOT Local_Channel-owned, so this is NOT a HIGH-2 violation — it is a callback context owned by the audio plugin host).
- [Phase 15.1-06]: Deviation #2 — VU peak migrated from canonical Local_Channel.decode_peak_vol[2] to std::atomic<float> peak_vol_l/peak_vol_r on the mirror. Removing m_locchan_cs from the audio path created a UI/audio race on decode_peak_vol; promoting to atomic on the mirror eliminates the race AND lets GetLocalChannelPeak read lock-free from the UI thread. Audio thread writes relaxed; UI reads relaxed. Display-only field, no synchronization-with-other-state needed.
- [Phase 15.1-06]: spsc_payloads.h UNTOUCHED — Wave-0 finality preserved per Codex M-9. Two new types live inline in njclient.h: LocalChannelMirror (the audio-thread-owned struct, not a payload — has embedded SpscRing<BlockRecord, 16> and atomic float peak fields, so it's NOT trivially copyable, but lives at fixed memory in the m_locchan_mirror[] array on NJClient) and NJClient::LocalChannelProcessorUpdate (deviation #1's ring element type — trivially copyable POD).
- [Phase 15.1-06]: MAX_LOCAL_CHANNELS hoisted from #define at the bottom of njclient.h to an #ifndef-guarded #define ABOVE the LocalChannelMirror struct, so the m_locchan_mirror[MAX_LOCAL_CHANNELS] member array can see the constant. The bottom #define is replaced by a comment forwarder; #ifndef guard prevents redefinition warnings.

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

Last session: 2026-04-26T17:34:00.000Z
Stopped at: 15.1-06 implementation complete (commits 0eb6914, 3846aa1, a010edc) — awaiting human-verify checkpoint UAT (Task 5). m_locchan_cs.Enter/Leave removed from process_samples 1961+2118, on_new_interval 2698+2721, AudioProc; replaced with LocalChannelMirror[MAX_LOCAL_CHANNELS] populated by drainLocalChannelUpdates() at top of AudioProc; m_audio_drain_generation atomic bumped after each drain (release-store). Codex HIGH-2 closed (no Local_Channel*/lc_ptr/void* in mirror; per-channel BlockRecord SPSC stored AS A MEMBER of mirror entry). Codex HIGH-3 closed (DeleteLocalChannel publish-wait-defer with 200ms gate + drainLocalChannelDeferredDelete from NinjamRunThread). Two deviations documented: cbf preserved via separate m_locchan_processor_q ring (Instatalk PTT — AUDIT H-03 was wrong about no callers); VU peak migrated to std::atomic<float> on mirror for lock-free GetLocalChannelPeak. test_local_channel_mirror 5/5 PASSED under Release + TSan with zero ThreadSanitizer reports. JamWideJuce_Standalone built green in both build-juce and build-tsan. spsc_payloads.h UNTOUCHED (Wave-0 M-9 stability preserved). Next: user runs the populated-server TSan UAT for 5+ minutes, exercises local-channel mutations (vol/pan/mute/solo/Add/Delete), confirms no glitches + zero TSan reports. On "approved" → mark 15.1-06 done in ROADMAP, advance to 15.1-07a (m_users_cs mirror, CR-01).
Resume file: .planning/phases/15.1-rt-safety-hardening/15.1-06-SUMMARY.md (UAT instructions in Task 5 section)
