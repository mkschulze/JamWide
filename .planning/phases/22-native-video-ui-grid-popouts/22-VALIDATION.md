---
phase: 22
slug: native-video-ui-grid-popouts
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-17
---

# Phase 22 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

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
| 22-01 T1 | 22-01 | 1 | DISP-01 (computeGridLayout) | — | pure-function layout solver, deterministic + 4:3 aspect | unit | `ctest -R "^video_grid_layout\$"` | 🟡 W0 (this plan creates) | ⬜ pending |
| 22-01 T2 | 22-01 | 1 | DISP-01 (tile substrate) | T-22-MO-1 | MEMBER-ORDER CONTRACT: subscription_ is LAST member | build | `cmake --build . --target JamWideJuce_VST3` | n/a (build) | ⬜ pending |
| 22-01 T3 | 22-01 | 1 | DISP-01 (tile substrate) | T-22-MO-1 | runtime offset proof subscription_ > all other members | unit | `ctest -R "^video_tile_member_order\$"` | 🟡 W0 (this plan creates) | ⬜ pending |
| 22-02 T1 | 22-02 | 2 | DISP-01 (band container) | T-22-RE-2 | 30Hz sink-poll + self-broadcast atomic observation | build | `cmake --build . --target JamWideJuce_VST3` | n/a (build) | ⬜ pending |
| 22-02 T2 | 22-02 | 2 | DISP-01, DISP-04 (toggle wiring) | T-22-RE-1 | toggleGridBand zero NJClient calls; auto-open latch fires once | build + grep gate | `awk '/toggleGridBand/,/^}/' juce/JamWideJuceEditor.cpp | grep -cE 'client\.\|NJClient::'` returns 0 | n/a (gate) | ⬜ pending |
| 22-02 T3 | 22-02 | 2 | DISP-01, DISP-04, D-05 | — | visual: band toggles, peer tile renders, auto-open fires | UAT | manual cells A-G | n/a (manual) | ⬜ pending |
| 22-03 T1 | 22-03 | 3 | DISP-02 | T-22-MM | popout DocumentWindow opens; multi-monitor clamp applied | build | `cmake --build . --target JamWideJuce_VST3` | n/a (build) | ⬜ pending |
| 22-03 T2 | 22-03 | 3 | DISP-02, DISP-03 (placeholders + bring-back) | T-22-LT-1 | editor destructor clears popouts BEFORE LookAndFeel teardown (POSITIONAL — line numbers checked) | build + positional gate | `awk '/JamWideJuceEditor::~JamWideJuceEditor/,/^}/' juce/JamWideJuceEditor.cpp \| grep -n -E 'remotePopouts_\.clear\|detachedGrid_\.reset\|gridBand_\.reset\|setLookAndFeel\(nullptr\)' \| python3 -c "import sys; rows=[l.rstrip().split(':',1) for l in sys.stdin]; nums=[(int(n),t) for n,t in rows]; cl=[n for n,t in nums if 'setLookAndFeel' not in t]; laf=[n for n,t in nums if 'setLookAndFeel' in t]; assert len(cl)>=3 and len(laf)>=1 and all(c<min(laf) for c in cl), 'order broken'" | n/a (gate) | ⬜ pending |
| 22-03 T3 | 22-03 | 3 | DISP-02 (lifetime) | T-22-MM, T-22-LT-2 | popout close-hides, bring-back destroys, multi-listener safe | unit | `ctest -R "^remote_peer_popout_lifetime\$"` | 🟡 W0 (this plan creates) | ⬜ pending |
| 22-03 T4 | 22-03 | 3 | DISP-02, DISP-03 | T-22-MM | visual: popouts, detached-grid, coexistence, multi-monitor | UAT | manual cells A-G | n/a (manual) | ⬜ pending |
| 22-04 T1 | 22-04 | 4 | DISP-01..04 (persistence) | T-22-SP | v4→v5 bump; `<video>` subtree; jlimit clamping + map cap | build + grep gate | `grep -q "kRemotePopoutMapCap = 64" juce/JamWideJuceProcessor.h` | n/a (gate) | ⬜ pending |
| 22-04 T2 | 22-04 | 4 | DISP-01..04 (persistence) | T-22-SP | inline-replica `V5VideoState`/`readV5Video`/`writeV5Video` round-trip + v4 graceful upgrade + 200→64 cap + 300-char username drop + jlimit bounds clamp (10 sub-tests). NO processor link. End-to-end UAT-only via Cell 13. | unit | `ctest -R "^plugin_state_v4_v5\$"` | 🟡 W0 (this plan creates) | ⬜ pending |
| 22-04 T3 | 22-04 | 4 | DISP-01..04 | — | UAT procedure + report templates exist (13 cells) | file existence | `test -f tests/uat/phase-22-grid-popout-uat-procedure.md` | n/a (doc) | ⬜ pending |
| 22-04 T4 | 22-04 | 4 | DISP-01, DISP-02, DISP-03, DISP-04 | T-22-SP, T-22-MM | operator UAT sign-off (State A or State B) | UAT | manual procedure | n/a (manual) | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_video_grid_layout.cpp` — pure-function unit tests for `computeGridLayout(N, W, H)`
- [ ] `tests/test_plugin_state_v4_v5.cpp` — round-trip serialization of `<video>` ValueTree subtree (per-peer popout bounds map, detached-grid bounds, band height, gridVisible flag)
- [ ] `tests/test_video_tile_member_order.cpp` — `static_assert` or destruction-order proof for MEMBER-ORDER CONTRACT on `SelfVideoTile` + `RemotePeerTile`
- [ ] `tests/test_remote_peer_popout_lifetime.cpp` — popout window close-hides + bring-back destroy paths exercise distributor `Subscription` RAII

*If none: "Existing infrastructure covers all phase requirements."*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Drag popout to second monitor and resize | DISP-02 | Real-monitor topology; JUCE bounds-clamping requires interactive feedback | Open phase 22 UAT cell 7 — open peer popout, drag to second monitor, resize, close+reopen → bounds persisted |
| Grid+popouts coexist, toggle does not close popouts | DISP-03 | Multi-window window-manager interactions | UAT cell 3 — open 2 popouts, toggle grid off, both popouts stay alive, toggle grid on, both still bound to live frames |
| Grid toggle preserves NINJAM session | DISP-04 | NINJAM session is network-stateful; verified by audio continuity | UAT cell 4 — connect to test server, toggle grid 5×, observe no peer drops in `ChannelStripArea` |
| Plugin-state v4 → v5 migration from existing DAW saves | D-19 | Real DAW session-save round-trip | UAT cell 9 — load a v4 save (Phase 19/20 era), verify graceful default-fill for missing `<video>` subtree fields |
| Auto-open band on first peer frame | D-05 | Real NINJAM peer + first H264 BEGIN packet | UAT cell 8 — connect with band closed, peer starts broadcasting, band auto-opens once; user closes, peer stops+restarts, band does NOT auto-reopen |
| Detached-grid window + per-peer popouts coexist with in-main-view band placeholder | D-03 | Multi-window visual + click affordance | UAT cell 5 — detach grid, then popout one peer from the detached grid, in-main-view band shows whole-band placeholder, detached grid shows per-peer placeholder for popped peer |

*If none: "All phase behaviors have automated verification."*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (4 unit test files listed above)
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter after gsd-planner populates task map

**Approval:** pending
