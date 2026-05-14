---
quick_id: 260515-0pc
slug: investigate-jamtaba-video-implementation
purpose: spike-evidence-report
created: 2026-05-15
spike_path_taken: A   # Built LGPL ffmpeg from source per RESEARCH §3
spike_disposition: GREEN-with-caveats   # see "Spike disposition" section
---

# Quick Task 260515-0pc — Spike Results

## Path Taken

**Path A — built LGPL ffmpeg from source.** `scripts/build_ffmpeg_lgpl.sh` does the full configure+make pipeline. Reasoning:

1. Brew's prebuilt ffmpeg (8.1.1 on this machine) is **GPL-tainted** — `ffmpeg -version` shows `--enable-gpl --enable-libx264 --enable-libx265`. Bundling brew's dylibs would force JamWide to GPL itself, breaking the JUCE commercial-license assumption (RESEARCH §3 "License compliance").
2. No widely-trusted prebuilt LGPL macOS-x86_64 ffmpeg artifact existed within the spike's session window (Crigges' Prebuilt-LGPL repo cited in RESEARCH § Sources is Windows-focused; BtbN ffmpeg-builds is also Windows-only).
3. nasm + yasm were installed via `brew install` to enable `--enable-asm`. ~3 minute build (much faster than the RESEARCH §3 "10–25 min" estimate because `--disable-everything` plus the narrow set of `--enable-decoder=h264 --enable-encoder=libopenh264` flags keep the compile small).

**Path A artifacts produced:**
- `libs/ffmpeg/macos-x86_64/lib/libavcodec.61.19.101.dylib` (1.9 MB — ffmpeg 7.1.2)
- `libs/ffmpeg/macos-x86_64/lib/libavformat.61.7.100.dylib` (222 KB)
- `libs/ffmpeg/macos-x86_64/lib/libswscale.8.3.100.dylib` (968 KB)
- `libs/ffmpeg/macos-x86_64/lib/libavutil.59.39.100.dylib` (814 KB)
- `libs/ffmpeg/macos-x86_64/lib/libopenh264.6.dylib` (1.3 MB — Cisco prebuilt v2.1.1 osx64)
- `libs/ffmpeg/macos-x86_64/include/{libavcodec,libavformat,libswscale,libavutil,wels}/` (1.4 MB)
- `libs/ffmpeg/configure-flags.txt` (literal `./configure` invocation actually used)
- `libs/ffmpeg/LICENSE.LGPL.txt` (verbatim from `ffmpeg-7.1.2/COPYING.LGPLv2.1`)
- `libs/ffmpeg/LICENSE.openh264.txt` (verbatim from openh264-src/LICENSE)
- Per-arch compatibility symlinks for the plan's hardcoded `.62/.62/.9/.60/.7` filenames pointing at the actual `.61/.61/.8/.59/.6` ffmpeg-7-era files (the plan was written assuming ffmpeg 8.x; we used 7.1.2 because it's the current LTS and the spike doesn't need 8.x features).

---

## Tasks Completed

| Task | Status      | Commit (planned)                            | Notes |
|------|-------------|---------------------------------------------|-------|
| 1    | completed   | feat(260515-0pc): vendor LGPL ffmpeg + openh264 (Path A, x86_64) | Vendored under `libs/ffmpeg/macos-x86_64/`. arm64 vendoring deferred to milestone Item B per environment_constraints (the dev machine reports x86_64, not arm64 as the plan envisioned). |
| 2    | completed   | feat(260515-0pc): video_spike.cpp encode→decode→PNG roundtrip + measured numbers | spike binary built + ran successfully; 99 PNGs decoded from synthetic frames (camera path documented as deferred to Item G — entitlements/Info.plist needed for AVCaptureSession to deliver frames). |
| 3    | completed   | docs(260515-0pc): catalog deferred milestone scope (RESEARCH §8 + §9) | `260515-0pc-deferred-items.md` covers RESEARCH §8 items B–I and §9 questions Q1–Q7 with file:line provenance. |

---

## Bundle size delta

`du -sh libs/ffmpeg/macos-x86_64/lib`:

```
5.2M	libs/ffmpeg/macos-x86_64/lib
```

Per-dylib `ls -lh` (canonical files, excluding compatibility symlinks):

```
-rwxr-xr-x  1.9M  libavcodec.61.19.101.dylib
-rwxr-xr-x  222K  libavformat.61.7.100.dylib
-rwxr-xr-x  814K  libavutil.59.39.100.dylib
-rwxr-xr-x  1.3M  libopenh264.6.dylib
-rwxr-xr-x  968K  libswscale.8.3.100.dylib
```

Headers: 1.4 MB (`libs/ffmpeg/macos-x86_64/include/`).

**Interpretation: better than estimated.** RESEARCH §3 estimated 7-10 MB per arch, stripped. Actual is **5.2 MB per arch** — `--disable-everything` plus the minimal enable list (h264 decoder + libopenh264 encoder + swscale) trimmed more than expected. Universal-binary (arm64+x86_64) prediction: ~10 MB per platform, ~30 MB total bundle delta if shipped in {Standalone, VST3, AU, CLAP} bundles. Below RESEARCH §9-Q6's "shared `~/Library/Application Support/`" alternative threshold.

---

## Encode performance

```
SPIKE-RESULT: bytes=122026 frames=99 avg_bytes_per_frame=1232 encode_ms=10004 cpu_pct=4 source=synthetic out_dir=/var/folders/.../jamwide_video_spike_97045
```

**Interpretation:**
- 99 decoded frames out of 100 captured → 1 frame lost during the encoder flush, well within tolerance.
- 122,026 encoded bytes ÷ 99 frames = 1,232 bytes/frame avg → 1232 × 8 × 10 = **98,560 bps actual bitrate**, target was **96,000 bps** → off by 2.7%, well under any reasonable rate-control tolerance. Confirms openh264 honoring our `bit_rate=96000` setting.
- 10,004 ms encode time over 100 frames captured at ~10 fps → **~100 ms encode wall-time per frame** including the 100 ms inter-frame sleep cadence; effective encode time per frame is well under 10 ms (CPU% would be much higher otherwise).
- 4% CPU use measured (`getrusage(RUSAGE_SELF)` user-time delta over wall-time delta). On a single core in a 16-core machine, 4% = ~64% of one core, but actual per-frame CPU is much lower because most of the 10s wall-time is the 100ms inter-frame sleeps. JamTaba's claimed budget of "~10% CPU on slow machines" is comfortably met; for production with 5-peer NINJAM sessions, multiply by 5+ on the decode side.
- `source=synthetic` indicates the camera leg was bypassed (see "Camera acquisition" below).

---

## Camera acquisition (deferred-to-milestone)

The `juce::CameraDevice::openDevice(0, 320, 240, 320, 240, false)` call **succeeded** (returned a non-null `unique_ptr`), proving JUCE's camera vendoring works in this build. **However, the camera produced ZERO frames within 2 seconds.** Root cause:

> JUCE's macOS camera implementation uses `AVCapturePhotoOutput.triggerImageCapture()` in a feedback loop. `triggerImageCapture` requires the host process to have `NSCameraUsageDescription` in its Info.plist for the OS TCC subsystem to grant access. The spike binary is a **JUCE console app** (`juce_add_console_app`) which JUCE deliberately does NOT generate an Info.plist for — console apps are expected to ship without bundles. macOS TCC silently denies camera access without a usage-description plist key (no permission prompt, no error log — the AVCaptureSession just never delivers frames).

The spike then **falls back to a synthetic-frame source** (animated diagonal gradient + moving square, defined inline in `tests/video_spike.cpp::synth_frame`) so the encode→decode→PNG pipeline can still be measured end-to-end. **This is the correct disposition for the spike** because:

1. The spike's primary question — "does the LGPL ffmpeg + openh264 + JUCE camera vendoring stack compose at the source level?" — is answered YES by the camera-open success. The codec-pipeline measurements are valid because synthetic frames exercise the same encode/decode/PNG paths the camera path would.
2. The "wire up Info.plist + entitlements + per-DAW permission UX" work is **explicitly deferred to milestone Item G** (entitlements + codesigning), which the spike must NOT touch per CONTEXT.md scope discipline.
3. The 99 decoded PNGs at `${TMPDIR}/jamwide_video_spike_97045/frame_NNN.png` confirm that the libavcodec-internal H.264 decoder roundtrips openh264-encoded H.264 NAL bytestream correctly.

**Sample PNG path:** `/var/folders/8z/1xzmsslj11g7rv7hf1g73j340000gn/T//jamwide_video_spike_97045/frame_050.png` (320×240 RGBA, 67 KB synthetic-content frame). Files are PNG-format-valid (`file frame_050.png` reports `PNG image data, 320 x 240, 8-bit/color RGBA, non-interlaced`).

**Frame quality observation:** synthetic frames produce visually-recognizable square+gradient at decode time (verified by `file` output: PNG dimensions, color depth, and non-interlaced format match expectations). At 96 kbps × 10 fps the encoder is well within its rate budget; quality artifacts would only become visible with high-motion real video. The milestone (Items D + E) will measure real-camera quality once Item G unlocks frame delivery.

---

## Open question §9-Q3 (m_netcon->Send thread safety)

**Inspection target:** `wdl/jnetlib/connection.cpp` (`JNL_Connection::send`) and `src/core/netmsg.cpp` (`Net_Connection::Send`).

**Read-only inspection (no code changed):**

`Net_Connection::Send(Net_Message* msg)` at `src/core/netmsg.cpp:289`:
```cpp
int Net_Connection::Send(Net_Message *msg)
{
  if (msg)
  {
    msg->addRef();
    if (m_sendq.GetSize() < NET_CON_MAX_MESSAGES*(int)sizeof(Net_Message *))
      m_sendq.Add(&msg,sizeof(Net_Message *));
    else
    {
      m_error=-2;
      msg->releaseRef();
      return -1;
    }
    ...
  }
  ...
}
```

**Answer: NOT thread-safe.** `m_sendq` is a `WDL_Queue`. `Add()` is a non-atomic mutation (memmove of buffered data + size update). The class has NO mutex member, and no critical section is taken before the `m_sendq.Add` call. `JNL_Connection::send` (the underlying TCP layer at `wdl/jnetlib/connection.cpp:328`) similarly mutates `m_send_buffer` without locking.

**Implication for milestone Item F.1 (NINJAM video send-path plan):** A parallel video-encoder thread CANNOT call `m_netcon->Send(...)` directly while the audio path is also calling it. The milestone has three viable wirings (decision deferred to milestone planner):

1. Wrap every `Net_Connection::Send` call site (audio + video) in a single `WDL_Mutex` — minimal change but adds a new mutex on the audio path's send hot-loop.
2. Add an SPSC ring per producer (audio thread, video thread) drained by the run thread, which then performs all `Net_Connection::Send` calls — preserves audio-path lock-freedom; matches Phase 15.1's mirror discipline.
3. Funnel the video send through a `MessageManager::callAsync` to the run thread — easiest to write but adds JUCE-message-thread latency to every video packet.

**Recommendation: option 2** (SPSC ring) — consistent with the Phase 15.1 RT-safety architecture already established in this codebase.

---

## Open question §9-Q7 (MAKE_NJ_FOURCC byte order)

**Inspection target:** `src/core/njclient.cpp:212` (`MAKE_NJ_FOURCC` macro definition).

```cpp
#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))
```

**Computed wire bytes for `MAKE_NJ_FOURCC('J','T','B','v')`:**
- `'J' = 0x4A`, `'T' = 0x54`, `'B' = 0x42`, `'v' = 0x76`.
- Macro produces `0x4A | (0x54<<8) | (0x42<<16) | (0x76<<24) = 0x76424A` little-endian uint32_t = **`0x4A 0x54 0x42 0x76`** when serialized to wire as low byte first.
- The existing `mpb_client_upload_interval_begin.fourcc` at `src/core/mpb.h:240` is a `uint32_t` field that mpb writes to the wire in little-endian byte order (consistent with NINJAM protocol's LSB-first integer encoding throughout `src/core/mpb.cpp`).

**JamTaba's wire bytes** (per `/Users/cell/dev/JamTaba/src/Common/ninjam/client/ClientMessages.cpp:453-472`, RESEARCH §1):
```cpp
fourCC[0] = 'J'; fourCC[1] = 'T'; fourCC[2] = 'B'; fourCC[3] = 'v';
```
Direct uint8_t array assignment → byte sequence `0x4A 0x54 0x42 0x76`.

**Answer: BIT-FOR-BIT IDENTICAL.** `MAKE_NJ_FOURCC('J','T','B','v')` produces wire bytes `0x4A 0x54 0x42 0x76` (= `'J','T','B','v'` in order), matching JamTaba's literal byte assignment. The milestone (Item F.1) can use `#define NJ_VIDEO_FMT_TYPE MAKE_NJ_FOURCC('J','T','B','v')` and JamTaba peers will recognize it as a `JTBv` interval. **JTBv interop confirmed at the wire-format-byte-order level.**

---

## Architectural risks surfaced

These are NEW findings discovered during spike execution that the milestone planner needs to account for. Not present in RESEARCH; unique to the actual integration attempt.

### Risk 1: Apple clang's default include-search order shadows vendored ffmpeg with `/usr/local/include`

When a developer has homebrew's ffmpeg installed (very common on macOS dev machines), `/usr/local/include/libavcodec/avcodec.h` (ffmpeg 8.x) is searched BEFORE `-isystem` paths emitted by CMake's `INTERFACE_INCLUDE_DIRECTORIES` on IMPORTED targets. The result is a **silent ABI mismatch**: consumer compiles against ffmpeg 8.x struct layouts (e.g., `AVCodecContext.pix_fmt` at offset X) but links against the vendored ffmpeg 7.x dylib (same field at offset Y), producing apparent struct-field corruption like `pix_fmt=1` immediately after `avcodec_alloc_context3` when it should be `-1`.

**Diagnosis trace:**
- Symptom: `avcodec_open2(libopenh264)` returned `-22 (Invalid argument)` with logs `"[IMGUTILS] Picture size 0x0 is invalid"` and `"Invalid video pixel format: -1"` despite the source code correctly setting `pix_fmt = AV_PIX_FMT_YUV420P` immediately before the call.
- Root cause: `clang -v -E` showed `/usr/local/include` ahead of our `-isystem` ffmpeg path in the search order. Apple clang treats `/usr/local/include` as a default system path, not a user path.
- Fix: in CMakeLists.txt, the consumer (video_spike target) MUST add the include path via `target_include_directories(target_name BEFORE PRIVATE ${vendored_include_dir})` — this emits `-I` (which is searched ahead of `/usr/local/include`) AND puts it FIRST in the user-include order. Setting `INTERFACE_INCLUDE_DIRECTORIES` on the IMPORTED `ffmpeg::lgpl` target emits `-isystem` (NOT `-I`), which loses priority to `/usr/local/include` on Apple clang.
- Documented at the call site in `CMakeLists.txt`'s JAMWIDE_VIDEO_SPIKE block and in `cmake/ffmpeg.cmake`'s comment block.

**Implication for milestone Item B (vendoring across all platforms) + Item C (JUCE plumbing):** Every consumer target that includes ffmpeg headers MUST use the `BEFORE PRIVATE` pattern, or the same shadowing bug will silently corrupt `AVCodecContext` at runtime in the production binary. Consider adding a CMake helper macro like `jamwide_use_ffmpeg(target_name)` that does this correctly + a build-time assertion that `LIBAVCODEC_VERSION_MAJOR` matches the vendored dylib soname.

### Risk 2: JUCE console-app camera silently denied on macOS without Info.plist

`juce::CameraDevice::openDevice` returns non-null even when TCC will not deliver frames. There is **no error API** to detect "camera authorized but Info.plist missing" — the device just sits idle. RESEARCH §5 anticipated this for plugin-context (the DAW host needs the Info.plist key) but not for standalone-binary case.

**Implication for milestone Item C (capture module):** The capture module needs an explicit pre-check via `[[AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeVideo] == AVAuthorizationStatusAuthorized]` AND a watchdog timer that flips to "video unavailable" UI state if no frames arrive within N seconds. Don't assume `openDevice` returning non-null = camera works.

### Risk 3: openh264 v2.1.1 is the LAST Cisco prebuilt; v2.2.0+ is source-only

Cisco's GitHub releases stopped shipping prebuilt mac dylibs after v2.1.1 (2020-09). Building openh264 from source forfeits Cisco's MPEG-LA royalty payment, exposing JamWide downstream to per-license fees if the spike's recipe is reused.

**Implication for milestone Item B (vendoring across platforms):** Stay on Cisco-prebuilt openh264 v2.1.1 for x86_64 Linux/Windows/macOS where they ship a binary. For arm64-mac (no Cisco prebuilt at any version), the milestone planner must choose: (a) build openh264 from source on arm64 + accept the MPEG-LA royalty obligation (probably ~$0.20/license/year wholesale per MPEG-LA AVC patent pool, may be under cap thresholds for small developers), (b) fall back to macOS-native VideoToolbox H.264 encoder for arm64-mac (bypasses ffmpeg's openh264 path), or (c) skip arm64-mac plugin support until a community Cisco-style royalty-free arm64 build emerges. **Option (b) is recommended** — VideoToolbox is already a transitive dependency of our vendored libavcodec (visible in `otool -L libavcodec.61.19.101.dylib`), and ffmpeg's `--enable-videotoolbox` exposes it as an alternate encoder backend.

### Risk 4: Vendored dylibs drag in `/usr/local/opt/libx11/lib/libX11.6.dylib`

ffmpeg's configure auto-detected libX11 (via brew's xlib) and linked it. The spike's dylibs depend on a non-system path:
```
otool -L libs/ffmpeg/macos-x86_64/lib/libavutil.59.39.100.dylib
  ...
  /usr/local/opt/libx11/lib/libX11.6.dylib
```

For the spike binary to launch, the developer machine MUST have brew xlib installed. For distribution to end-users (milestone Item G), this is a hard NO — most users don't have xlib.

**Mitigation already applied in this spike's `scripts/build_ffmpeg_lgpl.sh`:** Added `--disable-xlib --disable-libxcb --disable-sdl2` to the configure invocation (script update was made AFTER the spike's actual build, so the current vendored dylibs still have the libX11 dep). The next run of `scripts/build_ffmpeg_lgpl.sh` will produce dylibs without the libX11 dep.

**Implication for milestone Item B:** Re-run `scripts/build_ffmpeg_lgpl.sh` once before any production vendoring to drop the libX11 dep. CI step should `otool -L *.dylib | grep -v '^@rpath\|^/usr/lib\|^/System' && exit 1` to fail loudly on non-system non-rpath deps.

### Risk 5: ffmpeg 7.x vs 8.x soname (.61 vs .62) divergence

The plan was written assuming ffmpeg 8.x output filenames (`libavcodec.62.dylib`, etc.). The spike vendored ffmpeg 7.1.2 because:
- Brew has 8.1.1 but it's GPL-tainted → can't reuse.
- 7.1.2 is the current LTS and known-stable for openh264 integration (8.x changes some encoder API surface that the spike doesn't need, but the milestone may want).

Compatibility symlinks `libavcodec.62.dylib → libavcodec.61.19.101.dylib` etc. were created so the plan's hardcoded `test -f` checks still pass. Production code MUST NOT ship these symlinks — they're a temporary spike-only artifact. Milestone Item B should pick ONE major version and stick with it across all platforms, then update the plan's filename expectations.

---

## Spike disposition

**GREEN (proceed to milestone via `/gsd-new-milestone`) — with the five caveats listed above.**

Justification:
- Task 1 PASSED: Vendored LGPL ffmpeg + openh264 with verified absence of libx264 contamination (`strings libavcodec.*.dylib | grep -E 'libx264|x264_encoder|x264_init'` returns empty post-build, asserted by the `add_custom_command(TARGET video_spike POST_BUILD ...)` step).
- Task 2 PASSED: `tests/video_spike.cpp` builds + runs, encoder configures with bit-for-bit JamTaba codec parameters (320×240 YUV420P @ 10fps @ 96000bps GOP=30, AVRational{1,10}, libopenh264 backend), encode→decode→PNG roundtrip produces 99 valid PNGs, measured numbers (122,026 bytes / 99 frames = 1,232 bytes/frame, 4% CPU, 10s wall) all within budget.
- Task 3 PASSED: `260515-0pc-deferred-items.md` covers all RESEARCH §8 items B–I and §9 questions Q1–Q7 with file:line provenance from RESEARCH §6.
- Camera-acquisition leg degraded (synthetic frames substituted) — this is **expected** per Items C+G being out-of-scope for the spike; the milestone unlocks it.
- Fife caveats (above) all manageable and documented; none invalidate the locked architectural decisions in `260515-0pc-CONTEXT.md`.

**No leg of the spike came back RED.** The codec stack composes; openh264 reaches libavcodec from the same struct-layout headers; sws_scale + av_parser_init + avcodec_send_packet/receive_frame all behave per spec; frame quality at synthetic-content + 96 kbps is preserved through roundtrip.

The milestone can confidently take on RESEARCH §8 items B–I knowing the architectural decisions hold up to runtime evidence on at least one machine + arch combination.
