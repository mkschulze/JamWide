---
quick_id: 260515-0pc
slug: investigate-jamtaba-video-implementation
status: research-addendum
date: 2026-05-15
supersedes_sections:
  - "RESEARCH.md §1 (codec parameters / wire format)"
  - "CONTEXT.md decision #4 (bit-for-bit JamTaba wire compat)"
  - "deferred-items.md item F.1 (NINJAM channel wiring)"
sources:
  - /Users/cell/dev/ninjamzap-server/docs/VIDEO_SUPPORT.md (server doc)
  - /Users/cell/dev/ninjamzap-server/ninjam/server/usercon.{h,cpp} (server impl)
  - /Users/cell/dev/ninjamzap-core/core/ninjamclient/libninjamcore/njclient.{h,cpp} (client impl)
  - /Users/cell/dev/ninjamzap-core/tests/video-sync/ (26 test scenarios + harness)
  - https://github.com/jacinside/ninjamzap-server/blob/main/docs/VIDEO_SUPPORT.md
---

# Quick Task 260515-0pc — Research Addendum: NinjamZap Video Sync (supersedes JamTaba JTBv)

## Why this addendum exists

User feedback 2026-05-15: the JamWide video implementation should **not** mimic JamTaba's wire format. Instead, it should mimic **NinjamZap** — a fork of NINJAM with a working multi-stream video sync that is already proven on iOS/Android (NinjamZap mobile app). The NinjamZap server fork lives at `/Users/cell/dev/ninjamzap-server`; the client core lives at `/Users/cell/dev/ninjamzap-core`.

Substantively NinjamZap is *better* than JamTaba for our purposes:

1. **Multi-codec wire format** — uses standard fourCCs `H264` / `VP8 ` / `MJPG` (not JamTaba's proprietary `JTBv`). The server distinguishes video from audio purely by fourCC; no special namespace.
2. **Server-side multithreading** — each private room runs in its own pthread, with a two-pass loop that processes audio before video. JamTaba assumes single-threaded vanilla NINJAM and chokes audio when video congests the relay.
3. **Server-side congestion control** — drops video frames per-subscriber when their send queue exceeds `VideoCongestionThreshold` (default 50%), so audio never gets behind a slow video subscriber.
4. **GUID-pairing audio-video sync** — the receiver matches each video interval's marker GUID against the *audio* channel's downstream GUID (`ds`/`prev_ds`) for the same sender. This solves the "video-1-interval-early" problem inherently, without timing heuristics. JamTaba has no equivalent.
5. **Codec-agnostic transport** — the C++ core only routes opaque bytes via a `RawDataSendBegin/Write/IsEnd` API. Encoder and decoder live in the application layer (Swift `VTCompressionSession` on iOS, MediaCodec on Android, JUCE/openh264 in our case). Easier to swap codecs and add hardware acceleration per platform.

This is a *cleaner* architecture than what we were planning to port.

## Updated locked decisions (apply to deferred-items milestone)

The four CONTEXT.md decisions remain mostly valid; only #4 changes substantively:

1. **Replace VDO.Ninja entirely** — UNCHANGED.
2. **ffmpeg + JUCE CameraDevice** — UNCHANGED. (NinjamZap proves the C++ core stays codec-agnostic; encoder lives in JUCE-app layer; ffmpeg/openh264 stays as the encoder/decoder backend.)
3. **Standalone AND plugin parity** — UNCHANGED.
4. **Wire-format target** — **CHANGED**: bit-for-bit **NinjamZap-compatible**, not JamTaba-compatible. Use fourCC `H264` (= `MAKE_NJ_FOURCC('H','2','6','4')` = 0x34363248), the 24-byte interval marker, the 4-byte BE length-prefix per frame, and the GUID-pairing receiver decision tree. JamTaba clients will NOT see our video and we will not see theirs (different fourCCs); cross-client interop is sacrificed for sync correctness + multi-codec future-proofing + ninjamzap-server compatibility.

**NEW decision (locked) — Server compatibility target:**

5. **Target NinjamZap-server fork as the reference server.** JamWide users hosting their own server should run the ninjamzap-server fork (or a compatible re-implementation). Vanilla NINJAM servers will *technically work* (server is opaque relay) but will lack: per-room threading, two-pass audio-priority processing, and per-subscriber video-frame congestion drop. Without those, audio quality degrades when video bandwidth spikes. Document this in the milestone-shipping README.

## Wire format spec (locked, bit-for-bit NinjamZap-compatible)

### Channel registration

When the JamWide client wants to broadcast video:

```cpp
// Set the local channel info with the video flag (0x10).
// chidx is whatever local channel slot we pick for video; ninjamzap convention is chidx=1.
client.SetLocalChannelInfo(
    /*chidx*/        1,
    /*name*/         "video",
    /*setsrcch*/     false, /*srcch*/  0,
    /*setbitrate*/   false, /*bitrate*/ 0,
    /*setbcast*/     true,  /*bcast*/  true,
    /*setoutch*/     false, /*outch*/  0,
    /*setflags*/     true,  /*flags*/  0x10           // ← video flag
);
client.SetVideoChannel(
    /*chidx*/  1,
    /*fourcc*/ MAKE_NJ_FOURCC('H','2','6','4')        // 0x34363248 little-endian on disk
);
client.NotifyServerOfChannelChange();
```

**Source:** `ninjamzap-core/tests/video-sync/harness/TestClient.cpp:82-90,138-146` and `njclient.h:233`.

The `0x10` flag bit tells the server (and other clients) that this channel is a video channel. The server uses it together with the fourCC for subscription routing and for the congestion check. Other clients use it to know they should subscribe via the video decoder rather than the audio decoder.

### 24-byte interval marker (sent first chunk inside every video BEGIN)

Every video interval starts with a fixed 24-byte chunk that lets the receiver pair the video interval against the matching audio interval:

```
Offset  Bytes  Field             Format
─────────────────────────────────────────────────────────────────
 0..3   4      length_prefix     BE u32, value = 20 (REQUIRED — see "frame framing" below)
 4..7   4      sender_swap       BE u32, value = sender's m_sync_interval_cnt
 8..23  16     audio_ch0_guid    raw bytes of sender's audio channel-0 m_curwritefile.guid
                                 for THIS interval (the same GUID the sender just emitted in
                                 their audio UPLOAD_INTERVAL_BEGIN this swap)
─────────────────────────────────────────────────────────────────
TOTAL: 24 bytes
```

The 4-byte length prefix is **mandatory** — it is part of the per-frame framing convention NinjamZap uses inside video channels, NOT a separate concern. It tells the receiver's reassembler "the next 20 bytes are one logical chunk" so chunks split across multiple `UPLOAD_INTERVAL_WRITE` messages (e.g. large H.264 IDR frames) can be reassembled correctly.

If the sender has not yet captured any audio for this interval (e.g. broadcaster set to muted, or audio-pause), the marker still emits with zero-filled `audio_ch0_guid` (16 bytes of 0). The receiver treats an all-zero `audio_guid` as "no GUID hint, fall back to NONE-match path".

**Source:** `njclient.cpp:3047-3071` (sender emit) and `njclient.cpp:1530-1547` (receiver parse).

### SPS/PPS placement (H.264-specific)

Right after the 24-byte marker, the sender emits its cached H.264 SPS/PPS as the **second chunk** of the video interval:

```cpp
m_video_spspps_cs.Enter();
if (m_video_spspps.GetSize() > 0)
    RawDataSendWrite(m_video_guid, m_video_spspps.Get(), m_video_spspps.GetSize(), false);
m_video_spspps_cs.Leave();
```

This means the receiver can join mid-stream and still decode: the very first interval they receive will carry SPS/PPS as chunk #2 (after the marker chunk). If the encoder regenerates SPS/PPS mid-stream (e.g. resolution change), the application calls `SetVideoSPSPPS(newData, len)` and the next interval picks them up automatically.

**Source:** `njclient.cpp:3072-3076` (sender emit), `njclient.cpp:2177-2187` (`SetVideoSPSPPS` API).

### Per-frame chunk framing inside the video channel

Each H.264 NAL unit (or batched NAL group) the application queues via `QueueVideoFrame(data, len)` should be wrapped in a 4-byte BE length prefix:

```cpp
// From TestClient.cpp:120-127
void TestClient::sendVideoFrame(const void *data, int len) {
  std::vector<uint8_t> chunk(4 + len);
  writeBE32(chunk.data(), (uint32_t)len);   // [4B BE total length][payload]
  std::memcpy(chunk.data() + 4, data, len);
  client_->QueueVideoFrame(chunk.data(), (int)chunk.size());
}
```

This is the same length-prefix convention as the 24-byte marker. The receiver uses it to identify logical frame boundaries even when the underlying NINJAM `UPLOAD_INTERVAL_WRITE` chunks split frames mid-stream (which they will, for any H.264 IDR larger than the per-write size cap).

`QueueVideoFrame` is a no-op if `m_video_active` is false or `m_video_interval_open` is false — that is, frames captured before the next `on_new_interval` BEGIN are dropped. The capture pipeline doesn't need to coordinate with the audio thread; it just calls `QueueVideoFrame` whenever the encoder emits a frame.

**Source:** `njclient.cpp:2116-2123` (`QueueVideoFrame` impl), `tests/video-sync/harness/TestClient.cpp:120-127` (caller-side wrap).

### Sender state machine (driven by `on_new_interval`, runs on audio thread)

```
on_new_interval() {                                     // called once per audio interval swap
  ...                                                   // (existing audio interval handling)

  if (m_video_active) {
    if (m_video_interval_open)
      RawDataSendWrite(m_video_guid, NULL, 0, true);    // END previous video interval
    RawDataSendBegin(m_video_guid, m_video_fourcc, m_video_chidx, 0);   // BEGIN new
    m_video_interval_open = true;

    // 1) emit 24-byte sync marker
    unsigned char marker[24] = { 0,0,0,20, /* swap BE */, /* audio_ch0_guid */ };
    RawDataSendWrite(m_video_guid, marker, 24, false);

    // 2) emit cached SPS/PPS as second chunk
    if (m_video_spspps.GetSize() > 0)
      RawDataSendWrite(m_video_guid, m_video_spspps.Get(), m_video_spspps.GetSize(), false);

    // ... QueueVideoFrame() chunks arrive between intervals from the capture/encode thread ...
  } else if (m_video_interval_open) {
    RawDataSendWrite(m_video_guid, NULL, 0, true);      // END at boundary if just deactivated
    m_video_interval_open = false;
  }
}
```

**Critical correctness invariant:** the END for interval N **MUST** be sent BEFORE the BEGIN for interval N+1, both in the same audio-thread iteration. This is what gives the server the ability to relay video at the exact same interval boundary as audio, which is what makes the GUID-pairing work on the receiver side.

**Source:** `njclient.cpp:3041-3082`.

## Receiver decision tree (4-stage pipeline + GUID matching)

NinjamZap maintains four buffer slots per video stream (`accumulating → next → pending → playing`), one set per remote user × video chidx pair:

```
                                      on_new_interval()
                                            │
                                            ▼
       ┌──── STAGE 1: PROMOTE ─────────────────────────────────────────┐
       │ if pending.active && pending.frameCount >= 1                 │
       │     playing.copyFrom(pending);  pending.reset();             │
       │     frame_idx = 0;                                           │
       └──────────────────────────────────────────────────────────────┘
                                            │
                                            ▼
                                  next.active && next.frameCount >= 1?
                                            │
                              ┌─── no ─────┴─── yes ───┐
                              │                         │
                              ▼                         ▼
              (handled in receiver-WRITE              audio_has_data?
               path; nothing to do here)                │
                                            ┌─── no ───┴─── yes ──────────────────┐
                                            │                                       │
                                            ▼                                       ▼
                                       HOLD (no audio)                    GUID match?
                                       hold_count = 0;                    (next.audio_guid vs senderDs->guid AND prev_ds_guid)
                                       empty_count = 0;                          │
                                                                ┌─ matches ds ─┴─ matches prev ─┴─ no match ─┐
                                                                │                  │                            │
                                                                ▼                  ▼                            ▼
                                                       PLAY (DS match):    PLAY (PREV match):       hold_count++
                                                       defer 1 swap        play immediately         if hold_count >= 4:
                                                       next → pending      next → playing             DROP-RESYNC (drop next,
                                                       (gets promoted at   (audio is already           clear synced flag)
                                                        SWAP+1)             heading out speaker)
                                                                                                       else: HOLD (stay in next)
```

**Why DS match defers and PREV match plays immediately:**

- **DS match** (`next.audio_guid == senderDs->guid`): the sender's audio data for this same interval is currently in the local downstream queue but not yet audible (it'll be audible during the next SWAP+1 to SWAP+2 window because of the audio output buffer). So we hold the video in `pending` for one more swap and promote it then. Result: video appears at exactly the same wall-clock moment as the matching audio.
- **PREV match** (`next.audio_guid == prev_ds_guid`): the sender's audio for this interval was the *previous* downstream and is currently being played out by the speaker right now. So we play the video immediately. Result: same alignment.
- **NONE match** (rare; usually means a stale or legacy non-marker payload): play immediately as best-effort.

**`kHoldCapDrop = 4`**: we tolerate 4 consecutive mismatches before dropping the queued video and resyncing. Without this cap, a chronic GUID mismatch (e.g. one sender's audio dies but their video keeps going) would queue indefinitely. The trade-off: ONE missed video interval keeps every subsequent interval aligned, vs. drift that compounds.

**Source:** `njclient.cpp:3084-3219` for the full state machine; `njclient.h:386-414` for the buffer struct definitions.

### Why 4 buffers (not 3)

JamTaba ports often try to do this with 3 slots (`accumulating → next → playing`). The `pending` slot exists *specifically* to add the 1-swap defer for DS-match cases. Without it you get the well-known "video plays one interval before audio" bug — repro-tested in `ninjamzap-core/tests/video-sync/scenarios/02_video_one_interval_early.cpp`. Don't try to skip the pending slot.

## Server-side architecture (ninjamzap-server fork)

Vanilla NINJAM works (opaque relay). For acceptable production behavior under video load, you want the ninjamzap-server fork's three additions:

### 1. Per-room threading (`PrivateGroupMode`)

```
Main thread:    accept connections → lobby auth → migration handoff
                                                     │
              ┌──────────────────────────────────────┘
              ▼            ▼            ▼
        Room "jazz"  Room "rock"  Room "blues"
        thread       thread       thread
        (independent)
```

Each `User_Group` runs `ThreadRun` on its own pthread. Lobby stays on main thread. Users hand off via a thread-safe migration queue. Without this, all rooms share the main thread and HD video relay starves audio of CPU.

**Source:** `ninjamzap-server/docs/VIDEO_SUPPORT.md` "Thread-Per-Group Architecture" section; `ninjam/server/usercon.{h,cpp}`.

### 2. Two-pass processing (audio before video, per loop iteration)

Each `User_Group::Run()` has two passes:

- **Pass 1**: `User_Connection::Run(audio_only=true)`. Video messages are stashed in `m_deferred_video_msg`; only audio messages are processed and relayed.
- **Pass 2**: After all users finish Pass 1, iterate users with `m_deferred_video_msg != NULL` and process those video messages.

This ensures audio relay always happens before video relay within each loop iteration, keeping audio latency consistent even under video load.

**Source:** ibid., "Audio Priority (Two-Pass Processing)".

### 3. Per-subscriber video congestion control

Before relaying a video frame to a specific subscriber, the server checks the subscriber's per-connection send queue depth:

```cpp
if (is_video_fourcc(t->fourcc) &&
    u->m_netcon.GetSendQueueCount() > NET_CON_MAX_MESSAGES * g_config_video_congestion_pct / 100)
{
    // drop this video frame for this subscriber
    // audio continues flowing normally
}
```

Each subscriber is evaluated independently — a slow client only loses its own video. Audio is never affected by this check. When the congested subscriber's queue drains, video delivery resumes automatically.

**Default `VideoCongestionThreshold`: 50%**. Tunable per server.

**Source:** ibid., "Video Frame Dropping (Congestion Control)".

### Server config

```
AllowVideoChannels yes               # gate; default off
VideoTransferTimeout 30              # seconds; raise for long BPI/low BPM rooms
VideoCongestionThreshold 50          # percent; lower = drops video earlier to protect audio
SendBufferKB 256                     # per-connection TCP send buffer
RecvBufferKB 128                     # per-connection TCP recv buffer
PrivateGroupMode 20                  # max private rooms (each gets own thread)
```

Per-bandwidth sizing:

| Quality | Resolution | Bitrate    | Per interval (16 BPI @ 120 BPM) |
|---------|------------|------------|----------------------------------|
| Low     | 320×240    | ~100 kbps  | ~100 KB                         |
| Medium  | 640×480    | ~300 kbps  | ~300 KB                         |
| HD      | 1280×720   | ~800 kbps  | ~800 KB                         |

**Source:** ibid., "Configuration Reference" + "Sizing Guide".

## What this changes vs. our existing PLAN.md and SUMMARY.md

The spike (commits 43f7c4f, d4403e5, f861ca1, fc5b345 — all on `quick/260515-0pc-jamtaba-video-port`) **remains valid** as evidence that ffmpeg + openh264 + JUCE CameraDevice compose end-to-end. The compose proof is independent of wire format. **Do not redo the spike.**

What changes for the *milestone* (deferred items B–I):

| Original (JamTaba) | Updated (NinjamZap) | Why |
|---|---|---|
| fourCC `JTBv` | fourCC `H264` (or `VP8 ` / `MJPG` later) | Standard fourCCs; server distinguishes via existing UPLOAD_INTERVAL_BEGIN field; future codec swaps are wire-format-clean |
| 4-byte chunk framing only | 4-byte BE length prefix per frame + 24-byte sync marker | Marker enables GUID-pair audio-video sync; length prefix enables multi-write reassembly |
| Single-slot interval (next → playing) | 4-stage pipeline (accumulating → next → pending → playing) | `pending` adds the 1-swap defer that fixes "video-1-interval-early" |
| No GUID matching | DS / PREV / no-match decision tree with hold_count cap | Solves audio-video sync at the protocol level, not via timing heuristics |
| `start_decode` intercepts at receive path | `RawDataCallback` + `VideoFrameReadyCallback` API | Clean separation between transport (codec-agnostic core) and codec layer (application) |
| Add `JTBv` to JamWide channel constants | Use existing fourcc plumbing, just add `H264` constant | Less invasive |
| Vanilla NINJAM server tolerates fine | NinjamZap server fork strongly recommended for production | Per-room threading + two-pass + congestion drop are non-trivial benefits under video load |
| Single sender, single receiver | Multi-stream per-user buffers (one VideoRecvState per username×chidx) | Required for multi-peer rooms |

## Updated milestone scope (replaces RESEARCH §8 items B–I)

The original RESEARCH §8 items B–I assumed a JamTaba port. With the NinjamZap design, the items shift:

- **B. Vendor LGPL ffmpeg+openh264 across platforms** — UNCHANGED.
- **C. JUCE CameraDevice integration (capture module, frame format conversion)** — UNCHANGED. Just feeds bytes to encoder.
- **D. H.264 encoder using openh264 (with VTCompressionSession on iOS-style hardware accel later)** — UNCHANGED. Note: NinjamZap iOS uses Apple's `VTCompressionSession`. JamWide should plan for `VideoToolbox` on macOS/arm64, `MediaFoundation` on Windows, `V4L2_M2M` on Linux as future hardware-accel paths after the openh264 baseline lands.
- **E. H.264 decoder + display widget** — UNCHANGED architecture, but the decoder feeds into a per-user 4-stage receive pipeline.
- **F. NINJAM transport wiring** — **CHANGED**: implement the NinjamZap `RawDataSendBegin/Write/IsEnd` API on the send side, plus the `RawDataCallback` + receive-side 4-stage pipeline + GUID-matching decision tree. Reference implementation in `ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp` lines 2047-2123 (send), 1300-1550 (receive WRITE handling), 3041-3219 (interval state machine).
- **G. Plugin entitlements + codesigning** — UNCHANGED.
- **H. Remove VDO.Ninja stack** — UNCHANGED.
- **I. Per-DAW UAT** — UNCHANGED.
- **NEW: J. Server-side adaptation** — Decide whether JamWide ships a recommended server build (e.g. fork ninjamzap-server, integrate with our Docker image) or just documents that users should run ninjamzap-server. Either way, document the `AllowVideoChannels yes` + `PrivateGroupMode N` requirement for production video.

## UI rendering model [LOCKED-2026-05-15]

**Native rendering only.** The locked rendering model is `juce::Component` grid in the main view + `juce::DocumentWindow` popouts per user. No browser companion in v1.

Once H.264 frames are decoded into `juce::Image` objects (Item E), JamWide owns them and can render in any combination:

- **Grid view:** A `juce::Component` containing one tile per remote user, laid out flow-style or fixed-grid based on user preference. Lives inside the plugin/standalone main window. Lightweight — `juce::Image` repaints on the message thread driven by the audio thread's `VideoFrameReadyCallback` via `MessageManager::callAsync`.
- **Per-user popouts:** Each remote user can be detached into a top-level `juce::DocumentWindow` (resizable, draggable to any monitor). The popout owns its own renderer; the grid view shows a placeholder ("popped out") tile in its slot. This preserves the multi-monitor strength of the existing VDO.Ninja browser companion without the WebSocket dependency.
- **Both simultaneously:** A user can keep the grid view open AND pop out specific users. The single decoded frame stream feeds both renderers (cheap — `juce::Image` is reference-counted internally).

The grid + popout pattern is essentially what JamTaba's CLAP plugin already does (inline preview + togglable video window). NinjamZap mobile uses a similar mode-switching pattern. Both prove the UX works without a browser.

A literal browser-companion path is **rejected for v1**. Implementing it would require a parallel transport: decoded `juce::Image` → MJPEG re-encode → local WebSocket server → browser MJPEG/WebRTC client. That doubles the milestone work AND reintroduces the entire `companion/` HTML/TS maintenance surface that locked decision #1 set out to remove. If user demand emerges later, evaluate as a separate post-v1 milestone.

## Audio codec scope [CLARIFIED-2026-05-15]

**Audio codec is OUT OF SCOPE for the native-video work.**

User asked whether we should update JamWide's audio codec to OPUS based on a recollection that ninjamzap-server supports OPUS. Verification:

```
$ grep -rni "opus" /Users/cell/dev/ninjamzap-server/ /Users/cell/dev/ninjamzap-core/ \
    | grep -v ".git\|build/\|README"
WDL/metadata.h:688:  if (!stricmp(filetype, ".ogg") || !stricmp(filetype, ".opus"))
```

The single hit is just an audio-file-extension recognizer for ID3-style metadata reads — NOT a NINJAM transport codec. **Both ninjamzap-server and ninjamzap-core support only `MAKE_NJ_FOURCC('O','G','G','v')` on the wire** (verified at `ninjamzap-server/ninjam/njclient.cpp:37,1491,1667` and `ninjamzap-core/njclient.cpp:51,2204`).

**JamWide's own roadmap independently tracks Opus** as `Phase 16 "Opus Codec Integration"` in the v1.2 "Security & Quality" milestone (see `.planning/ROADMAP.md`). That phase is the right place for the Opus work; the native-video milestone should NOT entangle it.

For the native-video milestone: stay on OGGv. JamWide↔JamWide and JamWide↔NinjamZap audio interop is preserved.

If/when JamWide eventually adds Opus (Phase 16): NinjamZap users would NOT receive JamWide's Opus audio (different fourCC, would be ignored). Per-session codec negotiation (e.g., advertise capabilities, downgrade to OGGv if any peer is non-Opus) is a Phase 16 design problem, not a video-milestone problem.

## Cross-platform reality check [NEW-2026-05-15]

User asked whether the GUID-matching algorithm works on Windows and Linux. Answer: **the algorithm is fully portable** but **the supporting capture infrastructure is not.**

| Concern | macOS | Windows | Linux |
|---|---|---|---|
| GUID-matching state machine (Items F.0-F.3) | ✓ pure C++ memcmp + integer compare | ✓ same | ✓ same |
| `juce::CameraDevice` for capture | ✓ `juce_CameraDevice_mac.h` (AVFoundation backend) | ✓ `juce_CameraDevice_windows.h` (MediaFoundation backend) | **✗ no `juce_CameraDevice_linux.h` exists** — confirmed by `ls libs/juce/modules/juce_video/native/` |
| openh264 (LGPL/BSD H.264 encoder) | ✓ Cisco prebuilt up to v2.1.1 only on Mac; source-build required for v2.2.0+ (forfeits MPEG-LA royalty payment) | ✓ Cisco prebuilts ongoing | ✓ Cisco prebuilts ongoing |
| libavcodec H.264 decoder | ✓ vendored | ✓ vendored | ✓ system or vendored |
| Plugin sandboxing constraints | macOS Hardened Runtime + camera entitlement | Windows AppContainer (rare in DAWs) | typically no plugin sandbox |
| Hardware codec backends (post-v1) | VideoToolbox (already a transitive dep of vendored libavcodec) | MediaFoundation Transform | V4L2_M2M / VAAPI |
| Receive-only path (decode + display) | ✓ | ✓ | ✓ |
| Capture+broadcast path | ✓ | ✓ | **deferred to Item K** |

**Linux camera gap is the only hard blocker.** Linux receive-only in v1 is a clean cut: Linux users participate as audio-broadcasters + video-viewers, which still gives them a real participation experience (and matches what most DAWs on Linux give users today — cameras are a desktop-app primitive, not a plugin one). Item K (added to deferred-items.md as a post-v1 single-plan phase) closes the gap by adding a direct V4L2 capture wrapper behind a `JamWideCameraDevice` abstraction.

## Open questions for the milestone planner (updates RESEARCH §9)

These are NEW questions surfaced by the NinjamZap study:

- **Q8.** Should JamWide write its own multithreaded NINJAM server fork, or contribute upstream to ninjamzap-server, or run ninjamzap-server unmodified? The user-provided context says they have access to the ninjamzap-server source — deciding the maintenance model now affects integration plan.
- **Q9.** What channel index should JamWide use for video? NinjamZap convention is `chidx=1`. JamWide may have local-channel mapping conflicts; need to audit `MAX_LOCAL_CHANNELS` and existing channel-index assignments before committing.
- **Q10.** How do we represent the video flag `0x10` in JamWide's channel info? Need to confirm the existing `SetLocalChannelInfo` flags parameter is wired through and that bit 0x10 is unclaimed by other JamWide-specific channel uses.
- **Q11.** The receive-side 4-stage pipeline allocates `WDL_HeapBuf` per stream per slot. With N peers × 4 slots × variable frame size, that's potentially significant memory under HD video. Need a budget number for the milestone (estimate: 800 KB/interval × 4 slots × 6 peers = 19 MB worst case for HD). Compare against existing per-peer audio decode buffer budget; flag if it bumps total memory >2× current footprint.
- **Q12.** NinjamZap's `kHoldCapDrop = 4` was tuned for typical jam-room conditions. Is it the right value for JamWide's typical use cases (DAW-hosted plugin contexts, where the host audio output buffer may be larger or differently shaped than iOS/Android)?
- **Q13.** Hardware H.264 encode/decode on macOS-arm64 via VideoToolbox (per spike Risk #3) is an alternative to openh264-from-source. Should the milestone *plan* for VideoToolbox on arm64 from day one, or land openh264 first and add VideoToolbox as a follow-up phase?

## Recommendation for the milestone

1. **Carry the spike's vendored ffmpeg as-is** to prove the codec layer works on dev arch. Re-run `scripts/build_ffmpeg_lgpl.sh` first to clear the libX11 spurious dep (per spike Risk #4).
2. **Skip JTBv entirely** — implement NinjamZap's `H264` fourCC + 24-byte marker + 4-byte length prefix from the start.
3. **Implement the receive-side 4-stage pipeline before the send-side**. Reason: the pipeline can be tested against synthetic interval data without a working capture path; the send path can't be tested without the receive path also working. Start where verifiable evidence is cheapest.
4. **Use the 26 NinjamZap test scenarios as the JamWide UAT spec.** They cover: late join, sparse video, sender-leaves, multi-receiver fan-out, BPM/BPI changes mid-session, network drops, etc. Port the harness (`tests/video-sync/harness/TestClient.{h,cpp}`) into JamWide's tests/ tree as the starting framework — it's already known to drive the C++ core correctly.
5. **Plan for ninjamzap-server fork as the reference server**. JamWide's CI integration tests should spin up a ninjamzap-server Docker container (the upstream `Dockerfile` is at `/Users/cell/dev/ninjamzap-server/Dockerfile` — copy it into JamWide's CI fixtures).

## Confidence

- **NinjamZap wire format** (24B marker, 4B length prefix, fourCC values): **HIGH** — read source verbatim with file:line citations.
- **Receiver decision tree** (4-stage pipeline, DS/PREV match, kHoldCapDrop=4): **HIGH** — read source verbatim, cross-checked with diagrams user provided and with `02_video_one_interval_early.cpp` test scenario.
- **Server-side multithreading impact**: **HIGH** — `VIDEO_SUPPORT.md` is comprehensive; matches `usercon.{h,cpp}` structure I sampled.
- **Hardware codec swap feasibility (VideoToolbox/etc.)**: **MEDIUM** — well-documented platform APIs but no spike yet.
- **Memory budget for receive pipeline at HD scale**: **LOW** — needs measurement during milestone, not estimable from source alone.

## Metadata

**Addendum date:** 2026-05-15
**Authoritative until:** ninjamzap-core or ninjamzap-server changes the wire format. Suggest milestone re-pulls both repos and re-verifies before any release.
