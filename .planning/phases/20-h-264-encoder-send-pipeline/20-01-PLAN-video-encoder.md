---
phase: 20
plan: 01
slug: video-encoder
type: execute
wave: 1
depends_on:
  - 20-00
files_modified:
  - CMakeLists.txt
  - juce/video/encoder/VideoEncoder.h
  - juce/video/encoder/VideoEncoderConfig.h
  - juce/video/encoder/Openh264Encoder.h
  - juce/video/encoder/Openh264Encoder.cpp
  - juce/video/encoder/VideoEncoderListener.h
  - tests/test_video_encoder.cpp
autonomous: true
requirements:
  - COD-01
threat_refs:
  - T-20-01
  - T-20-SC
review_refs:
  - R3-no-mustfix-direct (encoder side; D-15 IDR sync from CONTEXT.md)

must_haves:
  truths:
    - "A pure-virtual jamwide::VideoEncoder interface exists at juce/video/encoder/VideoEncoder.h with the open/close/reconfigure/notifyIntervalStart/getInputDropCount/getFrameOutputCount surface from CONTEXT.md `<specifics>`, plus a publishSpsPps callback hookup and a publishEncodedNal callback hookup so Plan 20-02 can attach NJClient without VideoEncoder having a hard dependency on NJClient"
    - "VideoEncoderConfig POD at juce/video/encoder/VideoEncoderConfig.h carries width/height/frameRate/targetBitrateKbps/H264Profile/gopHintFrames per CONTEXT.md `<specifics>`"
    - "Openh264Encoder concrete implementation lives in juce/video/encoder/Openh264Encoder.{h,cpp}; uses libavcodec (avcodec_find_encoder(AV_CODEC_ID_H264)) which selects libopenh264 because the vendored ffmpeg builds expose openh264 as the H.264 encoder; ported from JamTaba's FFMpegMuxer.cpp:237-277 configure block per CONTEXT.md D-02 + D-05 + D-06"
    - "Openh264Encoder owns its own thread (juce::Thread subclass per D-02 + Claude's-discretion default); subscribes to JamWideFrameDistributor::Subscription on open(); does BGRA→YUV420P conversion via libswscale sws_scale (NOT JamTaba's per-pixel macro, per D-02); produces H.264 NAL units + SPS/PPS"
    - "H.264 Baseline profile, level 3.1, no B-frames (D-05); RC_BITRATE_MODE with allow_skip_frames=1 (D-06); slice_mode=fixed (single-slice default per D-Discretion); loopfilter_disable=1 (D-Discretion); thread_count=1 (single-threaded default per D-Discretion)"
    - "Bitrate ladder: Low=100 kbps, Medium=300 kbps, High=800 kbps mapped from Phase 19 capture preset (D-16)"
    - "One IDR per NINJAM interval via std::atomic<uint64_t> m_audio_interval_seq read on the encoder thread before each avcodec_send_frame; on change, frame_->pict_type=AV_PICTURE_TYPE_I and frame_->key_frame=1 are set so libavcodec/openh264 emits an IDR NAL — port of CONTEXT.md `<canonical_refs>` 'Forcing IDR' example"
    - "Drop-oldest backpressure on the input SPSC ring between JamWideFrameDistributor::onFrame (camera-callback thread, producer) and the encoder thread (consumer) per D-07; observable counter m_encoder_input_drops increments on overwrite; getInputDropCount() reads it; counter being non-zero at phase close fails Plan 20-03's UAT (D-07)"
    - "Reconfigure (preset change, fatal error, resolution change) tears down the libavcodec context, opens a new one, and re-publishes SPS/PPS via the publishSpsPps callback (D-04); existing input frames-in-flight are flushed gracefully (drain avcodec on encoder thread, no race)"
    - "Encoder lifecycle: instance constructed when camera opens (Plan 20-03 wiring); encoder thread starts on open() — open() is called by Plan 20-03 ONLY when broadcast toggles on (D-13 idle-cost zero when not broadcasting); close() stops the thread and frees the libavcodec context"
    - "publishSpsPps is invoked on the encoder thread once per session and after each reconfigure; payload is raw [SPS-NAL][PPS-NAL] concatenation, no per-NAL length prefix (CONTEXT.md `<specifics>`); Plan 20-02 attaches NJClient::SetVideoSPSPPS as the publishSpsPps target"
    - "publishEncodedNal is invoked on the encoder thread for each frame's NAL bytes (or NAL-group bytes); payload is the RAW NAL bytes WITHOUT the 4-byte BE length prefix — Plan 20-02 owns the length-prefix wrapping inside QueueVideoFrame BEFORE calling RawDataSendWrite (since the prefix is per CONTEXT.md `<specifics>` 'Per-frame chunk format' the same 4-byte BE convention as the marker; Plan 20-02 already holds m_video_cs when this lands)"
    - "tests/test_video_encoder.cpp covers (a) bring-up: open(config) → publishSpsPps fires within 200ms with non-zero len; (b) IDR-sync counter: notifyIntervalStart(seq+1) → next encoded frame is an IDR (detect by NAL nal_unit_type==5 or by avcodec frame_->pict_type==AV_PICTURE_TYPE_I in the test's mock encoder publishEncodedNal callback inspection); (c) drop-oldest backpressure: producer overruns the input SPSC at 60fps for 100 frames while consumer is gated → getInputDropCount() > 0 + getFrameOutputCount() unaffected by drops (drops are pre-encode); (d) reconfigure: switch from Low to High preset → publishSpsPps fires AGAIN with potentially different bytes"
  artifacts:
    - path: "juce/video/encoder/VideoEncoder.h"
      provides: "Abstract VideoEncoder interface with open/close/reconfigure/notifyIntervalStart + getInputDropCount/getFrameOutputCount + SPS/PPS and encoded-NAL publish callbacks; namespace jamwide; pure C++ (no NJClient include)"
      exports:
        - "class VideoEncoder"
        - "using PublishSpsPpsCallback = std::function<void(const void* data, int len)>"
        - "using PublishEncodedNalCallback = std::function<void(const void* data, int len)>"
    - path: "juce/video/encoder/VideoEncoderConfig.h"
      provides: "VideoEncoderConfig POD per CONTEXT.md `<specifics>`; H264Profile enum (Baseline only in v1.3)"
      exports:
        - "struct VideoEncoderConfig"
        - "enum class H264Profile { Baseline }"
    - path: "juce/video/encoder/Openh264Encoder.h"
      provides: "Openh264Encoder concrete VideoEncoder implementation; owns juce::Thread; subscribes to JamWideFrameDistributor::Subscription"
      exports:
        - "class Openh264Encoder : public VideoEncoder"
    - path: "juce/video/encoder/Openh264Encoder.cpp"
      provides: "libavcodec H.264 encoder via libopenh264 backend; ported from JamTaba FFMpegMuxer.cpp:237-277; BGRA→YUV420P via libswscale sws_scale; one-IDR-per-interval via m_audio_interval_seq atomic; drop-oldest backpressure on input SPSC; per-preset bitrate ladder"
      min_lines: 400
    - path: "juce/video/encoder/VideoEncoderListener.h"
      provides: "Optional listener interface for fatal-error notifications and reconfigure-completed events (planner picks granularity per D-Discretion 'Debug logging surface'); used by Plan 20-03 to log encoder events via juce::Logger::writeToLog on the message thread"
      exports:
        - "class VideoEncoderListener"
    - path: "tests/test_video_encoder.cpp"
      provides: "Bring-up + IDR-sync + drop-oldest + reconfigure unit-test coverage"
      min_lines: 250
    - path: "CMakeLists.txt"
      provides: "Wire VideoEncoder.h + VideoEncoderConfig.h + Openh264Encoder.{h,cpp} + VideoEncoderListener.h into the JamWideJuce target; add test_video_encoder executable under JAMWIDE_BUILD_TESTS with jamwide_use_ffmpeg(test_video_encoder) AND link against JamWideJuce's encoder-only sources (NOT the full plugin lib, per Phase 19 MEDIUM-5 pure-C++ test discipline); add_test(NAME video_encoder ...)"
      contains: "Openh264Encoder.cpp"
  key_links:
    - from: "Openh264Encoder"
      to: "JamWideFrameDistributor"
      via: "Subscription handle held as a member; ~Openh264Encoder releases it under the existing Phase 19 HIGH-2 RAII semantics so onFrame can no longer reach a destroyed encoder"
      pattern: "registerSubscriber"
    - from: "Openh264Encoder encoder thread"
      to: "NJClient::m_audio_interval_seq (Plan 20-02 owns the field; this plan reads via a configurable atomic-pointer or a function-pointer hand-off)"
      via: "std::atomic<uint64_t>* injected at open() — encoder reads observed_seq = ptr->load(relaxed) before each encode; on change, set pict_type=AV_PICTURE_TYPE_I"
      pattern: "AV_PICTURE_TYPE_I"
    - from: "Openh264Encoder encoder thread"
      to: "Plan 20-02 NJClient::SetVideoSPSPPS"
      via: "publishSpsPps callback hookup at open(); encoder calls it once after SPS/PPS extracted from the first IDR's extradata (or from openh264's onParameterSetEvent if direct API path) AND after each reconfigure"
      pattern: "publishSpsPps_\\("
    - from: "Openh264Encoder encoder thread"
      to: "Plan 20-02 NJClient::QueueVideoFrame"
      via: "publishEncodedNal callback hookup at open(); encoder calls it for each output NAL (or NAL-group) on the encoder thread — Plan 20-02's QueueVideoFrame internally takes m_video_cs"
      pattern: "publishEncodedNal_\\("
---

<objective>
Plan 20-01 stands up the abstract `VideoEncoder` interface + the openh264 (libavcodec backend) concrete implementation that consumes Phase 19's `JamWideFrameDistributor` BGRA frames and emits H.264 NAL units + SPS/PPS at the spike-validated baseline (D-02 D-05 D-06). The encoder owns its own thread; the audio thread and NJClient are NOT involved at this layer. Per-preset bitrate ladder (Low/Medium/High → 100/300/800 kbps) is mapped from Phase 19's preset enum at open() time per D-16. One IDR per NINJAM interval is achieved by reading a `std::atomic<uint64_t>* m_audio_interval_seq` injected by Plan 20-02 — on counter change, the encoder forces an IDR for that frame via libavcodec's `pict_type=AV_PICTURE_TYPE_I + key_frame=1` request to libopenh264, per CONTEXT.md `<canonical_refs>` "Forcing IDR for Interval-Boundary Keyframes" (D-15). Drop-oldest backpressure on the SPSC input ring + the observable `m_encoder_input_drops` counter implement D-07. Reconfigure (preset change, fatal error, resolution change) is tear-down + rebuild + republish SPS/PPS (D-04).

Purpose: this is the encoder side of the Phase 20 architecture. It is autonomous (no NJClient include, no audio-thread contact, no `m_video_cs` involvement) so it can be unit-tested in isolation against synthetic frames before Plan 20-02 wires it into the NJClient state machine. The SPS/PPS publish callback + per-NAL publish callback are the seams that Plan 20-02 attaches to.

Output: A `juce::Thread`-backed H.264 encoder that opens with a config, subscribes to the frame distributor, encodes 320×240@10fps at ~98 kbps on the spike baseline, force-IDRs at interval boundaries, and surfaces SPS/PPS + encoded NALs to attached callbacks. A pure-C++ in-process test (`test_video_encoder`) that exercises bring-up, IDR-sync, drop-oldest backpressure, and reconfigure — all without an NJClient or JUCE plugin target link, mirroring Phase 19 MEDIUM-5's pure-C++ test discipline.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/REQUIREMENTS.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-VALIDATION.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-00-PLAN-substrate-revision.md
@.planning/phases/19-camera-capture-permission-ux/19-01-capture-pipeline-PLAN.md
@.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-spike-results.md
@juce/video/native/JamWideFrameDistributor.h
@cmake/ffmpeg.cmake

<interfaces>
<!-- Key contracts the executor needs. Extracted verbatim from JUCE + JamWide codebase + ffmpeg API. -->

From juce/video/native/JamWideFrameDistributor.h (Phase 19, exists):
  class JamWideFrameDistributor::Subscriber { virtual void onFrame(const juce::Image& image) = 0; };
  class JamWideFrameDistributor::Subscription { /* RAII handle; destructor blocks until in-flight onFrame returns */ };
  Subscription JamWideFrameDistributor::registerSubscriber(Subscriber* s);
  // onFrame may be called on the camera-callback thread (JUCE "any thread") — Openh264Encoder
  // MUST be thread-safe in onFrame: copy BGRA bytes into the input SPSC and return immediately;
  // do not block, do not allocate (use a pre-allocated frame slab pool).

From cmake/ffmpeg.cmake (Phase 14.3-01, exists):
  add_library(ffmpeg::lgpl INTERFACE IMPORTED)
  target_link_libraries(ffmpeg::lgpl INTERFACE libavcodec libavformat libavutil libswscale libopenh264 ...)
  // Helper macro at cmake/jamwide_use_ffmpeg.cmake: jamwide_use_ffmpeg(<target>)
  // — applies include dirs + link libs + (macOS) install_name_tool rewriting for @loader_path/../Frameworks/.

From JamTaba src/Common/video/FFMpegMuxer.cpp:237-277 (canonical port target, per CONTEXT.md `<canonical_refs>` "JamTaba reference"):
  codec = avcodec_find_encoder(AV_CODEC_ID_H264);   // libavcodec selects libopenh264 because that's what the vendored ffmpeg links
  codecContext = avcodec_alloc_context3(codec);
  codecContext->bit_rate = videoBitRate;            // 100000 / 300000 / 800000 per preset (D-16)
  codecContext->rc_max_rate = videoBitRate;
  codecContext->rc_buffer_size = videoBitRate;
  codecContext->width  = videoResolution.width();   // 320 / 640 / 1280 per preset
  codecContext->height = videoResolution.height();  // 240 / 480 /  720 per preset
  codecContext->time_base = AVRational{ 1, (int)videoFrameRate };  // 10 / 15 / 30
  codecContext->gop_size = 30;                      // 1 hint; force-IDR via pict_type overrides per-frame (D-15)
  codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
  codecContext->profile = FF_PROFILE_H264_BASELINE; // D-05
  codecContext->level   = 31;                       // D-05 (Baseline 3.1)
  av_opt_set(codecContext->priv_data, "rc_mode",            "bitrate", 0);   // D-06 (RC_BITRATE_MODE)
  av_opt_set(codecContext->priv_data, "allow_skip_frames",  "1",       0);   // openh264 RC_BITRATE_MODE requirement
  av_opt_set(codecContext->priv_data, "slice_mode",         "fixed",   0);   // D-Discretion: single-slice
  av_opt_set(codecContext->priv_data, "loopfilter_disable", "1",       0);   // D-Discretion
  codecContext->thread_count = 1;                                            // D-Discretion: single-threaded
  av_dict_set(&opts, "preset", "veryfast", 0);
  avcodec_open2(codecContext, codec, &opts);

From the spike results (canonical_refs, spike-results.md):
  Baseline measurement: 320×240@10fps → ~98 kbps avg bitrate, 4% CPU on intel-mac. ffmpeg lib add: 5.2 MB per arch.
  Q3 thread-safety: libavcodec frame send/receive is single-thread; we have one encoder thread per Openh264Encoder instance, so no cross-thread ffmpeg API.
  Q7 fourCC byte order: H264 → MAKE_NJ_FOURCC('H','2','6','4') (already in 14.3-02 is_video_fourcc helper).

From CONTEXT.md `<canonical_refs>` "Forcing IDR" example (port target for D-15):
  uint64_t observed = audio_interval_seq_ptr_->load(std::memory_order_relaxed);
  if (observed != lastObservedIntervalSeq_) {
    if (frame_) { frame_->pict_type = AV_PICTURE_TYPE_I; frame_->key_frame = 1; }
    lastObservedIntervalSeq_ = observed;
  }
  // applied IMMEDIATELY before avcodec_send_frame() on the encoder thread.

VideoEncoder pure-virtual interface (DEFINE IN THIS PLAN at juce/video/encoder/VideoEncoder.h):
  namespace jamwide {
    using PublishSpsPpsCallback     = std::function<void(const void* data, int len)>;
    using PublishEncodedNalCallback = std::function<void(const void* data, int len)>;
    class VideoEncoder {
    public:
      virtual ~VideoEncoder() = default;
      virtual bool open(const VideoEncoderConfig& cfg,
                        JamWideFrameDistributor* dist,
                        std::atomic<uint64_t>* audioIntervalSeq,
                        PublishSpsPpsCallback     publishSpsPps,
                        PublishEncodedNalCallback publishEncodedNal,
                        VideoEncoderListener*     listener = nullptr) = 0;
      virtual void close() = 0;
      virtual bool reconfigure(const VideoEncoderConfig& cfg) = 0;
      virtual uint64_t getInputDropCount()   const noexcept = 0;
      virtual uint64_t getFrameOutputCount() const noexcept = 0;
    };
  }

VideoEncoderConfig POD (DEFINE IN THIS PLAN at juce/video/encoder/VideoEncoderConfig.h):
  namespace jamwide {
    enum class H264Profile : int { Baseline = 0 };  // Main / High reserved for post-v1.3 backends
    struct VideoEncoderConfig {
      int width;                // 320 / 640 / 1280 per Low/Medium/High
      int height;               // 240 / 480 /  720
      int frameRate;            // 10  /  15 /   30
      int targetBitrateKbps;    // 100 / 300 / 800
      H264Profile profile;      // Baseline (v1.3)
      int gopHintFrames;        // hint for openh264 internal scheduling; force-IDR overrides per-frame
    };
  }

VideoEncoderListener (DEFINE IN THIS PLAN at juce/video/encoder/VideoEncoderListener.h):
  namespace jamwide {
    class VideoEncoderListener {
    public:
      virtual ~VideoEncoderListener() = default;
      virtual void onEncoderOpened(const VideoEncoderConfig& cfg) {}
      virtual void onEncoderClosed() {}
      virtual void onEncoderReconfigured(const VideoEncoderConfig& cfg) {}
      virtual void onEncoderFatalError(const char* reason) {}
      virtual void onSpsPpsPublished(int spsPpsLen) {}
    };
  }

Bitrate ladder helper (DECLARE inside juce/video/encoder/VideoEncoderConfig.h):
  inline VideoEncoderConfig makeConfigForPreset(int preset /* 0=Low, 1=Medium, 2=High */) {
    switch (preset) {
      case 0: return { 320,  240, 10, 100, H264Profile::Baseline, 30 };  // Low (spike baseline)
      case 1: return { 640,  480, 15, 300, H264Profile::Baseline, 30 };  // Medium
      case 2: return { 1280, 720, 30, 800, H264Profile::Baseline, 60 };  // High
      default: return makeConfigForPreset(0);
    }
  }

Input SPSC ring (DEFINE INSIDE Openh264Encoder.cpp; do NOT put in src/threading/spsc_payloads.h — Phase 15.1-04 M-9 finality):
  Frame-slab pool layout (per CONTEXT.md Pitfall #3 "Encoder thread heap allocation throttle"):
    - 4 slots of std::vector<unsigned char> sized to max-expected BGRA frame (= 1280*720*4 = 3.6 MB worst-case at High)
    - SPSC ring of 4 indices (head/tail relaxed atomics); onFrame writes to slot[head], advances head; encoder reads from slot[tail], advances tail.
    - Drop-oldest: when onFrame finds head+1 == tail (full), overwrite slot[tail] (i.e. drop the oldest unconsumed frame), advance tail, then enqueue the new frame at head — increments m_encoder_input_drops.
    - All onFrame work is pre-allocated copy + atomic-store; no heap allocation in steady state.
</interfaces>
</context>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| camera-callback thread → encoder input SPSC | Subscribers receive frames on JUCE "any thread"; Openh264Encoder's onFrame copies into a pre-allocated slot under SPSC discipline |
| encoder thread → libavcodec / libopenh264 | single-thread context per encoder instance (D-Discretion thread_count=1); no cross-thread ffmpeg API calls within an encoder |
| encoder thread → Plan 20-02 (publishSpsPps / publishEncodedNal callbacks) | callbacks are invoked on the encoder thread; Plan 20-02's QueueVideoFrame internally takes m_video_cs, so cross-thread serialization is the callee's responsibility |
| encoder thread → JamWideFrameDistributor::Subscription lifetime | Phase 19 HIGH-2 contract: ~Subscription blocks until in-flight onFrame exits; Openh264Encoder MUST hold its Subscription as a member that gets destroyed BEFORE the encoder thread joins and BEFORE the libavcodec context is freed |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-20-01 | Tampering (encoder use-after-free / data-race) | Openh264Encoder + JamWideFrameDistributor + Plan 20-02 callbacks | mitigate | Subscription RAII (Phase 19 HIGH-2) ensures onFrame can't reach a destroyed Openh264Encoder; encoder thread join in close() happens BEFORE Subscription release so the encoder's own slab pool is alive when onFrame stops; publishSpsPps / publishEncodedNal are std::function copies captured at open(), so Plan 20-02 lifecycle of those callbacks is Plan 20-02's responsibility (per the layered review pattern from memory feedback_phase19_review_layers) |
| T-20-IDR | Tampering (IDR-sync drift exceeding 1 frame) | m_audio_interval_seq atomic read in encoder thread | accept | D-15 explicitly accepts up to 1 frame of drift (~33-100ms at our frame rates); test_video_encoder asserts: 100 consecutive interval boundary changes produce 100 IDR frames within +/- 1 frame of the boundary |
| T-20-OOM | Information disclosure (slab-pool OOM under sustained input overrun) | encoder input SPSC | mitigate | Drop-oldest semantics + observable getInputDropCount(); Plan 20-03 UAT acceptance fails if drop count != 0 at phase close (D-07); slab-pool size 4 frames is sufficient because consumer (encoder thread) runs at ≥ input frame rate in steady state |
| T-20-SC | Tampering (supply chain) | libavcodec/libavutil/libswscale/libopenh264 | mitigate | Vendored under libs/ffmpeg/* by Phase 14.3-01; this plan does NOT install new packages, only links via existing cmake/ffmpeg.cmake INTERFACE target; supply-chain audit was discharged at 14.3-01 and is re-affirmed at Phase 23 packaging; no [ASSUMED]/[SUS]/[SLOP] packages introduced here |
| T-20-FATAL | Denial of service (encoder fatal error → silent stop) | Openh264Encoder fatal-error path | mitigate | VideoEncoderListener::onEncoderFatalError() is invoked on the encoder thread; Plan 20-03's owner attaches a listener that logs via juce::Logger::writeToLog (message thread) AND triggers a reconfigure attempt (D-04 tear-down + rebuild) so the encoder self-heals |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Define VideoEncoder + VideoEncoderConfig + VideoEncoderListener interfaces (Wave-0 contracts for Plan 20-02 to bind to)</name>
  <files>
    juce/video/encoder/VideoEncoder.h,
    juce/video/encoder/VideoEncoderConfig.h,
    juce/video/encoder/VideoEncoderListener.h,
    CMakeLists.txt
  </files>
  <behavior>
    - juce/video/encoder/VideoEncoder.h declares the abstract `jamwide::VideoEncoder` interface verbatim per the `<interfaces>` block above; no Openh264Encoder include; only forward-declares `JamWideFrameDistributor` and uses `<atomic>` + `<functional>` + `<cstdint>` + `<cstddef>` from std.
    - juce/video/encoder/VideoEncoderConfig.h declares `H264Profile` enum + `VideoEncoderConfig` POD + the `makeConfigForPreset(int preset)` inline helper that maps Phase 19's 0/1/2 → Low/Medium/High capture preset to the matching width/height/frameRate/targetBitrateKbps combo (D-16).
    - juce/video/encoder/VideoEncoderListener.h declares the optional listener interface with onEncoderOpened / onEncoderClosed / onEncoderReconfigured / onEncoderFatalError / onSpsPpsPublished hooks (default implementations as `{}` so subclasses override only what they need).
    - CMakeLists.txt adds the three headers to the JamWideJuce target's source list (alongside the existing native camera headers at line ~218 area); no library dependencies pulled in by the interface headers (they are header-only contracts at this task).
    - These three files together are the Wave-0 contracts Plan 20-02 imports — keeps task 2 (the Openh264Encoder impl) decoupled from Plan 20-02's parallel work.
  </behavior>
  <action>
    Write the three header files verbatim per the `<interfaces>` block above. Use Phase 19's existing namespace `jamwide` (consistent with JamWideFrameDistributor). Headers must compile standalone — no NJClient include, no plugin include — and the JamWideJuce target must continue to build cleanly after they are added to the source list (the lib doesn't depend on them yet at this task; they are reserved seams).
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target JamWideJuce -- -j8 2>&amp;1 | tail -20</automated>
    Plugin target builds cleanly after the three new headers are added to source list.
  </verify>
  <done>
    Three header files exist and declare the documented interfaces. CMakeLists.txt references them in JamWideJuce's source list. JamWideJuce target builds clean. No other code imports these yet — they are contracts that Task 2 (impl) and Plan 20-02 (consumer) will bind to.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Implement Openh264Encoder + per-preset config + IDR-sync + drop-oldest backpressure + reconfigure + tests/test_video_encoder.cpp</name>
  <files>
    juce/video/encoder/Openh264Encoder.h,
    juce/video/encoder/Openh264Encoder.cpp,
    tests/test_video_encoder.cpp,
    CMakeLists.txt
  </files>
  <behavior>
    Implementation outline (Openh264Encoder.cpp body MUST follow this structure top-to-bottom):

    1. CTOR / DTOR
       - ctor: zero-init all members; the libavcodec context (AVCodecContext*) is null until open(); the input slab-pool (4 slots × `std::vector<unsigned char>` sized to maxBgraBytes) is constructed empty here.
       - dtor: if open, calls close() which: (a) signals encoder thread exit; (b) joins thread; (c) releases JamWideFrameDistributor Subscription (this triggers Phase 19 HIGH-2 in-flight wait); (d) frees AVCodecContext + AVFrame + AVPacket + SwsContext.

    2. open(cfg, distributor, audioIntervalSeq, publishSpsPps, publishEncodedNal, listener)
       - precondition asserts: open() can only be called when idle; if encoder thread is running, return false (caller bug; per D-13 Plan 20-03 only calls open() when broadcast toggles on).
       - allocate libavcodec resources per the JamTaba port block in `<interfaces>`. allocate SwsContext for the configured width/height (sws_getContext / sws_scale path; src format = AV_PIX_FMT_BGRA, dst = AV_PIX_FMT_YUV420P).
       - allocate slab pool: 4 slots × std::vector<unsigned char>(cfg.width * cfg.height * 4); allocate AVFrame with width/height/format set + av_frame_get_buffer().
       - register JamWideFrameDistributor subscription: subscription_ = distributor->registerSubscriber(this); (Openh264Encoder implements JamWideFrameDistributor::Subscriber and stores subscription_ as a member).
       - publish first SPS/PPS: libavcodec writes SPS/PPS into AVCodecContext::extradata after avcodec_open2 if profile setup is correct; extract via codecContext_->extradata + codecContext_->extradata_size and invoke publishSpsPps_(extradata, extradata_size). If extradata is null (depends on openh264 build flags), defer to first-IDR path: capture the first emitted AV_PKT_FLAG_KEY packet's NAL stream, parse SPS (nal_unit_type 7) + PPS (nal_unit_type 8) by scanning [0x00 0x00 0x00 0x01] start codes, concatenate the SPS-NAL + PPS-NAL, and invoke publishSpsPps_ — record this with listener_->onSpsPpsPublished(len).
       - start juce::Thread (or std::thread per D-Discretion encoder thread lifecycle); enter run loop.

    3. run() loop on encoder thread
       Loop while !threadShouldExit():
         a. Wait for next frame slot at tail: spin/sleep-yield if SPSC empty (use AbstractFifo-style or hand-rolled tail!=head check); minimum-latency variant: juce::WaitableEvent signaled by onFrame.
         b. Convert BGRA → YUV420P via sws_scale into AVFrame buffers; release input slot.
         c. Read m_audio_interval_seq via audioIntervalSeq_->load(relaxed); if changed vs lastObservedIntervalSeq_, set frame_->pict_type=AV_PICTURE_TYPE_I, frame_->key_frame=1, update lastObservedIntervalSeq_.
         d. avcodec_send_frame(codecContext_, frame_); loop avcodec_receive_packet(codecContext_, pkt_); for each packet, invoke publishEncodedNal_(pkt_->data, pkt_->size) on the encoder thread; av_packet_unref(pkt_). Increment m_frame_output_count.
         e. Reset frame_->pict_type=AV_PICTURE_TYPE_NONE for next frame.
       On loop exit: avcodec flush (avcodec_send_frame(ctx, NULL); drain remaining packets).

    4. onFrame(const juce::Image& image)  — called on camera-callback thread (JUCE "any thread")
       - Use juce::Image::BitmapData(image, juce::Image::BitmapData::readOnly) to get the raw pixel pointer; assume BGRA byte order per Phase 19 D-04 + Assumption A7 in 20-RESEARCH.md (verify in Plan 20-03 UAT against the spike's red-frame round-trip).
       - SPSC enqueue: load head + tail; if (head - tail) >= slabCount, drop-oldest: advance tail (drop the oldest unconsumed slot), increment m_encoder_input_drops, then proceed.
       - memcpy width*height*4 bytes from BitmapData::data into slabPool_[head % slabCount]; advance head (release-store).
       - Signal encoder thread (if using juce::WaitableEvent: event.signal()).
       - Total work: ~3 MB memcpy at High preset (worst-case); ~300 KB at Low; sub-millisecond at typical capture rates; no heap allocation in steady state (slab is pre-allocated at open()).

    5. close()
       - Signal thread exit (threadShouldExit() returns true OR a std::atomic<bool> running_ flag). Signal WaitableEvent so the thread wakes from its idle wait.
       - thread_.stopThread(2000) or thread_.join() — wait for clean exit.
       - subscription_ = Subscription{}; — destroying the active subscription triggers Phase 19 HIGH-2 wait-for-in-flight.
       - Free libavcodec resources (av_packet_free, av_frame_free, sws_freeContext, avcodec_free_context). Clear slab pool.
       - listener_->onEncoderClosed() if listener attached.

    6. reconfigure(const VideoEncoderConfig& cfg)
       - close() (no Subscription release; reconfigure preserves the distributor binding).
       - re-open libavcodec resources with new cfg; re-publish SPS/PPS (the open() path already does this).
       - listener_->onEncoderReconfigured(cfg).

    7. test_video_encoder.cpp (~250 LOC)
       Test scaffold copies the TEST()/PASS()/FAIL() macro pattern from tests/test_rawdata_send.cpp. Sub-tests:
         A. test_encoder_bringup_publishes_sps_pps — feed 1 synthetic frame (solid color, allocated via juce::Image-like POD or a raw BGRA buffer adapter; test_video_encoder can avoid the JUCE dep by faking a minimal `juce::Image`-shaped struct that BitmapData consumers don't need — or alternatively use a test-only `feedRawBgra(width, height, bytes)` helper exposed under JAMWIDE_BUILD_TESTS on Openh264Encoder); within 200ms publishSpsPps callback fires with len > 0.
         B. test_encoder_idr_on_interval_change — set audioIntervalSeq atomic; feed N frames; bump the atomic; feed M more frames; assert: exactly one of the M frames is an IDR (detect by inspecting the published NAL: first byte after start code 0x00000001 has nal_unit_type = (byte & 0x1f) == 5).
         C. test_encoder_drop_oldest_under_input_overrun — feed at 60 fps for 100 frames into an encoder configured for 10 fps with NO consumer drain (or drain is gated); assert getInputDropCount() > 0; assert getFrameOutputCount() <= 10 (consumer rate).
         D. test_encoder_reconfigure_republishes_sps_pps — open() at preset 0 (Low); collect first SPS/PPS payload; call reconfigure(makeConfigForPreset(2)) (High); assert: publishSpsPps fires a SECOND time after reconfigure; the second payload may be byte-identical or different (openh264 SPS/PPS may not change across compatible profile/level configs — the test asserts the CALLBACK fires; byte-equality is not asserted).

       Use a minimal test harness that does NOT link against JamWideJuce — link against Openh264Encoder.cpp + VideoEncoder.h + VideoEncoderConfig.h sources directly, plus ffmpeg::lgpl via jamwide_use_ffmpeg(test_video_encoder). This matches Phase 19 MEDIUM-5's pure-C++ test discipline.
  </behavior>
  <action>
    Port the libavcodec configure block verbatim from JamTaba FFMpegMuxer.cpp:237-277 with the Phase 20 additions documented in CONTEXT.md `<canonical_refs>` "JamTaba openh264 Configure Block". The encoder thread is a juce::Thread subclass (preferred over std::thread per JUCE consistency with NinjamRunThread + JamWideCameraDevice patterns) with run() implementing the steady-state loop above. For the IDR-sync write to frame_->pict_type, set the pict_type AFTER sws_scale completes (because sws_scale produces a fresh frame each call) and BEFORE avcodec_send_frame — match the CONTEXT.md "Forcing IDR" example exactly. For BGRA → YUV420P, use libswscale sws_getContext (SWS_BILINEAR for downscale; the spike uses SWS_BICUBIC at line 142 of the spike-results notes — planner picks bilinear for steady-state perf at our resolutions) + sws_scale per frame. For the SPS/PPS extraction path, default to `codecContext_->extradata` if non-null after `avcodec_open2`; otherwise fall back to first-IDR NAL-stream scan as described. Slab-pool size is 4 frames (CONTEXT.md Pitfall #3); slab byte-size is cfg.width*cfg.height*4 (BGRA worst-case at High = 3.6 MB; total slab pool worst-case ~14.4 MB which is acceptable for the broadcast-active path per D-13 lifecycle).
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target test_video_encoder JamWideJuce -- -j8 &amp;&amp; ctest -R video_encoder --output-on-failure 2>&amp;1 | tail -30</automated>
    JamWideJuce target builds with Openh264Encoder included; test_video_encoder binary builds and executes; 4/4 sub-tests pass.
  </verify>
  <done>
    Openh264Encoder.{h,cpp} compile and link inside JamWideJuce; tests/test_video_encoder.cpp's 4 sub-tests are green under `ctest -R video_encoder`; getInputDropCount() and getFrameOutputCount() return live counters; the encoder thread exits cleanly on close() and the Subscription is released BEFORE the libavcodec context is freed (verifiable in code by source-line ordering in close()). No heap allocation observed on the camera-callback thread's onFrame steady-state path (verify by inspection — slab pool is pre-allocated; memcpy is the only work). `ctest --output-on-failure` is fully green (regression check that the new encoder header + cpp + test target don't break unrelated tests).
  </done>
</task>

</tasks>

<verification>
- `cd build-juce && cmake --build . --target JamWideJuce -- -j8` exits 0
- `cd build-juce && cmake --build . --target test_video_encoder -- -j8 && ctest -R video_encoder --output-on-failure` exits 0
- `cd build-juce && ctest --output-on-failure` exits 0 (full-suite regression check; the substrate revision from 20-00 + the encoder addition here must coexist with all existing tests)
- `grep -c "AV_PICTURE_TYPE_I" juce/video/encoder/Openh264Encoder.cpp` returns ≥ 1 (the IDR-force site exists)
- `grep -c "registerSubscriber" juce/video/encoder/Openh264Encoder.cpp` returns ≥ 1 (the Phase 19 distributor binding exists)
- Encoder thread `getInputDropCount()` is zero on the bring-up test sub-test A (no overrun, no drops); explicitly non-zero on sub-test C (drop-oldest exercise)
- TSan smoke (optional; Plan 20-03 owns the full populated UAT): `--tsan` build of `test_video_encoder` runs sub-tests A through D under TSan and emits zero data-race reports
</verification>

<success_criteria>
- Plan 20-01 delivers the encoder backend per CONTEXT.md D-01 / D-02 / D-04 / D-05 / D-06 / D-07 / D-13 / D-15 / D-16.
- Plan 20-02 has stable contracts to bind to: VideoEncoder.h pure-virtual surface, VideoEncoderConfig.h POD + preset helper, plus the publishSpsPps + publishEncodedNal callback signatures.
- Per-preset bitrate ladder is exact: 100 / 300 / 800 kbps target (D-16); validated empirically in Plan 20-03 UAT against the spike's measured baseline.
- One IDR per NINJAM interval is delivered with up to 1 frame of drift (D-15); test_encoder_idr_on_interval_change asserts this deterministically against the m_audio_interval_seq atomic.
- Drop-oldest backpressure + m_encoder_input_drops counter implement D-07; counter being non-zero at phase close is a fail-condition that Plan 20-03 UAT asserts against.
</success_criteria>

<output>
On completion, write `.planning/phases/20-h-264-encoder-send-pipeline/20-01-SUMMARY.md` per the get-shit-done summary template. Capture in the summary: (a) the exact libavcodec / libopenh264 versions visible at the link site (avcodec_version() output) so Plan 23 packaging can pin them; (b) the measured per-preset encode CPU + actual avg bitrate from the test_video_encoder bring-up run (target: Low preset ~98 kbps from the spike baseline ±10%); (c) any deviations from the JamTaba configure block (e.g. preset string choice if "veryfast" turned out to be too slow); (d) whether SPS/PPS came from extradata path or first-IDR-NAL-scan fallback path on the test host.
</output>
