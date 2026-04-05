# Phase 7: DAW Sync and Session Polish - Context

**Gathered:** 2026-04-05
**Status:** Ready for planning

<domain>
## Phase Boundary

Transport-aware broadcasting via AudioPlayHead, full JamTaba-style DAW sync with interval alignment, live BPM/BPI changes at interval boundaries without reconnect, session position tracking (interval count, elapsed time, beat position), BPM/BPI vote UI, standalone pseudo-transport, and research deliverables (VDO.Ninja video feasibility, OSC evaluation, MCP assessment). Requirements: SYNC-01, SYNC-02, SYNC-03, SYNC-04, SYNC-05, RES-01, RES-02, RES-03.

NOT in scope: Video implementation (research only), cross-DAW sync implementation (research only), CLAP plugin transport sync, new mixer features, codec changes.

</domain>

<decisions>
## Implementation Decisions

### Transport Sync Behavior
- **D-01:** When DAW transport stops, silence send only — stop broadcasting audio but keep hearing remote participants. Connection stays active.
- **D-02:** Full JamTaba-style sync — 3-state machine (IDLE → WAITING → ACTIVE). Sync button, BPM match validation (integer comparison), PPQ offset calculation, NINJAM interval aligned to DAW measure boundary.
- **D-03:** Sync button lives in ConnectionBar, next to existing Route and Fit buttons.
- **D-04:** During sync WAITING state, send silence — don't broadcast until synced. Remote users hear nothing until host transport starts.
- **D-05:** When sync is active and server BPM changes, auto-disable sync (JamTaba behavior) — notify user to match host tempo and re-sync.
- **D-06:** JUCE target only for DAW sync — CLAP plugin is the legacy target and does not get sync features.
- **D-07:** Sync button hidden in standalone mode — no host transport available, no point showing the button.

### BPM/BPI Vote UI
- **D-08:** Dedicated BPM/BPI vote controls integrated into the BeatBar — click the BPM or BPI display to open an inline edit field.
- **D-09:** Direct edit + Enter to vote — click BPM value, type new number, press Enter sends `!vote bpm N` via existing chat command mechanism. Same for BPI.

### Live BPM/BPI Change Notification
- **D-10:** BeatBar flash/highlight — flash the BPM/BPI text for 2-3 seconds when server changes tempo.
- **D-11:** System chat message — post "[Server] BPM changed from X to Y" in the chat panel when BPM/BPI changes.

### Session Position Display
- **D-12:** Display: interval count, elapsed session time, current beat / total beats, sync status indicator.
- **D-13:** Session info strip lives below the BeatBar — a dedicated info strip.
- **D-14:** Collapsible/toggleable — user controls whether info strip is expanded or hidden.
- **D-15:** Hidden by default — available via a toggle, takes no space when hidden.

### Standalone Pseudo-Transport
- **D-16:** Auto-play on connect — standalone always broadcasts when connected. No play/stop button needed. Connecting IS starting.
- **D-17:** BeatBar works in standalone — it already uses uiSnapshot data from NJClient interval position. No extra work needed for pseudo-beat display.

### Research Deliverables
- **D-18:** Video feasibility doc (RES-01) focuses on VDO.Ninja WebRTC sidecar approach — both the music sync buffer demo (vdo.ninja/alpha/examples/music-sync-buffer-demo) and general VDO.Ninja integration feasibility.
- **D-19:** OSC evaluation (RES-02) and MCP assessment (RES-03) are brief summaries — half-page each, quick feasibility verdict without detailed protocol analysis.
- **D-20:** Research deliverables live in `.planning/references/` alongside existing JAMTABA-DAW-SYNC-ANALYSIS.md.

### State Persistence
- **D-21:** Persist session info strip visibility (expanded/hidden) via ValueTree property. Sync preference does NOT persist — starts fresh each session.

### Claude's Discretion
- PPQ-to-sample offset calculation implementation details (adapt JamTaba algorithm for JUCE AudioPlayHead::PositionInfo)
- Sync state machine internal design (atomic flags, command queue integration)
- BeatBar inline edit implementation (TextEditor overlay, popup, or custom painted)
- Session info strip compact vs expanded layout details
- Research document internal structure and depth beyond stated scope
- BPM vote validation ranges (40-400 BPM, 2-192 BPI per JamTaba constants, or different)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### DAW Sync Implementation Reference
- `.planning/references/JAMTABA-DAW-SYNC-ANALYSIS.md` — Complete JamTaba DAW sync analysis: 3-state machine, PPQ offset calculation, transport edge detection, BPM validation, auto-disable on server BPM change, audio routing during states, thread safety considerations
- `.planning/references/JAMTABA-DAW-SYNC-ANALYSIS.md` §6.1 — JamWide-specific implementation requirements with JUCE API mapping
- `.planning/references/JAMTABA-DAW-SYNC-ANALYSIS.md` §6.3 — Plugin format transport API coverage (VST3, AU, Standalone)

### Video Research References
- `https://github.com/steveseguin/vdo.ninja` — VDO.Ninja source (WebRTC video sidecar approach)
- `https://vdo.ninja/alpha/examples/music-sync-buffer-demo` — Music sync buffer demo for audio-video alignment

### Core Audio Architecture
- `juce/JamWideJuceProcessor.h` — AudioProcessor with 17 stereo output buses, APVTS, uiSnapshot atomics, threading contract
- `juce/JamWideJuceProcessor.cpp` — processBlock, createParameterLayout, getState/setState
- `src/core/njclient.h` — GetPosition(), GetActualBPM(), GetBPI(), updateBPMinfo(), AudioProc signature
- `src/core/njclient.cpp` §743-750 — updateBPMinfo() implementation (BPM receive from server)
- `src/core/njclient.cpp` §809-850 — Interval boundary BPM application in AudioProc
- `src/core/njclient.cpp` §1187 — MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY handler

### UI Components
- `juce/ui/ConnectionBar.h` — Global controls area (Connect, Browse, Fit, Route, Codec). Sync button goes here.
- `juce/ui/BeatBar.cpp` — BeatBar::update(bpi, beat, intervalPos, intervalLen). Vote UI integrates here.
- `juce/JamWideJuceEditor.h` — Editor shell with timer-polled event drain, BeatBar and ConnectionBar integration
- `juce/NinjamRunThread.cpp` §345-355 — uiSnapshot atomic updates (BPM, BPI, interval position)

### Requirements
- `.planning/REQUIREMENTS.md` — SYNC-01 through SYNC-05, RES-01 through RES-03

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **uiSnapshot atomics** — `bpm`, `bpi`, `interval_position`, `interval_length` already exposed for lock-free UI reads
- **BeatBar** — Already animates based on interval position, adapts numbering by BPI range
- **ConnectionBar** — Global controls area with Route, Fit, Codec buttons. Natural home for Sync button.
- **cmd_queue / evt_queue** — SpscRing for UI↔Run thread communication. Sync commands can use this.
- **NJClient::updateBPMinfo()** — Already handles CONFIG_CHANGE_NOTIFY at protocol level. Live BPM/BPI changes work internally.
- **NJClient::GetPosition()** — Returns interval position and length in samples. Already used by NinjamRunThread.
- **NJClient::m_loopcnt** — Internal interval counter, can be exposed for session tracking.

### Established Patterns
- **Timer-driven UI updates** — Editor polls uiSnapshot at 20Hz, BeatBar updated via ChannelStripArea 30Hz timer
- **Command queue** — UI pushes commands to processor via SpscRing, run thread processes
- **ValueTree persistence** — Non-APVTS state persisted via ValueTree properties (chatSidebarVisible, localTransmit, etc.)
- **Atomic snapshot** — Audio-thread-safe state sharing via UiAtomicSnapshot struct

### Integration Points
- **processBlock** — Add AudioPlayHead query, transport edge detection, sync gating logic
- **NinjamRunThread** — Expose m_loopcnt via uiSnapshot for interval count display
- **ConnectionBar** — Add Sync button (hidden in standalone)
- **BeatBar** — Add inline BPM/BPI edit capability for vote UI
- **Editor** — Add collapsible session info strip below BeatBar
- **getStateInformation/setStateInformation** — Persist info strip visibility

</code_context>

<specifics>
## Specific Ideas

- JamTaba DAW sync analysis (`.planning/references/JAMTABA-DAW-SYNC-ANALYSIS.md`) is the blueprint for the sync implementation — adapt the 3-state machine, PPQ offset calc, and BPM validation for JUCE's AudioPlayHead API
- VDO.Ninja (not JamTaba H.264) is the video approach to research — WebRTC sidecar, music sync buffer demo
- BPM/BPI vote is inline in BeatBar — click the number, type, Enter to vote. Minimal, discoverable.
- Session info strip is hidden by default, collapsible — power users toggle it on, casual users don't see it
- Sync button hidden (not disabled) in standalone — avoids confusion entirely

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 07-daw-sync-and-session-polish*
*Context gathered: 2026-04-05*
