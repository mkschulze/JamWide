# Phase 23: macOS Universal + Windows Build & Codesign - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-19
**Phase:** 23-macos-universal-windows-build-codesign
**Areas discussed:** Phase reframe (regression-fix vs greenfield), universal-mac strategy, Windows openh264 sourcing, Windows codesigning scope, plan split

---

## Pre-discussion CI signal (informed all subsequent gray areas)

While discuss-phase was running, the user's prior-session push triggered CI run `26062553592` on `quick/260515-0pc-native-video-port`. By the time gray areas were presented, `build-windows` had already failed at the `Vendor LGPL ffmpeg` step with `ERROR: openh264 >= 1.3.0 not found using pkg-config` (plus a `WARNING: gendef/dlltool not on PATH` earlier). This concrete failure became the anchor for the Windows discussion below.

`build-macos` was still in progress at the time of writing CONTEXT.md.

---

## Gray Areas Presented (initial framing)

I presented four candidate gray areas, framed around the original ROADMAP's "greenfield Windows build" interpretation of Phase 23:

| Area | Frame |
|------|-------|
| Universal-mac stitching strategy | Lipo timing + per-arch ffmpeg build sequencing |
| Windows openh264 sourcing | Prebuilt vs source-build vs pacman (today's failure) |
| Windows codesigning scope | Unsigned beta vs cert acquisition |
| Phase 23 scope split + ordering | Three plans as-roadmapped vs reorder vs merge |

## User's Response (verbatim, reframes the entire phase)

> "we had a working CI pipeline that built cross platform targets with standalone, on windows, mac and linux, I just want that to be back after our change with video to ninjamzap video approach."

**Effect:** This reframe collapses three of the four candidate gray areas. Phase 23 is no longer "decide HOW to build cross-platform" — it's "restore what worked before Phase 14.3 ffmpeg vendoring broke it." The original ROADMAP framing implied greenfield infrastructure work; the user's framing is regression-fix.

---

## Reframed Decisions (derived from user's redirect + technical investigation)

### Phase scope (was "greenfield Windows build")

| Option | Description | Selected |
|--------|-------------|----------|
| Greenfield — design new cross-platform packaging | Original ROADMAP framing | |
| Regression-fix — restore pre-Phase-14.3 CI state | User's reframe; ground-truth via `git show 4463542~1:.github/workflows/juce-build.yml` showed universal mac + Windows + Linux all running as default jobs before the video work | ✓ |

**User's choice:** Regression-fix.
**Notes:** Verified by reading the workflow file at commit `4463542~1`: pre-Phase-14.3 the workflow had `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and unconditional `build-windows` + `build-linux` jobs. That's the target state to restore.

### Universal-mac stitching (was discrete options A/B/C)

| Option | Description | Selected |
|--------|-------------|----------|
| (A) Build per-arch lanes separately + lipo at end | Multiple CI lanes; lipo final artifacts | |
| (B) Single configure with `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` | What pre-Phase-14.3 workflow did | ✓ |
| (C) Hybrid — vendor per-arch + lipo to universal libs + single JUCE build | More complex variation of B | |

**User's choice (implicit from "restore"):** B — matches the pre-Phase-14.3 workflow shape exactly.
**Technical work to make B viable:** Fix `cmake/ffmpeg.cmake` to load both `libs/ffmpeg/macos-arm64/` AND `libs/ffmpeg/macos-x86_64/` trees (per commit 4463542 body: it currently only loads the host arch, which fails x86_64 slice with "Undefined symbols"). CI's `Vendor LGPL ffmpeg` step must populate BOTH arch trees.

### Windows openh264 sourcing (concrete failure today)

| Option | Description | Selected |
|--------|-------------|----------|
| (A) Cisco win64 prebuilt + drop .pc file into PKG_CONFIG_PATH | Matches macOS recipe; MPEG-LA royalty caveat documented; fastest | ✓ |
| (B) Build openh264 from source under MSYS2 | No prebuilt royalty caveat; slower CI | |
| (C) pacman install if available | Depends on MSYS2 repo state; less reproducible | |

**Claude's recommended pick (within user's "restore" framing):** A — most consistent with the existing macOS-x86_64 + macOS-arm64 sourcing pattern. Researcher in plan-phase verifies the prebuilt is still fetchable; if not, falls back to (B).

### Windows codesigning scope

| Option | Description | Selected |
|--------|-------------|----------|
| (A) Ship unsigned for beta; SmartScreen warning in release notes | Memory `project_apple_signing` documents only Apple cert | ✓ |
| (B) Get EV cert (~$300+/year, slow procurement) | Defer to v1.4 | |
| (C) Skip signtool entirely from Phase 23 | Same UX as A; cleaner code path | |

**Claude's pick (within "restore" framing):** A — matches the user's prior CI which also shipped unsigned Windows artifacts pre-Phase-14.3 (no codesign step in `build-windows` job pre-4463542).

### Linux build posture

| Option | Description | Selected |
|--------|-------------|----------|
| (A) Receive-only standalone (camera code conditional-compiled out) | Same shape as pre-Phase-19 Linux; Item K (V4L2) explicitly deferred | ✓ |
| (B) Full V4L2 camera implementation in Phase 23 | Scope creep; Item K is post-v1.3 | |
| (C) Drop Linux from beta entirely | User explicitly asked for Linux back | |

**User's choice (implicit from "windows, mac and linux"):** A.

### Plan split (refocused from ROADMAP's universal/windows/CI-lanes split)

| Option | Description | Selected |
|--------|-------------|----------|
| Three plans as ROADMAPped (23-01 mac universal / 23-02 Windows / 23-03 CI lanes) | Pre-reframe shape | |
| Three plans one-per-regression (23-01 mac universal / 23-02 Windows / 23-03 Linux), each owns its CI lane end-to-end | Regression-fix friendly; avoids cross-plan CI dependencies | ✓ |

**Claude's pick:** Three plans, one per platform regression. Cross-plan CI dependencies were a needless coupling in the original split — folding CI work into each platform plan keeps the boundary clean.

---

## Claude's Discretion

- Whether `cmake/ffmpeg.cmake` does lipo at the CMake level (creating a single universal `.dylib` in the build tree) or whether it passes `-arch <slice>` link flags per slice — both work, planner chooses to fit existing CMake idiom.
- Specific MSYS2 packages or build-from-source path for openh264 on Windows — researcher's call after verifying what's actually available.
- Specific UI message text for Linux "Camera not available" affordance — UI pattern matches the existing Phase 19 "Camera unavailable" path for unentitled DAWs.

## Deferred Ideas

- **Windows EV code-signing cert** — re-evaluate v1.4 once beta feedback surfaces SmartScreen as a real adoption blocker.
- **Linux V4L2 camera capture (broadcast)** — Item K, post-v1.3 dedicated phase.
- **Universal Windows binary (x64 + ARM64)** — Windows-on-ARM not on JamWide roadmap.
- **Proper Windows installer (MSIX / WiX / Inno Setup)** — Phase 23 ships a zip per memory `project_release_packaging`; installer can be a follow-up packaging phase if users request it.
- **Notarization for Windows** — Microsoft Store certification is the only analog; out of scope.
