---
phase: 5
slug: mixer-ui-and-channel-controls
status: draft
shadcn_initialized: false
preset: none
created: 2026-04-04
---

# Phase 5 -- UI Design Contract

> Interaction and component contract for mixer controls, fader interactions, and state persistence. This spec covers **only the new components and behaviors Phase 5 adds** to the existing Phase 4 shell.
>
> **Foundation:** All spacing, typography, color tokens, layout structure, and window dimensions are inherited from [04-UI-SPEC.md](../04-core-ui-panels/04-UI-SPEC.md). This document does not duplicate them.
>
> **Platform:** JUCE 8.0.12 (C++ desktop plugin / standalone). All visual values consumed by `JamWideLookAndFeel` and custom `paint()` overrides.

---

## Design System (Inherited)

| Property | Value | Source |
|----------|-------|--------|
| Tool | JUCE LookAndFeel_V4 (`JamWideLookAndFeel`) | 04-UI-SPEC |
| Spacing | 4/8/16/24/32 scale | 04-UI-SPEC |
| Typography | 11/13/16 px, Regular + Bold | 04-UI-SPEC |
| Color tokens | 20+ tokens in `JamWideLookAndFeel.h` | 04-UI-SPEC, code |
| Strip dimensions | 100px wide, 106px pitch, 626px tall | 04-UI-SPEC |

**No new spacing tokens, typography sizes, or color tokens are introduced by Phase 5.** All visual values use existing tokens defined in Phase 4.

---

## New Components

### 1. VbFader (Custom Component)

The primary new interaction element. VB-Audio Voicemeeter Banana-style big vertical fader.

| Property | Value | Source |
|----------|-------|--------|
| Class name | `VbFader` | D-01 |
| Parent class | `juce::Component` (fully custom painted) | Claude's discretion |
| Orientation | Vertical, bottom = min, top = max | D-01 |
| Track width | 10px centered in component bounds | 04-UI-SPEC line 119 |
| Track color | `kBorderSubtle` (#3A3D58) | 04-UI-SPEC |
| Fill color | `kAccentConnect` (#40E070) green, from bottom to thumb position | D-01, 04-UI-SPEC |
| Thumb shape | 44px diameter circle | 04-UI-SPEC line 115 |
| Thumb border | 2px solid `kAccentConnect` (#40E070) | D-01 |
| Thumb fill | `kSurfaceStrip` (#2A2D48) | Consistent with strip bg |
| dB readout | Centered ON the thumb, 11px Bold, `kTextPrimary` (#E0E0E0) | D-01, D-05 |
| dB format | One decimal: "-6.0", "+2.5", "0.0", "-inf" | D-05 |
| Value range | 0.0 to 2.0 linear (maps to -inf to +6 dB) | D-06 |
| dB scale ticks | +6, 0, -6, -18, -inf -- 1px marks right of track, `kTextSecondary` (#8888AA) | D-06 |
| Tick label font | 9px Regular, `kTextSecondary` | Proportional to track |

#### dB Mapping

Linear-to-dB conversion for display:

| Linear Value | dB Display |
|-------------|------------|
| 2.0 | "+6.0" |
| 1.0 | "0.0" |
| 0.5 | "-6.0" |
| 0.125 | "-18.0" |
| 0.0 | "-inf" |

Formula: `dB = 20 * log10(linear)`. Threshold for -inf display: linear < 0.001.

#### VbFader Interactions

| Interaction | Behavior | Source |
|-------------|----------|--------|
| Mouse drag | Vertical drag moves thumb. 1px = smallest increment. No horizontal dead zone needed (thumb is on the track). | Standard |
| Scroll wheel | 1 notch = 0.5 dB change. Hover over any part of the strip activates scroll for that strip's fader. | D-02 |
| Double-click | Reset to 0 dB (linear 1.0). Thumb animates to center position over 100ms. | D-03 |
| Right-click | No context menu (reserved for future). | -- |
| Keyboard | No keyboard focus. Faders are mouse-only in this phase. | -- |

#### Scroll Wheel Detail

Scroll events received anywhere within the `ChannelStrip` bounds (not just on the fader) adjust that strip's fader. This matches DAW mixer behavior where hovering over a channel and scrolling adjusts volume without precise fader targeting.

Scroll step mapping:
- 1 scroll notch = 0.5 dB change in the fader value
- At the -inf end (linear < 0.01): 1 notch jumps to -40 dB (linear ~0.01) or to 0.0 (mute)
- At the +6 dB end: clamp to 2.0 (linear)
- Direction: scroll up = louder, scroll down = quieter

#### Double-Click Reset Detail

- Double-click detected via `mouseDoubleClick()` override on VbFader
- Resets value to 1.0 (0 dB)
- Brief 100ms linear animation via `juce::ComponentAnimator` or manual interpolation in paint
- During animation, intermediate values are painted but NOT sent to the parameter (only final value is set)

---

### 2. Pan Slider (Horizontal)

| Property | Value | Source |
|----------|-------|--------|
| Type | Horizontal slider, custom painted | D-08 |
| Position | Footer zone top row, 18px tall | D-07 |
| Width | Strip width minus 8px padding (92px at 100px strip) | D-08 |
| Track height | 4px, centered vertically in 18px row | Proportional |
| Track color | `kBorderSubtle` (#3A3D58) | Consistent |
| Fill color | None (no directional fill -- center-notched) | D-08 |
| Thumb | 12px diameter circle, `kTextPrimary` (#E0E0E0) fill | Proportional to row |
| Center notch | 1px vertical tick at center of track, `kTextSecondary` | D-08 |
| Value range | -1.0 (full left) to +1.0 (full right), default 0.0 (center) | D-04 |
| Label | None (pan position implied by thumb) | Space-constrained |

#### Pan Slider Interactions

| Interaction | Behavior | Source |
|-------------|----------|--------|
| Mouse drag | Horizontal drag moves thumb. Constrained to track bounds. | Standard |
| Double-click | Reset to 0.0 (center). No animation needed (instant snap). | D-04 |
| Scroll wheel | Not handled by pan slider. Strip scroll goes to fader. | D-02 |

---

### 3. Mute Button

| Property | Value | Source |
|----------|-------|--------|
| Label | "M" | D-09 |
| Size | 18px tall, half of footer row width minus 2px gap (~44px wide) | D-07 |
| Position | Footer zone bottom row, left side | D-07 |
| Inactive state | `kBorderSubtle` border, `kSurfaceStrip` fill, `kTextSecondary` text | Consistent |
| Active state | `kAccentDestructive` (#E04040) fill, `kTextPrimary` text, no border | D-09 |
| Toggle | `setClickingTogglesState(true)` | Standard |

---

### 4. Solo Button

| Property | Value | Source |
|----------|-------|--------|
| Label | "S" | D-09 |
| Size | 18px tall, half of footer row width minus 2px gap (~44px wide) | D-07 |
| Position | Footer zone bottom row, right side | D-07 |
| Inactive state | `kBorderSubtle` border, `kSurfaceStrip` fill, `kTextSecondary` text | Consistent |
| Active state | `kAccentWarning` (#CCB833) fill, `kTextPrimary` text, no border | D-09 |
| Toggle | `setClickingTogglesState(true)` | Standard |

#### Solo Behavior (Additive)

| Rule | Detail | Source |
|------|--------|--------|
| Mode | Additive solo | D-11 |
| No solos active | All channels play normally | Standard |
| One or more solos active | Only soloed channels are heard; all non-soloed channels are effectively muted | D-11 |
| Mute + Solo | If a channel is both muted and soloed, mute wins (channel is silent) | Standard DAW behavior |
| Solo scope | Global across all strips (local + remote). Master strip has no solo. | D-11 |
| Implementation | Maintain a `soloCount` atomic or counter. When soloCount > 0, non-soloed channels have their audio multiplied by 0.0 in the mixer stage. | Claude's discretion |

---

### 5. Metronome Section (Inside Master Strip)

| Property | Value | Source |
|----------|-------|--------|
| Position | Below the master fader, in master strip footer area | D-16 |
| Height | 38px (reuse footer zone height) | D-07 |
| Contents | Horizontal metronome fader (18px row) + Mute button (18px row) + 2px gap | D-17, D-18 |

#### Metronome Fader

| Property | Value | Source |
|----------|-------|--------|
| Orientation | Horizontal | D-17 |
| Track height | 4px, centered in 18px row | Consistent with pan slider |
| Track width | Master strip width minus 8px padding (~102px) | D-17 |
| Track color | `kBorderSubtle` (#3A3D58) | Consistent |
| Fill color | `kAccentWarning` (#CCB833) yellow, from left to thumb | D-17 |
| Thumb | 12px diameter circle, `kAccentWarning` fill, `kBgPrimary` border | D-17 |
| Value range | 0.0 to 2.0 linear (-inf to +6 dB) | Same as volume faders |
| Default | 0.5 (metroVol APVTS default) | Existing APVTS |
| No pan | Metronome has no pan control | D-18 |

#### Metronome Mute Button

| Property | Value | Source |
|----------|-------|--------|
| Label | "MUTE" | D-18 |
| Size | 18px tall, full width of master footer (~102px) | D-16 |
| Inactive state | `kBorderSubtle` border, `kBgElevated` fill, `kTextSecondary` text | Consistent |
| Active state | `kAccentDestructive` (#E04040) fill, `kTextPrimary` text | Consistent with mute |

#### Metronome Data Binding

| Parameter | NJClient Field | Source |
|-----------|---------------|--------|
| Volume | `config_metronome` atomic | D-19 |
| Mute | `config_metronome_mute` atomic | D-19 |

These are already exposed as APVTS params (`metroVol`, `metroMute`). Phase 5 connects the metronome UI controls to these existing parameters.

---

## Footer Zone Layout (38px)

All non-Master strips share this footer layout.

```
+--[strip 100px]--+
| Pan Slider       |  <- 18px row, 4px top padding
| [  ----O----  ]  |
|                  |
| [  M  ] [  S  ] |  <- 18px row, 2px gap from pan
+------------------+
      2px bottom padding
```

| Row | Height | Contents | Padding |
|-----|--------|----------|---------|
| Pan row | 18px | Horizontal pan slider, 4px left/right margin | 4px top |
| Button row | 18px | Mute + Solo side-by-side, 2px gap between | 2px gap from pan |
| Bottom pad | 2px | Empty | -- |

Total: 4 + 18 + 2 + 18 + 2 = 44px... adjusted to fit in 38px:

Revised fit in 38px:
| Row | Height | Contents |
|-----|--------|----------|
| Pan row | 16px | Horizontal pan slider, 4px left/right margin |
| Gap | 2px | Separator |
| Button row | 16px | Mute (M) + Solo (S) side-by-side, 2px gap between |
| Bottom pad | 4px | Empty |

Total: 16 + 2 + 16 + 4 = 38px.

---

## Master Strip Footer Layout (38px)

```
+--[master 110px]--+
| Metro Fader       |  <- 16px row
| [===O-----------] |
|                   |
| [     MUTE      ] |  <- 16px row
+-------------------+
```

| Row | Height | Contents |
|-----|--------|----------|
| Metro fader row | 16px | Horizontal yellow fader, 4px left/right margin |
| Gap | 2px | Separator |
| Mute row | 16px | Full-width "MUTE" button |
| Bottom pad | 4px | Empty |

---

## Local Channel Expand/Collapse (D-12, D-13)

Local channels reuse the same expand/collapse group pattern as remote multi-channel users (already built in Phase 4 ChannelStripArea).

| State | Visual | Behavior |
|-------|--------|----------|
| Collapsed | Single "Local" strip showing Ch1 controls, "4ch" badge, expand arrow | Default state |
| Expanded | Parent "Local" strip + 3 child strips (Ch2, Ch3, Ch4) slide right | All 4 channels visible |

### Per-Child Strip Contents

| Element | Type | Details | Source |
|---------|------|---------|--------|
| Channel name | Label | "Ch1", "Ch2", "Ch3", "Ch4" | D-12 |
| Input selector | ComboBox | "In: Local 1" through "In: Local 4" (maps to `srcch`) | D-14 |
| Transmit toggle | TextButton | "TX" green when active, toggles `broadcast` via `SetLocalChannelInfo` | D-15 |
| VU meter | VuMeter | Same segmented LED as remote strips | UI-08 |
| Volume fader | VbFader | Same as remote (0.0-2.0 range) | UI-05 |
| Pan slider | Horizontal slider | Same as remote (-1.0 to +1.0) | UI-05 |
| Mute button | TextButton | "M" red when active | UI-05 |
| Solo button | TextButton | "S" yellow when active | UI-05 |

### Local Channel Data Binding

| Control | NJClient API | Direction |
|---------|-------------|-----------|
| Input selector | `SetLocalChannelInfo(ch, name, true, srcch, ...)` | UI -> Run thread |
| Transmit | `SetLocalChannelInfo(ch, name, ..., true, broadcast, ...)` | UI -> Run thread |
| Volume | `SetLocalChannelMonitoring(ch, true, vol, ...)` | UI -> Run thread |
| Pan | `SetLocalChannelMonitoring(ch, ..., true, pan, ...)` | UI -> Run thread |
| Mute | `SetLocalChannelMonitoring(ch, ..., true, mute, ...)` | UI -> Run thread |
| Solo | `SetLocalChannelMonitoring(ch, ..., solo)` | UI -> Run thread |
| VU level | `GetLocalChannelPeak(ch)` | Run thread -> UI (polled) |

---

## Remote Channel Data Binding

| Control | NJClient API | Direction |
|---------|-------------|-----------|
| Volume | `SetUserChannelState(useridx, channelidx, ..., true, vol, ...)` | UI -> Run thread |
| Pan | `SetUserChannelState(useridx, channelidx, ..., true, pan, ...)` | UI -> Run thread |
| Mute | `SetUserChannelState(useridx, channelidx, ..., true, mute, ...)` | UI -> Run thread |
| Solo | `SetUserChannelState(useridx, channelidx, ..., solo)` | UI -> Run thread |
| VU level | `GetUserChannelPeak(useridx, channelidx)` via cachedUsers | Run thread -> UI (polled) |

All remote mixer commands flow through `cmd_queue` as they did in Phase 4. The new command types needed:

| Command | Payload | Purpose |
|---------|---------|---------|
| `SetUserChannelStateCommand` | useridx, channelidx, vol, pan, mute, solo flags | Remote channel mixer control |
| `SetLocalChannelMonitoringCommand` | ch, vol, pan, mute, solo | Local channel monitoring |
| `SetLocalChannelInfoCommand` | ch, srcch, broadcast | Local input selector + transmit |

---

## State Persistence (APVTS)

### Existing Parameters (Phase 4, Already Defined)

| Parameter ID | Type | Range | Default |
|-------------|------|-------|---------|
| `masterVol` | float | 0.0-2.0 | 1.0 |
| `masterMute` | bool | -- | false |
| `metroVol` | float | 0.0-2.0 | 0.5 |
| `metroMute` | bool | -- | false |

### New Parameters (Phase 5 Adds)

| Parameter ID | Type | Range | Default | Source |
|-------------|------|-------|---------|--------|
| `localVol_0` through `localVol_3` | float | 0.0-2.0 | 1.0 | D-21 |
| `localPan_0` through `localPan_3` | float | -1.0 to 1.0 | 0.0 | D-21 |
| `localMute_0` through `localMute_3` | bool | -- | false | D-21 |
| `localTx_0` through `localTx_3` | bool | -- | true (Ch0), false (Ch1-3) | D-21 |
| `localSrcCh_0` through `localSrcCh_3` | int (choice) | 0-3 | 0, 1, 2, 3 respectively | D-21 |
| `uiScale` | float (choice) | 1.0, 1.5, 2.0 | 1.0 | D-23 |
| `chatVisible` | bool | -- | true | D-23 |

All parameter IDs use version suffix `1` (e.g., `juce::ParameterID{"localVol_0", 1}`), consistent with state version 1.

### Non-APVTS Persistent State

| State | Storage | Source |
|-------|---------|--------|
| Last server address | `juce::String` on Processor | D-22 (already exists) |
| Last username | `juce::String` on Processor | D-22 (already exists) |

Written to state XML in `getStateInformation()`, read back in `setStateInformation()`.

### NOT Persisted

| State | Reason | Source |
|-------|--------|--------|
| Remote user mixer settings | Users change between sessions | D-24 |
| Expanded group state | UI-only, recreated fresh | 04-UI-SPEC |
| Horizontal scroll offset | UI-only, recreated fresh | 04-UI-SPEC |

---

## Interaction Contracts (Phase 5 Additions)

### Fader Drag

1. `mouseDown()` on thumb: capture, store drag offset from thumb center
2. `mouseDrag()`: move thumb vertically, clamped to track bounds, update value continuously
3. `mouseUp()`: release capture
4. If click is on track but NOT on thumb: thumb jumps to click position immediately (no animation)

### Scroll Wheel on Strip

1. `mouseWheelMove()` override on `ChannelStrip` (not VbFader)
2. Delta mapped: 1 notch scroll = 0.5 dB change in fader value
3. Forward to that strip's VbFader `adjustByDb(+/-0.5)`
4. If strip has no fader (should not happen in Phase 5), ignore

### Double-Click Reset

1. `mouseDoubleClick()` on VbFader: reset to 1.0 (0 dB), 100ms animation
2. `mouseDoubleClick()` on pan slider: reset to 0.0 (center), instant snap
3. Double-click NOT intercepted on mute/solo buttons (they are toggle buttons)

### Solo State Machine

```
soloCount = count of strips where solo == true

For each strip during audio mixing:
  if soloCount == 0:
    strip plays normally (respects its own mute state)
  else if strip.solo == true:
    strip plays normally (respects its own mute state)
  else:
    strip is silent (audio * 0.0)
```

Solo state tracked as a simple counter. When any solo button toggles:
1. Update the strip's solo state
2. Increment/decrement `soloCount`
3. All strips re-evaluate their effective mute state on next audio callback

---

## VU Meter Integration (Phase 5 Completion)

Phase 4 built VuMeter and the 30Hz timer. Phase 5 ensures all channels report live levels.

| Channel Type | VU Source | Notes |
|-------------|-----------|-------|
| Remote | `cachedUsers[i].vu_left/vu_right` | Already wired in Phase 4 |
| Local | `GetLocalChannelPeak(ch)` | NEW: poll in 30Hz timer callback |
| Master | Master output level from processBlock | NEW: write to atomic, read in timer |

### Local VU Polling

Add to `ChannelStripArea::timerCallback()`:
```
For each local channel (0-3):
  float peak = client->GetLocalChannelPeak(ch);
  localStrip[ch].setVuLevels(peak, peak);  // mono, same L/R
  localStrip[ch].tickVu();
```

### Master VU

Master VU reads from a new atomic pair on the processor:
- `uiSnapshot.master_vu_left` (atomic float)
- `uiSnapshot.master_vu_right` (atomic float)

Written in `processBlock()` after final mix, read by 30Hz timer.

---

## Copywriting Contract (Phase 5 Additions)

| Element | Copy | Source |
|---------|------|--------|
| dB readout format | "-6.0", "+2.5", "0.0", "-inf" | D-05 |
| Mute button label | "M" | D-09 |
| Solo button label | "S" | D-09 |
| Metronome mute label | "MUTE" | D-18 |
| Transmit active | "TX" | Existing code |
| Local channel names | "Ch1", "Ch2", "Ch3", "Ch4" | D-12 |
| Local group badge | "4ch" | D-12 |
| Input selector items | "In: Local 1", "In: Local 2", "In: Local 3", "In: Local 4" | D-14 |
| Master strip label | "MASTER" | 04-UI-SPEC |

No new empty states or error states. Phase 5 operates entirely within the connected state where channel strips are already populated by Phase 4 logic.

---

## Registry Safety

This phase uses no package registries. All components are custom JUCE C++ code.

---

## Checker Sign-Off

- [ ] Dimension 1 Copywriting: PASS
- [ ] Dimension 2 Visuals: PASS
- [ ] Dimension 3 Color: PASS
- [ ] Dimension 4 Typography: PASS
- [ ] Dimension 5 Spacing: PASS
- [ ] Dimension 6 Registry Safety: PASS

**Approval:** pending

---

*Phase: 05-mixer-ui-and-channel-controls*
*Contract created: 2026-04-04*
*Foundation: 04-UI-SPEC.md (inherited, not duplicated)*
