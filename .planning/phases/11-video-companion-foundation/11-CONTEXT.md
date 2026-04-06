# Phase 11: Video Companion Foundation - Context

**Gathered:** 2026-04-06
**Status:** Ready for planning

<domain>
## Phase Boundary

One-click browser-based video companion for NINJAM sessions via VDO.Ninja. Covers VID-01 (one-click launch), VID-02 (auto room ID), VID-03 (audio suppression), VID-04 (video grid), VID-05 (privacy notice), VID-06 (Chromium warning). Does NOT include popout mode (VID-07), buffer sync (VID-08), room security hardening (VID-09), roster discovery (VID-10), OSC video control (VID-11), or bandwidth profiles (VID-12) — those are Phases 12-13.

</domain>

<decisions>
## Implementation Decisions

### Video Button & Plugin UI
- **D-01:** Video launch button lives in the ConnectionBar (top bar), next to connect/disconnect controls. Video is session-related, groups naturally with connection state.
- **D-02:** Toggle button with state indicator. Camera icon changes color: grey = off/inactive, green = video active (browser was opened). Dark Voicemeeter theme styling.
- **D-03:** Button always visible but disabled (greyed out) when not connected to a NINJAM server. Click while disconnected shows tooltip "Connect to a server first."
- **D-04:** Clicking while green re-opens the companion page (re-launch, not stop). Handles case where user closed the browser tab.

### Privacy & First-Run Flow
- **D-05:** On every video launch, show a modal dialog with: (1) IP exposure privacy notice ("Video uses VDO.Ninja WebRTC. Your IP address will be visible to other participants."), (2) Chromium browser recommendation if default browser is non-Chromium. User must click "I understand" to proceed.
- **D-06:** No persistence of privacy acknowledgment — modal shows every session. No state version bump needed.
- **D-07:** Browser detection via platform APIs. macOS: read LSHandlerURLScheme via Launch Services to get default browser bundle ID. Windows: read HKCU\Software\Microsoft\Windows\Shell\Associations for http handler. Known Chromium bundle IDs: com.google.Chrome, com.microsoft.edgemac, com.brave.Browser, com.vivaldi.Vivaldi, etc.
- **D-08:** Modal reuses the existing LicenseDialog pattern (dark modal, text + accept button). Consistent UX with the NINJAM license acceptance flow.

### Companion Page Design
- **D-09:** Branded JamWide page with logo, session info header, and connection status. VDO.Ninja iframe fills the main viewport with &cleanoutput (no VDO.Ninja UI chrome).
- **D-10:** Audio suppression via &noaudio VDO.Ninja URL parameter. No audio elements created at all — NINJAM handles all audio.
- **D-11:** Companion page receives configuration from plugin via local WebSocket. Plugin runs a WS server; companion page is the WS client. Enables two-way communication.
- **D-12:** WebSocket server implemented using a lightweight WS library compiled into the JUCE plugin (e.g., libwebsockets or header-only lib). Adds a dependency but keeps everything in one process.

### WebSocket Protocol
- **D-13:** JSON message format over WebSocket. Plugin sends messages like `{"type":"config","room":"xyz","push":"username","noaudio":true}`. Easy to parse in JS, human-readable for debugging.
- **D-14:** Phase 11 message types: (1) `config` — room, push, noaudio, wsPort. Sent on WS connect. Password is intentionally excluded from the config message — sending the NINJAM session password to a browser page is a security leak risk; the password is only used server-side for room ID derivation. (2) `roster` — `{type:"roster", users:[{idx, name, streamId}]}`. Sent on roster change. Companion page can show user labels alongside video.
- **D-15:** No auto-reconnect on WebSocket drop. Companion page shows a "Reconnect" button or user re-clicks Video in the plugin. Simpler; avoids complexity of exponential backoff for foundation phase.

### Room ID & Connection Flow
- **D-16:** Room ID derived from NINJAM server address + session password. Private servers with passwords get unique rooms. Hash(server:port + password) sanitized for VDO.Ninja room parameter.
- **D-17:** Public servers with no password: use fixed salt — Hash(server:port + "jamwide-public"). Deterministic; all JamWide users on that server land in the same room. Not trivially guessable from server name.
- **D-18:** One-click flow: (1) Show privacy/browser modal, wait for accept. (2) Build VDO.Ninja companion URL with room/push/noaudio params. (3) Open default browser via juce::URL::launchInDefaultBrowser(). (4) Start local WebSocket server. (5) Set button to green/active.
- **D-19:** No video state persistence across DAW sessions. Video active state resets on reload — user must click Video again each session.

### GitHub Pages Hosting
- **D-20:** Companion page source lives in the same repo at /docs/video/. GitHub Pages serves from /docs. Single repo, single CI pipeline, version-locked with the plugin.
- **D-21:** Build step via Vite. TypeScript support, dev server with hot reload. Production output to /docs/video/. Custom subdomain (e.g., video.jamwide.app) with CNAME in repo.

### Username-to-Stream Mapping
- **D-22:** NINJAM usernames sanitized to alphanumeric + underscores only for VDO.Ninja push= stream IDs. "Dave@guitar" becomes "Daveguitar", "user name" becomes "username".
- **D-23:** Collision resolution via index suffix: if two users sanitize to the same name, second gets "_2" suffix (join-order based). VDO.Ninja sees distinct streams.

### Claude's Discretion
- WebSocket library choice (libwebsockets, ixwebsocket, or header-only alternative)
- Exact companion page visual design (layout grid, colors, font choices within JamWide branding)
- WebSocket port selection strategy (fixed default vs dynamic)
- VDO.Ninja URL parameter fine-tuning beyond the decided &noaudio and &cleanoutput
- Hash function choice for room ID derivation (SHA256, MD5, or similar)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### VDO.Ninja Design Spec
- `docs/superpowers/specs/2026-04-05-v1.1-osc-video-design.md` — Full VDO.Ninja architecture, URL parameters, external API, sync mechanism, display modes, browser requirements

### Plugin UI (extend these)
- `juce/ui/ConnectionBar.h` — ConnectionBar component where video button will be added
- `juce/ui/ConnectionBar.cpp` — ConnectionBar implementation, layout, button patterns
- `juce/ui/LicenseDialog.h` — Modal dialog pattern to reuse for privacy notice
- `juce/ui/LicenseDialog.cpp` — LicenseDialog implementation (dark theme modal with accept button)

### Processor & State
- `juce/JamWideJuceProcessor.h` — Main processor class, state persistence, member declarations
- `juce/NinjamRunThread.cpp` — UserInfoChangedEvent dispatch, connection state management

### Existing Patterns
- `juce/ui/JamWideLookAndFeel.h` — Custom Voicemeeter dark theme LookAndFeel
- `juce/ui/JamWideLookAndFeel.cpp` — Color palette, button styling, component drawing

### GitHub Pages Target
- `docs/` — Existing docs directory; companion page goes in `docs/video/`

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `LicenseDialog` (juce/ui/LicenseDialog.cpp): Modal dialog with dark theme, text display, and accept button. Reuse pattern for privacy notice modal.
- `ConnectionBar` (juce/ui/ConnectionBar.cpp): Top bar component with connect/disconnect buttons. Video button integrates here.
- `JamWideLookAndFeel` (juce/ui/JamWideLookAndFeel.cpp): Custom dark theme with Voicemeeter Banana styling. All new UI components must use this.
- `juce::URL::launchInDefaultBrowser()`: JUCE built-in for opening URLs in default browser.

### Established Patterns
- SPSC cmd_queue: UI thread is single producer, run thread is consumer. Video commands should follow this pattern.
- UiEvent variant: Run thread → UI communication via typed events in ui_event.h. Roster changes could use this path.
- State persistence: JamWideJuceProcessor handles save/load via getStateInformation/setStateInformation. D-19 says no video persistence, so no changes needed.

### Integration Points
- `ConnectionBar::resized()` — Add video button to layout
- `JamWideJuceProcessor` — Add WebSocket server lifecycle (start on video launch, stop on destroy)
- `NinjamRunThread` — UserInfoChangedEvent already fires on roster changes; bridge to WebSocket roster message
- `CMakeLists.txt` — Add WebSocket library dependency
- `docs/video/` — New directory for companion page (Vite project)

</code_context>

<specifics>
## Specific Ideas

- Branded companion page with JamWide logo — not just a bare iframe wrapper
- Custom subdomain (video.jamwide.app) for professional URL
- Vite build for companion page — modern DX with TypeScript
- Privacy modal every session (no "don't show again") — conservative approach
- Roster message in foundation phase even though advanced roster discovery is Phase 12 — enables user labels in the companion page grid

</specifics>

<deferred>
## Deferred Ideas

- Popout mode (individual browser windows per user) — Phase 13 (VID-07)
- setBufferDelay sync with NINJAM intervals — Phase 12 (VID-08)
- Room password hardening (hash in URL fragment) — Phase 12 (VID-09)
- Advanced roster discovery via VDO.Ninja external API — Phase 12 (VID-10)
- OSC video control (/JamWide/video/*) — Phase 13 (VID-11)
- Bandwidth-aware video profiles — Phase 12 (VID-12)
- Auto-reconnect WebSocket with exponential backoff — future enhancement
- Auto-launch video on connect preference — future enhancement (requires state persistence)

</deferred>

---

*Phase: 11-video-companion-foundation*
*Context gathered: 2026-04-06*
