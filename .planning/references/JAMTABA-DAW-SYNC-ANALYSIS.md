# JamTaba DAW Sync & BPM Handling — Technical Reference for JamWide

This document captures JamTaba's DAW host synchronization, BPM/BPI handling, and voting
mechanisms as a technical basis for implementing equivalent functionality in JamWide.

Source: `/Users/cell/dev/JamTaba/` (explored 2026-04-04)

---

## 1. Architecture Overview

JamTaba uses a layered architecture for NINJAM handling:

```
┌──────────────────────────────────────────────────────────┐
│  JamTabaVSTPlugin (VST2 host interface)                  │
│  - getHostBpm(), hostIsPlaying(), getTimeInfo()          │
│  - processReplacing() — detects host transport changes   │
│  - getStartPositionForHostSync() — PPQ-to-sample offset  │
├──────────────────────────────────────────────────────────┤
│  NinjamControllerPlugin (plugin-specific sync layer)     │
│  - waitingForHostSync state flag                         │
│  - stopAndWaitForHostSync() / startSynchronizedWithHost()│
│  - process() override — routes to full or input-only     │
├──────────────────────────────────────────────────────────┤
│  NinjamController (base — interval engine)               │
│  - process() — main audio processing with interval loop  │
│  - handleNewInterval() — scheduled event processing      │
│  - SchedulableEvent system for BPM/BPI changes           │
│  - setBpm() / setBpi() recalculation                     │
├──────────────────────────────────────────────────────────┤
│  Service (network protocol)                              │
│  - ConfigChangeNotifyMessage handling                    │
│  - serverBpmChanged / serverBpiChanged signals           │
│  - voteToChangeBPM() / voteToChangeBPI()                 │
├──────────────────────────────────────────────────────────┤
│  NinjamRoomWindowPlugin (sync UI)                        │
│  - "Sync with Host" button with BPM validation           │
│  - Auto-disable on server BPM change                     │
└──────────────────────────────────────────────────────────┘
```

**JamWide equivalent mapping:**

| JamTaba | JamWide |
|---------|---------|
| JamTabaVSTPlugin | JamWideJuceProcessor (processBlock) |
| NinjamControllerPlugin | Does not exist yet |
| NinjamController | NJClient (AudioProc) |
| Service | NJClient (Run loop, message parsing) |
| NinjamRoomWindowPlugin | ConnectionBar / Editor |

---

## 2. DAW Host Synchronization

### 2.1 State Machine

```
     IDLE (normal playback)
      │
      ├── User clicks "Sync with Host" button
      │   VALIDATION: host BPM must == server BPM (exact integer match)
      │   If mismatch → reject, show "Change [host] BPM to [server BPM]"
      │
      ▼
   WAITING
      │  • waitingForHostSync = true
      │  • reset() called — discards all downloaded intervals
      │  • intervalPosition frozen at 0
      │  • Metronome, MIDI sync, remote tracks: DEACTIVATED
      │  • Loopers: DEACTIVATED
      │  • process() routes to input-only path (no ninjam rendering)
      │  • Audio input STILL encoded and transmitted to server
      │  • UI shows "Press play in [host] to sync with JamTaba!"
      │
      ├── Host transport starts (play button pressed)
      │   Detected by: hostIsPlaying() && !hostWasPlayingInLastAudioCallBack
      │   (edge detection — true only on stopped→playing transition)
      │
      ▼
    ACTIVE (synced playback)
      │  • startSynchronizedWithHost(offset) called
      │  • intervalPosition = calculated offset from host PPQ
      │  • All audio nodes REACTIVATED
      │  • process() routes to full NinjamController::process()
      │  • NINJAM interval aligned to DAW measure boundary
      │
      ├── Server BPM changes → disableHostSync() auto-called
      │   Signal chain: Service::serverBpmChanged → scheduleBpmChangeEvent
      │   → setBpm() → emit currentBpmChanged → disableHostSync()
      │
      ├── User unchecks button → disableHostSync()
      │
      ▼
     IDLE
```

### 2.2 Transport Detection

**File:** `JamTabaVSTPlugin.cpp:177-212`

```cpp
void JamTabaVSTPlugin::processReplacing(float **inputs, float **outputs, VstInt32 sampleFrames)
{
    if (controller->isPlayingInNinjamRoom()) {
        // Query host for tempo, transport state, PPQ position
        timeInfo = getTimeInfo(kVstTransportPlaying | kVstTransportChanged | kVstTempoValid);

        // Edge detection: was stopped, now playing
        if (transportStartDetectedInHost()) {
            auto ninjamController = controller->getNinjamController();
            if (ninjamController->isWaitingForHostSync())
                ninjamController->startSynchronizedWithHost(getStartPositionForHostSync());
        }
    }
    // ... audio processing ...
    hostWasPlayingInLastAudioCallBack = hostIsPlaying();
}
```

**Edge detection function** (`JamTabaPlugin.h:60-63`):
```cpp
inline bool JamTabaPlugin::transportStartDetectedInHost() const
{
    return hostIsPlaying() && !hostWasPlayingInLastAudioCallBack;
}
```

| Previous Callback | Current Callback | transportStartDetected |
|---|---|---|
| stopped | playing | **TRUE** (sync trigger) |
| stopped | stopped | false |
| playing | playing | false |
| playing | stopped | false |

### 2.3 PPQ-to-Sample Offset Calculation

**File:** `JamTabaVSTPlugin.cpp:159-175`

This is the core algorithm that aligns the NINJAM interval with the DAW's measure boundary:

```cpp
qint32 JamTabaVSTPlugin::getStartPositionForHostSync() const
{
    qint32 startPosition = 0;
    double samplesPerBeat = (60.0 * timeInfo->sampleRate) / timeInfo->tempo;

    if (timeInfo->ppqPos > 0) {
        // REAPER path: positive ppqPos from project start
        double cursorPosInMeasure = timeInfo->ppqPos - timeInfo->barStartPos;

        if (cursorPosInMeasure > 0.00000001) {
            // Cursor is mid-measure — calculate delay until next measure start
            double samplesUntilNextMeasure =
                (timeInfo->timeSigNumerator - cursorPosInMeasure) * samplesPerBeat;
            startPosition = -samplesUntilNextMeasure;  // Negative = delay
        }
        // else: cursor at measure boundary, startPosition = 0
    }
    else {
        // CUBASE path: zero or negative ppqPos
        startPosition = timeInfo->ppqPos * samplesPerBeat;
    }

    return startPosition;
}
```

**Variables used from VST timeInfo:**
- `timeInfo->sampleRate` — host sample rate (double)
- `timeInfo->tempo` — host BPM (double)
- `timeInfo->ppqPos` — position in quarter notes from project start (double)
- `timeInfo->barStartPos` — ppqPos of current bar start (double)
- `timeInfo->timeSigNumerator` — beats per measure (e.g. 4 for 4/4 time)

**Calculation examples:**

**Case A: Reaper, cursor at measure start** (ppqPos=8.0, barStartPos=8.0)
```
cursorPosInMeasure = 8.0 - 8.0 = 0.0 (below tolerance)
startPosition = 0
→ Ninjam interval starts immediately aligned
```

**Case B: Reaper, cursor at beat 3 of 4/4** (ppqPos=10.0, barStartPos=8.0, timeSig=4)
```
samplesPerBeat = (60 * 44100) / 120 = 22050
cursorPosInMeasure = 10.0 - 8.0 = 2.0
samplesUntilNextMeasure = (4 - 2.0) * 22050 = 44100
startPosition = -44100 (delay ninjam by 44100 samples)
→ Ninjam interval waits for next measure boundary
```

**Case C: Cubase, project start** (ppqPos=-8.0)
```
samplesPerBeat = (60 * 44100) / 120 = 22050
startPosition = -8.0 * 22050 = -176400
→ Large negative offset, handled by modulo in startSynchronizedWithHost()
```

**Floating-point tolerance:** The `> 0.00000001` check handles a known Reaper bug where
`barStartPos = 4.9999999` when actual is 5.0, which would create a false micro-offset.

### 2.4 Applying the Offset

**File:** `NinjamControllerPlugin.cpp:52-63`

```cpp
void NinjamControllerPlugin::startSynchronizedWithHost(qint32 startPosition)
{
    if (waitingForHostSync) {
        waitingForHostSync = false;
        if (startPosition >= 0)
            intervalPosition = startPosition % samplesInInterval;
        else
            intervalPosition = samplesInInterval - qAbs(startPosition % samplesInInterval);
        activateAudioNodes();
    }
}
```

**Modulo arithmetic:**

Positive offset: `intervalPosition = offset % samplesInInterval`
- Direct position within interval

Negative offset: `intervalPosition = samplesInInterval - abs(offset % samplesInInterval)`
- Wraps backward from end of interval
- Example: offset=-22050, interval=44100 → position=22050 (halfway)

**Edge case:** If offset is exactly divisible by interval length (e.g., -176400 % 44100 = 0),
`intervalPosition = 44100`, which wraps to 0 on next modulo in `process()`.

### 2.5 Audio Routing During States

**File:** `NinjamControllerPlugin.cpp:65-71`

```cpp
void NinjamControllerPlugin::process(const audio::SamplesBuffer &in,
                                     audio::SamplesBuffer &out, int sampleRate)
{
    if (!waitingForHostSync)
        NinjamController::process(in, out, sampleRate);  // Full ninjam
    else
        controller->doAudioProcess(in, out, sampleRate);  // Input-only
}
```

| State | Remote audio | Metronome | Encoding/Tx | Interval advancing |
|---|---|---|---|---|
| IDLE (normal) | Yes | Yes | Yes | Yes |
| WAITING | No | No | Input only | **No** (frozen) |
| ACTIVE (synced) | Yes | Yes | Yes | Yes (from offset) |

### 2.6 Auto-Disable on Server BPM Change

**File:** `NinjamRoomWindowPlugin.cpp:11-24` (constructor signal connection):
```cpp
connect(controller->getNinjamController(), &NinjamController::currentBpmChanged,
        this, &NinjamRoomWindowPlugin::disableHostSync);
```

**File:** `NinjamRoomWindowPlugin.cpp:26-41`:
```cpp
void NinjamRoomWindowPlugin::disableHostSync()
{
    if (ninjamPanel->hostSyncButtonIsChecked()) {
        setHostSyncState(false);
        ninjamPanel->uncheckHostSyncButton();
        // Show: "The BPM has changed! Please stop [Host] and change BPM to [newBpm]!"
    }
}
```

**Signal chain:**
```
Server sends ConfigChangeNotify(newBpm)
  → Service::process() → Service::setBpm() → emit serverBpmChanged
    → NinjamController::scheduleBpmChangeEvent() → queued
      → handleNewInterval() → processScheduledChanges() → setBpm()
        → emit currentBpmChanged
          → NinjamRoomWindowPlugin::disableHostSync()
            → unchecks button, shows message, deactivates sync
```

### 2.7 BPM Validation Before Sync

**File:** `NinjamRoomWindowPlugin.cpp:44-67`:
```cpp
void NinjamRoomWindowPlugin::setHostSyncState(bool syncWithHost)
{
    if (syncWithHost) {
        int ninjamBpm = ninjamController->getCurrentBpm();
        int hostBpm = controller->getHostBpm();

        if (hostBpm == ninjamBpm) {
            ninjamController->stopAndWaitForHostSync();
            // Show: "Press play/start in [Host] to sync with JamTaba!"
        }
        else {
            // Show: "Change [Host] BPM to [ninjamBpm] and try sync again!"
            ninjamPanel->uncheckHostSyncButton();
        }
    }
    else {
        ninjamController->disableHostSync();
    }
}
```

**Rules:**
- Integer comparison: `hostBpm == ninjamBpm` (rounded, not floating-point)
- No tolerance — 120.5 BPM in host vs 120 BPM on server = rejected
- User must manually set DAW tempo to match server

---

## 3. BPM/BPI Handling — Interval Engine

### 3.1 How NJClient (JamWide) Handles BPM Today

**File:** `src/core/njclient.cpp:743-750` (BPM receive):
```cpp
void NJClient::updateBPMinfo(int bpm, int bpi)
{
    m_misc_cs.Enter();
    m_bpm = bpm;
    m_bpi = bpi;
    m_beatinfo_updated = 1;
    m_misc_cs.Leave();
}
```

**File:** `src/core/njclient.cpp:809-850` (BPM application in AudioProc):
```cpp
int offs = 0;
while (len > 0) {
    int x = m_interval_length - m_interval_pos;
    if (!x || m_interval_pos < 0) {
        // INTERVAL BOUNDARY — apply pending BPM/BPI
        m_misc_cs.Enter();
        if (m_beatinfo_updated) {
            double v = (double)m_bpm * (1.0 / 60.0);   // beats/sec
            v = (double)m_bpi / v;                       // seconds/interval
            v *= (double)srate;                          // samples/interval
            m_beatinfo_updated = 0;
            m_interval_length = (int)v;
            m_active_bpm = m_bpm;
            m_active_bpi = m_bpi;
            m_metronome_interval = (int)((double)m_interval_length / (double)m_active_bpi);
        }
        m_misc_cs.Leave();
        on_new_interval();
        m_interval_pos = 0;
        x = m_interval_length;
    }
    process_samples(inbuf, innch, outbuf, outnch, x, srate, offs, ...);
    m_interval_pos += x;
    offs += x;
    len -= x;
}
```

**Key formula:** `interval_length = (bpi / (bpm / 60)) * sample_rate`

Example: 120 BPM, 16 BPI, 48000 Hz:
```
beats_per_sec = 120 / 60 = 2
interval_sec = 16 / 2 = 8
interval_samples = 8 * 48000 = 384000
```

### 3.2 How JamTaba Handles BPM (for comparison)

JamTaba's `NinjamController` wraps the same math in a higher-level layer:

```cpp
long NinjamController::computeTotalSamplesInInterval()
{
    double intervalPeriod = 60000.0 / currentBpm * currentBpi;  // ms
    return (long)(sampleRate * intervalPeriod / 1000.0);
}
```

Equivalent formula — same result, different expression.

**Key difference: JamTaba uses a SchedulableEvent queue**, while NJClient uses a flag.
Both apply changes at interval boundaries only. Functionally identical.

### 3.3 Interval Position Tracking

**NJClient (JamWide):**
- `m_interval_pos` — incremented by sample count in AudioProc loop
- `m_interval_length` — total samples per interval
- Exposed via `GetPosition(&pos, &len)`
- NinjamRunThread reads and stores in `uiSnapshot` atomics
- Beat calculation: `beat = bpi * interval_pos / interval_length`

**NinjamController (JamTaba):**
- `intervalPosition` — incremented with modulo wrap: `(pos + step) % samplesInInterval`
- `samplesInInterval` — total samples per interval
- Beat detection: `currentBeat = intervalPosition / getSamplesPerBeat()`
- Emits `intervalBeatChanged(beat)` signal on beat transitions

---

## 4. BPM/BPI Voting

### 4.1 JamTaba Implementation

**UI:** NinjamPanel has `comboBpm` and `comboBpi` QComboBoxes.

**Vote flow:**
```
NinjamPanel::bpmComboActivated(text)
  → NinjamRoomWindow::setNewBpm(text)
    → validates: newBpm != currentBpm
    → NinjamController::voteBpm(newBpm)
      → Service::voteToChangeBPM(newBpm)
        → sends chat message: "!vote bpm 120"
```

**File:** `Service.cpp:222-232`:
```cpp
void Service::voteToChangeBPM(quint16 newBPM)
{
    QString text = "!vote bpm " + QString::number(newBPM);
    sendMessageToServer(ClientToServerChatMessage::buildPublicMessage(text));
}

void Service::voteToChangeBPI(quint16 newBPI)
{
    QString text = "!vote bpi " + QString::number(newBPI);
    sendMessageToServer(ClientToServerChatMessage::buildPublicMessage(text));
}
```

**Chat command format:**
- `!vote bpm <number>` (e.g., `!vote bpm 120`)
- `!vote bpi <number>` (e.g., `!vote bpi 16`)

**Server response:** If vote passes, server broadcasts `ConfigChangeNotifyMessage` with new values.
All clients apply the change at next interval boundary.

### 4.2 JamWide Current State

JamWide has no dedicated BPM vote UI. Users can manually type `!vote bpm N` in the chat panel.
The server-side response (ConfigChangeNotifyMessage) is handled identically by NJClient.

### 4.3 ServerInfo Validation Ranges (JamTaba)

```cpp
static const int MIN_BPM = 40;
static const int MAX_BPM = 400;
static const int MIN_BPI = 2;
static const int MAX_BPI = 192;
```

---

## 5. Connection Establishment (JamTaba)

### 5.1 Full Auth Flow

```
1. Socket connects to server:port (TCP, LowDelayOption=1)
2. Server → Client: AuthChallengeMessage
   - challenge (bytes), licence (string), protocolVersion, serverCapabilities
3. Client → Server: ClientAuthUserMessage
   - username, SHA1(password + challenge), protocolVersion
4. Server → Client: AuthReplyMessage
   - flag (1=authenticated), newUserName, maxChannels
5. Client → Server: ClientSetChannel
   - channel names and metadata
6. Client creates ServerInfo, emits connectedInServer signal
7. MainController creates NinjamController, calls start(serverInfo)
8. NinjamController schedules initial BPM/BPI events, creates encoders
9. Two intervals must pass (TOTAL_PREPARED_INTERVALS=2) before transmit starts
```

### 5.2 JamWide Equivalent

JamWide uses the same protocol (NJClient handles it internally in `Run()`):
```
1. NJClient::Connect(host, user, pass) creates JNL_Connection
2. Run() loop processes AUTH_CHALLENGE → sends auth reply
3. Run() processes AUTH_REPLY → calls NotifyServerOfChannelChange()
4. Run() processes CONFIG_CHANGE_NOTIFY → updateBPMinfo(bpm, bpi)
5. AudioProc applies BPM at next interval boundary
```

**Difference:** JamTaba's Service emits Qt signals for each protocol event.
JamWide's NJClient handles everything internally — the JamWide JUCE layer polls
status via `cached_status` atomic and `GetPosition()`.

---

## 6. What JamWide Needs to Implement

### 6.1 DAW Sync Feature

**Required components:**

1. **Host transport query** — Read JUCE's `AudioPlayHead` in processBlock:
   ```cpp
   auto playHead = getPlayHead();
   if (playHead) {
       auto posInfo = playHead->getPosition();
       // posInfo->getBpm(), posInfo->getIsPlaying(),
       // posInfo->getPpqPosition(), posInfo->getBarStartPpqPosition(),
       // posInfo->getTimeSignature()
   }
   ```

2. **Transport edge detection** — Track `wasPlaying` vs `isPlaying` across callbacks.
   Trigger sync on stopped→playing transition.

3. **PPQ-to-sample offset calculation** — Adapt JamTaba's algorithm for JUCE's
   `AudioPlayHead::PositionInfo` API (same data, different struct names).

4. **Sync state management** — `waitingForHostSync` flag on processor.
   When waiting: freeze interval position, skip ninjam rendering, pass through input only.
   When triggered: set `m_interval_pos` to calculated offset.

5. **BPM validation** — Compare host BPM (rounded) to server BPM before allowing sync.

6. **Auto-disable** — When server BPM changes, disable sync and notify user.

7. **UI** — Sync button in ConnectionBar or separate control.
   Shows host BPM, sync state, instructions.

### 6.2 BPM Vote UI

**Required components:**

1. **Vote button/controls** — BPM/BPI input fields or spinbox near BeatBar.
2. **Vote command** — Send `!vote bpm N` / `!vote bpi N` via existing chat queue.
3. **Validation** — Reject if same as current, clamp to valid range (40-400 BPM, 2-192 BPI).

### 6.3 Plugin Format Transport API Coverage

JamWide produces plugins in multiple formats across two build targets. All formats except
Standalone provide the transport data needed for DAW sync.

#### JUCE Target (VST3, AU, Standalone)

JUCE's `AudioPlayHead::PositionInfo` provides a unified API across formats.
The underlying format-specific sources:

| Field | VST3 (`Vst::ProcessContext`) | AU (Host Callbacks) | Standalone |
|---|---|---|---|
| BPM | `kTempoValid` → `tempo` | `HostCallback_GetBeatAndTempo` | ✗ (no host) |
| Is Playing | `kPlaying` flag | Transport state callback | ✓ (basic) |
| PPQ Position | `kProjectTimeMusicValid` → `projectTimeMusic` | Beat/tempo callback | ✗ |
| Bar Start | `kBarPositionValid` → `barPositionMusic` | Musical time location callback | ✗ |
| Time Signature | `kTimeSigValid` → `timeSigNumerator/Denominator` | Musical time location callback | ✗ |

**VST3 on Linux** works identically to macOS/Windows — JUCE's AudioPlayHead is cross-platform.
The VST3 host provides `Vst::ProcessContext` the same way on all operating systems.

**JUCE API mapping from JamTaba's VST2:**

| JamTaba VST2 API | JUCE Equivalent |
|---|---|
| `getTimeInfo(flags)` | `getPlayHead()->getPosition()` |
| `timeInfo->tempo` | `posInfo->getBpm()` → `std::optional<double>` |
| `timeInfo->flags & kVstTransportPlaying` | `posInfo->getIsPlaying()` → `bool` (non-optional) |
| `timeInfo->ppqPos` | `posInfo->getPpqPosition()` → `std::optional<double>` |
| `timeInfo->barStartPos` | `posInfo->getPpqPositionOfLastBarStart()` → `std::optional<double>` |
| `timeInfo->timeSigNumerator` | `posInfo->getTimeSignature()->numerator` → `std::optional<TimeSignature>` |
| `timeInfo->sampleRate` | `getSampleRate()` |

**Note:** JUCE returns `std::optional` for most playhead values — must check `.has_value()`
before using. Not all hosts provide all fields. Code must gracefully degrade when values
are missing.

#### CLAP Target (non-JUCE, ImGui plugin)

The CLAP API provides equivalent data via `clap_event_transport_t` in the process events.
Currently JamWide's CLAP plugin (`src/plugin/clap_entry.cpp:256-292`) only reads
`CLAP_TRANSPORT_IS_PLAYING` — all other fields are available but ignored.

| Field | CLAP API (`clap_event_transport_t`) | Guard Flag |
|---|---|---|
| BPM | `transport->tempo` (double) | `CLAP_TRANSPORT_HAS_TEMPO` |
| Is Playing | `transport->flags & CLAP_TRANSPORT_IS_PLAYING` | always available |
| PPQ Position | `transport->song_pos_beats` (clap_beattime) | `CLAP_TRANSPORT_HAS_BEATS_TIMELINE` |
| Bar Start | `transport->bar_start` (clap_beattime) | `CLAP_TRANSPORT_HAS_BEATS_TIMELINE` |
| Time Signature | `transport->tsig_num`, `transport->tsig_denom` | `CLAP_TRANSPORT_HAS_TIME_SIGNATURE` |
| Tempo Ramp | `transport->tempo_inc` (per-sample) | `CLAP_TRANSPORT_HAS_TEMPO` |

CLAP also provides `CLAP_TRANSPORT_IS_RECORDING`, `CLAP_TRANSPORT_IS_LOOP_ACTIVE`,
and `CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL` flags.

#### Implementation Strategy

DAW sync needs **two parallel implementations** sharing the same algorithm:

1. **JUCE target** — Read `getPlayHead()->getPosition()` in `processBlock()`.
   One codebase covers VST3 (macOS, Windows, Linux) and AU.

2. **CLAP target** — Read `clap_event_transport_t` from `process()` events.
   Same offset calculation, different struct access.

**Shared logic** (PPQ offset calculation, state machine, BPM validation) should live in
a common header or in NJClient itself, with thin wrappers in each plugin target that
extract the host transport data and pass it to the shared logic.

**Standalone** has no host — DAW sync button should be hidden or disabled in standalone mode.
Detect via `juce::PluginHostType::getPluginLoadedAs() == AudioProcessor::wrapperType_Standalone`
or simply check if `getPlayHead()` returns nullptr.

---

## 7. Thread Safety Considerations for JamWide

JamTaba uses a QMutex around the entire `process()` call. JamWide's NJClient uses
`m_misc_cs` for BPM updates and `clientLock` for Run() thread operations.

For DAW sync in JamWide:
- `waitingForHostSync` should be `std::atomic<bool>` (written by message thread, read by audio thread)
- Host transport state tracking (`wasPlaying`) is audio-thread-only, no sync needed
- The offset write to `m_interval_pos` must be coordinated — either:
  - Set it from the audio thread (processBlock) when detecting transport start, OR
  - Use an atomic command that the AudioProc loop checks at interval boundary

**Recommended approach:** Set offset from processBlock (audio thread) since that's where
host transport info is available. NJClient::AudioProc is called from processBlock, so the
interval position write is naturally synchronized.
