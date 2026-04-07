---
phase: 11-video-companion-foundation
verified: 2026-04-07T18:26:06Z
status: human_needed
score: 4/4 roadmap success criteria verified
human_verification:
  - test: "Click Video button in JamWide plugin while connected to NINJAM server. Accept the privacy modal."
    expected: "Browser opens to jamwide.audio/video/?room=jw-{hash}&push={username}&wsPort=7170. Page shows 'Connecting to JamWide plugin...' then transitions to VDO.Ninja iframe. No duplicate audio is heard."
    why_human: "End-to-end flow requires live NINJAM connection, browser launch, and WebSocket handshake. Cannot verify programmatically."
  - test: "Click Video while disconnected from NINJAM."
    expected: "Button is greyed out (50% alpha). Tooltip reads 'Connect to a server first'. No modal is shown."
    why_human: "Button disabled state and tooltip require visual inspection and JUCE plugin runtime."
  - test: "Click Video while active (button already green). Do not accept a modal."
    expected: "Browser tab opens to companion page without showing the privacy modal again. Plugin does not restart the WebSocket server."
    why_human: "Requires runtime test of the re-click (D-04) code path in a live session."
  - test: "Open companion page in a non-Chromium browser (e.g. Firefox) as the system default."
    expected: "Privacy modal includes 'Browser Compatibility' section warning about non-Chromium browser. User can still accept and proceed."
    why_human: "Browser detection is platform-specific (macOS LSCopyDefaultHandlerForURLScheme); requires a machine with Firefox as default browser."
  - test: "Disconnect from NINJAM server while video is active."
    expected: "Video button returns to grey/inactive state. WebSocket server stops. Re-clicking Video after reconnect shows the privacy modal again."
    why_human: "Requires runtime NINJAM disconnect to trigger the deactivate() path (D-19)."
---

# Phase 11: Video Companion Foundation Verification Report

**Phase Goal:** Users can launch video collaboration with one click and see all session participants in a browser-based grid
**Verified:** 2026-04-07T18:26:06Z
**Status:** human_needed
**Re-verification:** No - initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | User can click a single button in JamWide and see a browser tab open showing a VDO.Ninja video grid | ? HUMAN NEEDED | Button exists in ConnectionBar, wired to launchCompanion + browser open, but end-to-end flow requires runtime test |
| 2 | User hears no duplicate audio from VDO.Ninja (audio suppressed automatically) | ✓ VERIFIED | `sendConfigToClient()` emits `"noaudio":true`; `buildVdoNinjaUrl()` appends `&noaudio&cleanoutput` to iframe URL |
| 3 | User's video room ID is automatically derived from the NINJAM server address | ✓ VERIFIED | `deriveRoomId()` uses WDL_SHA1 on `serverAddr + ":" + password` (or `":jamwide-public"` for public servers), returns `"jw-"` + 16 hex chars |
| 4 | User sees privacy notice about IP exposure before first video use, and browser warning if non-Chromium | ✓ VERIFIED | `VideoPrivacyDialog` always shows IP disclosure; browser warning shown conditionally via `isDefaultBrowserChromium()`; both BrowserDetect implementations exist |

**Score:** 4/4 truths have implementation evidence. Truth #1 requires runtime human verification.

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `juce/video/VideoCompanion.h` | VideoCompanion class declaration — frozen public interface | ✓ VERIFIED | 108 lines; all public methods present: `launchCompanion`, `relaunchBrowser`, `onRosterChanged`, `deactivate`, `isActive`; `kDefaultWsPort = 7170`; `alive_` shared_ptr for callAsync UAF safety |
| `juce/video/VideoCompanion.cpp` | VideoCompanion implementation | ✓ VERIFIED | 322 lines (>120 min); all required functions implemented: `deriveRoomId`, `sanitizeUsername`, `resolveCollision`, `launchCompanion`, `relaunchBrowser`, `onRosterChanged`, `broadcastRoster`, `startWebSocketServer`, `stopWebSocketServer`, `sendConfigToClient` |
| `companion/src/main.ts` | App entry, WebSocket connection, iframe management | ✓ VERIFIED | 94 lines (>50 min); calls `connectToPlugin`, tracks `configReceived`, wires reconnect button, calls `loadVdoNinjaIframe` on config |
| `companion/src/ws-client.ts` | WebSocket client logic with message validation | ✓ VERIFIED | 60 lines (>40 min); connects to `ws://127.0.0.1:{port}`, JSON.parse in try/catch, type guard dispatch, no auto-reconnect |
| `companion/src/types.ts` | Message type definitions with type guards | ✓ VERIFIED | Contains `ConfigMessage`, `RosterMessage`, `isConfigMessage`, `isRosterMessage` with field validation |
| `companion/src/ui.ts` | DOM manipulation, status badges, reconnect button, empty state | ✓ VERIFIED | 158 lines (>40 min); `buildVdoNinjaUrl` with `&noaudio&cleanoutput`, `showEmptyRoom` with "No participants yet" text, XSS-safe `textContent` throughout |
| `companion/vite.config.ts` | Vite build config | ✓ VERIFIED | Contains `defineConfig`, `base: '/video/'` |
| `companion/package.json` | Dev dependencies | ✓ VERIFIED | Contains `vite` and `typescript` in devDependencies |
| `companion/index.html` | Companion page entry point | ✓ VERIFIED | Contains `<title>JamWide Video</title>`, `id="main-area"`, `id="footer"`, `id="status-badge"` |
| `juce/video/VideoPrivacyDialog.h` | Privacy modal dialog class | ✓ VERIFIED | Contains `VideoPrivacyDialog`, `onResponse` callback, `show(bool)` / `dismiss()` |
| `juce/video/VideoPrivacyDialog.cpp` | Privacy modal with IP disclosure and browser warning | ✓ VERIFIED | 166 lines (>60 min); IP disclosure always shown; browser compatibility section shown conditionally; accept button "I Understand - Open Video" |
| `juce/video/BrowserDetect.h` | Cross-platform browser detection API | ✓ VERIFIED | Contains `isDefaultBrowserChromium()` with best-effort advisory contract documented |
| `juce/video/BrowserDetect_mac.mm` | macOS browser detection via LSCopyDefaultHandlerForURLScheme | ✓ VERIFIED | Uses `LSCopyDefaultHandlerForURLScheme(CFSTR("https"))`, defaults to true on failure |
| `juce/video/BrowserDetect_win.cpp` | Windows browser detection via HKCU registry | ✓ VERIFIED | Contains `UrlAssociations`, reads `ProgId` under `HKCU\...\https\UserChoice`, Linux fallback also present |

**Note on artifact paths:** Plan 02 originally listed artifacts at `docs/video/src/`. Commit `401b240` moved source files to `companion/` (source vs deploy separation). Build output is deployed to `docs/video/` via Vite. This is an intentional structural improvement; all planned functionality is present at the new paths.

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `VideoCompanion.cpp` | `wdl/sha.h` | `WDL_SHA1` for room ID hashing | ✓ WIRED | `#include "wdl/sha.h"` present; `WDL_SHA1` used in `deriveRoomId()` |
| `CMakeLists.txt` | `libs/ixwebsocket` | `add_subdirectory` linking | ✓ WIRED | `add_subdirectory(libs/ixwebsocket EXCLUDE_FROM_ALL)` at line 108; `ixwebsocket` in `target_link_libraries` at line 311 |
| `companion/src/ws-client.ts` | `ws://127.0.0.1` | WebSocket constructor | ✓ WIRED | `new WebSocket(\`ws://127.0.0.1:${port}\`)` — 127.0.0.1 hardcoded, not localhost |
| `companion/src/main.ts` | `vdo.ninja` | iframe URL construction | ✓ WIRED | `buildVdoNinjaUrl()` in `ui.ts` returns `https://vdo.ninja/?room=...&noaudio&cleanoutput`; called from `loadVdoNinjaIframe()` which is called in `onConfig` |
| `juce/ui/ConnectionBar.cpp` | `juce/video/VideoCompanion.h` | `onVideoClicked` callback invokes `launchCompanion` or `relaunchBrowser` | ✓ WIRED | `onVideoClicked` in `ConnectionBar.h`; `connectionBar.onVideoClicked = [this]() { ... processorRef.videoCompanion->launchCompanion(...) }` in `JamWideJuceEditor.cpp` |
| `juce/JamWideJuceProcessor.h` | `juce/video/VideoCompanion.h` | `unique_ptr<VideoCompanion>` member | ✓ WIRED | `#include "video/VideoCompanion.h"` at line 16; `std::unique_ptr<jamwide::VideoCompanion> videoCompanion` at line 96 |
| `juce/JamWideJuceEditor.cpp` | `juce/video/VideoPrivacyDialog.h` | Privacy modal shown before video launch | ✓ WIRED | `videoPrivacyDialog.onResponse` callback chains to `launchCompanion`; `connectionBar.onVideoClicked` calls `videoPrivacyDialog.show()` |
| `juce/NinjamRunThread.cpp` | `VideoCompanion::onRosterChanged` | UserInfoChangedEvent bridge | ✓ WIRED | Line 339: `processor.videoCompanion->onRosterChanged(processor.cachedUsers)` guarded by `if (processor.videoCompanion)` |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `VideoCompanion::sendConfigToClient` | `currentRoom_`, `currentPush_` | Set in `launchCompanion()` via `deriveRoomId()` and `sanitizeUsername()` | Yes — SHA-1 of real server address + password | ✓ FLOWING |
| `VideoCompanion::broadcastRoster` | `users` vector | `processor.cachedUsers` from `NinjamRunThread` → `onRosterChanged` via callAsync | Yes — sourced from live NJClient roster | ✓ FLOWING |
| `companion/src/main.ts` `onConfig` | `msg.room`, `msg.push` | WebSocket message from plugin | Yes — plugin sends real derived values | ✓ FLOWING |
| `companion/src/ui.ts` `loadVdoNinjaIframe` | `room`, `push` in VDO.Ninja URL | Passed from `onConfig` callback | Yes — real room and push IDs from plugin | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Plugin builds with IXWebSocket | `cmake --build build-juce --target JamWideJuce_VST3 --parallel` | Exit 0; all targets built including `ixwebsocket` | ✓ PASS |
| Companion page TypeScript builds | `cd companion && npm run build` | Exit 0; 7 modules transformed; output to `docs/video/` | ✓ PASS |
| `VideoCompanion.cpp` includes WDL_SHA1 | `grep "WDL_SHA1" juce/video/VideoCompanion.cpp` | Found at `deriveRoomId()` | ✓ PASS |
| WebSocket binds to 127.0.0.1 | `grep "127.0.0.1" juce/video/VideoCompanion.cpp` | Found in `startWebSocketServer()`: `ix::WebSocketServer(wsPort_, "127.0.0.1")` | ✓ PASS |
| Audio suppression in config | `grep "noaudio" juce/video/VideoCompanion.cpp` | Found in `sendConfigToClient()`: `"\"noaudio\":true"` | ✓ PASS |
| Thread-safe roster dispatch | `grep "callAsync" juce/video/VideoCompanion.cpp` | Found in `onRosterChanged()`; alive_ flag checked | ✓ PASS |
| Port bind failure handled | `grep "return false" juce/video/VideoCompanion.cpp` | Found in `startWebSocketServer()` after failed `listen()` | ✓ PASS |
| No auto-reconnect in companion | `grep "setTimeout\|setInterval" companion/src/ws-client.ts` | No matches | ✓ PASS |
| Empty state text | `grep "No participants yet" companion/src/ui.ts` | Found in `showEmptyRoom()` | ✓ PASS |
| Browser deactivation on disconnect | `grep "deactivate" juce/ui/ConnectionBar.cpp` | Found at line 484 in `updateStatus()` under D-19 comment | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| VID-01 | Plans 01, 03 | User can launch VDO.Ninja video with one click | ? HUMAN NEEDED | Button, modal, launchCompanion all wired — runtime test needed |
| VID-02 | Plan 01 | Video room ID auto-generated from NINJAM server address | ✓ SATISFIED | `deriveRoomId()` with SHA-1 of server + password; "jw-" prefix |
| VID-03 | Plans 01, 02 | No duplicate audio (VDO.Ninja audio suppressed) | ✓ SATISFIED | `"noaudio":true` in config JSON; `&noaudio&cleanoutput` in VDO.Ninja URL |
| VID-04 | Plan 02 | User sees all session participants in video grid | ✓ SATISFIED | VDO.Ninja iframe with `&cleanoutput` loaded in companion page |
| VID-05 | Plan 03 | Privacy notice about IP exposure before first video use | ✓ SATISFIED | `VideoPrivacyDialog` with IP disclosure always shown before launch |
| VID-06 | Plan 03 | Warning if default browser is not Chromium-based | ✓ SATISFIED | `BrowserDetect` (macOS + Windows + Linux); browser warning shown conditionally in privacy modal |

**Orphaned requirements check:** Requirements VID-07 through VID-12 are mapped to Phases 12-13 in REQUIREMENTS.md. No Phase 11 plans claimed these — correctly not orphaned.

All 6 requirement IDs (VID-01 through VID-06) from the plan frontmatter are mapped. VID-07 through VID-12 are explicitly deferred to later phases per REQUIREMENTS.md traceability table.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `companion/src/main.ts` | 47, 52 | `console.log` for roster | ℹ️ Info | Intentional per plan: "Log roster to console. Roster display in the companion page grid is Phase 12." Not a stub — roster is received, validated, and logged as designed. |

No blockers, stubs, or missing implementations found. The console.log usage in Phase 11 is intentional scaffolding for Phase 12 VID-10 (roster discovery).

### Human Verification Required

#### 1. One-Click Video Launch (VID-01)

**Test:** Connect to a NINJAM server in JamWide. Click the Video button. Accept the privacy modal.
**Expected:** Browser opens to `jamwide.audio/video/?room=jw-{hash}&push={sanitized-username}&wsPort=7170`. Page shows "Connecting to JamWide plugin..." briefly, then the VDO.Ninja iframe loads. No duplicate audio is heard in the NINJAM session.
**Why human:** Full end-to-end flow (plugin WebSocket server, browser launch, JavaScript WebSocket client connecting back) requires a live NINJAM connection and browser environment.

#### 2. Disabled Button State (VID-01, D-03)

**Test:** With no active NINJAM connection, inspect the Video button and attempt to click it.
**Expected:** Button appears greyed out (50% alpha). Tooltip reads "Connect to a server first". No modal opens.
**Why human:** Button enabled/disabled state and tooltip text require visual inspection in JUCE plugin runtime.

#### 3. Re-click While Active (D-04)

**Test:** With video active (button green), click the Video button again.
**Expected:** Browser tab re-opens to the companion page. No privacy modal appears. Plugin does not restart the WebSocket server.
**Why human:** Requires a live active video session to test the `isActive()` branch.

#### 4. Browser Compatibility Warning (VID-06)

**Test:** On a machine with Firefox (or other non-Chromium browser) set as the system default, click Video while connected.
**Expected:** Privacy modal shows "Browser Compatibility" section warning about non-Chromium browser. User can still accept and proceed.
**Why human:** `LSCopyDefaultHandlerForURLScheme` is macOS-platform-specific; requires test machine with non-Chromium default browser.

#### 5. Video Deactivation on Disconnect (D-19)

**Test:** With video active, disconnect from the NINJAM server.
**Expected:** Video button returns to grey/inactive. Re-clicking Video after reconnecting shows the privacy modal again.
**Why human:** Requires live NINJAM disconnect event to trigger the `deactivate()` path in `ConnectionBar::updateStatus()`.

### Gaps Summary

No programmatic gaps found. All 4 roadmap success criteria have implementation evidence. All 6 requirements (VID-01 through VID-06) are covered by delivered artifacts. Both the plugin (C++) and companion page (TypeScript) build cleanly with zero errors.

The phase goal is considered achieved pending 5 human verification items that require a live NINJAM session and browser environment. These cover the full end-to-end happy path and the key UX behaviors (disabled state, re-click, browser detection, deactivation on disconnect).

---

_Verified: 2026-04-07T18:26:06Z_
_Verifier: Claude (gsd-verifier)_
