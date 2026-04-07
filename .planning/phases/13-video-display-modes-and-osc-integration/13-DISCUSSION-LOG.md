# Phase 13: Video Display Modes and OSC Integration - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md -- this log preserves the alternatives considered.

**Date:** 2026-04-07
**Phase:** 13-video-display-modes-and-osc-integration
**Areas discussed:** Popout window mechanics, OSC video command design, Display mode switching, Multi-monitor workflow, Popout page architecture, TouchOSC template updates, WebSocket message for close/deactivate, Popout URL construction

---

## Popout Window Mechanics

### Popout Trigger

| Option | Description | Selected |
|--------|-------------|----------|
| Click roster pill | Clicking a user's name pill opens their video in a new window | ✓ |
| Dedicated popout button per user | Small popout icon button next to each roster pill | |
| Right-click context menu | Right-click on roster pill shows menu with 'Pop out' option | |

**User's choice:** Click roster pill
**Notes:** Natural interaction -- pill already represents the user and has data-stream-id from Phase 12.

### Popout Window Content

| Option | Description | Selected |
|--------|-------------|----------|
| Solo VDO.Ninja iframe | Single VDO.Ninja iframe showing only that user's stream | ✓ |
| Branded mini-companion | Full companion page in miniature with header and controls | |
| Raw VDO.Ninja URL | Open VDO.Ninja directly without companion wrapper | |

**User's choice:** Solo VDO.Ninja iframe
**Notes:** Minimal chrome, just the video with name label.

### Disconnect Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Show 'disconnected' overlay | Keep window open, show overlay. Resumes on reconnect. | ✓ |
| Auto-close the window | Close popout when user leaves | |
| Show placeholder, close after timeout | Show 'Disconnected' for 10 seconds then auto-close | |

**User's choice:** Show 'disconnected' overlay

### Multiple Popouts

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, unlimited | Each pill click opens a new window | ✓ |
| One at a time | New popout replaces previous | |
| Max 4 popouts | Cap at 4 simultaneous windows | |

**User's choice:** Yes, unlimited

---

## OSC Video Command Design

### Video Open/Close

| Option | Description | Selected |
|--------|-------------|----------|
| Toggle address | /JamWide/video/active float 1.0/0.0 | ✓ |
| Separate open/close addresses | /JamWide/video/open and /close | |
| Combined with mode argument | /JamWide/video/mode with string arg | |

**User's choice:** Toggle address

### OSC Popout Trigger

| Option | Description | Selected |
|--------|-------------|----------|
| By user index | /JamWide/video/popout/{idx} float trigger | ✓ |
| By username string | /JamWide/video/popout with string arg | |
| Popout all at once | /JamWide/video/popout/all trigger | |

**User's choice:** By user index

### OSC Video Scope

| Option | Description | Selected |
|--------|-------------|----------|
| Video active + popout only | Minimal: active toggle and popout triggers | ✓ |
| Full video control surface | Add quality and delay control | |
| Active + popout + quality | Add quality but not buffer delay | |

**User's choice:** Video active + popout only

### Popout Communication Wire

| Option | Description | Selected |
|--------|-------------|----------|
| New WebSocket message type | Plugin sends {type:'popout', streamId} via WS | ✓ |
| URL-based trigger | Plugin opens browser tab directly | |
| PostMessage to companion | Plugin sends postMessage to iframe | |

**User's choice:** New WebSocket message type

---

## Display Mode Switching

### Display Modes

| Option | Description | Selected |
|--------|-------------|----------|
| Grid + popout only | Default grid plus per-user popout windows | ✓ |
| Grid + spotlight + popout | Add spotlight mode for enlarged single user | |
| Grid + focus + popout | Focus mode hides all but one user in main window | |

**User's choice:** Grid + popout only

### Re-click Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Focus existing window | Bring existing popout to front | ✓ |
| Open duplicate window | Always open new window | |
| Toggle (close if open) | Click once to pop out, again to close | |

**User's choice:** Focus existing window

---

## Multi-Monitor Workflow

### Position Persistence

| Option | Description | Selected |
|--------|-------------|----------|
| No persistence | Open at browser default. Matches Phase 11 D-19. | ✓ |
| Remember per-user positions | localStorage keyed by streamId | |
| Remember generic positions | Store by slot index | |

**User's choice:** No persistence

### Window Features

| Option | Description | Selected |
|--------|-------------|----------|
| Minimal with name in title | No toolbar, no menubar, resizable, name in title bar | ✓ |
| Always-on-top option | Pin/unpin button for always-on-top | |
| Fullscreen toggle | Fullscreen button using Fullscreen API | |

**User's choice:** Minimal with name in title

---

## Popout Page Architecture

### Page Structure

| Option | Description | Selected |
|--------|-------------|----------|
| Separate popout.html page | New minimal page, reads params from URL | ✓ |
| Same page with query param | index.html?mode=popout with conditional rendering | |
| Inline data URL | Generate popout as data: or blob: URL | |

**User's choice:** Separate popout.html page

### Popout WebSocket

| Option | Description | Selected |
|--------|-------------|----------|
| No WS, opener notifies via postMessage | Main companion sends postMessage to popouts | ✓ |
| Each popout connects to WS | Each popout opens own WebSocket | |
| Fire and forget | No updates, VDO.Ninja handles blank streams | |

**User's choice:** No WS, opener notifies via postMessage

---

## TouchOSC Template Updates

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, add video section | Active toggle + 8 popout buttons in existing template | ✓ |
| Skip template update | Keep Phase 10 template as-is | |
| Separate video template | New .tosc file for video only | |

**User's choice:** Yes, add video section

---

## WebSocket Message for Close/Deactivate

| Option | Description | Selected |
|--------|-------------|----------|
| Send 'deactivate' WS message, companion closes popouts | Plugin sends {type:'deactivate'} before stopping WS | ✓ |
| Just stop the WS server | Let companion detect disconnect | |
| Plugin opens 'close all' browser URL | Trigger via URL | |

**User's choice:** Send 'deactivate' WS message, companion closes popouts

---

## Popout URL Construction

| Option | Description | Selected |
|--------|-------------|----------|
| VDO.Ninja &view= parameter | &view=streamId with same room/password/quality params | ✓ |
| Separate room per popout | Unique sub-room for each popout | |
| Embed stream URL directly | Direct stream URL format | |

**User's choice:** VDO.Ninja &view= parameter

---

## Claude's Discretion

- Exact CSS for disconnect overlay in popout windows
- postMessage protocol between main companion and popout windows
- Error handling for popup blockers
- TouchOSC template visual layout
- Whether popout.html needs its own Vite entry point

## Deferred Ideas

- Spotlight/focus display mode
- Always-on-top option for popout windows
- Popout window position persistence
- Fullscreen toggle in popouts
- OSC bandwidth profile control
- OSC buffer delay override
