---
phase: 22-native-video-ui-grid-popouts
plan: 03
subsystem: ui-video-popouts-detached-grid
tags: [video, ui, juce, popout, detached-grid, placeholder, h3-4-state-truth-table, m5-typed-dispatch, m7-dual-band-sync, h2-narrowed-stdunordered_map, t-22-mm-multimonitor, disp-02, disp-03, wave-3]
requires:
  - phase: 22-01
    provides: VideoTileBase + VideoPopoutTarget + RemotePeerTile (with Phase 21 Subscription) — independent Subscriptions per popout (DISP-03)
  - phase: 22-02
    provides: VideoGridBand with Mode enum + setPeerPoppedOut/setDetachedActive declared (bodies wired in this plan) + onDetachRequested/onPeerPopoutRequested callback surface
  - phase: 21
    provides: JamWideRemoteFrameDistributor::Subscription + PeerVideoSink::addListener (D-06 multi-subscriber per peer)
  - phase: 19
    provides: CameraPreviewWindow (D-09 self-popout reuse path — NO second self-popout window created)
provides:
  - juce/ui/video/RemotePeerPopoutWindow.{h,cpp}        # per-peer DocumentWindow; 4:3 aspect-lock; T-22-MM clamp
  - juce/ui/video/DetachedGridWindow.{h,cpp}            # singleton whole-grid DocumentWindow; M7 Mode::DetachedBand inner
  - juce/ui/video/PopoutPlaceholderCard.{h,cpp}         # per-peer "Popped out →" card with H3-exclusive bring-back
  - juce/ui/video/DetachedGridPlaceholderCard.{h,cpp}   # whole-band "Grid is in detached window →" card
  - JamWideJuceEditor::openOrToggleRemotePopout        # H3 4-state truth table + M5 typed dispatch + M7 dual-band sync
  - JamWideJuceEditor::bringBackRemotePopout           # codex H3 EXCLUSIVE destroy path; M7 dual-band sync
  - JamWideJuceEditor::openOrToggleDetachedGrid        # M7 replay-into-new-detached-band on open
  - JamWideJuceEditor::reattachGrid                    # destroy detached-grid window + clear setDetachedActive
  - VideoGridBand::setPeerPoppedOut + setDetachedActive bodies  # Plan 22-02 declared the surface; this plan fills the bodies
  - tests/test_remote_peer_popout_lifetime.cpp         # 7 tests covering H3 4-state + DISP-03 + T-22-MM + RAII teardown
affects:
  - 22-04 (state persistence — will swap the editor's getInitialPopoutBounds + getInitialDetachedGridBounds stubs for processor accessors and wire onBoundsChanged to setRemotePopoutBounds)
tech-stack:
  added: []
  patterns:
    - "H3 4-state truth table state machine — (A) absent → CREATE+SHOW; (B) visible → HIDE; (C) hidden → RE-SHOW non-destructive; (D) destroyed = absent. Placeholder click is the EXCLUSIVE destroy path."
    - "M5 typed VideoPopoutTarget dispatch via enum class kind — NO magic-string sentinel anywhere in editor"
    - "M7 dual-band placeholder-state sync — editor drives setPeerPoppedOut on BOTH gridBand_ AND detachedGrid_->getGridBand() (when non-null)"
    - "M7 replay-into-new-detached-band on open — editor walks remotePopouts_ and replays setPeerPoppedOut(true) onto the new detached band so it starts up consistent with the main band"
    - "T-22-MM multi-monitor clamp at popout construct — intersect requested bounds against Desktop::getInstance().getDisplays(); ANY non-empty overlap = keep user-recoverable (intersects vs contains semantics); no overlap = primary-monitor-centered fallback"
    - "H2 NARROWED — std::unordered_map<juce::String, std::unique_ptr<T>> for the in-memory popout + placeholder maps (juce::HashMap::set copy-assigns at juce_HashMap.h:244; cannot hold move-only unique_ptr)"
    - "RESEARCH Pitfall 3 positional ordering — editor destructor clears popouts (3 lines) BEFORE the existing Phase 19 teardown body; setLookAndFeel(nullptr) remains the LAST line"
    - "Friend-class test probe under #ifdef JAMWIDE_BUILD_TESTS — RemotePeerPopoutTestProbe accesses tilePtr_ + username_ without leaking them into production ABI"
key-files:
  created:
    - juce/ui/video/RemotePeerPopoutWindow.h
    - juce/ui/video/RemotePeerPopoutWindow.cpp
    - juce/ui/video/DetachedGridWindow.h
    - juce/ui/video/DetachedGridWindow.cpp
    - juce/ui/video/PopoutPlaceholderCard.h
    - juce/ui/video/PopoutPlaceholderCard.cpp
    - juce/ui/video/DetachedGridPlaceholderCard.h
    - juce/ui/video/DetachedGridPlaceholderCard.cpp
    - tests/test_remote_peer_popout_lifetime.cpp
  modified:
    - juce/ui/video/VideoGridBand.h         # placeholder state members + onPlaceholderBringBack callback
    - juce/ui/video/VideoGridBand.cpp       # setPeerPoppedOut + setDetachedActive bodies; resized() placeholder-vs-tile slot swap
    - juce/JamWideJuceEditor.h              # remotePopouts_ (std::unordered_map H2 NARROWED) + detachedGrid_ + 6 new methods + dtor decl
    - juce/JamWideJuceEditor.cpp            # band callback wiring + 6 new method bodies + Pitfall 3 dtor reorder
    - CMakeLists.txt                        # 4 new source pairs added to JamWideJuce + test_remote_peer_popout_lifetime block
key-decisions:
  - "codex H3 4-state truth table is the SINGLE source of truth for tile ↗ semantics. State (C) hidden→re-show via setVisible(true) is NON-DESTRUCTIVE. The destroy path is exclusively the placeholder card click → bringBackRemotePopout → unique_ptr.reset() → ~RemotePeerTile → ~Subscription (which blocks for in-flight per Phase 21 D-06). This eliminates the iter-1 ambiguity (was ↗ supposed to destroy on the second click or re-show?) and prevents double-destroy UAF scenarios."
  - "codex M5 typed VideoPopoutTarget dispatch — switching on t.kind is the SINGLE source of truth in openOrToggleRemotePopout; NO magic-string sentinel anywhere. T-22-MO-4 magic-string spoof surface closed by construction."
  - "codex M7 dual-band sync — every open/close/bring-back path drives setPeerPoppedOut on BOTH gridBand_ AND (when non-null) detachedGrid_->getGridBand(). Plus the M7 replay loop in openOrToggleDetachedGrid walks remotePopouts_ and replays current state onto the freshly-constructed detached band so it starts up consistent with the main band."
  - "codex H2 NARROWED — std::unordered_map (NOT juce::HashMap) for remotePopouts_ + peerPlaceholders_. juce::HashMap::set() does copy-assign (juce_HashMap.h:244); cannot hold move-only std::unique_ptr<T>. The juce/midi/MidiMapper.h:105 precedent proves std::unordered_map<juce::String, V> compiles cleanly (JUCE 8 ships std::hash<juce::String>). H2's narrower juce::HashMap ground is preserved for Plan 22-04's copyable Rectangle<int>-valued bounds map where deterministic iteration matters for diffable saved XML."
  - "RESEARCH Pitfall 3 positional ordering — editor destructor clears the popout map + detached grid + grid band FIRST (3 lines), then the existing Phase 19 teardown body, with setLookAndFeel(nullptr) as the LAST line. The popouts hold raw LookAndFeel pointers via setLookAndFeel; their destructors call setLookAndFeel(nullptr) but require the editor's lookAndFeel member to still be alive. Without this ordering, the editor would tear down its LookAndFeel before the popout destructors finished, producing a UAF."
  - "D-09 Self-popout reuses Phase 19's CameraPreviewWindow — editor's openOrToggleRemotePopout Self branch calls drivePreviewWindowVisibility(Capturing) and returns. NO RemotePeerPopoutWindow is ever created for VideoPopoutTargetKind::Self — that would double-subscribe to the camera and violate the Phase 19 subscriber MEMBER-ORDER assumption."
  - "T-22-MM multi-monitor clamp uses .intersects (any overlap) not .contains (full containment). Partial-overlap windows stay user-recoverable; fully-off-screen windows fall back to primary-monitor-centered default. Test 4 of test_remote_peer_popout_lifetime exercises the fallback path via obviously-off-screen bounds (-99999,-99999,320,240)."
  - "UTF-8 polish — switched title literals from juce::String(\"...\\xE2\\x80\\x94...\") (asserts at juce_String.cpp:327 because >127 bytes get treated as ASCII) to juce::String::fromUTF8(\"...\"). Applies to RemotePeerPopoutWindow title, DetachedGridWindow title, placeholder card default labels, and the VideoGridBand.cpp ph->setLabel calls."
requirements-completed: [DISP-02, DISP-03]
duration: ~50min
completed: 2026-05-18
---

# Phase 22 Plan 03: Native Video Popouts + Detached Grid + Placeholder State Machine

**Wave-3 deliverable** — per-peer DocumentWindow popouts + singleton whole-grid DocumentWindow + placeholder cards that swap in for live tiles when popouts open / grid detaches + the editor's H3 4-state truth-table state machine with M5 typed dispatch and M7 dual-band placeholder sync. All four codex review closures (H2 NARROWED, H3 4-state, M5 typed, M7 dual-band) land in code and are mechanically gated.

## Commits

| Task | Name                                                              | Commit    | Files                                                                                                                                                                              |
| ---- | ----------------------------------------------------------------- | --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | RemotePeerPopoutWindow + DetachedGridWindow (M7 + T-22-MM)        | `1d0e917` | juce/ui/video/RemotePeerPopoutWindow.{h,cpp}, juce/ui/video/DetachedGridWindow.{h,cpp}, CMakeLists.txt                                                                            |
| 2    | Popout state machine (H3 4-state + M5 typed + M7 dual-band)       | `224f57d` | juce/ui/video/{PopoutPlaceholderCard,DetachedGridPlaceholderCard}.{h,cpp}, juce/ui/video/VideoGridBand.{h,cpp}, juce/JamWideJuceEditor.{h,cpp}, CMakeLists.txt                    |
| 3    | test_remote_peer_popout_lifetime (H3 4-state + DISP-03 RAII)       | `f286054` | tests/test_remote_peer_popout_lifetime.cpp, juce/ui/video/RemotePeerPopoutWindow.cpp + DetachedGridWindow.cpp + PopoutPlaceholderCard.h + DetachedGridPlaceholderCard.h + VideoGridBand.cpp, CMakeLists.txt |

## Verification

### Automated (per plan `<verification>`)

1. **Plan 22-01 + 22-02 baselines still pass:**
   ```
   ctest -R "video_grid_layout|video_tile_member_order" --output-on-failure
   → 2/2 PASS (Test #26 video_grid_layout, Test #31 video_tile_member_order)
   ```

2. **New Plan 22-03 test passes:**
   ```
   ctest -R "^remote_peer_popout_lifetime$" --output-on-failure
   → 1/1 PASS (Test #32 remote_peer_popout_lifetime; ~1.1s)

   Direct binary run (7/7 sub-tests):
   PASS [testConstructAndDestroy — state A→D minimal cycle clean]
   PASS [testCloseButtonHides — state B→C, onCloseRequested fires]
   PASS [testReshowAfterClose — state C→B RE-SHOW is non-destructive]
   PASS [testMultiMonitorClamp — off-screen bounds get clamped to primary]
   PASS [testTwoListenersOneSink — DISP-03 independent Subscriptions]
   PASS [testBringBackDestroysWindow — state C→D destroy via .reset()]
   PASS [testStateTransitionsH3 — full A→B→C→B→C→D cycle clean]
   results: 7 passed, 0 failed
   ```

3. **All three Phase 22 tests in one run:**
   ```
   ctest -R "video_grid_layout|video_tile_member_order|remote_peer_popout" --output-on-failure
   → 3/3 PASS, 1.42s total
   ```

4. **Build cleanly:**
   ```
   cmake --build build-juce --target JamWideJuce_VST3 JamWideJuce_Standalone test_remote_peer_popout_lifetime
   → all three targets build successfully on macOS x86_64 (Debug)
   ```

5. **codex H2 NARROWED — std::unordered_map for in-memory unique_ptr storage:**
   ```
   grep -q "std::unordered_map<juce::String, std::unique_ptr<jamwide::RemotePeerPopoutWindow>>" juce/JamWideJuceEditor.h  → MATCH
   ! grep -E "juce::HashMap<juce::String, std::unique_ptr<jamwide::RemotePeerPopoutWindow"   juce/JamWideJuceEditor.h     → NO MATCH (would fail compile)
   grep -q "std::unordered_map<juce::String, std::unique_ptr<jamwide::PopoutPlaceholderCard>>" juce/ui/video/VideoGridBand.h → MATCH
   ! grep -E "juce::HashMap<juce::String, std::unique_ptr<jamwide::PopoutPlaceholderCard"     juce/ui/video/VideoGridBand.h → NO MATCH
   awk '/openOrToggleDetachedGrid/,/^}/' juce/JamWideJuceEditor.cpp | grep -q "for.*auto.*remotePopouts_"   → MATCH (range-for, not HashMap::Iterator)
   ! grep -q "HashMap.*Iterator" juce/JamWideJuceEditor.cpp                                                  → NO MATCH
   ```

6. **codex H3 4-state truth table:**
   ```
   awk '/openOrToggleRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp | grep -q "popout->isVisible()"   → MATCH (the (B) vs (C) branch)
   awk '/openOrToggleRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp | grep -q "popout->setVisible(false)" → MATCH (state B→C hide)
   awk '/openOrToggleRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp | grep -q "popout->setVisible(true)"  → MATCH (state C→B re-show)
   awk '/openOrToggleRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp | grep -qE "re-show|RE-SHOW|state.*C" → MATCH (comment documents H3 disambiguation)
   awk '/^void JamWideJuceEditor::bringBackRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp | grep -q "it->second.reset()" && grep -q "remotePopouts_.erase"  → MATCH (state C→D EXCLUSIVE destroy)
   ```

7. **codex M5 typed dispatch:**
   ```
   grep -qE "openOrToggleRemotePopout\(jamwide::VideoPopoutTarget" juce/JamWideJuceEditor.h   → MATCH (typed signature)
   awk '/openOrToggleRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp | grep -qE "VideoPopoutTargetKind::Self|t.kind == jamwide::VideoPopoutTargetKind"  → MATCH (.kind switch)
   ! grep -q "kSelfTileSentinel" juce/JamWideJuceEditor.cpp   → NO MATCH (sentinel eliminated)
   ! grep -q '"__self__"' juce/JamWideJuceEditor.cpp           → NO MATCH (sentinel eliminated)
   ```

8. **codex M7 dual-band sync:**
   ```
   awk '/^void JamWideJuceEditor::openOrToggleRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp | grep -q "detachedGrid_->getGridBand"   → MATCH (open path)
   awk '/^void JamWideJuceEditor::bringBackRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp | grep -q "detachedGrid_->getGridBand"       → MATCH (bring-back path)
   awk '/^void JamWideJuceEditor::openOrToggleDetachedGrid/,/^}/' juce/JamWideJuceEditor.cpp | grep -q "remotePopouts_" && grep -q "setPeerPoppedOut.*true"  → MATCH (M7 replay loop)
   grep -q "VideoGridBand::Mode::DetachedBand" juce/ui/video/DetachedGridWindow.cpp   → MATCH (inner band uses DetachedBand)
   grep -q "getGridBand" juce/ui/video/DetachedGridWindow.h                            → MATCH (accessor declared)
   ```

9. **RESEARCH Pitfall 3 positional ordering — editor destructor clears popouts BEFORE setLookAndFeel(nullptr):**
   ```
   awk '/JamWideJuceEditor::~JamWideJuceEditor/,/^}/' juce/JamWideJuceEditor.cpp \
     | grep -n -E 'remotePopouts_\.clear|detachedGrid_\.reset|gridBand_\.reset|setLookAndFeel\(nullptr\)' \
     | python3 -c "..."   → OK clears=[17, 18, 19] setLookAndFeel=[32]
   ```

10. **T-22-MM multi-monitor clamp — Pattern 3 implemented in both windows:**
    ```
    grep -q "Desktop::getInstance().getDisplays" juce/ui/video/RemotePeerPopoutWindow.cpp   → MATCH
    grep -q "Desktop::getInstance().getDisplays" juce/ui/video/DetachedGridWindow.cpp       → MATCH
    Test 4 of test_remote_peer_popout_lifetime exercises (-99999,-99999,320,240) → fallback
    ```

### Manual / live UAT (Task 4 checkpoint — DEFERRED)

Task 4 is a `checkpoint:human-verify` task with 8 test cells (A through H). Same defer pattern as Plan 22-02 Task 3 — all 8 cells require either a live peer broadcasting H264 at `video.ninjamzap.com:2049` OR a multi-monitor desktop setup. None are runnable inside the worktree-agent environment.

| Cell | What it tests                                                       | Automatable? | Status                                                            |
| ---- | ------------------------------------------------------------------- | ------------ | ----------------------------------------------------------------- |
| A    | Per-peer popout open/hide/destroy (DISP-02 + codex H3 4-state)      | Partial      | Test 7 (testStateTransitionsH3) covers the state machine; visual confirm DEFERRED to operator |
| B    | Detached-grid open/close (DISP-02 detached + M7 no-recursive-detach) | Partial      | Plan 22-02 Task 1 paint() Mode-conditional verified by grep; visual confirm DEFERRED |
| C    | DISP-03 coexistence + codex M7 dual-band sync                       | Partial      | Test 5 (testTwoListenersOneSink) verifies multi-Subscription; live coexistence DEFERRED |
| D    | Multi-monitor placement (DISP-02 multi-monitor / T-22-MM)           | Partial      | Test 4 (testMultiMonitorClamp) verifies the clamp logic; live multi-monitor DEFERRED |
| E    | Self-tile popout reuses Phase 19 CameraPreviewWindow (D-09 + M5)    | Partial      | grep gate confirms drivePreviewWindowVisibility call + no sentinel; visual DEFERRED |
| F    | Toggle the band while popouts are open (DISP-04 + DISP-03)          | No (UAT)     | DEFERRED — needs live broadcast                                   |
| G    | Editor close with open popouts (Pitfall 3)                          | Partial      | Test 1 (testConstructAndDestroy) + dtor positional grep cover the static case; live full-DAW-close DEFERRED |
| H    | M7 replay-on-open-detached                                          | No (UAT)     | DEFERRED — needs ≥2 live peers + visual confirm                   |

The DEFERRED cells should be folded into Plan 22-04's manual UAT cycle since that's the next plan in the wave AND it already has a live-UAT checkpoint as part of state-persistence verification. Plan 22-04's UAT will inherit:

- Plan 22-02's deferred cells (8 cells A-H from `22-02-SUMMARY.md`)
- Plan 22-03's deferred cells (8 cells A-H from this summary)

That's the natural place for the consolidated Phase 22 live-UAT report since Plan 22-04 wires state persistence (the final missing piece for the user-facing experience) and adds a `checkpoint:human-verify` task that the live UAT folds into.

### Critical regression checks (mechanically verified)

Three regression modes called out in the plan are CLOSED in code:

1. **H3 regression** — "if tile ↗ on hidden popout destroys instead of re-shows, that's a state-(C) regression."
   `openOrToggleRemotePopout` checks `popout->isVisible()` and branches: visible→hide, hidden→`setVisible(true)`. The destroy path is only inside `bringBackRemotePopout` (which is called from the placeholder card's `onBringBack`, NOT from tile ↗). Confirmed by `awk` slice + `grep` on the editor's openOrToggleRemotePopout body and by Test 3 (`testReshowAfterClose`) + Test 7 (`testStateTransitionsH3`).

2. **M7 dual-band sync regression** — "if popping out a peer while the detached grid is open leaves the detached band showing the live tile, M7 is broken."
   `openOrToggleRemotePopout`'s state-(A) construct block calls `setPeerPoppedOut(username, true)` on BOTH `gridBand_` AND (when non-null) `detachedGrid_->getGridBand()`. `bringBackRemotePopout` mirrors this for the destroy path. Confirmed by `awk` slice + `grep` on both function bodies.

3. **M7 replay regression** — "if opening the detached grid while popouts are already open leaves the detached band showing live tiles for popped-out peers, the replay loop is broken."
   `openOrToggleDetachedGrid`'s post-construct block walks `remotePopouts_` via C++17 structured-binding range-for and calls `detachedBand->setPeerPoppedOut(username, true)` for every active popout. Confirmed by `awk` slice + `grep`.

## Threat Flags

No new security-relevant surface introduced beyond what the plan's `<threat_model>` documents. T-22-MM (multi-monitor topology change) + T-22-LT-1 (editor destructor order) + T-22-LT-2 (two popouts same peer) all mitigated:

- **T-22-MM** — `clampToVisibleDisplays_` helper in both windows; Test 4 verifies fallback.
- **T-22-LT-1** — Editor destructor's `remotePopouts_.clear()` → `detachedGrid_.reset()` → `gridBand_.reset()` precede `setLookAndFeel(nullptr)`; positional grep gate confirms.
- **T-22-LT-2** — Phase 21 D-06 multi-subscriber per sink; Test 5 verifies independent Subscriptions across two popouts for the same peer.

## Known Stubs

`getInitialPopoutBounds(username)` and `getInitialDetachedGridBounds()` are intentional Plan 22-03 stubs returning hardcoded defaults `{100,100,320,240}` and `{200,200,800,450}` respectively. Plan 22-04 swaps them for `processorRef.getRemotePopoutBounds(username)` and `processorRef.getDetachedGridBounds()`. The defaults are NOT display-aware at this level — the windows' constructors perform the `Desktop::getDisplays()` clamp (T-22-MM mitigation) at construction time, so stale persisted bounds on a now-disconnected monitor still get clamped to the primary display.

`popout->onCloseRequested` and `popout->onBoundsChanged` lambdas in `openOrToggleRemotePopout` are no-op stubs in this plan (the close lambda uses `juce::ignoreUnused(capturedUsername)`; the bounds lambda uses `juce::ignoreUnused(r)`). Plan 22-04 replaces both with real processor accessors. This is documented in code comments at the call sites.

These stubs do not prevent the plan's user-facing goal — clicking ↗ creates a popout, clicking the popout's X hides it, clicking the placeholder card destroys it, and the H3 4-state truth table works end-to-end. Persistence (saving bounds across plugin reloads) is explicitly Plan 22-04's territory per Plan 22-03's depends-on chain.

## Files Created/Modified

**Created (9 files):**

- `juce/ui/video/RemotePeerPopoutWindow.h` / `.cpp` — per-peer DocumentWindow; 4:3 aspect-locked; wraps a fresh RemotePeerTile via `setContentOwned`; T-22-MM multi-monitor clamp at construction; closeButtonPressed HIDES (D-17); UTF-8 title via `juce::String::fromUTF8`.
- `juce/ui/video/DetachedGridWindow.h` / `.cpp` — singleton whole-grid DocumentWindow; NOT aspect-locked; constructs inner VideoGridBand with `Mode::DetachedBand` (codex M7 critical); exposes `getGridBand()` accessor (non-owning) for editor-side dual-band sync; T-22-MM multi-monitor clamp.
- `juce/ui/video/PopoutPlaceholderCard.h` / `.cpp` — per-peer "Popped out →" card; click invokes `onBringBack` (the codex H3 EXCLUSIVE destroy path); dark VB-style rounded-rectangle backdrop matching surrounding chrome.
- `juce/ui/video/DetachedGridPlaceholderCard.h` / `.cpp` — whole-band "Grid is in detached window →" card; near-clone of `PopoutPlaceholderCard` with larger font + default label.
- `tests/test_remote_peer_popout_lifetime.cpp` — 7 lifetime + state-machine sub-tests covering codex H3 4-state truth table, T-22-MM clamp fallback, DISP-03 multi-Subscription, and `unique_ptr.reset()` destroy path RAII.

**Modified (5 files):**

- `juce/ui/video/VideoGridBand.h` — added `#include "PopoutPlaceholderCard.h"` + `"DetachedGridPlaceholderCard.h"` + `<unordered_set>`; added `onPlaceholderBringBack` callback (`std::function<void(const juce::String&)>` — empty username = reattach, non-empty = bringBack); added private state members `poppedOutPeers_` (std::unordered_set), `detachedActive_` bool, `peerPlaceholders_` (std::unordered_map per H2 NARROWED), `detachedPlaceholder_` unique_ptr.
- `juce/ui/video/VideoGridBand.cpp` — implemented `setPeerPoppedOut(username, poppedOut)` (lazy-create placeholder card, mutate `poppedOutPeers_` set, call `resized()`) and `setDetachedActive(active)` (lazy-create `detachedPlaceholder_`, flip flag, call `resized()`). Rewrote `resized()` body to swap placeholder-vs-tile per slot based on `poppedOutPeers_` membership AND full-band-detach branch when `detachedActive_`. Switched literal labels to `juce::String::fromUTF8(...)` to avoid juce_String.cpp:327 ASCII assertion.
- `juce/JamWideJuceEditor.h` — added `#include "ui/video/{VideoTileBase,RemotePeerPopoutWindow,DetachedGridWindow}.h"` + `<unordered_map>`; added 6 new method declarations (`openOrToggleRemotePopout(VideoPopoutTarget)`, `bringBackRemotePopout(juce::String)`, `openOrToggleDetachedGrid()`, `reattachGrid()`, `getInitialPopoutBounds(juce::String)`, `getInitialDetachedGridBounds()`); added private members `remotePopouts_` (std::unordered_map H2 NARROWED) + `detachedGrid_` (unique_ptr).
- `juce/JamWideJuceEditor.cpp` — wired band callbacks (`onDetachRequested` → `openOrToggleDetachedGrid`; `onPeerPopoutRequested` → `openOrToggleRemotePopout` (typed M5 lambda); `onPlaceholderBringBack` → empty-username = `reattachGrid()` / non-empty = `bringBackRemotePopout(...)`); implemented all 6 new method bodies with codex H3 4-state state machine + M5 typed dispatch + M7 dual-band sync + M7 replay-into-new-detached-band; rewrote destructor with RESEARCH Pitfall 3 positional ordering (3 clear/reset lines BEFORE the existing Phase 19 teardown; setLookAndFeel(nullptr) LAST). Switched popout title literals to `juce::String::fromUTF8`.
- `CMakeLists.txt` — added 4 new source pairs to `target_sources(JamWideJuce PRIVATE ...)` (RemotePeerPopoutWindow + DetachedGridWindow + PopoutPlaceholderCard + DetachedGridPlaceholderCard); added new `juce_add_console_app(test_remote_peer_popout_lifetime)` block mirroring `test_video_tile_member_order` with the appropriate test-source set + `JAMWIDE_BUILD_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1` defines + njclient/juce_core/juce_events/juce_graphics/juce_gui_basics links + `add_test(NAME remote_peer_popout_lifetime)`.

## Decisions Made

1. **codex H3 state-(C) RE-SHOW is non-destructive.** Hitting tile ↗ while the popout is hidden invokes `setVisible(true)` — bounds + RemotePeerTile + Subscription are preserved. Destroy is exclusively via placeholder card click → `bringBackRemotePopout`. This makes "bring back" the single predictable destroy path and prevents double-destroy UAF scenarios. Documented in code comments at the editor's `openOrToggleRemotePopout` body AND tested explicitly by Test 3 (`testReshowAfterClose`) + Test 7 (`testStateTransitionsH3`).

2. **codex M5 typed VideoPopoutTarget dispatch.** Editor's `openOrToggleRemotePopout(jamwide::VideoPopoutTarget t)` switches on `t.kind` directly. NO `kSelfTileSentinel` / `"__self__"` literal anywhere. The enum is the single source of truth — a peer whose NINJAM username happens to collide with any sentinel literal cannot spoof the Self branch (T-22-MO-4 mitigation by construction).

3. **codex M7 dual-band placeholder sync.** Every open/close/bring-back path drives `setPeerPoppedOut` on BOTH `gridBand_` AND (when non-null) `detachedGrid_->getGridBand()`. PLUS the M7 replay loop in `openOrToggleDetachedGrid` walks `remotePopouts_` and replays current popout state onto the freshly-constructed detached band so it starts up consistent with the main band. Without these three sync points, opening the detached grid OR creating a popout while the other is active produces inconsistent placeholder state across surfaces.

4. **codex H2 NARROWED — std::unordered_map for in-memory unique_ptr value-type maps.** `juce::HashMap::set()` does copy-assign at `juce_HashMap.h:244`; cannot hold move-only types. `juce/midi/MidiMapper.h:105` is the precedent proving `std::unordered_map<juce::String, V>` compiles cleanly in this codebase (JUCE 8 ships `std::hash<juce::String>`). The narrower juce::HashMap ground is preserved for Plan 22-04's copyable `Rectangle<int>`-valued bounds map where deterministic iteration matters for diffable saved XML.

5. **RESEARCH Pitfall 3 positional destructor ordering.** Editor destructor clears popouts (3 lines: `remotePopouts_.clear()` + `detachedGrid_.reset()` + `gridBand_.reset()`) BEFORE the existing Phase 19 teardown body. `setLookAndFeel(nullptr)` remains the LAST line. Without this, the editor would tear down its LookAndFeel before the popout destructors finished, producing a UAF on the raw `juce::LookAndFeel*` pointer the popouts hold.

6. **D-09 Self-popout reuse — NO second self-popout window.** Editor's `openOrToggleRemotePopout` Self branch calls `drivePreviewWindowVisibility(jamwide::CameraState::Capturing)` and returns. No `RemotePeerPopoutWindow` is ever created for `VideoPopoutTargetKind::Self`. Creating one would double-subscribe to the camera and violate the Phase 19 subscriber MEMBER-ORDER assumption.

7. **T-22-MM uses .intersects (not .contains).** Multi-monitor clamp keeps partial-overlap windows user-recoverable instead of force-recentring them on the primary display. Only fully-off-screen windows fall back to primary-monitor-centered default. The `intersectsRectangle` semantics is the right UX choice — users who deliberately position a window straddling two monitors should not have it snap back to one.

8. **UTF-8 polish.** All title + label literals constructed from byte sequences with >127 bytes use `juce::String::fromUTF8(...)` instead of the `juce::String(const char*)` ASCII-assuming ctor. The latter asserts at `juce_String.cpp:327` (caught during Task 3 test runs). Touches RemotePeerPopoutWindow title, DetachedGridWindow title, both placeholder card default labels, and VideoGridBand's `setPeerPoppedOut` / `setDetachedActive` body label calls. The header-icon U+2197 arrow at VideoGridBand.cpp:307 was added in Plan 22-02 and is outside this plan's scope.

## Deviations from Plan

None — plan executed exactly as written.

The only minor judgement call was the UTF-8 polish (decision 8 above): the plan does NOT explicitly mandate `juce::String::fromUTF8`, but it's a clear-cut Rule 1 quality fix for the assertion noise that surfaced during Task 3 test runs. Documenting it as a decision rather than a deviation since it doesn't change the plan's content / behaviour, just suppresses an avoidable assertion.

## Issues Encountered

1. **Architecture mismatch on first cmake configure.** Default `cmake -B build-juce` produces an arm64 build but the vendored ffmpeg/openh264 are x86_64-only locally per project memory `project_local_build_setup`. Resolved by re-configuring with `-DCMAKE_OSX_ARCHITECTURES=x86_64 -DJAMWIDE_UNIVERSAL=OFF` (the same flags `./scripts/build.sh` passes by default on macOS).

2. **JUCE String ASCII assertion in tests.** First test run produced 8× `JUCE Assertion failure in juce_String.cpp:327` lines on stderr (one per popout title constructed with UTF-8 byte sequences via `juce::String(const char*)`). All tests still PASSED, but the assertion noise made the output cluttered. Fixed in the same Task 3 commit by switching to `juce::String::fromUTF8(...)` everywhere new code constructs strings with UTF-8 bytes (titles + placeholder labels).

3. **Pitfall 3 assertion grep false positive.** First check of the Pitfall 3 positional-ordering script flagged a "broken ordering" because the comment text in the dtor mentioned `setLookAndFeel(nullptr)` literally, which the grep treated as a code call. Fixed by paraphrasing the comment to "detach call" / "lookAndFeel teardown" instead of repeating the literal API call name; the actual call site moves stayed put.

## Next Phase Readiness

**Plan 22-04 (state persistence):** Wave-4 ready — `gridBandVisible_` + `gridBandHeight_` + popout bounds + detached-grid bounds are all editor or processor fields that need v5 ValueTree persistence. The plan will:

1. Swap `JamWideJuceEditor::getInitialPopoutBounds(username)` for `processorRef.getRemotePopoutBounds(username)` accessor calls. The processor adds a `juce::HashMap<juce::String, juce::Rectangle<int>>` member (the H2-narrowing point — copyable Rectangle<int> value type + deterministic XML iteration order).
2. Swap `JamWideJuceEditor::getInitialDetachedGridBounds()` for `processorRef.getDetachedGridBounds()`.
3. Wire `popout->onBoundsChanged` to `processorRef.setRemotePopoutBounds(username, r)` and `detachedGrid_->onBoundsChanged` to `processorRef.setDetachedGridBounds(r)`.
4. Bump plugin state schema v4 → v5 and add a new `<video>` ValueTree subtree (D-19) with `gridVisible` / `gridBandHeight` / `detachedGridBounds` / `popoutBounds`.
5. Add live UAT cycle that folds in Plan 22-02 + Plan 22-03's DEFERRED cells (all 16 cells of the joint Phase 22 UAT).

**Phase 22 closure:** After Plan 22-04 lands, Phase 22 is functionally complete. The next plan is Phase 23 (macOS universal + Windows build + codesign).

## Self-Check: PASSED

**Created files (all exist on `worktree-agent-a43e140b16d069302`):**

- FOUND: `juce/ui/video/RemotePeerPopoutWindow.h`
- FOUND: `juce/ui/video/RemotePeerPopoutWindow.cpp`
- FOUND: `juce/ui/video/DetachedGridWindow.h`
- FOUND: `juce/ui/video/DetachedGridWindow.cpp`
- FOUND: `juce/ui/video/PopoutPlaceholderCard.h`
- FOUND: `juce/ui/video/PopoutPlaceholderCard.cpp`
- FOUND: `juce/ui/video/DetachedGridPlaceholderCard.h`
- FOUND: `juce/ui/video/DetachedGridPlaceholderCard.cpp`
- FOUND: `tests/test_remote_peer_popout_lifetime.cpp`

**Commits (all exist on `worktree-agent-a43e140b16d069302`):**

- FOUND: `1d0e917` (Task 1 — RemotePeerPopoutWindow + DetachedGridWindow + M7 inner-band Mode::DetachedBand + T-22-MM clamp)
- FOUND: `224f57d` (Task 2 — popout state machine: H3 4-state + M5 typed + M7 dual-band + Pitfall 3 dtor)
- FOUND: `f286054` (Task 3 — test_remote_peer_popout_lifetime + UTF-8 polish)

**Tests (all PASS):**

- video_grid_layout: 8/8 PASS
- video_tile_member_order: 4/4 PASS
- remote_peer_popout_lifetime: 7/7 PASS  ← NEW this plan

**JamWideJuce_VST3 + JamWideJuce_Standalone both build cleanly** on macOS x86_64 (Debug) with all 9 new sources linked and the new test target included. No new linker warnings beyond the pre-existing duplicate `-rpath` ffmpeg warning that is benign and inherited from Phase 14.3.

**All automated success criteria from the plan PASS:**

- DISP-02 — per-peer popouts open as separate `juce::DocumentWindow`s; T-22-MM clamp prevents off-screen openings (Test 4).
- DISP-03 — band + popouts coexist via independent Phase 21 Subscriptions (Test 5).
- D-09 — self-tile popout reuses `previewWindow_`; NO second self-popout (grep `! kSelfTileSentinel`).
- D-17 — closeButtonPressed hides (Test 2 + grep on `setVisible(false)` inside closeButtonPressed body).
- D-18 — placeholder bring-back destroys (Test 6 + grep on `bringBackRemotePopout` containing `.reset()` + `.erase()`).
- T-22-MM mitigated — Test 4 verifies clamp fallback.
- T-22-LT-1 mitigated — Pitfall 3 positional grep on dtor passes.
- codex H2 NARROWED — std::unordered_map used; H2-narrowed precedent + `! juce::HashMap<juce::String, std::unique_ptr<...>>` grep both confirm.
- codex H3 — 4-state truth table in `openOrToggleRemotePopout`; state-(C) re-show non-destructive; Test 7 verifies all transitions.
- codex M5 — `.kind` switch; no magic-string anywhere in editor.
- codex M7 — `getGridBand()` accessor + `Mode::DetachedBand` inner band + dual-band sync in 3 places + replay-on-open.
- All 3 tasks complete (Tasks 1 + 2 + 3 committed atomically); Task 4 checkpoint cells DEFERRED to Plan 22-04 live UAT per plan-deferral protocol.
