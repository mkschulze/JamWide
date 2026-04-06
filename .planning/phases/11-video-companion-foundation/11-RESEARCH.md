# Phase 11: Video Companion Foundation - Research

**Researched:** 2026-04-06
**Domain:** VDO.Ninja integration, WebSocket server (C++/JUCE), companion web page (Vite/TypeScript), browser detection
**Confidence:** HIGH

## Summary

Phase 11 adds a one-click video companion to JamWide that opens a branded web page in the user's default browser, embedding VDO.Ninja in an iframe with audio suppressed. The plugin communicates with the companion page via a local WebSocket server (ws://127.0.0.1) to send room configuration and roster updates.

The phase has two distinct implementation surfaces: (1) C++/JUCE plugin changes -- a Video button in ConnectionBar, a privacy modal (reusing LicenseDialog pattern), a WebSocket server, room ID derivation, and browser detection; and (2) a web companion page -- a Vite/TypeScript project in `docs/video/` that connects to the plugin's WebSocket, receives config, and renders a VDO.Ninja iframe with `&noaudio` and `&cleanoutput` parameters.

The critical technical risk is the mixed-content constraint: the companion page is served via HTTPS (GitHub Pages), but must connect to a local WebSocket on `ws://127.0.0.1`. Modern browsers (Chrome 53+, Firefox, Safari) exempt `127.0.0.1` from mixed-content blocking, but `localhost` is NOT exempt. The WebSocket URL must use `ws://127.0.0.1:{port}`, never `ws://localhost:{port}`.

**Primary recommendation:** Use IXWebSocket (v11.4.6, BSD-3, no-TLS mode) as the WebSocket server library, WDL_SHA1 (already linked) for room ID hashing, and Vite 8.x with vanilla TypeScript for the companion page. No new JUCE modules needed.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Video launch button lives in the ConnectionBar (top bar), next to connect/disconnect controls.
- **D-02:** Toggle button with state indicator. Camera icon changes color: grey = off/inactive, green = video active.
- **D-03:** Button always visible but disabled when not connected. Tooltip "Connect to a server first."
- **D-04:** Clicking while green re-opens the companion page (re-launch, not stop).
- **D-05:** On every video launch, show a modal dialog with IP exposure privacy notice and Chromium browser warning.
- **D-06:** No persistence of privacy acknowledgment -- modal shows every session.
- **D-07:** Browser detection via platform APIs. macOS: LSHandlerURLScheme. Windows: HKCU registry.
- **D-08:** Modal reuses existing LicenseDialog pattern.
- **D-09:** Branded JamWide page with logo, session info header, VDO.Ninja iframe with &cleanoutput.
- **D-10:** Audio suppression via &noaudio VDO.Ninja URL parameter.
- **D-11:** Companion page receives configuration from plugin via local WebSocket.
- **D-12:** WebSocket server implemented using a lightweight WS library compiled into the JUCE plugin.
- **D-13:** JSON message format over WebSocket.
- **D-14:** Phase 11 message types: config (room, push, password, noaudio, wsPort) and roster ({type, users}).
- **D-15:** No auto-reconnect on WebSocket drop. Manual "Reconnect" button.
- **D-16:** Room ID derived from NINJAM server address + session password.
- **D-17:** Public servers use fixed salt: Hash(server:port + "jamwide-public").
- **D-18:** One-click flow: modal -> build URL -> launch browser -> start WS server -> set button green.
- **D-19:** No video state persistence across DAW sessions.
- **D-20:** Companion page source lives in the same repo at /docs/video/. GitHub Pages from /docs.
- **D-21:** Build step via Vite. TypeScript support, dev server with hot reload. Production output to /docs/video/.
- **D-22:** NINJAM usernames sanitized to alphanumeric + underscores for VDO.Ninja push= stream IDs.
- **D-23:** Collision resolution via index suffix for duplicate sanitized names.

### Claude's Discretion
- WebSocket library choice (libwebsockets, ixwebsocket, or header-only alternative)
- Exact companion page visual design (layout grid, colors, font choices within JamWide branding)
- WebSocket port selection strategy (fixed default vs dynamic)
- VDO.Ninja URL parameter fine-tuning beyond the decided &noaudio and &cleanoutput
- Hash function choice for room ID derivation (SHA256, MD5, or similar)

### Deferred Ideas (OUT OF SCOPE)
- Popout mode (individual browser windows per user) -- Phase 13 (VID-07)
- setBufferDelay sync with NINJAM intervals -- Phase 12 (VID-08)
- Room password hardening (hash in URL fragment) -- Phase 12 (VID-09)
- Advanced roster discovery via VDO.Ninja external API -- Phase 12 (VID-10)
- OSC video control (/JamWide/video/*) -- Phase 13 (VID-11)
- Bandwidth-aware video profiles -- Phase 12 (VID-12)
- Auto-reconnect WebSocket with exponential backoff -- future enhancement
- Auto-launch video on connect preference -- future enhancement
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| VID-01 | User can launch VDO.Ninja video with one click from the plugin UI | Video button in ConnectionBar (D-01 through D-04), privacy modal (D-05/D-08), juce::URL::launchInDefaultBrowser() |
| VID-02 | User's video room ID is auto-generated from the NINJAM server address | WDL_SHA1 hash of server:port + password/salt, sanitized to VDO.Ninja room name constraints (1-49 alphanumeric chars) |
| VID-03 | User hears no duplicate audio from VDO.Ninja (audio suppressed automatically) | &noaudio URL parameter on VDO.Ninja iframe (D-10), verified via official docs |
| VID-04 | User sees all session participants in a video grid layout | VDO.Ninja room mode: guests joining with &room=X&push=Y automatically see all other room participants in grid |
| VID-05 | User receives a privacy notice about IP exposure before first video use | Privacy modal dialog every launch (D-05/D-06), copy from UI-SPEC |
| VID-06 | User is warned if their default browser is not Chromium-based | Browser detection via platform APIs (D-07), conditional section in privacy modal |
</phase_requirements>

## Standard Stack

### Core (Plugin Side -- C++)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| IXWebSocket | 11.4.6 | WebSocket server for plugin-to-companion communication | C++11, client+server, CMake-native, BSD-3 license, no-TLS mode for localhost, cross-platform (macOS/Windows/Linux) |
| WDL_SHA1 | (bundled) | Room ID hashing from server address + password | Already linked in project (wdl static lib), no new dependency |
| JUCE juce_gui_extra | (bundled) | juce::URL::launchInDefaultBrowser() | Already linked, handles opening default browser cross-platform |

[VERIFIED: npm registry for IXWebSocket version is not applicable -- GitHub release v11.4.6 confirmed via web search]
[VERIFIED: WDL SHA1 already in project CMakeLists.txt wdl target]
[VERIFIED: juce_gui_extra already linked in CMakeLists.txt]

### Core (Companion Page -- Web)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Vite | 8.0.5 | Build tool, dev server, production bundling | Decision D-21, standard for modern web projects |
| TypeScript | 6.0.2 | Type safety for companion page | Decision D-21 |

[VERIFIED: npm registry -- vite@8.0.5, typescript@6.0.2 are current]

### Alternatives Considered (Claude's Discretion Resolution)

| Instead of | Could Use | Tradeoff | Recommendation |
|------------|-----------|----------|----------------|
| IXWebSocket | libwebsockets | Pure C, more protocols (HTTP/2, MQTT), but heavier, complex API, requires CMake integration effort | **Use IXWebSocket** -- simpler C++ API, server+client in one lib, no-TLS mode avoids OpenSSL dependency |
| IXWebSocket | websocketpp | Header-only, but requires Boost.Asio or standalone Asio, heavier dependency chain | **Use IXWebSocket** -- fewer transitive deps |
| WDL_SHA1 | juce::SHA256 (juce_cryptography) | SHA-256 is stronger, but requires adding juce_cryptography module | **Use WDL_SHA1** -- already linked, SHA-1 is fine for non-security room ID derivation |
| WDL_SHA1 | juce::MD5 (juce_cryptography) | Also requires juce_cryptography module | **Use WDL_SHA1** -- zero new deps |
| Fixed port 7170 | Dynamic port (try, increment) | Dynamic handles multi-instance but adds complexity | **Use fixed default 7170** for Phase 11; multi-instance is rare for this use case |

**WebSocket port strategy:** Use port 7170 as fixed default. This port is unassigned by IANA, unlikely to conflict. The companion page URL will include the port as a query parameter (e.g., `?wsPort=7170`), making future dynamic allocation possible without changing the companion page code.

**Hash function:** Use WDL_SHA1 already linked in the `wdl` static library target. SHA-1 produces a 40-hex-char digest; truncate to the first 16 characters for VDO.Ninja room names (which allow 1-49 alphanumeric chars). This provides sufficient uniqueness (64 bits) while keeping room IDs readable.

**Installation (IXWebSocket):**
```bash
git submodule add https://github.com/machinezone/IXWebSocket.git libs/ixwebsocket
```

**CMake integration:**
```cmake
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_WS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/ixwebsocket EXCLUDE_FROM_ALL)
target_link_libraries(JamWideJuce PRIVATE ixwebsocket)
```

**Companion page setup:**
```bash
cd docs/video
npm create vite@latest . -- --template vanilla-ts
npm install
```

## Architecture Patterns

### Recommended Project Structure
```
juce/
  video/
    VideoCompanion.h         # WebSocket server + room ID + browser launch
    VideoCompanion.cpp       # Implementation
    VideoPrivacyDialog.h     # Privacy/browser-warning modal
    VideoPrivacyDialog.cpp   # Implementation
    BrowserDetect.h          # Platform-specific default browser detection
    BrowserDetect_mac.mm     # macOS: LSCopyDefaultHandlerForURLScheme
    BrowserDetect_win.cpp    # Windows: HKCU registry read
docs/
  video/
    index.html               # Companion page entry point
    src/
      main.ts                # App entry, WebSocket connection, iframe management
      ws-client.ts           # WebSocket client logic
      types.ts               # Message type definitions (config, roster)
      ui.ts                  # DOM manipulation, status badges, reconnect button
    vite.config.ts           # Vite config (base path, build output)
    tsconfig.json            # TypeScript config
    package.json             # Dev dependencies only
```

### Pattern 1: VideoCompanion Lifecycle (JUCE Plugin Side)
**What:** Single-owner component managing WebSocket server, room ID derivation, and browser launch
**When to use:** For all video companion functionality in the processor
**Example:**
```cpp
// Source: Follows OscServer ownership pattern (juce/osc/OscServer.h)
class VideoCompanion
{
public:
    explicit VideoCompanion(JamWideJuceProcessor& proc);
    ~VideoCompanion();

    // Called by UI thread when user clicks Video button
    void launchCompanion(const juce::String& serverAddr,
                         const juce::String& username,
                         const juce::String& password);

    // Called by run thread (or bridged via event) on roster change
    void sendRosterUpdate(const std::vector<NJClient::RemoteUserInfo>& users);

    bool isActive() const { return active.load(std::memory_order_relaxed); }

private:
    void startWebSocketServer();
    void stopWebSocketServer();
    juce::String deriveRoomId(const juce::String& serverAddr,
                              const juce::String& password);
    juce::String sanitizeUsername(const juce::String& name);

    JamWideJuceProcessor& processor;
    std::atomic<bool> active{false};
    int wsPort = 7170;
    // IXWebSocket server instance
};
```

### Pattern 2: WebSocket JSON Protocol
**What:** Simple JSON messages between plugin and companion page
**When to use:** All plugin-to-companion communication
**Example:**
```cpp
// Plugin sends on WebSocket connect:
// {"type":"config","room":"a1b2c3d4e5f6g7h8","push":"Dave_guitar","noaudio":true,"wsPort":7170}

// Plugin sends on roster change:
// {"type":"roster","users":[{"idx":0,"name":"Dave@guitar","streamId":"Daveguitar"},{"idx":1,"name":"Bob","streamId":"Bob"}]}
```

### Pattern 3: Browser Detection (Platform-Specific)
**What:** Detect default browser bundle ID to warn about non-Chromium browsers
**When to use:** Before showing privacy modal, to conditionally show browser warning section
**Example:**
```objc
// macOS (BrowserDetect_mac.mm)
// Source: [CITED: developer.apple.com/documentation/coreservices/1441725-lscopydefaulthandlerforurlscheme]
#import <CoreServices/CoreServices.h>

bool isDefaultBrowserChromium()
{
    CFStringRef bundleId = LSCopyDefaultHandlerForURLScheme(CFSTR("https"));
    if (!bundleId) return false;

    NSString* bid = (__bridge_transfer NSString*)bundleId;
    // Known Chromium-based browser bundle IDs
    NSArray* chromiumIds = @[
        @"com.google.Chrome",
        @"com.microsoft.edgemac",
        @"com.brave.Browser",
        @"com.vivaldi.Vivaldi",
        @"com.opera.Opera",
        @"com.nickvision.nickel"
    ];
    for (NSString* cid in chromiumIds) {
        if ([bid isEqualToString:cid]) return true;
    }
    return false;
}
```

```cpp
// Windows (BrowserDetect_win.cpp)
// Source: [CITED: Registry path from HKCU\Software\Microsoft\Windows\Shell\Associations]
#include <windows.h>
#include <string>

bool isDefaultBrowserChromium()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice",
        0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t progId[256];
    DWORD size = sizeof(progId);
    bool result = false;
    if (RegQueryValueExW(hKey, L"ProgId", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(progId), &size) == ERROR_SUCCESS)
    {
        std::wstring id(progId);
        // Known Chromium ProgIds
        result = (id.find(L"ChromeHTML") != std::wstring::npos ||
                  id.find(L"MSEdgeHTM") != std::wstring::npos ||
                  id.find(L"BraveHTML") != std::wstring::npos ||
                  id.find(L"VivaldiHTM") != std::wstring::npos ||
                  id.find(L"OperaStable") != std::wstring::npos);
    }
    RegCloseKey(hKey);
    return result;
}
```

### Pattern 4: Room ID Derivation
**What:** Deterministic room ID from NINJAM server address + password
**When to use:** Every video launch to auto-generate VDO.Ninja room name
**Example:**
```cpp
// Source: Uses WDL_SHA1 already linked in project (wdl/sha.h)
#include "wdl/sha.h"

juce::String deriveRoomId(const juce::String& serverAddr, const juce::String& password)
{
    juce::String input = serverAddr;
    if (password.isEmpty())
        input += ":jamwide-public";  // D-17: fixed salt for public servers
    else
        input += ":" + password;

    WDL_SHA1 sha;
    auto utf8 = input.toUTF8();
    sha.add(utf8.getAddress(), static_cast<int>(utf8.sizeInBytes() - 1));

    unsigned char hash[WDL_SHA1SIZE]; // 20 bytes
    sha.result(hash);

    // Convert first 8 bytes to 16 hex chars (fits VDO.Ninja 1-49 char limit)
    juce::String roomId = "jw-";
    for (int i = 0; i < 8; ++i)
        roomId += juce::String::toHexString(hash[i]).paddedLeft('0', 2);

    return roomId;  // e.g., "jw-a1b2c3d4e5f6g7h8"
}
```

### Pattern 5: VDO.Ninja Companion URL Construction
**What:** Build the full VDO.Ninja URL with all required parameters
**When to use:** After user accepts privacy modal, before launching browser
**Example:**
```cpp
juce::String buildCompanionUrl(const juce::String& roomId,
                                const juce::String& pushId,
                                int wsPort)
{
    // Companion page URL (GitHub Pages)
    juce::String url = "https://video.jamwide.app/";  // or docs path
    url += "?room=" + juce::URL::addEscapeChars(roomId, true);
    url += "&push=" + juce::URL::addEscapeChars(pushId, true);
    url += "&wsPort=" + juce::String(wsPort);
    return url;
}
```

The companion page (not the plugin) builds the VDO.Ninja iframe URL:
```typescript
// Companion page builds VDO.Ninja URL from config received via WebSocket
function buildVdoNinjaUrl(config: Config): string {
    const base = "https://vdo.ninja/";
    const params = new URLSearchParams({
        room: config.room,
        push: config.push,
        noaudio: "",       // presence = enabled
        cleanoutput: "",   // presence = enabled
    });
    return `${base}?${params.toString()}`;
}
```

### Anti-Patterns to Avoid
- **ws://localhost instead of ws://127.0.0.1:** Browsers exempt 127.0.0.1 from mixed-content blocking but NOT localhost. Using localhost will cause WebSocket connection failures from HTTPS-served companion pages. [VERIFIED: Chrome issue tracker, Mozilla bug tracker]
- **Audio thread WebSocket operations:** WebSocket server must run on its own thread (IXWebSocket handles this internally). Never call WS send/receive from JUCE audio thread.
- **Embedding VDO.Ninja URL in plugin binary:** The companion page must be a separate web page (not BinaryData resource) because it needs to be served over HTTPS for WebRTC to work, and needs its own origin context for iframe permissions.
- **Using `&view` without `&room`:** For auto-grid of all participants, guests must join with `&room=X&push=Y`. Using `&view` alone shows nothing unless specific stream IDs are provided.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WebSocket protocol | Custom HTTP upgrade + frame parsing | IXWebSocket server | RFC 6455 compliance, ping/pong, frame masking, per-message deflate |
| Room ID hashing | String concatenation or simple modulo | WDL_SHA1 + hex truncation | Collision-resistant, deterministic, already linked |
| Browser opening | Platform-specific exec/open calls | juce::URL::launchInDefaultBrowser() | Already handles macOS/Windows/Linux, sandboxing, URL encoding |
| JSON serialization | Manual string building | Simple manual construction is OK for 2 message types | Only 2 message types with flat structure; a full JSON library (nlohmann) is overkill. Use juce::var and juce::JSON if preferred. |
| Username sanitization | Regex | Simple character-by-character filter | Only need alphanum + underscore; regex is overkill and a dependency |

**Key insight:** The plugin-side WebSocket is localhost-only, unencrypted, with a trivial 2-message JSON protocol. Simplicity beats features here. IXWebSocket provides the minimal reliable WebSocket server implementation without dragging in TLS dependencies.

## Common Pitfalls

### Pitfall 1: Mixed Content Blocking (ws:// from HTTPS)
**What goes wrong:** Companion page served via HTTPS (GitHub Pages) cannot connect to ws://localhost WebSocket. Browser blocks connection as mixed content.
**Why it happens:** Browsers block HTTP content loaded from HTTPS pages by default.
**How to avoid:** Always use `ws://127.0.0.1:{port}`, never `ws://localhost:{port}`. Chrome 53+ and Firefox explicitly exempt 127.0.0.1 from mixed-content rules. Safari also exempts loopback.
**Warning signs:** WebSocket connection fails silently or with "Mixed Content" error in browser console.
[VERIFIED: Chrome issue 40386732, Mozilla bug 1376309, MDN mixed content docs]

### Pitfall 2: WebSocket Port Already In Use
**What goes wrong:** If user runs two JamWide instances, second instance fails to bind port 7170.
**Why it happens:** OS prevents two processes from binding the same TCP port.
**How to avoid:** For Phase 11, document this as a known limitation. The companion page URL includes `wsPort=` parameter, so future phases can implement port probing (try 7170, 7171, 7172...) without changing companion page code.
**Warning signs:** IXWebSocket server start returns error; log and set error state on VideoCompanion.

### Pitfall 3: LSCopyDefaultHandlerForURLScheme Deprecation (macOS)
**What goes wrong:** Apple deprecated this API, may remove in future macOS versions.
**Why it happens:** Apple is consolidating Launch Services APIs.
**How to avoid:** Wrap in a try/catch or availability check. As fallback, assume Chromium (skip warning). The warning is advisory, not blocking -- the worst case is not showing a warning to a Safari user. [CITED: Apple Developer docs note deprecation but no replacement date]
**Warning signs:** Compiler deprecation warnings on newer macOS SDKs.

### Pitfall 4: SPSC Queue Single-Producer Violation
**What goes wrong:** VideoCompanion sends roster updates from the run thread, but cmd_queue assumes single producer (UI thread).
**Why it happens:** Roster changes fire on the run thread (NinjamRunThread::run), but WebSocket operations happen from the VideoCompanion.
**How to avoid:** VideoCompanion's `sendRosterUpdate` must be called from the correct thread context. The existing pattern is: run thread pushes `UserInfoChangedEvent` to `evt_queue`, UI thread drains and triggers roster send via VideoCompanion. Keep WebSocket sends on the message thread or on IXWebSocket's own thread (via a thread-safe send queue). IXWebSocket's send() is thread-safe internally.
**Warning signs:** Garbled messages, crashes in release builds, ASAN data race reports.

### Pitfall 5: VDO.Ninja Room Name Constraints
**What goes wrong:** Room ID contains invalid characters, VDO.Ninja silently fails.
**Why it happens:** VDO.Ninja room names are 1-49 characters, alphanumeric + hyphens only, case-sensitive.
**How to avoid:** The SHA1 hex output naturally produces only [0-9a-f] characters. The "jw-" prefix adds hyphens. Result like "jw-a1b2c3d4e5f6g7h8" (19 chars) is always valid. [VERIFIED: docs.vdo.ninja/advanced-settings/setup-parameters/room]
**Warning signs:** Users not appearing in same room despite same server.

### Pitfall 6: Username Sanitization Collisions
**What goes wrong:** Two users with different NINJAM names sanitize to the same VDO.Ninja stream ID, causing stream conflicts.
**Why it happens:** Sanitization strips non-alphanumeric characters. "Dave@guitar" and "Dave_guitar" both become "Daveguitar" (only underscores kept per D-22 actually: "Dave@guitar" -> "Daveguitar", "Dave_guitar" -> "Dave_guitar").
**How to avoid:** D-23 specifies collision resolution via index suffix ("_2", "_3"). Implement by maintaining a set of assigned stream IDs and appending suffix on collision.
**Warning signs:** VDO.Ninja shows fewer streams than expected.

### Pitfall 7: macOS Firewall Prompt on Localhost Binding
**What goes wrong:** macOS may prompt "Do you want JamWide to accept incoming network connections?" when binding a TCP server socket.
**Why it happens:** macOS Application Firewall detects new server sockets. Binding to 127.0.0.1 specifically (not 0.0.0.0) typically avoids this, but behavior varies.
**How to avoid:** Bind explicitly to `127.0.0.1` (not `0.0.0.0` or `::1`). If firewall prompt still appears, it's a one-time user action. Document in release notes. Code-signed builds are less likely to trigger repeated prompts. [ASSUMED]
**Warning signs:** Users report firewall dialogs on first video launch.

## Code Examples

### VDO.Ninja URL Parameters (Official Docs)

```
# Audio suppression -- disables audio playback entirely
# Source: [CITED: docs.vdo.ninja/advanced-settings/audio-parameters/noaudio]
&noaudio

# Clean output -- removes all VDO.Ninja UI chrome
# Source: [CITED: docs.vdo.ninja/advanced-settings/design-parameters/cleanoutput]
&cleanoutput

# Room join -- guests see all other room participants automatically
# Source: [CITED: docs.vdo.ninja/getting-started/rooms]
&room=ROOMNAME

# Stream ID -- publish with specific ID for user-to-stream mapping
# Source: [CITED: docs.vdo.ninja/advanced-settings/setup-parameters/push]
&push=STREAMID

# Room name constraints: 1-49 chars, alphanumeric + hyphens, case-sensitive
# Source: [CITED: docs.vdo.ninja/advanced-settings/setup-parameters/room]

# Password constraints: 1-49 chars, alphanumeric only, case-sensitive
# Source: [CITED: docs.vdo.ninja/advanced-settings/setup-parameters/and-password]
```

### IXWebSocket Server (Minimal Example)

```cpp
// Source: [CITED: machinezone.github.io/IXWebSocket]
#include <ixwebsocket/IXWebSocketServer.h>

ix::WebSocketServer server(7170, "127.0.0.1");  // Bind localhost only

server.setOnClientMessageCallback(
    [](std::shared_ptr<ix::ConnectionState> connectionState,
       ix::WebSocket& webSocket,
       const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            // New client connected -- send config
            webSocket.send(R"({"type":"config","room":"jw-abc123","push":"Dave","noaudio":true,"wsPort":7170})");
        }
        else if (msg->type == ix::WebSocketMessageType::Close) {
            // Client disconnected
        }
    }
);

auto res = server.listen();
if (res.first) {
    server.start();  // Non-blocking, runs on IXWebSocket's own thread
}
// ... later:
server.stop();
```

### Companion Page WebSocket Client (TypeScript)

```typescript
// Source: Pattern based on D-11, D-13, D-14, D-15 decisions
interface ConfigMessage {
    type: "config";
    room: string;
    push: string;
    noaudio: boolean;
    wsPort: number;
}

interface RosterUser {
    idx: number;
    name: string;
    streamId: string;
}

interface RosterMessage {
    type: "roster";
    users: RosterUser[];
}

type PluginMessage = ConfigMessage | RosterMessage;

function connectToPlugin(port: number): WebSocket {
    // CRITICAL: Use 127.0.0.1, NOT localhost (mixed content)
    const ws = new WebSocket(`ws://127.0.0.1:${port}`);

    ws.onopen = () => {
        updateStatus("connected");
    };

    ws.onmessage = (event) => {
        const msg: PluginMessage = JSON.parse(event.data);
        if (msg.type === "config") {
            loadVdoNinjaIframe(msg);
        } else if (msg.type === "roster") {
            updateRoster(msg.users);
        }
    };

    ws.onclose = () => {
        updateStatus("disconnected");
    };

    return ws;
}
```

### LicenseDialog Reuse Pattern (Privacy Modal)

```cpp
// Source: Existing pattern in juce/ui/LicenseDialog.h/cpp
// VideoPrivacyDialog follows the same structure:
// - Dark overlay scrim (kSurfaceScrim)
// - Centered dialog (480x320 per UI-SPEC)
// - Accept/Skip buttons
// - No outside-click dismiss
// Key differences from LicenseDialog:
// - Title: "Video -- Privacy Notice" (not "Server License Agreement")
// - Body: Two sections (IP disclosure + optional browser warning)
// - Accept text: "I Understand -- Open Video"
// - Skip text: "Skip Video"
// - Size: 480x320 (not 500x400)
// - Accept button has initial keyboard focus
// - Escape key triggers Skip Video
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Embedded WebView in plugin | Browser companion page | VDO.Ninja best practice | Avoids 50-100MB WebView dependency, works with all plugin formats |
| Plugin serves HTML via embedded HTTP server | HTTPS companion page + WS config channel | Mixed content rules tightened 2020+ | Must serve from HTTPS origin; plugin only needs WS server |
| ws://localhost for local WS | ws://127.0.0.1 for local WS | Chrome 53 (2016) | Browsers exempt 127.0.0.1 but not localhost from mixed content |
| VDO.Ninja &password in URL | &hash= with # fragment | VDO.Ninja v24+ | Prevents password logging by signaling server (deferred to Phase 12) |
| LSCopyDefaultHandlerForURLScheme | Same API (deprecated but no replacement) | macOS 12+ deprecation | Still works, use with deprecation pragmas |

**Deprecated/outdated:**
- `LSCopyDefaultHandlerForURLScheme`: Deprecated in macOS 12 but no replacement API exists. Still functional. Use `#pragma clang diagnostic ignored "-Wdeprecated-declarations"` around the call. [CITED: Apple Developer docs]

## Assumptions Log

> List all claims tagged `[ASSUMED]` in this research. The planner and discuss-phase use this
> section to identify decisions that need user confirmation before execution.

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Binding to 127.0.0.1 specifically avoids macOS firewall prompts in most cases | Pitfall 7 | Users may get unexpected firewall dialogs; advisory only, not blocking |
| A2 | Port 7170 is unlikely to conflict with other software | Standard Stack | If conflicting, WS server fails to start; documented as known limitation for Phase 11 |
| A3 | VDO.Ninja room guests automatically see all other guests in a grid layout | Architecture Patterns | If not, companion page may need &scene or additional parameters |

## Open Questions

1. **Windows Firewall Prompt on Localhost Bind**
   - What we know: macOS firewall may prompt; binding to 127.0.0.1 usually avoids it. Windows Defender Firewall behavior for localhost binding is unvalidated.
   - What's unclear: Does Windows prompt when a JUCE plugin binds a TCP server socket to 127.0.0.1?
   - Recommendation: Test during implementation. If prompted, document as known behavior. The state blocker in STATE.md ("OpenSSL linkage on Windows CI unvalidated") is unrelated since we chose IXWebSocket without TLS.

2. **IXWebSocket Thread Safety with JUCE Message Thread**
   - What we know: IXWebSocket's send() is thread-safe. IXWebSocket runs its own event loop thread.
   - What's unclear: Are there potential issues with IXWebSocket's thread interacting with JUCE's message thread or process lifecycle?
   - Recommendation: VideoCompanion destructor must call `server.stop()` before IXWebSocket is destroyed. Follow OscServer's `alive` shared_ptr pattern for safe async callbacks.

3. **GitHub Pages CNAME for video.jamwide.app**
   - What we know: D-21 mentions custom subdomain. GitHub Pages supports CNAME.
   - What's unclear: Is the domain jamwide.app registered and configured for GitHub Pages? Is the CNAME already set up?
   - Recommendation: Use relative path (`/docs/video/`) during development. CNAME setup is a deployment task, not a code task.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Node.js | Companion page build (Vite) | Yes | v22.22.1 | -- |
| npm | Companion page dependencies | Yes | 10.9.4 | -- |
| CMake | Plugin build, IXWebSocket integration | Yes | 4.3.1 | -- |
| Git | IXWebSocket submodule | Yes | (system) | -- |
| Xcode / clang | macOS Obj-C++ for browser detection | Yes | (system) | -- |

**Missing dependencies with no fallback:**
- None -- all required tools are available.

**Missing dependencies with fallback:**
- None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CTest (CMake) + custom test executables |
| Config file | CMakeLists.txt (JAMWIDE_BUILD_TESTS option) |
| Quick run command | `cmake --build build --target test_ws_loopback && ctest --test-dir build -R ws_loopback` |
| Full suite command | `cmake -B build -DJAMWIDE_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| VID-01 | One-click video launch (button click -> browser open) | manual-only | Manual: click Video button in connected state, verify browser opens | -- |
| VID-02 | Room ID auto-derived from server address | unit | `ctest --test-dir build -R room_id_derivation` | Wave 0 |
| VID-03 | Audio suppression via &noaudio param | unit | `ctest --test-dir build -R vdo_url_params` | Wave 0 |
| VID-04 | Video grid shows all participants | manual-only | Manual: connect 2+ users, verify grid | -- |
| VID-05 | Privacy notice modal shown | manual-only | Manual: click Video, verify modal appears | -- |
| VID-06 | Chromium browser warning | unit | `ctest --test-dir build -R browser_detect` | Wave 0 |

### Sampling Rate
- **Per task commit:** Build plugin (`cmake --build build`), run unit tests if applicable
- **Per wave merge:** Full build + pluginval validation (`cmake --build build --target validate`)
- **Phase gate:** Full suite green + manual VID-01/VID-04/VID-05 verification

### Wave 0 Gaps
- [ ] `tests/test_room_id.cpp` -- covers VID-02 (room ID derivation from server address + password)
- [ ] `tests/test_username_sanitize.cpp` -- covers D-22/D-23 (username -> stream ID sanitization + collision resolution)
- [ ] `tests/test_vdo_url.cpp` -- covers VID-03 (URL parameter construction with &noaudio)
- [ ] IXWebSocket submodule added: `git submodule add https://github.com/machinezone/IXWebSocket.git libs/ixwebsocket`

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | WebSocket is localhost-only, no auth needed for Phase 11 |
| V3 Session Management | No | No sessions; each browser tab is independent |
| V4 Access Control | No | Single-user localhost communication |
| V5 Input Validation | Yes | Validate JSON messages from WebSocket; sanitize room/push params before URL construction |
| V6 Cryptography | No | SHA-1 used for room ID derivation (not security-sensitive) |

### Known Threat Patterns

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| WebSocket hijacking from malicious tab | Spoofing | Bind to 127.0.0.1 only; future: add auth token in WS URL query param (deferred) |
| IP exposure via WebRTC STUN/TURN | Information Disclosure | Privacy modal warns user (VID-05); cannot be prevented in peer-to-peer video |
| Room ID guessing | Information Disclosure | SHA-1 hash with 64-bit truncation; deterministic but not trivially guessable from server name (D-17) |
| VDO.Ninja iframe injection | Tampering | Companion page uses fixed VDO.Ninja origin; CSP headers on GitHub Pages |

**Note:** D-16/D-17 room ID derivation is not a security boundary. The room name is not a secret -- any JamWide user on the same NINJAM server will derive the same room. Room password hardening (via &hash and # fragment) is explicitly deferred to Phase 12 (VID-09).

## Sources

### Primary (HIGH confidence)
- [VDO.Ninja &noaudio docs](https://docs.vdo.ninja/advanced-settings/audio-parameters/noaudio) -- audio suppression parameter behavior
- [VDO.Ninja &cleanoutput docs](https://docs.vdo.ninja/advanced-settings/design-parameters/cleanoutput) -- UI chrome removal
- [VDO.Ninja &room docs](https://docs.vdo.ninja/advanced-settings/setup-parameters/room) -- room name constraints (1-49 chars, alphanumeric + hyphens)
- [VDO.Ninja &push docs](https://docs.vdo.ninja/advanced-settings/setup-parameters/push) -- stream ID publishing
- [VDO.Ninja &password docs](https://docs.vdo.ninja/advanced-settings/setup-parameters/and-password) -- password constraints
- [VDO.Ninja rooms overview](https://docs.vdo.ninja/getting-started/rooms) -- guests automatically see all other room participants
- [IXWebSocket GitHub](https://github.com/machinezone/IXWebSocket) -- v11.4.6, BSD-3, server+client, cross-platform
- [IXWebSocket build docs](https://machinezone.github.io/IXWebSocket/build/) -- CMake options, USE_TLS flag
- [Apple LSCopyDefaultHandlerForURLScheme](https://developer.apple.com/documentation/coreservices/1441725-lscopydefaulthandlerforurlscheme) -- macOS default browser detection
- [JUCE SHA256](https://docs.juce.com/master/classSHA256.html) -- available but requires juce_cryptography module (not used)
- Codebase: `wdl/sha.h` -- WDL_SHA1 already linked via wdl static target
- Codebase: `juce/ui/LicenseDialog.h/cpp` -- reusable modal pattern
- Codebase: `juce/ui/ConnectionBar.h/cpp` -- button placement and layout pattern
- Codebase: `juce/osc/OscServer.h` -- lifecycle/ownership pattern for server components

### Secondary (MEDIUM confidence)
- [Chrome mixed content exception for 127.0.0.1](https://issues.chromium.org/issues/40386732) -- 127.0.0.1 exempt, localhost NOT exempt
- [Mozilla mixed content bug](https://bugzilla.mozilla.org/show_bug.cgi?id=1376309) -- Firefox matches Chrome behavior for loopback
- [Windows registry default browser](https://www.codeproject.com/Tips/607833/Where-is-the-Default-Browser-Command-Line-in-Regis) -- HKCU ProgId approach
- npm registry: vite@8.0.5, typescript@6.0.2 -- current versions verified

### Tertiary (LOW confidence)
- macOS firewall behavior for 127.0.0.1 binding -- based on general knowledge, not verified for JUCE plugins specifically
- IXWebSocket thread safety claims -- documented but not independently verified in JUCE context

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- IXWebSocket verified via GitHub, WDL_SHA1 verified in codebase, Vite/TS verified via npm
- Architecture: HIGH -- patterns derived from existing codebase (OscServer, LicenseDialog, ConnectionBar)
- Pitfalls: HIGH -- mixed content behavior verified via browser bug trackers, VDO.Ninja constraints verified via official docs
- Browser detection: MEDIUM -- API exists but macOS deprecation creates future uncertainty
- WebSocket port/firewall: MEDIUM -- reasonable defaults but untested in multi-instance and Windows scenarios

**Research date:** 2026-04-06
**Valid until:** 2026-05-06 (30 days -- VDO.Ninja URL params and IXWebSocket are stable)
