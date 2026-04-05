# Phase 7: DAW Sync and Session Polish - Research

**Researched:** 2026-04-05
**Domain:** JUCE AudioPlayHead transport sync, NINJAM interval alignment, BPM/BPI voting, session tracking
**Confidence:** HIGH

## Summary

Phase 7 implements DAW transport-aware broadcasting (silence send when DAW is stopped), full JamTaba-style sync with PPQ offset alignment, live BPM/BPI change handling at interval boundaries, session position tracking, BPM/BPI vote UI, standalone pseudo-transport, and three research deliverables (video, OSC, MCP).

The existing codebase already provides the critical building blocks: NJClient::AudioProc accepts `isPlaying`, `isSeek`, and `cursessionpos` parameters (currently hardcoded to defaults); NJClient applies BPM/BPI changes at interval boundaries via `updateBPMinfo()` + `m_beatinfo_updated` flag; `m_loopcnt` tracks interval count; `GetSessionPosition()` returns elapsed milliseconds; the SPSC command/event queues handle UI-to-audio-thread communication; and the BeatBar already renders beat position from `uiSnapshot` atomics. The JamTaba DAW sync analysis (`.planning/references/JAMTABA-DAW-SYNC-ANALYSIS.md`) provides a complete implementation blueprint that maps directly to JUCE's `AudioPlayHead::PositionInfo` API.

**Primary recommendation:** Implement transport sync and silence-on-stop first (processBlock AudioPlayHead query), then layer the 3-state sync machine, then add vote UI and session info strip as separate UI tasks. Research deliverables are independent documents written to `.planning/references/`.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** When DAW transport stops, silence send only -- stop broadcasting audio but keep hearing remote participants. Connection stays active.
- **D-02:** Full JamTaba-style sync -- 3-state machine (IDLE -> WAITING -> ACTIVE). Sync button, BPM match validation (integer comparison), PPQ offset calculation, NINJAM interval aligned to DAW measure boundary.
- **D-03:** Sync button lives in ConnectionBar, next to existing Route and Fit buttons.
- **D-04:** During sync WAITING state, send silence -- don't broadcast until synced. Remote users hear nothing until host transport starts.
- **D-05:** When sync is active and server BPM changes, auto-disable sync (JamTaba behavior) -- notify user to match host tempo and re-sync.
- **D-06:** JUCE target only for DAW sync -- CLAP plugin is the legacy target and does not get sync features.
- **D-07:** Sync button hidden in standalone mode -- no host transport available, no point showing the button.
- **D-08:** Dedicated BPM/BPI vote controls integrated into the BeatBar -- click the BPM or BPI display to open an inline edit field.
- **D-09:** Direct edit + Enter to vote -- click BPM value, type new number, press Enter sends `!vote bpm N` via existing chat command mechanism. Same for BPI.
- **D-10:** BeatBar flash/highlight -- flash the BPM/BPI text for 2-3 seconds when server changes tempo.
- **D-11:** System chat message -- post "[Server] BPM changed from X to Y" in the chat panel when BPM/BPI changes.
- **D-12:** Display: interval count, elapsed session time, current beat / total beats, sync status indicator.
- **D-13:** Session info strip lives below the BeatBar -- a dedicated info strip.
- **D-14:** Collapsible/toggleable -- user controls whether info strip is expanded or hidden.
- **D-15:** Hidden by default -- available via a toggle, takes no space when hidden.
- **D-16:** Auto-play on connect -- standalone always broadcasts when connected. No play/stop button needed. Connecting IS starting.
- **D-17:** BeatBar works in standalone -- it already uses uiSnapshot data from NJClient interval position. No extra work needed for pseudo-beat display.
- **D-18:** Video feasibility doc (RES-01) focuses on VDO.Ninja WebRTC sidecar approach.
- **D-19:** OSC evaluation (RES-02) and MCP assessment (RES-03) are brief summaries -- half-page each.
- **D-20:** Research deliverables live in `.planning/references/`.
- **D-21:** Persist session info strip visibility via ValueTree property. Sync preference does NOT persist -- starts fresh each session.

### Claude's Discretion
- PPQ-to-sample offset calculation implementation details (adapt JamTaba algorithm for JUCE AudioPlayHead::PositionInfo)
- Sync state machine internal design (atomic flags, command queue integration)
- BeatBar inline edit implementation (TextEditor overlay, popup, or custom painted)
- Session info strip compact vs expanded layout details
- Research document internal structure and depth beyond stated scope
- BPM vote validation ranges (40-400 BPM, 2-192 BPI per JamTaba constants, or different)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SYNC-01 | Plugin reads host transport state (playing/stopped) via AudioPlayHead | AudioPlayHead::PositionInfo::getIsPlaying() returns bool (non-optional). Query in processBlock, pass to AudioProc's existing `isPlaying` param. |
| SYNC-02 | Broadcasting only occurs when DAW is playing | AudioProc already accepts `isPlaying` param (defaults true). Pass actual host state. NJClient::process_samples suppresses encoding when `!isPlaying`. |
| SYNC-03 | Session position tracked across intervals | NJClient::GetLoopCount() returns m_loopcnt (interval count). GetSessionPosition() returns elapsed ms. Both accessible from run thread for uiSnapshot. |
| SYNC-04 | Live BPM/BPI changes applied at interval boundaries without reconnect | Already implemented in NJClient::AudioProc via m_beatinfo_updated flag. Server sends CONFIG_CHANGE_NOTIFY, updateBPMinfo() sets flag, AudioProc applies at next boundary. Need UI notification only. |
| SYNC-05 | Standalone mode provides pseudo-transport with server BPM | D-16/D-17: standalone always broadcasts (isPlaying=true default). BeatBar already works from uiSnapshot. No sync button needed. |
| RES-01 | Video feasibility document | VDO.Ninja WebRTC sidecar approach per D-18. Write to .planning/references/VIDEO-FEASIBILITY.md |
| RES-02 | OSC cross-DAW sync evaluation | Half-page per D-19. Write to .planning/references/OSC-EVALUATION.md |
| RES-03 | MCP bridge feasibility assessment | Half-page per D-19. Write to .planning/references/MCP-ASSESSMENT.md |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | AudioPlayHead API, UI components, ValueTree persistence | Already pinned as project submodule; AudioPlayHead::PositionInfo is the transport query API |
| NJClient | (bundled) | NINJAM protocol, AudioProc with isPlaying param, interval engine | Core audio engine already in project; has all hooks needed for transport sync |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| std::atomic | C++17 | Sync state flags (waitingForHostSync, syncActive) | Audio-thread-safe state sharing without locks |
| SpscRing | (bundled) | Sync commands via existing cmd_queue / evt_queue | UI thread sync button actions -> run thread |

No new dependencies required. All functionality builds on existing JUCE 8.0.12 APIs and NJClient infrastructure.

## Architecture Patterns

### Recommended Integration Points

```
JamWideJuceProcessor::processBlock()
  |-- Query getPlayHead()->getPosition()
  |-- Extract isPlaying, BPM, PPQ, barStart, timeSig
  |-- Edge detection: wasPlaying_ -> isPlaying transition
  |-- If waitingForHostSync && transport started: calculate offset, set m_interval_pos
  |-- Pass isPlaying to AudioProc (currently hardcoded true)
  |
NinjamRunThread::run()
  |-- Expose m_loopcnt via uiSnapshot.interval_count atomic
  |-- Expose GetSessionPosition() via uiSnapshot.session_elapsed_ms atomic
  |-- Detect BPM/BPI changes: compare previous vs current, push BpmChangedEvent
  |
ConnectionBar
  |-- Sync button (TextButton, hidden when standalone per D-07)
  |-- onClick: validate BPM match, push SyncCommand to cmd_queue
  |
BeatBar
  |-- Add BPM/BPI text labels (clickable for vote per D-08)
  |-- Add TextEditor overlay for inline editing
  |-- Flash animation on BPM change (D-10)
  |
SessionInfoStrip (new component)
  |-- Interval count, elapsed time, beat/total, sync status
  |-- Collapsible, hidden by default (D-14, D-15)
  |-- Lives below BeatBar in editor layout
```

### Pattern 1: Transport Edge Detection in processBlock

**What:** Detect stopped-to-playing transport transition on audio thread for sync trigger.
**When to use:** Every processBlock call when sync is in WAITING state.
**Example:**
```cpp
// In JamWideJuceProcessor::processBlock():
auto* playHead = getPlayHead();
bool hostPlaying = false;
double hostBpm = 0.0;
double ppqPos = 0.0;
double barStart = 0.0;
int timeSigNum = 4;

if (playHead)
{
    if (auto pos = playHead->getPosition())
    {
        hostPlaying = pos->getIsPlaying();
        if (auto bpm = pos->getBpm())         hostBpm = *bpm;
        if (auto ppq = pos->getPpqPosition()) ppqPos = *ppq;
        if (auto bar = pos->getPpqPositionOfLastBarStart()) barStart = *bar;
        if (auto sig = pos->getTimeSignature()) timeSigNum = sig->numerator;
    }
}

// Edge detection (audio-thread-only state, no sync needed)
bool transportJustStarted = hostPlaying && !wasPlaying_;
wasPlaying_ = hostPlaying;

// If sync waiting and transport just started: calculate offset
if (syncWaiting_.load(std::memory_order_acquire) && transportJustStarted)
{
    int offset = calculateSyncOffset(ppqPos, barStart, timeSigNum,
                                      hostBpm, storedSampleRate);
    // Apply offset to NJClient interval position
    applySyncOffset(offset);
    syncWaiting_.store(false, std::memory_order_release);
    syncActive_.store(true, std::memory_order_release);
}

// Pass actual transport state to AudioProc
client->AudioProc(inPtrs, numInputChannels, outPtrs, kTotalOutChannels,
                  numSamples, static_cast<int>(storedSampleRate),
                  false, hostPlaying);
```
**Source:** JUCE 8.0.12 AudioPlayHead.h, JamTaba sync analysis section 2.2

### Pattern 2: PPQ-to-Sample Offset Calculation (JamTaba Adaptation)

**What:** Calculate how many samples to offset NINJAM interval start to align with DAW measure boundary.
**When to use:** When sync transitions from WAITING to ACTIVE on transport start.
**Example:**
```cpp
// Adapt JamTaba's getStartPositionForHostSync() for JUCE PositionInfo
int calculateSyncOffset(double ppqPos, double barStart, int timeSigNum,
                        double hostBpm, double sampleRate)
{
    double samplesPerBeat = (60.0 * sampleRate) / hostBpm;
    int startPosition = 0;

    if (ppqPos > 0.0)
    {
        double cursorInMeasure = ppqPos - barStart;
        if (cursorInMeasure > 0.00000001)  // Float tolerance (Reaper bug workaround)
        {
            double samplesUntilNextMeasure =
                (timeSigNum - cursorInMeasure) * samplesPerBeat;
            startPosition = -static_cast<int>(samplesUntilNextMeasure);
        }
    }
    else
    {
        startPosition = static_cast<int>(ppqPos * samplesPerBeat);
    }
    return startPosition;
}

// Apply offset using JamTaba's modulo arithmetic
void applySyncOffset(int startPosition)
{
    int intervalLen = client->GetPosition(nullptr, nullptr); // get length
    int pos, len;
    client->GetPosition(&pos, &len);

    if (startPosition >= 0)
        // Direct position within interval (m_interval_pos is public enough via GetPosition)
        // Need to set m_interval_pos - see "Don't Hand-Roll" for how to expose this
        newIntervalPos = startPosition % len;
    else
        newIntervalPos = len - std::abs(startPosition % len);
}
```
**Source:** JAMTABA-DAW-SYNC-ANALYSIS.md sections 2.3, 2.4

### Pattern 3: Sync State Machine

**What:** Three-state machine controlling sync behavior (IDLE, WAITING, ACTIVE).
**When to use:** Manages the sync lifecycle from button click to synced playback.
```
enum class SyncState { IDLE, WAITING, ACTIVE };

// On processor (audio-thread-safe):
std::atomic<bool> syncWaiting_{false};   // WAITING state flag
std::atomic<bool> syncActive_{false};    // ACTIVE state flag
bool wasPlaying_{false};                 // Audio-thread-only, no sync needed

// State transitions:
// IDLE -> WAITING: User clicks Sync, BPM validated, syncWaiting_ = true
// WAITING -> ACTIVE: processBlock detects transport start, calculates offset
// ACTIVE -> IDLE: Server BPM changes, or user disables sync
// WAITING -> IDLE: User cancels sync
```

### Pattern 4: BPM Vote via Chat Command

**What:** Send `!vote bpm N` / `!vote bpi N` as chat message using existing SendChatCommand.
**When to use:** When user edits BPM/BPI inline in BeatBar and presses Enter.
```cpp
// From BeatBar inline edit callback:
jamwide::SendChatCommand cmd;
cmd.type = "MSG";
cmd.text = "!vote bpm " + juce::String(newBpm).toStdString();
processorRef.cmd_queue.try_push(std::move(cmd));
```
**Source:** JAMTABA-DAW-SYNC-ANALYSIS.md section 4.1

### Anti-Patterns to Avoid
- **Locking from audio thread:** NEVER acquire clientLock from processBlock. Use atomics for sync state flags. The offset calculation and interval position write happen on audio thread during processBlock -> AudioProc call chain, which is already designed lock-free.
- **Polling transport outside processBlock:** AudioPlayHead::getPosition() is only valid inside processBlock. Do not call from timer or other threads.
- **Persisting sync state:** Per D-21, sync preference does NOT persist across sessions. Always start in IDLE state.
- **Showing sync in standalone:** Per D-07, hide (not disable) the sync button. Detect via `wrapperType == AudioProcessor::wrapperType_Standalone` on the processor.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Transport query | Custom host polling | `getPlayHead()->getPosition()` | JUCE handles VST3/AU differences; returns Optional for missing fields |
| Interval position write for sync | Direct m_interval_pos access hack | Add a public `SetSyncOffset(int)` method on NJClient | m_interval_pos is protected; clean API > friend class or raw member access |
| BPM change notification | Custom polling in run thread | Compare `GetActualBPM()` / `GetBPI()` each iteration vs cached values | Simple, already in the run loop; push event on change |
| Vote command | Custom network message | `ChatMessage_Send("MSG", "!vote bpm N")` via existing SendChatCommand | NINJAM voting IS a chat command; server already handles it |
| Inline text edit overlay | Custom painted text input | `juce::TextEditor` positioned over BeatBar BPM/BPI label | TextEditor handles keyboard, selection, validation out of the box |
| Elapsed time formatting | Manual string formatting | `juce::RelativeTime::inMilliseconds(ms).getDescription()` or simple div/mod | JUCE has RelativeTime but simple mm:ss formatting is trivial |

**Key insight:** The entire sync mechanism exists as algorithm in JamTaba and as API hooks in NJClient. This phase is adaptation and wiring, not invention. The NJClient AudioProc already suppresses encoding when `isPlaying=false` -- passing the actual host transport state is the primary requirement.

## Common Pitfalls

### Pitfall 1: Optional Values from AudioPlayHead
**What goes wrong:** Accessing `getBpm()`, `getPpqPosition()`, etc. without checking `has_value()` causes undefined behavior or crashes.
**Why it happens:** JUCE PositionInfo returns `std::optional<T>` for most fields. Not all hosts provide all data.
**How to avoid:** Always check `has_value()` or use `value_or(default)`. Gracefully degrade when PPQ or bar start is unavailable (disable sync button with "Host does not provide position data").
**Warning signs:** Crash in processBlock only in certain DAWs (e.g., Pro Tools doesn't provide barStart).

### Pitfall 2: Writing m_interval_pos From Wrong Thread
**What goes wrong:** Race condition between AudioProc advancing m_interval_pos and sync code writing it.
**Why it happens:** AudioProc runs inside processBlock (audio thread). If sync offset is written from a different thread, values can be torn.
**How to avoid:** Per JAMTABA-DAW-SYNC-ANALYSIS.md section 7: set offset FROM processBlock, since AudioProc is called from processBlock. The write is naturally serialized.
**Warning signs:** Occasional glitch/click at sync start, interval position jumps randomly.

### Pitfall 3: Standalone getPlayHead() Returns Null
**What goes wrong:** In standalone mode, `getPlayHead()` returns a valid pointer but the PositionInfo has limited data (no PPQ, no bar start).
**Why it happens:** JUCE standalone wrapper provides a basic AudioPlayHead that reports isPlaying=true but no musical position.
**How to avoid:** Hide sync button in standalone (D-07). For the transport-silencing feature (SYNC-01/02), standalone always passes `isPlaying=true` to AudioProc (D-16: auto-play on connect).
**Warning signs:** Sync button visible in standalone, user clicks it, validation fails with confusing error.

### Pitfall 4: BPM Float Comparison
**What goes wrong:** Host BPM is 120.00001, server BPM is 120. Float comparison fails, user can't sync.
**Why it happens:** DAWs report fractional BPM. JamTaba uses integer truncation for comparison.
**How to avoid:** Round both to int: `static_cast<int>(hostBpm) == static_cast<int>(serverBpm)`. This matches JamTaba's behavior exactly.
**Warning signs:** "BPM mismatch" when both appear to be 120.

### Pitfall 5: BeatBar TextEditor Focus Stealing
**What goes wrong:** When inline TextEditor appears for BPM vote, keyboard shortcuts in the DAW stop working.
**Why it happens:** JUCE TextEditor grabs keyboard focus. Some DAWs don't regain focus when TextEditor is destroyed.
**How to avoid:** Call `unfocusAllComponents()` after vote is submitted. Use `TextEditor::onReturnKey` and `onEscapeKey` to dismiss cleanly. Keep TextEditor lifetime short (create on click, destroy on submit/escape).
**Warning signs:** User votes, then can't use spacebar for play/stop in DAW until clicking elsewhere.

### Pitfall 6: NJClient m_interval_pos Access for Sync
**What goes wrong:** Need to SET m_interval_pos for sync offset, but it's a private member.
**Why it happens:** NJClient's API is mostly read-only for interval position. The sync feature needs to write it.
**How to avoid:** Add a public `SetIntervalPosition(int pos)` method to NJClient, or use the existing AudioProc flow where the offset is applied. Alternatively, the sync state can be managed entirely in processBlock by manipulating what gets passed to AudioProc.
**Warning signs:** Using `friend class` or direct member access -- fragile and breaks encapsulation.

### Pitfall 7: BPM Change During Sync Active State
**What goes wrong:** Server BPM changes while sync is active. NINJAM interval length changes, but DAW tempo doesn't, causing drift.
**Why it happens:** DAW tempo is independent of server tempo. Sync alignment is only valid when they match.
**How to avoid:** Per D-05: auto-disable sync when server BPM changes. Push SyncDisabledEvent to UI. Show notification to user.
**Warning signs:** Audio starts drifting after a BPM vote passes while sync is active.

## Code Examples

### Example 1: Reading Transport State in processBlock

```cpp
// Source: JUCE 8.0.12 AudioPlayHead.h (verified from libs/JUCE)
// In JamWideJuceProcessor::processBlock():

bool hostPlaying = true;  // Default: playing (standalone behavior, D-16)

if (auto* playHead = getPlayHead())
{
    if (auto pos = playHead->getPosition())
    {
        hostPlaying = pos->getIsPlaying();

        // Cache host BPM for sync validation (optional value)
        if (auto bpm = pos->getBpm())
            cachedHostBpm_.store(static_cast<float>(*bpm),
                                std::memory_order_relaxed);
    }
}

// Pass actual transport state to NJClient
client->AudioProc(inPtrs, numInputChannels, outPtrs, kTotalOutChannels,
                  numSamples, static_cast<int>(storedSampleRate),
                  false,          // justmonitor
                  hostPlaying);   // isPlaying (was hardcoded true)
```

### Example 2: NJClient AudioProc isPlaying Behavior (Already Implemented)

```cpp
// Source: src/core/njclient.cpp:1944-1958
// When isPlaying=false, NJClient suppresses broadcast encoding:
if (isPlaying && !lc->bcast_active && lc->broadcasting)
{
    // Start broadcasting
    lc->bcast_active = true;
    lc->m_bq.AddBlock(0, cursessionpos, NULL, -1);
}
if (!isPlaying && lc->bcast_active)
{
    // Stop broadcasting (send end-of-stream marker)
    lc->m_bq.AddBlock(0, 0.0, NULL, 0);
    lc->bcast_active = false;
}
// Remote audio continues playing regardless of isPlaying state
```

### Example 3: Standalone Detection

```cpp
// Source: JUCE 8.0.12 AudioProcessor.h (wrapperType is public const member)
// In ConnectionBar or Editor:
bool isStandalone = (processorRef.wrapperType
                     == juce::AudioProcessor::wrapperType_Standalone);

// Hide sync button entirely in standalone (D-07)
syncButton.setVisible(!isStandalone);
```

### Example 4: BPM/BPI Vote via Existing Chat Command

```cpp
// Source: JAMTABA-DAW-SYNC-ANALYSIS.md section 4.1
// Validation: JamTaba uses MIN_BPM=40, MAX_BPM=400, MIN_BPI=2, MAX_BPI=192
int newBpm = textEditor.getText().getIntValue();
if (newBpm >= 40 && newBpm <= 400 && newBpm != currentBpm)
{
    jamwide::SendChatCommand cmd;
    cmd.type = "MSG";
    cmd.text = "!vote bpm " + std::to_string(newBpm);
    processorRef.cmd_queue.try_push(std::move(cmd));
}
```

### Example 5: Session Info Strip Data Sources

```cpp
// All data already available from NJClient:
int intervalCount  = client->GetLoopCount();       // m_loopcnt (njclient.h:164)
unsigned int elapsedMs = client->GetSessionPosition(); // ms since connect
float bpm = client->GetActualBPM();                // m_active_bpm
int bpi = client->GetBPI();                        // m_active_bpi
int pos, len;
client->GetPosition(&pos, &len);                   // interval position/length
int currentBeat = (len > 0) ? (bpi * pos / len) : 0;
```

### Example 6: BPM Change Detection in Run Thread

```cpp
// In NinjamRunThread::run(), after updating uiSnapshot:
float newBpm = static_cast<float>(client->GetActualBPM());
int newBpi = client->GetBPI();
float prevBpm = processor.uiSnapshot.bpm.load(std::memory_order_relaxed);
int prevBpi = processor.uiSnapshot.bpi.load(std::memory_order_relaxed);

processor.uiSnapshot.bpm.store(newBpm, std::memory_order_relaxed);
processor.uiSnapshot.bpi.store(newBpi, std::memory_order_relaxed);

if (prevBpm > 0.0f && static_cast<int>(prevBpm) != static_cast<int>(newBpm))
{
    // Push BPM change event for UI flash + chat notification
    processor.evt_queue.try_push(jamwide::BpmChangedEvent{prevBpm, newBpm});
    // Also push system chat message (D-11)
    ChatMessage msg;
    msg.type = ChatMessageType::System;
    msg.content = "[Server] BPM changed from "
                  + std::to_string(static_cast<int>(prevBpm))
                  + " to " + std::to_string(static_cast<int>(newBpm));
    msg.timestamp = currentTimeString();
    processor.chat_queue.try_push(std::move(msg));
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| VST2 getTimeInfo() | JUCE AudioPlayHead::PositionInfo (Optional-based) | JUCE 7+ | Must check has_value() on all fields except getIsPlaying() |
| JamTaba direct m_interval_pos write | Public setter or audio-thread offset mechanism | N/A (new design) | Need clean API addition to NJClient |
| Raw isPlaying boolean | isPlaying already param in AudioProc | Already exists | Just pass actual value instead of default |
| Chat-only BPM voting | Inline BeatBar editing | Phase 7 feature | Better UX than typing commands |

## Open Questions

1. **NJClient m_interval_pos write access for sync offset**
   - What we know: m_interval_pos is a private member. GetPosition() reads it. The sync algorithm needs to SET it.
   - What's unclear: Best way to expose write access without breaking NJClient encapsulation.
   - Recommendation: Add `void SetIntervalPosition(int pos) { m_interval_pos = pos; }` to NJClient public API. This is safe because it will only be called from processBlock (audio thread), which is the same thread that calls AudioProc (which reads/writes m_interval_pos). No race condition.

2. **BeatBar inline edit: TextEditor overlay vs custom painted**
   - What we know: D-08 says click BPM/BPI to open inline edit. D-09 says direct edit + Enter.
   - What's unclear: Whether to use a JUCE TextEditor overlay or a custom painted approach.
   - Recommendation: Use `juce::TextEditor` positioned over the BPM/BPI label area. It is the simplest approach, handles all keyboard input, and can be styled to match the dark theme. Create on click, destroy on Enter/Escape.

3. **Host BPM availability in standalone**
   - What we know: Standalone AudioPlayHead may or may not report BPM. Per D-07, sync button is hidden in standalone.
   - What's unclear: Whether we need a host BPM display for the sync validation message.
   - Recommendation: Only show host BPM in the sync validation context (when user clicks Sync). In standalone, sync button is hidden so the question is moot.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CTest with assert-based tests (no external framework) |
| Config file | CMakeLists.txt (enable_testing() at line 364) |
| Quick run command | `cmake --build build-juce --target test_flac_codec && ctest --test-dir build-juce -R flac` |
| Full suite command | `cmake --build build-juce && ctest --test-dir build-juce` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| SYNC-01 | Read host transport state | manual-only | Manual: load plugin in DAW, verify transport read | N/A - requires DAW host |
| SYNC-02 | Broadcast only when playing | manual-only | Manual: play/stop DAW, verify audio on server | N/A - requires NINJAM server |
| SYNC-03 | Session position tracked | unit | Verify GetLoopCount/GetSessionPosition exposure | Wave 0 |
| SYNC-04 | Live BPM/BPI at boundary | manual-only | Manual: vote BPM change, verify no reconnect | N/A - requires NINJAM server |
| SYNC-05 | Standalone pseudo-transport | manual-only | Manual: run standalone, verify auto-broadcast | N/A - requires audio device |
| RES-01 | Video feasibility doc | manual-only | Verify file exists at .planning/references/VIDEO-FEASIBILITY.md | N/A |
| RES-02 | OSC evaluation | manual-only | Verify file exists at .planning/references/OSC-EVALUATION.md | N/A |
| RES-03 | MCP assessment | manual-only | Verify file exists at .planning/references/MCP-ASSESSMENT.md | N/A |

### Sampling Rate
- **Per task commit:** Build verification (`cmake --build build-juce`)
- **Per wave merge:** Full build + pluginval validation
- **Phase gate:** Full build green, manual DAW test checklist

### Wave 0 Gaps
- Phase 7 features are primarily audio-thread and UI integration that require a live DAW host or NINJAM server for meaningful testing. Unit tests are limited to verifiable pure functions (PPQ offset calculation, BPM validation logic).
- [ ] `tests/test_sync_offset.cpp` -- PPQ-to-sample offset calculation unit test (pure math, no DAW needed)
- Remaining requirements require manual integration testing with a DAW host.

## Sources

### Primary (HIGH confidence)
- `libs/JUCE/modules/juce_audio_basics/audio_play_head/juce_AudioPlayHead.h` -- PositionInfo API, getIsPlaying(), getBpm(), getPpqPosition(), getPpqPositionOfLastBarStart(), getTimeSignature()
- `libs/JUCE/modules/juce_audio_processors_headless/processors/juce_AudioProcessor.h` -- wrapperType enum, WrapperType::wrapperType_Standalone
- `src/core/njclient.h` -- AudioProc signature (isPlaying param), GetPosition(), GetLoopCount(), GetActualBPM(), GetBPI(), GetSessionPosition()
- `src/core/njclient.cpp:767` -- AudioProc implementation with isPlaying flow-through to process_samples
- `src/core/njclient.cpp:1883-1958` -- process_samples: isPlaying controls bcast_active start/stop
- `src/core/njclient.cpp:1187-1194` -- MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY handler calling updateBPMinfo()
- `.planning/references/JAMTABA-DAW-SYNC-ANALYSIS.md` -- Complete JamTaba sync analysis: 3-state machine, PPQ offset, transport edge detection, BPM validation

### Secondary (MEDIUM confidence)
- `juce/JamWideJuceProcessor.cpp:213` -- Current processBlock AudioProc call (only 4 positional args, defaults isPlaying=true)
- `juce/NinjamRunThread.cpp:344-356` -- Existing uiSnapshot update pattern for BPM/BPI/interval position
- `src/threading/ui_command.h` -- Existing command types (SendChatCommand for vote, pattern for new SyncCommand)
- `src/threading/ui_event.h` -- Existing event types (pattern for BpmChangedEvent, SyncStateEvent)
- `juce/ui/ConnectionBar.h` -- Current button layout (Connect, Browse, Fit, Route, Codec) where Sync button goes
- `juce/ui/BeatBar.h/cpp` -- Current BeatBar with update(bpi, beat, iPos, iLen) interface

### Tertiary (LOW confidence)
- None -- all findings verified against actual source code.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new libraries needed, all JUCE APIs verified against pinned 8.0.12 source
- Architecture: HIGH -- JamTaba analysis provides proven algorithm, NJClient already has all hooks
- Pitfalls: HIGH -- identified from actual code review (AudioPlayHead Optional handling, thread safety, m_interval_pos access)

**Research date:** 2026-04-05
**Valid until:** 2026-05-05 (stable -- JUCE 8.0.12 pinned, NJClient API stable)
