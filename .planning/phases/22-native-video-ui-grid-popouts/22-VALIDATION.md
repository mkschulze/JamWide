---
phase: 22
slug: native-video-ui-grid-popouts
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-17
updated: 2026-05-18  # codex --reviews replan applied: H1+H2+H3+M4+M5+M6+M7+L8+L9
---

# Phase 22 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution. **Updated 2026-05-18** for the codex `--reviews` replan (H1 BotFilter extraction, H2 juce::HashMap, H3 4-state truth table, M4 lock-release-then-sync, M5 typed callback, M6 production-source grep gate, M7 detached-band sync, L8 invariant test, L9 UTF-8 + glyph check).

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (JUCE host) + manual UAT cells |
| **Config file** | `build-juce/CTestTestfile.cmake` (generated) |
| **Quick run command** | `cd build-juce && ctest -R "phase22" --output-on-failure` |
| **Full suite command** | `cd build-juce && ctest --output-on-failure` |
| **Estimated runtime** | ~30 seconds (unit) + ~5 min manual UAT |

---

## Sampling Rate

- **After every task commit:** Run `cd build-juce && ctest -R "phase22|frame_distributor|plugin_state" --output-on-failure`
- **After every plan wave:** Run `cd build-juce && ctest --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green + UAT cells signed off
- **Max feedback latency:** ~30 seconds

---

## Per-Task Verification Map

> Populated by `gsd-planner` from per-plan tasks. Each task row maps Task ID → Plan → Wave → Requirement → Threat → Test Type → Automated Command → Status. Manual UAT-only behaviors live in the Manual-Only Verifications section below.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 22-01 T1 | 22-01 | 1 | DISP-01 (computeGridLayout) | — | pure-function layout solver, deterministic + 4:3 aspect; **L8: Test 5 asserts FIT invariants not exact cols** | unit | `ctest -R "^video_grid_layout\$"` | 🟡 W0 (this plan creates) | ⬜ pending |
| 22-01 T2 | 22-01 | 1 | DISP-01 (tile substrate) | T-22-MO-1, T-22-MO-4 | MEMBER-ORDER CONTRACT: subscription_ is LAST member; **M5: typed `VideoPopoutTarget` callback (no magic-string sentinel)**; **L9: UTF-8 encoding gate** | build + L9 gate + M5 gate | `cmake --build . --target JamWideJuce_VST3 && ! grep -rq '"__self__"' juce/ui/video/ && python3 -c "[open(f,'rb').read().decode('utf-8') for f in ['juce/ui/video/VideoTileBase.cpp','juce/ui/video/SelfVideoTile.cpp','juce/ui/video/RemotePeerTile.cpp']]; print('OK')"` | n/a (build+gate) | ⬜ pending |
| 22-01 T3 | 22-01 | 1 | DISP-01 (tile substrate) | T-22-MO-1 | runtime offset proof subscription_ > all other members | unit | `ctest -R "^video_tile_member_order\$"` | 🟡 W0 (this plan creates) | ⬜ pending |
| 22-02 T0 (NEW H1) | 22-02 | 2 | DISP-01 (precondition) | T-22-RE-3 | **H1: BotFilter extraction from anon namespace — `juce/ui/BotFilter.h`/`.cpp` exist with external linkage; ChannelStripArea.cpp anon-namespace `isBot`/`stripAtSuffix` DELETED; all call sites use `jamwide::` prefix** | build + H1 gate | `cmake --build . --target JamWideJuce_VST3 && test -f juce/ui/BotFilter.h && test -f juce/ui/BotFilter.cpp && grep -q "namespace jamwide" juce/ui/BotFilter.cpp && ! awk '/^namespace \{/,/^} \/\/ anonymous namespace/' juce/ui/ChannelStripArea.cpp \| grep -E '^bool isBot\|^juce::String stripAtSuffix'` | n/a (build+gate) | ⬜ pending |
| 22-02 T1 | 22-02 | 2 | DISP-01 (band container) | T-22-RE-2 | 30Hz sink-poll + self-broadcast atomic observation; **M5: typed `VideoPopoutTarget` callback signature**; **M7: `Mode` enum (MainBand\|DetachedBand)**; **H2 NARROWED: `std::unordered_map<juce::String, std::unique_ptr<RemotePeerTile>>` for peerTiles_** (juce::HashMap cannot hold move-only unique_ptr) | build + M5+M7+H2-narrowed gates | `cmake --build . --target JamWideJuce_VST3 && grep -q "enum class Mode" juce/ui/video/VideoGridBand.h && grep -q "std::unordered_map<juce::String, std::unique_ptr<RemotePeerTile>>" juce/ui/video/VideoGridBand.h && ! grep -q "kSelfTileSentinel" juce/ui/video/VideoGridBand.cpp && grep -q "jamwide::isBot" juce/ui/video/VideoGridBand.cpp` | n/a (build+gate) | ⬜ pending |
| 22-02 T2 | 22-02 | 2 | DISP-01, DISP-04 (toggle wiring) | T-22-RE-1 | toggleGridBand zero NJClient calls; auto-open latch fires once; **M4: lock-release-then-sync (NO `MessageManager::callAsync`)**; **H1: `jamwide::isBot` in auto-open latch (no extern)** | build + M4+H1 gates | `cmake --build . --target JamWideJuce_VST3 && awk '/^void JamWideJuceEditor::toggleGridBand/,/^}/' juce/JamWideJuceEditor.cpp \| grep -cE 'client\.\|NJClient::' \| grep -q '^0$' && awk '/Phase 22 D-05 — auto-open/,/if \(shouldOpen\)/' juce/JamWideJuceEditor.cpp \| ! grep -q "MessageManager::callAsync" && grep -q "jamwide::isBot" juce/JamWideJuceEditor.cpp && ! grep -qE "extern bool isBot" juce/JamWideJuceEditor.cpp` | n/a (gate) | ⬜ pending |
| 22-02 T3 | 22-02 | 2 | DISP-01, DISP-04, D-05 | — | visual: band toggles, peer tile renders, auto-open fires; **NEW cell H: H1 regression check (bot filtering still works in mixer)** | UAT | manual cells A-H (8 cells) | n/a (manual) | ⬜ pending |
| 22-03 T1 | 22-03 | 3 | DISP-02 | T-22-MM | popout DocumentWindow opens; multi-monitor clamp applied; **M7: `DetachedGridWindow` constructs inner band with `Mode::DetachedBand`; `getGridBand()` accessor exposed** | build + M7 gates | `cmake --build . --target JamWideJuce_VST3 && grep -q "VideoGridBand::Mode::DetachedBand\|Mode::DetachedBand" juce/ui/video/DetachedGridWindow.cpp && grep -q "getGridBand" juce/ui/video/DetachedGridWindow.h` | n/a (build+gate) | ⬜ pending |
| 22-03 T2 | 22-03 | 3 | DISP-02, DISP-03 (placeholders + bring-back) | T-22-LT-1, T-22-LT-3 | editor destructor clears popouts BEFORE LookAndFeel teardown (POSITIONAL — line numbers checked); **H2 NARROWED: `std::unordered_map<juce::String, std::unique_ptr<RemotePeerPopoutWindow>>` for `remotePopouts_`** (juce::HashMap::set copy-assigns move-only unique_ptr; MidiMapper.h:105 precedent for std::unordered_map<juce::String,...>); **H3: 4-state truth table with re-show non-destructive on state C**; **M5: `openOrToggleRemotePopout(VideoPopoutTarget)` typed dispatch**; **M7: dual-band sync on open/bring-back + replay-into-new-detached-band** | build + positional gate + H2-narrowed+H3+M5+M7 gates | `awk '/JamWideJuceEditor::~JamWideJuceEditor/,/^}/' juce/JamWideJuceEditor.cpp \| grep -n -E 'remotePopouts_\.clear\|detachedGrid_\.reset\|gridBand_\.reset\|setLookAndFeel\(nullptr\)' \| python3 -c "import sys; rows=[l.rstrip().split(':',1) for l in sys.stdin]; nums=[(int(n),t) for n,t in rows]; cl=[n for n,t in nums if 'setLookAndFeel' not in t]; laf=[n for n,t in nums if 'setLookAndFeel' in t]; assert len(cl)>=3 and len(laf)>=1 and all(c<min(laf) for c in cl), 'order broken'" && grep -q "std::unordered_map<juce::String, std::unique_ptr<jamwide::RemotePeerPopoutWindow>>" juce/JamWideJuceEditor.h && awk '/openOrToggleRemotePopout/,/^}/' juce/JamWideJuceEditor.cpp \| grep -q "popout->isVisible()" && grep -q "VideoPopoutTargetKind::Self" juce/JamWideJuceEditor.cpp && awk '/^void JamWideJuceEditor::openOrToggleDetachedGrid/,/^}/' juce/JamWideJuceEditor.cpp \| grep -q "remotePopouts_" && ! grep -q '"__self__"' juce/JamWideJuceEditor.cpp | n/a (gate) | ⬜ pending |
| 22-03 T3 | 22-03 | 3 | DISP-02 (lifetime) | T-22-MM, T-22-LT-2 | popout close-hides, bring-back destroys, multi-listener safe; **H3: Test 7 verifies all 4 state transitions** | unit | `ctest -R "^remote_peer_popout_lifetime\$"` | 🟡 W0 (this plan creates) | ⬜ pending |
| 22-03 T4 | 22-03 | 3 | DISP-02, DISP-03 | T-22-MM | visual: popouts, detached-grid, coexistence, multi-monitor; **NEW cell A step 4 (H3 re-show), cell C step 2 (M7 dual-band sync), cell H (M7 replay-on-open-detached)** | UAT | manual cells A-H (8 cells) | n/a (manual) | ⬜ pending |
| 22-04 T1 | 22-04 | 4 | DISP-01..04 (persistence) | T-22-SP, T-22-DR | v4→v5 bump; `<video>` subtree; jlimit clamping + map cap; **H2: `juce::HashMap<juce::String, juce::Rectangle<int>>` for `remotePopoutBoundsMap_` + `juce::HashMap::Iterator` for snapshot**; **M6: production source grep gate verifies SAVE block + LOAD block both reference all 11 property names** | build + H2+M6 gates | `cmake --build . --target JamWideJuce_VST3 && grep -q "kRemotePopoutMapCap = 64" juce/JamWideJuceProcessor.h && grep -q "juce::HashMap<juce::String, juce::Rectangle<int>>" juce/JamWideJuceProcessor.h && grep -q "HashMap.*Iterator\|HashMap<.*>::Iterator" juce/JamWideJuceProcessor.cpp && bash -c 'set -e; for p in gridVisible gridBandHeight detachedGridX detachedGridY detachedGridWidth detachedGridHeight name x y w h; do awk "/Phase 22 D-19 — video grid \\\\+ popout persisted state/,/state.appendChild\\\\(videoTree/" juce/JamWideJuceProcessor.cpp \| grep -q "\\"$p\\""; awk "/STEP 6: Phase 22/,/^[[:space:]]*}/" juce/JamWideJuceProcessor.cpp \| grep -q "getProperty(\\"$p\\""; done; echo OK'` | n/a (build+gate) | ⬜ pending |
| 22-04 T2 | 22-04 | 4 | DISP-01..04 (persistence) | T-22-SP, T-22-DR | inline-replica `V5VideoState`/`readV5Video`/`writeV5Video` round-trip + v4 graceful upgrade + 200→64 cap + 300-char username drop + jlimit bounds clamp (10 sub-tests). NO processor link. End-to-end UAT-only via Cell 13. **M6 grep gate in T1 catches production drift independently.** | unit | `ctest -R "^plugin_state_v4_v5\$"` | 🟡 W0 (this plan creates) | ⬜ pending |
| 22-04 T3 | 22-04 | 4 | DISP-01..04 | — | UAT procedure + report templates exist (13 cells); **L9: Cell 1 includes `↗` glyph check** | file existence + L9 gate | `test -f tests/uat/phase-22-grid-popout-uat-procedure.md && grep -q "L9\|tofu\|glyph\|TYPOGRAPHY" tests/uat/phase-22-grid-popout-uat-procedure.md` | n/a (doc) | ⬜ pending |
| 22-04 T4 | 22-04 | 4 | DISP-01, DISP-02, DISP-03, DISP-04 | T-22-SP, T-22-MM | operator UAT sign-off (State A or State B) | UAT | manual procedure | n/a (manual) | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_video_grid_layout.cpp` — pure-function unit tests for `computeGridLayout(N, W, H)`; Test 5 uses L8 invariant assertions (codex)
- [ ] `tests/test_plugin_state_v4_v5.cpp` — round-trip serialization of `<video>` ValueTree subtree (inline replica — does NOT link processor; M6 grep gate covers schema drift)
- [ ] `tests/test_video_tile_member_order.cpp` — `static_assert` or destruction-order proof for MEMBER-ORDER CONTRACT on `SelfVideoTile` + `RemotePeerTile`
- [ ] `tests/test_remote_peer_popout_lifetime.cpp` — popout window close-hides + bring-back destroy paths exercise distributor `Subscription` RAII; Test 7 validates H3 4-state truth table

*If none: "Existing infrastructure covers all phase requirements."*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Drag popout to second monitor and resize | DISP-02 | Real-monitor topology; JUCE bounds-clamping requires interactive feedback | Open phase 22 UAT cell 7 — open peer popout, drag to second monitor, resize, close+reopen → bounds persisted |
| Grid+popouts coexist, toggle does not close popouts | DISP-03 | Multi-window window-manager interactions | UAT cell 3 — open 2 popouts, toggle grid off, both popouts stay alive, toggle grid on, both still bound to live frames |
| Grid toggle preserves NINJAM session | DISP-04 | NINJAM session is network-stateful; verified by audio continuity | UAT cell 4 — connect to test server, toggle grid 5×, observe no peer drops in `ChannelStripArea` |
| Plugin-state v4 → v5 migration from existing DAW saves | D-19 | Real DAW session-save round-trip | UAT cell 9 — load a v4 save (Phase 19/20 era), verify graceful default-fill for missing `<video>` subtree fields. **M6 grep gate already catches schema-level drift at Task 1 build.** |
| Auto-open band on first peer frame | D-05 | Real NINJAM peer + first H264 BEGIN packet | UAT cell 8 — connect with band closed, peer starts broadcasting, band auto-opens once; user closes, peer stops+restarts, band does NOT re-auto-open |
| Detached-grid window + per-peer popouts coexist with in-main-view band placeholder | D-03 | Multi-window visual + click affordance | UAT cell 5 — detach grid, then popout one peer from the detached grid, in-main-view band shows whole-band placeholder, detached grid shows per-peer placeholder for popped peer |
| **H3 codex: 4-state truth table for tile `↗`** (NEW) | DISP-02 | State C re-show is interactive; requires user to verify "hidden popout re-shows on next `↗` click" | Plan 22-03 UAT cell A step 4: with popout in state C (hidden via X), click tile `↗` — popout must RE-SHOW at last bounds; NOT destroy. Unit test (Test 7) covers the same transitions in isolation. |
| **M7 codex: detached-band placeholder sync** (NEW) | DISP-03 | Two surfaces (main band + detached band) must both reflect the same placeholder state | Plan 22-03 UAT cell C step 2: open band + detached grid + popout one peer; verify BOTH the main band slot AND the detached band slot show "Popped out →" placeholder for that peer (not live tile). |
| **L9 codex: `↗` glyph rendering** (NEW) | DISP-02 | Platform-specific font fallback may render `↗` as tofu on Windows | Plan 22-04 UAT cell 1: confirm the `↗` glyph in the popout button renders correctly (no `□`). If missing on Windows, file TYPOGRAPHY issue; fallback is `Path`-drawn triangle. |

*If none: "All phase behaviors have automated verification."*

---

## Codex Review Fix Tracker (`--reviews` replan, 2026-05-18)

| Concern | Severity | Status | Verification |
|---------|----------|--------|--------------|
| H1 — BotFilter extraction (anon-namespace internal linkage) | HIGH | planned | Plan 22-02 Task 0 + acceptance gate at line VALIDATION row 22-02 T0 |
| H2 — `juce::HashMap` replaces `std::unordered_map<juce::String, ...>` | HIGH | planned | Plans 22-02 Task 1, 22-03 Task 2, 22-04 Task 1 acceptance gates |
| H3 — 4-state truth table for tile `↗` semantics | HIGH | planned | Plan 22-03 Task 2 `openOrToggleRemotePopout`; Plan 22-03 Task 3 Test 7 |
| M4 — `MessageManager::callAsync` UAF in auto-open latch | MEDIUM | planned | Plan 22-02 Task 2 acceptance gate: `! grep -q "MessageManager::callAsync"` in auto-open block |
| M5 — Typed `VideoPopoutTarget` replaces magic-string sentinel | MEDIUM | planned | Plan 22-01 Task 2 declares enum/struct; Plans 22-02 + 22-03 wire pass-through; gate: `! grep -rq '"__self__"' juce/` |
| M6 — Production-vs-test inline-replica drift defense | MEDIUM | planned | Plan 22-04 Task 1 acceptance gate: SAVE+LOAD blocks reference all 11 property names |
| M7 — Detached-grid sync with main-band placeholder state | MEDIUM | planned | Plan 22-02 Task 1 `Mode` enum; Plan 22-03 Task 1 `getGridBand()`; Plan 22-03 Task 2 dual-band sync + replay |
| L8 — `computeGridLayout` invariant assertions over exact cols | LOW | planned | Plan 22-01 Task 1 Test 5 uses fit-invariant assertions |
| L9 — UTF-8 encoding + `↗` glyph rendering | LOW | planned | Plan 22-01 Task 2 UTF-8 decode gate; Plan 22-04 Task 3 UAT Cell 1 glyph check |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (4 unit test files listed above)
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter after gsd-planner populates task map
- [ ] All 9 codex review concerns (3 HIGH + 4 MEDIUM + 2 LOW) tracked in fix table above

**Approval:** pending — requires Wave 0 test files to be created (test gates above will turn green) AND operator UAT sign-off (Plan 22-04 Task 4)
