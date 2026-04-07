# Phase 13: Video Display Modes and OSC Integration - Context

**Gathered:** 2026-04-07
**Status:** Ready for planning

<domain>
## Phase Boundary

Per-participant video popout windows for multi-monitor setups, plus OSC command surface control for video open/close and popout triggers. Covers VID-07 (popout windows) and VID-11 (OSC video control). Does NOT include spotlight/focus display modes, OSC bandwidth/buffer control, or popout window position persistence.

</domain>

<decisions>
## Implementation Decisions

### Popout Window Mechanics (VID-07)
- **D-01:** Popout triggered by clicking a roster pill in the companion page. The pill already has `data-stream-id` from Phase 12. Click opens a new browser window with a solo VDO.Ninja iframe for that user's stream.
- **D-02:** Popout window content is a solo VDO.Ninja iframe showing only the target user's stream via `&view=streamId` parameter. Same room, password, quality, and chunked mode params as the main companion page. Minimal chrome -- just the video with a name label and disconnect overlay.
- **D-03:** When the source user disconnects while their popout is open, show a semi-transparent overlay with the user's name and "Disconnected". If they reconnect, the stream resumes automatically. Window stays open.
- **D-04:** Unlimited simultaneous popout windows allowed. Each roster pill click opens a new window. User can pop out as many participants as they want across monitors.
- **D-05:** Re-clicking a roster pill for an already-popped-out user focuses the existing window (`window.focus()`) instead of opening a duplicate. Companion tracks open popout windows by stream ID.

### Popout Page Architecture
- **D-06:** Separate `popout.html` page in the companion directory. Minimal JS. Reads room, push, view (stream ID), password, quality from URL query parameters passed by the opener. No WebSocket connection -- the main companion notifies popouts via `postMessage`.
- **D-07:** Main companion tracks open popout windows in a Map<streamId, Window>. On roster change, sends `postMessage({type:'roster', users})` to each open popout. Popout checks if its target stream ID is still in the roster; if not, shows disconnect overlay.
- **D-08:** New companion source file `popout.ts` for the popout page logic. Shared code (URL builder, types) imported from existing `ui.ts` and `types.ts`.

### Popout URL Construction
- **D-09:** VDO.Ninja `&view=streamId` parameter isolates a single user's stream in the iframe. Combined with same room, password, quality, chunked, noaudio, and cleanoutput params from the main companion's current state.
- **D-10:** Popout URL built by extending the existing `buildVdoNinjaUrl()` with an optional `viewStreamId` parameter. When set, appends `&view={streamId}` to the URL.

### Popout Window Features
- **D-11:** Window opened with `window.open(url, windowName, features)` where features = `width=640,height=480,toolbar=no,menubar=no,resizable=yes`. Window title set to "{Username} - JamWide Video".
- **D-12:** No position/size persistence across sessions. Consistent with Phase 11 D-19 (no video state persistence). User arranges windows manually each session.

### Deactivate Behavior
- **D-13:** When plugin deactivates video (user clicks toggle off, or OSC `/video/active 0.0`), plugin sends `{type:'deactivate'}` WebSocket message to all clients before stopping the WS server. Companion receives it, closes all popout windows via `window.close()`, and shows connection lost state in the main page.

### OSC Video Control (VID-11)
- **D-14:** `/JamWide/video/active` -- float toggle (1.0 = launch companion/activate, 0.0 = deactivate). Bidirectional feedback: sends current state back to control surface.
- **D-15:** `/JamWide/video/popout/{idx}` -- float trigger (1.0) pops out the remote user at roster index `idx`. Plugin looks up the stream ID from the roster at that index, then sends `{type:'popout', streamId:'...'} ` via WebSocket to the companion page.
- **D-16:** OSC video scope is minimal: active toggle + popout triggers only. No OSC addresses for bandwidth profile or buffer delay (those remain companion-page-only controls). Matches VID-11 requirement scope.
- **D-17:** Popout flow from OSC: TouchOSC -> `/JamWide/video/popout/{idx}` -> `OscServer::handleOsc()` -> lookup `roster[idx].streamId` -> `VideoCompanion::requestPopout(streamId)` -> WebSocket send `{type:'popout', streamId}` -> companion `ws-client.ts` dispatches `onPopout` callback -> `window.open(popoutUrl)`.

### TouchOSC Template Update
- **D-18:** Update the existing `.tosc` template (from Phase 10) with a video control section: video active toggle button + 8 popout trigger buttons (matching the existing 8-slot remote user layout).
- **D-19:** Video panel layout: toggle button labeled "ACTIVE" plus a row of 8 numbered popout buttons [1]-[8]. Positioned in a new "VIDEO" section of the template.

### Display Modes
- **D-20:** Two display modes only: grid (default, VDO.Ninja's multi-participant view) and popout (per-user separate window). No spotlight or focus modes in this phase.

### Claude's Discretion
- Exact CSS for the disconnect overlay in popout windows
- Exact postMessage protocol between main companion and popout windows
- Error handling for popup blockers (window.open() may be blocked)
- TouchOSC template visual layout (colors, spacing, button sizes)
- Whether popout.html needs its own Vite entry point or can be a simple static page

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### VDO.Ninja Integration
- `.planning/research/FEATURES.md` -- VDO.Ninja API commands, &view= parameter, room management
- `.planning/research/SUMMARY.md` -- Dependency decisions, companion page architecture
- `.planning/phases/11-video-companion-foundation/11-CONTEXT.md` -- Phase 11 decisions (WebSocket protocol, room ID derivation, username sanitization, companion page design)
- `.planning/phases/12-video-sync-and-roster-discovery/12-CONTEXT.md` -- Phase 12 decisions (roster strip, buffer delay, bandwidth profiles, room hash)
- `.planning/phases/12-video-sync-and-roster-discovery/12-RESEARCH.md` -- VDO.Ninja URL parameters, chunked mode, postMessage API

### OSC Integration
- `.planning/phases/09-osc-server-core/09-CONTEXT.md` -- Phase 9 OSC server foundation
- `.planning/phases/10-osc-remote-users-and-template/10-CONTEXT.md` -- Phase 10 remote user OSC, positional index model, TouchOSC template, dynamic address generation

### Existing Implementation
- `juce/video/VideoCompanion.h` -- Public interface (do not change existing methods)
- `juce/video/VideoCompanion.cpp` -- WebSocket server, roster broadcast, buffer delay
- `juce/osc/OscServer.h` -- OSC server with address map and timer callback
- `juce/osc/OscServer.cpp` -- handleOscOnMessageThread, dynamic address generation pattern
- `companion/src/ui.ts` -- buildVdoNinjaUrl, loadVdoNinjaIframe, renderRosterStrip
- `companion/src/main.ts` -- WebSocket callback wiring, roster handling, popout window tracking
- `companion/src/ws-client.ts` -- WsCallbacks type, message dispatch
- `companion/src/types.ts` -- Message types, type guards
- `assets/JamWide.tosc` -- TouchOSC template to extend

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `buildVdoNinjaUrl()` in `companion/src/ui.ts` -- extend with `&view=` parameter for popout URLs
- `renderRosterStrip()` in `companion/src/ui.ts` -- attach click handler to pills for popout trigger
- `WsCallbacks` type in `companion/src/ws-client.ts` -- extend with `onPopout` and `onDeactivate` callbacks
- `OscServer::handleOscOnMessageThread()` -- add video address prefix matching (same pattern as remote user addresses)
- Phase 10 dynamic address generation pattern in `OscServer::timerCallback()` -- send `/video/active` feedback

### Established Patterns
- WebSocket JSON message protocol: `{type:"...", ...}` with type guards
- OSC positional index model: `/JamWide/remote/{idx}/...` (reuse for `/JamWide/video/popout/{idx}`)
- localStorage persistence for companion settings (effects, bandwidth profile)
- `juce::URL::launchInDefaultBrowser()` for opening companion page

### Integration Points
- Plugin VideoCompanion needs new `requestPopout(streamId)` method sending WS message
- Plugin VideoCompanion::deactivate() needs to send `{type:'deactivate'}` before stopping WS
- OscServer needs new video address handling in handleOscOnMessageThread()
- OscServer::timerCallback() needs video active state feedback
- Companion main.ts needs popout window tracking (Map<streamId, Window>)
- Companion needs new popout.html + popout.ts entry point

</code_context>

<specifics>
## Specific Ideas

No specific requirements -- open to standard approaches for popout window management and OSC address handling.

</specifics>

<deferred>
## Deferred Ideas

- Spotlight/focus display mode (single user fills main companion window) -- future phase if needed
- Always-on-top option for popout windows -- requires non-standard browser APIs or Electron
- Popout window position persistence -- complex with changing stream IDs, keep manual for now
- Fullscreen toggle button in popout windows -- could add via Fullscreen API in a future phase
- OSC bandwidth profile control (`/JamWide/video/quality`) -- keep companion-only for now
- OSC buffer delay override -- auto-calculated from BPM/BPI, no manual control needed

</deferred>

---

*Phase: 13-video-display-modes-and-osc-integration*
*Context gathered: 2026-04-07*
