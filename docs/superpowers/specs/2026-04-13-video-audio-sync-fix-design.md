# Video-Audio Sync Fix — Design Spec

**Date:** 2026-04-13
**Status:** Approved
**Scope:** Debug setBufferDelay pipeline + add manual delay slider

## Problem

When a user claps on camera, other participants see the clap instantly via WebRTC (~100-300ms) but hear the audio ~8 seconds later via NINJAM (one full interval at 120 BPM / 16 BPI). The existing `setBufferDelay` mechanism was built in Phase 12 to delay video to match audio, but it's not working — video arrives undelayed on the receiving end.

## Root Cause Investigation

The `setBufferDelay` pipeline has three delivery mechanisms:

1. **URL parameter** (`&buffer=8000`): Baked into the VDO.Ninja iframe URL at creation time. Most reliable — VDO.Ninja reads it before any JavaScript loads.
2. **postMessage API** (`{setBufferDelay: 8000}`): Sent from companion page to VDO.Ninja iframe at runtime. May fail if iframe hasn't loaded, or if VDO.Ninja doesn't honor it post-init.
3. **WebSocket broadcast**: Plugin sends `{"type":"bufferDelay","delayMs":8000}` to companion page, which relays via postMessage.

**Likely failure points (ranked):**
1. The `&buffer=N` URL parameter may not be included in the initial iframe URL, or may be set to 0
2. The postMessage fires before the VDO.Ninja iframe has loaded its JavaScript
3. `broadcastBufferDelay` fires before the companion WebSocket connects (BPM/BPI event arrives on connect, but WebSocket may not be ready)
4. VDO.Ninja's `setBufferDelay` postMessage API may not work reliably with `&chunked` mode

## Fix: Part A — Debug & Fix the Pipeline

### Add Diagnostic Logging

Add `console.log` statements at each step of the delay chain in the companion page:
- When `bufferDelay` WebSocket message arrives (log the value)
- When `postMessage({setBufferDelay: N})` is sent to iframe (log the value)
- The initial iframe URL (log to verify `&buffer=N` and `&chunked` are present)
- When the iframe loads (log whether delay was applied before or after load)

### Verify URL Parameter

In `companion/src/ui.ts`, verify that `buildVdoNinjaUrl()` includes `&buffer=N` with the correct value. The delay must be available at URL construction time — if the BPM/BPI hasn't arrived yet when the iframe URL is built, the buffer will be 0.

**Fix:** Cache the last known buffer delay. When building the iframe URL, use the cached value. If no value is cached (first load), use 8000ms as the default (matches the most common NINJAM setting of 120 BPM / 16 BPI).

### Ensure postMessage Timing

Add an `onload` listener to the iframe. After the iframe loads, re-send the `setBufferDelay` postMessage to ensure VDO.Ninja receives it even if the initial delivery was too early.

### Visible Delay Status

Show the current buffer delay in the companion page footer: "Buffer: 8.0s (auto)" — this confirms to users that the delay mechanism is active.

## Fix: Part C — Manual Delay Slider

### UI

In the companion page footer, add:
- A horizontal slider labeled "Video Delay"
- Current value display (e.g., "8.0s")
- Range: 0ms to 30000ms, step 500ms
- An "Auto" toggle button to switch between automatic (from plugin) and manual override

### Behavior

- **Auto mode (default):** Delay value comes from plugin's `broadcastBufferDelay`. Slider position tracks the auto value. Label shows "8.0s (auto)".
- **Manual mode:** User drags the slider. Companion sends `setBufferDelay` directly to iframe via postMessage. Plugin broadcasts are ignored until Auto is re-enabled. Label shows "8.0s (manual)".
- **BPM/BPI change:** If in auto mode, slider updates to new calculated value. If in manual mode, no change (user has overridden).
- **Iframe reload:** Re-apply current delay (auto or manual) on iframe load event.

### Data Flow

```
Auto mode:
  Plugin → WebSocket → Companion → postMessage → VDO.Ninja iframe
  Plugin → URL param (&buffer=N) → VDO.Ninja iframe (initial load)

Manual mode:
  User drags slider → Companion → postMessage → VDO.Ninja iframe
  (Plugin broadcasts ignored)
```

## Files Modified

| File | Change |
|------|--------|
| `companion/src/ui.ts` | Add delay status display, manual slider, auto/manual toggle, iframe onload re-send, URL parameter verification |
| `companion/src/main.ts` | Add pipeline logging, handle manual override state (skip auto-apply when manual), cache buffer delay |
| `companion/src/styles.css` (or inline) | Slider and status styling in companion footer |

## What Stays Untouched

- Plugin C++ code — `broadcastBufferDelay()` already works correctly
- WebSocket protocol — `BufferDelayMessage` type unchanged
- VDO.Ninja API — we use the same `setBufferDelay` postMessage + URL parameter

## Testing

1. Open companion page in Chrome, open DevTools console
2. Connect to a NINJAM room (120 BPM / 16 BPI)
3. Verify console shows: "Buffer delay received: 8000ms", "postMessage sent: setBufferDelay=8000", "iframe URL contains &buffer=8000"
4. Verify footer shows "Buffer: 8.0s (auto)"
5. Have another participant clap on camera — verify video and audio arrive at roughly the same time (within ~1s)
6. Drag manual slider to 0ms — verify video arrives instantly (no delay)
7. Drag manual slider to 16000ms — verify extra delay on video
8. Click "Auto" — verify slider returns to plugin-calculated value
9. Change BPM/BPI on server — verify auto mode updates, manual mode doesn't

## Out of Scope

- **Client-side video buffer (Approach B):** Deferred. Only needed if VDO.Ninja's `setBufferDelay` API proves fundamentally unreliable after debugging.
- **Plugin-side changes:** No C++ modifications needed for this fix.
- **Self-monitoring sync:** The user seeing their own video instantly while their own audio takes 8s is unavoidable — NINJAM's interval model makes this inherent.
