# Audio Prelisten — Design Spec

**Date:** 2026-04-13
**Status:** Approved
**Approach:** Reuse main NJClient with mode switch (Approach B)

## Overview

Allow users to hear what's happening in a NINJAM server room before joining. A "Listen" button in the server browser connects the main NJClient in receive-only mode — zero local channels, auto-accept license, `justmonitor=true`. One room at a time. Audio mixes into the plugin output with a dedicated volume control.

## User Experience

### Trigger
Each populated server row in the server browser gets a **Listen** button (play icon + "Listen"). Empty rooms (0 users) have no button. Clicking "Listen" starts a background connection to that server. Clicking another room's "Listen" stops the current and switches. Clicking "Stop" disconnects.

### Visual Feedback
- **Active row**: blue-tinted background, "LISTENING" badge next to server name, animated left-edge bar (3px, blue gradient, pulsing)
- **Volume slider**: appears in the server browser title bar (right side, next to Refresh) only while prelistening. Small horizontal slider labeled "VOL".
- **Idle rows**: subtle Listen button (play icon, muted colors)

### Audio Output
Prelisten audio mixes into the same plugin output as a normal session. A `prelisten_volume` control (0.0–1.0, default 0.7) scales the mixed remote audio before it reaches the output buffer. This works in both VST3/plugin and standalone modes.

### One at a Time
Only one prelisten connection at a time. Clicking Listen on a new room disconnects the previous one first. This keeps resource usage minimal and avoids audio confusion.

## Architecture

### Approach: Reuse Main NJClient with Mode Switch

The existing `NJClient` instance connects normally through the full NINJAM handshake but in a restricted mode:
- Zero local channels (nothing to send)
- `justmonitor = true` in `process_samples()` (skips all encoding/transmission)
- `config_autosubscribe = 1` (subscribe to all remote channels automatically)
- License auto-accepted (callback returns 1 immediately)
- Username prefixed with `[preview]` to signal listen-only intent to other users

**Trade-off accepted:** Cannot prelisten while already in a session — must disconnect first. This is acceptable because users browse servers before joining, not during an active jam. If the user is already connected and clicks Listen, the prelisten is blocked (button disabled or shows tooltip explaining why).

### Connection State Machine

```
DISCONNECTED
    │
    ├── Click Listen ──► PRELISTENING
    │                       │
    │                       ├── Click another Listen ──► Disconnect + Reconnect (prelisten)
    │                       ├── Click Connect ──► Disconnect + Reconnect (full session)
    │                       ├── Click Stop / Close browser ──► DISCONNECTED
    │                       │
    ├── Click Connect ──► CONNECTING (normal auth flow)
```

### Audio Routing (Prelisten)

```
Server Room Audio (TCP)
    │
    ▼
NJClient.AudioProc(justmonitor=true)
    │
    ├── Decode Vorbis/FLAC per subscribed channel
    ├── Mix all remote channels together
    │
    ▼
processBlock()
    │
    ├── Scale by prelisten_volume (0.0–1.0)
    │
    ▼
DAW output / Standalone speakers
```

## Components & File Changes

### Modified Files

| File | Change |
|------|--------|
| `juce/JamWideJuceProcessor.h` | Add `std::atomic<bool> prelisten_mode{false}`, `std::atomic<float> prelisten_volume{0.7f}` |
| `juce/JamWideJuceProcessor.cpp` | In `processBlock()`: when `prelisten_mode`, scale output by `prelisten_volume` |
| `juce/NinjamRunThread.cpp` | Check `prelisten_mode` on connect: auto-accept license callback, skip local channel setup, set `config_autosubscribe = 1`, prefix username with `[preview]` |
| `juce/ui/ServerBrowserOverlay.h` | Add Listen/Stop button component per row, volume slider, active row index tracking, listening state |
| `juce/ui/ServerBrowserOverlay.cpp` | Render Listen/Stop buttons in `paintListBoxItem()`, add volume slider to title bar (visible only when prelistening), handle button clicks, active row highlight with blue tint and "LISTENING" badge |
| `src/threading/ui_command.h` | Add `PrelistenCommand { std::string host; int port; }` and `StopPrelistenCommand` |
| `src/threading/ui_event.h` | Add `PrelistenStateEvent { bool connected; std::string server_name; }` |
| `juce/JamWideJuceEditor.cpp` | Wire prelisten commands from browser overlay callbacks, drain `PrelistenStateEvent` to update browser overlay state |

### Unchanged
- `NJClient` core (`njclient.h/.cpp`) — no modifications needed
- Protocol layer (`mpb.h`, `netmsg.h`) — no protocol changes
- Audio codec path — same Vorbis/FLAC decode pipeline
- Remote user management — same subscription mechanism

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Server unreachable / timeout | Show brief error in status label ("Connection failed"), clear listening state after 10s timeout |
| Server rejects zero-channel client | Fall back to creating a single silent local channel (send silence) |
| Server requires password | Hide Listen button for that row (or show lock icon) |
| Already in a session | Disable Listen buttons (tooltip: "Disconnect first to preview rooms"). The browser overlay checks `processor.client->GetStatus() == NJC_STATUS_OK && !prelisten_mode` to detect an active non-prelisten session. |
| Server list refreshes while prelistening | Maintain prelisten connection; update row UI to match new list position |

## Visibility

With zero local channels, the NINJAM server still registers the client as a user. Other users in the room may see `[preview]username` with 0 channels. This is protocol-level — true invisibility would require server-side changes. The `[preview]` prefix communicates intent clearly.

If the server rejects zero-channel clients, the fallback creates one silent local channel. This means the user appears as a normal participant but sends silence.

## Scope Boundaries

**In scope:**
- Listen/Stop button per populated server row
- One-at-a-time prelisten via main NJClient
- Prelisten volume slider in browser title bar
- Active row visual feedback (blue tint, badge, animated edge)
- Auto-accept license, auto-subscribe all channels
- Prelisten → full session transition (disconnect + reconnect)
- Error handling for unreachable/password-protected servers

**Out of scope:**
- Prelisten while already in a session (requires second NJClient — future work)
- Per-channel volume/mute in prelisten mode
- VU meters or channel list display during prelisten
- Prelisten audio on a separate output bus
- Server-side spectator protocol support
