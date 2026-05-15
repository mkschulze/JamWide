# cmake/ffmpeg.cmake
# Quick task 260515-0pc — vendored LGPL ffmpeg + Cisco openh264 INTERFACE target.
#
# Exports `ffmpeg::lgpl` as an INTERFACE library that resolves to the per-arch
# vendored dylibs under libs/ffmpeg/macos-${ARCH}/. The video_spike test target
# (Task 2) consumes this. Production targets (JamWideJuce, njclient) do NOT
# consume this — wiring those is deferred to milestone item B/C/F per
# 260515-0pc-deferred-items.md.
#
# DEV-ARCH NOTE: Spike was vendored on x86_64 macOS (the only arch the dev
# machine reports). arm64 macOS, Linux x86_64, Windows x86_64 all deferred to
# milestone item B (universal-binary stitching + cross-platform vendoring).

if(TARGET ffmpeg::lgpl)
    return()
endif()

if(NOT APPLE)
    message(WARNING "ffmpeg::lgpl is only vendored for macOS in this spike — "
                    "non-Apple platforms are deferred to milestone item B.")
    return()
endif()

# Detect arch from CMAKE_HOST_SYSTEM_PROCESSOR or CMAKE_OSX_ARCHITECTURES.
if(CMAKE_OSX_ARCHITECTURES AND NOT "${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
    list(LENGTH CMAKE_OSX_ARCHITECTURES _arch_count)
    if(_arch_count GREATER 1)
        message(WARNING "ffmpeg::lgpl: requested universal build (${CMAKE_OSX_ARCHITECTURES}) "
                        "but spike vendored single-arch only — falling back to host arch. "
                        "Universal-binary stitching is milestone item B.")
        set(_ffmpeg_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    else()
        set(_ffmpeg_arch "${CMAKE_OSX_ARCHITECTURES}")
    endif()
else()
    set(_ffmpeg_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

if(_ffmpeg_arch STREQUAL "arm64")
    set(_ffmpeg_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/macos-arm64")
elseif(_ffmpeg_arch STREQUAL "x86_64" OR _ffmpeg_arch STREQUAL "AMD64")
    set(_ffmpeg_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/macos-x86_64")
else()
    message(FATAL_ERROR "ffmpeg::lgpl: unsupported arch '${_ffmpeg_arch}' (expected arm64 or x86_64)")
endif()

if(NOT EXISTS "${_ffmpeg_dir}/lib")
    message(WARNING "ffmpeg::lgpl: vendored tree missing at ${_ffmpeg_dir}/lib — "
                    "run scripts/build_ffmpeg_lgpl.sh first. Defining empty INTERFACE so "
                    "downstream guards (e.g. JAMWIDE_VIDEO_SPIKE) can fail loudly.")
    add_library(ffmpeg::lgpl INTERFACE IMPORTED)
    return()
endif()

# Resolve dylib paths. The version suffixes track the ffmpeg release vendored
# by scripts/build_ffmpeg_lgpl.sh (currently ffmpeg 7.1.2 + openh264 2.1.1).
file(GLOB _avcodec_dylib  "${_ffmpeg_dir}/lib/libavcodec.*.dylib")
file(GLOB _avformat_dylib "${_ffmpeg_dir}/lib/libavformat.*.dylib")
file(GLOB _swscale_dylib  "${_ffmpeg_dir}/lib/libswscale.*.dylib")
file(GLOB _avutil_dylib   "${_ffmpeg_dir}/lib/libavutil.*.dylib")
file(GLOB _openh264_dylib "${_ffmpeg_dir}/lib/libopenh264.*.dylib")

# Filter out symlinks — keep only the versioned canonical files.
foreach(_var _avcodec_dylib _avformat_dylib _swscale_dylib _avutil_dylib _openh264_dylib)
    set(_keep "")
    foreach(_p ${${_var}})
        if(NOT IS_SYMLINK "${_p}")
            list(APPEND _keep "${_p}")
        endif()
    endforeach()
    if(_keep)
        list(GET _keep 0 ${_var})
    else()
        # If only symlinks exist, just take the first one.
        if(${_var})
            list(GET ${_var} 0 ${_var})
        endif()
    endif()
endforeach()

add_library(ffmpeg::lgpl INTERFACE IMPORTED)
# IMPORTANT (#260515-0pc spike root cause): Apple clang's default include
# search order puts /usr/local/include BEFORE both -I and -isystem dirs
# emitted from INTERFACE_INCLUDE_DIRECTORIES on IMPORTED targets. Because
# the consumer often has homebrew's ffmpeg 8.x at /usr/local/include, the
# consumer would compile against ffmpeg-8 struct layouts (avcodec 62) but
# link against our vendored ffmpeg-7 dylibs (avcodec 61) — silently
# corrupting AVCodecContext at runtime (pix_fmt=1 right after
# avcodec_alloc_context3 when it should be -1).
#
# To force the vendored headers FIRST in the search order, the consumer
# MUST add the include path with `target_include_directories(<tgt> BEFORE
# PRIVATE ...)`. That puts -I FIRST (clang sorts user -I dirs ahead of
# /usr/local/include).
#
# We DO NOT set INTERFACE_INCLUDE_DIRECTORIES here, because (a) it would
# emit -isystem which is searched AFTER /usr/local/include on Apple, and
# (b) it would also dedup with the consumer's BEFORE -I (same canonical
# path), making the consumer's BEFORE -I get silently dropped.
#
# Document this for the consumer at the call site (see CMakeLists.txt's
# JAMWIDE_VIDEO_SPIKE block).
target_link_libraries(ffmpeg::lgpl INTERFACE
    "${_avcodec_dylib}"
    "${_avformat_dylib}"
    "${_swscale_dylib}"
    "${_avutil_dylib}"
    "${_openh264_dylib}"
)
# Stash the include path on the target as a custom property so consumers
# can read it back without hardcoding the per-arch logic again.
set_target_properties(ffmpeg::lgpl PROPERTIES
    JAMWIDE_FFMPEG_INCLUDE_DIR "${_ffmpeg_dir}/include"
)

# Set rpath so the executable finds the vendored dylibs at runtime.
# For the spike, the dylibs live in libs/ffmpeg/macos-${ARCH}/lib/ at the
# repo root. Production wiring (deferred — milestone item G) will copy them
# into the bundle's Contents/Frameworks/ via a post-build install_name_tool
# step.
target_link_options(ffmpeg::lgpl INTERFACE
    "LINKER:-rpath,${_ffmpeg_dir}/lib"
)

message(STATUS "ffmpeg::lgpl resolved → ${_ffmpeg_dir}")
message(STATUS "  avcodec  = ${_avcodec_dylib}")
message(STATUS "  avformat = ${_avformat_dylib}")
message(STATUS "  swscale  = ${_swscale_dylib}")
message(STATUS "  avutil   = ${_avutil_dylib}")
message(STATUS "  openh264 = ${_openh264_dylib}")
