# cmake/jamwide_use_ffmpeg.cmake
# Phase 14.3-01 — Apple-clang BEFORE PRIVATE include discipline helper.
#
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
# cmake/ffmpeg.cmake DELIBERATELY DOES NOT set INTERFACE_INCLUDE_DIRECTORIES,
# because (a) it would emit -isystem which is searched AFTER /usr/local/include
# on Apple, and (b) it would also dedup with the consumer's BEFORE -I (same
# canonical path), making the consumer's BEFORE -I get silently dropped.
#
# This macro encapsulates the correct discipline so future ffmpeg consumers
# cannot accidentally bypass it. Always invoke it instead of wiring
# `target_link_libraries(<tgt> PRIVATE ffmpeg::lgpl)` directly.
#
# Usage:
#   include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ffmpeg.cmake)
#   include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/jamwide_use_ffmpeg.cmake)
#   add_executable(my_consumer ...)
#   jamwide_use_ffmpeg(my_consumer)

if(JAMWIDE_USE_FFMPEG_INCLUDED)
    return()
endif()
set(JAMWIDE_USE_FFMPEG_INCLUDED TRUE)

macro(jamwide_use_ffmpeg target)
    if(NOT TARGET ffmpeg::lgpl)
        message(FATAL_ERROR
            "jamwide_use_ffmpeg(${target}): ffmpeg::lgpl target not defined — "
            "include(cmake/ffmpeg.cmake) before calling jamwide_use_ffmpeg().")
    endif()

    get_target_property(_jw_ff_inc ffmpeg::lgpl JAMWIDE_FFMPEG_INCLUDE_DIR)
    if(NOT _jw_ff_inc)
        message(FATAL_ERROR
            "jamwide_use_ffmpeg(${target}): JAMWIDE_FFMPEG_INCLUDE_DIR property "
            "is missing on ffmpeg::lgpl — vendored tree probably not yet built. "
            "Run scripts/build_ffmpeg_lgpl.sh first.")
    endif()

    # BEFORE PRIVATE — emits -I (not -isystem) and puts it FIRST in the user-
    # include search order, overriding Apple clang's default /usr/local/include
    # search position.
    target_include_directories(${target} BEFORE PRIVATE "${_jw_ff_inc}")
    target_link_libraries(${target} PRIVATE ffmpeg::lgpl)
endmacro()
