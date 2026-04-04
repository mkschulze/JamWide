---
phase: 6
slug: multichannel-output-routing
status: draft
shadcn_initialized: false
preset: none
created: 2026-04-04
---

# Phase 6 -- UI Design Contract

> Interaction and component contract for multichannel output routing UI. This spec covers **only the new components and behaviors Phase 6 adds** to the existing Phase 5 mixer.
>
> **Foundation:** All spacing, typography, color tokens, layout structure, strip dimensions, fader interactions, and window dimensions are inherited from [04-UI-SPEC.md](../04-core-ui-panels/04-UI-SPEC.md) and [05-UI-SPEC.md](../05-mixer-ui-and-channel-controls/05-UI-SPEC.md). This document does not duplicate them.
>
> **Platform:** JUCE 8.0.12 (C++ desktop plugin / standalone). All visual values consumed by `JamWideLookAndFeel` and custom `paint()` overrides.

---

## Design System (Inherited)

| Property | Value | Source |
|----------|-------|--------|
| Tool | JUCE LookAndFeel_V4 (`JamWideLookAndFeel`) | 04-UI-SPEC |
| Spacing | 4/8/16/24/32 scale | 04-UI-SPEC |
| Typography | 9/11/13 px, Regular + Bold | 04-UI-SPEC, existing code |
| Color tokens | 20+ tokens in `JamWideLookAndFeel.h` | 04-UI-SPEC, code |
| Strip dimensions | 100px wide, 106px pitch, 626px tall | 04-UI-SPEC |

**No new spacing tokens, typography sizes, or color tokens are introduced by Phase 6.** All visual values use existing tokens defined in Phases 4 and 5.

---

## Spacing Scale (Inherited)

| Token | Value | Usage |
|-------|-------|-------|
| xs | 4px | Icon gaps, inline padding, fader/VU margin |
| sm | 8px | Compact element spacing, ConnectionBar area.reduced() |
| md | 16px | Default element spacing, section gap between status and right controls |
| lg | 24px | Section padding |
| xl | 32px | Layout gaps |

Exceptions: none for this phase.

---

## Typography (Inherited)

| Role | Size | Weight | Line Height | Usage in Phase 6 |
|------|------|--------|-------------|-------------------|
| Micro | 9px | Regular (400) | 1.2 | Build rev, dB tick labels |
| Body | 11px | Regular (400) | 1.5 | Channel names, codec labels, routing selector text |
| Label | 13px | Regular (400) | 1.5 | ConnectionBar fields, status labels, route button text |
| Label Bold | 13px | Bold (700) | 1.5 | Status label, BPM/BPI label |

No new typography sizes are needed. The route button and dropdown use 13px Regular, consistent with ConnectionBar controls (Connect, Browse, Fit, Codec).

---

## Color (Inherited)

| Role | Value | Usage in Phase 6 |
|------|-------|-------------------|
| Dominant (60%) | `kBgPrimary` #1A1D2E | Background behind strips, editor bg |
| Secondary (30%) | `kBgElevated` #222540, `kSurfaceStrip` #2A2D48 | ConnectionBar bg, strip backgrounds |
| Accent (10%) | `kAccentConnect` #40E070 | Route button text when routing is active (green = "routing enabled") |
| Warning | `kAccentWarning` #CCB833 | Not used in Phase 6 new elements |
| Destructive | `kAccentDestructive` #E04040 | Not used in Phase 6 new elements |

Accent reserved for: Route button text color when routing mode is active (by-user or by-channel). When routing mode is Manual (default), the route button text uses `kTextSecondary` (same as Fit button inactive state).

---

## New Components

### 1. Route Button (Quick-Assign)

A new button added to `ConnectionBar`, placed between the Fit button and the Codec selector (right-aligned section).

| Property | Value | Source |
|----------|-------|--------|
| Type | `juce::TextButton` with popup menu | D-02, Claude's discretion |
| Label | "Route" | Concise, fits ConnectionBar density |
| Size | 52px wide x 28px tall | Proportional to Fit (36px) and Codec (80px) buttons |
| Position | Between Fit button and Codec selector in right-aligned section | D-02 |
| Inactive state (Manual mode) | `kSurfaceStrip` fill, `kTextSecondary` text | Consistent with Fit button inactive |
| Active state (by-user or by-channel) | `kSurfaceStrip` fill, `kAccentConnect` text | Green text indicates routing is active |
| Border | 1px `kBorderSubtle`, `kBorderFocus` on hover | Consistent with all ConnectionBar buttons |
| Corner radius | 4px | Consistent with `drawButtonBackground` |

#### Route Button Interaction

| Interaction | Behavior | Source |
|-------------|----------|--------|
| Left-click | Opens popup menu below the button | D-02 |
| Menu items | 3 items: "Manual (Main Mix)", "Assign by User", "Assign by Channel" | D-02 |
| Menu check mark | Current routing mode has a check mark | Standard PopupMenu behavior |
| Menu font | 13px Regular, `kTextPrimary` | Consistent with PopupMenu styling |
| Menu background | `kSurfaceOverlay` | Existing PopupMenu LookAndFeel |
| Menu highlight | `kSurfaceStrip` bg, `kTextHeading` text | Existing PopupMenu LookAndFeel |

#### Route Button Label State

| Routing Mode | Button Label | Text Color |
|-------------|-------------|------------|
| Manual (default) | "Route" | `kTextSecondary` #8888AA |
| By User (active) | "Route" | `kAccentConnect` #40E070 |
| By Channel (active) | "Route" | `kAccentConnect` #40E070 |

The button label remains "Route" in all modes. The text color change provides a subtle visual indicator that multichannel routing is active. No label change is needed because the routing mode is visible in the per-strip routing selectors and in the dropdown check mark.

#### Route Button Popup Menu Detail

```
+------------------------------+
|  * Manual (Main Mix)         |
|    Assign by User            |
|    Assign by Channel         |
+------------------------------+
```

| Menu Item | Action | Source |
|-----------|--------|--------|
| "Manual (Main Mix)" | Sets routing mode to 0. All existing user `out_chan_index` reset to 0 (Main Mix). Future users route to Main Mix. | D-01, D-13 |
| "Assign by User" | Sets routing mode to 2. Iterates all connected users and assigns each user to a sequential stereo bus (one bus per user). Future users auto-assigned. | D-02, D-03 |
| "Assign by Channel" | Sets routing mode to 1. Iterates all connected users and assigns each channel to its own sequential stereo bus. Future users auto-assigned. | D-02 |

All three actions push a `SetRoutingModeCommand` through the SPSC command queue. The sweep of existing users happens on the Run thread, not the message thread.

---

### 2. Routing Selector Update (Existing Component)

The routing selector ComboBox already exists on each Remote and RemoteChild `ChannelStrip` from Phase 4. Phase 6 updates its behavior and labeling.

#### Current State (Phase 4)

| Property | Current Value |
|----------|--------------|
| Items | "Out 1/2" through "Out 16" (16 items) |
| Item IDs | 1 through 16 |
| Default selected | ID 1 ("Out 1/2") |
| Visible on | Remote, RemoteChild strips |

#### Phase 6 Changes

| Property | New Value | Source |
|----------|-----------|--------|
| Prepend item | "Main Mix" as item ID 1 (index 0) | D-01: default is Main Mix |
| Existing items shift | "Remote 1" through "Remote 16" as item IDs 2-17 | Clarity: "Remote N" instead of "Out N/N+1" |
| Default selected | ID 1 ("Main Mix") | D-01, D-13 |
| Tooltip | "Output bus routing" | Minimal guidance |

#### Revised Routing Selector Items

| Item ID | Label | Maps to bus index | Channel pair |
|---------|-------|------------------|--------------|
| 1 | "Main Mix" | 0 | [0, 1] |
| 2 | "Remote 1" | 1 | [2, 3] |
| 3 | "Remote 2" | 2 | [4, 5] |
| ... | ... | ... | ... |
| 17 | "Remote 16" | 16 | [32, 33] |

The label change from "Out 1/2" to "Remote 1" is clearer for the user: "Remote 1" means "the first dedicated output bus for remote audio." The bus 0 is now explicitly labeled "Main Mix" rather than being implied by "Out 1/2".

#### Routing Selector Visual Feedback

| State | Visual Change |
|-------|---------------|
| Routed to Main Mix (default) | Standard ComboBox appearance, `kTextPrimary` text | 
| Routed to Remote 1-16 | ComboBox text color changes to `kAccentConnect` (#40E070) | 

The green text on a non-default routing selection provides an at-a-glance indicator of which strips have been routed to dedicated buses.

#### Routing Selector Interaction

| Interaction | Behavior | Source |
|-------------|----------|--------|
| User changes selector | Push `SetUserChannelStateCommand` with `set_outch=true, outchannel=(busIndex*2)` through cmd_queue | D-04 |
| Quick-assign overwrites | When Route button triggers assign-by-user or assign-by-channel, all routing selectors update to reflect new assignments | D-02, Pitfall 4 |
| Manual mode resets | When Route button switches to "Manual (Main Mix)", all selectors reset to "Main Mix" | D-01 |
| User leaves | Selector is destroyed with the strip. Bus remains reserved per D-05 (no other strip moves). New users get Main Mix per D-06 if all 16 are occupied. | D-05, D-06 |

---

### 3. DAW Tooltip Label

A one-line tooltip shown on the Route button to guide DAW users to enable additional outputs.

| Property | Value | Source |
|----------|-------|--------|
| Tooltip target | Route button | D-15 |
| Tooltip text | "Enable additional outputs in your DAW's plugin I/O settings" | D-15, exact copy |
| Font | Default JUCE tooltip (13px) | Standard |
| Background | `kSurfaceOverlay` | Consistent with PopupMenu |

This is a standard JUCE tooltip set via `routeButton.setTooltip(...)`. No custom tooltip component needed.

---

## ConnectionBar Layout Update

Phase 6 inserts the Route button into the ConnectionBar right-aligned section. The revised layout:

### Current Right-Aligned Section (Phase 5)

```
... [userCountLabel] [bpmBpiLabel] ... gap ... [Fit 36px] [gap 6px] [Codec 80px]
```

### Phase 6 Right-Aligned Section

```
... [userCountLabel] [bpmBpiLabel] ... gap ... [Fit 36px] [gap 6px] [Route 52px] [gap 6px] [Codec 80px]
```

#### ConnectionBar::resized() Right Section Update

| Element | Width | Gap After |
|---------|-------|-----------|
| Codec selector | 80px | -- (rightmost) |
| Route button | 52px | 6px |
| Fit button | 36px | 6px |
| User count label | 70px | 6px |

Layout direction: right-to-left from `area.getRight()`. Same pattern as existing code.

---

## State Persistence (Phase 6 Additions)

### Non-APVTS Persistent State (New)

| State | Type | Default | Storage | Source |
|-------|------|---------|---------|--------|
| `routingMode` | int | 0 (Manual) | ValueTree property | D-12, D-13 |

Written in `getStateInformation()`, restored in `setStateInformation()`. On restore, the routing mode is applied via command queue on next connect.

### NOT Persisted

| State | Reason | Source |
|-------|--------|--------|
| Individual user-to-bus mappings | Users change between sessions | D-12 |
| Per-strip routing selector positions | Derived from runtime routing state | D-12 |

---

## Interaction Contracts (Phase 6 Additions)

### Route Button Click

1. User clicks "Route" button in ConnectionBar
2. PopupMenu appears below the button with 3 items
3. Current routing mode has a check mark (tick)
4. User selects a mode
5. `SetRoutingModeCommand` pushed to cmd_queue
6. Run thread processes command:
   - Sets `config_remote_autochan` and `config_remote_autochan_nch`
   - Sweeps all existing users to reassign `out_chan_index`
7. Next UI poll cycle: routing selectors on all strips update to reflect new assignments
8. Route button text color updates (green if non-manual, secondary if manual)

### Per-Strip Routing Override

1. User changes routing selector on a specific ChannelStrip
2. `SetUserChannelStateCommand` pushed with `set_outch=true`
3. Run thread sets `out_chan_index` on that specific channel
4. This override persists until next quick-assign or manual reset
5. Source: D-04

### Bus Overflow (Silent Fallback)

1. All 16 Remote buses are occupied by existing users
2. A new user joins the session
3. New user is assigned `out_chan_index = 0` (Main Mix)
4. No error dialog, no warning toast, no visual indicator
5. User can manually reassign if they want
6. Source: D-06

### User Departure

1. Remote user disconnects
2. Their ChannelStrip is removed from the UI
3. Their bus assignment is NOT automatically freed or reassigned to other users
4. The bus "stays reserved" (in practice: no other user's `out_chan_index` changes)
5. Next quick-assign operation will reassign all buses fresh
6. Source: D-05

---

## Routing Selector Refresh from Snapshot

The routing selector on each strip must reflect the current `out_chan_index` value for that channel. This is read from the UI snapshot (polled at 10Hz via the existing timer).

| Snapshot Field | Maps To |
|----------------|---------|
| `RemoteChannelInfo::out_chan_index` (NEW) | Routing selector selected item |

Conversion: `out_chan_index / 2 + 1` gives the ComboBox item ID (1 = Main Mix at index 0, 2 = Remote 1 at index 2, etc.).

The refresh happens in `ChannelStripArea::refreshFromUsers()` during the existing 30Hz timer callback. When the `out_chan_index` for a channel changes (via quick-assign or manual command), the selector updates on the next poll cycle.

---

## Copywriting Contract (Phase 6 Additions)

| Element | Copy | Source |
|---------|------|--------|
| Route button label | "Route" | Claude's discretion |
| Route menu item 1 | "Manual (Main Mix)" | D-01 |
| Route menu item 2 | "Assign by User" | D-02 |
| Route menu item 3 | "Assign by Channel" | D-02 |
| Routing selector default | "Main Mix" | D-01, D-13 |
| Routing selector items | "Remote 1" through "Remote 16" | Claude's discretion (improved from "Out 1/2") |
| Route button tooltip | "Enable additional outputs in your DAW's plugin I/O settings" | D-15, exact |

### No Empty States

Phase 6 operates within the connected state where channel strips are already populated. The routing selector defaults to "Main Mix" which is a valid, functional state -- not an "empty" state.

### No Error States

Routing failures are handled silently: bus overflow falls back to Main Mix (D-06). No error messages or dialogs are needed in the UI.

### No Destructive Actions

Switching routing modes is non-destructive: audio continues flowing. Switching to "Manual (Main Mix)" resets all routing to the default. No confirmation dialogs are needed because:
- Audio is never interrupted (Main Mix always contains everything per D-08)
- Individual bus routing is not persisted (D-12), so there is nothing to "lose"

---

## Component Inventory Summary

| Component | Status | Changes |
|-----------|--------|---------|
| `ConnectionBar` | Modify | Add `routeButton` member, update `resized()` layout, add popup menu handler |
| `ChannelStrip` | Modify | Update `routingSelector` items (add "Main Mix", rename "Out N" to "Remote N"), add green text feedback |
| `ChannelStripArea` | Modify | Wire `onRoutingChanged` callback to push `SetUserChannelStateCommand` with `set_outch`, refresh selector from snapshot |
| `JamWideLookAndFeel` | No change | All colors and overrides already exist |
| `JamWideJuceProcessor` | Modify | Add `routingMode` state, expand `processBlock` output buffer, state persistence |

No new visual components are created. All UI changes use existing component types (TextButton, ComboBox, PopupMenu) with existing LookAndFeel styling.

---

## Registry Safety

This phase uses no package registries. All components are custom JUCE C++ code.

| Registry | Blocks Used | Safety Gate |
|----------|-------------|-------------|
| N/A | N/A | Not applicable -- native C++ project |

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

*Phase: 06-multichannel-output-routing*
*Contract created: 2026-04-04*
*Foundation: 04-UI-SPEC.md + 05-UI-SPEC.md (inherited, not duplicated)*
