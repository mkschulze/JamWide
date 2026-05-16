#pragma once
// Phase 20-01 — abstract VideoEncoder interface (D-01).
//
// Plan 20-02 binds to this surface to attach the NJClient state machine
// (`SetVideoSPSPPS` and `QueueVideoFrame`); the concrete `Openh264Encoder`
// implementation (libavcodec backend; libopenh264 codec selection) lives
// in this same directory. Future hardware backends (VideoToolbox /
// MediaFoundation) plug into the same interface without touching call
// sites.
//
// Threading contract:
//   - open()/close()/reconfigure()/notifyIntervalStart() are called from
//     the message thread (the encoder owner on JamWideJuceProcessor —
//     Plan 20-03).
//   - publishSpsPps and publishEncodedNal callbacks are invoked on the
//     encoder thread (a juce::Thread subclass owned by the concrete
//     implementation per D-02). Cross-thread serialization with NJClient's
//     m_video_cs / m_video_spspps_cs is the callee's responsibility — the
//     encoder hands raw bytes off and does not touch NJClient state.
//   - notifyIntervalStart is a non-blocking pulse: the encoder thread
//     polls the injected `audioIntervalSeq` atomic at frame-encode time
//     (D-15) and forces an IDR when the value changes. The argument here
//     is hint-only; the canonical source of truth is the audioIntervalSeq
//     atomic injected at open().
//
// Lifecycle (R4 H9 LOCKED 7-step close ordering — see Openh264Encoder.cpp):
//   1. set m_closing = true
//   2. release JamWideFrameDistributor::Subscription
//   3. (Subscription destructor blocks for in-flight onFrame)
//   4. signal encoder thread to wake-and-exit
//   5. encoder thread drains pending frames to no-op, exits loop
//   6. join encoder thread
//   7. free libavcodec / sws / slab pool / SPSC ring
//
// Reconfigure path (R4 H9 — Subscription preserved): encoder thread
// receives a RECONFIGURE sentinel through the input SPSC, drains the
// current libavcodec instance, opens a new instance with new params,
// regenerates SPS/PPS, invokes publishSpsPps. NO close() is called.
// The Subscription survives — frames arriving on the camera-callback
// thread during the libavcodec instance swap continue to enqueue into
// the slab pool; they are processed by the new instance after the swap.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace jamwide {

// Forward declarations — keeps this header dependency-free (no NJClient,
// no libavcodec, no JUCE). Plan 20-02 includes both headers explicitly.
class JamWideFrameDistributor;
class VideoEncoderListener;
struct VideoEncoderConfig;

// Callback shape for SPS/PPS publish (D-03 / D-13). Plan 20-02 binds
// `[NJClient*](const void* data, int len) { njclient->SetVideoSPSPPS(data, len); }`
// at open() time. Payload is raw [SPS-NAL][PPS-NAL] concatenation with
// no per-NAL length prefix (CONTEXT.md `<specifics>` "SPS/PPS chunk
// format").
using PublishSpsPpsCallback = std::function<void(const void* data, int len)>;

// Callback shape for encoded-NAL publish. Plan 20-02 binds
// `[NJClient*](const void* data, int len) { njclient->QueueVideoFrame(data, len); }`
// at open() time. Payload is raw NAL bytes (or NAL-group bytes for an
// IDR that spans multiple NAL units) WITHOUT the 4-byte BE length
// prefix — Plan 20-02 owns the length-prefix wrapping inside
// QueueVideoFrame before calling RawDataSendWrite (CONTEXT.md
// `<specifics>` "Per-frame chunk format").
using PublishEncodedNalCallback = std::function<void(const void* data, int len)>;

class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    // open(): construct libavcodec / sws contexts; allocate slab pool;
    // register subscription with the distributor; start encoder thread;
    // emit initial SPS/PPS via publishSpsPps. Returns false if any step
    // fails (caller treats this as a fatal that will surface via the
    // listener's onEncoderFatalError path). open() may NOT be called
    // when the encoder is already open — caller must close() first.
    //
    // - cfg                 : per-preset config (Low/Medium/High); use
    //                         makeConfigForPreset(preset).
    // - dist                : Phase 19 frame source. The encoder owns its
    //                         JamWideFrameDistributor::Subscription as a
    //                         member; the distributor must out-live the
    //                         encoder (close() releases the subscription
    //                         BEFORE the distributor is destroyed; this
    //                         is the layering enforced by Plan 20-03).
    // - audioIntervalSeq    : pointer to an std::atomic<uint64_t> that
    //                         Plan 20-02 bumps inside on_new_interval
    //                         (D-15). The encoder thread reads with
    //                         relaxed memory ordering before each
    //                         avcodec_send_frame; on change, forces IDR
    //                         via frame_->pict_type=AV_PICTURE_TYPE_I.
    // - publishSpsPps       : invoked once after open() (extradata path)
    //                         and after each reconfigure().
    // - publishEncodedNal   : invoked for each emitted NAL on the
    //                         encoder thread.
    // - listener            : optional; nullptr is acceptable. Used by
    //                         Plan 20-03 to log encoder lifecycle and
    //                         attempt fatal-error self-heal via
    //                         reconfigure.
    virtual bool open(const VideoEncoderConfig&    cfg,
                      JamWideFrameDistributor*     dist,
                      std::atomic<std::uint64_t>*  audioIntervalSeq,
                      PublishSpsPpsCallback        publishSpsPps,
                      PublishEncodedNalCallback    publishEncodedNal,
                      VideoEncoderListener*        listener = nullptr) = 0;

    // close(): R4 H9 LOCKED 7-step teardown ordering (see header comment).
    // Safe to call when not open (no-op). After close() returns, the
    // encoder may be safely deleted OR re-opened.
    virtual void close() = 0;

    // reconfigure(): R4 H9 — Subscription preserved. Sends a sentinel
    // through the input SPSC; the encoder thread swaps libavcodec instance
    // and republishes SPS/PPS without releasing the Subscription. Returns
    // false if not currently open or if the sentinel could not be queued.
    virtual bool reconfigure(const VideoEncoderConfig& cfg) = 0;

    // Counter accessors. Both relaxed atomics — observability only.
    // getInputDropCount() returns the count of frames that were
    // overwritten by the drop-oldest input SPSC backpressure (D-07);
    // Plan 20-03 UAT acceptance fails if this is non-zero at phase close.
    // getFrameOutputCount() returns the total count of encoded NAL
    // publishes (one per emitted packet from avcodec_receive_packet).
    virtual std::uint64_t getInputDropCount()   const noexcept = 0;
    virtual std::uint64_t getFrameOutputCount() const noexcept = 0;
};

} // namespace jamwide
