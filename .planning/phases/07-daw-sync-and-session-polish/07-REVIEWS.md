---
phase: 7
reviewers: [codex, claude]
reviewed_at: 2026-04-05T03:15:00Z
plans_reviewed: [07-01-PLAN.md, 07-02-PLAN.md, 07-03-PLAN.md]
---

# Cross-AI Plan Review -- Phase 7

## Codex Review

### Plan 07-01: Transport Sync Core

**Summary:** Directionally correct and fits the current architecture. processBlock is the right place to read AudioPlayHead, run thread is right for server BPM/BPI changes. Main weakness: plan collapses timing domains without fully specifying ownership at interval boundaries, fallback behavior for incomplete host timing, or how transport-driven send muting interacts with AudioProc.

**Strengths:**
- Keeps host transport reads on the audio thread (only JUCE-correct place)
- Preserves three-thread model without introducing locks
- Uses atomics and queue events consistently with current design
- Treats standalone separately (AudioPlayHead may be null)
- Includes edge detection and interval-boundary application

**Concerns:**
- **HIGH**: Does not state exactly how "broadcasting only occurs when DAW is playing" is enforced. If implemented by stopping AudioProc, risks breaking receive audio, metronome, connection liveness. Must mute only local send input.
- **HIGH**: PPQ offset calculation underspecified for JUCE host data quality. Many hosts omit or destabilize ppqPosition during preroll/loop/seek. Without fallback rules, state machine may flap or never lock.
- **HIGH**: No handling for transport discontinuities: loop wrap, seek, punch-in, tempo automation, sample-position reset, host start mid-bar.
- **MEDIUM**: Integer truncation too weak for tempo-change detection. May miss non-integer changes or produce jitter false positives (119.999/120.001).
- **MEDIUM**: Ownership blurred between host sync state and server interval state. SetIntervalPosition() role (authoritative vs advisory) unclear.
- **MEDIUM**: No handling for offline render / non-realtime processing.
- **MEDIUM**: Auto-disable sync event model lacks reason payload (host mismatch vs server change vs manual cancel).
- **LOW**: Threat model's "race conditions avoided by single-thread serialization" only holds for run thread, not audio thread atomics.

**Suggestions:**
- Define audio-path behavior explicitly: zero only local transmit input, not whole processing path
- Split sync ownership (audio thread reads transport, run thread applies server changes, NJClient consumes minimal inputs)
- Specify host-timing fallback order for JUCE
- Replace integer truncation with epsilon or normalized comparison
- Add explicit handling for seek/loop/rewind discontinuities
- Define SetIntervalPosition() contract (block-level, sample-accurate, or interval-boundary-only)
- Add host-behavior checklist for REAPER, Logic, Ableton, Bitwig

**Risk: MEDIUM-HIGH**

---

### Plan 07-02: Sync UI

**Summary:** Covers user-visible requirements well and maps to existing JUCE editor/component structure. Main risk is coupling to unclear state semantics from 07-01. UI assumes distinct sync states, mismatch reasons, session counters, and persistence that are not fully defined in core plan.

**Strengths:**
- UI scope appropriate and aligned with locked decisions
- Correctly hides sync in standalone, keeps BeatBar active
- Uses processor-owned state and ValueTree persistence
- Includes event draining and timer polling matching current design
- Manual visual checkpoint is valuable for DAW UI work

**Concerns:**
- **HIGH**: SYNC-03 depends on accurate session tracking but plan only mentions "timer-based session info updates." Mixing timer-derived elapsed time with run-thread interval counters can drift.
- **HIGH**: Event model only has SyncStateChangedEvent without reason payload. Needs disabled_due_to_bpm_change, waiting_for_downbeat, host_timing_unavailable.
- **MEDIUM**: Inline BPM/BPI vote via !vote needs stricter formatting rules.
- **MEDIUM**: TextEditor overlay in plugin UIs is a known host pain point. Focus handling mentioned but not escape/cancel across hosts.
- **MEDIUM**: Right-click context menu may not behave uniformly in plugin hosts on macOS.
- **LOW**: Flash animation behavior on repeated BPM/BPI changes within flash window unspecified.

**Suggestions:**
- Add dedicated immutable UI sync model from 07-01
- Make SessionInfoStrip entirely snapshot-driven
- Add explicit inline-edit behaviors (Enter commits, Escape cancels, focus loss cancels)
- Define exact !vote output format and normalization rules
- Add non-context-menu path to show/hide info strip
- Extend checkpoint to include editor destruction/recreation while connected

**Risk: MEDIUM**

---

### Plan 07-03: Research Deliverables

**Summary:** Appropriately small, matches requirement scope. Not risky from implementation perspective but should define decision criteria and comparison structure.

**Strengths:**
- Scope contained and aligned with roadmap
- File targets explicit, matching D-20
- Avoids engineering spillover
- Focuses on VDO.Ninja sidecar direction

**Concerns:**
- **MEDIUM**: "No security threats" too broad. Should note privacy/network implications for VDO.Ninja/WebRTC and OSC exposure.
- **MEDIUM**: OSC matrix lacks fixed evaluation rubric.
- **LOW**: MCP assessment may be too shallow without clear separation of transport sync vs session control vs workflow tooling.

**Suggestions:**
- Define fixed template (Goal, Constraints, Findings, Recommendation, Open questions)
- Use consistent OSC columns (transport read/write, tempo access, bridge feasibility, setup burden, reliability)
- Include latency budget and security implications in video doc
- Add dates/version context for external tools

**Risk: LOW**

---

## Claude Review

### Plan 07-01: Transport Sync Core

**Summary:** Well-structured plan, PPQ offset algorithm faithfully adapted from JamTaba's proven implementation. Thread safety model (atomics-only on audio thread) is sound. Two-task decomposition is logical. Some edge cases in sync offset calculation and a subtle thread safety concern in BPM change detection.

**Strengths:**
- Faithful JamTaba port including REAPER float tolerance workaround and Cubase negative-PPQ path
- Correct JUCE 7+ AudioPlayHead usage with std::optional accessors and proper guards
- Audio thread safety: all sync via std::atomic, no mutex/allocation/blocking in processBlock
- NJClient API minimalism: single inline setter, valid same-thread safety argument
- Leverages existing AudioProc isPlaying parameter for transport gating

**Concerns:**
- **HIGH**: Two-boolean sync state race. Run thread auto-disable (store false) can race with audio thread WAITING->ACTIVE transition (store true). Audio thread's store wins, sync activates despite BPM change. Fix: single std::atomic<int> with compare_exchange_strong.
- **MEDIUM**: wasPlaying_ set to overridden hostPlaying (false during WAITING). After WAITING->ACTIVE in same processBlock, wasPlaying_=false (overridden value). Next call sees spurious transport edge. Fix: store raw transport state separately.
- **MEDIUM**: SyncCommand processed on run thread but atomics also written from processBlock. Two separate atomics allow momentary impossible states. Single atomic int is cleaner.
- **LOW**: Integer truncation for BPM comparison -- matches JamTaba but fractional-BPM servers could produce false matches.
- **LOW**: std::to_string in chat messages inconsistent with juce::String usage elsewhere.

**Suggestions:**
- Replace two booleans with std::atomic<int> syncState_{0} (0=IDLE, 1=WAITING, 2=ACTIVE) using compare_exchange_strong
- Store raw transport state separately from overridden hostPlaying (rawHostPlaying pattern)
- Add isStandalone() helper to avoid repeated wrapperType checks
- Both suggestions are implementable during execution without scope change

**Risk: MEDIUM**

---

### Plan 07-02: Sync UI

**Summary:** Comprehensive, covers all interactive elements. Follows existing codebase patterns and handles JUCE-specific concerns. Checkpoint with 10-step test plan is a good safety net. Main concerns: BeatBar paint() rewrite complexity and TextEditor lifecycle.

**Strengths:**
- Consistent with existing patterns (TextButton styling, setOpaque paint pattern)
- Focus management handled (unfocusAllComponents, referencing RESEARCH Pitfall 5)
- Input validation tight (setInputRestrictions + range check)
- State persistence follows established pattern (ValueTree like chatSidebarVisible)
- Three-state sync button design intuitive with color feedback

**Concerns:**
- **HIGH**: BeatBar paint() rewrite is fragile. Partial implementation leaves executor to merge two paint implementations. Existing paint() uses getWidth() in calculations that must change to (getWidth() - kLabelAreaWidth). Risk of segment sizing bugs.
- **MEDIUM**: BubbleMessageComponent lifecycle. showAt() called before addChildComponent -- order should be reversed.
- **MEDIUM**: BeatBar processorPtr_ is raw pointer. If processor destroyed before BeatBar, dangles. Use reference like ConnectionBar.
- **MEDIUM**: onFocusLost lambda calls dismissVoteEditor() which destroys TextEditor during its own focusLost callback. This is undefined behavior. Fix: MessageManager::callAsync to defer destruction.
- **LOW**: Right-click context menu integration needs verification of existing mouseDown override.
- **LOW**: SessionInfoStrip "connected" heuristic could be false on first interval.

**Suggestions:**
- Provide complete BeatBar::paint() method to avoid merge errors
- Defer dismissVoteEditor() via MessageManager::callAsync
- Use reference instead of raw pointer for BeatBar processor access
- Fix BubbleMessageComponent order (addChildComponent before showAt)
- Add startTimerHz(30) to BeatBar during flash animation for smoother visuals

**Risk: MEDIUM**

---

### Plan 07-03: Research Deliverables

**Summary:** Straightforward documentation-only plan. Content outlines thorough, OSC matrix covers major DAWs, MCP assessment correctly identifies protocol mismatch. No build risk.

**Strengths:**
- Clear document structure with specific sections
- Correct MCP analysis (request/response vs streaming mismatch)
- VDO.Ninja focus is correct (WebRTC superior to H.264-over-NINJAM)
- Appropriately scoped per D-19

**Concerns:**
- **LOW**: OSC matrix accuracy (Ableton M4L included in Suite since Live 12, Bitwig is via controller API not native OSC, FL Studio has some OSC via MIDI scripting)
- **LOW**: VDO.Ninja alpha URL may change
- **LOW**: Missing concrete latency quantification (WebRTC 100-300ms vs NINJAM 16s at 32/120)

**Suggestions:**
- Verify OSC matrix entries against current DAW docs
- Add concrete latency comparison table to VIDEO-FEASIBILITY.md
- Note alpha URL instability

**Risk: LOW**

---

## Consensus Summary

### Agreed Strengths
- **Architecture is sound**: Both reviewers agree processBlock is the correct place for AudioPlayHead queries and the three-thread model is preserved correctly (Codex + Claude)
- **JamTaba algorithm port is proven**: Both acknowledge the PPQ offset calculation is a faithful port of production-tested code (Codex + Claude)
- **Wave structure and dependencies are correct**: 07-01 -> 07-02 ordering is right, 07-03 independent (Codex + Claude)
- **All 8 requirements covered**: Complete coverage validated by both reviewers (Codex + Claude)
- **Research plan is low-risk and well-scoped**: Both rate Plan 03 as LOW risk (Codex + Claude)

### Agreed Concerns

**1. Two-boolean sync state machine is racy (HIGH)**
Both reviewers independently identified that using two `std::atomic<bool>` (syncWaiting_, syncActive_) creates a race between run thread auto-disable and audio thread WAITING->ACTIVE transition. Both recommend replacing with a single `std::atomic<int>` state machine using `compare_exchange_strong`.

**2. Event model lacks reason/context payloads (MEDIUM-HIGH)**
Both note that SyncStateChangedEvent doesn't carry reason (server BPM change, manual cancel, host timing unavailable). The UI needs this to show appropriate feedback.

**3. BeatBar paint() rewrite is fragile (HIGH per Claude, implicit in Codex)**
Claude specifically flags that the partial paint() implementation requires the executor to merge getWidth() substitutions correctly. Codex raises related concerns about host timing edge cases that would affect the same visual components.

**4. Transport edge cases underspecified (HIGH per Codex, MEDIUM per Claude)**
Codex emphasizes missing handling for loop/seek/preroll/offline-render. Claude identifies the wasPlaying_ override bug that relates to edge detection. Both agree the state machine needs more specified behavior for non-trivial transport scenarios.

**5. TextEditor lifecycle in plugin context (MEDIUM)**
Claude identifies the focusLost self-destruction UB. Codex raises related TextEditor focus handling concerns across hosts. Both agree deferred destruction is needed.

### Divergent Views

**Transport discontinuity handling:**
- Codex treats this as HIGH priority and wants explicit handling for loop/seek/punch-in/offline-render before implementation
- Claude treats it as MEDIUM, noting the JamTaba algorithm is proven and the wasPlaying_ bug is the more immediate concern. Both are valid -- Codex's view is more conservative (design it now), Claude's is more pragmatic (fix the concrete bug, handle edge cases during testing)

**BPM comparison method:**
- Codex wants epsilon-based comparison to replace integer truncation
- Claude notes it matches JamTaba and is a known trade-off, LOW priority
- Both have merit; the JamTaba precedent is persuasive but epsilon would be more robust

**OSC matrix accuracy:**
- Claude provides specific corrections (Ableton Live 12 includes M4L, Bitwig is controller API not native)
- Codex raises the structural concern (no fixed rubric)
- Complementary views -- both should be addressed
