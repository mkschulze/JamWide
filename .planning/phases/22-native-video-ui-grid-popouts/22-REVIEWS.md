---
phase: 22
reviewers: [codex]
reviewed_at: 2026-05-18T00:22:00Z
plans_reviewed: [22-01-PLAN.md, 22-02-PLAN.md, 22-03-PLAN.md, 22-04-PLAN.md]
notes:
  - claude reviewer skipped because /gsd-review was invoked from inside Claude Code CLI (SELF_CLI=claude)
  - gemini, opencode, qwen, cursor, coderabbit not installed
  - ollama, lm_studio, llama_cpp not running locally
  - codex review used gpt-5.5 via openai provider, sandbox=read-only
---

# Cross-AI Plan Review — Phase 22

## Codex Review (gpt-5.5)

## Summary

Overall, the Phase 22 plan set is strong and unusually well-grounded in existing code patterns. The decomposition follows real dependency boundaries: tile substrate → grid integration → popouts/placeholders → persistence/UAT. The strongest parts are the explicit lifetime contracts, the editor teardown ordering checks, the multi-monitor clamp, and the state-injection hardening in Plan 22-04. Main risks are around over-reliance on grep/inline-replica tests, a few brittle cross-plan contracts, and some UX/state ambiguities around "close hides" versus "bring back destroys."

Overall risk: **MEDIUM**. The architecture is sound, but the implementation touches JUCE windows, async subscriptions, editor lifetime, plugin state, and manual UAT-heavy behavior. Those are all places where small ordering mistakes can become real crashes.

## Strengths

- Clear sequential layering. 22-01 creates low-level substrate before UI integration; 22-02 adds the band; 22-03 adds multi-window behavior; 22-04 adds persistence and final UAT.

- Lifetime risk is treated seriously. The MEMBER-ORDER CONTRACT, popout destructor ordering, `Subscription` ownership, and editor teardown before LookAndFeel teardown are all explicitly planned and checked.

- Good reuse of proven local patterns. `CameraPreviewTile`, `CameraPreviewWindow`, `ConnectionBar::CameraButton`, and plugin state v3→v4 are the right analogs.

- State serialization hardening is better than typical plugin-state work. The map cap, username cap, bounds clamping, and missing-`<video>` default path directly address DAW-project-state trust boundaries.

- Multi-monitor handling is practical. The "intersects any display" policy is reasonable for musician workflows where straddling monitors is valid.

- Test scope is mostly honest. The plans clearly distinguish what can be automated from what must be UAT-verified.

## Concerns

- **HIGH: Plan 22-02 proposes exposing `isBot` / `stripAtSuffix` via declarations while implementations remain in `ChannelStripArea.cpp`.** That creates a hidden link dependency and awkward ownership. These helpers are shared domain utilities now. They should move to `BotFilter.cpp/.h`, not be declared in a header while implemented in an unrelated component TU.

- **HIGH: `std::unordered_map<juce::String, ...>` may not compile unless a hash exists.** Several plans use `unordered_map<juce::String, ...>`. If the repo already has a `std::hash<juce::String>` specialization, fine. If not, this should be `std::map`, `juce::HashMap`, or an unordered map with an explicit hasher. This affects editor popout maps, processor persisted bounds, and test replicas.

- **HIGH: Plan 22-03 close semantics are internally inconsistent.** D-17 says closing a popout hides and keeps the underlying subscription alive. But Task 2 says clicking tile `↗` again may call `bringBackRemotePopout`, destroying it. Also manual Cell A says close X hides, placeholder remains, clicking placeholder destroys. That is okay, but the code path for clicking `↗` while popout exists should be explicit: either re-show hidden popout or bring back/destroy. Current text says "toggle visibility (or destroy via bring-back per D-18 — pick destroy semantics)" then implements destroy. That risks surprising users who expect `↗` to re-open the hidden popout.

- **MEDIUM: Inline-replica state test is acceptable but weaker than claimed.** It verifies the intended schema logic, not that production `getStateInformation`/`setStateInformation` actually uses that logic correctly. Deferring production round-trip to UAT Cell 13 is pragmatic, but UAT is not a great substitute for a deterministic automated test of serialization.

- **MEDIUM: The `kSelfTileSentinel = "__self__"` contract is brittle.** Centralizing the literal is better than scattering it, but it still overloads username identity with a magic string. A real user named `__self__` is unlikely but not impossible. This should ideally be a typed callback or enum.

- **MEDIUM: Plan 22-02 `MessageManager::callAsync([this]{ ... })` can outlive the editor.** The callback captures raw `this`. If the editor is destroyed before the async call runs, that is a UAF risk. Since the timer already runs on the message thread, prefer setting a local flag inside the mutex and calling `toggleGridBand(true)` after releasing the lock, synchronously.

- **MEDIUM: Detached grid containing another `VideoGridBand` may inherit main-band-only behavior.** The detached grid likely should not show its own detach button, should handle placeholder state consistently, and should know about popped-out peers. The plan says detached-grid window contains a fresh `VideoGridBand`, but the state synchronization between the main band and detached band needs to be sharper.

- **LOW: `computeGridLayout` acceptance is partly loose.** For example, "9 peers returns cols=3 or cols=4" weakens the layout contract. That may be fine, but tests should assert invariants more than exact columns if discretion is intended.

- **LOW: Visual glyph rendering uses unicode arrows.** The project mostly seems ASCII/C++; if compiler/source settings are stable this is fine, but UI code using `"↗"` / `"→"` should be checked against file encoding and platform font fallback.

## Suggestions

- Move bot filtering into real shared files:
  - `juce/ui/BotFilter.h`
  - `juce/ui/BotFilter.cpp`
  - update `ChannelStripArea.cpp`, `VideoGridBand.cpp`, and `JamWideJuceEditor.cpp` to include the header.
  Avoid extern declarations against `ChannelStripArea.cpp`.

- Replace the self sentinel with a typed callback:
  ```cpp
  enum class VideoPopoutTargetKind { Self, RemotePeer };

  struct VideoPopoutTarget {
      VideoPopoutTargetKind kind;
      juce::String username;
  };

  std::function<void(VideoPopoutTarget)> onPopoutRequested;
  ```
  This removes collision risk and makes D-09 impossible to miss.

- In Plan 22-03, define hidden-popout behavior exactly:
  - Close X: hide window, keep placeholder.
  - Clicking placeholder: destroy window, restore tile.
  - Clicking tile `↗` while a hidden popout exists: either re-show it or destroy/recreate it, but choose explicitly.
  I'd recommend re-showing hidden popouts; "bring back" remains the destroy path.

- Replace the raw `this` async auto-open callback with a lock-safe synchronous pattern:
  ```cpp
  bool shouldOpen = false;
  {
      std::lock_guard<std::mutex> lk(processorRef.cachedUsersMutex);
      // inspect sinks; set shouldOpen
  }
  if (shouldOpen)
      toggleGridBand(true);
  ```

- Add one production serialization smoke test if feasible. Even a small console test that constructs the processor may be heavy, but extracting pure helpers such as `writeVideoStateToValueTree` / `readVideoStateFromValueTree` would give both production and tests one implementation. If you keep the inline replica, add a source-level grep gate proving production contains all expected property names.

- Use an ordered map or explicit hasher for `juce::String` maps. This also makes saved `<popout>` child order deterministic, which helps tests and diffability.

- Tighten detached-grid synchronization:
  - Detached grid should receive the same popped-out peer set as the main band.
  - Detached grid probably should not expose another detach affordance.
  - Main and detached bands should share placeholder state through editor-controlled setters.

- Consider a small `VideoBounds`/`VideoState` helper in processor code to centralize clamping. Right now clamping ranges appear in multiple places and tests. That is manageable but easy to drift.

## Risk Assessment

**Overall: MEDIUM.**

The plan quality is high, and the major architectural choices are defensible. The remaining risk is implementation brittleness: JUCE component/window lifetime, duplicated state logic, raw async callbacks, and cross-plan stub replacement. None of these require rethinking the phase, but they should be tightened before execution.

The strict sequential wave structure is justified. There is limited safe parallelism because each wave creates APIs consumed by the next. The only plausible parallel work is UAT documentation or state-test scaffolding while earlier UI work proceeds, but the implementation itself is genuinely load-bearing in sequence.

---

## Consensus Summary

> **Note:** Only one reviewer (codex/gpt-5.5) was available — claude is skipped when /gsd-review is invoked from inside Claude Code CLI; gemini, opencode, qwen, cursor, coderabbit, and local model servers were not installed/running. Single-reviewer consensus = codex's findings verbatim.

### Agreed Strengths (codex-only baseline)

- Sequential layering matches real dependencies (substrate → integration → multi-window → persistence)
- Lifetime risk is explicitly addressed (MEMBER-ORDER CONTRACT, destructor teardown ordering, popout `Subscription` RAII)
- Reuse of proven local patterns (CameraPreviewTile, CameraPreviewWindow, plugin state v3→v4)
- State serialization hardening (T-22-SP cap/clamp/jlimit) above typical plugin-state work
- Multi-monitor "intersects any display" policy is correct for musician workflows
- Test scope honestly distinguishes automated from UAT-only

### Agreed Concerns (codex-only baseline; prioritize HIGH for `--reviews` replan)

**HIGH (must address):**
1. **`isBot` / `stripAtSuffix` extern-from-ChannelStripArea pattern** — refactor to `juce/ui/BotFilter.h`/`.cpp` instead of cross-TU declarations
2. **`std::unordered_map<juce::String, ...>` may not compile** — verify `std::hash<juce::String>` exists; otherwise use `std::map`, `juce::HashMap`, or explicit hasher
3. **Plan 22-03 close-semantics inconsistency** — clicking `↗` while popout hidden should re-show, not destroy. "Bring back" remains the explicit destroy path on placeholder click. Document the 4-state truth table inline.

**MEDIUM (should address):**
4. **`MessageManager::callAsync([this]{...})` UAF risk** — replace with lock-release-then-sync-call pattern in 22-02 auto-open hook
5. **`kSelfTileSentinel` magic-string brittle** — replace with `enum class VideoPopoutTargetKind { Self, RemotePeer }` typed callback
6. **Inline-replica test vs production code-path drift** — add a `grep`-gate on production source to prove property names match, OR extract pure helpers and call them from both production and test
7. **Detached-grid sync with main-band placeholder state** — main band and detached band must share popout state; detached grid should NOT expose its own detach affordance

**LOW (nice to have):**
8. `computeGridLayout` cols=3-or-4 disjunction — tighten to invariant assertions
9. Unicode arrow `↗` / `→` — verify file encoding + platform font fallback

### Divergent Views

(N/A — single reviewer)

---

## Recommendation

Run `/gsd-plan-phase 22 --reviews` to replan incorporating these findings. The HIGH concerns (1-3) are the priority — concerns 2 and 3 are correctness issues that would surface at execution time (compile failure on unordered_map; UX confusion on `↗` semantics). Concern 1 is an organizational improvement that prevents future drift.

For the MEDIUM-7 detached-grid sync issue: this directly maps to the open question Q3 in 22-RESEARCH.md (4-state placeholder truth table). The current resolution says "Plan 22-03 Task 2 Behaviour 5 implements" — but per codex the implementation reuses VideoGridBand which may inherit main-band-only assumptions. Worth a closer look during the `--reviews` replan.
