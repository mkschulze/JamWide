---
quick_id: 260515-0pc
slug: investigate-jamtaba-video-implementation
purpose: milestone-handoff
created: 2026-05-15
---

# Quick Task 260515-0pc — Deferred Items (Milestone Handoff)

> **Source:** Authoritative scope catalog for the native-video milestone (created by quick task 260515-0pc).
> See sibling files: 260515-0pc-CONTEXT.md (locked decisions), 260515-0pc-RESEARCH.md (initial JamTaba research), **260515-0pc-RESEARCH-ADDENDUM.md (NinjamZap pivot — supersedes JamTaba wire format and Item F below)**, 260515-0pc-spike-results.md (measured spike evidence).
> To start the milestone: `/gsd-new-milestone` and seed it from this file.
>
> **2026-05-15 PIVOT NOTICE:** Sections marked `[SUPERSEDED-2026-05-15]` were originally written for a JamTaba-style port but the user directed us to follow NinjamZap instead. The corrected design is in `260515-0pc-RESEARCH-ADDENDUM.md`. Item F below is the most affected; Section 3's "Wire format" decision is now NinjamZap-compatible (not JamTaba); a new Item J (server-side adaptation) is added at the end; and Section 2 has new questions Q8-Q13.

This file enumerates **everything the spike intentionally did NOT do**, so the milestone planner can pick up cleanly. The spike answered *one* question: "do JUCE CameraDevice + LGPL ffmpeg + Cisco openh264 compose end-to-end on the dev arch?" Everything else from RESEARCH §6 (integration points), §7 (pitfalls), §8 (deferred items B–I), and §9 (open questions) is captured here.

---

## Section 1: Deferred Items (RESEARCH §8 B–I)

Each item below was explicitly scoped OUT of this quick task. Each is sized in *plans* (not hours) using STATE.md "Performance Metrics" table averages: small plans = 100–300 LOC + 1–2 tasks; medium plans = 500–800 LOC + 2–3 tasks; large plans = 1000+ LOC + 3+ tasks.

### B. ffmpeg + openh264 vendoring across all platforms

- **Goal:** Reproduce the spike's `libs/ffmpeg/macos-${ARCH}/` tree for **macOS arm64**, **Linux x86_64**, **Windows x86_64**, plus universal-binary stitching for macOS (lipo arm64 + x86_64 → fat dylibs). The spike vendored x86_64 macOS only.
- **Phase size estimate:** **2 plans** — Plan B.1: arm64-mac + universal stitching (≈300 LOC of CMake + scripts; depends on a known-good Cisco openh264 source-build path since Cisco does not publish arm64-mac prebuilts after v2.1.1 osx64). Plan B.2: Linux + Windows (≈400 LOC; Windows ffmpeg builds are notoriously cranky per RESEARCH §3 "Distribution strategy").
- **Dependencies:** This spike (item A) — confirms the LGPL recipe works at all; B reproduces it across the matrix.
- **File:line provenance from RESEARCH §6:**
  - `libs/ffmpeg/macos-x86_64/{lib,include}/` — spike output, item B parameterizes per arch.
  - `cmake/ffmpeg.cmake` — spike's per-arch dispatcher; needs lipo path + Linux/Windows dispatch.
  - `scripts/build_ffmpeg_lgpl.sh` — spike's source-build recipe; needs cross-compilation knobs.
- **Spike-relevant findings:** Build recipe works (LGPL discipline holds, openh264 reachable). Cisco openh264 v2.1.1 osx64 prebuilt was the only arch that "just worked" — newer Cisco versions ship source-only. **Critical risk for arm64-mac:** if no Cisco prebuilt exists, the milestone faces a license question (build openh264 from source = JamWide pays MPEG-LA royalties) — see RESEARCH §3 "License compliance" + Q1 below for the disposition.

### C. JUCE CameraDevice integration

- **Goal:** Wire `juce::CameraDevice` into the JamWideJuce binary (NOT just a standalone test). Capture frames on the message thread, convert ARGB→YUV420P off the message thread (worker pool), feed the encoder. Implement permission-denial fallback UX per RESEARCH §5 (REAPER, Live, Bitwig do NOT request `com.apple.security.device.camera` for themselves; the plugin must catch denial and show a "camera unavailable" dialog without crashing).
- **Phase size estimate:** **2 plans** — Plan C.1: capture module + worker-thread frame-format conversion (≈500 LOC). Plan C.2: permission UX + per-DAW behavior matrix (≈300 LOC + a lot of testing).
- **Dependencies:** Item B (vendored ffmpeg dylibs available across platforms) + Item G (entitlements wired so the JUCE plugin even has camera access in the standalone case).
- **File:line provenance from RESEARCH §6:**
  - `juce/JamWideJuceProcessor.h:119` and `juce/JamWideJuceProcessor.cpp:61,69-72` — places where the new CameraDevice owner needs to live (replacing the removed VideoCompanion owner).
  - `juce/ui/ConnectionBar.cpp:206-217,512-528,644-650` — video button currently launches VDO.Ninja; rewire to toggle local capture.
  - `libs/juce/modules/juce_video/capture/juce_CameraDevice.h:78-81` — camera open API; `:202-206` — Listener::imageReceived "any thread" contract.
- **Spike-relevant findings:** Spike's `tests/video_spike.cpp` proves the `juce::CameraDevice::openDevice(0, 320, 240, 320, 240, false)` synchronous-open path returns a usable device on macOS x86_64 standalone. Listener callback pattern (cv-guarded deque) works at 10fps. **Plugin-context behavior is UNVERIFIED by spike** — see Q4 below for milestone scope.

### D. Encoder + interval-frame chunker (port FFMpegMuxer.cpp)

- **Goal:** Port JamTaba's `src/Common/video/FFMpegMuxer.cpp` (644 lines) to JamWide. Replace `QThreadPool(1)` worker with `juce::Thread` or `std::thread`; replace `Q_OBJECT` `dataEncoded` signal with `std::function<void(std::vector<uint8_t>, bool isFirstPart)>` callback. Implement the 4 KB chunker that converts encoder packets into NINJAM `UploadIntervalWrite` payloads (RESEARCH §1 "Frame chunking strategy").
- **Phase size estimate:** **2 plans** — Plan D.1: encoder + chunker (≈600 LOC). Plan D.2: interval-boundary state machine (the `startNewInterval` / `finishCurrentInterval` cycle from JamTaba `FFMpegMuxer.cpp:160-185` — easy to get wrong; deserves a TDD plan with red→green covering all the edge cases from JamTaba's source).
- **Dependencies:** Items B + C (need vendored ffmpeg + a frame source).
- **File:line provenance from RESEARCH §6:**
  - JamTaba reference: `/Users/cell/dev/JamTaba/src/Common/video/FFMpegMuxer.cpp:237-277` (encoder configure), `:541-577` (RGB→YUV macro), `:586-643` (no AVFormat container, raw NAL bytestream), `:99` (`QThreadPool(1)` worker pattern).
  - Spike's `tests/video_spike.cpp` lines around the encoder configuration block — bit-for-bit JamTaba codec params, transferable to the production encoder.
- **Spike-relevant findings:** Spike confirms openh264 encoder reachable from libavcodec (`avcodec_find_encoder_by_name("libopenh264") != nullptr`). Spike also measured per-frame encoded-byte averages — see `260515-0pc-spike-results.md` for the "Encode performance" section, which is the rough budget the milestone's chunker should validate against.

### E. Decoder + display widget (port FFMpegDemuxer.cpp + VideoWidget.cpp)

- **Goal:** Port JamTaba's `src/Common/video/FFMpegDemuxer.cpp` (252 lines) to feed `juce::Image` into a `juce::Component::paint` (or `juce::OpenGLContext` for performance). One decoder per remote user, fan-out to per-user video tile in the JUCE editor.
- **Phase size estimate:** **2 plans** — Plan E.1: decoder + per-user owner (≈500 LOC). Plan E.2: display widget + paint integration (≈400 LOC + UI design call: tile size, layout, hide-when-no-video).
- **Dependencies:** Item B (vendored ffmpeg) + Item F (NINJAM receive-path that hands assembled bytes to the decoder).
- **File:line provenance from RESEARCH §6:**
  - JamTaba reference: `/Users/cell/dev/JamTaba/src/Common/video/FFMpegDemuxer.cpp:75-143` (custom AVIO read for raw NAL bytestream), `:251` (`imagesDecoded` signal — entire interval's frames emitted at once).
  - JamWide integration point: `juce/JamWideJuceProcessor.{h,cpp}` — needs a `std::map<std::string, std::shared_ptr<VideoDecoder>>` keyed by username, populated in the receive-path (item F).
- **Spike-relevant findings:** Spike's decoder path (`avcodec_find_decoder(AV_CODEC_ID_H264)` + `av_parser_init` + `avcodec_send_packet`/`receive_frame` + `sws_scale` to BGRA + `juce::PNGImageFormat::writeImageToStream`) is the same skeleton the production decoder uses. The "write PNG" sink swaps for "publish to UI thread via callAsync" in production.

### F. NINJAM channel wiring — NinjamZap-compatible video transport [REVISED-2026-05-15]

> **REVISED 2026-05-15.** Original Item F targeted JamTaba's `JTBv` fourCC. User redirected to NinjamZap design. Authoritative spec now in `260515-0pc-RESEARCH-ADDENDUM.md` "Wire format spec (locked, bit-for-bit NinjamZap-compatible)" section. Original text preserved at the bottom of this item under `[SUPERSEDED]` for traceability.

- **Goal (REVISED):** Implement NinjamZap's full video transport. On the send side: a `RawDataSendBegin/Write/IsEnd` API mirroring `ninjamzap-core/njclient.cpp:2047-2065`, plus video state in `on_new_interval` (lines 3041-3082) emitting fourCC `H264` (= `MAKE_NJ_FOURCC('H','2','6','4')`), the **24-byte interval marker** (`[4B BE prefix=20][4B BE swap_count][16B audio_ch0_guid]`), then cached SPS/PPS, then frames each wrapped in `[4B BE length][payload]`. On the receive side: per-user-channel `VideoRecvState` with the **4-stage pipeline** (`accumulating → next → pending → playing`) and the **GUID-pairing decision tree** (DS match → 1-swap defer; PREV match → play immediately; no-match → HOLD with `kHoldCapDrop=4` resync). Reference implementation: `ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp` lines 2047-2123 (send), 1300-1550 (receive WRITE handling), 3041-3219 (interval state machine).
- **Phase size estimate (REVISED):** **5 plans** (was 3). The NinjamZap design is more code than the JamTaba port (additional 4-stage pipeline + decision tree + per-stream state) but it removes the entire "wire-format compatibility against a captured JamTaba bytestream" plan because NinjamZap is the reference.
  - **Plan F.0:** Add the codec-agnostic `RawDataSendBegin/Write` + `RawDataCallback` + `VideoFrameReadyCallback` API surface to JamWide's `NJClient` equivalent. ≈300 LOC + 100 LOC tests. Foundation for everything else; testable without a real codec.
  - **Plan F.1:** Send-path video state machine. Add `m_video_active`, `m_video_fourcc`, `m_video_chidx`, `m_video_guid`, `m_video_interval_open`, `m_video_spspps` members to `NJClient`. Implement `SetVideoChannel/StopVideoChannel/QueueVideoFrame/SetVideoSPSPPS`. Wire BEGIN/marker/SPS-PPS/END emission into `on_new_interval` per `njclient.cpp:3041-3082`. ≈400 LOC + 200 LOC tests using ninjamzap-core's test harness as a reference.
  - **Plan F.2:** Receive-path 4-stage pipeline. Implement `VideoRecvBuffer`/`VideoRecvState` per `njclient.h:343-417`, the WRITE-time accumulation with marker parse per `njclient.cpp:1530-1547`, and the per-stream lookup `findVideoStream`. ≈500 LOC + 300 LOC tests.
  - **Plan F.3:** GUID-pairing decision tree in `on_new_interval` per `njclient.cpp:3084-3219` (PROMOTE pending→playing, then DS/PREV/no-match dispatch with `kHoldCapDrop=4`). ≈400 LOC of state-machine logic + 400 LOC of corner-case tests (port the 26 NinjamZap test scenarios at `ninjamzap-core/tests/video-sync/scenarios/`).
  - **Plan F.4:** `Net_Connection::Send` thread-safety mitigation (per Q3 resolution: `m_sendq.Add` is not thread-safe; need SPSC ring or mutex around the video producer's calls). ≈150 LOC + 100 LOC stress tests.
- **Dependencies:** Items B + C + D + E (vendored ffmpeg, JUCE camera capture, encoder, decoder all needed before F has anything to transport). F.0 has no codec dependency and can land first as a refactoring foundation.
- **File:line provenance:**
  - **NinjamZap reference (READ — port from these):**
    - `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:200-236` — public API surface
    - `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.h:334-417` — receive-side state structures
    - `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2047-2123` — send API impl (`RawDataSendBegin/Write`, `SetVideoChannel`, `QueueVideoFrame`, `SetVideoSPSPPS`)
    - `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:1300-1550` — WRITE-time receive handling and 24-byte marker parse
    - `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3041-3082` — sender on_new_interval (BEGIN/marker/SPS-PPS/END)
    - `/Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3084-3219` — receiver on_new_interval decision tree
    - `/Users/cell/dev/ninjamzap-core/tests/video-sync/scenarios/*.cpp` (26 scenarios) — port to JamWide tests/ as the UAT spec
    - `/Users/cell/dev/ninjamzap-core/tests/video-sync/harness/TestClient.{h,cpp}` — adopt as JamWide's video integration test harness
  - **JamWide integration points (WRITE — modify these):**
    - `src/core/njclient.cpp:154-155` — existing `MAKE_NJ_FOURCC('O','G','G','v')` and `'F','L','A','C'` patterns; add `MAKE_NJ_FOURCC('H','2','6','4')` define here.
    - `src/core/njclient.cpp:2407-2413,2468-2474,2504-2530,2546-2569` — current INTERVAL BEGIN/WRITE send patterns to model the new RawData* APIs after.
    - `src/core/njclient.cpp:2105-2189,2148,2220-2259` — current INTERVAL BEGIN/WRITE receive path; line 2148 is the choke point where unknown-fourCC routing decisions happen. NEW: add `is_video_fourcc` check (`'H264'`, `'VP8 '`, `'MJPG'`) BEFORE `start_decode` so video bytes never get fed to Vorbis.
    - `src/core/mpb.h:240` — `mpb_client_upload_interval_begin.fourcc` field (already exists, just reused for video values).
    - `src/core/netmsg.cpp:289` — `m_sendq.Add` site (Q3 confirmed not thread-safe). Wrap with mutex or route through SPSC ring in F.4.
- **Spike-relevant findings:** Spike does NOT touch any networking code (per CONTEXT.md scope discipline). Q7 (`MAKE_NJ_FOURCC` byte order) RESOLVED-BY-SPIKE — confirms macro byte order is correct for either fourCC family. `H264` literal byte order: `0x48 0x32 0x36 0x34` (little-endian on disk) — same convention as `OGGv`.

---

> [SUPERSEDED-2026-05-15 — original JamTaba `JTBv` design preserved for traceability only; do not implement.]
>
> ~~On the send side, add `MAKE_NJ_FOURCC('J','T','B','v')` upload-interval-begin + chunked write from the new video encoder, on a per-user chidx=1.~~ ~~On the receive side, intercept `JTBv` fourcc at `src/core/njclient.cpp:2148` BEFORE `start_decode` to prevent feeding H.264 bytes to Vorbis.~~ ~~Plan F.3: TDD plan covering the wire-format compatibility against a captured JamTaba bytestream.~~

### G. Plugin entitlements + codesigning (camera entitlement + per-dylib signing)

- **Goal:** Add `<key>com.apple.security.device.camera</key><true/>` to `JamWide.entitlements`. Add `CAMERA_PERMISSION_ENABLED TRUE` + `HARDENED_RUNTIME_OPTIONS` to the `juce_add_plugin` block. Augment the codesign foreach loop in `CMakeLists.txt:244-269` to (a) sign each ffmpeg dylib individually with the camera entitlement BEFORE bundle signing, (b) `install_name_tool -change` to rewrite dylib load paths to `@loader_path/../Frameworks/libX.Y.dylib`, (c) re-sign the bundle with `--deep --strict`. RESEARCH §3 "macOS codesigning" notes this is fundamentally NEW territory — JamWide has never embedded a non-statically-linked dylib before.
- **Phase size estimate:** **2 plans** — Plan G.1: entitlement + JUCE plumbing (≈100 LOC + a lot of testing). Plan G.2: per-dylib codesign + Frameworks/ rewriting (≈300 LOC of CMake post-build steps; potentially a separate `cmake/codesign-frameworks.cmake`).
- **Dependencies:** Item B (need vendored dylibs to sign) + Item C (need camera-using code to require the entitlement).
- **File:line provenance from RESEARCH §6:**
  - `JamWide.entitlements:5-10` — current entitlements; `com.apple.security.device.camera` key missing.
  - `CMakeLists.txt:145-162` — `juce_add_plugin` block; needs `CAMERA_PERMISSION_ENABLED TRUE` + `HARDENED_RUNTIME_OPTIONS`.
  - `CMakeLists.txt:244-269` — current codesign foreach; needs per-dylib pre-pass + `install_name_tool` rewriting.
- **Spike-relevant findings:** Spike does NOT touch entitlements, codesigning, or `juce_add_plugin` block (per CONTEXT.md scope discipline). Q4 (CLAP plugin in `FORMATS` line) and Q5 (universal binary stitching) are flagged below as DEFERRED-TO-MILESTONE for this item.

### H. Remove VDO.Ninja stack (file deletes per RESEARCH §6 list + cmake cleanup)

- **Goal:** Delete `juce/video/VideoCompanion.{h,cpp}`, `juce/video/VideoPrivacyDialog.{h,cpp}`, `juce/video/BrowserDetect_*.{h,cpp,mm}`, `companion/` directory, `tests/test_video_sync.cpp`. Remove `ixwebsocket` link if not used elsewhere (`grep -r ixwebsocket src/ juce/` to confirm). Remove `videoCompanion` member from `JamWideJuceProcessor`. Replace `videoPrivacyDialog` setup in `JamWideJuceEditor` with the new camera-permission UX (already implemented in item C).
- **Phase size estimate:** **1 plan** — purely subtractive; ~10 file deletes + ~5 cmake edits + a `git grep` audit to catch any orphan references (≈200 LOC of edits, mostly in `-` form).
- **Dependencies:** Items C + F (the new in-app/in-plugin video stack must be functional before VDO.Ninja can be torn out — otherwise users lose video entirely).
- **File:line provenance from RESEARCH §6:**
  - Files to remove: full list at RESEARCH §6 "Files to remove (full list)".
  - CMakeLists.txt edits: lines `134-139` (ixwebsocket section), `189-199` (target_sources for video files), `217` (link to ixwebsocket), `358-360` (test_video_sync target).
  - JamWideJuceProcessor edits: lines `:119` (header member), `:61` (constructor init), `:69-72` (destructor cleanup) — all in `juce/JamWideJuceProcessor.{h,cpp}`.
  - JamWideJuceEditor edits: lines `:5-6` (includes), `:106-144` (privacyDialog setup), `:186-189,229-230,343-352,441-460,533-535` (videoCompanion call sites).
  - ConnectionBar edits: `juce/ui/ConnectionBar.cpp:8` (include), `:512-528` (videoCompanion->isActive/deactivate references).
- **Spike-relevant findings:** Spike does NOT touch any of these files (per CONTEXT.md scope discipline). The deletion list is documented but unexecuted.

### I. Per-DAW UAT (Logic Pro, REAPER, Ableton Live, Bitwig)

- **Goal:** Manual UAT in each DAW: load JamWideJuce VST3 (and AU on macOS, CLAP if available), join a populated NINJAM session, click camera button, confirm video sends + receives. **REAPER is expected to fail** per RESEARCH §5 (SPARTA Issue #82) — REAPER doesn't request `com.apple.security.device.camera` for itself, so the plugin's camera-open call returns null. The fallback UX (Item C plan C.2) must catch this gracefully.
- **Phase size estimate:** **1 plan** — pure manual testing + documentation of per-DAW behavior; the deliverable is a results matrix in `.planning/phases/<XX>-jamtaba-video/per-DAW-UAT.md` (≈100 LOC of markdown).
- **Dependencies:** ALL prior items (B–H) — UAT is the last gate before merge.
- **File:line provenance from RESEARCH §6:**
  - RESEARCH §5 "DAW behavior" table — Logic Pro: likely works; REAPER: known fail (SPARTA #82); Live/Bitwig: extrapolated unknowns.
  - Any DAW that "passes" still requires probing for the corner cases in RESEARCH §7 "JUCE camera permission may not exist for plugins on macOS" (NSCameraUsageDescription is read from the *host* process, not the plugin bundle).
- **Spike-relevant findings:** Spike is **standalone-only** (RESEARCH §8 Option (a) explicit scope). Plugin-context behavior is UNVERIFIED. Q4 below tracks the CLAP-specific subset.

### K. Linux V4L2 capture path (post-v1, separate phase) [NEW-2026-05-15]

- **Goal:** Add direct V4L2 (`/dev/video0`) capture to JamWide so Linux users can broadcast video. JUCE's `juce_video` module has no Linux camera backend (`libs/juce/modules/juce_video/native/` lists only `juce_CameraDevice_{android,ios,mac,windows}.h` — confirmed by file-listing — and the file `juce_CameraDevice_linux.h` does not exist). The receive path works fine on Linux via libavcodec; only capture is missing.
- **Phase size estimate:** **1 plan** — purely additive Linux-specific code with a `JamWideCameraDevice` abstraction wrapping JUCE's `juce::CameraDevice` on macOS/Windows and a new V4L2 implementation on Linux. ≈300 LOC for V4L2 enumerate / open / format-negotiate / mmap-buffer-capture loop + ≈100 LOC for the abstraction shim + ≈150 LOC for tests against the V4L2 loopback (`v4l2loopback` kernel module gives a synthetic camera for CI).
- **Dependencies:** Items B + C + D + E + F (full receive + send pipeline must work on macOS/Windows first). Item K is fundamentally Linux-only; doesn't block other platforms.
- **File:line provenance / references:**
  - JUCE camera abstraction reference: `libs/juce/modules/juce_video/capture/juce_CameraDevice.h` (cross-platform API surface to mirror)
  - Reference V4L2 wrapper to study: ffmpeg's `libavdevice/v4l2.c` (already in our vendored ffmpeg if we vendored libavdevice; otherwise read from upstream source) — but DO NOT rely on libavdevice at runtime (LGPL configure flag may not include it). Write a slim direct V4L2 wrapper instead.
  - Linux build target: ffmpeg vendoring for Linux (Item B sub-task) lands the cross-platform decoder; capture is K-specific addition.
- **Spike-relevant findings:** N/A — spike is macOS-x86_64 only.
- **Defer rationale:** Linux capture is a single-platform engineering exercise that doesn't gate the macOS/Windows v1 ship. Linux *receive* IS in scope for v1 (just decode + display, no capture). UX gracefully shows "Video send unavailable on Linux" until Item K lands.

### J. Server-side adaptation (ninjamzap-server fork integration) [NEW-2026-05-15]

- **Goal:** Ensure JamWide users have a server they can deploy that supports the NinjamZap video transport with per-room threading, two-pass audio-priority processing, and per-subscriber video congestion drop. Three sub-options under Q8:
  - **(a)** JamWide ships its own ninjamzap-server fork (e.g. `JamWide/jamwide-server`) with a Dockerfile + deploy guide. Max control, max maintenance.
  - **(b)** Contribute JamWide-specific changes upstream to ninjamzap-server and have JamWide release notes pin to a ninjamzap-server tag.
  - **(c)** Run upstream ninjamzap-server unmodified; JamWide ships a "recommended server" doc.
- **Phase size estimate:** **2–3 plans** depending on Q8 outcome.
  - If (a) JamWide fork: Plan J.1 fork repo + CI integration (≈300 LOC of CMake + Dockerfile + GH Actions). Plan J.2 wire any JamWide-specific server tweaks (auth, room defaults). Plan J.3 deployment guide + release-notes pinning.
  - If (b) upstream contribution: Plan J.1 upstream PR(s). Plan J.2 release-notes pin + JamWide-bundled Docker compose example.
  - If (c) document-only: Plan J.1 a `docs/SERVER.md` linking to ninjamzap-server install instructions + the `AllowVideoChannels yes` + `PrivateGroupMode N` minimum config.
- **Dependencies:** Q8 user decision must come first. Item F (client transport) does not depend on J (J only affects server-deploy story; vanilla NINJAM is wire-compatible just slower under video load).
- **File:line provenance:**
  - Reference server source: `/Users/cell/dev/ninjamzap-server/` (full tree)
  - Reference Dockerfile: `/Users/cell/dev/ninjamzap-server/Dockerfile`
  - Reference deploy script: `/Users/cell/dev/ninjamzap-server/deploy.sh`
  - Reference config template: `/Users/cell/dev/ninjamzap-server/configs/` + `ninjam/server/example.cfg`
  - Reference docs: `/Users/cell/dev/ninjamzap-server/docs/VIDEO_SUPPORT.md` (1500+ words on architecture, sizing, troubleshooting — copy/adapt as JamWide's server doc)
- **Spike-relevant findings:** N/A — spike is client-only.

---

## Section 2: Open Questions (RESEARCH §9 Q1–Q7 + 2026-05-15 additions Q8–Q13)

| Q | Question (verbatim from RESEARCH §9) | Disposition | Notes |
|---|--------------------------------------|-------------|-------|
| Q1 | JUCE seat license coverage of `juce_video` | DEFERRED-TO-MILESTONE → Item C (capture module) | The spike compiles against `juce::juce_video` (AGPLv3 / Commercial header) on the assumption that JamWide's existing JUCE commercial seat covers it. Milestone planner MUST confirm with the user before Item C lands; if not covered, the milestone replans with a direct AVFoundation/MediaFoundation capture path (≈+1 plan, ≈+800 LOC). |
| Q2 | Existing video button — keep or remove in this task? | DEFERRED-TO-MILESTONE → Item H (VDO.Ninja removal) | Spike does NOT touch the button. Item H rewires it to toggle local capture, simultaneously removing the VDO.Ninja launch handler. |
| Q3 | NINJAM `m_netcon->Send` thread safety | RESOLVED-BY-SPIKE (partial) → see `260515-0pc-spike-results.md` "Open question §9-Q3" | Spike performed read-only inspection of `wdl/jnetlib/connection.cpp` and documents the Send thread-safety guarantee in `260515-0pc-spike-results.md`. Item F.1 (send-path plan) MUST honor that finding; if Send is NOT thread-safe, F.1 wraps it in a per-NJClient mutex or routes through a single-producer queue. |
| Q4 | CLAP plugin in `FORMATS` line | DEFERRED-TO-MILESTONE → Item G (entitlements + codesigning) | The CLAP wrapper repackages the JUCE plugin into a different bundle structure. Item G.2 must verify that the CLAP wrapper's `Contents/Frameworks/` (if it has one) gets the ffmpeg dylibs + codesigned identically to VST3/AU/Standalone. |
| Q5 | Universal binary stitching for ffmpeg dylibs | DEFERRED-TO-MILESTONE → Item B (cross-platform vendoring) | Spike vendored x86_64-mac only. Item B.1 adds the lipo step in `scripts/build_ffmpeg_lgpl.sh` to merge arm64-mac + x86_64-mac dylibs into fat binaries. CI workflow may need a new step that runs the build twice (once per arch) and lipos. |
| Q6 | Bundle size budget | DEFERRED-TO-MILESTONE → Item B (cross-platform) + Item G (entitlements) | Spike measured macOS-x86_64 dylib bundle size in `260515-0pc-spike-results.md`. Universal-binary number (Item B) doubles that. If exceed budget, Item G.2 may need to investigate the "shared `~/Library/Application Support/` location" alternative from RESEARCH §9-Q6. |
| Q7 | `JTBv` fourCC ASCII byte order | OBSOLETE → see Q7' below | Original Q7 was specific to JamTaba. After 2026-05-15 NinjamZap pivot, fourCC is `H264` not `JTBv`. The spike-confirmed property — `MAKE_NJ_FOURCC` is byte-order-correct for any 4-char ASCII fourCC — still holds; just substitute `H264` in F.1. |
| Q7' | `H264` fourCC ASCII byte order (NinjamZap) | RESOLVED → analogous to Q7 | `MAKE_NJ_FOURCC('H','2','6','4')` produces wire bytes `0x48,0x32,0x36,0x34` matching `ninjamzap-core/tests/video-sync/harness/TestClient.cpp:88-90` which builds the literal `('H') | ('2'<<8) | ('6'<<16) | ('4'<<24)` directly. Use `MAKE_NJ_FOURCC` macro verbatim. |
| Q8  | NinjamZap-server: own fork, contribute upstream, or run unmodified? [NEW-2026-05-15] | NEEDS-USER-DECISION | User has access to `/Users/cell/dev/ninjamzap-server`. Three options affect maintenance + integration model: (a) JamWide ships its own ninjamzap-server fork (max control, max maintenance burden); (b) Contribute JamWide-specific changes upstream and run upstream (lowest maintenance, requires upstream cooperation); (c) Run upstream unmodified, document version-pinning (zero maintenance, can't customise). Decide before Item J. |
| Q9  | What channel index for video? [NEW-2026-05-15] | DEFERRED-TO-MILESTONE → Item F.1 | NinjamZap convention is `chidx=1`. JamWide may have local-channel mapping conflicts; audit `MAX_LOCAL_CHANNELS` and existing channel-index assignments before committing to chidx=1. |
| Q10 | How does JamWide represent the `0x10` video flag in channel info? [NEW-2026-05-15] | DEFERRED-TO-MILESTONE → Item F.1 | NinjamZap uses bit `0x10` of the channel-info flags field. Confirm `SetLocalChannelInfo`'s flags arg is plumbed through and bit `0x10` is unclaimed by other JamWide-specific channel uses. |
| Q11 | Receive-pipeline memory budget under HD video [NEW-2026-05-15] | NEEDS-MEASUREMENT-IN-MILESTONE → Item F.2 | 4-stage pipeline allocates `WDL_HeapBuf` per stream per slot. Worst-case estimate: 800 KB/interval × 4 slots × 6 peers = 19 MB. Compare against existing per-peer audio decode buffer footprint; flag if it bumps total memory >2× current. May force a streaming-decode design instead of buffer-the-whole-frame. |
| Q12 | Is `kHoldCapDrop = 4` the right value for plugin contexts? [NEW-2026-05-15] | DEFERRED-TO-MILESTONE → Item F.3 | NinjamZap's value was tuned for iOS/Android jam-room use. DAW host audio output buffers may be larger or differently shaped than mobile, which could shift the optimal hold cap. Empirically tune during plugin-context UAT (Item I). |
| Q13 | Plan VideoToolbox (macOS-arm64) hardware codec from day one, or land openh264 first? [NEW-2026-05-15] | DEFERRED-TO-MILESTONE → Item D | Spike Risk #3 noted Cisco openh264 v2.1.1 is the LAST mac prebuilt, and v2.2.0+ requires building from source which forfeits MPEG-LA royalty payment. macOS-arm64 has VideoToolbox built in. Decide whether to architect Item D's encoder around an abstract `VideoEncoder` interface from day one (allowing both backends) or hardcode openh264 and refactor later. |

---

## Section 3: Locked Decisions to Honor

These are copied verbatim from `260515-0pc-CONTEXT.md` so the milestone planner does not accidentally re-litigate them. **Any milestone plan that contradicts one of these MUST first call `/gsd-discuss-phase` to revisit the decision with the user.**

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
- Authoritative spec: `260515-0pc-RESEARCH-ADDENDUM.md` "Wire format spec" section.

### Server compatibility [NEW-2026-05-15]
- **Target the ninjamzap-server fork as the reference server.** Vanilla NINJAM works (server is opaque relay) but lacks per-room threading, two-pass audio-priority processing, and per-subscriber video-frame congestion drop. Without those, audio quality degrades when video bandwidth spikes.
- JamWide hosts/users running their own server should run ninjamzap-server. Document this in the milestone-shipping README.

### UI rendering model [NEW-2026-05-15]
- **Native rendering only — popout `juce::DocumentWindow` per user + grid `juce::Component` in the main view.** No browser companion in v1.
- Decoded H.264 frames are owned by JamWide (`juce::Image`); we render them in any combination of grid (one tile per remote user inside the plugin/standalone window) + per-user popouts (multi-monitor friendly) + both at once if the user toggles.
- The two strengths of the existing VDO.Ninja browser companion (multi-monitor grid + per-user windows) are preserved in the native UI without the WebSocket+HTML/TS dependency surface.

### Audio codec [CLARIFIED-2026-05-15]
- **Stay on OGGv (Vorbis-in-Ogg)** for the native-video milestone. Do NOT entangle the Opus migration with this work.
- ninjamzap-server and ninjamzap-core support only OGGv on the wire (verified by exhaustive grep of both repos — only `WDL/metadata.h:688` mentions "opus" and that's just an audio file-extension recognizer for ID3-style metadata reads, NOT a NINJAM transport codec).
- JamWide's own roadmap tracks Opus separately as **Phase 16 "Opus Codec Integration"** under the v1.2 "Security & Quality" milestone (see `.planning/ROADMAP.md`). That work is independent.

### Cross-platform support [NEW-2026-05-15]
- **macOS + Windows: full send + receive parity from v1.** Both have working JUCE `CameraDevice` backends (`libs/juce/modules/juce_video/native/juce_CameraDevice_{mac,windows}.h`) and openh264 builds.
- **Linux: receive-only in v1.** JUCE's `juce_video` module has no Linux camera backend. Linux users can decode + view incoming video; cannot capture + broadcast.
- **Linux capture: deferred to Item K (post-v1, single phase).**

---

## Section 4: Cross-References

- Spike disposition (GREEN/YELLOW/RED) and measured-numbers report: `260515-0pc-spike-results.md`
- Locked decisions (this document copies them verbatim): `260515-0pc-CONTEXT.md`
- Full research with file:line citations: `260515-0pc-RESEARCH.md`
- This task's plan (executed by quick task 260515-0pc): `260515-0pc-PLAN.md`
- Milestone seed command: `/gsd-new-milestone` (will pull this file as authoritative scope)

---

## Section 5: Total milestone size estimate

Original sum (JamTaba design): B=2, C=2, D=2, E=2, F=3, G=2, H=1, I=1 = **15 plans across ~7 phases** at ~600 LOC/plan = ~9000 LOC.

**REVISED-2026-05-15 sum (NinjamZap design):** B=2, C=2, D=2, E=2, **F=5** (+2 vs original to cover the 4-stage pipeline + GUID decision tree + thread-safety mitigation), G=2, H=1, I=1, **J=2** (NEW server-side adaptation, mid-size). New total: **19 plans across ~8 phases** at ~600 LOC/plan = ~11,400 LOC. The +2,400 LOC overhead pays for the GUID-pairing sync that JamTaba lacks (and that fixes the "video-1-interval-early" bug repro'd in `ninjamzap-core/tests/video-sync/scenarios/02_video_one_interval_early.cpp`) plus a recommended-server story.

Compare against the reference: NinjamZap-core's video module is ~1500 LOC of njclient changes (lines 200-417 of njclient.h + lines 643-3219 video-related additions in njclient.cpp). JamWide gets the same functional surface plus the larger JamWide-specific overhead of platform vendoring (B), entitlements (G), removal (H), and server adaptation (J).
