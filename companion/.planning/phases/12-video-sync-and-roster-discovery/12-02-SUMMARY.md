---
phase: 12-video-sync-and-roster-discovery
plan: 02
subsystem: companion-page
tags: [vitest, typescript, buffer-delay, bandwidth-profiles, roster-strip, vdo-ninja, websocket]
dependency_graph:
  requires: [companion/src/types.ts, companion/src/ui.ts, companion/src/ws-client.ts]
  provides: [BufferDelayMessage type, isBufferDelayMessage guard, bandwidth profiles, buffer delay relay, roster strip rendering, isBotUser utility, Vitest test suite]
  affects: [companion/index.html, companion/style.css, companion/src/main.ts, docs/video/]
tech_stack:
  added: [vitest, jsdom]
  patterns: [TDD red-green, postMessage relay, localStorage persistence with defensive validation, full state replacement roster, XSS-safe textContent rendering]
key_files:
  created:
    - companion/vitest.config.ts
    - companion/src/__tests__/buffer-delay.test.ts
    - companion/src/__tests__/url-builder.test.ts
    - companion/src/__tests__/bandwidth-profile.test.ts
    - companion/src/__tests__/roster-labels.test.ts
  modified:
    - companion/package.json
    - companion/tsconfig.json
    - companion/src/types.ts
    - companion/src/ui.ts
    - companion/src/ws-client.ts
    - companion/src/main.ts
    - companion/index.html
    - companion/style.css
    - docs/video/index.html
    - docs/video/assets/index-Bla1S-mj.js
    - docs/video/assets/index-g_1o59Wf.css
decisions:
  - VDO.Ninja quality numbering is inverted (quality=0 is highest, quality=2 is lowest) -- BANDWIDTH_PROFILES constants encode this correctly
  - Chunked mode params (chunked, chunkbufferadaptive=0, chunkbufferceil=180000) required for buffer sync beyond 4 seconds
  - Bot user filtering uses case-insensitive prefix matching (handles ninbot_, ninbot2, Jambot_server variants)
  - Buffer delay cached in module-level variable and re-applied on iframe load event for state preservation across reloads
  - Roster strip uses full state replacement (clear + rebuild) per protocol contract, not incremental diff
metrics:
  duration: 5m 51s
  completed: 2026-04-07T21:09:44Z
  tasks_completed: 2
  tasks_total: 2
  tests_added: 39
  files_changed: 14
---

# Phase 12 Plan 02: Companion Page Features Summary

Buffer delay relay via postMessage with chunked mode URL params, bandwidth quality dropdown with defensive localStorage persistence, roster strip with full-state-replacement rendering and bot filtering, hash fragment forwarding to VDO.Ninja iframe, and 39 Vitest unit tests covering all logic paths.

## Task Completion

| Task | Name | Commit | Key Changes |
|------|------|--------|-------------|
| 1 | Vitest setup, types, URL builder, bandwidth profiles, buffer delay relay, roster rendering, and tests | 1d5888b | TDD: 39 tests + all production logic in types.ts and ui.ts |
| 2 | HTML, CSS, ws-client dispatch, main.ts wiring, and build verification | a93f2ea | Bandwidth dropdown, roster strip container, WS dispatch, main.ts wiring, Vite build |

## What Was Built

### Buffer Delay Relay (VID-08)
- `isBufferDelayMessage` type guard validates incoming WebSocket messages
- `applyBufferDelay()` caches delay value and forwards to VDO.Ninja iframe via `postMessage({setBufferDelay: N}, '*')`
- `reapplyCachedBufferDelay()` re-sends cached value after iframe reload (bandwidth or effect change)
- iframe `load` event handler automatically re-applies cached buffer delay

### Room Hash Handling (VID-09)
- Hash fragment parsed once from `window.location.hash` on page load
- Stored in module-level `roomHashFragment` variable
- Forwarded to every `buildVdoNinjaUrl` call as `&password=` parameter
- Survives iframe reloads without re-parsing

### Roster Name Labels (VID-10)
- `renderRosterStrip()` implements full state replacement per protocol contract
- Each call clears all existing pills and rebuilds from new users array
- Uses `textContent` (never innerHTML) for XSS safety (T-12-02 mitigation)
- `isBotUser()` filters known bot prefixes (ninbot, jambot, ninjam) case-insensitively
- Strip hides when no visible (non-bot) users remain
- Roster strip preserved across iframe reloads via DOM node re-attachment

### Bandwidth Quality Dropdown (VID-12)
- Three presets: Low (360p/500kbps), Balanced (720p/1.5Mbps), High (1080p/3Mbps)
- VDO.Ninja quality numbering correctly inverted (low.quality=2, high.quality=0)
- Persisted to localStorage under key `jamwide-bandwidth-profile`
- `getSavedBandwidthProfile()` validates stored value against `VALID_BANDWIDTH_PROFILES` set
- Invalid/missing/corrupted values fall back to 'balanced'
- Changing profile reloads iframe with updated quality and maxvideobitrate params

### Chunked Mode URL Params
- `&chunked` enables VDO.Ninja chunked transfer mode
- `&chunkbufferadaptive=0` disables adaptive buffering (fixed buffer needed for NINJAM sync)
- `&chunkbufferceil=180000` sets maximum buffer ceiling for long NINJAM intervals (up to 3 minutes)

### Test Suite
- 39 Vitest tests across 4 test files
- jsdom environment for DOM manipulation testing
- Covers: type guards, URL building, bandwidth profiles, localStorage validation, buffer delay caching, roster rendering lifecycle, bot filtering, XSS safety

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

1. All 39 Vitest tests pass: `npx vitest run` exits 0
2. TypeScript compiles: `npx tsc --noEmit` exits 0
3. Vite builds: `npx vite build` exits 0
4. Bandwidth dropdown in HTML: confirmed `<select id="bandwidth-profile">`
5. Roster strip in HTML: confirmed `<div id="roster-strip">`
6. XSS safety: `textContent` used for all user name rendering
7. Chunked mode: `&chunked`, `&chunkbufferadaptive=0`, `&chunkbufferceil=180000` in URL builder
8. Quality numbering: BANDWIDTH_PROFILES.low.quality=2, BANDWIDTH_PROFILES.high.quality=0 (inverted)
9. localStorage validation: `VALID_BANDWIDTH_PROFILES` Set guards all reads
10. Full state replacement: comment and implementation confirmed in renderRosterStrip

## Self-Check: PASSED

All 12 key files verified present on disk. Both task commits (1d5888b, a93f2ea) verified in git log.
