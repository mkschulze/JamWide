# Phase 20: H.264 Encoder & Send Pipeline - Context

**Gathered:** 2026-05-16
**Status:** Ready for planning

<domain>
## Phase Boundary

Webcam frames captured by Phase 19's `JamWideFrameDistributor` are H.264-encoded via vendored Cisco openh264 (libavcodec backend) and broadcast as NinjamZap-compatible video intervals on NINJAM channel 1, bit-for-bit wire-identical to NinjamZap mobile and the ninjamzap-core reference. This phase covers:

1. An abstract `VideoEncoder` interface + one openh264-backed implementation, owning its own thread; subscribes to `JamWideFrameDistributor` (BGRA `juce::Image` in), produces H.264 NAL units (encoded bytes out).
2. The send-side video state machine wired into `NJClient::on_new_interval` (audio thread): END (if open) → BEGIN → 24-byte interval marker → SPS/PPS (conditionally) → END at deactivate.
3. The per-frame chunk path: encoder thread heap-allocates each frame's `[4B BE length-prefix][NAL payload]` buffer and calls `NJClient::RawDataSendWrite` directly, leveraging Phase 14.3 substrate's any-thread-producer SPSC (capacity 64; run thread is sole `Net_Connection::Send` caller).
4. Channel registration at NINJAM-connection time: `SetLocalChannelInfo(chidx=1, name="video", flags=0x10)` + `SetVideoChannel(chidx=1, fourcc=H264)` + `NotifyServerOfChannelChange`.
5. Cross-thread coordination primitives: `std::atomic<bool> m_video_active`, `std::atomic<SpsPpsBuffer*> m_video_spspps` (with deferred-delete on the run thread), `std::atomic<uint64_t> m_audio_interval_seq` (audio→encoder for IDR sync). All audio-thread-side accesses are lock-free under the Path A primary architecture.
6. Per-preset bitrate ladder mapped from Phase 19's Low/Medium/High capture presets: 100 / 300 / 800 kbps target via `RC_BITRATE_MODE`.

**Maps to:** Requirements **COD-01** (openh264 encode), **COD-02** (4-byte BE chunking), **WIRE-01** (fourCC `H264` + 24-byte marker + SPS/PPS chunk #2 + per-frame length prefix), **WIRE-03** (concurrent audio+video producer arbitration).

**Out of scope:** Receive pipeline + 4-stage promotion + GUID-pairing decision tree (Phase 21), per-user video tile rendering and popouts (Phase 22), macOS universal + Windows packaging + per-dylib codesigning + signtool (Phase 23), per-DAW UAT + NinjamZap scenario port + beta validation (Phase 24). Phase 20 does NOT add a VideoToolbox backend (architected for it, deferred to a follow-up phase). Phase 20 does NOT add an "Auto" adaptive-bitrate preset (deferred to v1.4+).

</domain>

<decisions>
## Implementation Decisions

### Encoder Backend Strategy (Q13 resolved)

- **D-01: Abstract `VideoEncoder` pure-virtual interface, one openh264 implementation in Phase 20.** Future VideoToolbox / MediaFoundation impls plug into the same interface without touching call sites. Adds ~50–100 LOC of abstraction. Closes blocker Q13 (VideoToolbox vs openh264 from day 1).
- **D-02: Encoder owns its own thread + BGRA→YUV420P conversion internally (via `libswscale sws_scale`).** Subscribes to `JamWideFrameDistributor` per Phase 19 D-02 / D-04. Reading BGRA `juce::Image` in; emitting H.264 NAL units + SPS/PPS out.
- **D-03: SPS/PPS published from encoder to audio thread via `std::atomic<SpsPpsBuffer*>` with acquire/release ordering.** On first emit and after any reconfigure (preset change, fatal error, resolution change), encoder allocates an immutable `[SPS-NAL][PPS-NAL]` buffer and `store(release)`. Audio thread does `load(acquire)` + `memcpy` into the chunk during `on_new_interval`. Old buffer goes to the Phase 15.1-05 deferred-delete SPSC; run thread frees it after the audio thread has provably advanced past the swap point.
- **D-04: Encoder reconfiguration is always tear-down + rebuild + republish SPS/PPS.** Triggered by preset change, fatal openh264 error, or resolution change. Encoder thread closes the current openh264 instance, opens a new one with the new params, generates fresh SPS/PPS, atomic-swaps the buffer. Audio thread reads the new pointer at the next `on_new_interval`. ~1 frame interval stall during teardown is acceptable.
- **D-05: H.264 Baseline profile, level 3.1.** No B-frames (lower encode latency, lower decoder memory). NinjamZap-aligned (iOS app uses Baseline via `VTCompressionSession`). Universally decodable by mobile + desktop libavcodec.
- **D-06: openh264 `RC_BITRATE_MODE` (average-bitrate target).** Matches the spike's measured ~98 kbps at 320×240@10fps. Lets ninjamzap-server `VideoCongestionThreshold` (default 50%) work as designed — average-bitrate target means per-interval payload is predictable enough for the per-subscriber drop-decision to be meaningful.
- **D-07: Drop-oldest backpressure on the `JamWideFrameDistributor → encoder` input SPSC + observable counter `m_encoder_input_drops`.** When the input SPSC is full, the new frame overwrites the oldest unencoded one and `m_encoder_input_drops` is incremented. Mirrors Phase 15.1-05 fallback semantics (RT-safety > completeness). Non-zero counter at phase close fails Phase 20's verification gate (parallel to Phase 15.1-10's deferred-delete-overflows == 0 assertion).

### Encoder ↔ Audio-Thread Coordination (HYBRID — NinjamZap-faithful)

- **D-08: HYBRID emission model.** Audio thread (in `NJClient::on_new_interval`) emits the interval framing: END (if `m_video_interval_open`) → BEGIN → 24-byte marker (`[4B BE prefix=20][4B BE swap_count][16B audio_ch0_guid]`, all stack-allocated) → SPS/PPS (atomic-load from `m_video_spspps`, conditional on non-null) → END at deactivate. Encoder thread emits per-frame payload chunks: it heap-allocates each frame's `[4B BE length-prefix][NAL bytes]` buffer and calls `NJClient::RawDataSendWrite(m_video_guid, buf, len, false)` directly. Phase 14.3 substrate's any-thread-producer SPSC (`m_rawdata_sendq`, capacity 64) handles cross-thread Send safety automatically; run thread is the sole `Net_Connection::Send` caller. **Verbatim port of NinjamZap's pattern** (`njclient.cpp:2116-2123 QueueVideoFrame` from encoder thread + `njclient.cpp:3041-3082 on_new_interval video block` on audio thread).
- **D-09: Path A primary (atomic primitives); Path B explicit fallback (NinjamZap-literal `WDL_Mutex`).** Path A uses `std::atomic<bool> m_video_active` + `std::atomic<SpsPpsBuffer*> m_video_spspps` and is Phase 15.1-compliant (lock-free audio path). Path B replaces both with `WDL_Mutex m_video_cs` and `WDL_Mutex m_video_spspps_cs` taken on the audio thread inside `on_new_interval`, byte-for-byte mirroring NinjamZap source. **Path B does NOT change the HYBRID threading model — only the synchronization primitives differ.** Estimated edit: ~30 LOC + one `realtime-audio-reviewer` audit allow-list entry documenting the carve-out.
- **D-10: Path B trigger is objective, capped, and recorded as a Plan-level acceptance criterion.** Trigger: if 2 fix attempts under Path A cannot pass the critical NinjamZap sync scenarios (specifically the equivalents of `02_video_one_interval_early.cpp`, `04_late_join_midstream.cpp`, `06_audio_video_resync.cpp` — actual filenames TBD per Phase 21's scenario port). Path B is the documented carve-out; switching is a localized edit + an audit-allowlist update + a CONTEXT.md amendment. **Rationale:** sync work has burned too many cycles to absorb dogma; the user has direct evidence NinjamZap's mutex-based sync is correct (see `feedback_proven_over_pure` memory).
- **D-11: `m_video_active` toggle is `std::atomic<bool>` with acquire/release ordering.** UI thread sets `m_video_active.store(true, release)` when user clicks Camera → Broadcast. Audio thread reads `m_video_active.load(acquire)` at the top of the video block in `on_new_interval`. Release/acquire pairing guarantees the SPS/PPS atomic pointer is published before `m_video_active` flips on, so the audio thread can never observe "active but no SPS/PPS yet." Under Path B fallback: `WDL_Mutex m_video_cs.Enter()/Leave()`.
- **D-12: No chunk-handoff SPSC between encoder and audio thread.** The encoder thread Sends each frame chunk directly via Phase 14.3 substrate. The substrate's `m_rawdata_sendq` (any-thread-producer, run-thread-consumer, capacity 64) handles cross-thread safety. Substrate's `m_rawdata_sendq_overflows` counter must remain zero at phase close (carries Phase 14.3-02's Codex M-8 overflow-counter invariant).
- **D-13: Cold-start SPS/PPS handling follows NinjamZap (`njclient.cpp:3075`'s `if size > 0` pattern).** Audio thread always emits END/BEGIN/marker. SPS/PPS is emitted as the second chunk **only if** `m_video_spspps.load(acquire) != nullptr`. First broadcast interval after the user clicks Broadcast may be marker-only (encoder still initializing ~50–150 ms); subsequent intervals carry SPS/PPS. Encoder thread lifecycle: starts at broadcast-on (not at camera-open) to keep idle CPU low when the user is previewing without broadcasting.

### GOP / Keyframe Strategy

- **D-14: One IDR keyframe at the start of every NINJAM interval.** Predictable per-interval bandwidth; mid-stream joiners decode the very first interval they see; simplifies Phase 21 receive pipeline (each interval is self-contained). Bandwidth cost is ~15–25% higher than long-GOP at same quality, but the IDR-per-interval discipline is what makes the GUID-pairing receive-side semantics clean.
- **D-15: Encoder learns about interval boundaries via `std::atomic<uint64_t> m_audio_interval_seq`.** Audio thread increments the counter at the top of every `on_new_interval`. Encoder thread loads the counter before each frame encode; when it changes vs the last-observed value, the encoder sets openh264's `eForceIntraFrame` flag for THIS frame, forcing IDR. Up to 1 frame of drift between actual interval boundary and IDR (acceptable: ~33–100 ms at our frame rates). Lock-free; naturally handles BPM/BPI changes mid-session (interval cadence shifts, counter still increments at every actual boundary).

### Per-Preset Encoder Configuration

- **D-16: Bitrate ladder: Low → 100 kbps, Medium → 300 kbps, High → 800 kbps.** Spike-validated baseline (320×240@10fps measured ~98 kbps) + RESEARCH-ADDENDUM's sizing table. Each preset is fixed; verified by Phase 20 acceptance criterion ("two users, 5-minute broadcast at each preset, no audio glitches, no `m_rawdata_sendq_overflows` increment, ninjamzap-server congestion-drop counter below threshold").
- **D-17: No "Auto" adaptive-bitrate preset in v1.3.** Deferred to v1.4+ — requires either a server-side per-subscriber drop signal piped back to the client (new NINJAM message extension; ninjamzap-server has the metrics but JamWide's doc-only Q8 path does not include server-side changes) or a client-side TCP send-queue-depth heuristic (risk of oscillation, ~200 LOC of tuning). Three fixed presets are shippable beta surface; Auto is an evolution.
- **D-18: Video channel is registered at NINJAM-connection time.** When JamWide connects to a server, `NJClient` immediately calls `SetLocalChannelInfo(chidx=1, name="video", flags=0x10)` + `SetVideoChannel(chidx=1, fourcc=H264)` + `NotifyServerOfChannelChange`. Channel-index conflict detection happens at connect time; other clients see "user has video capability" from the moment of connect regardless of camera/broadcast state. Bandwidth cost zero when not broadcasting (no payload sent until broadcast-on). Matches NinjamZap mobile semantics.

### Claude's Discretion

- **Memory ordering details** for non-critical atomics (e.g., the IDR-sync counter — `relaxed` vs `acquire/release`). Planner picks per Standard `<atomic>` reasoning.
- **Slice mode** (single-slice vs multi-slice per frame in openh264). Default to single-slice (NinjamZap iOS default; one NAL unit per frame). Planner may switch to multi-slice if profiling shows decode-side benefits.
- **Encoder frame-stall watchdog** — mirror Phase 19's camera frame-stall watchdog on the encoder side (detect encoder hang via wall-clock gap between input frames consumed). Planner picks granularity.
- **openh264 speed/quality preset** (`uiIntraPeriod` is set by the IDR-sync counter; other params like `iLoopFilterDisableIdc`, `iMultipleThreadIdc` — planner picks defaults). Default to single-threaded (`iMultipleThreadIdc=1`) until profiling shows multi-threading is worthwhile at our resolutions.
- **Encoder thread lifecycle around plugin reload / DAW session save-restore** — planner picks; the simple default is "encoder lifetime tied to broadcast-active flag; plugin reload starts fresh."
- **Debug logging surface** during encoder bring-up (via `juce::Logger::writeToLog` per Phase 19 D-23 pattern). Planner picks what to log; default to encoder open/close, SPS/PPS regenerate, bitrate set, fatal errors.
- **File layout**: likely a new `juce/video/encoder/` subdirectory with `VideoEncoder.h` (interface), `Openh264Encoder.h/.cpp` (impl), `VideoChunker.h/.cpp` (length-prefix wrapping helper, if separable from encoder).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Wire-format spec (authoritative)

- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-RESEARCH-ADDENDUM.md` — **NinjamZap wire format spec section** (locked); decision tables; cross-platform reality check; open questions Q8–Q13.
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` — Item D (encoder + interval-frame chunker port reference: `FFMpegMuxer.cpp`); Item F.1 (send-path video state machine); Item F.4 (`Net_Connection::Send` thread-safety mitigation — already implemented in Phase 14.3 substrate).
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-spike-results.md` — measured spike numbers (320×240@10fps, ~98 kbps, 4% CPU, 5.2 MB ffmpeg per arch); Q3 thread-safety resolution; Q7 fourCC byte-order resolution.

### Substrate (Phase 14.3 — dependency baseline)

- `.planning/phases/14.3-native-video-foundation/14.3-SPEC.md` — RawData send/receive API spec, success criteria 4 (`RawDataSendBegin/Write` callable from any thread, internally serialized).
- `.planning/phases/14.3-native-video-foundation/14.3-02-SUMMARY.md` — As-shipped substrate: `m_rawdata_sendq` SPSC capacity 64; run-thread drain block at `njclient.cpp:2697-2776`; Pattern C discard-on-null guard; `m_rawdata_sendq_overflows` counter; `is_video_fourcc` helper recognizes `H264 / VP8 / MJPG`.
- `.planning/phases/14.3-native-video-foundation/14.3-03-SUMMARY.md` — Receive-path dispatch fix; unknown-fourCC no longer routes to Vorbis decoder.

### Prior phase dependencies

- `.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md` — Phase 19 D-02 (`JamWideFrameDistributor` SPSC subscriber pattern), D-04 (encoder subscriber owns BGRA→YUV420P conversion), D-09/D-11 (camera state independent of broadcast/connect), D-18 (three capture presets), D-25 (`<camera>` ValueTree subtree with `qualityPreset` field), D-29 (Phase 20 owns ALL audio-thread integration for camera).
- `.planning/phases/15.1-rt-safety-hardening/15.1-CONTEXT.md` — D-01 (SPSC-mediated audio-thread state updates), D-02 (deferred-delete SPSC pattern for heap deallocations off audio thread), D-07 (TSan dual-scope verification gate).

### NinjamZap reference (port from these — read source verbatim)

- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:200-236` — public API surface for `RawDataSendBegin/Write`, `SetVideoChannel`, `QueueVideoFrame`, `SetVideoSPSPPS`.
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2047-2123` — send-side impl: `RawDataSendBegin` (line 2047), `RawDataSendWrite` (line 2064), `QueueVideoFrame` (line 2116, called from encoder thread — direct `RawDataSendWrite` at line 2120).
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3041-3082` — sender's `on_new_interval` video block: END/BEGIN/marker/SPS-PPS pattern (audio thread).
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2092-2102` — Cold-start SPS/PPS path (`QueueVideoFrame` may initiate BEGIN if encoder is ready before on_new_interval ticks).

### JamWide integration points

- `src/core/njclient.h` (post-14.3-02) — public RawData API + `RawData_Callback` slot + `m_rawdata_sendq` SPSC + `m_rawdata_sendq_overflows` counter + `GetRawDataSendQueueOverflowCount()` accessor. Phase 20 adds: `m_video_active` (atomic bool), `m_video_spspps` (atomic SpsPpsBuffer*), `m_audio_interval_seq` (atomic uint64), `m_video_chidx` (int = 1), `m_video_guid[16]` (set at first SetVideoChannel call), `m_video_fourcc` (= `MAKE_NJ_FOURCC('H','2','6','4')`), `m_video_interval_open` (audio-thread-local bool), `m_encoder_input_drops` (atomic uint64).
- `src/core/njclient.cpp:154-155` — existing `MAKE_NJ_FOURCC('O','G','G','v')` + `'F','L','A','C'` patterns; Phase 20 adds `MAKE_NJ_FOURCC('H','2','6','4')` here (already exists as constant per 14.3-02's `is_video_fourcc` helper at line 233).
- `src/core/njclient.cpp` `on_new_interval` — Phase 20 inserts the video block (END/BEGIN/marker/SPS-PPS) after the existing audio interval handling. Marker construction uses already-on-audio-thread `m_sync_interval_cnt` + Phase 15.1-06 `LocalChannelMirror` for `audio_ch0_guid`.
- `juce/JamWideJuceProcessor.{h,cpp}` — Phase 20 adds the `VideoEncoder` owner (constructed when camera opens; starts encoder thread when broadcast toggles on).
- `juce/ui/ConnectionBar.cpp` (post-Phase-19) — Phase 19 wired the Camera button + state machine + preset right-click menu. Phase 20 adds the Broadcast toggle (could be a secondary state on the Camera button or a separate Broadcast button — planner picks; user pref is consistency with existing button semantics).
- `libs/ffmpeg/*/include/` and `libs/ffmpeg/*/lib/` — Phase 14.3-01 vendored libavcodec + libavformat + libavutil + libswscale + libopenh264. Phase 20 consumes via the existing `cmake/ffmpeg.cmake` IMPORTED INTERFACE target.

### JamTaba reference (encoder port reference only; non-authoritative)

- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:99` — `QThreadPool(1)` worker pattern; replaced in Phase 20 with `juce::Thread` or `std::thread`.
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:237-277` — openh264 encoder configure block; the bitrate / GOP / profile params are the spike-validated baseline to start from.
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:541-577` — RGB→YUV macro; Phase 20 uses `libswscale sws_scale` instead (more portable, NV12/YUV420P-aware).
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:586-643` — raw NAL bytestream (no AVFormat container); Phase 20 mirrors this approach.

### Memory references

- `feedback_proven_over_pure` — Phase 15.1 architectural invariants are negotiable in sync/timing/codec domains with documented carve-out and capped fix-attempt budget. **Anchors D-09/D-10's Path A/B framing.**
- `project_jamtaba_video_port` — milestone-level project memory; SUPERSEDED references kept for historical context.
- `feedback_uat_scope_redflags` — user's UAT discipline (verify the user-visible happy path; don't let executor "verify only X, skip Y" pass for broadcast). Phase 20's UAT must cover: connect 2 users → broadcast video at each preset → 5-minute session → audio glitch-free + counters at zero.
- `feedback_legacy_invariant_audit` — when adding mirror state on the audio thread, grep all writes to the indexing/state field (not just inside migrated functions). Phase 20 must check: every audio-thread access to `m_curwritefile.guid` (for the marker's audio_ch0_guid) is consistent with Phase 15.1-06 LocalChannelMirror semantics.
- `feedback_size_constant_lifecycle_audit` — Phase 14.3-02's `RAWDATA_SEND_QUEUE_CAPACITY = 64` is a sized constant; if Phase 20 finds 64 too small (high-fps preset under full broadcast may push more than ~63 items between drain cycles), audit the allocation lifecycle impact before bumping.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`NJClient::RawDataSendBegin/Write` substrate** (Phase 14.3-02) — Phase 20's primary consumer. Any-thread producer, bounded SPSC (64), run-thread drain, Pattern C discard guard. No further substrate work required.
- **`JamWideFrameDistributor`** (Phase 19) — Frame source. `VideoEncoder` registers as a subscriber via SPSC and receives BGRA `juce::Image` frames at the configured capture rate. Subscription RAII (Phase 19 HIGH-2) handles encoder lifetime.
- **Phase 15.1-05 deferred-delete SPSC** — Phase 20 reuses for `SpsPpsBuffer*` lifetime management (old buffers queued for run-thread free after atomic pointer swap).
- **`cmake/ffmpeg.cmake` IMPORTED INTERFACE** (Phase 14.3-01) — Phase 20 links `libavcodec` + `libavutil` + `libswscale` + `libopenh264` via the existing target.
- **`juce::Thread` patterns in codebase** (e.g., `NinjamRunThread`) — encoder thread implementation reference.
- **`std::atomic<T>` + `release/acquire` pairings** (Phase 15.1-04 SPSC infrastructure) — pattern for the Path A primitives.

### Established Patterns

- **Audio thread is sacred** (Phase 15.1 D-01). Phase 20 honours this under Path A by keeping all audio-thread reads as lock-free atomic loads + stack-allocated payloads. Path B is the documented escape hatch.
- **Counter-and-accessor for overflow events** (`GetBlockQueueDropCount`, `GetDeferredDeleteOverflowCount`, `GetRawDataSendQueueOverflowCount`). Phase 20 adds `GetEncoderInputDropCount()` following the same idiom.
- **SetVideoChannel before NotifyServerOfChannelChange** — NinjamZap pattern; mirrors `SetLocalChannelInfo` semantics that JamWide already uses for audio channels.
- **State persistence via ValueTree `<camera>` subtree** (Phase 19 D-25). Phase 20 may extend with broadcast-on persistence (e.g., remember "last broadcast was on" — open question, planner decides if it makes sense for v1.3).
- **`juce::Logger::writeToLog` for plugin-side events** (Phase 19 D-23). Phase 20 follows the same pattern for encoder open/close/error/SPS-PPS-regen events.

### Integration Points

- **`NJClient::on_new_interval`** (`src/core/njclient.cpp`, audio thread) — Phase 20 inserts the video block (END / BEGIN / marker / SPS-PPS / END-at-deactivate). Marker construction is fully on-stack; SPS/PPS is one atomic load + memcpy; no heap allocation, no mutex.
- **`NJClient::Run`** (`src/core/njclient.cpp`, run thread) — already drains `m_rawdata_sendq` post-14.3-02. Phase 20 may add a Phase 15.1-05-style `m_spspps_deferred_delete_q` drain alongside (run thread frees old `SpsPpsBuffer*`s after atomic swap).
- **`JamWideJuceProcessor`** — owns the `VideoEncoder` (constructed on camera-open, encoder thread starts on broadcast-on). Tied to Phase 19's existing camera owner.
- **`JamWideJuceProcessor::processBlock` → `client->AudioProc`** — unchanged; the audio thread already runs through `on_new_interval`, so the video block lands inline.
- **`ConnectionBar`** — Phase 20 wires a Broadcast toggle. Surface choice (button-state-on-Camera vs. separate Broadcast button) is Claude's discretion in planning, defaulting to consistency with the existing button state-machine pattern.
- **`NinjamRunThread::run` → connection-up callback** — Phase 20 adds the channel-registration call (`SetLocalChannelInfo` + `SetVideoChannel` + `NotifyServerOfChannelChange`) when the NINJAM session reaches authenticated state.

</code_context>

<specifics>
## Specific Ideas

- **24-byte marker layout (locked, NinjamZap):** `[4B BE prefix=20][4B BE swap_count from m_sync_interval_cnt][16B audio_ch0_guid]`. If `m_video_active` is on but the local user's audio channel 0 has no `m_curwritefile.guid` yet (broadcaster muted at startup, or audio paused), the marker emits 16 zero bytes — receivers treat zero-GUID as "fall back to NONE-match path."
- **SPS/PPS chunk format:** raw `[SPS-NAL][PPS-NAL]` concatenation, no per-NAL length prefix (NinjamZap pattern). Encoder produces them once per session; reconfigure tears down and produces fresh.
- **Per-frame chunk format:** `[4B BE length][NAL or NAL-group bytes]`. Length prefix is the SAME 4-byte BE convention as the 24-byte marker; receivers use it to identify logical frame boundaries even when `UPLOAD_INTERVAL_WRITE` chunks split frames mid-stream (which they will for IDRs larger than the per-write size cap).
- **Audio glitch test signature (acceptance criterion for D-08 hybrid model):** "two users, 5-minute broadcast at each preset, no audio glitches" — measured by user-subjective listening in UAT + `m_rawdata_sendq_overflows` + `m_encoder_input_drops` both zero at session close.
- **Path B trigger scenario hooks (D-10):** the equivalents of NinjamZap's `02_video_one_interval_early.cpp`, `04_late_join_midstream.cpp`, `06_audio_video_resync.cpp` — actual filenames TBD by Phase 21's scenario port. Per BETA-04, 20+ of 26 NinjamZap scenarios must pass for v1.3 beta acceptance; the critical sync scenarios are a strict subset.
- **VideoEncoder interface methods (rough sketch):**
  - `bool open(const VideoEncoderConfig& cfg)` — sets resolution / framerate / bitrate / profile + starts encoder thread + emits initial SPS/PPS
  - `void close()` — stops encoder thread, frees openh264 instance
  - `void reconfigure(const VideoEncoderConfig& cfg)` — tear-down + rebuild + republish SPS/PPS
  - `SpsPpsBuffer* getSpsPps() const` — atomic load (used by audio thread)
  - `void notifyIntervalStart(uint64_t seq)` — audio thread bumps the interval counter (encoder thread polls)
  - Counter accessors: `uint64_t getInputDropCount() const`, `uint64_t getFrameOutputCount() const`
- **VideoEncoderConfig fields:** `int width`, `int height`, `int frameRate`, `int targetBitrateKbps`, `H264Profile profile` (Baseline / Main / High), `int gopHintFrames` (= 1 means IDR every frame, encoder uses force-IDR signal anyway; this is just a heuristic for openh264's internal scheduling).

</specifics>

<deferred>
## Deferred Ideas

These came up during discussion but belong elsewhere — captured here so they're not lost.

- **VideoToolbox / MediaFoundation backends** — Phase 20 architects for them via the abstract `VideoEncoder` interface but does NOT ship them. Plan to add VideoToolbox on macOS arm64 + MediaFoundation on Windows in a follow-up phase, likely between Phase 23 and v1.4. Spike Risk #3 (Cisco openh264 v2.1.1 last Mac prebuilt) becomes deferred until that phase.
- **"Auto" adaptive-bitrate preset (4th preset)** — Phase 19 D-18 deferred to Phase 20; Phase 20 D-17 defers further to v1.4+. Requires either server-side per-subscriber drop signal piped back to the client (new NINJAM message extension; out of doc-only Q8 scope) or client-side TCP send-queue-depth heuristic (~200 LOC + tuning risk).
- **Bandwidth display in UI** — Showing current encoded bitrate, frame rate, dropped-frame counter to the user. Useful for diagnosing quality-vs-bandwidth trade-off during the beta. Not in v1.3 scope; revisit in Phase 22 or Phase 24 polish.
- **Encoder error surface in CameraStatusDialog** — Phase 19's status dialog covers camera permission errors. Encoder errors (openh264 init fail, encoding fatal) could surface via the same component. Planner decides if a Phase 20-specific dialog is needed or if `juce::Logger::writeToLog` + a console message is sufficient for the beta.
- **Multi-slice encoding** — H.264 supports splitting a frame into multiple NAL units (slices). Useful for decode parallelism + finer-grained resync on packet loss. NINJAM is TCP (no packet loss), and our resolutions don't benefit much from decode parallelism. Defer until profiling shows benefit.
- **Encoder thread lifecycle around plugin reload / session save-restore** — Claude's discretion; default to "encoder dies on plugin destruction, restarts on the next broadcast-on after plugin reload." If beta testers report broadcast-state persistence is desirable, revisit.
- **Frame-stall watchdog on encoder side** — mirror Phase 19's camera frame-stall watchdog. Planner's discretion; default to including a simple wall-clock-gap check on encoded output cadence in the planner's verification harness.
- **`docs/CAMERA.md` user-facing doc on broadcast** — Phase 19 D-26 defers full user docs to Phase 24. Phase 20 adds an internal note (CHANGELOG entry) about the new broadcast capability.
- **Audio-thread budget measurement under populated server + HD broadcast** — recorded as PLAN.md acceptance criterion (D-10 Path B trigger). Not a deferred idea per se — it lands in the plan; just noted here for traceability.
- **Web-companion-fed JTBv capture path (v1.4+ idea from Phase 19 session)** — already saved to memory as `project_web_capture_fallback`. Phase 20 keeps the substrate producer-agnostic (consistent with Phase 14.3-02 D-13 "API consistency tax: producer is opaque").

</deferred>

---

*Phase: 20-h-264-encoder-send-pipeline*
*Context gathered: 2026-05-16*
