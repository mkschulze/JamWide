---
phase: 22
plan: 01
subsystem: ui-video-tile-substrate
tags: [video, ui, juce, tile, member-order-contract, m5-typed-callback, wave-0]
requires:
  - juce/video/native/JamWideFrameDistributor.{h,cpp}   # Phase 19 callback Subscriber substrate
  - juce/video/distributor/JamWideRemoteFrameDistributor.{h,cpp}  # Phase 21 RAII Subscription substrate
  - juce/video/distributor/PeerVideoSink.{h,cpp}        # Phase 21 per-peer sink + atomic status fields
  - juce/ui/JamWideLookAndFeel.h                        # VB-Banana dark-theme palette tokens
provides:
  - juce/ui/video/computeGridLayout.h                   # Pure-C++ N×M grid layout solver
  - juce/ui/video/VideoTileBase.{h,cpp}                 # Shared tile chrome + M5 typed VideoPopoutTarget
  - juce/ui/video/SelfVideoTile.{h,cpp}                 # Phase 19 callback-Subscriber tile (MEMBER-ORDER)
  - juce/ui/video/RemotePeerTile.{h,cpp}                # Phase 21 RAII-Subscription tile (MEMBER-ORDER)
  - tests/test_video_grid_layout.cpp                    # 8 invariant tests for the layout solver
  - tests/test_video_tile_member_order.cpp              # Runtime offsetof proof for MEMBER-ORDER CONTRACT
affects:
  - CMakeLists.txt                                      # +2 test executables, +6 tile sources in JamWideJuce
tech-stack:
  added: []
  patterns:
    - MEMBER-ORDER CONTRACT (RAII subscription as LAST declared member)
    - VideoTileMemberOrderProbe friend-class pattern (offsetof for non-standard-layout types)
    - M5 typed-enum callback (VideoPopoutTargetKind + VideoPopoutTarget — replaces magic-string sentinel)
    - bufferLock snapshot-then-release (T-22-MO-2 DoS mitigation)
    - findSink-per-paint (no cached pointer — RESEARCH Pitfall 6)
key-files:
  created:
    - juce/ui/video/computeGridLayout.h
    - juce/ui/video/VideoTileBase.h
    - juce/ui/video/VideoTileBase.cpp
    - juce/ui/video/SelfVideoTile.h
    - juce/ui/video/SelfVideoTile.cpp
    - juce/ui/video/RemotePeerTile.h
    - juce/ui/video/RemotePeerTile.cpp
    - tests/test_video_grid_layout.cpp
    - tests/test_video_tile_member_order.cpp
  modified:
    - CMakeLists.txt
decisions:
  - "Two-tier selection in computeGridLayout: prefer fitting layout over non-fitting; within fits-bucket prefer larger tileW, within doesn't-fit bucket prefer larger cols (yields side-by-side at N=2 instead of one 4:3 vertical-overflow tile)"
  - "Runtime offset comparison via friend probe class — portable across non-standard-layout JUCE components; offsetof emits libc++ diagnostic on inheritance-from-Component types"
  - "Tile sources compile cleanly into JamWideJuce_VST3 with no new link libs — juce_video/juce_graphics/juce_events already linked"
  - "VideoTileMemberOrderProbe is JAMWIDE_BUILD_TESTS-guarded — friend declarations do not change production ABI"
metrics:
  duration_seconds: ~1850
  completed: 2026-05-18T00:23:19Z
  commits: 3
  files_created: 9
  files_modified: 1
---

# Phase 22 Plan 01: Native Video UI Tile Substrate Summary

One-line: Three atomic commits ship the foundation tile substrate for Phase 22's grid + popouts — a pure-C++ deterministic 4:3 grid layout solver, a JUCE-aware VideoTileBase chrome layer with the M5 typed `VideoPopoutTarget` callback (no magic strings), and two specialised tile classes (SelfVideoTile via Phase 19 callback Subscriber, RemotePeerTile via Phase 21 RAII Subscription) — both following Phase 19's MEMBER-ORDER CONTRACT verbatim, with a runtime-offset friend-probe test that mechanically encodes the contract.

## Commits

| Task | Name                                                                   | Commit    | Files                                                                                                              |
| ---- | ---------------------------------------------------------------------- | --------- | ------------------------------------------------------------------------------------------------------------------ |
| 1    | computeGridLayout pure helper + 8 invariant tests (RED→GREEN)          | `78f2780` | juce/ui/video/computeGridLayout.h, tests/test_video_grid_layout.cpp, CMakeLists.txt                                |
| 2    | VideoTileBase + SelfVideoTile + RemotePeerTile (MEMBER-ORDER + M5)     | `380a935` | juce/ui/video/{VideoTileBase,SelfVideoTile,RemotePeerTile}.{h,cpp}, CMakeLists.txt                                 |
| 3    | test_video_tile_member_order — MEMBER-ORDER CONTRACT proof             | `a5aa7b2` | tests/test_video_tile_member_order.cpp, juce/ui/video/VideoTileBase.cpp (Font deprecation fix), CMakeLists.txt     |

## Verification

### Automated (per plan `<verification>`)

1. **Build all three targets cleanly:**

   ```
   cmake --build build-juce --target test_video_grid_layout test_video_tile_member_order JamWideJuce_VST3
   ```

   Exit 0. JamWide.vst3 built and codesigned ad-hoc.

2. **ctest both new tests:**

   ```
   cd build-juce && ctest -R "video_grid_layout|video_tile_member_order" --output-on-failure
   ```

   Output:
   ```
   Test project /Users/cell/dev/JamWide/.claude/worktrees/agent-ac35e35395e0b18df/build-juce
       Start 26: video_grid_layout
   1/2 Test #26: video_grid_layout ................   Passed    0.01 sec
       Start 31: video_tile_member_order
   2/2 Test #31: video_tile_member_order ..........   Passed    0.03 sec

   100% tests passed, 0 tests failed out of 2
   ```

3. **MEMBER-ORDER CONTRACT manual confirmation (`grep -rq "subscription_;" juce/ui/video/*.h`):**

   ```
   juce/ui/video/SelfVideoTile.h:83:    JamWideFrameDistributor::Subscription subscription_;
   juce/ui/video/RemotePeerTile.h:66:    JamWideRemoteFrameDistributor::Subscription subscription_;
   ```

   Both are the LAST declared private member in their respective class bodies. The runtime offset probe in `test_video_tile_member_order` reports actual offsets:
   - `SelfVideoTile::subscription_` at offset **472** bytes (x86_64 macOS Release)
   - `RemotePeerTile::subscription_` at offset **328** bytes (x86_64 macOS Release)

4. **M5 sentinel fully eliminated:**

   ```
   grep -rq "kSelfTileSentinel" juce/ui/video/   → no matches
   grep -rq '"__self__"' juce/ui/video/          → no matches
   ```

5. **No scope creep (22-02 / 22-04 territory):**

   ```
   grep -rn "addAndMakeVisible\|setBounds" juce/ui/video/      → no matches (grid container = 22-02)
   grep -rn "setProperty\|setStateInformation" juce/ui/video/  → no matches (state = 22-04)
   ```

## MEMBER-ORDER CONTRACT enforcement mechanism

**Friend probe + runtime offset comparison.** Both tile headers declare `friend class VideoTileMemberOrderProbe;` inside `#ifdef JAMWIDE_BUILD_TESTS` (no production ABI impact). The probe class — defined in `tests/test_video_tile_member_order.cpp` — exposes static accessors that compute `reinterpret_cast<const char*>(&t.member_) - reinterpret_cast<const char*>(&t)` for each private member. The test asserts `selfSubscriptionOffset > {pendingMu_, pendingFrame_, currentMu_, currentFrame_, hovering_}` and `remoteSubscriptionOffset > {username_, chidx_, hovering_}` — proving by construction that `subscription_`'s dtor (which blocks on in-flight callbacks) runs FIRST in reverse-declaration-order destruction, before any upstream mutex/frame/string member is torn down. Runtime offset comparison was chosen over `offsetof` because both tile classes inherit from `juce::Component`, making them non-standard-layout — `offsetof` is conditionally supported by the standard and emits a libc++ diagnostic on such types.

## Codex Review Fixes Applied

| Fix | What landed |
|-----|-------------|
| **M5 — typed callback** | `enum class VideoPopoutTargetKind { Self, RemotePeer }` + `struct VideoPopoutTarget { kind; username; }` in `VideoTileBase.h`. `SelfVideoTile::mouseDown` emits `{VideoPopoutTargetKind::Self, juce::String{}}`. `RemotePeerTile::mouseDown` emits `{VideoPopoutTargetKind::RemotePeer, username_}`. No `kSelfTileSentinel` constant exists anywhere; no `"__self__"` literal exists anywhere. T-22-MO-4 spoof surface closed by construction. |
| **L8 — invariant-based 9-peer test** | `test_nine_peers_fit_invariants` asserts `cols∈[1,4]`, `cols*rows >= 9` (capacity), `cols*tileW + (cols+1)*spacing <= 800` (width-fit) AND `rows*tileH + (rows+1)*spacing <= 280` (height-fit) when `!needsScroll`. Does NOT pin exact column count — algorithm discretion preserved. |
| **L9 — UTF-8 encoding gate** | `python3 -c "[open(f,'rb').read().decode('utf-8') for f in [...]]; print('OK')"` returns OK for all four new `.cpp`/`.h` files. The `↗` glyph (U+2197) is embedded in `paintCommon`'s diagonal-line render code via a UTF-8 comment; no string literal contains the codepoint, but the file encoding gate still applies because the L9 codex concern was about file-encoding hygiene generally. |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Code Quality] Replaced deprecated `juce::Font(float)` with `juce::Font(juce::FontOptions(float))`**

- **Found during:** Task 3 build (linking the tile sources into `test_video_tile_member_order`)
- **Issue:** JUCE 8 marked the `juce::Font(float)` ctor `[[deprecated("Use the constructor that takes a FontOptions argument")]]`. My Task 2 commit's `paintCommon` body used the deprecated form in two places. Compiler emitted 2 `-Wdeprecated-declarations` warnings. The plan's verify gate is `cmake --build` success, which doesn't fail on warnings, but warnings on freshly-added code are a Rule 1 quality fix.
- **Fix:** Replaced both call sites with `juce::Font(juce::FontOptions(12.0f))` and `juce::Font(juce::FontOptions(11.0f))`.
- **Files modified:** `juce/ui/video/VideoTileBase.cpp` (2 lines).
- **Commit:** Folded into Task 3 commit `a5aa7b2`.

**2. [Plan-level adjustment, not a deviation per se] Layout-solver tie-breaker for the doesn't-fit bucket**

- **Found during:** Task 1 RED gate (test N=2 @ 800×280 failed because both cols=1 and cols=2 had `totalH > 280`, but the original "prefer larger tileW" rule picked cols=1 — wasting screen real estate on a single tile when the user clearly expected side-by-side).
- **Resolution:** Updated `computeGridLayout` to use two-tier selection: prefer any fitting layout over a non-fitting one (unchanged), then within the same bucket prefer larger tileW IF the layout fits, but prefer larger cols IF the layout doesn't fit. This makes 2 peers at 800×280 produce cols=2 (side-by-side, slight vertical overflow signalled via `needsScroll`) instead of cols=1 (one tile, massive overflow).
- **Documented in:** `computeGridLayout.h` header comment and commit message body for `78f2780`.

### Notes / Out-of-Scope Observations

- **`src/build_number.h` was already dirty at session start** (`gitStatus` listed it as `M`). The CMake `jamwide-build-number` target auto-increments this file every build. It is NOT included in any of my commits — it's tracked as a project-level untracked artifact that gets bumped per build (see project memory `project_build_number_automation`).
- **Submodules were uninitialised in the worktree.** Ran `git submodule update --init --recursive` once at the start of Task 1 to materialise `libs/juce`, `libs/libflac`, `libs/libogg`, `libs/libvorbis`, `libs/ixwebsocket`. This is a one-time worktree setup step; no source files were modified.
- **No `addAndMakeVisible`, no `setBounds`, no state-persistence calls in `juce/ui/video/`** — confirmed by the plan verification gates 5 and 6. The grid-container mounting + state persistence are correctly scoped to Plans 22-02 and 22-04.

## Authentication Gates

None. This plan ships pure tile + layout code with no network, codec, or platform-API calls.

## Threat Flags

No new security-relevant surface introduced beyond what the plan's `<threat_model>` already documented. T-22-MO-1/2/3/4 all addressed:

- **T-22-MO-1** (member ordering) — mitigated by `test_video_tile_member_order` static-runtime offset check.
- **T-22-MO-2** (bufferLock held during paint) — mitigated by snapshot-then-release pattern in `RemotePeerTile::paint` (line 64-68 of RemotePeerTile.cpp: `juce::ScopedLock` scope ends before `paintCommon` is called).
- **T-22-MO-3** (stale self-frame after state-machine transition) — Phase 19 substrate handles; this plan inherits the property.
- **T-22-MO-4** (magic-string spoof surface) — eliminated by the M5 typed `VideoPopoutTargetKind` enum.

## Known Stubs

None. Every line ships behaviour, not placeholder.

## Self-Check: PASSED

**Created files (all exist):**

- FOUND: juce/ui/video/computeGridLayout.h
- FOUND: juce/ui/video/VideoTileBase.h
- FOUND: juce/ui/video/VideoTileBase.cpp
- FOUND: juce/ui/video/SelfVideoTile.h
- FOUND: juce/ui/video/SelfVideoTile.cpp
- FOUND: juce/ui/video/RemotePeerTile.h
- FOUND: juce/ui/video/RemotePeerTile.cpp
- FOUND: tests/test_video_grid_layout.cpp
- FOUND: tests/test_video_tile_member_order.cpp

**Commits (all exist on worktree-agent-ac35e35395e0b18df):**

- FOUND: 78f2780 (Task 1)
- FOUND: 380a935 (Task 2)
- FOUND: a5aa7b2 (Task 3)

**Tests (both pass):**

- video_grid_layout: 8/8 PASS
- video_tile_member_order: 4/4 PASS

**JamWideJuce_VST3 still builds cleanly** with the 6 new tile sources linked into the production VST3 target. Build number incremented to 340 (build-juce/) — the plan does not own this state and no commit touches `src/build_number.h`.
