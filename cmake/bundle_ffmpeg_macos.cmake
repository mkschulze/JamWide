# cmake/bundle_ffmpeg_macos.cmake
# Phase 23 follow-up — bundle the vendored LGPL ffmpeg dylibs into a JUCE
# plugin bundle's Contents/Frameworks/ and rewrite the main binary's rpath so
# dyld finds them via @loader_path/../Frameworks instead of an absolute build-
# tree path baked in by `target_link_options(... LINKER:-rpath,...)`.
#
# Run as a POST_BUILD step:
#   cmake -DSRC_DIR=<ffmpeg-lib-dir>
#         -DBUNDLE=<bundle-dir>            # e.g. .../JamWide.app or .../JamWide.vst3
#         -DBINARY=<bundle>/Contents/MacOS/<name>
#         -P cmake/bundle_ffmpeg_macos.cmake
#
# Background (root cause of beta.20.8 crash-on-launch):
#   `otool -L JamWide.app/Contents/MacOS/JamWide` showed each ffmpeg dylib
#   referenced as `@rpath/libavcodec.61.19.101.dylib`, but the only LC_RPATH
#   on the binary was the GitHub Actions runner's absolute build path
#   (`/Users/runner/work/JamWide/JamWide/build/ffmpeg-universal/lib`). That
#   path doesn't exist on user machines → dyld aborts with
#   "Library not loaded: @rpath/libavcodec.61.19.101.dylib". Distribution
#   binaries need a bundle-relative rpath PLUS the dylibs physically present
#   inside Contents/Frameworks/. This script does both.

if(NOT SRC_DIR OR NOT BUNDLE OR NOT BINARY)
    message(FATAL_ERROR
        "bundle_ffmpeg_macos.cmake requires -DSRC_DIR -DBUNDLE -DBINARY")
endif()

if(NOT EXISTS "${SRC_DIR}")
    message(FATAL_ERROR "SRC_DIR does not exist: ${SRC_DIR}")
endif()
if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "BINARY does not exist: ${BINARY}")
endif()

set(_dst "${BUNDLE}/Contents/Frameworks")
file(MAKE_DIRECTORY "${_dst}")

# Copy every *.dylib in SRC_DIR into Frameworks/, preserving symlinks.
# CMake's `-E copy` follows symlinks (dereferences them) so we handle
# symlinks separately via create_symlink. The short-name symlinks (e.g.
# libavcodec.61.dylib → libavcodec.61.19.101.dylib) MUST survive because
# libavformat's LC_LOAD_DYLIB references @rpath/libavcodec.61.dylib (the
# short alias), not the versioned canonical name.
file(GLOB _items "${SRC_DIR}/*.dylib")
foreach(_item ${_items})
    get_filename_component(_name "${_item}" NAME)
    set(_d "${_dst}/${_name}")
    if(IS_SYMLINK "${_item}")
        file(READ_SYMLINK "${_item}" _link_target)
        file(REMOVE "${_d}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${_link_target}" "${_d}"
            RESULT_VARIABLE _rc
        )
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR
                "Failed to create symlink ${_d} -> ${_link_target}")
        endif()
    else()
        # configure_file with COPYONLY does timestamp-based skip, so
        # incremental rebuilds don't re-copy ~50MB of dylibs.
        configure_file("${_item}" "${_d}" COPYONLY)
    endif()
endforeach()

# Rewrite rpaths on the main binary.
#   1. Strip any LC_RPATH whose path looks like a build-tree absolute path —
#      either the SRC_DIR itself or anything matching /build/ffmpeg-universal/.
#      install_name_tool -delete_rpath requires an exact match; we discover
#      what's currently there via `otool -l` and only delete what exists.
#   2. Add @loader_path/../Frameworks if missing.
execute_process(
    COMMAND otool -l "${BINARY}"
    OUTPUT_VARIABLE _otool_out
    RESULT_VARIABLE _otool_rc
)
if(NOT _otool_rc EQUAL 0)
    message(FATAL_ERROR "otool -l failed on ${BINARY}")
endif()

# Parse LC_RPATH paths. Each load command appears as:
#   cmd LC_RPATH
#   cmdsize N
#   path /actual/path (offset 12)
string(REGEX MATCHALL "LC_RPATH[^\n]*\n[^\n]*\n[^\n]*path [^ ]+ " _rpath_blocks "${_otool_out}")
set(_existing_rpaths "")
foreach(_block ${_rpath_blocks})
    if(_block MATCHES "path ([^ ]+) ")
        list(APPEND _existing_rpaths "${CMAKE_MATCH_1}")
    endif()
endforeach()

set(_target_rpath "@loader_path/../Frameworks")

foreach(_rp ${_existing_rpaths})
    # Drop ANY rpath that's an absolute filesystem path under a build dir
    # (matches both local dev builds and CI runner builds). We keep
    # @loader_path/... or @executable_path/... rpaths untouched.
    if(_rp MATCHES "^/")
        execute_process(
            COMMAND install_name_tool -delete_rpath "${_rp}" "${BINARY}"
            RESULT_VARIABLE _rc
            ERROR_VARIABLE _err
            OUTPUT_QUIET
        )
        if(NOT _rc EQUAL 0)
            message(WARNING
                "install_name_tool -delete_rpath '${_rp}' failed: ${_err}")
        else()
            message(STATUS "  stripped build-tree rpath: ${_rp}")
        endif()
    endif()
endforeach()

# Add the bundle-relative rpath if it isn't already there.
list(FIND _existing_rpaths "${_target_rpath}" _has_target_rpath)
if(_has_target_rpath EQUAL -1)
    execute_process(
        COMMAND install_name_tool -add_rpath "${_target_rpath}" "${BINARY}"
        RESULT_VARIABLE _rc
        ERROR_VARIABLE _err
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "install_name_tool -add_rpath '${_target_rpath}' failed: ${_err}")
    endif()
    message(STATUS "  added bundle rpath: ${_target_rpath}")
endif()
