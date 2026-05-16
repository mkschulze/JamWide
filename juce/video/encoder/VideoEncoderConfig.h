#pragma once
// Phase 20-01 — VideoEncoderConfig POD + H264Profile enum + preset helper.
//
// CONTEXT.md `<specifics>`: width / height / frameRate / targetBitrateKbps
// / H264Profile / gopHintFrames per CONTEXT.md and D-16's bitrate ladder.
//
// The preset → config mapping is the v1.3 Low / Medium / High ladder (D-16):
//
//   preset 0 → Low    : 320×240 @ 10 fps,  100 kbps, Baseline, gop=30
//   preset 1 → Medium : 640×480 @ 15 fps,  300 kbps, Baseline, gop=30
//   preset 2 → High   : 1280×720 @ 30 fps, 800 kbps, Baseline, gop=60
//
// Phase 19's CapturePreset enum publishes 0/1/2 — the encoder consumes that
// same int via `makeConfigForPreset(preset)` at open() time. Main/High H.264
// profiles are reserved for post-v1.3 hardware backends (VideoToolbox /
// MediaFoundation); v1.3 ships Baseline only (D-05).

namespace jamwide {

// H.264 profile selector. Baseline is the v1.3 ship target (D-05); Main /
// High are placeholders for post-v1.3 hardware backends. Values are NOT
// passed through to libavcodec directly — the openh264 backend maps
// H264Profile::Baseline → FF_PROFILE_H264_BASELINE in Openh264Encoder.cpp.
enum class H264Profile : int {
    Baseline = 0,
};

struct VideoEncoderConfig {
    int         width             = 320;                  // 320 / 640 / 1280
    int         height            = 240;                  // 240 / 480 /  720
    int         frameRate         = 10;                   // 10 /  15 /   30
    int         targetBitrateKbps = 100;                  // 100 / 300 / 800
    H264Profile profile           = H264Profile::Baseline;
    int         gopHintFrames     = 30;                   // hint only — force-IDR overrides per-frame
};

// Phase 19 capture-preset → encoder-config mapping (D-16).
// `preset` mirrors Phase 19's CapturePreset enum (0 = Low, 1 = Medium,
// 2 = High). Unknown values fall back to Low (defensive).
inline VideoEncoderConfig makeConfigForPreset(int preset) noexcept {
    switch (preset) {
        case 0:
            return { 320,  240, 10, 100, H264Profile::Baseline, 30 };  // Low — spike baseline
        case 1:
            return { 640,  480, 15, 300, H264Profile::Baseline, 30 };  // Medium
        case 2:
            return { 1280, 720, 30, 800, H264Profile::Baseline, 60 };  // High
        default:
            return { 320,  240, 10, 100, H264Profile::Baseline, 30 };  // defensive default
    }
}

} // namespace jamwide
