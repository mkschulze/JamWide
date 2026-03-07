# Technology Stack

**Project:** JamWide JUCE Migration
**Researched:** 2026-03-07
**Overall Confidence:** MEDIUM-HIGH

## Recommended Stack

### Core Framework

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| JUCE | 8.0.12 | Plugin framework, UI, audio, standalone | Industry standard for cross-platform audio plugins. Replaces Dear ImGui (UI), clap-wrapper (plugin formats), and custom platform code. Single framework covers VST3/AU/Standalone/CLAP(v9). Latest stable release Dec 2025. | HIGH |
| C++ | 20 | Language standard | Already in use. JUCE 8 requires C++17 minimum; C++20 gives us std::atomic improvements, concepts, ranges -- all useful for lock-free audio code. | HIGH |
| CMake | 3.22+ | Build system | JUCE 8 uses CMake natively via `juce_add_plugin()`. Already our build system. Bump minimum from 3.20 to 3.22 for better JUCE compatibility. | HIGH |

### Audio Codecs

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| libFLAC (standalone) | 1.5.0 | Lossless audio encoding/decoding for NINJAM | Use standalone libFLAC 1.5.0 (Feb 2025), NOT JUCE's bundled FlacAudioFormat. Reason: NINJAM needs raw `FLAC__stream_encoder_init_stream()` / `FLAC__stream_decoder_init_stream()` with memory callbacks for real-time interval-based encoding. JUCE's FlacAudioFormat wraps FLAC as a file-oriented AudioFormatReader/Writer -- wrong abstraction for streaming NINJAM intervals. 1.5.0 adds multithreaded encoding and bumps libFLAC ABI to v14. | HIGH |
| libogg | 1.3.5+ | OGG container (existing) | Retained for Vorbis backward compatibility. Already a git submodule. | HIGH |
| libvorbis | 1.3.7+ | OGG Vorbis codec (existing) | Retained for backward compatibility with Vorbis-only NINJAM clients. Already a git submodule. | HIGH |

**Critical decision: Why standalone libFLAC, not JUCE's bundled FLAC**

JUCE bundles FLAC 1.4.3 inside `juce_audio_formats` behind the `JUCE_USE_FLAC` flag. This provides `FlacAudioFormat` -- a file-oriented reader/writer using JUCE's `InputStream`/`OutputStream` abstraction. This is designed for reading/writing `.flac` files, not for real-time stream encoding into memory buffers with callbacks.

NINJAM's codec interface (`I_NJEncoder`/`I_NJDecoder`) requires:
- `FLAC__stream_encoder_init_stream()` with write callbacks to `WDL_Queue` buffers
- `FLAC__stream_decoder_init_stream()` with read callbacks from network-received byte buffers
- Frame-level control (`process_single()`) for low-latency decoding
- `reinit()` at interval boundaries (each NINJAM interval = complete FLAC stream)

None of this maps to JUCE's `AudioFormatReader`/`AudioFormatWriter` pattern. The existing `FLAC_INTEGRATION_PLAN.md` correctly specifies standalone libFLAC with stream callbacks. Use libFLAC 1.5.0 as a git submodule via `add_subdirectory()`.

**JUCE_USE_FLAC can still be enabled** for any future file export features (recording sessions to disk), but the NINJAM codec pipeline uses standalone libFLAC directly.

### Networking

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| WDL jnetlib | (bundled) | TCP networking for NINJAM protocol | Retain initially. jnetlib is deeply integrated into NJClient's Run() loop and message parsing. Replacing with JUCE `StreamingSocket` is possible but would require rewriting NJClient's polling model. Defer to a later phase. | MEDIUM |
| JUCE StreamingSocket | (JUCE 8) | Future networking replacement | JUCE provides `juce::StreamingSocket` (TCP) and `juce::DatagramSocket` (UDP) in `juce_core`. Plan to migrate from WDL jnetlib in a future phase after core JUCE migration stabilizes. Not urgent -- jnetlib works, and NJClient is a carry-over module. | MEDIUM |
| JUCE URL/WebInputStream | (JUCE 8) | HTTP for server list | Replace WDL's HTTP GET (jnetlib httpget) with JUCE's `URL::createInputStream()` for fetching the server list. This is a simpler migration than the TCP socket and removes the blocking HTTP fetch problem. | MEDIUM |

### UI Framework

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| JUCE GUI (juce_gui_basics, juce_gui_extra) | 8.0.12 | Retained-mode UI | Replaces Dear ImGui. JUCE uses retained-mode `Component` hierarchy with `paint()`/`resized()` overrides. This is a full rewrite of the UI layer -- no incremental port from ImGui. JUCE handles DPI scaling, keyboard focus, accessibility natively. Eliminates all platform-specific GUI code (gui_macos.mm, gui_win32.cpp). | HIGH |
| JUCE LookAndFeel | 8.0.12 | Visual theming | Use custom `LookAndFeel_V4` subclass for JamWide's visual identity. Centralizes all rendering customization. | HIGH |

### Plugin Format Support

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| JUCE AudioProcessor | 8.0.12 | Plugin format abstraction | Replaces CLAP entry point + clap-wrapper. Single `AudioProcessor` subclass generates VST3, AU, AUv3, Standalone, and (with JUCE 9) CLAP. `processBlock()` replaces current `plugin_process()`. | HIGH |
| JUCE Standalone | 8.0.12 | Standalone application | Free byproduct of `juce_add_plugin(FORMATS Standalone VST3 AU)`. JUCE wraps the AudioProcessor in a standalone app with audio device selection dialog. No extra code needed. | HIGH |

### DAW Sync

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| JUCE AudioPlayHead | 8.0.12 | Read host transport state | `getPlayHead()->getPosition()` in `processBlock()` provides BPM, time signature, isPlaying, position in samples/beats/seconds. Replaces the manual transport reading from ReaNINJAM's `AudioProc`. Standard JUCE pattern -- works in all DAWs. | HIGH |
| JUCE juce_osc | 8.0.12 | OSC for cross-DAW sync | Built-in `OSCSender`/`OSCReceiver` in JUCE's `juce_osc` module. OSC 1.0 compliant. Many DAWs (REAPER, Ableton, Bitwig) support OSC for transport/tempo control. Use for sending tempo/transport commands to the host DAW. | MEDIUM |

### State Management

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| JUCE AudioProcessorValueTreeState | 8.0.12 | Parameter management and state persistence | Standard JUCE pattern for exposable parameters (master volume, metro volume, mutes, codec selection). Handles `getStateInformation()`/`setStateInformation()` for DAW project save/load. Replaces current atomic parameter cache. | HIGH |
| JUCE ValueTree | 8.0.12 | Non-parameter state (server settings, UI prefs) | Store connection history, preferred servers, UI layout in a separate ValueTree. Serialize to XML alongside APVTS state. | HIGH |

### Testing and Development

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| Catch2 | 3.7.x | Unit testing | Industry standard for C++ testing. Used by pamplejuce template. Tests for NJClient wrapper, codec roundtrips, bus layout validation. | HIGH |
| pluginval | latest | Plugin format validation | JUCE's official plugin validation tool. Tests VST3/AU/Standalone against format spec compliance. Run in CI. | HIGH |
| melatonin_inspector | latest | UI debugging (dev only) | Point-and-click component inspection during development. Invaluable for debugging JUCE UI layouts. Add as dev-only dependency. | MEDIUM |
| melatonin_audio_sparklines | latest | Audio buffer debugging (dev only) | ASCII visualization of AudioBlocks during development. Useful for verifying codec roundtrips and mixing. | LOW |

### Future/Research Dependencies

| Technology | Version | Purpose | When | Confidence |
|------------|---------|---------|------|------------|
| FFmpeg (libavcodec) | 7.x | Video encode/decode (research) | Only if video support approved after research phase. H.264 encoding/decoding for JamTaba-style video. Use foleys_video_engine or direct FFmpeg linking. LGPL license implications must be evaluated. | LOW |
| foleys_video_engine | latest | JUCE + FFmpeg integration | Wraps FFmpeg for JUCE. Provides video playback/encoding within JUCE apps. Only relevant if video feature approved. | LOW |
| MCP SDK | TBD | Cross-DAW sync via Model Context Protocol | Research path for cross-DAW transport/tempo control. No established JUCE integration exists. Would require custom IPC (likely local HTTP/WebSocket). | LOW |

## JUCE Module Selection

### Required Modules

```
juce_core                  - Threading, files, networking, containers
juce_events                - Message loop, timers, async callbacks
juce_data_structures       - ValueTree, UndoManager
juce_audio_basics          - AudioBuffer, AudioPlayHead, MIDI
juce_audio_processors      - AudioProcessor, plugin format wrappers
juce_audio_plugin_client   - Plugin entry points (VST3, AU, Standalone)
juce_audio_devices         - Audio I/O for standalone mode
juce_audio_formats         - AudioFormatManager (WAV, FLAC file export)
juce_gui_basics            - Component, LookAndFeel, Graphics
juce_gui_extra             - SystemTrayIconComponent (optional)
juce_osc                   - OSCSender, OSCReceiver
```

### Optional Modules (defer until needed)

```
juce_audio_utils           - AudioDeviceSelectorComponent (standalone)
juce_cryptography          - If SHA needs to move off WDL (not urgent)
juce_opengl                - Only if GPU-accelerated rendering needed
```

### Do NOT Use

```
juce_analytics             - No telemetry needed
juce_blocks_basics         - ROLI Blocks hardware, irrelevant
juce_box2d                 - Physics engine, irrelevant
juce_dsp                   - JUCE DSP module; NJClient handles all DSP
juce_product_unlocking     - No licensing/copy protection needed
juce_video                 - JUCE's built-in video is macOS/iOS only, not cross-platform
```

## CMake Configuration

### Target CMakeLists.txt Structure

```cmake
cmake_minimum_required(VERSION 3.22)
project(JamWide VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE as git submodule
add_subdirectory(libs/JUCE)

# Standalone libFLAC for NINJAM codec (NOT JUCE's bundled FLAC)
set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
set(WITH_OGG OFF CACHE BOOL "" FORCE)  # We use native FLAC, not OGG FLAC
add_subdirectory(libs/libflac EXCLUDE_FROM_ALL)

# libogg (retained for Vorbis)
add_subdirectory(libs/libogg EXCLUDE_FROM_ALL)

# libvorbis (retained for backward compat)
add_subdirectory(libs/libvorbis EXCLUDE_FROM_ALL)

# JamWide plugin
juce_add_plugin(JamWide
    COMPANY_NAME "JamWide"
    BUNDLE_ID "com.jamwide.client"
    PLUGIN_MANUFACTURER_CODE Jmwd
    PLUGIN_CODE Jwde
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "JamWide"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    # Multichannel: request up to 32 output channels
    # (actual bus layout configured in AudioProcessor constructor)
)

target_compile_definitions(JamWide PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1          # For file export, not NINJAM codec
    JUCE_USE_OGGVORBIS=0     # We link our own libogg/libvorbis
    JUCE_DISPLAY_SPLASH_SCREEN=0  # Requires commercial license
)

target_link_libraries(JamWide
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_osc
        # NJClient dependencies
        FLAC                  # Standalone libFLAC for NINJAM codec
        vorbis vorbisenc ogg  # Retained for Vorbis codec
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
```

### Multichannel Output Bus Configuration

```cpp
// In JamWideProcessor constructor:
JamWideProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        // Main stereo mix (always present)
        .withOutput("Main", juce::AudioChannelSet::stereo(), true)
        // Per-user stereo pairs (auxiliary outputs, disabled by default)
        .withOutput("User 1", juce::AudioChannelSet::stereo(), false)
        .withOutput("User 2", juce::AudioChannelSet::stereo(), false)
        .withOutput("User 3", juce::AudioChannelSet::stereo(), false)
        .withOutput("User 4", juce::AudioChannelSet::stereo(), false)
        .withOutput("User 5", juce::AudioChannelSet::stereo(), false)
        .withOutput("User 6", juce::AudioChannelSet::stereo(), false)
        .withOutput("User 7", juce::AudioChannelSet::stereo(), false)
        // Up to 16 stereo pairs = 32 channels
    )
{
}

bool isBusesLayoutSupported(const BusesLayout& layouts) const override
{
    // Main output must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    // Aux outputs must be stereo or disabled
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        auto set = layouts.outputBuses[i];
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}
```

## Alternatives Considered

| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| Framework | JUCE 8 | Qt (JamTaba uses Qt) | JUCE is purpose-built for audio plugins. Qt requires separate plugin wrappers (DPF, etc.). JamTaba uses Qt but its plugin support is limited compared to JUCE's native VST3/AU/Standalone. |
| Framework | JUCE 8 | iPlug2 | Smaller community, less mature plugin format support. No built-in OSC. Less suited for complex UIs. |
| Framework | JUCE 8 | Dear ImGui + clap-wrapper (current) | Already decided to migrate away. ImGui requires custom platform backends, clap-wrapper doesn't support Standalone, no JUCE-style bus layouts for multichannel. |
| FLAC | libFLAC 1.5.0 standalone | JUCE bundled FLAC (1.4.3) | JUCE's FlacAudioFormat is file-oriented (AudioFormatReader/Writer). NINJAM needs raw stream encoder/decoder with memory callbacks. Wrong abstraction layer. |
| FLAC | libFLAC 1.5.0 standalone | libFLAC 1.4.3 | 1.5.0 adds multithreaded encoding (future benefit), newer API, and is the current release. No reason to use older version. |
| Networking | WDL jnetlib (retain) | JUCE StreamingSocket | jnetlib is deeply integrated into NJClient's polling loop. Migrating risks breaking the protocol layer. Defer until NJClient refactoring phase. |
| Testing | Catch2 | GoogleTest | Either works. Catch2 is more popular in JUCE ecosystem (pamplejuce template uses it). Single-header convenience. |
| OSC | JUCE juce_osc | liblo | JUCE has built-in OSC. No reason to add external dependency for OSC 1.0. |
| Video | FFmpeg (research) | GStreamer | FFmpeg is what JamTaba uses for video. More widely used, better H.264 support, simpler API for encode/decode. |
| CI Template | Custom | pamplejuce | Pamplejuce is excellent for new projects. JamWide has existing CI and codebase structure. Reference pamplejuce patterns but don't adopt wholesale. |

## What NOT to Use

| Technology | Why Avoid |
|------------|-----------|
| JUCE Projucer | Deprecated project management tool. Use CMake directly. Projucer generates Xcode/VS projects but CMake is the modern standard. |
| JUCE_USE_OGGVORBIS | JUCE can bundle its own OGG Vorbis, but we already link our own libogg/libvorbis for NJClient compatibility. Enabling both causes symbol conflicts. Set to 0. |
| JUCE AudioFormatReader for NINJAM codec | Wrong abstraction. AudioFormatReader expects seekable streams. NINJAM intervals are streamed in real-time, decoded frame-by-frame. Use raw libFLAC C API. |
| clap-wrapper | Replaced by JUCE's native plugin format support. JUCE 8 supports VST3 and AU natively. CLAP comes in JUCE 9. |
| Dear ImGui | Replaced by JUCE Component system. ImGui's immediate-mode model doesn't integrate with JUCE's message thread architecture. |
| WDL mutexes | Replace with `std::mutex` / `std::lock_guard` (C++20) or JUCE's `CriticalSection`. WDL mutexes were already being replaced in current codebase. |
| juce_video module | Only supports macOS/iOS native video playback. Not cross-platform. If video is needed, use FFmpeg via foleys_video_engine. |
| JUCE 9 (for now) | No release date announced. CLAP support is the main draw. Use JUCE 8 for production; plan to upgrade to JUCE 9 when released for native CLAP output. |

## JUCE Version Strategy

**Use JUCE 8.0.12 now.** Pin as git submodule.

**JUCE 9 upgrade path:** JUCE 9 adds native CLAP plugin output -- currently the only JUCE 8 gap versus JamWide's current clap-wrapper approach. Since JUCE 8 already supports VST3 + AU + Standalone, CLAP output is a nice-to-have, not a blocker. When JUCE 9 ships (no announced date), add CLAP to the FORMATS list. AudioProcessor API is expected to remain backward-compatible.

**License:** JUCE 8 is dual-licensed AGPLv3 / Commercial. If JamWide is released as open source (AGPLv3-compatible), use the free AGPLv3 license. If closed-source distribution is needed, the Indie license is $40/user/month or $800 one-time. The `JUCE_DISPLAY_SPLASH_SCREEN=0` define requires a commercial license.

## Threading Model Migration

### Current (JamWide v1)
```
Audio Thread  -- atomics/SPSC -->  Run Thread  -- SPSC -->  UI Thread
(plugin_process)                   (NJClient::Run)          (ImGui render)
```

### Target (JamWide v2 / JUCE)
```
Audio Thread  -- atomics/FIFO -->  Network Thread  -- MessageManager -->  Message Thread
(processBlock)                     (NJClient::Run)                        (Component::paint)
```

**Key changes:**
- `processBlock()` replaces `plugin_process()` -- same real-time constraints, no locks
- JUCE `MessageManager` thread replaces ImGui render loop -- all UI updates via `Component::repaint()` + `AsyncUpdater`
- Network thread (NJClient::Run) stays as a `juce::Thread` subclass
- SPSC ring buffers replaced by `juce::AbstractFifo` or retained custom `SpscRing` (both work)
- Audio-to-UI communication via `AsyncUpdater::triggerAsyncUpdate()` or timer-based polling

## Installation / Submodule Setup

```bash
# Add JUCE as submodule (pinned to 8.0.12)
cd /Users/cell/dev/JamWide
git submodule add -b master https://github.com/juce-framework/JUCE.git libs/JUCE
cd libs/JUCE && git checkout 8.0.12 && cd ../..

# Add libFLAC as submodule (pinned to 1.5.0)
git submodule add https://github.com/xiph/flac.git libs/libflac
cd libs/libflac && git checkout 1.5.0 && cd ../..

# Existing submodules retained:
# libs/libogg    - for Vorbis backward compat
# libs/libvorbis - for Vorbis backward compat

# Submodules to REMOVE after migration:
# libs/clap          - replaced by JUCE AudioProcessor
# libs/clap-helpers  - replaced by JUCE AudioProcessor
# libs/clap-wrapper  - replaced by JUCE plugin format support
# libs/imgui         - replaced by JUCE GUI
```

## Dependency Lifecycle

| Dependency | Status | Action |
|------------|--------|--------|
| JUCE 8.0.12 | New (add) | Git submodule, pin to tag |
| libFLAC 1.5.0 | New (add) | Git submodule, pin to tag |
| libogg 1.3.5+ | Existing (retain) | Keep for Vorbis compat |
| libvorbis 1.3.7+ | Existing (retain) | Keep for Vorbis compat |
| WDL jnetlib | Existing (retain, deprecate later) | Keep for NJClient networking; plan migration to JUCE sockets |
| WDL SHA/RNG | Existing (retain, deprecate later) | Keep for NJClient auth; could move to juce_cryptography |
| picojson | Existing (retain) | Keep for server list JSON; could move to juce::JSON |
| Dear ImGui | Existing (remove) | Remove after JUCE UI complete |
| CLAP SDK | Existing (remove) | Remove after JUCE migration |
| clap-helpers | Existing (remove) | Remove after JUCE migration |
| clap-wrapper | Existing (remove) | Remove after JUCE migration |

## Sources

- [JUCE GitHub Releases](https://github.com/juce-framework/JUCE/releases) -- JUCE 8.0.12 is latest (Dec 2025) -- HIGH confidence
- [JUCE Roadmap Q3 2025](https://juce.com/blog/juce-roadmap-update-q3-2025/) -- JUCE 9 CLAP support planned, no release date -- HIGH confidence
- [JUCE Roadmap Q1 2025](https://juce.com/blog/juce-roadmap-update-q1-2025/) -- JUCE 9 feature list -- HIGH confidence
- [JUCE CMake API](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md) -- `juce_add_plugin` reference -- HIGH confidence
- [JUCE Get JUCE (Pricing)](https://juce.com/get-juce/) -- License tiers verified -- HIGH confidence
- [JUCE AudioPlayHead docs](https://docs.juce.com/master/classAudioPlayHead.html) -- Transport sync API -- HIGH confidence
- [JUCE StreamingSocket docs](https://docs.juce.com/master/classStreamingSocket.html) -- TCP networking -- HIGH confidence
- [JUCE OSC tutorial](https://juce.com/tutorials/tutorial_osc_sender_receiver/) -- Built-in OSC support -- HIGH confidence
- [JUCE FlacAudioFormat docs](https://docs.juce.com/master/classFlacAudioFormat.html) -- File-oriented FLAC, not for streaming -- HIGH confidence
- [JUCE Bus Layout tutorial](https://juce.com/tutorials/tutorial_audio_bus_layouts/) -- Multichannel output configuration -- HIGH confidence
- [FLAC 1.5.0 Release](https://github.com/xiph/flac/releases/tag/1.5.0) -- Latest libFLAC, Feb 2025 -- HIGH confidence
- [Xiph FLAC Stream Encoder API](https://xiph.org/flac/api/group__flac__stream__encoder.html) -- Raw stream API for NINJAM codec -- HIGH confidence
- [Pamplejuce template](https://github.com/sudara/pamplejuce) -- JUCE 8 + Catch2 + CI reference -- HIGH confidence
- [Foleys Video Engine](https://github.com/ffAudio/foleys_video_engine) -- JUCE + FFmpeg integration for video -- MEDIUM confidence
- [JUCE Multiple Output Buses forum](https://forum.juce.com/t/multiple-output-busses/37673) -- Multichannel routing patterns -- MEDIUM confidence
- [JUCE Forum: Multichannel output plugins](https://forum.juce.com/t/multichannel-output-plugins/26825) -- Per-user routing approach -- MEDIUM confidence
- [Melatonin Inspector](https://github.com/sudara/melatonin_inspector) -- JUCE UI debugging tool -- MEDIUM confidence

---

*Stack research: 2026-03-07*
