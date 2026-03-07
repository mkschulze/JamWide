# Phase 2: JUCE Scaffolding - Research

**Researched:** 2026-03-07
**Domain:** JUCE audio plugin framework, CMake build system, multi-bus architecture, CI/pluginval
**Confidence:** HIGH

## Summary

Phase 2 builds the JUCE plugin skeleton -- an AudioProcessor that compiles as VST3, AU, Standalone, and CLAP, passes pluginval validation, and establishes the thread architecture for NJClient integration. The plugin produces no audio yet (empty processBlock) and uses default JUCE look-and-feel for any UI.

JUCE 9 is NOT released. The latest stable release is JUCE 8.0.12 (December 16, 2025, tag `8.0.12`). The CONTEXT.md decision to "target JUCE 9" must be adjusted to JUCE 8.0.12. JUCE 9 was announced on the roadmap but has no release date. All research targets JUCE 8.0.12.

CLAP support requires clap-juce-extensions (free-audio/clap-juce-extensions) since JUCE 8 has no native CLAP authoring. This library is MIT-licensed and works with JUCE 8 despite officially documenting JUCE 6/7 support. JUCE 9 will add native CLAP, but it is not available yet.

**Primary recommendation:** Use JUCE 8.0.12 as a git submodule at `libs/juce` (tag `8.0.12`), with clap-juce-extensions as a submodule at `libs/clap-juce-extensions`. Build via `juce_add_plugin()` with FORMATS `VST3 AU Standalone`, plus `clap_juce_extensions_plugin()` for CLAP. Guard JUCE targets behind `JAMWIDE_BUILD_JUCE` CMake option. Add GitHub Actions CI with pluginval at strictness level 5.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- JUCE GPL license -- project is already GPLv2+, fully compatible, no splash screen
- Target JUCE 9 (latest release) -- **NOTE: JUCE 9 is not released; latest is 8.0.12. Research proceeds with 8.0.12.**
- Git submodule in libs/juce, consistent with existing dependency pattern (libogg, libvorbis, libflac)
- Use juce_add_plugin CMake API for format target generation
- Output formats: VST3, AU, standalone, and CLAP (via juce_clap_extensions)
- New plugin identity during transition: "JamWide JUCE" (temporary name to coexist with current CLAP plugin)
- Different plugin ID from current CLAP plugin so both can be loaded simultaneously
- 16+1 stereo output pairs (34 channels total): Bus 0 = main mix, Buses 1-16 = individual remote user/channel routing
- 4 stereo input buses (8 channels total): each maps to a separate NINJAM local channel
- Keep current CLAP/ImGui plugin buildable alongside JUCE version through Phase 5
- JUCE source lives in juce/ at project root (not inside src/) -- **NOTE: CONTEXT.md says "juce/ at project root" but also says "libs/juce" for submodule. Research uses libs/juce for submodule consistency, juce/ for JUCE source files.**
- Shared CMakeLists.txt with JAMWIDE_BUILD_JUCE / JAMWIDE_BUILD_CLAP options
- Both targets link the same njclient static library
- NinjamRunThread uses juce::Thread (JUCE-04 requirement)
- JUCE-idiomatic hybrid communication:
  - Run thread -> UI: juce::MessageManager::callAsync() / AsyncUpdater
  - UI -> Run thread: lock-free queue (SPSC or AbstractFifo)
  - Audio thread <-> anything: lock-free only (atomics, AbstractFifo)
  - Parameters: juce::AudioProcessorValueTreeState
- Include modules: juce_core, juce_events, juce_data_structures, juce_audio_basics, juce_audio_processors, juce_audio_formats, juce_audio_devices, juce_gui_basics, juce_gui_extra, juce_opengl
- Skip juce_dsp
- Custom vector/SVG UI planned (Phase 4+), Phase 2 uses default JUCE look
- GitHub Actions CI with pluginval from Phase 2, macOS and Windows
- Standalone: audio device selection via settings dialog/popup, remember settings via PropertiesFile

### Claude's Discretion
- Exact JUCE 8 tag/version to pin as submodule -- **Recommendation: 8.0.12**
- juce_clap_extensions integration approach (submodule vs. vendored) -- **Recommendation: git submodule at libs/clap-juce-extensions**
- Temporary plugin manufacturer code and bundle ID for "JamWide JUCE" -- **Recommendation: manufacturer "JmWd", plugin code "JwJc", bundle ID "com.jamwide.juce-client"**
- CMake option defaults (which targets build by default) -- **Recommendation: both JAMWIDE_BUILD_JUCE=ON and JAMWIDE_BUILD_CLAP=ON by default**
- OpenGLContext attachment strategy -- **Recommendation: attach to top-level editor component, detach in destructor; defer to Phase 4**
- PropertiesFile location and format for standalone settings -- **Recommendation: use ApplicationProperties with folderName "JamWide", filenameSuffix ".settings"**
- CI workflow structure -- **Recommendation: separate juce-build.yml workflow alongside existing build.yml**

### Deferred Ideas (OUT OF SCOPE)
- Custom LookAndFeel with SVG assets -- Phase 4 (Core UI Panels)
- CLAP format via native JUCE 9 support -- not possible yet (JUCE 9 unreleased)
- Old CLAP/ImGui code removal -- after Phase 5
- FLAC recording format support -- revisit when JUCE AudioFormatManager is available
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| JUCE-01 | Plugin builds as VST3 and AU using JUCE AudioProcessor | juce_add_plugin() with FORMATS VST3 AU Standalone; BusesProperties constructor for multi-bus; isBusesLayoutSupported() override |
| JUCE-02 | Standalone application mode works with audio device selection | Standalone format in juce_add_plugin(); StandalonePluginHolder handles AudioDeviceManager; PropertiesFile persists device selection |
| JUCE-04 | NJClient Run() thread operates via juce::Thread | Subclass juce::Thread, override run(), use threadShouldExit() for shutdown; startThread()/stopThread() lifecycle tied to plugin activate/deactivate |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | Audio plugin framework | Industry standard for VST3/AU/Standalone; GPL-compatible; active maintenance |
| clap-juce-extensions | latest main | CLAP format support for JUCE | Only way to build CLAP with JUCE 8; MIT licensed; used by Surge, Vital, etc. |
| pluginval | 1.x (latest release) | Plugin validation CI | Tracktion's official tool; tests crash safety, parameter fuzzing, state save/load |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| njclient (existing) | in-tree | NINJAM client core | Both CLAP and JUCE targets link this; no changes needed |
| wdl (existing) | in-tree | Networking, SHA, RNG | Transitive dependency via njclient |
| juce_audio_utils | 8.0.12 | Convenience module | Pulls in audio_processors, audio_devices, audio_formats, gui_basics |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| clap-juce-extensions | Wait for JUCE 9 native CLAP | JUCE 9 has no release date; clap-juce-extensions is production-proven now |
| JUCE 8.0.12 | JUCE develop branch | Stability risk; tagged release is safer for a scaffolding phase |
| PropertiesFile | Custom XML/JSON settings | PropertiesFile is JUCE-idiomatic, cross-platform, and automatic |

**Installation:**
```bash
# Add JUCE submodule
git submodule add https://github.com/juce-framework/JUCE.git libs/juce
cd libs/juce && git checkout 8.0.12 && cd ../..

# Add clap-juce-extensions submodule
git submodule add https://github.com/free-audio/clap-juce-extensions.git libs/clap-juce-extensions
cd libs/clap-juce-extensions && git submodule update --init --recursive && cd ../..
```

## Architecture Patterns

### Recommended Project Structure
```
juce/                           # JUCE plugin source files (NOT the framework)
  JamWideJuceProcessor.h        # AudioProcessor subclass
  JamWideJuceProcessor.cpp      # processBlock, bus layout, state
  JamWideJuceEditor.h           # AudioProcessorEditor subclass
  JamWideJuceEditor.cpp         # Minimal editor (placeholder for Phase 4)
  NinjamRunThread.h             # juce::Thread subclass for NJClient::Run()
  NinjamRunThread.cpp           # Thread implementation
libs/juce/                      # JUCE framework (git submodule @ 8.0.12)
libs/clap-juce-extensions/      # CLAP format support (git submodule)
.github/workflows/
  build.yml                     # Existing CLAP build workflow (unchanged)
  juce-build.yml                # New: JUCE build + pluginval CI
```

### Pattern 1: AudioProcessor with Multi-Bus Layout
**What:** Declare all input and output buses in the constructor via BusesProperties
**When to use:** Always -- bus layout must be declared at construction time
**Example:**
```cpp
// Source: JUCE official tutorial (tutorial_audio_bus_layouts)
JamWideJuceProcessor()
    : AudioProcessor(BusesProperties()
        // 4 stereo inputs (local NINJAM channels)
        .withInput("Local 1",  juce::AudioChannelSet::stereo(), true)
        .withInput("Local 2",  juce::AudioChannelSet::stereo(), false)
        .withInput("Local 3",  juce::AudioChannelSet::stereo(), false)
        .withInput("Local 4",  juce::AudioChannelSet::stereo(), false)
        // 17 stereo outputs (main mix + 16 routing slots)
        .withOutput("Main Mix",   juce::AudioChannelSet::stereo(), true)
        .withOutput("Remote 1",   juce::AudioChannelSet::stereo(), false)
        .withOutput("Remote 2",   juce::AudioChannelSet::stereo(), false)
        // ... up to Remote 16
        .withOutput("Remote 16",  juce::AudioChannelSet::stereo(), false))
{
}

bool isBusesLayoutSupported(const BusesLayout& layouts) const override
{
    // Main output must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Main input must be stereo
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // All other buses must be stereo or disabled
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& bus = layouts.outputBuses[i];
        if (!bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
            return false;
    }
    for (int i = 1; i < layouts.inputBuses.size(); ++i)
    {
        const auto& bus = layouts.inputBuses[i];
        if (!bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}
```

### Pattern 2: juce::Thread for NinjamRunThread
**What:** Subclass juce::Thread, implement run() with threadShouldExit() check
**When to use:** Replacing the current std::thread-based run_thread for the JUCE build
**Example:**
```cpp
// Source: JUCE Thread class reference (docs.juce.com)
class NinjamRunThread : public juce::Thread
{
public:
    NinjamRunThread(JamWideJuceProcessor& p)
        : juce::Thread("NinjamRun"), processor(p) {}

    ~NinjamRunThread() override
    {
        // CRITICAL: Must call stopThread before destruction
        stopThread(5000); // 5 second timeout
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            // Process commands from UI (lock-free queue)
            processCommands();

            // Run NJClient under mutex
            {
                const juce::ScopedLock sl(processor.getClientLock());
                if (auto* client = processor.getClient())
                {
                    while (!client->Run())
                    {
                        if (threadShouldExit()) return;
                    }
                }
            }

            // Adaptive sleep
            wait(isConnected() ? 20 : 50);
        }
    }

private:
    JamWideJuceProcessor& processor;
};
```

### Pattern 3: AudioProcessorValueTreeState for Parameters
**What:** APVTS manages parameter state, serialization, and thread-safe UI binding
**When to use:** For host-automatable parameters (master volume, mute, etc.)
**Example:**
```cpp
// Source: JUCE APVTS tutorial (juce.com/tutorials)
static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"masterVol", 1}, "Master Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"masterMute", 1}, "Master Mute", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"metroVol", 1}, "Metronome Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"metroMute", 1}, "Metronome Mute", false));

    return { params.begin(), params.end() };
}

// In processor constructor:
JamWideJuceProcessor()
    : AudioProcessor(BusesProperties()/* ... */),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}
```

### Pattern 4: CMake juce_add_plugin Integration
**What:** Use juce_add_plugin to generate format targets, then clap_juce_extensions_plugin for CLAP
**When to use:** The CMakeLists.txt configuration
**Example:**
```cmake
# In root CMakeLists.txt, guarded by JAMWIDE_BUILD_JUCE option:
option(JAMWIDE_BUILD_JUCE "Build JUCE plugin targets" ON)

if(JAMWIDE_BUILD_JUCE)
    # Add JUCE
    add_subdirectory(libs/juce EXCLUDE_FROM_ALL)

    juce_add_plugin(JamWideJuce
        PRODUCT_NAME "JamWide JUCE"
        COMPANY_NAME "JamWide"
        BUNDLE_ID "com.jamwide.juce-client"
        PLUGIN_MANUFACTURER_CODE JmWd
        PLUGIN_CODE JwJc
        FORMATS VST3 AU Standalone
        VST3_CATEGORIES Fx Network
        AU_MAIN_TYPE kAudioUnitType_Effect
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_SYNTH FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "JamWide needs microphone access for standalone mode"
    )

    juce_generate_juce_header(JamWideJuce)

    target_sources(JamWideJuce PRIVATE
        juce/JamWideJuceProcessor.cpp
        juce/JamWideJuceEditor.cpp
        juce/NinjamRunThread.cpp
    )

    target_compile_definitions(JamWideJuce PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0   # GPL license
    )

    target_link_libraries(JamWideJuce
        PRIVATE
            njclient              # Shared NJClient core
            juce::juce_audio_utils
            juce::juce_opengl
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    # CLAP via clap-juce-extensions
    add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
    clap_juce_extensions_plugin(TARGET JamWideJuce
        CLAP_ID "com.jamwide.juce-client"
        CLAP_FEATURES audio-effect utility mixing
    )
endif()
```

### Anti-Patterns to Avoid
- **Calling MessageManager from audio thread:** MessageManager::callAsync allocates memory. Never call from processBlock(). Use atomics or lock-free queues for audio->UI communication.
- **Forgetting stopThread() in destructor:** juce::Thread will assert if destroyed while running. Always call stopThread() with a timeout in the NinjamRunThread destructor.
- **Using JucePlugin_PreferredChannelConfigurations:** Deprecated approach. Use BusesProperties constructor and isBusesLayoutSupported() instead.
- **Attaching OpenGLContext in Phase 2:** OpenGL attachment has known issues with some hosts (scaling bugs in VST3). Defer to Phase 4 when custom UI is actually needed.
- **Mixing njclient static lib and JUCE includes:** The njclient target uses raw C++ (no JUCE). Keep it isolated. Only the JUCE processor/editor should use JUCE APIs. Link njclient as a dependency but do not add JUCE headers to njclient sources.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Plugin format wrappers | Custom VST3/AU host adapters | juce_add_plugin() | 1000s of host compatibility edge cases |
| Audio device management | Custom CoreAudio/WASAPI code | JUCE Standalone wrapper (StandalonePluginHolder) | Platform differences, sample rate negotiation, buffer management |
| Parameter serialization | Custom JSON state save/load | AudioProcessorValueTreeState | Thread-safe, undo support, XML round-trip, host integration |
| CLAP plugin format | Custom CLAP wrapper | clap_juce_extensions_plugin() | Protocol compliance, parameter mapping, UI embedding |
| Plugin validation | Manual DAW testing | pluginval | Automated crash detection, parameter fuzz, state round-trip |
| Standalone settings persistence | Custom config file | PropertiesFile / ApplicationProperties | Cross-platform paths, atomic saves, standard JUCE pattern |
| Thread management | std::thread with atomics | juce::Thread with threadShouldExit() | JUCE-idiomatic, built-in wait/notify, named threads, debug support |

**Key insight:** JUCE's value proposition is handling the ~80% of plugin boilerplate that every developer gets wrong in unique ways. Fighting the framework (using std::thread instead of juce::Thread, custom state instead of APVTS) creates maintenance burden and host compatibility issues.

## Common Pitfalls

### Pitfall 1: AU Multi-Bus Caching in Logic Pro
**What goes wrong:** Logic Pro caches AU bus configuration. If you change the number of buses, Logic won't see the change until the AU component version changes.
**Why it happens:** Apple's AudioComponentRegistrar caches bus counts per version number.
**How to avoid:** Increment the AU version number any time bus layout changes. Set bus count correctly from the start (17 output, 4 input) even though routing isn't wired until Phase 6.
**Warning signs:** Plugin shows only stereo in Logic despite isBusesLayoutSupported accepting multi-bus.

### Pitfall 2: OpenGLContext Scaling Bugs in VST3
**What goes wrong:** Attaching OpenGLContext to the plugin editor causes the entire UI to be scaled incorrectly in some VST3 hosts.
**Why it happens:** Host DPI scaling and OpenGL viewport calculations conflict.
**How to avoid:** Do NOT attach OpenGLContext in Phase 2. Defer to Phase 4 when custom rendering is actually needed. When implementing, consider attaching to a child component rather than the top-level editor.
**Warning signs:** UI appears at wrong size, doubled, or offset in specific hosts.

### Pitfall 3: MessageManager::callAsync Object Lifetime
**What goes wrong:** Component deleted before the async callback fires, causing use-after-free.
**Why it happens:** callAsync posts a lambda that captures a pointer. If the pointed-to object is deleted before the message thread processes it, the lambda crashes.
**How to avoid:** Use SafePointer<Component> in lambdas, or prefer AsyncUpdater (which cancels pending callbacks on destruction).
**Warning signs:** Intermittent crashes on plugin unload or editor close.

### Pitfall 4: juce::Thread Must Be Stopped Before Destruction
**What goes wrong:** Assertion failure or undefined behavior if juce::Thread is destroyed while running.
**Why it happens:** Unlike std::thread (which calls terminate), juce::Thread asserts in debug builds.
**How to avoid:** Always call stopThread(timeoutMs) in the destructor. Signal the thread to exit before calling stopThread. Use wait() instead of sleep in the run() method so signalThreadShouldExit() wakes the thread immediately.
**Warning signs:** Assertion on plugin unload. Plugin hangs on deactivate.

### Pitfall 5: JUCE + Existing CMake Conflicts
**What goes wrong:** JUCE's CMake functions modify global state (C++ standard, position-independent code, etc.) conflicting with existing targets.
**Why it happens:** add_subdirectory(libs/juce) may set global variables.
**How to avoid:** Use EXCLUDE_FROM_ALL when adding JUCE subdirectory. Set project-level C++ standard before adding JUCE. Guard JUCE targets with the JAMWIDE_BUILD_JUCE option so they don't affect CLAP build.
**Warning signs:** CLAP build breaks after adding JUCE. Compilation flags change unexpectedly.

### Pitfall 6: pluginval AU Failure in GitHub Actions
**What goes wrong:** pluginval fails for AU format on GitHub Actions macOS runners but passes locally.
**Why it happens:** AU plugins require registration with the system. GitHub Actions runners may not have the AU cache properly populated, or the component may not be found by auval.
**How to avoid:** Build with COPY_PLUGIN_AFTER_BUILD=TRUE so the AU component is installed to ~/Library/Audio/Plug-Ins/Components/. Run `killall -9 AudioComponentRegistrar` before pluginval to force re-scan. Use `pluginval --validate "/path/to/JamWide JUCE.component"` with explicit path.
**Warning signs:** "Could not create an instance of the plugin" error in CI only.

## Code Examples

### Empty processBlock (Phase 2 Scope)
```cpp
// Source: JUCE AudioProcessor documentation
void JamWideJuceProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Phase 2: produce silence on all outputs
    // Phase 3 will integrate NJClient::AudioProc here
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
}
```

### Minimal Editor (Phase 2 Placeholder)
```cpp
// Source: JUCE AudioProcessorEditor pattern
class JamWideJuceEditor : public juce::AudioProcessorEditor
{
public:
    explicit JamWideJuceEditor(JamWideJuceProcessor& p)
        : AudioProcessorEditor(p), processor(p)
    {
        setSize(800, 600);
        // Phase 2: just a label. Real UI in Phase 4.
        addAndMakeVisible(placeholder);
        placeholder.setText("JamWide JUCE - Coming Soon", juce::dontSendNotification);
        placeholder.setJustificationType(juce::Justification::centred);
        placeholder.setFont(juce::FontOptions(24.0f));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(getLookAndFeel().findColour(
            juce::ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        placeholder.setBounds(getLocalBounds());
    }

private:
    JamWideJuceProcessor& processor;
    juce::Label placeholder;
};
```

### getStateInformation / setStateInformation (APVTS)
```cpp
// Source: JUCE APVTS tutorial
void JamWideJuceProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JamWideJuceProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}
```

### pluginval CI Command
```bash
# macOS - validate VST3
pluginval --validate-in-process --strictness-level 5 \
    --validate "build/JamWideJuce_artefacts/Release/VST3/JamWide JUCE.vst3"

# macOS - validate AU
pluginval --validate-in-process --strictness-level 5 \
    --validate "build/JamWideJuce_artefacts/Release/AU/JamWide JUCE.component"

# macOS - validate Standalone (just checks it launches)
pluginval --validate-in-process --strictness-level 5 \
    --validate "build/JamWideJuce_artefacts/Release/Standalone/JamWide JUCE.app"

# Windows - validate VST3
pluginval.exe --validate-in-process --strictness-level 5 ^
    --validate "build\JamWideJuce_artefacts\Release\VST3\JamWide JUCE.vst3"
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Projucer project files | CMake with juce_add_plugin | JUCE 6 (2020) | Full CMake integration, no proprietary IDE |
| JucePlugin_PreferredChannelConfigurations | BusesProperties + isBusesLayoutSupported | JUCE 5+ | Flexible multi-bus support |
| Raw AudioProcessorParameter | AudioProcessorValueTreeState (APVTS) | JUCE 4.3+ | Thread-safe parameter management with undo |
| createPluginFilter() free function | juce_add_plugin auto-generates factory | JUCE 6+ | CMake handles plugin boilerplate |
| Custom CLAP wrapper (clap-wrapper) | clap-juce-extensions | 2022+ | Direct JUCE integration, no separate wrapper needed |
| Manual plugin validation | pluginval | 2018+ | Automated CI validation across formats |

**Deprecated/outdated:**
- Projucer: Still works but CMake is the recommended path for new projects
- JUCE_PLUGINHOST_VST: VST2 SDK is no longer distributed by Steinberg
- JucePlugin_PreferredChannelConfigurations macro: Use BusesProperties instead

## Open Questions

1. **clap-juce-extensions JUCE 8.0.12 compatibility**
   - What we know: README documents JUCE 6/7 support. Community reports it works with JUCE 8. Pamplejuce template uses it with JUCE 8.
   - What's unclear: Whether there are edge cases with JUCE 8.0.12 specifically.
   - Recommendation: Proceed with clap-juce-extensions + JUCE 8.0.12 (HIGH confidence it works based on pamplejuce and community usage). If issues arise, CLAP can be temporarily disabled without affecting VST3/AU/Standalone.

2. **JUCE source directory naming**
   - What we know: CONTEXT.md says "JUCE source lives in juce/ at project root" but also "Git submodule in libs/juce". These are two different directories.
   - What's unclear: Whether user means the JUCE framework submodule or the JamWide JUCE plugin source code.
   - Recommendation: Framework submodule at `libs/juce` (consistent with libs/ pattern). JamWide's JUCE plugin source code at `juce/` (project root), as CONTEXT.md specifies. This mirrors the existing pattern where the framework is in libs/ and project source is at the root level.

3. **Universal binary with JUCE**
   - What we know: Existing CLAP build uses CMAKE_OSX_ARCHITECTURES for universal binary. JUCE respects this variable.
   - What's unclear: Whether there are JUCE-specific universal binary issues.
   - Recommendation: The existing `JAMWIDE_UNIVERSAL` option and CMAKE_OSX_ARCHITECTURES setting should work transparently with JUCE targets. No special handling needed.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | pluginval (plugin format validation) + custom assert tests (existing) |
| Config file | None needed for pluginval (CLI tool). Existing CMake test config for unit tests. |
| Quick run command | `cmake --build build --target JamWideJuce_VST3 && pluginval --validate-in-process --strictness-level 5 --validate "build/JamWideJuce_artefacts/Release/VST3/JamWide JUCE.vst3"` |
| Full suite command | `cmake --build build && pluginval --validate-in-process --strictness-level 5 --validate "build/JamWideJuce_artefacts/Release/VST3/JamWide JUCE.vst3" && pluginval --validate-in-process --strictness-level 5 --validate "build/JamWideJuce_artefacts/Release/AU/JamWide JUCE.component"` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| JUCE-01 | Plugin builds as VST3 and AU | build + pluginval | `cmake --build build --target JamWideJuce_VST3 JamWideJuce_AU && pluginval --validate-in-process --strictness-level 5 --validate "build/JamWideJuce_artefacts/Release/VST3/JamWide JUCE.vst3"` | Wave 0 |
| JUCE-02 | Standalone launches with audio device selection | build + launch test | `cmake --build build --target JamWideJuce_Standalone` (manual verify standalone launches) | Wave 0 |
| JUCE-04 | NinjamRunThread starts/stops cleanly | pluginval lifecycle | `pluginval --validate-in-process --strictness-level 5 --validate "..."` (tests activate/deactivate cycle) | Wave 0 |

### Sampling Rate
- **Per task commit:** Build JamWideJuce_VST3 target (compile check)
- **Per wave merge:** Full pluginval validation of VST3 and AU
- **Phase gate:** All formats build + pluginval passes at strictness 5 + standalone launches

### Wave 0 Gaps
- [ ] `libs/juce` -- JUCE 8.0.12 submodule (add via git submodule add)
- [ ] `libs/clap-juce-extensions` -- clap-juce-extensions submodule
- [ ] `juce/JamWideJuceProcessor.h` / `.cpp` -- AudioProcessor subclass
- [ ] `juce/JamWideJuceEditor.h` / `.cpp` -- minimal AudioProcessorEditor
- [ ] `juce/NinjamRunThread.h` / `.cpp` -- juce::Thread subclass
- [ ] CMakeLists.txt additions for JUCE targets
- [ ] `.github/workflows/juce-build.yml` -- CI with pluginval

## Sources

### Primary (HIGH confidence)
- [JUCE GitHub Releases](https://github.com/juce-framework/JUCE/releases) -- confirmed latest is 8.0.12 (Dec 16, 2025)
- [JUCE CMake API documentation](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md) -- juce_add_plugin parameters, format targets
- [JUCE Audio Bus Layouts tutorial](https://juce.com/tutorials/tutorial_audio_bus_layouts/) -- BusesProperties, isBusesLayoutSupported
- [JUCE Thread class reference](https://docs.juce.com/master/classThread.html) -- run(), threadShouldExit(), stopThread()
- [JUCE APVTS tutorial](https://juce.com/tutorials/tutorial_audio_processor_value_tree_state/) -- parameter management
- [JUCE PropertiesFile](https://docs.juce.com/master/structPropertiesFile_1_1Options.html) -- standalone settings storage
- [pluginval GitHub](https://github.com/Tracktion/pluginval) -- CLI validation tool

### Secondary (MEDIUM confidence)
- [Pamplejuce template](https://github.com/sudara/pamplejuce) -- verified JUCE 8 + clap-juce-extensions + GitHub Actions + pluginval pattern
- [clap-juce-extensions README](https://github.com/free-audio/clap-juce-extensions/blob/main/README.md) -- CMake integration with clap_juce_extensions_plugin()
- [JUCE Forum: AU multi-bus](https://forum.juce.com/t/au-ignores-isbuseslayoutsupported-only-shows-stereo-despite-vst3-working-correctly/68058) -- AU caching pitfall
- [JUCE Forum: OpenGLContext scaling](https://forum.juce.com/t/bug-when-attaching-an-openglcontext-to-plugin-the-whole-ui-is-scaled-wrong-in-vst2-vst3/35118) -- OpenGL attachment issues
- [JUCE Forum: AsyncUpdater vs callAsync](https://forum.juce.com/t/asyncupdater-vs-messagemanager-callasync/23459) -- thread communication patterns

### Tertiary (LOW confidence)
- [JUCE Roadmap Q3 2025](https://juce.com/blog/juce-roadmap-update-q3-2025/) -- JUCE 9 native CLAP mentioned but no release date (validates our decision to use clap-juce-extensions)
- [Plugins for Everyone blog](https://reillyspitzfaden.com/posts/2025/08/plugins-for-everyone-crossplatform-juce-with-cmake-github-actions/) -- community CI patterns

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- JUCE 8.0.12 is a tagged release with extensive documentation; clap-juce-extensions is widely used
- Architecture: HIGH -- BusesProperties, juce::Thread, APVTS are well-documented JUCE patterns with official tutorials
- Pitfalls: HIGH -- AU caching, OpenGL scaling, callAsync lifetime issues are well-documented in JUCE forum posts
- Build/CI: MEDIUM -- pluginval CI patterns are documented but AU-specific CI issues may require debugging

**Research date:** 2026-03-07
**Valid until:** 2026-04-07 (30 days -- JUCE 8.x is stable; clap-juce-extensions actively maintained)
