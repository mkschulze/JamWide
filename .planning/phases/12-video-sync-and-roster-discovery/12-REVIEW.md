---
phase: 12-video-sync-and-roster-discovery
reviewed: 2026-04-07T21:20:37Z
depth: standard
files_reviewed: 17
files_reviewed_list:
  - CMakeLists.txt
  - juce/video/VideoCompanion.h
  - juce/video/VideoCompanion.cpp
  - juce/JamWideJuceEditor.cpp
  - companion/index.html
  - companion/package.json
  - companion/src/__tests__/bandwidth-profile.test.ts
  - companion/src/__tests__/buffer-delay.test.ts
  - companion/src/__tests__/roster-labels.test.ts
  - companion/src/__tests__/url-builder.test.ts
  - companion/src/main.ts
  - companion/src/types.ts
  - companion/src/ui.ts
  - companion/src/ws-client.ts
  - companion/style.css
  - companion/tsconfig.json
  - companion/vitest.config.ts
findings:
  critical: 1
  warning: 3
  info: 3
  total: 7
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-04-07T21:20:37Z
**Depth:** standard
**Files Reviewed:** 17
**Status:** issues_found

## Summary

Phase 12 delivers the VDO.Ninja video companion feature: a WebSocket bridge between the JUCE plugin and a browser companion page. The implementation covers WebSocket server lifecycle, room ID / password derivation, username sanitization, collision resolution, buffer delay relay, roster label strip rendering, and bandwidth profile selection.

The overall quality is high. Security-sensitive areas are handled thoughtfully (localhost-only bind, SHA-derived room password, JSON string escaping, XSS-safe textContent). Thread safety for the roster dispatch path is correctly implemented using the established `callAsync` + `alive_` UAF pattern.

One critical data-race exists: `sendConfigToClient` reads `currentRoom_`, `currentPush_`, `cachedDelayMs_`, and `wsPort_` from the IXWebSocket callback thread without holding any lock or guarantee that the message thread has finished writing those members. Three additional warnings cover a detached-thread leak in the destructor path, an unvalidated `parseInt` in the companion page, and a missing guard for zero `delayMs` that would send a semantically meaningless message. Three info-level items round out the review.

---

## Critical Issues

### CR-01: Data race in `sendConfigToClient` — IXWebSocket callback reads unsynchronized shared state

**File:** `juce/video/VideoCompanion.cpp:232` (callback) and `283-303` (`sendConfigToClient`)

**Issue:** `startWebSocketServer()` registers a client-message callback (lambda at line 225-236) that calls `sendConfigToClient(webSocket)` directly. IXWebSocket fires this callback on its own internal thread. `sendConfigToClient` reads `currentRoom_`, `currentPush_`, `wsPort_`, and `cachedDelayMs_` (all plain non-atomic members) without holding `wsMutex_` or any other synchronization primitive. These members are written by `launchCompanion()` on the message thread while the WS server is already running (idempotent start path), and by `deactivate()` which also runs on the message thread. This is an unsynchronized concurrent read/write — a data race under C++11 memory model, with undefined behavior.

The practical window: a WS client connects immediately after `startWebSocketServer()` returns but before `launchCompanion()` finishes writing `currentRoom_` and `currentPush_`. The callback fires while the message thread is still in `launchCompanion` writing those strings, producing a torn read.

**Fix:** Guard `sendConfigToClient` with `wsMutex_`, or snapshot the required fields into a small struct under the lock before starting the server, and pass the snapshot into the callback by value:

```cpp
// Option A: Snapshot approach (preferred — avoids lock inside IXWebSocket callback)
struct SessionSnapshot {
    juce::String room, push;
    int wsPort, cachedDelayMs;
};

// In launchCompanion(), populate snapshot BEFORE startWebSocketServer():
SessionSnapshot snap { currentRoom_, currentPush_, wsPort_, cachedDelayMs_ };

wsServer_->setOnClientMessageCallback(
    [this, snap](auto /*cs*/, ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open)
            sendConfigToClient(ws, snap);
    });
```

Alternatively (Option B), wrap the reads inside `sendConfigToClient` with a lock — but note that IXWebSocket's callback can fire while `startWebSocketServer` still holds `wsMutex_` (line 216), so the lock would need to be re-entrant or the callback must not try to acquire it directly. Option A avoids this entirely.

---

## Warnings

### WR-01: Detached thread in destructor path can outlive the `VideoCompanion` object's allocator

**File:** `juce/video/VideoCompanion.cpp:268-273`

**Issue:** `stopWebSocketServer()` spawns a detached `std::thread` that calls `serverToStop->wait()` then `serverToStop.reset()`. When `deactivate()` calls `stopWebSocketServer()`, the destructor (`~VideoCompanion`) later also calls it — but at that point `wsServer_` is already null so it's a no-op. However, `deactivate()` is not the only caller: the destructor also calls `stopWebSocketServer()` directly (line 37-42 in the destructor does its own synchronized stop). The detached-thread path is the deactivate case.

The actual risk is that if the DAW destroys the plugin rapidly (calling the destructor while the detached thread from a prior `deactivate()` call is still in `s->wait()`), the thread is safe because `serverToStop` is fully owned by the lambda — but the detached thread holds a `std::unique_ptr<ix::WebSocketServer>` that may try to log or interact with the IXWebSocket thread pool after the owning DLL has been partially unloaded on Windows. On macOS this is generally fine, but on Windows plugin unloading while a detached thread is alive is a known source of crashes.

**Fix:** Store the detached thread's future so the destructor can optionally join it, or use a shared ownership model:

```cpp
// In class members, add:
std::future<void> stopFuture_;

// In stopWebSocketServer():
if (serverToStop) {
    stopFuture_ = std::async(std::launch::async,
        [s = std::move(serverToStop)]() mutable {
            s->wait();
            s.reset();
        });
}

// In ~VideoCompanion(), before the lock block:
if (stopFuture_.valid())
    stopFuture_.wait();
```

This is a low-severity issue on macOS where this codebase primarily runs, but worth addressing for correctness.

### WR-02: `parseInt` result not validated — `NaN` port passed to WebSocket constructor

**File:** `companion/src/main.ts:26`

**Issue:**
```typescript
const port = parseInt(params.get('wsPort') || '7170', 10);
```
If `wsPort` is present in the URL but not a valid integer (e.g., `?wsPort=abc`), `parseInt` returns `NaN`. `NaN` is then passed to `new WebSocket(`ws://127.0.0.1:${NaN}`)` which produces the URL `ws://127.0.0.1:NaN` — an invalid URL that will throw or silently fail depending on the browser, swallowing the error with no user-visible feedback. The fallback `|| '7170'` only applies when `params.get('wsPort')` returns `null`, not when it returns a non-numeric string.

**Fix:**
```typescript
const rawPort = parseInt(params.get('wsPort') || '7170', 10);
const port = Number.isFinite(rawPort) && rawPort > 0 && rawPort < 65536
  ? rawPort
  : 7170;
```

### WR-03: `broadcastBufferDelay` and `sendConfigToClient` can send `delayMs: 0` to clients

**File:** `juce/video/VideoCompanion.cpp:298-303` and `juce/video/VideoCompanion.cpp:371-381`

**Issue:** `sendConfigToClient` sends the cached delay if `cachedDelayMs_ > 0` (line 298), which correctly skips zero. However, `broadcastBufferDelay` computes a new delay from the current BPM/BPI, updates `cachedDelayMs_`, and broadcasts unconditionally — including when the computed value is zero. A BPM of, say, 0.001 (extremely slow) with BPI of 1 gives `delayMs = 60000`, which is fine, but with very large BPM values the integer truncation can produce zero: e.g., BPM=61000 (absurd but not guarded) with BPI=1 gives `(60.0/61000)*1*1000 = 0.98...` which truncates to 0. More practically: if `bpi=0` slips through (the `bpi <= 0` guard only checks `bpi <= 0`, not a floor value), the result is zero. VDO.Ninja receiving `setBufferDelay: 0` may disable chunked buffering entirely.

**Fix:** Add a lower-bound guard after computing the delay:

```cpp
int computed = static_cast<int>((60.0 / static_cast<double>(bpm)) * bpi * 1000.0);
if (computed <= 0) return;  // Guard against zero/negative delay before caching or broadcasting
cachedDelayMs_ = computed;
```

---

## Info

### IN-01: `escapeJsonString` does not escape newlines, tabs, or control characters

**File:** `juce/video/VideoCompanion.cpp:278-281`

**Issue:**
```cpp
static juce::String escapeJsonString(const juce::String& s)
{
    return s.replace("\\", "\\\\").replace("\"", "\\\"");
}
```
This escapes backslashes and double-quotes but not `\n`, `\r`, `\t`, or other ASCII control characters (0x00-0x1F). A username containing a newline would produce malformed JSON. NINJAM usernames are generally constrained, but the sanitization (`sanitizeUsername`) only keeps alphanumeric and `_` characters — so `rawName` (used for the `name` field in roster JSON at line 343) is the unsanitized original name. A username like `"Alice\nBob"` would break the JSON object.

**Fix:** Add control-character escaping:
```cpp
static juce::String escapeJsonString(const juce::String& s)
{
    juce::String r = s.replace("\\", "\\\\").replace("\"", "\\\"")
                      .replace("\n", "\\n").replace("\r", "\\r")
                      .replace("\t", "\\t");
    return r;
}
```
For full correctness, characters 0x00-0x1F should be `\uXXXX`-escaped, but the newline/CR/tab cases are the realistic attack surface.

### IN-02: `showWaitingForVideo` is defined but never called

**File:** `companion/src/ui.ts:321-325`

**Issue:** `showWaitingForVideo()` is exported and defined, but no code in `main.ts` or any test file calls it. This is dead code that was likely intended for an intermediate "VDO.Ninja loading" state but was never wired up.

**Fix:** Either wire it up after `loadVdoNinjaIframe` is called (before the iframe's `load` event fires) or remove it if the pre-connection state covers the use case.

### IN-03: `companion/index.html` loads `./src/main.ts` directly — only works with Vite dev server

**File:** `companion/index.html:47`

**Issue:**
```html
<script type="module" src="./src/main.ts"></script>
```
Browsers cannot execute `.ts` files natively. This works in development because Vite intercepts and transpiles on the fly, but the production build process (`tsc && vite build`) outputs to `dist/`. If the built `dist/index.html` does not transform this reference correctly, or if someone opens `index.html` directly without Vite, the page will fail silently (the module script will 404 or throw a MIME type error). This is a standard Vite pattern, but the `build` script runs `tsc` before `vite build` — `tsc` emits to `./dist` per `tsconfig.json outDir`, and then `vite build` also outputs to `dist`, potentially causing confusion about which files win. Vite typically handles the TypeScript entry point directly and ignores the `tsc` output, so the double-build is redundant and could produce stale `.js` files in `dist/src/` that conflict with Vite's output.

**Fix:** Remove the `tsc &&` prefix from the build script and let Vite handle TypeScript compilation entirely, which is its default behavior:
```json
"build": "vite build"
```
Keep `tsc --noEmit` as a separate type-check step if desired:
```json
"typecheck": "tsc --noEmit"
```

---

_Reviewed: 2026-04-07T21:20:37Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
