# Phase 20: H.264 Encoder & Send Pipeline — Research

**Researched:** 2026-05-16
**Revised:** 2026-05-16 (post-`/gsd-review` codex finding — substrate revision)
**Domain:** H.264 video encoding (openh264 via libavcodec) + NinjamZap-wire-compatible send-path state machine + cross-thread RT-safe coordination
**Confidence:** HIGH

## CRITICAL UPDATE (2026-05-16 post-codex review)

**The "Substrate (Phase 14.3-02) is shipped and any-thread-producer-safe" claim throughout this document is INCORRECT.** Codex pre-plan review verified that `m_rawdata_sendq` is `jamwide::SpscRing<RawDataItem, 64>` with strict single-producer contract — multi-producer use is undefined behaviour. This invalidates the Path A architecture as originally framed.

**Resolution (CONTEXT.md D-19, Plan 20-00):** Replace the SPSC substrate with NinjamZap-literal `WDL_PtrList<RawDataQueueItem> + WDL_Mutex m_rawdata_cs`. Path A/B framing is retired — Phase 20 starts at NinjamZap-literal mutex semantics from day 1.

**Decomposition updated from 3 plans + 1 contingency → 4 plans (no contingency):**
- **20-00 (NEW): Substrate revision** — replace SPSC with NinjamZap-literal mutex queue; add `m_video_cs` + `m_video_spspps_cs`; write `realtime-audio-reviewer` audit-allowlist entries for the three carve-out sites; update `tests/test_rawdata_send.cpp`. ~150-200 LOC.
- **20-01: VideoEncoder interface + Openh264Encoder impl** (unchanged from original 20-01).
- **20-02: NJClient video send-path state machine** (revised: uses mutex primitives per D-08/D-11; no atomic-pointer-swap for SPS/PPS per revised D-03; adds `LocalChannelMirror.curwritefile_guid` per D-20).
- **20-03: Wiring + UAT + audio-thread budget measurement** (revised: budget is sanity check, not Path B trigger; the trigger no longer exists).

**Open questions retired:** Q3 (`realtime-audio-reviewer` allow-list format) becomes a Plan 20-00 deliverable; Q4 still applies but is decoupled from the substrate question; the "Path A correctness" confidence-MEDIUM line at the bottom of this file is moot. Sections of this document below that reference Path A/B should be read with this revision in mind — the *intent* of those sections (correctness, RT-safety, NinjamZap fidelity) is preserved, but the *primitives* are now uniformly NinjamZap-literal mutex.

The original D-10 scenario filename reconciliation (3 confirmed + 2 ghost filenames) becomes a CONTEXT.md correction in revised D-10. The reconciliation table later in this document (`02_video_one_interval_early.cpp` / `03_late_join.cpp` / `13_sps_pps_mid_stream.cpp` / `20_drop_resync_recovery.cpp` / `22_audio_then_video.cpp` / `25_no_initial_spspps.cpp` plus stress `18_extreme_short_intervals.cpp` / `26_send_buffer_pressure.cpp`) is the canonical list.

---

## Summary

Phase 20 is a port-from-reference exercise, not a green-field design. The wire format is locked, the substrate is shipped (Phase 14.3-02 `m_rawdata_sendq` SPSC + run-thread drain), the frame source contract is shipped (Phase 19 `JamWideFrameDistributor::Subscription`), and the reference implementation is sitting verbatim at `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2047-2123, 3041-3082`. The job is to translate NinjamZap's pattern into JamWide's Phase-15.1-compliant idioms (atomics + SPSC primitives instead of `WDL_Mutex`) while keeping a documented escape hatch (Path B = NinjamZap-literal `WDL_Mutex`) reachable in ≤30 LOC of localized edit if the atomic path fails sync correctness gates.

The encoder side is also a port — JamTaba's `FFMpegMuxer.cpp:237-277` and the spike's `tests/video_spike.cpp` already prove the openh264-via-libavcodec recipe works at 320×240@10fps@~98 kbps. Three known-good preset bitrates need wiring (100/300/800 kbps per Low/Medium/High). The encoder owns its thread, owns BGRA→YUV420P conversion via `libswscale`, and Sends each frame chunk directly via Phase 14.3 substrate.

The Path A/B framing in CONTEXT.md D-09/D-10 is the most architecturally novel piece. The default path (Path A, atomic primitives, Phase-15.1-compliant) is correct for greenfield reasoning but unproven for video/audio interval sync at this granularity. Path B (NinjamZap-literal `WDL_Mutex` on the audio thread inside `on_new_interval`) is proven by the user against a real NinjamZap mobile peer. The trigger to switch is objective (≥3 failed Path-A fixes against specific NinjamZap sync scenarios), bounded (~30 LOC + audit-allowlist entry + CONTEXT.md amendment), and recorded in the plan as an acceptance gate.

**Primary recommendation:** Decompose into **3 plans**:
- **20-01** — `VideoEncoder` interface + `Openh264Encoder` impl + per-preset config + BGRA→YUV420P conversion + encoder thread + SPS/PPS atomic-pointer-swap + `m_encoder_input_drops` counter. Audio-thread-free; testable in isolation against synthetic frames.
- **20-02** — `NJClient` send-path video state machine (`on_new_interval` block: END/BEGIN/marker/SPS-PPS, Path A primitives), `m_video_active`, `m_audio_interval_seq` (IDR-sync), `SetVideoChannel`/`StopVideoChannel`/`QueueVideoFrame`/`SetVideoSPSPPS` API mirroring NinjamZap, channel-registration hook in the NINJAM connect-up callback. Path A only; Path B carve-out staged as a Plan 20-04 contingency.
- **20-03** — `JamWideJuceProcessor` ownership wiring (`VideoEncoder` lifecycle around camera + broadcast toggle), `ConnectionBar` Broadcast button, integration UAT against a 2-peer populated `video.ninjamzap.com:2049` session at each preset for 5 minutes, counters-zero gate, audio-thread budget measurement (Path B trigger acceptance criterion).

Plus contingency Plan **20-04 (conditional, only if 20-03 UAT trips the Path B trigger)** — swap `m_video_active`/`m_video_spspps` to `WDL_Mutex` carve-out, document the `realtime-audio-reviewer` allow-list entry, amend CONTEXT.md.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|--------------|----------------|-----------|
| BGRA→YUV420P color conversion | Encoder thread | — | Phase 19 D-04 explicitly defers to the encoder subscriber. Off audio thread, off message thread, off camera-callback thread. |
| H.264 encoding (NAL units + SPS/PPS) | Encoder thread | — | Long-running, allocating, libavcodec-internal locks; cannot be on audio thread. Per-frame work ~3-10 ms at our resolutions. |
| Per-frame `[4B BE length][NAL]` length-prefix wrapping | Encoder thread | — | Allocates the heap buffer that becomes the SPSC payload. Same thread as the encode so no extra copy. |
| `RawDataSendWrite(frame chunk)` call | Encoder thread | — | Phase 14.3 substrate guarantees any-thread producer; substrate's `m_rawdata_sendq` SPSC absorbs serialisation. **No chunk-handoff SPSC between encoder and audio thread** (CONTEXT D-12). |
| Interval BEGIN/END framing emission | Audio thread (`on_new_interval`) | — | NinjamZap-verbatim. END(N) and BEGIN(N+1) must be in the **same audio-thread iteration** to give the server interval-boundary alignment for relay (see `260515-0pc-RESEARCH-ADDENDUM.md` "Critical correctness invariant"). |
| 24-byte interval marker construction | Audio thread (`on_new_interval`) | — | Marker carries the local audio channel-0 GUID, which is owned by the audio thread (`lc->m_curwritefile.guid` is mirrored via Phase 15.1-06 `LocalChannelMirror`). On-stack 24-byte buffer; one `RawDataSendWrite`. |
| Cached SPS/PPS chunk emission | Audio thread (`on_new_interval`) | — | One `std::atomic<SpsPpsBuffer*>.load(acquire)` + one `memcpy` to the chunk buffer that Phase 14.3 substrate's `RawDataSendWrite` will heap-buf internally. No mutex under Path A. |
| `m_video_active` toggle (broadcast on/off) | UI/message thread (writer) → Audio thread (reader) | — | `std::atomic<bool>` release-store from UI; acquire-load from audio thread top of video block. Release/acquire pairs the SPS/PPS pointer publication so "active but no SPS/PPS yet" cannot be observed. |
| IDR-sync counter (audio→encoder) | Audio thread (writer) → Encoder thread (reader) | — | `std::atomic<uint64_t>` incremented at the top of every `on_new_interval`; encoder polls before each encode and flips `eForceIntraFrame` on change. Up to 1 frame drift acceptable. |
| Old `SpsPpsBuffer*` deferred-delete | Audio thread (drainer-aware) → Run thread (deleter) | — | Phase 15.1-05 pattern. Audio thread does not delete; pushes onto deferred-delete SPSC; run thread frees off-thread. |
| `Net_Connection::Send` (single caller) | Run thread (sole) | — | Phase 14.3-02 invariant: only `NJClient::Run`'s drain block calls `m_netcon->Send` for RawData. Pattern C discard guard handles Disconnect race. |
| Channel registration (`SetLocalChannelInfo` + `SetVideoChannel` + `NotifyServerOfChannelChange`) | Run thread (or NinjamRunThread connect-up callback) | — | Called once when NINJAM session authenticates; same thread context as existing audio channel registration. |
| Camera ↔ broadcast state coordination | Message thread (`JamWideJuceProcessor`) | — | Broadcast button toggle + Phase 19 camera-on-state are message-thread state. Encoder thread lifecycle (start on broadcast-on, stop on broadcast-off) is driven from here. |

**Why this matters:** the hybrid model (audio thread = framing; encoder thread = payload) is the exact NinjamZap split that proved correct in mobile-app deployment. Misattributing the per-frame `RawDataSendWrite` to the audio thread (the originally-selected option Q2.1 before mid-session correction in 20-DISCUSSION-LOG.md:106-107) would either (a) bottleneck on audio-thread budget under HD broadcast or (b) require a chunk-handoff SPSC that the substrate already obviates.

## User Constraints (from CONTEXT.md)

### Locked Decisions

**Encoder Backend Strategy:**
- **D-01**: Abstract `VideoEncoder` pure-virtual interface, one openh264 implementation in Phase 20. Future VideoToolbox / MediaFoundation impls plug into the same interface without touching call sites. Closes blocker Q13.
- **D-02**: Encoder owns its own thread + BGRA→YUV420P conversion internally (via `libswscale sws_scale`). Subscribes to `JamWideFrameDistributor` per Phase 19 D-02 / D-04.
- **D-03**: SPS/PPS published from encoder to audio thread via `std::atomic<SpsPpsBuffer*>` with acquire/release ordering; old buffer goes to Phase 15.1-05 deferred-delete SPSC.
- **D-04**: Encoder reconfiguration is always tear-down + rebuild + republish SPS/PPS. ~1 frame interval stall acceptable.
- **D-05**: H.264 Baseline profile, level 3.1. No B-frames. NinjamZap-aligned.
- **D-06**: openh264 `RC_BITRATE_MODE` (average-bitrate target).
- **D-07**: Drop-oldest backpressure on `JamWideFrameDistributor → encoder` input SPSC + observable counter `m_encoder_input_drops`. Non-zero at phase close fails verification gate.

**Encoder ↔ Audio-Thread Coordination (HYBRID — NinjamZap-faithful):**
- **D-08**: HYBRID emission model. Audio thread emits END/BEGIN/marker/SPS-PPS; encoder thread emits per-frame chunks via `RawDataSendWrite` directly. Verbatim port of NinjamZap's pattern at `njclient.cpp:2116-2123` + `:3041-3082`.
- **D-09**: Path A primary (atomic primitives); Path B explicit fallback (NinjamZap-literal `WDL_Mutex`). Path B does NOT change the HYBRID threading model — only the synchronization primitives differ.
- **D-10**: Path B trigger is objective, capped, and recorded as a Plan-level acceptance criterion. Trigger: ≥3 failed Path A fix attempts against the critical NinjamZap sync scenarios (`02_video_one_interval_early.cpp`, `03_late_join.cpp`, `22_audio_then_video.cpp` — confirmed filenames; see Open Questions below).
- **D-11**: `m_video_active` toggle is `std::atomic<bool>` with acquire/release.
- **D-12**: No chunk-handoff SPSC between encoder and audio thread. Encoder Sends directly via Phase 14.3 substrate.
- **D-13**: Cold-start SPS/PPS handling follows NinjamZap's `if size > 0` pattern. Audio thread always emits END/BEGIN/marker; SPS/PPS only if non-null. Encoder thread starts at broadcast-on.

**GOP / Keyframe Strategy:**
- **D-14**: One IDR keyframe at the start of every NINJAM interval.
- **D-15**: Encoder learns about interval boundaries via `std::atomic<uint64_t> m_audio_interval_seq`. Lock-free; naturally handles BPM/BPI changes.

**Per-Preset Encoder Configuration:**
- **D-16**: Bitrate ladder: Low → 100 kbps, Medium → 300 kbps, High → 800 kbps.
- **D-17**: No "Auto" adaptive-bitrate preset in v1.3.
- **D-18**: Video channel registered at NINJAM-connection time (`chidx=1` always; bandwidth cost zero when not broadcasting).

### Claude's Discretion

- Memory ordering details for non-critical atomics (IDR-sync counter — `relaxed` vs `acquire/release`).
- Slice mode (single-slice vs multi-slice per frame). Default to single-slice.
- Encoder frame-stall watchdog granularity (mirror Phase 19's camera watchdog).
- openh264 speed/quality preset params (`iLoopFilterDisableIdc`, `iMultipleThreadIdc`). Default single-threaded.
- Encoder thread lifecycle around plugin reload / DAW session save-restore.
- Debug logging surface (via `juce::Logger::writeToLog` per Phase 19 D-23 pattern).
- File layout: `juce/video/encoder/` subdirectory likely.

### Deferred Ideas (OUT OF SCOPE)

- VideoToolbox / MediaFoundation backends (architected for; deferred past Phase 23).
- "Auto" adaptive-bitrate 4th preset (v1.4+).
- Bandwidth display in UI (Phase 22 or 24 polish).
- Encoder error surface in CameraStatusDialog (planner discretion; default = `juce::Logger::writeToLog` only).
- Multi-slice encoding (defer until profiling shows benefit).
- Encoder lifecycle around plugin reload / DAW session save-restore (default: dies on plugin destruction, restarts on next broadcast-on).
- Frame-stall watchdog on encoder side (planner's discretion).
- `docs/CAMERA.md` user-facing broadcast doc (Phase 24).
- Web-companion-fed JTBv capture path (v1.4+; already in `project_web_capture_fallback`).
- Audio-thread budget measurement number (lands as PLAN.md acceptance criterion).

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| **COD-01** | User's webcam frames encode to H.264 via vendored Cisco openh264 (libavcodec backend) at the spike-validated baseline of ~98 kbps at 320×240/10 fps | `260515-0pc-spike-results.md` measured 320×240@10fps→98,560 bps actual vs 96,000 target (off by 2.7%). JamTaba `FFMpegMuxer.cpp:237-277` provides the encoder configure recipe (`bit_rate`, `gop_size=30`, `time_base={1, fps}`, `pix_fmt=AV_PIX_FMT_YUV420P`, `preset=veryfast`). Plan 20-01 ports this. |
| **COD-02** | Frames chunked into NINJAM upload-interval payloads following NinjamZap 4 KB-chunk + 4-byte BE length prefix convention | Phase 14.3-02 substrate already does the 4 KB chunking on the run-thread drain side (`MAX_ENC_BLOCKSIZE` split — see `src/core/njclient.cpp:2870-2884`). Plan 20 adds the 4-byte BE length prefix as a per-frame heap buffer the encoder allocates before `RawDataSendWrite`. Reference: `RESEARCH-ADDENDUM.md` "Per-frame chunk framing" + TestClient.cpp:120-127. |
| **WIRE-01** | Channel 1, fourCC `H264`, 24-byte marker, SPS/PPS chunk #2, per-frame BE length prefix | Wire format locked in `260515-0pc-RESEARCH-ADDENDUM.md` "Wire format spec" section. Reference impl: `ninjamzap-core/njclient.cpp:3041-3082` (verbatim audio-thread block). Plan 20-02 ports this. |
| **WIRE-03** | Concurrent audio + video producers do not race on `Net_Connection::Send` | Phase 14.3-02 substrate already resolved (any-thread producer via `m_rawdata_sendq` SPSC; run thread is sole `Net_Connection::Send` caller for RawData). Plan 20 must verify the capacity-64 ring absorbs the per-frame cadence under HD broadcast (acceptance criterion: `m_rawdata_sendq_overflows == 0` after 5-minute populated-server UAT). |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Cisco openh264 | 2.1.1 (last Cisco prebuilt for macOS) | H.264 Baseline encoder; royalty-free for end users (Cisco pays MPEG-LA) | [VERIFIED: spike vendored at `libs/ffmpeg/macos-x86_64/lib/libopenh264.6.dylib`] Already vendored Phase 14.3-01. JamTaba uses it. NinjamZap iOS uses VideoToolbox but architects for libavcodec/openh264 on desktop. |
| libavcodec (ffmpeg LGPL build) | 61.19.101 (= ffmpeg 7.1.2 era) | Encoder framework wrapping openh264 | [VERIFIED: spike vendored, link target `cmake/ffmpeg.cmake`] Same vendoring stack as Phase 14.3-01. `avcodec_find_encoder_by_name("libopenh264")` returns non-null in spike binary. |
| libswscale | 8.3.100 | BGRA→YUV420P color conversion on the encoder thread | [VERIFIED: spike vendored] More portable than JamTaba's hand-rolled RGB→YUV macro at `FFMpegMuxer.cpp:541-577`; NV12/YUV420P-aware; SIMD-accelerated. |
| libavutil | 59.39.100 | `AVFrame` allocation, `av_image_fill_arrays`, pixel-format constants | [VERIFIED: spike vendored] Transitive dep of libavcodec. |
| Phase 14.3-02 substrate (`m_rawdata_sendq` SPSC + `RawDataSendBegin`/`RawDataSendWrite` + run-thread drain) | shipped | Codec-agnostic transport — accepts any-thread producer, internally serialized, single `Net_Connection::Send` caller | [VERIFIED: `src/core/njclient.cpp:2810-2889` (drain block); `src/core/njclient.h:642-645` (API)] The single substrate Phase 20 depends on. |
| Phase 19 `JamWideFrameDistributor::Subscription` | shipped | RAII frame source; the encoder registers as a `Subscriber`, gets BGRA `juce::Image` frames at the capture rate, holds the `Subscription` member for the encoder's lifetime | [VERIFIED: `juce/video/native/JamWideFrameDistributor.h:33-107`] `Subscriber::onFrame(const juce::Image&)` is called on the camera-callback thread; the encoder must marshal to its own thread via an SPSC input ring (drop-oldest per D-07). |
| Phase 15.1-05 deferred-delete SPSC | shipped | Off-audio-thread deletion of orphaned `SpsPpsBuffer*` on encoder reconfigure | [VERIFIED: `src/core/njclient.h:723` `drainDeferredDelete()`] Phase 20 reuses the queue for `SpsPpsBuffer*` lifetime management (audio thread loads old pointer + pushes onto deferred-delete queue; run thread `delete`s). |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `juce::Thread` | JUCE 7 (vendored) | Encoder thread implementation | When a long-running, allocating, audio-callback-independent thread is needed. Existing precedent: `NinjamRunThread` at `juce/NinjamRunThread.cpp`. |
| `std::atomic<T>` with `acquire/release` | C++17 | Lock-free SPS/PPS pointer publication, `m_video_active` toggle, IDR-sync counter | Audio-thread side of any cross-thread state read; aligns with Phase 15.1 D-01 SPSC-mediated discipline. |
| `jamwide::SpscRing<T, N>` | Phase 15.1-04 shipped | Encoder input ring (`juce::Image` from camera-callback thread → encoder thread) | Drop-oldest backpressure per D-07. Capacity sized to one capture frame interval × small headroom (e.g., 8 for 10fps, 16 for 30fps). Producer = camera-callback thread (via `JamWideFrameDistributor::publish`); consumer = encoder thread. |
| `WDL_HeapBuf*` | WDL vendored | Phase 14.3-02 `RawDataItem::payload` ownership semantics | Encoder thread `new WDL_HeapBuf`, fills with `[4B BE length][NAL]`, hands to `RawDataSendWrite`. Substrate's drain `delete`s after Send. **Confirmed by reading 14.3-02 drain code: `delete item.payload` at `njclient.cpp:2886`.** |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `juce::Thread` for encoder | `std::thread` | `juce::Thread` plays nicer with JUCE's `Time` utilities and `MessageManager` notify hooks; `std::thread` would need slightly more manual lifecycle code. Negligible difference; default to `juce::Thread` for consistency with `NinjamRunThread`. |
| openh264 via libavcodec | Direct openh264 ISVCEncoder API | Direct API gets `ForceIntraFrame()` method without libavcodec's `pict->pict_type = AV_PICTURE_TYPE_I` indirection; ~10% less wrapper overhead. But spike + JamTaba both prove the libavcodec path works; rewriting against direct API for marginal benefit is unjustified. Stick with libavcodec wrapper for now. [CITED: ffmpeg/libavcodec/libopenh264enc.c] |
| `RC_BITRATE_MODE` | `RC_QUALITY_MODE` | Quality mode produces variable bitrate spikes that interact poorly with `ninjamzap-server`'s `VideoCongestionThreshold` per-subscriber drop logic. Bitrate mode gives the server a stable per-interval target to reason about. Locked by CONTEXT D-06. |
| Atomic `SpsPpsBuffer*` for SPS/PPS | Inline SPS/PPS in encoded frame stream | Inline emission ties SPS/PPS to keyframe cadence (which we have IDR-per-interval, so it would work) but adds bandwidth tax of duplicating SPS/PPS in every interval. Atomic pointer + emit-as-second-chunk-once-per-interval is more bandwidth-efficient and matches NinjamZap. |

**Installation:** Already complete. Phase 14.3-01 vendored everything. Plan 20 just consumes via existing `cmake/ffmpeg.cmake` IMPORTED INTERFACE target. No new vendoring work.

**Version verification:**

```bash
# These were verified during Phase 14.3-01:
ls libs/ffmpeg/macos-x86_64/lib/libopenh264.*.dylib  # libopenh264.6.dylib (Cisco v2.1.1)
ls libs/ffmpeg/macos-x86_64/lib/libavcodec.*.dylib   # libavcodec.61.19.101.dylib (ffmpeg 7.1.2)
ls libs/ffmpeg/macos-x86_64/lib/libswscale.*.dylib   # libswscale.8.3.100.dylib
```

For Plan 23 (cross-platform vendoring), `macos-arm64` is the open question: Cisco stopped publishing arm64-mac prebuilts past v2.1.1, so either (a) source-build openh264 v2.x for arm64 + accept MPEG-LA royalty obligation (~$0.20/license/year wholesale, may be under the small-developer cap), or (b) eventually fall back to VideoToolbox on arm64-mac. This is Phase 23 territory; Phase 20 only needs x86_64 to land.

## Package Legitimacy Audit

> Phase 20 installs **no new external packages**. All dependencies were vendored as source in Phase 14.3-01 (ffmpeg LGPL build from source; Cisco openh264 prebuilt fetched via documented official URL). No npm/pip/cargo registry surfaces involved.

| Package | Registry | Age | Downloads | Source Repo | slopcheck | Disposition |
|---------|----------|-----|-----------|-------------|-----------|-------------|
| ffmpeg 7.1.2 (libavcodec/libavformat/libavutil/libswscale) | source-build via `scripts/build_ffmpeg_lgpl.sh` | mature (>20 yrs) | N/A (project, not registry) | github.com/FFmpeg/FFmpeg | N/A — source build | Approved (Phase 14.3-01) |
| Cisco openh264 v2.1.1 | Cisco GitHub releases (osx64 prebuilt) | ~5 yrs (last Mac prebuilt) | N/A | github.com/cisco/openh264 | N/A — prebuilt | Approved (Phase 14.3-01) |

**Packages removed due to slopcheck [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none

Phase 20's substrate is already vendored; the only new code is C++ source code linking against already-trusted libraries.

## Architecture Patterns

### System Architecture Diagram

```
                      ┌────────────────────────────────────────────────┐
                      │ macOS AVFoundation / Win MediaFoundation       │
                      │ (camera-callback thread, JUCE "any thread")    │
                      └─────────────────────┬──────────────────────────┘
                                            │ juce::Image (BGRA)
                                            ▼
                  ┌─────────────────────────────────────────┐
                  │ JamWideFrameDistributor::publish(image) │
                  │ (Phase 19 — fan-out to N subscribers,   │
                  │  in-flight refcount lifetime model)     │
                  └─────────────────────────────────────────┘
                          │                              │
            (CameraPreviewTile  ─── Phase 19 ───)        │
                                                         ▼
                                  ┌──────────────────────────────────┐
                                  │ Openh264Encoder::onFrame(img)    │
                                  │ (Subscriber, camera-cb thread)   │
                                  │ try_push to input SPSC ring      │
                                  │  → drop-oldest on full           │
                                  │  → bump m_encoder_input_drops    │
                                  └─────────────────┬────────────────┘
                                                    │
                                       (cross-thread SPSC, capacity ~8)
                                                    │
                                                    ▼
                  ┌──────────────────────────────────────────────────┐
                  │ Encoder thread (juce::Thread, owned by encoder)  │
                  │ Loop:                                            │
                  │   1. pop juce::Image from input ring             │
                  │   2. read m_audio_interval_seq.load(acquire)     │
                  │      if changed since last → set                 │
                  │      eForceIntraFrame on next encode             │
                  │   3. sws_scale BGRA → YUV420P (own buffer)       │
                  │   4. avcodec_send_frame / receive_packet         │
                  │   5. on first emit / reconfigure:                │
                  │      build SpsPpsBuffer{[SPS-NAL][PPS-NAL]}      │
                  │      atomic_exchange m_video_spspps              │
                  │      push old pointer to deferred-delete SPSC    │
                  │   6. heap-alloc WDL_HeapBuf for                  │
                  │      [4B BE length][NAL bytes]                   │
                  │   7. NJClient::RawDataSendWrite(                 │
                  │        m_video_guid, buf, len, isEnd=false)      │
                  └─────────────────┬────────────────────────────────┘
                                    │
                                    ▼
                  ┌──────────────────────────────────────────────────┐
                  │ NJClient::RawDataSendWrite (Phase 14.3-02 API)   │
                  │ - alloc RawDataItem POD                          │
                  │ - try_push onto m_rawdata_sendq (SpscRing<64>)   │
                  │ - on overflow: bump m_rawdata_sendq_overflows    │
                  │                + writeLog warning                │
                  │                + delete payload heap-buf         │
                  └─────────────────┬────────────────────────────────┘
                                    │
              ┌─────────────────────┴─────────────────────┐
              │                                            │
              ▼                                            ▼
   ┌─────────────────────┐                  ┌──────────────────────────────┐
   │ Run thread          │                  │ Audio thread                  │
   │ (NJClient::Run)     │                  │ (NJClient::AudioProc)         │
   │                     │                  │   → on_new_interval()         │
   │ Per ~20ms tick:     │                  │ (Phase 15.1 hardened —        │
   │  1. drain           │                  │  no mutex / no malloc / no I/O│
   │     m_rawdata_sendq │                  │  / no logging on audio path)  │
   │  2. for each item:  │                  │                               │
   │     - if BEGIN:     │                  │ if m_video_active.load(acquire)│
   │       build         │                  │   if m_video_interval_open:   │
   │       upload_       │                  │     RawDataSendWrite(         │
   │       interval_     │                  │       guid, NULL, 0, true)    │
   │       begin Send    │                  │       // END previous         │
   │     - if WRITE:     │                  │   RawDataSendBegin(           │
   │       split payload │                  │     m_video_guid,             │
   │       at            │                  │     MAKE_NJ_FOURCC('H264'),   │
   │       MAX_ENC_BLOCK │                  │     chidx=1, estsize=0)       │
   │       SIZE          │                  │   m_video_interval_open=true  │
   │       Send each     │                  │   // 24B marker on stack      │
   │       chunk         │                  │   unsigned char marker[24];   │
   │  3. delete payload  │                  │   marker[0..3] = BE u32(20)   │
   │     heap-bufs       │                  │   marker[4..7] = BE u32(      │
   │  4. Pattern C       │                  │     m_sync_interval_cnt)      │
   │     discard guard   │                  │   marker[8..23] = copy from   │
   │     if !m_netcon    │                  │     m_locchan_mirror[0]       │
   │                     │                  │     .guid (or zeros)          │
   │ (sole               │                  │   RawDataSendWrite(           │
   │  Net_Connection::   │                  │     guid, marker, 24, false)  │
   │  Send caller        │                  │   // SPS/PPS conditional      │
   │  for RawData)       │                  │   auto* sp =                  │
   │                     │                  │     m_video_spspps.load(      │
   │                     │                  │       acquire);               │
   │                     │                  │   if (sp != nullptr)          │
   │                     │                  │     RawDataSendWrite(         │
   │                     │                  │       guid, sp->data,         │
   │                     │                  │       sp->len, false)         │
   │                     │                  │                               │
   │                     │                  │ else if m_video_interval_open │
   │                     │                  │   RawDataSendWrite(           │
   │                     │                  │     guid, NULL, 0, true)      │
   │                     │                  │   m_video_interval_open=false │
   │                     │                  │                               │
   │                     │                  │ m_audio_interval_seq          │
   │                     │                  │   .fetch_add(1, relaxed)      │
   │                     │                  │   // IDR sync signal          │
   └─────────────────────┘                  └──────────────────────────────┘
              │
              ▼
   ┌─────────────────────────────────────────────────────────┐
   │ Net_Connection::Send → TCP socket → NINJAM server       │
   │ (already-running ninjamzap-server: per-room thread +    │
   │  two-pass audio-priority + per-subscriber video         │
   │  congestion drop @ VideoCongestionThreshold=50%)        │
   └─────────────────────────────────────────────────────────┘
```

### Recommended Project Structure

```
juce/video/encoder/
├── VideoEncoder.h              # Pure-virtual interface (D-01)
├── VideoEncoderConfig.h        # POD config (width/height/fps/bitrateKbps/profile)
├── SpsPpsBuffer.h              # Immutable {data:vector<uint8_t>, len:size_t} for atomic pointer publish
├── Openh264Encoder.h           # Concrete impl
├── Openh264Encoder.cpp         # Constructor opens libavcodec context;
│                               # owns juce::Thread; owns input SpscRing<juce::Image>
│                               # owns sws_scale context; owns BGRA→YUV420P bounce buffer
└── VideoChunker.h              # (optional) [4B BE length][NAL] heap-buf builder helper

juce/JamWideJuceProcessor.h     # Add unique_ptr<VideoEncoder> encoder_
juce/JamWideJuceProcessor.cpp   # Construct on camera-open (Phase 19 hook); start thread on broadcast-on
juce/ui/ConnectionBar.{h,cpp}   # Add Broadcast button (state-machine on camera button or separate)

src/core/njclient.h             # Add:
                                #   std::atomic<bool> m_video_active{false}
                                #   std::atomic<SpsPpsBuffer*> m_video_spspps{nullptr}
                                #   std::atomic<uint64_t> m_audio_interval_seq{0}
                                #   int m_video_chidx{1}
                                #   unsigned char m_video_guid[16]{}
                                #   unsigned int m_video_fourcc{MAKE_NJ_FOURCC('H','2','6','4')}
                                #   bool m_video_interval_open{false}  // audio-thread-local
                                #   std::atomic<uint64_t> m_encoder_input_drops{0}
                                #   New public API:
                                #     SetVideoChannel(chidx, fourcc)
                                #     StopVideoChannel()
                                #     QueueVideoFrame(data, len)       // calls RawDataSendWrite
                                #     SetVideoSPSPPS(data, len)
                                #     GetEncoderInputDropCount() accessor

src/core/njclient.cpp           # Add to on_new_interval() (audio thread): the video block per D-08
                                # Add to NinjamRunThread connect-up: SetLocalChannelInfo + SetVideoChannel
                                #                                      + NotifyServerOfChannelChange

tests/test_video_encoder.cpp    # Unit: synthetic frames in → expected NAL bytes out;
                                # SPS/PPS regeneration on reconfigure; drop-oldest counter;
                                # eForceIntraFrame on interval-seq change.
tests/test_video_send_state.cpp # Unit: drive on_new_interval with mock state;
                                # verify marker bytes + SPS/PPS conditional emission +
                                # END/BEGIN ordering.
```

### Pattern 1: Audio-Thread Lock-Free SPS/PPS Read

**What:** Audio thread (`on_new_interval`) reads the cached SPS/PPS as one atomic load + one memcpy into a stack-allocated chunk buffer, no mutex.

**When to use:** Phase 15.1 D-01 forbids `WDL_Mutex` on the audio path. This is the Path A primary pattern.

**Example (Path A):**
```cpp
// Source: NinjamZap njclient.cpp:3041-3076 verbatim, with Path A primitives substituted
// for m_video_cs / m_video_spspps_cs

void NJClient::on_new_interval() {
  // ... existing audio interval handling (Phase 15.1-06 / -07b mirror reads) ...

  // Phase 20 video block (Path A: atomics):
  if (m_video_active.load(std::memory_order_acquire)) {
    if (m_video_interval_open) {
      RawDataSendWrite(m_video_guid, nullptr, 0, /*isEnd*/true);     // END previous
    }
    RawDataSendBegin(m_video_guid, m_video_fourcc, m_video_chidx, 0); // BEGIN new
    m_video_interval_open = true;

    // 24-byte marker — fully on stack, no allocation
    unsigned char marker[24];
    marker[0] = 0; marker[1] = 0; marker[2] = 0; marker[3] = 20;     // BE u32 = 20
    marker[4] = (unsigned char)((m_sync_interval_cnt >> 24) & 0xFF);
    marker[5] = (unsigned char)((m_sync_interval_cnt >> 16) & 0xFF);
    marker[6] = (unsigned char)((m_sync_interval_cnt >>  8) & 0xFF);
    marker[7] = (unsigned char)( m_sync_interval_cnt        & 0xFF);
    memset(marker + 8, 0, 16);
    // Phase 15.1-06 LocalChannelMirror gives audio-thread-safe access to ch0 GUID
    const auto& lcm0 = m_locchan_mirror[0];
    if (lcm0.active && lcm0.channel_idx == 0) {
      memcpy(marker + 8, lcm0.curwritefile_guid, 16);  // NEW mirror field — see Open Q below
    }
    RawDataSendWrite(m_video_guid, marker, 24, /*isEnd*/false);

    // SPS/PPS: conditional emit (cold-start safety per D-13)
    SpsPpsBuffer* sp = m_video_spspps.load(std::memory_order_acquire);
    if (sp != nullptr && sp->len > 0) {
      RawDataSendWrite(m_video_guid, sp->data, (int)sp->len, /*isEnd*/false);
    }
  } else if (m_video_interval_open) {
    RawDataSendWrite(m_video_guid, nullptr, 0, /*isEnd*/true);
    m_video_interval_open = false;
  }

  // Wake encoder thread for IDR sync (relaxed is fine — encoder polls)
  m_audio_interval_seq.fetch_add(1, std::memory_order_relaxed);
}
```

### Pattern 1b: Path B Carve-out (NinjamZap-literal)

**What:** Replace the two `std::atomic` primitives with `WDL_Mutex` taken on the audio thread, byte-for-byte mirroring NinjamZap.

**When to use:** If Plan 20-03 UAT exposes ≥3 sync correctness failures under Path A that cannot be resolved by ordering/fencing fixes within the same iteration count. Trigger is recorded in Plan 20-03 acceptance criteria.

**Example (Path B — change is localized):**
```cpp
// Source: NinjamZap njclient.cpp:3041-3076 unchanged from the verbatim port.

// In njclient.h, replace:
//   std::atomic<bool> m_video_active{false};
//   std::atomic<SpsPpsBuffer*> m_video_spspps{nullptr};
// with:
//   bool m_video_active{false};
//   WDL_Mutex m_video_cs;
//   WDL_HeapBuf m_video_spspps;
//   WDL_Mutex m_video_spspps_cs;

void NJClient::on_new_interval() {
  // ... existing audio interval handling ...

  m_video_cs.Enter();                       // <-- audio-thread mutex acquisition
  bool active = m_video_active;
  m_video_cs.Leave();

  if (active) {
    // ... same BEGIN/marker emission ...

    m_video_spspps_cs.Enter();              // <-- second audio-thread mutex
    if (m_video_spspps.GetSize() > 0) {
      RawDataSendWrite(m_video_guid, m_video_spspps.Get(), m_video_spspps.GetSize(), false);
    }
    m_video_spspps_cs.Leave();
  } else if (m_video_interval_open) {
    // ... END handling unchanged ...
  }
}
```

**Path B carve-out lines to flag in the `realtime-audio-reviewer` agent's allow-list:**
- `src/core/njclient.cpp` `on_new_interval` — two `WDL_Mutex::Enter()`/`Leave()` pairs (`m_video_cs`, `m_video_spspps_cs`).
- Rationale to record in the audit allow-list entry: "NinjamZap-literal sync carve-out per CONTEXT.md D-09/D-10; budget verified ≤ X μs under populated server + Y kbps broadcast in Plan 20-03 UAT (Path B trigger criteria met)."

### Pattern 2: Encoder Thread Emits Per-Frame Chunks Directly

**What:** Encoder thread `new`s a `WDL_HeapBuf`, fills with `[4B BE length][NAL bytes]`, calls `RawDataSendWrite(guid, heapbuf->Get(), heapbuf->GetSize(), false)`. The substrate copies into its own item-owned heap-buf inside the SPSC; encoder thread can immediately free its local copy. After Send, substrate's run-thread drain `delete`s the item's heap-buf.

**When to use:** Every encoded NAL unit. Verbatim NinjamZap pattern at `njclient.cpp:2116-2123`.

**Example:**
```cpp
// Source: ninjamzap-core/njclient.cpp:2116-2123 + the chunk-format
// from tests/video-sync/harness/TestClient.cpp:120-127 (caller-side wrap)

void Openh264Encoder::onPacketReady(const uint8_t* nalBytes, int nalLen, bool isKeyframe) {
  if (!njClient_->isVideoActive()) return;  // public accessor reading m_video_active.load(acquire)

  // Wrap: [4B BE length][NAL payload]
  const int chunkLen = 4 + nalLen;
  // Heap-alloc a small staging buffer to pass to RawDataSendWrite.
  // RawDataSendWrite internally copies into its own item.payload heap-buf —
  // we can free `chunk` immediately after the call returns.
  std::vector<uint8_t> chunk(chunkLen);
  chunk[0] = (uint8_t)((nalLen >> 24) & 0xFF);
  chunk[1] = (uint8_t)((nalLen >> 16) & 0xFF);
  chunk[2] = (uint8_t)((nalLen >>  8) & 0xFF);
  chunk[3] = (uint8_t)( nalLen        & 0xFF);
  memcpy(chunk.data() + 4, nalBytes, nalLen);

  njClient_->RawDataSendWrite(videoGuid_, chunk.data(), chunkLen, /*isEnd*/false);
}
```

### Pattern 3: SPS/PPS Atomic Pointer Swap with Deferred Delete

**What:** Encoder allocates a new `SpsPpsBuffer{data, len}` immutable struct, `atomic_exchange`s the published pointer, pushes the old pointer onto the existing Phase 15.1-05 deferred-delete SPSC.

**When to use:** On first encode and after every encoder reconfigure (preset change, fatal error, resolution change).

**Example:**
```cpp
// Source: established pattern — Phase 15.1-05 + Phase 14.3-02 atomic-publish idiom

struct SpsPpsBuffer {
  std::vector<uint8_t> data;  // concatenated [SPS-NAL][PPS-NAL]
  size_t len() const { return data.size(); }
};

void Openh264Encoder::publishNewSpsPps(const uint8_t* nalBytes, size_t totalLen) {
  auto* fresh = new SpsPpsBuffer{ std::vector<uint8_t>(nalBytes, nalBytes + totalLen) };

  // Atomic-swap with the currently published pointer.
  SpsPpsBuffer* old = njClient_->m_video_spspps.exchange(fresh, std::memory_order_acq_rel);

  if (old != nullptr) {
    // Old pointer goes to the deferred-delete queue. Run thread frees it.
    // Reusing Phase 15.1-05 infrastructure — the queue is for DecodeState* today,
    // but the pattern generalizes; either:
    //   (a) extend the existing queue with a generic deleter callback, OR
    //   (b) add a separate m_spspps_deferred_delete_q (same shape, smaller capacity).
    // Planner picks; (b) keeps types simple.
    njClient_->enqueueSpsPpsForDeferredDelete(old);
  }
}
```

### Anti-Patterns to Avoid

- **Audio thread allocating heap memory for the marker or SPS/PPS chunk** — Phase 15.1 D-01 forbids. The 24-byte marker MUST be stack-allocated. SPS/PPS MUST come via the atomic pointer (no `memcpy` of growable buffer into temp). NinjamZap's `WDL_HeapBuf m_video_spspps` works because they accept the mutex tax; Path A swaps this for atomic pointer.
- **Encoder thread copying frame payloads through an extra SPSC ring to the audio thread** — wastes a copy and adds latency. The substrate's `m_rawdata_sendq` already serializes; encoder calls `RawDataSendWrite` directly. CONTEXT D-12 locks this.
- **Calling `m_netcon->Send` from the encoder thread** — `Net_Connection::Send` is not thread-safe (spike Q3 finding at `260515-0pc-spike-results.md`). Always go through `RawDataSendBegin/Write` → substrate → run thread drain.
- **Coupling encoder lifecycle to camera-open** — encoder runs idle at ~3 W when no broadcast. Per CONTEXT D-13, encoder thread starts at broadcast-on, stops at broadcast-off. Camera-on alone does not spin up the encoder.
- **Skipping the deferred-delete on `SpsPpsBuffer*` swap** — if encoder reconfigures while the audio thread is mid-`on_new_interval` reading the old SPS/PPS pointer, freeing it on the encoder thread is a UAF. Phase 15.1-05 deferred-delete is the correct pattern; do not deviate.
- **Bumping `RAWDATA_SEND_QUEUE_CAPACITY` from 64 without checking allocation lifecycle** — `feedback_size_constant_lifecycle_audit` memory: 14.3-02's locked capacity at 64 sized for ~2-4 interval items/peer/sec. Phase 20 adds per-frame items at 10-30 fps, so the cadence is much higher BUT bounded by the run-thread drain at ~20 ms ticks, so 64 items is still ~1.5 sec of headroom at 30 fps. If overflow observed during UAT, audit allocation cost before bumping (`m_rawdata_sendq` slots are POD with `WDL_HeapBuf*` ownership; bumping to 128 doubles 64*sizeof(RawDataItem) ≈ 64*48 = 3 KB → 6 KB heap on NJClient instance, negligible).
- **Ignoring the END message at broadcast-off** — CONTEXT D-08 specifies "END at deactivate". If `m_video_active` flips false mid-stream and the audio thread skips the END, NINJAM server holds the upload-interval slot open until timeout (`VideoTransferTimeout` default 30s) and downstream receivers see a stuck interval. Always emit END.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| H.264 encoding | Custom NAL packetizer + entropy coder | `libavcodec` + `libopenh264` (vendored) | Two decades of optimisation; LGPL-clean; royalty-free for end users via Cisco; spike-proven at our resolutions. |
| BGRA→YUV420P color conversion | Hand-rolled per-pixel macro (JamTaba `FFMpegMuxer.cpp:541-577` pattern) | `libswscale sws_scale` | SIMD-accelerated; format-aware; trivially supports resolution rescale (NV12 fallback if we ever support iOS hardware decoder); no special-case bugs for odd image strides. |
| Cross-thread SPS/PPS publication | Mutex around heap buffer (NinjamZap pattern) on audio thread | `std::atomic<SpsPpsBuffer*>` with acquire/release + Phase 15.1-05 deferred-delete | Path A: Phase 15.1 D-01 lock-free invariant. Path B (escape hatch) only if Path A fails sync correctness gates. |
| Frame backpressure | Block camera-callback thread on full encoder ring | `jamwide::SpscRing` with drop-oldest + counter | Phase 19's `JamWideFrameDistributor::publish` cannot block (camera-callback thread reaches into the audio engine indirectly). Drop-oldest = "encoder will catch up next frame"; counter exposes silent loss for observability. Mirrors Phase 15.1-05 fallback discipline. |
| 4 KB chunk splitting | Custom chunker per call site | Phase 14.3-02 substrate's run-thread drain (`njclient.cpp:2870-2884`) | Already shipped; splits any `RawDataItem.payload` at `MAX_ENC_BLOCKSIZE` automatically; per-chunk `mpb_client_upload_interval_write` Send. Encoder just passes the full `[4B BE length][NAL]` buffer; substrate handles the rest. |
| `Net_Connection::Send` thread-safety | Per-site `WDL_Mutex` wrap | Phase 14.3-02 substrate (sole run-thread Send caller via `m_rawdata_sendq` SPSC) | Already shipped; verified by `test_rawdata_send` + Codex M-8 overflow counter. |
| 24-byte marker construction at runtime | Cached marker as member buffer | Stack-allocated `unsigned char marker[24]` inside `on_new_interval` | 24 bytes is too small to bother caching; the marker varies every interval anyway (`m_sync_interval_cnt` changes + audio ch0 GUID changes when broadcast cycles). Stack alloc is one mov + 6 BE-encode stores. |
| Interval-counter signaling encoder→audio sync | SPSC queue with `IntervalStart{seq, swap, guid}` records | `std::atomic<uint64_t> m_audio_interval_seq` | One atomic increment vs queue push; one acquire-load vs queue drain. Up to 1 frame drift acceptable (CONTEXT D-15). Lock-free; naturally handles BPM/BPI changes. |
| openh264 IDR forcing | Manual SPS/PPS reinjection per IDR | `eForceIntraFrame` per-frame flag via openh264 SetOption | One openh264 API call right before encode; encoder handles SPS/PPS regen internally. JamTaba pattern. [CITED: cisco/openh264 wiki "ISVCEncoder::ForceIntraFrame"] |

**Key insight:** Phase 20 has almost no novel implementation — it's a *port* of `ninjamzap-core/njclient.cpp:2047-2123, 3041-3082` into JamWide-idioms (atomics + SPSC instead of WDL_Mutex + WDL_PtrList). The risk surface is in the **threading model translation** (Path A correctness verification), not in inventing wire format or encoder configuration. Spend planning budget on Path A correctness gates, not on encoder bring-up.

## Runtime State Inventory

> This is a port/refactor phase. Inventory required.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| **Stored data** | None — the encoder owns no persistent state. The Phase 19 `<camera>` ValueTree subtree at `juce/JamWideJuceProcessor` may gain a `broadcastAck` boolean (planner discretion — see Privacy Modal in Open Q) but no existing keys are renamed. | None — additive only |
| **Live service config** | NINJAM server: video channel registration at `chidx=1` is **new wire traffic** but does not modify any stored config. JamWide's existing NINJAM server browser is **untouched** per STATE.md PASS 3 lock. `video.ninjamzap.com:2049` is the recommended public test server but is not encoded anywhere — beta testers enter it manually. | None for stored config; the new wire-level channel registration goes via the existing `SetLocalChannelInfo` + `NotifyServerOfChannelChange` plumbing |
| **OS-registered state** | None — no Task Scheduler / launchd / pm2 surfaces touched. macOS entitlements (`com.apple.security.device.camera`) and Info.plist (`NSCameraUsageDescription`) were added by Phase 19 D-28; Phase 20 does not modify them. | None |
| **Secrets and env vars** | None — no new SOPS keys, no .env, no CI env vars introduced. The vendored ffmpeg path is established by Phase 14.3-01 via `cmake/ffmpeg.cmake`. | None |
| **Build artifacts / installed packages** | New CMake target `Openh264Encoder` (or similar) compiled into the existing JUCE plugin targets (Standalone/VST3/AU/CLAP). New test target `test_video_encoder` and `test_video_send_state` under `JAMWIDE_BUILD_TESTS=ON`. **No new dynamic libraries** — encoder links against already-vendored libavcodec/libopenh264/libswscale via existing `cmake/ffmpeg.cmake` IMPORTED INTERFACE target. | Rebuild plugin targets; no clean rebuild required |

**Nothing found in category — Stored data, Live service config, OS-registered state, Secrets and env vars:** Verified by grep + reading Phase 19 D-25, Phase 14.3-01/-02 summaries, project memories. Phase 20 is strictly additive at the JUCE-app and NJClient public-API surfaces; substrate is shipped; wire format is locked.

## Common Pitfalls

### Pitfall 1: Audio-thread access to canonical `Local_Channel` for the marker's audio_ch0_guid

**What goes wrong:** The 24-byte marker carries `lc->m_curwritefile.guid` for the local user's audio channel 0. NinjamZap's pattern at `njclient.cpp:3062-3068` iterates `m_locchannels` and reads `lc->m_curwritefile.guid` directly. JamWide's Phase 15.1-06 deviation #1 prohibits the audio thread from dereferencing canonical `Local_Channel*` — all access must go through `m_locchan_mirror[ch]` by value.

**Why it happens:** The most natural verbatim port copies the NinjamZap audio-thread block, hits `m_locchannels.Get(li)->m_curwritefile.guid`, and the canonical pointer dereference races with `DeleteLocalChannel` happening on the run thread.

**How to avoid:** Add `unsigned char curwritefile_guid[16]` to `LocalChannelMirror` (the audio-thread-owned mirror struct), populated by the run thread on every `LocalChannelInfoUpdate` that includes a GUID change. The audio thread reads `m_locchan_mirror[0].curwritefile_guid` (16-byte memcpy). The GUID *changes* whenever `m_curwritefile` rolls over for a new audio interval, which already happens on the audio thread, so the mirror needs a new audio→encoder publish path: when the audio thread rolls the audio file (existing code path), it writes its new local GUID to `m_locchan_mirror[0].curwritefile_guid`. The audio thread is sole-writer to the mirror's audio-thread-owned fields (already established by Phase 15.1-07b adding `bcast_active` and `curwritefile_curbuflen`).

**Warning signs:** TSan race report between `mixInChannel` site (where m_curwritefile rolls) and `on_new_interval` site (where the marker reads it). Or sporadic crashes during `DeleteLocalChannel` while broadcasting.

### Pitfall 2: SPS/PPS deferred-delete race during encoder reconfigure

**What goes wrong:** Encoder thread reconfigures (preset change), atomic-swaps a fresh `SpsPpsBuffer*` in, queues the old one for deferred delete. Audio thread's `on_new_interval` already loaded the old pointer before the swap. Run thread drains the deferred-delete queue and `delete`s the old buffer. Audio thread then `memcpy`s from a freed pointer = UAF.

**Why it happens:** No generation barrier between the audio thread's `load(acquire)` and its subsequent `memcpy`. The deferred-delete queue assumes audio-thread observation has ceased before deletion, but a single `load` doesn't establish that.

**How to avoid:** Mirror Phase 15.1-06 HIGH-3's publish-wait-defer protocol. Either:
- **(a)** Use the existing `m_audio_drain_generation` mechanism: encoder reconfigure publishes new pointer + records gen=`m_audio_drain_generation.load(acquire)+1`; run thread holds the old pointer in a staging slot until it observes audio thread bump the generation; only then push to deferred-delete queue.
- **(b)** Simpler alternative: audio thread treats the loaded pointer as valid only **within the single `on_new_interval` call** — copies the SPS/PPS bytes into a temporary buffer at the start of the call, uses the temp for the chunk emission, and any pointer access ends with the call return. Encoder reconfigure then needs only ONE drain-cycle delay (run thread sees audio thread has returned from `on_new_interval` at least once after the swap) before the deferred delete. Phase 15.1-05 deferred-delete already provides this run-thread-sees-after-drain semantics for `DecodeState*` — reuse it.

**Warning signs:** TSan race report between encoder thread's `delete` (queued via run-thread drain) and audio thread's `on_new_interval` read. Or sporadic SIGSEGV in `RawDataSendWrite` with non-null but trash pointer.

### Pitfall 3: Encoder thread heap allocation throttle under HD broadcast

**What goes wrong:** At 30 fps × 800 kbps (High preset), encoder thread allocates one `WDL_HeapBuf`-equivalent per frame for the `[4B BE length][NAL]` wrap, calls `RawDataSendWrite` which internally allocates another `WDL_HeapBuf` payload. That's ~60 heap allocs/sec just for the wire framing, plus `sws_scale`'s internal allocs. macOS `malloc` lock contention can starve audio thread under load (`mach_vm_allocate` is the known offender per `feedback_size_constant_lifecycle_audit`).

**Why it happens:** Heap alloc cadence scales with framerate. JamWide's audio thread doesn't allocate (Phase 15.1) but the system allocator is still process-global; encoder thread's allocs can serialize with `malloc_zone_malloc` calls anywhere else.

**How to avoid:**
- Pre-allocate a frame buffer pool on encoder thread (e.g., 4 slots of max-expected NAL size, rotated). `WDL_HeapBuf` supports `Resize(N, true)` for amortised reuse.
- Pre-allocate `sws_scale` working buffers once at encoder open; reuse across frames.
- Profile under Instruments → Allocations tool while broadcasting at High preset; verify the encoder thread's allocation cadence is bounded.

**Warning signs:** Activity Monitor shows high `mach_vm_*` syscalls during HD broadcast. Audio glitches that correlate with encoder frame cadence.

### Pitfall 4: Path B trigger detection ambiguity

**What goes wrong:** Plan 20-03 UAT trips on a sync correctness failure. The team starts trying Path A fixes (memory ordering, fence insertion, mirror field additions). Without a clear iteration cap, the team burns weeks of cycles on Path A patches before falling back to Path B — exactly what `feedback_proven_over_pure` warns against.

**Why it happens:** Sync correctness bugs are notoriously hard to diagnose under Path A's lock-free reasoning. Each "fix attempt" feels like progress but doesn't actually move the line.

**How to avoid:** Plan 20-03 acceptance criteria record the Path B trigger objectively:
- **Trigger:** Three failed fix attempts under Path A against the critical sync scenarios (named below).
- **Each "fix attempt" definition:** A landed commit with TSan-green + a UAT pass attempt against the scenario.
- **Failure definition:** UAT shows DROP-RESYNC events, hold_count exceedance, or human-listener "video appears one interval early" report.
- **Switch cost recorded:** ~30 LOC + audit-allowlist entry + CONTEXT.md amendment. Plan 20-04 is staged as a conditional contingency.

The discipline is what protects us. The triggers must be falsifiable and pre-agreed.

**Warning signs:** Repeated "Path A revision N+1 didn't work either" commits without progress on the underlying sync gate.

### Pitfall 5: Cold-start gap (encoder spin-up vs first interval BEGIN)

**What goes wrong:** User clicks Broadcast. UI thread sets `m_video_active=true`. Audio thread's next `on_new_interval` sees active, emits BEGIN + marker, but `m_video_spspps` is still null (encoder thread hasn't generated SPS/PPS yet). Receiver sees BEGIN + marker, then frame chunks, but no SPS/PPS → decoder init fails → black screen until next interval.

**Why it happens:** Encoder spin-up is ~50-150 ms (Cisco openh264 SDK + first IDR). NINJAM intervals at 120 BPM × 16 BPI are 8 seconds → encoder is ready well before next interval. But at 240 BPM × 4 BPI = 1 second → encoder is NOT ready by next interval.

**How to avoid:** D-13 cold-start pattern is correct as documented: the audio thread emits BEGIN + marker every interval but emits SPS/PPS *only if* `m_video_spspps.load() != nullptr`. The first interval after broadcast-on is marker-only. The encoder produces frames + SPS/PPS within ~150 ms. Subsequent intervals carry SPS/PPS. **Receiver behavior:** NinjamZap receiver tolerates marker-only intervals (it just doesn't init decoder until SPS/PPS arrives). Reference: `RESEARCH-ADDENDUM.md` SPS/PPS placement section + `25_no_initial_spspps.cpp` scenario.

**Warning signs:** Receiver logs "decoder init failed" right after broadcast-on. Fixed by waiting one interval.

### Pitfall 6: NINJAM connect-up timing — registering chidx=1 before authentication completes

**What goes wrong:** Plan 20-02 wires `SetLocalChannelInfo + SetVideoChannel + NotifyServerOfChannelChange` into the NINJAM connect-up callback. If wired too early (before AUTH passes), the server rejects the channel info.

**Why it happens:** NINJAM protocol requires AUTH → channel registration; out-of-order is a protocol error.

**How to avoid:** Reuse the same hook the existing audio channel registration uses. Grep `juce/NinjamRunThread.cpp` for the post-AUTH callback site (`NJClient::SetLocalChannelInfo` is already called from somewhere — Phase 20 mirrors that exact ordering).

**Warning signs:** Server log: "channel info before auth"; JamWide log: AUTH succeeded but channel never registers.

## Code Examples

Verified patterns from official sources:

### NinjamZap Send-Side State Machine (verbatim port target)

```cpp
// Source: /Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3041-3082
// Phase 20 Plan 20-02 ports this verbatim, with one substitution:
// audio-thread mutex acquires → atomic loads (Path A) OR retained mutex (Path B).

if (m_video_active) {
  if (m_video_interval_open)
    RawDataSendWrite(m_video_guid, NULL, 0, true); // END previous
  RawDataSendBegin(m_video_guid, m_video_fourcc, m_video_chidx, 0); // BEGIN new
  m_video_interval_open = true;
  {
    unsigned char marker[24];
    marker[0] = 0; marker[1] = 0; marker[2] = 0; marker[3] = 20; // BE u32 = 20
    marker[4] = (unsigned char)((m_sync_interval_cnt >> 24) & 0xFF);
    marker[5] = (unsigned char)((m_sync_interval_cnt >> 16) & 0xFF);
    marker[6] = (unsigned char)((m_sync_interval_cnt >> 8) & 0xFF);
    marker[7] = (unsigned char)(m_sync_interval_cnt & 0xFF);
    memset(marker + 8, 0, 16);
    for (int li = 0; li < m_locchannels.GetSize(); li++) {       // NinjamZap-literal — JamWide
      Local_Channel *lc = m_locchannels.Get(li);                 // uses m_locchan_mirror[0]
      if (lc && lc->channel_idx == 0) {                          // .curwritefile_guid instead
        memcpy(marker + 8, lc->m_curwritefile.guid, 16);
        break;
      }
    }
    RawDataSendWrite(m_video_guid, marker, 24, false);
  }
  m_video_spspps_cs.Enter();                                     // Path B → kept as-is
  if (m_video_spspps.GetSize() > 0)                              // Path A → atomic load
    RawDataSendWrite(m_video_guid, m_video_spspps.Get(), m_video_spspps.GetSize(), false);
  m_video_spspps_cs.Leave();
} else if (m_video_interval_open) {
  RawDataSendWrite(m_video_guid, NULL, 0, true);
  m_video_interval_open = false;
}
```

### NinjamZap Encoder Thread Call (verbatim port target)

```cpp
// Source: /Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2116-2123
// Phase 20 encoder thread calls QueueVideoFrame after each NAL emit;
// QueueVideoFrame internally calls RawDataSendWrite.

void NJClient::QueueVideoFrame(const void *data, int len) {
  if (!m_video_active || !m_video_interval_open) return;
  if (data && len > 0)
    RawDataSendWrite(m_video_guid, data, len, false);
}
```

JamWide note: under Path A, `m_video_active` is `std::atomic<bool>` (acquire load). `m_video_interval_open` is **audio-thread-local** — but `QueueVideoFrame` is called from the encoder thread. NinjamZap's `m_video_interval_open` is also read without a lock here; this is fine because (a) a false-negative race (read sees `false`, audio thread just set `true`) just drops one frame's worth of payload, which the next interval refills; (b) a false-positive race (read sees `true`, audio thread is about to set `false`) just queues bytes that the substrate Sends but the receiver discards because the BEGIN's already ended.

Make `m_video_interval_open` `std::atomic<bool>` (relaxed) too, to make the cross-thread read explicit and TSan-clean.

### JamTaba openh264 Configure Block (port target for Plan 20-01)

```cpp
// Source: /Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:237-277
// Phase 20 Plan 20-01 ports this into Openh264Encoder::open().

bool FFMpegMuxer::addVideoStream(AVCodecID codecID, AVDictionary **opts) {
  codec = avcodec_find_encoder(codecID);  // pass AV_CODEC_ID_H264; libavcodec picks libopenh264
  if (!codec) return false;

  codecContext = avcodec_alloc_context3(codec);
  if (!codecContext) return false;

  codecContext->codec_id = codecID;
  codecContext->bit_rate = videoBitRate;            // 100000 / 300000 / 800000 per preset
  codecContext->rc_max_rate = videoBitRate;
  codecContext->rc_buffer_size = videoBitRate;
  codecContext->width  = videoResolution.width();   // 320 / 640 / 1280 per preset
  codecContext->height = videoResolution.height();  // 240 / 480 / 720 per preset
  codecContext->time_base = AVRational{ 1, (int)videoFrameRate };  // 10 / 15 / 30
  codecContext->gop_size = 30;                      // Phase 20 sets to fps × interval_seconds — see Open Q
  codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
  // Phase 20 additions:
  //   codecContext->profile = FF_PROFILE_H264_BASELINE;  // D-05
  //   codecContext->level   = 31;                        // D-05 (Baseline 3.1)
  //   av_opt_set(codecContext->priv_data,
  //              "rc_mode", "bitrate", 0);                 // D-06 (RC_BITRATE_MODE)
  //   av_opt_set(codecContext->priv_data,
  //              "allow_skip_frames", "1", 0);             // openh264 requirement for RC_BITRATE_MODE
  //   av_opt_set(codecContext->priv_data,
  //              "slice_mode", "fixed",  0);               // D-Discretion: single-slice
  //   av_opt_set(codecContext->priv_data,
  //              "loopfilter_disable", "1", 0);            // D-Discretion default
  //   codecContext->thread_count = 1;                      // D-Discretion: single-threaded
  if (codecContext->codec_id == AV_CODEC_ID_H264) {
    int ret = av_dict_set(opts, "preset", "veryfast", 0);
    if (ret != 0) return false;
  }
  return true;
}
```

[CITED: cisco/openh264 wiki — `RC_BITRATE_MODE` requires frame-skip enable; `RC_QUALITY_MODE` does not]

### Forcing IDR for Interval-Boundary Keyframes

```cpp
// Phase 20 encoder thread, called once per frame BEFORE avcodec_send_frame.
// Uses openh264-specific AV_OPT path because libavcodec's pict_type=AV_PICTURE_TYPE_I
// is request-only, not guaranteed-IDR.

void Openh264Encoder::maybeForceIdr() {
  const uint64_t observed = njClient_->m_audio_interval_seq.load(std::memory_order_relaxed);
  if (observed != lastObservedIntervalSeq_) {
    // Audio thread incremented the counter (interval boundary). Force IDR on this frame.
    if (frame_) {
      frame_->pict_type = AV_PICTURE_TYPE_I;
      frame_->key_frame = 1;
    }
    lastObservedIntervalSeq_ = observed;
  }
}
```

For the openh264 direct-API path (if we ever bypass libavcodec), the same effect is achieved by `ISVCEncoder::ForceIntraFrame(true)` immediately before the encode call. [CITED: cisco/openh264 wiki ISVCEncoder section]

### JamWide Phase 14.3-02 Drain Block (existing — reference only, not modified)

```cpp
// Source: /Users/cell/dev/JamWide/src/core/njclient.cpp:2810-2889
// Phase 20 does NOT modify this — it consumes it.
// Encoder thread's RawDataSendWrite items land here for Net_Connection::Send dispatch.

if (!m_netcon) {
  // Pattern C discard guard
  m_rawdata_sendq.drain([this](jamwide::RawDataItem&& item) {
    delete item.payload;
    m_rawdata_sendq_discards.fetch_add(1, std::memory_order_relaxed);
  });
} else {
  m_rawdata_sendq.drain([this](jamwide::RawDataItem&& item) {
    if (item.type == 0) {                             // BEGIN
      mpb_client_upload_interval_begin cuib;
      memcpy(cuib.guid, item.guid, sizeof(cuib.guid));
      cuib.fourcc = item.fourcc;
      cuib.chidx = item.chidx;
      cuib.estsize = item.estsize;
      m_netcon->Send(cuib.build());
    } else {                                          // WRITE (data/end)
      // 4 KB chunking — splits payload at MAX_ENC_BLOCKSIZE, emits one
      // upload_interval_write per chunk. flags=1 (end) only on FINAL chunk
      // if item.flags & 1.
      // [code body at njclient.cpp:2870-2884]
      delete item.payload;
    }
  });
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| JamTaba's `JTBv` proprietary fourCC | NinjamZap's standard `H264` fourCC + 24-byte marker + per-frame BE length prefix | 2026-05-15 user redirect | JamWide↔NinjamZap mobile interop GAINED; JamTaba interop SACRIFICED. Future MJPG/VP8 codec extension is wire-format-clean. |
| Audio thread holds `WDL_Mutex` (NinjamZap pattern) on video state | Phase 15.1-compliant atomic-pointer / atomic-bool primitives (Path A primary), with Path B carve-out as documented escape hatch | 2026-04-25 Phase 15.1 → 2026-05-16 CONTEXT D-09/D-10 | Audio path is lock-free under Path A. If Path A fails sync gates, Path B costs ~30 LOC and one realtime-audio-reviewer allow-list entry — bounded. |
| Heap-allocated chunk handoff between encoder and audio thread (early discussion option) | Direct encoder-thread `RawDataSendWrite` via Phase 14.3-02 substrate | 2026-05-16 mid-session correction (DISCUSSION-LOG.md:106-107) | Eliminates one cross-thread copy + SPSC ring per frame; encoder thread becomes a peer producer alongside audio thread on the substrate's any-thread-producer ring. |
| `cisco openh264` v2.1.1 last Mac prebuilt | TBD: VideoToolbox vs source-build per platform/arch | Open Q13 (Phase 23 territory) | macOS arm64 has no Cisco prebuilt above v2.1.1; options are (a) build from source + accept MPEG-LA royalty obligation, (b) fall back to VideoToolbox on arm64-mac. Architected for via `VideoEncoder` abstract interface (D-01); decision deferred to Phase 23 / post-v1.4. |

**Deprecated/outdated:**
- **JamTaba's per-pixel BGRA→YUV macro** (`FFMpegMuxer.cpp:541-577`): replaced by `libswscale sws_scale`. Phase 20 uses sws_scale per CONTEXT D-02.
- **VDO.Ninja browser companion**: still operational in parallel for v1.3 beta per Phase 19 D-27 / STATE Cell 9 disposition. Phase 20 does NOT touch any `juce/video/Video*.{h,cpp}` or `companion/` files; Phase 24+ post-beta teardown territory.
- **JamTaba's `QThreadPool(1)` encoder worker** pattern: replaced by `juce::Thread` per CONTEXT D-02 / D-Discretion.
- **Audio-thread `writeLog` / `writeUserChanLog` / `JAMWIDE_DEV_BUILD fopen`**: deleted by Phase 15.1-03. Phase 20 video logging goes via `juce::Logger::writeToLog` on the message thread (Phase 19 D-23 pattern).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The audio thread can dereference Phase 15.1-06 `LocalChannelMirror[0]` and read a per-channel `curwritefile_guid` field on the audio thread for the marker — **after Phase 20 adds that field**. The field does not exist today on the mirror; Phase 15.1-06 deviation #1 added other audio-thread-owned mirror fields, so the extension precedent exists. | Pattern 1 in Architecture Patterns | If `LocalChannelMirror` cannot accept another audio-thread-owned field cleanly (e.g., layout collision with existing trivial-copyability constraints), Plan 20-02 needs a Wave-0 mini-extension to `njclient.h` that adds the field and run-thread populates it. Risk is small — the field is 16 bytes of POD char array, trivially copyable. [ASSUMED] |
| A2 | The 24-byte marker's bytes 4-7 should carry `m_sync_interval_cnt` as BE u32. **NinjamZap source at `njclient.cpp:3057-3060` confirms this** — but the marker docstring at `RESEARCH-ADDENDUM.md:84-90` says "sender's `m_sync_interval_cnt`" which the audio thread already has access to. JamWide's `m_sync_interval_cnt` is at `njclient.h:267` (file-search line `m_sync_interval_cnt` confirms presence). Audio-thread-safe read because audio thread is its sole writer. | Wire-format / Pattern 1 | None — this is verified by direct source comparison. [VERIFIED: NinjamZap source at `njclient.cpp:3057-3060`; JamWide `m_sync_interval_cnt` member at `njclient.h:267`] |
| A3 | The 4 KB chunk splitting at `MAX_ENC_BLOCKSIZE` (= 4096) already happens automatically on the substrate's drain side. Phase 20 only needs to add the 4-byte BE length prefix at the front of the per-frame `RawDataSendWrite` payload. The receiver uses the length prefix to identify logical frame boundaries even when the substrate's chunks split a frame. | COD-02 requirement | None — verified by reading the drain block at `src/core/njclient.cpp:2870-2884`. [VERIFIED] |
| A4 | The 3 critical NinjamZap test scenarios for the Path B trigger (D-10) are: `02_video_one_interval_early.cpp` (audio-video sync correctness), `03_late_join.cpp` (late-join receiver state establishment), `22_audio_then_video.cpp` (mid-session video enable). **Filenames confirmed by direct listing of `ninjamzap-core/tests/video-sync/scenarios/`.** The 4th scenario CONTEXT D-10 mentions ("`06_audio_video_resync.cpp`") does NOT exist by that name in the upstream tree; the closest match is `20_drop_resync_recovery.cpp`. The planner should reconcile this filename discrepancy in Plan 20-03's acceptance criteria. | D-10 Path B trigger | If the planner doesn't reconcile, the Path B trigger criteria are ambiguous and may slip past UAT. Mitigation: name the actual filenames in PLAN 20-03 acceptance criteria. [ASSUMED — filenames confirmed by listing, but mapping to CONTEXT D-10's prose names is interpretive] |
| A5 | The audio-thread budget under populated server × HD broadcast can be measured by an Instruments-driven micro-benchmark: wrap `on_new_interval`'s video block in `mach_absolute_time()` samples, broadcast at High preset to `video.ninjamzap.com:2049` with 2-3 peers active, count the worst-case duration over a 5-minute session. Target: ≤ ~50 μs per call (matches Phase 15.1's measured baseline for `on_new_interval`'s audio block). If exceeded by ≥3×, Path B has measurable cost we need to weigh against Path A risk. | D-10 / Plan 20-03 acceptance | If we cannot get a meaningful budget measurement, the Path B trigger's "audio-thread budget" criterion is unfalsifiable. Mitigation: planner picks an alternative observable criterion (e.g., `m_rawdata_sendq_overflows == 0` + human-listening test). [ASSUMED — depends on Phase 15.1 measurement infrastructure being still available] |
| A6 | macOS `JAMWIDE_BUILD_TESTS=ON` + the existing `tests/test_rawdata_send.cpp` pattern can be extended to drive synthetic frame data into a heap-allocated `NJClient` and assert wire bytes WITHOUT a real `Net_Connection` mock. The existing `DrainRawDataSendQueueForTest` wrapper and `ChunkRawDataItem` static helper are gated behind `JAMWIDE_BUILD_TESTS` already; Plan 20-01's encoder unit tests can follow the same shape. | Test strategy / Validation Architecture | Risk is low — `test_rawdata_send.cpp` already demonstrates the pattern works for the substrate; Phase 20's encoder tests just extend it. [ASSUMED — but verified by reading Phase 14.3-02 summary at `14.3-02-SUMMARY.md`] |
| A7 | The encoder's BGRA input format (Phase 19 D-04) matches JUCE's `juce::Image::ARGB` byte order on the actual capture path. On macOS, `juce::CameraDevice`'s AVFoundation backend delivers BGRA per AVFoundation convention; JUCE's `juce::Image` is internally stored as ARGB on macOS but the underlying pixel data is BGRA. **The encoder needs to call `juce::Image::BitmapData` with `lockMode=readOnly` and read the raw byte pointer — the bit-order depends on the platform and the format string.** The mapping needs verification in Plan 20-01. | BGRA→YUV420P conversion | If the byte order is wrong, sws_scale produces color-swapped output. Mitigation: Plan 20-01 includes a known-pattern test (a synthetic red frame in → expected H.264 NAL bytes out → decoded → red frame out). Compare against the spike's existing `tests/video_spike.cpp` decoded PNG output (which is verified working). [ASSUMED] |

**If this table is empty:** All claims in this research were verified or cited — no user confirmation needed. *(Not empty — A1, A4, A5, A7 carry interpretive risk that the planner should resolve via plan-checker or codex review.)*

## Open Questions

1. **Path B trigger scenario filename reconciliation.**
   - What we know: NinjamZap's `tests/video-sync/scenarios/` contains 26 scenarios; D-10 names "equivalents of `02_video_one_interval_early.cpp`, `04_late_join_midstream.cpp`, `06_audio_video_resync.cpp`".
   - What's unclear: Only `02_video_one_interval_early.cpp` exists by that exact name. `03_late_join.cpp` is the late-join scenario; `06_user_leave.cpp` is user-leave; no file named `04_late_join_midstream.cpp` or `06_audio_video_resync.cpp` exists. The drop-resync scenario is at `20_drop_resync_recovery.cpp`.
   - Recommendation: Plan 20-03 acceptance criteria should explicitly name the actual files we test against. Suggest: `02_video_one_interval_early.cpp` (sync correctness), `03_late_join.cpp` (late-join), `13_sps_pps_mid_stream.cpp` (reconfigure), `20_drop_resync_recovery.cpp` (hold cap), `22_audio_then_video.cpp` (warm-start). Five scenarios, not three; planner picks the irreducible minimum.

2. **Audio-thread budget measurement under populated server × HD broadcast.**
   - What we know: Phase 15.1 baseline for `on_new_interval`'s audio block is documented somewhere (referenced in Phase 15.1-08 prealloc work); the video block adds: 1 atomic load, 1 stack alloc of 24 bytes, 1 BE encode, 1 memcpy of 16 bytes, 2 calls to `RawDataSendWrite` (which `new`s a `RawDataItem` and does an SPSC try_push). Total estimated wall-time: ~5-15 μs under no contention.
   - What's unclear: Under TCP back-pressure on the run thread's drain (substrate's `m_rawdata_sendq` fills up because the run thread can't drain fast enough), `RawDataSendWrite` will start failing into the overflow counter. The audio thread doesn't block on overflow — it just bumps the counter and continues. So the audio-thread budget itself stays bounded, BUT we lose frames silently.
   - Recommendation: Plan 20-03 UAT acceptance criteria record both (a) `GetRawDataSendQueueOverflowCount() == 0` at session close, AND (b) wall-clock measurement of the video block via a `JAMWIDE_DEV_BUILD`-gated `mach_absolute_time()` wrap. Target: ≤ 100 μs worst-case at High preset.

3. **Path B carve-out exact line list for the `realtime-audio-reviewer` audit allow-list.**
   - What we know: Path B replaces 2 `std::atomic` operations with 2 `WDL_Mutex::Enter/Leave` pairs inside `on_new_interval`. Lines are deterministic once the video block lands.
   - What's unclear: The `realtime-audio-reviewer` allow-list format and existing entries. Need to read `.claude/agents/realtime-audio-reviewer.md` to know what shape the entry should take.
   - Recommendation: Plan 20-04 (contingency Path B plan) opens with a "Wave 0" task that reads the agent prompt and crafts the allow-list entry. Plan 20-03 records the entry's existence as a Path B switch gate.

4. **openh264 configuration recipe — exact param values for Baseline 3.1 + RC_BITRATE_MODE.**
   - What we know: Profile = Baseline, Level = 31 (= 3.1), RC mode = bitrate. JamTaba sets `preset=veryfast`, `gop_size=30`, `pix_fmt=AV_PIX_FMT_YUV420P`, `time_base={1, fps}`, `bit_rate=N`. Spike confirmed these work at 320×240@10fps@96kbps.
   - What's unclear: openh264 via libavcodec requires `allow_skip_frames=1` when `rc_mode=bitrate` (per the cisco/openh264 issue threads). Defaults for `iLoopFilterDisableIdc`, `iMultipleThreadIdc`, `bSimulcastAVC`, `bUseLoadBalancing`, `iSpatialLayerNum` need to be set explicitly via `av_opt_set` to known-safe values (otherwise libavcodec uses its own defaults which may differ from openh264's recommended ones).
   - Recommendation: Plan 20-01 Wave 0 reads `libavcodec/libopenh264enc.c` (vendored in our ffmpeg tree at `libs/ffmpeg/macos-x86_64/include/...`) to enumerate the available `AV_OPT` keys, then sets a documented baseline param block. JamTaba's actual values are the proven start.

5. **`JamWideFrameDistributor::Subscriber` registration timing — encoder thread vs message thread.**
   - What we know: Phase 19 `Subscription` returns a moveable RAII handle. Caller must keep it alive. `~Subscription` blocks until any in-flight `onFrame` returns.
   - What's unclear: Should `Openh264Encoder` register the subscription on its constructor (= JamWideJuceProcessor construct time = message thread) or lazily on first broadcast-on? CONTEXT D-13 says "encoder thread starts at broadcast-on". The `Subscriber` itself can be alive without consuming frames (the input SPSC ring just fills and drops); the encoder thread is what runs the consumer end. Constructor registration is simpler and the cost is just one entry in the distributor's map.
   - Recommendation: Plan 20-01 registers the subscription at `Openh264Encoder` constructor time (= camera-on time per Phase 19 D-09). The encoder thread is started/stopped at broadcast-on/-off. The `onFrame` callback always pushes to the input SPSC; the encoder thread drains only when broadcasting (otherwise the SPSC fills up and drops, which costs the camera-callback thread one try_push call's worth of work per frame — negligible).

6. **Frame buffer ownership and free path verification.**
   - What we know: Phase 14.3-02 substrate's drain block (`njclient.cpp:2839-2887`) `delete item.payload` for type==1 items. The `item.payload` is a `WDL_HeapBuf*` allocated inside `RawDataSendWrite` via `m_data.Resize` (substrate code) + `memcpy` from the caller's buffer. The caller's buffer is the encoder's local `std::vector<uint8_t> chunk` which goes out of scope right after the call.
   - What's unclear: Does `RawDataSendWrite` actually `new WDL_HeapBuf` and `memcpy` the caller's bytes, or does it take ownership of the caller's pointer? Need to verify by reading the substrate impl.
   - Recommendation: Plan 20-01 Wave 0 verifies by reading `src/core/njclient.cpp` for the `RawDataSendWrite` impl (it's around line 2900 based on grep hits). Document the ownership contract in PLAN.md's "What the encoder does NOT need to free" section.

7. **BPM/BPI mid-session change handling for the encoder.**
   - What we know: Audio thread bumps `m_audio_interval_seq` at every actual interval boundary. BPM/BPI changes only affect the *cadence* of `on_new_interval` calls, not the encoder's per-frame work. So the encoder needs no special handling — `m_audio_interval_seq` changes when intervals change, encoder forces IDR, done.
   - What's unclear: Does the encoder need to re-set its `gop_size` hint when BPM/BPI changes? The answer is NO if `eForceIntraFrame` overrides the GOP scheduler (it does), but the encoder's internal rate control may have trouble if `gop_size` is "wrong" by a factor of 10x.
   - Recommendation: Phase 20 sets `gop_size = max(int)` or similar large value so openh264's internal GOP scheduling is effectively disabled, and the encoder relies entirely on `eForceIntraFrame` flag. JamTaba uses `gop_size=30`; the spike used 30; this works fine because we force IDR every interval anyway. Plan 20-01 picks `gop_size = 300` (10× safety margin); if rate control behaves poorly, revisit.

8. **Encoder lifecycle around plugin reload / DAW session save-restore.**
   - What we know: CONTEXT D-Discretion defers this. Default: encoder dies with plugin destruction; restarts fresh on next broadcast-on. ValueTree `<camera>` subtree (Phase 19 D-25) persists `qualityPreset` but does NOT persist `broadcastOn` — every plugin load starts with broadcast off.
   - What's unclear: If the user has saved a DAW session with JamWide broadcasting, and reopens the session, should broadcast auto-resume? Phase 19 D-10 says "always starts OFF on plugin launch". Phase 20 should mirror.
   - Recommendation: Default = broadcast always starts OFF. The user has to click Broadcast every time. Mirrors Phase 19's camera-always-starts-off precedent. No state to persist for Phase 20's broadcast surface beyond what Phase 19 already persists for camera.

9. **Privacy modal for broadcast (Phase 19 D-22 introduced one for camera-on; Phase 20 adds broadcast).**
   - What we know: Phase 19 D-22 created the first-use modal "JamWide broadcasts your camera..." that triggers when user clicks Camera button AFTER granting OS permission. The copy already mentions broadcast.
   - What's unclear: Does Phase 20 need a second modal at first broadcast-on? Phase 19 D-22's copy already covers it. Adding a second modal feels redundant.
   - Recommendation: Plan 20-03 reuses Phase 19 D-22's modal. The `<camera>` ValueTree `privacyAck` field already gates it. No new modal in Phase 20. If beta testers request a "confirm before broadcasting to N peers" affordance, revisit as a quick task.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| libavcodec (ffmpeg LGPL) | All encoder code | ✓ (Phase 14.3-01 vendored) | 61.19.101 (ffmpeg 7.1.2 era) | — |
| libavformat | None at runtime; transitive linkage only | ✓ | 61.7.100 | — |
| libavutil | All encoder code (AVFrame, etc.) | ✓ | 59.39.100 | — |
| libswscale | BGRA→YUV420P conversion | ✓ | 8.3.100 | — |
| libopenh264 (Cisco prebuilt) | H.264 encoder backend | ✓ | 2.1.1 (osx64) | — |
| JUCE 7 `juce::Thread` | Encoder thread | ✓ (in tree) | — | `std::thread` (C++17) |
| JUCE 7 `juce::Image` / `BitmapData` | Reading BGRA pixel data from frame distributor | ✓ (in tree, Phase 19 verified) | — | — |
| Phase 14.3-02 substrate (`m_rawdata_sendq` API) | All wire transport | ✓ (shipped) | — | — |
| Phase 19 `JamWideFrameDistributor` | Frame source | ✓ (shipped) | — | — |
| Phase 15.1-05 deferred-delete SPSC | `SpsPpsBuffer*` lifetime | ✓ (shipped) | — | — |
| Phase 15.1-06 `m_locchan_mirror` | Audio-thread-safe ch0 GUID for the marker | ✓ (shipped, but needs new `curwritefile_guid` field per Pitfall 1) | — | Path B (`WDL_Mutex m_video_cs` + direct `lc->m_curwritefile.guid`) |
| `cmake/ffmpeg.cmake` IMPORTED INTERFACE | Linking | ✓ (Phase 14.3-01 ships) | — | — |
| `JAMWIDE_BUILD_TESTS` test harness | Plan 20-01 + 20-02 unit tests | ✓ (Phase 14.3-02 ships `test_rawdata_send.cpp` pattern) | — | — |
| `video.ninjamzap.com:2049` (public ninjamzap-server) | Plan 20-03 UAT | ✓ (user-confirmed running, public, free) | — | Local Docker `ninjamzap-server` per `docs/SERVER.md` plan (Phase 24 territory but available now) |
| ninjamzap-core test scenarios (`tests/video-sync/scenarios/*.cpp`) | Plan 20-03 acceptance criteria (test names) | ✓ (at `/Users/cell/dev/ninjamzap-core/tests/video-sync/scenarios/`) | — | — |
| TSan (`./scripts/build.sh --tsan`) | Phase 15.1 dual-scope verification | ✓ (shipped Phase 15.1-04) | — | — |
| Instruments / Time Profiler | Audio-thread budget measurement (Plan 20-03) | ✓ (macOS dev machine has it) | — | — |

**Missing dependencies with no fallback:** None — Phase 20 is purely an integration phase consuming shipped substrates.

**Missing dependencies with fallback:**
- macOS arm64 / Apple Silicon: testing the universal build requires arm64 hardware which the current dev machine lacks (STATE.md Cell 4 blocked). Fallback: Plan 20 targets x86_64 only; arm64 validation lives in Phase 23.
- Windows x86_64: same situation — Plan 20 targets macOS, Plan 23 cross-platform packages.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | CTest with custom test executables; existing Catch2 not used in unit tests for `njclient` (POD test wrappers via `JAMWIDE_BUILD_TESTS`) |
| Config file | `CMakeLists.txt` `if(JAMWIDE_BUILD_TESTS)` block (~line 137) |
| Quick run command | `./scripts/build.sh --tests && cd build-test && ctest --output-on-failure -R "video_encoder\|video_send_state\|rawdata_send"` |
| Full suite command | `./scripts/build.sh --tests && cd build-test && ctest --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| COD-01 | openh264 encoder at ~98 kbps at 320×240/10fps | unit | `ctest -R video_encoder_bitrate` | ❌ Wave 0 of Plan 20-01 |
| COD-01 | Preset bitrate ladder (100/300/800 kbps) | unit | `ctest -R video_encoder_preset_bitrate` | ❌ Wave 0 of Plan 20-01 |
| COD-01 | Encoder reconfigure → fresh SPS/PPS published | unit | `ctest -R video_encoder_reconfigure_publishes_spspps` | ❌ Wave 0 of Plan 20-01 |
| COD-01 | Drop-oldest backpressure on input SPSC, counter bumps | unit | `ctest -R video_encoder_input_drop_counter` | ❌ Wave 0 of Plan 20-01 |
| COD-02 | `[4B BE length][NAL]` framing format | unit | `ctest -R video_encoder_length_prefix_framing` | ❌ Wave 0 of Plan 20-01 |
| COD-02 | 4 KB chunk splitting via substrate (verify via existing `rawdata_send_roundtrip` extension) | unit | `ctest -R rawdata_send_roundtrip` | ✓ (Phase 14.3-02 exists; extends to verify with H264 fourCC) |
| WIRE-01 | 24-byte marker layout (BE u32 prefix=20, BE u32 swap_count, 16B audio GUID) | unit | `ctest -R video_send_state_marker_bytes` | ❌ Wave 0 of Plan 20-02 |
| WIRE-01 | Channel registration emits correct fourCC + flags=0x10 | unit | `ctest -R video_send_state_channel_register` | ❌ Wave 0 of Plan 20-02 |
| WIRE-01 | Cold-start: marker-only first interval if SPS/PPS null | unit | `ctest -R video_send_state_cold_start` | ❌ Wave 0 of Plan 20-02 |
| WIRE-01 | END at deactivate (broadcast-off mid-interval) | unit | `ctest -R video_send_state_end_at_deactivate` | ❌ Wave 0 of Plan 20-02 |
| WIRE-01 | IDR sync via `m_audio_interval_seq` counter | unit | `ctest -R video_encoder_idr_sync` | ❌ Wave 0 of Plan 20-01 |
| WIRE-03 | `m_rawdata_sendq_overflows == 0` after 5-minute populated-server broadcast | integration UAT | manual (no automated) | ❌ Plan 20-03 manual UAT |
| WIRE-03 | TSan-clean concurrent audio + video producer on `Net_Connection::Send` | integration | `./scripts/build.sh --tsan && build-tsan/JamWide_Standalone` + manual broadcast | ✓ (Phase 15.1-04 ships `--tsan`) |
| (Path B trigger) | NinjamZap sync scenarios `02/03/13/20/22` against Path A | integration UAT | manual (UAT against `video.ninjamzap.com:2049`) | ❌ Plan 20-03 |
| (Acceptance: 5-min populated UAT) | 2 users connected to `video.ninjamzap.com:2049`, broadcasting at each preset, 5 minutes, no audio glitches | manual UAT | UAT checklist in Plan 20-03 | ❌ Plan 20-03 |

### Sampling Rate

- **Per task commit:** `./scripts/build.sh --tests && cd build-test && ctest -R "(video|rawdata)" --output-on-failure` — ≤ 30 sec
- **Per wave merge:** `./scripts/build.sh --tests && cd build-test && ctest --output-on-failure` — ≤ 90 sec
- **Phase gate:** Full ctest green + `./scripts/build.sh --tsan` clean + manual UAT at all 3 presets against `video.ninjamzap.com:2049`

### Wave 0 Gaps

- [ ] `tests/test_video_encoder.cpp` — unit tests for `Openh264Encoder` (covers COD-01 bullets)
- [ ] `tests/test_video_send_state.cpp` — unit tests for `NJClient` video state machine (covers WIRE-01 bullets)
- [ ] Extension to `tests/test_rawdata_send.cpp` — add H264 fourCC + chunk-split-with-length-prefix sub-tests (covers COD-02 + WIRE-01 substrate path)
- [ ] CMake wiring in `CMakeLists.txt` `if(JAMWIDE_BUILD_TESTS)` block — add `add_executable(test_video_encoder ...) + add_test(...)` and same for `test_video_send_state`
- [ ] (No new fixtures needed — heap-allocate `NJClient` per `test_rawdata_send.cpp` precedent)
- [ ] (No framework install — CTest + raw C++ harness already in tree)

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | NINJAM session auth handled at Connect time; video channel reuses authenticated session — no new auth surface |
| V3 Session Management | no | Video shares the NINJAM TCP session — same lifetime, no separate session |
| V4 Access Control | no | NINJAM server controls who's in the room (existing); video respects the same `SetLocalChannelInfo` flag plumbing |
| V5 Input Validation | yes | Encoded H.264 bytes flow OUTBOUND only in Phase 20; no untrusted input parsed. Phase 21 (receive) is where input validation matters. **But:** the marker construction MUST validate `m_sync_interval_cnt` and `lc->m_curwritefile.guid` are well-formed before serialising — null GUID is handled (zero-fill per RESEARCH-ADDENDUM:93-95) but a corrupted GUID is silently passed through. Mitigation: GUID is owned by audio thread, populated by `WDL_RNG_bytes` which always produces 16 random bytes — no validation needed beyond "is `LocalChannelMirror[0]` active". |
| V6 Cryptography | no | NINJAM transport is plaintext TCP today (no TLS in v1.3). Video payload is H.264 bytes — opaque to NINJAM. **No new crypto.** Phase 15 separately tracked AES-256-CBC for raw payload at v1.4+. |
| V7 Error Handling | yes | Encoder fatal errors (openh264 init fail, encode fail) MUST be surfaced — not crash. Plan 20-01 logs via `juce::Logger::writeToLog` per Phase 19 D-23. Camera permission denial (Phase 19) is already handled; encoder errors join the same status surface. |
| V8 Data Protection | yes | Video bytes are user-personal-image data. Risk: privacy modal acknowledgement (Phase 19 D-22 covers this) + encoded bytes on NINJAM wire to peers. **No new persistent storage** of video (broadcast is ephemeral). |
| V12 Files | no | No file I/O introduced. Encoder produces bytes → SPSC → wire. No write to disk. |
| V13 API | yes | New public `NJClient::SetVideoChannel/StopVideoChannel/QueueVideoFrame/SetVideoSPSPPS` API. NinjamZap-mirror; symbol names verbatim. Must be callable from non-audio threads safely (matches the existing `RawDataSendBegin/Write` "any thread" contract). |

### Known Threat Patterns for {stack}

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Encoder thread heap-allocation race with audio thread (`mach_vm_allocate` contention) | DoS / Performance | Pre-allocate frame buffer pool on encoder thread (per Pitfall 3); minimal heap growth in steady state |
| `SpsPpsBuffer*` UAF during reconfigure | Tampering / SIGSEGV | Phase 15.1-05 deferred-delete pattern (per Pitfall 2) |
| `m_video_active` toggle race causing "active but no SPS/PPS" black-screen receiver | Tampering / Availability | Release/acquire ordering pairs SPS/PPS publication with active toggle (CONTEXT D-11) |
| `Net_Connection::Send` corruption from concurrent producers | Tampering | Phase 14.3-02 substrate (sole run-thread Send caller — verified by `test_rawdata_send`) |
| Encoder crash (libavcodec / openh264 internal) | Availability | Catch via libavcodec error returns; reconfigure encoder (tear-down + rebuild per D-04); surface via `juce::Logger::writeToLog`. Production fallback: encoder reports "fatal error" state, UI shows "Video unavailable — restart broadcast"; audio unaffected. |
| Memory blowup from queued frames (encoder behind on a slow CPU) | DoS | Drop-oldest input SPSC (D-07) + `m_encoder_input_drops` counter |
| Excessive bandwidth from buggy bitrate config | DoS (network) | `RC_BITRATE_MODE` average-rate target + ninjamzap-server's per-subscriber congestion drop (`VideoCongestionThreshold` = 50%) — already in upstream server |
| User mistakenly broadcasts video (privacy concern) | Information Disclosure | Phase 19 D-22 first-use modal already covers; Phase 20 reuses |

## Sources

### Primary (HIGH confidence)
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2047-2123` — send API impl (RawDataSendBegin/Write, SetVideoChannel, QueueVideoFrame, SetVideoSPSPPS, cold-start path)
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3041-3082` — audio-thread video block in on_new_interval (END/BEGIN/marker/SPS-PPS pattern; verbatim port target)
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:205-236` — public API surface to mirror (RawDataCallback typedef, SetVideoChannel/StopVideoChannel/QueueVideoFrame/SetVideoSPSPPS decls)
- `/Users/cell/dev/ninjamzap-core/tests/video-sync/scenarios/02_video_one_interval_early.cpp` — bug-repro for the D-10 trigger criterion (verbatim, with exact filename and bug description)
- `/Users/cell/dev/ninjamzap-core/tests/video-sync/scenarios/` directory listing — 26 scenarios, filenames confirmed
- `/Users/cell/dev/JamWide/src/core/njclient.cpp:2810-2889` — Phase 14.3-02 substrate's run-thread drain block (Phase 20 consumes; verbatim verified)
- `/Users/cell/dev/JamWide/src/core/njclient.h:642-754` — Phase 14.3-02 substrate's public API + overflow accessor + SPSC capacity
- `/Users/cell/dev/JamWide/src/core/njclient.cpp:212-254` — `MAKE_NJ_FOURCC` macro definition + `is_video_fourcc` helper (verbatim verified)
- `/Users/cell/dev/JamWide/src/core/njclient.cpp:4794-4912` — current `on_new_interval` audio block (Phase 20 inserts video block at the end of this method)
- `/Users/cell/dev/JamWide/juce/video/native/JamWideFrameDistributor.h:33-107` — Phase 19's frame source API (RAII Subscription contract; "any thread" onFrame)
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-RESEARCH-ADDENDUM.md` — NinjamZap wire format spec (locked); 4-stage receive pipeline (Phase 21 territory); GUID-pairing decision tree
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` — Item D (encoder port reference), Item F.1 (send-path state machine), Item F.4 (Net_Connection::Send thread-safety)
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-spike-results.md` — measured 320×240@10fps@~98 kbps, Q3 thread-safety, Q7 fourCC byte-order
- `.planning/phases/14.3-native-video-foundation/14.3-02-SUMMARY.md` — as-shipped substrate facts (SPSC capacity 64, run-thread drain at `:2697-2776`, Pattern C discard guard, test-only wrappers under JAMWIDE_BUILD_TESTS)
- `.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md` — Phase 19 D-02/D-04/D-09/D-10/D-18/D-22/D-25/D-29 (frame distributor pattern, encoder owns BGRA→YUV420P, broadcast-always-off-on-launch, capture presets, privacy modal, ValueTree subtree, audio-thread integration deferred to Phase 20)
- `.planning/phases/15.1-rt-safety-hardening/15.1-CONTEXT.md` — D-01 (audio thread is sacred), D-02 (deferred-delete pattern), D-07 (TSan dual-scope verification)
- `.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md` — Phase 20 locked decisions D-01 through D-18
- `.planning/phases/20-h-264-encoder-send-pipeline/20-DISCUSSION-LOG.md` — alternatives considered, mid-session HYBRID correction at 15:00
- `.planning/REQUIREMENTS.md` — v1.3 Native Video requirements (COD-01, COD-02, WIRE-01, WIRE-03, etc.)
- `.planning/STATE.md` — milestone scope, Phase 19/14.3 substrate landings, carried blockers (Q13 VideoToolbox, capacity sizing)

### Secondary (MEDIUM confidence)
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:237-277` — encoder configure block (used as bitrate/GOP/profile/preset reference, but JamTaba's Q_OBJECT/QThreadPool wrapping is REPLACED)
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:541-577` — RGB→YUV macro (REPLACED by libswscale per CONTEXT D-02)
- `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:99` — QThreadPool(1) worker (REPLACED by juce::Thread per CONTEXT D-Discretion)
- Cisco openh264 wiki — `ISVCEncoder` API (ForceIntraFrame method): https://github.com/cisco/openh264/wiki/ISVCEncoder
- Cisco openh264 wiki — `TypesAndStructures` (SEncParamExt, RC_BITRATE_MODE, PRO_BASELINE): https://github.com/cisco/openh264/wiki/TypesAndStructures
- ffmpeg libopenh264enc source — AV_OPT key listing: https://ffmpeg.org/doxygen/5.1/libopenh264enc_8c_source.html (matches our vendored 7.1.2 era)
- openh264 issue #2451 "Issue with bitrate control" — confirms `RC_BITRATE_MODE` requires `allow_skip_frames=1`: https://github.com/cisco/openh264/issues/2451

### Tertiary (LOW confidence)
- *(None — all critical claims for Phase 20 are sourced from authoritative or directly-verified material above.)*

## Metadata

**Confidence breakdown:**
- **Wire format spec** (24B marker, SPS/PPS chunk #2, 4-byte BE length prefix, fourCC H264): **HIGH** — read source verbatim at multiple sites; cross-verified with `RESEARCH-ADDENDUM.md`'s explicit wire format section; matches `02_video_one_interval_early.cpp` bug repro narrative.
- **Phase 14.3-02 substrate consumption** (RawDataSendBegin/Write, m_rawdata_sendq, run-thread drain, Pattern C): **HIGH** — read shipped code at the exact line numbers documented; `test_rawdata_send.cpp` proves the contract.
- **Phase 19 frame distributor contract** (Subscription RAII, onFrame "any thread"): **HIGH** — read shipped code in `JamWideFrameDistributor.h`.
- **Encoder configuration recipe** (Baseline 3.1 + RC_BITRATE_MODE + GOP heuristic): **MEDIUM** — JamTaba pattern + Cisco docs + spike validation cover the steady state; exact `av_opt_set` key strings need Plan 20-01 Wave 0 verification against the vendored libopenh264enc.c source. Risk: minor (defaults likely safe; values may need tweaks for our resolutions).
- **Path A correctness for video/audio interval sync**: **MEDIUM** — Phase 15.1's atomic-mediated discipline is proven for the audio path's existing state, but never specifically validated against the NinjamZap GUID-pairing scenarios at this granularity. The Path B carve-out (D-09/D-10) is the explicit hedge against this uncertainty. Plan 20-03 UAT acceptance criteria carry the validation.
- **Audio-thread budget under populated server × HD broadcast**: **LOW** — measurement is missing. Assumption A5 carries this; Plan 20-03 records as acceptance criterion.
- **macOS arm64 / VideoToolbox eventual fallback**: **MEDIUM** — Phase 23 territory; Phase 20's abstract VideoEncoder interface (D-01) handles the deferral cleanly.
- **`LocalChannelMirror` extension for audio_ch0_guid**: **MEDIUM** — Phase 15.1-06 deviation #1 precedent suggests adding fields is allowed; the specific field is new in Phase 20 and needs Plan 20-02 Wave 0 verification of layout.
- **NinjamZap sync scenario filename mapping (D-10)**: **MEDIUM** — three scenarios confirmed (`02`, `03`, `22`); the prose names `04_late_join_midstream` and `06_audio_video_resync` in CONTEXT D-10 do NOT match any actual filename. Assumption A4 flags this.

**Research date:** 2026-05-16
**Valid until:** 2026-06-15 (30 days) — ninjamzap-core/-server may push changes that invalidate the wire format spec; Plan 20-02 Wave 0 should re-pull both repos and re-verify before committing.

---

*Phase 20 is a port-from-reference exercise. The wire format is locked. The substrate is shipped. The risk surface is the threading-model translation. Plan accordingly.*
