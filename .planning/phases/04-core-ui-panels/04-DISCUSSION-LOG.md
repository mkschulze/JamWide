# Phase 4: Core UI Panels - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-04
**Phase:** 04-core-ui-panels
**Areas discussed:** Panel layout, Visual design, Server browser flow, Chat panel scope, License dialog, Window sizing & resizing, Password field behavior, Disconnected state UI

---

## Panel Layout

| Option | Description | Selected |
|--------|-------------|----------|
| Mixer-first (VB-Audio style) | Always-visible layout. Channel strips with VU meters, chat docked right, server browser as overlay. | ✓ |
| Session-focused (chat-centric) | Chat dominates center, users as compact sidebar. |  |
| Tabbed panels | Tabs across top, one panel visible at a time. |  |

**User's choice:** Mixer-first (VB-Audio style)
**Notes:** User specifically referenced VB-Audio Voicemeeter Banana and provided a screenshot. Wants users and chat as the main focus with "big faders like in the VB audio."

---

## Visual Design

| Option | Description | Selected |
|--------|-------------|----------|
| Full custom LookAndFeel | Dark theme, custom-drawn components, SVG assets via Sketch MCP. Every component styled. | ✓ |
| Themed stock JUCE | Custom dark ColorScheme on stock JUCE. Custom-draw only VU meters and channel strips. |  |
| Stock JUCE, style later | Default JUCE look, no customization. Purely functional. |  |

**User's choice:** Full custom LookAndFeel
**Notes:** VB-Audio Voicemeeter Banana is the primary design reference.

---

## Server Browser Flow

| Option | Description | Selected |
|--------|-------------|----------|
| Click fills, double-click connects | Single click fills address, double-click auto-connects. | ✓ |
| Click connects directly | Single click immediately connects. |  |
| Browse-only, manual connect | Browser is purely informational. |  |

**User's choice:** Click fills, double-click connects

**Server info to display (multi-select):**

| Option | Selected |
|--------|----------|
| Server name + address | ✓ |
| User count | ✓ |
| BPM / BPI | ✓ |
| Topic / description | ✓ |

**Notes:** All info fields selected.

---

## Chat Panel Scope

**Message types (multi-select):**

| Option | Selected |
|--------|----------|
| Chat messages (MSG) | ✓ |
| Join/Part notifications | ✓ |
| Topic/server messages | ✓ |
| Private messages (PRIVMSG) | ✓ |

**Chat scroll behavior:**

| Option | Description | Selected |
|--------|-------------|----------|
| Auto-scroll, manual pause | Auto-scrolls to newest. Scrolling up pauses. Jump-to-bottom indicator. | ✓ |
| Always auto-scroll | Always jumps to newest, can't scroll back. |  |
| Manual scroll only | No auto-scroll. |  |

**User's choice:** All message types, auto-scroll with manual pause
**Notes:** Standard chat UX with jump-to-bottom.

---

## License Dialog

| Option | Description | Selected |
|--------|-------------|----------|
| Modal popup | Dark-themed modal with Accept/Decline. Blocks session. | ✓ |
| Inline banner | Dismissible banner in chat area. |  |
| Auto-accept with log | Keep auto-accept, log in chat. |  |

**User's choice:** Modal popup
**Notes:** Consistent with standard NINJAM client behavior.

---

## Window Sizing & Resizing

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed size (VB-Audio style) | Fixed window, pixel-perfect. |  |
| Resizable with constraints | Resizable with minimum size, panels reflow. |  |
| Fixed with scale options | Fixed layout with 1x/1.5x/2x scale. | ✓ |

**User's choice:** Fixed with scale options
**Notes:** Matches VB-Audio approach — fixed layout, scale for HiDPI.

---

## Password Field Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Always visible, compact | Small field always in connection bar. |  |
| Show on demand | Hidden by default, toggle to reveal. | ✓ |
| You decide | Claude picks. |  |

**User's choice:** Show on demand
**Notes:** Cleaner connection bar for the common case (public servers).

---

## Disconnected State UI

| Option | Description | Selected |
|--------|-------------|----------|
| Empty strips + prompt | Dimmed placeholders, welcome message, Browse Servers button. | ✓ |
| Full layout, just empty | Same layout, blank channel area. |  |
| Server browser auto-opens | Server browser overlay auto-opens on launch. |  |

**User's choice:** Empty strips + prompt
**Notes:** User reviewed ASCII preview and confirmed.

---

## Claude's Discretion

- Exact fixed window dimensions
- SVG asset design specifics and color values
- Chat message formatting details
- VU meter update rate and visual style
- Scale option UI placement
- Event queue consumption strategy
- Connection bar field widths and spacing

## Deferred Ideas

- **DAW sync from JamTaba** — Phase 7 scope. User has local JamTaba clone at `/Users/cell/dev/JamTaba`.
- **VDO.ninja video integration** — Future phase. User wants VDO.ninja specifically as the video solution.
- **Timing Guide removal** — In-scope cleanup (not deferred).
