---
status: partial
phase: 12-video-sync-and-roster-discovery
source: [12-VERIFICATION.md]
started: 2026-04-07
updated: 2026-04-07
---

## Current Test

[awaiting human testing]

## Tests

### 1. Buffer delay sync with live NINJAM session
expected: VDO.Ninja actually receives and applies setBufferDelay postMessage when BPM/BPI changes
result: [pending]

### 2. Password fragment locks VDO.Ninja room
expected: Unauthorized users cannot join; fragment not visible in network requests
result: [pending]

### 3. Roster strip live updates
expected: Add/remove/bot-filtering works correctly in a real NINJAM session
result: [pending]

### 4. Bandwidth profile visual quality change
expected: iframe URL params update correctly, quality visibly changes, localStorage persists across reload
result: [pending]

## Summary

total: 4
passed: 0
issues: 0
pending: 4
skipped: 0
blocked: 0

## Gaps
