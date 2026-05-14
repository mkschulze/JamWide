// tests/video_spike.cpp
//
// Quick task 260515-0pc — feasibility spike for JamTaba-style native video
// in JamWide. This is NOT production code. It is a standalone console-app
// test executable that proves the LGPL ffmpeg + Cisco openh264 + JUCE
// CameraDevice stack composes end-to-end with the JamTaba codec parameters
// (320×240 YUV420P @ 10fps @ 96kbps H.264 baseline, raw NAL bytestream).
//
// Build (gated behind JAMWIDE_VIDEO_SPIKE option, default OFF):
//   cmake -S . -B build-spike -G Ninja \
//         -DJAMWIDE_BUILD_TESTS=ON -DJAMWIDE_VIDEO_SPIKE=ON
//   cmake --build build-spike --target video_spike
//
// Run:
//   build-spike/video_spike   # exits 0 on success, 77 on no-camera-skip
//
// Output (one line, shell-greppable):
//   SPIKE-RESULT: bytes=NNN frames=NNN encode_ms=NNN cpu_pct=NN out_dir=PATH
//
// Decoded PNG frames written to:
//   $JAMWIDE_VIDEO_SPIKE_OUT (if set) or ${TMPDIR:-/tmp}/jamwide_video_spike_<pid>/
//
// LOCKED CODEC PARAMS (RESEARCH § 1, do not modify):
//   AV_CODEC_ID_H264 / libopenh264 backend / AV_PIX_FMT_YUV420P
//   320×240 / 10 fps / 96000 bps / GOP=30 / time_base={1,10}
//   Raw NAL bytestream — NO AVFormat container

// IMPORTANT: ffmpeg headers BEFORE JuceHeader.h. JUCE's headers transitively
// drag in stdlib + Cocoa Obj-C wrappers that, when included before ffmpeg's
// libavcodec headers, cause avcodec_open2(libopenh264) to behave as if
// pix_fmt were AV_PIX_FMT_NONE (-1) and width/height were zero — even though
// the AVCodecContext fields are correctly set immediately before the call.
// Verified by reduction to a JUCE-free C program (see /tmp/test_openh264.c
// in the spike work tree) which succeeds with the same flags. Symptom:
// "[libopenh264] [IMGUTILS @ ...] Picture size 0x0 is invalid" + "Invalid
// video pixel format: -1" at avcodec_open2 → rc=-22. Root cause is the
// header ordering. This is documented as a spike-time deviation in
// 260515-0pc-spike-results.md; the milestone (Items C/D) needs to follow
// the "ffmpeg first" include discipline.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <JuceHeader.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>   // getpid
#include <vector>

namespace fs = std::filesystem;
using clock_hr = std::chrono::high_resolution_clock;

// LOCKED constants from RESEARCH § 1 — bit-for-bit JamTaba compatibility.
static constexpr int kWidth = 320;
static constexpr int kHeight = 240;
static constexpr int kFps = 10;
static constexpr int kBitrate = 96000;
static constexpr int kGopSize = 30;
static constexpr int kTotalFrames = 100;

namespace {

// Capture listener: pushes JUCE camera frames into an SPSC-ish deque.
// imageReceived may fire from any thread; we copy out.
class CaptureListener : public juce::CameraDevice::Listener {
public:
    void imageReceived(const juce::Image& img) override {
        std::lock_guard<std::mutex> lk(mu_);
        // Clone to detach from JUCE's internal buffer.
        queue_.push_back(img.createCopy());
        cv_.notify_one();
    }

    bool wait_for_frame(juce::Image& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lk(mu_);
        if (!cv_.wait_for(lk, timeout, [this]{ return !queue_.empty(); }))
            return false;
        out = queue_.front();
        queue_.pop_front();
        return true;
    }

    size_t queue_size() {
        std::lock_guard<std::mutex> lk(mu_);
        return queue_.size();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<juce::Image> queue_;
};

// Convert a juce::Image (ARGB on macOS) to a YUV420P AVFrame using sws_scale.
// caller must allocate dst frame.
bool argb_to_yuv420p(const juce::Image& img, AVFrame* dst, SwsContext* sws) {
    juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readOnly);
    const uint8_t* src_planes[1] = { bmp.data };
    int src_strides[1] = { bmp.lineStride };

    // JUCE's macOS Image::ARGB has byte order BGRA in memory (little-endian uint32).
    // ffmpeg's AV_PIX_FMT_BGRA matches that. On Windows it would be RGB; for the
    // spike on macOS we hardcode BGRA — milestone item C generalizes per platform.
    int rc = sws_scale(sws, src_planes, src_strides, 0, kHeight,
                       dst->data, dst->linesize);
    return rc > 0;
}

void write_png(const std::string& path, AVFrame* yuv, SwsContext* sws_back) {
    // Convert YUV420P → BGRA into a juce::Image and write PNG.
    juce::Image out(juce::Image::ARGB, kWidth, kHeight, true);
    juce::Image::BitmapData bmp(out, juce::Image::BitmapData::readWrite);

    uint8_t* dst_planes[1] = { bmp.data };
    int dst_strides[1] = { bmp.lineStride };

    sws_scale(sws_back, yuv->data, yuv->linesize, 0, kHeight,
              dst_planes, dst_strides);

    juce::File f(path);
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) {
        juce::PNGImageFormat fmt;
        fmt.writeImageToStream(out, *stream);
    }
}

double cpu_user_seconds() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return static_cast<double>(ru.ru_utime.tv_sec) +
           static_cast<double>(ru.ru_utime.tv_usec) / 1.0e6;
}

}  // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juce_init;

    // ---- 0. Decide output dir -------------------------------------------
    std::string out_dir;
    if (const char* env = std::getenv("JAMWIDE_VIDEO_SPIKE_OUT")) {
        out_dir = env;
    } else {
        const char* tmpdir = std::getenv("TMPDIR");
        if (!tmpdir) tmpdir = "/tmp";
        out_dir = std::string(tmpdir) + "/jamwide_video_spike_" +
                  std::to_string(static_cast<long>(getpid()));
    }
    fs::create_directories(out_dir);

    // ---- 1. Open camera (synchronous) -----------------------------------
    // SPIKE deviation (260515-0pc): The plan's Task 2 envisions opening
    // the dev machine's camera. On macOS, console-app binaries without an
    // Info.plist (NSCameraUsageDescription key) cannot receive frames from
    // AVCaptureSession (the underlying API JUCE uses) — TCC silently denies.
    // This spike falls back to a SYNTHETIC frame source (animated gradient)
    // when the camera produces zero frames, so the encode→decode→PNG pipeline
    // can still be measured end-to-end. The fallback path produces real
    // measured numbers (bitstream size, encode CPU%, frame-quality
    // observation post-PNG) that answer the spike's primary question
    // ("does the codec stack compose?") — only the camera-acquisition leg
    // is degraded, and that leg is gated by Item G in deferred-items.md
    // (entitlements + codesigning) which is OUT OF SCOPE for this spike.
    bool use_synthetic_frames = false;

    std::cerr << "[1/6] Opening camera (320×240)...\n";
    std::unique_ptr<juce::CameraDevice> cam(
        juce::CameraDevice::openDevice(0, kWidth, kHeight, kWidth, kHeight, false));

    if (!cam) {
        std::cerr << "INFO: juce::CameraDevice::openDevice returned null — falling back\n"
                     "      to synthetic-frame mode (animated gradient). Codec pipeline\n"
                     "      measurements are still valid; only camera acquisition is mocked.\n";
        use_synthetic_frames = true;
    }

    CaptureListener listener;
    if (cam) cam->addListener(&listener);

    // ---- 2. Configure encoder (LOCKED JamTaba params) -------------------
    std::cerr << "[2/6] Configuring openh264 encoder (320×240 YUV420P @ 10fps @ 96kbps)...\n";

    const AVCodec* enc = avcodec_find_encoder_by_name("libopenh264");
    if (!enc) {
        std::cerr << "SPIKE-FAIL: avcodec_find_encoder_by_name(\"libopenh264\") returned null.\n"
                     "            ffmpeg build is broken — --enable-libopenh264 missing or\n"
                     "            openh264 not registered at runtime.\n";
        cam->removeListener(&listener);
        return 1;
    }

    AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
    if (!enc_ctx) {
        std::cerr << "SPIKE-FAIL: avcodec_alloc_context3 returned null.\n";
        return 1;
    }
    std::cerr << "      post-alloc: w=" << enc_ctx->width
              << " h=" << enc_ctx->height
              << " pix_fmt=" << enc_ctx->pix_fmt
              << " codec_id=" << enc_ctx->codec_id << "\n";
    enc_ctx->width        = kWidth;
    enc_ctx->height       = kHeight;
    enc_ctx->coded_width  = kWidth;    // H.264 special-case in encode_preinit; setting these
    enc_ctx->coded_height = kHeight;   // avoids the "Ignoring invalid" zero-out path.
    enc_ctx->pix_fmt      = AV_PIX_FMT_YUV420P;
    enc_ctx->time_base    = AVRational{1, 10};
    enc_ctx->framerate    = AVRational{10, 1};
    enc_ctx->bit_rate     = 96000;
    enc_ctx->rc_max_rate  = 96000;
    enc_ctx->rc_buffer_size = 96000;
    enc_ctx->gop_size     = 30;
    enc_ctx->max_b_frames = 0;     // baseline-only, no B-frames (matches JamTaba)

    std::cerr << "      pre-open: w=" << enc_ctx->width
              << " h=" << enc_ctx->height
              << " coded_w=" << enc_ctx->coded_width
              << " coded_h=" << enc_ctx->coded_height
              << " pix_fmt=" << enc_ctx->pix_fmt
              << " codec_id=" << enc_ctx->codec_id
              << " bitrate=" << enc_ctx->bit_rate
              << " gop=" << enc_ctx->gop_size << "\n";

    // Sanity check: does av_get_pix_fmt_name(YUV420P) return non-null in this binary?
    const char* yuv_name = av_get_pix_fmt_name(AV_PIX_FMT_YUV420P);
    const char* none_name = av_get_pix_fmt_name(AV_PIX_FMT_NONE);
    std::cerr << "      sanity: av_get_pix_fmt_name(YUV420P=0)='"
              << (yuv_name ? yuv_name : "<null>") << "'"
              << " av_get_pix_fmt_name(NONE=-1)='"
              << (none_name ? none_name : "<null>") << "'\n";

    // What does the encoder advertise?
    const enum AVPixelFormat* pix_fmts = nullptr;
    int npf = 0;
    int rc_cfg = avcodec_get_supported_config(enc_ctx, nullptr,
                                               AV_CODEC_CONFIG_PIX_FORMAT,
                                               0, (const void**)&pix_fmts, &npf);
    std::cerr << "      avcodec_get_supported_config(PIX_FORMAT) rc=" << rc_cfg
              << " npf=" << npf;
    if (pix_fmts) {
        std::cerr << " formats=[";
        for (int i = 0; i < npf; ++i) {
            std::cerr << (i ? "," : "") << static_cast<int>(pix_fmts[i]);
        }
        std::cerr << "]";
    }
    std::cerr << "\n";

    int open_rc = avcodec_open2(enc_ctx, enc, nullptr);
    if (open_rc < 0) {
        char errbuf[256];
        av_strerror(open_rc, errbuf, sizeof(errbuf));
        std::cerr << "SPIKE-FAIL: avcodec_open2 (encoder) failed: rc=" << open_rc
                  << " (" << errbuf << ")\n"
                  << "      post-open: w=" << enc_ctx->width
                  << " h=" << enc_ctx->height
                  << " pix_fmt=" << enc_ctx->pix_fmt << "\n";
        avcodec_free_context(&enc_ctx);
        cam->removeListener(&listener);
        return 1;
    }

    // ---- 3. Allocate scratch frames + sws contexts ----------------------
    AVFrame* enc_frame = av_frame_alloc();
    enc_frame->format = AV_PIX_FMT_YUV420P;
    enc_frame->width  = kWidth;
    enc_frame->height = kHeight;
    av_image_alloc(enc_frame->data, enc_frame->linesize, kWidth, kHeight,
                   AV_PIX_FMT_YUV420P, 32);

    SwsContext* sws_argb_to_yuv = sws_getContext(
        kWidth, kHeight, AV_PIX_FMT_BGRA,
        kWidth, kHeight, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    SwsContext* sws_yuv_to_bgra = sws_getContext(
        kWidth, kHeight, AV_PIX_FMT_YUV420P,
        kWidth, kHeight, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    // ---- 4. Capture + encode loop ---------------------------------------
    std::cerr << "[3/6] Capturing + encoding " << kTotalFrames
              << " frames @ ~10 fps...\n";

    std::vector<uint8_t> bitstream;
    bitstream.reserve(64 * 1024);

    AVPacket* pkt = av_packet_alloc();

    const auto encode_start = clock_hr::now();
    const double cpu_start = cpu_user_seconds();

    auto next_frame_deadline = clock_hr::now();
    int captured = 0;
    juce::Image last_img;

    // Synthetic-frame helper — produces an animated diagonal gradient + moving
    // square. Used when the camera produced zero frames within the first 2s
    // (macOS console-app permission limitation; see fallback notes above).
    auto synth_frame = [](int idx) {
        juce::Image img(juce::Image::ARGB, kWidth, kHeight, true);
        juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readWrite);
        const int sq_x = (idx * 4) % (kWidth - 40);
        const int sq_y = ((idx * 3) % (kHeight - 40));
        for (int y = 0; y < kHeight; ++y) {
            uint8_t* row = bmp.data + y * bmp.lineStride;
            for (int x = 0; x < kWidth; ++x) {
                uint8_t* px = row + x * 4;
                bool sq = (x >= sq_x && x < sq_x + 40 &&
                           y >= sq_y && y < sq_y + 40);
                px[0] = sq ? 255 : (uint8_t)((x + idx) & 0xff);  // B
                px[1] = sq ? 64  : (uint8_t)((y + idx) & 0xff);  // G
                px[2] = sq ? 64  : (uint8_t)((x ^ y) & 0xff);    // R
                px[3] = 255;                                     // A
            }
        }
        return img;
    };

    // First-frame probe: if camera was opened but produces nothing within 2s,
    // switch to synthetic mode for the rest of the run (don't fail the spike).
    if (!use_synthetic_frames) {
        juce::Image probe;
        if (!listener.wait_for_frame(probe, std::chrono::seconds(2))) {
            std::cerr << "INFO: camera opened but produced zero frames in 2s — switching\n"
                         "      to synthetic-frame mode for the remainder of the run.\n";
            use_synthetic_frames = true;
        } else {
            last_img = probe;
            captured++;
        }
    }

    for (int i = 0; i < kTotalFrames; ++i) {
        next_frame_deadline += std::chrono::milliseconds(100);  // 10 fps cadence

        juce::Image img;
        if (use_synthetic_frames) {
            img = synth_frame(i);
            captured++;
        } else {
            const auto wait_for = std::chrono::milliseconds(150);
            if (listener.wait_for_frame(img, wait_for)) {
                last_img = img;
                captured++;
            } else if (last_img.isValid()) {
                img = last_img;
            } else {
                std::cerr << "WARN: no last_img to pad with at frame " << i << "; using synth.\n";
                img = synth_frame(i);
                captured++;
            }
        }

        if (!argb_to_yuv420p(img, enc_frame, sws_argb_to_yuv)) {
            std::cerr << "WARN: sws_scale (encode) failed at frame " << i << "\n";
            continue;
        }
        enc_frame->pts = i;

        if (avcodec_send_frame(enc_ctx, enc_frame) < 0) {
            std::cerr << "WARN: avcodec_send_frame failed at frame " << i << "\n";
            continue;
        }

        while (avcodec_receive_packet(enc_ctx, pkt) >= 0) {
            bitstream.insert(bitstream.end(),
                             pkt->data, pkt->data + pkt->size);
            av_packet_unref(pkt);
        }

        std::this_thread::sleep_until(next_frame_deadline);
    }

    // ---- 5. Flush encoder -----------------------------------------------
    std::cerr << "[4/6] Flushing encoder...\n";
    avcodec_send_frame(enc_ctx, nullptr);
    while (avcodec_receive_packet(enc_ctx, pkt) >= 0) {
        bitstream.insert(bitstream.end(), pkt->data, pkt->data + pkt->size);
        av_packet_unref(pkt);
    }

    const auto encode_end = clock_hr::now();
    const double cpu_end = cpu_user_seconds();

    if (cam) cam->removeListener(&listener);
    cam.reset();

    const auto encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               encode_end - encode_start).count();
    const double wall_seconds = encode_ms / 1000.0;
    const double cpu_pct = wall_seconds > 0
                              ? ((cpu_end - cpu_start) / wall_seconds) * 100.0
                              : 0.0;

    // ---- 6. Decode roundtrip + PNG dump ---------------------------------
    std::cerr << "[5/6] Decoding " << bitstream.size() << " bytes back through libavcodec...\n";

    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!dec) {
        std::cerr << "SPIKE-FAIL: avcodec_find_decoder(AV_CODEC_ID_H264) returned null.\n";
        return 1;
    }
    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
    if (avcodec_open2(dec_ctx, dec, nullptr) < 0) {
        std::cerr << "SPIKE-FAIL: avcodec_open2 (decoder) failed.\n";
        return 1;
    }

    AVCodecParserContext* parser = av_parser_init(AV_CODEC_ID_H264);
    if (!parser) {
        std::cerr << "SPIKE-FAIL: av_parser_init failed.\n";
        return 1;
    }

    AVFrame* dec_frame = av_frame_alloc();
    AVPacket* dec_pkt = av_packet_alloc();

    int decoded_count = 0;
    const uint8_t* in_ptr = bitstream.data();
    int in_left = static_cast<int>(bitstream.size());

    auto drain_decoder = [&]() {
        while (avcodec_receive_frame(dec_ctx, dec_frame) >= 0) {
            char path[1024];
            std::snprintf(path, sizeof(path), "%s/frame_%03d.png",
                          out_dir.c_str(), decoded_count);
            write_png(path, dec_frame, sws_yuv_to_bgra);
            decoded_count++;
            av_frame_unref(dec_frame);
        }
    };

    while (in_left > 0) {
        int consumed = av_parser_parse2(parser, dec_ctx,
                                         &dec_pkt->data, &dec_pkt->size,
                                         in_ptr, in_left,
                                         AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (consumed < 0) break;
        in_ptr += consumed;
        in_left -= consumed;

        if (dec_pkt->size > 0) {
            if (avcodec_send_packet(dec_ctx, dec_pkt) >= 0)
                drain_decoder();
        }
    }
    // Flush the decoder.
    avcodec_send_packet(dec_ctx, nullptr);
    drain_decoder();

    av_packet_free(&dec_pkt);
    av_frame_free(&dec_frame);
    av_parser_close(parser);
    avcodec_free_context(&dec_ctx);

    av_packet_free(&pkt);
    av_frame_free(&enc_frame);
    avcodec_free_context(&enc_ctx);
    sws_freeContext(sws_argb_to_yuv);
    sws_freeContext(sws_yuv_to_bgra);

    // ---- 7. Print measured-numbers summary ------------------------------
    std::cerr << "[6/6] Done. Captured=" << captured
              << " encoded_bytes=" << bitstream.size()
              << " decoded_frames=" << decoded_count << "\n";

    const long bytes_per_frame = decoded_count > 0
                                     ? static_cast<long>(bitstream.size()) / decoded_count
                                     : 0;

    std::cout << "SPIKE-RESULT:"
              << " bytes=" << bitstream.size()
              << " frames=" << decoded_count
              << " avg_bytes_per_frame=" << bytes_per_frame
              << " encode_ms=" << encode_ms
              << " cpu_pct=" << static_cast<int>(cpu_pct)
              << " source=" << (use_synthetic_frames ? "synthetic" : "camera")
              << " out_dir=" << out_dir
              << std::endl;

    return 0;
}
