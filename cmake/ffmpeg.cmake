# cmake/ffmpeg.cmake
# Phase 14.3-01 — vendored LGPL ffmpeg + Cisco openh264 INTERFACE target.
#
# Exports `ffmpeg::lgpl` as an INTERFACE library that resolves to the
# vendored shared libraries under libs/ffmpeg/<platform>/ for whichever
# platform CMake is configuring on. Consumers MUST use the
# `jamwide_use_ffmpeg(<target>)` macro from cmake/jamwide_use_ffmpeg.cmake
# — the macro bakes in the Apple-clang `BEFORE PRIVATE` include discipline
# required to avoid the homebrew-ffmpeg shadowing bug (Landmine L6).
#
# Platforms (mapping per phase 14.3-01 plan, file map):
#   APPLE arm64       → libs/ffmpeg/macos-arm64/   (Cisco openh264 source-build)
#   APPLE x86_64      → libs/ffmpeg/macos-x86_64/  (Cisco osx64 prebuilt)
#   UNIX (Linux only) → libs/ffmpeg/linux-x86_64/  (Cisco linux64 prebuilt)
#   WIN32             → libs/ffmpeg/windows-x86_64/ (Cisco win64 prebuilt)

if(TARGET ffmpeg::lgpl)
    return()
endif()

if(APPLE)
    # Detect arch from CMAKE_HOST_SYSTEM_PROCESSOR or CMAKE_OSX_ARCHITECTURES.
    if(CMAKE_OSX_ARCHITECTURES AND NOT "${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
        list(LENGTH CMAKE_OSX_ARCHITECTURES _arch_count)
        if(_arch_count GREATER 1)
            message(WARNING "ffmpeg::lgpl: requested universal build (${CMAKE_OSX_ARCHITECTURES}) "
                            "but vendored trees are per-arch — falling back to host arch. "
                            "Universal-binary stitching is a Phase 14.3 follow-up.")
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
        message(FATAL_ERROR "ffmpeg::lgpl: unsupported macOS arch '${_ffmpeg_arch}' (expected arm64 or x86_64)")
    endif()
    set(_lib_glob "*.dylib")
    set(_have_symlinks TRUE)
elseif(UNIX AND NOT APPLE)
    set(_ffmpeg_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/linux-x86_64")
    set(_lib_glob "*.so*")
    set(_have_symlinks TRUE)
elseif(WIN32)
    set(_ffmpeg_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/windows-x86_64")
    set(_lib_glob "*.dll")
    set(_have_symlinks FALSE)
else()
    message(FATAL_ERROR "ffmpeg::lgpl: unsupported platform")
endif()

if(NOT EXISTS "${_ffmpeg_dir}/lib")
    message(WARNING "ffmpeg::lgpl: vendored tree missing at ${_ffmpeg_dir}/lib — "
                    "run scripts/build_ffmpeg_lgpl.sh first. Defining empty INTERFACE so "
                    "downstream guards (e.g. JAMWIDE_VIDEO_SPIKE) can fail loudly.")
    add_library(ffmpeg::lgpl INTERFACE IMPORTED)
    return()
endif()

# Resolve library paths. The version suffixes track the ffmpeg release vendored
# by scripts/build_ffmpeg_lgpl.sh (currently ffmpeg 7.1.2 + openh264 2.1.1).
file(GLOB _avcodec_lib  "${_ffmpeg_dir}/lib/libavcodec.${_lib_glob}")
file(GLOB _avformat_lib "${_ffmpeg_dir}/lib/libavformat.${_lib_glob}")
file(GLOB _swscale_lib  "${_ffmpeg_dir}/lib/libswscale.${_lib_glob}")
file(GLOB _avutil_lib   "${_ffmpeg_dir}/lib/libavutil.${_lib_glob}")
file(GLOB _openh264_lib "${_ffmpeg_dir}/lib/libopenh264.${_lib_glob}")

# Filter out symlinks — keep only the versioned canonical files. On Windows
# (.dll) there are no symlinks, so this is a no-op; on macOS/Linux it
# resolves to e.g. libavcodec.61.19.101.dylib not libavcodec.dylib.
if(_have_symlinks)
    foreach(_var _avcodec_lib _avformat_lib _swscale_lib _avutil_lib _openh264_lib)
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
else()
    foreach(_var _avcodec_lib _avformat_lib _swscale_lib _avutil_lib _openh264_lib)
        if(${_var})
            list(GET ${_var} 0 ${_var})
        endif()
    endforeach()
endif()

add_library(ffmpeg::lgpl INTERFACE IMPORTED)
# IMPORTANT (Landmine L6 / #260515-0pc spike root cause): Apple clang's default
# include search order puts /usr/local/include BEFORE both -I and -isystem
# dirs emitted from INTERFACE_INCLUDE_DIRECTORIES on IMPORTED targets. Because
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
# The cmake/jamwide_use_ffmpeg.cmake helper macro encapsulates this
# discipline — every consumer should call jamwide_use_ffmpeg(<target>)
# rather than wiring the include path manually.
target_link_libraries(ffmpeg::lgpl INTERFACE
    "${_avcodec_lib}"
    "${_avformat_lib}"
    "${_swscale_lib}"
    "${_avutil_lib}"
    "${_openh264_lib}"
)
# Stash the include path on the target as a custom property so the
# jamwide_use_ffmpeg() macro can read it back without re-computing the
# per-platform logic.
set_target_properties(ffmpeg::lgpl PROPERTIES
    JAMWIDE_FFMPEG_INCLUDE_DIR "${_ffmpeg_dir}/include"
)

# POSIX runtime discovery: emit -rpath pointing at the vendored dir so the
# executable finds the shared libraries at runtime. On Windows (.dll
# discovery via consumer-adjacent path or %PATH%) this is a no-op.
if(NOT WIN32)
    target_link_options(ffmpeg::lgpl INTERFACE
        "LINKER:-rpath,${_ffmpeg_dir}/lib"
    )
endif()

message(STATUS "ffmpeg::lgpl resolved → ${_ffmpeg_dir}")
message(STATUS "  avcodec  = ${_avcodec_lib}")
message(STATUS "  avformat = ${_avformat_lib}")
message(STATUS "  swscale  = ${_swscale_lib}")
message(STATUS "  avutil   = ${_avutil_lib}")
message(STATUS "  openh264 = ${_openh264_lib}")
