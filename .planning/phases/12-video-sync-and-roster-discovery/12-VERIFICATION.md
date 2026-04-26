---
phase: 12-video-sync-and-roster-discovery
verified: 2026-04-08T00:20:00Z
status: human_needed
score: 12/12 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Buffer delay syncs video to NINJAM interval"
    expected: "When BPM/BPI changes in a live session, the VDO.Ninja companion page updates video buffer delay to match the new interval (e.g., 120 BPM / 8 BPI = 4000ms)"
    why_human: "Requires live NINJAM session, actual VDO.Ninja iframe, and observing buffer timing change. Cannot verify postMessage reception by VDO.Ninja via static analysis."
  - test: "Password fragment locks VDO.Ninja room"
    expected: "Joining a passworded NINJAM server opens companion URL with #password=<16hexchars> in the fragment; users without that password cannot join the VDO.Ninja room"
    why_human: "Requires a live session with a password, browser inspection, and a second browser without the password attempting to join."
  - test: "Roster strip shows NINJAM names over video"
    expected: "When remote users connect/disconnect, pill badges in the roster strip appear/disappear correctly without page reload. Bot users (ninbot, jambot, ninjam) are not shown."
    why_human: "Requires live roster change events from a real NINJAM session or simulated WebSocket injection."
  - test: "Bandwidth profile dropdown changes video quality"
    expected: "Selecting Low/Balanced/High reloads iframe with correct quality/maxvideobitrate params, selection survives page reload, and video quality visibly changes."
    why_human: "Requires browser with VDO.Ninja iframe loaded; visual quality change cannot be verified statically."
---

# Phase 12: Video Sync and Roster Discovery — Verification Report

**Phase Goal:** Users experience video buffering synced to NINJAM timing with automatic participant discovery and room security
**Verified:** 2026-04-08T00:20:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | User's video streams buffer according to current NINJAM interval (setBufferDelay matches BPM/BPI) | ✓ VERIFIED | `broadcastBufferDelay` in VideoCompanion.cpp computes `(60.0/bpm)*bpi*1000`, broadcasts `{"type":"bufferDelay","delayMs":N}`; companion `applyBufferDelay()` calls `postMessage({setBufferDelay: N}, '*')` to iframe; BPM/BPI change events wired in JamWideJuceEditor.cpp drainEvents |
| 2 | User can see NINJAM usernames as pill badges in companion page roster strip | ✓ VERIFIED | `renderRosterStrip()` in ui.ts creates `<span class="roster-pill">` per user using `textContent` (XSS-safe); `#roster-strip` div present in index.html; roster strip in style.css |
| 3 | Video room secured with password derived from NINJAM session | ✓ VERIFIED | `deriveRoomPassword()` computes `SHA256(password+":"+roomId).substring(0,16)`; appended as `#password=` fragment in `buildCompanionUrl()`; companion parses `window.location.hash` and passes to VDO.Ninja as `&password=` in iframe URL |
| 4 | User can select bandwidth-aware video profile (Low/Balanced/High) | ✓ VERIFIED | `BANDWIDTH_PROFILES` constant in ui.ts (low=360p/500kbps, balanced=720p/1.5Mbps, high=1080p/3Mbps); `<select id="bandwidth-profile">` in index.html; persisted via localStorage with `getSavedBandwidthProfile()` defensive fallback; iframe reload on change wired in main.ts |

**Score:** 4/4 roadmap success criteria verified

### Plan 01 Must-Haves (9 truths)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Plugin calculates buffer delay from BPM/BPI and broadcasts via WebSocket on connect and on change | ✓ VERIFIED | `broadcastBufferDelay(float bpm, int bpi)` in VideoCompanion.cpp lines 360-382; sends JSON `{"type":"bufferDelay","delayMs":N}`; called from BpmChangedEvent and BpiChangedEvent handlers in JamWideJuceEditor.cpp |
| 2 | Plugin derives SHA-256 derived room password from NINJAM password and room ID | ✓ VERIFIED | `deriveRoomPassword()` at line 80-95 in VideoCompanion.cpp: `juce::SHA256 sha(input.toUTF8()); return sha.toHexString().substring(0, 16)` |
| 3 | Plugin appends derived room password as URL fragment (#password=) to companion URL, never as query parameter | ✓ VERIFIED | `buildCompanionUrl()` at line 165-167: `url += "#password=" + derivedPassword` — uses `#` not `?` |
| 4 | Public servers (no password) produce no password fragment | ✓ VERIFIED | `deriveRoomPassword()` returns `{}` when `password.isEmpty()` (line 83); empty derivedPassword skips `if (derivedPassword.isNotEmpty())` in `buildCompanionUrl()` |
| 5 | Buffer delay updates only on BPM/BPI change events, not on every beat | ✓ VERIFIED | Called exclusively from `BpmChangedEvent` and `BpiChangedEvent` handlers in drainEvents (JamWideJuceEditor.cpp lines 308-326), not from any beat tick handler |
| 6 | Invalid BPM/BPI values (zero, negative, NaN) produce no broadcast and no crash | ✓ VERIFIED | Guards at VideoCompanion.cpp lines 364-366: `if (bpm <= 0.0f \|\| bpi <= 0) return;` then `if (std::isnan(bpm)) return;` |
| 7 | Cached buffer delay and derived room password are sent to newly connecting WebSocket clients | ✓ VERIFIED | `sendConfigToClient()` checks `if (cachedDelayMs_ > 0)` and sends bufferDelay JSON after config message (lines 297-303) |
| 8 | State is cleared when switching from private to public sessions (deactivate clears password state) | ✓ VERIFIED | `deactivate()` at lines 393-395: `currentPassword_.clear(); currentDerivedPassword_.clear(); cachedDelayMs_ = 0;` |
| 9 | SHA-256 truncation to 16 hex chars explicitly justified in code comments | ✓ VERIFIED | VideoCompanion.h lines 95-108: comment documents "16 hex chars = 64 bits of entropy", "VDO.Ninja itself internally truncates its own password hashes to just 4 hex chars (16 bits)", "2^48 times stronger" |

### Plan 02 Must-Haves (12 truths)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Companion receives bufferDelay message and forwards to iframe via postMessage | ✓ VERIFIED | `isBufferDelayMessage` in types.ts; dispatched in ws-client.ts; `onBufferDelay` callback calls `applyBufferDelay(msg.delayMs)` in main.ts; `applyBufferDelay()` calls `iframe.contentWindow.postMessage({ setBufferDelay: delayMs }, '*')` in ui.ts |
| 2 | Companion shows NINJAM usernames as pill badges in roster strip | ✓ VERIFIED | `renderRosterStrip()` in ui.ts creates roster-pill spans; `#roster-strip` in index.html; CSS at line 134 and 150 of style.css |
| 3 | Roster strip updates dynamically on every roster message (full state replacement) | ✓ VERIFIED | `renderRosterStrip()` uses `while (strip.firstChild) strip.removeChild(strip.firstChild)` then rebuilds from full array per protocol contract comment |
| 4 | Bandwidth quality dropdown with Low/Balanced/High presets persists via localStorage | ✓ VERIFIED | `getSavedBandwidthProfile()` with `VALID_BANDWIDTH_PROFILES` Set guard; `saveBandwidthProfile()`; localStorage key `jamwide-bandwidth-profile` |
| 5 | Changing bandwidth profile reloads iframe with updated quality and maxvideobitrate params | ✓ VERIFIED | `bwSelect.addEventListener('change', ...)` in main.ts calls `loadVdoNinjaIframe(...)` which calls `buildVdoNinjaUrl()` with current profile |
| 6 | VDO.Ninja URL includes chunked mode parameters for buffer sync beyond 4 seconds | ✓ VERIFIED | `buildVdoNinjaUrl()` in ui.ts lines 146-148: `'&chunked'`, `'&chunkbufferadaptive=0'`, `'&chunkbufferceil=180000'` |
| 7 | Companion reads hash fragment from URL and passes to VDO.Ninja iframe URL as password parameter | ✓ VERIFIED | main.ts lines 33-34: `const hashParams = new URLSearchParams(window.location.hash.substring(1)); const roomHashFragment = hashParams.get('password') \|\| '';`; passed to every `buildVdoNinjaUrl` call as `&password=encodeURIComponent(hashFragment)` |
| 8 | Buffer delay cached and re-applied after iframe reload | ✓ VERIFIED | `reapplyCachedBufferDelay()` in ui.ts; `iframe.addEventListener('load', () => { reapplyCachedBufferDelay(); })` in `loadVdoNinjaIframe()` |
| 9 | State preserved across iframe reloads | ✓ VERIFIED | `roomHashFragment` module-level const in main.ts (parsed once); `cachedBufferDelay` module-level in ui.ts; `getSavedBandwidthProfile()` reads localStorage; roster strip DOM node preserved via `existingStrip` re-attachment in `loadVdoNinjaIframe()` |
| 10 | Invalid localStorage values for bandwidth profile fall back to balanced | ✓ VERIFIED | `getSavedBandwidthProfile()` checks `VALID_BANDWIDTH_PROFILES.has(stored)` before trusting stored value; returns `'balanced'` otherwise |
| 11 | Known bot users filtered from roster strip | ✓ VERIFIED | `isBotUser()` in ui.ts with `BOT_PREFIXES = ['ninbot', 'jambot', 'ninjam']` using `lower.startsWith(prefix)`; `renderRosterStrip()` filters with `users.filter(u => !isBotUser(u.name))` |
| 12 | All Vitest unit tests pass | ✓ VERIFIED | `npx vitest run` in /Users/cell/dev/JamWide/companion: "4 passed (4)" test files, "39 passed (39)" tests |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `CMakeLists.txt` | juce_cryptography linked | ✓ VERIFIED | Line 316: `juce::juce_cryptography` in PRIVATE section of `target_link_libraries(JamWideJuce ...)` |
| `juce/video/VideoCompanion.h` | broadcastBufferDelay and deriveRoomPassword declarations | ✓ VERIFIED | Both present; deriveRoomPassword includes full 64-bit truncation rationale comment |
| `juce/video/VideoCompanion.cpp` | Buffer delay, SHA-256 password, URL builder, state management | ✓ VERIFIED | All implemented at correct lines; juce::SHA256 used; #password= fragment; all guards present |
| `juce/JamWideJuceEditor.cpp` | BPM/BPI handlers forward to VideoCompanion | ✓ VERIFIED | Both BpmChangedEvent and BpiChangedEvent call `processorRef.videoCompanion->broadcastBufferDelay(bpm, bpi)` with guard |
| `companion/src/types.ts` | BufferDelayMessage type and isBufferDelayMessage guard | ✓ VERIFIED | Both present and substantive |
| `companion/src/ui.ts` | BANDWIDTH_PROFILES, buildVdoNinjaUrl, applyBufferDelay, renderRosterStrip | ✓ VERIFIED | All present with full implementations |
| `companion/src/main.ts` | onBufferDelay callback, bandwidth dropdown wiring, hash fragment parsing | ✓ VERIFIED | All wired |
| `companion/src/ws-client.ts` | isBufferDelayMessage dispatch in WS handler | ✓ VERIFIED | Imported and dispatched to `onBufferDelay` callback |
| `companion/index.html` | bandwidth-profile select and roster-strip div | ✓ VERIFIED | Both present at lines 28 and 38 |
| `companion/style.css` | #roster-strip and .roster-pill styles | ✓ VERIFIED | Lines 134 and 150 |
| `companion/vitest.config.ts` | Vitest configuration | ✓ VERIFIED | jsdom environment, correct include pattern |
| `companion/src/__tests__/buffer-delay.test.ts` | Buffer delay relay tests | ✓ VERIFIED | 6 tests covering caching, postMessage, re-apply, no-op |
| `companion/src/__tests__/url-builder.test.ts` | URL builder tests | ✓ VERIFIED | 10 tests covering chunked params, quality profiles, password, effects |
| `companion/src/__tests__/roster-labels.test.ts` | Roster rendering tests | ✓ VERIFIED | Present, 39 total across all 4 files |
| `companion/src/__tests__/bandwidth-profile.test.ts` | Bandwidth persistence tests | ✓ VERIFIED | Present, localStorage validation covered |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `juce/JamWideJuceEditor.cpp` | `VideoCompanion::broadcastBufferDelay` | BpmChangedEvent/BpiChangedEvent handler in drainEvents | ✓ WIRED | Both event handlers call `processorRef.videoCompanion->broadcastBufferDelay(bpm, bpi)` with null+active guard |
| `juce/video/VideoCompanion.cpp` | `ix::WebSocket::send` | broadcastBufferDelay JSON broadcast | ✓ WIRED | `client->send(json.toStdString())` in broadcastBufferDelay and sendConfigToClient |
| `juce/video/VideoCompanion.cpp` | companion URL | buildCompanionUrl appends #password= fragment | ✓ WIRED | `url += "#password=" + derivedPassword` when derivedPassword is non-empty |
| `companion/src/ws-client.ts` | `companion/src/main.ts` | onBufferDelay callback in WsCallbacks | ✓ WIRED | `callbacks.onBufferDelay(parsed)` dispatched; `onBufferDelay(msg) { applyBufferDelay(msg.delayMs); }` in main.ts |
| `companion/src/main.ts` | `companion/src/ui.ts` | applyBufferDelay function call | ✓ WIRED | Imported and called in `onBufferDelay` callback |
| `companion/src/ui.ts` | VDO.Ninja iframe | postMessage({setBufferDelay: N}) | ✓ WIRED | `iframe.contentWindow.postMessage({ setBufferDelay: delayMs }, '*')` |
| `companion/src/ui.ts` | VDO.Ninja iframe | buildVdoNinjaUrl with &quality and &maxvideobitrate | ✓ WIRED | Both params included from BANDWIDTH_PROFILES lookup |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `VideoCompanion::broadcastBufferDelay` | `cachedDelayMs_` | Computed from `processor_.uiSnapshot.bpm/bpi` atomics fed by NinjamRunThread | Yes — live atomics from NJClient session state | ✓ FLOWING |
| `VideoCompanion::deriveRoomPassword` | SHA-256 hash | Computed from `currentPassword_` (stored from `launchCompanion()` arg) and `currentRoom_` | Yes — live password from session | ✓ FLOWING |
| `companion/src/ui.ts::renderRosterStrip` | `users: RosterUser[]` | WebSocket roster message from plugin's `broadcastRoster()` which reads NJClient roster | Yes — real roster from NJClient | ✓ FLOWING |
| `companion/src/ui.ts::applyBufferDelay` | `delayMs: number` | WebSocket bufferDelay message from plugin | Yes — computed from live BPM/BPI | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Vitest unit tests pass | `cd /Users/cell/dev/JamWide/companion && npx vitest run` | 4 test files, 39 tests — all passed | ✓ PASS |
| juce_cryptography linked in CMakeLists | `grep "juce_cryptography" CMakeLists.txt` | Line 316: `juce::juce_cryptography` | ✓ PASS |
| SHA256 used (not WDL SHA1) for password | `grep "juce::SHA256" juce/video/VideoCompanion.cpp` | Line 93: `juce::SHA256 sha(input.toUTF8())` | ✓ PASS |
| #password= fragment (not query param) | `grep "#password=" juce/video/VideoCompanion.cpp` | Line 167: `url += "#password=" + derivedPassword` | ✓ PASS |
| NaN guard in broadcastBufferDelay | `grep "isnan" juce/video/VideoCompanion.cpp` | Lines 197, 365: both guard sites present | ✓ PASS |
| deriveRoomHash NOT used (renamed) | `grep "deriveRoomHash" VideoCompanion.cpp VideoCompanion.h` | No matches | ✓ PASS |
| All 4 commit hashes in git log | `git log --oneline` | 45776dc, 519b50a, 1d5888b, a93f2ea all present | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| VID-08 | Plans 01 + 02 | User's video buffering syncs to NINJAM interval timing via setBufferDelay | ✓ SATISFIED | C++ broadcastBufferDelay computes `(60/bpm)*bpi*1000`; TypeScript applyBufferDelay relays via postMessage; chunked mode enabled for >4s intervals; buffer delay cached and re-applied on iframe reload |
| VID-09 | Plans 01 + 02 | User's video room is secured with a derived password | ✓ SATISFIED | SHA-256 derived password in VideoCompanion; appended as URL `#password=` fragment; companion parses and forwards to VDO.Ninja as `&password=`; public rooms produce no password |
| VID-10 | Plan 02 | User can see which VDO.Ninja streams map to which NINJAM users | ✓ SATISFIED | Roster strip with pill badges in companion page; `renderRosterStrip()` implements full-state replacement; bot users filtered; NINJAM names displayed via XSS-safe textContent |
| VID-12 | Plan 02 | User can select a bandwidth-aware video profile | ✓ SATISFIED | BANDWIDTH_PROFILES with low/balanced/high presets; localStorage persistence with defensive validation; bandwidth dropdown in HTML; iframe reload on change with correct quality params |

No orphaned requirements — all four Phase 12 requirements (VID-08, VID-09, VID-10, VID-12) are claimed and satisfied across the two plans.

### Anti-Patterns Found

No blockers or warnings found.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | No TODOs, FIXMEs, placeholders, or empty stubs found | — | — |

Note: `return {}` in `deriveRoomPassword` for empty password is intentional guard logic, not a stub — the calling code handles the empty string to omit the URL fragment.

### Human Verification Required

#### 1. Buffer Delay Sync with Live NINJAM Session

**Test:** Connect to a NINJAM server via JamWide. Launch the video companion. Join the session with another user. Use a NINJAM server that allows BPM/BPI changes (or use a test server). Observe the companion page when BPM/BPI changes.
**Expected:** VDO.Ninja buffer delay updates to `floor((60/BPM)*BPI*1000)` ms when BPM or BPI changes. Video streams re-buffer accordingly. On initial load, if BPM/BPI are already set, the companion receives the cached delay immediately after config.
**Why human:** Requires live NINJAM session + VDO.Ninja iframe. postMessage reception by VDO.Ninja's internal buffering system cannot be verified by static analysis.

#### 2. Password Fragment Locks VDO.Ninja Room

**Test:** Connect to a passworded NINJAM server. Launch video companion. Inspect the companion page URL — it should contain `#password=<16hexchars>`. Attempt to join the same VDO.Ninja room from a different browser/tab without the password fragment (using only `?room=jw-...`).
**Expected:** Only users with the correct password fragment can join the VDO.Ninja room. The password fragment is not visible in network requests (browser-only, per RFC 3986 section 3.5).
**Why human:** Requires a passworded NINJAM session, browser devtools inspection, and a second browser attempting to join the VDO.Ninja room.

#### 3. Roster Strip Live Updates

**Test:** Launch companion page. Have a remote user join and leave the NINJAM session. Observe the roster strip at the bottom of the video area. Also test with NINJAM server bots (ninbot, jambot variants).
**Expected:** Pill badges appear/disappear as users join and leave. Bot users are not shown. Strip hides entirely when no non-bot users are present. Strip survives bandwidth profile changes.
**Why human:** Requires live roster change events from real NINJAM session or simulated WebSocket injection from browser console.

#### 4. Bandwidth Profile Visual Quality Change

**Test:** Open the companion page with VDO.Ninja video active. Switch between Low/Balanced/High in the bandwidth dropdown. Reload the page and verify the dropdown restores the saved selection. Check browser devtools Network tab for the iframe URL on reload.
**Expected:** Each profile change reloads the iframe with the correct `&quality=` and `&maxvideobitrate=` params. Selection persists across page reload. Video quality visibly changes.
**Why human:** Visual quality assessment requires an active VDO.Ninja session. localStorage persistence verification requires browser session.

### Gaps Summary

No gaps found. All 12 Plan must-haves are verified, all 4 roadmap success criteria are satisfied, all 4 requirement IDs (VID-08, VID-09, VID-10, VID-12) are covered. Phase awaits human testing of live browser + NINJAM integration behaviors that cannot be verified statically.

---

_Verified: 2026-04-08T00:20:00Z_
_Verifier: Claude (gsd-verifier)_
