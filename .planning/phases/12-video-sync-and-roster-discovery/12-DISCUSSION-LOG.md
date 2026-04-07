# Phase 12: Video Sync and Roster Discovery - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions captured in CONTEXT.md — this log preserves the discussion.

**Date:** 2026-04-07
**Phase:** 12-video-sync-and-roster-discovery
**Mode:** discuss
**Areas discussed:** Interval-synced buffering, Room security, Roster discovery, Bandwidth profiles

## Gray Areas Identified

1. Interval-synced buffering — how setBufferDelay flows through the stack
2. Room security — how to pass NINJAM password to VDO.Ninja securely
3. Roster discovery — external API vs username convention
4. Bandwidth profiles — where selector lives, what presets to offer

## Decisions Made

### Interval-Synced Buffering
- **Selected:** Automatic from BPM/BPI
- Plugin calculates delay, sends via WebSocket, companion forwards to VDO.Ninja iframe via postMessage

### Room Security
- **Selected:** Hash in URL fragment
- SHA-256 of password + room_id, passed as &hash=#fragment (never sent to server)

### Roster Discovery
- **Selected:** Username convention only
- Phase 11's sanitized usernames already match VDO.Ninja push= stream IDs
- No external API needed (VDO.Ninja API is DRAFT, adds complexity)

### Bandwidth Profiles
- **Selected:** Companion page dropdown
- Low/Balanced/High presets in header dropdown, persists via localStorage
- Same pattern as existing effects dropdown

## Corrections Made

No corrections — all recommended options confirmed.
