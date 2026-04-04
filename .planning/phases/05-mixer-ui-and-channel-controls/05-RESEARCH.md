# Phase 5: Mixer UI and Channel Controls - Research

**Researched:** 2026-04-04
**Domain:** JUCE 8.0.12 custom UI components, mixer controls, state persistence, NJClient integration
**Confidence:** HIGH

## Summary

Phase 5 fills the placeholder footer zones in ChannelStrip with functional pan/mute/solo controls, adds the custom VbFader component alongside the existing VuMeter, wires metronome controls into the master strip, implements additive solo logic, extends APVTS with local channel parameters, and completes state persistence via getStateInformation/setStateInformation.

The existing codebase is well-structured for this work. ChannelStrip already has the three-zone layout (header 66px, center ~516px, footer 38px) with the footer explicitly marked as a Phase 5 placeholder. The command/event system (SpscRing-based UiCommand/UiEvent) already includes SetLocalChannelMonitoringCommand and SetUserChannelStateCommand with volume, pan, mute, and solo fields -- all wired through NinjamRunThread::processCommands() to NJClient. The VuMeter is already driven by a centralized 30Hz timer. The primary new work is: (1) the VbFader custom component, (2) filling the footer zone with pan + M/S buttons, (3) solo logic, (4) metronome section in master strip, (5) local channel expand/collapse with 4 channels, and (6) APVTS extension with persistence.

**Primary recommendation:** Build VbFader as a fully custom juce::Component (not a subclassed juce::Slider) to achieve the Voicemeeter Banana fader design with circular thumb, dB readout on the thumb, and green fill. Use juce::Slider only for the horizontal pan slider and metronome slider, customized via LookAndFeel overrides.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** VB-Audio Voicemeeter Banana style big vertical fader per UI-SPEC -- 10px track, 44px circular thumb with green border, dB readout ON the thumb, green fill below thumb
- **D-02:** Mouse scroll wheel adjusts volume when hovering over a strip -- 1 step ~0.5 dB, fine-grained control
- **D-03:** Double-click on fader resets to 0 dB (unity gain)
- **D-04:** Double-click on pan knob/slider resets to center (0.0)
- **D-05:** dB readout format: one decimal place (e.g., "-6.0", "+2.5", "-inf")
- **D-06:** Fader range: -inf to +6 dB (0.0 to 2.0 linear), with dB scale ticks at +6, 0, -6, -18, -inf
- **D-07:** Footer zone (38px): pan slider on top (18px), Mute + Solo buttons side by side below (18px)
- **D-08:** Pan slider is horizontal, center-notched, spanning full strip width (100px minus padding)
- **D-09:** Mute button: "M", red (#E04040) when active. Solo button: "S", yellow (#CCB833) when active
- **D-10:** VU meter (24px) + gap (6px) + fader track (10px) = 40px centered in 100px strip, per UI-SPEC
- **D-11:** Additive solo -- multiple channels can be soloed simultaneously. Only soloed channels are heard; non-soloed channels are muted. Standard DAW mixer behavior.
- **D-12:** Expose all 4 NINJAM input channels using the same expand/collapse group pattern as remote multi-channel users
- **D-13:** Local strip uses parent/child logic: collapsed shows first channel, expanded shows all 4 with individual fader/VU/pan/mute/solo
- **D-14:** Each child strip has an input bus selector for its NINJAM channel
- **D-15:** Transmit toggle per local channel
- **D-16:** Metronome controls live inside the master strip, below the master fader
- **D-17:** Horizontal metronome fader with yellow (#CCB833) fill, per UI-SPEC
- **D-18:** Metronome has volume + mute only (no pan control) -- keeps it simple
- **D-19:** NJClient config_metronome and config_metronome_mute atomics are already available
- **D-20:** Persist via APVTS getStateInformation/setStateInformation (already scaffolded)
- **D-21:** Persist: master vol/mute, metronome vol/mute (already in APVTS), local channel vol/pan/mute/transmit, input selector
- **D-22:** Persist: last server address, last username (not password)
- **D-23:** Persist: UI scale factor (1x/1.5x/2x), chat sidebar visibility
- **D-24:** Do NOT persist remote user mixer state -- users change between sessions, keying by username would go stale
- **D-25:** State version already at 1 with forward-compatible migration pattern (Plan 02-01 decision)

### Claude's Discretion
- Custom VbFader component implementation details (paint, drag, thumb rendering)
- Pan slider implementation (horizontal juce::Slider customized or custom painted)
- How local channel expand/collapse stores expanded state
- Exact APVTS parameter IDs for new parameters (local channel vol/pan/mute)
- How solo logic is implemented (NJClient level vs UI-side mute routing)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| UI-04 | Remote user mixer (volume, pan, mute, solo per channel) | VbFader for volume, horizontal Slider for pan, TextButton M/S, SetUserChannelStateCommand already exists in command system, additive solo logic |
| UI-05 | Local channel controls (volume, pan, mute, monitoring) | Same controls as remote, expand/collapse for 4 NINJAM channels via parent/child pattern, SetLocalChannelMonitoringCommand exists, SetLocalChannelInfoCommand for input selector/transmit |
| UI-06 | Metronome controls (volume, pan, mute) | Horizontal yellow slider + mute button in master strip. D-18 scopes to volume+mute only (no pan). config_metronome/config_metronome_mute atomics available. Already synced in processBlock. |
| UI-08 | VU meters for local and remote channels | VuMeter already functional with 30Hz centralized timer. Phase 5 changes layout (VU 24px + gap 6px + fader 10px), no VU code changes needed |
| JUCE-06 | Plugin state saves/restores via getStateInformation/setStateInformation | APVTS scaffolding exists with 4 params. Extend with local channel params. Non-APVTS state (lastServer, lastUsername, scaleFactor, chatVisibility) serialized alongside in the same XML ValueTree. State version remains 1. |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | Framework (UI components, AudioProcessor, APVTS) | Already pinned as submodule |
| juce::Component | 8.0.12 | Base class for VbFader custom component | Standard JUCE pattern for fully custom controls |
| juce::Slider | 8.0.12 | Pan slider (horizontal), metronome slider (horizontal) | Standard JUCE widget with LookAndFeel customization |
| juce::TextButton | 8.0.12 | Mute/Solo toggle buttons | Already used throughout codebase |
| juce::AudioProcessorValueTreeState | 8.0.12 | Parameter management and state persistence | Already in use (apvts member on processor) |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| juce::NormalisableRange | 8.0.12 | Skewed fader range mapping (linear 0-2 with log-like feel) | For APVTS parameter definitions with setSkewForCentre |
| juce::ValueTree | 8.0.12 | State serialization (non-APVTS properties alongside APVTS) | For persisting lastServer, lastUsername, scaleFactor, chatVisibility |
| juce::LookAndFeel_V4 | 8.0.12 | Override drawLinearSlider for pan/metronome sliders | Extend existing JamWideLookAndFeel |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Custom VbFader Component | Subclassed juce::Slider | Slider has too much built-in behavior (popup bubble, text box) that fights the Voicemeeter design. Custom Component is simpler for circular thumb with dB-on-thumb, green fill, and precise control over mouse interaction. |
| juce::Slider for pan | Custom Component | Pan slider is standard enough for Slider + LookAndFeel override. Center-notch and horizontal layout map naturally to Slider::LinearHorizontal. |

**Installation:** No new dependencies. All components are JUCE built-in classes.

## Architecture Patterns

### Recommended Project Structure
```
juce/ui/
    VbFader.h / .cpp         # New: custom vertical fader component
    ChannelStrip.h / .cpp    # Modified: add VbFader, pan, M/S, footer wiring
    ChannelStripArea.h / .cpp # Modified: local expand/collapse, solo logic, 4 local channels
    JamWideLookAndFeel.h / .cpp # Modified: drawLinearSlider override for pan/metro
juce/
    JamWideJuceProcessor.h / .cpp # Modified: extend APVTS, persist non-APVTS state
    NinjamRunThread.cpp       # Modified: setup all 4 local channels on connect
    JamWideJuceEditor.cpp     # Minor: no structural changes needed
```

### Pattern 1: Custom Component Fader (VbFader)
**What:** A fully custom juce::Component that handles mouseDown/mouseDrag/mouseUp/mouseWheelMove/mouseDoubleClick for fader interaction, with custom paint() for the Voicemeeter Banana style.
**When to use:** When the desired fader appearance and interaction diverge significantly from juce::Slider defaults.
**Example:**
```cpp
// VbFader.h
class VbFader : public juce::Component
{
public:
    VbFader();

    void setValue(float newValue);       // 0.0 to 2.0 linear
    float getValue() const;
    void setDefaultValue(float val);     // for double-click reset

    std::function<void(float)> onValueChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override;

private:
    float value_ = 1.0f;        // linear 0.0-2.0
    float defaultValue_ = 1.0f; // reset target (0 dB = 1.0)
    int dragStartY_ = 0;
    float dragStartValue_ = 0.0f;

    float valueToY(float val) const;    // linear -> pixel position
    float yToValue(int y) const;        // pixel -> linear
    juce::String formatDb(float linear) const; // -> "-6.0", "+2.5", "-inf"

    static constexpr int kTrackWidth = 10;
    static constexpr int kThumbDiameter = 44;
    static constexpr float kMinLinear = 0.0f;   // -inf dB
    static constexpr float kMaxLinear = 2.0f;   // +6 dB
};
```

### Pattern 2: Linear-to-dB Conversion
**What:** Convert between the linear 0.0-2.0 range used internally and dB display values.
**When to use:** For dB readout on fader thumb, dB scale tick marks.
**Example:**
```cpp
// Linear gain to dB (0.0 -> -inf, 1.0 -> 0 dB, 2.0 -> +6.02 dB)
static juce::String linearToDbString(float linear)
{
    if (linear <= 0.0001f) return "-inf";
    float db = 20.0f * std::log10(linear);
    // Clamp display
    if (db > 6.0f) db = 6.0f;
    // Format with sign
    if (db >= 0.0f)
        return "+" + juce::String(db, 1);
    return juce::String(db, 1);
}

// dB scale tick positions (per D-06): +6, 0, -6, -18, -inf
// Map to linear: 2.0, 1.0, 0.5012, 0.1259, 0.0
static constexpr float kDbTicks[] = { 2.0f, 1.0f, 0.5012f, 0.1259f, 0.0f };
static constexpr const char* kDbTickLabels[] = { "+6", "0", "-6", "-18", "-inf" };
```

### Pattern 3: Additive Solo Logic (UI-side)
**What:** Solo state tracked in the ChannelStripArea. When any channel is soloed, all non-soloed channels have their effective mute set. Applied by pushing SetUserChannelStateCommand / SetLocalChannelMonitoringCommand with mute=true.
**When to use:** Solo is a UI-level concern -- NJClient has per-channel solo fields but the "any solo active" aggregation must be done in the UI layer.
**Example:**
```cpp
// In ChannelStripArea or a dedicated SoloManager:
void recalculateSolo()
{
    bool anySoloed = false;
    // Check all remote channels + local channels for solo state
    for (auto& strip : allStrips)
        if (strip->isSoloed()) anySoloed = true;

    if (anySoloed) {
        // Mute all non-soloed channels
        for (auto& strip : allStrips)
            strip->setEffectiveMute(!strip->isSoloed());
    } else {
        // Restore actual mute state
        for (auto& strip : allStrips)
            strip->setEffectiveMute(strip->isExplicitlyMuted());
    }
}
```

**Important note on NJClient solo:** NJClient::SetUserChannelState already accepts a `setsolo` parameter and NJClient internally tracks solo state. The same is true for SetLocalChannelMonitoring. The NJClient implementation handles solo-muting internally in its audio mixing. Therefore, the simplest approach is to push solo state changes directly to NJClient via the command queue and let NJClient handle the audio-level solo logic. The UI only needs to track which channels are soloed for visual display purposes.

### Pattern 4: APVTS Attachment Pattern
**What:** Use juce::AudioProcessorValueTreeState::SliderAttachment and ButtonAttachment to bind UI controls to APVTS parameters.
**When to use:** For local channel parameters that persist. NOT for remote user state (D-24 says don't persist).
**Example:**
```cpp
// In ChannelStrip (for local strips only):
std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAttachment;
std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAttachment;
std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;

// Problem: VbFader is NOT a juce::Slider, so SliderAttachment won't work directly.
// Solution: Use ParameterAttachment (lower-level) or manually sync via listener.
```

**Recommended approach for VbFader + APVTS:** Use `juce::ParameterAttachment` (available in JUCE 8). This attaches to an APVTS parameter and provides a callback when the parameter changes, plus a `setValueAsCompleteGesture()` / `beginGesture()` / `setValueAsPartOfGesture()` / `endGesture()` API for host automation recording.

```cpp
// VbFader integration with APVTS:
class VbFader : public juce::Component
{
public:
    void attachToParameter(juce::RangedAudioParameter& param,
                          juce::UndoManager* undoManager = nullptr);
private:
    std::unique_ptr<juce::ParameterAttachment> attachment_;
};
```

### Pattern 5: Non-APVTS State in ValueTree
**What:** Store non-automatable settings (lastServer, lastUsername, scaleFactor, chatVisibility) as properties on the APVTS ValueTree state, alongside the standard parameter tree.
**When to use:** For state that must survive DAW save/load but isn't an automatable parameter.
**Example:**
```cpp
// In getStateInformation:
auto state = apvts.copyState();
state.setProperty("stateVersion", currentStateVersion, nullptr);
state.setProperty("lastServer", lastServerAddress, nullptr);
state.setProperty("lastUsername", lastUsername, nullptr);
state.setProperty("scaleFactor", scaleFactor, nullptr);
state.setProperty("chatVisible", chatSidebarVisible, nullptr);
// Local channel settings (not APVTS params, but persistent)
for (int ch = 0; ch < 4; ++ch) {
    state.setProperty("localCh" + juce::String(ch) + "Input", localInputSelector[ch], nullptr);
    state.setProperty("localCh" + juce::String(ch) + "Transmit", localTransmit[ch], nullptr);
}
```

### Anti-Patterns to Avoid
- **Subclassing juce::Slider for VbFader:** The Slider class has deeply embedded assumptions about thumb shape, text boxes, and popup values that fight the Voicemeeter Banana design. Override surface area is too large. Custom Component is simpler.
- **Acquiring clientLock from UI thread:** The threading contract explicitly prohibits this. All communication must go through cmd_queue/evt_queue.
- **Static variables for UI state:** Phase 4 learned this lesson (prevPollStatus_ was changed from static to member). All per-editor state must be instance members.
- **Persisting remote user mixer state:** D-24 explicitly prohibits this. Users change between sessions, so keyed state goes stale.
- **Using APVTS for remote channel parameters:** Remote channels are dynamic (users join/leave). APVTS parameters are fixed at construction time. Remote channel state is transient UI-only state.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Parameter persistence | Custom XML serialization | APVTS + ValueTree properties | APVTS handles undo, automation, state save/load. ValueTree properties handle non-automatable extras. |
| Fader-to-host-automation bridge | Manual beginChangeGesture/endChangeGesture | juce::ParameterAttachment | Handles gesture grouping, thread-safe parameter updates, correct automation recording |
| Timer-driven VU updates | New timer per VbFader | Existing 30Hz centralized timer in ChannelStripArea | Already established pattern, adding more timers wastes CPU |
| Solo aggregation | Manual solo counting per strip | NJClient's internal solo handling | NJClient already has solo fields on SetUserChannelState and SetLocalChannelMonitoring. Push solo state via command queue; NJClient handles audio-level muting. |

**Key insight:** The command/event system and NJClient API already support every mixer operation needed. Phase 5 is primarily a UI component creation and wiring task, not an architecture extension.

## Common Pitfalls

### Pitfall 1: VbFader Value Clamping at Extremes
**What goes wrong:** Linear 0.0 maps to -inf dB. Dragging to exact 0.0 is hard; slight mouse movement above 0 produces audible residual signal. Conversely, the 0.0-2.0 range must never exceed 2.0 on the high end.
**Why it happens:** Mouse coordinate to value mapping without proper clamping.
**How to avoid:** Clamp value in yToValue() to [0.0, 2.0]. Snap values below a threshold (e.g., linear < 0.001) to exactly 0.0 for true silence. Add a small dead zone at bottom of fader travel.
**Warning signs:** Channel not fully silent when fader is at bottom.

### Pitfall 2: Mouse Drag Sensitivity Too High
**What goes wrong:** Fader jumps erratically when dragging because mouse Y delta maps to too large a value change.
**Why it happens:** Direct pixel-to-value mapping on a 516px fader travel with 0-2 range means ~0.004 per pixel, which is fine, but scroll wheel at 1 step = 0.5 dB needs careful calibration.
**How to avoid:** Use velocity-based dragging: store drag start position and value, compute delta from start, not incremental. For scroll wheel, convert 0.5 dB to linear delta at current position (dB is logarithmic, so the linear step varies).
**Warning signs:** Fader "jumps" instead of smooth tracking.

### Pitfall 3: APVTS Parameter IDs Must Be Unique and Version-Tagged
**What goes wrong:** Adding new parameters with version number 1 (same as existing params) causes state restoration issues when loading presets saved before the new params existed.
**Why it happens:** APVTS uses ParameterID(name, versionHint). The versionHint tells APVTS how to handle state migration.
**How to avoid:** Use version hint 1 for the new parameters since state version remains 1. The forward-compatible migration pattern in setStateInformation already handles missing parameters gracefully -- APVTS restores defaults for parameters not found in the saved state.
**Warning signs:** Parameters reset to defaults unexpectedly after DAW reload.

### Pitfall 4: Scroll Wheel Events on Overlapping Components
**What goes wrong:** Mouse wheel over a strip scrolls the Viewport horizontally AND adjusts the fader simultaneously.
**Why it happens:** JUCE propagates mouse wheel events up the component hierarchy unless consumed.
**How to avoid:** VbFader::mouseWheelMove() must consume the event (do not call Component::mouseWheelMove() when handling fader scroll). Alternatively, use setWantsKeyboardFocus(true) and check if hover is over fader vs. strip background.
**Warning signs:** Fader moves while strip area scrolls simultaneously.

### Pitfall 5: Solo State Not Applied to New Users Joining Mid-Session
**What goes wrong:** A new user joins while solo is active. Their channels play audio because they were created after the solo state was set.
**Why it happens:** Solo state is only applied when toggled, not on channel creation.
**How to avoid:** When refreshFromUsers() rebuilds strips, reapply current solo state to all channels (including newly created ones). If any channel is soloed, new channels should start muted (unless they are the soloed one).
**Warning signs:** Audio leaks from new user while solo is supposed to be active.

### Pitfall 6: Local Channel Setup on Connect
**What goes wrong:** Currently, NinjamRunThread sets up only channel 0 on connect. Phase 5 exposes 4 local channels, but channels 1-3 are never created in NJClient.
**Why it happens:** The on-connect setup was written for Phase 3's single-channel behavior.
**How to avoid:** On connect (NJC_STATUS_OK transition in NinjamRunThread::run()), set up all 4 local channels via SetLocalChannelInfo, with transmit=false for channels 1-3 by default. Restore persisted transmit/input state from APVTS/ValueTree.
**Warning signs:** Only first local channel has audio; channels 1-3 are silent or missing.

### Pitfall 7: processBlock Must Sync New APVTS Parameters
**What goes wrong:** New local channel parameters in APVTS are not synced to NJClient in processBlock, so fader changes have no audible effect.
**Why it happens:** processBlock currently syncs only masterVol, masterMute, metroVol, metroMute. New local channel params need similar atomic sync.
**How to avoid:** For local channels, syncing in processBlock is NOT the correct approach. NJClient::SetLocalChannelMonitoring is the API, and it must be called under clientLock (which processBlock does NOT hold). Use the command queue pattern: UI pushes SetLocalChannelMonitoringCommand, run thread applies it. Master/metro params work via atomics because NJClient reads them atomically in AudioProc. Local channel monitoring is different -- it uses locked state.
**Warning signs:** Volume/pan changes for local channels have no audible effect.

## Code Examples

### VbFader Paint Implementation
```cpp
// Source: Custom implementation following D-01, D-05, D-06, D-10 from UI-SPEC
void VbFader::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float cx = bounds.getCentreX();

    // Track (10px wide, centered)
    auto trackBounds = juce::Rectangle<float>(
        cx - kTrackWidth * 0.5f, 0.0f,
        (float)kTrackWidth, bounds.getHeight());

    // Track background
    g.setColour(juce::Colour(0xff0A0D18));  // kVuBackground
    g.fillRoundedRectangle(trackBounds, 2.0f);

    // Green fill below thumb
    float thumbY = valueToY(value_);
    auto fillBounds = trackBounds.withTop(thumbY);
    g.setColour(juce::Colour(0xff2A7A4A));  // Green fill
    g.fillRoundedRectangle(fillBounds, 2.0f);

    // dB scale ticks (right of track)
    g.setFont(juce::FontOptions(9.0f));
    g.setColour(juce::Colour(0xff8888AA));  // kTextSecondary
    for (int i = 0; i < 5; ++i)
    {
        float tickY = valueToY(kDbTicks[i]);
        float tickX = cx + kTrackWidth * 0.5f + 4.0f;
        g.drawText(kDbTickLabels[i],
                   juce::Rectangle<float>(tickX, tickY - 6.0f, 30.0f, 12.0f),
                   juce::Justification::centredLeft, false);
    }

    // Thumb (44px circle with green border)
    float thumbCX = cx;
    float thumbCY = thumbY;
    float r = kThumbDiameter * 0.5f;

    // Thumb background
    g.setColour(juce::Colour(0xff2A2D48));  // kSurfaceStrip
    g.fillEllipse(thumbCX - r, thumbCY - r, kThumbDiameter, kThumbDiameter);

    // Thumb green border
    g.setColour(juce::Colour(0xff40E070));  // kAccentConnect
    g.drawEllipse(thumbCX - r, thumbCY - r, kThumbDiameter, kThumbDiameter, 2.0f);

    // dB text ON thumb
    g.setColour(juce::Colour(0xffE0E0E0));  // kTextPrimary
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText(formatDb(value_),
               juce::Rectangle<float>(thumbCX - r, thumbCY - 7.0f,
                                       (float)kThumbDiameter, 14.0f),
               juce::Justification::centred, false);
}
```

### Fader Mouse Interaction
```cpp
// Source: Implementation of D-02, D-03
void VbFader::mouseDown(const juce::MouseEvent& e)
{
    dragStartY_ = e.y;
    dragStartValue_ = value_;
    if (attachment_) attachment_->beginGesture();
}

void VbFader::mouseDrag(const juce::MouseEvent& e)
{
    // Invert Y: dragging up increases value
    float deltaPixels = (float)(dragStartY_ - e.y);
    float range = (float)getHeight() - kThumbDiameter;
    float deltaValue = (deltaPixels / range) * (kMaxLinear - kMinLinear);
    float newValue = juce::jlimit(kMinLinear, kMaxLinear,
                                   dragStartValue_ + deltaValue);
    // Snap near-zero to zero
    if (newValue < 0.001f) newValue = 0.0f;
    setValue(newValue);
}

void VbFader::mouseUp(const juce::MouseEvent&)
{
    if (attachment_) attachment_->endGesture();
}

void VbFader::mouseDoubleClick(const juce::MouseEvent&)
{
    setValue(defaultValue_);  // D-03: reset to 0 dB (1.0 linear)
}

void VbFader::mouseWheelMove(const juce::MouseEvent&,
                              const juce::MouseWheelDetails& wheel)
{
    // D-02: ~0.5 dB per scroll step
    // 0.5 dB in linear at unity = 10^(0.5/20) - 1.0 ~ 0.059
    // But constant linear step feels wrong on a dB scale.
    // Better: compute dB, add 0.5 * direction, convert back.
    float currentDb = (value_ <= 0.0001f) ? -100.0f
                     : 20.0f * std::log10(value_);
    float newDb = currentDb + wheel.deltaY * 0.5f;
    newDb = juce::jlimit(-100.0f, 6.0f, newDb);
    float newLinear = (newDb <= -100.0f) ? 0.0f
                     : std::pow(10.0f, newDb / 20.0f);
    setValue(juce::jlimit(kMinLinear, kMaxLinear, newLinear));
}
```

### Pan Slider LookAndFeel Override
```cpp
// Source: Implementation of D-07, D-08 via JamWideLookAndFeel extension
void JamWideLookAndFeel::drawLinearSlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
    juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
            sliderPos, 0, 0, style, slider);
        return;
    }

    auto bounds = juce::Rectangle<float>((float)x, (float)y,
                                          (float)width, (float)height);
    float trackH = 4.0f;
    float trackY = bounds.getCentreY() - trackH * 0.5f;

    // Track
    g.setColour(juce::Colour(kVuBackground));
    g.fillRoundedRectangle(bounds.getX(), trackY, bounds.getWidth(), trackH, 2.0f);

    // Center notch
    float centerX = bounds.getX() + bounds.getWidth() * 0.5f;
    g.setColour(juce::Colour(kBorderSubtle));
    g.drawVerticalLine((int)centerX, trackY - 2.0f, trackY + trackH + 2.0f);

    // Fill from center to thumb position
    bool isMetronome = slider.getName() == "MetroSlider";
    juce::Colour fillColour = isMetronome
        ? juce::Colour(kAccentWarning)  // Yellow for metronome
        : juce::Colour(kAccentConnect); // Green for pan

    if (!isMetronome) {
        // Pan: fill from center to position
        float fillLeft = juce::jmin(centerX, sliderPos);
        float fillRight = juce::jmax(centerX, sliderPos);
        g.setColour(fillColour);
        g.fillRoundedRectangle(fillLeft, trackY, fillRight - fillLeft, trackH, 2.0f);
    } else {
        // Metronome: fill from left to position
        g.setColour(fillColour);
        g.fillRoundedRectangle(bounds.getX(), trackY, sliderPos - bounds.getX(), trackH, 2.0f);
    }

    // Thumb (small circle)
    float thumbSize = 12.0f;
    g.setColour(juce::Colour(kTextPrimary));
    g.fillEllipse(sliderPos - thumbSize * 0.5f,
                  bounds.getCentreY() - thumbSize * 0.5f,
                  thumbSize, thumbSize);
}
```

### Mute/Solo Button Styling
```cpp
// Source: Implementation of D-09
void configureMuteButton(juce::TextButton& btn)
{
    btn.setButtonText("M");
    btn.setClickingTogglesState(true);
    btn.setColour(juce::TextButton::buttonColourId,
                  juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    btn.setColour(juce::TextButton::buttonOnColourId,
                  juce::Colour(JamWideLookAndFeel::kAccentDestructive));  // Red when active
    btn.setColour(juce::TextButton::textColourOffId,
                  juce::Colour(JamWideLookAndFeel::kTextSecondary));
    btn.setColour(juce::TextButton::textColourOnId,
                  juce::Colour(0xffFFFFFF));  // White text on red
}

void configureSoloButton(juce::TextButton& btn)
{
    btn.setButtonText("S");
    btn.setClickingTogglesState(true);
    btn.setColour(juce::TextButton::buttonColourId,
                  juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    btn.setColour(juce::TextButton::buttonOnColourId,
                  juce::Colour(JamWideLookAndFeel::kAccentWarning));  // Yellow when active
    btn.setColour(juce::TextButton::textColourOffId,
                  juce::Colour(JamWideLookAndFeel::kTextSecondary));
    btn.setColour(juce::TextButton::textColourOnId,
                  juce::Colour(0xff000000));  // Black text on yellow
}
```

### APVTS Parameter Extension
```cpp
// Source: Implementation of D-20, D-21
// Add to createParameterLayout():
// Local channel parameters (4 channels x 3 params = 12 new params)
for (int ch = 0; ch < 4; ++ch)
{
    juce::String prefix = "localCh" + juce::String(ch);

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{prefix + "Vol", 1}, "Local Ch" + juce::String(ch + 1) + " Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{prefix + "Pan", 1}, "Local Ch" + juce::String(ch + 1) + " Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{prefix + "Mute", 1}, "Local Ch" + juce::String(ch + 1) + " Mute",
        false));
}
```

### Non-APVTS State Persistence
```cpp
// Source: Implementation of D-22, D-23
void JamWideJuceProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("stateVersion", currentStateVersion, nullptr);
    // Non-APVTS persistent state
    state.setProperty("lastServer", lastServerAddress, nullptr);
    state.setProperty("lastUsername", lastUsername, nullptr);
    state.setProperty("scaleFactor", (double)scaleFactor, nullptr);
    // Local channel input selectors and transmit state (not automatable)
    for (int ch = 0; ch < 4; ++ch)
    {
        state.setProperty("localCh" + juce::String(ch) + "Input",
                          localInputSelector[ch], nullptr);
        state.setProperty("localCh" + juce::String(ch) + "Tx",
                          localTransmit[ch], nullptr);
    }
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Custom XML state serialization | APVTS copyState/replaceState + ValueTree properties | JUCE 5+ (mature by JUCE 8) | Single source of truth for all persistent state |
| Per-component Timer for VU | Centralized 30Hz timer in ChannelStripArea | Phase 4 (REVIEW FIX #7) | All VU updates driven by one timer -- already established |
| juce::Slider with extensive LAF overrides | Custom Component for unique faders | Common JUCE pattern | Cleaner code, full control over rendering and interaction |
| Static variables in editor | Instance member variables | Phase 4 (REVIEW FIX) | Prevents state leaking across editor reconstructions |

**Deprecated/outdated:**
- `juce::AudioProcessorParameter::beginChangeGesture()` / `endChangeGesture()`: Replaced by `juce::ParameterAttachment` which handles gestures automatically (JUCE 6+)

## Open Questions

1. **Fader value-to-position mapping: linear or skewed?**
   - What we know: The fader range is 0.0-2.0 linear (D-06). NJClient uses linear gain. dB scale ticks are at +6, 0, -6, -18, -inf.
   - What's unclear: Should the fader travel be linearly proportional to gain (bottom half is -inf to 0 dB, top quarter is 0-6 dB), or should it use a skew to give more fader travel in the -20 to +6 dB range?
   - Recommendation: Use a skewed mapping where the midpoint of fader travel corresponds to approximately -6 dB (linear ~0.5). This gives the most useful range the most physical travel. Implement via a custom mapping function in VbFader, not NormalisableRange skew (since VbFader is a custom Component, not a Slider). A practical approach: `value = pow(normalizedY, 2.5) * 2.0` gives more resolution in the low/mid range.

2. **Solo state via NJClient vs. UI-only tracking**
   - What we know: NJClient::SetUserChannelState and SetLocalChannelMonitoring both accept solo parameters. NJClient likely handles solo-muting internally in its audio mixer.
   - What's unclear: Does NJClient's internal solo logic match the "additive solo" behavior described in D-11? (Multiple solos, only soloed channels audible.)
   - Recommendation: Test NJClient's solo behavior. If it implements additive solo correctly, simply push solo state via commands and let NJClient handle audio. If not, implement solo logic UI-side by overriding effective mute state.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CMake CTest + pluginval (no unit test framework for JUCE UI) |
| Config file | CMakeLists.txt (JAMWIDE_BUILD_TESTS option, pluginval validation target) |
| Quick run command | `cmake --build build --target validate` |
| Full suite command | `cmake --build build --target validate && cmake --build build --target test` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| UI-04 | Remote volume/pan/mute/solo controls work | manual | Load in DAW, connect to server, adjust remote channel controls | N/A |
| UI-05 | Local channel controls work, 4 channels, expand/collapse | manual | Load in DAW, expand local strip, adjust each channel | N/A |
| UI-06 | Metronome volume/mute in master strip | manual | Load in DAW, connect, adjust metronome slider | N/A |
| UI-08 | VU meters display levels for local and remote | manual | Load in DAW, connect, observe VU activity | N/A (already working from Phase 4) |
| JUCE-06 | State persists across DAW save/load | manual + pluginval | `cmake --build build --target validate` (pluginval tests state save/restore) | Exists (pluginval target) |

### Sampling Rate
- **Per task commit:** `cmake --build build -j` (compile check)
- **Per wave merge:** `cmake --build build --target validate` (pluginval validation)
- **Phase gate:** Full pluginval validation + manual testing against live NINJAM server

### Wave 0 Gaps
- No automated UI unit tests exist (standard for JUCE GUI projects -- pluginval covers host integration)
- Pluginval will verify state save/restore works for JUCE-06
- Manual testing is required for all UI-04, UI-05, UI-06, UI-08 behaviors (these are visual/interactive requirements)

*(No new test infrastructure needed -- pluginval already validates state persistence. UI controls require manual verification.)*

## Sources

### Primary (HIGH confidence)
- **JUCE 8.0.12 source code** (local submodule at `libs/juce/`) -- juce::Slider, juce::Component, LookAndFeel_V4::drawLinearSlider, juce::ParameterAttachment, juce::NormalisableRange, APVTS
- **Existing codebase** -- ChannelStrip.h/cpp, ChannelStripArea.h/cpp, VuMeter.h/cpp, JamWideJuceProcessor.h/cpp, NinjamRunThread.cpp, JamWideLookAndFeel.h/cpp, ui_command.h, ui_state.h
- **04-UI-SPEC.md** -- Full visual specification for fader, VU, controls, colors, spacing
- **05-CONTEXT.md** -- 25 locked decisions (D-01 through D-25)

### Secondary (MEDIUM confidence)
- **NJClient API** (`src/core/njclient.h`) -- SetUserChannelState, SetLocalChannelMonitoring, GetUserChannelPeak, config_metronome atomics -- verified by reading header

### Tertiary (LOW confidence)
- NJClient internal solo logic behavior -- needs runtime verification (Open Question #2)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all JUCE built-in, already in use in project
- Architecture: HIGH -- patterns follow established Phase 4 conventions (centralized timer, command/event queues, LookAndFeel tokens)
- Pitfalls: HIGH -- derived from direct code reading and understanding of threading contract
- VbFader design: HIGH -- specifications are detailed in UI-SPEC and CONTEXT decisions
- Solo logic: MEDIUM -- NJClient API has solo fields, but internal behavior not runtime-verified

**Research date:** 2026-04-04
**Valid until:** 2026-05-04 (stable -- no external dependency changes expected)
