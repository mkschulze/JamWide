---
phase: 06
reviewers: [claude, codex]
reviewed_at: 2026-04-04T21:10:00Z
plans_reviewed: [06-01-PLAN.md, 06-02-PLAN.md]
---

# Cross-AI Plan Review -- Phase 06

## Claude Review

Now I have a thorough understanding of the codebase. Here's my review:

---

# Phase 6 Plan Review: Multichannel Output Routing

## Plan 06-01: Audio Backend (Wave 1)

### Summary

A well-structured plan that correctly leverages NJClient's existing `out_chan_index` routing infrastructure rather than reinventing it. The signal flow design is sound -- expand the AudioProc output buffer, let NJClient handle per-user routing internally, then post-process for main-mix accumulation. However, there is one critical bug (metronome bus collision), one data race, and a behavioral change to the metronome's relationship with master volume that should be addressed before execution.

### Strengths

- **Leverages existing NJClient infrastructure.** The plan correctly identifies that `out_chan_index`, `find_unused_output_channel_pair()`, `config_remote_autochan`, and `SetUserChannelState` already exist and just need to be wired through the command queue. No modifications to NJClient internals.
- **Clean two-pass quick-assign sweep.** Reset all to bus 0, then reassign sequentially -- avoids fragmentation and ensures deterministic assignment order.
- **Correct handling of the `isEnabled()` pitfall.** DAWs may disable buses; the plan explicitly checks `outputBus->isEnabled()` before writing. This is essential for Logic Pro and Bitwig which don't enable all buses by default.
- **AudioProc zeros the output buffer** (confirmed at `njclient.cpp:772`), so the `outputScratch.setSize()` with `keepExistingContent=false` doesn't leave garbage -- no hidden buffer initialization bug.
- **Persistence is scoped correctly per D-12.** Only the routing mode (0/1/2) is persisted, not individual user-to-bus mappings. `juce::jlimit(0, 2, rawRoutingMode)` validates on restore.
- **Complete acceptance criteria.** Every code change has a corresponding grep-verifiable acceptance criterion.

### Concerns

- **HIGH: Metronome bus collision with auto-assign.** `config_remote_autochan_nch = 34` means `find_unused_output_channel_pair()` searches channels 2 through 32 in steps of 2 (`njclient.cpp:1007`). This includes channels 32-33, which is the metronome bus (D-11). A user could be auto-assigned to the same output pair as the metronome, causing mixed audio on that bus. Fix: use `config_remote_autochan_nch = 32` to exclude the metronome pair from the search space. This reduces user-assignable buses to 15 (channels 2-31), with channel 32-33 reserved exclusively for metronome. Alternatively, declare 18 output buses (36 channels) and put metronome on channels 34-35, preserving 16 user buses.

- **HIGH: `routingMode` is a plain `int` with cross-thread access.** The message thread writes it (Plan 06-02 popup callback: `processorRef.routingMode = mode;`), and the run thread reads it (Plan 06-01 Task 1 step 7: `client->config_remote_autochan = processor.routingMode;`). This is a data race per C++ memory model. It must be `std::atomic<int>`.

- **MEDIUM: Metronome volume behavior changes.** In original NJClient code (`njclient.cpp:2151-2174`), the metronome is mixed into outbuf[0..1] *after* master volume is applied at line 2109-2148 -- so the metronome is independent of master volume. In the new plan, the metronome goes to bus 16 (channels 32-33), then the accumulation loop applies master volume when summing it into the main mix: `mainL[s] += busL[s] * mvL`. This means the metronome is now *attenuated by master volume* in the main mix, which is a behavioral change. Musicians typically expect the click to remain audible when they pull down the master fader. Fix: either skip the metronome bus in the accumulation loop and add it to main mix without master volume, or document this as intentional.

- **MEDIUM: D-06 (overflow fallback to Main Mix) not fully met.** `find_unused_output_channel_pair()` at `njclient.cpp:1006-1016` never returns 0 when `config_remote_autochan_nch >= 4` -- if all pairs are occupied, it returns the *least-used* pair (always >= 2). So when all buses are full and a new user joins (triggering the auto-assign at `njclient.cpp:1265-1290`), they get doubled-up on an existing bus rather than falling back to Main Mix as D-06 specifies. The quick-assign sweep avoids this (it resets first), but subsequent user joins during an active auto-assign mode are affected.

- **LOW: `outputScratch.setSize()` called on audio thread.** Even with `avoidReallocating=true`, a buffer-size change could trigger heap allocation. Pre-allocating in `prepareToPlay()` with maximum block size and using `outputScratch.clear()` in `processBlock` would be safer. AudioProc does zero the buffer, so this is mitigated in practice, but it's still a latent allocation hazard if the DAW calls `processBlock` with a larger block size before `prepareToPlay`.

### Suggestions

- Change `config_remote_autochan_nch` from `34` to `32` everywhere it appears. Update the UI to show "Remote 1" through "Remote 15" for user-assignable buses, with "Remote 16" documented as metronome-only. Or, if 16 user buses are important, add an 18th output bus.
- Change `int routingMode{0}` to `std::atomic<int> routingMode{0}` in JamWideJuceProcessor.h. Adjust all reads/writes to use `.load()` / `.store()`.
- Add special handling for the metronome bus in the accumulation loop:
  ```cpp
  // Accumulate user buses (skip metronome bus)
  for (int bus = 1; bus < kNumOutputBuses - 1; ++bus) { /* with master vol */ }
  // Accumulate metronome WITHOUT master vol (preserves original behavior)
  { mainL[s] += metroBusL[s]; mainR[s] += metroBusR[s]; }
  ```
- Pre-allocate `outputScratch` in `prepareToPlay()`:
  ```cpp
  void prepareToPlay(double sr, int maxBlock) {
      outputScratch.setSize(kTotalOutChannels, maxBlock, false, true, false);
  }
  ```
  Then in processBlock, just use it (AudioProc zeros it anyway).

### Risk Assessment: **MEDIUM-HIGH**

The core signal flow design is correct and well-reasoned. The metronome bus collision is a functional correctness bug that could cause audible glitches in real sessions, but it's a one-line fix. The data race is technically UB but unlikely to cause visible issues in practice (int reads/writes are typically atomic on x86/ARM). The metronome-master-volume interaction is a design choice that should be explicitly decided rather than accidentally introduced.

---

## Plan 06-02: UI Wiring (Wave 2)

### Summary

A straightforward UI wiring plan that connects the Route button and routing selectors to the command infrastructure from Plan 01. The approach is clean -- popup menu on the ConnectionBar, updated ComboBox items on ChannelStrip, callbacks through the existing command queue. The plan correctly avoids triggering onChange when updating selectors from snapshot data (`dontSendNotification`).

### Strengths

- **Uses existing UI patterns.** The Route button follows the same TextButton + PopupMenu pattern as other ConnectionBar controls. Color tokens reference the shared LookAndFeel constants.
- **Correct snapshot-driven refresh.** `setRoutingBus()` uses `dontSendNotification` to avoid feedback loops where a snapshot update triggers a command that triggers another snapshot.
- **Green highlight feedback** on both the Route button and individual routing selectors gives clear visual indication of active routing state.
- **Clean bus-index-to-channel conversion.** `cmd.outchannel = busIndex * 2` is the correct mapping (bus 0 = channel 0, bus 1 = channel 2, etc.), matching NJClient's `out_chan_index` convention.
- **Tooltip per D-15.** The "Enable additional outputs..." tooltip is placed on the Route button where it's most discoverable.
- **Persisted state initialization.** `setRoutingModeHighlight(processorRef.routingMode)` in the editor constructor ensures the Route button reflects the saved state on editor open.

### Concerns

- **MEDIUM: Route button popup accesses `processorRef.routingMode` for checkmark state.** The popup lambda captures `this` and reads `processorRef.routingMode` to show the tick next to the current mode. If `routingMode` is made `std::atomic<int>` (per Plan 01 fix), this needs `.load()`. Currently it also writes `processorRef.routingMode = mode;` directly from the message thread popup callback -- this should be the canonical write site, but only if the run thread doesn't also write it.

- **MEDIUM: `files_modified` frontmatter is incomplete.** Task 2 explicitly modifies `ChannelStrip.h` (adding `setRoutingBus` declaration) and `ChannelStrip.cpp` (adding implementation), but neither is in the YAML frontmatter `files_modified` list. This could cause execution validation issues if the GSD framework checks modified files against the manifest.

- **LOW: Menu item ID-to-mode mapping is non-sequential.** Item 1 -> mode 0 (Manual), Item 2 -> mode 2 (By User), Item 3 -> mode 1 (By Channel). The 2<->3 swap is easy to misread during maintenance. Consider using named constants or reordering to match.

- **LOW: Routing selector shows 17 items (Main Mix + Remote 1-16) but not all may be usable.** If Plan 01 is fixed to use `config_remote_autochan_nch = 32` (15 user buses), the selector would still show "Remote 16" which is the metronome bus. Selecting it manually would route a user's audio to the same bus as the metronome. The selector items should match the actual available buses.

- **LOW: No visual indication of bus occupancy.** When a user manually picks a bus from the routing selector, there's no way to see if another user is already on that bus. This isn't a plan defect per se (it's not in the requirements), but it's a usability gap worth noting for future work.

### Suggestions

- Add `ChannelStrip.h` and `ChannelStrip.cpp` to the `files_modified` frontmatter in the YAML header.
- If 06-01 changes to `nch = 32`, update the routing selector to show "Main Mix" + "Remote 1" through "Remote 15" (15 items, not 16), and optionally add "Metronome" as a read-only label for the last bus.
- Consider adding a comment in the popup callback explaining the ID-to-mode mapping, or use an enum:
  ```cpp
  enum { kMenuManual = 1, kMenuByUser, kMenuByChannel };
  ```
- Ensure all `processorRef.routingMode` accesses use atomic operations if the field is made atomic per the Plan 01 fix.

### Risk Assessment: **LOW**

This plan is largely mechanical UI wiring with well-understood patterns. The concerns are mostly about consistency with Plan 01 fixes and minor maintainability issues. No audio thread safety concerns since all work is on the message thread with commands dispatched through the lock-free SPSC queue.

---

## Cross-Plan Assessment

### Dependency Correctness
Plan 06-02 correctly depends on 06-01. All interfaces consumed by 06-02 (SetRoutingModeCommand, set_outch field, out_chan_index in snapshot, routingMode member) are produced by 06-01. No circular dependencies.

### Coverage of Phase Requirements

| Requirement | Coverage | Notes |
|---|---|---|
| MOUT-01 | Plan 01 + 02 | Per-user routing to separate stereo pairs via expanded AudioProc + routing selector |
| MOUT-02 | Plan 01 + 02 | By-user auto-assign via SetRoutingModeCommand + Route button popup |
| MOUT-03 | Plan 01 + 02 | By-channel auto-assign via SetRoutingModeCommand + Route button popup |
| MOUT-04 | Plan 01 | Metronome on bus 16 via SetMetronomeChannel(32) + accumulation into main mix |
| MOUT-05 | Plan 01 | Main mix always on bus 0 via post-AudioProc accumulation loop |

All five requirements are addressed.

### Missing from Both Plans

- **D-16 (username persistence verification)** is mentioned in constraints but neither plan addresses it. This should be a verification step during or after execution, not necessarily a code task.
- **DAW testing (D-14)** is in the success criteria but not in the plans' verification steps. The plans verify compilation only. Manual DAW testing in REAPER, Logic Pro, and Bitwig should be explicitly called out as a post-execution verification step.
- **D-05 (bus reservation on user leave)** is inherently handled by NJClient -- when a user disconnects, their `RemoteUser` is removed. The bus becomes "available" because no `RemoteUser_Channel` references it anymore. However, the plan doesn't explicitly clear bus assignments when users leave in manual mode, which matches D-05. Worth a verification note.

### Overall Phase Risk: **MEDIUM**

---

## Codex Review

# Plan Review: Phase 6 Multichannel Output Routing

## Plan 06-01: Audio Backend

### Summary
Plan 06-01 is pointed in the right direction and uses the existing NJClient routing primitives instead of inventing a parallel system, which is the correct architectural choice. The main risk is that a few key behaviors in the current plan do not actually satisfy the phase decisions as written: bus reservation on user departure, metronome bus exclusivity, and audio-thread-safe buffer management all need to be tightened before execution.

### Strengths
- Reuses NJClient's existing `out_chan_index`, `config_remote_autochan`, and `find_unused_output_channel_pair()` instead of reimplementing routing logic.
- Keeps routing changes on the run thread via the command queue, which matches the existing thread ownership model.
- Separates "per-user bus routing" from "main mix always on bus 0" with post-processing accumulation in `processBlock()`, which is the cleanest way to satisfy MOUT-05.
- Explicitly calls out disabled JUCE buses and the need to guard `getBusBuffer()` writes.
- Persists routing mode outside APVTS, which is the right choice for non-automatable UI/session state.

### Concerns
- **HIGH**: The plan does not preserve bus reservations after a user leaves, but D-05 requires that behavior. `find_unused_output_channel_pair()` only scans current remote users, so vacated buses will be reused by future joins unless JamWide tracks reservations separately.
- **HIGH**: The plan assigns the metronome to channels `32/33` while also allowing auto-assignment across all 16 remote buses. That means Remote 16 can collide with the metronome unless the allocator explicitly excludes the last bus from remote-user assignment.
- **HIGH**: `outputScratch.setSize(...)` in `processBlock()` introduces allocation risk on the audio thread. The plan should specify preallocation in `prepareToPlay()` and a strategy for block-size growth without heap work in the realtime path.
- **MEDIUM**: "Apply routing mode on connect" is underspecified for reconnection and state restore ordering. The persisted processor state, NJClient runtime config, and UI highlight can drift unless one component is designated as the source of truth and replayed consistently.
- **MEDIUM**: The plan says "persist metronome bus setting," but the bus is fixed by D-11. Persisting a constant adds state surface without real user value.
- **MEDIUM**: Validation is too weak for this wave. Grep/build checks do not verify multi-bus correctness, metronome isolation, or bus-0 accumulation behavior.

### Suggestions
- Add an explicit JamWide-side reservation table for remote buses so D-05 is enforceable independently of NJClient's current-user scan.
- Reserve the final stereo pair exclusively for metronome and constrain remote auto-assignment to buses 1-15, or document that only 15 remote buses remain available.
- Preallocate `inputScratch` and `outputScratch` in `prepareToPlay()`, and only resize off the audio thread or behind a bounded non-allocating strategy.
- Define one authoritative routing-state flow:
  1. `routingMode` stored on processor
  2. replayed to run thread on connect/reconnect
  3. mirrored back to UI from acknowledged state
- Add at least one automated processor-level test that verifies:
  - routed remote audio appears on its dedicated pair,
  - bus 0 still contains that audio,
  - metronome is isolated to the reserved bus.

### Risk Assessment
**HIGH**. The overall direction is sound, but the current plan has requirement-level gaps that can cause incorrect routing behavior even if the code compiles cleanly.

---

## Plan 06-02: UI Wiring

### Summary
Plan 06-02 is mostly scoped correctly and builds on the right integration points, but it currently mixes UI optimism with backend state, and it under-specifies how multi-channel users behave in the strip model. The biggest issue is that the proposed "refresh from snapshot on each poll cycle" conflicts with the project's current `cachedUsers` threading contract and can create a data-race-prone update path.

### Strengths
- Keeps UI-originated routing changes flowing through the existing command queue instead of calling NJClient directly.
- Reuses the existing routing selector rather than adding new routing controls per strip.
- Provides a small, coherent UX surface: one quick-assign control plus per-strip overrides.
- Uses existing look-and-feel tokens and existing editor wiring patterns.

### Concerns
- **HIGH**: The plan proposes refreshing routing selectors from `cachedUsers` on each poll cycle, but `cachedUsers` is written by the run thread and read on the message thread without a read lock. The current design only stays defensible when reads happen after event delivery; poll-driven reads widen the race window.
- **MEDIUM**: The UI sets `processorRef.routingMode` immediately from the Route popup before the command queue is acknowledged. If `try_push()` fails or the run thread lags, the UI and persisted state can claim a mode change that never actually reached NJClient.
- **MEDIUM**: Multi-channel-user behavior is not fully resolved. In by-user mode all channels for a user share one bus, but the current strip model has a parent strip plus child strips. The plan does not clearly define which selectors are authoritative, how the parent reflects shared routing, or how conflicting child edits should be presented.
- **MEDIUM**: The plan file's declared `files_modified` is inconsistent with the task body. `juce/JamWideJuceEditor.cpp` and `juce/ui/ChannelStrip.h` are required by the task but omitted from the front matter, which is execution-risky in a GSD workflow.
- **LOW**: The color-change behavior is purely visual and not tied to effective DAW bus enablement. A strip can appear "routed" even when the host has that auxiliary output disabled.

### Suggestions
- Update routing selectors only during `UserInfoChangedEvent`-driven refresh, or move to a copied snapshot/lock-protected read model before adding poll-based updates.
- Treat the run-thread-applied mode as authoritative. The UI can optimistically highlight, but it should revert or refresh from acknowledged state rather than directly mutating processor/session state first.
- Define explicit multi-channel strip semantics:
  - parent strip shows shared user routing in by-user mode,
  - child strips show per-channel routing in manual/by-channel mode,
  - manual child override behavior is deterministic and documented.
- Fix the front matter so all touched files are declared.
- Add one manual UAT step specifically for:
  - multi-channel user in by-user mode,
  - manual override of one child strip,
  - subsequent new-user join.

### Risk Assessment
**MEDIUM-HIGH**. The UI work is conceptually straightforward, but the current plan has state-consistency and threading gaps that could make the routing UI misleading or unstable.

---

## Overall Assessment

### Summary
The phase plan is architecturally strong at a high level and sensibly leverages NJClient's existing routing system, but it is not execution-ready yet. The main blockers are requirement mismatches around reserved buses and metronome isolation, plus a weakly specified state/thread model between processor, run thread, and UI.

### Highest-Priority Fixes
- Make bus reservation semantics explicit and implementable.
- Reserve the metronome bus so remote routing cannot collide with it.
- Remove audio-thread allocation from the plan.
- Avoid poll-based `cachedUsers` reads unless snapshot ownership is made thread-safe.
- Strengthen verification beyond grep/build checks.

---

## Consensus Summary

### Agreed Strengths
- **Correct architectural choice:** Both reviewers agree the plan correctly reuses NJClient's existing `out_chan_index`, `config_remote_autochan`, and `find_unused_output_channel_pair()` infrastructure instead of building parallel routing logic. (Claude + Codex)
- **Clean main-mix accumulation pattern:** Post-AudioProc accumulation into bus 0 is the right way to satisfy MOUT-05 while keeping individual buses isolated. (Claude + Codex)
- **Command queue threading model:** All routing state changes flow through the SPSC command queue to the run thread, matching the established architecture. (Claude + Codex)
- **JUCE disabled-bus guard:** Both reviewers note the correct `isEnabled()` check before writing to JUCE output buses. (Claude + Codex)
- **Routing mode persistence outside APVTS:** Non-automatable state stored as ValueTree property is the right pattern. (Claude + Codex)

### Agreed Concerns
- **CRITICAL: Metronome bus collision with auto-assign (HIGH):** Both reviewers independently identified that `config_remote_autochan_nch = 34` allows auto-assignment to channels 32-33 which collides with the metronome bus. Fix: constrain to `nch = 32` or add an 18th bus. (Claude + Codex)
- **HIGH: Audio-thread allocation in `outputScratch.setSize()`:** Both reviewers flag that calling `setSize()` in `processBlock()` can trigger heap allocation. Pre-allocate in `prepareToPlay()` instead. (Claude + Codex)
- **MEDIUM: `files_modified` frontmatter incomplete in 06-02:** Both note that ChannelStrip.h/cpp and JamWideJuceEditor.cpp are modified but not listed in the YAML header. (Claude + Codex)
- **MEDIUM: Validation is compilation-only:** Neither plan includes functional verification of multi-bus correctness, metronome isolation, or main-mix accumulation behavior. (Claude + Codex)

### Divergent Views
- **D-05 bus reservation:** Codex rates this HIGH (requires a JamWide-side reservation table), while Claude treats it as correctly handled by omission (NJClient removes the user, bus becomes available, matching D-05's "stays reserved until manually reassigned" since nobody auto-shifts). The truth depends on whether D-05 means "reserved across reconnections" or just "no auto-shifting during a session."
- **`routingMode` data race:** Claude explicitly flags the `int routingMode` cross-thread access as HIGH (technically UB), while Codex frames it as MEDIUM (underspecified state flow). Both agree it needs atomic access or single-thread ownership.
- **Metronome volume behavior:** Claude identifies a behavioral change (metronome becomes attenuated by master volume in main mix accumulation). Codex does not raise this concern. This is a design decision that should be explicitly addressed.
- **`cachedUsers` threading:** Codex raises a HIGH concern about poll-driven reads from `cachedUsers` widening a race window. Claude does not flag this, noting the existing snapshot-driven refresh pattern handles it correctly.
