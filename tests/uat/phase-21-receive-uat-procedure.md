# Phase 21 Receive UAT — Operator Procedure

**Plan:** 21-03 Task 5 (`checkpoint:human-verify`, gate="blocking")
**Acceptance gates:** Phase 21 ROADMAP success criteria 1–4 + codex review Cluster 1 / 9 / 10 LOW
**Estimated duration:** 35–50 minutes (5 min × Cell 1 lip-sync + 12 s × Cell 2 join-late + 8 s × Cell 3 freeze/resume + 5 min × Cell 4 three-peer + report write-up)

This procedure validates the end-to-end Phase 21 receive-side H.264 decode pipeline against the populated public NinjamZap server (`video.ninjamzap.com:2049`). It is the user-visible happy-path verification required to close Phase 21 per `feedback_uat_scope_redflags` — Plan 21-01/02/03 unit + e2e tests are not a substitute.

---

## Phase Closure Policy (codex review Cluster 9)

Phase 21 closure REQUIRES one of the following two states:

**State A — Full PASS:**

- All 4 cells (Cell 1, 2, 3, 4) marked PASS.
- Report status line reads: `Phase 21 closed (Cells 1-4 PASS)`.

**State B — Deferred-risk close:**

- Any BLOCKED cell becomes a deferred-risk record. Each BLOCKED cell:
  1. Documented in this report with the symptom + suspected root cause + observed counter readings.
  2. Linked from `.planning/STATE.md` as a tracked follow-up risk for Phase 24 (beta validation).
  3. Closed in the report with status `BLOCKED — see STATE.md → Phase 24 BETA-XX`.
- Report status line reads: `Phase 21 closed with deferred-risk Cells X, Y tracked in STATE.md → Phase 24`.

**NOT acceptable — "BLOCKED-but-closed" without deferred-risk record:**

The prior plan iteration allowed Cells 2 and 3 to be marked BLOCKED while still closing the phase. Per codex review, this is forbidden. BLOCKED cells are PHASE SUCCESS CRITERIA per the ROADMAP and MUST become tracked follow-up risks (not silent omissions) if they cannot be exercised.

---

## Prerequisites

1. macOS or Windows host, build present at:

   ```text
   build-juce/JamWideJuce_artefacts/Release/Standalone/JamWide.app
   ```

   If missing, run `./scripts/build.sh JamWideJuce_Standalone`.

2. Internet connection that can reach `video.ninjamzap.com:2049`. The community-operated public instance is the validated v1.3 beta reference per SRV-01.

3. **For Cells 1 + 2 + 3:** ONE other peer broadcasting video into the same room. Either:
   - A second physical machine running the same JamWide standalone build (preferred — fully reproduces the production peer-receive path)
   - A NinjamZap mobile peer connected to the same server (acceptable — confirms wire-format compatibility from a different encoder implementation)
   - **Coordinate the second peer out-of-band (Slack / SMS) with a beta-tester collaborator before starting the UAT.**

4. **For Cell 4:** TWO other peers broadcasting simultaneously into the same room.

5. Receive-surface dimensions: **v1.3 fixed at 320×240** per codex Cluster 10 LOW. This is NOT "first-seen peer resolution" — the SwsContext recreate path inside `Openh264Decoder` (per D-07 + Pitfall 7) handles peers broadcasting at arbitrary resolutions by rescaling to the 320×240 sink surface.

## Acceptance thresholds + ROADMAP success-criteria mapping

| Cell | ROADMAP success criterion | Acceptance gate |
|------|---------------------------|-----------------|
| 1 | "video appears at same wall-clock moment as audio (no 'video one interval early')" | 5 min lip-sync clean; `drop_resync_count == 0`; `hold_count == 0` at T+5:00; **`getRunVideoReceiveBlockMaxNanosForTest() < 1,000,000` (1 ms) at steady-state** (codex Cluster 1) |
| 2 | "mid-session joiner sees video within ≤2 NINJAM intervals" | First frame visible within 2 intervals (~12 s at 4-bar BPI/100 BPM) of clicking Connect |
| 3 | "peer audio stops while video continues → freeze gracefully → kHoldCapDrop=4 resync" | Last frame freezes (not black); `hold_count` reaches 2 (overlay `'syncing…'` appears); `drop_resync_count` increments by exactly 1 after the 4th hold; post-unmute, tile rejoins within 1 interval |
| 4 | "3+ peers simultaneously, per-peer isolation" | 5 min completes; each peer's tile decodes independently; no decoder cross-contamination (each peer's counters move independently) |

---

## Counter-readout instructions (all cells)

JamWide exposes the following diagnostics via the `JAMWIDE_BUILD_TESTS`-only debug surface. To read counters during a UAT cell:

1. Build with tests enabled:

   ```sh
   ./scripts/build.sh --tests
   ```

   The `JAMWIDE_BUILD_TESTS=1` define enables `GetVideoStreamForTest(username, chidx)` + `getRunVideoReceiveBlockMaxNanosForTest()` accessors on `NJClient`.

2. Read counters via lldb on the running process:

   ```sh
   lldb --batch --one-line 'attach --waitfor JamWide' \
        --one-line 'expr ((NJClient*)processor->client.get())->getRunVideoReceiveBlockMaxNanosForTest()' \
        --one-line 'detach'
   ```

   (Substitute the appropriate symbol path — `processor` may be a static or a frame-scoped pointer; locate via `frame variable`.)

3. Alternative: stream the values via `juce::Logger::writeToLog` in a debug build by adding a temporary `Timer` that polls the counters every second and writes to the console.

Per-peer counters (on `PeerVideoSink` via `JamWideRemoteFrameDistributor::findSink(username, chidx)`):

- `hold_count` (atomic int) — overlay control per D-17
- `decode_error_count` (atomic int) — diagnostic per D-18
- `drop_resync_count` (atomic int) — diagnostic for kHoldCapDrop=4 forced resyncs
- `synced` (atomic bool) — first GUID-pair match observed
- `first_frame_seen` (atomic bool) — first decoded frame visible per D-19

Audio-thread-receive-block timing (per codex Cluster 1, JAMWIDE_BUILD_TESTS only):

- `NJClient::getRunVideoReceiveBlockMaxNanosForTest()` — max duration in ns of any `runVideoReceiveBlock_` invocation since reset
- `NJClient::resetRunVideoReceiveBlockTimingForTest()` — reset the max-nanos counter (call at the start of each cell)

---

## Cell 1: Wall-clock audio-video alignment (5+ min, no 'video one interval early')

**ROADMAP success criterion 1.**

### Setup

1. Launch JamWide standalone instance B (you, the UAT operator).
2. Coordinate with the peer (instance A) to connect to `video.ninjamzap.com:2049` room `jamwide-uat-21`.
3. Instance A: open camera + start broadcast at Medium preset (640×480, 15fps recommended for clear lip-sync).
4. Instance A: from a microphone, say "one one one one one" at exactly 1 syllable per second (use a metronome at 60 BPM).
5. Instance B: connect to the same room. Wait for instance A's tile to appear in the (Phase 22) grid.

   **Note:** In Phase 21 there is no Phase 22 UI grid; we rely on the diagnostic readout below. Phase 22 will land the tile UI in v1.4+. For Phase 21 UAT, verify the decoded frames are produced by reading per-peer counters AND by examining the decoded image bytes via a one-shot debug capture (described below).

6. Instance B: reset the counter before starting the 5-min watch:

   ```sh
   lldb --batch -p $(pgrep -f JamWide) \
        --one-line 'expr ((NJClient*)processor->client.get())->resetRunVideoReceiveBlockTimingForTest()' \
        --one-line 'detach'
   ```

### Steps

1. Start a 5:00 timer on a separate stopwatch / phone.
2. Watch the counters every 30 s for 5 min. Record at T+1:00, T+2:00, T+3:00, T+4:00, T+5:00:
   - `getRunVideoReceiveBlockMaxNanosForTest()` (should stay < 1,000,000 = 1 ms)
   - Peer A's `hold_count` (should stay 0)
   - Peer A's `drop_resync_count` (should stay 0)
   - Peer A's `decode_error_count` (should stay 0 or near 0)
3. At T+5:00 capture one decoded frame by reading peer A's `PeerVideoSink::image_front` bytes (via `pollOneFrameForTest`-style helper added behind the JAMWIDE_BUILD_TESTS flag for UAT, or by capturing screen of the eventual Phase 22 tile).
4. **The key assertion:** at every sampling point, the video and audio are aligned — the `one` syllable sound and the visible mouth movement happen at the same wall-clock moment. If video appears EARLIER than the audio by ≥1 NINJAM interval (~3-8 s), this is the regression Plan 21-01 / 21-02 are specifically designed to prevent (D-15 GUID-pair decision tree + Plan 21-02 marker frame parsing).

### Acceptance

- `drop_resync_count == 0` at T+5:00.
- `hold_count == 0` at T+5:00.
- `getRunVideoReceiveBlockMaxNanosForTest()` final reading **< 1,000,000 ns (1 ms)** per swap under steady-state (codex Cluster 1 gate).
- Zero perceived audio-video drift over the 5 min.
- `decode_error_count` final reading == 0 OR explainable from one transient network glitch documented in the report.

### Failure modes

- Video appears EARLIER than lip movement → check D-15 GUID-pair logic; was the marker frame parsed correctly?
- Video lags AUDIO by ≥1 NINJAM interval → check the DS-match `next → pending` defer in `runVideoReceiveBlock_`.
- `getRunVideoReceiveBlockMaxNanosForTest()` ≥ 1 ms → codex Cluster 1 envelope BREACH; investigate the audio-thread receive block; check for an inadvertent allocation / mutex re-entry.

---

## Cell 2: Mid-session join sees video within ≤2 NINJAM intervals

**ROADMAP success criterion 2.**

### Setup

1. Coordinate with peer A: peer A starts broadcasting Low (320×240, 10fps) at T-30 seconds, BEFORE you (instance B) connect.
2. Confirm peer A is broadcasting steady (their local UI shows green camera + broadcast active).

### Steps

1. Start a stopwatch.
2. At T+0 click Connect on instance B → `video.ninjamzap.com:2049` room `jamwide-uat-21`.
3. Wait. Note the timestamp at which:
   - Peer A's row appears in the local roster (audio path online)
   - Peer A's `PeerVideoSink::first_frame_seen` flips to true (read via debug accessor)

### Acceptance

- `first_frame_seen` flips to true within **2 NINJAM intervals (~12 s at 4-bar BPI / 100 BPM)** of clicking Connect.
- After flip, subsequent frames continue to arrive (counter readings stable).

### Failure modes

- `first_frame_seen` never flips → check Plan 21-03 Task 2 two-phase startup flow; did `completeVideoDecoderStartup_` fire on the first BEGIN from peer A?
- Flips but takes > 2 intervals → check whether peer A's IDR is sent every interval per Phase 20 D-14 (otherwise the receiver can't decode without the SPS+PPS+IDR triplet); check whether the receiver's GUID-pair decision tree is dropping the first few intervals as no-match HOLDs.

---

## Cell 3: Peer audio stops → freeze → kHoldCapDrop=4 → resume cleanly

**ROADMAP success criterion 3.**

### Setup

1. Coordinate with peer A: peer A is connected + broadcasting steady.
2. Instance B: connected, decoding peer A's video successfully (verify `first_frame_seen == true`).

### Steps

1. Reset peer A's `drop_resync_count` and `hold_count` to 0 (or just record their starting values before the test).
2. Peer A: mute audio channel 1 (the channel carrying the marker GUIDs). Keep video broadcast active.
3. On instance B, watch the counters every 1 second:
   - `hold_count` should increment by 1 each NINJAM swap interval as the GUID-pair decision finds no match
   - At `hold_count == 2`, the (Phase 22) tile should overlay `'syncing…'` (in Phase 21 we read this from the counter; in v1.4 Phase 22 will render the overlay)
   - At `hold_count == 4` (kHoldCapDrop), the receiver force-resyncs:
     - `drop_resync_count` increments by exactly 1
     - The `next` slot is reset
     - The pipeline awaits the next valid BEGIN
4. Peer A: unmute audio channel 1.
5. Instance B: within ≤1 NINJAM interval, peer A's tile resumes decoding cleanly.

### Acceptance

- During the 4 holds, the last decoded frame stays visible (not black, not crash).
- `'syncing…'` overlay appears at `hold_count ≥ 2` (D-17 threshold).
- `drop_resync_count` increments by EXACTLY 1 after the 4th hold.
- Post-unmute, tile rejoins within 1 NINJAM interval.

### Failure modes

- Tile goes black → wrong; the last frame should freeze. Check Plan 21-03 Task 1 `PeerVideoSink` double-buffer — `image_front` should NOT be cleared on hold.
- Crash → likely a UAF on the `PeerVideoSink*` or `Openh264Decoder*` path. Check codex Cluster 3 four-step shutdown protocol — did `setSink(nullptr)` race against an in-flight `scaleAndSwapImage_`?
- `drop_resync_count` never increments → `kHoldCapDrop = 4` constant may be wrong; check `runVideoReceiveBlock_` body.

---

## Cell 4: 3+ peers simultaneously, per-peer isolation, ≥5 min

**ROADMAP success criterion 4.**

### Setup

1. Coordinate with TWO other peers (instance A + instance C) to broadcast video simultaneously in room `jamwide-uat-21-3way`.
2. Choose different presets per peer to stress the SwsContext-recreate path:
   - Peer A: Low (320×240, 10fps)
   - Peer C: Medium (640×480, 15fps)
   - You (instance B): no video; receive only

### Steps

1. Connect instance B to the room.
2. Verify both peer A and peer C have `first_frame_seen == true` within 2 NINJAM intervals (Cell 2 acceptance, but for 2 peers).
3. Watch all peers' counters every 30 s for 5 min. Record at T+1:00, T+2:00, T+3:00, T+4:00, T+5:00:
   - Peer A's `decode_error_count`, `hold_count`, `drop_resync_count`
   - Peer C's `decode_error_count`, `hold_count`, `drop_resync_count`
   - Audio-thread block: `getRunVideoReceiveBlockMaxNanosForTest()` (should stay < 1 ms despite 2 peers)

### Acceptance

- 5 min completes without crash.
- Per-peer counters are independent: a brief network glitch on peer A's stream causes peer A's `hold_count` to bump but does NOT affect peer C's counters (and vice versa).
- `getRunVideoReceiveBlockMaxNanosForTest()` final reading **< 1,000,000 ns (1 ms)** despite 2 peers under contention (codex Cluster 1 gate at scale).
- No decoder cross-contamination: peer A's decode errors don't trigger peer C's `decode_error_count` increments.

### Failure modes

- One peer's tile goes black when another peer's stream glitches → per-peer isolation broken; check that each `VideoRecvState` has its own `decoder` shared_ptr and own `SpscRing<int, 4>` slot index queue (Plan 21-02 codex Cluster 2 Option A).
- `getRunVideoReceiveBlockMaxNanosForTest()` ≥ 1 ms under 2 peers → codex Cluster 1 envelope breach at scale; check whether the audio-thread receive block's per-peer iteration cost is reasonable.
- Counter readings move in lockstep across peers → cross-contamination; check the per-`VideoRecvState` ownership of `decoderSlots` / `decoderSlotIndexQ` / `decoderProducerSeq`.

---

## Report write-up

Open `tests/uat/phase-21-receive-uat-report.md` (template). For each Cell:

1. Record PASS / FAIL / BLOCKED + diagnostic counter readings + brief observation notes.
2. If BLOCKED: write a deferred-risk record (symptom + suspected root cause + observed counter readings) AND add a corresponding entry to `.planning/STATE.md` under a "Phase 21 deferred risks → Phase 24 follow-up" section.
3. If FAIL: write a root-cause hypothesis + proposed remediation scope. Do NOT close the phase.
4. Set the report status line to one of:
   - `Phase 21 closed (Cells 1-4 PASS)` — all 4 cells PASS
   - `Phase 21 closed with deferred-risk Cells X, Y tracked in STATE.md → Phase 24` — some BLOCKED, all FAILs resolved
   - (Do NOT write a status line if any cell remains FAIL — the phase is NOT closed.)

Commit the report file separately from the procedure file so the procedure stays clean for the next phase that reuses the pattern.

---

## Appendix: Counter readout examples (lldb on macOS)

```sh
# Attach to the running JamWide process.
lldb -p $(pgrep -f "JamWide.app/Contents/MacOS/JamWide")

# Inside lldb:
(lldb) expr ((NJClient*)processor->client.get())->getRunVideoReceiveBlockMaxNanosForTest()
(uint64_t) $0 = 145632   # 145.6 µs — well under 1 ms gate.

(lldb) expr ((NJClient*)processor->client.get())->GetVideoStreamForTest("alice", 1)
(NJClient::VideoRecvState *) $1 = 0x000060000003b290

(lldb) expr $1->hold_count
(int) $2 = 0

(lldb) expr $1->drop_resync_count
(int) $3 = 0
```

Sink-side counters require the `JamWideRemoteFrameDistributor::findSink` accessor — added in Plan 21-03 Task 1. The accessor is callable on the message thread but the `decode_error_count` / `first_frame_seen` atomics are read lock-free from any thread.
