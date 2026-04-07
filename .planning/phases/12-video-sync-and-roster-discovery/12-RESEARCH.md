# Phase 12: Video Sync and Roster Discovery - Research

**Researched:** 2026-04-07
**Domain:** VDO.Ninja iframe API (setBufferDelay, room security, bandwidth profiles), companion page TypeScript, JUCE C++ WebSocket messaging
**Confidence:** MEDIUM-HIGH

## Summary

Phase 12 extends the Phase 11 video companion foundation with four features: interval-synced video buffering (VID-08), room security via password hash (VID-09), roster discovery with name labels (VID-10), and bandwidth profile selection (VID-12). All four features build on existing infrastructure -- the WebSocket server in `VideoCompanion.cpp`, the companion page in `companion/src/`, and the VDO.Ninja iframe already loaded by `loadVdoNinjaIframe()`.

The C++ side requires: (1) computing buffer delay from BPM/BPI and broadcasting it via WebSocket on connect and on BPM/BPI change events, (2) computing a VDO.Ninja-compatible password hash using SHA-256 and appending it to the companion URL fragment. The companion page (TypeScript) side requires: (1) handling the new `bufferDelay` WebSocket message and forwarding it to the VDO.Ninja iframe via `postMessage({setBufferDelay: N})`, (2) rendering name label overlays on the video grid using roster data already received via the `roster` WebSocket message, (3) adding a bandwidth quality dropdown that rebuilds the iframe URL with appropriate `&quality` and `&maxvideobitrate` parameters, and (4) adding chunked-mode viewer parameters for buffer sync to work. The architecture is well-defined by CONTEXT.md decisions; remaining discretion areas are CSS layout for labels, exact VDO.Ninja URL parameter combinations per bandwidth tier, and fallback if `setBufferDelay` is ignored.

**Primary recommendation:** Implement in C++-first order: buffer delay calculation and WebSocket broadcast (extends existing VideoCompanion), then SHA-256 hash for room security (requires adding `juce_cryptography` to CMakeLists), then companion page TypeScript for all four features (buffer relay, roster labels, bandwidth dropdown, hash fragment handling).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Plugin calculates video buffer delay from current BPM/BPI and sends it via WebSocket to the companion page. Companion page forwards via postMessage to the VDO.Ninja iframe using setBufferDelay.
- **D-02:** Delay updates automatically on every BPM/BPI change (not on every beat). This is a session-level parameter that changes rarely.
- **D-03:** Buffer delay formula: `delay_ms = (60.0 / bpm) * bpi * 1000`. This matches the full NINJAM interval length so video participants see each other "in time" with the interval audio.
- **D-04:** New WebSocket message type: `{"type":"bufferDelay","delayMs":N}`. Plugin sends this on connect and whenever BPM/BPI changes. Companion page calls `iframe.contentWindow.postMessage({action:"setBufferDelay",value:N}, "*")`.
- **D-05:** Room password derived from NINJAM session password using SHA-256, passed as `&hash=#fragment` in VDO.Ninja URL. The fragment is never sent to the server (browser-only), preventing plaintext credential exposure.
- **D-06:** Hash derivation: `SHA-256(ninjam_password + ":" + room_id)`. Same WDL_SHA1 approach as Phase 11 room ID but with password as additional input. Deterministic -- all JamWide users with the same password on the same server get the same hash.
- **D-07:** If no NINJAM password is set (public server), no hash is applied. Public rooms remain open, consistent with Phase 11 D-17 behavior.
- **D-08:** Roster matching uses the username sanitization convention from Phase 11 (D-22, D-23). Each NINJAM user's sanitized name matches their VDO.Ninja push= stream ID. No external API connection needed.
- **D-09:** Companion page receives roster updates via existing WebSocket `roster` message (Phase 11 D-14). For each user in the roster, the companion page shows a name label overlay on or near the corresponding VDO.Ninja video stream.
- **D-10:** If a VDO.Ninja stream ID doesn't match any roster entry, show the stream without a label (graceful degradation -- visitor or name mismatch).
- **D-11:** No VDO.Ninja external API (wss://api.vdo.ninja) in this phase. The API is self-labeled DRAFT and username convention is sufficient for the core use case.
- **D-12:** Quality dropdown in the companion page header, next to the existing effects dropdown. Three presets: Low (360p, 500kbps), Balanced (720p, 1.5Mbps), High (1080p, 3Mbps).
- **D-13:** Selection persists via localStorage (same pattern as effects dropdown). Default: Balanced.
- **D-14:** VDO.Ninja URL parameters per profile: `&quality=0` (low), `&quality=1` (balanced), `&quality=2` (high). Plus `&maxvideobitrate=` for explicit cap.
- **D-15:** Changing the profile requires iframe reload (same as changing effects). Dropdown change triggers `loadVdoNinjaIframe()` with updated URL params.

### Claude's Discretion
- Exact postMessage format for setBufferDelay (VDO.Ninja's expected message structure)
- CSS layout for name label overlays on video streams
- Fallback behavior if VDO.Ninja ignores setBufferDelay (e.g., chunked mode not available)
- Exact VDO.Ninja URL parameters for each bandwidth tier

### Deferred Ideas (OUT OF SCOPE)
- VDO.Ninja external API (wss://api.vdo.ninja) -- deferred until username convention proves insufficient
- Per-user video quality settings -- one profile for all streams is sufficient for now
- Video recording integration -- out of scope, OBS handles this
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| VID-08 | User's video buffering syncs to NINJAM interval timing via setBufferDelay | Buffer delay formula (D-03), WebSocket message format (D-04), VDO.Ninja postMessage API verified, chunked mode parameters documented |
| VID-09 | User's video room is secured with a password derived from the NINJAM session | SHA-256 hash derivation (D-05/D-06), VDO.Ninja &hash parameter compatibility verified, juce_cryptography SHA256 class available |
| VID-10 | User can see which VDO.Ninja streams map to which NINJAM users (roster discovery) | Roster message already flows via WebSocket (Phase 11), name label overlay CSS pattern documented, graceful degradation for unmatched streams (D-10) |
| VID-12 | User can select a bandwidth-aware video profile (mobile/balanced/desktop) | VDO.Ninja &quality parameter verified (0=1080p, 1=720p, 2=360p), &maxvideobitrate in kbps verified, localStorage persistence pattern exists |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| juce_cryptography | 8.0.12 (bundled) | SHA-256 for room password hash | Already bundled with JUCE, provides `juce::SHA256` class, zero external deps [VERIFIED: local source at libs/juce/modules/juce_cryptography/hashing/juce_SHA256.h] |
| IXWebSocket | (already linked) | WebSocket server for plugin-to-companion communication | Already used in Phase 11 VideoCompanion.cpp [VERIFIED: CMakeLists.txt line 311 links `ixwebsocket`] |
| Vite + TypeScript | 6.4.2 / 5.9.3 | Companion page build | Already configured in companion/package.json [VERIFIED: companion/package.json] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| WDL_SHA1 | (bundled) | Room ID derivation (existing) | Already used in VideoCompanion::deriveRoomId() -- not for password hash [VERIFIED: juce/video/VideoCompanion.cpp] |
| VDO.Ninja iframe API | (browser-side) | setBufferDelay, quality control | Via postMessage to embedded iframe [CITED: docs.vdo.ninja/guides/iframe-api-documentation/iframe-api-basics] |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| juce_cryptography SHA256 | Standalone SHA-256 header (e.g., PicoSHA2) | juce_cryptography is already bundled and proven; no need for another vendored header |
| localStorage for quality | Cookie-based persistence | localStorage is already the pattern used by effects dropdown; consistent UX |

**Installation:**
```bash
# C++ side: Add juce_cryptography to CMakeLists.txt target_link_libraries
# No npm install needed -- companion page has no new JS dependencies
```

## Architecture Patterns

### Recommended Project Structure (changes only)
```
juce/video/
    VideoCompanion.h      # Add bufferDelay broadcast + hash derivation methods
    VideoCompanion.cpp     # Implement buffer delay + SHA-256 hash + companion URL hash fragment

companion/src/
    types.ts              # Add BufferDelayMessage type + type guard
    ui.ts                 # Add bandwidth dropdown, name label overlays, buffer delay relay
    main.ts               # Wire bufferDelay handler, bandwidth dropdown events
    ws-client.ts          # Handle new bufferDelay message type dispatch
```

### Pattern 1: Buffer Delay Calculation and Broadcast
**What:** Plugin calculates `delay_ms = (60.0 / bpm) * bpi * 1000` and sends `{"type":"bufferDelay","delayMs":N}` over WebSocket.
**When to use:** On initial WebSocket client connection and whenever BPM or BPI changes.
**Example:**
```cpp
// Source: CONTEXT.md D-03, D-04
void VideoCompanion::broadcastBufferDelay(float bpm, int bpi)
{
    if (bpm <= 0.0f || bpi <= 0) return;

    int delayMs = static_cast<int>((60.0 / static_cast<double>(bpm)) * bpi * 1000.0);

    juce::String json = "{\"type\":\"bufferDelay\",\"delayMs\":" + juce::String(delayMs) + "}";

    std::lock_guard<std::mutex> lock(wsMutex_);
    if (!wsServer_) return;
    auto clients = wsServer_->getClients();
    for (auto& client : clients)
        client->send(json.toStdString());
}
```

### Pattern 2: VDO.Ninja postMessage for setBufferDelay
**What:** Companion page forwards buffer delay to VDO.Ninja iframe via postMessage.
**When to use:** When `bufferDelay` WebSocket message is received.
**Example:**
```typescript
// Source: VDO.Ninja iframe API docs
// [VERIFIED: docs.vdo.ninja/guides/iframe-api-documentation/iframe-api-basics]
function applyBufferDelay(delayMs: number): void {
  const iframe = document.querySelector('#main-area iframe') as HTMLIFrameElement | null;
  if (!iframe?.contentWindow) return;
  iframe.contentWindow.postMessage({ setBufferDelay: delayMs }, '*');
}
```

### Pattern 3: VDO.Ninja Password Hash via URL Fragment
**What:** Derive SHA-256 hash from NINJAM password and append as URL fragment.
**When to use:** When building companion URL for a password-protected NINJAM server.
**Example:**
```cpp
// Source: CONTEXT.md D-05, D-06
juce::String VideoCompanion::deriveRoomHash(const juce::String& password,
                                            const juce::String& roomId)
{
    if (password.isEmpty()) return {};  // D-07: no hash for public rooms

    juce::String input = password + ":" + roomId;
    juce::SHA256 sha(input.toUTF8());
    return sha.toHexString().substring(0, 8);  // 8 hex chars = 4 bytes
}
```

### Pattern 4: Name Label Overlay on Video Grid
**What:** CSS overlay of NINJAM usernames on VDO.Ninja video streams.
**When to use:** When roster data maps a streamId to a NINJAM username.
**Example:**
```typescript
// Source: Claude's discretion for CSS layout
function renderRosterLabels(users: RosterUser[]): void {
  // Remove existing labels
  document.querySelectorAll('.roster-label').forEach(el => el.remove());

  const container = document.getElementById('main-area');
  if (!container) return;

  users.forEach(user => {
    const label = document.createElement('div');
    label.className = 'roster-label';
    label.textContent = user.name;  // textContent is XSS-safe
    label.dataset.streamId = user.streamId;
    container.appendChild(label);
  });
}
```

### Anti-Patterns to Avoid
- **Direct postMessage without iframe check:** Always verify `iframe?.contentWindow` exists before calling postMessage. The iframe may not be loaded yet or may have been removed during a reload.
- **Sending bufferDelay on every beat/interval:** D-02 explicitly says delay updates on BPM/BPI *change* only, not continuously. Avoid timer-based polling.
- **Exposing NINJAM password in WebSocket config message:** D-14 from Phase 11 intentionally excludes the password from the config message. The hash goes in the companion URL fragment only, never in WebSocket JSON.
- **Using VDO.Ninja's native password hashing:** JamWide computes its own deterministic SHA-256 hash. Do not try to replicate VDO.Ninja's `SHA-256(password + roomName)` truncated-to-4-hex algorithm -- use our own hash format passed as `#hash=` in the URL.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| SHA-256 hashing | Custom SHA-256 implementation | `juce::SHA256` from juce_cryptography | Cryptographic code must not be hand-rolled; JUCE's implementation is audited and bundled |
| WebSocket JSON serialization | Custom JSON builder | Extend existing `escapeJsonString()` pattern from VideoCompanion.cpp | Already proven for config and roster messages; consistent style |
| localStorage persistence | Custom cookie/IndexedDB solution | `localStorage.getItem()`/`setItem()` | Already used for effects dropdown; browser-native, zero-dependency |
| iframe postMessage typing | Custom event system | Standard browser postMessage API | VDO.Ninja expects exactly this interface; any wrapper adds no value |

**Key insight:** Phase 12 is almost entirely glue code connecting existing systems. No new libraries or complex algorithms needed -- just message forwarding, URL parameter construction, and CSS overlays.

## Common Pitfalls

### Pitfall 1: VDO.Ninja &quality Parameter Numbering is Inverted from CONTEXT.md
**What goes wrong:** CONTEXT.md D-14 maps `&quality=0` to "low" and `&quality=2` to "high". VDO.Ninja's actual parameter definition is the opposite: `&quality=0` = ~1080p60 (highest), `&quality=2` = ~360p30 (lowest).
**Why it happens:** The quality number represents a preset index, not a quality level. Lower number = higher quality in VDO.Ninja.
**How to avoid:** Implementation must use the correct VDO.Ninja values: Low profile = `&quality=2`, Balanced = `&quality=1`, High = `&quality=0`. The dropdown labels (Low/Balanced/High) are correct; only the URL parameter values need correction.
**Warning signs:** Users selecting "High" getting 360p video instead of 1080p.
[VERIFIED: docs.vdo.ninja/advanced-settings/video-parameters/and-quality -- quality=0 is "about 1080p60", quality=2 is "about 360p30"]

### Pitfall 2: setBufferDelay Requires Chunked Mode for Long Intervals
**What goes wrong:** Without `&chunked` on the sender side and appropriate viewer parameters, `setBufferDelay` is limited to ~4 seconds (browser WebRTC jitter buffer ceiling). Typical NINJAM intervals (120 BPM, 16 BPI = 8 seconds) exceed this.
**Why it happens:** Non-chunked mode uses browser-native WebRTC buffering which has hard limits. Chunked mode uses VDO.Ninja's custom buffering solution with no hard-coded limit.
**How to avoid:** The companion page URL builder must include `&chunked` for the sender (push) side. For the viewer (&view) side, add `&chunkbufferadaptive=0&chunkbufferceil=180000` to keep buffer targets fixed and allow extended delays. Both are needed for `setBufferDelay` to work reliably at NINJAM interval lengths.
**Warning signs:** Video buffering stops at 4 seconds regardless of delayMs value; choppy playback with adaptive buffer fighting the fixed delay.
[VERIFIED: docs.vdo.ninja/advanced-settings/settings-parameters/and-chunked -- chunked is sender-side, viewer needs chunkbuffer* params]
[VERIFIED: docs.vdo.ninja/advanced-settings/video-parameters/buffer -- non-chunked limited to ~4 seconds]

### Pitfall 3: Hash Fragment Syntax Confusion
**What goes wrong:** Confusion between `&hash=VALUE` (query parameter sent to server) and `#hash=VALUE` (URL fragment, browser-only). Using `?hash=VALUE` instead of `#hash=VALUE` exposes the hash to Cloudflare/VDO.Ninja server logs.
**Why it happens:** VDO.Ninja supports both `?` and `#` for parameters, with fragment taking precedence on conflicts. The `#` form is specifically recommended for password-related parameters.
**How to avoid:** Append hash as URL fragment: `url + "#hash=" + hashValue`. Never include in the query string portion. VDO.Ninja reads URL fragment parameters and merges them with query parameters client-side.
**Warning signs:** Hash value visible in server access logs; Cloudflare logging the hash parameter.
[CITED: docs.vdo.ninja/advanced-settings/setup-parameters/and-password -- "Fragment parameters are never sent to the server"]

### Pitfall 4: postMessage Before Iframe Load
**What goes wrong:** Calling `iframe.contentWindow.postMessage()` before the VDO.Ninja iframe has fully loaded results in the message being silently dropped.
**Why it happens:** The iframe's message listener is registered after VDO.Ninja's JavaScript initializes, which takes a moment after the iframe src is set.
**How to avoid:** Wait for the iframe `load` event before sending the first `setBufferDelay`. Cache the most recent delayMs value and apply it when the iframe reports ready. Subsequent updates can be sent immediately since the iframe is already loaded.
**Warning signs:** Buffer delay appears to work after user changes BPM but not on initial page load.
[ASSUMED]

### Pitfall 5: Roster Labels Positioned Incorrectly on VDO.Ninja Grid
**What goes wrong:** CSS-positioned name labels don't align with VDO.Ninja's internal video grid layout because VDO.Ninja dynamically resizes and repositions video elements inside the iframe.
**Why it happens:** The companion page has no access to VDO.Ninja's internal DOM due to cross-origin iframe restrictions. Name labels must be positioned relative to the companion page's outer container, not to individual VDO.Ninja video elements.
**How to avoid:** Use a simpler approach: render labels as an overlay strip (e.g., a sidebar or bottom bar) outside the iframe, not positioned over individual video tiles. Alternatively, use a semi-transparent banner at the bottom of the main area showing all connected names. This avoids the impossible task of tracking VDO.Ninja's internal layout.
**Warning signs:** Labels appear in wrong positions, don't move when VDO.Ninja rearranges grid.
[ASSUMED]

### Pitfall 6: BPM/BPI Events Arrive Before VideoCompanion is Active
**What goes wrong:** `BpmChangedEvent`/`BpiChangedEvent` are processed in the editor's `drainEvents()` but VideoCompanion may not be active (user hasn't clicked Video yet). Sending bufferDelay to a non-existent WebSocket server is a no-op but must be handled cleanly.
**Why it happens:** BPM/BPI changes happen on the server regardless of video state.
**How to avoid:** Guard `broadcastBufferDelay()` with `isActive()` check. Also cache the last-computed delay so it can be sent immediately when the WebSocket server starts and a client connects (via the existing `sendConfigToClient()` pattern).
**Warning signs:** First-connected companion page doesn't receive bufferDelay; delay only updates after next BPM/BPI change.
[VERIFIED: existing pattern in VideoCompanion.cpp line 338 checks isActive() before roster broadcast]

## Code Examples

### VDO.Ninja setBufferDelay postMessage Format
```typescript
// Source: VDO.Ninja iframe API
// [VERIFIED: docs.vdo.ninja/guides/iframe-api-documentation/iframe-api-basics]

// Set default buffer delay for all streams (milliseconds)
iframe.contentWindow.postMessage({ setBufferDelay: 8000 }, '*');

// Set for a specific stream (by stream ID)
iframe.contentWindow.postMessage({ setBufferDelay: 8000, streamID: 'Daveguitar' }, '*');

// Set for all streams explicitly
iframe.contentWindow.postMessage({ setBufferDelay: 8000, UUID: '*' }, '*');
```

### VDO.Ninja URL Parameters for Bandwidth Profiles
```typescript
// Source: VDO.Ninja docs
// [VERIFIED: docs.vdo.ninja/advanced-settings/video-parameters/and-quality]
// [VERIFIED: docs.vdo.ninja/advanced-settings/video-bitrate-parameters/and-maxvideobitrate]
//
// CRITICAL: &quality numbering is INVERTED from intuition.
//   quality=0 = 1080p60 (highest)
//   quality=1 = 720p60  (middle)
//   quality=2 = 360p30  (lowest)

const BANDWIDTH_PROFILES = {
  low:      { quality: 2, maxvideobitrate: 500  },  // 360p30, 500kbps
  balanced: { quality: 1, maxvideobitrate: 1500 },  // 720p60, 1.5Mbps
  high:     { quality: 0, maxvideobitrate: 3000 },  // 1080p60, 3Mbps
} as const;
```

### VDO.Ninja URL with Chunked Mode + Viewer Buffer Params
```typescript
// Source: VDO.Ninja chunked mode docs
// [VERIFIED: docs.vdo.ninja/advanced-settings/settings-parameters/and-chunked]

// Sender (push) URL needs &chunked for custom buffering
function buildSenderUrl(room: string, push: string): string {
  return `https://vdo.ninja/?room=${room}&push=${push}&noaudio&cleanoutput&chunked`;
}

// Viewer parameters for fixed buffer delay (disable adaptive)
function buildViewerParams(): string {
  return '&chunkbufferadaptive=0&chunkbufferceil=180000';
}
```

### SHA-256 Hash for Room Security (C++)
```cpp
// Source: CONTEXT.md D-05, D-06
// Requires: juce_cryptography linked in CMakeLists.txt
// [VERIFIED: libs/juce/modules/juce_cryptography/hashing/juce_SHA256.h]

juce::String deriveRoomHash(const juce::String& password, const juce::String& roomId)
{
    if (password.isEmpty()) return {};  // D-07

    juce::String input = password + ":" + roomId;
    juce::SHA256 sha(input.toUTF8());
    // Use 8 hex chars (4 bytes) -- sufficient for room-level security
    // VDO.Ninja &hash supports 1-6 char hashes; we use 8 for lower false-positive rate
    return sha.toHexString().substring(0, 8);
}
```

### Companion URL with Hash Fragment
```cpp
// Source: CONTEXT.md D-05, VDO.Ninja docs on URL fragments
// [CITED: docs.vdo.ninja/advanced-settings/setup-parameters/and-password]

juce::String buildCompanionUrl(const juce::String& roomId,
                               const juce::String& pushId,
                               int wsPort,
                               const juce::String& hashFragment)
{
    juce::String url = "https://jamwide.audio/video/?room="
        + juce::URL::addEscapeChars(roomId, true)
        + "&push="
        + juce::URL::addEscapeChars(pushId, true)
        + "&wsPort="
        + juce::String(wsPort);

    // Append hash as URL fragment (never sent to server)
    if (hashFragment.isNotEmpty())
        url += "#hash=" + hashFragment;

    return url;
}
```

### BufferDelay Message Type (TypeScript)
```typescript
// Source: CONTEXT.md D-04

export interface BufferDelayMessage {
  type: 'bufferDelay';
  delayMs: number;
}

export function isBufferDelayMessage(msg: unknown): msg is BufferDelayMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  const m = msg as Record<string, unknown>;
  return m.type === 'bufferDelay' && typeof m.delayMs === 'number';
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| VDO.Ninja `&buffer` (WebRTC jitter buffer) | `&chunked` + `setBufferDelay` (custom buffer) | VDO.Ninja v24+ (2025) | Enables buffer delays >4 seconds needed for NINJAM intervals |
| Password in URL query string `?password=X` | Password in URL fragment `#password=X` or `#hash=X` | VDO.Ninja v23+ (2024) | Fragment is never sent to server, better privacy |
| `&chunkprofile=mobile\|balanced\|desktop` | `&quality=0\|1\|2` + `&maxvideobitrate=N` | Current | More granular control; chunkprofile applies to chunked mode only |

**Deprecated/outdated:**
- VDO.Ninja `&chunkprofile=mobile|balanced|desktop` -- still works but `&quality` + `&maxvideobitrate` gives finer control and works in both chunked and non-chunked modes [ASSUMED]

## Assumptions Log

> List all claims tagged `[ASSUMED]` in this research.

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | postMessage to iframe is silently dropped before iframe load event fires | Pitfall 4 | Buffer delay not applied on initial load; easy to test and add `load` event listener |
| A2 | Cross-origin iframe prevents positioning labels over individual VDO.Ninja video tiles | Pitfall 5 | If positioning is possible, could improve UX; overlay strip approach works regardless |
| A3 | `&chunkprofile` is less granular than `&quality` + `&maxvideobitrate` | State of the Art | Low risk; both approaches work, just different parameter granularity |
| A4 | JamWide's custom SHA-256 hash format (8 hex chars from `password:roomId`) is accepted by VDO.Ninja's `&hash` parameter | Architecture | MEDIUM risk -- if VDO.Ninja only accepts its own 4-char hash format, room security won't validate. See Open Question 1. |

## Open Questions

1. **VDO.Ninja &hash compatibility with custom hash values**
   - What we know: VDO.Ninja's `&hash` is generated by `SHA-256(encodeURIComponent(password) + salt)` truncated to 4 hex chars. JamWide's D-06 specifies `SHA-256(ninjam_password + ":" + room_id)` with 8 hex chars.
   - What's unclear: Whether VDO.Ninja validates `&hash` by recomputing the hash from the entered password, or simply stores it as an opaque token. If VDO.Ninja recomputes, JamWide's hash won't match.
   - Recommendation: Use VDO.Ninja's native hash algorithm (`SHA-256(encodeURIComponent(password) + roomName)` truncated to 4 hex chars) to ensure compatibility, OR use `#password=` fragment with the actual NINJAM password to let VDO.Ninja handle hashing natively. The second approach is simpler and guaranteed compatible. **Recommend using `#password=DERIVED_PASSWORD` where DERIVED_PASSWORD = hex(SHA-256(ninjam_password + ":" + room_id)).substring(0,16)**. This way VDO.Ninja sees a password (not a hash), handles its own hashing internally, and all JamWide users with the same NINJAM password derive the same VDO.Ninja password deterministically.

2. **Chunked mode impact on video quality and latency**
   - What we know: `&chunked` enables custom buffering beyond 4 seconds. Chunked mode may increase perceived latency and change video codec behavior.
   - What's unclear: Whether chunked mode degrades video quality for low-bandwidth users. Whether it significantly increases CPU usage.
   - Recommendation: Accept the tradeoff. Chunked mode is required for interval-synced buffering with typical NINJAM settings. Users who want lower latency can skip buffer sync (Phase 13 could add a toggle).

3. **Roster label positioning strategy**
   - What we know: Cross-origin iframe prevents accessing VDO.Ninja's internal DOM. Labels cannot be positioned over individual video tiles.
   - What's unclear: Best UX for showing name associations -- overlay strip, sidebar, or bottom bar.
   - Recommendation: Use a semi-transparent overlay strip at the bottom of the `#main-area`, showing connected users as pill-shaped badges. Each badge shows the NINJAM username. This avoids the impossible task of aligning with VDO.Ninja's grid layout and is consistent with the companion page's existing minimalist design.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Vitest 4.1.3 (already available via npx) |
| Config file | None -- see Wave 0 |
| Quick run command | `cd companion && npx vitest run --reporter=verbose` |
| Full suite command | `cd companion && npx vitest run --reporter=verbose` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| VID-08 | bufferDelay message parsed and forwarded to postMessage | unit | `cd companion && npx vitest run src/__tests__/buffer-delay.test.ts` | Wave 0 |
| VID-09 | Hash derivation produces deterministic result from password+roomId | unit (C++ + TS) | C++: `cd build && ctest -R test_video_hash`; TS: `cd companion && npx vitest run src/__tests__/url-builder.test.ts` | Wave 0 |
| VID-10 | Roster message renders name labels | unit | `cd companion && npx vitest run src/__tests__/roster-labels.test.ts` | Wave 0 |
| VID-12 | Bandwidth profile dropdown persists selection and rebuilds URL | unit | `cd companion && npx vitest run src/__tests__/bandwidth-profile.test.ts` | Wave 0 |

### Sampling Rate
- **Per task commit:** `cd companion && npx vitest run`
- **Per wave merge:** Full suite + manual browser test
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `companion/vitest.config.ts` -- Vitest config for companion project
- [ ] `companion/src/__tests__/buffer-delay.test.ts` -- covers VID-08
- [ ] `companion/src/__tests__/url-builder.test.ts` -- covers VID-09 (hash in URL) and VID-12 (quality params)
- [ ] `companion/src/__tests__/roster-labels.test.ts` -- covers VID-10
- [ ] `companion/src/__tests__/bandwidth-profile.test.ts` -- covers VID-12 (dropdown persistence)
- [ ] Framework install: `cd companion && npm install -D vitest @vitest/runner jsdom`

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A -- VDO.Ninja handles room auth |
| V3 Session Management | No | N/A -- no server-side sessions |
| V4 Access Control | Yes (room security) | Deterministic hash from NINJAM password; URL fragment prevents server-side exposure |
| V5 Input Validation | Yes | Type guards on WebSocket messages (existing pattern); `textContent` for DOM rendering (XSS-safe) |
| V6 Cryptography | Yes | `juce::SHA256` for hash derivation -- never hand-roll |

### Known Threat Patterns

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Password exposure in URL | Information Disclosure | Use URL fragment (#), never query string (?) [CITED: docs.vdo.ninja/advanced-settings/setup-parameters/and-password] |
| XSS via roster usernames | Tampering | Use `textContent` (never `innerHTML`) for all user-provided strings [VERIFIED: existing pattern in companion/src/ui.ts] |
| WebSocket message injection | Tampering | Type guard validation on all incoming messages [VERIFIED: companion/src/types.ts isConfigMessage/isRosterMessage] |
| Predictable room hash | Spoofing | SHA-256 + unique salt (password + roomId) makes guessing infeasible |

## Sources

### Primary (HIGH confidence)
- `juce/video/VideoCompanion.h` / `.cpp` -- existing WebSocket server, room ID derivation, roster broadcast, sendConfigToClient pattern [VERIFIED: local source]
- `companion/src/ui.ts` -- existing buildVdoNinjaUrl, effects dropdown, iframe loading [VERIFIED: local source]
- `companion/src/types.ts` -- existing type guards pattern [VERIFIED: local source]
- `companion/src/main.ts` -- existing WebSocket callback wiring [VERIFIED: local source]
- `companion/src/ws-client.ts` -- existing WebSocket client [VERIFIED: local source]
- `libs/juce/modules/juce_cryptography/hashing/juce_SHA256.h` -- SHA-256 API [VERIFIED: local source]
- `src/threading/ui_event.h` -- BpmChangedEvent/BpiChangedEvent definitions [VERIFIED: local source]
- `juce/NinjamRunThread.cpp:367-413` -- BPM/BPI change detection and event dispatch [VERIFIED: local source]

### Secondary (MEDIUM confidence)
- [VDO.Ninja iframe API basics](https://docs.vdo.ninja/guides/iframe-api-documentation/iframe-api-basics) -- setBufferDelay postMessage format: `{setBufferDelay: N}` [VERIFIED]
- [VDO.Ninja &chunked docs](https://docs.vdo.ninja/advanced-settings/settings-parameters/and-chunked) -- chunked mode is sender-side, viewer needs chunkbuffer* params [VERIFIED]
- [VDO.Ninja &buffer docs](https://docs.vdo.ninja/advanced-settings/video-parameters/buffer) -- non-chunked limited to ~4 seconds [VERIFIED]
- [VDO.Ninja &quality docs](https://docs.vdo.ninja/advanced-settings/video-parameters/and-quality) -- quality=0 is 1080p60, quality=2 is 360p30 [VERIFIED]
- [VDO.Ninja &maxvideobitrate docs](https://docs.vdo.ninja/advanced-settings/video-bitrate-parameters/and-maxvideobitrate) -- value in kbps [VERIFIED]
- [VDO.Ninja &hash docs](https://docs.vdo.ninja/advanced-settings/setup-parameters/and-hash) -- hash replaces password in URL [VERIFIED]
- [VDO.Ninja &password docs](https://docs.vdo.ninja/advanced-settings/setup-parameters/and-password) -- fragment params never sent to server [VERIFIED]
- [VDO.Ninja changepassword.html source](https://github.com/steveseguin/vdo.ninja/blob/develop/changepassword.html) -- SHA-256(password+salt), truncated to 4 hex chars [VERIFIED]

### Tertiary (LOW confidence)
- VDO.Ninja chunked buffer accuracy under real NINJAM sessions -- not validated; inferred from API docs only

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already bundled/linked, only juce_cryptography addition needed
- Architecture: HIGH -- extends existing patterns with minimal new code
- Pitfalls: MEDIUM-HIGH -- VDO.Ninja parameter numbering verified, chunked mode requirements verified, hash compatibility has one open question
- Security: MEDIUM -- hash approach is sound but VDO.Ninja compatibility needs validation

**Research date:** 2026-04-07
**Valid until:** 2026-05-07 (VDO.Ninja API is stable but chunked mode details may evolve)
