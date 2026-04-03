# Phase 4: Core UI Panels - Context

**Gathered:** 2026-04-04
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace the minimal Phase 3 editor with full JUCE Components for connection, chat, server browser, status display, and codec selection. All UI rebuilt as JUCE Components with no Dear ImGui (JUCE-05). Remove unused Timing Guide code. This phase delivers the core interaction surface; mixer controls (volume, pan, mute, solo per channel) are Phase 5.

</domain>

<decisions>
## Implementation Decisions

### Panel Layout
- **D-01:** Mixer-first layout inspired by VB-Audio Voicemeeter Banana — everything visible on one screen, no tabs or navigation
- **D-02:** Remote users displayed as vertical channel strips with VU meters (faders + knobs added in Phase 5)
- **D-03:** Chat panel docked on the right side, always visible
- **D-04:** Connection bar across the top (server address, username, connect/disconnect button, status info, codec selector)
- **D-05:** Server browser opens as a modal overlay, not a separate panel
- **D-06:** Local channel strip displayed alongside remote user strips

### Visual Design
- **D-07:** Full custom LookAndFeel — dark theme, custom-drawn components, pro-audio aesthetic
- **D-08:** SVG assets designed via Sketch MCP server for all UI elements
- **D-09:** VB-Audio Voicemeeter Banana is the primary design reference (dark blue/gray tones, dense layout, big faders, illuminated VU meters)
- **D-10:** Custom-draw VU meters, channel strips, buttons, and all components — no stock JUCE appearance

### Server Browser
- **D-11:** Single click on a server fills the server address into the connection bar
- **D-12:** Double-click fills AND auto-connects
- **D-13:** Each server entry shows: server name + address, user count, BPM/BPI, topic/description
- **D-14:** Server browser overlay closes on connect

### Chat Panel
- **D-15:** Display all message types: regular chat (MSG), join/part notifications, topic/server messages, and private messages (PRIVMSG)
- **D-16:** Auto-scroll to newest messages by default
- **D-17:** Scrolling up pauses auto-scroll; a "jump to bottom" indicator appears
- **D-18:** Different visual styling per message type (color-coded or formatted, matching dark theme)

### License Dialog
- **D-19:** Modal popup dialog appears when server sends license text after connection
- **D-20:** Dark-themed to match the rest of the UI, with Accept/Decline buttons
- **D-21:** Session blocked until user responds (consistent with NINJAM client behavior)

### Window & Scaling
- **D-22:** Fixed window size (no free resizing) — pixel-perfect layout like Voicemeeter Banana
- **D-23:** Scale options: 1x / 1.5x / 2x (accessible via menu or settings) for HiDPI support

### Connection Bar
- **D-24:** Password field hidden by default; a lock icon or toggle reveals it when needed
- **D-25:** Server address and username fields always visible in the top bar
- **D-26:** Codec selector (FLAC/Vorbis dropdown) in the connection/status bar area

### Disconnected State
- **D-27:** Same layout structure as connected state but channel strip area shows empty/dimmed placeholders
- **D-28:** Welcome message/prompt in the main area: "Connect to a server to start jamming"
- **D-29:** Prominent [Browse Servers] button in the empty state
- **D-30:** Chat area shows "Connect to a server to start chatting"

### Cleanup
- **D-31:** Remove Timing Guide code (ui_latency_guide.cpp/h) — user confirmed it's not useful

### Claude's Discretion
- Exact fixed window dimensions (800x600 or larger to fit all panels)
- SVG asset design specifics and exact color values
- Chat message formatting details (timestamps, name colors)
- VU meter update rate and visual style (peak hold, ballistics)
- Scale option UI placement (menu bar, settings gear, etc.)
- Event queue consumption strategy (timer polling vs AsyncUpdater vs hybrid)
- Connection bar field widths and spacing

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Design Reference
- VB-Audio Voicemeeter Banana (https://vb-audio.com/Voicemeeter/banana.htm) — Primary visual design reference. Dark theme, channel strip layout, big faders, illuminated VU meters, dense pro-audio aesthetic.

### Existing UI to Port
- `src/ui/ui_connection.cpp` / `src/ui/ui_connection.h` — ImGui connection panel (port to JUCE Components)
- `src/ui/ui_chat.cpp` / `src/ui/ui_chat.h` — ImGui chat panel with MSG, PRIVMSG, JOIN, PART, TOPIC handling
- `src/ui/ui_server_browser.cpp` / `src/ui/ui_server_browser.h` — ImGui server browser (fetch + display)
- `src/ui/ui_status.cpp` / `src/ui/ui_status.h` — ImGui status display (BPM/BPI, connection state)
- `src/ui/ui_local.cpp` / `src/ui/ui_local.h` — ImGui local channel panel (has codec selector)
- `src/ui/ui_state.h` — ImGui UI state struct (90 members — reference for data model)
- `src/ui/ui_meters.cpp` / `src/ui/ui_meters.h` — ImGui VU meters

### JUCE Foundation (Phase 3)
- `juce/JamWideJuceEditor.cpp` / `juce/JamWideJuceEditor.h` — Current minimal editor to REPLACE
- `juce/JamWideJuceProcessor.h` — Processor with cmd_queue, getClient(), clientLock, APVTS
- `juce/NinjamRunThread.cpp` / `juce/NinjamRunThread.h` — Run thread (command dispatch)

### Thread Communication
- `src/threading/ui_command.h` — Command types (ConnectCommand, SendChatCommand, SetEncoderFormatCommand, etc.)
- `src/threading/ui_event.h` — Event types (ChatMessageEvent, StatusChangedEvent, ServerListEvent, etc.)
- `src/threading/spsc_ring.h` — Lock-free SPSC queue template

### Server List
- `src/net/server_list.cpp` / `src/net/server_list.h` — ServerListFetcher (HTTP via jnetlib, dual format parser)
- `src/ui/server_list_types.h` — ServerListEntry struct

### Code to Remove
- `src/ui/ui_latency_guide.cpp` / `src/ui/ui_latency_guide.h` — Timing Guide (confirmed not useful)

### Requirements
- `.planning/REQUIREMENTS.md` — JUCE-05, UI-01, UI-02, UI-03, UI-07, UI-09

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `jamwide::SpscRing<jamwide::UiCommand, 256>` cmd_queue on Processor — already wired for UI→Run thread commands
- `jamwide::UiEvent` variant with ChatMessageEvent, StatusChangedEvent, UserInfoChangedEvent, TopicChangedEvent, ServerListEvent — all event types needed for Phase 4 already defined
- `NJClient::cached_status` atomic — already used by Phase 3 editor for 10Hz polling
- `ServerListFetcher` — existing HTTP fetcher for public server list, returns `ServerListEvent`
- `SendChatCommand` — already in the command variant, ready for chat input
- `RequestServerListCommand` — already in the command variant, ready for server browser

### Established Patterns
- 10Hz Timer polling of `cached_status` (Phase 3 editor) — extend to poll other NJClient state
- SPSC command queue for UI→Run thread (ConnectCommand, DisconnectCommand already working)
- Event queue for Run thread→UI (defined in ui_event.h but NOT yet consumed by JUCE editor)
- `processorRef` pattern for editor→processor access (renamed from `processor` to avoid -Wshadow-field)
- JUCE AudioProcessorValueTreeState (apvts) for host-exposed parameters

### Integration Points
- `JamWideJuceEditor` — complete replacement (current is minimal Phase 3 placeholder)
- `JamWideJuceProcessor::createEditor()` — returns new editor instance
- `NinjamRunThread` — may need to push events to a queue that JUCE editor consumes
- `CMakeLists.txt` JUCE target — add new source files for JUCE UI components
- License callback in NJClient — currently auto-accepts; needs to signal UI and wait for response

</code_context>

<specifics>
## Specific Ideas

- VB-Audio Voicemeeter Banana: dark blue-gray background, illuminated green/yellow/red VU meters, channel strips with labels at top, faders below meters, routing buttons as toggle pills
- Big faders are essential — user specifically requested "big faders like in the VB audio"
- Channel strips should feel like a real mixer console, not a generic list
- The existing ImGui UI has working implementations of all panels — use as functional reference for behavior, but redesign visually from scratch for JUCE

</specifics>

<deferred>
## Deferred Ideas

- **DAW sync from JamTaba** (`/Users/cell/dev/JamTaba`) — User wants to implement JamTaba's DAW sync mechanism. This is Phase 7 scope (SYNC-01 through SYNC-05). Capture JamTaba's approach during Phase 7 research.
- **VDO.ninja video integration** — User wants VDO.ninja as a video solution for JamWide users. Video was scoped as research-only this milestone (RES-01). Consider as its own phase in a future milestone.
- **Timing Guide removal** — In-scope cleanup for Phase 4 (D-31), not deferred.

### Reviewed Todos (not folded)
None — no pending todos matched Phase 4 scope.

</deferred>

---

*Phase: 04-core-ui-panels*
*Context gathered: 2026-04-04*
