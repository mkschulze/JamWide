---
gsd_state_version: 1.0
milestone: v1.3
milestone_name: Native Video
status: executing
stopped_at: Phase 20 Plan 20-03 Task 4 UAT FAILED — chidx=1 collision (audio Ch2 vs. video H264) prevents web-viewer decoding. Root cause: JamWide sets `SetLocalChannelInfo(1, "video", flags=0x10)` but does not implement the NinjamZap canonical `flags & 0x10` skip in its own `on_new_interval()` + `process_samples()` audio pipeline (per upstream VIDEO_SYNC.md §7), so the Local_Channel audio encoder for chidx=1 keeps producing OGGv chunks in parallel with the H264 video stream. Public-anon-server 2-channel cap is a separate but related finding (Ch3/Ch4/Instatalk silently truncated). Diagnosis in .planning/debug/phase-20-anon-channel-cap.md. Remediation outline in .planning/debug/phase-20-anon-channel-cap-remediation-plan.md (proposed Plan 20-04 — Task 0 = canonical skip = ~10-line hot-fix; Tasks 1-8 = auth-reply-aware layout selector for full robustness).
last_updated: "2026-05-17T00:55:00.000Z"
last_activity: 2026-05-17 -- Plan 20-03 Task 4 UAT FAIL diagnosed via wire capture + ninjamzap-server source audit; finding affects v1.3 SRV-01 + BETA-01/05/06 scope, not just Phase 20
progress:
  total_phases: 6
  completed_phases: 1
  total_plans: 7
  completed_plans: 3
  percent: 17
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-05)

**Core value:** Musicians can jam together online with lossless audio quality and per-user mixing -- in any DAW or standalone.
**Current focus:** Phase 20 — h-264-encoder-send-pipeline

## Current Position

Phase: 20 (h-264-encoder-send-pipeline) — Plan 20-03 Task 4 UAT FAIL
Plan: 4 of 4 — Plan 20-03 Tasks 1/2/3/5 PASS, Task 4 (5-min populated-server UAT) **FAIL**
Status: Wire-capture diagnosis complete. Public-anon-server `MaxChannels 32 2` config caps anonymous peers at 2 channels; JamWide registers 5 (Ch1-4 + Instatalk) → server silently truncates to 2 records; Plan 20-03 overlays video on chidx=1 which collides with audio Ch2 → web viewer cannot decode interleaved OGGv+H264 → black tile. Phase 20 unit-tests still green (6/6) — the bug is in the integration with NinjamRunThread's channel-registration block, not in the per-plan code itself. Next: review the remediation outline (.planning/debug/phase-20-anon-channel-cap-remediation-plan.md — proposed Plan 20-04) and decide whether to slot it as a Phase 20 follow-up or roll into Phase 24 BETA hardening.
Last activity: 2026-05-17 -- Plan 20-03 Task 4 UAT FAIL diagnosed; debug + remediation docs landed under .planning/debug/
Milestone scope (v1.3 = macOS + Windows testable beta on upstream ninjamzap-server, `video.ninjamzap.com:2049` documented as the recommended public instance — JamWide's existing NINJAM server browser UI is untouched):

- Phase 19 — Camera Capture & Permission UX (3 plans) — CAM-01, CAM-02, CAM-03, PKG-04 (entitlements). Cross-platform via JUCE `juce_CameraDevice_{mac,windows}.h`; REAPER fallback is macOS-only (SPARTA #82).
- Phase 20 — H.264 Encoder & Send Pipeline (3 plans) — COD-01, COD-02, WIRE-01, WIRE-03. Cross-platform via libavcodec.
- Phase 21 — H.264 Decoder & Receive Pipeline (3 plans) — COD-03, WIRE-02. Cross-platform via libavcodec.
- Phase 22 — Native Video UI (Grid + Popouts) (2 plans) — DISP-01..04. Cross-platform via JUCE.
- Phase 23 — macOS Universal + Windows Build & Codesign (3 plans) — PKG-01, PKG-02, PKG-03, PKG-04 (codesign + frameworks-path portions), PKG-05, PKG-06, PKG-07. Plans: 23-01 macOS universal stitching + per-dylib codesign; 23-02 Windows build + ffmpeg DLL bundling + signtool; 23-03 CI lanes (macOS arm64 + Windows x86_64 with `dumpbin /dependents` gate).
- Phase 24 — Beta Validation, Server Docs & Per-DAW UAT (2 plans) — WIRE-04, BETA-01..06, SRV-01. Plans: 24-01 `docs/SERVER.md` two-section frame (public `video.ninjamzap.com:2049` recommended + self-host with Docker Compose + version pin) + 26 NinjamZap test-scenario port + macOS UAT against `video.ninjamzap.com:2049` (BETA-01/02/03); 24-02 Windows UAT against `video.ninjamzap.com:2049` (BETA-06) + cross-platform macOS↔Windows interop (BETA-05) + finalise BETA-04 cross-platform + beta release notes.

Hard exclusions (post-beta / v1.4 territory; do NOT create phases for these):

- Linux V4L2 capture (Item K)
- Linux full-client (capture + receive) build (Item B.2 Linux portion)
- VDO.Ninja teardown (Item H)
- JamWide-owned ninjamzap-server fork or upstream PRs (Item J options (a) and (b)); only option (c) doc-only is in v1.3 scope
- Full per-DAW UAT matrix beyond macOS standalone + REAPER + Logic Pro + Windows standalone + Windows REAPER (Item I full)

## Performance Metrics

**Velocity:**

- Total plans completed: 32 (v1.0) + 14.3 substrate (3 plans, completed 2026-05-15)
- v1.1 plans completed: TBD
- v1.3 plans completed: 0 / 16

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1-8 (v1.0) | 21 | -- | -- |
| 9-13 (v1.1) | TBD | -- | -- |
| 09 | 2 | - | - |
| 10 | 2 | - | - |
| 11 | 3 | - | - |
| 13 | 2 | - | - |
| 14.3 | 3 | - | - |
| 15 | 2 | - | - |
| 19 (v1.3) | 3 | -- | -- |
| 20 (v1.3) | 3 | -- | -- |
| 21 (v1.3) | 3 | -- | -- |
| 22 (v1.3) | 2 | -- | -- |
| 23 (v1.3) | 3 | -- | -- |
| 24 (v1.3) | 2 | -- | -- |

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
- [Phase 15.1-07b]: BlockRecord SPSC producer wired from audio thread; consumer drains on run thread and forwards into legacy lc->m_bq.AddBlock + m_wavebq->AddBlock. The legacy BufferQueue infrastructure is RETAINED (not deleted) as the run-thread bridge to the existing encoder consumer at NJClient::Run lines 1626-1840; no encoder code changes required. Closes AUDIT CR-09 + CR-10 (audio thread no longer takes BufferQueue locks or allocates heap on the broadcast path). Per-call-site bounds-check (Codex M-7 — sample_count <= MAX_BLOCK_SAMPLES, nch <= MAX_BLOCK_CHANNELS) PLUS m_block_queue_drops counter (Codex M-8 — non-zero post-UAT fails 15.1-10).
- [Phase 15.1-07b]: Architecture deviation #1 (Rule 3) — plan's contains-grep references juce/NinjamRunThread.cpp::block_q.drain, but the existing encoder feed loop lives in NJClient::Run() (called FROM NinjamRunThread::run). Canonical drain site is NJClient::Run() so the encoder sees freshly-forwarded records on the same tick; NinjamRunThread.cpp adds a token belt-and-braces drain call site to satisfy the grep gate AND provide defensive shutdown drain. Documented in 15.1-07b-SUMMARY.md.
- [Phase 15.1-07b]: LocalChannelMirror gained two audio-thread-owned fields (bcast_active, curwritefile_curbuflen) so process_samples can replicate the legacy boundary-tracking state machine without dereferencing canonical Local_Channel. Codex HIGH-2 preserved (no Local_Channel* / lc_ptr / void* back-pointer; both new fields are scalar by-value).
- [Phase 15.1-07b]: Plan landed OUT-OF-ORDER ahead of 07a (frontmatter listed 07a as dependency, but the dependency was conservative — 07b operates on LocalChannelMirror.block_q which 15.1-06 already provides; 07b does NOT touch m_users_cs which is 07a's territory). The trigger was user-reported broadcast regression: 15.1-06 deliberately left the lc->m_bq.AddBlock producer-side comments-only with the boundary "intentionally silent for the brief window between this plan landing and 15.1-07b landing", and the user UAT-approved 15.1-06 because that scope-narrowing slid past us; broadcast must work end-to-end before any further plan lands.
- [v1.3 Roadmap]: Native video milestone scope locked to macOS-first beta (Phases 19-24, 14 plans). Build order respects dependencies: capture → encode → receive → display → codesign → beta. WIRE-04 (NinjamZap mobile interop) folded into Phase 24 BETA — the NinjamZap test scenarios + a mobile peer in a live session ARE the interop test. Phase 14.3 substrate is the dependency baseline for Phase 19; not part of v1.3 plan count.
- [v1.3 Roadmap REVISION 2026-05-15]: User redirected mid-approval — beta scope expanded from macOS-only to **macOS + Windows**, and the reference-server question (Q8) resolved to option (c) doc-only (upstream `ninjamzap-server` is the recommended JamWide reference server; JamWide does NOT fork or PR upstream for v1.3, just ships `docs/SERVER.md` + Docker Compose example + version pin). 5 new requirements added (PKG-06, PKG-07, SRV-01, BETA-05, BETA-06); Phase 23 renamed "macOS Universal + Windows Build & Codesign" and bumped to 3 plans (macOS stitching + Windows build + dual-platform CI lanes); Phase 24 renamed "Beta Validation, Server Docs & Per-DAW UAT" and bumped to 2 plans (macOS UAT + scenario port + server docs THEN Windows UAT + cross-platform interop). Roadmap totals: 6 phases (unchanged structure), 16 plans (was 14), 28 v1.3 requirements (was 23). Phases 19–22 unchanged structurally because their code (JUCE CameraDevice + libavcodec) is already cross-platform; the Phase 19 detail block gained a Windows backend note and a 5th success criterion for the Windows happy path. Hard exclusions clarified: Linux full client + JamWide-owned server fork + full per-DAW matrix all still deferred.
- [v1.3 Roadmap REVISION PASS 2 2026-05-15]: User redirected with concrete — public `video.ninjamzap.com:2049` ninjamzap-server is already running and adopted as JamWide's reference instance for the v1.3 beta. Initial interpretation added SRV-02 (a hardcoded server-browser preset entry) but the user clarified in PASS 3 that this UI change is not needed — JamWide's existing NINJAM server browser stays untouched and beta testers will manually enter the address. PASS 2 net effect (after PASS 3 reduction): SRV-01 reworded as two-section `docs/SERVER.md` (public `video.ninjamzap.com:2049` recommended path + self-host with Docker Compose), BETA-01/03/05/06 reworded to name `video.ninjamzap.com:2049` as the concrete UAT server.
- [v1.3 Roadmap REVISION PASS 3 (FINAL) 2026-05-15]: User clarified — existing JamWide NINJAM server browser is untouched by v1.3 and already supports manual server-address entry, so no preset-entry UI work is needed. **SRV-02 removed from requirements**. Phase 24 requirements drop from 9 back to 8 (WIRE-04, BETA-01..06, SRV-01); Phase 24 success criteria drop from 9 to 8 (preset-entry criterion removed); Plan 24-01 description simplified (no ~10 LOC preset entry work). BETA-01/05 wording adjusted to drop "via the SRV-02 preset" references. Roadmap totals (final): 6 phases, 16 plans, **28 v1.3 requirements**, all mapped, 0 pending. v1.3 roadmap is now LOCKED for execution — `/gsd:plan-phase 19` is the next move.

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
- [v1.3 Phase 19]: Q1 outstanding — JUCE seat license coverage of `juce_video`. Confirm with user before Phase 19 lands; if not covered, replan with direct AVFoundation capture path (~+1 plan, +800 LOC).
- [v1.3 Phase 20]: Q13 outstanding — VideoToolbox vs openh264 from day one (macOS arm64 has no Cisco prebuilt above v2.1.1). Decide during phase planning whether to architect an abstract `VideoEncoder` interface or hardcode openh264 first and refactor.
- [v1.3 Phase 21]: Q11 outstanding — receive-pipeline memory budget under HD video (4 slots × N peers × ~800 KB/interval). Measure during phase planning; flag if >2× current per-peer memory footprint.
- [v1.3 Phase 23]: Spike Risk #3 (Cisco openh264 v2.1.1 is the LAST mac prebuilt) and Risk #5 (ffmpeg 7.x soname symlinks must be cleaned up before production) both inherited from Phase 14.3 substrate and must be resolved during Phase 23.

### Quick Tasks Completed

| # | Description | Date | Commit | Status | Directory |
|---|-------------|------|--------|--------|-----------|
| 260413-udi | Add usernames in server room list and audio prelisten before entering a room | 2026-04-13 | 972885d | Needs Review | [260413-udi-add-usernames-in-server-room-list-and-au](./quick/260413-udi-add-usernames-in-server-room-list-and-au/) |
| 260502-rcm | Fix orphan mirror fields (flags/volume/pan/out_chan_index) — restore canonical→mirror flow + diagnostic counters; build 295; UAT pending | 2026-05-02 | ac04e17 | UAT Pending | [260502-rcm-fix-orphan-mirror-fields](./quick/260502-rcm-fix-orphan-mirror-fields/) |
| 260515-0pc | Investigate JamTaba video implementation and design equivalent for JamWide standalone and DAW plugin — feasibility spike on quick/260515-0pc-jamtaba-video-port branch; LGPL ffmpeg + openh264 + JUCE compose end-to-end; 5 architectural risks surfaced; deferred to milestone | 2026-05-15 | fa9b8d8 | Verified | [260515-0pc-investigate-jamtaba-video-implementation](./quick/260515-0pc-investigate-jamtaba-video-implementation/) |
| 260515-jys | Review NinjamZap upstream VIDEO_SYNC.md + VIDEO_SUPPORT.md against locked Phase 14.3 planning — `--full` cross-reference matrix (16 upstream claims × 8 locked decisions × 3 plan files); verdict NO_PLAN_ADJUSTMENT_NEEDED; v1.3 deferral boundaries confirmed; 17 v1.3-territory upstream details catalogued for the deferred milestone | 2026-05-15 | 8166609 | Complete | [260515-jys-review-ninjamzap-upstream-video-sync-md-](./quick/260515-jys-review-ninjamzap-upstream-video-sync-md-/) |

## Session Continuity

Last session: 2026-05-16T16:59:16.757Z
Stopped at: Phase 20 CONTEXT.md + RESEARCH.md revised post-codex (substrate revision locked, ready for re-review or plan)
Most recent commit on working branch: 4e44a02 (wip: phase-19-uat paused at machine-side complete)

### Phase 19 UAT — actual outcome (machine-side, macOS x86_64)

Session 2026-05-16: ran `/gsd-verify-work 19` (11 UAT cells) with inline fixes for surfaced bugs.

Build numbers: 318 → 319 → 320 → 321.
Commits made this session: 5250ff1, fb98ce0, 9679e7b, 76f509b (and wip 4e44a02).

| Cell | Status | Notes |
|------|--------|-------|
| 1 — Standalone happy path (Intel) | PASS | Surfaced connection-bar overlap → fixed in 5250ff1 (removed legacy Video button, bumped kBaseWidth 1030→1200) |
| 2 — Logic Pro AU happy path | BLOCKED | Logic Pro not installed; user observation that Bitwig works on same code path |
| 3 — REAPER VST3 plugin fallback | PASS | CameraStatusDialog appears with REAPER bundle context |
| 4 — Apple Silicon Standalone | BLOCKED | x86_64 dev machine; defer to Phase 23 universal lane |
| 5 — Mid-session revoke watchdog | BLOCKED_SPEC_GAP | macOS Sonoma+ defers TCC revoke to next launch — System Settings toggle CANNOT produce mid-session frame stall. FrameStallWatchdog code is correct (unit-tested via test_camera_frame_stall.cpp); only the integration trigger spec is wrong. Needs rewrite against USB unplug, scripted AVCaptureSession invalidation, or rolled into Cell 10. |
| 6 — Notarization stapler validate | BLOCKED | Needs Developer-ID-signed + notarized + stapled build; deferred to Phase 23 packaging lane |
| 7 — Windows standalone happy path | BLOCKED | No Windows machine in session; defer to Phase 23-03 |
| 8 — Windows privacy block | BLOCKED | Same as Cell 7 |
| 9 — VDO.Ninja coexistence toast (D-27) | SKIPPED / N-A | Toast condition unreachable after Video button removal in 5250ff1. Recommend retiring D-27 from v1.3 acceptance criteria + removing coexistence-toast code with next VideoCompanion cleanup. |
| 10 — Recheck permission still-denied | PASS (after fix) | Surfaced D-12 silent-no-op gap (D-14 dialog cache suppression suppressed re-show) → fixed in 9679e7b: editor's RecheckPermission handler now calls cameraStatusDialog_.reset() before cam->recheckPermission() |
| 11 — License flow (juce_video AGPL gate) | PASS | Risk C confirmed properly gated |

Net: 4 PASS, 1 SKIPPED (Cell 9), 6 BLOCKED (Cells 2/4/6/7/8 environment, Cell 5 spec gap), 0 ISSUES.

### Decisions captured this session (carry forward — STATE-of-record after HANDOFF.json deletion)

- [Phase 19 UAT]: Removed legacy VDO.Ninja Video button from ConnectionBar (commit 5250ff1). Connection bar's right cluster was overlapping the left cluster at default 1030 px width. With Phase 19 Camera as sole video entry point, the legacy affordance is redundant. kBaseWidth bumped 1030 → 1200 to fit post-removal layout (left ~700 + right ~458 + 130 'Recheck permission' label needs ~1175; 1200 gives ~85 px breathing room).
- [Phase 19 UAT]: Cell 9 (D-27 VDO.Ninja coexistence toast) marked SKIPPED/N-A. Coexistence toast at JamWideJuceEditor.cpp:193 required activating VDO.Ninja via the Video button → VideoPrivacyDialog → launchCompanion(). With that UI path removed, videoCompanion->isActive() cannot become true via user interaction. Recommend retiring D-27 from v1.3 acceptance criteria.
- [Phase 19 UAT]: Cell 5 spec is incompatible with macOS Sonoma+ TCC behavior — needs rewrite. macOS does not enforce TCC revoke mid-session; shows 'Quit & Reopen / Later' modal and defers to next launch. FrameStallWatchdog code is fine. Alternative triggers to consider: USB camera physical unplug, scripted AVCaptureSession invalidation, or accept Cell 5 is exercised only by unit tests + Cell 10's TCC-revoke-on-relaunch path.
- [Phase 19 UAT]: Patched D-12 Recheck-permission silent-no-op gap (commit 9679e7b). When user clicked Recheck while still denied, state machine emitted same cause and D-14 dialog cache silently dismissed the re-show. Fix: editor's Action::RecheckPermission handler now calls cameraStatusDialog_.reset() before cam->recheckPermission(). On still-denied path the dialog re-displays; on now-authorized path no fallback is emitted and the camera opens normally.
- [v1.4+ deferred idea]: Browser-fed JTBv capture path. User raised: 'streaming the video via web again but using this new method with GUID matching?' Phase 14.3's RawDataSendWrite(guid[16], ..., JTBv, ...) is producer-agnostic — a browser companion using WebCodecs/getUserMedia could feed frames into the same pipeline. Unentitled hosts (REAPER) could get camera via the browser. Strictly v1.4+ (v1.3 PASS 3 lock: no VDO.Ninja teardown, no new web stack). Saved to user memory as `project_web_capture_fallback.md`.

### Advisory constraints surfaced this session (carry forward)

- **macOS Sonoma TCC revoke is deferred-to-restart.** Toggling camera permission OFF for a running app on Sonoma+ shows a 'Quit & Reopen / Later' modal and defers actual enforcement until the next app launch. Frames continue flowing through the open AVCaptureSession. Any UAT/test that assumes a System Settings toggle will produce mid-session frame stop is wrong. Use USB unplug, scripted session invalidation, or restart-path testing instead. Discovered via Phase 19 UAT Cell 5.
- **scripts/build.sh default target includes failing test_encryption.** Running `./scripts/build.sh` with no args enumerates all targets including test_encryption.cpp (pre-existing compile failure — encrypt_payload_with_iv undeclared identifier, documented in May-15 HANDOFF as deferred). Workaround: pass JUCE plugin targets explicitly — `./scripts/build.sh JamWideJuce_Standalone JamWideJuce_AU JamWideJuce_VST3 JamWideJuce_CLAP`. ninja's per-target dependency graph correctly skips test_encryption since it's not a dep of those targets. Discovered via Phase 19 UAT first build attempt.
- **Cell 5 UAT spec is documentation gap, not code gap.** FrameStallWatchdog at juce/video/native/JamWideCameraDevice.cpp:83-99 is correctly implemented and unit-tested via tests/test_camera_frame_stall.cpp. The Phase 19 integration UAT Cell 5 description is what's wrong. When rewriting, validate against tests/test_camera_frame_stall.cpp's trigger model, not the System Settings toggle assumption.

### Human actions still pending (all non-blocking)

1. **Decide Phase 19 closeout direction** — close-on-this-platform vs hold-for-Phase-23. Machine-side UAT complete on macOS-x86_64. 6 cells blocked on other environments. Phase 23 packaging will unlock Cell 6; Phase 23-03 CI lane will unlock Cells 4/7/8. Phase 19 is functionally validated for the Intel-Mac happy path + plugin fallback + privacy sequence + license. Closure question is purely milestone bookkeeping.
2. **Rewrite UAT Cell 5** against a trigger macOS actually supports.
3. **Decide `.planning/config.json`** — pre-existing dirty file (newline-EOF fix per May-15 HANDOFF, orthogonal to Phase 19).
4. **(orthogonal) Decide merge timing** for quick/260515-0pc-jamtaba-video-port. Branch is 95 commits ahead of origin/main. Phase 14.3 substrate + Phase 19 native camera both live here. Milestone-merge bookkeeping.
5. **(orthogonal) Address pre-existing Phase 15.1 awaiting-UAT-decision.** Phase 15.1 stabilization at build 290 has been awaiting decision since pre-Phase-14.3. Three 15.1-07a fixes (b8ca083 / 7e69e1a / 00dfee7) need either real-DAW-evidence close OR formal bundled UAT (TSan Standalone rebuild + lldb counter readout + Instruments perf budget). Independent of Phase 19.

### Phase 19 planning artifacts (kept for cross-phase context)

  - `.planning/phases/19-camera-capture-permission-ux/19-01-PLAN-capture-pipeline.md` (Wave 1, 5 tasks) — DONE
  - `.planning/phases/19-camera-capture-permission-ux/19-02-PLAN-ui-and-persistence.md` (Wave 2, 3 tasks) — DONE
  - `.planning/phases/19-camera-capture-permission-ux/19-03-PLAN-fallback-and-verification.md` (Wave 3, 3 tasks — serial after 19-02) — DONE
  - `.planning/phases/19-camera-capture-permission-ux/19-REVIEWS.md` (codex review, all findings addressed in revision 2)

Phase 19 planning summary: research (47d8317) → validation (76d656a) → 3 plans (227bac0) → revision 1 [plan-checker blockers] (0a88d42) → STATE.md (aedb46d) → codex review NEEDS_REVISION (3fb83b0) → revision 2 [reviews mode] (74c9c01) → plan-checker re-verification PASSED → execution → security 7/7 STRIDE closed → validation 7/7 automated + 10 manual UAT cells classified → UAT machine-side 2026-05-16 (4 pass, 1 skip, 6 blocked, 0 issues). 30 locked decisions, 7 STRIDE threats (5 original + T-19-SC license-supply-chain + T-19-PT preview-tile UAF), 10 Wave 0 gaps (8 original + test_frame_distributor_lifetime + test_camera_frame_stall), 5 ROADMAP success criteria all covered. Key architectural patterns introduced in revision 2: Subscription RAII for FrameDistributor (HIGH-2), atomic generation tokens for 5 async-callback sites (HIGH-3), juce::AsyncUpdater for preview-tile repaints (HIGH-4), privacy-modal AFTER auth grant (HIGH-5), continuous frame-stall watchdog for mid-session revoke detection (HIGH-6), Action-enum + actionFor() helper for JUCE button-index mapping (HIGH-7), pure-C++ CameraStateMachine class (MEDIUM-3).

Prior session (Phase 15.1-07b — keep for cross-phase context): 15.1-07b COMPLETE (commits dbdaf98, edb2769) — broadcast restored end-to-end. All 7 audio-thread BufferQueue::AddBlock sites replaced with SPSC try_push. Resume file (v1.2): `.planning/phases/15.1-rt-safety-hardening/15.1-07b-SUMMARY.md`.
