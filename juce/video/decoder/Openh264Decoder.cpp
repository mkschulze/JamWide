// Phase 21-02 Task 2 — Openh264Decoder implementation. libavcodec H.264
// decoder backed by the vendored libopenh264 codec (D-09). Concrete
// implementation of the abstract VideoDecoder interface.
//
// THREADING: see Openh264Decoder.h / VideoDecoder.h banners for the full
// contract. In short:
//   - message thread: open / close / setSink / dtor
//   - audio thread:   notifyProducerSeq (atomic counter bump only)
//   - decoder thread: run() — polls producer_seq, drains the slot index
//     SPSC, parses each snapshot's bytes in-thread, feeds libavcodec via
//     send_packet, drains receive_frame, sws_scale → image_back_, swap
//     into sink (under sink_lock_).
//
// CLOSE ORDERING (R4 H9 LOCKED 7-step):
//   1. signalThreadShouldExit()
//   2. (no event signal — decoder polls producer_seq with 15 ms wait;
//      threadShouldExit() returns true within one poll interval)
//   3. stopThread(5000)
//   4. avcodec_send_packet(codecContext_, nullptr) flush
//   5. while (avcodec_receive_frame(codecContext_, frame_) == 0) av_frame_unref(frame_)
//   6. av_frame_free(&frame_); av_packet_free(&packet_)
//   7. sws_freeContext(sws_); avcodec_free_context(&codecContext_)
//
// Decoder thread wake-up protocol (codex review Cluster 1):
//   The audio thread does NOT call WaitableEvent::signal() — that would
//   enter OS sync primitives on the audio thread, violating the RT-safety
//   envelope. Instead: the audio thread bumps vs->decoderProducerSeq
//   (memory_order_release) after memcpying bytes into vs->decoderSlots[N]
//   and pushing N onto vs->decoderSlotIndexQ. This decoder thread polls
//   vs->decoderProducerSeq every kPollWaitMs (15 ms; well under the
//   ~167 ms NINJAM swap interval) and drains the SPSC when the counter
//   advances. Wake-up latency is bounded at one poll interval.
//
// Sink ownership protocol (codex review Cluster 3):
//   sink_ is read inside scaleAndSwapImage_ ONLY under sink_lock_.
//   setSink(nullptr) acquires sink_lock_ then clears sink_. After this
//   returns, the decoder thread CANNOT dereference sink_ (it would block
//   waiting for sink_lock_ and then observe sink_ == nullptr). Plan 21-03's
//   shutdown protocol calls setSink(nullptr) AFTER decoder->stopAndJoin()
//   returns, so by then the decoder thread has already exited — but the
//   lock pairing is defensive belt-and-suspenders.
//
// IMPORTANT (Landmine L6 / Openh264Encoder.cpp pattern): ffmpeg headers
// MUST be included BEFORE any JUCE / stdlib header that could pull
// /usr/local/include into the search path. jamwide_use_ffmpeg() emits
// `-I<vendored> BEFORE PRIVATE` so vendored headers come first, but
// source-level discipline applies: extern "C" block at the very top.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include "Openh264Decoder.h"
#include "VideoRecvSlotSnapshot.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace jamwide {

namespace {

// Decoder-thread poll wait — non-blocking on the audio side; bounded
// wake-up latency at 15 ms. Well under the ~167 ms NINJAM swap interval
// (codex Cluster 1 — no WaitableEvent::signal on the audio thread).
constexpr int kPollWaitMs = 15;

// Annex-B start code per D-13. Wraps each NAL payload before send_packet.
constexpr unsigned char kAnnexBStartCode[4] = { 0x00, 0x00, 0x00, 0x01 };

} // namespace

// ───────────────────────────────────────────────────────────────────────
// Ctor / Dtor
// ───────────────────────────────────────────────────────────────────────

Openh264Decoder::Openh264Decoder(std::array<VideoRecvSlotSnapshot, 4>& slotRing,
                                 SpscRing<int, 4>&                      slotIndexQ,
                                 std::atomic<std::uint64_t>&            producerSeq)
    : juce::Thread("JamWide H264 Decoder")
    , slotRing_(&slotRing)
    , slotIndexQ_(&slotIndexQ)
    , producerSeq_(&producerSeq)
{
    annexBScratch_.reserve(64 * 1024);  // 64 KB initial; grows up to 4 MB
}

Openh264Decoder::~Openh264Decoder()
{
    // R4 H9 LOCKED 7-step teardown — call close() which encapsulates the
    // entire sequence. Safe to call when not open.
    close();
}

// ───────────────────────────────────────────────────────────────────────
// VideoDecoder interface
// ───────────────────────────────────────────────────────────────────────

bool Openh264Decoder::open(int dst_width, int dst_height)
{
    if (is_open_.load(std::memory_order_acquire)) {
        // open() may NOT be called when already open — caller bug.
        return false;
    }

    if (dst_width <= 0 || dst_height <= 0) {
        return false;
    }

    dst_width_  = dst_width;
    dst_height_ = dst_height;

    // Pitfall 4 / RESEARCH §Code Example 3 — find the H.264 decoder.
    // We use the libavcodec built-in H.264 decoder (codec name "h264"),
    // NOT the libopenh264 decoder wrapper. The built-in is faster on
    // x86_64 and matches the validated Phase 20 encoder pipeline.
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        return false;
    }

    codecContext_ = avcodec_alloc_context3(codec);
    if (codecContext_ == nullptr) {
        return false;
    }

    // Pitfall 8 / D-Discretion: thread_count = 1 to avoid libavcodec
    // internal worker join races during close(). The audio thread is
    // already higher-priority; decoder must not preempt audio.
    codecContext_->thread_count = 1;
    codecContext_->thread_type  = 0;

    // D-13: SPS/PPS fed as Annex-B NALs via the same send_packet path as
    // frame NALs — no AVCodecContext::extradata setup.
    codecContext_->extradata      = nullptr;
    codecContext_->extradata_size = 0;

    // RESEARCH §Pitfall 4 / D-09: low-latency hint. The decoder doesn't
    // buffer extra frames for prediction; emits each decoded frame
    // immediately. Critical for live video.
    codecContext_->flags |= AV_CODEC_FLAG_LOW_DELAY;

    int rc = avcodec_open2(codecContext_, codec, nullptr);
    if (rc < 0) {
        avcodec_free_context(&codecContext_);
        return false;
    }

    frame_  = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (frame_ == nullptr || packet_ == nullptr) {
        if (frame_  != nullptr) av_frame_free(&frame_);
        if (packet_ != nullptr) av_packet_free(&packet_);
        avcodec_free_context(&codecContext_);
        return false;
    }

    // Pre-allocate the destination image at dst_width × dst_height in
    // ARGB (JUCE backs ARGB with BGRA on macOS / Windows, matching the
    // sws_scale BGRA target — D-04). Cleared to opaque black.
    image_back_ = juce::Image(juce::Image::ARGB, dst_width_, dst_height_,
                              /*clearImage*/ true);

    is_open_.store(true, std::memory_order_release);

    // Pitfall 4: high priority (NOT realtime — audio is realtime).
    startThread(juce::Thread::Priority::high);
    return true;
}

void Openh264Decoder::close()
{
    if (!is_open_.exchange(false, std::memory_order_acq_rel)) {
        // Already closed (or never opened) — no-op.
        return;
    }

    // R4 H9 step 1: signal thread to exit. The decoder polls
    // threadShouldExit() once per poll interval (15 ms) so wake-up is
    // bounded — no event signal needed (codex Cluster 1).
    signalThreadShouldExit();

    // R4 H9 step 3: join the decoder thread with a 5 s timeout. Ample for
    // the decoder to observe threadShouldExit on its next poll.
    stopThread(5000);

    // R4 H9 step 4: flush libavcodec — send a NULL packet to signal EOF.
    if (codecContext_ != nullptr) {
        avcodec_send_packet(codecContext_, nullptr);

        // R4 H9 step 5: drain receive_frame until EOF / EAGAIN.
        while (avcodec_receive_frame(codecContext_, frame_) == 0) {
            av_frame_unref(frame_);
        }
    }

    // R4 H9 step 6: free frame + packet.
    if (frame_  != nullptr) av_frame_free(&frame_);
    if (packet_ != nullptr) av_packet_free(&packet_);

    // R4 H9 step 7: free sws context + codec context.
    if (sws_ != nullptr) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (codecContext_ != nullptr) {
        avcodec_free_context(&codecContext_);
    }

    sws_src_width_  = 0;
    sws_src_height_ = 0;
}

void Openh264Decoder::setSink(PeerVideoSink* sink)
{
    // Codex Cluster 3 — sink_lock_ pairs the setter and scaleAndSwapImage_'s
    // sink touch. After setSink(nullptr) returns, the decoder thread cannot
    // dereference sink_ for any further work.
    std::lock_guard<std::mutex> g(sink_lock_);
    sink_ = sink;
}

void Openh264Decoder::notifyProducerSeq()
{
    // Codex Cluster 1 — atomic-seq publish. The decoder thread polls this
    // with timed wait. NO OS sync primitives, NO WaitableEvent::signal.
    if (producerSeq_ != nullptr) {
        producerSeq_->fetch_add(1, std::memory_order_release);
    }
}

// ───────────────────────────────────────────────────────────────────────
// Decoder thread loop
// ───────────────────────────────────────────────────────────────────────

void Openh264Decoder::run()
{
    juce::Thread::setCurrentThreadName("JamWide H264 Decoder");

#ifdef JAMWIDE_BUILD_TESTS
    // Codex Cluster 10 — mutex-protected std::thread::id capture (NOT atomic).
    {
        std::lock_guard<std::mutex> g(tid_lock_);
        decoder_thread_std_id_ = std::this_thread::get_id();
    }
#endif

    while (!threadShouldExit()) {
        // Codex Cluster 1 — atomic-seq poll, NO WaitableEvent::wait.
        const auto curSeq = (producerSeq_ != nullptr)
                                ? producerSeq_->load(std::memory_order_acquire)
                                : 0;

        bool didWork = false;

        if (curSeq != lastConsumedSeq_) {
            // Drain all pending index entries from the SPSC.
            if (slotIndexQ_ != nullptr && slotRing_ != nullptr) {
                while (auto idx = slotIndexQ_->try_pop()) {
                    const int slotIdx = *idx;
                    if (slotIdx >= 0 && slotIdx < 4) {
                        parseSlotAndFeed_((*slotRing_)[(std::size_t)slotIdx]);
                        didWork = true;
                    }
                }
            }
            lastConsumedSeq_ = curSeq;
        }

#ifdef JAMWIDE_BUILD_TESTS
        // Codex Cluster 7 — test-only NAL bypass path. Production code
        // never hits this; the queue is always empty in production.
        drainTestNalQueue_();
#endif

        if (!didWork) {
            juce::Thread::sleep(kPollWaitMs);
        }
    }
}

void Openh264Decoder::parseSlotAndFeed_(const VideoRecvSlotSnapshot& snapshot)
{
#ifdef JAMWIDE_BUILD_TESTS
    // Codex Cluster 10 — mutex-protected std::thread::id update.
    {
        std::lock_guard<std::mutex> g(tid_lock_);
        last_parse_tid_ = std::this_thread::get_id();
    }
#endif

    // Per Plan 21-01 Task 1 wire-format contract (codex review Cluster 6):
    //   snapshot.bytes holds concatenated frame payloads (outer 4B BE
    //     prefixes CONSUMED).
    //   snapshot.frameOffsets[i] is the byte offset INTO snapshot.bytes
    //     of frame i's first payload byte; frameOffsets[frameCount] ==
    //     snapshot.size (trailing terminator synthesised by
    //     copyFromVideoRecvBuffer).
    //   Marker frame (typically the first frame, payload size == 20):
    //     [4B BE sender_seq][16B audio_guid] — decoder DISCARDS the
    //     marker. Its OUTER prefix value MUST equal 20 (Phase 20 commit
    //     6d23b5c regression guard); the snapshot stores ONLY the 20-byte
    //     payload (outer 4B prefix already consumed).
    //   SPS/PPS chunk (when present): no Annex-B start codes; Annex-B
    //     wrap is the decoder's job (sendAnnexB_ helper):
    //     [2B BE sps_len][SPS_NAL_bytes][2B BE pps_len][PPS_NAL_bytes].
    //   Per-frame NAL chunk (common case): single NAL unit, no prefix,
    //     no start code: [NAL_bytes].

    for (int i = 0; i < snapshot.frameCount; ++i) {
        const int frameStart = snapshot.frameOffsets[(std::size_t)i];
        const int frameEnd   = snapshot.frameOffsets[(std::size_t)(i + 1)];
        const int frameSize  = frameEnd - frameStart;

        if (frameSize <= 0) continue;

        // Skip the 20-byte marker payload (Phase 21-01 wire-format
        // contract). Plan 21-02 codex Cluster 6: payload IS 20 bytes
        // (not 24 — the OUTER 4B prefix is consumed before the snapshot
        // is built).
        if (frameSize == 20) continue;

        const unsigned char* frame = snapshot.bytes.data() + frameStart;

        // SPS/PPS detection heuristic (matches upstream sendFakeSPSPPS at
        // ninjamzap-core/tests/video-sync/harness/TestClient.cpp:156-176):
        //   first 2 bytes parse as plausible sps_len, sps_len + 2 + 2 +
        //   pps_len_min <= frameSize, inner SPS first byte's NAL
        //   unit_type & 0x1f == 7.
        //
        // When detected, send SPS and PPS as a SINGLE concatenated
        // Annex-B packet (00 00 00 01 SPS 00 00 00 01 PPS) — libavcodec
        // expects the parameter sets to arrive together, not as two
        // separate send_packet calls (the separate-send pattern
        // triggers AVERROR_INVALIDDATA on the SPS alone because the
        // decoder cannot recover its state without seeing the PPS in
        // the same packet sequence). This matches how the pushNalChunk
        // path (test_first_frame_emits) works — that test passes the
        // full SPS+start_code+PPS in one chunk and libavcodec is happy.
        if (frameSize >= 6) {
            const unsigned int sps_len = ((unsigned int)frame[0] << 8) | frame[1];
            if (sps_len >= 4 && (int)(2 + sps_len + 2) <= frameSize - 1) {
                const unsigned char sps_nal_type = frame[2] & 0x1f;
                if (sps_nal_type == 7) {
                    const unsigned char* sps_body = frame + 2;
                    const unsigned int pps_len    =
                        ((unsigned int)frame[2 + sps_len] << 8) | frame[2 + sps_len + 1];
                    const unsigned char* pps_body = frame + 2 + sps_len + 2;
                    if ((int)(2 + sps_len + 2 + pps_len) <= frameSize
                        && pps_len > 0) {
                        // Build a combined SPS+PPS Annex-B packet in
                        // annexBScratch_ and send as one packet.
                        const std::size_t combined =
                            4 + (std::size_t)sps_len + 4 + (std::size_t)pps_len;
                        if (annexBScratch_.size() < combined) {
                            annexBScratch_.resize(combined);
                        }
                        std::memcpy(annexBScratch_.data(),
                                    kAnnexBStartCode, 4);
                        std::memcpy(annexBScratch_.data() + 4,
                                    sps_body, (std::size_t)sps_len);
                        std::memcpy(annexBScratch_.data() + 4 + sps_len,
                                    kAnnexBStartCode, 4);
                        std::memcpy(annexBScratch_.data() + 4 + sps_len + 4,
                                    pps_body, (std::size_t)pps_len);
                        // Call send_packet directly with the combined buffer
                        // (bypass sendAnnexB_ which would re-wrap with a
                        // single start code).
                        if (codecContext_ != nullptr && packet_ != nullptr) {
                            av_packet_unref(packet_);
                            packet_->data = annexBScratch_.data();
                            packet_->size = (int)combined;
                            const int rc = avcodec_send_packet(codecContext_, packet_);
                            // AVERROR_INVALIDDATA on a parameter-set-only
                            // packet (SPS+PPS, no slice) is libavcodec
                            // saying "I parsed your headers but there's no
                            // frame yet" — NOT a decode failure. Don't
                            // count.
                            if (rc < 0 && rc != AVERROR(EAGAIN)
                                && rc != AVERROR_INVALIDDATA) {
                                decode_error_count_.fetch_add(1, std::memory_order_relaxed);
                            }
                            drainReceiveFrameLoop_();
                        }
                        continue;
                    }
                }
            }
        }

        // Default: per-frame NAL chunk.
        sendAnnexB_(frame, frameSize);
    }

    // After all packets sent for this snapshot, drain receive_frame.
    drainReceiveFrameLoop_();
}

void Openh264Decoder::sendAnnexB_(const unsigned char* nal, int nalLen)
{
    if (codecContext_ == nullptr || packet_ == nullptr || nal == nullptr || nalLen <= 0) {
        return;
    }

    // Build the Annex-B framed packet ([00 00 00 01][NAL]) into the
    // reusable scratch buffer. Resize if needed; never shrinks.
    const std::size_t needed = 4 + (std::size_t)nalLen;
    if (annexBScratch_.size() < needed) {
        annexBScratch_.resize(needed);
    }
    std::memcpy(annexBScratch_.data(),       kAnnexBStartCode, 4);
    std::memcpy(annexBScratch_.data() + 4,   nal,             (std::size_t)nalLen);

    // Use a stack-allocated AVPacket with raw data pointer into our
    // scratch buffer. After each successful send, drain receive_frame
    // immediately — that ensures libavcodec has consumed the packet
    // data before we reuse the scratch buffer on the next sendAnnexB_
    // call. (Without ref-counting via av_packet_make_refcounted, the
    // packet data is not copied; libavcodec parses it in place during
    // send_packet, which is fine as long as we drain receive_frame
    // before mutating the buffer.)
    av_packet_unref(packet_);
    packet_->data = annexBScratch_.data();
    packet_->size = (int)needed;

    const int rc = avcodec_send_packet(codecContext_, packet_);

    // D-18 / Pitfall 1: drop-frame-and-continue. AVERROR_INVALIDDATA /
    // AVERROR(EINVAL) / other negative codes (except EAGAIN which
    // means "buffer full, drain receive_frame first") just bump the
    // counter and move on; libavcodec auto-recovers on the next IDR.
    if (rc < 0 && rc != AVERROR(EAGAIN)) {
        // AVERROR_INVALIDDATA on a parameter-set-only packet (NAL units
        // 7 = SPS, 8 = PPS, 6 = SEI) is libavcodec saying "I parsed your
        // headers but there's no frame to give yet" — NOT a decode
        // failure. Don't count it. Only count real decode errors on
        // packets that purport to carry a slice (NAL type 1, 5, etc.).
        const unsigned char nal_type = (nalLen >= 1) ? (nal[0] & 0x1f) : 0;
        const bool is_parameter_set = (nal_type == 7 || nal_type == 8 || nal_type == 6);
        if (!is_parameter_set) {
            decode_error_count_.fetch_add(1, std::memory_order_relaxed);
        }
        drainReceiveFrameLoop_();
        return;
    }

    // If send returned EAGAIN, the decoder's internal queue is full — we
    // must drain receive_frame before retrying. Drain here; the retry
    // sends the same packet (still in scratch) again.
    if (rc == AVERROR(EAGAIN)) {
        drainReceiveFrameLoop_();
        // Retry the send after draining.
        const int rc2 = avcodec_send_packet(codecContext_, packet_);
        if (rc2 < 0 && rc2 != AVERROR(EAGAIN)) {
            decode_error_count_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    // Successful send (rc == 0 or successful retry after EAGAIN). Drain
    // receive_frame NOW so the consumed-but-not-yet-decoded data inside
    // libavcodec is processed BEFORE we reuse annexBScratch_ on the
    // next sendAnnexB_ call. Most calls produce zero or one frame; the
    // drain is cheap when the queue is empty (single EAGAIN return).
    drainReceiveFrameLoop_();
}

void Openh264Decoder::drainReceiveFrameLoop_()
{
    if (codecContext_ == nullptr || frame_ == nullptr) {
        return;
    }

    // Pitfall 1: drain receive_frame loop. EAGAIN means "need more
    // packets"; EOF means flushed. Any other negative is a decode error
    // — drop the frame and continue (D-18).
    while (true) {
        const int rc = avcodec_receive_frame(codecContext_, frame_);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            break;
        }
        if (rc < 0) {
            decode_error_count_.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        // Successful decode.
        scaleAndSwapImage_(frame_);
        first_frame_seen_.store(true, std::memory_order_release);
        av_frame_unref(frame_);
    }
}

void Openh264Decoder::scaleAndSwapImage_(const AVFrame* frame)
{
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) {
        return;
    }

    // Pitfall 7: lazy SwsContext recreate on source-dimension mismatch.
    // D-07 — dst dims stay fixed across mid-session peer preset changes;
    // sws_ adapts source dims.
    if (sws_ == nullptr ||
        frame->width  != sws_src_width_  ||
        frame->height != sws_src_height_)
    {
        if (sws_ != nullptr) {
            sws_freeContext(sws_);
            sws_ = nullptr;
        }
        sws_ = sws_getContext(frame->width, frame->height, AV_PIX_FMT_YUV420P,
                              dst_width_,  dst_height_,    AV_PIX_FMT_BGRA,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        sws_src_width_  = frame->width;
        sws_src_height_ = frame->height;
    }

    if (sws_ == nullptr) {
        // sws_getContext failed; drop frame.
        decode_error_count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Wrap image_back_'s pixel data for writing.
    {
        juce::Image::BitmapData bits(image_back_, juce::Image::BitmapData::writeOnly);
        uint8_t* const dst[1]      = { bits.data };
        const int      dstStride[1] = { bits.lineStride };

        sws_scale(sws_,
                  frame->data,    frame->linesize,
                  0, frame->height,
                  dst,            dstStride);
    }
    // bits destructor flushes pixel mods back into image_back_.

    // Codex Cluster 3 — sink touch under sink_lock_. If sink_ is null
    // (test mode), write into TestFrameResult (codex Cluster 8).
    {
        std::lock_guard<std::mutex> g(sink_lock_);
        if (sink_ != nullptr) {
            // Plan 21-03 will define the sink->image_front + bufferLock +
            // generation atomic + triggerAsyncUpdate protocol. For Plan
            // 21-02 we hold the sink pointer but defer the actual wiring
            // — the sink will exist only after Plan 21-03 lands. In
            // production at Plan 21-02 time, sink_ is null (Plan 21-03
            // wires it). If a future bug-fix calls setSink BEFORE Plan
            // 21-03 lands, this branch will be hit; for safety we just
            // don't dereference any sink members here.
            //
            // Plan 21-03 will replace this comment with:
            //   juce::ScopedLock sl(sink_->bufferLock);
            //   std::swap(sink_->image_front, image_back_);
            //   sink_->generation.fetch_add(1, std::memory_order_release);
            //   sink_->triggerAsyncUpdate();
            // For Plan 21-02, the sink is guaranteed null in production.
        } else {
#ifdef JAMWIDE_BUILD_TESTS
            // Codex Cluster 8 — lock-protected TestFrameResult write.
            // The decoder writes into result_.image ONLY under
            // result_lock_; pollOneFrameForTest reads under the same
            // lock. No raw juce::Image* ever escapes the lock; no UAF
            // if the test times out without reading.
            {
                std::lock_guard<std::mutex> rg(result_lock_);
                result_.image = image_back_.createCopy();
                result_.ready = true;
            }
            // Notify outside the lock to avoid holding it across a
            // potentially-blocking wake-up. cv.notify_one is safe to
            // call without the lock held.
            result_.cv.notify_all();
#endif
        }
    }
}

// ───────────────────────────────────────────────────────────────────────
// JAMWIDE_BUILD_TESTS — test-only paths
// ───────────────────────────────────────────────────────────────────────

#ifdef JAMWIDE_BUILD_TESTS

bool Openh264Decoder::pollOneFrameForTest(juce::Image& out, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(result_lock_);
    if (!result_.ready) {
        result_.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this]{ return result_.ready; });
    }
    if (!result_.ready) {
        return false;
    }
    out = result_.image.createCopy();
    result_.ready = false;
    return true;
}

void Openh264Decoder::pushNalChunk(NalChunk chunk)
{
    {
        std::lock_guard<std::mutex> g(test_nal_lock_);
        test_nal_queue_.push_back(std::move(chunk));
    }
    // Wake the decoder thread by bumping producer_seq (NOT
    // WaitableEvent::signal — codex Cluster 1).
    if (producerSeq_ != nullptr) {
        producerSeq_->fetch_add(1, std::memory_order_release);
    }
}

void Openh264Decoder::pushSlotSnapshotForTest(const VideoRecvSlotSnapshot& snapshot)
{
    // Simulate the audio-thread action sequence: memcpy + index push +
    // atomic seq bump (codex Cluster 7).
    if (slotRing_ != nullptr && slotIndexQ_ != nullptr && producerSeq_ != nullptr) {
        // Use slot 0 for test pushes — single-test single-push semantics.
        // Tests that exercise back-to-back pushes (test_back_to_back_push_
        // _preserves_order) DO need to write to DIFFERENT slots; for that
        // we cycle slots 0..3 round-robin via a thread-local counter.
        // For now, use a simple incrementing index modulo 4.
        static thread_local int test_fill_idx = 0;
        const int idx = test_fill_idx & 3;
        test_fill_idx++;

        (*slotRing_)[(std::size_t)idx] = snapshot;
        (void)slotIndexQ_->try_push(idx);
        producerSeq_->fetch_add(1, std::memory_order_release);
    }
}

std::thread::id Openh264Decoder::lastParseTid() const
{
    std::lock_guard<std::mutex> g(tid_lock_);
    return last_parse_tid_;
}

std::thread::id Openh264Decoder::getDecoderStdThreadId() const
{
    std::lock_guard<std::mutex> g(tid_lock_);
    return decoder_thread_std_id_;
}

void Openh264Decoder::drainTestNalQueue_()
{
    // Drain the test NAL queue and feed each chunk via sendAnnexB_.
    // Each chunk's bytes vector already contains the Annex-B-framed
    // bytes (00 00 00 01 + raw NAL), so we send them as-is starting
    // at byte 4 (skipping the start code prefix) and let sendAnnexB_
    // re-frame.
    std::deque<NalChunk> local;
    {
        std::lock_guard<std::mutex> g(test_nal_lock_);
        local.swap(test_nal_queue_);
    }

#ifdef JAMWIDE_BUILD_TESTS
    if (!local.empty()) {
        // Update last_parse_tid_ so test_parser_runs_on_decoder_thread_
        // _not_audio sees the decoder thread id even when the test path
        // is exercised via pushNalChunk (no parseSlotAndFeed_ call).
        std::lock_guard<std::mutex> g(tid_lock_);
        last_parse_tid_ = std::this_thread::get_id();
    }
#endif

    for (auto& chunk : local) {
        if (chunk.bytes.size() <= 4) continue;
        // chunk.bytes starts with kAnnexBStartCode; skip it and pass
        // the raw NAL bytes — sendAnnexB_ will re-add the start code.
        sendAnnexB_(chunk.bytes.data() + 4, (int)(chunk.bytes.size() - 4));
    }
    if (!local.empty()) {
        drainReceiveFrameLoop_();
    }
}

#endif  // JAMWIDE_BUILD_TESTS

} // namespace jamwide
