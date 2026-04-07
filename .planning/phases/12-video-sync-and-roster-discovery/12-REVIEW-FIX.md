---
phase: 12-video-sync-and-roster-discovery
fixed_at: 2026-04-07T22:11:05Z
review_path: .planning/phases/12-video-sync-and-roster-discovery/12-REVIEW.md
iteration: 1
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 12: Code Review Fix Report

**Fixed at:** 2026-04-07T22:11:05Z
**Source review:** .planning/phases/12-video-sync-and-roster-discovery/12-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 4
- Fixed: 4
- Skipped: 0

## Fixed Issues

### CR-01: Data race in `sendConfigToClient` -- IXWebSocket callback reads unsynchronized shared state

**Files modified:** `juce/video/VideoCompanion.cpp`, `juce/video/VideoCompanion.h`
**Commit:** 2d53634
**Applied fix:** Added `SessionSnapshot` struct to capture `currentRoom_`, `currentPush_`, `wsPort_`, and `cachedDelayMs_` by value before starting the WebSocket server. The snapshot is passed into the IXWebSocket callback lambda by copy, so `sendConfigToClient` reads the snapshot instead of unsynchronized shared members. Also moved the initial `cachedDelayMs_` computation to before the server start so the snapshot includes it. This eliminates the data race without requiring a lock inside the IXWebSocket callback thread.

### WR-01: Detached thread in destructor path can outlive the `VideoCompanion` object's allocator

**Files modified:** `juce/video/VideoCompanion.cpp`, `juce/video/VideoCompanion.h`
**Commit:** 899bc6b
**Applied fix:** Replaced the detached `std::thread` in `stopWebSocketServer()` with `std::async(std::launch::async, ...)`, storing the returned `std::future<void>` in a new `stopFuture_` member. The destructor now calls `stopFuture_.wait()` before proceeding with its own synchronized stop, ensuring the async teardown task completes before the object (or DLL) is destroyed. Added `#include <future>` to the header.

### WR-02: `parseInt` result not validated -- `NaN` port passed to WebSocket constructor

**Files modified:** `companion/src/main.ts`
**Commit:** c99b8a2
**Applied fix:** Replaced single `parseInt` call with a two-step validation: `rawPort` is parsed first, then validated with `Number.isFinite(rawPort) && rawPort > 0 && rawPort < 65536`. Falls back to default port 7170 if validation fails, preventing `ws://127.0.0.1:NaN` URLs from malformed `wsPort` query parameters.

### WR-03: `broadcastBufferDelay` and `sendConfigToClient` can send `delayMs: 0` to clients

**Files modified:** `juce/video/VideoCompanion.cpp`
**Commit:** ecc3438
**Applied fix:** Added a guard after computing the delay: the result is stored in a local `computed` variable and checked with `if (computed <= 0) return;` before updating `cachedDelayMs_` or broadcasting. This prevents extreme BPM values (where integer truncation yields zero) from sending `delayMs: 0` to VDO.Ninja, which would disable chunked buffering.

## Skipped Issues

None -- all in-scope findings were fixed.

---

_Fixed: 2026-04-07T22:11:05Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
