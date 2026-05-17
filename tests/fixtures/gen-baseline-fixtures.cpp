// Plan 21-02 Task 3 — fixture generator for test_video_decoder.
//
// EXCLUDE_FROM_ALL CMake target. Run manually to regenerate the
// committed .bin fixtures under tests/fixtures/:
//
//   ./scripts/build.sh --tests gen_baseline_fixtures
//   ./build-test/gen_baseline_fixtures
//
// Generates:
//   sps_pps_baseline_320x240.bin    SPS+PPS (Annex-B framed) for H.264 Baseline 320×240
//   idr_baseline_320x240.bin        One IDR slice (Annex-B framed) at 320×240
//   sps_pps_baseline_640x480.bin    Same at 640×480 (for source-resolution-change test)
//   idr_baseline_640x480.bin        Same at 640×480
//   idr_baseline_320x240_red.bin    320×240 IDR with red Y/UV (back-to-back-push test)
//   idr_baseline_320x240_green.bin  320×240 IDR with green Y/UV
//   marker_payload_outer20.bin      24-byte on-wire marker [4B BE outer=20][20B payload]
//                                   for the codex Cluster 7 full-NinjamZap-bytes test
//
// SHA-256 hashes for tampering detection are recorded in
// tests/fixtures/README.md alongside provenance documentation.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Write `bytes` to `path`. Returns true on success.
bool write_file(const std::string& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::fprintf(stderr, "failed to open for write: %s\n", path.c_str());
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              (std::streamsize)bytes.size());
    if (!ofs) {
        std::fprintf(stderr, "failed to write: %s\n", path.c_str());
        return false;
    }
    std::printf("  wrote %s (%zu bytes)\n", path.c_str(), bytes.size());
    return true;
}

// Split an Annex-B byte stream into individual NAL units. Each output
// element begins with the start code (00 00 00 01) and contains exactly
// one NAL unit's worth of bytes. (Some openh264 outputs use 00 00 01;
// we normalize to the 4-byte start code.)
std::vector<std::vector<unsigned char>>
split_annexb_nals(const unsigned char* data, std::size_t size)
{
    std::vector<std::vector<unsigned char>> nals;
    if (size < 4) return nals;

    auto is_start_code_4 = [&](std::size_t i){
        return i + 3 < size
            && data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1;
    };
    auto is_start_code_3 = [&](std::size_t i){
        return i + 2 < size
            && data[i] == 0 && data[i+1] == 0 && data[i+2] == 1;
    };

    std::vector<std::size_t> starts;  // index of the first byte of the start code
    std::vector<std::size_t> codeLens;
    for (std::size_t i = 0; i < size; ) {
        if (is_start_code_4(i)) {
            starts.push_back(i);
            codeLens.push_back(4);
            i += 4;
        } else if (is_start_code_3(i)) {
            starts.push_back(i);
            codeLens.push_back(3);
            i += 3;
        } else {
            ++i;
        }
    }
    starts.push_back(size);
    codeLens.push_back(0);

    for (std::size_t j = 0; j + 1 < starts.size(); ++j) {
        const std::size_t startPos    = starts[j];
        const std::size_t payloadEnd  = starts[j + 1];
        const std::size_t payloadStart = startPos + codeLens[j];
        if (payloadStart >= payloadEnd) continue;
        std::vector<unsigned char> nal;
        nal.reserve(4 + (payloadEnd - payloadStart));
        nal.insert(nal.end(), {0x00, 0x00, 0x00, 0x01});
        nal.insert(nal.end(), data + payloadStart, data + payloadEnd);
        nals.push_back(std::move(nal));
    }
    return nals;
}

// Fill a YUV420P frame with a flat color expressed as Y/U/V samples.
void fill_yuv420p(AVFrame* frame, uint8_t y, uint8_t u, uint8_t v)
{
    const int w = frame->width;
    const int h = frame->height;
    // Y plane.
    for (int row = 0; row < h; ++row) {
        std::memset(frame->data[0] + row * frame->linesize[0], y, (std::size_t)w);
    }
    // U + V planes (half-resolution chroma in 420).
    for (int row = 0; row < h / 2; ++row) {
        std::memset(frame->data[1] + row * frame->linesize[1], u, (std::size_t)w / 2);
        std::memset(frame->data[2] + row * frame->linesize[2], v, (std::size_t)w / 2);
    }
}

// Encode one IDR frame at `width × height` using libopenh264 Baseline,
// fill the Y/U/V planes with the requested flat color. Returns the raw
// Annex-B byte stream that openh264 emits (which contains SPS + PPS +
// IDR NALs concatenated when global_header is OFF).
std::vector<unsigned char>
encode_one_idr(int width, int height, uint8_t y_sample, uint8_t u_sample, uint8_t v_sample)
{
    std::vector<unsigned char> out;

    const AVCodec* codec = avcodec_find_encoder_by_name("libopenh264");
    if (codec == nullptr) {
        std::fprintf(stderr, "libopenh264 encoder not available\n");
        return out;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (ctx == nullptr) return out;

    ctx->width        = width;
    ctx->height       = height;
    ctx->time_base    = AVRational{1, 10};   // 10 fps
    ctx->framerate    = AVRational{10, 1};
    ctx->pix_fmt      = AV_PIX_FMT_YUV420P;
    ctx->gop_size     = 1;                    // every frame is an IDR
    ctx->max_b_frames = 0;
    ctx->bit_rate     = 100 * 1000;           // 100 kbps
    ctx->profile      = FF_PROFILE_H264_BASELINE;
    ctx->thread_count = 1;

    // libopenh264 doesn't accept the "profile" string option directly —
    // we set ctx->profile already above, that's sufficient for libopenh264
    // to pick Baseline.
    int rc = avcodec_open2(ctx, codec, nullptr);
    if (rc < 0) {
        std::fprintf(stderr, "avcodec_open2 (encoder) failed: rc=%d\n", rc);
        avcodec_free_context(&ctx);
        return out;
    }

    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        avcodec_free_context(&ctx);
        return out;
    }
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width  = width;
    frame->height = height;
    rc = av_frame_get_buffer(frame, 32);
    if (rc < 0) {
        av_frame_free(&frame);
        avcodec_free_context(&ctx);
        return out;
    }
    fill_yuv420p(frame, y_sample, u_sample, v_sample);
    frame->pts = 0;
    // Mark as keyframe so the encoder always emits SPS/PPS + IDR.
    frame->pict_type = AV_PICTURE_TYPE_I;

    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        av_frame_free(&frame);
        avcodec_free_context(&ctx);
        return out;
    }

    // Send the frame.
    rc = avcodec_send_frame(ctx, frame);
    if (rc < 0) {
        std::fprintf(stderr, "send_frame failed: rc=%d\n", rc);
    }

    // Drain — libopenh264 sometimes wants flush.
    avcodec_send_frame(ctx, nullptr);

    while (true) {
        rc = avcodec_receive_packet(ctx, pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
        if (rc < 0) {
            std::fprintf(stderr, "receive_packet failed: rc=%d\n", rc);
            break;
        }
        out.insert(out.end(), pkt->data, pkt->data + pkt->size);
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return out;
}

// Split the encoded stream into:
//   sps_pps_bytes — Annex-B framed concatenation of SPS + PPS NALs
//   idr_bytes     — Annex-B framed IDR slice NAL
//
// NAL unit_type is in (byte_after_start_code & 0x1f). 7 = SPS, 8 = PPS,
// 5 = IDR slice. Other types (SEI, etc.) are appended to the SPS/PPS
// stream so the decoder sees them ahead of the IDR.
void split_sps_pps_and_idr(const std::vector<unsigned char>& encoded,
                            std::vector<unsigned char>& sps_pps_bytes,
                            std::vector<unsigned char>& idr_bytes)
{
    sps_pps_bytes.clear();
    idr_bytes.clear();
    auto nals = split_annexb_nals(encoded.data(), encoded.size());
    for (auto& nal : nals) {
        if (nal.size() < 5) continue;
        const unsigned char unit_type = nal[4] & 0x1f;
        if (unit_type == 7 || unit_type == 8 || unit_type == 6) {
            // SPS / PPS / SEI go in the parameter-set stream.
            sps_pps_bytes.insert(sps_pps_bytes.end(), nal.begin(), nal.end());
        } else if (unit_type == 5) {
            // IDR slice.
            idr_bytes.insert(idr_bytes.end(), nal.begin(), nal.end());
        }
    }
}

// Build the 24-byte on-wire marker frame:
//   [4B BE outer_len = 20][4B BE sender_seq][16B audio_guid]
std::vector<unsigned char> build_marker_payload_outer20()
{
    std::vector<unsigned char> v(24);
    // Outer length prefix (BE, value 20).
    v[0] = 0x00; v[1] = 0x00; v[2] = 0x00; v[3] = 0x14;  // 0x14 = 20
    // sender_seq = 0xDEAD0042 (BE).
    v[4] = 0xDE; v[5] = 0xAD; v[6] = 0x00; v[7] = 0x42;
    // audio_guid = 16 × 0xAA.
    std::fill(v.begin() + 8, v.end(), (unsigned char)0xAA);
    return v;
}

bool generate_codec_fixtures(int width, int height,
                              uint8_t y, uint8_t u, uint8_t v,
                              const std::string& sps_pps_path,
                              const std::string& idr_path)
{
    std::printf("encoding %dx%d Y=%u U=%u V=%u ...\n", width, height, y, u, v);
    auto encoded = encode_one_idr(width, height, y, u, v);
    if (encoded.empty()) {
        std::fprintf(stderr, "encoder produced no bytes for %dx%d\n", width, height);
        return false;
    }
    std::vector<unsigned char> sps_pps_bytes, idr_bytes;
    split_sps_pps_and_idr(encoded, sps_pps_bytes, idr_bytes);
    if (sps_pps_bytes.empty() || idr_bytes.empty()) {
        std::fprintf(stderr, "split failed: sps_pps=%zu idr=%zu\n",
                     sps_pps_bytes.size(), idr_bytes.size());
        return false;
    }
    if (!write_file(sps_pps_path, sps_pps_bytes)) return false;
    if (!write_file(idr_path,     idr_bytes))     return false;
    return true;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Output directory (default: tests/fixtures relative to CWD).
    std::string outDir = "tests/fixtures";
    if (argc >= 2) outDir = argv[1];

    std::printf("Plan 21-02 Task 3 fixture generator\n");
    std::printf("output dir: %s\n", outDir.c_str());

    // 1. Baseline 320×240 gray.
    if (!generate_codec_fixtures(320, 240, 128, 128, 128,
                                  outDir + "/sps_pps_baseline_320x240.bin",
                                  outDir + "/idr_baseline_320x240.bin")) {
        return 1;
    }

    // 2. Baseline 640×480 gray (for source-resolution-change test).
    if (!generate_codec_fixtures(640, 480, 128, 128, 128,
                                  outDir + "/sps_pps_baseline_640x480.bin",
                                  outDir + "/idr_baseline_640x480.bin")) {
        return 1;
    }

    // 3. Baseline 320×240 RED (Y≈82, U≈90, V≈240 for BT.601 red).
    {
        std::printf("encoding 320x240 RED ...\n");
        auto encoded = encode_one_idr(320, 240, 82, 90, 240);
        if (encoded.empty()) { return 1; }
        std::vector<unsigned char> sps_pps_bytes, idr_bytes;
        split_sps_pps_and_idr(encoded, sps_pps_bytes, idr_bytes);
        if (idr_bytes.empty()) return 1;
        // Write the IDR only — tests reuse the gray SPS/PPS for red+green
        // since the SPS/PPS is identical (same width/height/profile).
        if (!write_file(outDir + "/idr_baseline_320x240_red.bin", idr_bytes)) return 1;
    }

    // 4. Baseline 320×240 GREEN (Y≈145, U≈54, V≈34 for BT.601 green).
    {
        std::printf("encoding 320x240 GREEN ...\n");
        auto encoded = encode_one_idr(320, 240, 145, 54, 34);
        if (encoded.empty()) { return 1; }
        std::vector<unsigned char> sps_pps_bytes, idr_bytes;
        split_sps_pps_and_idr(encoded, sps_pps_bytes, idr_bytes);
        if (idr_bytes.empty()) return 1;
        if (!write_file(outDir + "/idr_baseline_320x240_green.bin", idr_bytes)) return 1;
    }

    // 5. Marker payload (codex Cluster 7): 24-byte on-wire bytes,
    //    20-byte payload after outer prefix consumed.
    {
        auto bytes = build_marker_payload_outer20();
        if (!write_file(outDir + "/marker_payload_outer20.bin", bytes)) return 1;
    }

    std::printf("\nFixture generation complete.\n");
    return 0;
}
