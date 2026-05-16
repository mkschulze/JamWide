---
date: 2026-05-17
phase: 20
plan: 20-03
status: BLOCKING — Plan 20-03 UAT Task 4 FAIL
severity: HIGH
discovered_by: Phase 20 broadcast UAT against video.ninjamzap.com:2049
diagnostician: Claude Code (multi-turn UAT session)
---

# Phase 20 — Anonymous NINJAM channel cap (2 channels per peer) blocks v1.3 public-server broadcast

## TL;DR

JamWide's broadcast UAT against the public `video.ninjamzap.com:2049` ninjamzap-server failed: the JamWide-Standalone peer appeared as a participant tile but the tile stayed **black** while the same operator's NinjamZap mobile peer (parallel connection, same room) rendered live. The wire-level diagnosis identified **two layered issues**:

1. **Surface symptom** — JamWide registers chidx=1 *twice*: first as audio "Ch2" (`SetLocalChannelInfo` in the audio loop, fourCC OGGv), then as video (`SetLocalChannelInfo` + `SetVideoChannel`, fourCC H264). Both encoders then upload distinct GUIDs on chidx=1, with audio winning the codec contest server-side.
2. **Underlying constraint** — Standard NINJAM (and ninjamzap-server) caps **anonymous** users at **2 channels per peer** (`MaxChannels 32 2` in the server config; `g_config_maxch_anon = 2` hardcoded default). JamWide's 5-channel registration (Ch1, Ch2, Ch3, Ch4, Instatalk) is silently truncated to the first 2 records, so Ch3/Ch4/Instatalk never reach other peers regardless of the video collision.

Both layers point to the same fix-direction: **on public + anonymous servers, JamWide must operate in a 2-channel "audio chidx=0 + video chidx=1" layout**, matching NinjamZap-mobile's design exactly. Multi-bus mixing + Instatalk are deferred to authenticated / self-hosted / private-room servers where the user limit is 32.

The discovery is a Phase 20 BLOCKING UAT failure (Plan 20-03 Task 4 → FAIL), AND a v1.3-scope finding that affects Phase 14.2 (Instatalk on public servers), the milestone `docs/SERVER.md` (SRV-01) wording, and the WIRE-04 / BETA-01/05/06 acceptance criteria.

---

## Background

Phase 20 (H.264 Encoder & Send Pipeline) shipped 4 plans:

- **20-00** substrate revision (NinjamZap-literal `WDL_PtrList` + `WDL_Mutex` RawData send queue)
- **20-01** openh264 encoder via libavcodec
- **20-02** NJClient send-side video state machine (whole-block `m_video_cs`, atomic two-uint64_t GUID seqlock)
- **20-03** processor wiring + UAT harness

All 4 plans landed on `quick/260515-0pc-jamtaba-video-port`, 6/6 Phase 20 unit tests pass on the main tree. Plan 20-03 Task 4 (`autonomous: false`, the 5-minute populated-server UAT) is the only remaining acceptance gate per `feedback_uat_scope_redflags` — happy-path broadcast against a real populated room.

The operator ran the UAT on `video.ninjamzap.com:2049` (the public ninjamzap-server instance recommended by SRV-01 / BETA-01 / BETA-05). The web viewer at `https://www.ninjamzap.com/live/video.ninjamzap.com/2049` confirmed both peers appeared in the room, NinjamZap-mobile rendered live, JamWide-Standalone stayed black.

## Reproduction

1. Build JamWide standalone: `bash tests/uat/phase-20-broadcast-uat.sh --build`
2. Launch: `open build-juce/JamWideJuce_artefacts/Release/Standalone/JamWide.app`
3. Connect to `video.ninjamzap.com:2049` with any username, blank password (anonymous)
4. Left-click Camera button → wait for `Capturing` state
5. Right-click Camera → `Start Broadcast` (Low preset)
6. Open `https://www.ninjamzap.com/live/video.ninjamzap.com/2049` in a browser → observe black tile next to your username
7. Connect NinjamZap mobile peer to the same room with camera on → mobile tile renders live, JamWide tile stays black

## Wire evidence

Capture: `sudo tcpdump -i any -nn -X -c 80 'src host 192.168.178.26 and dst host 137.66.24.229 and dst port 2049' > /tmp/jamwide-wire.txt`

Decoded `CLIENT_UPLOAD_INTERVAL_BEGIN` messages (NINJAM message type `0x83`, format = `[type:1] [length:4 LE] [GUID:16] [estsize:4 LE] [fourCC:4] [chidx:1]`):

| BEGIN | GUID head | fourCC ASCII | fourCC hex | chidx | Source |
|---|---|---|---|---|---|
| #1 | `7b6590…` | **OGGv** | `4f 47 47 76` | `0x00` | NJClient audio encoder for Local_Channel `Ch1` |
| #2 | `09e967…` | **OGGv** | `4f 47 47 76` | `0x01` | NJClient audio encoder for Local_Channel `Ch2` |
| #3 | `475967…` | **H264** | `48 32 36 34` | `0x01` | NJClient::QueueVideoFrame → RawDataSendBegin/Write from Plan 20-02 |

Raw hex of BEGIN #3 (the colliding video one):

```
0x0030:  f340 afbc 83 19 00 00 00 47 59 67 d4 41 d8 7a    .@.....GYg.A.z
0x0040:  92 27 33 f3 c5 aa f2 f4 d4 00 00 00 00 48 32 36   .'3..........H26
0x0050:  34 01                                            4.
```

Byte-by-byte parse:

- `83` — NINJAM CLIENT_UPLOAD_INTERVAL_BEGIN
- `19 00 00 00` — payload length 25 bytes (LE)
- `47 59 67 d4 41 d8 7a 92 27 33 f3 c5 aa f2 f4 d4` — 16-byte GUID
- `00 00 00 00` — estsize 0 (LE)
- `48 32 36 34` — fourCC "H264"
- `01` — **chidx = 1** ← **colliding with BEGIN #2 (audio Ch2 on chidx=1)**

## Server-side root cause (ninjamzap-server source)

Confirmed against `/Users/cell/dev/ninjamzap-server`:

### Anonymous channel cap = 2

`ninjam/server/ninjamsrv.cpp:636-637` — hardcoded defaults:

```cpp
g_config_maxch_anon=2;
g_config_maxch_user=32;
```

`ninjam/server/ninjamsrv.cpp:362-367` — `MaxChannels` config parser:

```cpp
else if (!stricmp(t,"MaxChannels"))
{
  g_config_maxch_user=lp->gettoken_int(1);
  g_config_maxch_anon=lp->gettoken_int(lp->getnumtokens()>2?2:1);
}
```

`configs/default-2050.cfg:4` and `configs/default.cfg:4` — what the public server is almost certainly running:

```
MaxChannels 32 2
```

Two-arg form ⇒ `maxch_user=32`, `maxch_anon=2`. **Public anonymous users are capped at 2 channels per peer.**

### Server-side enforcement is silent truncation

`ninjam/server/ninjamsrv.cpp:240-241` — anon login path:

```cpp
privs=(g_config_allow_anonchat?PRIV_CHATSEND:0) | …;
max_channels=g_config_maxch_anon;   // = 2 for our case
```

`ninjam/server/usercon.cpp:602` — channel-info-update processing:

```cpp
while ((offs=chi.parse_get_rec(offs,&chnp,&v,&p,&f))>0
       && whichch < MAX_USER_CHANNELS
       && whichch < m_max_channels)
```

The `&& whichch < m_max_channels` clause silently breaks the parse loop after the first 2 records. JamWide's 5-channel batched `mpb_client_set_channel_info` message gets truncated to 2 records server-side, and the server never tells JamWide it dropped the rest.

### Server tells client the cap in the auth reply

`ninjam/mpb.h:64-74`:

```cpp
mpb_server_auth_reply() : flag(0), errmsg(0), maxchan(32) { }
…
char maxchan;
```

`ninjam/njclient.cpp:1070`:

```cpp
m_max_localch=ar.maxchan;
```

So **the client knows the server's per-peer limit at auth time** — it's stored as `NJClient::m_max_localch`. JamWide just isn't consulting it before laying out channels. NinjamZap mobile presumably respects this value implicitly because it only registers 2 channels regardless.

## Why JamWide's tile shows up at all (despite truncation)

The first 2 records of JamWide's channel-info-update reach the server:

- Record #1: chidx=0, name="Ch1", flags=0x00, fourCC OGGv (from audio loop)
- Record #2: chidx=1, name="Ch2", flags=0x00, fourCC OGGv (from audio loop)

…but then Plan 20-03's `SetLocalChannelInfo(1, "video", flags=0x10)` call **redefines** the record at chidx=1 inside NJClient's local-channel state *before* `NotifyServerOfChannelChange` builds the outbound packet. So actually the SECOND record ends up being:

- Record #2: chidx=1, name="video", flags=0x10, fourCC=H264 (after SetVideoChannel)

This is why the web viewer shows JamWide *with a video tile* — chidx=1 IS registered as flags=0x10 / H264 on the server side. But meanwhile NJClient's audio encoder for the original Ch2 Local_Channel (channel_idx=1) is *still alive* and emits OGGv BEGIN/WRITE messages on chidx=1 every NINJAM interval. The web decoder for chidx=1 sees both codecs on the same channel and falls back to interpreting bytes as audio.

The 3rd-5th channels (Ch3, Ch4, Instatalk) are silently dropped at line `usercon.cpp:602`. They do not appear in the web viewer's room state at all.

## Impact

| Surface | Status | Notes |
|---|---|---|
| Plan 20-03 Task 4 — 5-min populated UAT | **FAIL** | Video tile stays black on public anon server; web viewer can't decode interleaved OGGv + H264 on chidx=1 |
| Plan 20-03 Tasks 1, 2, 3, 5 — code/test landing | PASS | All unit tests pass, code merged, harness shipped — the *integration* with NinjamRunThread channel layout is what's wrong |
| Phase 14.2 Instatalk on public anon servers | **silently degraded** | Instatalk chidx=4 is dropped by the server's 2-channel cap, so other peers never hear PTT voice; never noticed before because Phase 14.2 UAT used a self-host / authenticated path |
| JamWide multi-bus mixing (Ch2/Ch3/Ch4) on public anon servers | **silently degraded** | Other peers only receive Ch1 audio; users can speak/play on Ch2-Ch4 locally but it never crosses the wire |
| SRV-01 `docs/SERVER.md` wording | **needs revision** | Must call out that public anon mode = 2 channels; full layout requires authenticated or self-hosted |
| BETA-01 / BETA-05 / BETA-06 acceptance | **affected** | Two macOS / two Windows / cross-platform UAT criteria all currently say "broadcast 5 minutes" but don't say *which channel layout* — they will all fail the same way until remediation lands |
| NinjamZap mobile interop | **unaffected** | Mobile uses 2-channel layout natively, already compatible |

## Related memories / prior decisions

- `feedback_uat_scope_redflags` — "Never let an executor's 'verify only X, skip Y' UAT pass as routine when Y is a user-visible happy-path (broadcast, login, save/load)." This finding is exactly that flag firing again: Plans 20-00/01/02 + 20-03 Tasks 1-3 unit-tested cleanly, but the happy-path UAT against the actual deployment surface (public anon server) revealed an integration constraint no unit test can see.
- `feedback_legacy_invariant_audit` — "When a refactor introduces a shadow representation of state, grep ALL writes to the indexing field." Plan 20-03's `SetLocalChannelInfo(1, "video", …)` introduced a shadow registration on chidx=1; should have audited all *existing* writers to chidx=1 first (which would have caught the Ch2-on-chidx=1 conflict).
- `feedback_phase19_review_layers` — Phase 19's cross-AI plan-review-then-code-review pattern catches lifetime bugs. Phase 20 followed the same pattern but didn't include a "real-server channel-layout review" pass. Adding such a pass to Phase 20-redux (and any phase that touches NINJAM channel registration) would close this gap.

## Recommended fix

**Update 2026-05-17** — upstream NinjamZap docs confirm the canonical client-side fix: `https://github.com/jacinside/ninjamzap-core/blob/main/docs/VIDEO_SYNC.md` §7 says *"Channel is flagged video-only (`flags & 0x10`) so the client's audio pipeline skips it in both `on_new_interval()` and `process_samples()`."* JamWide currently sets the server-visible `flags=0x10` correctly but does not implement the client-side skip in its own audio pipeline, so the audio encoder keeps producing OGGv data on chidx=1 in parallel with video.

The **smallest possible fix** is implementing the canonical skip (~10 lines, two `if (lc->flags & 0x10) continue;` guards in NJClient). This alone should resolve the surface-level chidx=1 collision. The deeper anonymous-channel-cap issue (truncation of Ch3/Ch4/Instatalk) remains, but its impact is "silently degraded multi-bus mixing on public anon servers" rather than "video doesn't work at all" — which is a documented limitation, not a P0 bug.

See sibling document: [`phase-20-anon-channel-cap-remediation-plan.md`](./phase-20-anon-channel-cap-remediation-plan.md) for the full task breakdown. The proposed Plan 20-04 starts with the canonical-skip fix (Task 0) as a hot-fix-able step, then optionally extends into an auth-reply-aware layout selector:

- **Task 0 (canonical fix)** — implement `flags & 0x10` skip in `on_new_interval()` + `process_samples()`. Smallest-possible change. Re-run UAT after this — may be sufficient for v1.3 beta acceptance.
- **Tasks 1-8 (full remediation)** — auth-reply-aware mode selector + UI indicator + `docs/SERVER.md` matrix + extended test coverage. Buys robustness across the full range of NINJAM server configurations including stricter caps and intermediate (3-5 channel) tiers.

## References

- Wire capture: `/tmp/jamwide-wire.txt` (operator's local machine, transient)
- ninjamzap-server source: `/Users/cell/dev/ninjamzap-server`
  - `ninjam/server/ninjamsrv.cpp:240-241, 362-367, 636-637`
  - `ninjam/server/usercon.cpp:602`
  - `ninjam/mpb.h:64-74` (auth-reply struct)
  - `ninjam/njclient.cpp:1070` (client reads maxchan)
  - `configs/default-2050.cfg:4` and `configs/default.cfg:4` (`MaxChannels 32 2`)
- JamWide source:
  - `juce/NinjamRunThread.cpp:334-426` (the audio + Instatalk + video channel registration block)
  - `src/core/njclient.cpp:2980-3035` (Plan 20-00 run-thread drain of m_rawdata_sendq into `mpb_client_upload_interval_begin/write`)
- Phase 20 artifacts:
  - `.planning/phases/20-h-264-encoder-send-pipeline/20-00-substrate-revision-SUMMARY.md`
  - `.planning/phases/20-h-264-encoder-send-pipeline/20-01-video-encoder-SUMMARY.md`
  - `.planning/phases/20-h-264-encoder-send-pipeline/20-02-video-state-machine-SUMMARY.md`
  - `.planning/phases/20-h-264-encoder-send-pipeline/20-03-processor-wiring-and-uat-SUMMARY.md`
  - `.planning/phases/20-h-264-encoder-send-pipeline/HUMAN-UAT.md`
  - `tests/uat/phase-20-broadcast-uat-procedure.md`
