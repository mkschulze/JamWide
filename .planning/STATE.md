---
gsd_state_version: 1.0
milestone: v1.1
milestone_name: OSC + Video
status: defining
stopped_at: null
last_updated: "2026-04-05"
last_activity: 2026-04-05
progress:
  total_phases: 0
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-05)

**Core value:** Musicians can jam together online with lossless audio quality and per-user mixing -- in any DAW or standalone.
**Current focus:** Defining requirements for v1.1

## Current Position

Phase: Not started (defining requirements)
Plan: —
Status: Defining requirements
Last activity: 2026-04-05 — Milestone v1.1 started

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- VDO.Ninja browser companion (not embedded WebView) — keeps plugin lightweight, browser handles video rendering
- OSC via juce_osc (IEM pattern) — no external deps, proven across 20+ IEM plugins
- Index-based OSC addressing for remote users — stable fader mapping, name broadcast on roster change
- VDO.Ninja iframe-based API is the only official integration method; companion page approach avoids this complexity
- FLAC is default codec (CODEC-05)
- Non-APVTS state extracted BEFORE replaceState to prevent property loss across JUCE versions

### Pending Todos

(Carried from v1.0)
- Phase 3 audio transmission not working end-to-end — needs debugging (user reported; will address during v1.1)

### Blockers/Concerns

- libFLAC integration approach decided: git submodule at libs/libflac (xiph/flac @ 1.5.0), WITH_OGG OFF.
- JUCE license resolved: GPL, no splash screen required (JUCE_DISPLAY_SPLASH_SCREEN=0).
