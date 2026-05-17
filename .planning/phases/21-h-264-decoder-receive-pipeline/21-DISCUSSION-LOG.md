# Phase 21: H.264 Decoder & Receive Pipeline - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-17
**Phase:** 21-h-264-decoder-receive-pipeline
**Areas discussed:** Decoded-image delivery to Phase 22 UI, Decoder thread model + per-peer memory budget, 4-stage pipeline ownership + Phase 15.1-07a mirror interaction, kHoldCapDrop=4 + decoder error UX

---

## Decoded-image delivery to Phase 22 UI

### Q1: How should the decoder hand decoded frames to Phase 22's UI?

| Option | Description | Selected |
|--------|-------------|----------|
| Atomic-snapshot juce::Image per peer | Each peer's decoder owns a stable juce::Image; on each newly-decoded frame, writes pixels then bumps an atomic generation counter. UI polls or hops via AsyncUpdater and reads the latest snapshot. Latest-frame-wins. Lock-free read/write. | ✓ |
| Per-peer SPSC ring of frames | Decoder pushes decoded juce::Images into a bounded SPSC ring; UI drains and renders the LAST one each repaint. Preserves order. Adds ring memory and drop policy. | |
| Listener callback through juce::MessageManager::callAsync | Decoder calls listener.onNewFrame(image) which dispatches via callAsync to UI. Simplest. Heap allocation per frame. | |

**User's choice:** Atomic-snapshot juce::Image per peer (Recommended)
**Notes:** Reuses Phase 19 HIGH-4 pattern verbatim. User selected the preview-illustrated code shape.

### Q2: How should Phase 22's UI tile learn that a new frame is ready?

| Option | Description | Selected |
|--------|-------------|----------|
| Per-peer juce::AsyncUpdater | Decoder calls triggerAsyncUpdate after bumping generation. Zero idle CPU. Self-coalescing. Phase 19 HIGH-4 verbatim. | ✓ |
| juce::Timer poll at 30 Hz on UI tile | Each tile owns a Timer(33ms). Reads sink.generation each tick. ~30 ticks/sec per visible tile even at idle. | |
| Hybrid — AsyncUpdater + slow fallback Timer | Belt-and-braces. Adds complexity for negligible robustness gain. | |

**User's choice:** Per-peer juce::AsyncUpdater (Recommended)

### Q3: Who owns the per-peer sink, and how does Phase 22 discover + bind to it?

| Option | Description | Selected |
|--------|-------------|----------|
| JamWideRemoteFrameDistributor (symmetric to Phase 19) | A new top-level service owns the per-peer sink map; subscribeToPeer returns RAII Subscription. Phase 19 HIGH-2 pattern verbatim. | ✓ |
| Sink owned by NJClient::VideoRecvState[] | Each peer's VideoRecvState owns its own sink. Tighter coupling. No RAII; lifetime by convention. | |
| Sink owned by Phase 22 tile, decoder writes into tile-owned image | Inverted ownership. Tile teardown must serialize with decoder writes. Reintroduces the lifetime bug HIGH-2 was designed to prevent. | |

**User's choice:** JamWideRemoteFrameDistributor with RAII Subscription (Recommended)

### Q4: What pixel format should the distributor's juce::Image carry?

| Option | Description | Selected |
|--------|-------------|----------|
| BGRA via libswscale | sws_scale(YUV420P → BGRA32). juce::Image::ARGB-backed; matches Phase 19 capture symmetrically. | ✓ |
| ARGB via libswscale | sws_scale(YUV420P → ARGB). Channel order swap on paint. | |
| Keep YUV420P, defer conversion to paint time | Lowest decode-thread cost but moves work to UI thread. Adds OpenGL dependency. | |

**User's choice:** BGRA via libswscale (Recommended)

### Q5 (deeper area A): How should the Subscription handle multiple subscribers per peer (DISP-03)?

| Option | Description | Selected |
|--------|-------------|----------|
| Sink owns a vector<onRepaint>; each Subscription holds a handle | Listener pattern with RAII detach. Grid + popout both subscribe independently. Phase 19 distributor pattern verbatim. | ✓ |
| Sink-per-tile (decoder fans out to N sinks for the same peer) | Multiple sinks per peer. Costs N×image memory. Wasteful at HD. | |
| Single subscriber only — popout 'takes' the subscription from grid | Breaks DISP-03's "simultaneously" requirement. | |

**User's choice:** Sink owns a vector<onRepaint> (Recommended)

### Q6 (deeper area A): Mid-session peer resolution change handling?

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed at first-seen resolution; sws_ adapts | Sink image fixed at first frame's resolution; sws_scale rescales subsequent frames if source size changes. Single allocation; no UI re-layout. | ✓ |
| Fixed at HD (1280×720); sws_ always scales source to HD | Constant memory cost. Quality loss on upscale. Wasteful for SD-preset peers. | |
| Recreate sink->image on resolution change; UI handles re-layout | Most code paths; UI must handle layout shifts. Overkill. | |

**User's choice:** Fixed at first-seen resolution; sws_ adapts (Recommended)

### Q7 (deeper area A): Pixel-write tearing protection?

| Option | Description | Selected |
|--------|-------------|----------|
| Double-buffer: front/back; swap under brief CriticalSection | Decoder writes into image_back; ref-counted swap under microsecond lock; UI reads image_front under same lock. Zero tearing. 2× image memory per peer. | ✓ |
| Single buffer with CriticalSection around decoder write | UI ↔ decoder mutex contention we explicitly avoided in Q1. | |
| Single buffer, accept brief tearing as cosmetic | Visible horizontal tear band ≤1 frame. | |

**User's choice:** Double-buffer with ref-counted swap (Recommended)

---

## Decoder thread model + per-peer memory budget

### Q1: Where should libavcodec H.264 decode happen for each peer?

| Option | Description | Selected |
|--------|-------------|----------|
| One decode thread per peer | Each VideoRecvState owns its own juce::Thread + SPSC of NAL chunks. Isolation per peer. Mirrors Phase 20 encoder threading. | ✓ |
| Single shared decode thread + per-peer SPSC | Round-robin. CPU-efficient at N peers but one peer's slow decode stalls everyone. | |
| Decode inline on NJClient::Run thread | Zero new threads. At HD scale, decode CPU stalls the run thread. | |

**User's choice:** One decode thread per peer (Recommended)

### Q2: When should a peer's decoder + sink + thread spin up and tear down?

| Option | Description | Selected |
|--------|-------------|----------|
| Lazy: spin-up on first H264 BEGIN, tear-down on peer leave | VideoRecvState created on first BEGIN for that peer; destroyed on m_remoteusers deletion. Zero cost for peers who never broadcast video. Mirrors Phase 20 D-13. | ✓ |
| Eager: spin-up at peer join, tear-down at peer leave | 8 idle threads + codecCtx allocs in a populated room. Saves cold-start latency. | |
| Pool: pre-allocate K decoder slots; claim/return | Avoids per-peer thread create/destroy cost. Adds slot-acquire failure handling. | |

**User's choice:** Lazy (Recommended)

### Q3: How should we bound per-peer memory and recover when a peer's interval goes way over expected?

| Option | Description | Selected |
|--------|-------------|----------|
| Per-peer per-slot cap 4 MB; on exceed drop rest-of-interval | 5× the expected ~800 KB HD IDR ceiling. Slot pre-allocates 4 MB on creation. Drop further bytes when capped. | ✓ |
| Per-peer total cap 16 MB across 4 slots; evict oldest playing slot | Caps total per peer regardless of slot distribution. More complex eviction logic. | |
| No cap — trust ninjamzap-server VideoCongestionThreshold | Simplest. Unbounded accumulators are a real DoS surface. | |

**User's choice:** Per-peer per-slot cap 4 MB (Recommended)

### Q4: AVCC parsing location — run thread or decoder thread?

| Option | Description | Selected |
|--------|-------------|----------|
| Run thread parses; pushes structured NalChunks to decoder SPSC | Clean separation: wire-format on run thread, codec-API on decoder thread. Parsing is cheap (~10 µs per HD interval). | ✓ |
| Run thread pushes whole slot bytes; decoder thread parses + decodes | Mixes wire-format with codec semantics. Wire errors get diagnosed via libavcodec errors. | |
| Inline parse + decode on run thread (no decoder thread) | Already rejected by Q1. | |

**User's choice:** Run thread parses (Recommended)

### Q5 (deeper area B): How should the decoder consume the SPS/PPS chunk from each interval?

| Option | Description | Selected |
|--------|-------------|----------|
| Feed SPS/PPS as Annex-B packets, same path as frame NALs | Wrap each NAL with 00 00 00 01; push as NalChunk; libavcodec recognizes unit_type 7/8 and updates state. No special extradata handling. | ✓ |
| Set SPS/PPS as AVCodecContext::extradata before opening codec | Build avcC blob; set on context. Mid-stream SPS/PPS changes require avcodec_flush_buffers + reopen. | |
| Hybrid: extradata on first SPS/PPS, ignore subsequent (with reset on change) | Lowest steady-state cost. Probably premature optimization. | |

**User's choice:** Feed as Annex-B packets (Recommended)

---

## 4-stage pipeline ownership + Phase 15.1-07a mirror interaction

### Q1: Where should the 4-stage pipeline state live?

| Option | Description | Selected |
|--------|-------------|----------|
| Verbatim upstream: WDL_PtrList<VideoRecvState> on NJClient + m_video_recv_cs | Port upstream verbatim. State is run-thread + audio-thread-shared under mutex. Phase 15.1-07a HIGH-2 preserved. Most expedient route to a passing canonical sync-scenario test suite. | ✓ |
| Embed VideoRecvState in RemoteUser struct | Useridx-keyed. Couples video state to roster lifetime. Affects cache locality. | |
| New JamWideVideoReceiveService class outside NJClient | Cleaner separation. Harder to verify against the 26 upstream sync scenarios. | |

**User's choice:** Verbatim upstream WDL_PtrList<VideoRecvState> (Recommended)

### Q2: How should JamWideRemoteFrameDistributor key its sinks?

| Option | Description | Selected |
|--------|-------------|----------|
| Distributor keyed by (username, chidx); UI binds by username | Stable across roster shifts. Matches upstream's findVideoStream pattern. | ✓ |
| Distributor keyed by monotonic peerId assigned at first H264 BEGIN | Bulletproof; adds translation layer NJClient must own. | |
| Distributor keyed by useridx; tile validates username on each repaint | Cheapest data structure; adds revalidation logic to every paint. | |

**User's choice:** Distributor keyed by (username, chidx) (Recommended)

### Q3: Should the audio thread run the receive-side SWAP + GUID-pair decision tree?

| Option | Description | Selected |
|--------|-------------|----------|
| Audio thread owns SWAP + decision tree under m_video_recv_cs | Verbatim upstream on_new_interval pattern. Adds Phase 15.1 D-09-symmetric carve-out. | ✓ |
| Move SWAP to NJClient::Run (run thread), audio thread only does timing | Adds ~1 interval of latency. Breaks success criterion 1 timing contract. | |
| Audio thread enqueues 'swap requested' to run thread; run thread does swap | Variable run-thread polling latency. Same compromise as option 2. | |

**User's choice:** Audio thread owns SWAP (Recommended)

### Q4: Verbatim copyFrom on audio thread, or 1-line deviation to pointer-swap?

| Option | Description | Selected |
|--------|-------------|----------|
| Verbatim copyFrom; add to audit-allowlist envelope | Accept upstream's pending.copyFrom(next) pattern verbatim. Memcpy ~0.4 ms per peer per interval. NinjamZap-literal. | ✓ |
| 1-line deviation: VideoRecvBuffer slots as std::unique_ptr; pointer-swap | Lock-free O(1) swap. Deviates from upstream's embedded-value layout. | |
| Pool of N pre-allocated VideoRecvBuffers per peer; SWAP rotates pool indices | Cleanest RT-safety. Adds pool management. Overengineered. | |

**User's choice:** Verbatim copyFrom (Recommended for proven_over_pure)

---

## kHoldCapDrop=4 + decoder error UX

### Q1: What should the user see during the kHoldCapDrop=4 hold period and resync?

| Option | Description | Selected |
|--------|-------------|----------|
| Last-frame freeze; subtle 'syncing…' overlay after 2 holds | First 1-2 holds freeze frame; holds 3-4 show subtle overlay; resync proceeds cleanly. | ✓ |
| Last-frame freeze only; no overlay | Mobile NinjamZap behavior. User has no signal something's wrong. | |
| Immediate visual hint: tile dims to 70% on first hold, restores on resync | Aggressive feedback. Might feel jittery on rapid hold/resume cycles. | |

**User's choice:** Last-frame freeze + 'syncing…' overlay after 2 holds (Recommended)

### Q2: When libavcodec returns an error mid-interval, how should the decoder thread recover?

| Option | Description | Selected |
|--------|-------------|----------|
| Drop the frame, bump counter, continue | IDR-per-interval auto-recovery at next interval boundary. Bounded counter. | ✓ |
| Drop the rest of the interval; wait for next BEGIN | More conservative; explicit interval boundary in recovery. Adds per-interval tracking. | |
| Tear down and reinit the per-peer decoder on error | Heaviest hammer. ~50-100 ms cold-start latency on each error. | |

**User's choice:** Drop frame + counter + continue (Recommended)

### Q3: First-frame UX (before any decoded frame arrives)?

| Option | Description | Selected |
|--------|-------------|----------|
| Tile chrome + 'video starting…' overlay until first frame | Same pattern reused for mid-stream-join recovery. Clear user signal. | ✓ |
| Solid color (matching chrome) until first frame | Minimal; no info. | |
| Black tile until first frame | Mobile NinjamZap behavior. Ambiguous. | |

**User's choice:** Tile chrome + 'video starting…' overlay (Recommended)

### Q4: How should PeerVideoSink expose tile status to Phase 22?

| Option | Description | Selected |
|--------|-------------|----------|
| Atomic status fields on PeerVideoSink | hold_count, decode_error_count, drop_resync_count, synced, first_frame_seen — all atomic, read lock-free per repaint. | ✓ |
| Listener callback on status changes | Adds async signal path beyond AsyncUpdater. Heap allocation per transition. | |
| Direct NJClient query: GetVideoStatusForPeer(name, chidx) | Couples UI render path to NJClient internals; UI ↔ audio mutex contention. | |

**User's choice:** Atomic status fields on PeerVideoSink (Recommended)

---

## Claude's Discretion

- Decoder thread priority (default: normal; do not preempt audio thread)
- libavcodec multi-threading (default: thread_count=1; profile under HD before enabling)
- SpscRing<NalChunk, 32> depth (32 holds ~2 intervals at worst; planner picks)
- NalChunk storage (inline small + heap fallback, or always-heap — planner picks)
- File layout (planner mirrors Phase 20: juce/video/decoder/ + juce/video/distributor/)
- Test scenario port matrix from the 26 upstream scenarios (planner picks the high-value subset starting with the 6 listed in CONTEXT.md)
- Empty-peer reaping policy (default: stay alive until peer leaves; planner picks if profiling shows memory pressure)
- Debug logging surface (default: open/close + first decode error + every 100th decode error after that)

## Deferred Ideas

- Mid-write startPlaying optimization (verbatim port acceptable; not a deferred decision per se, just a faithful-port acknowledgement)
- VideoToolbox / MediaFoundation hardware-decode backends (abstracted via VideoDecoder interface; libavcodec/openh264 software-only in Phase 21; v1.4+)
- Adaptive quality / per-peer downscale on CPU pressure (deferred)
- OpenGL-backed juce::Image for HD render performance (deferred; default software-backed)
- Empty-peer reaping policy (deferred — Phase 24 BETA profiling can justify if needed)
- VRR / variable frame-rate rendering (deferred; AsyncUpdater coalescing handles implicitly for v1.3)
