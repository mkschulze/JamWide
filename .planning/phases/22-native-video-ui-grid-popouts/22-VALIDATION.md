---
phase: 22
slug: native-video-ui-grid-popouts
status: draft
nyquist_compliant: false
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
| TBD | TBD | TBD | DISP-01 | — | per-peer tile renders inside main view band | unit + UAT | `ctest -R "phase22_grid_layout"` | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | DISP-02 | — | popout window opens on click, drag-to-second-monitor works | UAT | manual cell 2 | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | DISP-03 | T-22-SP | popout survives grid toggle without UAF | UAT | manual cell 3 | ❌ W0 | ⬜ pending |
| TBD | TBD | TBD | DISP-04 | — | grid toggle off/on does not disconnect NINJAM session | UAT | manual cell 4 | ❌ W0 | ⬜ pending |

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
