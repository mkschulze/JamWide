# Plan: REAPER macOS spacebar/input fix (mac-only, no ImGui fork)

## Goal
Stop REAPER from triggering transport shortcuts (space/arrow/etc) while ImGui is capturing keyboard input, without touching Windows code or maintaining a fork of ImGui.

## Constraints
- macOS only.
- No changes to Windows GUI or platform code paths.
- Do not modify ImGui submodule files.
- Preserve text input/IME behavior (space should still be inserted in text fields).

## Proposed Approach (REAPER-specific)
Install a **REAPER-only local event monitor** from our own macOS view (`gui_macos.mm`) to consume keyboard events when ImGui wants them, while manually forwarding those events to ImGui’s `NSTextInputClient` subview so text input still works.

Key points:
- Detect REAPER by exact bundle ID `com.cockos.reaper`.
- Only act on key events from our view’s window.
- Only consume when ImGui wants keyboard capture.
- Forward to ImGui’s `KeyEventResponder` (hidden `NSTextInputClient` subview) before returning `nil`.

## Implementation Outline (no code yet)
1. **Detect REAPER host**
   - Read `[NSBundle mainBundle].bundleIdentifier` once in `NinjamView` init and cache a `bool is_reaper_host`.
   - Match exactly `com.cockos.reaper`.

2. **Install a local monitor in `NinjamView`**
   - File: `JamWide/src/platform/gui_macos.mm`.
   - Add an `id event_monitor_` to `NinjamView` and remove it on dealloc or when view loses its window.
   - Add monitor for `NSEventMaskKeyDown | NSEventMaskKeyUp` (optionally `FlagsChanged`, but likely leave it unconsumed).

3. **Monitor handler behavior**
   - Ignore events if `event.window != self.window` or if the view/window is not active/visible.
   - Set ImGui current context from `self.imguiContext` and check `io.WantCaptureKeyboard`.
   - If **not** REAPER or ImGui doesn’t want capture: return event unchanged.
   - If REAPER + ImGui wants capture:
     - Find the `NSTextInputClient` subview (`findTextInputView`).
     - Call `[input_view keyDown:event]` / `[input_view keyUp:event]` to feed ImGui and trigger `interpretKeyEvents:`.
     - Return `nil` to consume so REAPER doesn’t see it.
   - If no input view is found, do **not** consume (return event).

4. **Safety checks**
   - Ensure the monitor does not run when the view is removed or hidden.
   - Avoid stealing keys from other REAPER windows/plugins by checking `event.window` and `isKeyWindow`.

## Validation Checklist
- **REAPER macOS**
  - Spacebar inserts space in ImGui text fields (no transport toggle).
  - Arrow/tab/enter do not trigger REAPER shortcuts when ImGui wants keyboard.
  - Transport shortcuts still work when focus is outside ImGui input.

- **Other hosts (macOS)**
  - Bitwig/GarageBand behavior unchanged.
  - Text input/IME still works.

## Notes / Risks
- Returning `nil` from a local monitor bypasses default responder dispatch; forwarding to ImGui’s `NSTextInputClient` is required to keep text input working.
- ImGui already installs its own local monitor; our monitor must be careful to only consume on REAPER and only when ImGui wants capture.
- **Renamed `NinjamView` to `JamWideView`** while implementing this fix (cleanup from project rename).

## Implementation Status
**✅ IMPLEMENTED** in build r121 (2026-01-13)

Changes made to `src/platform/gui_macos.mm`:
- Renamed `NinjamView` → `JamWideView`
- Added `is_reaper_host_` detection via `[[NSBundle mainBundle] bundleIdentifier]`
- Added `event_monitor_` ivar for local keyboard event monitor
- Installed REAPER-only local monitor in `viewDidMoveToWindow` that:
  - Checks `io.WantCaptureKeyboard` before consuming
  - Forwards events to ImGui's `NSTextInputClient` subview
  - Returns `nil` to consume events, preventing REAPER transport shortcuts
- Monitor is properly removed in `dealloc` and when view loses its window

## Session Notes (2026-01-13)
- Discovered bug: spacebar triggers REAPER transport instead of typing space in text fields
- Spacebar **works** in GarageBand AU - confirms REAPER intercepts at a higher level
- Caps Lock text input works in REAPER - confirms ImGui receives some events
- Windows fix (r119) uses `WH_GETMESSAGE` hook + dummy EDIT control
- macOS equivalent is `NSEvent addLocalMonitorForEventsMatchingMask:`
- Current build: r120 (verified macOS build after Windows fix pull)

## Extended Testing (2026-01-13 continued)
After implementing local monitor + sendEvent swizzle approaches:

**Findings:**
- Local event monitor works for regular keys (a-z, numbers) but NOT spacebar
- `performKeyEquivalent:` only fires for Cmd+key combinations, not bare spacebar
- sendEvent swizzle shows spacebar **never reaches sendEvent** at all
- Diagnostic logs confirm: REAPER intercepts spacebar via **CGEventTap** or similar system-level hook **before** `[NSApplication sendEvent:]` is called
- This is why all Cocoa-level interception methods fail for spacebar specifically

**Conclusion:** Spacebar is intercepted at a level below Cocoa (CGEventTap/IOKit). Plugin-level interception is not possible without:
1. Installing our own CGEventTap (too invasive, security issues, not appropriate for a plugin)
2. Using REAPER's own extension API to signal text input focus

## Research: REAPER Extension API for Keyboard Focus (2026-01-13)

**DISCOVERED:** REAPER provides a `hwnd_info` callback registration that allows plugins to signal keyboard focus state!

From `reaper_plugin.h`:
```cpp
hwnd_info:  (6.29+)
  query information about a hwnd
    int (*callback)(HWND hwnd, INT_PTR info_type);
   -- note, for v7.23+ you may also use:
    int (*callback)(HWND hwnd, INT_PTR info_type, const MSG *msg);

  return 0 if hwnd is not a known window, or if info_type is unknown

  info_type:
     0 = query if hwnd should be treated as a text-field for purposes of global hotkeys
         return: 1 if text field
         return: -1 if not text field
     1 = query if global hotkeys should be processed for this context  (6.60+)
         return 1 if global hotkey should be skipped (and default accelerator processing used instead)
         return -1 if global hotkey should be forced
```

**This is the proper solution!**

### New Approach: `hwnd_info` Registration

1. **Obtain REAPER API function pointers** via CLAP host extension or AU/VST3 equivalent
2. **Register a `hwnd_info` callback** that:
   - Checks if the queried HWND is our plugin window
   - Returns 1 for `info_type=0` when `io.WantTextInput` or `io.WantCaptureKeyboard` is true
   - Returns 1 for `info_type=1` to skip global hotkeys when we want keyboard
3. **REAPER will then skip transport shortcuts** for our window when the callback indicates text input

### Implementation Challenges

- **CLAP:** Can use `clap_host.get_extension(host, "cockos.reaper_extension")` to get `reaper_plugin_info_t*`
- **VST3:** May need to query REAPER-specific extension interface
- **AU v2:** Less clear how to access REAPER API (may not be available)

The CLAP path is the most promising since JamWide is CLAP-first and we already have the host pointer.

### Next Steps
1. Check if clap-wrapper or our CLAP code has access to the CLAP host extension mechanism
2. Query for `cockos.reaper_extension` to get `reaper_plugin_info_t*`
3. Register `hwnd_info` callback that returns 1 when ImGui wants text input
4. Test if this prevents spacebar transport trigger

## Implementation Details (2026-01-13)

### Files to Modify

1. **`src/plugin/jamwide_plugin.h`**
   - Add `void* reaper_info` field to store `reaper_plugin_info_t*`
   
2. **`src/plugin/clap_entry.cpp`**
   - In `factory_create_plugin`: Query `host->get_extension(host, "cockos.reaper_extension")`
   - Store result in plugin->reaper_info
   - If available, register `hwnd_info` callback via `rec->Register("hwnd_info", callback)`

3. **`src/platform/gui_macos.mm`**
   - Store plugin's NSView pointer in a global map when GUI is created
   - When `hwnd_info` callback is called with our view's HWND:
     - Check `io.WantTextInput` or `io.WantCaptureKeyboard`
     - Return 1 (text field) if true
     - Return 0 otherwise

### hwnd_info Callback Signature
```cpp
// For REAPER 6.29+
int jamwide_hwnd_info_callback(HWND hwnd, INT_PTR info_type);

// For REAPER 7.23+, optional extended version with message context
int jamwide_hwnd_info_callback(HWND hwnd, INT_PTR info_type, const MSG *msg);
```

### Callback Behavior
- `info_type == 0`: Return 1 if hwnd is our view AND `io.WantTextInput == true`, else 0
- `info_type == 1`: Return 1 to skip global hotkeys when we want keyboard, else 0
- Return 0 for unknown hwnd or unknown info_type

### Key Insight
On macOS with SWELL, `HWND` is typically an `NSView*`. So we can compare the hwnd parameter directly against our JamWideView pointer.

### Registration Flow
1. `factory_create_plugin()` → Query REAPER extension
2. `gui_create()` → Store NSView* in global active views set
3. REAPER calls `hwnd_info_callback()` → Check if hwnd is our view, return appropriate value
4. `gui_destroy()` → Remove from active views set
5. Plugin destroy → Unregister callback with `Register("-hwnd_info", callback)`

## Implementation Complete (r127)

### Files Changed

1. **`src/platform/reaper_integration.h`** (NEW)
   - Header for REAPER extension API integration
   - Declares `reaper_integration_init()`, `reaper_integration_shutdown()`
   - Declares `reaper_register_view()`, `reaper_unregister_view()`, `reaper_is_active()`

2. **`src/platform/reaper_integration.mm`** (NEW)
   - Implementation of REAPER hwnd_info callback registration
   - Queries `host->get_extension(host, "cockos.reaper_extension")` to get REAPER API
   - Registers callback via `rec->Register("hwnd_info", hwnd_info_callback)`
   - Callback checks if queried HWND is our view and returns 1 if `io.WantTextInput` is true

3. **`src/plugin/clap_entry.cpp`**
   - Added `#include "platform/reaper_integration.h"` (macOS only)
   - Call `reaper_integration_init(host)` in `factory_create_plugin()`
   - Call `reaper_integration_shutdown()` in `jamwide_entry_deinit()`

4. **`src/platform/gui_macos.mm`**
   - Added `#include "platform/reaper_integration.h"`
   - Added `jamwide_view_wants_keyboard()` callback function
   - Call `reaper_register_view()` when view attaches to window
   - Call `reaper_unregister_view()` when view detaches/deallocates

5. **`CMakeLists.txt`**
   - Added `src/platform/reaper_integration.mm` to macOS sources

### How It Works

1. When the CLAP plugin is loaded in REAPER, `factory_create_plugin()` queries for `cockos.reaper_extension`
2. If found (we're in REAPER), we register our `hwnd_info_callback` with REAPER
3. When our GUI view attaches to a window, we register it with our integration module
4. REAPER calls our `hwnd_info_callback` before processing global hotkeys:
   - `info_type=0`: "Is this a text field?" → Return 1 if ImGui wants text input
   - `info_type=1`: "Should global hotkeys be skipped?" → Return 1 if ImGui wants keyboard
5. If we return 1, REAPER skips transport shortcuts like spacebar for our window

### Testing Results (r127)

**SOLUTION FOUND: REAPER's "Send all keyboard input to plug-in" option**

When enabled (FX menu → "Send all keyboard input to plug-in"):
- ✅ Spacebar works in text fields
- ✅ All keys work correctly
- REAPER bypasses its CGEventTap for this plugin

**hwnd_info API - Not needed:**
- Callback registered successfully but REAPER doesn't call it for plugin views
- The "Send all keyboard input to plug-in" option is the intended solution

**sendEvent swizzle - Works for keys that reach NSApplication:**
- Letters successfully intercepted and forwarded to ImGui
- With REAPER's keyboard input option enabled, spacebar also works

## Final Solution

**User must enable:** FX menu → "Send all keyboard input to plug-in"

This is REAPER's official mechanism for plugins that need keyboard input.

### Implemented (r131)
- ✅ REAPER host detection via bundle ID (`com.cockos.reaper`)
- ✅ Dismissible UI hint shown when running in REAPER on macOS
- ✅ Removed experimental swizzle/monitor code (simplified codebase)
- ✅ Chat input focus retained after sending messages

## Status: COMPLETE

The solution relies on REAPER's built-in "Send all keyboard input to plug-in" option.
A helpful hint is shown to users when running in REAPER on macOS.
