# Phase 22 — `--reviews` Replan Notes

**Codex review concerns mapped to plan edits.** Created 2026-05-18 as the planner's working note while applying the 9 fixes (3 HIGH + 4 MEDIUM + 2 LOW) from `22-REVIEWS.md`.

This is NOT canon — the canonical changes are in the updated PLAN.md files. This file is the planner's audit trail so future maintainers can see the rationale.

## H1 — BotFilter extraction

**Codebase verified:** `juce/ui/ChannelStripArea.cpp:8-32` defines both `isBot` and `stripAtSuffix` inside an **anonymous namespace**, meaning internal linkage. The current Plan 22-02 `extern bool isBot(...)` approach would silently link to a stub (UB) or fail to link entirely. Refactor to `juce/ui/BotFilter.h`/`.cpp` is **mandatory** (not optional cleanup).

**Changes:**
- New file `juce/ui/BotFilter.h` — `namespace jamwide { bool isBot(const juce::String&); juce::String stripAtSuffix(const juce::String&); }`
- New file `juce/ui/BotFilter.cpp` — definitions moved verbatim out of `ChannelStripArea.cpp`'s anon namespace
- `ChannelStripArea.cpp` — DELETE the local `isBot` and `stripAtSuffix` definitions (lines 10-17, 26-30); `#include "BotFilter.h"`; replace bare calls with `jamwide::isBot(...)` / `jamwide::stripAtSuffix(...)`
- `VideoGridBand.cpp` — `#include "BotFilter.h"`; call `jamwide::isBot(...)` (Plan 22-02 Task 1)
- `JamWideJuceEditor.cpp` — `#include "ui/BotFilter.h"`; call `jamwide::isBot(...)` (Plan 22-02 Task 2 auto-open latch)
- `CMakeLists.txt` — add `juce/ui/BotFilter.h` and `juce/ui/BotFilter.cpp` to the `JamWideJuce` target_sources block

**Becomes Plan 22-02 Task 0** (Wave-0 prerequisite that runs BEFORE Tasks 1-3 since they all depend on the header).

## H2 — juce::HashMap<juce::String, ...>

**Codebase verified:**
- `juce/osc/OscAddressMap.h:65` uses `juce::HashMap<juce::String, int>` — closer analog
- `juce/midi/MidiMapper.h:105` uses `std::unordered_map<juce::String, int>` — works, but the JUCE precedent is the safer choice

**Changes (all 22-XX plans):**
- Replace every `std::unordered_map<juce::String, ...>` with `juce::HashMap<juce::String, ...>` in:
  - `juce/JamWideJuceProcessor.h` (`remotePopoutBoundsMap_`)
  - `juce/JamWideJuceEditor.h` (`remotePopouts_`)
  - `juce/ui/video/VideoGridBand.h` (`peerTiles_`, `peerPlaceholders_`)
  - `tests/test_plugin_state_v4_v5.cpp` (`V5VideoState::popoutBounds`)
- Iteration API differs: `juce::HashMap` uses `for (HashMap::Iterator it(map); it.next();)` not range-for. Update all iteration sites accordingly.
- `juce::HashMap::contains(key)` replaces `find(key) != end()`.
- Persistence ordering: `juce::HashMap` iteration order is hash-stable per session (good for diffability).

## H3 — Popout 4-state truth table

**Codex recommendation adopted:** re-show hidden popouts; "bring back" remains the destroy path.

**4-state truth table (formalized in Plan 22-03 Task 2):**

| State | Popout window | Tile `↗` click | Window X click | Placeholder click |
|-------|---------------|----------------|----------------|-------------------|
| (A) Absent | not created | CREATE+SHOW; placeholder mounts | n/a | n/a |
| (B) Visible | visible | HIDE; placeholder stays | HIDE; placeholder stays | RE-SHOW (rare path — placeholder usually hidden when popout visible) |
| (C) Hidden | hidden | RE-SHOW; placeholder stays | n/a | DESTROY; tile returns |
| (D) Destroyed | not created | (= A) | n/a | n/a |

**Changes:**
- Plan 22-03 Task 2 `openOrToggleRemotePopout(username)` body rewritten with explicit state check
- CONTEXT.md D-17 stays as-is ("Closing a popout HIDES the window") — D-18 stays as-is ("Bring back = destroy"). The 4-state table EXPANDS the implicit "popout exists but hidden + tile `↗` click" case which was previously ambiguous.
- Acceptance criteria updated to verify all 4 state transitions.
- New threat T-22-LT-3 added: "Tile `↗` clicked while popout is being destroyed by concurrent bring-back" — accept; placeholder and tile state are mutually exclusive in the band's `resized()` so the worst case is a no-op click.

## M4 — UAF risk on `MessageManager::callAsync([this]{...})`

**Codex recommendation adopted (option b):** lock-release-then-sync. The timer already runs on the message thread, so `callAsync` is unnecessary AND unsafe.

**Changes:**
- Plan 22-02 Task 2 auto-open latch in `JamWideJuceEditor::timerCallback()` rewritten:
  ```cpp
  bool shouldOpen = false;
  if (!gridAutoOpenLatchFired_.load(std::memory_order_acquire)) {
      if (auto* dist = processorRef.getRemoteFrameDistributor()) {
          std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
          for (const auto& u : processorRef.cachedUsers) {
              if (jamwide::isBot(juce::String(u.name))) continue;
              if (auto* sink = dist->findSink(u.name, /*chidx*/1)) {
                  if (sink->first_frame_seen.load(std::memory_order_acquire)) {
                      if (!gridAutoOpenLatchFired_.exchange(true)) {
                          shouldOpen = true;
                      }
                      break;
                  }
              }
          }
      }
  }
  if (shouldOpen) toggleGridBand(true);
  ```
- Acceptance: `! grep -q "MessageManager::callAsync" juce/JamWideJuceEditor.cpp` for the auto-open block (the editor uses `callAsync` elsewhere — the band-specific block must NOT use it).

## M5 — typed callback replaces magic string

**Codex pattern adopted verbatim:**
```cpp
enum class VideoPopoutTargetKind { Self, RemotePeer };
struct VideoPopoutTarget { VideoPopoutTargetKind kind; juce::String username; };
```

**Changes:**
- Plan 22-01 Task 2 `VideoTileBase.h` declares the enum + struct in `namespace jamwide`. DELETE `kSelfTileSentinel`.
- Plan 22-01 Task 2 `SelfVideoTile.cpp::mouseDown` → `if (onPopoutClicked_) onPopoutClicked_({VideoPopoutTargetKind::Self, {}});`
- Plan 22-01 Task 2 `RemotePeerTile.cpp::mouseDown` → `if (onPopoutClicked_) onPopoutClicked_({VideoPopoutTargetKind::RemotePeer, username_});`
- Plan 22-01 Task 2 `VideoTileBase.h` callback type: `std::function<void(VideoPopoutTarget)> onPopoutClicked_;`
- Plan 22-02 Task 1 `VideoGridBand`:
  - `onPeerPopoutRequested` callback type changes from `std::function<void(const juce::String&)>` to `std::function<void(VideoPopoutTarget)>`
  - Tile wiring becomes pass-through: `tile->onPopoutClicked_ = [this](VideoPopoutTarget t){ if (onPeerPopoutRequested) onPeerPopoutRequested(std::move(t)); };`
- Plan 22-03 Task 2 editor controller renamed: `openOrToggleRemotePopout(VideoPopoutTarget t)` switches on `t.kind`. Self branch calls `drivePreviewWindowVisibility(...)`; RemotePeer branch operates on `t.username`.
- All grep gates updated: `! grep -q '"__self__"'` becomes a stronger `! grep -rq '"__self__"' juce/ui/video/ juce/JamWideJuceEditor.cpp` (the literal must not appear anywhere).
- Plan 22-02 acceptance for VideoGridBand `! grep -q "kSelfTileSentinel" juce/ui/video/VideoGridBand.cpp` (constant DELETED, not renamed).

## M6 — production-vs-test drift defense

**Strategy adopted: option (a) — grep gate on production source.** Cheapest, no new files, defers the cleaner option (b) extraction to v1.4.

**Changes:**
- Plan 22-04 Task 1 acceptance gets a NEW source-level gate proving the production `<video>` save block names every field that V5VideoState declares:
  ```
  awk '/Phase 22 D-19 — video grid \+ popout persisted state/,/state.appendChild\(videoTree/' juce/JamWideJuceProcessor.cpp \
    | grep -q '"gridVisible"' \
    && grep -q '"gridBandHeight"' \
    && grep -q '"detachedGridX"' \
    && grep -q '"detachedGridY"' \
    && grep -q '"detachedGridWidth"' \
    && grep -q '"detachedGridHeight"' \
    && grep -q '"name"' \
    && grep -q '"x"' \
    && grep -q '"y"' \
    && grep -q '"w"' \
    && grep -q '"h"'
  ```
  Run separately for the LOAD block (`STEP 6: Phase 22`) with the same field names.
- CONTEXT.md `<deferred>` gains a new entry: "Extract `serializeVideoState`/`deserializeVideoState` pure helpers (v1.4+) — kills the inline-replica drift class entirely. Deferred from Phase 22 codex review (M6 option b)."

## M7 — Detached-grid sync with main band

**Codex recommendations adopted (all three):**
- Detached grid does NOT show its own detach affordance.
- Main and detached bands share popout state via editor-driven setters.
- Detached band receives the same popped-out peer set.

**Changes:**
- Plan 22-02 Task 1 `VideoGridBand.h` gains a `Mode` enum: `enum class Mode { MainBand, DetachedBand };` with constructor parameter `Mode mode = Mode::MainBand`.
- Plan 22-02 Task 1 `VideoGridBand.cpp::paint()` only renders the `↗` detach button when `mode_ == Mode::MainBand`. `mouseDown` on the detach hit-region is a no-op when `mode_ == Mode::DetachedBand`.
- Plan 22-03 Task 1 `DetachedGridWindow.cpp` constructs its inner band with `Mode::DetachedBand`.
- Plan 22-03 Task 2 editor `openOrToggleRemotePopout` (and `bringBackRemotePopout`) calls BOTH bands' `setPeerPoppedOut(username, ...)`:
  ```cpp
  if (gridBand_) gridBand_->setPeerPoppedOut(t.username, true);
  if (detachedGrid_ && detachedGrid_->getGridBand())
      detachedGrid_->getGridBand()->setPeerPoppedOut(t.username, true);
  ```
- Plan 22-03 Task 1 `DetachedGridWindow.h` exposes a `VideoGridBand* getGridBand() const noexcept { return gridPtr_; }` accessor (already has `gridPtr_` member; just promote to public-accessor).
- Acceptance: `grep -q "Mode mode_" juce/ui/video/VideoGridBand.h` AND `grep -q "if (mode_ == Mode::MainBand)" juce/ui/video/VideoGridBand.cpp`.

## L8 — `computeGridLayout` invariant-based tests

**Changes:**
- Plan 22-01 Task 1 Test 5 rewritten: instead of "9 peers returns cols=3 or cols=4", assert the invariant `(layout.cols * layout.rows >= N) && (layout.cols * layout.tileW + (layout.cols + 1) * layout.spacing <= W) && (layout.rows * layout.tileH + (layout.rows + 1) * layout.spacing <= H)`.
- Keep Tests 1 (zero peers), 2 (1 peer → cols=1), 3 (2 peers → cols=2) as exact-case sanity checks since the math has only one degree of freedom there.
- Update behaviour 5 to read: "for N=9 with band 800×280, returned (cols, rows) satisfies cols*rows>=9 AND cols*tileW <= 800 AND rows*tileH <= 280, and maximizes tileW."

## L9 — Unicode glyph rendering + UTF-8 encoding

**Changes:**
- Plan 22-01 Task 2 acceptance gains: `file juce/ui/video/*.cpp | grep -E "UTF-8|ASCII"` returns lines for every cpp (BOM-less UTF-8 is fine; ASCII is fine; UTF-16 fails).
- Plan 22-03 Task 4 UAT Cell 1 gets a new sub-step: "Confirm the `↗` glyph in the popout button renders correctly (no `□` tofu/missing-glyph indicator). If missing on Windows: filed in `tests/uat/phase-22-grid-popout-uat-report.md` as a TYPOGRAPHY issue — fallback rendering swaps `↗` for a small `Path`-drawn triangle. Acceptable for v1.3 beta if reported; mandatory fix for v1.3 release."
- Acceptance criterion added to Plan 22-01 Task 2: `python3 -c "import sys; [open(f, 'rb').read().decode('utf-8') for f in ['juce/ui/video/VideoTileBase.cpp', 'juce/ui/video/SelfVideoTile.cpp', 'juce/ui/video/RemotePeerTile.cpp']]; print('OK')"` exits 0 (validates UTF-8 decodability).

---

## Application Order

1. CONTEXT.md gets D-17 clarification (4-state table referenced from D-17 prose) and `<deferred>` gains M6 option (b) extraction note.
2. Plan 22-01 gets H2 + L8 + L9 + M5 fixes.
3. Plan 22-02 gets H1 (new Task 0) + H2 + M4 + M5 + M7 fixes.
4. Plan 22-03 gets H2 + H3 + M5 + M7 fixes.
5. Plan 22-04 gets H2 + M6 fix.
6. VALIDATION.md per-task gates updated for the new acceptance commands.
7. PATTERNS.md (optional) — annotate where `juce::HashMap` replaces `std::unordered_map`. Skipped this pass; PATTERNS is a research artifact and the plans are the canonical source.

---

## Iter-2 H2 Narrowing (2026-05-18)

The first `--reviews` replan applied H2 globally — `juce::HashMap<juce::String, ...>` in Plans 22-02, 22-03, and 22-04. Plan-checker re-verification flagged this as a BLOCKER: `juce::HashMap<juce::String, std::unique_ptr<T>>` is API-incompatible because:

- **`juce::HashMap::set()`** does copy-assignment (`getReference(newKey) = newValue;` at `libs/juce/modules/juce_core/containers/juce_HashMap.h:244`) — fails template instantiation on move-only `std::unique_ptr`
- **`juce::HashMap` has no `.find(key)`** that returns an iterator — only `contains()`, `getReference()`, etc.
- **`juce::HashMap::operator[]`** returns ValueType by value (`return ValueType();` at lines 182-190) — fails for non-copyable

The original codex H2 concern ("`std::unordered_map<juce::String, ...>` may not compile") was over-broad for THIS codebase. `juce/midi/MidiMapper.h:105` already proves `std::unordered_map<juce::String, int>` compiles cleanly — JUCE 8 ships `std::hash<juce::String>`. The planner correctly defended `std::unordered_map` in Plan 22-04's test inline-replica (citing this precedent at lines 422-466) but inconsistently applied `juce::HashMap` everywhere else for in-memory unique_ptr storage.

**H2 NARROWING (iter-2 fix):**

| Map | Value type | Final type | Rationale |
|-----|------------|------------|-----------|
| `peerTiles_` (22-02) | `std::unique_ptr<RemotePeerTile>` | `std::unordered_map` | move-only; juce::HashMap::set copy-assigns |
| `peerPlaceholders_` (22-03) | `std::unique_ptr<PopoutPlaceholderCard>` | `std::unordered_map` | move-only |
| `remotePopouts_` (22-03 editor) | `std::unique_ptr<RemotePeerPopoutWindow>` | `std::unordered_map` | move-only |
| `poppedOutPeers_` (22-03) | (set) | `std::unordered_set` | std::hash<juce::String> ships with JUCE 8 |
| `remotePopoutBoundsMap_` (22-04 processor) | `juce::Rectangle<int>` | `juce::HashMap` (KEEP) | copyable + deterministic XML iteration matters |

**API translation table** (used during the iter-2 edits):

| `juce::HashMap` API | `std::unordered_map` equivalent |
|---------------------|--------------------------------|
| `.set(k, v)` | `.emplace(k, std::move(v))` for move-only, or `m[k] = v` for copyable |
| `.find(k)` returns iter? | `.find(k)` returns iter (works in both for copyable; only std lets you do `it->second.reset()` on unique_ptr) |
| `.remove(k)` | `.erase(k)` or `.erase(it)` |
| `.contains(k)` | `.count(k)` or `.find(k) != .end()` (C++20: `.contains(k)`) |
| `::Iterator it(map); while(it.next()) { it.getKey(); it.getValue(); }` | `for (auto const& [k, v] : map) { ... }` (C++17 structured binding) |

**Lesson learned:** When a cross-AI reviewer flags a "may not compile" concern, the in-tree plan-checker can ground-truth the premise with one grep. If the codebase already has the pattern working, the concern is moot. The planner should ALWAYS check the precedent before adopting a structural fix that swaps a working pattern for a different one.

**Files touched in iter-2:**
- 22-02-PLAN.md (the iter-2 agent reverted peerTiles_ before its socket dropped)
- 22-03-PLAN.md (orchestrator finished via Edit tool: lines 53, 79, 295, 308, 333, 337, 343, 377, 417, 446, 474, 501, 531-535, 727)
- 22-CONTEXT.md (D-15 narrative narrowed)
- 22-VALIDATION.md (rows for 22-02 T1 + 22-03 T2 updated)
- This file (audit trail extended)

**Files preserved unchanged:**
- 22-04-PLAN.md (Plan 22-04's `juce::HashMap<juce::String, juce::Rectangle<int>>` is correct — value type is copyable)
- 22-01-PLAN.md (no maps with move-only value types)
- 22-RESEARCH.md (research is upstream of this code-level concern)
- 22-PATTERNS.md (research artifact — plans are canonical)
