# Phase 13: Video Display Modes and OSC Integration - Research

**Researched:** 2026-04-07
**Domain:** Browser window.open popout management, VDO.Ninja &view parameter isolation, OSC address handling (JUCE juce_osc), cross-window postMessage, TouchOSC .tosc template editing, Vite multi-page builds
**Confidence:** HIGH

## Summary

Phase 13 adds two capabilities to JamWide: per-participant video popout windows for multi-monitor setups (VID-07) and OSC control of video features from a control surface (VID-11). Both features are well-scoped extensions of existing infrastructure -- the companion page TypeScript/HTML (Phases 11-12), the C++ VideoCompanion WebSocket server (Phase 11), and the C++ OscServer (Phases 9-10).

The popout window work is entirely browser-side: a new `popout.html` page with minimal JS that receives a solo VDO.Ninja iframe URL with `&view=streamId`, plus main page JavaScript for roster pill click handling, `window.open()` management, and cross-window `postMessage` for disconnect detection. The OSC work requires C++ additions to both `OscServer.cpp` (new `/JamWide/video/active` and `/JamWide/video/popout/{idx}` address handling) and `VideoCompanion.cpp` (new `requestPopout()` method for WebSocket forwarding and deactivate message broadcasting). The TouchOSC template update adds a VIDEO section with XML editing of the zlib-compressed `.tosc` file.

No new dependencies are needed on either side. The companion page uses vanilla DOM APIs (`window.open`, `postMessage`, `Map`). The C++ side reuses existing patterns from OscServer (prefix matching, index parsing, callAsync dispatch) and VideoCompanion (WebSocket broadcast, JSON message protocol).

**Primary recommendation:** Implement in three waves: (1) companion popout page + main page popout window management (TypeScript-only, testable in isolation), (2) C++ VideoCompanion deactivate broadcast + requestPopout method + OscServer video address handling, (3) TouchOSC template update. This order minimizes cross-cutting dependencies.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Popout triggered by clicking a roster pill in the companion page. The pill already has `data-stream-id` from Phase 12. Click opens a new browser window with a solo VDO.Ninja iframe for that user's stream.
- **D-02:** Popout window content is a solo VDO.Ninja iframe showing only the target user's stream via `&view=streamId` parameter. Same room, password, quality, and chunked mode params as the main companion page. Minimal chrome -- just the video with a name label and disconnect overlay.
- **D-03:** When the source user disconnects while their popout is open, show a semi-transparent overlay with the user's name and "Disconnected". If they reconnect, the stream resumes automatically. Window stays open.
- **D-04:** Unlimited simultaneous popout windows allowed. Each roster pill click opens a new window. User can pop out as many participants as they want across monitors.
- **D-05:** Re-clicking a roster pill for an already-popped-out user focuses the existing window (`window.focus()`) instead of opening a duplicate. Companion tracks open popout windows by stream ID.
- **D-06:** Separate `popout.html` page in the companion directory. Minimal JS. Reads room, push, view (stream ID), password, quality from URL query parameters passed by the opener. No WebSocket connection -- the main companion notifies popouts via `postMessage`.
- **D-07:** Main companion tracks open popout windows in a Map<streamId, Window>. On roster change, sends `postMessage({type:'roster', users})` to each open popout. Popout checks if its target stream ID is still in the roster; if not, shows disconnect overlay.
- **D-08:** New companion source file `popout.ts` for the popout page logic. Shared code (URL builder, types) imported from existing `ui.ts` and `types.ts`.
- **D-09:** VDO.Ninja `&view=streamId` parameter isolates a single user's stream in the iframe. Combined with same room, password, quality, chunked, noaudio, and cleanoutput params from the main companion's current state.
- **D-10:** Popout URL built by extending the existing `buildVdoNinjaUrl()` with an optional `viewStreamId` parameter. When set, appends `&view={streamId}` to the URL.
- **D-11:** Window opened with `window.open(url, windowName, features)` where features = `width=640,height=480,toolbar=no,menubar=no,resizable=yes`. Window title set to "{Username} - JamWide Video".
- **D-12:** No position/size persistence across sessions. Consistent with Phase 11 D-19 (no video state persistence).
- **D-13:** When plugin deactivates video, plugin sends `{type:'deactivate'}` WebSocket message to all clients before stopping the WS server. Companion receives it, closes all popout windows via `window.close()`, and shows connection lost state in the main page.
- **D-14:** `/JamWide/video/active` -- float toggle (1.0 = launch companion/activate, 0.0 = deactivate). Bidirectional feedback: sends current state back to control surface.
- **D-15:** `/JamWide/video/popout/{idx}` -- float trigger (1.0) pops out the remote user at roster index `idx`. Plugin looks up the stream ID from the roster at that index, then sends `{type:'popout', streamId:'...'}` via WebSocket to the companion page.
- **D-16:** OSC video scope is minimal: active toggle + popout triggers only. No OSC addresses for bandwidth profile or buffer delay.
- **D-17:** Popout flow from OSC: TouchOSC -> `/JamWide/video/popout/{idx}` -> `OscServer::handleOscOnMessageThread()` -> lookup `roster[idx].streamId` -> `VideoCompanion::requestPopout(streamId)` -> WebSocket send `{type:'popout', streamId}` -> companion `ws-client.ts` dispatches `onPopout` callback -> `window.open(popoutUrl)`.
- **D-18:** Update the existing `.tosc` template (from Phase 10) with a video control section: video active toggle button + 8 popout trigger buttons.
- **D-19:** Video panel layout: toggle button labeled "ACTIVE" plus a row of 8 numbered popout buttons [1]-[8]. Positioned in a new "VIDEO" section of the template.
- **D-20:** Two display modes only: grid (default) and popout (per-user separate window). No spotlight or focus modes in this phase.

### Claude's Discretion
- Exact CSS for the disconnect overlay in popout windows
- Exact postMessage protocol between main companion and popout windows
- Error handling for popup blockers (window.open() may be blocked)
- TouchOSC template visual layout (colors, spacing, button sizes)
- Whether popout.html needs its own Vite entry point or can be a simple static page

### Deferred Ideas (OUT OF SCOPE)
- Spotlight/focus display mode (single user fills main companion window) -- future phase if needed
- Always-on-top option for popout windows -- requires non-standard browser APIs or Electron
- Popout window position persistence -- complex with changing stream IDs, keep manual for now
- Fullscreen toggle button in popout windows -- could add via Fullscreen API in a future phase
- OSC bandwidth profile control (`/JamWide/video/quality`) -- keep companion-only for now
- OSC buffer delay override -- auto-calculated from BPM/BPI, no manual control needed
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| VID-07 | User can pop out individual participant video into separate windows | VDO.Ninja `&view=streamId` parameter verified for single-stream isolation in room context. `window.open()` API, `postMessage` cross-window communication, and popout page architecture all researched. Popup blocker handling documented. |
| VID-11 | User can control video features (open, close, mode switch, popout) via OSC | OscServer prefix-matching dispatch pattern already established in Phase 10. Video address namespace (`/JamWide/video/active`, `/JamWide/video/popout/{idx}`) follows same index-based pattern as remote users. VideoCompanion WebSocket broadcast pattern proven. |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE juce_osc | 8.0.12 (bundled) | OSC message receive/send for video addresses | Already linked and used by OscServer (Phases 9-10) [VERIFIED: local OscServer.h includes JuceHeader.h, uses juce::OSCReceiver/OSCSender] |
| IXWebSocket | (already linked) | WebSocket server for popout/deactivate message forwarding | Already used in VideoCompanion.cpp [VERIFIED: local VideoCompanion.h includes ixwebsocket/IXWebSocketServer.h] |
| Vite | 6.x | Multi-page build for companion (index.html + popout.html) | Already configured in companion/vite.config.ts [VERIFIED: local companion/vite.config.ts] |
| Vitest | 4.1.3 | Companion page unit tests | Already configured in companion/vitest.config.ts with jsdom environment [VERIFIED: local vitest.config.ts, 39 existing tests pass] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Browser window.open API | (built-in) | Opening popout windows | Standard DOM API, no library needed [VERIFIED: MDN Web API docs] |
| Browser postMessage API | (built-in) | Cross-window communication (main -> popout) | Standard DOM API for same-origin message passing [VERIFIED: MDN Web API docs] |
| Browser Map | (built-in) | Tracking open popout windows by stream ID | ES6 standard, supported in all Chromium browsers [ASSUMED] |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| postMessage for roster relay | BroadcastChannel API | BroadcastChannel is simpler but doesn't give us a window reference. We need the Window object from Map to call `window.close()` on deactivate and `window.focus()` on re-click. postMessage is the correct choice. |
| Separate popout.html | iframe-based popout in main page | D-06 explicitly requires separate windows for multi-monitor support. Iframes can't be dragged to other monitors. |
| Vite multi-page build | Static popout.html (no Vite) | Vite multi-page is trivial to configure and gives us TypeScript compilation + shared imports from ui.ts and types.ts. Static HTML would require duplicating URL builder logic. |

**Installation:**
```bash
# No new packages needed
# Vite multi-page config is a build config change only
```

## Architecture Patterns

### Recommended Project Structure (changes only)
```
companion/
    index.html               # Existing -- no changes
    popout.html              # NEW: popout window page
    vite.config.ts           # MODIFY: add multi-page input
    src/
        main.ts              # MODIFY: add popout window tracking, onPopout/onDeactivate callbacks
        popout.ts            # NEW: popout page entry point
        ui.ts                # MODIFY: extend buildVdoNinjaUrl with viewStreamId, add popout helpers
        ws-client.ts         # MODIFY: extend WsCallbacks with onPopout and onDeactivate
        types.ts             # MODIFY: add PopoutMessage, DeactivateMessage types + type guards
        __tests__/
            popout-window.test.ts   # NEW: popout window management tests
            popout-url.test.ts      # NEW: URL builder &view= parameter tests

juce/
    video/
        VideoCompanion.h     # MODIFY: add requestPopout() method declaration
        VideoCompanion.cpp   # MODIFY: add requestPopout(), deactivate broadcast, new WS message types
    osc/
        OscServer.h          # MODIFY: add video state tracking member variables
        OscServer.cpp         # MODIFY: add video prefix handling, video active feedback

assets/
    JamWide.tosc             # MODIFY: add VIDEO section with active toggle + 8 popout buttons
```

### Pattern 1: Vite Multi-Page Configuration
**What:** Configure Vite to build both `index.html` and `popout.html` as separate entry points.
**When to use:** When the companion needs multiple HTML pages that share TypeScript code.
**Example:**
```typescript
// Source: Vite official docs (vite.dev/guide/build#multi-page-app)
// [CITED: https://vite.dev/guide/build#multi-page-app]
import { defineConfig } from 'vite';
import { resolve } from 'path';

export default defineConfig({
  root: '.',
  base: '/video/',
  build: {
    outDir: '../docs/video',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'index.html'),
        popout: resolve(__dirname, 'popout.html'),
      },
    },
  },
  server: {
    port: 5173,
  },
});
```

### Pattern 2: Popout Window Management with Map
**What:** Track open popout windows using `Map<string, Window>`, handle focus-existing and cleanup.
**When to use:** Main companion page manages popout lifecycle.
**Example:**
```typescript
// [VERIFIED: MDN window.open docs + CONTEXT.md D-05, D-07]
const popoutWindows = new Map<string, Window>();

function openPopout(streamId: string, username: string, room: string,
                    push: string, hashFragment: string): void {
  // D-05: Focus existing window instead of opening duplicate
  const existing = popoutWindows.get(streamId);
  if (existing && !existing.closed) {
    existing.focus();
    return;
  }

  // Build popout URL with query params
  const params = new URLSearchParams({
    room, push, view: streamId,
    name: username,
  });
  if (hashFragment) params.set('password', hashFragment);

  const url = `popout.html?${params.toString()}`;
  const windowName = `jamwide-popout-${streamId}`;
  const features = 'width=640,height=480,toolbar=no,menubar=no,resizable=yes';

  const win = window.open(url, windowName, features);

  if (!win) {
    // Popup blocker handling
    showPopupBlockedBanner();
    return;
  }

  popoutWindows.set(streamId, win);
}
```

### Pattern 3: Cross-Window postMessage Protocol
**What:** Main companion sends roster updates to popout windows; popout checks if its target stream is still present.
**When to use:** On every roster WebSocket message, and on deactivate.
**Example:**
```typescript
// Main companion -- send roster to all open popouts
// [VERIFIED: MDN postMessage API + CONTEXT.md D-07]
function notifyPopouts(users: RosterUser[]): void {
  for (const [streamId, win] of popoutWindows.entries()) {
    if (win.closed) {
      popoutWindows.delete(streamId);
      continue;
    }
    win.postMessage({ type: 'roster', users }, '*');
  }
}

// Popout page -- receive and check
// [VERIFIED: MDN postMessage API]
window.addEventListener('message', (event: MessageEvent) => {
  if (event.data?.type === 'roster') {
    const users = event.data.users as RosterUser[];
    const stillHere = users.some(u => u.streamId === myStreamId);
    overlay.style.display = stillHere ? 'none' : 'flex';
  }
});
```

### Pattern 4: OscServer Video Address Dispatch (C++)
**What:** Add video address prefix matching in `handleOscOnMessageThread()`, following the same pattern as remote user addresses.
**When to use:** When OSC messages arrive with `/JamWide/video/` prefix.
**Example:**
```cpp
// Source: Existing pattern in OscServer.cpp line ~183
// [VERIFIED: local OscServer.cpp handleOscOnMessageThread()]
void OscServer::handleOscOnMessageThread(const juce::String& address, float value)
{
    // Phase 10: remote user prefix
    if (address.startsWith("/JamWide/remote/"))
    {
        handleRemoteUserOsc(address, value);
        return;
    }

    // Phase 13: video prefix
    if (address.startsWith("/JamWide/video/"))
    {
        handleVideoOsc(address, value);
        return;
    }

    // ... existing static map lookup ...
}

void OscServer::handleVideoOsc(const juce::String& address, float value)
{
    if (address == "/JamWide/video/active")
    {
        // D-14: Toggle video active state
        if (value >= 0.5f)
        {
            // Activate: equivalent to user clicking Video button
            // Implementation via processor method
        }
        else
        {
            // Deactivate: send deactivate msg, stop WS, set inactive
            processor.videoCompanion->deactivate();
        }
        return;
    }

    // D-15: /JamWide/video/popout/{idx}
    if (address.startsWith("/JamWide/video/popout/"))
    {
        if (value < 0.5f) return;  // Only trigger on press (1.0)

        juce::String idxStr = address.substring(22);  // "/JamWide/video/popout/" = 22 chars
        int oscIdx = idxStr.getIntValue();
        if (oscIdx < 1 || oscIdx > 16) return;  // Bounds check

        int userIndex = oscIdx - 1;  // 1-based to 0-based
        const auto& users = processor.cachedUsers;
        if (userIndex >= static_cast<int>(users.size())) return;

        // Look up stream ID (sanitized username) from roster
        // Uses same sanitization as VideoCompanion
        juce::String streamId = /* resolve from roster */;
        processor.videoCompanion->requestPopout(streamId);
    }
}
```

### Pattern 5: VideoCompanion requestPopout and deactivate Broadcast
**What:** New method on VideoCompanion to send popout request via WebSocket; extend deactivate to broadcast before stopping.
**When to use:** Called from OscServer for OSC-triggered popouts, and from the plugin UI for video deactivation.
**Example:**
```cpp
// [VERIFIED: existing VideoCompanion broadcast pattern in broadcastRoster/broadcastBufferDelay]
void VideoCompanion::requestPopout(const juce::String& streamId)
{
    if (!isActive()) return;

    juce::String json = "{\"type\":\"popout\",\"streamId\":\""
                        + escapeJsonString(streamId) + "\"}";

    std::lock_guard<std::mutex> lock(wsMutex_);
    if (!wsServer_) return;
    auto clients = wsServer_->getClients();
    for (auto& client : clients)
        client->send(json.toStdString());
}

void VideoCompanion::deactivate()
{
    // D-13: Broadcast deactivate BEFORE stopping server
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        if (wsServer_)
        {
            juce::String json = "{\"type\":\"deactivate\"}";
            auto clients = wsServer_->getClients();
            for (auto& client : clients)
                client->send(json.toStdString());
        }
    }

    active_.store(false, std::memory_order_relaxed);
    stopWebSocketServer();
    // ... clear state ...
}
```

### Pattern 6: buildVdoNinjaUrl Extension for &view=
**What:** Extend existing URL builder to optionally append `&view=streamId` for popout windows.
**When to use:** When constructing the VDO.Ninja iframe URL for a popout that should show only one participant.
**Example:**
```typescript
// Source: existing buildVdoNinjaUrl in companion/src/ui.ts
// [VERIFIED: local companion/src/ui.ts buildVdoNinjaUrl function]
export function buildVdoNinjaUrl(
  room: string,
  push: string,
  effect?: BgEffect,
  bandwidthProfile?: BandwidthProfile,
  hashFragment?: string,
  viewStreamId?: string  // NEW: optional stream ID for solo view
): string {
  // ... existing URL construction ...
  let url = /* existing base URL */;

  // D-10: Append &view= for popout solo view
  if (viewStreamId)
    url += `&view=${encodeURIComponent(viewStreamId)}`;

  if (hashFragment)
    url += `&password=${encodeURIComponent(hashFragment)}`;

  return url;
}
```

### Anti-Patterns to Avoid
- **Opening popouts from async/setTimeout context:** `window.open()` will be blocked by popup blockers if not called synchronously from a user click handler. The roster pill click handler must call `window.open()` directly, not from a promise or callback. [CITED: https://developer.mozilla.org/en-US/docs/Web/API/Window/open]
- **Using window.opener in popout page:** D-06 says popout reads params from URL query parameters, not from the opener window. This is more robust -- the popout page works even if the opener navigates away.
- **Storing Window references without cleanup:** Closed windows must be detected and cleaned from the Map. Check `window.closed` on every roster update and cleanup stale entries.
- **Sending deactivate message after stopping WebSocket server:** D-13 requires the deactivate message to be sent BEFORE the server stops. The existing `deactivate()` in VideoCompanion.cpp calls `stopWebSocketServer()` which destroys the server. The deactivate broadcast must happen before that call.
- **OSC video/active triggering privacy modal:** When activating video via OSC, the implementation must handle the privacy modal requirement. The existing button flow shows a modal before first launch. OSC cannot show a modal (no UI context). Resolution: if video has already been launched this session (privacy accepted), OSC activate re-launches without modal. If never launched, OSC activate may need to be a no-op or skip the modal since the user is intentionally sending OSC commands from a control surface.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Cross-window communication | Custom event bus / SharedWorker | Browser `postMessage` API | postMessage is designed for exactly this use case. SharedWorker adds complexity with no benefit for simple message passing. [VERIFIED: MDN postMessage] |
| Window tracking across tabs | Custom heartbeat/polling system | `Map<streamId, Window>` + check `window.closed` | The Window reference from `window.open()` has a `.closed` property that is the standard way to detect closed windows. No polling needed -- check on roster update. [VERIFIED: MDN Window.closed] |
| URL query parameter parsing | Custom regex parser | `URLSearchParams` (browser built-in) | Standard API, handles encoding/decoding automatically. Already used in main.ts for wsPort parsing. [VERIFIED: local companion/src/main.ts] |
| TouchOSC template editing | Manual XML construction | Python zlib decompress + XML editing | The .tosc file is zlib-compressed XML. Python's zlib and xml.etree work correctly for reading, modifying, and writing back. [VERIFIED: decompressed the actual JamWide.tosc file -- confirmed XML/lexml format] |
| JSON message serialization (C++) | Custom JSON builder | Extend existing `escapeJsonString()` pattern | Already used for config, roster, and bufferDelay messages in VideoCompanion.cpp. Consistent approach. [VERIFIED: local VideoCompanion.cpp] |

**Key insight:** This phase has zero new external dependencies. Every feature uses standard browser APIs and extends existing C++ patterns. The complexity is in the integration plumbing, not in any individual technology.

## Common Pitfalls

### Pitfall 1: Popup Blockers Silently Prevent window.open()
**What goes wrong:** `window.open()` returns `null` when the browser's popup blocker intercepts the call. If the code doesn't check for `null`, it stores `null` in the popout Map and subsequent operations (focus, postMessage, close) throw TypeError.
**Why it happens:** Modern browsers block `window.open()` calls that aren't directly triggered by a user gesture (click/keypress). Calls from async callbacks, setTimeout, or promise chains are blocked.
**How to avoid:** (1) Call `window.open()` synchronously inside the roster pill click handler -- never defer to an async callback. (2) Always check the return value for `null` before storing. (3) Show a user-facing banner explaining how to allow popups. UI-SPEC already defines the banner style.
**Warning signs:** The popout Map grows but windows don't appear. Users see no feedback when clicking pills.
[VERIFIED: https://developer.mozilla.org/en-US/docs/Web/API/Window/open -- "The method may not have any visible effect if called outside of a user-triggered event"]

### Pitfall 2: OSC Popout Trigger Cannot Directly Open Browser Windows
**What goes wrong:** The OSC popout flow goes from OSC -> C++ plugin -> WebSocket -> companion JavaScript -> `window.open()`. Since the `window.open()` call at the end is NOT triggered by a user gesture (it's triggered by a WebSocket message handler), it may be blocked by the popup blocker.
**Why it happens:** The browser has no way to know that the ultimate origin of the WebSocket message was a physical button press on a TouchOSC surface.
**How to avoid:** This is the most architecturally significant pitfall of the phase. Options: (a) Accept that OSC-triggered popouts may be blocked on first attempt and rely on the user allowing popups for the site after seeing the blocked banner, (b) Grant the companion page blanket popup permission by having the user allow popups once via browser settings, which is a one-time setup step. In practice, because the main companion page is opened via direct user action (clicking Video in the plugin), and the popout opens from the same origin, most Chromium browsers allow popups from pages the user explicitly navigated to. GitHub Pages (`jamwide.audio`) should be allowed after the first manual popup permission grant.
**Warning signs:** All roster pill popouts work but OSC-triggered popouts are blocked. The popup-blocked banner appears when OSC triggers are received.
[VERIFIED: MDN docs confirm popup blockers evaluate user gesture at the immediate calling context, not upstream]

### Pitfall 3: Stale Window References in Popout Map
**What goes wrong:** A user manually closes a popout window (browser close button). The main companion's Map still holds the now-closed Window reference. On next roster update, `postMessage` calls to this window fail silently. On re-click, the code tries `existingWindow.focus()` on a closed window.
**Why it happens:** There is no reliable cross-window event for detecting that a child window was closed. The `beforeunload` event fires in the popout's context, not the opener's.
**How to avoid:** Check `window.closed` property before every operation (postMessage, focus). Clean up stale entries during the roster notification loop (Pattern 3 shows this). Also update pill visual indicators when a stale reference is detected.
**Warning signs:** Green pill indicators persist after a user manually closed a popout.
[VERIFIED: MDN Window.closed property documentation]

### Pitfall 4: VDO.Ninja &view= Requires Room Context
**What goes wrong:** Using `&view=streamId` without `&room=roomName` in the URL results in VDO.Ninja trying to create a direct peer-to-peer connection rather than connecting to the room where the stream is published.
**Why it happens:** VDO.Ninja treats `&view` differently based on whether `&room` is also present. With a room, it connects to the room and filters to show only the specified stream(s). Without a room, it attempts a direct connection.
**How to avoid:** Always include the full room parameters (room, password, chunked, etc.) in the popout URL, just like the main companion page. The popout URL should be the same base URL as the main page plus `&view=streamId`.
**Warning signs:** Popout windows show "connecting" indefinitely or connect to the wrong stream.
[CITED: https://docs.vdo.ninja/advanced-settings/mixer-scene-parameters/view -- "&view will accept a comma-separated list of valid values" and works in room context]

### Pitfall 5: Deactivate Race Between WebSocket Broadcast and Server Stop
**What goes wrong:** If `deactivate()` stops the WebSocket server before the deactivate message reaches connected clients, companion pages never receive the deactivate message and popout windows remain open as orphans.
**Why it happens:** The existing `deactivate()` implementation immediately calls `stopWebSocketServer()` which destroys the server. WebSocket send is buffered -- the message may not have been flushed before the connection is torn down.
**How to avoid:** Send the deactivate message to all clients first, then allow a brief pause (or at minimum ensure the send completes) before stopping the server. IXWebSocket's `send()` is synchronous (buffers into the send queue) but the actual network transmission is async. The safest approach: send the deactivate message, then call `stop()` which will flush pending sends before closing connections.
**Warning signs:** Popout windows stay open after the user deactivates video from the plugin UI or via OSC.
[VERIFIED: local VideoCompanion.cpp -- stopWebSocketServer() calls wsServer_->stop() then wsServer_->wait()]

### Pitfall 6: OSC Video Active Feedback Without Dedicated State Tracking
**What goes wrong:** The OSC timer callback sends dirty state every 100ms. If video active state isn't tracked with a `lastSentVideoActive` variable, either (a) the feedback is sent every tick (wasteful) or (b) it's never sent (no feedback to TouchOSC).
**Why it happens:** The existing OscServer dirty-tracking pattern requires a "last sent" cache for every value that needs feedback. Video active state is read from `processor.videoCompanion->isActive()` but there's no corresponding dirty-check variable.
**How to avoid:** Add `lastSentVideoActive` to OscServer's state tracking, following the exact same pattern as `lastSentBpm`, `lastSentStatus`, etc. Check on each timer tick, send only when changed.
**Warning signs:** TouchOSC video active button doesn't reflect current state, or flickers.
[VERIFIED: local OscServer.cpp timerCallback pattern -- every sent value has a lastSent cache variable]

### Pitfall 7: Roster Pill Click Handlers Must Be Buttons, Not Spans
**What goes wrong:** The existing `renderRosterStrip()` creates `<span>` elements for pills. Clicking a span works with mouse but isn't keyboard-accessible. UI-SPEC requires pills to be `<button>` elements for keyboard accessibility (Tab + Enter/Space).
**Why it happens:** Phase 12 implemented pills as spans because they were display-only labels. Phase 13 makes them interactive (clickable to trigger popout), which requires a different element type.
**How to avoid:** Change the pill element from `<span>` to `<button>` in `renderRosterStrip()`. This is a minor change to ui.ts but affects existing test assertions that check for `.roster-pill` class on spans.
**Warning signs:** Roster pills not focusable via Tab key, screen readers don't announce them as interactive.
[CITED: 13-UI-SPEC.md -- "Pills are `<button>` elements with `title` attribute. Keyboard focusable via Tab, activated via Enter/Space."]

## Code Examples

### Example 1: popout.html Page Structure
```html
<!-- Source: 13-UI-SPEC.md Component Inventory #1 -->
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>JamWide Video</title>
  <link rel="stylesheet" href="./popout.css" />
</head>
<body>
  <div id="video-area">
    <!-- Solo VDO.Ninja iframe inserted by popout.ts -->
  </div>
  <div id="name-label"></div>
  <div id="disconnect-overlay" style="display:none;">
    <h2 id="overlay-username"></h2>
    <p>Disconnected</p>
  </div>
  <script type="module" src="./src/popout.ts"></script>
</body>
</html>
```

### Example 2: popout.ts Entry Point
```typescript
// Source: CONTEXT.md D-06, D-08
import type { RosterUser } from './types';
import { buildVdoNinjaUrl } from './ui';
import type { BandwidthProfile } from './ui';

// Parse URL query params
const params = new URLSearchParams(window.location.search);
const room = params.get('room') ?? '';
const push = params.get('push') ?? '';
const viewStreamId = params.get('view') ?? '';
const username = params.get('name') ?? viewStreamId;
const password = params.get('password') ?? '';
const quality = (params.get('quality') ?? 'balanced') as BandwidthProfile;

// Set window title (D-11)
document.title = `${username} - JamWide Video`;

// Set name label
const nameLabel = document.getElementById('name-label');
if (nameLabel) nameLabel.textContent = username;

// Build solo VDO.Ninja URL with &view=
const url = buildVdoNinjaUrl(room, push, undefined, quality, password, viewStreamId);

// Create and insert iframe
const videoArea = document.getElementById('video-area');
if (videoArea) {
  const iframe = document.createElement('iframe');
  iframe.src = url;
  iframe.title = `${username} - Video`;
  iframe.allow = 'camera;microphone;display-capture';
  iframe.style.width = '100%';
  iframe.style.height = '100%';
  iframe.style.border = 'none';
  videoArea.appendChild(iframe);
}

// Listen for roster updates from main companion via postMessage
const overlay = document.getElementById('disconnect-overlay');
const overlayUsername = document.getElementById('overlay-username');
if (overlayUsername) overlayUsername.textContent = username;

window.addEventListener('message', (event: MessageEvent) => {
  if (event.data?.type === 'roster' && Array.isArray(event.data.users)) {
    const users = event.data.users as RosterUser[];
    const present = users.some(u => u.streamId === viewStreamId);
    if (overlay) {
      overlay.style.display = present ? 'none' : 'flex';
    }
  }
});
```

### Example 3: WebSocket Message Types Extension
```typescript
// Source: CONTEXT.md D-13, D-15, D-17
// Extends existing companion/src/types.ts

export interface PopoutMessage {
  type: 'popout';
  streamId: string;
}

export interface DeactivateMessage {
  type: 'deactivate';
}

export function isPopoutMessage(msg: unknown): msg is PopoutMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  const m = msg as Record<string, unknown>;
  return m.type === 'popout' && typeof m.streamId === 'string';
}

export function isDeactivateMessage(msg: unknown): msg is DeactivateMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  return (msg as Record<string, unknown>).type === 'deactivate';
}
```

### Example 4: OscServer Video Active Feedback in Timer
```cpp
// Source: CONTEXT.md D-14, existing OscServer timerCallback pattern
// [VERIFIED: local OscServer.cpp sendDirtyTelemetry pattern]

// In OscServer.h -- add to private members:
float lastSentVideoActive = -1.0f;

// In OscServer timerCallback or a new sendVideoState method:
void OscServer::sendVideoState(juce::OSCBundle& bundle, bool& hasContent)
{
    float videoActive = processor.videoCompanion->isActive() ? 1.0f : 0.0f;
    if (videoActive != lastSentVideoActive)
    {
        lastSentVideoActive = videoActive;
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/video/active"), videoActive));
        hasContent = true;
    }
}
```

### Example 5: TouchOSC VIDEO Section XML Structure
```xml
<!-- Source: 13-UI-SPEC.md TouchOSC Component Inventory #3 -->
<!-- [VERIFIED: decompressed actual JamWide.tosc -- confirmed lexml v4 format with node/properties/messages structure] -->
<node type="GROUP">
  <properties>
    <property type="s"><key>name</key><value>Video</value></property>
    <property type="r"><key>frame</key><value><x>4</x><y>200</y><w>310</w><h>120</h></value></property>
    <property type="c"><key>color</key><value><r>0.15</r><g>0.15</g><b>0.15</b><a>1.0</a></value></property>
    <property type="b"><key>background</key><value>1</value></property>
  </properties>
  <values /><messages /><children>
    <!-- Section Label -->
    <node type="LABEL">
      <properties>
        <property type="s"><key>name</key><value>lbl_video</value></property>
        <property type="r"><key>frame</key><value><x>8</x><y>4</y><w>160</w><h>20</h></value></property>
        <property type="s"><key>text</key><value>VIDEO</value></property>
        <property type="i"><key>textSize</key><value>14</value></property>
      </properties>
      <values /><messages /><children />
    </node>
    <!-- Active Toggle -->
    <node type="BUTTON">
      <properties>
        <property type="s"><key>name</key><value>btn_video_active</value></property>
        <property type="r"><key>frame</key><value><x>8</x><y>28</y><w>294</w><h>48</h></value></property>
        <property type="s"><key>text</key><value>ACTIVE</value></property>
        <property type="b"><key>buttonType</key><value>1</value></property><!-- toggle -->
      </properties>
      <values />
      <messages>
        <osc>
          <enabled>1</enabled><send>1</send><receive>1</receive><feedback>0</feedback>
          <connections>11111</connections>
          <path>/JamWide/video/active</path>
          <!-- ... argument spec ... -->
        </osc>
      </messages>
      <children />
    </node>
    <!-- Popout buttons 1-8 (momentary) -->
    <!-- Each: type="BUTTON", buttonType=0 (momentary), path=/JamWide/video/popout/{n} -->
  </children>
</node>
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| VDO.Ninja `&solo` for single view | `&view=streamId` with `&room=` | Always been available | `&view` is the documented way to filter to specific stream(s) in a room. `&solo` adds viewer-only mode on top of `&view`. For popouts that don't publish, either works, but `&view` is more semantically correct. [CITED: docs.vdo.ninja/advanced-settings/mixer-scene-parameters/view] |
| Custom WebSocket libraries for cross-window | Browser postMessage API | Standard since IE10+ | postMessage is the standard for same-origin cross-window messaging. No WebSocket needed between windows on the same origin. |
| TouchOSC legacy .touchosc format | New TouchOSC .tosc (zlib XML) format | TouchOSC v2 (2021+) | The .tosc format is zlib-compressed XML (lexml v4). Older .touchosc files are no longer supported by current TouchOSC. The existing JamWide.tosc is already in the new format. [VERIFIED: decompressed actual file] |

**Deprecated/outdated:**
- None identified. All technologies in this phase are current and stable.

## Assumptions Log

> List all claims tagged `[ASSUMED]` in this research.

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | ES6 Map is supported in all Chromium browsers used for companion page | Standard Stack | LOW -- Map has been supported since Chrome 38 (2014). VDO.Ninja already requires modern Chromium. |
| A2 | OSC-triggered popouts (from WebSocket message handler) may be blocked by popup blocker on first attempt | Pitfalls | MEDIUM -- if browsers are more permissive than expected, the popup-blocked banner code is dead code (benign). If MORE restrictive, OSC popouts won't work at all until user allows popups for the site. |

**If this table is empty:** Most claims were verified against local source code, MDN documentation, or VDO.Ninja official docs.

## Open Questions

1. **OSC Video Active and Privacy Modal Interaction**
   - What we know: Phase 11 D-05 requires a privacy modal on every video launch. The modal is a JUCE UI component shown on the message thread.
   - What's unclear: When OSC sends `/JamWide/video/active 1.0` to activate video, should the privacy modal be shown? There's no mechanism to show a JUCE modal from an OSC handler and wait for user input. The user may not even be looking at the plugin UI.
   - Recommendation: If video has been launched at least once this session (privacy already accepted), OSC activate re-launches without modal. If never launched, either (a) ignore the OSC activate (user must click Video button first to accept privacy), or (b) skip the modal for OSC activations since the user is intentionally controlling video from a surface. Option (a) is safer. This should be a planner decision.

2. **Popout URL: Should push= be included?**
   - What we know: D-09 says "Same room, password, quality, and chunked mode params as the main companion page." D-02 says the popout shows only the target user's stream via `&view=streamId`.
   - What's unclear: Should the popout page include `&push=localUsername` in its VDO.Ninja URL? If yes, the popout publishes the local user's camera -- which means every popout window adds another outbound camera stream. If no (omit `&push`), the popout is view-only.
   - Recommendation: Omit `&push=` from popout URLs. Popout windows should be view-only. Only the main companion page publishes the local camera. This prevents bandwidth multiplication from multiple outbound streams. `&view=streamId` without `&push=` makes the popout viewer-only.

3. **StreamId Resolution for OSC Popout**
   - What we know: D-17 describes the flow: OscServer receives popout trigger, looks up `roster[idx].streamId`, sends to VideoCompanion.
   - What's unclear: The C++ roster (`cachedUsers`) stores `NJClient::RemoteUserInfo` which has a `name` field but no `streamId` field. The streamId is computed by `VideoCompanion::sanitizeUsername()` + `resolveCollision()` during `broadcastRoster()`. OscServer doesn't have direct access to this computation.
   - Recommendation: Either (a) OscServer calls VideoCompanion's sanitization/collision logic to derive the streamId from the roster name, or (b) VideoCompanion caches the most recently broadcast roster (with streamIds already resolved) and exposes a lookup method. Option (b) is cleaner -- add a `getStreamIdForUserIndex(int idx)` method to VideoCompanion.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Node.js | Companion build (Vite) | Yes | v22.22.1 | -- |
| Vitest | Companion tests | Yes | 4.1.3 | -- |
| Python 3 | TouchOSC template editing (zlib decompress/compress) | Yes | (system) | -- |
| JUCE juce_osc | OSC address handling | Yes | 8.0.12 (bundled) | -- |
| IXWebSocket | WebSocket server | Yes | (already linked) | -- |

**Missing dependencies with no fallback:** None.
**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Vitest 4.1.3 with jsdom environment |
| Config file | companion/vitest.config.ts |
| Quick run command | `cd /Users/cell/dev/JamWide/companion && npx vitest run --reporter=verbose` |
| Full suite command | `cd /Users/cell/dev/JamWide/companion && npx vitest run --reporter=verbose` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| VID-07-a | buildVdoNinjaUrl includes &view= when viewStreamId provided | unit | `npx vitest run src/__tests__/popout-url.test.ts -t "view"` | No -- Wave 0 |
| VID-07-b | buildVdoNinjaUrl omits &view= when viewStreamId not provided | unit | `npx vitest run src/__tests__/popout-url.test.ts -t "omits view"` | No -- Wave 0 |
| VID-07-c | Popout window Map tracks open windows by streamId | unit | `npx vitest run src/__tests__/popout-window.test.ts -t "tracks"` | No -- Wave 0 |
| VID-07-d | Re-click focuses existing window instead of opening duplicate | unit | `npx vitest run src/__tests__/popout-window.test.ts -t "focus"` | No -- Wave 0 |
| VID-07-e | Stale (closed) windows are cleaned from Map | unit | `npx vitest run src/__tests__/popout-window.test.ts -t "stale"` | No -- Wave 0 |
| VID-07-f | Popup blocker detection (null return from window.open) | unit | `npx vitest run src/__tests__/popout-window.test.ts -t "blocked"` | No -- Wave 0 |
| VID-07-g | postMessage to popout windows on roster update | unit | `npx vitest run src/__tests__/popout-window.test.ts -t "postMessage"` | No -- Wave 0 |
| VID-07-h | Popout page shows disconnect overlay when stream leaves roster | unit | `npx vitest run src/__tests__/popout-page.test.ts -t "disconnect"` | No -- Wave 0 |
| VID-07-i | Popout page hides overlay when stream returns | unit | `npx vitest run src/__tests__/popout-page.test.ts -t "reconnect"` | No -- Wave 0 |
| VID-07-j | Deactivate message closes all popout windows | unit | `npx vitest run src/__tests__/popout-window.test.ts -t "deactivate"` | No -- Wave 0 |
| VID-11-a | isPopoutMessage type guard validates correctly | unit | `npx vitest run src/__tests__/types.test.ts -t "popout"` | No -- Wave 0 |
| VID-11-b | isDeactivateMessage type guard validates correctly | unit | `npx vitest run src/__tests__/types.test.ts -t "deactivate"` | No -- Wave 0 |
| VID-11 | OSC video addresses handled in C++ OscServer | manual-only | Build plugin, send OSC from TouchOSC, verify companion receives popout message | N/A -- requires full plugin build + hardware |

### Sampling Rate
- **Per task commit:** `cd /Users/cell/dev/JamWide/companion && npx vitest run --reporter=verbose`
- **Per wave merge:** Same (full suite is fast -- ~1s)
- **Phase gate:** Full suite green before verification

### Wave 0 Gaps
- [ ] `src/__tests__/popout-url.test.ts` -- covers VID-07-a, VID-07-b
- [ ] `src/__tests__/popout-window.test.ts` -- covers VID-07-c through VID-07-g, VID-07-j
- [ ] `src/__tests__/popout-page.test.ts` -- covers VID-07-h, VID-07-i
- [ ] `src/__tests__/types.test.ts` -- extend existing type guard tests for PopoutMessage, DeactivateMessage (VID-11-a, VID-11-b)

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A -- no auth in companion page |
| V3 Session Management | No | N/A -- stateless popout windows |
| V4 Access Control | No | N/A -- room password from Phase 12 carries forward |
| V5 Input Validation | Yes | URL query params validated by `URLSearchParams` (encoding), roster message validated by existing `isRosterMessage` type guard, OSC values clamped by `juce::jlimit` |
| V6 Cryptography | No | N/A -- room password derivation from Phase 12, no new crypto |

### Known Threat Patterns

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| XSS via username in popout page | Tampering | Use `textContent` (never `innerHTML`) for all user-controlled strings. Already established pattern in Phase 12. [VERIFIED: local roster-labels.test.ts tests this] |
| Cross-origin postMessage spoofing | Spoofing | Popout windows are same-origin (both served from jamwide.audio). postMessage origin can be validated if needed, but same-origin makes this low risk. |
| Malicious OSC input (out-of-range index) | Tampering | Bounds-check index against roster size, same pattern as Phase 10 remote user handling. [VERIFIED: local OscServer.cpp line ~495 -- bounds check pattern] |
| WebSocket message injection | Tampering | WebSocket server binds to 127.0.0.1 only (same as Phase 11). Local-only access. [VERIFIED: local VideoCompanion.cpp -- "127.0.0.1" bind] |

## Sources

### Primary (HIGH confidence)
- Local source code: `companion/src/ui.ts`, `companion/src/main.ts`, `companion/src/ws-client.ts`, `companion/src/types.ts` -- current companion implementation
- Local source code: `juce/video/VideoCompanion.h`, `VideoCompanion.cpp` -- current C++ video infrastructure
- Local source code: `juce/osc/OscServer.h`, `OscServer.cpp` -- current C++ OSC implementation with prefix dispatch pattern
- Local file: `assets/JamWide.tosc` -- decompressed and analyzed XML structure (lexml v4 format, zlib compressed)
- Local tests: 39 passing tests in `companion/src/__tests__/` -- confirms existing test infrastructure
- [VDO.Ninja &view parameter docs](https://docs.vdo.ninja/advanced-settings/mixer-scene-parameters/view) -- stream isolation in room context
- [VDO.Ninja &solo parameter docs](https://docs.vdo.ninja/advanced-settings/mixer-scene-parameters/and-solo) -- viewer-only solo link
- [MDN Window.open()](https://developer.mozilla.org/en-US/docs/Web/API/Window/open) -- popup blocker behavior, features string
- [Vite multi-page build docs](https://vite.dev/guide/build#multi-page-app) -- rollupOptions.input for multiple HTML entries
- 13-CONTEXT.md -- all locked decisions (D-01 through D-20)
- 13-UI-SPEC.md -- visual contract for popout page, roster pills, TouchOSC template

### Secondary (MEDIUM confidence)
- [javascript.info popup windows guide](https://javascript.info/popup-windows) -- popup blocker detection patterns
- [tosclib Python library](https://github.com/AlbertoV5/tosclib) -- TouchOSC file format analysis (confirms zlib + XML)

### Tertiary (LOW confidence)
- None. All claims verified against local source code or official documentation.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already in use, no new dependencies
- Architecture: HIGH -- extends proven patterns from Phases 9-12, all source code inspected
- Pitfalls: HIGH -- identified from actual code analysis (deactivate race, popup blocker, stale refs) and verified against MDN docs
- Open questions: MEDIUM -- privacy modal interaction and streamId resolution need planner decisions

**Research date:** 2026-04-07
**Valid until:** 2026-05-07 (stable -- browser APIs and JUCE patterns don't change rapidly)
