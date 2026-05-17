# Phase 21: H.264 Decoder & Receive Pipeline — Research

**Researched:** 2026-05-17
**Domain:** H.264 video decoding (libavcodec) + NinjamZap-wire-compatible 4-stage receive pipeline + GUID-pairing audio-video sync + per-peer juce::Image delivery
**Confidence:** HIGH

## Summary

Phase 21 is the symmetric inverse of Phase 20 — a port-from-reference exercise where the
4-stage pipeline data model, the WRITE-handler accumulation logic, and the
`on_new_interval` audio-thread SWAP + GUID-pair decision tree all already exist verbatim
at `ninjamzap-core/njclient.h:334-417` and `ninjamzap-core/njclient.cpp:1300-1592 / :3084-3260`.
The job is to port those byte-for-byte, register an `RawData_Callback` for fourcc=H264
into the Phase 14.3-03 dispatch substrate, attach a per-peer libavcodec decoder thread
that consumes parsed NAL chunks from a SPSC ring, and surface decoded BGRA `juce::Image`
frames into a new `JamWideRemoteFrameDistributor` that Phase 22 tiles will subscribe to.

The receive-side wire format is locked, validated byte-for-byte against the canonical
NinjamZap web viewer during Phase 20 closure (commit 8d17498), and exercised by the
upstream test harness at `tests/video-sync/harness/TestClient.cpp`. The decoder side has
no upstream reference (NinjamZap's decoder is the canonical web viewer / mobile app —
neither is in scope to copy) — Phase 21 uses libavcodec's modern `avcodec_send_packet`
/ `avcodec_receive_frame` API with Annex-B framed SPS/PPS + per-frame NAL submission
(no `extradata`/avcC path; matches D-13).

The architectural surface is well-defined by CONTEXT.md's 20 locked decisions — there
is essentially zero open architectural design space. The planner's freedom is in
ergonomic choices (file layout, SpscRing depth, NalChunk storage strategy, decoder
thread priority, logging cadence) plus the test scenario subset selection.

**Primary recommendation:** Decompose into **3 plans** matching the natural seams of the
codebase:

- **21-01 — Receive-side state machine + WRITE-handler accumulation + audio-thread
  SWAP/decision-tree port.** Adds `VideoRecvBuffer` + `VideoRecvState` struct
  definitions, `WDL_PtrList<VideoRecvState> m_video_streams` + `WDL_Mutex
  m_video_recv_cs` + `findVideoStream` / `findOrCreateVideoStream` /
  `findVideoStreamByGUID` / `removeVideoStream` helpers to NJClient. Wires the
  `RawData_Callback` for fourcc=H264 into the existing Phase 14.3-03 dispatch
  surface (BEGIN creates VideoRecvState if missing; WRITE routes by GUID into
  the appropriate slot with multi-write reassembly + 24B marker parsing on
  first frame; END moves accumulating → next). Lands the audio-thread SWAP +
  STAGE-1 promote + GUID-pair decision tree in `on_new_interval`. Audit-allowlist
  envelope entries written to `.claude/agents/realtime-audio-reviewer.md`. Pure
  state-machine code, no decoder yet — output is "playing slot ready for
  consumption." Testable in isolation against synthetic WRITE injections via
  the existing `DispatchTestServerDownloadIntervalBegin/Write` substrate.
  Ports 4 of the 6 critical scenarios (`02_video_one_interval_early`,
  `03_late_join`, `20_drop_resync_recovery`, `22_audio_then_video`).

- **21-02 — VideoDecoder interface + libavcodec H.264 decoder thread + AVCC
  parsing + SpscRing NAL handoff.** Adds the new `juce/video/decoder/`
  subdirectory mirroring Phase 20's `juce/video/encoder/`: `VideoDecoder.h`
  abstract interface, `Openh264Decoder.h/.cpp` concrete impl owning
  `juce::Thread` + `AVCodecContext` + `SwsContext` + per-peer
  `SpscRing<NalChunk, 32>`. Decoder thread is a pure libavcodec consumer (pop
  NalChunk → `avcodec_send_packet` → `avcodec_receive_frame` loop → `sws_scale`
  BGRA → swap front/back → bump generation → triggerAsyncUpdate). Run-thread
  AVCC parsing helper that produces `NalChunk{ParamSet, ...}` for SPS/PPS and
  `NalChunk{Frame, ...}` for frame NALs from the playing slot's bytes. Ports
  the remaining 2 critical scenarios (`13_sps_pps_mid_stream`, `25_no_initial_spspps`).

- **21-03 — JamWideRemoteFrameDistributor + PeerVideoSink + lazy
  lifecycle + JamWideJuceProcessor wiring + UX/status-fields surface +
  receive-side UAT.** Adds the new distributor service (symmetric inverse of
  Phase 19's `JamWideFrameDistributor`) with per-peer `PeerVideoSink`
  (double-buffered juce::Image, atomic generation, AsyncUpdater, atomic
  status fields, listener-vector for DISP-03 multi-tile support). Wires
  `VideoRecvState` to lazily spin up + tear down the decoder + sink on first
  H264 BEGIN / peer leave. Constructs the distributor on `JamWideJuceProcessor`
  symmetric to the existing send-side distributor. UAT against a 3-peer
  populated `video.ninjamzap.com:2049` session for ≥5 minutes (the 4 success
  criteria all become testable here once the full stack is plugged together).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|--------------|----------------|-----------|
| `MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN` dispatch (H264 fourcc) | Run thread (`NJClient::Run`) | — | Phase 14.3-03 substrate already lands here; Phase 21 registers the callback. |
| `MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE` accumulation (per-frame bytes into `accumulating` slot) | Run thread (`NJClient::Run`) | — | NinjamZap-verbatim — upstream does WRITE accumulation on the run thread (`njclient.cpp:1439-1592`). Allocation + memcpy + frame-offset bookkeeping all stay off the audio thread. |
| 24-byte marker parsing (first frame of accumulating) — extract `audio_ch0_guid` + `sender_seq` | Run thread (inside WRITE handler) | — | Same critical-section context as the WRITE accumulation; marker is part of the wire stream, not a separate event. NinjamZap-verbatim at `njclient.cpp:1534-1547`. |
| Mid-download `startPlaying` (move accumulating → next once first complete frame arrives) | Run thread (inside WRITE handler) | — | NinjamZap pattern at `njclient.cpp:1549-1561`. The 4-stage pipeline is not strictly END-triggered — it can promote earlier if data is flowing. |
| STAGE-1 promote (pending → playing) | Audio thread (`on_new_interval`) | — | NinjamZap-verbatim at `njclient.cpp:3091-3102`. Held under `m_video_recv_cs` for the whole video block. |
| GUID-pair decision tree (DS-match → defer to pending, PREV-match → playing, no-match → HOLD with `kHoldCapDrop=4`) | Audio thread (`on_new_interval`) | — | NinjamZap-verbatim at `njclient.cpp:3116-3219`. The protocol-level fix for the "video one interval early" bug — must be byte-faithful. |
| `m_remoteuser_mirror[s].next_ds[0]->guid` read for GUID comparison | Audio thread | — | Phase 15.1-07a HIGH-2 carve-out already accepted this access. Phase 21 reads two GUID fields per VideoRecvState per swap (current `senderDs->guid` + `vs->prev_ds_guid` cached from previous swap). |
| Push parsed NalChunks onto decoder's SpscRing | Audio thread (after SWAP places bytes in `playing`) | — | `vs->decoderInputQ.try_push(...)` is lock-free; runs immediately after the audio-thread SWAP block decides this slot plays. **Allocation-free** — NalChunk storage uses inline buffer + heap fallback as a `std::variant<InlineNal, HeapNal>` POD; or alternatively the parser allocates run-thread heap that the SPSC moves. Planner picks. |
| AVCC parsing (24B marker → SPS/PPS chunk → per-frame chunks) | **Decoder thread (D-12 revised + B-1; codex Cluster 2)** | — | Per CONTEXT.md D-12 revised + B-1 + codex Cluster 2: the AVCC parser (`Openh264Decoder::parseSlotAndFeed_`) lives on the DECODER thread, not the audio or run thread. The audio thread only memcpys the 0–4 MB playing-slot bytes into a `VideoRecvState`-owned `VideoRecvSlotSnapshot` and pushes an integer index onto `SpscRing<int, 4>` (Option-A redesign — `std::array<VideoRecvSlotSnapshot, 4>` + index-only SPSC). The decoder thread pops the index, walks the snapshot bytes (24B marker discard → SPS/PPS inner-length-prefixed chunk → per-frame AVCC chunks), wraps each NAL with Annex-B start code (`00 00 00 01`), and feeds via `avcodec_send_packet`. **The audit-allowlist envelope (D-16) explicitly does NOT cover parser work on the audio thread** — only the bounded slot-memcpy + index push. Rationale: wire-format and codec-API responsibilities co-located on the libavcodec consumer simplify the threading audit (one thread, one mutex-free SPSC pop, then pure libavcodec calls). |
| `avcodec_send_packet` + `avcodec_receive_frame` loop | Decoder thread (per peer) | — | Pure libavcodec consumer. One thread per peer for isolation (D-09). |
| `sws_scale` YUV420P → BGRA | Decoder thread | — | Co-located with decoder for zero-copy after `receive_frame`. SwsContext owned by decoder thread; lazy-recreated on source-resolution change (D-07). |
| Double-buffered `juce::Image` swap | Decoder thread (writer) → UI thread (reader) | — | Brief `juce::CriticalSection` for the swap; `std::atomic<uint64_t> generation` for lock-free latest-frame-wins read. D-08. |
| `triggerAsyncUpdate` on PeerVideoSink (signal new frame) | Decoder thread (caller) → Message thread (handler) | — | D-02. AsyncUpdater coalesces — multiple decoder triggers between paints collapse to one `handleAsyncUpdate`. |
| `handleAsyncUpdate` fan-out to registered listeners (Phase 22 tiles) | Message thread | — | D-06. Iterates `std::vector<std::function<void()>>` under a brief `juce::CriticalSection`. Each Phase 22 tile's callback calls `tile->triggerAsyncUpdate()` (separate AsyncUpdater per tile). |
| Subscription lifetime (RAII detach on Phase 22 tile destruction) | Message thread (caller) → Message thread (detacher) | — | Phase 19 HIGH-2 verbatim. Move-only `Subscription` member; destructor calls `distributor->unregisterAndWait(id)` to block any in-flight callback before returning. |
| Lazy decoder lifecycle spin-up | Run thread (first H264 BEGIN) | — | D-10. Same context as `findOrCreateVideoStream`. New VideoRecvState ctor allocates AVCodecContext + SwsContext + spawns decoder thread + creates PeerVideoSink + registers with distributor. |
| Lazy decoder lifecycle tear-down | Run thread (peer leave / m_remoteusers Delete) | — | D-10. Symmetric — destructor joins decoder thread + frees codec/sws context + removes sink from distributor (which detaches active subscriptions). |
| Distributor key `(username, chidx)` mapping | Message thread (subscribe) + Run thread (lifecycle) | — | D-05. `unordered_map<std::string, std::unique_ptr<PeerVideoSink>>` under a `juce::CriticalSection`. Roster shifts on NJClient invalidate `useridx` but not username. |
| First-frame UX + hold-state UX (overlays + last-frame freeze) | Phase 22 (consumer of atomic status fields) | — | Out of scope for Phase 21 implementation — Phase 21 exposes the atomic status fields (`first_frame_seen`, `hold_count`, `decode_error_count`, `drop_resync_count`, `synced`) and Phase 22 paints based on their values. |

**Why this matters:** Phase 21 has FIVE distinct threads in play at once (run + audio +
N decoder threads + camera-callback already from Phase 19 send-side + message). Every
piece of state has exactly one thread of ownership in the table above, with the
hand-off mechanisms explicit. Misattributing the WRITE accumulation to the audio thread
(or the SWAP block to the run thread) would inadvertently introduce a Phase 15.1-pure
audit-CRITICAL or break the GUID-pair sync semantics.

## User Constraints (from CONTEXT.md)

### Locked Decisions

**UI Delivery (Phase 22 ingestion contract):**

- **D-01**: Atomic-snapshot `juce::Image` per peer; latest-frame-wins; lock-free read. Each peer's `PeerVideoSink` owns a stable `juce::Image` member; decoder writes pixels then bumps an atomic generation counter. UI reads the latest snapshot under a brief `juce::CriticalSection` (microseconds; only swaps the ref-counted shared pointer, no pixel copy). Reuses Phase 19 HIGH-4's atomic-generation pattern.
- **D-02**: Per-peer `juce::AsyncUpdater` for "new frame ready" UI signal. After bumping `generation`, decoder calls `sink->triggerAsyncUpdate()`. AsyncUpdater coalesces (multiple triggers between paints become one) and dispatches on the message thread.
- **D-03**: New `JamWideRemoteFrameDistributor` service; symmetric inverse of Phase 19's `JamWideFrameDistributor`. Owns the per-peer sink map and the lifecycle. `Subscription` RAII handle from `subscribeToPeer(username, chidx, onRepaint)`.
- **D-04**: BGRA pixel format via `libswscale` YUV420P→BGRA. Sink's `juce::Image` format = `juce::Image::ARGB` (BGRA on macOS/Windows). Matches Phase 19's capture-side pixel layout symmetrically.
- **D-05**: Distributor keyed by `(username, chidx)` string, NOT useridx. Sink map is `unordered_map<std::string, std::unique_ptr<PeerVideoSink>>` with key `"username:chidx"`. Stable across NJClient's user-leave roster shifts.
- **D-06**: Multiple simultaneous subscribers per peer (DISP-03 support). PeerVideoSink owns a `std::vector<std::function<void()>>` of registered onRepaint callbacks under a `juce::CriticalSection`. Grid tile + popout each call subscribe independently.
- **D-07**: Sink `juce::Image` fixed at first-seen peer resolution; sws_ adapts to keep image size stable across mid-session peer preset changes.
- **D-08**: Double-buffered front/back `juce::Image` swapped under brief `CriticalSection` for tearing protection. Decoder writes into `image_back`; on completion takes a microsecond-scoped `juce::ScopedLock` on `bufferLock`, `std::swap`s the two ref-counted `juce::Image`s, bumps generation, releases lock, fires AsyncUpdater.

**Decoder Thread Model + Per-Peer Memory:**

- **D-09**: One libavcodec decoder thread per peer (`juce::Thread`). Each `VideoRecvState` owns its own decoder thread + AVCodecContext + SwsContext + per-peer `SpscRing<NalChunk, 32>` populated by the run thread.
- **D-10**: Lazy decoder lifecycle — spin-up on first H264 BEGIN per peer; tear-down on peer leave. Zero cost for peers who never broadcast video.
- **D-11**: Per-peer per-slot soft cap of 4 MB; on exceed drop rest-of-interval. Each of the 4 `VideoRecvBuffer` slots pre-allocates a `WDL_HeapBuf` of 4 MB on creation. **No allocation in the WRITE handler** — pre-allocation at slot creation is critical for this gate to be RT-clean.
- **D-12 (revised 2026-05-17 per checker B-1)**: AVCC parsing on **DECODER thread**; the audio thread only memcpys the playing-slot bytes into a `VideoRecvState`-owned `VideoRecvSlotSnapshot` and pushes an integer index onto `SpscRing<int, 4>`. Original D-12 wording said "run thread"; the practical contract (per codex Cluster 2 Option A) is that the parser lives where the libavcodec consumer lives — the decoder thread — keeping wire-format and codec-API responsibilities co-located on a single non-realtime thread. Authoritative locked text in `21-CONTEXT.md` D-12.
- **D-13**: SPS/PPS fed as Annex-B packets via same code path as frame NALs (no avcC extradata). Run thread wraps SPS/PPS NALs with Annex-B start code (`00 00 00 01`) and pushes as a `NalChunk{kind: ParamSet}` on the decoder SPSC.

**4-Stage Pipeline Ownership + Phase 15.1-07a Mirror Interaction:**

- **D-14**: Verbatim upstream pattern — `WDL_PtrList<VideoRecvState> m_video_streams` + `WDL_Mutex m_video_recv_cs` on NJClient. Port `VideoRecvState` + `VideoRecvBuffer` struct definitions verbatim from `ninjamzap-core/njclient.h:334-417`.
- **D-15**: Audio thread owns SWAP + GUID-pair decision tree under `m_video_recv_cs`. Verbatim upstream `on_new_interval` video receive block. The 1-swap defer through `pending` is the protocol-level fix for the "video 1 interval early" bug.
- **D-16**: Phase 21 audio-thread audit-allowlist envelope (parallel to Phase 20 D-09). Plan 21-XX writes the carve-out entries to `.claude/agents/realtime-audio-reviewer.md` covering `m_video_recv_cs.Enter/Leave`, `WDL_PtrList<VideoRecvState>` iteration, scalar reads on VideoRecvBuffer, `VideoRecvBuffer::copyFrom`, `SpscRing<NalChunk, 32>::try_push`, `m_remoteuser_mirror[s].next_ds[0]->guid` access. **No `writeLog` calls on the audio-thread receive path.**

**Hold/Error UX (Phase 22 surface):**

- **D-17**: kHoldCapDrop=4 UX — last-frame freeze + subtle 'syncing…' overlay after 2 holds.
- **D-18**: Decoder-error recovery — drop frame + bump counter + continue. IDR-per-interval (Phase 20 D-14) means libavcodec naturally auto-recovers at the next interval boundary.
- **D-19**: First-frame UX — tile chrome + 'video starting…' overlay until first decoded frame.
- **D-20**: Atomic status fields on PeerVideoSink (`hold_count`, `decode_error_count`, `drop_resync_count`, `synced`, `first_frame_seen`). Phase 22 tile reads all of these lock-free per repaint.

### Claude's Discretion

- Decoder thread priority (`juce::Thread::setPriority` normal vs slightly elevated). Default: normal.
- libavcodec multi-threading (`AVCodecContext::thread_count` per peer). Default: 1 (single-threaded decode).
- `SpscRing<NalChunk, 32>` depth — planner picks if more headroom needed.
- `NalChunk` storage — small inline buffer + heap fallback vs always-heap.
- File layout — mirror Phase 20's `juce/video/encoder/` symmetrically as `juce/video/decoder/` containing `VideoDecoder.h` (interface), `Openh264Decoder.h/.cpp` (impl), and `juce/video/distributor/JamWideRemoteFrameDistributor.h/.cpp`.
- Test scaffolding helpers — gated under `JAMWIDE_BUILD_TESTS`. Planner picks coverage matrix from the 26 upstream sync scenarios (port the high-value subset: `02_video_one_interval_early`, `03_late_join`, `13_sps_pps_mid_stream`, `20_drop_resync_recovery`, `22_audio_then_video`, `25_no_initial_spspps`).
- Empty-peer reaping policy — default: stay alive until peer leaves.
- Debug logging surface — `juce::Logger::writeToLog` for decoder open/close, resolution changes, decoder errors. Default: log open/close + first decode error + every 100th decode error after that.

### Deferred Ideas (OUT OF SCOPE)

- Mid-write startPlaying optimization (upstream's "audio_guid matches current DS at WRITE time → skip 1-swap defer") — verbatim port acceptable per `feedback_proven_over_pure`; **acknowledged we'll port faithfully**, not deferred.
- VideoToolbox / MediaFoundation hardware-decode backends — abstracted via the `VideoDecoder` interface, but only the libavcodec/openh264 software implementation lands in Phase 21. v1.4+ adds hardware backends.
- Adaptive quality / per-peer downscale on CPU pressure — deferred. Default to fixed-resolution decode for v1.3.
- OpenGL-backed `juce::Image` for HD render performance — deferred to v1.4+ if needed.
- Empty-peer reaping policy — deferred.
- VRR / variable frame-rate rendering — handled implicitly by AsyncUpdater coalescing.

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| **COD-03** | Remote video bytes decode per-peer to a `juce::Image`, with one independent decoder instance per remote user | NinjamZap reference has no decoder (web viewer / mobile only); Phase 21 uses libavcodec via the existing vendored ffmpeg 7.1.2 LGPL build (Phase 14.3-01). `avcodec_find_decoder(AV_CODEC_ID_H264)` is the entry point; `avcodec_send_packet` + `avcodec_receive_frame` modern API is the loop. SwsContext for YUV420P→BGRA. D-09 puts one decoder thread per peer for isolation. Plan 21-02 owns this. [VERIFIED: vendored avcodec.h has the symbols at `libs/ffmpeg/macos-x86_64/include/libavcodec/avcodec.h:2526 / :2547`] |
| **WIRE-02** | Receiving user's video plays in sync with audio at interval boundaries via the GUID-pairing decision tree: DS-match → 1-swap-defer, PREV-match → play-immediately, no-match → HOLD with `kHoldCapDrop=4` resync | NinjamZap source at `njclient.cpp:3084-3219` is the byte-exact reference. The 1-swap defer through `pending` is the protocol-level fix for "video 1 interval early." Phase 21 D-15 ports verbatim. Plan 21-01 owns this. |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| libavcodec (ffmpeg LGPL build) | 61.19.101 (= ffmpeg 7.1.2 era) | H.264 decoder framework | [VERIFIED: vendored at `libs/ffmpeg/macos-x86_64/lib/libavcodec.61.19.101.dylib`] Same vendoring stack as Phase 20. `avcodec_find_decoder(AV_CODEC_ID_H264)` returns the built-in H.264 decoder; no `libopenh264` decode path is needed (Cisco openh264 is encode-only for our purposes — the libavcodec built-in H.264 decoder is what every desktop ffmpeg uses). |
| libswscale | 8.3.100 | YUV420P→BGRA color conversion + scale (handles mid-stream resolution changes per D-07) | [VERIFIED: vendored] Same library as Phase 20 encoder. `sws_getContext(srcW, srcH, AV_PIX_FMT_YUV420P, dstW, dstH, AV_PIX_FMT_BGRA, SWS_BILINEAR, ...)`. SIMD-accelerated on x86_64 and ARM64. |
| libavutil | 59.39.100 | `AVFrame` allocation, `av_image_alloc`, pixel-format constants, `av_packet_alloc`/`av_packet_unref` | [VERIFIED: vendored] Transitive dep. |
| Phase 14.3-03 receive-side dispatch substrate (RawData_Callback) | shipped | BEGIN/WRITE/END event delivery for fourcc=H264 | [VERIFIED: `src/core/njclient.cpp:2292+ / :2458+`] Phase 21 registers the callback and plugs in to the existing event dispatch. No substrate work needed. |
| Phase 15.1-04 `jamwide::SpscRing<T, N>` | shipped (Wave 0, FINAL) | Per-peer decoder input queue (run thread → decoder thread NAL handoff) | [VERIFIED: `src/threading/spsc_ring.h`] Power-of-2 capacity, acquire/release fences, alignas(64) cache-line separation. Lock-free, RT-safe. Used at capacity 32 for the per-peer NalChunk queue. |
| Phase 19 `JamWideFrameDistributor::Subscription` pattern | shipped | Reusable RAII pattern; mirror for the inverse direction | [VERIFIED: `juce/video/native/JamWideFrameDistributor.h`] Phase 21's `JamWideRemoteFrameDistributor::Subscription` is the same shape (move-only, ~Subscription calls `unregisterAndWait`, blocks in-flight callbacks). |
| `juce::AsyncUpdater` | JUCE 7 | Per-peer "new frame ready" signal coalesced onto message thread | [VERIFIED: `juce/video/native/CameraPreviewTile.h:31`] Phase 19 verbatim pattern. `PeerVideoSink` inherits and reuses. |
| `juce::Image` (ARGB format = BGRA-backed on macOS/Windows) | JUCE 7 | Per-peer decoded-frame surface | [VERIFIED: same format Phase 19 captures into] Ref-counted internal pixel data; `std::swap` of two `juce::Image` instances is O(1) (just refcount bumps). |
| WDL primitives (`WDL_PtrList<T>`, `WDL_HeapBuf`, `WDL_TypedBuf<int>`, `WDL_Mutex`, `WDL_MutexLock`) | vendored | NinjamZap-literal data structures — same types upstream's `VideoRecvBuffer`/`VideoRecvState` use | [VERIFIED: `libs/wdl/` in tree] Verbatim port keeps the same types. |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `juce::Thread` | JUCE 7 | Per-peer decoder thread implementation | One per peer (D-09). Existing precedent: `NinjamRunThread`, `Openh264Encoder` (Phase 20-01). |
| `juce::CriticalSection` | JUCE 7 | Brief tearing-protection lock for image_front/back swap (D-08) + listener-vector lock (D-06) | Microsecond-scoped; only swaps refcount pointers, no pixel copies. Compatible with the lock-free generation atomic. |
| `std::atomic<T>` | C++17 | `generation`, `hold_count`, `decode_error_count`, `drop_resync_count`, `synced`, `first_frame_seen` on PeerVideoSink — all lock-free reads from Phase 22 paint cycle | Aligns with Phase 15.1 D-01 lock-free-mediation discipline. |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| libavcodec H.264 decoder | Direct openh264 ISVCDecoder API | Direct API has lower wrapper overhead but is decode-only for the Cisco openh264 binary, which means a separate `libopenh264.dylib` symbol load. libavcodec wraps `libopenh264` for encode but uses its OWN built-in C-language H.264 decoder for decode (faster than openh264's decoder by ~20-30% per FFmpeg's own benchmarks). Stick with libavcodec wrapper — Phase 21 uses the BUILT-IN H.264 decoder (`avcodec_find_decoder(AV_CODEC_ID_H264)` returns the in-tree codec, NOT a libopenh264 wrapper). [CITED: ffmpeg/libavcodec/h264dec.c] |
| One decoder thread per peer | Single decoder thread, multiplex peers via context switch | Single-thread multiplex saves N threads but adds head-of-line blocking — one slow peer freezes everyone else. D-09 locks one-per-peer for isolation. At 8 peers = 8 decoder threads, fine on modern desktop (cf. Chrome runs 30+ threads per video tab). |
| Double-buffered juce::Image | Triple-buffered (writer / staging / reader) | Triple-buffering eliminates writer-side stalls if reader is mid-paint, but adds 1.5× memory (10.5 MB at HD per peer vs 7 MB). At our paint cadence (vsync-driven, ~16 ms) and decoder cadence (~33-100 ms), the double-buffer never stalls — the writer is always done well before the next paint. |
| AVCC parsing on decoder thread (D-12 revised) | AVCC parsing on audio or run thread | Per CONTEXT.md D-12 revised + B-1 + codex Cluster 2: the parser MUST live on the decoder thread. Audio-thread parsing would expand the Phase 15.1 audit-allowlist envelope to cover non-bounded work (full NAL walk + Annex-B wrap allocations); run-thread parsing is envelope-allowed but separates the parser from its libavcodec consumer (one extra cross-thread handoff for no benefit). Decoder-thread parsing co-locates wire-format and codec-API responsibilities on a single non-realtime thread, simplifies the threading audit, and matches the codex review's Option A index-only SPSC redesign (`std::array<VideoRecvSlotSnapshot, 4>` + `SpscRing<int, 4>`). |
| `unordered_map<std::string, std::unique_ptr<PeerVideoSink>>` for distributor | `WDL_PtrList<PeerVideoSink*>` + linear scan by key | Matches the rest of upstream's WDL_PtrList style but std::unordered_map has O(1) lookup. At 3-8 peers the linear scan is fine either way; std::unordered_map is more idiomatic C++. **Default: std::unordered_map.** |
| Annex-B SPS/PPS (D-13) via same path as frames | `extradata` field on `AVCodecContext` | extradata path needs avcC format (length-prefixed) which is what we already have on the wire — BUT it must be set BEFORE `avcodec_open2()` and never changed. Mid-stream SPS/PPS updates (peer preset change) can't reset extradata cleanly. Annex-B + same-path-as-frame-NALs handles mid-stream updates by construction. D-13 locks this. |

**Installation:** No new packages. All dependencies vendored as native libraries in Phase 14.3-01.

**Version verification:**

```bash
ls libs/ffmpeg/macos-x86_64/lib/libavcodec.*.dylib     # libavcodec.61.19.101.dylib
ls libs/ffmpeg/macos-x86_64/lib/libswscale.*.dylib     # libswscale.8.3.100.dylib
ls libs/ffmpeg/macos-x86_64/lib/libavutil.*.dylib      # libavutil.59.39.100.dylib
grep "avcodec_send_packet" libs/ffmpeg/macos-x86_64/include/libavcodec/avcodec.h
# → declared at line 2526
```

**Cross-platform Windows note:** PKG-06 / PKG-07 (Phase 23) — same libavcodec/libswscale
DLLs from the Windows ffmpeg LGPL build. Loader resolution order is the same as Phase 20's
encoder dlopen path. No new dlopen / DLL search work in Phase 21 — we inherit Phase 14.3-01's
linkage.

## Package Legitimacy Audit

> Phase 21 installs **no new external packages**. All dependencies (libavcodec /
> libswscale / libavutil) were vendored as native libraries in Phase 14.3-01.
> No npm/pip/cargo registry surfaces are involved.

| Package | Registry | Age | Downloads | Source Repo | slopcheck | Disposition |
|---------|----------|-----|-----------|-------------|-----------|-------------|
| ffmpeg 7.1.2 (libavcodec/libswscale/libavutil) | source-build via `scripts/build_ffmpeg_lgpl.sh` | mature (>20 yrs) | N/A (project, not registry) | github.com/FFmpeg/FFmpeg | N/A — source build | Approved (Phase 14.3-01) |

**Packages removed due to slopcheck [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none

Note: a defensive slopcheck pass against `libavcodec` / `libswscale` / `libavutil` on PyPI
returned [SLOP] for all three — expected, because those names do not exist on PyPI;
they are native system libraries with no Python registry presence. Cross-ecosystem
confusion check passed by inspection (these are LGPL C libraries vendored as `.dylib` /
`.dll`, not registry packages).

## Architecture Patterns

### System Architecture Diagram

```
                ┌──────────────────────────────────────────────────────────┐
                │ NINJAM server → TCP socket → Net_Connection::Recv         │
                │ (delivers MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN/WRITE)   │
                └─────────────────────────┬────────────────────────────────┘
                                          │
                                          ▼
                ┌──────────────────────────────────────────────────────────┐
                │ NJClient::Run (run thread)                                │
                │ MESSAGE switch:                                           │
                │   BEGIN(fourcc=H264) →                                    │
                │     RawData_Callback dispatched (Phase 14.3-03 substrate) │
                │     onRawDataBegin(...) callback → Phase 21 handler:      │
                │       acquire m_video_recv_cs                             │
                │       findOrCreateVideoStream(username, chidx)            │
                │         (lazy: ctor allocates 4× 4MB slots, spawns        │
                │          decoder thread, creates PeerVideoSink,           │
                │          registers with distributor)                      │
                │       reset vs->accumulating; mark active                 │
                │       release m_video_recv_cs                             │
                │                                                           │
                │   WRITE(matched GUID) →                                   │
                │     acquire m_video_recv_cs                               │
                │     findVideoStreamByGUID OR route via append_active      │
                │     append bytes to target slot (4MB cap check)            │
                │     multi-write reassembly via pending_remaining +         │
                │       4B BE length prefix per logical frame                │
                │     on first complete frame of accumulating:               │
                │       parse 24B marker → audio_guid + sender_seq           │
                │       if !next.active: copyFrom(accumulating) →             │
                │         (mid-download startPlaying) +                       │
                │         set append_active=true, append_to_next=true         │
                │     release m_video_recv_cs                                │
                │                                                           │
                │   WRITE(flags&1, END) →                                    │
                │     acquire m_video_recv_cs                                │
                │     if !wasAppending: copyFrom(accumulating) → next        │
                │     reset accumulating + append flags                      │
                │     release m_video_recv_cs                                │
                └─────────────────────────┬────────────────────────────────┘
                                          │
                                          │ (audio thread fires at interval boundary)
                                          ▼
                ┌──────────────────────────────────────────────────────────┐
                │ NJClient::AudioProc → on_new_interval (audio thread)       │
                │ (after existing audio interval handling, after Phase 20    │
                │  send-side video block)                                    │
                │                                                            │
                │ acquire m_video_recv_cs (D-16 envelope)                    │
                │ for each VideoRecvState vs in m_video_streams:             │
                │   STAGE 1: if pending.active && pending.frameCount >= 1:   │
                │     playing.copyFrom(pending); pending.reset()              │
                │                                                            │
                │   if next.active && next.frameCount >= 1:                  │
                │     find sender's DecodeState via                          │
                │       m_remoteuser_mirror[s].next_ds[0] (Phase 15.1-07a    │
                │       HIGH-2 carve-out)                                    │
                │     compare videoAudioGuid against senderDs->guid (DS)     │
                │       and vs->prev_ds_guid (PREV)                          │
                │     DS-match → pending.copyFrom(next); next.reset();       │
                │                  set append_to_pending=true                │
                │     PREV-match → playing.copyFrom(next); next.reset();     │
                │                  immediate play (no defer)                 │
                │     no-match → hold_count++;                               │
                │                if hold_count >= 4 (kHoldCapDrop):          │
                │                  drop_resync_count++; next.reset();        │
                │                  synced=false; hold_count=0                │
                │     update prev_ds_guid for next swap                      │
                │   else: hold_count=0; empty_count++                        │
                │                                                            │
                │   (If a slot is now playing:)                              │
                │     parse playing.data via AVCC walk →                     │
                │       NalChunk{ParamSet, sps_annexb} +                     │
                │       NalChunk{ParamSet, pps_annexb} +                     │
                │       NalChunk{Frame, nal_bytes} × frame_count             │
                │     vs->decoderInputQ.try_push(...) for each (SPSC)        │
                │                                                            │
                │ release m_video_recv_cs                                    │
                └─────────────────────────┬────────────────────────────────┘
                                          │ NalChunk SPSC (per peer, cap 32)
                                          ▼
                ┌──────────────────────────────────────────────────────────┐
                │ Openh264Decoder::run (decoder thread, one per peer)        │
                │ Loop:                                                      │
                │   1. wait on event / poll SPSC                             │
                │   2. pop NalChunk                                          │
                │   3. wrap NAL bytes in AVPacket (av_packet_alloc;          │
                │      av_packet_from_data or set ->data + ->size)           │
                │   4. avcodec_send_packet(ctx, pkt)                         │
                │      → AVERROR(EAGAIN): drain receive first, then retry    │
                │      → AVERROR_INVALIDDATA: drop, bump decode_error_count, │
                │        continue (libavcodec auto-recovers on next IDR)     │
                │   5. while (avcodec_receive_frame(ctx, frame) == 0):       │
                │        if (sws_ == nullptr || frame size changed):         │
                │          sws_freeContext(old); sws_ = sws_getContext(...);  │
                │          (D-07: keep sink output dims fixed)                │
                │        av_image_alloc on image_back juce::Image bits        │
                │        sws_scale(sws_, frame->data, frame->linesize,        │
                │                   0, frame->height, dst_data, dst_linesize) │
                │        bufferLock.Enter();                                   │
                │        std::swap(image_front, image_back);                  │
                │        generation.fetch_add(1, release);                    │
                │        bufferLock.Leave();                                   │
                │        first_frame_seen.store(true, release);                │
                │        sink->triggerAsyncUpdate();                           │
                └─────────────────────────┬────────────────────────────────┘
                                          │ AsyncUpdater coalesced trigger
                                          ▼
                ┌──────────────────────────────────────────────────────────┐
                │ PeerVideoSink::handleAsyncUpdate (message thread)          │
                │ (no work; AsyncUpdater dispatch is the signal —             │
                │  registered listeners snapshot the image themselves)        │
                │                                                            │
                │ for (auto& cb : listeners_) cb();                          │
                │   (each cb = Phase 22 tile's `[tile]{                       │
                │      tile->triggerAsyncUpdate();                            │
                │    }`)                                                      │
                └─────────────────────────┬────────────────────────────────┘
                                          │
                                          ▼
                ┌──────────────────────────────────────────────────────────┐
                │ Phase 22 tile::paint (message thread, vsync-driven)        │
                │ uint64_t gen = sink->generation.load(acquire);             │
                │ if (gen != last_gen):                                      │
                │   bufferLock.Enter();                                       │
                │   img_to_paint = sink->image_front; // refcount bump         │
                │   bufferLock.Leave();                                       │
                │   last_gen = gen;                                          │
                │ g.drawImage(img_to_paint, ...)                              │
                │ if (!sink->first_frame_seen) draw 'video starting…' overlay │
                │ if (sink->hold_count >= 2)   draw 'syncing…' overlay        │
                └──────────────────────────────────────────────────────────┘
```

### Recommended Project Structure

```
juce/video/decoder/
├── VideoDecoder.h               # Pure-virtual interface (mirrors VideoEncoder.h)
├── Openh264Decoder.h            # Concrete impl (decoder thread + libavcodec)
└── Openh264Decoder.cpp

juce/video/distributor/           # NEW directory (Phase 21)
├── JamWideRemoteFrameDistributor.h   # Symmetric inverse of JamWideFrameDistributor
├── JamWideRemoteFrameDistributor.cpp
├── PeerVideoSink.h              # Double-buffered juce::Image + atomic status fields
└── PeerVideoSink.cpp

src/core/njclient.h              # Add (verbatim port from ninjamzap-core/njclient.h:334-417):
                                 #   struct VideoRecvBuffer { … };
                                 #   struct VideoRecvState  { … decoderInputQ, decoder, sink … };
                                 #   WDL_PtrList<VideoRecvState> m_video_streams;
                                 #   WDL_Mutex m_video_recv_cs;
                                 #   VideoRecvState* findVideoStream(username, chidx);
                                 #   VideoRecvState* findOrCreateVideoStream(...);
                                 #   VideoRecvState* findVideoStreamByGUID(guid);
                                 #   void removeVideoStream(username, chidx);
                                 # Plus accessors for JamWideRemoteFrameDistributor*
                                 # (constructed at NJClient ctor; passed to Plan 21-03).

src/core/njclient.cpp            # Plan 21-01:
                                 #   helpers (findVideoStream, etc.) — port from
                                 #     ninjamzap-core/njclient.cpp:2124-2173
                                 #   RawData_Callback registration for fourcc=H264:
                                 #     dispatches to NJClient::onVideoBegin/Write/End
                                 #     internal methods (run-thread)
                                 #   onVideoBegin: findOrCreate + accumulating.reset
                                 #     + lazy lifecycle spin-up (D-10)
                                 #   onVideoWrite: GUID-routed slot accumulation +
                                 #     4MB cap + marker parse + mid-download
                                 #     startPlaying — verbatim port of
                                 #     ninjamzap-core/njclient.cpp:1439-1565
                                 #   onVideoEnd: accumulating → next move —
                                 #     verbatim port of njclient.cpp:1568-1592
                                 #   on_new_interval video receive block:
                                 #     STAGE 1 promote + GUID-pair tree —
                                 #     verbatim port of njclient.cpp:3084-3219
                                 #   m_remoteusers user-leave: video-state reset —
                                 #     verbatim port of njclient.cpp:1300-1325
                                 # Plan 21-02:
                                 #   AVCC walker helper (parse playing slot →
                                 #     NalChunks → SpscRing<NalChunk, 32>::try_push)

juce/JamWideJuceProcessor.{h,cpp} # Add (Plan 21-03):
                                  #   std::unique_ptr<JamWideRemoteFrameDistributor>
                                  #     remoteFrameDistributor;
                                  #   ctor: construct distributor BEFORE NJClient
                                  #     (NJClient takes a non-owning pointer in its ctor)
                                  #   dtor: distributor destroyed AFTER NJClient

tests/test_video_recv_state.cpp   # Unit — Plan 21-01 substrate
                                  # Drives DispatchTestServerDownloadIntervalBegin/Write/End
                                  # to exercise the 4-stage pipeline + GUID-pair tree
                                  # against synthetic NJClient state. No decoder.
                                  # Scenarios ported: 02, 03, 20, 22 (4 of 6).

tests/test_video_decoder.cpp      # Unit — Plan 21-02
                                  # Feeds synthetic Annex-B NAL streams (test fixtures:
                                  # a small SPS+PPS + I-frame from a fixture file)
                                  # into Openh264Decoder; verifies first-frame output,
                                  # error-recovery path on bad NAL, resolution-change
                                  # path (SwsContext recreate).
                                  # Scenarios ported: 13, 25 (2 of 6).

tests/test_remote_frame_distributor.cpp  # Unit — Plan 21-03
                                          # PeerVideoSink double-buffer + atomic generation
                                          # + listener-vector lifetime under move-only
                                          # Subscription pattern.

tests/test_video_sync_e2e.cpp     # Optional integration — Plan 21-03
                                  # Ties Plan 21-01 + 21-02 + 21-03 together against a
                                  # mock TestClient producer (NinjamZap harness shape).
                                  # Verifies success criteria 1 + 2 + 3 + 4 in-process.
```

### Pattern 1: Verbatim VideoRecvBuffer / VideoRecvState Port

**What:** Port `VideoRecvBuffer` + `VideoRecvState` struct definitions byte-for-byte from
upstream, extending only with the JamWide-specific decoder + sink ownership fields.

**When to use:** Plan 21-01 — this is the canonical data model. Per `feedback_proven_over_pure`,
DO NOT redesign these structs to fit JamWide's RAII / atomic preferences. The whole
point of the carve-out envelope is that the canonical NinjamZap pattern is known-correct
and we accept its style (WDL primitives + manual lifetime) as the substrate.

**Example (port verbatim):**

```cpp
// Source: ninjamzap-core/njclient.h:345-413 verbatim with JamWide-specific extensions
struct VideoRecvBuffer {
  WDL_HeapBuf data;                  // 4 MB pre-allocated soft cap (D-11)
  WDL_TypedBuf<int> frameOffsets;
  int frameCount;
  char username[256];
  unsigned char guid[16];
  unsigned int fourcc;
  int chidx;
  int interval_seq;
  unsigned char audio_guid[16];      // from 24B marker
  bool active;
  int pending_remaining;             // multi-write reassembly
  int sender_seq;                    // from 24B marker
  VideoRecvBuffer() : frameCount(0), fourcc(0), chidx(0),
                      interval_seq(-1), active(false),
                      pending_remaining(0), sender_seq(-1) {
    username[0] = 0;
    memset(guid, 0, 16);
    memset(audio_guid, 0, 16);
    data.Resize(0, false);
    data.Resize(4 * 1024 * 1024, true);  // PRE-ALLOCATE D-11 4MB cap
    data.Resize(0, false);               // size back to 0; capacity stays at 4MB
  }
  void reset();
  void copyFrom(const VideoRecvBuffer &src);
};

struct VideoRecvState {
  VideoRecvBuffer accumulating;
  VideoRecvBuffer next;
  VideoRecvBuffer pending;
  VideoRecvBuffer playing;
  int frame_idx;
  int expected_frames;
  bool append_active;
  bool append_to_next;
  bool append_to_pending;
  unsigned char append_guid[16];
  char stream_username[256];
  int stream_chidx;
  char key[280];
  int  empty_count;
  int  hold_count;
  unsigned char prev_ds_guid[16];
  bool synced;
  int  last_played_sender_seq;
  unsigned char last_played_audio_guid[16];
  int  drop_resync_count;

  // JamWide additions (D-09 / D-10 lazy lifecycle):
  std::unique_ptr<jamwide::Openh264Decoder> decoder;      // owns juce::Thread + codec context
  jamwide::SpscRing<jamwide::NalChunk, 32>  decoderInputQ;
  jamwide::PeerVideoSink*                   sink = nullptr;  // owned by distributor

  VideoRecvState();
  ~VideoRecvState();  // joins decoder thread, removes sink from distributor
};
```

### Pattern 2: Decoder Thread (libavcodec consumer)

**What:** Per-peer `juce::Thread` subclass that owns the `AVCodecContext` + `SwsContext` +
runs the `avcodec_send_packet` / `avcodec_receive_frame` loop. Symmetric to Phase 20's
`Openh264Encoder` (encoder owns thread, drains SPSC of input frames, runs libavcodec;
decoder follows the inverse pattern).

**When to use:** Plan 21-02 — single concrete implementation behind the abstract
`VideoDecoder` interface (D-09 + Claude's discretion on file layout).

**Example (skeleton):**

```cpp
// juce/video/decoder/Openh264Decoder.cpp
namespace jamwide {

void Openh264Decoder::run() {
  juce::Thread::setCurrentThreadName("JamWide H264 Decoder");

  while (!threadShouldExit()) {
    // Wait for SPSC signal — pending_event_ pulsed by run thread after push
    pending_event_.wait(100);  // 100 ms max sleep — also drains stale state

    while (auto chunk_opt = inputQ_->try_pop()) {
      auto& chunk = *chunk_opt;
      AVPacket* pkt = av_packet_alloc();
      pkt->data = chunk.bytes.data();
      pkt->size = (int)chunk.bytes.size();

      int send_rc = avcodec_send_packet(codecContext_, pkt);
      av_packet_free(&pkt);

      if (send_rc == AVERROR_INVALIDDATA || send_rc == AVERROR(EINVAL)) {
        sink_->decode_error_count.fetch_add(1, std::memory_order_relaxed);
        // drop frame, continue — libavcodec auto-recovers on next IDR (D-18)
        continue;
      }
      if (send_rc == AVERROR(EAGAIN)) {
        // shouldn't happen for H.264 unless we starve receive_frame —
        // drain first, then retry on next loop iteration. Per upstream
        // FFmpeg docs: "send_packet AVERROR(EAGAIN) implies receive
        // will succeed" — no infinite loop possible.
      }

      // Drain frames — for H.264 Baseline (no B-frames) one packet → 0..1 frame
      while (true) {
        int rc = avcodec_receive_frame(codecContext_, frame_);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
        if (rc < 0) {
          sink_->decode_error_count.fetch_add(1, std::memory_order_relaxed);
          break;
        }
        // sws_scale into image_back's juce::Image bits
        scaleAndSwapImage_(frame_);
        sink_->first_frame_seen.store(true, std::memory_order_release);
        sink_->triggerAsyncUpdate();
        av_frame_unref(frame_);
      }
    }
  }
  // Drain flush: send NULL to avcodec_send_packet, drain receive until EOF
  avcodec_send_packet(codecContext_, nullptr);
  while (avcodec_receive_frame(codecContext_, frame_) == 0) {
    av_frame_unref(frame_);
  }
}

void Openh264Decoder::scaleAndSwapImage_(const AVFrame* frame) {
  // D-07: lazy sws_ recreate on resolution change
  if (!sws_ || frame->width != sws_src_width_ || frame->height != sws_src_height_) {
    if (sws_) sws_freeContext(sws_);
    sws_ = sws_getContext(frame->width, frame->height, AV_PIX_FMT_YUV420P,
                          dst_width_, dst_height_, AV_PIX_FMT_BGRA,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    sws_src_width_  = frame->width;
    sws_src_height_ = frame->height;
  }

  juce::Image::BitmapData bits(image_back_, juce::Image::BitmapData::writeOnly);
  uint8_t* dst_data[4] = {bits.data, nullptr, nullptr, nullptr};
  int dst_linesize[4]  = {bits.lineStride, 0, 0, 0};
  sws_scale(sws_, frame->data, frame->linesize, 0, frame->height,
            dst_data, dst_linesize);

  {
    const juce::ScopedLock lock(sink_->bufferLock);
    std::swap(sink_->image_front, image_back_);
    sink_->generation.fetch_add(1, std::memory_order_release);
  }
}

}  // namespace jamwide
```

### Pattern 3: PeerVideoSink (Phase 19 atomic-generation + AsyncUpdater verbatim)

**What:** Per-peer sink owning the double-buffered juce::Image, atomic generation
counter, atomic status fields, and the listener-vector. Lives inside the distributor's
map; its lifetime is bounded by the VideoRecvState that triggered its creation.

**When to use:** Plan 21-03 — the central data structure that Phase 22 reads.

**Example:**

```cpp
namespace jamwide {

class PeerVideoSink : public juce::AsyncUpdater {
public:
  PeerVideoSink(int width, int height)
    : image_front(juce::Image::ARGB, width, height, true /*clear*/),
      image_back (juce::Image::ARGB, width, height, true) {}

  // Decoder thread writes pixels into image_back, then under bufferLock
  // swaps front/back and bumps generation. UI thread reads image_front
  // under the same lock briefly (refcount bump only — no pixel copy).
  juce::Image                 image_front;
  juce::Image                 image_back;
  juce::CriticalSection       bufferLock;
  std::atomic<std::uint64_t>  generation{0};

  // Lock-free status fields (D-20) — read by Phase 22 paint() each repaint
  std::atomic<int>            hold_count{0};
  std::atomic<int>            decode_error_count{0};
  std::atomic<int>            drop_resync_count{0};
  std::atomic<bool>           synced{false};
  std::atomic<bool>           first_frame_seen{false};

  // Listener-vector (D-06) — DISP-03 multi-tile support
  void addListener   (std::uint64_t id, std::function<void()> cb);
  void removeListener(std::uint64_t id);

  void handleAsyncUpdate() override {
    juce::ScopedLock lock(listenerLock_);
    for (auto& [id, cb] : listeners_) cb();
  }

private:
  juce::CriticalSection                                                     listenerLock_;
  std::unordered_map<std::uint64_t, std::function<void()>>                  listeners_;
};

}  // namespace jamwide
```

### Pattern 4: JamWideRemoteFrameDistributor (symmetric inverse of Phase 19 distributor)

**What:** Owns the per-peer `PeerVideoSink` map keyed by `(username, chidx)`. Returns
RAII `Subscription` from `subscribeToPeer(...)`. Phase 22 tiles hold the Subscription
as a member; destruction detaches the callback before the tile is torn down.

**When to use:** Plan 21-03 — exposed to Phase 22 via a `JamWideJuceProcessor` accessor.

**Example:**

```cpp
namespace jamwide {

class JamWideRemoteFrameDistributor {
public:
  class Subscription {
  public:
    Subscription() = default;
    Subscription(Subscription&&) noexcept;
    Subscription& operator=(Subscription&&) noexcept;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription();  // calls dist->unsubscribe(key, id); blocks in-flight
  private:
    friend class JamWideRemoteFrameDistributor;
    Subscription(JamWideRemoteFrameDistributor* d, std::string key, std::uint64_t id)
      : dist(d), key(std::move(key)), id(id) {}
    JamWideRemoteFrameDistributor* dist = nullptr;
    std::string                    key;
    std::uint64_t                  id   = 0;
  };

  [[nodiscard]]
  Subscription subscribeToPeer(const std::string& username, int chidx,
                                std::function<void()> onRepaint);

  // Lifecycle hooks called by NJClient (run thread):
  PeerVideoSink* findOrCreateSink(const std::string& username, int chidx,
                                  int width, int height);
  void           removeSink(const std::string& username, int chidx);

  PeerVideoSink* findSink(const std::string& username, int chidx) noexcept;

private:
  std::string makeKey_(const std::string& username, int chidx) const {
    return username + ":" + std::to_string(chidx);
  }
  void unsubscribe_(const std::string& key, std::uint64_t id) noexcept;

  juce::CriticalSection                                                   mu_;
  std::unordered_map<std::string, std::unique_ptr<PeerVideoSink>>         sinks_;
  std::uint64_t                                                           nextId_ = 1;
};

}  // namespace jamwide
```

### Anti-Patterns to Avoid

- **Allocating in the audio-thread SWAP block outside the D-16 envelope.** Allocations
  inside `m_video_recv_cs` are accepted (NinjamZap-literal envelope). Allocations
  OUTSIDE that critical section, on the audio thread, are CRITICAL. Specifically: do
  NOT allocate when populating the NalChunk SPSC if NalChunk is variable-size — use a
  fixed-size inline buffer + heap-fallback variant.
- **Calling decoder-thread methods directly from the audio thread.** The
  `vs->decoderInputQ.try_push(...)` is the only audio-thread → decoder-thread
  communication. Do NOT signal the decoder thread synchronously from the audio thread
  (no Sem::release, no WaitableEvent::signal that the decoder thread waits on while
  holding any audio-relevant resource — the SPSC push is the signal).
- **Forgetting to reset prev_ds_guid + hold_count on user-leave.** Upstream's
  `njclient.cpp:1300-1325` does this explicitly (commented "Without this,
  `prev_ds_guid` / `hold_count` / `synced` lingered and caused spurious PREV matches
  and 'video earlier than audio'"). Phase 21 MUST port this byte-for-byte — it's a
  documented landmine.
- **Triple-buffering the juce::Image.** D-08 locks double-buffered. Triple adds memory
  cost without correctness benefit at our paint cadence vs decoder cadence.
- **Sharing one SwsContext across peers.** D-09 / D-07 — each decoder thread owns its
  own SwsContext (`sws_getContext`-allocated, lazy-recreated on source-resolution
  change). libswscale's documented thread-safety is "one context per thread" — sharing
  contexts across decoder threads is undefined behaviour.
- **Setting AVCodecContext::extradata.** D-13 — feed SPS/PPS as Annex-B NALs through
  the same `avcodec_send_packet` path. extradata can't be changed mid-stream
  cleanly; Annex-B by-construction handles mid-stream preset changes.
- **Tearing down the decoder thread on each `decode_error_count` bump.** D-18 — drop
  the frame, bump counter, continue. libavcodec auto-recovers on the next IDR
  (Phase 20 D-14 guarantees IDR every interval, so ≤ ~3-8 second recovery window).
- **Letting a slow peer's decoder block other peers.** D-09 puts one thread per peer
  for isolation. Do not introduce shared queues or sequential dispatch.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| H.264 decoding | A custom Annex-B parser + decoder | `avcodec_find_decoder(AV_CODEC_ID_H264)` from libavcodec | H.264 decoding has decades of edge cases — entropy decoding, deblocking, CABAC, slice-headers, NAL unit reassembly, error concealment. libavcodec's H.264 decoder is what every desktop video player uses; rolling your own is a multi-month project. |
| YUV→BGRA color conversion + scaling | A manual SIMD or scalar loop | `libswscale sws_scale` | SIMD-optimised, handles all libavcodec pixel formats including YUV420P, NV12, and YUV422P. JamTaba's hand-rolled macro at `FFMpegMuxer.cpp:541-577` exists as a cautionary tale — it's slower and pixel-format-limited. |
| 4-stage pipeline state machine | A new design with `enum Stage { ACC, NEXT, PEND, PLAY }` and transition rules | Verbatim port of upstream `VideoRecvBuffer` × 4 + `VideoRecvState` struct definitions | The pipeline's correctness has been validated against the canonical NinjamZap web viewer + iOS/Android peers. Redesigning to fit JamWide-idiomatic C++ would re-introduce the "video one interval early" bug class. |
| GUID-pairing decision tree | A custom design with a state-table or callback dispatcher | Verbatim port of upstream `on_new_interval` block at `njclient.cpp:3084-3219` | Same as 4-stage pipeline — proven against real peer interop. DS-match + PREV-match + HOLD-cap=4 + resync semantics are all subtle; any deviation from upstream is a new bug. |
| Frame distribution + multi-subscriber lifetime | A new lifetime model | Reuse Phase 19's `JamWideFrameDistributor::Subscription` HIGH-2 RAII pattern verbatim | The pattern is hardened (move-only handle, `unregisterAndWait` blocks in-flight callbacks). Phase 21's symmetric inverse uses the same shape. |
| Atomic-generation lock-free image read | A custom seqlock or RCU | Reuse Phase 19 HIGH-4 atomic-generation + AsyncUpdater pattern verbatim | The pattern is hardened (single writer, single generation atomic, brief refcount lock for tearing protection). |
| SPS/PPS extradata management | Build `extradata` byte buffer + reset before `avcodec_open2` | Annex-B feed via same `avcodec_send_packet` path as frame NALs (D-13) | Annex-B handles mid-stream SPS/PPS updates by construction; extradata cannot. libavcodec recognises NAL unit_type=7/8 in the stream and updates internal state. |
| SpscRing | A new ring or std::queue + mutex | Reuse Phase 15.1-04's `jamwide::SpscRing<T, N>` | Two-layer enforcement at allocations + tests + audit-allowlist. NalChunk handoff is single-producer (run thread) single-consumer (decoder thread) — exact fit. |
| Per-peer decoder thread orchestration | A thread-pool with priority queues | One `juce::Thread` per VideoRecvState, owned directly | Per-peer isolation (D-09). At 8 peers = 8 decoder threads — fine on modern desktop. |

**Key insight:** Phase 21 is overwhelmingly a port-from-reference exercise. The only
green-field design is:
1. The decoder thread + libavcodec wrapping (no upstream reference)
2. The `JamWideRemoteFrameDistributor` + `PeerVideoSink` (Phase 19 symmetric inverse)
3. The lazy lifecycle wiring (Phase 20 D-13 symmetric inverse)

Everything else — `VideoRecvBuffer`/`VideoRecvState` struct, WRITE accumulation,
24B marker parsing, mid-download startPlaying, audio-thread SWAP, GUID-pair tree,
user-leave reset, multi-write reassembly — is **byte-for-byte port of NinjamZap source**
per `feedback_proven_over_pure`. Do not redesign.

## Runtime State Inventory

> Phase 21 is a feature-add phase, not a rename/refactor/migration. Section included
> for completeness.

| Category | Items Found | Action Required |
|----------|-------------|-----------------|
| Stored data | None — Phase 21 adds new runtime state (VideoRecvState per peer, PeerVideoSink) but does not persist anything to disk or to APVTS state. The `<camera>` subtree (Phase 19 D-25) is untouched. | None |
| Live service config | None — JamWide does not provision external services; the NINJAM server is configured by the user via the existing server browser. | None |
| OS-registered state | None | None |
| Secrets/env vars | None | None |
| Build artifacts | None new — Phase 21 adds source files but reuses Phase 14.3-01's vendored libavcodec / libswscale dylibs/DLLs. | None |

## Common Pitfalls

### Pitfall 1: Decoder hang under input starvation (EAGAIN endless loop)

**What goes wrong:** `avcodec_send_packet` returns `AVERROR(EAGAIN)`, decoder thread
loops trying to send the same packet, never drains receive — endless loop.

**Why it happens:** The send/receive API contract is: when send returns EAGAIN, you
MUST drain receive_frame until it also returns EAGAIN, THEN retry send. Naive code that
"retries send on EAGAIN without draining receive first" deadlocks the codec.

**How to avoid:** In the decoder thread loop, ALWAYS drain `avcodec_receive_frame`
until it returns EAGAIN BEFORE calling send_packet again. If send_packet returns
EAGAIN, the drain in the previous iteration was incomplete — drain immediately and
retry. Per FFmpeg's own documentation: "A codec is not allowed to return AVERROR(EAGAIN)
for both sending and receiving" — so the pattern in Pattern 2 above (drain receive in
a `while` loop inside the per-send iteration) is the safe shape.

**Warning signs:** Decoder thread CPU pinned at 100% while frame_output_count is not
advancing. Per-peer `decode_error_count` is zero (so no errors), but no frames are
appearing — clear signature of the EAGAIN loop.

### Pitfall 2: libavcodec version differences across vendored ffmpeg builds

**What goes wrong:** The macOS and Windows ffmpeg LGPL builds may not have identical
libavcodec versions. Mid-stream SPS/PPS handling, decoder error codes, or
`av_image_alloc` semantics could differ subtly. Code that works on macOS x86_64
breaks on Windows x86_64 at the cross-platform UAT (BETA-05 / BETA-06).

**Why it happens:** The vendored libs at `libs/ffmpeg/macos-x86_64/lib/libavcodec.61.19.101.dylib`
are pinned to ffmpeg 7.1.2. The Windows build (PKG-07, Phase 23) needs to use the SAME
ffmpeg version — but the Windows LGPL build script (Phase 23 deliverable) may inherit
a different upstream pin if it's not careful.

**How to avoid:**
1. Plan 23 (Windows build) MUST use the same ffmpeg source tag (`n7.1.2`) as macOS.
2. Plan 21-02's decoder code uses ONLY the public API (`avcodec_send_packet`,
   `avcodec_receive_frame`, `sws_getContext`, `sws_scale`, `av_image_alloc`,
   `av_frame_alloc`, `av_packet_alloc`) — no version-conditional code paths.
3. Plan 21-03 UAT acceptance includes a Windows-side dry-run before BETA-05 / BETA-06
   close, so version drift is caught in-phase rather than in beta.
4. CI lane (Phase 23-03) runs `dumpbin /dependents` on Windows + `otool -L` on macOS
   to verify the SAME ffmpeg minor version is loaded.

**Warning signs:** Test scenarios PASS on macOS but FAIL on Windows with cryptic
`AVERROR(EINVAL)` or `AVERROR(ENOMEM)` codes. Visual: a 1-pixel-line-shift artifact
on one platform (sws_scale linesize alignment differences).

### Pitfall 3: ffmpeg DLL resolution order on Windows

**What goes wrong:** Windows finds the wrong `avcodec-61.dll` (e.g., a system-installed
version, or a different app's version on PATH), loads it, and the plugin crashes at
load time with `EntryPointNotFoundException` or returns wrong results.

**Why it happens:** Windows DLL search order: app directory → System32 → SysWOW64 →
PATH directories. JUCE plugins are loaded into a host process (REAPER / Live / Bitwig),
not run as their own .exe — so the "app directory" is the HOST's directory, not the
JamWide plugin's directory. If the host happens to ship its own ffmpeg DLLs (REAPER
does for some installs), THOSE get loaded instead of ours.

**How to avoid:**
1. Plan 21-02 NEVER calls `LoadLibrary("avcodec-61.dll")` explicitly — links statically
   against the vendored import library, so the loader follows the linkage records.
2. Plan 23-02 (Windows packaging) places JamWide's DLLs ALONGSIDE the plugin DLL
   (`%PROGRAMFILES%\Common Files\VST3\JamWide\JamWide.vst3\Contents\x86_64-win\`) and
   uses `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` via `AddDllDirectory` at plugin entry to
   pin the search path.
3. Alternatively, use `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_USER_DIRS |
   LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` in plugin init to exclude unsafe paths.
4. Plan 21-02 test surface: a Windows-side test that LoadLibrary-probes the loaded
   `avcodec-61.dll` path and asserts it matches `JamWide.vst3` directory.

**Warning signs:** Plugin loads fine in Standalone (JamWide.exe directory has its own
DLLs) but crashes when loaded by REAPER. `Process Monitor` or `dependency walker` on
the host process shows the wrong avcodec DLL path.

### Pitfall 4: Decoder thread starvation under macOS QoS scheduling

**What goes wrong:** macOS Sonoma+ uses QoS classes for thread scheduling, not raw
pthread priorities. JUCE 7.0.10+ removed the integer setPriority API — `juce::Thread::setPriority`
no longer maps onto realtime scheduling on POSIX. The decoder thread can be starved by
audio-thread priority OR by efficiency-core demotion.

**Why it happens:** JUCE 7 changed thread priority to an enum API (Background / Default
/ High / Realtime). The default decoder thread priority is `Default`. The audio thread
runs at `Realtime` priority (via JUCE's Workgroups integration on macOS). Under sustained
CPU load (multiple HD peer decode + audio mixing + UI paint), the macOS scheduler may
demote the decoder thread to efficiency cores and starve it.

**How to avoid:**
1. Plan 21-02 sets `juce::Thread::Priority::High` on the decoder thread (between
   default and realtime). DON'T set Realtime — that competes with audio.
2. Plan 21-03 UAT measures decoder cadence under 3-peer HD load + audio glitch-free
   verification (success criterion 4). If decoder cadence drops below 10 fps per peer
   at 1280×720, escalate to a single-threaded multiplex decoder (Phase 24+) or reduce
   per-peer preset.
3. Document the constraint in CONTEXT.md Claude's Discretion: priority default = High,
   not Realtime.

**Warning signs:** Frame freeze on multiple peers simultaneously while audio stays
clean. `decode_error_count` stays at zero (no errors), `drop_resync_count` stays at
zero (no sync drops) — clear signature of pure CPU starvation.

### Pitfall 5: Phase 15.1-07a mirror lifetime contract — `next_ds[0]` UAF window

**What goes wrong:** Audio thread reads `m_remoteuser_mirror[s].next_ds[0]->guid` in
the GUID-pair decision tree. If `next_ds[0]` is in the middle of being swapped (Phase
15.1-05 handover SPSC), the audio thread could deref a freed pointer.

**Why it happens:** Phase 15.1-07a accepted this as a HIGH-2 carve-out — the audio
thread CAN read `next_ds[0]->guid` because the DecodeState ownership crosses thread
boundaries via the SPSC-handoff pattern (audio thread takes ownership at swap time,
old pointer goes to deferred-delete SPSC). The carve-out is for `guid` field ONLY —
do NOT read `decode_codec` or `decode_buf` from the audio thread.

**How to avoid:**
1. Plan 21-01 reads ONLY `senderDs->guid` (16 bytes, fixed at DecodeState creation,
   never written after construction). Do not read any other DecodeState field.
2. The audit-allowlist envelope (D-16) explicitly carves out this single read site.
3. Plan 21-01 verification grep: `grep -E 'm_remoteuser_mirror|next_ds\[0\]' src/core/njclient.cpp`
   must show ONLY the GUID-pair-tree read + the Phase 15.1-07a-blessed sites.

**Warning signs:** TSan flags a race between audio-thread `next_ds[0]->guid` read and
the run-thread DecodeState handover. (This SHOULDN'T happen — Phase 15.1-07a's SPSC
handover with deferred-delete is the guarantee — but if you accidentally read another
field of DecodeState, the race surfaces.)

### Pitfall 6: VideoRecvBuffer::copyFrom timing under 4 MB load at HD × N peers

**What goes wrong:** Each `copyFrom` is `WDL_HeapBuf::Resize(sz, false)` + `memcpy(dst, src, sz)`
where `sz` can be up to 4 MB. Audio-thread SWAP iterates over N peers and does
multiple copyFroms per swap (STAGE 1 promote = pending→playing, and DS-match =
next→pending). At HD × 8 peers, worst-case ~64 MB copied on the audio thread per
swap.

**Why it happens:** WDL_HeapBuf::Resize with `false` second arg preserves data — it's
a realloc, which can be O(n) memcpy. The Resize-to-fit-then-memcpy pattern is what
upstream uses; under HD × 8 peers it becomes a measurable audio-thread budget
consumer.

**How to avoid:**
1. Pre-allocate the WDL_HeapBuf to 4 MB at VideoRecvBuffer ctor (D-11). After
   pre-allocation, `Resize(sz, false)` to a smaller size does NOT shrink — it just
   sets the logical size. Subsequent grows up to 4 MB don't realloc.
2. Audio-thread SWAP block is timed in Plan 21-03 UAT. Acceptance threshold: worst-case
   SWAP duration at HD × 8 peers ≤ 5 ms (well within NINJAM interval = seconds).
3. If 5 ms exceeded, pivot to pointer-swap semantics: VideoRecvBuffer holds
   `std::unique_ptr<HeapBuf>`, copyFrom swaps the pointer instead of copying bytes.
   But this changes the upstream API shape — NinjamZap-literal is to keep copyFrom-by-bytes.

**Warning signs:** Audio glitches at scale (4+ peers HD broadcasting). Profile shows
audio thread spending >1 ms inside `m_video_recv_cs`.

### Pitfall 7: Resolution-change handling — SwsContext recreate timing

**What goes wrong:** Peer toggles Low→High preset mid-session. Decoder thread sees a
larger source frame on the next `avcodec_receive_frame`. SwsContext was created for
the old smaller source dims — sws_scale would either misalign or crash.

**Why it happens:** `sws_getContext` allocates internal scratch buffers sized for the
declared src/dst dims. Feeding it a frame with different src dims is undefined behavior
(per libswscale source).

**How to avoid:**
1. Plan 21-02 decoder loop checks `frame->width != sws_src_width_ || frame->height != sws_src_height_`
   before each `sws_scale`. If mismatched, `sws_freeContext(old) + sws_getContext(new)`.
2. The recreate is on the decoder thread (not audio thread) — allocations are fine here.
3. The SINK juce::Image dst dims stay fixed (D-07) — `sws_scale` adapts. User sees
   slightly softer or sharper frames, no resize artifacts.
4. Verify: scenario `13_sps_pps_mid_stream` (Plan 21-02) — feeds mid-stream SPS/PPS
   that signals a resolution change; assert decoder thread doesn't crash and sink dims
   are unchanged.

**Warning signs:** Decoder thread crash on the first frame after a peer toggles
quality preset. Or visual: misalignment / corruption in the rendered tile after
preset change.

### Pitfall 8: libavcodec decoder context destruction while packet in flight

**What goes wrong:** VideoRecvState destruction (peer leaves) joins the decoder thread,
then calls `avcodec_free_context`. If the join doesn't actually wait for the loop body
to finish (e.g., a stale packet pointer is in flight on a worker thread that libavcodec
spawned internally for thread_count > 1), free_context could race with internal worker.

**Why it happens:** libavcodec's H.264 decoder, when `thread_count > 1`, spawns internal
worker threads. `avcodec_free_context` is supposed to join those — but only if the main
thread is also not in flight inside `avcodec_send_packet` / `avcodec_receive_frame`.

**How to avoid:**
1. CONTEXT.md Claude's Discretion locks `thread_count = 1` by default (per Phase 20
   symmetry). At thread_count=1, there are no libavcodec internal workers — the
   teardown is straightforward.
2. ~Openh264Decoder() ordering: (a) `signalThreadShouldExit()`, (b) signal SPSC
   event so decoder thread wakes from wait, (c) `stopThread(timeout)` to join,
   (d) `avcodec_send_packet(ctx, nullptr)` flush, (e) drain `avcodec_receive_frame`
   loop, (f) `av_frame_free`, `av_packet_free`, (g) `sws_freeContext`, (h)
   `avcodec_free_context`. R4 H9 LOCKED 7-step ordering from Phase 20's encoder is
   the symmetric template.
3. Plan 21-03 test scenario: a synthetic peer-leave during active decode — verify no
   crash, no use-after-free under ASan + TSan.

**Warning signs:** Crash on peer leave under sustained broadcast. ASan flags
use-after-free inside libavcodec.

## Code Examples

Verified patterns from official sources and existing JamWide patterns:

### Example 1: VideoRecvBuffer ctor with 4 MB pre-allocation (D-11)

```cpp
// Source: upstream ninjamzap-core/njclient.h:362 (ctor body) + JamWide D-11 extension
VideoRecvBuffer::VideoRecvBuffer()
  : frameCount(0), fourcc(0), chidx(0), interval_seq(-1),
    active(false), pending_remaining(0), sender_seq(-1) {
  username[0] = 0;
  memset(guid, 0, 16);
  memset(audio_guid, 0, 16);
  // D-11: pre-allocate 4 MB so WRITE-handler accumulation is realloc-free.
  data.Resize(4 * 1024 * 1024, true);  // reserve+commit
  data.Resize(0, false);                // logical size 0; capacity stays
}
```

### Example 2: AVCC parser producing NalChunks

```cpp
// Run thread (or audio thread inside SWAP, both envelope-accepted under D-16).
// Walks the playing slot's bytes per the wire-format spec:
//   24B marker chunk → discard (already consumed during accumulating)
//   SPS/PPS chunk    → [2B BE sps_len][SPS_raw][2B BE pps_len][PPS_raw]
//                       → emit two ParamSet NalChunks (Annex-B wrapped)
//   Per-frame chunk  → raw NAL bytes (no inner length prefix; outer 4B BE
//                       length is stripped by the chunker, NOT the NAL itself)
//                       → emit Frame NalChunk (Annex-B wrapped)
void parsePlayingSlotAndEnqueue(VideoRecvState& vs) {
  const unsigned char* p = (const unsigned char*)vs.playing.data.Get();
  const int* offsets = vs.playing.frameOffsets.Get();
  const int  count   = vs.playing.frameCount;
  if (count < 1) return;

  for (int i = 0; i < count; i++) {
    int frame_start = offsets[i];
    int frame_end   = (i + 1 < count) ? offsets[i + 1] : vs.playing.data.GetSize();
    int frame_size  = frame_end - frame_start;
    const unsigned char* frame = p + frame_start;

    // Frame 0 of accumulating = 24B marker, already parsed during WRITE handler.
    // Frames after that are SPS/PPS chunk (frame i=1 if SPS/PPS present)
    // or per-frame NAL chunks.
    if (i == 0 && frame_size == 24) continue;  // marker; already consumed

    if (looksLikeSpsPpsChunk(frame, frame_size)) {
      // [2B BE sps_len][SPS][2B BE pps_len][PPS]
      uint16_t sps_len = ((uint16_t)frame[0] << 8) | frame[1];
      const unsigned char* sps_raw = frame + 2;
      uint16_t pps_len = ((uint16_t)frame[2 + sps_len] << 8) | frame[3 + sps_len];
      const unsigned char* pps_raw = frame + 4 + sps_len;

      // D-13: Annex-B wrap (00 00 00 01 + raw NAL)
      NalChunk sps;
      sps.kind = NalChunk::ParamSet;
      sps.bytes.reserve(4 + sps_len);
      static const unsigned char start_code[4] = {0, 0, 0, 1};
      sps.bytes.insert(sps.bytes.end(), start_code, start_code + 4);
      sps.bytes.insert(sps.bytes.end(), sps_raw, sps_raw + sps_len);
      vs.decoderInputQ.try_push(std::move(sps));

      NalChunk pps;
      pps.kind = NalChunk::ParamSet;
      pps.bytes.reserve(4 + pps_len);
      pps.bytes.insert(pps.bytes.end(), start_code, start_code + 4);
      pps.bytes.insert(pps.bytes.end(), pps_raw, pps_raw + pps_len);
      vs.decoderInputQ.try_push(std::move(pps));
    } else {
      // Per-frame NAL chunk
      NalChunk fr;
      fr.kind = NalChunk::Frame;
      fr.bytes.reserve(4 + frame_size);
      static const unsigned char start_code[4] = {0, 0, 0, 1};
      fr.bytes.insert(fr.bytes.end(), start_code, start_code + 4);
      fr.bytes.insert(fr.bytes.end(), frame, frame + frame_size);
      vs.decoderInputQ.try_push(std::move(fr));
    }
  }
}
```

### Example 3: Decoder context open (Plan 21-02)

```cpp
// Source: ffmpeg.org doxygen + Phase 20 Openh264Encoder.cpp (symmetric template)
bool Openh264Decoder::open(int dst_width, int dst_height) {
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) return false;

  codecContext_ = avcodec_alloc_context3(codec);
  if (!codecContext_) return false;

  // Single-threaded decode (CONTEXT.md Claude's Discretion default).
  codecContext_->thread_count = 1;
  codecContext_->thread_type  = 0;

  // No extradata (D-13) — SPS/PPS come through as NALs on the wire.
  codecContext_->extradata      = nullptr;
  codecContext_->extradata_size = 0;

  // Lower latency: AV_CODEC_FLAG_LOW_DELAY hints "no output reordering"
  // (matches Phase 20 D-05 baseline profile = no B-frames anyway).
  codecContext_->flags |= AV_CODEC_FLAG_LOW_DELAY;

  int rc = avcodec_open2(codecContext_, codec, nullptr);
  if (rc < 0) {
    avcodec_free_context(&codecContext_);
    return false;
  }

  frame_  = av_frame_alloc();
  packet_ = av_packet_alloc();
  dst_width_  = dst_width;
  dst_height_ = dst_height;
  // sws_ allocated lazily on first received frame (D-07 size handling).

  startThread(juce::Thread::Priority::high);
  return true;
}
```

### Example 4: PeerVideoSink subscribe / handleAsyncUpdate (Phase 19 symmetric)

```cpp
// Source: Phase 19 JamWideFrameDistributor pattern adapted for the inverse direction.
JamWideRemoteFrameDistributor::Subscription
JamWideRemoteFrameDistributor::subscribeToPeer(const std::string& username,
                                                int chidx,
                                                std::function<void()> onRepaint) {
  const juce::ScopedLock lock(mu_);
  std::string key = makeKey_(username, chidx);
  auto it = sinks_.find(key);
  if (it == sinks_.end()) {
    // Sink not yet created — the lazy lifecycle hasn't fired (peer hasn't
    // broadcast video yet). Insert a placeholder; the run-thread BEGIN
    // handler will populate it when the peer actually starts.
    sinks_[key] = nullptr;  // placeholder
    it = sinks_.find(key);
  }
  std::uint64_t id = nextId_++;
  if (it->second)  // sink already exists
    it->second->addListener(id, std::move(onRepaint));
  else            // sink will be created later; we have to defer the listener-add
    deferred_listeners_[key].emplace_back(id, std::move(onRepaint));
  return Subscription(this, std::move(key), id);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `avcodec_decode_video2` (deprecated API) | `avcodec_send_packet` + `avcodec_receive_frame` state machine | FFmpeg 3.1 (2016) | Modern API supports out-of-order packet submission and clean draining at EOS. Phase 21 uses the modern API. |
| `extradata` for SPS/PPS | Annex-B in-band (NAL unit_type 7/8 on `avcodec_send_packet` path) | Has always been supported; we prefer it for mid-stream updates | D-13 — extradata can't be changed mid-stream cleanly; Annex-B handles peer preset changes by construction. |
| Hand-rolled YUV→RGB SIMD | `libswscale sws_scale` | libswscale has been standard since FFmpeg 0.5 | Phase 20 already adopted libswscale for the encoder side. Phase 21 uses it on the decoder side symmetrically. |
| Atomic-pointer-swap for cross-thread image publish (Phase 20 R0 draft) | Brief CriticalSection around image-pointer swap + atomic generation for lock-free read (Phase 20 R2) | Mid-Phase-20 codex review surfaced UAF window in atomic-pointer-swap | Phase 21 uses the same pattern (D-08): brief mutex for the swap, atomic for the freshness signal. |

**Deprecated/outdated:**
- `avcodec_decode_video2`: do not use; replaced by send/receive split.
- `juce::Thread::startThread(int priority)`: removed in JUCE 7.0.10+; use `juce::Thread::Priority` enum.
- `WDL_Mutex` on the audio thread WITHOUT a documented carve-out: would be Phase 15.1
  CRITICAL by default. Phase 21 only takes `m_video_recv_cs` on the audio thread under
  the D-16 audit-allowlist envelope.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | libavcodec's built-in H.264 decoder (NOT a libopenh264 decoder wrapper) is the `avcodec_find_decoder(AV_CODEC_ID_H264)` return — based on training knowledge of ffmpeg's codec registry, NOT verified against the vendored ffmpeg's specific build flags. | Standard Stack alternatives | Low. Even if the path went through libopenh264's decoder, the API surface is identical (`avcodec_send_packet` etc.). Could be slightly slower or have different error-recovery semantics. Verify at Plan 21-02 with `codec->name` after `avcodec_find_decoder`. |
| A2 | `juce::Thread::Priority::High` is appropriate for decoder thread on macOS Sonoma+ (not Realtime, not Default). | Common Pitfalls #4 | Medium. If High demotes to efficiency cores under load, decoder cadence drops. Verify at Plan 21-03 UAT under 3-peer HD load. Fallback: use audio workgroup APIs (post-v1.3). |
| A3 | `WDL_HeapBuf::Resize(sz, true)` reserves+commits 4 MB at ctor; subsequent `Resize(smaller, false)` does not shrink the allocation. | Pattern 1 ctor / Pitfall 6 | Medium. If Resize shrinks-on-smaller, the WRITE handler reallocs on every accumulator change, breaking D-11 RT-clean gate. Verify in Plan 21-01 Wave 0 task with a unit test of WDL_HeapBuf::Resize behavior. |
| A4 | libavcodec's H.264 decoder accepts SPS/PPS as standalone Annex-B NALs (unit_type 7 / 8) mid-stream and updates internal state without an explicit reset call. | D-13 / Anti-patterns | Low — verified by web search of ffmpeg-bitstream-filters docs (handling of NAL type 5 with prepended SPS/PPS in Annex-B). But not directly tested in this codebase yet. Verify at Plan 21-02 with scenario `13_sps_pps_mid_stream`. |
| A5 | `sws_freeContext + sws_getContext` is allocation-free in the sense of "no thread-pool churn or codec re-init storm" — only the SwsContext's own internal scratch buffers. | Pitfall 7 | Low. libswscale's documented behavior. Verify at Plan 21-02 unit test that recreates SwsContext 100x in a tight loop and asserts no leaks (Valgrind / ASan). |
| A6 | `AV_CODEC_FLAG_LOW_DELAY` is honored by libavcodec's H.264 decoder for our use case (Baseline profile, no B-frames). | Example 3 | Low. The flag is a hint; for Baseline profile it should be a no-op since there's no reordering. Verify at Plan 21-02 by measuring first-frame latency. |
| A7 | macOS Sonoma's TCC has no decoder-specific permission — only camera (granted in Phase 19). Decoder runs in user-mode like any audio decoder. | (implicit throughout) | Low. Verified by inspection — TCC does not gate libavcodec / libswscale calls. |
| A8 | JUCE 7's `juce::Image::ARGB` constructor with `clearImage=true` produces a fully transparent (alpha=0) image that paint() will render as empty until the first frame. | PeerVideoSink ctor in Pattern 3 | Low. Per JUCE docs. Verify at Plan 21-03 test that the placeholder tile chrome paints correctly before first frame. |

**Note:** Many "facts" in this research about NinjamZap upstream are NOT assumptions —
they were verified by direct file:line read against `ninjamzap-core/njclient.h` and
`ninjamzap-core/njclient.cpp`. The `[VERIFIED: ninjamzap-core/njclient.cpp:NNNN]` tags
throughout are real source references. The 8 assumptions above are the libavcodec /
JUCE / WDL behavioral claims that are based on web search or training data.

## Open Questions (RESOLVED)

1. **Where exactly does AVCC parsing live — run thread or audio thread?**
   - What we know: CONTEXT.md D-12 says "run thread"; D-16 envelope ALLOWS audio-thread
     allocations inside `m_video_recv_cs`. Both are technically valid per the locked
     envelope.
   - What's unclear: which produces a cleaner seam — having the audio thread
     (already inside the SWAP critical section, already allowed to allocate per D-16)
     parse + push, OR pushing a single `NalChunk{SlotReady, raw_bytes}` and letting
     decoder thread parse?
   - Recommendation: **Plan 21-01 implements audio-thread parsing** — the audio thread
     already has the slot bytes in hand after copyFrom, and a single AVCC walk
     is ~10 µs of work (within the existing D-16 envelope). This matches NinjamZap-
     literal more closely (upstream's web viewer probably parses on the receive side,
     not in a separate thread). The decoder thread is left as a pure libavcodec consumer.
     Document the audio-thread AVCC walk explicitly in the audit-allowlist envelope.
   - **RESOLVED:** AVCC parsing lives on the DECODER thread per CONTEXT.md D-12 revised + B-1 (see `21-CONTEXT.md` L55). The audio thread only memcpys the 0–4 MB slot bytes into a `VideoRecvState`-owned `VideoRecvSlotSnapshot` and pushes an integer index into `SpscRing<int, 4>` — no parsing happens on the audio thread. The Architectural Responsibility Map at L87 is updated to reflect this in the same change. Original recommendation above (audio-thread parser) is **superseded**; this resolution overrides it and is what Plans 21-01 and 21-02 implement (codex Cluster 2 Option A redesign).

2. **Subscribe-before-peer-exists race in JamWideRemoteFrameDistributor.**
   - What we know: Phase 22 tile may instantiate before the corresponding peer has
     broadcast any video (so PeerVideoSink doesn't exist yet). D-05 keys by (username,
     chidx).
   - What's unclear: should `subscribeToPeer` create a placeholder sink eagerly, or
     defer the listener-add until the run-thread BEGIN handler creates the sink?
   - Recommendation: **Defer the listener-add.** Distributor maintains a
     `deferred_listeners_[key]` queue; when the run thread creates the PeerVideoSink
     (lazy lifecycle in D-10), it flushes any deferred listeners onto the sink's
     listener-vector. Eager-placeholder is simpler but wastes ~30 MB of pre-allocated
     juce::Image bits per peer who never broadcasts. Default to deferred.
   - **RESOLVED:** Defer the listener-add, per Plan 21-03's distributor design. Distributor maintains a `deferred_listeners_[key]` queue keyed by `"username:chidx"` (D-05); when the run thread lazily creates the PeerVideoSink on first H264 BEGIN (D-10), it flushes any deferred listeners onto the sink's listener-vector under the listener `juce::CriticalSection` (D-06). No eager placeholder sinks. Locked in `21-CONTEXT.md` Discretion section ("empty-peer reaping policy" + D-10 lazy lifecycle).

3. **Sink dimensions when the peer's first frame arrives — are they configurable?**
   - What we know: D-07 fixes sink dims at first-seen peer resolution. So if peer
     starts at Medium (640×480) and switches to High (1280×720), the sink stays at
     640×480 and sws_ downscales.
   - What's unclear: should the sink optionally upscale (so a Low-preset broadcaster
     who later switches to High doesn't visually degrade)?
   - Recommendation: **No upscaling**. Sink dims = first-seen dims. Phase 22 tiles
     scale the image at paint time anyway (`drawImage` with target rect). Upscaling
     in sws_ would waste decoder thread CPU.
   - **RESOLVED:** No sws_-side upscaling; sink dims fixed at first-seen peer resolution per CONTEXT.md D-07. Phase 22 tiles handle final display scaling at paint time via `juce::Graphics::drawImage(...)` with the target rect. Decoder thread's `sws_scale` only downscales (or 1:1 copies) to the fixed sink dims; no decoder CPU spent on upscaling.

4. **What does the audit-allowlist envelope for Phase 21 look like in detail?**
   - What we know: D-16 lists the carve-out categories.
   - What's unclear: exact file:line entries that Plan 21-01 will write into
     `.claude/agents/realtime-audio-reviewer.md`.
   - Recommendation: Plan 21-01 lands the entries with `<line-placeholder>` tokens
     symmetric to Phase 20-00's pattern, then Plan 21-01's last task re-grep-refreshes
     the actual line numbers (R4 M13 closure pattern from Phase 20).
   - **RESOLVED:** Plan 21-01 lands the carve-out entries (per CONTEXT.md D-16, revised to reflect D-12 revised: `m_video_recv_cs.Enter/Leave`, `WDL_PtrList<VideoRecvState>` iteration, scalar reads on VideoRecvBuffer, `VideoRecvBuffer::copyFrom`, **decoder-owned memcpy + `SpscRing<int, 4>::try_push` of a slot index** — replaces the earlier `SpscRing<NalChunk, 32>::try_push` envelope entry since NalChunks no longer cross the audio-thread boundary, `m_remoteuser_mirror[s].next_ds[0]->guid` access, **explicit non-coverage of parser work on the audio thread**, no `writeLog`). Implementation uses `<line-placeholder>` tokens written in Plan 21-01 Task N and re-grep-refreshed in Plan 21-01's last task (R4 M13 closure pattern from Phase 20).

5. **Is there a security consideration for accepting untrusted H.264 from peers?**
   - What we know: NINJAM servers are user-operated; a malicious peer could send
     malformed H.264 to crash libavcodec. The decode-error counter + drop-frame
     pattern (D-18) handles this at the JamWide layer.
   - What's unclear: is libavcodec's H.264 decoder hardened against malicious input?
     History: ffmpeg has had CVEs (CVE-2024-7272, etc.) in H.264 / HEVC decoders.
   - Recommendation: This is **not in scope for Phase 21** — Phase 21 inherits the
     vendored ffmpeg version's threat surface. Phase 24 (beta validation) should
     check the CVE list for ffmpeg 7.1.2 vs current; if a critical decoder CVE is
     unfixed, Phase 23 picks up a newer ffmpeg point release. Document as a Phase 24
     review item.
   - **RESOLVED:** Out of scope for v1.3 / Phase 21. Phase 21 inherits Phase 14.3-01's vendored ffmpeg 7.1.2 threat surface; the decode-error counter + drop-frame recovery (D-18) covers malformed/truncated NALs at the JamWide layer (recorded as threat T-21-10 in Plan 21-02's STRIDE register). Phase 24 (beta validation) reviews the ffmpeg 7.1.2 CVE list; if a critical H.264 decoder CVE is unfixed, Phase 23 picks up a newer point release. Tracked as a Phase 24 review item.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| libavcodec (vendored) | Plan 21-02 decoder | ✓ | 61.19.101 (= ffmpeg 7.1.2 era) | — |
| libswscale (vendored) | Plan 21-02 sws_scale | ✓ | 8.3.100 | — |
| libavutil (vendored) | Plan 21-02 av_frame_alloc etc. | ✓ | 59.39.100 | — |
| JUCE 7 (juce_video, juce_events, juce_gui_basics) | Plan 21-03 PeerVideoSink + tile integration | ✓ | JUCE 7.x (Phase 19 baseline) | — |
| C++17 compiler | All plans | ✓ | clang on macOS x86_64; same toolchain Phase 20 used | — |
| cmake 3.x + ninja | Build | ✓ | cmake 4.3.2, ninja 1.13.2 | — |
| NinjamZap test scenario harness (`tests/video-sync/harness/`) | Plan 21-02 / 21-03 scenario ports | ✓ (read-only reference at `/Users/cell/dev/ninjamzap-core/...`) | — | — |
| `video.ninjamzap.com:2049` public test server | Plan 21-03 UAT | ✓ (operational; used during Phase 20 closure) | — | self-host `ninjamzap-server` via Phase 24-deferred Docker Compose |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** none.

The Phase 21 implementation has zero new external dependencies — it consumes only what
Phase 14.3 already vendored + Phase 19 already provides + Phase 20 already validated.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | CTest + plain C++ test executables (existing pattern from Phase 14.3-03 `test_video_fourcc.cpp` and Phase 20 `test_video_state_machine.cpp` / `test_video_encoder.cpp`) |
| Config file | `CMakeLists.txt` `if(JAMWIDE_BUILD_TESTS)` block (existing pattern) |
| Quick run command | `./scripts/build.sh --tests test_video_recv_state test_video_decoder test_remote_frame_distributor` |
| Full suite command | `cd build-juce && ctest --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| COD-03 | One independent decoder per peer; YUV→BGRA via sws_scale; juce::Image delivery | unit + integration | `ctest -R video_decoder` | ❌ Wave 0 (test_video_decoder.cpp) |
| COD-03 | First-frame visible after IDR arrival | unit | `ctest -R video_decoder -V` (sub-test "first_frame_emits") | ❌ Wave 0 |
| COD-03 | Decode-error recovery without thread teardown | unit | `ctest -R video_decoder -V` (sub-test "corrupt_nal_recovers_on_next_idr") | ❌ Wave 0 |
| COD-03 | Mid-stream SPS/PPS update (peer preset change) | unit | `ctest -R video_decoder -V` (sub-test "sps_pps_mid_stream_reconfig") | ❌ Wave 0 |
| COD-03 | Resolution change handling (SwsContext recreate) | unit | `ctest -R video_decoder -V` (sub-test "source_resolution_change_no_crash") | ❌ Wave 0 |
| WIRE-02 | DS-match → 1-swap defer through pending | unit | `ctest -R video_recv_state -V` (sub-test "ds_match_defers_to_pending") | ❌ Wave 0 |
| WIRE-02 | PREV-match → immediate play (no defer) | unit | `ctest -R video_recv_state -V` (sub-test "prev_match_plays_immediately") | ❌ Wave 0 |
| WIRE-02 | No-match → HOLD, kHoldCapDrop=4 → drop_resync | unit | `ctest -R video_recv_state -V` (sub-test "no_match_holds_then_drops_at_4") | ❌ Wave 0 |
| WIRE-02 | STAGE-1 promote pending → playing | unit | `ctest -R video_recv_state -V` (sub-test "pending_promotes_to_playing_on_next_swap") | ❌ Wave 0 |
| WIRE-02 | Mid-download startPlaying (accumulating → next during WRITE) | unit | `ctest -R video_recv_state -V` (sub-test "mid_download_start_playing") | ❌ Wave 0 |
| WIRE-02 | User-leave clears prev_ds_guid + hold_count | unit | `ctest -R video_recv_state -V` (sub-test "user_leave_resets_video_sync_state") | ❌ Wave 0 |
| WIRE-02 | Multi-write reassembly with 4B BE length prefix | unit | `ctest -R video_recv_state -V` (sub-test "split_frame_reassembles_via_length_prefix") | ❌ Wave 0 |
| WIRE-02 | 24B marker parsing extracts audio_ch0_guid + sender_seq | unit | `ctest -R video_recv_state -V` (sub-test "marker_parse_extracts_guid_and_seq") | ❌ Wave 0 |
| WIRE-02 | Burst BEGIN discards stale `next` | unit | `ctest -R video_recv_state -V` (sub-test "burst_begin_discards_stale_next") | ❌ Wave 0 |
| WIRE-02 (3-peer parallel) | Per-peer isolation: one peer's decode error doesn't affect others | integration | `ctest -R video_sync_e2e -V` (sub-test "three_peers_isolated_decode_errors") | ❌ Wave 0 |
| Multi-tile (DISP-03 prep) | Two listeners on same peer both receive frame signals | unit | `ctest -R remote_frame_distributor -V` (sub-test "two_listeners_same_peer_both_called") | ❌ Wave 0 |
| Subscription lifetime (HIGH-2 mirror) | ~Subscription blocks in-flight callback | unit | `ctest -R remote_frame_distributor -V` (sub-test "subscription_dtor_blocks_in_flight") | ❌ Wave 0 |
| Success criterion 1 (no 1-interval-early) | 5+ min wall-clock continuous playback, video paints at same swap as audio | UAT (manual cell) | `tests/uat/phase-21-receive-uat-report.md` Cell 1 | manual (Plan 21-03) |
| Success criterion 2 (mid-session joiner ≤2 intervals) | Connect to live broadcaster mid-stream; first frame visible within 2 SWAPs of subscribing | UAT (manual cell) | Cell 2 | manual |
| Success criterion 3 (audio stops, video freezes after 4 holds, recovers) | Pause sender audio, observe receiver tile last-frame freezes after 1-2 swaps + 'syncing…' overlay at 2+; resume audio, observe clean rejoin | UAT (manual cell) | Cell 3 | manual |
| Success criterion 4 (3+ peers simultaneously, per-peer isolation) | 3 standalone JamWide instances broadcasting + receiving each other, ≥ 5 min, no audio glitches | UAT (manual cell) | Cell 4 | manual |

### Sampling Rate

- **Per task commit:** `./scripts/build.sh --tests test_video_recv_state` (Plan 21-01) or
  `test_video_decoder` (Plan 21-02) or `test_remote_frame_distributor` (Plan 21-03) —
  the test relevant to the task being committed
- **Per wave merge:** Full ctest suite — `cd build-juce && ctest --output-on-failure`
- **Phase gate:** Full ctest green + Plan 21-03 UAT cells 1-4 PASS before `/gsd:verify-work`

### Wave 0 Gaps

- [ ] `tests/test_video_recv_state.cpp` — covers WIRE-02 sub-scenarios via
  `DispatchTestServerDownloadIntervalBegin/Write/End` test surface (Phase 14.3-03
  pattern). Ports upstream scenarios 02, 03, 20, 22.
- [ ] `tests/test_video_decoder.cpp` — covers COD-03 sub-scenarios via direct
  `Openh264Decoder` API exercise with synthetic Annex-B fixtures (small H.264 IDR +
  SPS/PPS from a vendored test asset under `tests/fixtures/`). Ports upstream
  scenarios 13, 25.
- [ ] `tests/test_remote_frame_distributor.cpp` — covers Subscription RAII +
  multi-listener + atomic-generation semantics.
- [ ] `tests/test_video_sync_e2e.cpp` (optional) — ties Plan 21-01 + 21-02 + 21-03
  together in-process via the existing `DispatchTestServer*` substrate. Demonstrates
  success criterion 1 (wall-clock alignment) via instrumented swap+paint cycle counter.
- [ ] CMakeLists.txt — `add_executable` + `add_test` entries for each new test target.
  Mirror existing pattern from `test_video_fourcc` block.
- [ ] `tests/fixtures/sps_pps_baseline_320x240.bin` + `tests/fixtures/idr_baseline_320x240.bin`
  — small H.264 test assets (generated from a known-good openh264 encoder run; one-time
  fixture commit).

### Proof-by-Construction Architecture (per Success Criterion)

**Success criterion 1** ("video appears at same wall-clock moment as matching audio"):
- **Mechanism that GUARANTEES alignment:** The 1-swap defer through `pending` (D-15)
  is the protocol-level fix. DS-match never plays the video AT swap N — it stages it
  in `pending` and STAGE-1 promotes to `playing` at swap N+1. The audio for that
  interval is in the decoder pipeline NOW at swap N but becomes audible during
  [swap N+1, swap N+2] due to the audio output buffering (~1 swap). So video at
  swap N+1 + audio at [swap N+1, N+2] = aligned at the speaker.
- **Test that DETECTS misalignment:** `test_video_recv_state` sub-test
  "ds_match_defers_to_pending" asserts that after a DS-match swap, `next` is empty
  and `pending` contains the matched frames; the next swap promotes pending → playing.
  UAT Cell 1 detects drift by user-subjective listening + a counter that increments
  every time the video's `sender_seq` does NOT match the audio's `m_sync_interval_cnt - 1`.

**Success criterion 2** ("mid-session joiner ≤2 interval boundaries"):
- **Mechanism that GUARANTEES fast first-frame:** Phase 20 D-14 (IDR every interval)
  + Phase 21's "SPS/PPS in chunk #2 of every BEGIN with `m_video_spspps.GetSize() > 0`"
  (D-13). Receiver joining mid-stream sees the very next BEGIN with full SPS/PPS +
  IDR — decoder can decode the first interval visible to it.
- **Test that DETECTS slow first-frame:** UAT Cell 2 — connect a second JamWide
  instance to the same room while peer 1 is mid-broadcast; measure wall-clock from
  subscribe to first PeerVideoSink.first_frame_seen=true. Acceptance: ≤ 2 × NINJAM
  interval (≤ ~12 seconds at typical BPM/BPI).

**Success criterion 3** ("audio stops, video freezes after 4 holds, recovers cleanly"):
- **Mechanism that GUARANTEES graceful freeze:** kHoldCapDrop=4 (D-15) + last-frame
  freeze + 'syncing…' overlay at 2 holds (D-17). Decoder thread keeps last-good frame
  in `image_front` until next successful frame; sink's `hold_count` atomic ticks up
  per audio-thread swap with no-match.
- **Test that DETECTS bad recovery:** `test_video_recv_state` sub-test
  "no_match_holds_then_drops_at_4" asserts hold_count progresses 1→2→3→4, then
  `drop_resync_count++` fires and `next.reset()` + `synced=false`. UAT Cell 3
  exercises the full path: peer A pauses audio, peer B observes freeze + overlay;
  peer A resumes, peer B observes clean rejoin within 1 swap.

**Success criterion 4** ("3+ peers simultaneously, per-peer isolation"):
- **Mechanism that GUARANTEES isolation:** D-09 one-decoder-thread-per-peer +
  per-peer SpscRing + per-peer PeerVideoSink. No shared queue, no shared codec context,
  no shared sink. One peer's decode failure / slow CPU / corrupted stream affects
  ONLY that peer's tile.
- **Test that DETECTS isolation breach:** `test_video_sync_e2e` sub-test
  "three_peers_isolated_decode_errors" injects corrupt H.264 into peer 1's stream
  while peers 2 + 3 send valid streams; asserts peer 1's decode_error_count climbs
  while peers 2 + 3's stays at zero. UAT Cell 4 — 3-instance live session for ≥ 5 min.

## Security Domain

> Phase 21 receives untrusted H.264 video bytes from peers over the NINJAM TCP socket
> and feeds them into libavcodec's H.264 decoder. The implementation handles the
> standard libavcodec attack surface; specific JamWide-layer considerations below.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|------------------|
| V2 Authentication | no | NINJAM auth is handled at the wire layer (existing); Phase 21 sees post-auth peer data only |
| V3 Session Management | no | No new session state at this layer |
| V4 Access Control | no | All connected peers are authenticated to the same room; video is broadcast (no per-peer access checks) |
| V5 Input Validation | yes | 24B marker parsing, 4B BE length prefix parsing, AVCC walker, NAL unit parsing — all must reject malformed input gracefully (drop frame, bump counter, do not crash) |
| V6 Cryptography | no | No cryptographic operations at this layer |

### Known Threat Patterns for {libavcodec H.264 + GUID-pair sync stack}

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed H.264 NAL crashes libavcodec → process crash | Denial of Service | Catch return codes from `avcodec_send_packet` / `avcodec_receive_frame`; on AVERROR_INVALIDDATA, drop frame + bump counter + continue (D-18). libavcodec is hardened against malformed input but CVEs do appear — Phase 24 picks up ffmpeg security updates. |
| Adversarial peer sends huge interval payload to OOM the receiver | Denial of Service | D-11 per-slot 4 MB soft cap. On exceed: mark `accumulating.capped=true`, drop further WRITE bytes, bump `slot_drop_count`. No allocation in WRITE handler beyond the pre-allocated cap. |
| Adversarial peer crafts NALs that exploit a libavcodec CVE → RCE | Elevation of Privilege | Cannot fully mitigate at JamWide layer — depends on libavcodec maintenance. Mitigation: pin ffmpeg version + monitor ffmpeg CVEs + ship security updates via Phase 24+ release process. Defense-in-depth: macOS hardened runtime + Windows mitigation policies already constrain what the process can do. |
| Adversarial peer sends conflicting username:chidx values to confuse the distributor | Tampering | D-05 keys by `(username, chidx)` from the WRITE handler's `tracker->username` field, which comes from NJClient's authenticated peer identity (server-vouched). A peer cannot impersonate another peer's username at the NINJAM protocol level. Cross-channel collision (same peer broadcasts on chidx=1 AND chidx=2) just creates two sinks — not a security issue. |
| GUID collision (statistical) between video stream and audio stream | Tampering | Phase 14.3-03 `matched=true` flag already handles this — RawData GUID match short-circuits the audio download loop. WDL_RNG_bytes-random GUIDs make actual collision birthday probability ~2^-128. |
| Stack/heap exhaustion via NAL flood from one peer to starve other peers' decoders | Denial of Service | D-09 per-peer thread + per-peer SPSC ring isolates one peer's decoder thread from others. SpscRing capacity 32 + drop-oldest semantics bound memory. |
| Use-after-free in PeerVideoSink during peer leave with active subscribers | Information Disclosure / Crash | Phase 19 HIGH-2 Subscription RAII pattern verbatim — `~Subscription` blocks until in-flight `handleAsyncUpdate` callback returns. NJClient's run-thread `removeVideoStream` joins the decoder thread before `removeSink` from distributor. |
| Use-after-free of `m_remoteuser_mirror[s].next_ds[0]` during GUID-pair read | Crash | Phase 15.1-07a HIGH-2 carve-out already covers this — DecodeState ownership crosses threads via SPSC handoff with deferred-delete. Audit-allowlist envelope explicitly accepts the read site. |
| Resource exhaustion via N peers × 4 stages × 4 MB pre-allocated slot = 64 MB per peer | Denial of Service | At realistic populated NINJAM sizes (≤ 8 peers), total memory cost = 8 × 64 MB = 512 MB pre-allocated. This is bounded and predictable — not unbounded growth. Open-room max users (server-side cap) bounds this. Documented in Common Pitfalls / Q11 STATE blocker. |

## Sources

### Primary (HIGH confidence)

- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:334-417` — `VideoRecvBuffer` + `VideoRecvState` struct definitions
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:1300-1325` — user-leave video state reset
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:1344-1437` — BEGIN handler (RawData_Callback dispatch + accumulating slot setup)
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:1439-1592` — WRITE/END handler (per-frame accumulation + 24B marker parse + mid-download startPlaying + END → next move)
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2124-2173` — `findVideoStream` / `findOrCreateVideoStream` / `findVideoStreamByGUID` / `removeVideoStream` helpers
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3084-3260` — audio-thread `on_new_interval` video receive block (STAGE 1 promote + GUID-pair tree + kHoldCapDrop=4 logic)
- `/Users/cell/dev/ninjamzap-core/tests/video-sync/harness/TestClient.cpp:120-176` — byte-exact wire reference for `sendVideoFrame` / `sendFakeSPSPPS` framing
- `/Users/cell/dev/ninjamzap-core/tests/video-sync/scenarios/` (26 scenarios) — canonical sync test cases
- `/Users/cell/dev/JamWide/libs/ffmpeg/macos-x86_64/include/libavcodec/avcodec.h:95-249, 2504-2547` — modern `avcodec_send_packet` / `avcodec_receive_frame` API docs + state machine semantics (in-tree header)
- `/Users/cell/dev/JamWide/.planning/phases/14.3-native-video-foundation/14.3-03-SUMMARY.md` — RawData_Callback dispatch substrate (the substrate Phase 21 plugs into)
- `/Users/cell/dev/JamWide/.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md` — symmetric send-side pattern; D-08/D-09/D-13/D-14/D-15/D-19 carve-out templates
- `/Users/cell/dev/JamWide/.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md` — HIGH-2 Subscription pattern + HIGH-4 AsyncUpdater pattern
- `/Users/cell/dev/JamWide/.claude/agents/realtime-audio-reviewer.md:164-244` — Phase 20 audit-allowlist envelope template (Phase 21 D-16 mirror target)

### Secondary (MEDIUM confidence)

- [FFmpeg send/receive encoding and decoding API overview](https://ffmpeg.org/doxygen/3.3/group__lavc__encdec.html) — state machine semantics for `avcodec_send_packet` / `avcodec_receive_frame`
- [FFmpeg avcodec_receive_frame Returns -11: Complete Guide to Fixing AVERROR_EAGAIN](https://copyprogramming.com/howto/ffmpeg-avcodec-receive-frame-returns-11-why-and-how-to-solve-it) — EAGAIN recovery pattern
- [FFmpeg bitstream filters documentation](https://ffmpeg.org/ffmpeg-bitstream-filters.html) — Annex-B vs MP4-format framing; SPS/PPS handling
- [JUCE 7 Thread Priority API breaking changes](https://github.com/juce-framework/JUCE/blob/master/BREAKING_CHANGES.md) — `juce::Thread::setPriority` integer API removed; new `Priority` enum
- [JUCE Forum: Mac M1 thread priority & Audio Workgroups](https://forum.juce.com/t/mac-m1-thread-priority-audio-workgroups/52188) — macOS QoS scheduling interaction
- [FFmpeg multithreading for H.264 decoding](https://libav-user.ffmpeg.narkive.com/PhkgWe5D/ffmpeg-decoding-h264-and-multithreading) — `thread_count` + `thread_type` semantics

### Tertiary (LOW confidence)

- [Windows DLL search path documentation](https://stefanoborini.com/windows-dll-search-path/) — DLL resolution order on Windows (referenced for Pitfall #3)
- [FFmpeg.Loader best-practice DLL loading](https://github.com/lethek/FFmpeg.Loader) — referenced for context on Windows loader paths

## Metadata

**Confidence breakdown:**
- 4-stage pipeline + GUID-pair tree: **HIGH** — verbatim port of source-verified upstream
- Decoder thread + libavcodec wrapping: **HIGH** — modern API documented in in-tree header
- PeerVideoSink + atomic-generation pattern: **HIGH** — Phase 19 verbatim
- Sub-millisecond audio-thread budget under 8-peer HD copyFrom: **MEDIUM** — needs Plan 21-03 measurement
- macOS QoS scheduling impact on decoder thread under load: **MEDIUM** — JUCE forum reports it works, but no first-party JamWide measurement yet
- Windows DLL resolution for the receive-side path: **MEDIUM** — Phase 20 already validated the load path; Phase 21 inherits it (no new dlopen)
- libavcodec H.264 decoder CVE surface in ffmpeg 7.1.2: **LOW** — depends on Phase 24's pre-beta security review

**Research date:** 2026-05-17
**Valid until:** ~2026-06-15 (30 days for stable substrate; refresh if libavcodec API or
upstream NinjamZap reference changes substantially)
