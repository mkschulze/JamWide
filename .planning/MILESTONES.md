# Milestones

## v1.0 MVP (Shipped: 2026-04-05)

**Phases completed:** 8 phases, 21 plans, 42 tasks

**Key accomplishments:**

- Custom LookAndFeel with Voicemeeter Banana dark theme, Processor-owned event queues and cachedUsers, full NinjamRunThread callbacks with license deadlock prevention and GetRemoteUsersSnapshot
- ConnectionBar with server/codec/status/scale and ChatPanel with color-coded auto-scrolling messages, composed in a rewritten Editor with 20Hz SPSC event drain
- Segmented LED VU meters, channel strips, beat bar, and mixer area container with centralized 30Hz timer and live remote VU levels
- Custom VbFader component with power-curve mapping, drawLinearSlider for pan/metronome, and 12 APVTS local channel parameters
- VbFader, pan slider, mute/solo buttons on all strips with stable-identity remote command dispatch via findRemoteIndex()
- Local 4-channel expand/collapse with APVTS-attached controls, metronome slider/mute in master strip, and multi-bus processBlock collecting all 4 stereo input buses
- Full plugin state save/restore with explicit schema, ordered non-APVTS extraction before replaceState, and input bus validation
- 34-channel audio path with per-bus routing, main-mix accumulation (master vol on remote buses, metronome excluded), SetRoutingModeCommand dispatch with metronome-safe auto-assign, and routing mode state persistence
- Route button with popup menu for Manual/By User/By Channel quick-assign, updated routing selectors (Main Mix + Remote 1-15) with green feedback and command queue wiring
- AudioPlayHead transport integration with single-atomic sync state machine, PPQ offset calculation, seek/loop handling, and BPM/BPI change detection with reason-aware events
- Sync button with 3-state feedback, BPM/BPI inline vote editing with flash animation, SessionInfoStrip, TX button on all local channels, chat cleanup, and plugin renaming
- VDO.Ninja video feasibility with latency comparison, OSC per-DAW evaluation matrix with fixed rubric, and MCP bridge assessment with transport/session/workflow use-case separation
- Commit:

---
