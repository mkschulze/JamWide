---
quick_id: 260515-0pc
slug: investigate-jamtaba-video-implementation
status: research-complete
date: 2026-05-15
---

# Quick Task 260515-0pc: JamTaba-style Native Video for JamWide — Research

## Summary

JamTaba's video pipeline is small (~1100 LOC of `src/Common/video/`), Qt+ffmpeg-based, and ships into both VST and CLAP plugins via a plain VPATH include — no `#ifdef` gating. Wire format is dead simple: an extra NINJAM upload channel (chidx=1) tagged with the fourCC `JTBv` carries an H.264 bytestream chunked at 4 KB into normal NINJAM interval parts. The receiving client picks `JTBv` vs `OGGv` from the `UploadIntervalBegin` fourCC field.

For JamWide, the locked decisions are sound: JUCE has a working `juce::CameraDevice` with first-class `juce_add_plugin` plumbing for both `NSCameraUsageDescription` (Info.plist) and the `com.apple.security.device.camera` hardened-runtime entitlement. The two real risks are (a) **ffmpeg LGPL-vs-GPL discipline** — JamTaba links `-lx264` (GPL); JamWide must use libavcodec internal H.264 decoder + Cisco openh264 encoder to stay LGPL — and (b) **DAW camera permission**: REAPER (and likely most DAWs except Logic Pro) does not request `com.apple.security.device.camera` for itself, so plugin-context camera access **will crash unless the plugin gracefully handles permission denial**.

A discriminator gap also exists in JamWide's NINJAM receive path: today (`njclient.cpp:2148`), any non-zero fourcc that isn't FLAC falls through to `CreateNJDecoder()` (Vorbis), so JamWide running unchanged against a JamTaba-emitting peer would feed H.264 bytes into Vorbis and silently produce garbage, not crash. The receive-side change must intercept `JTBv` BEFORE `start_decode`.

This task is correctly scoped as a multi-phase milestone. The most useful single quick task is option (a) below — a feasibility spike of capture+encode+decode+display in the standalone, no network, no removal — which proves ffmpeg/CameraDevice/openh264 work end-to-end on this machine before committing to phased delivery.

**Primary recommendation:** Scope this quick task to a feasibility spike (option a in §8). Ship the rest as a milestone via `/gsd-new-milestone`.

---

## 1. JamTaba video pipeline — exact specs

### Codec parameters

All values taken from `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:237-277` and `/Users/cell/dev/JamTaba/src/Common/MainController.cpp:35-36,112-120`:

| Parameter                | Value                                          | Source                            |
|--------------------------|------------------------------------------------|-----------------------------------|
| Codec ID                 | `AV_CODEC_ID_H264`                             | FFMpegMuxer.cpp:172               |
| Encoder backend          | `-lx264` (libx264, GPL)                        | VstPlugin.pro:75; ClapPlugin.pro:110 |
| H.264 preset             | `"veryfast"` (av_dict_set "preset")            | FFMpegMuxer.cpp:269               |
| Pixel format             | `AV_PIX_FMT_YUV420P`                           | FFMpegMuxer.cpp:266               |
| Resolution (default cap) | 320×240 QVGA, multiple-of-2 enforced           | MainController.cpp:36; FFMpegMuxer.cpp:89,256 |
| Frame rate               | 10 fps (`CAMERA_FPS`)                          | MainController.cpp:35,119         |
| Bitrate (default)        | 96 kbps (`VideoQualityMedium`)                 | FFMpegMuxer.cpp:39,91             |
| Bitrate options          | 64 / 96 / 128 / 400 kbps                       | FFMpegMuxer.cpp:36-41             |
| `rc_max_rate`            | = `bit_rate` (CBR-ish)                         | FFMpegMuxer.cpp:254               |
| `rc_buffer_size`         | = `bit_rate`                                   | FFMpegMuxer.cpp:255               |
| GOP size                 | 30 frames (= 3s @ 10 fps)                      | FFMpegMuxer.cpp:265               |
| Container                | NONE — raw H.264 NAL bytestream               | FFMpegMuxer.cpp:586-643 (no AVFormat write); decoder uses custom AVIO read in FFMpegDemuxer.cpp:75-143 |
| Time base                | `{1, frameRate}` (1/10s ticks)                 | FFMpegMuxer.cpp:263               |
| RGB→YUV path             | `RGBtoYUV420P` macro then `sws_scale` if needed| FFMpegMuxer.cpp:500-580           |

### Frame chunking strategy (encode → NINJAM intervals)

The encoder runs in a `QThreadPool(1)` worker (FFMpegMuxer.cpp:99). One captured frame may emit zero or one packet (`avcodec_receive_packet` returns `EAGAIN` until the codec has buffered enough). Each packet emits a `dataEncoded(QByteArray, isFirstPacket)` signal (FFMpegMuxer.cpp:632), and `MainController::enqueueVideoDataToUpload` accumulates these and flushes:

- On `isFirstPacket = true` (first packet of a new interval): flush previous interval's tail with `isLast=true`, then `sendIntervalBegin` with a fresh GUID and chidx=1 (MainController.cpp:373-383).
- Append packet bytes to the in-flight buffer (line 389).
- When buffer >= **4096 bytes**, flush as a non-final part (lines 391-395). This keeps individual NINJAM `UploadIntervalWrite` messages bounded.
- On the next interval boundary, a `sendIntervalPart` with the residual buffer + `isLast=true` closes the previous video interval; `startNewInterval` triggers the encoder to flush its buffered frames (FFMpegMuxer.cpp:160-185 calls `prepareToEncodeNewInterval` which calls `finishCurrentInterval` which drains the codec).

**Implication for JamWide:** the upload-side state machine is two layers — (1) a frame-rate-paced encoder that emits raw H.264 packets, (2) a 4 KB chunker that converts encoder output into NINJAM `UploadIntervalWrite` payloads keyed by a per-interval GUID + chidx=1.

### Channel-naming convention (the wire-format discriminator)

This is **the most important finding**. There is no separate "video channel name" string. Discrimination happens via the **fourCC field of the `UploadIntervalBegin` message**:

- `'O','G','G','v'` = audio interval
- `'J','T','B','v'` = video interval (literal "JTBv")

Source: `/Users/cell/dev/JamTaba/src/Common/ninjam/client/ClientMessages.cpp:453-472`. The receive side checks the fourCC bytes verbatim: `/Users/cell/dev/JamTaba/src/Common/ninjam/client/ServerMessages.cpp:560-571`.

The fourCC is a 4-byte fixed field that already exists in the NINJAM `UploadIntervalBegin` message — JamTaba reuses it as a codec/payload-type tag. NINJAM servers don't inspect or validate it; they just relay. JamWide already has identical infrastructure (`mpb_client_upload_interval_begin.fourcc` at `src/core/mpb.h:240`) and uses `MAKE_NJ_FOURCC('O','G','G','v')` for Vorbis (`src/core/njclient.cpp:154`) and `'F','L','A','C'` for FLAC. Adding `MAKE_NJ_FOURCC('J','T','B','v')` is one line.

The chidx=1 ("video uses the 2nd channel of the user") is a JamTaba convention to avoid colliding with the audio channel that's typically published as chidx=0; it is NOT part of the wire-format protocol — the fourCC is what differentiates audio from video. JamTaba's comment confirms it's a soft convention (MainController.cpp:382: `"always sending video in 2nd channel to avoid drop intervals in first channel"`).

### Audio-video sync mechanism

There is no sync. JamTaba relies on **NINJAM's existing interval-aligned delivery**: video frames captured during interval N are sent via the chidx=1 channel of interval N, and arrive at peers aligned to interval N+1's playback (same as audio). Within an interval, video frames have whatever inter-frame timing the encoder produced (1/10s @ 10 fps). The decoder side (`FFMpegDemuxer::decode()`) decodes all packets in the interval into `QList<QImage>` and emits them all at once via `imagesDecoded` signal (FFMpegDemuxer.cpp:251); the renderer (VideoWidget) is then responsible for paced display.

There is no PTS-to-audio-clock mapping. There are also no I-frame guarantees within an interval — H.264 `gop_size=30` means a keyframe appears every 30 frames (= 3 seconds) which spans multiple intervals; if a peer joins mid-stream they will see corrupted frames until the next keyframe. JamTaba just lives with this.

**Implication for JamWide:** keep the same model. Don't try to do anything fancier — wire-format compatibility requires it.

[VERIFIED: read FFMpegMuxer.cpp, FFMpegDemuxer.cpp, MainController.cpp, ClientMessages.cpp, ServerMessages.cpp directly]

---

## 2. JamTaba plugin proof

### VST plugin
`/Users/cell/dev/JamTaba/PROJECTS/VstPlugin/VstPlugin.pro` does NOT explicitly list FFMpeg sources, but `Jamtaba-common.pri:76-79,202-205` (which it includes via `include(../Jamtaba-common.pri)` line 1) adds them via VPATH. So the same sources compile into VST. Confirmed indirectly because the same `.pri` is used by the standalone where video demonstrably works.

### CLAP plugin
`/Users/cell/dev/JamTaba/PROJECTS/ClapPlugin/` build output directory contains `FFMpegMuxer.o`, `FFMpegDemuxer.o`, `VideoFrameGrabber.o`, `VideoWidget.o` plus their MOC variants. Confirmed by `ls /Users/cell/dev/JamTaba/PROJECTS/ClapPlugin/ | grep -iE "video|ffmpeg"`.

### `#ifdef` gating
None. Video sources are always compiled. The capture-side gating is dynamic: `MainController::handleNewNinjamInterval()` only calls `videoEncoder.startNewInterval()` when `mainWindow->cameraIsActivated()` is true (MainController.cpp:278-280); the user enables it via UI.

### Webcam permission handling
JamTaba does NOT call any explicit permission API. It uses Qt 5's `QCamera` (via Qt Multimedia + DirectShow plugin on Windows: `QTPLUGIN += dsengine` in VstPlugin.pro:7 and ClapPlugin.pro:8). On macOS, AVFoundation is invoked under the hood by Qt; the OS will throw a permission prompt the first time the AVCaptureDevice is started. JamTaba apparently relies on whatever Qt's underlying behavior is and does not have a fallback UX for "user denied camera." [CITED: `grep` of JamTaba source shows no `AVCaptureDevice authorizationStatus` or `requestPermission` calls anywhere]

JamTaba is also not Hardened Runtime-signed in their default builds (their CI artifacts are ad-hoc-signed for Mac), so the entitlement question doesn't bite them as hard. JamWide already has Hardened Runtime opt-in (`JAMWIDE_HARDENED_RUNTIME=ON` in CMakeLists.txt:246) — this is **strictly better** than JamTaba's posture but means the camera entitlement is mandatory.

[VERIFIED: read both .pro files, confirmed .o files exist, grep'd for permission APIs]

---

## 3. ffmpeg integration plan for JamWide

### Library set (LGPL-only, JamWide is non-GPL)

| Lib              | Purpose                                          | Required? |
|------------------|--------------------------------------------------|-----------|
| libavcodec       | H.264 encode (via openh264) + decode (internal)  | yes       |
| libavformat      | NOT strictly needed — JamTaba uses raw NAL bytestream + custom AVIO. Could be omitted by going straight through `avcodec_send_frame`/`avcodec_receive_packet` and skipping the `avformat_open_input` round-trip on the decode side. Keeping it makes the JamTaba demuxer port mechanical. | recommended for compatibility |
| libswscale       | YUV↔RGB color conversion (decoded YUV420P → RGBA for display)| yes |
| libavutil        | Common utilities (frame allocation)              | yes (transitive) |
| libswresample    | NOT needed (audio resampling)                    | no — can disable at configure time |
| libavdevice      | NOT needed (we use juce::CameraDevice for capture, not ffmpeg's avdevice) | no |
| libavfilter      | NOT needed                                       | no        |
| openh264 (Cisco) | LGPL-safe H.264 encoder backend                  | yes       |
| **NOT** libx264  | GPL — would force JamWide to GPL                 | **no**    |
| **NOT** libfdk_aac, libvpx, libx265 | not needed and either GPL/non-LGPL or non-DFSG | **no** |

`./configure` flags (for source build):
```
--enable-shared --disable-static --disable-gpl --disable-nonfree
--disable-programs --disable-doc --disable-avdevice --disable-swresample
--disable-avfilter --disable-postproc --disable-network
--enable-libopenh264 --enable-encoder=libopenh264 --enable-decoder=h264
--enable-protocol=file --enable-demuxer=h264 --enable-muxer=h264
--enable-parser=h264 --disable-everything
```
This produces a minimal LGPL ffmpeg.

### Distribution strategy

| Option | Pros | Cons |
|--------|------|------|
| **Vendored prebuilt per-platform** | Reproducible, no build-toolchain churn for contributors, fast CI | Have to maintain three binaries × two arches, must verify LGPL compliance trail |
| Build-from-source in CMake | Full control, single source of truth | Adds 5-10 min to clean builds; Windows ffmpeg builds are notoriously cranky |
| System package (homebrew/apt) | Smallest repo footprint | Users must install separately; not viable for plugin redistribution because users running a DAW don't generally have ffmpeg installed |

**Recommendation:** vendored prebuilt LGPL minimal builds in `libs/ffmpeg/{macos-x86_64,macos-arm64,linux-x86_64,windows-x86_64}/{lib,include}` with a single `cmake/ffmpeg.cmake` that picks the right tree. JamWide already does this for libflac/libvorbis/libogg (see `libs/` listing), and the CMake plumbing pattern is established. [ASSUMED: that vendored binary distribution is acceptable to project policy; user should confirm.]

### Bundle size delta

| Lib                             | Brew 8.1.1 (full GPL) | LGPL minimal (estimated) |
|---------------------------------|-----------------------|--------------------------|
| libavcodec.62.dylib             | 11 MB                 | ~5-7 MB                  |
| libavformat.62.dylib            | 2.1 MB                | ~1-1.5 MB                |
| libswscale.9.dylib              | (small, ~700 KB)      | ~500 KB                  |
| libavutil.60.dylib              | 691 KB                | ~600 KB                  |
| libopenh264.dylib               | (separate)            | ~700 KB                  |
| **Total per-arch, stripped**    | ~15 MB                | **~7-10 MB**             |
| **Universal binary (x86_64+arm64)** | ~30 MB           | **~14-20 MB**            |

[VERIFIED: ran `ls -lh /usr/local/opt/ffmpeg/lib/`. Estimated LGPL sizes are downward extrapolation; real numbers should be measured during the spike.]

This is significant — the current JamWide standalone binary is comparable in size to 7-10 MB. Bundle will roughly double on each platform. Bundle bloat is a risk worth acknowledging up-front.

### License compliance (LGPL)

LGPL allows linking from non-GPL applications **only if dynamically linked** AND the application carries:
1. A copy of the LGPL text in the distribution (or a link).
2. Notice in About dialog naming "FFmpeg" with version, license URL, and statement that ffmpeg is dynamically linked and can be replaced.
3. End-user has the ability to relink against a modified ffmpeg — satisfied by dynamic linking + readable build flags.
4. openh264 is BSD; needs notice but no LGPL constraints. Cisco's prebuilt binaries are royalty-free; **building openh264 from source means the user (JamWide) is responsible for MPEG-LA royalties** — use Cisco-distributed openh264 binaries to inherit Cisco's royalty payment. [CITED: WebSearch result on openh264 patent licensing.]

[CITED: https://www.ffmpeg.org/legal.html, https://en.wikipedia.org/wiki/OpenH264]

### macOS codesigning — dylibs to sign

When `JAMWIDE_HARDENED_RUNTIME=ON`, every embedded dylib needs to be signed independently before the bundle is signed. List of dylibs to sign on macOS (to be placed at `Contents/Frameworks/` of each plugin bundle):
- `libavcodec.62.dylib`
- `libavformat.62.dylib`
- `libswscale.9.dylib`
- `libavutil.60.dylib`
- `libopenh264.7.dylib`

CMake codesign block at `CMakeLists.txt:244-269` currently signs the bundle `$<TARGET_BUNDLE_DIR:JamWideJuce_${_fmt}>` only — needs to be augmented to also sign embedded dylibs FIRST, then re-sign the bundle. JUCE pattern: dylibs go into `Contents/Frameworks/` and are referenced via `@loader_path/../Frameworks/libavcodec.62.dylib` (set with `install_name_tool` post-build).

[ASSUMED: that vendored ffmpeg dylibs will be physically copied into each plugin's `Contents/Frameworks/`. JamWide doesn't currently do this for any other dylib because it statically links libflac/libvorbis/libogg. ffmpeg LGPL forbids static linking — this is a fundamentally new pattern in the project.]

---

## 4. JUCE CameraDevice in plugin context

### Status
Already vendored. `libs/juce/modules/juce_video/` ships `juce::CameraDevice` (header `capture/juce_CameraDevice.h`). License is **AGPLv3 / Commercial** (line 51 of `juce_video.h`). JamWide already has a JUCE commercial license (per other JUCE modules in use). [ASSUMED: that the existing JUCE license covers `juce_video`. Unconfirmed in this research; the user should double-check the seat license terms cover the video module.]

### Enablement
`#define JUCE_USE_CAMERA 0` by default (juce_video.h:74). Must be flipped to 1 in the plugin target via `target_compile_definitions(JamWideJuce PUBLIC JUCE_USE_CAMERA=1)`. Module must also be linked: `target_link_libraries(JamWideJuce PRIVATE juce::juce_video)`.

OSX framework deps that get pulled in automatically by the JUCE module: AVKit, AVFoundation, CoreMedia (juce_video.h:55).

### Plugin context warnings
**None in JUCE source.** The macOS impl (`libs/juce/modules/juce_video/native/juce_CameraDevice_mac.h`) is plain AVCaptureSession — runs on whatever thread the JUCE message thread happens to be on; the per-frame callback (`Listener::imageReceived`) is documented to fire from "any thread, must be fast" (juce_CameraDevice.h:202-206). This is the same threading contract as `juce::AudioIODevice` — easy to handle by routing into a JUCE `MessageManager::callAsync` or an SPSC ring (the project already has SPSC infrastructure: see Phase 15.1's `src/threading/spsc_payloads.h`).

**Practical concern:** JUCE's CameraDevice uses `AVCaptureSession` which in turn requires a runloop. In a VST3 plugin the JUCE message thread runs as long as the editor exists — if a user opens the plugin headless (no editor), `juce::CameraDevice` MAY have issues. Worth probing during the spike.

### Frame format
`juce::Image` (CameraDevice.h:213). Internally `Image::ARGB` on macOS, `Image::RGB` on Windows. Need to read `Image::BitmapData` and convert to YUV420P before feeding ffmpeg's encoder. Same conversion JamTaba does (FFMpegMuxer.cpp:541-577); JUCE's Image API is similar to QImage so the port is mechanical.

### Framerate negotiation
`CameraDevice::openDevice (deviceIndex, minWidth, minHeight, maxWidth, maxHeight, highQuality)` (juce_CameraDevice.h:78-81). No framerate parameter — must throttle in our `Listener` callback by dropping frames to hit 10 fps (matching JamTaba's `CAMERA_FPS=10`).

[CITED: JUCE forum thread "New feature: Camera support for iOS and Android" + JUCE source files.]

---

## 5. macOS Hardened Runtime + DAW webcam permission

### Required entitlement
`com.apple.security.device.camera` in `JamWide.entitlements`. Currently JamWide.entitlements has `audio-input`, `network.client`, `network.server` (all 3 lines: `/Users/cell/dev/JamWide/JamWide.entitlements:5-10`). Camera key is missing.

### Where to add it
Two changes:

1. **`JamWide.entitlements`** — add `<key>com.apple.security.device.camera</key><true/>`. This file is already plumbed into `codesign --entitlements` at `CMakeLists.txt:251-253`.
2. **`juce_add_plugin` block at `CMakeLists.txt:145-162`** — add:
   ```
   CAMERA_PERMISSION_ENABLED TRUE
   CAMERA_PERMISSION_TEXT "JamWide needs camera access for video collaboration"
   HARDENED_RUNTIME_ENABLED TRUE
   HARDENED_RUNTIME_OPTIONS com.apple.security.device.camera com.apple.security.device.audio-input com.apple.security.network.client com.apple.security.network.server
   ```
   `CAMERA_PERMISSION_ENABLED TRUE` adds `NSCameraUsageDescription` to Info.plist (verified at `libs/juce/extras/Build/juce_build_tools/utils/juce_PlistOptions.cpp:149-150`); without this the OS denies the access without showing a prompt, and AVFoundation returns `notDetermined` permanently.
   `HARDENED_RUNTIME_OPTIONS` populates the entitlements plist (verified at `libs/juce/extras/Build/juce_build_tools/utils/juce_Entitlements.cpp:103-105`).

### DAW behavior

This is the single most important risk. From SPARTA Issue #82 ([CITED](https://github.com/leomccormack/SPARTA/issues/82)): on macOS 11+, **the DAW process** must request the camera permission via TCC; the plugin (loaded as a dylib in the DAW's address space) inherits the host's permissions and cannot independently prompt. If REAPER never requests camera, the camera access call from inside REAPER will simply fail (or worse, crash on hardened runtime).

| DAW          | Requests camera permission? | Behavior            |
|--------------|------------------------------|---------------------|
| **Logic Pro**| Yes (since Apple ships it; AVFoundation is a system framework with default access) | Likely works |
| **REAPER**   | NO (per SPARTA #82)          | Crash/silent failure unless plugin handles denial |
| **Ableton Live** | Unknown — needs probing  | Likely no, similar to REAPER |
| **Bitwig**   | Unknown                       | Likely no            |
| **Standalone JamWide** | Self — yes if entitlement is set | Works |

[CITED: SPARTA Issue #82 documents REAPER specifically. Other DAW behavior is extrapolation; user should test each.]

### Fallback UX (mandatory)

The plugin MUST handle `AVCaptureDevice authorizationStatus == .denied` and `.restricted` gracefully:
1. On video-button click, call `AVCaptureDevice.authorizationStatus(for: .video)` BEFORE attempting `juce::CameraDevice::openDevice`.
2. If `.notDetermined`, call `AVCaptureDevice.requestAccess(for: .video, completionHandler:)`. In a hardened-runtime DAW that doesn't have NSCameraUsageDescription, this returns `false` immediately; treat as denied.
3. If denied/restricted, show a small dialog: "Camera unavailable in this DAW. Camera works in standalone or in DAWs that explicitly request camera access (e.g. Logic Pro). Click OK to continue without video."
4. Do not crash. Do not retry. Do not block the audio thread.

This fallback is what differentiates JamWide from the SPARTA bug — they crash; we won't.

[CITED: https://github.com/leomccormack/SPARTA/issues/82, Apple TCC docs]

---

## 6. JamWide integration points (file:line citations)

### NINJAM upload path (where video send hooks in)

JamTaba's `ninjamService->sendIntervalBegin(GUID, channelIndex, false)` and `ninjamService->sendIntervalPart(GUID, bytes, isLast)` are NOT a single call site in JamWide. JamWide does the equivalent in `NJClient::Run()` directly via `m_netcon->Send()`:

- **`src/core/njclient.cpp:2407-2413`** — current INTERVAL BEGIN with empty GUID + fourcc=0 (silence marker for the audio channel)
- **`src/core/njclient.cpp:2468-2474`** — current INTERVAL BEGIN with audio fourcc (Vorbis/FLAC) — the model for our video begin
- **`src/core/njclient.cpp:2504-2530`** — current INTERVAL WRITE chunked through audio encoder's Available/Get loop — the model for our video chunked write
- **`src/core/njclient.cpp:2546-2569`** — current INTERVAL WRITE final chunk with `flags=1` (isLast)

**For video, the new code should NOT live inside NJClient::Run()'s tight audio-encoder loop.** The cleanest pattern is a parallel upload state machine on a dedicated thread (mirroring JamTaba's `QThreadPool(1)`) that emits `m_netcon->Send(cuib_video.build())` and `m_netcon->Send(wh_video.build())` directly. `m_netcon->Send` thread safety needs verification — likely uses a mutex internally; check during spike.

### NINJAM download path (where video receive hooks in)

- **`src/core/njclient.cpp:2105-2189`** — `MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN` handler. The branch at line 2148 (`else if (dib.fourcc) // download coming`) is where JamTaba video frames currently land — and they get fed to `ds->Open(this, dib.fourcc, ...)` which routes through `start_decode` and ends up in `CreateNJDecoder()` (Vorbis) for unknown fourcc (line 2683-2684). **This is a wire-compatibility bug today** — JamWide running unchanged against a JamTaba peer would not crash but would silently feed H.264 bytes into the Vorbis decoder.
- **`src/core/njclient.cpp:2220-2259`** — `MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE` handler. Routes by GUID match into `ds->Write()`. Video receive needs a parallel "video downloads in flight" registry keyed by GUID that intercepts `JTBv` fourcc at the BEGIN handler BEFORE `start_decode` is called.
- **Required new check at line ~2147** (before the `else if (dib.fourcc)` branch): if `dib.fourcc == MAKE_NJ_FOURCC('J','T','B','v')`, route into a video-download state machine.

### Audio-interval reception fan-out (where video frames feed into display)

- **`src/core/njclient.cpp:2148-2160`** — current path that creates a `RemoteDownload` for audio. Video equivalent should bypass this entirely; instead, accumulate `JTBv` bytes per (user, GUID) pair until the `flags & 1` (isLast) terminator, then hand the assembled byte buffer to the video decoder + display widget.
- **Display side** — the video frames need to fan out to a JUCE component owned by `JamWideJuceEditor`. Since this is per-remote-user, the natural place is `juce/ui/ChannelStrip.cpp` or a sibling widget. The processor side (`JamWideJuceProcessor`) needs a `std::map<std::string, std::shared_ptr<VideoDecoder>>` keyed by username, with thread-safe access (the run thread receives bytes, the message thread polls for new frames).

### Existing video button (to repurpose)

- **`juce/ui/ConnectionBar.h:33,72`** — `std::function<void()> onVideoClicked` and `juce::TextButton videoButton`
- **`juce/ui/ConnectionBar.cpp:206-217`** — button setup, "Video" label, currently disabled until connected (line 213), `setTooltip("Open video companion in browser")` will need to change
- **`juce/ui/ConnectionBar.cpp:644-650`** — `setVideoActive(bool)` toggles button color
- **`juce/JamWideJuceEditor.cpp:127-144`** — current click handler that launches the VDO.Ninja browser companion

The button should be renamed conceptually to "Camera" (or kept as "Video"), and its onClick handler should toggle local camera capture instead of launching a browser.

### Files to remove (full list)

```
juce/video/BrowserDetect.h
juce/video/BrowserDetect_mac.mm
juce/video/BrowserDetect_win.cpp
juce/video/VideoCompanion.h
juce/video/VideoCompanion.cpp
juce/video/VideoPrivacyDialog.h
juce/video/VideoPrivacyDialog.cpp
companion/                          (entire directory tree including e2e/, src/)
tests/test_video_sync.cpp
```

CMakeLists.txt edits required:
- **`CMakeLists.txt:134-139`** — IXWebSocket subsection. Comment says "WebSocket server for video companion, per D-12". If no other code uses ixwebsocket, this can be removed entirely. Quick `grep -r ixwebsocket src/ juce/` will confirm.
- **`CMakeLists.txt:189-199`** — `target_sources` block listing `juce/video/VideoCompanion.cpp`, `juce/video/VideoPrivacyDialog.cpp`, `juce/video/BrowserDetect_mac.mm`, `juce/video/BrowserDetect_win.cpp` — delete these lines.
- **`CMakeLists.txt:217`** — `target_link_libraries(JamWideJuce PRIVATE ... ixwebsocket ...)` — remove `ixwebsocket` if unused elsewhere.
- **`CMakeLists.txt:358-360`** — `add_executable(test_video_sync ...)` and `add_test(NAME video_sync ...)` — delete.

JamWideJuceProcessor edits:
- **`juce/JamWideJuceProcessor.h:119`** — `std::unique_ptr<jamwide::VideoCompanion> videoCompanion;` — remove.
- **`juce/JamWideJuceProcessor.cpp:61`** — `videoCompanion = std::make_unique<jamwide::VideoCompanion>(*this);` — remove.
- **`juce/JamWideJuceProcessor.cpp:69-72`** — destructor cleanup line referencing videoCompanion — remove.

JamWideJuceEditor edits:
- **`juce/JamWideJuceEditor.cpp:5-6`** — `#include "video/BrowserDetect.h"` and `#include "video/VideoCompanion.h"` — remove.
- **`juce/JamWideJuceEditor.cpp:106-144`** — `videoPrivacyDialog` setup, browser-detection-based privacy modal — remove or replace with new camera-permission UX.
- **`juce/JamWideJuceEditor.cpp:186-189,229-230,343-352,441-460,533-535`** — all videoCompanion->* call sites — remove.

ConnectionBar edits:
- **`juce/ui/ConnectionBar.cpp:8`** — `#include "video/VideoCompanion.h"` — remove.
- **`juce/ui/ConnectionBar.cpp:512-528`** — videoCompanion->isActive() / deactivate() references — replace with new local camera state.

[VERIFIED: read all cited files at the cited line numbers]

---

## 7. Pitfalls and constraints

### 320×240 cap — keep, raise, or make configurable?
**Keep at 320×240 for v1** for bit-for-bit JamTaba compatibility. Bandwidth-wise: 96 kbps × N peers + audio adds up fast on a NINJAM server. Future enhancement: capability negotiation via NINJAM chat ("/me supports video=640x480") to opt into higher resolutions only when both peers know the other supports it.

### Bandwidth: video on top of audio
At 96 kbps video + ~128 kbps Vorbis audio per peer = ~225 kbps upload. With 5 peers in a session: every peer is downloading 5 × 225 = 1.1 Mbps + their own 225 kbps upload. Current NINJAM servers don't enforce bandwidth caps but musicians on home connections may saturate. Recommend exposing a "video off" toggle (already implied by the per-user video button).

### GUI thread safety for plugin display widget
The decode side runs on the NINJAM run thread (`NinjamRunThread::run` calls `NJClient::Run()` which processes incoming messages). Decoded video frames must reach the JUCE message thread before being painted. Use `juce::MessageManager::callAsync([img]{ ... })` or post via existing JamWide SPSC infrastructure. The Phase 15.1 mirror pattern (RemoteUserMirror keyed by stable slot) is exactly the right shape.

### Camera permission denied = silent garbage
If `juce::CameraDevice::openDevice` returns nullptr because permission is denied, naive code will either crash on dereference or just silently produce no video. The first run on every DAW must explicitly check `AVCaptureDevice.authorizationStatus` and show the fallback dialog described in §5.

### `JTBv` fourCC ambiguity
JamTaba uses `'v'` lowercase to mark the codec family ('OGGv' = Ogg Vorbis, 'JTBv' = JamTaba video). If JamWide ever wants to send a different codec (e.g. AV1), a new fourCC like `'JWAV'` (JamWide AV1) would be needed AND would break JamTaba interop. For v1, stick with `JTBv` exactly.

### x264 license trap
JamTaba's existing build links `-lx264` (GPL). If the JamWide port "just installs ffmpeg from brew" or copies homebrew dylibs, those are GPL-tainted and would force JamWide to GPL itself — incompatible with the JUCE commercial license. **The CI must verify ffmpeg dylibs were built without `--enable-gpl` and without `--enable-libx264`.** A grep-the-output check (`strings libavcodec.dylib | grep -q libx264 && echo BAD`) catches accidental contamination.

### macOS notarization with embedded dylibs
Apple's notarization service rejects binaries with unsigned or improperly-signed dylibs. The codesign step must:
1. Sign each ffmpeg dylib individually first (with hardened runtime + camera entitlement).
2. Use `install_name_tool -change` to set load paths to `@loader_path/../Frameworks/libX.Y.dylib`.
3. Sign the bundle wrapping them (with `--deep --strict`).
4. Re-staple notarization ticket.

JamWide's current notarization flow (per memory `project_apple_signing.md`) handles step 4 but not steps 1-2 because no embedded dylibs exist today. New territory.

### JUCE camera permission may not exist for plugins on macOS
The `CAMERA_PERMISSION_ENABLED TRUE` path adds `NSCameraUsageDescription` to the **Info.plist of the plugin bundle**. macOS reads the Info.plist of the **process executable** (the DAW) for permission descriptions, not the plugin. So the description string in Info.plist of the plugin may be ignored entirely. The TCC system uses the host process's bundle identifier to track permission. This means:
1. Standalone build: works as expected.
2. Plugin in DAW: the DAW must already have NSCameraUsageDescription in its Info.plist OR the OS shows a generic prompt (or nothing). We have no control over this.

[ASSUMED, requires verification during spike: behavior of NSCameraUsageDescription inside a plugin bundle vs. the host's bundle.]

---

## 8. Recommended scoping for the quick task

This is structurally a multi-phase milestone. Suggest scoping THIS quick task to **Option (a)** below and explicitly deferring everything else to `/gsd-new-milestone`.

### Option (a) — RECOMMENDED — Feasibility spike (1-3 plan tasks)

**Goal:** Prove that ffmpeg LGPL build + JUCE CameraDevice + openh264 work end-to-end on the developer's machine, in standalone, before committing to phased delivery. No network, no removal.

Tasks:
1. Vendor a minimal LGPL ffmpeg build (libavcodec + libavformat + libswscale + libavutil + libopenh264) for macOS arm64 only (the dev machine arch).
2. Add a tiny test executable `tests/video_spike.cpp` that:
   - Opens the default `juce::CameraDevice` synchronously.
   - Captures 100 frames at 10 fps into `juce::Image`.
   - Encodes each frame to H.264 with the JamTaba codec parameters (320×240 YUV420P, 96kbps, openh264 backend).
   - Decodes the H.264 bytestream back through libavcodec.
   - Writes 100 PNGs of the decoded frames to /tmp.
3. Manual verification: open the PNGs and confirm they look like the camera input.

**Output:** Definitive answer to "do these libraries actually compose?" plus measured bundle-size delta and Mac-specific codesigning learnings before committing to the larger phased work.

**Why not Option (b):** Scaffolding without runtime evidence wastes time if the camera path doesn't work in plugin context.
**Why not Option (c):** End-to-end thin slice in a single quick task is too much surface area; the wire-format-compatibility piece alone deserves its own plan with TSan-style discipline (the receive path edits the same `m_users_cs`-protected code Phase 15.1 just hardened).

### Deferred-to-milestone items (suggest creating `260515-0pc-deferred-items.md`)

A. **Spike** — option (a) above (THIS quick task)
B. **ffmpeg+openh264 vendoring across all platforms** (macOS x86_64, Linux x86_64, Windows x86_64, plus universal-binary stitching for macOS)
C. **JUCE CameraDevice integration** (capture module, frame-format conversion, message-thread plumbing, permission UX with denial fallback)
D. **Encoder + interval-frame chunker** (port FFMpegMuxer.cpp to JUCE, drop QtConcurrent for std::thread, drop Q_OBJECT signals for std::function or JUCE ChangeBroadcaster)
E. **Decoder + display widget** (port FFMpegDemuxer.cpp + VideoWidget.cpp; render via JUCE Component::paint or OpenGL component for performance)
F. **NINJAM channel wiring** (`JTBv` fourCC begin/write on send; intercept `JTBv` BEFORE `start_decode` on receive; per-user GUID assembly buffer)
G. **Plugin entitlements + codesigning** (camera entitlement in JamWide.entitlements + JUCE_HARDENED_RUNTIME_OPTIONS in juce_add_plugin + per-dylib codesign step with @loader_path rewriting)
H. **Remove VDO.Ninja stack** (file deletes per §6 list; CMakeLists cleanup; processor + editor cleanup)
I. **Per-DAW UAT** (Logic Pro, REAPER, Ableton Live, Bitwig — with explicit known-failure documentation for DAWs that don't request camera permission)

The order of B-I is roughly the order the milestone should execute in. F can land before A-E if the plan elects to mock the encoder output.

---

## 9. Open questions for the planner

1. **JUCE seat license coverage of `juce_video`** — does JamWide's existing JUCE commercial license entitle the project to ship the AGPL-licensed `juce_video` module? Need user confirmation; without it, the camera capture would have to be reimplemented directly via AVFoundation/MediaFoundation/v4l2.
2. **Existing video button — keep or remove in this task?** Per §6, it's currently wired to launch VDO.Ninja. If we go with Option (a) (spike), the button can stay untouched; if we go with Option (c) (thin slice), the button must be partially repurposed.
3. **NINJAM `m_netcon->Send` thread safety** — JamTaba dispatches via the QObject signal/slot mechanism which serializes onto the Qt event loop. JamWide's `JNL_Connection::Send` may or may not be thread-safe for concurrent audio-encoder + video-encoder writes. Needs verification during the spike (the audio-thread send path is single-threaded today — adding a video sender opens a new contention point).
4. **CLAP plugin in `FORMATS` line** — `CMakeLists.txt:151` says `FORMATS VST3 AU Standalone` and CLAP is layered on via `clap_juce_extensions_plugin` at line 231. The user's locked decision lists VST3/AU/CLAP. Does the existing CLAP target inherit camera entitlements correctly (the CLAP wrapper repackages the JUCE plugin into a different bundle structure — Info.plist may need to be re-emitted)?
5. **Universal binary stitching for ffmpeg dylibs** — the project memory mentions local builds are x86_64-only and CI builds universal. Does CI have a workflow for `lipo`-merging two-arch ffmpeg dylibs into a fat binary? If not, that's an additional CI plan task.
6. **Bundle size budget** — Adding ~14-20 MB of universal ffmpeg dylibs to each of {Standalone, VST3, AU, CLAP} bundles roughly doubles the installer size. Is that acceptable, or should ffmpeg live in a single shared `~/Library/Application Support/JamWide/` location that all bundles dlopen?
7. **`JTBv` fourCC ASCII byte order** — JamWide's `MAKE_NJ_FOURCC` macro at `src/core/njclient.cpp:154` should be inspected to confirm it produces the same wire bytes as JamTaba's manual `fourCC[0]='J'; fourCC[1]='T'; ...` — if there's an endianness or ordering mismatch, JamWide's `JTBv` send won't match JamTaba's `JTBv` recognition.

---

## Sources

### Primary (HIGH confidence — direct file reads)
- JamTaba: `/Users/cell/dev/JamTaba/src/Common/video/{FFMpegMuxer,FFMpegDemuxer,VideoFrameGrabber,FFMpegCommon}.{h,cpp}`
- JamTaba: `/Users/cell/dev/JamTaba/src/Common/MainController.cpp` lines 30-50, 100-130, 270-300, 365-400
- JamTaba: `/Users/cell/dev/JamTaba/src/Common/ninjam/client/{ClientMessages,ServerMessages,Service}.cpp`
- JamTaba: `/Users/cell/dev/JamTaba/PROJECTS/{VstPlugin/VstPlugin,ClapPlugin/ClapPlugin,Jamtaba-common}.{pro,pri}`
- JamTaba CLAP build outputs at `/Users/cell/dev/JamTaba/PROJECTS/ClapPlugin/*.o`
- JamWide: `/Users/cell/dev/JamWide/src/core/{njclient.cpp,mpb.h}`
- JamWide: `/Users/cell/dev/JamWide/juce/{JamWideJuceEditor,JamWideJuceProcessor}.{h,cpp}`, `juce/ui/ConnectionBar.{h,cpp}`
- JamWide: `/Users/cell/dev/JamWide/CMakeLists.txt`, `/Users/cell/dev/JamWide/JamWide.entitlements`
- JamWide: `/Users/cell/dev/JamWide/libs/juce/modules/juce_video/{juce_video.h,capture/juce_CameraDevice.h,native/juce_CameraDevice_mac.h}`
- JamWide: `/Users/cell/dev/JamWide/libs/juce/extras/Build/CMake/JUCEUtils.cmake` lines 287, 367-368, 403-406, 1789, 1978-2012
- JamWide: `/Users/cell/dev/JamWide/libs/juce/extras/Build/juce_build_tools/utils/{juce_PlistOptions.cpp,juce_Entitlements.cpp}`
- Local ffmpeg sizes: `/usr/local/opt/ffmpeg/lib/` (homebrew 8.1.1)

### Secondary (MEDIUM confidence — web sources, official documentation)
- [SPARTA Issue #82 — Powermap, DirASS, SLDoA crash on hardened-runtime DAWs](https://github.com/leomccormack/SPARTA/issues/82) — definitive evidence of REAPER camera-permission issue
- [Cisco openh264 GitHub](https://github.com/cisco/openh264) — BSD license, royalty-free binaries
- [OpenH264 Wikipedia](https://en.wikipedia.org/wiki/OpenH264) — Cisco MPEG-LA royalty payment for prebuilt binaries
- [FFmpeg Legal](https://www.ffmpeg.org/legal.html) — LGPL vs GPL configure flag implications
- [Prebuilt LGPL FFmpeg with OpenH264 GitHub](https://github.com/Crigges/Prebuilt-LGPL-2.1-FFmpeg-with-OpenH264) — reference for LGPL configure pattern
- [JUCE forum: "New feature: Camera support for iOS and Android"](https://forum.juce.com/t/new-feature-camera-support-for-ios-and-android/27409) — camera enablement context

### Tertiary (LOW confidence)
- LGPL minimal ffmpeg size estimates (~7-10 MB per arch) — extrapolated downward from full-fat homebrew sizes; should be verified by actually building during the spike

## Metadata

**Confidence breakdown:**
- JamTaba codec/wire-format specs: HIGH — read the source verbatim
- JamWide integration points: HIGH — read the source verbatim
- ffmpeg LGPL minimal build feasibility: MEDIUM — well-documented pattern, but exact dylib sizes need to be measured
- DAW-specific camera permission behavior: MEDIUM for REAPER (cited evidence), LOW for Live/Bitwig (extrapolated)
- JUCE CameraDevice in plugin context: MEDIUM — JUCE supports it but practical plugin-context behavior unverified
- macOS NSCameraUsageDescription inside a plugin Info.plist: LOW — needs spike-time verification

**Research date:** 2026-05-15
**Valid until:** 2026-06-15 (LGPL ffmpeg ecosystem moves slowly; openh264 stable since 2014; JUCE 8 API stable)
