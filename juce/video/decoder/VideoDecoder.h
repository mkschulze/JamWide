#pragma once
// Phase 21-02 — abstract VideoDecoder interface (D-09 / D-10 / D-13).
//
// Symmetric inverse of `juce/video/encoder/VideoEncoder.h`. The decoder
// owns its `juce::Thread` + `AVCodecContext*` + `SwsContext*` and runs
// libavcodec off-thread; Phase 22's grid + popouts read decoded frames
// via the `PeerVideoSink` wired by Plan 21-03.
//
// Threading contract:
//   - `open() / close() / isOpen()` are called from the message thread
//     (the decoder owner; Plan 21-03's `JamWideRemoteFrameDistributor`).
//   - `setSink(...)` (codex Cluster 3) is a PUBLIC API safe to call with
//     `nullptr` AT ANY TIME. After `setSink(nullptr)` returns, the
//     decoder thread MUST NOT dereference `sink_` for any further work
//     (the implementation holds an internal `sink_lock_` during BOTH the
//     setter AND the decoder thread's sink-touch — see
//     `Openh264Decoder::scaleAndSwapImage_`). Plan 21-03's shutdown
//     protocol calls `decoder->setSink(nullptr)` AFTER stop+join.
//   - `notifyProducerSeq()` is called by the producer (audio thread)
//     after a memcpy + try_push onto the `VideoRecvState`-owned slot
//     ring. The default implementation just bumps an atomic counter; NO
//     OS sync primitives, NO `juce::WaitableEvent::signal()` (codex
//     Cluster 1 — RT-safety envelope).
//   - The decoder thread polls the producer-seq atomic with a short timed
//     wait (~15 ms; well under the ~167 ms NINJAM swap interval) and
//     drains pending slot indices when the counter advances. Wake-up
//     latency is bounded at one poll interval.
//
// Lifecycle (R4 H9 LOCKED 7-step close ordering — see Openh264Decoder.cpp):
//   1. signalThreadShouldExit()
//   2. (no event signal needed — the decoder polls producerSeq with 15 ms
//      wait; threadShouldExit() returns true within one poll interval)
//   3. stopThread(5000) — joins the decoder thread with a 5 s timeout
//   4. avcodec_send_packet(codecContext_, nullptr) flush
//   5. while (avcodec_receive_frame(codecContext_, frame_) == 0) av_frame_unref(frame_)
//   6. av_frame_free(&frame_); av_packet_free(&packet_)
//   7. sws_freeContext(sws_); avcodec_free_context(&codecContext_)
//
// The `thread_count = 1` setting in `open()` avoids libavcodec internal
// worker join races during step 3 (Pitfall 8).

#include "NalChunk.h"

#include <juce_graphics/juce_graphics.h>

#include <cstdint>

namespace jamwide {

// Forward declarations — keep this header dependency-light. Plan 21-03's
// `PeerVideoSink` lives in `juce/video/distributor/` and is forward-
// declared here so the interface can carry a sink pointer without pulling
// in juce_events for AsyncUpdater.
class PeerVideoSink;
struct VideoRecvSlotSnapshot;

class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;

    // open(): construct AVCodecContext + SwsContext + AVFrame + AVPacket;
    // allocate the destination `juce::Image` at `dst_width × dst_height`
    // (D-07 — fixed at first-seen-peer resolution, or at v1.3 the
    // distributor's pre-selected receive surface size); start decoder
    // thread. Returns false on any libavcodec / sws failure. open() may
    // NOT be called when the decoder is already open — caller must
    // close() first.
    virtual bool open(int dst_width, int dst_height) = 0;

    // close(): R4 H9 LOCKED 7-step teardown ordering (see header
    // comment). Safe to call when not open (no-op). After close()
    // returns, the decoder may be safely deleted OR re-opened.
    virtual void close() = 0;

    // Whether the decoder is currently open + thread is running.
    virtual bool isOpen() const noexcept = 0;

    // Codex Cluster 3 — sink pointer setter. PUBLIC API; safe to call
    // with `nullptr` at any time. After `setSink(nullptr)` returns, the
    // decoder thread will not dereference the sink for any further work.
    // Plan 21-03 calls `decoder->setSink(nullptr)` AFTER stop+join so
    // by then the decoder thread has already exited — but the internal
    // sink_lock_ pairing is the structural guarantee.
    virtual void setSink(PeerVideoSink* sink) = 0;

    // Codex Cluster 1 — producer notification. The producer (audio
    // thread) calls this AFTER memcpying bytes into a slot AND pushing
    // the slot index onto the SPSC. Default implementation just bumps
    // an atomic counter (memory_order_release); the decoder thread polls
    // it. NO OS sync primitives. The audio thread MUST NOT call
    // `juce::WaitableEvent::signal()` or any equivalent OS sync call.
    virtual void notifyProducerSeq() = 0;

    // Observability counters (relaxed atomics; for diagnostics only).
    virtual int  getDecodeErrorCount() const noexcept = 0;
    virtual bool hasFirstFrameSeen()   const noexcept = 0;

#ifdef JAMWIDE_BUILD_TESTS
    // Codex Cluster 8 — lock-protected test result. Blocks up to
    // `timeout_ms` for the next decoded frame and copies the image bytes
    // into `out`. Returns true if a frame was captured before timeout;
    // false if timeout fired with no frame. Internally the decoder writes
    // into a `TestFrameResult` (owned by Openh264Decoder; protected by
    // `result_lock_`); the test reads under the same lock. NO raw
    // `juce::Image*` ever escapes the lock — no UAF if the test times
    // out without reading.
    virtual bool pollOneFrameForTest(juce::Image& out, int timeout_ms) = 0;

    // Test-only: bypass the slot-snapshot parser and feed an Annex-B
    // chunk directly into the decoder's internal NAL queue. Used by
    // `test_first_frame_emits` and friends so the test does not need to
    // construct a full `VideoRecvSlotSnapshot`.
    virtual void pushNalChunk(NalChunk chunk) = 0;

    // Codex Cluster 7 — test-only: simulate the production audio→decoder
    // path by memcpying the snapshot into slot 0 of the decoder's
    // backing slot ring, pushing index 0 onto the SPSC, and bumping the
    // producer-seq atomic. The decoder thread polls + parses on its
    // normal run() loop.
    virtual void pushSlotSnapshotForTest(const VideoRecvSlotSnapshot& snapshot) = 0;
#endif
};

} // namespace jamwide
