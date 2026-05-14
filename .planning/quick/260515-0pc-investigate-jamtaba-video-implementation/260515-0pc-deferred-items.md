---
quick_id: 260515-0pc
slug: investigate-jamtaba-video-implementation
purpose: milestone-handoff
created: 2026-05-15
---

# Quick Task 260515-0pc — Deferred Items (Milestone Handoff)

> **Source:** Authoritative scope catalog for the JamTaba-video milestone (created by quick task 260515-0pc).
> See sibling files: 260515-0pc-CONTEXT.md (locked decisions), 260515-0pc-RESEARCH.md (full research), 260515-0pc-spike-results.md (measured spike evidence).
> To start the milestone: `/gsd-new-milestone` and seed it from this file.

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

### F. NINJAM channel wiring (`JTBv` fourCC begin/write on send + intercept on receive)

- **Goal:** On the send side, add `MAKE_NJ_FOURCC('J','T','B','v')` upload-interval-begin + chunked write from the new video encoder, on a per-user chidx=1 (RESEARCH §1 "Channel-naming convention"). On the receive side, intercept `JTBv` fourcc at `src/core/njclient.cpp:2148` BEFORE `start_decode` to prevent feeding H.264 bytes to Vorbis (RESEARCH §6 documents this as a wire-compatibility BUG today: JamWide unchanged would silently corrupt video into Vorbis).
- **Phase size estimate:** **3 plans** — Plan F.1: send path (`m_netcon->Send` thread-safety probe per Q3, then a parallel video upload state machine that doesn't sit inside `NJClient::Run()`'s tight audio loop — ≈400 LOC). Plan F.2: receive path interception + per-(user, GUID) assembly buffer (≈500 LOC). Plan F.3: TDD plan covering the wire-format compatibility against a captured JamTaba bytestream (≈200 LOC tests + golden bytestream sample).
- **Dependencies:** Items B + D + E (encoder produces bytes, decoder consumes them, NINJAM moves them between peers).
- **File:line provenance from RESEARCH §6:**
  - `src/core/njclient.cpp:154-155` — existing `MAKE_NJ_FOURCC('O','G','G','v')` and `'F','L','A','C'` patterns; new `'J','T','B','v'` define added here.
  - `src/core/njclient.cpp:2407-2413,2468-2474,2504-2530,2546-2569` — current INTERVAL BEGIN/WRITE patterns to model the video send path after.
  - `src/core/njclient.cpp:2105-2189,2148,2220-2259` — current INTERVAL BEGIN/WRITE receive path; line 2148 is where `JTBv` interception goes BEFORE `start_decode` (RESEARCH §6 documents the wire-compat bug).
  - `src/core/mpb.h:240` — `mpb_client_upload_interval_begin.fourcc` field (already exists, just reused).
- **Spike-relevant findings:** Spike does NOT touch any networking code (per CONTEXT.md scope discipline). Q7 (`MAKE_NJ_FOURCC` byte order vs JamTaba's literal `fourCC[N]=` assignment) is RESOLVED-BY-SPIKE in `260515-0pc-spike-results.md` — confirms `'J','T','B','v'` little-endian byte order matches JamTaba's wire format.

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

---

## Section 2: Open Questions (RESEARCH §9 Q1–Q7)

| Q | Question (verbatim from RESEARCH §9) | Disposition | Notes |
|---|--------------------------------------|-------------|-------|
| Q1 | JUCE seat license coverage of `juce_video` | DEFERRED-TO-MILESTONE → Item C (capture module) | The spike compiles against `juce::juce_video` (AGPLv3 / Commercial header) on the assumption that JamWide's existing JUCE commercial seat covers it. Milestone planner MUST confirm with the user before Item C lands; if not covered, the milestone replans with a direct AVFoundation/MediaFoundation capture path (≈+1 plan, ≈+800 LOC). |
| Q2 | Existing video button — keep or remove in this task? | DEFERRED-TO-MILESTONE → Item H (VDO.Ninja removal) | Spike does NOT touch the button. Item H rewires it to toggle local capture, simultaneously removing the VDO.Ninja launch handler. |
| Q3 | NINJAM `m_netcon->Send` thread safety | RESOLVED-BY-SPIKE (partial) → see `260515-0pc-spike-results.md` "Open question §9-Q3" | Spike performed read-only inspection of `wdl/jnetlib/connection.cpp` and documents the Send thread-safety guarantee in `260515-0pc-spike-results.md`. Item F.1 (send-path plan) MUST honor that finding; if Send is NOT thread-safe, F.1 wraps it in a per-NJClient mutex or routes through a single-producer queue. |
| Q4 | CLAP plugin in `FORMATS` line | DEFERRED-TO-MILESTONE → Item G (entitlements + codesigning) | The CLAP wrapper repackages the JUCE plugin into a different bundle structure. Item G.2 must verify that the CLAP wrapper's `Contents/Frameworks/` (if it has one) gets the ffmpeg dylibs + codesigned identically to VST3/AU/Standalone. |
| Q5 | Universal binary stitching for ffmpeg dylibs | DEFERRED-TO-MILESTONE → Item B (cross-platform vendoring) | Spike vendored x86_64-mac only. Item B.1 adds the lipo step in `scripts/build_ffmpeg_lgpl.sh` to merge arm64-mac + x86_64-mac dylibs into fat binaries. CI workflow may need a new step that runs the build twice (once per arch) and lipos. |
| Q6 | Bundle size budget | DEFERRED-TO-MILESTONE → Item B (cross-platform) + Item G (entitlements) | Spike measured macOS-x86_64 dylib bundle size in `260515-0pc-spike-results.md`. Universal-binary number (Item B) doubles that. If exceed budget, Item G.2 may need to investigate the "shared `~/Library/Application Support/` location" alternative from RESEARCH §9-Q6. |
| Q7 | `JTBv` fourCC ASCII byte order | RESOLVED-BY-SPIKE → see `260515-0pc-spike-results.md` "Open question §9-Q7" | Spike confirms `MAKE_NJ_FOURCC('J','T','B','v')` produces wire bytes `0x4A,0x54,0x42,0x76` matching JamTaba's `fourCC[0]='J'; fourCC[1]='T'; ...` direct assignment. Item F.1 can use the macro verbatim. |

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

### Wire format
- **Bit-for-bit JamTaba-compatible.** Same channel-naming convention, same codec params, same interval framing.
- Goal: JamTaba and JamWide users see each other's video in mixed sessions on vanilla NINJAM servers, no protocol extension required.

---

## Section 4: Cross-References

- Spike disposition (GREEN/YELLOW/RED) and measured-numbers report: `260515-0pc-spike-results.md`
- Locked decisions (this document copies them verbatim): `260515-0pc-CONTEXT.md`
- Full research with file:line citations: `260515-0pc-RESEARCH.md`
- This task's plan (executed by quick task 260515-0pc): `260515-0pc-PLAN.md`
- Milestone seed command: `/gsd-new-milestone` (will pull this file as authoritative scope)

---

## Section 5: Total milestone size estimate

Summing the per-item phase sizes (B=2, C=2, D=2, E=2, F=3, G=2, H=1, I=1) = **15 plans across ~7 phases** (item I is a single UAT plan; items B–H are 2 plans each with item F at 3). At an average of ~600 LOC per plan, that is ~9000 LOC of net change (additions + deletions) across the whole milestone. This matches RESEARCH §0 "Summary" claim that the JamTaba video module is ~1100 LOC of `src/Common/video/` — JamWide gets the same functional surface, plus the larger JamWide-specific overhead of platform vendoring (B), entitlements (G), and removal (H).
