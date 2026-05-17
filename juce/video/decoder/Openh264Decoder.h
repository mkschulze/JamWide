#pragma once
// Phase 21-02 — Openh264Decoder: libavcodec H.264 decoder with the
// libopenh264 backend (D-09 + Pitfall 4). Concrete implementation of the
// abstract VideoDecoder interface.
//
// Architecture summary (CONTEXT.md D-12 revised + codex review Cluster
// 1 / 2 Option A / 3 / 6 / 8 / 10):
//
//   - The constructor takes THREE references — to the slot ring
//     (`std::array<VideoRecvSlotSnapshot, 4>`), to the integer-index
//     SPSC (`SpscRing<int, 4>`), and to the producer-seq atomic. All
//     three live on the SAME `VideoRecvState` that owns this decoder
//     (Plan 21-03 wires them at construction time).
//
//   - The audio thread, inside `runVideoReceiveBlock_` under
//     `m_video_recv_cs`, picks the next-fill slot index, memcpys the
//     playing-slot bytes + frameOffsets into `decoderSlots[idx]`, pushes
//     the integer `idx` onto `decoderSlotIndexQ`, and bumps
//     `decoderProducerSeq.fetch_add(1, release)`. NO `WaitableEvent::signal()`
//     on the audio thread (codex Cluster 1).
//
//   - The decoder thread polls `*producerSeq_` every ~15 ms (well under
//     the ~167 ms NINJAM swap interval), drains pending indices, and
//     parses each snapshot's slot bytes in-thread: 20-byte marker
//     discard → SPS/PPS chunk → per-frame NAL extraction with Annex-B
//     wrap → `avcodec_send_packet` → `avcodec_receive_frame` drain →
//     `sws_scale` YUV420P → BGRA → swap into `image_back_` → bump
//     generation counter → trigger AsyncUpdate on the sink (codex
//     Cluster 3 — sink touch under sink_lock_).
//
//   - The AVCC parser runs on the DECODER thread per CONTEXT.md D-12
//     revised (B-1 resolution). Wire-format and codec-API responsibilities
//     are co-located on a single non-realtime thread.
//
//   - `setSink(jamwide::PeerVideoSink* sink)` is a PUBLIC API (codex
//     Cluster 3) safe to call with `nullptr` at any time. The internal
//     `sink_lock_` pairs the setter and the decoder thread's
//     sink-touch in `scaleAndSwapImage_`. After `setSink(nullptr)`
//     returns, the decoder thread cannot dereference sink_ for any
//     further work. Plan 21-03's shutdown protocol calls
//     `decoder->setSink(nullptr)` AFTER stop+join.
//
//   - Test-only (`JAMWIDE_BUILD_TESTS`): `pollOneFrameForTest` is
//     lock-protected via `TestFrameResult` (codex Cluster 8 — no raw
//     `juce::Image*` race on test timeout). `last_parse_tid_` +
//     `decoder_thread_std_id_` are mutex-protected `std::thread::id`
//     under `tid_lock_` (codex Cluster 10 — atomic-thread-id is not
//     guaranteed lock-free on all standard libraries; we use a mutex).
//
// Lifecycle (R4 H9 LOCKED 7-step close ordering): see VideoDecoder.h
// banner. Implementation in Openh264Decoder.cpp.

#include "VideoDecoder.h"
#include "NalChunk.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include "../../../src/threading/spsc_ring.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#ifdef JAMWIDE_BUILD_TESTS
#  include <condition_variable>
#  include <deque>
#  include <thread>
#endif

// Forward declarations for ffmpeg types — keep this header free of
// ffmpeg includes. The .cpp file is the only consumer of the actual
// AVCodecContext / AVFrame / SwsContext layouts.
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace jamwide {

// Forward declarations (Plan 21-03 owns the full types).
class  PeerVideoSink;
struct VideoRecvSlotSnapshot;

class Openh264Decoder
    : public juce::Thread
    , public VideoDecoder
{
public:
    // Ctor takes references to the three VideoRecvState-owned fields
    // (codex Cluster 2 Option A): the slot ring, the integer-index
    // SpscRing, and the producer-seq atomic. The references must
    // out-live the decoder (enforced by VideoRecvState owning the
    // decoder as `std::unique_ptr<Openh264Decoder>` AND being the same
    // object that owns the three fields).
    Openh264Decoder(std::array<VideoRecvSlotSnapshot, 4>& slotRing,
                    SpscRing<int, 4>&                      slotIndexQ,
                    std::atomic<std::uint64_t>&            producerSeq);

    ~Openh264Decoder() override;

    Openh264Decoder(const Openh264Decoder&) = delete;
    Openh264Decoder& operator=(const Openh264Decoder&) = delete;

    // VideoDecoder interface:
    bool open(int dst_width, int dst_height) override;
    void close() override;
    bool isOpen() const noexcept override { return is_open_.load(std::memory_order_acquire); }

    void setSink(PeerVideoSink* sink) override;

    // Producer notification — bumps producer_seq + does a release fence.
    // Audio thread calls this AFTER memcpying + try_pushing the slot
    // index. NO OS sync primitives (codex Cluster 1).
    void notifyProducerSeq() override;

    int  getDecodeErrorCount() const noexcept override {
        return decode_error_count_.load(std::memory_order_relaxed);
    }
    bool hasFirstFrameSeen()   const noexcept override {
        return first_frame_seen_.load(std::memory_order_acquire);
    }

#ifdef JAMWIDE_BUILD_TESTS
    bool pollOneFrameForTest(juce::Image& out, int timeout_ms) override;
    void pushNalChunk(NalChunk chunk) override;
    void pushSlotSnapshotForTest(const VideoRecvSlotSnapshot& snapshot) override;

    // B-1 enforcement accessors (codex Cluster 10 — mutex-protected, not
    // atomic-thread-id):
    std::thread::id lastParseTid() const;
    std::thread::id getDecoderStdThreadId() const;
#endif

private:
    // juce::Thread
    void run() override;

    // Private helpers — all run on the decoder thread.
    void scaleAndSwapImage_(const AVFrame* frame);
    void parseSlotAndFeed_(const VideoRecvSlotSnapshot& snapshot);
    void sendAnnexB_(const unsigned char* nal, int nalLen);
    void drainReceiveFrameLoop_();

#ifdef JAMWIDE_BUILD_TESTS
    void drainTestNalQueue_();
#endif

    // libavcodec / sws state — decoder thread owns this exclusively after
    // open(). Allocated in open() on the message thread BEFORE the
    // decoder thread is started, then handed off.
    AVCodecContext* codecContext_   = nullptr;
    SwsContext*     sws_            = nullptr;
    AVFrame*        frame_          = nullptr;
    AVPacket*       packet_         = nullptr;
    int             sws_src_width_  = 0;
    int             sws_src_height_ = 0;
    int             dst_width_      = 0;
    int             dst_height_     = 0;

    // Destination image (BGRA, dst_width × dst_height). The decoder
    // writes sws_scale output into this. Plan 21-02 keeps it local; Plan
    // 21-03 will swap it with PeerVideoSink::image_front under
    // sink->bufferLock.
    juce::Image     image_back_;

    // Reusable Annex-B framing buffer; grown up to 4 MB as needed; never
    // shrunk. Avoids per-frame allocation inside sendAnnexB_.
    std::vector<unsigned char> annexBScratch_;

    // Codex Cluster 3 — sink ownership protocol:
    //   sink_ is read inside scaleAndSwapImage_ ONLY under sink_lock_.
    //   setSink(nullptr) acquires sink_lock_ then clears sink_; after
    //   this returns, the decoder thread cannot dereference sink_ (the
    //   sink-touch inside scaleAndSwapImage_ takes the same lock and
    //   then observes sink_ == nullptr).
    mutable std::mutex      sink_lock_;
    PeerVideoSink*          sink_ = nullptr;  // guarded by sink_lock_

    // Codex Cluster 2 Option A — slot ring is VideoRecvState-owned; the
    // decoder holds references. The three references all point to fields
    // on the SAME VideoRecvState that owns this decoder.
    std::array<VideoRecvSlotSnapshot, 4>* slotRing_     = nullptr;
    SpscRing<int, 4>*                     slotIndexQ_   = nullptr;
    std::atomic<std::uint64_t>*           producerSeq_  = nullptr;
    std::uint64_t                         lastConsumedSeq_ = 0;  // decoder-thread-local

    std::atomic<int>        decode_error_count_{0};
    std::atomic<int>        slotview_drop_count_{0};
    std::atomic<bool>       first_frame_seen_{false};
    std::atomic<bool>       is_open_{false};

#ifdef JAMWIDE_BUILD_TESTS
    // Codex Cluster 10 — mutex-protected std::thread::id (lock-free
    // atomic-thread-id is NOT guaranteed on all standard libraries;
    // this rarely-touched value uses a mutex instead).
    mutable std::mutex      tid_lock_;
    std::thread::id         last_parse_tid_{};
    std::thread::id         decoder_thread_std_id_{};

    // Codex Cluster 8 — lock-protected test result. The decoder writes
    // into result_.image under result_lock_ + signals result_.cv;
    // pollOneFrameForTest takes the lock, copies bytes out, returns.
    // No raw juce::Image* ever escapes the lock; no UAF if the test
    // times out without taking the result.
    struct TestFrameResult {
        juce::Image             image;
        bool                    ready = false;
        std::condition_variable cv;
    };
    mutable std::mutex      result_lock_;
    TestFrameResult         result_;

    // Codex Cluster 7 — test-only NAL bypass queue. pushNalChunk pushes
    // into here; the decoder thread drains it inside run() and feeds
    // each chunk directly via sendAnnexB_ (no slot-snapshot parsing
    // bypass).
    std::deque<NalChunk>    test_nal_queue_;
    mutable std::mutex      test_nal_lock_;
#endif
};

} // namespace jamwide
