# JamWide Retrospective

## Milestone: v1.0 — MVP

**Shipped:** 2026-04-05
**Phases:** 8 | **Plans:** 21

### What Was Built
- FLAC lossless codec integrated into NINJAM protocol alongside Vorbis
- Full JUCE plugin (VST3/AU/CLAP/Standalone) with pluginval validation and GitHub Actions CI
- NJClient audio bridge with processBlock integration and NinjamRunThread lifecycle
- Complete custom UI with Voicemeeter Banana dark theme LookAndFeel
- Full mixer with VbFader, VU meters, per-channel controls, state persistence
- 34-channel multichannel output routing with 3 auto-assign modes
- DAW transport sync with BPM/BPI voting and session tracking
- Research deliverables: VDO.Ninja video feasibility, OSC evaluation, MCP assessment

### What Worked
- Vertical slice phasing (scaffolding → audio → UI → controls → routing → sync) kept each phase shippable
- GSD planning workflow with discuss → plan → execute → verify cycles caught integration gaps early
- Phase 8 gap closure pattern (milestone audit → targeted fixes) was efficient
- IEM Plugin Suite as OSC reference and VDO.Ninja music-sync-buffer-demo as video reference gave concrete implementation targets for v1.1

### What Was Inefficient
- Phase 4 and 5 had some scope overlap (VU meters appeared in Phase 4 plan but belonged more naturally in Phase 5)
- Milestone audit discovered CODEC-05 doc mismatch that could have been caught by updating requirements when the decision was made

### Patterns Established
- Voicemeeter Banana dark theme as the UI design language
- SPSC event queue pattern for thread-safe UI↔network communication
- Atomic snapshot pattern for audio thread → UI data flow
- Command queue pattern for UI → NJClient thread-safe dispatch
- Index-based user addressing (stable indices, name broadcast separately)

### Key Lessons
- Default to Vorbis for interop — FLAC as opt-in preserves backward compatibility
- Keep video rendering in the browser, not the plugin — WebView in audio plugins is a trap
- OSC is the practical path for remote control — MCP is too high-latency for real-time sync

## Cross-Milestone Trends

| Metric | v1.0 |
|--------|------|
| Phases | 8 |
| Plans | 21 |
| LOC | 18,300 |
| Timeline | 30 days |
| Gap closure phases | 1 (Phase 8) |
