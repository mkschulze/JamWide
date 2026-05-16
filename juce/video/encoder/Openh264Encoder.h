#pragma once
// Phase 20-01 — Openh264Encoder: libavcodec H.264 encoder with the
// libopenh264 backend (D-01 / D-02). Concrete implementation of the
// abstract VideoEncoder interface.
//
// Threading model:
//   - open() / close() / reconfigure() called from the message thread
//     (Plan 20-03 owner).
//   - onFrame() called from the camera-callback thread (JUCE "any
//     thread") via the JamWideFrameDistributor::Subscription. Copies
//     BGRA bytes into a pre-allocated slot in the slab pool; signals
//     the encoder thread; no allocation in steady state.
//   - Encoder thread (juce::Thread subclass) runs run() — picks slots
//     from the input SPSC, runs sws_scale for BGRA→YUV420P, sends to
//     libavcodec, drains packets, invokes publishEncodedNal callback.
//     Reads the injected audio-interval-seq atomic before each encode;
//     on change, sets frame_->pict_type=AV_PICTURE_TYPE_I to force IDR
//     (D-15).
//
// Lifecycle (R4 H9 LOCKED 7-step close ordering):
//   1. m_closing.store(true, release)
//   2. release JamWideFrameDistributor::Subscription
//   3. (implicit — Phase 19 HIGH-2 ~Subscription blocks for in-flight)
//   4. signal encoder thread via WaitableEvent
//   5. encoder thread observes m_closing, drains pending, exits loop
//   6. join encoder thread
//   7. free libavcodec / sws / slabs / SPSC ring
//
// Reconfigure path (R4 H9 — Subscription preserved): a RECONFIGURE
// sentinel slot in the SPSC carries the new config. The encoder thread
// drains pending real frames through the current libavcodec instance,
// closes it, opens a new instance with the new config, regenerates
// SPS/PPS, invokes publishSpsPps, listener_->onEncoderReconfigured.
// The Subscription is NOT touched — frames arriving during the swap
// are buffered in the slab pool.

#include "VideoEncoder.h"
#include "VideoEncoderConfig.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "../native/JamWideFrameDistributor.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Forward-declare libavcodec / libswscale types — keeps the header free
// of ffmpeg includes. The .cpp file is the only consumer of the actual
// AVCodecContext / AVFrame / SwsContext layouts.
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace jamwide {

class Openh264Encoder
    : public VideoEncoder
    , public JamWideFrameDistributor::Subscriber
    , private juce::Thread
{
public:
    Openh264Encoder();
    ~Openh264Encoder() override;

    Openh264Encoder(const Openh264Encoder&) = delete;
    Openh264Encoder& operator=(const Openh264Encoder&) = delete;

    // VideoEncoder
    bool open(const VideoEncoderConfig&    cfg,
              JamWideFrameDistributor*     dist,
              std::atomic<std::uint64_t>*  audioIntervalSeq,
              PublishSpsPpsCallback        publishSpsPps,
              PublishEncodedNalCallback    publishEncodedNal,
              VideoEncoderListener*        listener = nullptr) override;

    void close() override;
    bool reconfigure(const VideoEncoderConfig& cfg) override;

    std::uint64_t getInputDropCount()   const noexcept override {
        return m_encoder_input_drops.load(std::memory_order_relaxed);
    }
    std::uint64_t getFrameOutputCount() const noexcept override {
        return m_frame_output_count.load(std::memory_order_relaxed);
    }

    // JamWideFrameDistributor::Subscriber — called on the camera-callback
    // thread; never blocks; copies into the slab pool and signals the
    // encoder thread.
    void onFrame(const juce::Image& image) override;

#if JAMWIDE_BUILD_TESTS
    // Test-only path: feed a raw BGRA buffer directly into the input SPSC
    // without going through the JamWideFrameDistributor. Used by
    // tests/test_video_encoder.cpp so the test does not need to fake a
    // juce::Image owner. Pre-conditions: encoder open(); cfg.width *
    // cfg.height * 4 == bytes.size().
    void feedRawBgraForTest(const unsigned char* bgra, int width, int height);

    // Returns the raw pointer to the embedded juce::Thread for tests
    // that need to verify thread-state.
    juce::Thread* getEncoderThreadForTest() noexcept { return this; }
#endif

private:
    // ───── slab-pool / SPSC input ring ─────
    // Pitfall #3: pre-allocated input slot pool sized to the worst-case
    // BGRA payload (1280×720×4 = 3.6 MB at High). 4 slots is enough
    // because the consumer (encoder thread) runs at ≥ input frame rate
    // in steady state; under sustained overrun the drop-oldest semantics
    // (D-07) kick in and bump m_encoder_input_drops.
    static constexpr int kSlabCount = 4;

    enum class SlotType : std::uint8_t {
        Empty       = 0,
        FrameBgra   = 1,
        Reconfigure = 2,
    };

    struct Slot {
        std::vector<unsigned char>  bgra;          // width*height*4 BGRA bytes when FrameBgra
        VideoEncoderConfig          reconfig_cfg{};// only valid when type == Reconfigure
        SlotType                    type = SlotType::Empty;
        int                         width  = 0;
        int                         height = 0;
    };

    // SPSC indices — producer (onFrame on camera-callback thread) advances
    // head_; consumer (encoder thread) advances tail_. Drop-oldest applies
    // when head_+1 == tail_ at producer side: advance tail_ (drops oldest
    // unread slot), bump m_encoder_input_drops, then enqueue at head_.
    Slot                                     slabs_[kSlabCount];
    std::atomic<std::uint64_t>               head_{0};
    std::atomic<std::uint64_t>               tail_{0};
    juce::WaitableEvent                      pending_event_;  // signaled by onFrame, waited by run loop
    std::atomic<std::uint64_t>               m_encoder_input_drops{0};
    std::atomic<std::uint64_t>               m_frame_output_count{0};

    // ───── libavcodec state — encoder thread owns this exclusively ─────
    // Allocated in open() on the message thread BEFORE the encoder thread
    // is started, then handed off (no further mutation from message thread
    // until close()). Reconfigure swaps these atomically on the encoder
    // thread (between draining the current instance and opening the new
    // one).
    AVCodecContext*  codecContext_ = nullptr;
    AVFrame*         frame_        = nullptr;
    AVPacket*        packet_       = nullptr;
    SwsContext*      sws_          = nullptr;

    VideoEncoderConfig                       current_cfg_{};
    std::uint64_t                            last_observed_interval_seq_ = 0;
    // Tracks whether SPS/PPS has been published for the CURRENT
    // libavcodec instance. Reset to false at open() and at the start of
    // reconfigure; set to true once publishExtractedSpsPps_ (from
    // extradata) OR scanAndPublishSpsPps_ (from first IDR fallback) has
    // fired the callback.
    bool                                     sps_pps_published_ = false;

    // ───── lifetime / R4 H9 close ordering ─────
    std::atomic<bool>                        m_closing{false};
    std::atomic<bool>                        m_open{false};
    JamWideFrameDistributor::Subscription    subscription_;
    std::atomic<std::uint64_t>*              audio_interval_seq_ = nullptr;

    PublishSpsPpsCallback                    publishSpsPps_;
    PublishEncodedNalCallback                publishEncodedNal_;
    VideoEncoderListener*                    listener_ = nullptr;

    // ───── encoder thread (juce::Thread) ─────
    void run() override;

    // Helpers — encoder thread only.
    bool allocateLibavcodecResources_(const VideoEncoderConfig& cfg);
    void freeLibavcodecResources_();
    void publishExtractedSpsPps_();
    void processFrameSlot_(Slot& slot);
    void drainEncoder_();  // calls receive_packet in a loop until EAGAIN
    void handleReconfigure_(const VideoEncoderConfig& newCfg);
    void scanAndPublishSpsPps_(const unsigned char* nal_stream, int len);

    // Helper for the drop-oldest enqueue path inside onFrame /
    // feedRawBgraForTest.
    void enqueueBgraSlot_(const unsigned char* bgra, int width, int height);
};

} // namespace jamwide
