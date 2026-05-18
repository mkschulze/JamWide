# Phase 23: macOS Universal + Windows Build & Codesign - Context

**Gathered:** 2026-05-19
**Status:** Ready for planning

<domain>
## Phase Boundary

**Restore the pre-Phase-14.3 cross-platform CI** so JamWide ships standalone + plugin artifacts on **macOS universal (arm64 + x86_64), Windows x86_64, and Linux x86_64** — with the native video stack (Phases 19–22) working end-to-end on the platforms where it can, and gracefully degraded where it cannot.

**User framing (locked, supersedes ROADMAP's "greenfield Windows build" tone):**
> "We had a working CI pipeline that built cross platform targets with standalone, on windows, mac and linux. I just want that to be back after our change with video to ninjamzap video approach."

Phase 23 is a **regression-fix phase**, not greenfield infrastructure. The video work (Phase 14.3 ffmpeg vendoring + Phase 19 camera + Phase 20-22 codec/UI) broke three CI lanes that previously worked. Each needs targeted fixes; no new packaging architecture.

**Out of scope (deferred to its own phase or post-v1.3):**
- Windows code-signing certificate acquisition + signtool (cert doesn't exist; ship unsigned for beta with SmartScreen warning in release notes)
- Linux V4L2 camera capture (Item K, post-v1; Linux ships as receive-only — broadcast disabled)
- Universal Windows binary / ARM Windows (Microsoft's own ARM story is fragmented; not on roadmap)

</domain>

<decisions>
## Implementation Decisions

### Regression Inventory (the three concrete breaks)

- **D-01 macOS universal regression:** `cmake/ffmpeg.cmake` only loads ONE per-arch ffmpeg tree (the host arch), so when `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` builds the x86_64 slice, the linker fails with `Undefined symbols`. Fix scope: teach `cmake/ffmpeg.cmake` to detect `CMAKE_OSX_ARCHITECTURES` list and load BOTH `libs/ffmpeg/macos-arm64/` AND `libs/ffmpeg/macos-x86_64/` trees, then `lipo` them into universal `.dylib`s at link time (or set `-arch <slice>` flags per per-arch input). Root cause attributed in commit `4463542` body.

- **D-02 Windows ffmpeg vendoring regression:** Today's CI run (`26062553592`) failed at MSYS2 `./configure --enable-libopenh264` with `ERROR: openh264 >= 1.3.0 not found using pkg-config` (plus a `WARNING: gendef/dlltool not on PATH` earlier in the run). Fix scope: `scripts/build_ffmpeg_lgpl.sh` windows-x86_64 leg needs to either (a) install Cisco openh264 v2.x prebuilt + drop a `.pc` file into `PKG_CONFIG_PATH`, or (b) build openh264 from source under MSYS2 first, install into a prefix, point pkg-config at it. Option (a) matches the macOS recipe (Cisco prebuilts when available) and is the recommended path. Also resolve the gendef/dlltool PATH issue — install via pacman (`mingw-w64-x86_64-tools-git` or equivalent) or invoke from full path.

- **D-03 Linux camera-code regression:** `juce::CameraDevice` doesn't exist on Linux (no `juce_CameraDevice_linux.h` in juce_video module). Phase 19's camera-capture path includes the header unconditionally. Fix scope: conditional-compile the camera code with `#if JUCE_MAC || JUCE_WINDOWS` (or equivalent JUCE platform guards); Linux build ships a **receive-only standalone** — full video reception + display, no broadcast affordance. UI shows "Camera not available on Linux (V4L2 capture coming in v1.4)" in the camera button popover.

### Universal-mac Strategy

- **D-04** Use the single-CMake-configure approach with `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` (matching pre-Phase-14.3 workflow). Vendored ffmpeg + openh264 must exist for BOTH macos-arm64 AND macos-x86_64 in `libs/ffmpeg/` before configure. Per-arch `.dylib`s get lipo'd into universal `.dylib`s by `cmake/ffmpeg.cmake`'s fix (D-01). The CI script's "Vendor LGPL ffmpeg" step must produce BOTH arches, not just the host. Cisco openh264 v2.1.1 is the last mac prebuilt; arm64 must build from source (Spike Risk #3).

### Windows Codesigning

- **D-05** Ship the Windows beta artifact **unsigned**. Memory `project_apple_signing` documents only the Apple cert (Team ID `T3KK66Q67T`); no Windows code-signing cert exists. SmartScreen will warn users; document the click-through path in beta release notes ("More info → Run anyway"). Re-evaluate cert acquisition in v1.4 once beta validation completes.

### Linux Build Posture

- **D-06** Linux standalone ships **receive-only** (D-03). All audio + remote video reception works; local camera + broadcast affordance is hidden. This is the same shape as the existing JamWide v1.0 / v1.1 Linux artifacts plus video receive. Aligns with the existing roadmap deferral: "Linux V4L2 capture (Item K) is post-v1.3".

### Plan Split (refocused for regression-fix scope)

- **D-07** Three plans, one per regression — but each plan now owns its CI lane re-enabling end-to-end (instead of original ROADMAP's "23-03 = CI lanes for all platforms" model, which would have created cross-plan dependencies):
  - **Plan 23-01** macOS universal restoration: fix `cmake/ffmpeg.cmake` per-arch loading + lipo, restore `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` in CI, extend Vendor LGPL ffmpeg step to build both arches, verify existing per-dylib codesign loop + `install_name_tool` + notarization chain still works for universal output.
  - **Plan 23-02** Windows build restoration: fix MSYS2 openh264 sourcing in `scripts/build_ffmpeg_lgpl.sh`, fix gendef/dlltool PATH, re-enable build-windows CI gate (already done in commit `4634073`), ensure DLL bundling step works, add Windows leg to `verify_ffmpeg_lgpl.sh` (LGPL `strings` gate + `dumpbin /dependents` clean-deps gate).
  - **Plan 23-03** Linux build restoration: conditional-compile Phase 19 camera code (Linux skip path), re-enable build-linux CI gate, ensure ffmpeg vendoring Linux leg of `scripts/build_ffmpeg_lgpl.sh` still works (it was wired in commit `d760cb1` but never landed green), Linux UI affordance for "camera not available", add Linux leg to `verify_ffmpeg_lgpl.sh`.

### CI Lane + Release Job

- **D-08** Release job (`release:`) `needs:` list grows to `[build-macos, build-windows, build-linux]` — all three must be green for a tag to ship. Release upload includes `JamWide-macOS.tar.gz`, `JamWide-Windows.zip`, `JamWide-Linux.tar.gz` (per memory `project_release_packaging`: tar.gz preserves Unix permissions, zip for Windows). Current commit `4634073` has done the Windows half; Linux half + macOS-universal pickup land in their respective plans.

### Claude's Discretion
- Whether `cmake/ffmpeg.cmake` does lipo at the CMake level (creating a single universal `.dylib` in the build tree) or whether it passes `-arch <slice>` link flags per slice. Both work; the planner can pick whichever fits the existing CMake idiom in this codebase.
- Specific MSYS2 packages or build-from-source path for openh264 on Windows — researcher's call after verifying what's actually available in MSYS2 repos.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### ROADMAP + Requirements
- `.planning/ROADMAP.md` Phase 23 section — goal, plans (the locked 3-plan split), success criteria (6 items)
- `.planning/REQUIREMENTS.md` v1.3 § "Platform Packaging" — PKG-01..07
- `.planning/quick/260515-0pc-investigate-jamtaba-video-implementation/260515-0pc-deferred-items.md` Items B partial + G.2

### Regression-Cause Commits
- `git show 4463542` — "ci: defer build-windows + universal-macOS to Phase 23" — body documents the `cmake/ffmpeg.cmake` per-arch load bug (D-01)
- `git show 30ee519` — "ci: defer build-linux too" — documents Phase 19 `juce::CameraDevice` Linux incompatibility (D-03)
- `git show d760cb1` — "ci(14.3): wire ffmpeg vendoring on Linux + Windows" — last attempt at the cross-platform vendoring; never landed green

### Existing Build Infrastructure (touch, don't replace)
- `.github/workflows/juce-build.yml` — current single-workflow CI (build-macos, build-windows, build-linux, release)
- `scripts/build_ffmpeg_lgpl.sh` — per-platform tag dispatch (macos-x86_64, macos-arm64, linux-x86_64, windows-x86_64); Windows leg fails today at openh264 pkg-config
- `scripts/verify_ffmpeg_lgpl.sh` — LGPL discipline gate; needs Windows + Linux legs verified
- `cmake/ffmpeg.cmake` — the file at the heart of D-01; needs per-arch detection + lipo
- `CMakeLists.txt:244-269` — current macOS codesign loop (per ROADMAP); plan 23-01 verifies it survives universal builds
- `JamWide.entitlements` — camera entitlement landed in Phase 19; Phase 23 codesign verification reads this

### Live CI Failure (today's data point)
- GitHub Actions run `26062553592` job `build-windows` step `Vendor LGPL ffmpeg` — `ERROR: openh264 >= 1.3.0 not found using pkg-config`
- Same run had warning `gendef/dlltool not on PATH — Windows ffmpeg link-test will fail` from `scripts/build_ffmpeg_lgpl.sh`

### Memory Pointers (cross-session context)
- [`project_apple_signing`] Team ID `T3KK66Q67T`, notarization via API Key — wire intact in current workflow
- [`feedback_check_ci_before_tag`] Always check CI green before tagging — applies directly to Phase 23 exit
- [`project_release_packaging`] macOS/Linux tar.gz, Windows zip — D-08 enforces
- [`feedback_copy_vst_on_build`] JUCE Copy After Build wiring — relevant for Standalone vs plugin install paths
- [`reference_javier_ninjamzap_encoder_review`] Encoder VT-compat verified; not a Phase 23 concern but informs which platforms need camera support

### Spike Risks Carried Forward
- Spike Risk #3 (`260515-0pc` spike findings): Cisco openh264 v2.1.1 is the LAST mac prebuilt — arm64 builds from source. Plan 23-01 confirms the source-build path is operational in CI.
- Spike Risk #4: libX11 spurious dep on Linux mitigated; CI Linux leg must enforce via `verify_ffmpeg_lgpl.sh`.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **`scripts/build_ffmpeg_lgpl.sh` per-platform dispatch** — the OS+ARCH detection skeleton is already there (lines 76-84 of script for MINGW path). Reuse the structure; fix the openh264 sourcing in the windows-x86_64 leg.
- **`cmake/ffmpeg.cmake` single-arch loader** — current implementation does the right thing for non-universal builds; extend rather than rewrite.
- **Macos codesign loop in `CMakeLists.txt:244-269`** — already handles per-dylib signing. Universal binaries just multiply the input set; the loop logic survives.
- **`verify_ffmpeg_lgpl.sh` LGPL gate** — already iterates `libs/ffmpeg/*/` populated trees; adding Windows/Linux legs is extending the iteration, not rewriting.
- **Phase 19 camera affordance UI (Camera button popover)** — has a "camera unavailable" path for unentitled DAWs (e.g., REAPER macOS); Linux skip path can reuse this UI affordance with a different message.

### Established Patterns
- **CI gates as `bash scripts/<gate>.sh`** — verify scripts are idiomatic; new gates should follow this pattern not inline into the workflow YAML.
- **Per-platform tag layout `libs/ffmpeg/<os>-<arch>/`** — keep extending this. Don't introduce new layout schemes.
- **Conditional compile via `JUCE_MAC` / `JUCE_WINDOWS` / `JUCE_LINUX`** — Phase 19 already uses these for some paths (`BrowserDetect_win.cpp`, etc.). D-03's camera-code gate fits this pattern.

### Integration Points
- **`cmake/ffmpeg.cmake` → linker** — the file produces the `target_link_libraries` calls; this is where lipo'd universal libs flow into the build graph.
- **`build_ffmpeg_lgpl.sh` → `libs/ffmpeg/<platform>/`** — script populates the layout that ffmpeg.cmake reads.
- **CI workflow `Vendor LGPL ffmpeg` step → script** — workflow shells out to the script per-platform; for macOS universal the script must produce BOTH arm64 + x86_64 trees (currently single-arch per CI run).

</code_context>

<specifics>
## Specific Ideas

- **User's reframe (verbatim, locked):** "we had a working CI pipeline that built cross platform targets with standalone, on windows, mac and linux, I just want that to be back after our change with video to ninjamzap video approach." This is the success-bar phrasing: Phase 23 EXIT = three-platform CI green + standalone artifact downloadable per-platform + the v1.3 video stack working where each platform can.
- **Beta tag target:** `v1.1-beta.20.7` once all three CI lanes green (per existing tag convention). Release notes call out: first cross-platform beta since Phase 14.3 ffmpeg vendoring landed.
- **Pre-Phase-14.3 CI is the visual reference** — `git show 4463542~1:.github/workflows/juce-build.yml` shows the target shape (universal mac + Windows + Linux all running as default jobs, no `if: ${{ false }}` gates).

</specifics>

<deferred>
## Deferred Ideas

- **Windows EV code-signing cert acquisition** — ~$300+/year, slow procurement. Re-evaluate at v1.4 once beta-validation surfaces whether SmartScreen friction is a real adoption blocker. Track in roadmap backlog as a follow-up Windows-packaging phase.
- **Linux V4L2 camera capture** — confirmed Item K, post-v1. Linux ships receive-only in Phase 23; broadcast deferred to a dedicated Linux-V4L2 phase post-beta.
- **Universal Windows binary (x64 + ARM64)** — Windows-on-ARM is fragmented and not on JamWide's roadmap. Stay x64-only on Windows.
- **MSIX / WiX / Inno Setup proper Windows installer** — Phase 23 ships a zip (per memory `project_release_packaging`). Proper installer can land in a later packaging phase if users ask for it.
- **Notarization for Windows** — Windows doesn't have an equivalent. Microsoft Store certification is the closest analog and is out of scope.

</deferred>

---

*Phase: 23-macos-universal-windows-build-codesign*
*Context gathered: 2026-05-19*
