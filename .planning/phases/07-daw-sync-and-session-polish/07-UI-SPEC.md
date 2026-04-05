---
phase: 7
slug: daw-sync-and-session-polish
status: draft
shadcn_initialized: false
preset: none
created: 2026-04-05
---

# Phase 7 -- UI Design Contract

> Interaction and component contract for DAW sync, BPM/BPI voting, session position tracking, and BPM change notification UI. This spec covers **only the new components and behaviors Phase 7 adds** to the existing Phase 6 mixer.
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
| Typography | 9/11/13/16 px, Regular + Bold | 04-UI-SPEC, existing code |
| Color tokens | 20+ tokens in `JamWideLookAndFeel.h` | 04-UI-SPEC, code |
| Strip dimensions | 100px wide, 106px pitch, 626px tall | 04-UI-SPEC |

**No new color tokens are introduced by Phase 7.** One new color use pattern is introduced: `kAccentConnect` pulsing animation for BPM/BPI change flash (D-10). All visual values use existing tokens defined in Phases 4 and 5.

---

## Spacing Scale (Inherited)

| Token | Value | Usage |
|-------|-------|-------|
| xs | 4px | Icon gaps, inline padding, SessionInfoStrip internal element gaps |
| sm | 8px | Compact element spacing, SessionInfoStrip label padding |
| md | 16px | Default element spacing, gap between info strip sections |
| lg | 24px | Section padding |
| xl | 32px | Layout gaps |

Exceptions: none for this phase.

---

## Typography (Inherited)

| Role | Size | Weight | Line Height | Usage in Phase 7 |
|------|------|--------|-------------|-------------------|
| Micro | 9px | Regular (400) | 1.2 | SessionInfoStrip secondary labels ("Intervals:", "Elapsed:", "Sync:") |
| Body | 11px | Regular (400) | 1.5 | SessionInfoStrip data values, BPM/BPI text labels on BeatBar, Sync button label |
| Label | 13px | Regular (400) | 1.5 | BPM/BPI inline edit TextEditor, system chat messages |
| Label Bold | 13px | Bold (700) | 1.5 | BPM/BPI display values on BeatBar (clickable) |

No new typography sizes are needed. The Sync button, BPM/BPI labels, inline edit, and SessionInfoStrip all use existing sizes. The BPM/BPI inline edit TextEditor uses 13px Regular, consistent with ConnectionBar text fields.

---

## Color (Inherited)

| Role | Value | Usage in Phase 7 |
|------|-------|-------------------|
| Dominant (60%) | `kBgPrimary` #1A1D2E | Background behind SessionInfoStrip, editor bg |
| Secondary (30%) | `kBgElevated` #222540, `kSurfaceStrip` #2A2D48 | ConnectionBar bg (Sync button home), SessionInfoStrip background |
| Accent (10%) | `kAccentConnect` #40E070 | Sync button text when sync is ACTIVE; BPM/BPI flash animation color |
| Warning | `kAccentWarning` #CCB833 | Sync button text when sync is WAITING (amber = "waiting to sync") |
| Destructive | `kAccentDestructive` #E04040 | Not used in Phase 7 new elements |
| System chat | `kChatSystem` #E64D4D | BPM/BPI change system messages in ChatPanel (existing token) |

Accent reserved for: Sync button text color when sync is ACTIVE (green = "synced"). BPM/BPI flash on change (2-3 second pulse to `kAccentConnect` then fade back to `kTextPrimary`).

---

## New Components

### 1. Sync Button

A new button added to `ConnectionBar`, placed between the Route button and the Codec selector (right-aligned section).

| Property | Value | Source |
|----------|-------|--------|
| Type | `juce::TextButton` | D-03 |
| Label | "Sync" | Concise, consistent with Route/Fit naming |
| Size | 44px wide x 28px tall | Proportional to Route (52px) and Fit (36px) buttons |
| Position | Between Route button and Codec selector in right-aligned section | D-03 |
| IDLE state | `kSurfaceStrip` fill, `kTextSecondary` text | Consistent with Fit/Route button inactive state |
| WAITING state | `kSurfaceStrip` fill, `kAccentWarning` text | Amber text = "waiting for host transport" |
| ACTIVE state | `kSurfaceStrip` fill, `kAccentConnect` text | Green text = "synced to host transport" |
| Border | 1px `kBorderSubtle`, `kBorderFocus` on hover | Consistent with all ConnectionBar buttons |
| Corner radius | 4px | Consistent with `drawButtonBackground` |
| Standalone visibility | **Hidden** (`setVisible(false)`) | D-07 |
| Tooltip | "Sync NINJAM intervals to DAW transport" | Guidance for plugin users |

#### Sync Button State Machine

```
IDLE ----[click, BPM valid]----> WAITING ----[transport starts]----> ACTIVE
  ^                                 |                                    |
  |                                 |                                    |
  +------[click cancels]------------+                                    |
  |                                                                      |
  +------[server BPM changes]--------------------------------------------|
  |                                                                      |
  +------[click disables]------------------------------------------------+
```

| State | Button Label | Text Color | Behavior |
|-------|-------------|------------|----------|
| IDLE | "Sync" | `kTextSecondary` #8888AA | Not synced. Click to initiate. |
| WAITING | "Sync" | `kAccentWarning` #CCB833 | Waiting for DAW play. Click to cancel. Silence is sent (D-04). |
| ACTIVE | "Sync" | `kAccentConnect` #40E070 | Synced. Click to disable. Audio aligned to DAW measures. |

#### Sync Button Interaction

| Interaction | Behavior | Source |
|-------------|----------|--------|
| Click (IDLE) | Validate BPM match (integer comparison). If match: push SyncCommand, transition to WAITING. If mismatch: show tooltip "Host BPM does not match server BPM" (no state change). | D-02, RESEARCH Pitfall 4 |
| Click (WAITING) | Cancel sync. Push SyncCancelCommand. Transition to IDLE. | Claude's discretion |
| Click (ACTIVE) | Disable sync. Push SyncDisableCommand. Transition to IDLE. | Claude's discretion |
| Server BPM changes (ACTIVE) | Auto-disable sync. Push SyncDisabledEvent to UI. Transition to IDLE. Show system chat: "[Server] Sync disabled -- BPM changed" | D-05 |
| Transport starts (WAITING) | processBlock detects edge, calculates PPQ offset, transitions to ACTIVE on audio thread. UI polls state. | D-02, RESEARCH Pattern 1 |

#### BPM Mismatch Feedback

When the user clicks Sync in IDLE state and the host BPM does not match the server BPM (integer comparison):

| Property | Value |
|----------|-------|
| Feedback type | `juce::BubbleMessageComponent` positioned below Sync button |
| Message | "Host tempo (N BPM) does not match server (M BPM)" |
| Duration | 3 seconds, auto-dismiss |
| Font | 11px Regular, `kTextPrimary` |
| Background | `kSurfaceOverlay` |
| Border | 1px `kBorderSubtle` |

---

### 2. BPM/BPI Vote UI (BeatBar Enhancement)

Two new clickable label areas on the BeatBar that allow inline BPM and BPI editing for voting.

| Property | Value | Source |
|----------|-------|--------|
| Location | Left end of BeatBar, before beat segments | D-08 |
| BPM label | "120" (current BPM, bold) | D-08 |
| BPI label | "16" (current BPI, bold) | D-08 |
| Label font | 13px Bold | Consistent with ConnectionBar bpmBpiLabel |
| Label color | `kTextPrimary` #E0E0E0 | Standard text |
| Separator | "/" character between BPM and BPI, 11px Regular, `kTextSecondary` | Compact visual separator |
| Total label area width | 72px (reserved from left side of BeatBar) | Fits "999/999" worst case |

#### BPM/BPI Label Layout

```
+-------+------------------------------------------------------------------+
| 120/16|[1][2][3][4][5][6][7][8][9][10][11][12][13][14][15][16]           |
+-------+------------------------------------------------------------------+
  ^72px^  ^--- Beat segments start at x=72 --->
```

| Sub-element | Width | Content | Alignment |
|-------------|-------|---------|-----------|
| BPM value | 36px | Current BPM, 13px Bold | Right-aligned |
| Separator "/" | ~8px | "/" , 11px Regular `kTextSecondary` | Centred |
| BPI value | 28px | Current BPI, 13px Bold | Left-aligned |

#### Inline Edit Interaction

| Interaction | Behavior | Source |
|-------------|----------|--------|
| Click on BPM value | Create `juce::TextEditor` overlay, 36px wide, positioned over BPM label. Pre-fill with current BPM. Select all text. | D-08, D-09 |
| Click on BPI value | Create `juce::TextEditor` overlay, 28px wide, positioned over BPI label. Pre-fill with current BPI. Select all text. | D-08, D-09 |
| Enter key | Validate input (BPM: 40-400, BPI: 2-192). If valid, push `SendChatCommand` with "!vote bpm N" or "!vote bpi N". Destroy TextEditor. Call `unfocusAllComponents()`. | D-09, RESEARCH Pitfall 5 |
| Escape key | Cancel edit. Destroy TextEditor. Call `unfocusAllComponents()`. | Claude's discretion |
| Click outside | Cancel edit. Destroy TextEditor. Call `unfocusAllComponents()`. | Claude's discretion |
| Invalid input | TextEditor border flashes `kAccentDestructive` for 500ms. TextEditor remains open. | Claude's discretion |

#### Inline Edit TextEditor Styling

| Property | Value |
|----------|-------|
| Font | 13px Regular |
| Background | `kSurfaceInput` #252840 |
| Text color | `kTextPrimary` #E0E0E0 |
| Border | 1px `kBorderFocus` #5A6080 (focused state) |
| Corner radius | 4px (standard TextEditor) |
| Max characters | 3 (BPM: "400", BPI: "192") |
| Input filter | Numeric only (0-9) |

---

### 3. BPM/BPI Change Flash Animation (BeatBar Enhancement)

When the server changes BPM or BPI, the BPM/BPI label area on the BeatBar flashes to draw attention.

| Property | Value | Source |
|----------|-------|--------|
| Trigger | BpmChangedEvent or BpiChangedEvent received from evt_queue | D-10 |
| Animation | BPM/BPI label text color transitions from `kTextPrimary` to `kAccentConnect` (#40E070) and back | D-10 |
| Duration | 2.5 seconds total: 200ms ramp to green, 2300ms fade back to white | D-10 ("2-3 seconds") |
| Implementation | Custom `paint()` interpolation using `juce::Time::getMillisecondCounterHiRes()` delta from flash start | Claude's discretion |
| Repaint trigger | 30Hz timer already exists on ChannelStripArea; BeatBar repaints on update() calls | Existing pattern |
| Chat notification | Concurrent: "[Server] BPM changed from X to Y" posted to chat_queue | D-11 |

#### Flash State

| Property | Type | Purpose |
|----------|------|---------|
| `flashStartMs_` | `double` | Timestamp when flash started. 0.0 = no flash active. |
| Flash active check | `(now - flashStartMs_) < 2500.0` | Determines if flash interpolation is needed |

Color interpolation: `alpha = 1.0 - ((now - flashStartMs_) / 2500.0)`. Text color = `kAccentConnect.interpolatedWith(kTextPrimary, 1.0 - alpha)`.

---

### 4. SessionInfoStrip (New Component)

A collapsible horizontal strip displayed below the BeatBar showing session tracking data.

| Property | Value | Source |
|----------|-------|--------|
| Type | `juce::Component` (custom) | D-13 |
| Position | Below BeatBar, above ChannelStripArea | D-13 |
| Height (expanded) | 20px | Compact single-line strip |
| Height (collapsed) | 0px (invisible, removed from layout) | D-15 |
| Default state | Hidden (collapsed) | D-15 |
| Background | `kVuBackground` #0A0D18 | Same as BeatBar background, visual continuity |
| Border | 1px `kBorderSubtle` bottom edge only | Subtle separator from mixer area below |

#### SessionInfoStrip Layout

```
+--------------------------------------------------------------------------+
| Intervals: 42  |  Elapsed: 12:34  |  Beat: 7/16  |  Sync: IDLE         |
+--------------------------------------------------------------------------+
```

| Section | Width | Content | Font | Color |
|---------|-------|---------|------|-------|
| Interval count | auto | "Intervals: {N}" | Label 9px `kTextSecondary`, Value 11px `kTextPrimary` | -- |
| Elapsed time | auto | "Elapsed: {mm:ss}" | Label 9px `kTextSecondary`, Value 11px `kTextPrimary` | -- |
| Beat position | auto | "Beat: {current}/{total}" | Label 9px `kTextSecondary`, Value 11px `kTextPrimary` | -- |
| Sync status | auto | "Sync: IDLE/WAITING/ACTIVE" | Label 9px `kTextSecondary`, Value 11px with state color | -- |

| Sync State | Value Text | Value Color |
|------------|-----------|-------------|
| IDLE | "IDLE" | `kTextSecondary` #8888AA |
| WAITING | "WAITING" | `kAccentWarning` #CCB833 |
| ACTIVE | "ACTIVE" | `kAccentConnect` #40E070 |

Sections separated by 16px gaps. Each section internally: label text (9px `kTextSecondary`) followed by 4px gap followed by value text (11px `kTextPrimary`). All content vertically centered in the 20px height.

#### SessionInfoStrip in Standalone Mode

| Property | Value | Source |
|----------|-------|--------|
| Sync section | Hidden (not rendered) | D-07: no sync concept in standalone |
| Remaining sections | Interval count, Elapsed time, Beat position visible | Still useful data |

#### SessionInfoStrip Toggle

| Property | Value | Source |
|----------|-------|--------|
| Toggle mechanism | Right-click context menu on BeatBar or ConnectionBar | Claude's discretion |
| Menu item | "Show Session Info" (checkmark when expanded) | Claude's discretion |
| State persistence | `infoStripVisible` ValueTree property (bool, default false) | D-21 |

The toggle is added to the existing right-click context menu on the ConnectionBar (which already has scale options). Adding it there keeps discoverability consistent with the existing scale factor toggle pattern.

---

## Editor Layout Update

Phase 7 inserts the SessionInfoStrip between the BeatBar and the ChannelStripArea. The layout constants update:

### Current Layout (Phase 6)

```
+----------------------------------------------------------------------+
|  ConnectionBar (44px)                                                |
+----------------------------------------------------------------------+
|  BeatBar (22px)                                                      |
+----------------------------------------------------------------------+
|  ChannelStripArea (remaining height)                                 |
+----------------------------------------------------------------------+
```

### Phase 7 Layout (info strip expanded)

```
+----------------------------------------------------------------------+
|  ConnectionBar (44px)                                                |
+----------------------------------------------------------------------+
|  BeatBar (22px)                                                      |
+----------------------------------------------------------------------+
|  SessionInfoStrip (20px)                                             |
+----------------------------------------------------------------------+
|  ChannelStripArea (remaining height - 20px)                          |
+----------------------------------------------------------------------+
```

### Phase 7 Layout (info strip collapsed -- DEFAULT)

```
+----------------------------------------------------------------------+
|  ConnectionBar (44px)                                                |
+----------------------------------------------------------------------+
|  BeatBar (22px)                                                      |
+----------------------------------------------------------------------+
|  ChannelStripArea (remaining height)                                 |
+----------------------------------------------------------------------+
```

Layout constants:

| Constant | Value | Condition |
|----------|-------|-----------|
| `kSessionInfoStripHeight` | 20 | When visible |
| Info strip Y position | `kConnectionBarHeight + kBeatBarHeight` = 66 | Fixed |
| ChannelStripArea Y | 66 (hidden) or 86 (visible) | Depends on info strip state |

---

## ConnectionBar Layout Update

Phase 7 inserts the Sync button into the ConnectionBar right-aligned section.

### Phase 6 Right-Aligned Section

```
... [userCountLabel] [bpmBpiLabel] ... gap ... [Fit 36px] [gap 6px] [Route 52px] [gap 6px] [Codec 80px]
```

### Phase 7 Right-Aligned Section

```
... [userCountLabel] [bpmBpiLabel] ... gap ... [Fit 36px] [gap 6px] [Sync 44px] [gap 6px] [Route 52px] [gap 6px] [Codec 80px]
```

#### ConnectionBar::resized() Right Section Update

| Element | Width | Gap After |
|---------|-------|-----------|
| Codec selector | 80px | -- (rightmost) |
| Route button | 52px | 6px |
| Sync button | 44px | 6px |
| Fit button | 36px | 6px |
| User count label | 70px | 6px |

Layout direction: right-to-left from `area.getRight()`. Same pattern as existing code. In standalone mode, the Sync button is hidden and its 50px (44px + 6px gap) is reclaimed.

---

## BeatBar Layout Update

Phase 7 adds a 72px label area to the left of the BeatBar for BPM/BPI display and inline voting.

### Current BeatBar Layout (Phase 6)

```
+----------------------------------------------------------------------+
|[1][2][3][4][5][6][7][8][9][10][11][12][13][14][15][16]               |
+----------------------------------------------------------------------+
```

### Phase 7 BeatBar Layout

```
+-------+--------------------------------------------------------------+
|120/16 |[1][2][3][4][5][6][7][8][9][10][11][12][13][14][15][16]       |
+-------+--------------------------------------------------------------+
 ^72px^  ^--- Beat segments now start at x=72
```

The BPM/BPI label area is painted in `BeatBar::paint()`. Beat segment width calculation changes from `getWidth() / bpi` to `(getWidth() - 72) / bpi` with segment X offset of 72.

---

## State Persistence (Phase 7 Additions)

### Non-APVTS Persistent State (New)

| State | Type | Default | Storage | Source |
|-------|------|---------|---------|--------|
| `infoStripVisible` | bool | false | ValueTree property | D-21 |

### NOT Persisted

| State | Reason | Source |
|-------|--------|--------|
| Sync state (IDLE/WAITING/ACTIVE) | Starts fresh each session | D-21 |
| Interval count | Runtime only, resets on reconnect | Claude's discretion |
| Elapsed session time | Runtime only, resets on reconnect | Claude's discretion |

---

## Interaction Contracts (Phase 7 Additions)

### Sync Button Click (Plugin Mode)

1. User clicks "Sync" button in ConnectionBar (IDLE state)
2. Plugin reads `cachedHostBpm_` and compares to server BPM (integer comparison)
3. If BPM matches: push `SyncCommand` to cmd_queue, Sync button transitions to WAITING (amber text)
4. If BPM mismatches: show BubbleMessageComponent with "Host tempo (N BPM) does not match server (M BPM)", no state change
5. In WAITING state: processBlock sends silence to NJClient (D-04)
6. DAW transport starts: processBlock detects stopped-to-playing edge, calculates PPQ offset, transitions to ACTIVE
7. Sync button text turns green (ACTIVE) on next UI poll
8. User clicks Sync again: transitions to IDLE, sync offset cleared

### BPM/BPI Vote

1. User clicks BPM value in BeatBar label area
2. TextEditor overlay appears, pre-filled with current BPM, all text selected
3. User types new BPM (numeric filter, max 3 chars)
4. User presses Enter
5. Input validated: 40-400 for BPM, 2-192 for BPI
6. If valid: push `SendChatCommand` with "!vote bpm N" to cmd_queue, destroy TextEditor
7. If invalid: border flashes red 500ms, TextEditor stays open
8. `unfocusAllComponents()` called on dismiss to return keyboard focus to DAW

### BPM/BPI Change Notification

1. NinjamRunThread detects BPM or BPI change (compare current vs cached)
2. Pushes `BpmChangedEvent` to evt_queue
3. Pushes system chat message "[Server] BPM changed from X to Y" to chat_queue (D-11)
4. Editor drains evt_queue, sets `flashStartMs_` on BeatBar
5. BeatBar label text animates green-to-white over 2.5 seconds (D-10)
6. If sync was ACTIVE: auto-disable, push `SyncDisabledEvent`, chat message "[Server] Sync disabled -- BPM changed" (D-05)

### SessionInfoStrip Toggle

1. User right-clicks ConnectionBar (or BeatBar)
2. Context menu shows "Show Session Info" with checkmark if currently visible
3. User toggles item
4. `infoStripVisible` flipped, editor calls `resized()` to redistribute layout
5. SessionInfoStrip height transitions between 0px and 20px immediately (no animation)
6. Value persisted via ValueTree property (D-21)

---

## Copywriting Contract (Phase 7 Additions)

| Element | Copy | Source |
|---------|------|--------|
| Sync button label | "Sync" | D-03, Claude's discretion |
| Sync button tooltip | "Sync NINJAM intervals to DAW transport" | Claude's discretion |
| BPM mismatch bubble | "Host tempo ({N} BPM) does not match server ({M} BPM)" | Claude's discretion |
| Sync disabled chat msg | "[Server] Sync disabled -- BPM changed" | D-05 |
| BPM changed chat msg | "[Server] BPM changed from {X} to {Y}" | D-11 |
| BPI changed chat msg | "[Server] BPI changed from {X} to {Y}" | D-11 (analogous) |
| Info strip toggle menu | "Show Session Info" | Claude's discretion |
| Info strip labels | "Intervals:", "Elapsed:", "Beat:", "Sync:" | D-12 |
| Info strip sync values | "IDLE", "WAITING", "ACTIVE" | D-02, Claude's discretion |
| Vote validation ranges | BPM: 40-400, BPI: 2-192 | RESEARCH (JamTaba constants) |

### Empty States

| Context | Copy | Source |
|---------|------|--------|
| SessionInfoStrip (not connected) | "Intervals: --  Elapsed: --  Beat: --/--" | Claude's discretion |
| Sync status (standalone) | Hidden entirely | D-07 |

The SessionInfoStrip shows placeholder dashes when not connected. When connected, all values populate from NJClient data immediately.

### Error States

| Context | Copy | Source |
|---------|------|--------|
| BPM mismatch on sync attempt | Bubble: "Host tempo ({N} BPM) does not match server ({M} BPM)" | D-02 |
| Invalid vote input | TextEditor border flash (red, 500ms), no text feedback | Claude's discretion |
| Host provides no PPQ data | Sync button disabled with tooltip: "Host does not provide position data" | RESEARCH Pitfall 1 |

### Destructive Actions

Phase 7 has no destructive actions. Disabling sync does not interrupt audio (remote audio continues, local audio only stops broadcasting during WAITING/ACTIVE per D-01). Voting is a non-destructive server request.

---

## Component Inventory Summary

| Component | Status | Changes |
|-----------|--------|---------|
| `ConnectionBar` | Modify | Add `syncButton` member, update `resized()` layout, add sync click handler with BPM validation |
| `BeatBar` | Modify | Add 72px BPM/BPI label area (left side), inline TextEditor overlay for voting, flash animation state |
| `SessionInfoStrip` | **New** | 20px collapsible strip: interval count, elapsed time, beat position, sync status |
| `JamWideJuceEditor` | Modify | Add `SessionInfoStrip` member, update `resized()` for conditional info strip height, add info strip toggle to context menu, drain sync events |
| `JamWideJuceProcessor` | Modify | Add `syncWaiting_`/`syncActive_` atomics, `cachedHostBpm_` atomic, AudioPlayHead query in processBlock |
| `NinjamRunThread` | Modify | Expose `m_loopcnt` and `GetSessionPosition()` via uiSnapshot, detect BPM/BPI changes and push events |
| `JamWideLookAndFeel` | No change | All colors and overrides already exist |

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

*Phase: 07-daw-sync-and-session-polish*
*Contract created: 2026-04-05*
*Foundation: 04-UI-SPEC.md + 05-UI-SPEC.md + 06-UI-SPEC.md (inherited, not duplicated)*
