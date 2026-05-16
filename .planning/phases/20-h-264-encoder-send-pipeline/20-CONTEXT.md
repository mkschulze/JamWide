# Phase 20: H.264 Encoder & Send Pipeline - Context

**Gathered:** 2026-05-16
**Revised:** 2026-05-16 (post-`/gsd-review` codex finding — substrate revision)
**Status:** Ready for planning

<domain>
## Phase Boundary

Webcam frames captured by Phase 19's `JamWideFrameDistributor` are H.264-encoded via vendored Cisco openh264 (libavcodec backend) and broadcast as NinjamZap-compatible video intervals on NINJAM channel 1, bit-for-bit wire-identical to NinjamZap mobile and the ninjamzap-core reference. This phase covers:

1. **Substrate revision (Phase 14.3-02 correction).** Replace `m_rawdata_sendq` `SpscRing<RawDataItem, 64>` with NinjamZap-literal `WDL_PtrList<RawDataQueueItem> + WDL_Mutex m_rawdata_cs`. The shipped SPSC substrate guarantees only single-producer safety, which the HYBRID emission model violates (audio thread AND encoder thread both produce). Mutex-based queue is what NinjamZap actually uses; multi-producer support is intrinsic. ~150–200 LOC of substrate change + test updates.
2. An abstract `VideoEncoder` interface + one openh264-backed implementation, owning its own thread; subscribes to `JamWideFrameDistributor` (BGRA `juce::Image` in), produces H.264 NAL units (encoded bytes out).
3. The send-side video state machine wired into `NJClient::on_new_interval` (audio thread): END (if open) → BEGIN → 24-byte interval marker → SPS/PPS (conditionally) → END at deactivate. All under `m_rawdata_cs` + `m_video_cs` / `m_video_spspps_cs` (NinjamZap-literal).
4. The per-frame chunk path: encoder thread heap-allocates each frame's `[4B BE length-prefix][NAL payload]` buffer and calls `NJClient::RawDataSendWrite` directly (now mutex-protected by the revised substrate).
5. Channel registration at NINJAM-connection time: `SetLocalChannelInfo(chidx=1, name="video", flags=0x10)` + `SetVideoChannel(chidx=1, fourcc=H264)` + `NotifyServerOfChannelChange`.
6. Cross-thread coordination primitives (NinjamZap-literal): `bool m_video_active` + `unsigned char m_video_guid[16]` under `WDL_Mutex m_video_cs`; `WDL_HeapBuf m_video_spspps` under `WDL_Mutex m_video_spspps_cs`; `std::atomic<uint64_t> m_audio_interval_seq` (audio→encoder for IDR sync; write-once-read-many atomic is fine without mutex). Audio-thread mutex acquisitions are accepted Phase 15.1 carve-outs documented in `realtime-audio-reviewer` audit allowlist.
7. Per-preset bitrate ladder mapped from Phase 19's Low/Medium/High capture presets: 100 / 300 / 800 kbps target via `RC_BITRATE_MODE`.

**Maps to:** Requirements **COD-01** (openh264 encode), **COD-02** (4-byte BE chunking), **WIRE-01** (fourCC `H264` + 24-byte marker + SPS/PPS chunk #2 + per-frame length prefix), **WIRE-03** (concurrent audio+video producer arbitration).

**Out of scope:** Receive pipeline + 4-stage promotion + GUID-pairing decision tree (Phase 21), per-user video tile rendering and popouts (Phase 22), macOS universal + Windows packaging + per-dylib codesigning + signtool (Phase 23), per-DAW UAT + NinjamZap scenario port + beta validation (Phase 24). Phase 20 does NOT add a VideoToolbox backend (architected for it, deferred to a follow-up phase). Phase 20 does NOT add an "Auto" adaptive-bitrate preset (deferred to v1.4+).

</domain>

<decisions>
## Implementation Decisions

### Encoder Backend Strategy (Q13 resolved)

- **D-01: Abstract `VideoEncoder` pure-virtual interface, one openh264 implementation in Phase 20.** Future VideoToolbox / MediaFoundation impls plug into the same interface without touching call sites. Adds ~50–100 LOC of abstraction. Closes blocker Q13 (VideoToolbox vs openh264 from day 1).
- **D-02: Encoder owns its own thread + BGRA→YUV420P conversion internally (via `libswscale sws_scale`).** Subscribes to `JamWideFrameDistributor` per Phase 19 D-02 / D-04. Reading BGRA `juce::Image` in; emitting H.264 NAL units + SPS/PPS out.
- **D-03: SPS/PPS stored as `WDL_HeapBuf m_video_spspps` under `WDL_Mutex m_video_spspps_cs` (NinjamZap-literal — see `ninjamzap-core/njclient.cpp:2177-2187 SetVideoSPSPPS` + `:3075` emit).** On first emit and after any reconfigure (preset change, fatal error, resolution change), encoder thread takes the mutex, `SetSize` + `memcpy` into the heap buf, releases. Audio thread takes the mutex during `on_new_interval` SPS/PPS emit, calls `RawDataSendWrite(m_video_spspps.Get(), m_video_spspps.GetSize(), false)` under the lock, releases. Mutex acquisition on audio thread is an accepted Phase 15.1 carve-out (audit allowlist entry — see D-09). **Revised 2026-05-16 from atomic-pointer-swap; Pitfall #2 UAF window is moot under mutex semantics.**
- **D-04: Encoder reconfiguration is always tear-down + rebuild + republish SPS/PPS.** Triggered by preset change, fatal openh264 error, or resolution change. Encoder thread closes the current openh264 instance, opens a new one with the new params, generates fresh SPS/PPS, takes `m_video_spspps_cs` and writes into the heap buf. Audio thread reads the new SPS/PPS at the next `on_new_interval` (under mutex). ~1 frame interval stall during teardown is acceptable.
- **D-05: H.264 Baseline profile, level 3.1.** No B-frames (lower encode latency, lower decoder memory). NinjamZap-aligned (iOS app uses Baseline via `VTCompressionSession`). Universally decodable by mobile + desktop libavcodec.
- **D-06: openh264 `RC_BITRATE_MODE` (average-bitrate target).** Matches the spike's measured ~98 kbps at 320×240@10fps. Lets ninjamzap-server `VideoCongestionThreshold` (default 50%) work as designed — average-bitrate target means per-interval payload is predictable enough for the per-subscriber drop-decision to be meaningful.
- **D-07: Drop-oldest backpressure on the `JamWideFrameDistributor → encoder` input SPSC + observable counter `m_encoder_input_drops`.** When the input SPSC is full, the new frame overwrites the oldest unencoded one and `m_encoder_input_drops` is incremented. Mirrors Phase 15.1-05 fallback semantics (RT-safety > completeness). Non-zero counter at phase close fails Phase 20's verification gate (parallel to Phase 15.1-10's deferred-delete-overflows == 0 assertion).

### Encoder ↔ Audio-Thread Coordination (HYBRID — NinjamZap-literal substrate)

> **Revised 2026-05-16 post-codex review.** The original Path A (atomic primitives) / Path B (mutex fallback) framing was based on the assumption that Phase 14.3-02's `m_rawdata_sendq` substrate supported any-thread producers. **Codex review verified the substrate is `SpscRing<RawDataItem, 64>` (single-producer, no CAS on `try_push`), which directly contradicts the HYBRID model's two-producer requirement.** Path A as written cannot work without substrate revision. Per user's `feedback_proven_over_pure` and explicit direction to "follow the NinjamZap approach," Phase 20 starts at the NinjamZap-literal mutex substrate from day 1. The Path A/B framing is retired; there is one path, and it mirrors NinjamZap's actual `WDL_PtrList + WDL_Mutex` substrate.

- **D-08: HYBRID emission model under NinjamZap-literal mutex substrate.** Audio thread (in `NJClient::on_new_interval`) emits the interval framing: END (if `m_video_interval_open`) → BEGIN → 24-byte marker (`[4B BE prefix=20][4B BE swap_count][16B audio_ch0_guid]`, stack-allocated) → SPS/PPS (read under `m_video_spspps_cs`, conditional on size > 0) → END at deactivate. Encoder thread emits per-frame payload chunks: it heap-allocates each frame's `[4B BE length-prefix][NAL bytes]` buffer and calls `NJClient::RawDataSendWrite(m_video_guid, buf, len, false)` directly. All `RawDataSendBegin/Write` calls take the substrate's `m_rawdata_cs` mutex (the revised, multi-producer-safe queue per D-19) internally; callers do not. The video-state mutex `m_video_cs` is taken by audio thread inside `on_new_interval` for the marker construction (covers `m_video_active`, `m_video_guid`) and released before SPS/PPS emit. **Verbatim port of NinjamZap's pattern** (`njclient.cpp:2116-2123 QueueVideoFrame` from encoder thread + `njclient.cpp:3041-3082 on_new_interval video block` on audio thread, both under the same mutex-based substrate that exists in NinjamZap source).
- **D-09: Phase 15.1 carve-out documented for three mutex acquisition sites on the audio thread.** `realtime-audio-reviewer` audit allowlist gains three entries (in `.claude/agents/realtime-audio-reviewer.md` or its config): (a) `m_rawdata_cs.Enter()` inside `RawDataSendBegin/Write` when called from `on_new_interval` — justified: NinjamZap-literal multi-producer Send queue; (b) `m_video_cs.Enter()` at the top of `on_new_interval` video block — justified: NinjamZap-literal state guard for `m_video_active` / `m_video_guid`; (c) `m_video_spspps_cs.Enter()` at the SPS/PPS emit site — justified: NinjamZap-literal SPS/PPS buffer guard. Phase 15.1-10's `auditor-zero-CRITICAL` gate is amended in 20-00 to accept these three carve-outs by file:line + reason; all other audio-thread mutex acquisitions must stay zero. **Rationale:** the user has direct evidence NinjamZap's mutex-based sync is correct on iOS/Android; we ship at parity rather than fight a substrate redesign for theoretical RT-safety purity (see `feedback_proven_over_pure` memory).
- **D-10: NinjamZap sync scenario port is the correctness gate (Path B trigger framing retired).** Phase 21's BETA-04 scenario port at `tests/video-sync/scenarios/` is the canonical correctness suite. Critical sync scenarios that MUST pass for Phase 20 to close: `02_video_one_interval_early.cpp`, `03_late_join.cpp`, `13_sps_pps_mid_stream.cpp`, `20_drop_resync_recovery.cpp`, `22_audio_then_video.cpp`, `25_no_initial_spspps.cpp`. Stress diagnostics (best-effort, not hard gates): `18_extreme_short_intervals.cpp`, `26_send_buffer_pressure.cpp`. **Revised 2026-05-16:** the original D-10 filenames (`04_late_join_midstream.cpp`, `06_audio_video_resync.cpp`) do not exist in the NinjamZap test directory — corrected per codex review's directory listing. The Path A/B 2-fix-budget trigger is no longer applicable (we start at NinjamZap-literal); if any sync scenario fails under Phase 20, debug toward NinjamZap source as the canonical correct behavior (deviations in our port are the bug).
- **D-11: `m_video_active` is `bool` under `WDL_Mutex m_video_cs` (NinjamZap-literal).** UI thread acquires `m_video_cs` when user clicks Broadcast, flips the bool, releases. Audio thread acquires `m_video_cs` at the top of the video block in `on_new_interval`, reads the bool, releases (before the per-interval framing emit, which takes `m_rawdata_cs` internally). Same mutex protects `m_video_guid[16]` writes by audio thread (BEGIN's GUID-generation site).
- **D-12: No encoder→audio chunk-handoff SPSC.** The encoder thread Sends each frame chunk directly via the revised mutex-based substrate (`m_rawdata_cs` taken internally by `RawDataSendWrite`). NinjamZap-literal. Run thread drains `m_rawdata_sendq` (now WDL_PtrList) under `m_rawdata_cs`, frees the heap buf after socket Send completes.
- **D-13: Cold-start SPS/PPS handling follows NinjamZap (`njclient.cpp:3075`'s `if size > 0` pattern).** Audio thread always emits END/BEGIN/marker. SPS/PPS is emitted as the second chunk **only if** `m_video_spspps.GetSize() > 0` (read under `m_video_spspps_cs`). First broadcast interval after the user clicks Broadcast may be marker-only (encoder still initializing ~50–150 ms); subsequent intervals carry SPS/PPS. Encoder thread lifecycle: starts at broadcast-on (not at camera-open) to keep idle CPU low when the user is previewing without broadcasting.

### GOP / Keyframe Strategy

- **D-14: One IDR keyframe at the start of every NINJAM interval.** Predictable per-interval bandwidth; mid-stream joiners decode the very first interval they see; simplifies Phase 21 receive pipeline (each interval is self-contained). Bandwidth cost is ~15–25% higher than long-GOP at same quality, but the IDR-per-interval discipline is what makes the GUID-pairing receive-side semantics clean.
- **D-15: Encoder learns about interval boundaries via `std::atomic<uint64_t> m_audio_interval_seq`.** Audio thread increments the counter at the top of every `on_new_interval`. Encoder thread loads the counter before each frame encode; when it changes vs the last-observed value, the encoder sets openh264's `eForceIntraFrame` flag for THIS frame, forcing IDR. Up to 1 frame of drift between actual interval boundary and IDR (acceptable: ~33–100 ms at our frame rates). Lock-free; naturally handles BPM/BPI changes mid-session (interval cadence shifts, counter still increments at every actual boundary).

### Per-Preset Encoder Configuration

- **D-16: Bitrate ladder: Low → 100 kbps, Medium → 300 kbps, High → 800 kbps.** Spike-validated baseline (320×240@10fps measured ~98 kbps) + RESEARCH-ADDENDUM's sizing table. Each preset is fixed; verified by Phase 20 acceptance criterion ("two users, 5-minute broadcast at each preset, no audio glitches, no TSan races, no `m_encoder_input_drops` increment, ninjamzap-server congestion-drop counter below threshold"). **Revised 2026-05-16:** the `m_rawdata_sendq_overflows` counter is removed under D-19's NinjamZap-literal substrate (the WDL_PtrList queue is unbounded — NinjamZap accepts that); the new observability surface is the `m_rawdata_cs` contention rate + WDL_PtrList depth at drain time (planner adds tracing if needed).
- **D-17: No "Auto" adaptive-bitrate preset in v1.3.** Deferred to v1.4+ — requires either a server-side per-subscriber drop signal piped back to the client (new NINJAM message extension; ninjamzap-server has the metrics but JamWide's doc-only Q8 path does not include server-side changes) or a client-side TCP send-queue-depth heuristic (risk of oscillation, ~200 LOC of tuning). Three fixed presets are shippable beta surface; Auto is an evolution.
- **D-18: Video channel is registered at NINJAM-connection time.** When JamWide connects to a server, `NJClient` immediately calls `SetLocalChannelInfo(chidx=1, name="video", flags=0x10)` + `SetVideoChannel(chidx=1, fourcc=H264)` + `NotifyServerOfChannelChange`. Channel-index conflict detection happens at connect time; other clients see "user has video capability" from the moment of connect regardless of camera/broadcast state. Bandwidth cost zero when not broadcasting (no payload sent until broadcast-on). Matches NinjamZap mobile semantics.

### Substrate Revision & Pitfall Fixes (added 2026-05-16 post-codex review)

- **D-19: Phase 14.3-02 substrate is revised in Plan 20-00 to be NinjamZap-literal.** The shipped `m_rawdata_sendq` (`jamwide::SpscRing<RawDataItem, 64>`) is replaced with `WDL_PtrList<RawDataQueueItem> m_rawdata_sendq` + `WDL_Mutex m_rawdata_cs`, mirroring NinjamZap source verbatim. The original `m_rawdata_sendq_overflows` counter + Pattern C discard-on-null guard + `RAWDATA_SEND_QUEUE_CAPACITY = 64` constant are retired (NinjamZap's queue is unbounded; backpressure exists only at the per-Net_Connection TCP send queue, governed by `ninjamzap-server`'s VideoCongestionThreshold). `RawDataSendBegin/Write` acquire `m_rawdata_cs` for the WDL_HeapBuf allocation + WDL_PtrList push (a single critical section per call); the run-thread drain in `NJClient::Run` swaps the list out under the mutex, then processes the swapped-out list without holding the lock. `tests/test_rawdata_send.cpp` is updated: the 8 sub-tests' assertions about ring capacity / overflow counter are replaced with mutex-correctness assertions (e.g., 2-thread producer stress + run-thread drain, sequential consistency of message order per producer). **Plan 20-00 also writes the audit-allowlist entries from D-09** so that `realtime-audio-reviewer` accepts the three mutex acquisition sites on the audio thread.

- **D-20: Pitfall #1 fix — `LocalChannelMirror.curwritefile_guid[16]`.** RESEARCH §Pitfall 1 identified that the 24-byte marker's `audio_ch0_guid` requires audio-thread access to the local user's audio channel 0's `m_curwritefile.guid`. Phase 15.1-06 HIGH-2 forbids the audio thread from dereferencing canonical `Local_Channel*`, so the GUID must live on `LocalChannelMirror`. **Decision:** add `unsigned char curwritefile_guid[16]` to `LocalChannelMirror` (in `src/core/njclient.h` adjacent to existing audio-thread-owned mirror fields like `bcast_active`, `curwritefile_curbuflen` per Phase 15.1-07b precedent). The audio thread writes this field at the same site where `m_curwritefile.guid` is regenerated today (`src/core/njclient.cpp:2606` per RESEARCH citation) — Plan 20-02 audits this site + any other writers of `m_curwritefile.guid` to ensure the mirror stays in lockstep. The audio thread reads `m_locchan_mirror[0].curwritefile_guid` when constructing the 24-byte marker in `on_new_interval`. No mutex needed (single-writer audio-thread access to mirror).

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

### Substrate (Phase 14.3 — dependency baseline + REVISED in Plan 20-00)

- `.planning/phases/14.3-native-video-foundation/14.3-SPEC.md` — RawData send/receive API spec; success criteria 4 was "callable from any thread, internally serialized" but **the implementation guarantees only single-producer safety (SpscRing). Plan 20-00 revises the substrate to actually deliver multi-producer safety via WDL_Mutex per NinjamZap source.**
- `.planning/phases/14.3-native-video-foundation/14.3-02-SUMMARY.md` — As-shipped substrate before Plan 20-00: `m_rawdata_sendq` SPSC capacity 64; run-thread drain block at `njclient.cpp:2697-2776`; Pattern C discard-on-null guard; `m_rawdata_sendq_overflows` counter; `is_video_fourcc` helper recognizes `H264 / VP8 / MJPG`. **After Plan 20-00 (per D-19):** the SPSC + overflow counter + Pattern C guard are retired; queue becomes NinjamZap-literal `WDL_PtrList<RawDataQueueItem>` + `WDL_Mutex m_rawdata_cs` (unbounded; backpressure exists only at the TCP layer governed by ninjamzap-server's VideoCongestionThreshold). The `is_video_fourcc` helper at line 233 is unchanged.
- `.planning/phases/14.3-native-video-foundation/14.3-03-SUMMARY.md` — Receive-path dispatch fix; unknown-fourCC no longer routes to Vorbis decoder. **Unchanged by Plan 20-00** (Plan 20-00 only touches the send-side substrate).

### Prior phase dependencies

- `.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md` — Phase 19 D-02 (`JamWideFrameDistributor` SPSC subscriber pattern), D-04 (encoder subscriber owns BGRA→YUV420P conversion), D-09/D-11 (camera state independent of broadcast/connect), D-18 (three capture presets), D-25 (`<camera>` ValueTree subtree with `qualityPreset` field), D-29 (Phase 20 owns ALL audio-thread integration for camera).
- `.planning/phases/15.1-rt-safety-hardening/15.1-CONTEXT.md` — D-01 (SPSC-mediated audio-thread state updates), D-02 (deferred-delete SPSC pattern for heap deallocations off audio thread), D-07 (TSan dual-scope verification gate).

### NinjamZap reference (port from these — read source verbatim)

- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:200-236` — public API surface for `RawDataSendBegin/Write`, `SetVideoChannel`, `QueueVideoFrame`, `SetVideoSPSPPS`.
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2047-2123` — send-side impl: `RawDataSendBegin` (line 2047), `RawDataSendWrite` (line 2064), `QueueVideoFrame` (line 2116, called from encoder thread — direct `RawDataSendWrite` at line 2120).
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3041-3082` — sender's `on_new_interval` video block: END/BEGIN/marker/SPS-PPS pattern (audio thread).
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2092-2102` — Cold-start SPS/PPS path (`QueueVideoFrame` may initiate BEGIN if encoder is ready before on_new_interval ticks).

### JamWide integration points

- `src/core/njclient.h` (post-14.3-02, REVISED by Plan 20-00) — public RawData API + `RawData_Callback` slot. **Plan 20-00 changes:** `m_rawdata_sendq` → `WDL_PtrList<RawDataQueueItem>`; adds `WDL_Mutex m_rawdata_cs`; removes `m_rawdata_sendq_overflows` counter + `GetRawDataSendQueueOverflowCount()` accessor + `RAWDATA_SEND_QUEUE_CAPACITY` constant. **Plan 20-02 adds:** `bool m_video_active` (under m_video_cs), `WDL_HeapBuf m_video_spspps` (under m_video_spspps_cs), `WDL_Mutex m_video_cs`, `WDL_Mutex m_video_spspps_cs`, `std::atomic<uint64_t> m_audio_interval_seq`, `int m_video_chidx` (=1), `unsigned char m_video_guid[16]` (under m_video_cs), `unsigned int m_video_fourcc` (= `MAKE_NJ_FOURCC('H','2','6','4')`), `bool m_video_interval_open` (audio-thread-local), `std::atomic<uint64_t> m_encoder_input_drops`. **`LocalChannelMirror` gains `unsigned char curwritefile_guid[16]` per D-20.**
- `src/core/njclient.cpp:154-155` — existing `MAKE_NJ_FOURCC('O','G','G','v')` + `'F','L','A','C'` patterns; the `MAKE_NJ_FOURCC('H','2','6','4')` constant already exists per 14.3-02's `is_video_fourcc` helper at line 233.
- `src/core/njclient.cpp:2606` — write site for `m_curwritefile.guid` (existing run-thread-path code per RESEARCH §Pitfall 1). Plan 20-02 audits this site + any other writers to ensure `LocalChannelMirror.curwritefile_guid` stays in lockstep (D-20).
- `src/core/njclient.cpp` `on_new_interval` — Phase 20 inserts the video block (END/BEGIN/marker/SPS-PPS) after the existing audio interval handling. Audio thread takes `m_video_cs` to read `m_video_active` + `m_video_guid`; takes `m_video_spspps_cs` to read SPS/PPS; `RawDataSendBegin/Write` calls take `m_rawdata_cs` internally. Marker construction uses already-on-audio-thread `m_sync_interval_cnt` + `LocalChannelMirror.curwritefile_guid` (new field per D-20) for `audio_ch0_guid`.
- `juce/JamWideJuceProcessor.{h,cpp}` — Phase 20 adds the `VideoEncoder` owner (constructed when camera opens; starts encoder thread when broadcast toggles on).
- `juce/ui/ConnectionBar.cpp` (post-Phase-19) — Phase 19 wired the Camera button + state machine + preset right-click menu. Phase 20 adds the Broadcast toggle (could be a secondary state on the Camera button or a separate Broadcast button — planner picks; user pref is consistency with existing button semantics).
- `libs/ffmpeg/*/include/` and `libs/ffmpeg/*/lib/` — Phase 14.3-01 vendored libavcodec + libavformat + libavutil + libswscale + libopenh264. Phase 20 consumes via the existing `cmake/ffmpeg.cmake` IMPORTED INTERFACE target.

### JamTaba reference (encoder port reference only; non-authoritative)

- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:99` — `QThreadPool(1)` worker pattern; replaced in Phase 20 with `juce::Thread` or `std::thread`.
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:237-277` — openh264 encoder configure block; the bitrate / GOP / profile params are the spike-validated baseline to start from.
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:541-577` — RGB→YUV macro; Phase 20 uses `libswscale sws_scale` instead (more portable, NV12/YUV420P-aware).
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:586-643` — raw NAL bytestream (no AVFormat container); Phase 20 mirrors this approach.

### Memory references

- `feedback_proven_over_pure` — Phase 15.1 architectural invariants are negotiable in sync/timing/codec domains with documented carve-out. **Anchors D-09's three-site Phase 15.1 carve-out + Plan 20-00's substrate revision.** Codex's pre-plan review (2026-05-16) demonstrated empirically why the Phase 15.1-pure path could not work for this domain — Phase 14.3-02's SPSC substrate is single-producer and the HYBRID model needs two producers. Per the memory's "if pure path fails the test gate, switch to reference-faithful," we move to NinjamZap-literal mutex from day 1.
- `project_jamtaba_video_port` — milestone-level project memory; SUPERSEDED references kept for historical context.
- `feedback_uat_scope_redflags` — user's UAT discipline (verify the user-visible happy path; don't let executor "verify only X, skip Y" pass for broadcast). Phase 20's UAT must cover: connect 2 users → broadcast video at each preset → 5-minute session → audio glitch-free + drop counters at zero + TSan clean under populated load.
- `feedback_legacy_invariant_audit` — **PRIMARY ANCHOR for D-19's substrate revision.** When 14.3-02 replaced NinjamZap's `WDL_PtrList + WDL_Mutex` with `SpscRing`, the single-producer-vs-multi-producer invariant was silently broken (a shadow representation of the queue was introduced; the producer-count invariant in the original didn't survive the migration). Phase 20 codex review caught this exact pattern. Plan 20-00 reverts the substrate to match NinjamZap's actual mutex-based queue. Phase 20 audits must also check: every audio-thread access to `m_curwritefile.guid` for the marker's audio_ch0_guid is consistent with Phase 15.1-06 LocalChannelMirror semantics (D-20).
- `feedback_size_constant_lifecycle_audit` — Plan 20-00 retires `RAWDATA_SEND_QUEUE_CAPACITY = 64` (NinjamZap's queue is unbounded). The lesson still applies: if future profiling shows mutex contention on `m_rawdata_cs` at HD broadcast under populated server, audit the contention path before introducing a bound (NinjamZap doesn't bound — neither should we without measured evidence).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`NJClient::RawDataSendBegin/Write` substrate** (Phase 14.3-02, REVISED by Plan 20-00) — Phase 20's primary consumer. Plan 20-00 replaces the SPSC + overflow counter + Pattern C guard with NinjamZap-literal `WDL_PtrList<RawDataQueueItem>` + `WDL_Mutex m_rawdata_cs`. Multi-producer-safe by virtue of the mutex. Run-thread drain swaps the list out under the mutex, processes outside.
- **`JamWideFrameDistributor`** (Phase 19) — Frame source. `VideoEncoder` registers as a subscriber via SPSC and receives BGRA `juce::Image` frames at the configured capture rate. Subscription RAII (Phase 19 HIGH-2) handles encoder lifetime.
- **`cmake/ffmpeg.cmake` IMPORTED INTERFACE** (Phase 14.3-01) — Phase 20 links `libavcodec` + `libavutil` + `libswscale` + `libopenh264` via the existing target.
- **`juce::Thread` patterns in codebase** (e.g., `NinjamRunThread`) — encoder thread implementation reference.
- **`WDL_Mutex` patterns in NJClient** (Phase 15.1-06 / 15.1-07a precedents) — `m_users_cs`, `m_locchan_cs` are the existing audio-thread-aware mutex sites that 15.1 hardened OUT of the audio path. The Phase 20 video mutexes (`m_video_cs`, `m_video_spspps_cs`, `m_rawdata_cs`) are the documented exceptions — audio thread DOES take them, per the D-09 carve-out.

### Established Patterns

- **Audio thread is sacred — with documented exceptions** (Phase 15.1 D-01 + Phase 20 D-09 carve-out). Phase 20 takes `m_video_cs`, `m_video_spspps_cs`, `m_rawdata_cs` on the audio thread inside `on_new_interval` — the only such carve-outs in the codebase. `realtime-audio-reviewer` is configured to accept these three sites and treat all others as CRITICAL.
- **Counter-and-accessor for backpressure events** (`GetBlockQueueDropCount`, `GetDeferredDeleteOverflowCount`). Phase 20 adds `GetEncoderInputDropCount()` following the same idiom. The Phase 14.3-02 `GetRawDataSendQueueOverflowCount` is retired by Plan 20-00 (NinjamZap-literal queue is unbounded).
- **SetVideoChannel before NotifyServerOfChannelChange** — NinjamZap pattern; mirrors `SetLocalChannelInfo` semantics that JamWide already uses for audio channels.
- **State persistence via ValueTree `<camera>` subtree** (Phase 19 D-25). Phase 20 may extend with broadcast-on persistence (e.g., remember "last broadcast was on" — open question, planner decides if it makes sense for v1.3).
- **`juce::Logger::writeToLog` for plugin-side events** (Phase 19 D-23). Phase 20 follows the same pattern for encoder open/close/error/SPS-PPS-regen events.
- **Mirror-by-value on the audio thread** (Phase 15.1-06 / 15.1-07b precedent). `LocalChannelMirror` already carries `bcast_active`, `curwritefile_curbuflen`, atomic peak vol. Plan 20-02 adds `curwritefile_guid[16]` to the same struct (D-20) — no `Local_Channel*` escape hatch, matches Phase 15.1-06 HIGH-2 architectural choice.

### Integration Points

- **`NJClient::on_new_interval`** (`src/core/njclient.cpp`, audio thread) — Phase 20 inserts the video block after existing audio interval handling. Acquires `m_video_cs` for the marker construction (reads `m_video_active`, `m_video_guid`); acquires `m_video_spspps_cs` for SPS/PPS emit. `RawDataSendBegin/Write` calls take `m_rawdata_cs` internally. Three mutex acquisitions on the audio thread per video interval — accepted carve-out per D-09.
- **`NJClient::Run`** (`src/core/njclient.cpp`, run thread) — Plan 20-00 revises the existing `m_rawdata_sendq` drain block at `njclient.cpp:2697-2776` to take `m_rawdata_cs` and swap-the-list-out semantics. Run thread frees `RawDataQueueItem`'s heap buf after `m_netcon->Send` completes.
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
- **Audio glitch test signature (acceptance criterion for D-08 hybrid model):** "two users, 5-minute broadcast at each preset, no audio glitches, TSan clean" — measured by user-subjective listening in UAT + `m_encoder_input_drops` at zero + `m_video_cs` / `m_video_spspps_cs` / `m_rawdata_cs` contention rate observable but below a reasonable budget (planner picks an acceptance threshold).
- **NinjamZap critical sync scenarios (per D-10 corrected list):** `02_video_one_interval_early.cpp`, `03_late_join.cpp`, `13_sps_pps_mid_stream.cpp`, `20_drop_resync_recovery.cpp`, `22_audio_then_video.cpp`, `25_no_initial_spspps.cpp` — these MUST pass for Phase 20 to close. Stress diagnostics (best-effort): `18_extreme_short_intervals.cpp`, `26_send_buffer_pressure.cpp`. Per BETA-04, 20+ of 26 NinjamZap scenarios must pass for v1.3 beta acceptance; the critical sync subset is a strict subset of that.
- **VideoEncoder interface methods (rough sketch):**
  - `bool open(const VideoEncoderConfig& cfg)` — sets resolution / framerate / bitrate / profile + starts encoder thread + emits initial SPS/PPS into `NJClient::m_video_spspps` (under mutex)
  - `void close()` — stops encoder thread, frees openh264 instance
  - `void reconfigure(const VideoEncoderConfig& cfg)` — tear-down + rebuild + republish SPS/PPS
  - `void notifyIntervalStart(uint64_t seq)` — called by audio thread to bump the interval counter (encoder thread polls; D-15)
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
- **Audio-thread budget measurement under populated server + HD broadcast** — recorded as PLAN.md acceptance criterion in Plan 20-03. Measure worst-case `on_new_interval` duration (including the three mutex acquisitions + Send calls) at HD broadcast × 6-peer populated server. Not a Path B trigger (no Path B framework anymore); a sanity check that the NinjamZap-literal substrate scales to JamWide's expected populated load. If budget is exceeded, escalate as a substrate-tuning subplan, not as a sync-architecture change.
- **Web-companion-fed JTBv capture path (v1.4+ idea from Phase 19 session)** — already saved to memory as `project_web_capture_fallback`. Phase 20 keeps the substrate producer-agnostic (consistent with Phase 14.3-02 D-13 "API consistency tax: producer is opaque").

</deferred>

---

*Phase: 20-h-264-encoder-send-pipeline*
*Context gathered: 2026-05-16*
