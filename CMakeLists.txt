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
option(JAMWIDE_BUILD_CLAP "Build CLAP/ImGui plugin targets" ON)

# Plugin identity (single source of truth for JUCE targets)
set(JAMWIDE_JUCE_BUNDLE_ID "com.jamwide.juce-client")
set(JAMWIDE_JUCE_MANUFACTURER_CODE "JmWd")
set(JAMWIDE_JUCE_PLUGIN_CODE "JwJc")

# CLAP submodules (only when building CLAP/ImGui targets)
if(JAMWIDE_BUILD_CLAP)
    add_subdirectory(libs/clap EXCLUDE_FROM_ALL)
    add_subdirectory(libs/clap-helpers EXCLUDE_FROM_ALL)
    add_subdirectory(libs/clap-wrapper EXCLUDE_FROM_ALL)
endif()

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

# NJClient core (static) — shared by both CLAP and JUCE targets
add_library(njclient STATIC
    src/core/njclient.cpp
    src/core/netmsg.cpp
    src/core/mpb.cpp
    src/core/njmisc.cpp
)
target_include_directories(njclient PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(njclient PUBLIC wdl vorbis vorbisenc ogg FLAC)
if(JAMWIDE_DEV_BUILD)
    target_compile_definitions(njclient PRIVATE JAMWIDE_DEV_BUILD=1)
endif()

# ── CLAP/ImGui Plugin Targets ──
if(JAMWIDE_BUILD_CLAP)

# Dear ImGui (manual setup - no CMakeLists in repo)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})

# Platform-specific ImGui backends
if(WIN32)
    target_sources(imgui PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR}/backends)
    target_link_libraries(imgui PUBLIC
        d3d11
        dxgi
        d3dcompiler
    )
elseif(APPLE)
    target_sources(imgui PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_osx.mm
        ${IMGUI_DIR}/backends/imgui_impl_metal.mm
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR}/backends)
    target_link_libraries(imgui PUBLIC
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework GameController"
    )
endif()

# Threading library
add_library(jamwide-threading STATIC
    src/threading/run_thread.cpp
    src/net/server_list.cpp
)
target_include_directories(jamwide-threading PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(jamwide-threading PUBLIC clap)
if(JAMWIDE_DEV_BUILD)
    target_compile_definitions(jamwide-threading PRIVATE JAMWIDE_DEV_BUILD=1)
endif()

# UI library
add_library(jamwide-ui STATIC
    src/ui/ui_main.cpp
    src/ui/ui_status.cpp
    src/ui/ui_connection.cpp
    src/ui/ui_server_browser.cpp
    src/ui/ui_chat.cpp
    src/ui/ui_local.cpp
    src/ui/ui_latency_guide.cpp
    src/ui/ui_master.cpp
    src/ui/ui_remote.cpp
    src/ui/ui_meters.cpp
    src/ui/ui_util.cpp
)
target_include_directories(jamwide-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(jamwide-ui PUBLIC imgui clap)
if(JAMWIDE_DEV_BUILD)
    target_compile_definitions(jamwide-ui PRIVATE JAMWIDE_DEV_BUILD=1)
endif()

# Plugin implementation (static)
add_library(jamwide-impl STATIC
    src/plugin/clap_entry.cpp
)
target_include_directories(jamwide-impl PRIVATE
    src
    src/third_party
)

# Platform-specific GUI sources
if(APPLE)
    target_sources(jamwide-impl PRIVATE 
        src/platform/gui_macos.mm
    )
elseif(WIN32)
    target_sources(jamwide-impl PRIVATE src/platform/gui_win32.cpp)
endif()

target_link_libraries(jamwide-impl PUBLIC
    clap
    clap-helpers
    njclient
    jamwide-threading
    jamwide-ui
    imgui
)
if(JAMWIDE_DEV_BUILD)
    target_compile_definitions(jamwide-impl PRIVATE JAMWIDE_DEV_BUILD=1)
endif()

make_clapfirst_plugins(
    TARGET_NAME jamwide
    IMPL_TARGET jamwide-impl
    OUTPUT_NAME "JamWide"
    ENTRY_SOURCE "src/plugin/clap_entry_export.cpp"
    BUNDLE_IDENTIFIER "com.jamwide.client"
    BUNDLE_VERSION "1.0.0"
    PLUGIN_FORMATS CLAP VST3 AUV2
    AUV2_MANUFACTURER_NAME "JamWide"
    AUV2_MANUFACTURER_CODE "JMWD"
    AUV2_SUBTYPE_CODE "JWAU"
    AUV2_INSTRUMENT_TYPE "aufx"
)

if(APPLE)
    add_custom_command(TARGET jamwide_clap POST_BUILD
        COMMAND SetFile -a B "$<TARGET_BUNDLE_DIR:jamwide_clap>"
        COMMENT "Setting bundle bit on JamWide.clap"
    )
endif()

install(TARGETS jamwide_clap
    LIBRARY DESTINATION lib/clap
    BUNDLE DESTINATION lib/clap
)

endif() # JAMWIDE_BUILD_CLAP

# ── JUCE Plugin Targets ──
if(JAMWIDE_BUILD_JUCE)
    add_subdirectory(libs/juce EXCLUDE_FROM_ALL)

    juce_add_plugin(JamWideJuce
        PRODUCT_NAME "JamWide JUCE"
        COMPANY_NAME "JamWide"
        BUNDLE_ID "${JAMWIDE_JUCE_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE ${JAMWIDE_JUCE_MANUFACTURER_CODE}
        PLUGIN_CODE ${JAMWIDE_JUCE_PLUGIN_CODE}
        FORMATS VST3 AU Standalone
        VST3_CATEGORIES Fx Network
        AU_MAIN_TYPE kAudioUnitType_Effect
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
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
        juce/ui/VuMeter.cpp
        juce/ui/ChannelStrip.cpp
        src/net/server_list.cpp
    )

    target_compile_definitions(JamWideJuce PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(JamWideJuce
        PRIVATE
            njclient
            juce::juce_audio_utils
            juce::juce_opengl
            juce::juce_gui_extra
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

    # macOS dev codesigning (required for AU validation on recent macOS)
    if(APPLE)
        option(JAMWIDE_DEV_CODESIGN "Ad-hoc codesign plugin bundles after build" ON)
        if(JAMWIDE_DEV_CODESIGN)
            foreach(_fmt Standalone VST3 AU CLAP)
                if(TARGET JamWideJuce_${_fmt})
                    add_custom_command(TARGET JamWideJuce_${_fmt} POST_BUILD
                        COMMAND /usr/bin/codesign --force --sign "-"
                                --timestamp=none --deep "$<TARGET_BUNDLE_DIR:JamWideJuce_${_fmt}>"
                        COMMENT "Dev codesign JamWideJuce_${_fmt}"
                        VERBATIM
                    )
                endif()
            endforeach()
        endif()
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

    if(DEFINED _pluginval_url)
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
endif()
