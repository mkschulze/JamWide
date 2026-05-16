# Phase 20 Broadcast UAT — Operator Procedure

**Plan:** 20-03 Task 4 (`checkpoint:human-verify`, gate="blocking")
**Acceptance gates:** R3 MF4 + R4 M11 + Phase 20 ROADMAP success criteria
**Estimated duration:** 35-45 minutes (5 min × 3 presets × 2 peers + TSan re-run + R4 M11 teardown checks + report write-up)

This procedure validates the end-to-end Phase 20 send-side H.264 broadcast against the populated public NinjamZap server (`video.ninjamzap.com:2049`). It is the user-visible happy-path verification required to close Phase 20 per `feedback_uat_scope_redflags` — Plan 20-00/01/02 unit tests + Plan 20-03 lifecycle test are not a substitute.

## Prerequisites

1. macOS or Windows host, build present at:

   ```text
   build-juce/JamWideJuce_artefacts/Release/Standalone/JamWide.app
   ```

   If missing, run `bash tests/uat/phase-20-broadcast-uat.sh --build`.

2. Internet connection that can reach `video.ninjamzap.com:2049`. Pre-flight check:

   ```sh
   bash tests/uat/phase-20-broadcast-uat.sh --check
   ```

   If the public server is down, you may use a local `ninjamzap-server-docker` instance on `localhost:2049` as a fallback (acceptable per CONTEXT.md `<deferred>`); record the fallback in the report.

3. **At least one of:**
   - A second physical machine running the same JamWide standalone build (preferred — fully reproduces the production peer-receive path)
   - A NinjamZap mobile peer connected to the same server (acceptable — confirms wire-format compatibility from a different codec implementation)
   - **Solo run with `tcpdump` capture is acceptable for the wire-format-only check** (Step 4) but does NOT satisfy the "2 peers, audio-glitch-free" requirement; record as such in the report.

4. `tcpdump` (or Wireshark) for the wire-format-snoop step. macOS ships tcpdump in `/usr/sbin/tcpdump`. On Linux: `sudo apt install tcpdump`.

5. For the TSan dual-scope check: a separate `--tsan` build of the standalone. Build with `./scripts/build.sh --tsan` (see CONTEXT.md / Phase 15.1 D-07 references).

## Acceptance thresholds (R3 MF4 — locked in PLAN.md)

For every preset, after 5 minutes of broadcast:

| Threshold | Source | Value |
|---|---|---|
| `m_rawdata_sendq_high_water_mark` | `GetRawDataSendQueueHighWaterMark()` | < 32 items |
| `contention_count / total_enqueues` | `GetRawDataMutexContentionCount()` / `GetRawDataSendQueueTotalEnqueueCount()` | < 1% |
| `m_encoder_input_drops` | `GetEncoderInputDropCount()` | == 0 |
| `on_new_interval` video block worst-case | `GetOnNewIntervalVideoBlockWorstCaseNs()` (JAMWIDE_BUILD_TESTS only) | ≤ 200,000 ns (200 µs) |

Plus:

- **Subjective audio:** zero audible glitches at any preset.
- **TSan dual-scope** (Medium preset only): zero `WARNING: ThreadSanitizer: data race` reports.
- **Channel registration:** instance A surfaces channel 1 with BOTH name "video" (via `SetLocalChannelInfo`) AND fourCC `H264` (via `SetVideoChannel`) within 1s of connection (per D-18).
- **Wire-format check** (best-effort tcpdump): first chunk of each interval is 24 bytes matching the marker spec `[00 00 00 14][BE u32 swap_count][16B GUID]`.
- **R4 M11 path 1** (normal broadcast-off): END observed on wire within one NINJAM interval (~3-8s).
- **R4 M11 path 2** (Disconnect): END observed on wire BEFORE the connection terminates.
- **R4 M11 path 3** (plugin destruction): best-effort; documented in report but NOT a hard fail.

## Step 1 — Launch instance A, connect, register video channel

1. Open `build-juce/JamWideJuce_artefacts/Release/Standalone/JamWide.app`.
2. Connect to `video.ninjamzap.com:2049` (server field) with a unique username (e.g. `jamwide-uat-${USER}`); leave password blank for anonymous join.
3. Verify NINJAM auth succeeds (status bar shows the room name / user count).
4. **Verify channel-1 registration:** in the application log (Apple Console.app filter `JamWide`, or the in-app debug snapshot), confirm these lines appear within 1 second of connect:

   ```text
   [SetLocalChannelInfo] chidx=1 name=video flags=0x10
   [SetVideoChannel]     chidx=1 fourcc=H264 (0x34363248)
   ```

   (Exact log format may differ — the structural requirement is that BOTH calls fire at connect-up per D-18.)

5. Audio: connect any DAW (e.g. Logic Pro) using the JamWide AU/VST3 plugin to confirm the existing audio path is unaffected. Play any source through the plugin; verify audio reaches the room. **At this point, video is registered but NOT broadcasting** — bandwidth cost on channel 1 is zero (no payload, only the channel metadata).

## Step 2 — Open camera + start broadcast (preset = Low)

1. Right-click the Camera button in the ConnectionBar → `Low (320x240, 10fps)` to select the preset (if not already selected).
2. Left-click the Camera button to open the camera. Wait for the state to transition to `Capturing` (button turns green). The preview window opens.
3. Right-click the Camera button → `Start Broadcast`.
4. The log should print (within 200 ms):

   ```text
   [Phase 20] Broadcast ON (preset=0)
   [Phase 20] encoder opened 320x240 @ 10fps, 100 kbps
   [Phase 20] SPS/PPS published N bytes      (typically 30-50 bytes)
   ```

5. Right-click again → the menu now shows `Stop Broadcast` (label flipped).

## Step 3 — 5-minute Low-preset run + per-30s counter readout

1. Set a 5-minute timer.
2. Every 30 seconds during the run, sample these four counters. The recommended path is `lldb` attached to the running JamWide process (the JAMWIDE_BUILD_TESTS=ON build exposes the accessors as regular C++ methods you can call from an `expr` command):

   ```text
   $ lldb -p <pid-of-JamWide>
   (lldb) expr (uint64_t)((NJClient*)0xADDR)->GetRawDataSendQueueHighWaterMark()
   (lldb) expr (uint64_t)((NJClient*)0xADDR)->GetRawDataMutexContentionCount()
   (lldb) expr (uint64_t)((NJClient*)0xADDR)->GetRawDataSendQueueTotalEnqueueCount()
   (lldb) expr (uint64_t)((NJClient*)0xADDR)->GetEncoderInputDropCount()
   (lldb) expr (uint64_t)((NJClient*)0xADDR)->GetOnNewIntervalVideoBlockWorstCaseNs()
   ```

   - The NJClient pointer is reachable via `processor.client.get()` from any frame that has a JamWideJuceProcessor.
   - If lldb scripting proves brittle, a manual fallback is acceptable: print the counters from within the running app via a debug command (e.g. add a `/uatcounters` chat command for the operator's convenience — that addition is out-of-scope for Plan 20-03 but a future hardening can wire it).

3. Subjective audio check: listen for dropouts on instance A's local monitoring path AND on the connected peer's audio if a second JamWide is running. Note any glitches verbatim in the report.

4. At T=5:00, sample one final time. Run the assert helper for the per-preset gate:

   ```sh
   echo "<high_water> <contention> <total_enq> <drops> <audio_ns>" \
       | bash tests/uat/phase-20-broadcast-uat.sh --assert
   ```

   Record PASS or FAIL with the values in the report.

## Step 4 — 2-peer wire-level reception check

1. Connect instance B (second machine OR NinjamZap mobile) to the same server.
2. Verify instance B sees instance A as a participant (the user count / roster shows the username from Step 1).
3. Verify instance B sees a video capability on channel 1 — in JamWide's debug snapshot, check the remote-user GetUserChannelState for `chidx=1` showing `fourcc=H264` and `flags=0x10`.
4. Phase 20 only validates the SEND path — instance B will NOT render the video (Phase 21 wires receive + decode + Phase 22 wires tile rendering). For Phase 20 the assertion is that the wire is bit-for-bit NinjamZap-compatible — instance B should accumulate the chunks (verify via `GetRawDataDownloadCount` accessor under JAMWIDE_BUILD_TESTS if available, or via Wireshark capture).
5. Optional but recommended: `tcpdump` capture filtered on the JamWide TCP src port + server TCP dst port, and confirm the per-interval first chunk is 24 bytes matching `00 00 00 14 XX XX XX XX <16B GUID>`:

   ```sh
   sudo tcpdump -i en0 -A -nn -s 0 host video.ninjamzap.com and port 2049 -w /tmp/phase20.pcap
   # ... then in Wireshark/scapy, inspect chunks and verify the 24-byte marker shape
   ```

## Step 5 — Repeat Steps 2-4 at preset = Medium

1. Right-click Camera → `Stop Broadcast` to take Low down.
2. Right-click Camera → `Medium (640x480, 15fps)`.
3. Right-click Camera → `Start Broadcast`.
4. Repeat Step 3 (5-min run + per-30s sampling + per-preset assert).
5. Repeat Step 4 (peer reception check).

## Step 6 — Repeat Steps 2-4 at preset = High

1. Right-click Camera → `Stop Broadcast`.
2. Right-click Camera → `High (1280x720, 30fps)`.
3. Right-click Camera → `Start Broadcast`.
4. Repeat Step 3.
5. Repeat Step 4.

## Step 7 — TSan dual-scope re-run (Medium preset only)

1. Quit the regular standalone.
2. Rebuild as `--tsan`:

   ```sh
   ./scripts/build.sh --tsan
   ```

3. Launch the TSan standalone. Re-connect to the server.
4. Right-click Camera → `Medium` → `Start Broadcast`.
5. Run for 5 minutes. Monitor stderr for any:

   ```text
   WARNING: ThreadSanitizer: data race
   ```

6. Pass criterion: zero TSan reports during the run. Record any reports observed (path + summary) in the report with a disposition.

## Step 8 — R4 M11 END-on-broadcast-off teardown verification

After completing Steps 2-7, validate each of the three documented END teardown paths.

### Path 1: Normal broadcast-off

1. With Broadcast active at any preset, start a `tcpdump` capture (or have the second peer ready to observe).
2. Right-click Camera → `Stop Broadcast`.
3. Watch the wire / receive log for an END NAL (`flags=1`, length-zero payload) on channel 1 within one NINJAM interval (~3-8 s).
4. Record observed latency in the report (e.g. "Path 1 END observed at t+4.2 s — PASS").

### Path 2: Disconnect teardown

1. Re-enable Broadcast at preset=Low; let it run 30 seconds.
2. Capture tcpdump as above.
3. Click the Disconnect button in the ConnectionBar.
4. Observe the END NAL BEFORE the TCP FIN. Record in the report.

### Path 3: Plugin destruction

1. Re-connect, re-enable Broadcast at preset=Low; let it run 30 seconds.
2. Capture tcpdump.
3. Quit the JamWide standalone (Cmd+Q on macOS, Alt+F4 on Windows).
4. Observe the wire — the END may or may not arrive depending on m_netcon teardown timing. Per R4 M11 path 3 this is best-effort; do NOT fail the UAT if path 3 misses the END. Record whether observed in the report.

## Step 9 — Capture the final UAT report

Open `tests/uat/phase-20-broadcast-uat-report.md` (template at the bottom of this procedure) and fill in:

- Pre-flight result + server used.
- One row per preset with columns: high-water-mark, contention ratio (% and raw count/denominator), drops, audio-thread budget worst-case (ns), audio-glitch subjective note, peer reception confirmation.
- TSan status (Medium only): zero races OR list of observed races with disposition.
- Wire-format check: marker bytes captured (yes/no, optionally include a sample hex dump).
- Channel registration: SetLocalChannelInfo + SetVideoChannel calls observed at connect-up (yes/no per D-18).
- R4 M11 teardown:
  - Path 1 (normal off): observed latency in seconds.
  - Path 2 (Disconnect): END observed before TCP FIN? Yes/No.
  - Path 3 (plugin destruction): END observed? Yes/No (best-effort).
- Pass/fail verdict + any follow-up items.

The report's mere existence + signed-off pass status is the resume-signal for the Plan 20-03 Task 4 checkpoint. The orchestrator types `approved — all gates pass` followed by the report path to unblock Task 5 (CHANGELOG closeout).

---

## Pass criteria summary (Phase 20 acceptance gate)

| # | Gate | Pass if |
|---|------|---------|
| 1 | Channel registration | Instance A surfaces channel 1 with name "video" AND fourCC H264 within 1s of connect |
| 2 | Low preset 5-min  | high_water<32, contention<1%, drops==0, audio_ns≤200000 |
| 3 | Medium preset 5-min | same gates as Low |
| 4 | High preset 5-min | same gates as Low |
| 5 | TSan dual-scope (Medium) | zero ThreadSanitizer data race reports |
| 6 | Subjective audio | no audible glitches at any preset |
| 7 | Wire-format check (best-effort) | first interval chunk is 24 bytes matching marker spec |
| 8 | R4 M11 path 1 | END observed on wire within one interval (~3-8s) |
| 9 | R4 M11 path 2 | END observed on wire before TCP FIN on Disconnect |
| 10 | R4 M11 path 3 | best-effort; not a hard fail |

ALL of gates 1-9 must hold for Phase 20 to close. Gate 10 is documentation-only.

---

## Report template

Copy the block below into `tests/uat/phase-20-broadcast-uat-report.md` and fill in:

```markdown
# Phase 20 Broadcast UAT Report

**Date:** YYYY-MM-DD
**Operator:** <name>
**Build:** <git rev>
**Server used:** video.ninjamzap.com:2049  /  local fallback
**Peer setup:** <second JamWide on machine X / NinjamZap mobile / solo+tcpdump>

## Channel registration (D-18)
- [ ] SetLocalChannelInfo(1, "video", flags=0x10) observed at connect-up
- [ ] SetVideoChannel(1, H264) observed at connect-up

## Per-preset results

| Preset | high_water | contention (count/total = %) | drops | audio_ns | glitches? | gate |
|--------|-----------:|-----------------------------:|------:|---------:|-----------|------|
| Low    |            |                              |       |          |           |      |
| Medium |            |                              |       |          |           |      |
| High   |            |                              |       |          |           |      |

## TSan dual-scope (Medium, --tsan build)
- Result: zero races / <N> races (list below)

## Wire-format check (best-effort)
- [ ] First-chunk-per-interval is 24 bytes matching `00 00 00 14 .. .. .. .. <16B GUID>` shape

## R4 M11 teardown verification
- Path 1 (normal broadcast-off) — END latency: <seconds> — PASS / FAIL
- Path 2 (Disconnect)            — END before TCP FIN: Yes / No — PASS / FAIL
- Path 3 (plugin destruction)    — END observed: Yes / No — best-effort

## Verdict

- Overall pass status: PASS / FAIL
- Follow-up items (if any):

## Notes

(operator notes, observations, deviation comments)
```
