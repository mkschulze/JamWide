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

    target_compile_definitions(JamWideJuce PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
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

endif()
