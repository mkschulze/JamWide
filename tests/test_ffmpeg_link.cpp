// tests/test_ffmpeg_link.cpp
//
// Phase 14.3-01 Task 3 — JAMWIDE_VIDEO_SPIKE-gated codec-init smoke test
// covering Phase 14.3 success criteria SC-1 (vendored libs present) +
// SC-3 (dlopen succeeds; encoder available) + SC-8 (codec-find half).
//
// Carved from tests/video_spike.cpp's first 50 lines: same header-ordering
// preamble (ffmpeg headers FIRST, no JUCE, no stdlib include before
// ffmpeg — Landmine L6); just the avformat_version() and
// avcodec_find_encoder_by_name("libopenh264") assertions. JUCE camera /
// graphics / encode / decode / PNG-dump scaffolding entirely stripped.
//
// Build (gated behind JAMWIDE_VIDEO_SPIKE option):
//   cmake -S . -B build-juce -G Ninja -DJAMWIDE_BUILD_TESTS=ON -DJAMWIDE_VIDEO_SPIKE=ON
//   cmake --build build-juce --target test_ffmpeg_link
//
// Run:
//   build-juce/test_ffmpeg_link        (exits 0 on success, 1 on any failure)
//   cd build-juce && ctest -R ffmpeg_link_smoke --output-on-failure
//
// Expected output:
//   OK: avformat_version=<N>, libopenh264 encoder available

// IMPORTANT: ffmpeg headers BEFORE any other system include. JUCE's headers
// and stdlib headers, when included before ffmpeg's libavcodec headers,
// cause silent struct-ABI corruption in AVCodecContext if /usr/local/include
// has ffmpeg 8.x while we link against the vendored ffmpeg 7.x dylibs
// (Apple-clang search order, /usr/local/include precedes -I). The CMake
// `jamwide_use_ffmpeg()` macro emits `-I<vendored>` with BEFORE PRIVATE
// so the vendored headers win the search order, but the runtime header-
// ordering discipline at the source level is still the safest belt-and-
// braces guard. Diagnosed during 260515-0pc spike — see comment block in
// tests/video_spike.cpp:28-39 for the original root-cause analysis.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <cstdio>

int main()
{
    if (avformat_version() == 0) {
        fprintf(stderr, "FAIL: avformat_version() returned 0 (libavformat not loaded)\n");
        return 1;
    }

    const AVCodec* enc = avcodec_find_encoder_by_name("libopenh264");
    if (enc == nullptr) {
        fprintf(stderr, "FAIL: avcodec_find_encoder_by_name(\"libopenh264\") "
                        "returned nullptr — encoder not registered. "
                        "Check vendored libavcodec configuration "
                        "(--enable-libopenh264 --enable-encoder=libopenh264).\n");
        return 1;
    }

    printf("OK: avformat_version=%u, libopenh264 encoder available\n",
           avformat_version());
    return 0;
}
