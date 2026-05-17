cmake_minimum_required(VERSION 3.20)
project(jamwide VERSION 1.0.0 LANGUAGES C CXX)

# Windows: Prevent min/max macros from conflicting with std::min/std::max
# and use dynamic MSVC runtime for compatibility with clap-wrapper
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    # Use dynamic runtime library (MD/MDd) to match clap-wrapper
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
    option(JAMWIDE_UNIVERSAL "Build universal binary (arm64 + x86_64)" ON)
    if(JAMWIDE_UNIVERSAL)
        if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "")
            set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
        endif()
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(JAMWIDE_BUILD_TESTS "Build tests" OFF)
option(JAMWIDE_DEV_BUILD "Enable development build with verbose logging" ON)
option(JAMWIDE_BUILD_JUCE "Build JUCE plugin targets" ON)
# 15.1-04: ThreadSanitizer build. When ON, codesign and hardened-runtime are
# skipped (TSan injects a runtime library that ad-hoc codesigning does not
# cover, per 15.1-RESEARCH macOS caveat #1). Wired up by scripts/build.sh --tsan.
option(JAMWIDE_TSAN "Build with ThreadSanitizer (skips codesign and hardened runtime)" OFF)
# 260515-0pc Quick task feasibility spike: opt-in build of tests/video_spike.cpp.
# Default OFF so headless CI without a camera (or without vendored ffmpeg) is
# unaffected. Enable explicitly:
#   cmake -DJAMWIDE_BUILD_TESTS=ON -DJAMWIDE_VIDEO_SPIKE=ON ...
# Requires libs/ffmpeg/macos-${ARCH}/ to be populated by scripts/build_ffmpeg_lgpl.sh.
option(JAMWIDE_VIDEO_SPIKE "Build the video_spike feasibility test (requires vendored LGPL ffmpeg)" OFF)
# Auto-increment build number on every build
add_custom_target(jamwide-build-number ALL
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_NUMBER_FILE=${CMAKE_SOURCE_DIR}/src/build_number.h
        -P ${CMAKE_SOURCE_DIR}/cmake/increment-build-revision.cmake
    COMMENT "Incrementing build number"
)

# Plugin identity (single source of truth for JUCE targets)
set(JAMWIDE_JUCE_BUNDLE_ID "com.jamwide.juce-client")
set(JAMWIDE_JUCE_MANUFACTURER_CODE "JmWd")
set(JAMWIDE_JUCE_PLUGIN_CODE "JwJc")


# libogg
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/libogg EXCLUDE_FROM_ALL)

# libvorbis (needs OGG_INCLUDE_DIR set)
set(OGG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/libogg/include)
set(OGG_LIBRARY ogg)
add_subdirectory(libs/libvorbis EXCLUDE_FROM_ALL)

# libFLAC
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WITH_OGG OFF CACHE BOOL "" FORCE)        # Native FLAC, not OGG FLAC
set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)    # C API only
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
set(INSTALL_PKGCONFIG_MODULES OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/libflac EXCLUDE_FROM_ALL)

# WDL library (static) — shared by both CLAP and JUCE targets
add_library(wdl STATIC
    wdl/jnetlib/asyncdns.cpp
    wdl/jnetlib/connection.cpp
    wdl/jnetlib/httpget.cpp
    wdl/jnetlib/listen.cpp
    wdl/jnetlib/util.cpp
    wdl/sha.cpp
    wdl/rng.cpp
)
target_include_directories(wdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/wdl)
if(WIN32)
    # Add UTF8 wrapper functions for Windows
    target_sources(wdl PRIVATE wdl/win32_utf8.c)
    target_link_libraries(wdl PUBLIC ws2_32 comdlg32 shell32)
endif()

# ── Crypto (AES-256-CBC, SHA-256 key derivation — Phase 15) ──
# Windows: BCrypt (built into Windows, no external dependency needed)
# macOS/Linux: OpenSSL
if(NOT WIN32)
    if(APPLE AND NOT OPENSSL_ROOT_DIR)
        # Fallback: help CMake find Homebrew OpenSSL on macOS (not in default search path)
        execute_process(COMMAND brew --prefix openssl@3
            OUTPUT_VARIABLE OPENSSL_BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(OPENSSL_BREW_PREFIX)
            set(OPENSSL_ROOT_DIR "${OPENSSL_BREW_PREFIX}" CACHE PATH "OpenSSL root")
        endif()
    endif()
    find_package(OpenSSL REQUIRED)
endif()

# NJClient core (static) — shared by both CLAP and JUCE targets
add_library(njclient STATIC
    src/core/njclient.cpp
    src/core/netmsg.cpp
    src/core/mpb.cpp
    src/core/njmisc.cpp
    src/crypto/nj_crypto.cpp
)
target_include_directories(njclient PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(njclient PUBLIC wdl vorbis vorbisenc ogg FLAC)
if(WIN32)
    target_link_libraries(njclient PUBLIC bcrypt)
else()
    target_link_libraries(njclient PUBLIC OpenSSL::Crypto)
endif()
if(JAMWIDE_DEV_BUILD)
    target_compile_definitions(njclient PRIVATE JAMWIDE_DEV_BUILD=1)
endif()
if(JAMWIDE_BUILD_TESTS)
    target_compile_definitions(njclient PRIVATE JAMWIDE_BUILD_TESTS=1)
endif()

# ── IXWebSocket (WebSocket server for video companion, per D-12) ──
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(USE_WS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/ixwebsocket EXCLUDE_FROM_ALL)

# ── JUCE Plugin Targets ──
if(JAMWIDE_BUILD_JUCE)
    add_subdirectory(libs/juce EXCLUDE_FROM_ALL)

    juce_add_plugin(JamWideJuce
        PRODUCT_NAME "JamWide"
        COMPANY_NAME "JamWide"
        BUNDLE_ID "${JAMWIDE_JUCE_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE ${JAMWIDE_JUCE_MANUFACTURER_CODE}
        PLUGIN_CODE ${JAMWIDE_JUCE_PLUGIN_CODE}
        FORMATS VST3 AU Standalone
        VST3_CATEGORIES Fx Network
        AU_MAIN_TYPE kAudioUnitType_Effect
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_SYNTH FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_AUTO_MANIFEST FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "JamWide needs microphone access for standalone mode"
        # Phase 19-01 Task 1: NSCameraUsageDescription plumbed into Info.plist by JUCE.
        CAMERA_PERMISSION_ENABLED TRUE
        CAMERA_PERMISSION_TEXT "JamWide uses your webcam to share video with NINJAM peers."
    )

    juce_generate_juce_header(JamWideJuce)

    target_sources(JamWideJuce PRIVATE
        juce/JamWideJuceProcessor.cpp
        juce/JamWideJuceEditor.cpp
        juce/NinjamRunThread.cpp
        juce/ui/JamWideLookAndFeel.cpp
        juce/ui/ConnectionBar.cpp
        juce/ui/ChatPanel.cpp
        juce/ui/VbFader.cpp
        juce/ui/VuMeter.cpp
        juce/ui/ChannelStrip.cpp
        juce/ui/BeatBar.cpp
        juce/ui/SessionInfoStrip.cpp
        juce/ui/ChannelStripArea.cpp
        juce/ui/ServerBrowserOverlay.cpp
        juce/ui/LicenseDialog.cpp
        juce/osc/OscAddressMap.cpp
        juce/osc/OscServer.cpp
        juce/osc/OscStatusDot.cpp
        juce/osc/OscConfigDialog.cpp
        juce/midi/MidiMapper.cpp
        juce/midi/MidiLearnManager.cpp
        juce/midi/MidiStatusDot.cpp
        juce/midi/MidiConfigDialog.cpp
        juce/video/VideoCompanion.cpp
        juce/video/VideoPrivacyDialog.cpp
        src/net/server_list.cpp
    )

    # BrowserDetect: macOS uses .mm, Windows/Linux uses .cpp
    if(APPLE)
        target_sources(JamWideJuce PRIVATE juce/video/BrowserDetect_mac.mm)
    else()
        target_sources(JamWideJuce PRIVATE juce/video/BrowserDetect_win.cpp)
    endif()

    # Phase 19-01 Task 1: native camera capture sources. All files exist as stubs
    # after this plan's Task 1 Step 1; subsequent tasks (and downstream plans
    # 19-02 / 19-03) fill them with real implementations. Stubs are kept in the
    # build so CMake configure + JamWideJuce build both succeed at end of every
    # task in 19-01 (HIGH-1 mitigation — no missing source files).
    target_sources(JamWideJuce PRIVATE
        juce/video/native/JamWideFrameDistributor.cpp
        juce/video/native/CameraStateMachine.cpp
        juce/video/native/JamWideCameraDevice.cpp
        juce/video/native/CameraStatusDialog.cpp
        juce/video/native/CameraPreviewWindow.cpp
        juce/video/native/CameraPreviewTile.cpp
        juce/video/native/NativeCameraPrivacyDialog.cpp
    )

    # Phase 19-01 Task 1: CameraAuthorization platform dispatch (mirrors BrowserDetect).
    # macOS .mm calls AVCaptureDevice authorizationStatusForMediaType; Windows .cpp
    # returns NotApplicable (no TCC analogue on desktop — cause detected via
    # openDeviceAsync + watchdog instead).
    if(APPLE)
        target_sources(JamWideJuce PRIVATE juce/video/native/CameraAuthorization_mac.mm)
    else()
        target_sources(JamWideJuce PRIVATE juce/video/native/CameraAuthorization_windows.cpp)
    endif()

    # Phase 20-01 Task 1: VideoEncoder interface headers (Wave-1 contracts).
    # Header-only at Task 1 — no library dependencies; Plan 20-02 will bind to
    # these surfaces.
    target_sources(JamWideJuce PRIVATE
        juce/video/encoder/VideoEncoder.h
        juce/video/encoder/VideoEncoderConfig.h
        juce/video/encoder/VideoEncoderListener.h
    )

    # Phase 20-01 Task 2: Openh264Encoder concrete implementation.
    # Links libavcodec / libswscale / libopenh264 via jamwide_use_ffmpeg().
    # The ffmpeg cmake include + ffmpeg::lgpl target are gated on the vendored
    # tree being present (libs/ffmpeg/<platform>/lib/); ffmpeg.cmake emits a
    # WARNING + empty INTERFACE if not present, so we include it once here.
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ffmpeg.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/jamwide_use_ffmpeg.cmake)
    target_sources(JamWideJuce PRIVATE
        juce/video/encoder/Openh264Encoder.h
        juce/video/encoder/Openh264Encoder.cpp
    )

    # Plan 21-02 Task 1: VideoDecoder interface + NalChunk POD + VideoRecvSlotSnapshot
    # POD (codex Cluster 2 Option A) + Openh264Decoder header (Wave-2 contracts).
    # Plan 21-02 Task 2: Openh264Decoder.cpp implementation. Links libavcodec /
    # libswscale via jamwide_use_ffmpeg() — same linkage as the Phase 20 encoder
    # block above (already invoked on JamWideJuce a few lines below).
    target_sources(JamWideJuce PRIVATE
        juce/video/decoder/VideoDecoder.h
        juce/video/decoder/NalChunk.h
        juce/video/decoder/VideoRecvSlotSnapshot.h
        juce/video/decoder/Openh264Decoder.h
        juce/video/decoder/Openh264Decoder.cpp
    )

    # Plan 21-03 Task 1: JamWideRemoteFrameDistributor + PeerVideoSink.
    # Symmetric inverse of Phase 19's JamWideFrameDistributor (camera-side).
    # PeerVideoSink subclasses juce::AsyncUpdater (juce_events) and owns
    # double-buffered juce::Image (juce_graphics) — both already linked
    # transitively below. No ffmpeg dependency in the distributor itself.
    target_sources(JamWideJuce PRIVATE
        juce/video/distributor/JamWideRemoteFrameDistributor.h
        juce/video/distributor/JamWideRemoteFrameDistributor.cpp
        juce/video/distributor/PeerVideoSink.h
        juce/video/distributor/PeerVideoSink.cpp
    )

    jamwide_use_ffmpeg(JamWideJuce)

    target_compile_definitions(JamWideJuce PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        # Phase 19-01 Task 1 (Risk E closure): expose juce::CameraDevice on the
        # JamWideJuce target (was previously scoped only to the video_spike test).
        JUCE_USE_CAMERA=1
    )

    target_include_directories(JamWideJuce PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/juce
    )

    add_dependencies(JamWideJuce jamwide-build-number)

    target_link_libraries(JamWideJuce
        PRIVATE
            njclient
            ixwebsocket
            juce::juce_audio_utils
            juce::juce_opengl
            juce::juce_gui_extra
            juce::juce_osc
            juce::juce_cryptography
            # Phase 19-01 Task 1: juce::CameraDevice + juce::ImageButton/Image
            # support. Already linked into the video_spike test target; promoted
            # here onto JamWideJuce now that the native capture pipeline is
            # being wired into the plugin (Risk E closure).
            juce::juce_video
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    # CLAP via clap-juce-extensions
    add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
    clap_juce_extensions_plugin(TARGET JamWideJuce
        CLAP_ID "${JAMWIDE_JUCE_BUNDLE_ID}"
        CLAP_FEATURES audio-effect utility mixing
    )

    # macOS code signing
    # - Dev builds: ad-hoc signing (default, no certificate needed)
    # - Release builds: Developer ID signing with hardened runtime
    #   cmake -DJAMWIDE_CODESIGN_IDENTITY="Developer ID Application: ..." -DJAMWIDE_HARDENED_RUNTIME=ON
    # - 15.1-04: TSan builds skip codesign entirely (NOT JAMWIDE_TSAN guard
    #   below) — TSan injects a runtime library that ad-hoc codesigning does
    #   not cover, leading to launch failure on macOS. See 15.1-RESEARCH
    #   macOS caveat #1.
    if(APPLE AND NOT JAMWIDE_TSAN)
        set(JAMWIDE_CODESIGN_IDENTITY "-" CACHE STRING "Code signing identity (- = ad-hoc)")
        option(JAMWIDE_HARDENED_RUNTIME "Enable hardened runtime (required for notarization)" OFF)

        set(_sign_flags --force --deep)
        if(JAMWIDE_HARDENED_RUNTIME)
            list(APPEND _sign_flags --options runtime --timestamp)
            if(EXISTS "${CMAKE_SOURCE_DIR}/JamWide.entitlements")
                list(APPEND _sign_flags --entitlements "${CMAKE_SOURCE_DIR}/JamWide.entitlements")
            endif()
        else()
            list(APPEND _sign_flags --timestamp=none)
        endif()

        foreach(_fmt Standalone VST3 AU CLAP)
            if(TARGET JamWideJuce_${_fmt})
                add_custom_command(TARGET JamWideJuce_${_fmt} POST_BUILD
                    COMMAND /usr/bin/codesign ${_sign_flags}
                            --sign "${JAMWIDE_CODESIGN_IDENTITY}"
                            "$<TARGET_BUNDLE_DIR:JamWideJuce_${_fmt}>"
                    COMMENT "Codesign JamWideJuce_${_fmt} (${JAMWIDE_CODESIGN_IDENTITY})"
                    VERBATIM
                )
            endif()
        endforeach()
    endif()
    # Pluginval validation: cmake --build build --target validate
    # Downloads pluginval automatically on first run, caches in build dir
    if(APPLE)
        set(_pluginval_url "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip")
        set(_pluginval_exe "${CMAKE_BINARY_DIR}/_pluginval/pluginval.app/Contents/MacOS/pluginval")
    elseif(WIN32)
        set(_pluginval_url "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip")
        set(_pluginval_exe "${CMAKE_BINARY_DIR}/_pluginval/pluginval.exe")
    endif()

    if(APPLE AND DEFINED _pluginval_url)
        add_custom_target(validate
            COMMAND ${CMAKE_COMMAND}
                -D "PLUGINVAL_URL=${_pluginval_url}"
                -D "PLUGINVAL_EXE=${_pluginval_exe}"
                -D "PLUGINVAL_DIR=${CMAKE_BINARY_DIR}/_pluginval"
                -D "VST3_PATH=$<TARGET_BUNDLE_DIR:JamWideJuce_VST3>"
                -P "${CMAKE_SOURCE_DIR}/cmake/run-pluginval.cmake"
            DEPENDS JamWideJuce_VST3
            COMMENT "Validating VST3 with pluginval (strictness 5)"
            VERBATIM
        )
    endif()

    # OSC loopback smoke test (inside JUCE section for module access)
    if(JAMWIDE_BUILD_TESTS)
        juce_add_console_app(test_osc_loopback PRODUCT_NAME "OSC Loopback Test")
        juce_generate_juce_header(test_osc_loopback)

        target_sources(test_osc_loopback PRIVATE tests/test_osc_loopback.cpp)
        target_include_directories(test_osc_loopback PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/juce
        )
        target_link_libraries(test_osc_loopback PRIVATE
            juce::juce_osc
            juce::juce_events
            juce::juce_core
        )
        target_compile_definitions(test_osc_loopback PRIVATE
            JUCE_STANDALONE_APPLICATION=1
        )
        add_test(NAME osc_loopback COMMAND test_osc_loopback)

        juce_add_console_app(test_midi_mapping PRODUCT_NAME "MIDI Mapping Test")
        juce_generate_juce_header(test_midi_mapping)
        target_sources(test_midi_mapping PRIVATE tests/test_midi_mapping.cpp)
        target_include_directories(test_midi_mapping PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/juce
        )
        target_link_libraries(test_midi_mapping PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_basics
            juce::juce_events
            juce::juce_core
        )
        target_compile_definitions(test_midi_mapping PRIVATE
            JUCE_STANDALONE_APPLICATION=1
        )
        add_test(NAME midi_mapping COMMAND test_midi_mapping)

        # 260515-0pc Quick task feasibility spike: capture→encode→decode→PNG-dump
        # roundtrip against the vendored LGPL ffmpeg + Cisco openh264 + JUCE
        # CameraDevice. Standalone executable; does NOT link into JamWideJuce
        # (the spike is non-invasive to the shipping product per RESEARCH § 8
        # Option (a)). Gated by JAMWIDE_VIDEO_SPIKE option AND JAMWIDE_BUILD_TESTS;
        # default OFF so headless CI without a camera is unaffected.
        if(JAMWIDE_VIDEO_SPIKE)
            include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ffmpeg.cmake)
            include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/jamwide_use_ffmpeg.cmake)

            juce_add_console_app(video_spike PRODUCT_NAME "JamWide Video Spike")
            juce_generate_juce_header(video_spike)

            target_sources(video_spike PRIVATE tests/video_spike.cpp)
            # Phase 14.3-01: jamwide_use_ffmpeg() bakes in the Apple-clang
            # BEFORE PRIVATE include discipline (Landmine L6 / spike Risk #1)
            # AND links ffmpeg::lgpl in one shot. See cmake/jamwide_use_ffmpeg.cmake
            # for the full rationale.
            jamwide_use_ffmpeg(video_spike)
            target_include_directories(video_spike PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${CMAKE_CURRENT_SOURCE_DIR}/juce
            )
            target_link_libraries(video_spike PRIVATE
                juce::juce_video
                juce::juce_graphics
                juce::juce_events
                juce::juce_core
            )
            target_compile_definitions(video_spike PUBLIC
                JUCE_USE_CAMERA=1
                JUCE_STANDALONE_APPLICATION=1
            )
            add_test(NAME video_spike COMMAND video_spike)

            # Phase 14.3-01 Task 3: carved-out codec-init smoke test
            # (SC-1 / SC-3 / SC-8). Mirrors video_spike's juce_add_console_app
            # scaffolding minus the JUCE camera/graphics/encode/decode/PNG-dump
            # modules — links ffmpeg::lgpl only via jamwide_use_ffmpeg().
            juce_add_console_app(test_ffmpeg_link PRODUCT_NAME "FFmpeg Link Smoke Test")
            juce_generate_juce_header(test_ffmpeg_link)
            target_sources(test_ffmpeg_link PRIVATE tests/test_ffmpeg_link.cpp)
            jamwide_use_ffmpeg(test_ffmpeg_link)
            target_compile_definitions(test_ffmpeg_link PRIVATE JUCE_STANDALONE_APPLICATION=1)
            add_test(NAME ffmpeg_link_smoke COMMAND test_ffmpeg_link)

            # Phase 14.3-01 Task 3: replace the original spike's inline
            # strings-grep POST_BUILD step with a call to the standalone
            # scripts/verify_ffmpeg_lgpl.sh gate (single source of truth — the
            # same script runs in .github/workflows/juce-build.yml). Covers
            # both the libx264 strings discipline AND the otool linkage
            # assertion that clears spike Risk #4 (libX11 spurious dep).
            add_custom_command(TARGET video_spike POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "Verifying LGPL discipline (scripts/verify_ffmpeg_lgpl.sh)..."
                COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_ffmpeg_lgpl.sh
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Phase 14.3-01: verify_ffmpeg_lgpl.sh"
                VERBATIM
            )
        endif()
    endif()

endif() # JAMWIDE_BUILD_JUCE

# Tests
if(JAMWIDE_BUILD_TESTS)
    enable_testing()
    add_executable(test_flac_codec tests/test_flac_codec.cpp)
    target_include_directories(test_flac_codec PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(test_flac_codec PRIVATE FLAC wdl)
    # Only expose interfaces from vorbisencdec.h (no Vorbis codec deps)
    target_compile_definitions(test_flac_codec PRIVATE WDL_VORBIS_INTERFACE_ONLY)
    add_test(NAME flac_codec COMMAND test_flac_codec)

    add_executable(test_encryption tests/test_encryption.cpp)
    target_include_directories(test_encryption PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(test_encryption PRIVATE njclient)
    add_test(NAME encryption COMMAND test_encryption)

    # Phase 14.3-02: RawData send-side public API + SPSC + drain + is_video_fourcc.
    # Pure-C++ in-process test linking against njclient static library; matches
    # the test_encryption wiring above. JAMWIDE_BUILD_TESTS is already defined
    # on the njclient target (line 137) so the test-only helpers
    # IsVideoFourcc / DrainRawDataSendQueueForTest / ChunkRawDataItem compile
    # into the library and link cleanly here.
    add_executable(test_rawdata_send tests/test_rawdata_send.cpp)
    target_include_directories(test_rawdata_send PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(test_rawdata_send PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_link_libraries(test_rawdata_send PRIVATE njclient)
    add_test(NAME rawdata_send_roundtrip COMMAND test_rawdata_send)

    # Plan 20-02 Task 3: send-side video state machine (on_new_interval video block,
    # whole-block m_video_cs serialization, 4-byte BE length-prefix wrapping in
    # QueueVideoFrame, cold-start option (b), interleave defense). Pure-C++
    # in-process test linking against njclient static library; uses the
    # JAMWIDE_BUILD_TESTS-gated NJClient::RunOneIntervalForTest helper.
    add_executable(test_video_state_machine tests/test_video_state_machine.cpp)
    target_include_directories(test_video_state_machine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(test_video_state_machine PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_link_libraries(test_video_state_machine PRIVATE njclient)
    add_test(NAME video_state_machine COMMAND test_video_state_machine)

    # Plan 21-01 Task 3: receive-side state machine + GUID-pair decision
    # tree test (WIRE-02). Eleven sub-tests covering the nine original
    # behavior scenarios plus two codex Cluster 10 malformed-length tests
    # (zero-length frame, oversize prefix clamps at 4 MB cap). Mirrors
    # the test_video_state_machine wiring; pure-C++ in-process test
    # linking against the njclient static library via the
    # JAMWIDE_BUILD_TESTS-gated thin-forwarder dispatchers
    # DispatchTestVideoRecvBegin/Write/End +
    # RunOnNewIntervalReceiveBlockForTest + GetVideoStreamForTest +
    # AddTestRemoteUserMirrorWithDs + ClearTestRemoteUserMirror +
    # DispatchTestUserLeaveForVideoReset (codex Cluster 5 — no
    # state-machine duplication, helpers are the SAME ones production
    # dispatchers and the audio-thread on_new_interval call).
    add_executable(test_video_recv_state tests/test_video_recv_state.cpp)
    target_include_directories(test_video_recv_state PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(test_video_recv_state PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_link_libraries(test_video_recv_state PRIVATE njclient)
    add_test(NAME video_recv_state COMMAND test_video_recv_state)

    # Plan 20-02 Task 2: per-channel atomic seqlock on Local_Channel for the
    # audio_ch0_guid read (R3 MF1 + R4 H8 — TSan-clean by design via atomic
    # two-uint64_t halves). Stress writer/reader concurrency, retry-cap fallback,
    # and single-thread sanity.
    add_executable(test_curwritefile_guid_seqlock tests/test_curwritefile_guid_seqlock.cpp)
    target_include_directories(test_curwritefile_guid_seqlock PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(test_curwritefile_guid_seqlock PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_link_libraries(test_curwritefile_guid_seqlock PRIVATE njclient)
    add_test(NAME curwritefile_guid_seqlock COMMAND test_curwritefile_guid_seqlock)

    # Phase 14.3-03: RawData receive-path dispatch unit tests. Pure-C++ in-process
    # test linking against njclient static library; mirrors the test_rawdata_send
    # wiring above. JAMWIDE_BUILD_TESTS=1 on the test target exposes the
    # NJClient::DispatchTestServerDownloadIntervalBegin /
    # DispatchTestServerDownloadIntervalWrite / AddTestRemoteUser /
    # GetRawDataDownloadCount / GetMDownloadsCount helpers declared under the
    # same gate in njclient.h. NOT under JAMWIDE_VIDEO_SPIKE (Landmine L8 —
    # pure-C++ in-process test, no ffmpeg/JUCE-video dependency).
    add_executable(test_video_fourcc tests/test_video_fourcc.cpp)
    target_include_directories(test_video_fourcc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(test_video_fourcc PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_link_libraries(test_video_fourcc PRIVATE njclient)
    add_test(NAME video_fourcc COMMAND test_video_fourcc)

    add_executable(test_video_sync tests/test_video_sync.cpp)
    target_include_directories(test_video_sync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME video_sync COMMAND test_video_sync)

    # 15.1-02 CR-03: TSan-targeted stress test for the release/acquire pattern in
    # NJClient::updateBPMinfo / AudioProc, and Codex L-10 edge-triggered-semantics
    # coverage. Pure-C++ (no NJClient link) — exercises the pattern in isolation.
    add_executable(test_njclient_atomics tests/test_njclient_atomics.cpp)
    target_include_directories(test_njclient_atomics PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME njclient_atomics COMMAND test_njclient_atomics)

    # 15.1-04 SPSC infrastructure: roundtrip + concurrent producer/consumer for
    # every payload variant (RemoteUserUpdate, LocalChannelUpdate, BlockRecord,
    # DecodeChunk, DecodeArmRequest, RemoteUser*/Local_Channel* deferred-free).
    # Pure-C++ (no NJClient link) — exercises the SPSC primitive + payload
    # contract in isolation. Designed to also run under -fsanitize=thread
    # (--tsan build, JAMWIDE_TSAN=ON).
    add_executable(test_spsc_state_updates tests/test_spsc_state_updates.cpp)
    target_include_directories(test_spsc_state_updates PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME spsc_state_updates COMMAND test_spsc_state_updates)

    # 15.1-05 deferred-delete: 256-burst push + drain stress + 50 Hz audio-rate
    # producer/consumer + overflow-counter mechanism (Codex M-8). Pure-C++ (no
    # NJClient link). Designed to also run cleanly under -fsanitize=thread
    # (--tsan build, JAMWIDE_TSAN=ON).
    add_executable(test_deferred_delete tests/test_deferred_delete.cpp)
    target_include_directories(test_deferred_delete PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME deferred_delete COMMAND test_deferred_delete)

    # 15.1-06 LocalChannelMirror: concurrent mutation/apply + HIGH-2 (no
    # lc_ptr) + HIGH-3 (generation-gate deferred-free) coverage. Pure-C++
    # (no NJClient link). Designed to run cleanly under -fsanitize=thread.
    add_executable(test_local_channel_mirror tests/test_local_channel_mirror.cpp)
    target_include_directories(test_local_channel_mirror PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME local_channel_mirror COMMAND test_local_channel_mirror)

    # 15.1-07b BlockRecord SPSC: fill/drain integrity, overflow counter,
    # Codex M-7 bounds-check exercise. Pure-C++ (no NJClient link).
    # Designed to run cleanly under -fsanitize=thread.
    add_executable(test_block_queue_spsc tests/test_block_queue_spsc.cpp)
    target_include_directories(test_block_queue_spsc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME block_queue_spsc COMMAND test_block_queue_spsc)

    # 15.1-07a RemoteUserMirror: concurrent peer-churn + HIGH-2 (no user_ptr)
    # + HIGH-3 (generation-gate deferred-free for canonical RemoteUser)
    # coverage. Pure-C++ (no NJClient link). Designed to run cleanly under
    # -fsanitize=thread.
    add_executable(test_remote_user_mirror tests/test_remote_user_mirror.cpp)
    target_include_directories(test_remote_user_mirror PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME remote_user_mirror COMMAND test_remote_user_mirror)

    # 15.1-07c DecodeMediaBuffer SPSC: DecodeChunk byte-stream roundtrip,
    # partial-read consumer buffer, concurrent writer/reader FIFO, atomic
    # refcnt UAF-safety, overflow drop policy, Size() legacy semantics
    # (CR-12). Pure-C++ (no NJClient link). Designed to run cleanly under
    # -fsanitize=thread.
    add_executable(test_decode_media_buffer_spsc tests/test_decode_media_buffer_spsc.cpp)
    target_include_directories(test_decode_media_buffer_spsc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME decode_media_buffer_spsc COMMAND test_decode_media_buffer_spsc)

    # 15.1-09 DecodeState arming + Codex HIGH-1 audit-invariant: arm-request
    # round-trip, capacity bound, concurrent producer/consumer, decode_fp ==
    # nullptr post-publish (HIGH-1 audit invariant), refill-loop byte-stream
    # integrity, dead-entry reaper (CR-08, H-04). Pure-C++ (no NJClient link).
    # Designed to run cleanly under -fsanitize=thread.
    add_executable(test_decode_state_arming tests/test_decode_state_arming.cpp)
    target_include_directories(test_decode_state_arming PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME decode_state_arming COMMAND test_decode_state_arming)

    # 15.1-08 prealloc-hardening: lighter sanity per Codex per-plan delta.
    # Verifies VorbisDecoder + FlacDecoder construct cleanly post-Prealloc and
    # the Codex M-7 SetMaxAudioBlockSize bound enforcement (assertion body
    # reimplemented in-test via a stand-in lambda; no NJClient link). Strict
    # allocation-count verification deferred to 15.1-10 Instruments UAT.
    # Links the same codec libraries as njclient (vorbis vorbisenc ogg FLAC)
    # so the full VorbisDecoder body compiles (NOT WDL_VORBIS_INTERFACE_ONLY,
    # because we construct VorbisDecoder directly here).
    add_executable(test_decoder_prealloc tests/test_decoder_prealloc.cpp)
    target_include_directories(test_decoder_prealloc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(test_decoder_prealloc PRIVATE wdl vorbis vorbisenc ogg FLAC)
    add_test(NAME decoder_prealloc COMMAND test_decoder_prealloc)

    # 15.1-10 Signal 1b — Codex M-5 automated NINJAM-like peer-churn simulation.
    # Satisfies the AUTOMATED-coverage portion of ROADMAP-15.1-S1. Three
    # concurrent threads (audio + run + reaper) cycle 1000 peer-churn patterns
    # through the production SPSC primitives + payloads (from 15.1-04), with
    # acceptance gates on drop counters (== 0) and TSan reports (== 0).
    # Pure-C++ — no NJClient link; mirrors the test_remote_user_mirror.cpp /
    # test_local_channel_mirror.cpp / test_decode_state_arming.cpp pattern.
    add_executable(test_peer_churn_simulation tests/test_peer_churn_simulation.cpp)
    target_include_directories(test_peer_churn_simulation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME peer_churn_simulation COMMAND test_peer_churn_simulation)

    # ───────────────────────────────────────────────────────────────────────
    # Phase 19-01 Task 1: native camera capture pipeline tests.
    # Pure-C++ executables — MEDIUM-5 mitigation: NO test links against the
    # JamWideJuce plugin target. Each test compiles whatever subset of
    # juce/video/native/*.cpp it needs as compile-time inputs.
    #
    # Test stubs (int main(){return 0;}) are created in Task 1 Step 1; bodies
    # are filled by later tasks / downstream plans:
    #   - test_frame_distributor / test_frame_distributor_lifetime → Task 3
    #   - test_camera_state_machine                                → Task 4
    #   - test_camera_retry_backoff / test_camera_frame_stall      → Task 5
    #   - test_plugin_state_v3_v4                                  → 19-02 Task 3
    #   - test_camera_cause_mapping                                → 19-03 Task 1
    # ───────────────────────────────────────────────────────────────────────
    add_executable(test_frame_distributor
        tests/test_frame_distributor.cpp
        juce/video/native/JamWideFrameDistributor.cpp)
    target_compile_definitions(test_frame_distributor PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_frame_distributor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_frame_distributor PRIVATE juce::juce_core juce::juce_graphics)
    add_test(NAME camera_frame_distributor COMMAND test_frame_distributor)

    add_executable(test_frame_distributor_lifetime
        tests/test_frame_distributor_lifetime.cpp
        juce/video/native/JamWideFrameDistributor.cpp)
    target_compile_definitions(test_frame_distributor_lifetime PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_frame_distributor_lifetime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_frame_distributor_lifetime PRIVATE juce::juce_core juce::juce_graphics)
    add_test(NAME camera_frame_distributor_lifetime COMMAND test_frame_distributor_lifetime)

    add_executable(test_camera_state_machine
        tests/test_camera_state_machine.cpp
        juce/video/native/CameraStateMachine.cpp)
    target_compile_definitions(test_camera_state_machine PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_camera_state_machine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_camera_state_machine PRIVATE juce::juce_core)
    add_test(NAME camera_state_machine COMMAND test_camera_state_machine)

    add_executable(test_camera_retry_backoff
        tests/test_camera_retry_backoff.cpp
        juce/video/native/CameraStateMachine.cpp)
    target_compile_definitions(test_camera_retry_backoff PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_camera_retry_backoff PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_camera_retry_backoff PRIVATE juce::juce_core)
    add_test(NAME camera_retry_backoff COMMAND test_camera_retry_backoff)

    add_executable(test_camera_frame_stall
        tests/test_camera_frame_stall.cpp
        juce/video/native/CameraStateMachine.cpp)
    target_compile_definitions(test_camera_frame_stall PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_camera_frame_stall PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_camera_frame_stall PRIVATE juce::juce_core)
    add_test(NAME camera_frame_stall COMMAND test_camera_frame_stall)

    add_executable(test_camera_cause_mapping
        tests/test_camera_cause_mapping.cpp
        juce/video/native/CameraStatusDialog.cpp)
    target_compile_definitions(test_camera_cause_mapping PRIVATE JAMWIDE_BUILD_TESTS=1)
    # /juce on the include path so the test can reach video/native/CameraStatusDialog.h
    # (matches the test_plugin_state_v3_v4 include layout from 19-02 Task 3).
    target_include_directories(test_camera_cause_mapping PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/juce)
    target_link_libraries(test_camera_cause_mapping PRIVATE juce::juce_core juce::juce_gui_basics)
    add_test(NAME camera_cause_mapping COMMAND test_camera_cause_mapping)

    # 19-02 Task 3: test_plugin_state_v3_v4 must link juce_gui_basics because
    # NativeCameraPrivacyDialog.h pulls in MessageBoxOptions for the showAsync
    # path. The dialog's isAckResult helper is a pure constexpr-style noexcept
    # function with no dialog instantiation in the test, so linking
    # juce_gui_basics here doesn't add any UI dependency at runtime.
    add_executable(test_plugin_state_v3_v4
        tests/test_plugin_state_v3_v4.cpp
        juce/video/native/NativeCameraPrivacyDialog.cpp)
    target_compile_definitions(test_plugin_state_v3_v4 PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_plugin_state_v3_v4 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/juce)
    target_link_libraries(test_plugin_state_v3_v4 PRIVATE
        juce::juce_core
        juce::juce_data_structures
        juce::juce_gui_basics)
    add_test(NAME plugin_state_v3_v4 COMMAND test_plugin_state_v3_v4)

    # Plan 21-02 Task 3: H.264 decoder COD-03 unit test (8 sub-tests).
    # Mirrors test_video_encoder; links libavcodec + libswscale via
    # jamwide_use_ffmpeg. Pure-C++ in-process; links the decoder .cpp
    # plus njclient. WORKING_DIRECTORY at source root so the fixture
    # files (tests/fixtures/*.bin) resolve correctly via relative paths
    # in the test source.
    add_executable(test_video_decoder
        tests/test_video_decoder.cpp
        juce/video/decoder/Openh264Decoder.cpp)
    target_compile_definitions(test_video_decoder PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_video_decoder PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/juce)
    target_link_libraries(test_video_decoder PRIVATE
        njclient
        juce::juce_core
        juce::juce_events
        juce::juce_graphics)
    if(APPLE)
        # Test_processor_video_lifecycle pattern: bump main-thread stack to
        # 16 MB so the macOS-default 8 MB doesn't bite during static init
        # of juce_graphics / juce_events / libavcodec global state. The
        # DecoderBundle (16 MB per snapshot ring) is heap-allocated via
        # unique_ptr inside each sub-test so it does NOT live on the stack,
        # but JUCE init still touches enough to overflow the default 8 MB
        # on x86_64 macOS.
        target_link_options(test_video_decoder PRIVATE
            "LINKER:-stack_size,0x1000000")  # 16 MB main-thread stack
    endif()
    jamwide_use_ffmpeg(test_video_decoder)
    add_test(NAME video_decoder COMMAND test_video_decoder
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Plan 21-02 Task 3 (EXCLUDE_FROM_ALL): fixture-binary generator.
    # Not built by default; run manually via:
    #   ./scripts/build.sh gen_baseline_fixtures
    #   ./build-juce/gen_baseline_fixtures
    # Regenerates tests/fixtures/*.bin using the vendored libopenh264
    # encoder. SHA-256 hashes recorded in tests/fixtures/README.md.
    add_executable(gen_baseline_fixtures EXCLUDE_FROM_ALL
                   tests/fixtures/gen-baseline-fixtures.cpp)
    target_include_directories(gen_baseline_fixtures PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    jamwide_use_ffmpeg(gen_baseline_fixtures)

    # Phase 20-01 Task 2: Openh264Encoder bring-up + IDR-sync + drop-oldest +
    # reconfigure (no subscription churn, R4 H9) + 7-step teardown ordering
    # (R4 H9). Pure-C++ test per MEDIUM-5 discipline — does NOT link against
    # JamWideJuce / NJClient. Links encoder sources + frame distributor +
    # JUCE graphics directly. ffmpeg::lgpl via jamwide_use_ffmpeg().
    add_executable(test_video_encoder
        tests/test_video_encoder.cpp
        juce/video/encoder/Openh264Encoder.cpp
        juce/video/native/JamWideFrameDistributor.cpp)
    target_compile_definitions(test_video_encoder PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_video_encoder PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_video_encoder PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_graphics)
    jamwide_use_ffmpeg(test_video_encoder)
    add_test(NAME video_encoder COMMAND test_video_encoder)

    # Phase 20-03 Task 2: encoder + NJClient lifecycle wiring validation
    # without standing up a JUCE plugin host. Exercises the same
    # T-20-03 ordering (SetVideoBroadcastActive(false) BEFORE
    # encoder.close()), R4 M11 path 2 (Disconnect emits END), and R4 M11
    # path 3 (destructor cleanup) that JamWideJuceProcessor::setBroadcastVideo
    # enforces in production. Links Openh264Encoder + JamWideFrameDistributor
    # + njclient + JUCE + ffmpeg.
    #
    # The combined link brings in a large amount of static initializer state
    # (njclient + juce_core + juce_events + juce_graphics + libavcodec /
    # libswscale / libopenh264). macOS' default 8 MB main-thread stack is
    # tight when those initializers run; ctest happens to launch with
    # enough headroom but bare shell invocations can hit ___chkstk_darwin
    # at startup. Bump the stack-size to 16 MB at link time so the test
    # runs identically under ctest and direct invocation.
    add_executable(test_processor_video_lifecycle
        tests/test_processor_video_lifecycle.cpp
        juce/video/encoder/Openh264Encoder.cpp
        juce/video/native/JamWideFrameDistributor.cpp)
    target_compile_definitions(test_processor_video_lifecycle PRIVATE JAMWIDE_BUILD_TESTS=1)
    target_include_directories(test_processor_video_lifecycle PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/juce)
    target_link_libraries(test_processor_video_lifecycle PRIVATE
        njclient
        juce::juce_core
        juce::juce_events
        juce::juce_graphics)
    if(APPLE)
        target_link_options(test_processor_video_lifecycle PRIVATE
            "LINKER:-stack_size,0x1000000")  # 16 MB main-thread stack
    endif()
    jamwide_use_ffmpeg(test_processor_video_lifecycle)
    add_test(NAME processor_video_lifecycle COMMAND test_processor_video_lifecycle)

    # Phase 20-03 Task 3: convenience target for the 5-minute populated-server
    # UAT. The script is the orchestrating harness; the actual broadcast is
    # human-driven (see tests/uat/phase-20-broadcast-uat-procedure.md). The
    # target is named `phase20-uat` so it stays grep-discoverable.
    add_custom_target(phase20-uat
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/uat/phase-20-broadcast-uat.sh" --full
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
        USES_TERMINAL
        COMMENT "Phase 20 broadcast UAT — opens the human-runnable procedure")

endif()
