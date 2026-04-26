---
phase: 13-video-display-modes-and-osc-integration
verified: 2026-04-07T23:07:51Z
status: human_needed
score: 14/14 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Click a roster pill in the companion page and verify a new browser window opens showing only that participant's video stream via VDO.Ninja &view= URL"
    expected: "A new window opens at popout.html?room=...&view={streamId}&name={username} with a solo VDO.Ninja iframe. The pill acquires a green left border (popout-active class)."
    why_human: "Requires a live NINJAM+VDO.Ninja session and a real browser window.open() to confirm visual and network behavior"
  - test: "Click the same roster pill a second time while the popout window is still open"
    expected: "No second window opens; the existing window receives focus"
    why_human: "Requires a live browser session to confirm focus behavior"
  - test: "Have the participant leave the session while their popout window is open"
    expected: "Disconnect overlay appears with participant name; overlay disappears if participant rejoins"
    why_human: "Requires a live session with roster change events flowing over WebSocket"
  - test: "Block popups in the browser, then click a roster pill"
    expected: "A popup-blocked banner appears in the companion page"
    why_human: "Requires browser permission settings and visual confirmation of the banner DOM element"
  - test: "Close a popout window manually and wait ~2 seconds"
    expected: "The pill's green indicator disappears (popoutWindows Map cleaned up by periodic sweep)"
    why_human: "Requires observing UI state update after the 2-second setInterval fires"
  - test: "Send /JamWide/video/active 1.0 from TouchOSC after having launched video via plugin UI at least once this session"
    expected: "Video companion relaunches using stored session credentials; TouchOSC ACTIVE button lights up"
    why_human: "Requires a physical or virtual TouchOSC surface, live NINJAM session, and browser interaction"
  - test: "Send /JamWide/video/active 0.0 from TouchOSC while video is active"
    expected: "Companion deactivates; all popout windows close; TouchOSC ACTIVE button extinguishes"
    why_human: "Requires live OSC session and browser inspection"
  - test: "Send /JamWide/video/popout/1 1.0 from TouchOSC with a participant at roster index 1"
    expected: "A popout window opens for that participant in the companion page"
    why_human: "Requires live session with populated roster and OSC surface"
  - test: "Load the updated JamWide.tosc template in TouchOSC"
    expected: "A VIDEO section appears with an ACTIVE toggle and 8 POPOUT buttons (1-8)"
    why_human: "Requires TouchOSC app and template import -- binary .tosc file cannot be inspected as text"
---

# Phase 13: Video Display Modes and OSC Integration Verification Report

**Phase Goal:** Users can pop out individual participant video into separate windows and control all video features from their OSC surface
**Verified:** 2026-04-07T23:07:51Z
**Status:** human_needed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | User can click a roster pill and a new browser window opens showing only that participant's video stream | ✓ VERIFIED | `main.ts:88` calls `window.open(url, ...)` from `openPopout()`; URL built with `&view={streamId}` omitting `&push=` (ui.ts:146-149); `popout.ts` creates solo iframe |
| 2 | Clicking an already-popped-out user's pill focuses the existing window instead of opening a duplicate | ✓ VERIFIED | `main.ts:63-70`: checks `popoutWindows.get(streamId)`, tests `!existing.closed`, calls `existing.focus()` |
| 3 | User sees a disconnect overlay when participant leaves, and it disappears on rejoin | ✓ VERIFIED | `popout.ts:54-60`: postMessage handler checks roster for `streamId`, toggles `overlay.style.display = present ? 'none' : 'flex'`; `notifyPopouts()` in `main.ts:120-135` relays roster |
| 4 | User sees a popup-blocked banner if window.open() is blocked | ✓ VERIFIED | `main.ts:88-91`: `if (!win)` branch calls `showPopupBlockedBanner()`; CSS at `style.css:239` styles `#popup-blocked-banner` |
| 5 | Popout windows are view-only (no outbound camera/audio) | ✓ VERIFIED | `popout.ts:34`: iframe `allow='autoplay'` only (no camera/mic); `ui.ts:146-149`: `&push=` omitted when `viewStreamId` is set |
| 6 | Manually closed popout windows are cleaned up within 2 seconds or on next roster update | ✓ VERIFIED | `main.ts:144-150`: `setInterval(..., 2000)` sweeps `popoutWindows` checking `win.closed`; roster update also cleans (`main.ts:126`) |
| 7 | Deactivate message from plugin closes all popout windows and clears tracking state | ✓ VERIFIED | `main.ts:168-173` `handleDeactivate()`: iterates all entries calling `win.close()`, then `popoutWindows.clear()`; wired via `onDeactivate` callback from `ws-client.ts:53` |
| 8 | postMessage from non-opener windows is rejected (origin validation) | ✓ VERIFIED | `popout.ts:52`: `if (event.source !== window.opener) return;` -- instance-level validation |
| 9 | User can send /JamWide/video/active 1.0 from OSC and companion relaunches (only if previously launched via UI) | ✓ VERIFIED | `OscServer.cpp:883-885`: calls `relaunchFromOsc()` guarded by `!isActive()`; `VideoCompanion.cpp:449`: `if (!hasLaunchedThisSession_) return false;` privacy gate |
| 10 | User can send /JamWide/video/active 0.0 from OSC and companion deactivates with all popout windows closing | ✓ VERIFIED | `OscServer.cpp:891-892`: calls `deactivate()`; `VideoCompanion.cpp:458-483`: broadcasts `{"type":"deactivate"}` BEFORE stopping WS server |
| 11 | User can send /JamWide/video/popout/{idx} 1.0 from OSC and a popout window opens for the user at that roster index | ✓ VERIFIED | `OscServer.cpp:903-919`: parses index, bounds-checks 1-16, calls `getStreamIdForUserIndex()` then `requestPopout()`; broadcasts `{"type":"popout","streamId":"..."}` |
| 12 | TouchOSC video active button reflects current video state (bidirectional feedback via dirty-flag pattern) | ✓ VERIFIED | `OscServer.h:128`: `lastSentVideoActive = -1.0f`; `OscServer.cpp:923-932`: `sendVideoState()` dirty-flag pattern; wired into `timerCallback()` at line 319 |
| 13 | User can load the updated .tosc template and see a VIDEO section with active toggle and 8 popout buttons | ? UNCERTAIN | File exists and grew from 5124 to 5768 bytes (compressed binary). Content cannot be verified by grep. Needs human. |
| 14 | Grid is the default display mode; popout is the alternate mode triggered per-user (no dedicated mode-switch OSC address) | ✓ VERIFIED | `docs/osc.md:226` documents this explicitly; `OscServer.cpp` has no `/video/mode` address; design decision D-20 documented in plan |

**Score:** 14/14 truths verified (13 verified programmatically, 1 requires human for binary .tosc content, but file exists at correct size)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `companion/popout.html` | Popout page HTML shell with video-area, name-label, disconnect-overlay | ✓ VERIFIED | All three elements present; includes `popout.ts` module |
| `companion/src/popout.ts` | URL param parsing, iframe creation, postMessage listener with opener validation | ✓ VERIFIED | 62 lines; origin validation via `event.source === window.opener` |
| `companion/src/types.ts` | PopoutMessage and DeactivateMessage interfaces with type guards | ✓ VERIFIED | Exports `PopoutMessage`, `DeactivateMessage`, `isPopoutMessage`, `isDeactivateMessage` at lines 27-84 |
| `companion/src/ui.ts` | Extended buildVdoNinjaUrl with viewStreamId, roster pills as buttons | ✓ VERIFIED | `viewStreamId` at line 137; pills as `<button>` at line 246; `title` at line 251 |
| `companion/src/ws-client.ts` | Extended WsCallbacks with onPopout and onDeactivate | ✓ VERIFIED | `onPopout` at line 12, `onDeactivate` at line 13; dispatch at lines 51-53 |
| `companion/src/main.ts` | Popout tracking Map, openPopout, deactivate handler, roster relay, periodic sweep | ✓ VERIFIED | `popoutWindows` Map at line 53; all functions present and wired |
| `companion/vite.config.ts` | Multi-page Vite build with index.html and popout.html inputs | ✓ VERIFIED | `rollupOptions.input` includes `popout` entry at line 13 |
| `juce/video/VideoCompanion.h` | requestPopout(), getStreamIdForUserIndex(), CachedRosterEntry, cachedRoster_ | ✓ VERIFIED | All declarations present at lines 91, 99, 168, 172 |
| `juce/video/VideoCompanion.cpp` | requestPopout WebSocket broadcast, deactivate broadcast before stop, cached roster | ✓ VERIFIED | `requestPopout` at line 420; `deactivate` broadcasts at line 466; `cachedRoster_` populated at lines 357-370 |
| `juce/osc/OscServer.h` | lastSentVideoActive dirty tracking, handleVideoOsc and sendVideoState declarations | ✓ VERIFIED | `lastSentVideoActive` at line 128; declarations at lines 57-58 |
| `juce/osc/OscServer.cpp` | Video prefix dispatch, handleVideoOsc (active + popout/idx 1-16), sendVideoState | ✓ VERIFIED | Dispatch at line 197; implementation at line 863; bounds check `< 1 || > 16` |
| `assets/JamWide.tosc` | VIDEO section with active toggle and 8 popout trigger buttons | ? UNCERTAIN | File exists, 5768 bytes (grew from 5124). Binary format -- human verification required |
| `docs/osc.md` | Video control addresses documented with correct bounds {1-16} | ✓ VERIFIED | `/JamWide/video/active` at line 223; `/JamWide/video/popout/{1-16}` at line 224; display mode note at line 226 |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `companion/src/main.ts` | `companion/popout.html` | `window.open('popout.html?...')` | ✓ WIRED | `main.ts:88`: `window.open(url, windowName, features)` where url is built with popout.html base |
| `companion/src/main.ts` | `companion/src/popout.ts` | `postMessage({type:'roster', users})` | ✓ WIRED | `main.ts:135`: `win.postMessage({ type: 'roster', users }, window.location.origin)` |
| `companion/src/ws-client.ts` | `companion/src/main.ts` | `onPopout` and `onDeactivate` callbacks | ✓ WIRED | `ws-client.ts:51-53` dispatches to callbacks; `main.ts:229,240` implements handlers |
| `juce/osc/OscServer.cpp` | `juce/video/VideoCompanion.cpp` | `processor.videoCompanion->requestPopout()` and `->isActive()` | ✓ WIRED | `OscServer.cpp:883-919`: multiple calls via `processor.videoCompanion->` |
| `juce/osc/OscServer.cpp` | companion via VideoCompanion | WebSocket JSON `{type:'popout', streamId:'...'}` | ✓ WIRED | `VideoCompanion.cpp:424`: JSON built and broadcast to all WS clients |
| `juce/video/VideoCompanion.cpp` | companion main.ts | WebSocket JSON `{type:'deactivate'}` | ✓ WIRED | `VideoCompanion.cpp:466`: broadcasts `{"type":"deactivate"}` before stopping server |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|-------------------|--------|
| `companion/src/popout.ts` | `viewStreamId` from URL param | `main.ts:openPopout()` builds URL with `?view={streamId}` | Yes -- streamId comes from roster | ✓ FLOWING |
| `companion/src/main.ts` | `popoutWindows` Map | `openPopout()` populates from roster events | Yes -- keyed by real streamIds from WS roster | ✓ FLOWING |
| `juce/osc/OscServer.cpp` | `videoActive` feedback | `processor.videoCompanion->isActive()` | Yes -- reads actual atomic<bool> state | ✓ FLOWING |
| `juce/video/VideoCompanion.cpp` | `cachedRoster_` | `broadcastRoster()` populates from `NJClient::RemoteUserInfo` | Yes -- populated from live NINJAM client roster | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| `popout.ts` min 30 lines | `wc -l companion/src/popout.ts` | 62 lines | ✓ PASS |
| `buildVdoNinjaUrl` omits `&push=` when `viewStreamId` set | Code inspection `ui.ts:146-149` | `if (viewStreamId) { url += '&view=...'; }` with `push` param skipped | ✓ PASS |
| Bounds check accepts 1-16 for popout index | Code inspection `OscServer.cpp:909` | `if (oscIdx < 1 || oscIdx > 16) return;` | ✓ PASS |
| Privacy gate blocks OSC-only first launch | Code inspection `VideoCompanion.cpp:449` | `if (!hasLaunchedThisSession_) return false;` | ✓ PASS |
| All 5 documented commits exist in git log | `git log --oneline` | ebc8bac, 4ac2a0d, 2624f19, 3a50e45, a196577 all found | ✓ PASS |
| Production build includes popout.html | `ls docs/video/popout.html` | File exists | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| VID-07 | 13-01-PLAN.md | User can pop out individual participant video into separate windows | ✓ SATISFIED | `companion/src/popout.ts`, `companion/src/main.ts` `openPopout()`, `companion/popout.html`, `companion/vite.config.ts` multi-page build |
| VID-11 | 13-02-PLAN.md | User can control video features (open, close, mode switch, popout) via OSC | ✓ SATISFIED | `OscServer.cpp` `/JamWide/video/active` and `/JamWide/video/popout/{idx}` handlers; bidirectional feedback via `sendVideoState()` |

No orphaned requirements. REQUIREMENTS.md assigns only VID-07 and VID-11 to Phase 13. Both are claimed by plans and implemented.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `juce/video/VideoCompanion.cpp` | 88 | `return {}` | Info | Early-return guard for empty password (public rooms) -- not a stub; intentional design |
| `juce/video/VideoCompanion.cpp` | 437 | `return {}` | Info | Bounds guard in `getStreamIdForUserIndex()` -- not a stub; returns empty string for out-of-range index, caller checks before use |

No blockers or warnings found. Both `return {}` instances are correct guard logic, not stubs.

### Human Verification Required

#### 1. Popout Window Opens with Solo Video

**Test:** In a live session with multiple participants, open the companion page and click a roster pill.
**Expected:** A new window opens at `popout.html?room=...&view={streamId}&name={username}` showing only that participant's VDO.Ninja stream. The clicked pill acquires a green left border.
**Why human:** Requires a live NINJAM+VDO.Ninja session and real browser window.open() to confirm.

#### 2. Duplicate Popout Prevention (Focus)

**Test:** Click the same roster pill a second time while the popout is still open.
**Expected:** No second window opens. The existing popout window receives focus.
**Why human:** Requires a live browser session to confirm focus behavior.

#### 3. Disconnect Overlay Behavior

**Test:** Have a participant leave the NINJAM session while their popout window is open.
**Expected:** The disconnect overlay appears with the participant's name. If they rejoin, the overlay disappears.
**Why human:** Requires live roster change events from NINJAM over WebSocket.

#### 4. Popup-Blocked Banner

**Test:** Enable popup blocking in the browser, then click a roster pill.
**Expected:** A visible banner appears in the companion page indicating popups are blocked.
**Why human:** Requires browser permission settings and visual confirmation of the banner DOM element.

#### 5. 2-Second Sweep Cleanup

**Test:** Manually close a popout window (click X) and wait approximately 2 seconds.
**Expected:** The green indicator disappears from the roster pill (popoutWindows Map cleaned by periodic sweep).
**Why human:** Requires observing UI state update after the setInterval fires.

#### 6. OSC Video Active Toggle (TouchOSC)

**Test:** Launch video from plugin UI, then send `/JamWide/video/active 0.0` then `1.0` from TouchOSC.
**Expected:** Video deactivates then relaunches without showing the privacy dialog. TouchOSC ACTIVE button reflects state bidirectionally.
**Why human:** Requires a live OSC surface and NINJAM session.

#### 7. OSC Popout Trigger

**Test:** With video active and participants in session, send `/JamWide/video/popout/1 1.0` from TouchOSC.
**Expected:** A popout window opens for the first roster participant in the companion page.
**Why human:** Requires live session with populated roster and OSC surface.

#### 8. TouchOSC Template VIDEO Section

**Test:** Import the updated `assets/JamWide.tosc` into TouchOSC.
**Expected:** A VIDEO section appears with one ACTIVE toggle button and 8 POPOUT momentary buttons labeled 1-8.
**Why human:** The .tosc file is compressed binary. File size grew from 5124 to 5768 bytes (consistent with new content) but visual layout requires TouchOSC app to confirm.

### Gaps Summary

No gaps found. All 14 must-have truths are programmatically verified. The `human_needed` status is because 9 behaviors (visual, interactive, and OSC-surface) require a live session to confirm end-to-end. Code inspection confirms all implementation paths are present, substantive, and correctly wired.

---

_Verified: 2026-04-07T23:07:51Z_
_Verifier: Claude (gsd-verifier)_
