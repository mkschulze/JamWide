---
quick_id: 260515-0pc
slug: investigate-jamtaba-video-implementation
review_date: 2026-05-15
review_depth: quick
verdict: PASS_WITH_NOTES
---

# 260515-0pc Code Review — JamTaba Video Feasibility Spike

## 1. Verdict

**PASS_WITH_NOTES** — The spike achieves its goal (proves the LGPL ffmpeg + Cisco openh264 + JUCE codec stack composes), all six critical invariants verified, no forbidden production files touched, default-OFF gating intact. Two material issues must be addressed **before any milestone code consumes this pattern**: (a) the vendored dylibs link against a homebrew-only `/usr/local/opt/libx11/lib/libX11.6.dylib` (portability blocker on machines without that brew formula), and (b) `configure-flags.txt` records a stale earlier configure invocation that does NOT include `--disable-xlib --disable-libxcb --disable-sdl2`, so the recorded compliance evidence diverges from the script's current intent. As spike code these are acceptable; as a milestone integration pattern they are not.

---

## 2. Critical findings

None block this spike's merge. (See § 3 for milestone-blocking items that this spike must fix or document before milestone consumes them.)

---

## 3. Major findings

### M-1 — Vendored dylibs link against `/usr/local/opt/libx11/lib/libX11.6.dylib`

**File:** `libs/ffmpeg/macos-x86_64/lib/libavcodec.61.19.101.dylib`, `libavformat.61.7.100.dylib`, `libavutil.59.39.100.dylib`, `libswscale.8.3.100.dylib` (verified via `otool -L`).

**Issue:** Every ffmpeg dylib in the tree carries a hard `LC_LOAD_DYLIB` reference to `/usr/local/opt/libx11/lib/libX11.6.dylib` (homebrew x11). On a machine without that brew formula, `dlopen` fails at process start with `Library not loaded: /usr/local/opt/libx11/lib/libX11.6.dylib`. This contradicts `scripts/build_ffmpeg_lgpl.sh:139` which has `--disable-xlib --disable-libxcb --disable-sdl2`. Root cause: the dylibs in tree were built by an earlier run of the script (commit `43f7c4f`) **before** those flags were added; the recorded `libs/ffmpeg/configure-flags.txt` confirms the older invocation (no xlib disables — see M-2). For the spike binary on the dev box this works; for ANY other machine, or for milestone CI, it does not.

**Fix:** Re-run `scripts/build_ffmpeg_lgpl.sh` (which now has the disables) and re-commit the regenerated dylibs + the regenerated `configure-flags.txt`. Verify with `otool -L libs/ffmpeg/macos-x86_64/lib/*.dylib | grep libX11` returning empty. (Defer-to-milestone is **not** acceptable here because milestone item B will universal-binary-stitch these same dylibs — the libX11 contamination would propagate.)

### M-2 — `configure-flags.txt` records a stale earlier run

**File:** `libs/ffmpeg/configure-flags.txt:8`

**Issue:** The recorded configure invocation is missing `--disable-xlib --disable-libxcb --disable-sdl2` that line 139 of the current script emits. Comparison via `diff <(grep -oE '\-\-(enable|disable)-[a-z0-9]+' scripts/build_ffmpeg_lgpl.sh | sort -u) <(grep -oE '\-\-(enable|disable)-[a-z0-9]+' libs/ffmpeg/configure-flags.txt | sort -u)` shows three flags present in the script but absent in the recorded output. This is a compliance-evidence integrity defect: anyone auditing whether the vendored binary was built with LGPL discipline will see `configure-flags.txt` and assume that command produced the dylibs in tree, but the dylibs link against libX11 (M-1) which only happens when xlib is enabled. The recorded evidence does not match the binary.

**Fix:** Same as M-1 — re-run the script after the xlib disables landed; the script overwrites `configure-flags.txt` on every run.

### M-3 — Arch-detection logic in `cmake/ffmpeg.cmake` and `CMakeLists.txt:362` can disagree under `JAMWIDE_UNIVERSAL=ON`

**File:** `CMakeLists.txt:362`, `cmake/ffmpeg.cmake:25-37`

**Issue:** `CMakeLists.txt:362` hardcodes the include path with `${CMAKE_HOST_SYSTEM_PROCESSOR}`, but `cmake/ffmpeg.cmake` has a more nuanced rule that prefers a single-value `CMAKE_OSX_ARCHITECTURES` and falls back to host on universal builds. With the project default `JAMWIDE_UNIVERSAL=ON` (CMakeLists.txt:15), `CMAKE_OSX_ARCHITECTURES` is `"arm64;x86_64"`. On an x86_64 host both paths happen to land at `macos-x86_64`, masking the divergence. On an arm64 host (or once milestone item B vendors arm64), the two paths still agree by accident only because both fall back to host. If a user ever does `cmake -DJAMWIDE_UNIVERSAL=OFF -DCMAKE_OSX_ARCHITECTURES=x86_64` on an arm64 host, `cmake/ffmpeg.cmake` correctly picks `macos-x86_64` while `CMakeLists.txt:362` picks `macos-arm64` — header search resolves to the wrong (or missing) path.

**Fix:** Either expose the `_ffmpeg_arch` choice from `cmake/ffmpeg.cmake` (cache var or target property) and have `CMakeLists.txt:362` read it back via `get_target_property(_inc ffmpeg::lgpl JAMWIDE_FFMPEG_INCLUDE_DIR)`, or move the `target_include_directories(video_spike BEFORE PRIVATE ...)` call inside `cmake/ffmpeg.cmake` as a helper macro. The latter is preferable because it keeps the "BEFORE PRIVATE" workaround localized with the IMPORTED-target-include-order explanation already documented at `cmake/ffmpeg.cmake:82-102`.

### M-4 — Build script has no checksum/signature verification on downloaded ffmpeg + openh264 tarballs

**File:** `scripts/build_ffmpeg_lgpl.sh:73, 120, 227`

**Issue:** All three downloads (`curl -fsSL` of openh264 dylib, ffmpeg source tarball, and license file) trust the HTTPS endpoint with no further verification. There is no `shasum -c`, no `gpg --verify`, no recorded SHA-256. ffmpeg.org publishes detached GPG signatures (`.asc` files) and SHA-256 checksums (`.tar.xz.sha256`) for every release; Cisco openh264 releases publish SHA-256 checksums on the GitHub release page. Because this script's output goes into JamWide's distribution (LGPL bundle), a compromised or man-in-the-middled tarball would compile cleanly and silently ship to users. The `set -euo pipefail` guard catches network errors but not bytes-tampered-but-fetched-OK.

**Fix:** Pin and verify SHA-256 for the openh264 dylib bz2 and ffmpeg tarball. Minimum acceptable form (no GPG keychain dependency):
```bash
FFMPEG_SHA256="<known good>"  # e.g., from https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz.sha256
echo "${FFMPEG_SHA256}  ffmpeg-${FFMPEG_VERSION}.tar.xz" | shasum -a 256 -c -
```
Fail the script (`exit 1`) on mismatch. Same pattern for `$DYLIB_BZ2`. Acceptable to defer to milestone item B IF this is explicitly tracked in `260515-0pc-deferred-items.md` as a milestone-blocking gate; if it's not currently tracked, add it.

### M-5 — Null-pointer dereference in spike error path when codec lookup fails AND camera failed

**File:** `tests/video_spike.cpp:204, 274`

**Issue:** Lines 204 and 274 both call `cam->removeListener(&listener)` without a null check, but the spike has explicit logic at line 186 that sets `cam` to null and falls through to synthetic mode. If `avcodec_find_encoder_by_name("libopenh264")` returns null (line 200) on a machine where the camera ALSO failed to open, the cleanup path segfaults instead of cleanly emitting `SPIKE-FAIL`. Same defect at line 274 (avcodec_open2 failure). Note line 401 correctly guards with `if (cam)`.

**Fix:**
```cpp
if (cam) cam->removeListener(&listener);  // both lines
```

### M-6 — Spike binary leaks ffmpeg state on multiple SPIKE-FAIL paths

**File:** `tests/video_spike.cpp:202-275, 415-422`

**Issue:** The five `SPIKE-FAIL` returns (lines 205, 211, 274-275, 416, 421) leak prior allocations: `enc` lookup leak is benign (no alloc), but `enc_ctx`/`pkt`/`enc_frame`/`sws_*` leak depending on which line fails. Also `dec_ctx` is unchecked for null at line 419 before being passed to `avcodec_open2`. For a spike binary that exits-on-fail this is acceptable LSAN noise, but if this code is mined as a starting point for milestone item C (which it will be — RESEARCH § 8 Option (a)), the leaks should not propagate. Suggest a single `goto cleanup` or RAII wrappers (`std::unique_ptr` with custom deleter for `AVCodecContext*`/`AVFrame*`/`AVPacket*`) before milestone consumption.

**Fix (spike-acceptable):** Document at file-top that this is one-shot test code with intentional exit-on-fail leaks and that milestone item C must rewrite with RAII. Document in `260515-0pc-deferred-items.md` (already partially mentioned per file comment at line 7).

---

## 4. Minor findings

### N-1 — `int in_left = static_cast<int>(bitstream.size())` can overflow

**File:** `tests/video_spike.cpp:436`

For 100 frames at 96 kbps the bitstream is well under 2 GB so this never fires in the spike (`kTotalFrames = 100`). Still, casting `size_t → int` without bounds-check is the kind of pattern that survives copy-paste into milestone code where `kTotalFrames` becomes a long-running camera capture. Use `static_cast<int>(std::min<size_t>(bitstream.size(), INT_MAX))` or refactor to a chunked loop.

### N-2 — `av_image_alloc` return code unchecked

**File:** `tests/video_spike.cpp:283`

`av_image_alloc` returns the buffer size on success and a negative AVERROR on failure. An OOM here would leave `enc_frame->data` uninitialized and the next `sws_scale` writes to garbage. Spike-acceptable; mention in the deferred-items doc.

### N-3 — `write_png` silently drops failures

**File:** `tests/video_spike.cpp:136-141`

If `f.createOutputStream()` returns null the function returns silently and `decoded_count` is incremented anyway, overstating successful frame decodes in the SPIKE-RESULT line. Add `if (!stream) { std::cerr << "WARN: cannot open " << path << "\n"; return; }` and have the lambda at line 438-446 not increment on failure.

### N-4 — `out_dir` from `JAMWIDE_VIDEO_SPIKE_OUT` env var has no validation

**File:** `tests/video_spike.cpp:157-158`

`fs::create_directories(out_dir)` is called with the unsanitized env var. A user passing `JAMWIDE_VIDEO_SPIKE_OUT="/etc/passwd_dir"` (or any nonsense) gets a try-and-fail; on macOS without sudo it's harmless. Spike-acceptable. Not a security finding because the env var is set by the user running the binary on their own machine — there is no attacker-supplied input here.

### N-5 — `bitstream.size()` of decode pipeline assumes encoder produces well-formed annex-B NAL stream

**File:** `tests/video_spike.cpp:381-385`

`avcodec_send_frame`/`avcodec_receive_packet` may emit AVCC-format packets (length-prefixed, not annex-B) depending on encoder defaults. libopenh264 emits annex-B by default and `av_parser_parse2(AV_CODEC_ID_H264)` parses annex-B, so the spike works. If the milestone switches encoder backends or sets `enc_ctx->extradata`, the parser path silently drops frames. Document the assumption.

### N-6 — `JAMWIDE_VIDEO_SPIKE` option requires both itself AND `JAMWIDE_BUILD_TESTS` to be ON

**File:** `CMakeLists.txt:301, 346`

Nested `if (JAMWIDE_BUILD_TESTS) { if (JAMWIDE_VIDEO_SPIKE) { ... } }`. Documented behavior (CMakeLists.txt:39) but the error message for `cmake -DJAMWIDE_VIDEO_SPIKE=ON` (without -DJAMWIDE_BUILD_TESTS=ON) is silent — the option appears to do nothing. Add an explicit `if (JAMWIDE_VIDEO_SPIKE AND NOT JAMWIDE_BUILD_TESTS) message(FATAL_ERROR "...") endif()` at the top of the file.

### N-7 — Compatibility-symlink hack (`libavcodec.62.dylib → .61.19.101.dylib`) is fragile

**File:** `scripts/build_ffmpeg_lgpl.sh:202-209`

Creating a `.62` symlink that points to a `.61.x` ABI is a binary lie. If anything ever links against the `.62` symlink expecting ffmpeg-8 ABI, it will silently get ffmpeg-7 layouts and corrupt structs at runtime — exactly the bug `target_include_directories(BEFORE PRIVATE)` was added to fix. Comment at line 196-200 says these exist "for the plan's verify gate" — once that gate is rewritten in the milestone, delete the symlinks and assert no consumer references the wrong soname.

### N-8 — `cmake/ffmpeg.cmake` glob may pick up the wrong dylib version

**File:** `cmake/ffmpeg.cmake:57-79`

`file(GLOB _avcodec_dylib "${_ffmpeg_dir}/lib/libavcodec.*.dylib")` matches `libavcodec.61.19.101.dylib`, `libavcodec.61.dylib`, `libavcodec.62.dylib`, `libavcodec.dylib` — four entries. The follow-up loop filters out symlinks and picks the first non-symlink. This works today but is order-dependent (`file(GLOB)` order is alphabetical on most filesystems but not guaranteed). Prefer an explicit version list, or `file(GLOB ... LIST_DIRECTORIES FALSE)` with sorted output and an assertion that exactly one non-symlink matches.

### N-9 — `target_link_options(... LINKER:-rpath,${_ffmpeg_dir}/lib)` bakes the absolute repo path into the spike binary

**File:** `cmake/ffmpeg.cmake:121-123`

The spike binary ends up with `LC_RPATH = /Users/.../JamWide/libs/ffmpeg/macos-x86_64/lib`. Move the binary anywhere off-machine and dlopen fails. Acceptable for the spike (test binary, not distributed). Document in `260515-0pc-deferred-items.md` that milestone item G (bundle wiring) replaces this with `@executable_path/../Frameworks` after `install_name_tool` post-build.

### N-10 — `.gitignore` `!libs/ffmpeg/**/*.dylib` whitelist is broader than needed

**File:** `.gitignore:28-30`

Whitelisting `**/*.dylib` under `libs/ffmpeg/` will accept future `.dylib` files added there with no review, including potentially x264-tainted ones. Tighter form: explicit per-file allowlist (`!libs/ffmpeg/macos-x86_64/lib/libavcodec.*.dylib` etc.) keeps the LGPL discipline visible at the gitignore layer. Low priority — the post-build `strings` check in CMakeLists.txt:386 is the authoritative LGPL gate.

### N-11 — Build script `set -euo pipefail` is correct, but `... | tail -40` masks the real error in pipe failures

**File:** `scripts/build_ffmpeg_lgpl.sh:159, 163`

`bash -c "$CONFIG_CMD" 2>&1 | tail -40` — if `bash -c "$CONFIG_CMD"` exits non-zero, `pipefail` propagates the failure (good), but the user sees only the last 40 lines of configure output, which on a configure failure may not include the actual error message (e.g., a missing dependency printed early). Suggest `tee` to a logfile in `$WORK` so failures can be diagnosed: `bash -c "$CONFIG_CMD" 2>&1 | tee "$WORK/configure.log" | tail -40`.

### N-12 — Spike comment at video_spike.cpp:117-119 hardcodes BGRA assumption with a milestone-defer note, but the encoder will silently produce wrong colors on any non-macOS BGRA platform

**File:** `tests/video_spike.cpp:117-119, 287`

`AV_PIX_FMT_BGRA` is hardcoded as the input format to `sws_scale`. On Windows JUCE's ARGB layout differs and the spike would produce color-swapped output. The comment correctly defers per-platform handling to milestone item C. Acceptable for the spike (single platform).

---

## 5. Verified invariants

| # | Invariant | Status | Evidence |
|---|---|---|---|
| 1 | LGPL discipline in configure flags | **PASS (script)** / **NOTE (recorded)** | `scripts/build_ffmpeg_lgpl.sh:136-141` has `--disable-gpl --enable-libopenh264`, no `--enable-libx264` anywhere. `--disable-libx264` is not present as a literal string but x264 is also not enabled, and `--disable-everything` (line 140, before per-component enables) acts as the safety net. Recorded `configure-flags.txt` matches on the LGPL portion (M-2 caveat is xlib, not GPL). |
| 2 | `JAMWIDE_VIDEO_SPIKE` defaults to OFF | **PASS** | `CMakeLists.txt:41` — `option(JAMWIDE_VIDEO_SPIKE "..." OFF)`. |
| 3 | No invasion of forbidden files | **PASS** | `git diff --name-only 43f7c4fb06fa6d539c02bbdf1d890164a4851601^ HEAD -- src/core/ juce/JamWideJuce*.cpp juce/ui/ juce/video/ companion/ JamWide.entitlements` returns empty. Only `CMakeLists.txt`, `cmake/ffmpeg.cmake`, `scripts/build_ffmpeg_lgpl.sh`, `tests/video_spike.cpp`, `.gitignore`, `libs/ffmpeg/**`, `.planning/quick/...` are touched. |
| 4 | Header-shadowing fix uses `BEFORE PRIVATE` | **PASS** | `CMakeLists.txt:361` — `target_include_directories(video_spike BEFORE PRIVATE ...)`, with the `BEFORE` keyword present and a 9-line comment block explaining why (lines 353-360). |
| 5 | Bash-script safety (`set -euo pipefail`, HTTPS, fail-loud) | **PASS-WITH-NOTES** | `set -euo pipefail` at line 36. All curl URLs are HTTPS (`grep -E 'http://' scripts/build_ffmpeg_lgpl.sh` returns empty). `curl -fsSL` causes HTTP errors to fail the script. **NO checksum verification** — see M-4. |
| 6 | No GPL-tainted (libx264) symbols in libavcodec | **PASS** | `strings libs/ffmpeg/macos-x86_64/lib/libavcodec.*.dylib \| grep -E 'libx264\|x264_encoder\|x264_init\|x264_param_default'` returns empty. |

---

## 6. Confidence

**Read in full:**
- `/Users/cell/dev/JamWide/CMakeLists.txt` (539 lines)
- `/Users/cell/dev/JamWide/cmake/ffmpeg.cmake` (131 lines)
- `/Users/cell/dev/JamWide/scripts/build_ffmpeg_lgpl.sh` (235 lines)
- `/Users/cell/dev/JamWide/tests/video_spike.cpp` (498 lines)
- `/Users/cell/dev/JamWide/.gitignore` (73 lines)
- `/Users/cell/dev/JamWide/libs/ffmpeg/configure-flags.txt` (8 lines)

**Verified via `Bash` tooling:**
- `git diff --name-only` of forbidden-paths set → empty (invariant 3)
- `strings ... | grep libx264` on every libavcodec dylib → empty (invariant 6)
- `otool -L` of all five vendored dylibs → discovered M-1 (libX11 contamination)
- `file` of libavcodec → confirmed single-arch x86_64 Mach-O (informs M-3)
- `git log` of commit range → confirmed five commits in spike scope
- `grep` audits of LGPL flags, HTTPS URLs, checksum patterns

**Spot-checked (not full reads):**
- License files (`LICENSE.LGPL.txt`, `LICENSE.openh264.txt`) — confirmed present and non-empty (502 + 22 lines), not read line-by-line
- Vendored ffmpeg headers — confirmed present, structure matches `libavcodec/` `libavformat/` `libavutil/` `libswscale/` `wels/` layout
- `.planning/quick/260515-0pc-...` deliverables — out of scope per task instructions

**Not reviewed (intentionally per task scope):**
- Vendored ffmpeg + openh264 source headers (upstream artifacts)
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` and `-spike-results.md` (they are findings narratives, not reviewable code)

---

_Reviewed: 2026-05-15_
_Reviewer: Claude (gsd-code-reviewer, adversarial stance)_
_Depth: quick (per task), with deeper drill-downs into security-critical script + invariant verification_
