# Phase 19: Camera Capture & Permission UX - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in `19-CONTEXT.md` — this log preserves the alternatives considered.

**Date:** 2026-05-16
**Phase:** 19-camera-capture-permission-ux
**Areas discussed:** Capture API, Frame fanout, TCC detection, Color conversion, Local preview placement, Open trigger, Popout properties, Window style, Lifecycle, Persistence, Connect tie, Button state, Fallback UI, Re-show, Windows path, Standalone tip, Device selection, Capture parameters, Preset UI, Mid-loss handling, Test strategy, Privacy notice, Logging, State schema, Documentation, Coexist UX, Notarization, Audio tie, Onboarding

---

## Area 1: Capture API (JUCE seat licence path)

### Sub-question 1.1: Capture API choice

| Option | Description | Selected |
|--------|-------------|----------|
| `juce::CameraDevice` | Cross-platform single API, spike-validated, AGPLv3 compatible via GPLv2+ "or later" clause, ~50–100 LOC | ✓ |
| Direct AVFoundation + Media Foundation wrappers | ~800 LOC, two backends, no JUCE-module licence dependency | |
| Hybrid (JUCE now, swap later) | Locks JamWideCameraDevice abstraction in now; swap implementation post-beta if needed | |

**User's choice:** `juce::CameraDevice`
**Notes:** AGPL→GPL compatibility via the GPLv2+ "or later version" clause makes the licence question moot for JamWide's distribution.

### Sub-question 1.2: Frame fanout

| Option | Description | Selected |
|--------|-------------|----------|
| Frame distributor with subscribers | Single Listener forwards to JamWideFrameDistributor; UI + encoder + popouts subscribe | ✓ |
| Direct fan-out from Listener | Listener copies to UI + pushes to encoder queue inline; Phase 22 refactors | |
| Defer fanout to Phase 20 | Phase 19 only wires preview; Phase 20 figures out encoder tap | |

**User's choice:** Frame distributor with subscribers
**Notes:** Extensible architecture for Phase 22 popouts.

### Sub-question 1.3: TCC detection on macOS

| Option | Description | Selected |
|--------|-------------|----------|
| Pre-check via AVCaptureDevice | Objective-C++ bridge, queries authorizationStatusForMediaType before openDevice | ✓ |
| Watchdog timer | Open device, time out after 2000ms if no frame, infer denied | |
| Hybrid | Pre-check + watchdog safety net | |

**User's choice:** Pre-check via AVCaptureDevice
**Notes:** Closes spike Risk #2 ambiguity cleanly.

### Sub-question 1.4: Frame-format conversion location

| Option | Description | Selected |
|--------|-------------|----------|
| In each subscriber | Distributor publishes BGRA; encoder converts in Phase 20 | ✓ |
| Pre-convert in distributor | Publish YUV420P; UI converts back to RGB | |
| Distributor publishes both formats | Two channels; heaviest per-frame cost | |

**User's choice:** In each subscriber
**Notes:** Clean separation; encoder owns its format.

---

## Area 2: Local Preview Placement

### Sub-question 2.1: Where does the preview live?

| Option | Description | Selected |
|--------|-------------|----------|
| Video tile row above ChannelStripArea | Horizontal strip; Phase 22 fills with remote tiles | |
| Toggleable Video panel (ChatPanel-style) | Collapsible side panel | |
| Floating popout window only | `juce::DocumentWindow` only; no in-plugin tile | ✓ |
| Replace ConnectionBar Video button | Inline 64×48 mini-preview | |

**User's choice:** Floating popout window only
**Notes:** Minimal disruption to existing mixer + ConnectionBar layout. Consistent with the user's "existing UI stays untouched" preference for the parallel beta.

### Sub-question 2.2: How does the user open the popout?

| Option | Description | Selected |
|--------|-------------|----------|
| New Camera button in ConnectionBar | Next to existing Video button | ✓ |
| Auto-open on first Connect | No UI entry needed | |
| Settings dialog toggle | Hidden in menu | |
| Plugin extras menu | Context/tools submenu | |

**User's choice:** New Camera button in ConnectionBar
**Notes:** Side-by-side with existing Video button enables A/B comparison during parallel beta.

### Sub-question 2.3: Popout window properties

| Option | Description | Selected |
|--------|-------------|----------|
| Resizable, 4:3 aspect-locked, position persists | Default 320×240; resize maintains 4:3; APVTS persists | ✓ |
| Fixed 320×240, no resize, no position memory | Minimal implementation | |
| Resizable any aspect, position persists | Free stretch; risk of distorted pixels | |
| Snap to preset sizes | Three size presets | |

**User's choice:** Resizable, 4:3 aspect-locked, position persists

### Sub-question 2.4: Window chrome / LookAndFeel

| Option | Description | Selected |
|--------|-------------|----------|
| Match JamWide LookAndFeel | VB-style dark; custom TitleBarComponent | ✓ |
| System-native window chrome | Native title bar | |
| Borderless / chromeless | Like FaceTime; drag-by-image | |

**User's choice:** Match JamWide LookAndFeel

---

## Area 3: Camera Lifecycle

### Sub-question 3.1: Camera vs popout orthogonality

| Option | Description | Selected |
|--------|-------------|----------|
| Orthogonal: button = camera, X = popout visibility | Two independent states; Phase 20 adds broadcast as third | ✓ |
| Linked: button = both states; X = both | 1:1 binding; Phase 20 refactors | |
| Linked but button auto-opens popout | Mixed model | |

**User's choice:** Orthogonal — button = camera, X = popout visibility
**Notes:** ~10 LOC up-front investment avoids Phase 20 refactor.

### Sub-question 3.2: Camera state persistence across launches

| Option | Description | Selected |
|--------|-------------|----------|
| Always start OFF | Privacy default; matches Zoom/FaceTime/OBS | ✓ |
| Resume previous state (sticky) | UX-friendly; privacy risk | |
| Resume preview state, not camera | UI remembers; capture state doesn't | |

**User's choice:** Always start OFF

### Sub-question 3.3: Camera tied to NINJAM Connect/Disconnect?

| Option | Description | Selected |
|--------|-------------|----------|
| Fully independent | Camera state orthogonal to connection state | ✓ |
| Auto-stop camera on Disconnect | Privacy: webcam light off when session ends | |
| Auto-start camera on Connect | Lowest friction; surprise factor | |
| Offer to start on Connect | One-time modal with checkbox | |

**User's choice:** Fully independent

### Sub-question 3.4: Camera button state after permission denial

| Option | Description | Selected |
|--------|-------------|----------|
| 'Recheck permission' label, clickable | Re-queries TCC on click | ✓ |
| Disabled (greyed) with tooltip | Plugin reload to retry | |
| Stays 'Camera' label, click re-triggers fallback | Simple state machine | |

**User's choice:** 'Recheck permission' label, clickable

---

## Area 4: Permission-Denial Fallback (DAW-Agnostic)

### Sub-question 4.1: Fallback UI shape (after user clarification — DAW-agnostic, not REAPER-specific)

| Option | Description | Selected |
|--------|-------------|----------|
| Non-blocking dialog with cause-aware diagnostics | Single dialog component, all causes flow through it | ✓ |
| Status dot indicator only | Red dot + tooltip; no dialog | |
| Inline status in ConnectionBar | Text strip; always visible | |
| Dialog + inline indicator combo | Both | |

**User's choice:** Non-blocking dialog with cause-aware diagnostics
**Notes:** **User clarification:** original question was framed REAPER-on-macOS-specific. Reformulated to be DAW-agnostic — the same fallback shape handles REAPER, Live, Bitwig, user-denied-via-Settings, camera-in-use, no-hardware, Windows privacy block, etc. Cause-aware copy inside a uniform UI shape. Future-proof: if Live ships an update tomorrow with the entitlement, no JamWide code change needed.

### Sub-question 4.2: Dialog re-show behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Suppress after first show until cause changes | First failure educates; subsequent clicks focus button; cause-change re-shows | ✓ |
| Show every time | Annoying after first time | |
| Show once per session (any cause) | Risk: cause changes don't re-trigger | |
| Show with 'Don't show again' checkbox | Adds permanent suppression toggle | |

**User's choice:** Suppress after first show until cause changes

### Sub-question 4.3: Windows-specific path

| Option | Description | Selected |
|--------|-------------|----------|
| Same dialog, platform-aware copy + deep-link | One component, conditional content, ms-settings: deep-link | ✓ |
| Platform-specific dialogs | Separate implementations | |
| Windows skips dialog — status indicator only | Reserve dialog for macOS | |

**User's choice:** Same dialog, platform-aware copy + deep-link

### Sub-question 4.4: Standalone-as-fallback suggestion

| Option | Description | Selected |
|--------|-------------|----------|
| Copy-only suggestion | Educational text, no button | ✓ |
| Suggestion with 'Launch Standalone' button | Brittle cross-platform launch; new instance loses session | |
| No mention | Focus on current context only | |

**User's choice:** Copy-only suggestion

---

## Area 5: Camera Device Selection

| Option | Description | Selected |
|--------|-------------|----------|
| Auto-pick, name in popout titlebar | deviceIndex=0; titlebar shows "Camera: FaceTime HD" | ✓ |
| Dropdown selector in mini settings dialog | Phase 19 ships full selection UI; ~150 LOC | |
| First-run picker, then sticky | Selection dialog first time | |

**User's choice:** Auto-pick, name shown in popout titlebar

---

## Area 6: Capture Parameters

### Sub-question 6.1: Resolution / fps defaults

| Option | Description | Selected |
|--------|-------------|----------|
| Hardcode 320×240@10fps spike baseline | One fixed format | |
| Three presets — Low / Medium / High | 320×240@10fps, 640×480@15fps, 1280×720@30fps | ✓ |
| Hardcode in Phase 19, revisit in Phase 20 | Clean phase separation | |

**User's choice:** Three presets — Low / Medium / High
**Notes:** Default Medium (640×480@15fps). Phase 20 may revisit per-preset bitrate trade-offs.

### Sub-question 6.2: Preset selector UI placement

| Option | Description | Selected |
|--------|-------------|----------|
| Camera button right-click menu | juce::PopupMenu submenu | ✓ |
| Cog icon in popout titlebar | Settings cog opens menu | |
| Plugin-wide settings dialog entry | Deepest navigation | |

**User's choice:** Camera button right-click menu

---

## Area 7: Frame Error Handling

| Option | Description | Selected |
|--------|-------------|----------|
| Pause + exponential-backoff retry | 1s/2s/4s/8s/16s up to 30s; matches Phase 17 pattern | ✓ |
| Hard fail — reuse permission-denial dialog | Manual retry only | |
| Silent pause + status indicator only | Lowest UX intrusion | |
| Retry once, then dialog | Compromise | |

**User's choice:** Pause + exponential-backoff retry

---

## Area 8: Test Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Layered: unit tests for logic, manual UAT for permissions | Distributor / state machine / retry timing unit-tested; permissions manual | ✓ |
| Manual UAT only | Lowest test infrastructure | |
| Heavy mocking | Wrap AVCaptureDevice + Media Foundation in interfaces; ~200 LOC | |
| Defer test layer to Phase 22 or 24 | Phase 19 manual-only | |

**User's choice:** Layered

---

## Area 9: Privacy Notice

| Option | Description | Selected |
|--------|-------------|----------|
| New first-use modal with 'Don't show again' | Distinct from VDO.Ninja VideoPrivacyDialog | ✓ |
| Inline first-use notice inside popout | Notice strip, fades after 5s | |
| No notice — same trust model as audio | Lowest friction | |
| Reuse VideoPrivacyDialog with edited copy | Awkward — VDO.Ninja-specific concern (IP exposure) doesn't apply | |

**User's choice:** New first-use modal with 'Don't show again'

---

## Area 10: Camera-Event Logging

| Option | Description | Selected |
|--------|-------------|----------|
| `juce::Logger::writeToLog` | Standard plugin log; same pattern as OSC/MIDI | ✓ |
| Existing wdl::writeLog | NJClient core convention | |
| Dedicated camera-events log file | Easier grep for beta debugging | |
| stderr in dev builds, juce::Logger in release | Pattern Phase 20+ may reuse | |

**User's choice:** `juce::Logger::writeToLog`

---

## Area 11: Plugin State Persistence (APVTS Schema)

| Option | Description | Selected |
|--------|-------------|----------|
| Bump state version 3 → 4, new `<camera>` ValueTree subtree | Clean upgrade path; existing schema-bump pattern | ✓ |
| Flat keys in existing state, no version bump | Simpler; less organized | |
| Defer all camera state to Phase 22 | Beta testers lose state every reload | |

**User's choice:** Bump to state version 4, new `<camera>` subtree

---

## Area 12: Documentation

| Option | Description | Selected |
|--------|-------------|----------|
| Tooltip + CHANGELOG entry; full docs in Phase 24 | Phase 19 focused on capture; user docs ship in user-facing phase | ✓ |
| Full bundle now (tooltip + CHANGELOG + docs/CAMERA.md draft + README) | Docs evolve with code | |
| Tooltip only, no CHANGELOG | Risk: changelog forgotten | |
| Draft docs/CAMERA.md now; finalize in Phase 24 | Skeleton commits now | |

**User's choice:** Tooltip + CHANGELOG entry; full docs in Phase 24

---

## Area 13: VDO.Ninja Coexistence

| Option | Description | Selected |
|--------|-------------|----------|
| Soft warning toast, no block | Lets beta testers A/B compare | ✓ |
| Hard block: Camera button disabled while VDO.Ninja active | Prevents A/B comparison | |
| No warning, let users run both | Confusing beta reports risk | |
| Dual-status indicator only | Lightest UX | |

**User's choice:** Soft warning toast, no block

---

## Area 14: macOS Notarization Impact

| Option | Description | Selected |
|--------|-------------|----------|
| Verify in Phase 19 | Build + codesign + notarize a test bundle; surface issues to Phase 23 | ✓ |
| Assume no impact; verify in Phase 23 | Risk: late issue partially invalidates Phase 19 UAT | |
| Add NSCameraUsageDescription + verify | Includes Info.plist string + validation | |

**User's choice:** Verify in Phase 19
**Notes:** Phase 19 explicitly adds NSCameraUsageDescription as part of the verification task (otherwise the OS won't show the permission prompt at all).

---

## Area 15: Audio-Thread Coordination Contract

| Option | Description | Selected |
|--------|-------------|----------|
| Phase 20 owns audio-thread integration | Phase 19 ships ZERO audio-path code | ✓ |
| Phase 19 lays atomic flag groundwork | Slight forward-investment | |
| Phase 19 ships full Phase 20 hooks (no-op encoder) | Most ambitious; most rework risk | |

**User's choice:** Phase 20 owns audio-thread integration

---

## Area 16: Onboarding Tour

| Option | Description | Selected |
|--------|-------------|----------|
| No tour — tooltip + CHANGELOG sufficient | Consistent with rest of JamWide UX | ✓ |
| First-launch coachmark near Camera button | Sets onboarding precedent | |
| Inline status text 'NEW: Camera (beta)' badge | Less intrusive than popover | |
| Splash screen on first launch | Heavy UX | |

**User's choice:** No tour — tooltip + CHANGELOG sufficient

---

## Claude's Discretion

Areas where the user deferred to Claude / planner:
- Sync vs async `juce::CameraDevice::openDeviceAsync`
- Exact pixel dimensions for popout min/default/max bounds
- Specific copy strings for the fallback dialog beyond the cause-aware skeleton
- Retry-thread implementation (separate `juce::Thread` vs `juce::TimedCallback` chain)
- File layout for camera code (likely `juce/video/native/` subdirectory)
- Whether to expose a `GetCameraPeakFrameRate()` accessor for beta debugging

## Deferred Ideas

(Captured in CONTEXT.md `<deferred>` section)

- Multi-camera dropdown UI (post-beta, quick task or Phase 22)
- VST3/AU/CLAP host-specific camera-entitlement DAW matrix (Phase 24 BETA-02/03)
- Onboarding tour / coachmark (revisit if beta testers report discoverability issues)
- Camera frame-rate / bandwidth telemetry (Phase 24)
- Privacy notice translations (v2+ i18n)
- `docs/CAMERA.md` user guide (Phase 24)
- Audio-thread `camera_active` flag (Phase 20)
- Per-preset bitrate trade-offs (Phase 20)
- Cross-platform launch-Standalone button (rejected)
- Linux camera capture (Item K, out of v1.3)
