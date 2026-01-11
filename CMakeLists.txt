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

# Options
option(JAMWIDE_BUILD_TESTS "Build tests" OFF)
option(JAMWIDE_DEV_BUILD "Enable development build with verbose logging" ON)

# Submodules
add_subdirectory(libs/clap EXCLUDE_FROM_ALL)
add_subdirectory(libs/clap-helpers EXCLUDE_FROM_ALL)
add_subdirectory(libs/clap-wrapper EXCLUDE_FROM_ALL)

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

# WDL library (static)
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

# NJClient core (static)
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
target_link_libraries(njclient PUBLIC wdl vorbis vorbisenc ogg)
if(JAMWIDE_DEV_BUILD)
    target_compile_definitions(njclient PRIVATE JAMWIDE_DEV_BUILD=1)
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
    target_sources(jamwide-impl PRIVATE src/platform/gui_macos.mm)
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
