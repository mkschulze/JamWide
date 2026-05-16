# Phase 20: H.264 Encoder & Send Pipeline - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-16
**Phase:** 20-h-264-encoder-send-pipeline
**Areas discussed:** Encoder backend strategy (Q13), Encoder ↔ audio-thread coordination, GOP / keyframe strategy, Per-preset encoder configuration

---

## Encoder Backend Strategy (Q13)

### Q1.1 — Encoder architecture re: openh264-vs-VideoToolbox

| Option | Description | Selected |
|--------|-------------|----------|
| Abstract VideoEncoder interface, openh264 impl | Pure-virtual interface; Phase 23 can plug VideoToolbox later. ~50–100 LOC abstraction. | ✓ |
| Hardcode openh264, refactor later | Smaller Phase 20; Phase 23 pays abstraction + VideoToolbox cost simultaneously at packaging time. | |
| Build openh264 + VideoToolbox both in Phase 20 | Largest scope (+1 plan, ~800 LOC); concentrates codec-bringup risk in one phase. | |
| Let Claude decide | (catch-all) | |

**User's choice:** Abstract VideoEncoder interface, openh264 impl.
**Notes:** Closes outstanding blocker Q13 from STATE.md.

### Q1.2 — VideoEncoder interface shape

| Option | Description | Selected |
|--------|-------------|----------|
| Owns-its-thread + SPSC output | Subscribes to FrameDistributor; does BGRA→YUV internally; writes encoded frames to output SPSC. Phase 15.1-aligned. | ✓ (later partially superseded — see Path correction below) |
| Callback-style (port FFMpegMuxer pattern) | std::function callback on encoder thread; caller marshals across threads. | |
| Synchronous push/pull | Caller drives encoder externally; simplest interface. | |
| Let Claude decide | | |

**User's choice:** Owns-its-thread + SPSC output.
**Notes:** Subsequently in Area 2 Q3 the encoder→audio SPSC handoff was removed under the HYBRID correction; the encoder still owns its thread + still has an INPUT SPSC (from FrameDistributor), but it Sends frame chunks directly via Phase 14.3 substrate rather than via a chunk-handoff ring.

### Q1.3 — SPS/PPS flow from encoder to audio thread

| Option | Description | Selected |
|--------|-------------|----------|
| Atomic pointer swap | std::atomic<SpsPpsBuffer*>; deferred-delete on run thread; lock-free. | ✓ |
| Inline in SPSC ring as special frame type | Encoder emits SpsPpsRecord and FrameRecord in same ring; mixing payload types. | |
| NinjamZap-faithful (mutex) | WDL_Mutex on audio thread; violates Phase 15.1 D-01. | |
| Let Claude decide | | |

**User's choice:** Atomic pointer swap. (Path B fallback: WDL_Mutex if Path A fails sync gates.)

### Q1.4 — Encoder reconfiguration (preset change / fatal error / resolution change)

| Option | Description | Selected |
|--------|-------------|----------|
| Tear-down + rebuild + republish SPS/PPS | Encoder thread closes openh264, opens new, generates fresh SPS/PPS, atomic-swaps. | ✓ |
| Soft reconfigure | openh264 SetOption for runtime param update; resolution still needs teardown. | |
| No reconfigure — disconnect/reconnect | User-hostile. | |
| Let Claude decide | | |

**User's choice:** Tear-down + rebuild.

### Q1.5 — H.264 profile + level

| Option | Description | Selected |
|--------|-------------|----------|
| Baseline 3.1 | NinjamZap-aligned (iOS uses Baseline). No B-frames; mobile-compat. | ✓ |
| Main 4.0 | ~10–15% better compression; 1-frame decode latency. | |
| High 4.0 | Best compression; older mobile decoders may lack support. | |
| Let Claude decide | | |

**User's choice:** Baseline 3.1.

### Q1.6 — openh264 rate-control mode

| Option | Description | Selected |
|--------|-------------|----------|
| RC_BITRATE_MODE | Average-bitrate target; predictable per-interval payload. | ✓ |
| RC_QUALITY_MODE | Variable bitrate to hit quality target; spikes problematic for congestion-drop. | |
| RC_BUFFERBASED_MODE | Hybrid bitrate + buffer-aware; more overhead. | |
| Let Claude decide | | |

**User's choice:** RC_BITRATE_MODE.

### Q1.7 — Backpressure when encoder is behind

| Option | Description | Selected |
|--------|-------------|----------|
| Drop oldest unencoded + bump counter | FrameDistributor input SPSC fixed depth; full → overwrite oldest + increment m_encoder_input_drops. | ✓ |
| Drop newest | Encoder finishes older queue first; less responsive to motion. | |
| Block camera-frame producer | Stall JUCE camera callback; risky. | |
| Let Claude decide | | |

**User's choice:** Drop oldest + counter (Phase 15.1-05 fallback semantics).

---

## Encoder ↔ Audio-Thread Coordination

### Q2.1 — Which thread emits RawDataSendBegin/Write for the video channel?

| Option | Description | Selected |
|--------|-------------|----------|
| Audio thread emits ALL video bytes (mislabeled NinjamZap-faithful) | Audio thread emits END/BEGIN/marker/SPS-PPS + drains encoder SPSC into per-frame writes. | Initially selected, then corrected |
| Hybrid: audio thread emits framing, encoder thread emits frame payloads | Audio thread does END/BEGIN/marker/SPS-PPS; encoder thread calls RawDataSendWrite directly per frame. | ✓ (after correction) |
| Encoder thread emits everything via deferred-trigger | Audio thread sets atomic interval_tick; encoder polls and emits everything. | |
| Let Claude decide | | |

**User's initial choice:** Option 1.
**Correction applied 2026-05-16T15:00:** Grep of NinjamZap source (njclient.cpp:2116-2123 QueueVideoFrame from encoder thread + njclient.cpp:3041-3082 audio thread does framing) + Phase 14.3-02 SUMMARY (substrate accepts any-thread producer via bounded SPSC) confirmed that Option 2 is the actual NinjamZap-faithful pattern. Option 1 as I described it was a JamWide-novel consolidation, not NinjamZap-faithful. User confirmed the correction.
**User's final choice:** Hybrid emission (Option 2).
**Notes:** User followed up with: "we've been struggeling with all our audio/video/interval sync work so far and I know that Ninjamzap synchronisation appraoch works because I tested it. So we can test path A but if it doesn't work, we should definetly follow the ninjamzap implementation exactly." — established the Path A primary / Path B fallback framing; saved to memory as `feedback_proven_over_pure`.

### Q2.2 — UI thread toggles m_video_active

| Option | Description | Selected |
|--------|-------------|----------|
| std::atomic<bool> with acquire/release | Lock-free; simple. | ✓ (Path A) |
| Phase 15.1-style LocalChannelUpdate SPSC | Reuses Phase 15.1-06 mechanism; more plumbing; ordering with channel events. | |
| Dedicated VideoStateUpdate SPSC | Cleaner separation; over-engineered for a single bool. | |
| Let Claude decide | | |

**User's choice:** atomic<bool> with acquire/release.
**Notes:** Path B fallback uses WDL_Mutex m_video_cs (NinjamZap-literal).

### Q2.3 — Encoder thread → audio thread per-frame chunk handoff

| Option | Description | Selected |
|--------|-------------|----------|
| SPSC ring of {ptr, len, isKeyframe, encoder_seq} records | Encoder pushes pointer+len; audio drains during on_new_interval. | Initially selected, then OBSOLETED |
| Pre-allocated frame pool + indices through SPSC | Zero heap after startup; bounded memory; complex; large pool for variable frame sizes. | |
| Inline byte-copy into Send | Audio thread copies bytes; the expensive operation we wanted to avoid. | |
| Let Claude decide | | |

**User's initial choice:** SPSC ring of records (Option 1).
**Correction applied 2026-05-16T15:00:** Under the HYBRID model (Q2.1 corrected answer), the encoder thread calls RawDataSendWrite directly — no chunk-handoff SPSC needed between encoder and audio. Phase 14.3 substrate's m_rawdata_sendq handles cross-thread Send safety.
**Final decision:** No chunk-handoff SPSC between encoder and audio thread. Encoder thread Sends directly via Phase 14.3 substrate. Backpressure (drop-oldest) still applies on the FrameDistributor→encoder INPUT SPSC (Q1.7), but not at the encoder→Send boundary.

### Q2.4 — Cold-start SPS/PPS behaviour (first interval after Broadcast click)

| Option | Description | Selected |
|--------|-------------|----------|
| Emit marker only; conditionally emit SPS/PPS | NinjamZap pattern (njclient.cpp:3075 'if size > 0'). First interval marker-only; SPS/PPS emerges from interval 2+. | ✓ |
| Defer first BEGIN until SPS/PPS ready | Sender stays in 'about to broadcast' state until next interval after encoder init. | |
| Pre-warm: encoder init at camera-open | Encoder runs idle while camera is on but not broadcasting; ~3W CPU cost 24×7. | |
| Let Claude decide | | |

**User's choice:** Conditional SPS/PPS emit (NinjamZap-faithful). Encoder lifecycle = start at broadcast-on.

---

## GOP / Keyframe Strategy

### Q3.1 — IDR keyframe placement relative to NINJAM intervals

| Option | Description | Selected |
|--------|-------------|----------|
| One IDR at the start of every interval | Predictable per-interval bandwidth; mid-stream joiners decode immediately. | ✓ |
| IDR every N intervals (long GOP, N=4 or 8) | ~15–25% bandwidth savings; joiners wait N-1 intervals. Complicates Phase 21 receive. | |
| IDR on demand + key-frame request RPC | NinjamZap doesn't have it; would be JamWide-only extension. | |
| Let Claude decide | | |

**User's choice:** One IDR per interval.

### Q3.2 — How encoder learns about interval boundaries

| Option | Description | Selected |
|--------|-------------|----------|
| Atomic interval counter polled by encoder per frame | std::atomic<uint64_t> m_audio_interval_seq; encoder polls before each encode. Up to 1 frame drift. | ✓ |
| SPSC notification ring from audio thread | IntervalStartUpdate {seq, audio_ch0_guid, swap_count}; tighter alignment; more plumbing. | |
| openh264 auto-IDR period (fps × interval_seconds) | uiIntraPeriod = derived from BPM/BPI; drifts on BPM/BPI changes. | |
| Let Claude decide | | |

**User's choice:** Atomic interval counter (option 1).

---

## Per-Preset Encoder Configuration

### Q4.1 — Bitrate ladder for the three presets

| Option | Description | Selected |
|--------|-------------|----------|
| Spike-validated → measurement-derived | Low → 100 kbps, Medium → 300 kbps, High → 800 kbps. Matches RESEARCH-ADDENDUM sizing table. | ✓ |
| Conservative ladder | Low → 64 kbps, Medium → 200 kbps, High → 500 kbps. More headroom; lower quality. | |
| Aggressive ladder | Low → 150 kbps, Medium → 500 kbps, High → 1.2 Mbps. Higher quality; more congestion-drop risk. | |
| Let Claude decide | | |

**User's choice:** Spike-validated ladder.

### Q4.2 — Auto adaptive preset?

| Option | Description | Selected |
|--------|-------------|----------|
| Defer Auto to a later phase | Ship Low/Medium/High only. Auto requires server-side or client-side feedback signal; ~200–500 LOC + tuning. | ✓ |
| Auto via client-side TCP send-queue depth | Encoder polls send-queue depth every ~500 ms; steps presets. Tuning-heavy. | |
| Auto via server-side per-subscriber drop count | Requires new NINJAM message extension + server cooperation. Out of doc-only Q8 scope. | |
| Let Claude decide | | |

**User's choice:** Defer Auto to v1.4+.

### Q4.3 — Video channel registration timing

| Option | Description | Selected |
|--------|-------------|----------|
| On NINJAM connection-up, reserve chidx=1 always | Other clients see 'video capable' from connect; conflict detection early; matches NinjamZap mobile. Zero bandwidth cost when not broadcasting. | ✓ |
| On camera-open | Channel registration deferred until camera available; more state machine complexity. | |
| On broadcast-on | Cleanest semantic; latest conflict detection. | |
| Let Claude decide | | |

**User's choice:** Connection-up registration; chidx=1 always reserved.

---

## Claude's Discretion

The following decisions were explicitly deferred to Claude / the planner:

- Specific atomic memory ordering for non-critical atomics (e.g., IDR-sync counter — `relaxed` vs `acquire/release`).
- Slice mode (single-slice vs multi-slice per frame in openh264). Default single-slice.
- Encoder-side frame-stall watchdog (mirror Phase 19's camera watchdog). Granularity is Claude's call.
- openh264 speed/quality preset params beyond profile + RC mode + GOP control (e.g., `iLoopFilterDisableIdc`, `iMultipleThreadIdc`). Default single-threaded encoder.
- Encoder thread lifecycle around plugin reload / DAW session save-restore.
- Debug logging surface — what to log via `juce::Logger::writeToLog`.
- File layout — likely `juce/video/encoder/` subdirectory.

## Deferred Ideas

Ideas mentioned during discussion that were noted for future phases:

- VideoToolbox / MediaFoundation backends (post-Phase-23 or v1.4).
- "Auto" adaptive-bitrate 4th preset (v1.4+).
- Bandwidth display in UI (Phase 22 or 24 polish).
- Encoder error surface in CameraStatusDialog (planner discretion).
- Multi-slice encoding (defer until profiling shows benefit).
- `docs/CAMERA.md` user-facing broadcast doc (Phase 24).
- Web-companion-fed JTBv capture path (v1.4+; saved to memory as `project_web_capture_fallback`).
- Audio-thread budget measurement (lands in PLAN.md as acceptance criterion + Path B trigger).

## Corrections Applied During Session

- **2026-05-16T15:00** — Area 2 Q1 answer flipped from "audio thread emits all bytes" to "HYBRID" after I confirmed via grep of NinjamZap source + Phase 14.3-02 SUMMARY that the audio=framing / encoder=per-frame pattern is the reference. Area 2 Q3 chunk-handoff SPSC OBSOLETED. User confirmed correction. All other decisions intact.

## Session Memory Updates

- **`feedback_proven_over_pure.md` — created.** Captures the principle that Phase 15.1 architectural invariants are negotiable in sync/timing/codec domains with documented carve-outs and capped fix-attempt budgets. Sourced from user's "we've been struggeling with all our audio/video/interval sync work so far" + "I know that Ninjamzap synchronisation approach works because I tested it" + "if it doesn't work, we should definetly follow the ninjamzap implementation exactly."
