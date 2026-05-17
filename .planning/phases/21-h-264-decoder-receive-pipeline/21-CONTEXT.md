# Phase 21: H.264 Decoder & Receive Pipeline - Context

**Gathered:** 2026-05-17
**Status:** Ready for planning

<domain>
## Phase Boundary

Remote peers' H.264 video bytes arriving over the NinjamZap-compatible wire are reassembled, decoded, and rendered into per-peer `juce::Image`s ready for Phase 22's grid + popouts. Symmetric inverse of Phase 20's send pipeline. This phase covers:

1. **Per-peer 4-stage receive pipeline** (`accumulating → next → pending → playing`) verbatim ported from upstream `ninjamzap-core`. Run thread accumulates incoming WRITE chunks; audio thread runs the SWAP + GUID-pair decision at every `on_new_interval` boundary; decoder thread runs libavcodec off-thread.
2. **GUID-pairing decision tree** matching the marker chunk's embedded `audio_ch0_guid` against the audio DecodeState chain (DS-match → 1-swap defer to `pending`, PREV-match → immediate play, no-match → HOLD with `kHoldCapDrop = 4` resync). Read against Phase 15.1-07a `m_remoteuser_mirror[s].next_ds[0]->guid` which is already audio-thread-accessible per the existing HIGH-2 carve-out.
3. **Per-peer libavcodec H.264 decoder** running on its own `juce::Thread`. Consumer of a per-peer `SpscRing<VideoRecvBufferView, 4>` slot-view queue populated by the audio thread; the decoder thread parses the slot bytes in-thread (per D-12 revised) and feeds libavcodec. YUV420P → BGRA via `libswscale sws_scale` into the peer's distributor sink.
4. **`JamWideRemoteFrameDistributor`** — symmetric inverse of Phase 19's `JamWideFrameDistributor`. Owns `unordered_map<key=(username,chidx), PeerVideoSink>`. Phase 22 tiles subscribe via RAII Subscription handle. Supports multiple simultaneous subscribers per peer (DISP-03: grid tile + popout active at the same time on the same peer).
5. **`PeerVideoSink`** with double-buffered `juce::Image` (front/back) under brief `juce::CriticalSection`, atomic generation counter for lock-free UI snapshot, per-peer `juce::AsyncUpdater` for UI signal, and atomic status fields (`hold_count`, `decode_error_count`, `drop_resync_count`, `synced`, `first_frame_seen`) read lock-free by Phase 22.
6. **Lazy lifecycle** — VideoRecvState + decoder thread + sink all spin up on first H264 BEGIN observed for a peer (via Phase 14.3-03's RawData_Callback dispatch substrate); tear down on peer leave (NJClient `m_remoteusers` deletion). Zero cost for peers who never broadcast video. Symmetric to Phase 20 D-13.
7. **Bounded per-peer memory** — each VideoRecvBuffer slot pre-allocates 4 MB of `WDL_HeapBuf`; on cap exceedance, mid-interval WRITEs are dropped, `slot_drop_count` is bumped, and the pipeline awaits the next BEGIN. Decoder errors drop the offending frame, bump `decode_error_count`, and let libavcodec auto-recover at the next IDR (which arrives within ≤1 NINJAM interval per Phase 20 D-14).
8. **First-frame UX + hold-state UX** — Phase 22 tiles render their chrome with a `'video starting…'` overlay until `first_frame_seen` flips; render `'syncing…'` overlay once `hold_count ≥ 2`. Last-decoded frame stays frozen on the tile during hold periods. No black tile, no aggressive dimming.

**Maps to:** Requirements **COD-03** (per-peer libavcodec decode to `juce::Image`), **WIRE-02** (GUID-pairing decision tree with `kHoldCapDrop = 4`).

**Out of scope:** UI tile component + grid layout + popout `juce::DocumentWindow` (Phase 22), macOS universal + Windows packaging + per-dylib codesigning (Phase 23), per-DAW UAT + NinjamZap scenario port + beta validation (Phase 24). Phase 21 does NOT implement adaptive bitrate / quality fallback under network pressure (deferred to v1.4+). Phase 21 does NOT add a VideoToolbox/MediaFoundation decode backend (architected via the `VideoDecoder` interface, deferred).

</domain>

<decisions>
## Implementation Decisions

### UI Delivery (Phase 22 ingestion contract)

- **D-01: Atomic-snapshot `juce::Image` per peer; latest-frame-wins; lock-free read.** Each peer's `PeerVideoSink` owns a stable `juce::Image` member; decoder writes pixels then bumps an atomic generation counter. UI reads the latest snapshot under a brief `juce::CriticalSection` (microseconds; only swaps the ref-counted shared pointer, no pixel copy). Latest-wins is the correct semantics for live video — no point queueing stale frames behind a freshly-decoded one. Reuses Phase 19 HIGH-4's atomic-generation pattern that solved the preview-tile race.

- **D-02: Per-peer `juce::AsyncUpdater` for "new frame ready" UI signal.** After bumping `generation`, decoder calls `sink->triggerAsyncUpdate()`. AsyncUpdater coalesces (multiple triggers between paints become one) and dispatches on the message thread. Zero idle CPU when no peers broadcasting (vs the Timer-based alternative). Phase 19 HIGH-4 verbatim.

- **D-03: New `JamWideRemoteFrameDistributor` service; symmetric inverse of Phase 19's `JamWideFrameDistributor`.** Owns the per-peer sink map and the lifecycle. `Subscription` RAII handle returned from `subscribeToPeer(username, chidx, onRepaint)` — Phase 19 HIGH-2 pattern verbatim, with the receive-side direction inverted. `~Subscription` detaches the callback so the decoder never calls into a dead Phase 22 tile.

- **D-04: BGRA pixel format via `libswscale` YUV420P→BGRA.** Sink's `juce::Image` format = `juce::Image::ARGB` (which JUCE backs with BGRA on macOS/Windows). Matches Phase 19's capture-side pixel layout symmetrically — encoder pipeline and decoder pipeline bracket the same JUCE pixel layout. `sws_scale` BGRA path is the most-optimized libswscale conversion in the vendored ffmpeg build.

- **D-05: Distributor keyed by `(username, chidx)` string, NOT useridx.** Sink map is `unordered_map<std::string, std::unique_ptr<PeerVideoSink>>` with key `"username:chidx"`. Stable across NJClient's user-leave roster shifts (where `useridx` is volatile). Phase 22 tile resolves username via `m_remoteusers[useridx]->name` at bind time and calls `distributor->subscribeToPeer(name, 1, ...)`. NJClient calls `distributor->removePeer(name, 1)` on `m_remoteusers` deletion. Matches upstream's `findVideoStream(username, chidx)` keying.

- **D-06: Multiple simultaneous subscribers per peer (DISP-03 support).** PeerVideoSink owns a `std::vector<std::function<void()>>` of registered onRepaint callbacks under a `juce::CriticalSection`. `subscribeToPeer` appends and returns a Subscription wrapping a stable handle; `~Subscription` removes the callback. `handleAsyncUpdate` iterates all callbacks. Grid tile + popout for the same peer each call subscribe independently and close independently. Same juce-style listener pattern as Phase 19's `JamWideFrameDistributor`.

- **D-07: Sink `juce::Image` fixed at first-seen peer resolution; sws_ adapts to keep image size stable across mid-session peer preset changes.** When a peer toggles Low→High preset mid-session, decoder detects the source resolution change and lazy-recreates `SwsContext` to scale from the new source dimensions to the existing fixed sink dimensions. Single image allocation per peer; no atomic-swap-with-tearing concerns on UI re-layout. User-visible behavior: peer preset change is invisible — they just see slightly sharper or softer frames depending on direction.

- **D-08: Double-buffered front/back `juce::Image` swapped under brief `CriticalSection` for tearing protection.** Decoder writes into `image_back`; on completion takes a microsecond-scoped `juce::ScopedLock` on `bufferLock`, `std::swap`s the two ref-counted `juce::Image`s (O(1) — just bumps internal refs), bumps generation, releases lock, fires AsyncUpdater. UI takes the same lock briefly to snapshot `image_front` under ref-bump. Costs 2× image memory per peer (~7 MB at HD) and trades it for zero visible tearing. Compatible with the lock-free generation atomic — UI's lock-free generation read precedes the brief ref-bump under bufferLock.

### Decoder Thread Model + Per-Peer Memory

- **D-09: One libavcodec decoder thread per peer (`juce::Thread`).** Each `VideoRecvState` owns its own decoder thread + AVCodecContext + SwsContext + per-peer `SpscRing<VideoRecvBufferView, 4>` slot-view queue populated by the audio thread (per D-12 revised — parser is on the decoder thread, NOT on the audio/run thread). Isolation: one peer's slow / corrupted decode doesn't stall any other peer or the audio thread. Symmetric to Phase 20's encoder threading (one thread per encoder instance). At 8 peers = 8 decoder threads, fine on modern desktop. Decoder thread loops: pop VideoRecvBufferView → parseSlotAndFeed_ (walks slot bytes, Annex-B wraps each NAL) → `avcodec_send_packet` → `avcodec_receive_frame` loop → `sws_scale` into `image_back` → swap front/back → bump generation → triggerAsyncUpdate.

- **D-10: Lazy decoder lifecycle — spin-up on first H264 BEGIN per peer; tear-down on peer leave.** The RawData_Callback BEGIN dispatch (Phase 14.3-03) is the moment NJClient gets `(username, chidx, fourcc=H264, guid)`. On first such event for a peer: create `VideoRecvState` + AVCodecContext + SwsContext + PeerVideoSink, spawn decoder thread, register sink with distributor. Tear-down: when NJClient observes the user dropping from `m_remoteusers`, destroy the matching `VideoRecvState` (joins decoder thread + frees codec context + removes sink from distributor — subscribed tiles see `~Subscription` detach). Zero cost for peers who never broadcast video. Symmetric to Phase 20 D-13 ("encoder starts at broadcast-on, not at camera-open").

- **D-11: Per-peer per-slot soft cap of 4 MB; on exceed drop rest-of-interval.** Each of the 4 `VideoRecvBuffer` slots pre-allocates a `WDL_HeapBuf` of 4 MB on creation (5× the expected ~800 KB HD IDR ceiling per Q11 STATE blocker). When `accumulating.data.size + new_bytes > 4 MB`, mark `accumulating.capped = true`, drop further WRITE bytes for this GUID, bump `slot_drop_count.fetch_add(1)` counter. Decoder skips the capped interval; pipeline advances normally on the next BEGIN. **No allocation in the WRITE handler** — pre-allocation at slot creation is critical for this gate to be RT-clean.

- **D-12: AVCC parsing on decoder thread (not audio thread or run thread); decoder thread parses then consumes via libavcodec.** [Revised 2026-05-17 per checker B-1 — original wording specified "run thread"; the practical contract is that the parser lives where the libavcodec consumer lives, which keeps wire-format and codec-API responsibilities co-located on a single non-realtime thread.] The audio thread's `on_new_interval` SWAP block (D-15) populates `vs->playing` and then hands a stable view of the playing-slot bytes (via decoder-owned memcpy at push time — `Openh264Decoder::pushSlotView` memcpys the slot bytes into a 4 MB pre-allocated decoder-owned buffer before returning, so the SPSC `SpscRing<VideoRecvBufferView, 4>` carries a view into the decoder's own buffer — Plan 21-02 Task 2 landed this decision) to the decoder thread via a per-peer SPSC. The decoder thread receives the entire slot bytes per pop and walks them in-thread: 24-byte marker discard → SPS/PPS chunk (`[4B BE outer_len][2B BE sps_len][SPS_no_start_code][2B BE pps_len][PPS_no_start_code]`) → per-frame AVCC chunks (`[4B BE nal_len][NAL]`). For each parsed NAL, wraps with Annex-B start code (`00 00 00 01`) and immediately feeds via `avcodec_send_packet`. Rationale: (a) keeps the audio thread minimal — only one SPSC push per swap inside `m_video_recv_cs`, honoring the D-09 carve-out budget; (b) upstream `ninjamzap-core` has no AVCC parser at all (the mobile iOS/Android app handles parsing on the device — for us the decoder thread is the natural owner because it is the libavcodec consumer anyway); (c) wire-format and codec-API responsibilities co-located simplifies the threading audit (one thread, one mutex-free SPSC pop, then pure libavcodec calls). Per `feedback_proven_over_pure`, this is the practical clarification of the original idealized "run thread" wording — the upstream behaviour is preserved (parse then send) while the JamWide threading boundary respects the Phase 15.1 envelope.

- **D-13: SPS/PPS fed as Annex-B packets via same code path as frame NALs (no avcC extradata).** Decoder thread parses the SPS/PPS chunk inner into `[SPS_NAL]` + `[PPS_NAL]`. Wraps each with Annex-B start code (`00 00 00 01`) and feeds via `avcodec_send_packet` directly (no intermediate SPSC since parsing and consuming happen on the same thread per D-12 revised). libavcodec recognizes NAL `unit_type 7` (SPS) and `8` (PPS) and updates internal state. No `AVCodecContext::extradata` setup; no `AV_CODEC_FLAG_GLOBAL_HEADER`. Same code path as frame NALs; handles mid-stream SPS/PPS updates (peer preset change) without special-casing.

### 4-Stage Pipeline Ownership + Phase 15.1-07a Mirror Interaction

- **D-14: Verbatim upstream pattern — `WDL_PtrList<VideoRecvState> m_video_streams` + `WDL_Mutex m_video_recv_cs` on NJClient.** Port `VideoRecvState` + `VideoRecvBuffer` struct definitions verbatim from `ninjamzap-core/njclient.h:334-417`. Helpers: `findVideoStream(username, chidx)`, `findOrCreateVideoStream(username, chidx)`, `findVideoStreamByGUID(guid)`. State is run-thread-and-audio-thread-shared (not audio-thread-exclusive); m_video_recv_cs synchronizes access. Audio thread never touches `m_remoteuser_mirror[].next_ds[0]` for write purposes — only reads `next_ds[0]->guid` for GUID-pair comparison (already accepted under Phase 15.1-07a HIGH-2 envelope).

- **D-15: Audio thread owns SWAP + GUID-pair decision tree under `m_video_recv_cs`.** Verbatim upstream `on_new_interval` video receive block: acquire `m_video_recv_cs`, iterate `m_video_streams`, run STAGE 1 promote (`pending → playing`), then for each VideoRecvState with `next.active && next.frameCount >= 1` run the GUID-pair decision (DS-match against `m_remoteuser_mirror[s].next_ds[0]->guid` → `next → pending`; PREV-match against `prev_ds_guid` → `next → playing` immediately; no-match → bump `hold_count`, drop `next.reset()` once `hold_count >= kHoldCapDrop = 4`). After the audio-thread SWAP populates the `playing` slot, push a stable VideoRecvBufferView (or memcpy into a decoder-owned buffer) onto the per-peer decoder SPSC. The decoder thread (D-12 revised) then walks the bytes and feeds libavcodec. Release `m_video_recv_cs`. The 1-swap defer through `pending` is the protocol-level fix for the "video 1 interval early" bug — keep this discipline.

- **D-16: Phase 21 audio-thread audit-allowlist envelope (parallel to Phase 20 D-09).** Plan 21-XX writes the following carve-out entries to `.claude/agents/realtime-audio-reviewer.md`:
  - `m_video_recv_cs.Enter/Leave` (held across the entire receive-side SWAP block in `on_new_interval`).
  - `WDL_PtrList<VideoRecvState>` iteration (`GetSize()` + `Get(i)`).
  - Scalar reads/writes on `VideoRecvBuffer` (`frameCount`, `active`, `audio_guid` memcmp, `sender_seq`, `data.GetSize()`, etc.).
  - `VideoRecvBuffer::copyFrom` (Resize + memcpy up to 4 MB per peer per interval — accepted as NinjamZap-literal; cost bounded at ~0.4 ms per peer at modern memory bandwidth).
  - Slot-view push onto the per-peer decoder SPSC via **decoder-owned memcpy at push time** (`Openh264Decoder::pushSlotView` memcpys the slot bytes into a 4 MB pre-allocated decoder-owned buffer; `SpscRing<VideoRecvBufferView, 4>::try_push` then carries a view into THAT buffer). Lock-free / RT-safe; replaces the previously envisioned `SpscRing<NalChunk, 32>::try_push` allowlist entry — the parsed NalChunks are now produced by the decoder thread per D-12 revised and never cross the audio-thread boundary.
  - `m_remoteuser_mirror[s].next_ds[0]->guid` access (already accepted under Phase 15.1-07a HIGH-2).
  - **No `writeLog` calls on the audio-thread receive path.** Plan 21-XX audits for any residual logs in the upstream-ported block and strips them (matching Phase 20 D-09's writeLog hygiene).

  Auditor's CRITICAL count must stay zero outside this envelope; envelope sites are explicitly accepted. Same shape as Phase 20 D-09 envelope.

### Hold/Error UX (Phase 22 surface)

- **D-17: kHoldCapDrop=4 UX — last-frame freeze + subtle 'syncing…' overlay after 2 holds.** First 1–2 consecutive holds: just freeze the last decoded frame (no overlay). Holds 3–4: PeerVideoSink's `hold_count` atomic crosses ≥2; Phase 22 tile reads this each repaint and overlays a subtle `'syncing…'` label in the tile corner. On forced resync (after the 4th hold), `drop_resync_count.fetch_add(1)` (diagnostic only); the next GUID-paired interval plays cleanly. No tile-clear, no jarring black. Matches user expectation 'looks like network is laggy' rather than 'something broke'.

- **D-18: Decoder-error recovery — drop frame + bump counter + continue.** When `avcodec_send_packet` or `avcodec_receive_frame` returns an error (corrupt NAL, missing reference, `AVERROR_INVALIDDATA`), decoder thread discards the offending packet, increments `decode_error_count.fetch_add(1)`, and continues processing the next NalChunk. IDR-per-interval (Phase 20 D-14) means libavcodec naturally auto-recovers at the next interval boundary (≤1 interval = a few seconds max). User sees a brief freeze on last-good-frame. No decoder teardown, no flush — bounded counter, no escalation. Same shape as Phase 20 D-07's drop-oldest backpressure pattern.

- **D-19: First-frame UX — tile chrome + 'video starting…' overlay until first decoded frame.** PeerVideoSink's `first_frame_seen` atomic flips false→true on the first successful `sws_scale` into `image_back`. Phase 22 tile reads this each repaint: if false, render the tile's frame/chrome (JamWide Voicemeeter Banana dark theme) with a centered `'video starting…'` label. After flip, transition to showing the decoded frame. Same pattern covers success criterion 2 ("video appears after at most 2 interval boundaries when mid-stream joining") and the brief gap between sink creation and the decoder's first output.

- **D-20: Atomic status fields on PeerVideoSink, paired with the image+generation lock-free contract.** Status fields: `std::atomic<int> hold_count` (0–4), `std::atomic<int> decode_error_count`, `std::atomic<int> drop_resync_count` (diagnostic), `std::atomic<bool> synced`, `std::atomic<bool> first_frame_seen`. Phase 22 tile reads all of these lock-free per repaint, same code path as the image-snapshot read. Status transitions are signaled via the same AsyncUpdater that signals new frames (no new threading model). Plays nicely with the latest-wins semantics: status is read once per paint cycle, never stale long.

### Claude's Discretion

- **Decoder thread priority** (`juce::Thread::setPriority` normal vs slightly elevated). Default to normal; the audio thread is already higher-priority and the decoder should not preempt audio.
- **libavcodec multi-threading** (`AVCodecContext::thread_count` per peer). Default to 1 (single-threaded decode) per Phase 20 D-Discretion symmetry. Profile under HD scale before enabling.
- **Audio→decoder handoff SPSC choice** — landed in Plan 21-02 Task 2: **decoder-owned memcpy at push time** (`Openh264Decoder::pushSlotView` memcpys the slot bytes into a 4 MB pre-allocated decoder-owned buffer; the SPSC `SpscRing<VideoRecvBufferView, 4>` carries a view into THAT buffer). RT-safe under the D-16 envelope; no cross-thread liveness contract beyond the SPSC itself.
- **`NalChunk` storage** — small inline buffer + heap fallback for large NALs, or always-heap. Decoder-thread-local; not crossing thread boundaries per D-12 revised so allocation strategy is purely a perf/clarity call.
- **File layout** — mirror Phase 20's `juce/video/encoder/` symmetrically as `juce/video/decoder/` containing `VideoDecoder.h` (interface), `Openh264Decoder.h/.cpp` (impl), and `juce/video/distributor/JamWideRemoteFrameDistributor.h/.cpp`.
- **Test scaffolding helpers** — gated under `JAMWIDE_BUILD_TESTS`, mirroring Phase 20 + 14.3-03's `DispatchTestServerDownloadIntervalBegin/Write` patterns. Planner picks coverage matrix from the 26 upstream sync scenarios (port the high-value subset: `02_video_one_interval_early`, `03_late_join`, `13_sps_pps_mid_stream`, `20_drop_resync_recovery`, `22_audio_then_video`, `25_no_initial_spspps`).
- **Empty-peer reaping policy** — if a peer goes silent (no H264 BEGIN for N intervals) without disconnecting, does the decoder + sink stay alive? Default: stay alive until peer leaves (no special reaping). Planner picks if memory pressure shows up in profiling.
- **Debug logging surface** — `juce::Logger::writeToLog` for decoder open/close, resolution changes, decoder errors. Default: log open/close + first decode error + every 100th decode error after that.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Wire-format spec (authoritative — Phase 20 validated byte-for-byte against the canonical web viewer)

- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-RESEARCH-ADDENDUM.md` — NinjamZap wire format spec section + "Receiver decision tree (4-stage pipeline + GUID matching)" — DS match defers 1 swap, PREV match plays immediately, no-match HOLDs with `kHoldCapDrop=4` resync.
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` — Item E.1 (port `JamTaba/src/Common/video/FFMpegDemuxer.cpp` — reference; we use libavcodec directly), Item F.2 (`VideoRecvBuffer`/`VideoRecvState` 4-stage pipeline), Item F.3 (GUID-pairing decision tree).

### NinjamZap reference (port from these — read source verbatim, NOT just the docs per `feedback_doc_vs_source_verification`)

- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:334-417` — `VideoRecvBuffer` + `VideoRecvState` struct definitions. Port verbatim. Includes `m_video_streams` WDL_PtrList + `m_video_recv_cs` mutex + helper signatures.
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:1300-1550` — receive-side WRITE handler (per-WRITE accumulation into `accumulating`, audio-guid extraction from marker, frameOffsets bookkeeping, sender_seq tracking).
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3084-3219` — audio-thread receive-side SWAP + GUID-pair decision tree (the on_new_interval video receive block). **This is the byte-exact reference for D-15.**
- `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2124-2173` — `findVideoStream` / `findOrCreateVideoStream` / `findVideoStreamByGUID` helpers.
- `/Users/cell/dev/ninjamzap-core/tests/video-sync/scenarios/` — 26 canonical receive-side sync scenarios. **Planner-validated subset (per Phase 20 D-10): `02_video_one_interval_early.cpp`, `03_late_join.cpp`, `13_sps_pps_mid_stream.cpp`, `20_drop_resync_recovery.cpp`, `22_audio_then_video.cpp`, `25_no_initial_spspps.cpp`.** Port the harness + scenario stubs as Wave-N Phase 21 tests.
- `/Users/cell/dev/ninjamzap-core/tests/video-sync/harness/TestClient.cpp:120-176` — byte-exact wire reference for SPS/PPS framing (`sendFakeSPSPPS` builds the `[4B BE outer_len][2B BE sps_len][SPS][2B BE pps_len][PPS]` blob the receiver expects). This is the same harness Phase 20 used to validate the send-side fix; the receive side parses the same shape.

### Phase 14.3 substrate (dependency baseline)

- `.planning/phases/14.3-native-video-foundation/14.3-03-SUMMARY.md` — receive-side RawData_Callback dispatch substrate. BEGIN handler at `MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN` inserts non-OGGv/non-FLAC fourCC into the RawData path (not Vorbis); WRITE handler GUID-matches against `m_rawdata_downloads` before falling through to audio download loop; eventType 0/1/2 (BEGIN/data/END) fired with appropriate parameters. **This is the substrate Phase 21 plugs into — no substrate work needed.**
- `src/core/njclient.cpp:2292+` — `MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN` case body (RawData dispatch already lands here).
- `src/core/njclient.cpp:2458+` — `MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE` case body (matched-flag wrap pattern).
- `src/core/njclient.cpp:3444+` — `DispatchTestServerDownloadIntervalBegin/Write` test helpers — Phase 21 mirrors these for test scaffolding.

### Phase 19 patterns (reused symmetrically)

- `.planning/phases/19-camera-capture-permission-ux/19-CONTEXT.md` — HIGH-2 RAII Subscription pattern (mirrored for Phase 21's `JamWideRemoteFrameDistributor::Subscription`), HIGH-4 atomic-generation + AsyncUpdater pattern (mirrored for `PeerVideoSink`).
- `juce/video/JamWideFrameDistributor.h/.cpp` — Phase 19's outgoing distributor; Phase 21's `JamWideRemoteFrameDistributor` is the symmetric inverse (same class shape, opposite data flow direction). Read for the listener-vector + Subscription class.

### Phase 20 dependencies (immediate predecessor — wire format validated)

- `.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md` — Phase 20 D-07 (drop-oldest backpressure counter pattern, mirrored for Phase 21 D-11/D-18); D-09 (audit-allowlist envelope, mirrored for Phase 21 D-16); D-13 (lazy lifecycle, mirrored for Phase 21 D-10); D-14 (IDR-per-interval — the precondition that makes Phase 21's "drop frame + auto-recover" recovery work); D-15 (`m_audio_interval_seq` atomic — same primitive available for receive-side cadence reads); D-19 (NinjamZap-literal substrate principles); D-20 (Phase 15.1-06 HIGH-2 carve-out template).
- `src/core/njclient.cpp:5283-5316` — Phase 20's `on_new_interval` SEND video block (the exact location Phase 21's RECEIVE video block lands adjacent to — same audio-thread context). Mirror the m_video_cs / m_video_recv_cs locking pattern.
- `tests/uat/phase-20-broadcast-uat-report.md` — Phase 20 closure record + 6-fix UAT-loop history; documents why `feedback_doc_vs_source_verification` matters and how byte-byte ninjamzap-core source verification is the gate (not docs).

### Phase 15.1-07a mirror (carve-out preserved)

- `.planning/phases/15.1-rt-safety-hardening/15.1-CONTEXT.md` — HIGH-2 mirror access (`m_remoteuser_mirror[s].next_ds[0]->guid` read on audio thread is already accepted), DecodeState SPSC handoff (audio thread can deref next_ds[0] safely because DecodeState ownership crosses thread boundaries via the SPSC-handoff pattern).
- `src/core/njclient.cpp` (15.1-07a mirror region) — `RemoteUserMirror.next_ds[0]` access pattern; Phase 21 reads `next_ds[0]->guid` and `prev_ds_guid` (cached previous swap's ds) for the GUID-pair decision.

### Project + memory (background)

- `.planning/PROJECT.md` — v1.3 Native Video milestone goal + macOS+Windows beta scope + ninjamzap-server reference policy.
- `.planning/REQUIREMENTS.md` — COD-03, WIRE-02 requirements (Phase 21 maps to these).
- `.claude/projects/-Users-cell-dev-JamWide/memory/feedback_proven_over_pure.md` — verbatim NinjamZap port over architectural purity (drives D-14, D-15, D-16).
- `.claude/projects/-Users-cell-dev-JamWide/memory/feedback_doc_vs_source_verification.md` — Phase 20 closure lesson; for Phase 21, the upstream source files cited above (not the docs) are the gates.
- `.claude/projects/-Users-cell-dev-JamWide/memory/feedback_uat_scope_redflags.md` — UAT scope discipline; Phase 21 success criterion 1 is "video appears at same wall-clock moment as audio" which is a user-visible happy path that must be explicitly UAT'd.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`SpscRing<T, N>`** (`src/threading/spsc_payloads.h`) — Phase 15.1-04 substrate; Phase 21 reuses for the per-peer audio→decoder slot-view handoff queue (audio thread pushes a single slot-view per swap; decoder thread pops, walks bytes, feeds libavcodec). Already RT-safe, MAX_BLOCK_SAMPLES contract documented at source, two-layer enforcement at allocations.
- **`juce::AsyncUpdater`** — Phase 19's HIGH-4 pattern; `PeerVideoSink` inherits and reuses verbatim for the "new frame ready" UI signal.
- **`juce::Thread`** — Phase 20's `Openh264Encoder` is the symmetric template for `Openh264Decoder` (encoder owns thread, drains SPSC of input frames, runs libavcodec; decoder follows the inverse pattern).
- **Phase 14.3-03 RawData_Callback dispatch** — `src/core/njclient.cpp:2292+` + `:2458+` + `:3444+` — substrate is in place; Phase 21 registers the H264 callback and plugs in to BEGIN (create VideoRecvState if missing) / WRITE (route to accumulating slot) / END (move accumulating → next) dispatch.
- **WDL primitives** — `WDL_PtrList<T>`, `WDL_HeapBuf`, `WDL_TypedBuf<int>`, `WDL_Mutex`, `WDL_MutexLock` — all used by upstream's `VideoRecvBuffer`/`VideoRecvState`; verbatim port keeps the same types.

### Established Patterns

- **Phase 19 HIGH-2 RAII Subscription** — Phase 21's `JamWideRemoteFrameDistributor::Subscription` mirrors this. Move-only, destructor detaches the callback so the publisher can never call into a dead subscriber.
- **Phase 19 HIGH-4 atomic generation counter + AsyncUpdater** — Phase 21's `PeerVideoSink` reuses verbatim for the lock-free latest-frame-wins contract.
- **Phase 20 D-09 audit-allowlist envelope** — Phase 21's D-16 mirrors the structure (mutex + alloc + memcpy carve-outs on audio thread accepted as NinjamZap-literal).
- **Phase 14.3-03 matched-flag wrap pattern** — `if (!matched) { ... existing audio download loop ... }` lets Phase 21 plug in without disturbing the audio download path.
- **JAMWIDE_BUILD_TESTS gating** — both Phase 20 and 14.3-03 expose case-body dispatcher methods (`DispatchTestServerDownloadIntervalBegin/Write`, `DrainRawDataSendQueueForTest`); Phase 21 mirrors with `DispatchTestVideoBegin/Write/End` + `DrainDecoderInputQForTest` etc.

### Integration Points

- **NJClient receive-side BEGIN handler** (`src/core/njclient.cpp:2292+`): Phase 21 registers a `RawData_Callback` for fourcc=H264. On BEGIN eventType=0, calls `findOrCreateVideoStream(username, chidx)` which (lazy) spins up VideoRecvState + decoder thread + sink + distributor entry.
- **NJClient receive-side WRITE handler** (`src/core/njclient.cpp:2458+`): On WRITE eventType=1 with matching GUID, routes bytes into `accumulating` slot's `data` (subject to D-11 4 MB cap). On WRITE eventType=2 (END), moves `accumulating → next` and resets `accumulating`.
- **NJClient on_new_interval** (`src/core/njclient.cpp:5283+`): Phase 21's audio-thread SWAP block lands ADJACENT to the existing Phase 20 send-side video block (same audio-thread critical section context, different mutex — `m_video_recv_cs` vs Phase 20's `m_video_cs`). Order: send-side block first (existing), then receive-side block (new in Phase 21).
- **JamWideJuceProcessor** owns the new `JamWideRemoteFrameDistributor` instance (lifetime parallel to the existing `JamWideFrameDistributor` for the camera path). Constructed at processor instantiation; destroyed at processor teardown. Phase 22 tiles get a reference to it via processor.
- **Phase 22 (consumer)** — Phase 22's per-peer tile component takes `distributor*` + `username` + `chidx` and calls `subscribeToPeer(name, chidx, [this]{ triggerAsyncUpdate(); })` at mount + `bufferLock`-protected snapshot read in `paint()`. Phase 22's grid layout iterates `m_remoteusers`, instantiates a tile per peer with `chidx=1`, and binds.

</code_context>

<specifics>
## Specific Ideas

- **Phase 22 UI overlay text:** `'video starting…'` until first frame; `'syncing…'` once `hold_count ≥ 2`. JamWide Voicemeeter Banana dark theme — overlay text should be soft white at ~70% opacity over a faint dark-translucent backdrop to remain readable against arbitrary peer video.
- **Subscription pattern naming:** `distributor->subscribeToPeer(username, chidx, onRepaint)` returns `JamWideRemoteFrameDistributor::Subscription` (the RAII handle). Phase 22 stores the handle as a member; destruction order is implicit.
- **Pure libavcodec consumer thread loop body:** kept tight and obvious — pop slot-view, walk AVCC bytes, build packet for each NAL, send_packet, drain receive_frame loop, sws_scale, swap, generation+, async update. Wire-format and codec-API responsibilities co-located per D-12 revised.
- **`drop_resync_count` is diagnostic, not user-visible.** Bumped on the kHoldCapDrop=4 → force-resync event. Plan 21-XX writes this to the audit-counter readout (similar to Phase 20's high-water / contention counter readouts in the UAT report).

</specifics>

<deferred>
## Deferred Ideas

- **Mid-write startPlaying optimization** (upstream's "audio_guid matches current DS at WRITE time → skip 1-swap defer") — verbatim port acceptable per `feedback_proven_over_pure`; not a deferred decision, just a subtopic we acknowledged we'd port faithfully.
- **VideoToolbox / MediaFoundation hardware-decode backends** — abstracted via the `VideoDecoder` interface (mirrors Phase 20's `VideoEncoder` pattern), but only the libavcodec/openh264 software implementation lands in Phase 21. v1.4+ adds hardware backends behind the same interface.
- **Adaptive quality / per-peer downscale on CPU pressure** — when N peers × HD decode exceeds available cores, the receive side could opt some peers down to a smaller `sws_scale` output. Deferred; default to fixed-resolution decode for v1.3.
- **OpenGL-backed `juce::Image` for HD render performance** — JUCE's software-backed images are fine at our scale (1280×720 paint at 30 fps); OpenGL upload only worth it if Phase 22 grid+popouts with N peers × HD shows measurable paint-thread load. v1.4+ if needed.
- **Empty-peer reaping policy** — for a peer that's gone silent without leaving the room, currently the decoder + sink stay alive. Memory pressure profiling in Phase 24 BETA could justify adding a reaping policy. Deferred.
- **VRR / variable frame-rate rendering** — currently assume fixed 10/15/30 fps from the encoder side; varying frame intervals are handled implicitly by the AsyncUpdater coalescing. No special VRR handling in v1.3.

</deferred>

---

*Phase: 21-h-264-decoder-receive-pipeline*
*Context gathered: 2026-05-17*
*Revised: 2026-05-17 per gsd-plan-checker B-1 (D-12 decoder-thread placement clarification)*
