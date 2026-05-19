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
    set(_universal_build FALSE)
    if(CMAKE_OSX_ARCHITECTURES AND NOT "${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
        list(LENGTH CMAKE_OSX_ARCHITECTURES _arch_count)
        if(_arch_count GREATER 1)
            set(_universal_build TRUE)
        else()
            set(_ffmpeg_arch "${CMAKE_OSX_ARCHITECTURES}")
        endif()
    else()
        set(_ffmpeg_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()

    if(_universal_build)
        # ───── Universal mac build ─────
        # CMAKE_OSX_ARCHITECTURES is a list (typically "arm64;x86_64") so the
        # JUCE/CMake build will compile each .o twice and the linker will
        # produce fat binaries. ffmpeg/openh264 dylibs must also be fat — lipo
        # the per-arch canonical files into universal dylibs in the build
        # tree, then point ffmpeg::lgpl at that build-tree directory.
        #
        # We DON'T commit the lipo'd universal tree to libs/ffmpeg/ —
        # CMAKE_BINARY_DIR is the right home for arch-combined artifacts that
        # the per-arch trees imply. Regenerated whenever the build dir is
        # wiped or whenever per-arch sources mtime newer than universal.
        set(_arm64_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/macos-arm64")
        set(_x86_dir   "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/macos-x86_64")
        set(_ffmpeg_dir "${CMAKE_BINARY_DIR}/ffmpeg-universal")

        if(NOT EXISTS "${_arm64_dir}/lib" OR NOT EXISTS "${_x86_dir}/lib")
            message(FATAL_ERROR
                "ffmpeg::lgpl: universal build (${CMAKE_OSX_ARCHITECTURES}) "
                "requires BOTH libs/ffmpeg/macos-arm64/ and "
                "libs/ffmpeg/macos-x86_64/ to be vendored. Found:\n"
                "  arm64:   ${_arm64_dir}/lib (exists: $<IF:EXISTS,YES,NO>)\n"
                "  x86_64:  ${_x86_dir}/lib (exists: $<IF:EXISTS,YES,NO>)")
        endif()

        if(NOT EXISTS "${_ffmpeg_dir}/lib/libavcodec.dylib")
            message(STATUS "ffmpeg::lgpl: building universal tree at ${_ffmpeg_dir}")
            file(MAKE_DIRECTORY "${_ffmpeg_dir}/lib")
            # Headers are identical between arches — copy arm64's set.
            file(COPY "${_arm64_dir}/include" DESTINATION "${_ffmpeg_dir}")

            # lipo each canonical versioned dylib (skip symlinks; we recreate
            # them after).
            file(GLOB _src_libs "${_arm64_dir}/lib/lib*.dylib")
            foreach(_arm64_lib ${_src_libs})
                if(IS_SYMLINK "${_arm64_lib}")
                    continue()
                endif()
                get_filename_component(_libname "${_arm64_lib}" NAME)
                set(_x86_lib "${_x86_dir}/lib/${_libname}")
                set(_uni_lib "${_ffmpeg_dir}/lib/${_libname}")
                if(NOT EXISTS "${_x86_lib}")
                    message(FATAL_ERROR
                        "ffmpeg::lgpl: macos-arm64 has ${_libname} but "
                        "macos-x86_64 doesn't — version mismatch between trees")
                endif()
                execute_process(
                    COMMAND lipo -create "${_arm64_lib}" "${_x86_lib}"
                                  -output "${_uni_lib}"
                    RESULT_VARIABLE _lipo_rc
                    ERROR_VARIABLE  _lipo_err
                )
                if(NOT _lipo_rc EQUAL 0)
                    message(FATAL_ERROR
                        "lipo failed for ${_libname}: ${_lipo_err}")
                endif()
                message(STATUS "  lipo'd ${_libname}")
            endforeach()

            # Recreate the symlink aliases (e.g. libavcodec.61.dylib →
            # libavcodec.61.19.101.dylib) so @rpath/libavcodec.61.dylib
            # resolves at runtime. Read symlink targets from the arm64 tree
            # (the structure is identical to x86_64 by construction).
            file(GLOB _all_libs "${_arm64_dir}/lib/lib*.dylib")
            foreach(_src ${_all_libs})
                if(IS_SYMLINK "${_src}")
                    get_filename_component(_linkname "${_src}" NAME)
                    execute_process(
                        COMMAND readlink "${_src}"
                        OUTPUT_VARIABLE _target
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                    )
                    execute_process(
                        COMMAND ln -sf "${_target}"
                                       "${_ffmpeg_dir}/lib/${_linkname}"
                    )
                endif()
            endforeach()
        endif()
        set(_lib_glob "*.dylib")
        set(_have_symlinks TRUE)
    else()
        # ───── Single-arch mac build (host or explicit) ─────
        if(_ffmpeg_arch STREQUAL "arm64")
            set(_ffmpeg_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/macos-arm64")
        elseif(_ffmpeg_arch STREQUAL "x86_64" OR _ffmpeg_arch STREQUAL "AMD64")
            set(_ffmpeg_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/macos-x86_64")
        else()
            message(FATAL_ERROR "ffmpeg::lgpl: unsupported macOS arch '${_ffmpeg_arch}' (expected arm64 or x86_64)")
        endif()
        set(_lib_glob "*.dylib")
        set(_have_symlinks TRUE)
    endif()
elseif(UNIX AND NOT APPLE)
    set(_ffmpeg_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/linux-x86_64")
    set(_lib_glob "*.so*")
    set(_have_symlinks TRUE)
elseif(WIN32)
    set(_ffmpeg_dir "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg/windows-x86_64")
    # MSVC links against .lib import libraries, not .dll directly.
    # build_ffmpeg_lgpl.sh's Windows post-process generates these via
    # gendef + dlltool so $PREFIX/lib has both .dll (runtime) and .lib
    # (link-time). ffmpeg's MinGW autotools convention drops the `lib`
    # prefix on Windows (e.g. avcodec-61.dll, avcodec-61.lib) — handle
    # that below by globbing both `lib${name}.*` and `${name}-*.lib`.
    set(_lib_glob "lib")
    set(_have_symlinks FALSE)
    set(_win32_no_libprefix TRUE)
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
if(_win32_no_libprefix)
    # Windows: ffmpeg DLLs/libs lack the `lib` prefix (avcodec-61.lib) and
    # are versioned (-61, -7, -8 etc.). Glob both `lib${name}*.lib`
    # (openh264 keeps the prefix) and `${name}-*.lib` (ffmpeg drops it).
    file(GLOB _avcodec_lib  "${_ffmpeg_dir}/lib/avcodec-*.lib"  "${_ffmpeg_dir}/lib/libavcodec.lib")
    file(GLOB _avformat_lib "${_ffmpeg_dir}/lib/avformat-*.lib" "${_ffmpeg_dir}/lib/libavformat.lib")
    file(GLOB _swscale_lib  "${_ffmpeg_dir}/lib/swscale-*.lib"  "${_ffmpeg_dir}/lib/libswscale.lib")
    file(GLOB _avutil_lib   "${_ffmpeg_dir}/lib/avutil-*.lib"   "${_ffmpeg_dir}/lib/libavutil.lib")
    file(GLOB _openh264_lib "${_ffmpeg_dir}/lib/libopenh264*.lib")
else()
    file(GLOB _avcodec_lib  "${_ffmpeg_dir}/lib/libavcodec.${_lib_glob}")
    file(GLOB _avformat_lib "${_ffmpeg_dir}/lib/libavformat.${_lib_glob}")
    file(GLOB _swscale_lib  "${_ffmpeg_dir}/lib/libswscale.${_lib_glob}")
    file(GLOB _avutil_lib   "${_ffmpeg_dir}/lib/libavutil.${_lib_glob}")
    file(GLOB _openh264_lib "${_ffmpeg_dir}/lib/libopenh264.${_lib_glob}")
endif()

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
# Stash include + lib dirs on the target as custom properties so the
# jamwide_use_ffmpeg() / jamwide_bundle_ffmpeg_apple() helpers can read
# them back without re-computing the per-platform logic.
set_target_properties(ffmpeg::lgpl PROPERTIES
    JAMWIDE_FFMPEG_INCLUDE_DIR "${_ffmpeg_dir}/include"
    JAMWIDE_FFMPEG_LIB_DIR "${_ffmpeg_dir}/lib"
)

# POSIX runtime discovery:
#   * Linux: emit -rpath pointing at the vendored dir so the executable
#     finds the shared libraries at runtime. Linux distribution still
#     deferred (per memory `project_jamtaba_video_port`: receive-only v1,
#     CameraDevice conditional pending), so the build-tree rpath is fine
#     for the dev/CI workflow we have today.
#   * Apple: do NOT bake in the build-tree path. Apple distribution
#     requires dylibs to live inside <bundle>/Contents/Frameworks/ with
#     the binary's rpath set to @loader_path/../Frameworks. That's done
#     post-build by jamwide_bundle_ffmpeg_apple() — see below + the
#     companion cmake/bundle_ffmpeg_macos.cmake script. The old
#     target_link_options(LINKER:-rpath,...) emission baked the CI
#     runner's absolute build path into shipping binaries and caused the
#     v1.1-beta.20.8 crash-on-launch on user machines.
#   * Windows: .dll discovery via consumer-adjacent path or %PATH%.
if(UNIX AND NOT APPLE)
    target_link_options(ffmpeg::lgpl INTERFACE
        "LINKER:-rpath,${_ffmpeg_dir}/lib"
    )
endif()

# Apple-only POST_BUILD bundling helper. Capture the cmake/ dir at
# definition time so the function call site doesn't need to know where
# the script lives.
set(_JAMWIDE_FFMPEG_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(jamwide_bundle_ffmpeg_apple target)
    if(NOT APPLE)
        return()
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "jamwide_bundle_ffmpeg_apple: target '${target}' does not exist")
    endif()
    get_target_property(_ff_lib_dir ffmpeg::lgpl JAMWIDE_FFMPEG_LIB_DIR)
    if(NOT _ff_lib_dir)
        message(FATAL_ERROR
            "jamwide_bundle_ffmpeg_apple(${target}): ffmpeg::lgpl missing "
            "JAMWIDE_FFMPEG_LIB_DIR property — vendored tree probably not built.")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${_ff_lib_dir}
            -DBUNDLE=$<TARGET_BUNDLE_DIR:${target}>
            -DBINARY=$<TARGET_FILE:${target}>
            -P "${_JAMWIDE_FFMPEG_CMAKE_DIR}/bundle_ffmpeg_macos.cmake"
        COMMENT "Bundling ffmpeg dylibs into ${target}"
        VERBATIM
    )
endfunction()

message(STATUS "ffmpeg::lgpl resolved → ${_ffmpeg_dir}")
message(STATUS "  avcodec  = ${_avcodec_lib}")
message(STATUS "  avformat = ${_avformat_lib}")
message(STATUS "  swscale  = ${_swscale_lib}")
message(STATUS "  avutil   = ${_avutil_lib}")
message(STATUS "  openh264 = ${_openh264_lib}")
