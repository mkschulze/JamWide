---
phase: 13-video-display-modes-and-osc-integration
plan: 01
subsystem: companion-popout
tags: [video, popout, vdo-ninja, postMessage, multi-window]
dependency_graph:
  requires: []
  provides: [popout-page, popout-types, popout-window-tracking, view-only-url]
  affects: [companion-main, companion-ws-client, companion-types, companion-ui, vite-config]
tech_stack:
  added: []
  patterns: [postMessage-origin-validation, periodic-window-cleanup, multi-page-vite-build]
key_files:
  created:
    - companion/popout.html
    - companion/src/popout.ts
    - companion/src/__tests__/popout-url.test.ts
    - companion/src/__tests__/popout-window.test.ts
    - docs/video/popout.html
  modified:
    - companion/src/types.ts
    - companion/src/ui.ts
    - companion/src/ws-client.ts
    - companion/src/main.ts
    - companion/vite.config.ts
    - companion/style.css
decisions:
  - "postMessage validated via event.source === window.opener (stronger than origin check for same-origin windows)"
  - "Popout iframe uses allow='autoplay' only (no camera/mic -- view-only)"
  - "Periodic 2-second sweep catches manually closed popout windows between roster updates"
  - "postMessage targetOrigin uses window.location.origin (not wildcard '*')"
metrics:
  duration_seconds: 273
  completed: "2026-04-07T22:53:41Z"
  tasks: 2
  files_created: 5
  files_modified: 6
  tests_added: 17
  tests_total: 56
---

# Phase 13 Plan 01: Per-Participant Video Popout Windows Summary

Per-participant video popout via window.open with postMessage roster relay, origin validation (event.source === window.opener), 2-second periodic cleanup sweep, and view-only VDO.Ninja iframe (autoplay-only permissions, no &push= in URL).

## What Was Built

### Task 1: Types, URL Builder Extension, Popout Page, and Tests (TDD)

Extended the companion page type system and URL builder to support popout windows:

- **types.ts**: Added `PopoutMessage` and `DeactivateMessage` interfaces with `isPopoutMessage()` and `isDeactivateMessage()` type guard functions. Updated `PluginMessage` union to include both.
- **ui.ts (buildVdoNinjaUrl)**: Added optional `viewStreamId` parameter (6th argument). When set, appends `&view={streamId}` and omits `&push=` entirely (popout windows are view-only).
- **ui.ts (renderRosterStrip)**: Changed pill elements from `<span>` to `<button>` with `title="Pop out {Username} video"` for accessibility and click handling.
- **popout.html**: New minimal HTML shell with video-area, name-label, and disconnect-overlay elements.
- **popout.ts**: Entry point for popout page -- parses URL params, builds solo VDO.Ninja iframe URL, creates iframe with `allow='autoplay'` only, listens for postMessage with `event.source === window.opener` validation.
- **vite.config.ts**: Updated to multi-page build with rollupOptions input for both `index.html` and `popout.html`.
- **Tests**: 17 new tests covering viewStreamId URL behavior, type guards, and button/title rendering.

### Task 2: Popout Window Management, WS Callbacks, Deactivate Handler, Production Build

Wired the full popout lifecycle into the main companion page:

- **ws-client.ts**: Extended `WsCallbacks` with `onPopout` and `onDeactivate`. Added dispatch branches for both new message types.
- **main.ts**: Added `popoutWindows` Map tracking, `openPopout()` (with duplicate-focus prevention), `notifyPopouts()` (roster relay with stale window cleanup), `handleDeactivate()` (close all + clear), `showPopupBlockedBanner()`, `updatePillIndicators()`, and a `setInterval` 2-second periodic sweep.
- **style.css**: Added button reset styles for `.roster-pill`, hover state, `.popout-active` indicator class (green left border), and `#popup-blocked-banner` styling.
- **Production build**: Rebuilt `docs/video/` with both pages.

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| `event.source === window.opener` for postMessage validation | Stronger than origin check for same-origin windows; validates exact window instance |
| `allow='autoplay'` only on popout iframe | Popout is view-only; no camera/mic permissions needed (addresses review MEDIUM) |
| `window.location.origin` as postMessage targetOrigin | Precise targeting instead of wildcard `*` (addresses Codex Round 2 MEDIUM) |
| 2-second periodic sweep for closed windows | Catches manually closed popouts between roster updates (addresses review HIGH) |
| Roster pills as `<button>` not `<span>` | Proper interactive element with title attribute for accessibility |

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

- All 56 tests pass (39 existing + 17 new)
- TypeScript compiles without errors (`tsc --noEmit`)
- Vite production build produces both `index.html` and `popout.html` in `docs/video/`
- All acceptance criteria checks pass for both tasks

## Commits

| Task | Hash | Message |
|------|------|---------|
| 1 (RED) | ebc8bac | test(13-01): add failing tests for popout types, URL builder, and roster buttons |
| 1 (GREEN) | 4ac2a0d | feat(13-01): implement popout types, URL builder extension, popout page, and Vite multi-page |
| 2 | 2624f19 | feat(13-01): add popout window management, WS callbacks, deactivate handler, and production build |

## Self-Check: PASSED

All 5 created files verified present. All 3 commits verified in git log.
