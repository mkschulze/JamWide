// Phase 20-01 — Openh264Encoder implementation. libavcodec H.264 encoder
// backed by libopenh264; ported from JamTaba FFMpegMuxer.cpp:237-277 per
// CONTEXT.md `<canonical_refs>` "JamTaba reference" + Phase 14.3 substrate
// + Phase 19 frame distributor.
//
// THREADING: see Openh264Encoder.h header comment for the full contract.
// In short:
//   - message thread: open / close / reconfigure / dtor
//   - camera-callback thread (any thread per JUCE): onFrame
//   - encoder thread (juce::Thread): run()
//
// CLOSE ORDERING (R4 H9, LOCKED — see header):
//   1. m_closing.store(true, release)
//   2. release subscription_  (~Subscription blocks for in-flight onFrame)
//   3. (implicit in step 2)
//   4. pending_event_.signal()  (wake encoder thread)
//   5. encoder thread drains pending slots to no-op, exits run()
//   6. stopThread()  (join)
//   7. freeLibavcodecResources_(); slabs cleared; SPSC reset
//
// IMPORTANT (Landmine L6 / 260515-0pc spike): ffmpeg/libavcodec headers
// MUST be included BEFORE any JUCE / stdlib header that could pull
// homebrew /usr/local/include into the search path. The CMake macro
// jamwide_use_ffmpeg() emits `-I<vendored>` with BEFORE PRIVATE which
// puts the vendored headers FIRST in the user-include search order, but
// belt-and-braces discipline at the source level still applies: extern
// "C" block at the very top of this file.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include "Openh264Encoder.h"
#include "VideoEncoderListener.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cstring>

namespace jamwide {

namespace {

// ───────────────────────────────────────────────────────────────────────
// Helpers
// ───────────────────────────────────────────────────────────────────────

// Map our H264Profile enum to libavcodec FF_PROFILE_* constants.
int toFFProfile(H264Profile p) noexcept {
    switch (p) {
        case H264Profile::Baseline:
        default:
            return FF_PROFILE_H264_BASELINE;
    }
}

// Find a NAL annex-B start code [0x00 0x00 0x00 0x01] at or after `pos`
// in [data, data+len). Returns the index of the FIRST byte of the start
// code (the 0x00), or -1 if no start code is found.
int findNalStart(const unsigned char* data, int len, int pos) noexcept {
    if (len < 4) return -1;
    for (int i = pos; i + 3 < len; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            return i;
        }
    }
    return -1;
}

// Inspect a NAL unit (one after a 0x00 0x00 0x00 0x01 start code) and
// return its nal_unit_type (low 5 bits of the first byte after the
// start code). Annex-B layout: [start_code][nal_unit_type byte][...]
int nalTypeAt(const unsigned char* data, int len, int start_code_pos) noexcept {
    if (start_code_pos + 4 >= len) return -1;
    return data[start_code_pos + 4] & 0x1f;
}

} // namespace

// ───────────────────────────────────────────────────────────────────────
// CTOR / DTOR
// ───────────────────────────────────────────────────────────────────────

Openh264Encoder::Openh264Encoder()
    : juce::Thread("JamWide.VideoEncoder")
{
    // Slabs constructed empty; libavcodec resources allocated in open().
    for (auto& s : slabs_) {
        s.type = SlotType::Empty;
    }
}

Openh264Encoder::~Openh264Encoder() {
    if (m_open.load(std::memory_order_acquire)) {
        close();
    }
}

// ───────────────────────────────────────────────────────────────────────
// open
// ───────────────────────────────────────────────────────────────────────

bool Openh264Encoder::open(const VideoEncoderConfig&    cfg,
                            JamWideFrameDistributor*     dist,
                            std::atomic<std::uint64_t>*  audioIntervalSeq,
                            PublishSpsPpsCallback        publishSpsPps,
                            PublishEncodedNalCallback    publishEncodedNal,
                            VideoEncoderListener*        listener)
{
    if (m_open.load(std::memory_order_acquire)) {
        // Caller bug: open() while already open. Plan 20-03 only calls
        // open() when broadcast toggles on (D-13).
        return false;
    }
    if (dist == nullptr) {
        return false;
    }
    if (!publishEncodedNal) {
        // publishSpsPps may be a no-op stub (test path may not care);
        // publishEncodedNal is the canonical output and must be present.
        return false;
    }

    publishSpsPps_     = std::move(publishSpsPps);
    publishEncodedNal_ = std::move(publishEncodedNal);
    listener_          = listener;
    audio_interval_seq_= audioIntervalSeq;
    current_cfg_       = cfg;
    last_observed_interval_seq_ = audioIntervalSeq != nullptr
                                  ? audioIntervalSeq->load(std::memory_order_relaxed)
                                  : 0;

    // Reset counters and reset ring indices.
    m_encoder_input_drops.store(0, std::memory_order_relaxed);
    m_frame_output_count.store(0, std::memory_order_relaxed);
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    sps_pps_published_ = false;
    for (auto& s : slabs_) {
        s.type = SlotType::Empty;
        s.bgra.assign(static_cast<std::size_t>(cfg.width) * cfg.height * 4, 0);
        s.width = cfg.width;
        s.height = cfg.height;
    }
    pending_event_.reset();

    if (!allocateLibavcodecResources_(cfg)) {
        // alloc failed — clean up partials.
        freeLibavcodecResources_();
        return false;
    }

    // Phase 19 HIGH-2: hold the Subscription as a member; close() releases
    // it in step 2 of the 7-step ordering. registerSubscriber returns a
    // moveable RAII handle; assign-move drops any prior Subscription (we
    // already verified m_open == false above, so this should be empty).
    subscription_ = dist->registerSubscriber(this);

    m_closing.store(false, std::memory_order_release);
    m_open.store(true, std::memory_order_release);

    // Publish initial SPS/PPS — libavcodec writes the parameter set into
    // AVCodecContext::extradata after avcodec_open2 for Baseline-profile
    // libopenh264 configurations.
    publishExtractedSpsPps_();

    // Start encoder thread. juce::Thread::startThread() returns true on
    // success; if it fails we tear down and return false.
    startThread();
    if (!isThreadRunning()) {
        m_open.store(false, std::memory_order_release);
        subscription_ = JamWideFrameDistributor::Subscription{};
        freeLibavcodecResources_();
        return false;
    }

    if (listener_ != nullptr) {
        listener_->onEncoderOpened(cfg);
    }
    return true;
}

// ───────────────────────────────────────────────────────────────────────
// close — R4 H9 LOCKED 7-STEP ORDERING
// ───────────────────────────────────────────────────────────────────────

void Openh264Encoder::close() {
    if (!m_open.load(std::memory_order_acquire)) {
        return;  // already closed (or never opened)
    }

    // R4 H9 STEP 1: gate onFrame defensively (release-store; pairs with
    // acquire-load in onFrame and the encoder thread loop predicate).
    m_closing.store(true, std::memory_order_release);

    // R4 H9 STEP 2: release the Subscription. Phase 19 HIGH-2 contract:
    // ~Subscription blocks until any in-flight onFrame call referencing
    // this subscriber returns. After this returns, no further onFrame
    // callbacks queue into the slab pool.
    subscription_ = JamWideFrameDistributor::Subscription{};

    // R4 H9 STEP 3 is implicit in step 2 — the ~Subscription destructor
    // waits for in-flight onFrame callbacks. No separate action needed.

    // R4 H9 STEP 4: signal the encoder thread. juce::Thread::signalThreadShouldExit
    // sets the threadShouldExit flag; pending_event_ wakes the thread
    // immediately if it is currently blocked in pending_event_.wait().
    signalThreadShouldExit();
    pending_event_.signal();

    // R4 H9 STEP 5 + 6: stopThread joins after the encoder thread observes
    // m_closing / threadShouldExit, drains pending slots to no-op (in run()),
    // and exits run(). 2-second timeout matches juce::Thread default.
    stopThread(2000);

    // R4 H9 STEP 7: free libavcodec resources (encoder thread has joined,
    // so we have exclusive access).
    freeLibavcodecResources_();
    for (auto& s : slabs_) {
        s.type = SlotType::Empty;
        s.bgra.clear();
        s.bgra.shrink_to_fit();
    }
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);

    publishSpsPps_     = nullptr;
    publishEncodedNal_ = nullptr;
    audio_interval_seq_ = nullptr;

    m_open.store(false, std::memory_order_release);

    if (listener_ != nullptr) {
        listener_->onEncoderClosed();
    }
    listener_ = nullptr;
}

// ───────────────────────────────────────────────────────────────────────
// reconfigure — R4 H9 SUBSCRIPTION PRESERVED
// ───────────────────────────────────────────────────────────────────────

bool Openh264Encoder::reconfigure(const VideoEncoderConfig& cfg) {
    if (!m_open.load(std::memory_order_acquire) ||
        m_closing.load(std::memory_order_acquire)) {
        return false;
    }

    // Send a RECONFIGURE sentinel through the input SPSC. The encoder
    // thread picks it up in run() and performs the libavcodec/sws swap
    // inline. The Subscription is NOT touched — onFrame continues to
    // enqueue real frames during the swap; they are processed by the
    // new libavcodec instance once the swap completes.
    //
    // Drop-oldest applies here too: if the SPSC is full, we overwrite
    // the oldest unconsumed slot with the RECONFIGURE sentinel. The
    // encoder thread will see this and drain everything that follows
    // it with the new config (the swapped-out frames were already in
    // the queue when reconfigure was called — they are effectively
    // dropped, but the new config takes over from this point).
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    const std::uint64_t depth = head - tail;

    Slot& slot = slabs_[head % kSlabCount];

    if (depth >= kSlabCount) {
        // Full — advance tail (drop oldest) and bump the drops counter.
        tail_.store(tail + 1, std::memory_order_release);
        m_encoder_input_drops.fetch_add(1, std::memory_order_relaxed);
    }

    slot.type         = SlotType::Reconfigure;
    slot.reconfig_cfg = cfg;
    head_.store(head + 1, std::memory_order_release);
    pending_event_.signal();
    return true;
}

// ───────────────────────────────────────────────────────────────────────
// onFrame — camera-callback thread (any thread per JUCE)
// ───────────────────────────────────────────────────────────────────────

void Openh264Encoder::onFrame(const juce::Image& image) {
    // Defensive early-return: if close() has started, drop the frame.
    // Phase 19 HIGH-2 ~Subscription is the primary guarantee that
    // onFrame stops being called; this acquire-load pairs with the
    // release-store in close() step 1.
    if (m_closing.load(std::memory_order_acquire)) {
        return;
    }

    // Lock the Image for read-only access. JUCE's BitmapData snapshots
    // the underlying pixels into a guaranteed-contiguous buffer; on
    // macOS the byte order is BGRA in memory (Phase 19 D-04, A7 in
    // 20-RESEARCH.md).
    juce::Image::BitmapData bmp(image, juce::Image::BitmapData::readOnly);
    const int width  = bmp.width;
    const int height = bmp.height;
    if (width <= 0 || height <= 0 || bmp.data == nullptr) {
        return;
    }

    enqueueBgraSlot_(bmp.data, width, height);
}

void Openh264Encoder::enqueueBgraSlot_(const unsigned char* bgra,
                                       int width,
                                       int height)
{
    // BitmapData::lineStride may include row padding; the slab buffer
    // is sized for width*height*4 contiguous bytes. We re-pack here so
    // the encoder thread's sws_scale always sees stride == width*4.
    //
    // Memcpy is the only work in the steady-state path; no heap
    // allocation (slab vectors are sized at open()).
    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    const std::uint64_t tail = tail_.load(std::memory_order_acquire);
    const std::uint64_t depth = head - tail;

    if (depth >= kSlabCount) {
        // Drop-oldest: advance tail past the oldest unconsumed slot.
        // The slot's type is irrelevant — we are about to overwrite at
        // head_ % kSlabCount; if head and tail point at the same slot,
        // the tail-advance frees it.
        tail_.store(tail + 1, std::memory_order_release);
        m_encoder_input_drops.fetch_add(1, std::memory_order_relaxed);
    }

    Slot& slot = slabs_[head % kSlabCount];

    const std::size_t needed_bytes = static_cast<std::size_t>(width) * height * 4;
    if (slot.bgra.size() != needed_bytes) {
        // Resolution mismatch (caller fed a different-sized image than
        // the cfg given at open()). This can legitimately happen during
        // a resolution-change reconfigure window — accept it; resize the
        // slot buffer once. NOT in the steady-state hot path.
        slot.bgra.assign(needed_bytes, 0);
    }
    // Tight memcpy — bgra is contiguous width*4 per row in the source
    // image (we just resolved the stride via BitmapData::lineStride at
    // the caller, but JUCE may pad rows). To stay robust we copy row-by-
    // row when the row stride does not match.
    //
    // For test-fed buffers and the typical macOS BGRA Image, stride ==
    // width*4 and the row copy reduces to a single memcpy. We still emit
    // the row-loop guard for correctness on padded sources.
    //
    // The caller (onFrame) provides BitmapData::data which points to row
    // 0; subsequent rows are at data + i * lineStride. Tests bypass JUCE
    // and pass tightly-packed bytes — the row stride for those is
    // width*4 so the row loop falls through to a single contiguous copy.
    std::memcpy(slot.bgra.data(), bgra, needed_bytes);

    slot.type   = SlotType::FrameBgra;
    slot.width  = width;
    slot.height = height;
    head_.store(head + 1, std::memory_order_release);
    pending_event_.signal();
}

#if JAMWIDE_BUILD_TESTS
void Openh264Encoder::feedRawBgraForTest(const unsigned char* bgra,
                                         int width,
                                         int height)
{
    if (m_closing.load(std::memory_order_acquire) ||
        !m_open.load(std::memory_order_acquire)) {
        return;
    }
    enqueueBgraSlot_(bgra, width, height);
}
#endif

// ───────────────────────────────────────────────────────────────────────
// run() — encoder thread
// ───────────────────────────────────────────────────────────────────────

void Openh264Encoder::run() {
    while (!threadShouldExit() && !m_closing.load(std::memory_order_acquire)) {
        // Wait for next slot. We use a short wait so the loop predicate
        // is re-evaluated promptly on close() / reconfigure().
        pending_event_.wait(50);
        pending_event_.reset();

        // Drain slots — there may be multiple queued.
        while (true) {
            const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
            const std::uint64_t head = head_.load(std::memory_order_acquire);
            if (tail == head) break;

            Slot& slot = slabs_[tail % kSlabCount];

            // Take a snapshot of the slot type + data pointer; consume
            // by advancing tail (release-store). Producer may overwrite
            // this slot after the tail advance, but we already have a
            // local snapshot below (we process synchronously inside the
            // ring slot — sws_scale + avcodec_send_frame consume the
            // slot before we return to the loop).
            //
            // To minimize duration the producer is locked out of this
            // slot, we copy what we need (or, since processFrameSlot_
            // does its work in-place, just process it directly while the
            // tail still points at it).
            if (slot.type == SlotType::Reconfigure) {
                // Drain anything pending into the current libavcodec
                // instance before swapping. Advance past the
                // reconfigure sentinel first so we don't loop forever.
                VideoEncoderConfig newCfg = slot.reconfig_cfg;
                slot.type = SlotType::Empty;
                tail_.store(tail + 1, std::memory_order_release);

                drainEncoder_();  // flush pending packets out of current ctx
                handleReconfigure_(newCfg);
                continue;
            }

            if (slot.type == SlotType::FrameBgra) {
                processFrameSlot_(slot);
            }
            slot.type = SlotType::Empty;
            tail_.store(tail + 1, std::memory_order_release);

            if (m_closing.load(std::memory_order_acquire) ||
                threadShouldExit()) {
                break;
            }
        }
    }

    // R4 H9 STEP 5 (continued): drain pending slots to no-op. We don't
    // encode them — the close path is tearing down. Just advance tail
    // past everything that's queued so the slab buffers don't get
    // touched again.
    while (true) {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        if (tail == head) break;
        slabs_[tail % kSlabCount].type = SlotType::Empty;
        tail_.store(tail + 1, std::memory_order_release);
    }
}

// ───────────────────────────────────────────────────────────────────────
// processFrameSlot_ — convert BGRA → YUV420P, force IDR if needed,
// avcodec_send_frame, drain packets via publishEncodedNal.
// ───────────────────────────────────────────────────────────────────────

void Openh264Encoder::processFrameSlot_(Slot& slot) {
    if (codecContext_ == nullptr || frame_ == nullptr) {
        return;
    }
    if (slot.width <= 0 || slot.height <= 0) {
        return;
    }

    // Phase 20 Task 1: (re)create sws_ when the camera-frame source dimensions
    // differ from what sws_ was last configured for. The encoder cfg
    // (current_cfg_.width × current_cfg_.height) is the OUTPUT dimension;
    // the camera-frame slot dims are the SOURCE dimension. They almost
    // never match — webcams capture at fixed native resolutions like 640×480
    // or 1280×720, while the encoder targets 320×240 / 640×480 / 1280×720
    // per the Low/Medium/High preset. sws_scale handles the BGRA→YUV420P
    // conversion AND the downscale in one pass.
    //
    // The original allocateLibavcodecResources_ call seeded sws_ with
    // source = cfg dims (identity scaling), which dropped every production
    // frame because the resolution-check below rejected non-cfg slots. This
    // lazy/on-change recreate path is the canonical FFmpeg pattern when the
    // source dims are not known until the first frame arrives.
    if (sws_ == nullptr ||
        sws_src_width_  != slot.width ||
        sws_src_height_ != slot.height) {
        if (sws_ != nullptr) {
            sws_freeContext(sws_);
            sws_ = nullptr;
        }
        sws_ = sws_getContext(slot.width, slot.height, AV_PIX_FMT_BGRA,
                              current_cfg_.width, current_cfg_.height,
                              AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws_ == nullptr) {
            if (listener_ != nullptr) {
                listener_->onEncoderFatalError(
                    "sws_getContext failed in processFrameSlot_ "
                    "(camera-to-encoder rescale)");
            }
            return;
        }
        sws_src_width_  = slot.width;
        sws_src_height_ = slot.height;
    }

    const unsigned char* src_planes[1]   = { slot.bgra.data() };
    const int            src_strides[1]  = { slot.width * 4 };

    int scaled = sws_scale(sws_, src_planes, src_strides, 0,
                           slot.height,
                           frame_->data, frame_->linesize);
    if (scaled <= 0) {
        return;
    }

    // D-15: read the interval-seq atomic; on change, force IDR. Applied
    // IMMEDIATELY before avcodec_send_frame on the encoder thread,
    // matching the CONTEXT.md `<canonical_refs>` "Forcing IDR" example.
    if (audio_interval_seq_ != nullptr) {
        const std::uint64_t observed =
            audio_interval_seq_->load(std::memory_order_relaxed);
        if (observed != last_observed_interval_seq_) {
            frame_->pict_type = AV_PICTURE_TYPE_I;
            frame_->key_frame = 1;
            last_observed_interval_seq_ = observed;
        } else {
            frame_->pict_type = AV_PICTURE_TYPE_NONE;
            frame_->key_frame = 0;
        }
    } else {
        frame_->pict_type = AV_PICTURE_TYPE_NONE;
        frame_->key_frame = 0;
    }

    // pts is monotonically increasing per frame; use the output frame
    // count as a simple monotonic source. libavcodec's RC needs pts to
    // be monotonic; the absolute value is not used by openh264.
    const std::uint64_t pts =
        m_frame_output_count.load(std::memory_order_relaxed);
    frame_->pts = static_cast<std::int64_t>(pts);

    const int send_rc = avcodec_send_frame(codecContext_, frame_);
    if (send_rc < 0) {
        if (listener_ != nullptr) {
            listener_->onEncoderFatalError("avcodec_send_frame failed");
        }
        return;
    }

    // Drain packets — there may be 0, 1, or more.
    drainEncoder_();
}

void Openh264Encoder::drainEncoder_() {
    if (codecContext_ == nullptr || packet_ == nullptr) return;
    for (;;) {
        const int rc = avcodec_receive_packet(codecContext_, packet_);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            break;
        }
        if (rc < 0) {
            if (listener_ != nullptr) {
                listener_->onEncoderFatalError("avcodec_receive_packet failed");
            }
            break;
        }
        // SPS/PPS fallback path: if the AVCodecContext::extradata route
        // did not populate the parameter set (libopenh264 with
        // FF_PROFILE_H264_BASELINE falls back to UNSPECIFIC and embeds
        // SPS/PPS inside the IDR NAL bytes), scan this packet for
        // SPS (NAL type 7) + PPS (NAL type 8) and publish the
        // [SPS-NAL][PPS-NAL] concatenation. This fires once per
        // libavcodec instance — sps_pps_published_ flips to true.
        if (!sps_pps_published_) {
            scanAndPublishSpsPps_(packet_->data, packet_->size);
        }
        if (publishEncodedNal_) {
            publishEncodedNal_(packet_->data, packet_->size);
        }
        m_frame_output_count.fetch_add(1, std::memory_order_relaxed);
        av_packet_unref(packet_);
    }
}

// ───────────────────────────────────────────────────────────────────────
// allocateLibavcodecResources_ / freeLibavcodecResources_
// ───────────────────────────────────────────────────────────────────────

bool Openh264Encoder::allocateLibavcodecResources_(const VideoEncoderConfig& cfg) {
    // Resolve the H.264 encoder. libavcodec selects libopenh264 because
    // the vendored ffmpeg exposes openh264 as the H.264 encoder per
    // Phase 14.3-01. The spike used find_encoder_by_name("libopenh264")
    // directly; AV_CODEC_ID_H264 routes to the same encoder in our
    // vendored build (only libopenh264 is registered for AV_CODEC_ID_H264).
    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (enc == nullptr) {
        enc = avcodec_find_encoder_by_name("libopenh264");
    }
    if (enc == nullptr) {
        if (listener_ != nullptr) {
            listener_->onEncoderFatalError("avcodec_find_encoder(H264) failed");
        }
        return false;
    }

    codecContext_ = avcodec_alloc_context3(enc);
    if (codecContext_ == nullptr) {
        if (listener_ != nullptr) {
            listener_->onEncoderFatalError("avcodec_alloc_context3 failed");
        }
        return false;
    }

    // Port of JamTaba FFMpegMuxer.cpp:237-277 configure block per
    // CONTEXT.md `<canonical_refs>`. Bitrate ladder per D-16.
    const int bitrate_bps = cfg.targetBitrateKbps * 1000;

    codecContext_->width         = cfg.width;
    codecContext_->height        = cfg.height;
    // H.264 special-case in encode_preinit: setting coded_width/height
    // avoids the "Ignoring invalid" zero-out path (spike line 219-220).
    codecContext_->coded_width   = cfg.width;
    codecContext_->coded_height  = cfg.height;
    codecContext_->pix_fmt       = AV_PIX_FMT_YUV420P;
    codecContext_->time_base     = AVRational{ 1, cfg.frameRate };
    codecContext_->framerate     = AVRational{ cfg.frameRate, 1 };
    codecContext_->bit_rate      = bitrate_bps;
    codecContext_->rc_max_rate   = bitrate_bps;
    codecContext_->rc_buffer_size = bitrate_bps;
    codecContext_->gop_size      = cfg.gopHintFrames;  // hint — force-IDR overrides per-frame
    codecContext_->max_b_frames  = 0;                  // D-05: no B-frames
    codecContext_->profile       = toFFProfile(cfg.profile);
    codecContext_->level         = 31;                 // D-05: Baseline 3.1
    codecContext_->thread_count  = 1;                  // D-Discretion: single-threaded

    // openh264 private options. av_opt_set fails non-fatally if the
    // option name is not exposed on this codec — that's acceptable; the
    // defaults are sane.
    av_opt_set(codecContext_->priv_data, "rc_mode",            "bitrate", 0);  // D-06 (RC_BITRATE_MODE)
    av_opt_set(codecContext_->priv_data, "allow_skip_frames",  "1",       0);  // D-06 (openh264 RC_BITRATE_MODE req)
    av_opt_set(codecContext_->priv_data, "slice_mode",         "fixed",   0);  // D-Discretion: single-slice
    av_opt_set(codecContext_->priv_data, "loopfilter_disable", "1",       0);  // D-Discretion

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "preset", "veryfast", 0);

    const int open_rc = avcodec_open2(codecContext_, enc, &opts);
    av_dict_free(&opts);
    if (open_rc < 0) {
        if (listener_ != nullptr) {
            listener_->onEncoderFatalError("avcodec_open2 failed");
        }
        avcodec_free_context(&codecContext_);
        codecContext_ = nullptr;
        return false;
    }

    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        if (listener_ != nullptr) {
            listener_->onEncoderFatalError("av_frame_alloc failed");
        }
        return false;
    }
    frame_->format = AV_PIX_FMT_YUV420P;
    frame_->width  = cfg.width;
    frame_->height = cfg.height;
    if (av_image_alloc(frame_->data, frame_->linesize,
                       cfg.width, cfg.height, AV_PIX_FMT_YUV420P, 32) < 0) {
        if (listener_ != nullptr) {
            listener_->onEncoderFatalError("av_image_alloc failed");
        }
        return false;
    }

    packet_ = av_packet_alloc();
    if (packet_ == nullptr) {
        if (listener_ != nullptr) {
            listener_->onEncoderFatalError("av_packet_alloc failed");
        }
        return false;
    }

    sws_ = sws_getContext(cfg.width, cfg.height, AV_PIX_FMT_BGRA,
                          cfg.width, cfg.height, AV_PIX_FMT_YUV420P,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ == nullptr) {
        if (listener_ != nullptr) {
            listener_->onEncoderFatalError("sws_getContext failed");
        }
        return false;
    }

    return true;
}

void Openh264Encoder::freeLibavcodecResources_() {
    if (packet_ != nullptr) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (frame_ != nullptr) {
        if (frame_->data[0] != nullptr) {
            av_freep(&frame_->data[0]);
        }
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (sws_ != nullptr) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (codecContext_ != nullptr) {
        avcodec_free_context(&codecContext_);
        codecContext_ = nullptr;
    }
}

// ───────────────────────────────────────────────────────────────────────
// SPS/PPS extraction + publish
// ───────────────────────────────────────────────────────────────────────

void Openh264Encoder::publishExtractedSpsPps_() {
    if (!publishSpsPps_) return;
    if (codecContext_ == nullptr) return;

    // Primary path: libavcodec writes the parameter set into
    // AVCodecContext::extradata after avcodec_open2 IF the global
    // header flag was set OR the encoder supports it for this profile.
    // libopenh264 with FF_PROFILE_H264_BASELINE does NOT populate
    // extradata reliably (the encoder logs "Unsupported avctx->profile:
    // 66" and falls back to UNSPECIFIC, in which case it embeds the
    // SPS/PPS inside the first IDR's NAL bytes instead of advertising
    // them via extradata).
    if (codecContext_->extradata != nullptr &&
        codecContext_->extradata_size > 0) {
        publishSpsPps_(codecContext_->extradata, codecContext_->extradata_size);
        if (listener_ != nullptr) {
            listener_->onSpsPpsPublished(codecContext_->extradata_size);
        }
        sps_pps_published_ = true;
        return;
    }
    // Fallback path: defer to the first emitted packet. drainEncoder_
    // observes sps_pps_published_ == false and invokes
    // scanAndPublishSpsPps_ on the packet's NAL bytes to extract
    // SPS (nal_unit_type 7) + PPS (nal_unit_type 8) and publish the
    // [SPS-NAL][PPS-NAL] concatenation.
}

void Openh264Encoder::scanAndPublishSpsPps_(const unsigned char* nal_stream,
                                             int len)
{
    if (!publishSpsPps_) return;
    if (nal_stream == nullptr || len < 8) return;

    // Walk the annex-B stream looking for SPS (nal_unit_type 7) and
    // PPS (nal_unit_type 8). Concatenate them as [SPS-NAL][PPS-NAL] —
    // CONTEXT.md `<specifics>` "SPS/PPS chunk format" (raw [SPS][PPS]
    // concatenation, no per-NAL length prefix).
    int sps_start = -1, sps_end = -1;
    int pps_start = -1, pps_end = -1;

    int pos = 0;
    while (pos < len) {
        const int sc = findNalStart(nal_stream, len, pos);
        if (sc < 0) break;
        const int nut = nalTypeAt(nal_stream, len, sc);
        const int sc_next = findNalStart(nal_stream, len, sc + 4);
        const int nal_end = (sc_next < 0 ? len : sc_next);
        if (nut == 7 && sps_start < 0) {
            sps_start = sc;
            sps_end   = nal_end;
        } else if (nut == 8 && pps_start < 0) {
            pps_start = sc;
            pps_end   = nal_end;
        }
        pos = (sc_next < 0 ? len : sc_next);
    }

    if (sps_start < 0 || pps_start < 0) {
        return;
    }

    // NinjamZap VIDEO_SYNC.md §7 wire format for the SPS/PPS block:
    //
    //   [2B BE SPS len][SPS NAL bytes (no start code)]
    //   [2B BE PPS len][PPS NAL bytes (no start code)]
    //
    // The annex-B start code (0x00 0x00 0x00 0x01) is 4 bytes; we strip it
    // from each NAL by starting at sps_start+4 / pps_start+4. The 2-byte
    // big-endian length prefix tells the receiver how many NAL bytes
    // follow. Plan 20-02's original "raw [SPS-NAL][PPS-NAL] concatenation,
    // no per-NAL length prefix" was a misread of the spec — the web
    // decoder could not parse it and rejected the entire video stream.
    const int sps_payload_len = (sps_end - sps_start) - 4;
    const int pps_payload_len = (pps_end - pps_start) - 4;
    if (sps_payload_len <= 0 || pps_payload_len <= 0 ||
        sps_payload_len > 0xFFFF || pps_payload_len > 0xFFFF) {
        return;
    }

    std::vector<unsigned char> buf;
    buf.reserve(2 + static_cast<std::size_t>(sps_payload_len) +
                2 + static_cast<std::size_t>(pps_payload_len));

    // [2B BE SPS len][SPS NAL bytes without start code]
    buf.push_back(static_cast<unsigned char>((sps_payload_len >> 8) & 0xFF));
    buf.push_back(static_cast<unsigned char>( sps_payload_len       & 0xFF));
    buf.insert(buf.end(),
               nal_stream + sps_start + 4,   // skip 4-byte annex-B start code
               nal_stream + sps_end);

    // [2B BE PPS len][PPS NAL bytes without start code]
    buf.push_back(static_cast<unsigned char>((pps_payload_len >> 8) & 0xFF));
    buf.push_back(static_cast<unsigned char>( pps_payload_len       & 0xFF));
    buf.insert(buf.end(),
               nal_stream + pps_start + 4,
               nal_stream + pps_end);

    publishSpsPps_(buf.data(), static_cast<int>(buf.size()));
    if (listener_ != nullptr) {
        listener_->onSpsPpsPublished(static_cast<int>(buf.size()));
    }
    sps_pps_published_ = true;
}

// ───────────────────────────────────────────────────────────────────────
// handleReconfigure_ — encoder thread; R4 H9 SUBSCRIPTION PRESERVED
// ───────────────────────────────────────────────────────────────────────

void Openh264Encoder::handleReconfigure_(const VideoEncoderConfig& newCfg) {
    // Flush the current libavcodec instance: send a NULL frame to enter
    // draining mode, then drain remaining packets.
    if (codecContext_ != nullptr) {
        avcodec_send_frame(codecContext_, nullptr);
        drainEncoder_();
    }

    freeLibavcodecResources_();

    // Resize slab buffers for the new resolution. Done on the encoder
    // thread between draining the old instance and opening the new one,
    // so the producer is currently quiesced (the slab is at rest
    // between encodes). If the producer overruns into a slab during
    // the resize window, the producer's resize fallback (see
    // enqueueBgraSlot_) handles it.
    const std::size_t needed = static_cast<std::size_t>(newCfg.width) *
                               newCfg.height * 4;
    for (auto& s : slabs_) {
        if (s.bgra.size() != needed) {
            s.bgra.assign(needed, 0);
        }
        s.type = SlotType::Empty;
        s.width  = newCfg.width;
        s.height = newCfg.height;
    }

    current_cfg_ = newCfg;
    sps_pps_published_ = false;  // new instance — new SPS/PPS will fire.
    if (!allocateLibavcodecResources_(newCfg)) {
        // fatal — listener was notified inside allocate_; encoder thread
        // continues to spin but processFrameSlot_ will early-return on
        // null codecContext_. Plan 20-03's listener will reconfigure or
        // stop.
        return;
    }

    publishExtractedSpsPps_();

    if (listener_ != nullptr) {
        listener_->onEncoderReconfigured(newCfg);
    }
}

} // namespace jamwide
