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

### Wire format [REVISED-2026-05-15]
- **Bit-for-bit NinjamZap-compatible** (was JamTaba; user redirected 2026-05-15).
- fourCC `H264` (= `MAKE_NJ_FOURCC('H','2','6','4')` = 0x34363248), 24-byte interval marker `[4B BE prefix=20][4B BE swap_count][16B audio_ch0_guid]`, 4-byte BE length prefix per frame, 4-stage receive pipeline (`accumulating → next → pending → playing`) with GUID-pairing decision tree (DS / PREV / no-match → `kHoldCapDrop=4`).
- Goal: JamWide users see each other's video on the ninjamzap-server fork (or a compatible server). JamTaba interop is sacrificed (different fourCC); future interop with NinjamZap mobile (iOS / Android) is GAINED.
- Authoritative spec: see `260515-0pc-RESEARCH-ADDENDUM.md` "Wire format spec" section.

### Server compatibility [NEW-2026-05-15]
- **Target the ninjamzap-server fork as the reference server.** Vanilla NINJAM works (server is opaque relay) but lacks per-room threading, two-pass audio-priority processing, and per-subscriber video-frame congestion drop. Without those, audio quality degrades when video bandwidth spikes.
- JamWide hosts/users running their own server should run ninjamzap-server. Document this in the milestone-shipping README.

### UI rendering model [NEW-2026-05-15]
- **Native rendering only — popout `juce::DocumentWindow` per user + grid `juce::Component` in the main view.** No browser companion in v1.
- Decoded H.264 frames live as `juce::Image` objects owned by JamWide; can be rendered in any combination of: a native grid (one tile per remote user inside the plugin/standalone window), per-user popouts that can be dragged to other monitors, or both simultaneously.
- The existing VDO.Ninja browser companion's two strengths (multi-monitor grid + per-user windows) are preserved in the native UI without the WebSocket+HTML/TS dependency surface.
- A literal browser-companion path is rejected for v1 — would require parallel transport (decode → MJPEG/WebRTC re-encode → local WebSocket → browser) that roughly doubles milestone work. Can be re-evaluated in a future milestone if user demand emerges.

### Audio codec [CLARIFIED-2026-05-15]
- **Audio codec is OUT OF SCOPE for the native-video work.** JamWide today supports OGGv (Vorbis-in-Ogg) and FLAC. NinjamZap-server and ninjamzap-core support only OGGv on the wire — no OPUS support exists in either repo (verified via grep — only WDL/metadata.h:688 mentions opus, and that is only an audio-file-extension recognizer for ID3-style metadata reads, not a NINJAM transport codec).
- JamWide's own roadmap has Opus tracked separately as **Phase 16 "Opus Codec Integration"** in the v1.2 "Security & Quality" milestone. Treat Opus integration as an independent decision belonging to that phase, NOT to the native-video work.
- Implication: stay on OGGv for audio in the video milestone; do not entangle Opus.

### Cross-platform support [NEW-2026-05-15]
- **macOS + Windows: full native send + receive parity from v1.** Both have working JUCE `CameraDevice` backends (`juce_CameraDevice_mac.h`, `juce_CameraDevice_windows.h`) and ffmpeg/openh264 builds.
- **Linux: receive-only in v1.** JUCE's `juce_video` module has no `juce_CameraDevice_linux.h` — Linux users can decode and view incoming video but cannot capture+broadcast their own. UX must show "Video send unavailable on Linux (planned for a later release)" instead of failing silently.
- **Linux send: deferred to its own phase post-v1.** Implementing direct V4L2 (`/dev/video0`) capture for Linux is single-platform work that doesn't block macOS/Windows shipping. Tracked as Item K in `260515-0pc-deferred-items.md`.

### Claude's Discretion
- Bundle/codesigning specifics, logging, telemetry, frame-rate/resolution defaults beyond the JamTaba-defined codec params, exact placement of the per-user popout-windows menu, default grid layout (e.g., 2×2 for 4 users vs flow-layout) — choose pragmatically.

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
