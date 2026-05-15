---
quick_id: 260515-0pc
slug: investigate-jamtaba-video-implementation
status: ready-for-research
gathered: 2026-05-15
---

# Quick Task 260515-0pc: JamTaba-style Native Video for JamWide — Context

<domain>
## Task Boundary

Investigate JamTaba's native video implementation (ffmpeg-based, integrated into application AND CLAP plugin), then design and implement an equivalent for JamWide that works in both the standalone app and the VST3/AU/CLAP plugin running inside a DAW.

**Out of scope (until a follow-up phase):** server-side relay optimisation, mobile clients, screen-share, multi-camera support, recording.
</domain>

<decisions>
## Implementation Decisions (LOCKED — do not revisit during planning)

### Coexistence strategy
- **Replace VDO.Ninja entirely.** Tear out `juce/video/VideoCompanion.{h,cpp}`, `juce/video/VideoPrivacyDialog.{h,cpp}`, the `companion/` directory, and the WebSocket server. Single, unified in-app/in-plugin video stack.
- Trade-off accepted: throws away ~4 phases of work (Phases 11, 12, 13, 14.2) and loses VDO.Ninja's mobile/browser reach. Justification: feature parity with JamTaba and a single product surface.

### Capture/codec stack
- **ffmpeg + JUCE CameraDevice.** Use `juce::CameraDevice` (juce_video module, already vendored at `libs/juce/modules/juce_video/`) for cross-platform capture. Use ffmpeg libavcodec/libavformat/libswscale for encoding/decoding/colour conversion.
- Closest to JamTaba's pipeline; reuses JUCE patterns already in the codebase; avoids platform-specific capture glue.

### Plugin context
- **Both standalone AND plugin parity.** Webcam capture must work in VST3/AU/CLAP hosted by a DAW.
- Implies: macOS Hardened Runtime entitlement `com.apple.security.device.camera`, codesigning of all ffmpeg dylibs in the bundle, DAW-host permission UX (first-run prompt, "video unavailable" fallback when host denies camera).

### Wire format
- **Bit-for-bit JamTaba-compatible.** Same channel-naming convention, same codec params, same interval framing.
- Goal: JamTaba and JamWide users see each other's video in mixed sessions on vanilla NINJAM servers, no protocol extension required.

### Claude's Discretion
- Bundle/codesigning specifics, logging, telemetry, UI placement of the new video widget, frame-rate/resolution defaults — choose pragmatically.

</decisions>

<specifics>
## Specific References

### JamTaba video module (local clone at `/Users/cell/dev/JamTaba`)
- `src/Common/video/VideoFrameGrabber.{h,cpp}` (70 + 64 lines) — Qt QCamera frame extraction
- `src/Common/video/FFMpegMuxer.{h,cpp}` (644 + 136 lines) — encoder (theora/webm) chunked into NINJAM intervals via `dataEncoded(QByteArray, isFirstPart)` signal
- `src/Common/video/FFMpegDemuxer.{h,cpp}` (252 + 44 lines) — receives interval bytes, decodes frames
- `src/Common/video/VideoWidget.{h,cpp}` (162 + 58 lines) — Qt widget that renders decoded frames
- `src/Common/video/FFMpegCommon.h` (49 lines) — shared codec config

### JamTaba wiring (`src/Common/MainController.cpp`)
- `MAX_VIDEO_SIZE = QSize(320, 240)` — hard cap at QVGA
- `videoEncoder` is owned by MainController (shared between Standalone and CLAP)
- `setVideoProperties(resolution)` clamps to nearest supported by encoder
- `videoEncoder.startNewInterval()` called per NINJAM interval boundary
- `videoEncoder.encodeImage(frame)` per captured frame; encoder emits `dataEncoded` chunks
- `enqueueVideoDataToUpload(encodedData, isFirstPart)` packages into `UploadIntervalData` then sends via `ninjamService->sendIntervalPart(GUID, bytes, isLast)` — same NINJAM mechanism as audio

### JamTaba plugin proof
- `PROJECTS/ClapPlugin/VideoFrameGrabber.o`, `FFMpegMuxer.o`, `FFMpegDemuxer.o`, `VideoWidget.o` all present
- Confirms ffmpeg-based capture/encode/decode works in CLAP plugin context (at least on the platforms JamTaba targets)

### JamWide existing video stack (TO BE REMOVED)
- `juce/video/VideoCompanion.{h,cpp}` — derives room ID, runs WebSocket server
- `juce/video/VideoPrivacyDialog.{h,cpp}` — first-run privacy notice
- `juce/JamWideJuceEditor.cpp` — video-button handler launches VDO.Ninja URL
- `companion/` directory — TypeScript/HTML browser companion that consumes the WS feed
- `tests/test_video_sync.cpp`, `companion/e2e/video-sync.spec.ts` — existing video-sync tests

### JamWide NINJAM client integration points (where the video channel must hook in)
- `src/core/` and similar — to be discovered during research; the executor and planner must locate the equivalent of JamTaba's `ninjamService->sendIntervalPart` in JamWide.

</specifics>

<canonical_refs>
## Canonical References

- JamTaba upstream: https://github.com/elieserdejesus/JamTaba (local clone `/Users/cell/dev/JamTaba`)
- NINJAM protocol: vanilla NinJam server is wire-format-agnostic about interval payload contents
- JUCE camera capture: `juce::CameraDevice` in `libs/juce/modules/juce_video/`
- ffmpeg licensing note: LGPL-only build is required for plugin distribution; no GPL components (e.g. x264 default, libfdk_aac) without license-cleared use
- Apple Hardened Runtime camera entitlement: `com.apple.security.device.camera` (must be added to plugin entitlements plist + signed with `--options runtime`)

</canonical_refs>

<scope_warning>
## Scope Warning for Planner

This is structurally a multi-phase milestone, not a 1–3-task quick. A complete delivery is realistically:

- A. Add ffmpeg LGPL build, plugin entitlements, codesigning updates
- B. Capture module (JUCE CameraDevice + frame format conversion)
- C. Encoder + interval-frame chunker (port `FFMpegMuxer.cpp`)
- D. Decoder + display widget (port `FFMpegDemuxer.cpp` + `VideoWidget.cpp`)
- E. NINJAM channel wiring (broadcast + receive)
- F. Remove VDO.Ninja stack + migrate UI button
- G. Per-DAW plugin webcam permission testing (Logic, Reaper, Live, Bitwig)

**Recommendation to the planner:** Scope this quick task to a *feasibility spike + first vertical slice* (e.g., capture-encode-self-display loop, no network yet, standalone only) and explicitly defer the full milestone to `/gsd-new-milestone`. Document the deferred work in `260515-0pc-deferred-items.md`.

</scope_warning>
