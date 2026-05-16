---
phase: 20
plan: 02
slug: video-state-machine
type: execute
wave: 2
depends_on:
  - 20-00
  - 20-01
files_modified:
  - src/core/njclient.h
  - src/core/njclient.cpp
  - tests/test_video_state_machine.cpp
  - tests/test_curwritefile_guid_seqlock.cpp
  - CMakeLists.txt
autonomous: true
requirements:
  - WIRE-01
  - COD-02
threat_refs:
  - T-20-02
  - T-20-02D
  - T-20-COLD
review_refs:
  - R3-MF1-d20-guid-determinism
  - R3-MF3-cold-start-sps-pps

must_haves:
  truths:
    - "NJClient gains the NinjamZap-literal video state members: WDL_Mutex m_video_cs, WDL_Mutex m_video_spspps_cs, bool m_video_active, bool m_video_interval_open, unsigned char m_video_guid[16], int m_video_chidx, unsigned int m_video_fourcc, WDL_HeapBuf m_video_spspps, int m_sync_interval_cnt, std::atomic<uint64_t> m_audio_interval_seq, std::atomic<uint64_t> m_encoder_input_drops mirror (read from Openh264Encoder); placement and names match ninjamzap-core/njclient.h:267 (m_sync_interval_cnt) and ninjamzap-core/njclient.cpp Plan-20-02-equivalent video state block"
    - "on_new_interval gains an inlined video block AFTER the existing per-channel BlockRecord push loop (audio thread location); the entire block is enclosed in a single WDL_MutexLock(&m_video_cs) RAII scope per D-08 — held across END/BEGIN/marker/SPS-PPS/END-at-deactivate per R3 must-fix item 6 (closed)"
    - "Inside the m_video_cs critical section: m_sync_interval_cnt is incremented; m_video_active/m_video_interval_open/m_video_guid are read+written; the 24-byte marker is constructed on-stack with [4B BE prefix=20][4B BE m_sync_interval_cnt][16B audio_ch0_guid]; audio_ch0_guid is sourced via the seqlock helper at Task 2 (Must-fix 1 resolution); m_video_spspps_cs is nested-acquired for the SPS/PPS read; RawDataSendBegin/RawDataSendWrite are called (each internally takes m_rawdata_cs per Plan 20-00 substrate)"
    - "SetVideoChannel(chidx, fourcc) is exposed on NJClient; called by Plan 20-03 NinjamRunThread connect-up callback AFTER the existing SetLocalChannelInfo block per D-18 + Pitfall 6; sets m_video_chidx=chidx, m_video_fourcc=fourcc; the NinjamZap variant flips m_video_active in this call — JamWide separates m_video_active toggling into a dedicated SetVideoBroadcastActive(bool) call so Plan 20-03's Broadcast button can drive it independently of channel registration (channel registration happens once at connect-up regardless of broadcast state per D-18)"
    - "SetVideoBroadcastActive(bool) is exposed on NJClient; acquires m_video_cs, sets m_video_active=value, releases; called from the message thread by Plan 20-03's Broadcast button toggle"
    - "QueueVideoFrame(const void* data, int len) is exposed on NJClient; called by Plan 20-01's Openh264Encoder publishEncodedNal callback on the encoder thread; acquires m_video_cs, checks m_video_active && m_video_interval_open, if true: builds the 4-byte BE length prefix on-stack into a [4][len] buffer (4 + len bytes; planner picks: small heap alloc for >4KB frames OR an `int frame_size = len; unsigned char prefix[4]; <write BE>; RawDataSendWrite(m_video_guid, prefix, 4, false); RawDataSendWrite(m_video_guid, data, len, false);` two-Write split — both options are NinjamZap-literal valid since the receiver re-assembles from the substrate's MAX_ENC_BLOCKSIZE chunks via the 4-byte length prefix regardless), calls the relevant RawDataSendWrite calls, releases m_video_cs"
    - "SetVideoSPSPPS(const void* data, int len) is exposed on NJClient; called by Plan 20-01's Openh264Encoder publishSpsPps callback on the encoder thread; acquires m_video_spspps_cs, ResizeOK + memcpy into m_video_spspps WDL_HeapBuf, releases — verbatim port of ninjamzap-core/njclient.cpp:2175-2188"
    - "Must-fix 1 (D-20 GUID race determinism) is resolved via per-channel atomic seqlock on Local_Channel::m_curwritefile.guid; the writer at src/core/njclient.cpp:2606 (run-thread broadcast/encode path) is wrapped to bump a std::atomic<uint64_t> seqlock counter (Local_Channel::m_curwritefile_guid_seq) BEFORE the 16-byte memcpy and AGAIN AFTER, with release fences; the audio-thread reader inside on_new_interval reads the seqlock counter (acquire), copies the 16 bytes, re-reads the counter (acquire), and retries on mismatch — bounded by a retry cap (default 4) after which the audio thread emits 16 zero bytes (NinjamZap-literal fallback per CONTEXT.md `<specifics>`: zero-GUID is the NONE-match path on the receiver side); TSan reports zero races on this site under --tsan build per Phase 15.1 D-07"
    - "Must-fix 3 (cold-start SPS/PPS resolution) is resolved via OPTION (b) marker-only-first-interval as accepted NinjamZap behavior: on the audio-thread side, the m_video_cs critical section ALWAYS emits BEGIN + marker if m_video_active, and emits SPS/PPS as chunk #2 ONLY IF m_video_spspps.GetSize() > 0 under the nested m_video_spspps_cs lock (verbatim port of ninjamzap-core/njclient.cpp:3073-3076); WIRE-01 success criterion 2 is re-worded in the per-plan acceptance criteria below to accept 'after at most 1 marker-only interval, SPS/PPS appears as chunk #2 for all subsequent intervals' per R3 must-fix item 3 + NinjamZap scenario `25_no_initial_spspps.cpp`"
    - "tests/test_video_state_machine.cpp covers (a) END/BEGIN/marker/SPS-PPS ordering under whole-block m_video_cs (R3 MF item 6 closed); (b) frame-during-marker interleave is blocked by m_video_cs serialization (cross-producer test mimicking the Plan 20-01 encoder thread + on_new_interval); (c) m_sync_interval_cnt incremented monotonically per call; (d) cold-start marker-only first interval (option (b)); (e) SetVideoBroadcastActive(false) → next on_new_interval emits END at the deactivate branch"
    - "tests/test_curwritefile_guid_seqlock.cpp covers (a) reader+writer concurrency stress test: 1 writer thread rolling the 16-byte GUID + bumping the seqlock at 100Hz, 1 reader thread reading via the seqlock helper at 1kHz, asserts: 0 torn reads (the reader either gets a complete pre-bump value, a complete post-bump value, OR falls back to 16-zero bytes after retry cap exceeded — never a half-and-half memcpy); (b) seqlock retry cap behavior: writer holds the seqlock at odd parity (mid-write) longer than the retry cap → reader returns 16 zero bytes (NONE-match path); (c) TSan-clean under --tsan build"
  artifacts:
    - path: "src/core/njclient.h"
      provides: "Public APIs SetVideoChannel/SetVideoBroadcastActive/QueueVideoFrame/SetVideoSPSPPS + private video state members (m_video_cs, m_video_spspps_cs, m_video_active, m_video_interval_open, m_video_guid, m_video_chidx, m_video_fourcc, m_video_spspps, m_sync_interval_cnt, m_audio_interval_seq) + the seqlock-helper friend hook on Local_Channel"
      contains: "WDL_Mutex m_video_cs"
    - path: "src/core/njclient.cpp"
      provides: "on_new_interval video block (whole-block m_video_cs); SetVideoChannel + SetVideoBroadcastActive + QueueVideoFrame + SetVideoSPSPPS impls; seqlock helper functions readGuidSeqlock(Local_Channel&, unsigned char out[16]) and writeGuidSeqlock(Local_Channel&, const unsigned char in[16]); audit-allowlist sites match the entries Plan 20-00 wrote into .claude/agents/realtime-audio-reviewer.md"
      contains: "m_video_cs.Enter()"
    - path: "tests/test_video_state_machine.cpp"
      provides: "Cross-producer state-machine coverage per CONTEXT.md `<specifics>` audio glitch test signature + R3 MF item 6 closure validation"
      min_lines: 300
    - path: "tests/test_curwritefile_guid_seqlock.cpp"
      provides: "Must-fix 1 deterministic-resolution test"
      min_lines: 150
    - path: "CMakeLists.txt"
      provides: "Wire test_video_state_machine + test_curwritefile_guid_seqlock executables under JAMWIDE_BUILD_TESTS; link against njclient static lib; add_test entries"
      contains: "test_video_state_machine"
  key_links:
    - from: "NJClient::on_new_interval (audio thread)"
      to: "NJClient::RawDataSendBegin / RawDataSendWrite (via WDL_Mutex m_rawdata_cs from Plan 20-00)"
      via: "video block holds m_video_cs across the entire END/BEGIN/marker/SPS-PPS sequence (D-08); RawDataSendBegin/Write each internally take m_rawdata_cs (D-19)"
      pattern: "m_video_cs\\.Enter\\(\\)"
    - from: "Plan 20-01 Openh264Encoder publishEncodedNal callback (encoder thread)"
      to: "NJClient::QueueVideoFrame → RawDataSendWrite"
      via: "publishEncodedNal_ std::function attached at Openh264Encoder::open(); calls back into NJClient::QueueVideoFrame which acquires m_video_cs, validates active+open, calls RawDataSendWrite under the mutex (D-11 NinjamZap-literal)"
      pattern: "QueueVideoFrame"
    - from: "Plan 20-01 Openh264Encoder publishSpsPps callback (encoder thread)"
      to: "NJClient::SetVideoSPSPPS"
      via: "publishSpsPps_ std::function attached at Openh264Encoder::open(); calls back into NJClient::SetVideoSPSPPS which acquires m_video_spspps_cs, ResizeOK + memcpy into m_video_spspps (D-03)"
      pattern: "SetVideoSPSPPS"
    - from: "audio-thread marker construction inside on_new_interval"
      to: "Local_Channel::m_curwritefile.guid (canonical field on Local_Channel; Phase 15.1-06 HIGH-2 carve-out per D-20)"
      via: "readGuidSeqlock(*m_locchannels.Get(0), markerGuid16) — seqlock-protected 16-byte memcpy with retry cap; on retry exhaustion: zero-fill (NONE-match path per CONTEXT.md `<specifics>`)"
      pattern: "readGuidSeqlock"
    - from: "Plan 20-03 NinjamRunThread connect-up (run thread)"
      to: "NJClient::SetVideoChannel(chidx=1, fourcc=H264) + NotifyServerOfChannelChange"
      via: "channel registration block in juce/NinjamRunThread.cpp added by Plan 20-03; this plan provides the API surface only"
      pattern: "SetVideoChannel"
---

<objective>
Plan 20-02 ports the NinjamZap send-side video state machine into NJClient, verbatim per CONTEXT.md D-08 / D-09 / D-11 / D-13 / D-18 / D-20 + R2/R3 must-fix items. The audio thread acquires `m_video_cs` ONCE at the top of `on_new_interval`'s video block and holds it across the entire END→BEGIN→marker→SPS-PPS→END-at-deactivate sequence (NinjamZap-literal closure of R2 H6 wire-ordering race). Inside the critical section: `m_sync_interval_cnt` increments; the 24-byte marker is constructed with `[4B BE prefix=20][4B BE m_sync_interval_cnt][16B audio_ch0_guid]`; `audio_ch0_guid` is sourced via the per-channel atomic seqlock helper (Must-fix 1 deterministic resolution — does NOT defer to TSan); the nested `m_video_spspps_cs` is acquired for the SPS/PPS read; `RawDataSendBegin`/`RawDataSendWrite` are called (each takes `m_rawdata_cs` internally per Plan 20-00's substrate).

The encoder-thread surface (`QueueVideoFrame` + `SetVideoSPSPPS`) mirrors NinjamZap's API verbatim with one JamWide-specific split: NinjamZap's `SetVideoChannel` toggles `m_video_active=true` in the same call (`ninjamzap-core/njclient.cpp:2088`); JamWide separates this into `SetVideoBroadcastActive(bool)` so Plan 20-03's Broadcast button can drive it from the message thread independently of channel registration (which happens once at connect-up regardless of broadcast state per D-18 + Pitfall 6 + the existing JamWide NinjamRunThread connect-up pattern at line 322-389).

Must-fix 1 (D-20 GUID race determinism): implemented as a per-channel atomic seqlock on `Local_Channel::m_curwritefile.guid` — the run-thread writer at `src/core/njclient.cpp:2606` bumps an atomic `m_curwritefile_guid_seq` counter (release-fence) before the 16-byte memcpy and again after; the audio-thread reader uses the seqlock pattern (acquire-load seq → memcpy → acquire-load seq again; retry on parity mismatch up to N=4; fall back to 16-zero bytes which is the NinjamZap-receiver-side NONE-match path per CONTEXT.md `<specifics>`). This makes the race deterministic and TSan-clean, not "if TSan flags" per R3 must-fix item 1.

Must-fix 3 (cold-start SPS/PPS): adopt option (b) marker-only-first-interval as accepted NinjamZap behavior per R3 must-fix item 3. The audio thread ALWAYS emits END+BEGIN+marker if `m_video_active`; SPS/PPS chunk #2 is emitted ONLY IF `m_video_spspps.GetSize() > 0` (NinjamZap-literal `if size > 0` pattern at `ninjamzap-core/njclient.cpp:3073-3076`). WIRE-01 wording is reconciled in this plan's acceptance: "after at most 1 marker-only interval, SPS/PPS appears as chunk #2 for all subsequent intervals."

Purpose: this is the architecturally-novel core of Phase 20. It introduces three audio-thread carve-outs (mutex acquisitions, heap allocation inside RawDataSendBegin/Write, RNG inside RawDataSendBegin, marker memcpy, direct Local_Channel field read) which Plan 20-00 has already documented in the `realtime-audio-reviewer` envelope. Determinism over probabilism applies: the GUID race is closed by a concrete seqlock, not deferred to TSan discovery.

Output: A `NJClient` whose `on_new_interval` emits NinjamZap-wire-identical interval framing under whole-block `m_video_cs`; encoder-thread `QueueVideoFrame`/`SetVideoSPSPPS` APIs ready for Plan 20-01 + 20-03 to attach; a deterministic seqlock making the `audio_ch0_guid` read TSan-clean; two new test executables (`test_video_state_machine` + `test_curwritefile_guid_seqlock`) covering state-machine ordering, frame-during-marker interleave defense, cold-start option (b), and seqlock retry/fallback behavior.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/REQUIREMENTS.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-CONTEXT.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-VALIDATION.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-REVIEWS.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-00-PLAN-substrate-revision.md
@.planning/phases/20-h-264-encoder-send-pipeline/20-01-PLAN-video-encoder.md
@.planning/phases/15.1-rt-safety-hardening/15.1-06-SUMMARY.md
@src/core/njclient.h
@src/core/njclient.cpp
@juce/video/encoder/VideoEncoder.h
@juce/video/encoder/Openh264Encoder.h

<interfaces>
<!-- Key contracts the executor needs. Extracted from NinjamZap reference (verbatim port targets) + JamWide as-shipped + the upstream Plan 20-00 and 20-01 surfaces. -->

From ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:3041-3082 — CANONICAL on_new_interval video block (port target for Task 1):
  // (running on the audio thread — same thread that drains audio intervals)
  // m_video_cs.Enter()       ← Plan 20-02 adds; NinjamZap source is implicit single-thread for this block
  // [...JamWide-specific m_sync_interval_cnt increment here per CONTEXT.md ; NinjamZap bumps elsewhere at line 3024]
  m_sync_interval_cnt++;
  if (m_video_active) {
    if (m_video_interval_open)
      RawDataSendWrite(m_video_guid, NULL, 0, true); // END previous
    RawDataSendBegin(m_video_guid, m_video_fourcc, m_video_chidx, 0); // BEGIN new
    m_video_interval_open = true;
    {
      unsigned char marker[24];
      marker[0] = 0; marker[1] = 0; marker[2] = 0; marker[3] = 20; // BE u32 = 20
      marker[4] = (unsigned char)((m_sync_interval_cnt >> 24) & 0xFF);
      marker[5] = (unsigned char)((m_sync_interval_cnt >> 16) & 0xFF);
      marker[6] = (unsigned char)((m_sync_interval_cnt >> 8) & 0xFF);
      marker[7] = (unsigned char)(m_sync_interval_cnt & 0xFF);
      memset(marker + 8, 0, 16);
      // JamWide deviation per D-20: replace the NinjamZap `for (li ...)` direct deref
      // with seqlock-protected read via readGuidSeqlock helper (Task 2).
      if (m_locchannels.GetSize() > 0) {
        Local_Channel *lc = m_locchannels.Get(0);   // channel_idx==0 by JamWide convention
        if (lc && lc->channel_idx == 0) {
          readGuidSeqlock(*lc, marker + 8);          // seqlock-safe 16-byte read; on retry cap, leaves zeros
        }
      }
      RawDataSendWrite(m_video_guid, marker, 24, false);
    }
    m_video_spspps_cs.Enter();
    if (m_video_spspps.GetSize() > 0)
      RawDataSendWrite(m_video_guid, m_video_spspps.Get(), m_video_spspps.GetSize(), false);
    m_video_spspps_cs.Leave();
  } else if (m_video_interval_open) {
    RawDataSendWrite(m_video_guid, NULL, 0, true);
    m_video_interval_open = false;
  }
  // m_video_cs.Leave()        ← Plan 20-02 adds

From ninjamzap-core/core/ninjamclient/libninjamcore/njclient.cpp:2084-2121 — CANONICAL public API (port target for Task 1):
  void NJClient::SetVideoChannel(int chidx, unsigned int fourcc) {
    m_video_chidx = chidx;
    m_video_fourcc = fourcc;
    m_video_active = true;                           // ← JamWide REMOVES this line; SetVideoBroadcastActive owns it
    // NinjamZap's "BEGIN immediately if mid-interval" path is omitted in JamWide v1.3 —
    // Plan 20-03 only calls SetVideoChannel at connect-up (before any interval has started),
    // so the mid-interval BEGIN race window doesn't apply. SetVideoBroadcastActive(true)
    // takes effect at the NEXT on_new_interval, which is NinjamZap's `SetVideoChannel`
    // mid-interval branch behavior anyway when SetVideoChannel is called at connect-up.
  }
  void NJClient::StopVideoChannel() { m_video_active = false; }   // ← JamWide RENAMES to SetVideoBroadcastActive(false)
  void NJClient::QueueVideoFrame(const void *data, int len) {
    if (!m_video_active || !m_video_interval_open) return;
    if (data && len > 0)
      RawDataSendWrite(m_video_guid, data, len, false);
    // JamWide ADDS: acquire m_video_cs at the top per D-11; release at function exit (RAII WDL_MutexLock).
    // The active+open checks happen INSIDE the lock so they observe the latest audio-thread writes.
    // JamWide ALSO ADDS: 4-byte BE length-prefix split via two RawDataSendWrite calls (or single combined alloc; planner picks) per CONTEXT.md COD-02.
  }
  void NJClient::SetVideoSPSPPS(const void *data, int len) {       // verbatim port of cpp:2175-2188
    m_video_spspps_cs.Enter();
    if (data && len > 0) { m_video_spspps.ResizeOK(len); memcpy(m_video_spspps.Get(), data, len); }
    else                 { m_video_spspps.Resize(0); }
    m_video_spspps_cs.Leave();
  }

NJClient new public APIs (DECLARE IN THIS PLAN at src/core/njclient.h, public section near the existing video/audio API area):
  // Phase 20 — H.264 send-side video state machine. NinjamZap-literal per CONTEXT.md D-08/D-09/D-11/D-13/D-18/D-20.
  void SetVideoChannel(int chidx, unsigned int fourcc);          // run-thread; called once at connect-up per D-18
  void SetVideoBroadcastActive(bool active);                     // message-thread; called by Plan 20-03 Broadcast button
  void QueueVideoFrame(const void* data, int len);               // encoder-thread; called by Plan 20-01 publishEncodedNal callback
  void SetVideoSPSPPS(const void* data, int len);                // encoder-thread; called by Plan 20-01 publishSpsPps callback
  // Observability — Plan 20-03 UAT reads these
  std::atomic<uint64_t>* getAudioIntervalSeqPtr() noexcept { return &m_audio_interval_seq; }     // Plan 20-01 reads
  uint64_t GetAudioIntervalSeq() const noexcept { return m_audio_interval_seq.load(std::memory_order_relaxed); }

NJClient new private state members (DECLARE IN THIS PLAN at src/core/njclient.h, near m_rawdata_sendq area added by 20-00):
  // Phase 20 video send state — NinjamZap-literal per CONTEXT.md D-08 + D-11 + D-13.
  WDL_Mutex                  m_video_cs;           // held across whole on_new_interval video block
  WDL_Mutex                  m_video_spspps_cs;    // nested inside m_video_cs for SPS/PPS read
  bool                       m_video_active{false};       // under m_video_cs
  bool                       m_video_interval_open{false};// under m_video_cs
  unsigned char              m_video_guid[16]{};   // under m_video_cs; GUID for the current open interval
  int                        m_video_chidx{1};     // under m_video_cs (set by SetVideoChannel at connect-up); default 1
  unsigned int               m_video_fourcc{MAKE_NJ_FOURCC('H','2','6','4')};  // set by SetVideoChannel
  WDL_HeapBuf                m_video_spspps;       // under m_video_spspps_cs
  int                        m_sync_interval_cnt{0};      // bumped under m_video_cs at top of on_new_interval video block
  std::atomic<uint64_t>      m_audio_interval_seq{0};     // bumped (release) at top of on_new_interval; Plan 20-01 reads (relaxed)
  // Plan 20-01 publishes this via callback; mirrored here for observability/JAMWIDE_BUILD_TESTS readout
  std::atomic<uint64_t>      m_encoder_input_drops_mirror{0};

Local_Channel addition (DECLARE IN THIS PLAN at src/core/njclient.h, inside the Local_Channel class definition; Phase 15.1-06 HIGH-2 carve-out per D-20):
  // Seqlock on m_curwritefile.guid (16 bytes) — atomic counter bumped (release) by the run-thread writer
  // around the memcpy that updates the GUID. The audio-thread reader inside NJClient::on_new_interval
  // does an acquire-load → memcpy → acquire-load → parity check, retrying up to 4 times on parity
  // mismatch before falling back to 16 zero bytes (NinjamZap-receiver-side NONE-match path).
  // Even parity = "clean"; odd parity = "writer is mid-update".
  std::atomic<uint64_t> m_curwritefile_guid_seq{0};

Seqlock helpers (DECLARE IN THIS PLAN at src/core/njclient.h, near Local_Channel; DEFINE in src/core/njclient.cpp):
  // Returns true on clean read, false on retry-cap exceeded (out16 is zero-filled in that case).
  // Acquire-load the counter, memcpy guid, acquire-load the counter again; on (begin == end && (begin & 1) == 0) the
  // read is consistent. Retry up to N=4 times; on exhaustion, zero-fill out16 and return false. Uses
  // std::atomic<uint64_t>::load(std::memory_order_acquire). The 16-byte memcpy in the middle is plain memcpy —
  // the seqlock contract is that the WRITER frames it with the release-fence parity bumps.
  bool readGuidSeqlock(Local_Channel& lc, unsigned char out16[16]) noexcept;
  // Run-thread side; called at the existing src/core/njclient.cpp:2606 write site BEFORE and AFTER the memcpy
  // that fills lc.m_curwritefile.guid. Internally: m_curwritefile_guid_seq.fetch_add(1, release) → memcpy →
  // m_curwritefile_guid_seq.fetch_add(1, release). After both increments, the counter is back to even parity
  // and the new value is observable by the reader.
  void writeGuidSeqlock(Local_Channel& lc, const unsigned char in16[16]) noexcept;

From JamWide src/core/njclient.cpp:2606 (write site to wrap with writeGuidSeqlock per Must-fix 1):
  // Pre-plan: the run-thread broadcast/encode path writes lc->m_curwritefile.guid directly via memcpy.
  // This plan replaces that memcpy with writeGuidSeqlock(*lc, newGuid16) — same memcpy, framed by parity bumps.

From JamWide juce/NinjamRunThread.cpp connect-up callback (lines 322-389) — Plan 20-03 will add the SetVideoChannel + NotifyServerOfChannelChange call after the existing SetLocalChannelInfo block. Plan 20-02 only needs to know this is where the call will land so the API surface is shaped correctly (chidx=1 is the JamWide convention; existing audio channels 0-3 + Instatalk channel 4 fit the documented mapping, leaving chidx 5+ untouched).

Bit-for-bit wire-format spec (CONTEXT.md `<specifics>` — for test_video_state_machine assertions):
  - 24-byte marker: byte[0..3] = 0x00 0x00 0x00 0x14 (BE u32 = 20); byte[4..7] = m_sync_interval_cnt as BE u32; byte[8..23] = audio_ch0_guid 16 bytes.
  - SPS/PPS chunk: raw [SPS-NAL][PPS-NAL] concatenation; whatever Plan 20-01 publishes via publishSpsPps is what we emit.
  - Per-frame chunk: [4B BE length][NAL or NAL-group bytes] — Plan 20-02's QueueVideoFrame ADDS the 4-byte BE length prefix in front of the bytes the encoder published (Plan 20-01 publishes raw NALs without prefix).
</interfaces>
</context>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| audio thread (on_new_interval) ↔ encoder thread (QueueVideoFrame) | both producers serialize via m_video_cs (D-08 / D-11); wire ordering is naturally enforced |
| audio thread ↔ run thread (Local_Channel::m_curwritefile.guid) | 16-byte field crosses thread boundary; readGuidSeqlock/writeGuidSeqlock implement the contract |
| message thread (SetVideoBroadcastActive) ↔ audio thread (on_new_interval reads m_video_active) | m_video_cs serializes the toggle vs the read |
| encoder thread (SetVideoSPSPPS) ↔ audio thread (on_new_interval reads m_video_spspps) | m_video_spspps_cs serializes the publish vs the read |
| run thread (drain at NJClient::Run) ↔ audio thread (RawDataSendWrite from on_new_interval video block) | m_rawdata_cs (Plan 20-00) serializes the producer vs consumer |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-20-02 | Tampering (wire-ordering race: frame between marker and SPS-PPS) | NJClient on_new_interval + QueueVideoFrame | mitigate | Whole-block m_video_cs in on_new_interval (D-08) + QueueVideoFrame acquires same mutex (D-11); test_video_state_machine sub-test (b) reproduces the cross-producer interleave attempt deterministically and asserts the wire-order END→BEGIN→marker→SPS-PPS→frame |
| T-20-02D | Tampering (torn 16-byte read of canonical audio_ch0_guid) | Local_Channel::m_curwritefile.guid (cross-thread) | mitigate | Per-channel atomic seqlock (m_curwritefile_guid_seq) implemented in this plan per R3 MF1; writer side bumps parity at src/core/njclient.cpp:2606 around the memcpy; reader side retries up to 4 times then falls back to 16-zero (NinjamZap NONE-match path); test_curwritefile_guid_seqlock stress test exercises the race deterministically under --tsan |
| T-20-COLD | Information disclosure (cold-start SPS/PPS absence → receiver decoder init failure on first interval) | NJClient on_new_interval video block | mitigate-with-tradeoff | Option (b) per R3 MF3: first interval after broadcast-on may be marker-only if encoder still warming (~50-150ms per CONTEXT.md D-13 + Pitfall 5); receiver tolerates per NinjamZap scenario `25_no_initial_spspps.cpp`; subsequent intervals carry SPS/PPS as chunk #2; WIRE-01 success criterion 2 wording reconciled in this plan per acceptance criterion below |
| T-20-AUDIT | Tampering (CRITICAL audit violations from audio-thread carve-outs) | realtime-audio-reviewer at phase close | mitigate | All audit-allowlist sites in this plan (m_video_cs.Enter on audio thread, m_video_spspps_cs.Enter nested, m_rawdata_cs.Enter via RawDataSendBegin/Write, WDL_RNG_bytes, new RawDataQueueItem, item->data.ResizeOK, memcpy of marker/SPS-PPS, direct Local_Channel read) are pre-accepted in the envelope Plan 20-00 wrote to .claude/agents/realtime-audio-reviewer.md; Plan 20-02 introduces NO new audio-thread carve-outs outside that envelope; auditor zero-CRITICAL gate at phase close MUST pass |
| T-20-SC | Tampering (supply chain) | no new packages | n/a | This plan touches only existing source; no package installs |
</threat_model>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Add NJClient video state machine — on_new_interval video block + SetVideoChannel/SetVideoBroadcastActive/QueueVideoFrame/SetVideoSPSPPS APIs (NinjamZap-literal per D-08 + D-11 + D-13)</name>
  <files>
    src/core/njclient.h,
    src/core/njclient.cpp
  </files>
  <behavior>
    - Add the 10 private state members per `<interfaces>` to NJClient (placed near m_rawdata_sendq which Plan 20-00 already moved here; group as "Phase 20 video send state" comment block).
    - Initialize members in NJClient ctor: m_video_active=false, m_video_interval_open=false, m_video_chidx=1, m_video_fourcc=MAKE_NJ_FOURCC('H','2','6','4'), m_sync_interval_cnt=0, m_audio_interval_seq=0, m_video_guid memset to 0, m_video_spspps default-constructed (empty WDL_HeapBuf).
    - In on_new_interval, AFTER the existing per-channel BlockRecord push loop (currently ending around line 4845) and BEFORE the per-peer slot loop (currently starting around line 4852), insert the video block:

        // Phase 20: NinjamZap-literal send-side video state machine.
        // Whole-block m_video_cs serialization closes R2 H6 wire-ordering race per D-08.
        // All audio-thread carve-outs in this scope are accepted by realtime-audio-reviewer
        // per the envelope published in Plan 20-00 (.claude/agents/realtime-audio-reviewer.md).
        {
          WDL_MutexLock vlock(&m_video_cs);            // RAII Enter/Leave per JamWide standard pattern (see m_log_cs uses)
          m_sync_interval_cnt++;
          m_audio_interval_seq.fetch_add(1, std::memory_order_release);  // Plan 20-01 encoder thread reads this (relaxed)
          if (m_video_active) {
            if (m_video_interval_open) {
              RawDataSendWrite(m_video_guid, NULL, 0, true);              // END previous
            }
            RawDataSendBegin(m_video_guid, m_video_fourcc, m_video_chidx, 0);  // BEGIN new (also generates new m_video_guid via WDL_RNG_bytes)
            m_video_interval_open = true;
            unsigned char marker[24];
            marker[0] = 0; marker[1] = 0; marker[2] = 0; marker[3] = 20;
            marker[4] = (unsigned char)((m_sync_interval_cnt >> 24) & 0xFF);
            marker[5] = (unsigned char)((m_sync_interval_cnt >> 16) & 0xFF);
            marker[6] = (unsigned char)((m_sync_interval_cnt >> 8) & 0xFF);
            marker[7] = (unsigned char)(m_sync_interval_cnt & 0xFF);
            memset(marker + 8, 0, 16);                                    // default to NONE-match path
            if (m_locchannels.GetSize() > 0) {
              Local_Channel *lc = m_locchannels.Get(0);
              if (lc && lc->channel_idx == 0) {
                readGuidSeqlock(*lc, marker + 8);                          // seqlock-safe; on retry exhaustion, zeros stay
              }
            }
            RawDataSendWrite(m_video_guid, marker, 24, false);
            {
              WDL_MutexLock slock(&m_video_spspps_cs);                    // nested inside m_video_cs (D-03)
              if (m_video_spspps.GetSize() > 0) {
                RawDataSendWrite(m_video_guid, m_video_spspps.Get(), m_video_spspps.GetSize(), false);
              }
            }
          } else if (m_video_interval_open) {
            RawDataSendWrite(m_video_guid, NULL, 0, true);                // END at deactivate (still under m_video_cs)
            m_video_interval_open = false;
          }
        }

    - Add SetVideoChannel(int chidx, unsigned int fourcc) — acquires m_video_cs, sets m_video_chidx=chidx + m_video_fourcc=fourcc, releases. Does NOT toggle m_video_active (JamWide split per `<interfaces>`).
    - Add SetVideoBroadcastActive(bool active) — acquires m_video_cs, sets m_video_active=active, releases. The next on_new_interval picks up the new state.
    - Add QueueVideoFrame(const void* data, int len) — acquires m_video_cs, if (m_video_active && m_video_interval_open && data && len > 0), build the 4-byte BE length prefix on stack: `unsigned char prefix[4]; prefix[0]=(len>>24)&0xFF; prefix[1]=(len>>16)&0xFF; prefix[2]=(len>>8)&0xFF; prefix[3]=len&0xFF;`. Issue TWO RawDataSendWrite calls under m_video_cs: first the prefix (4 bytes), then the data (len bytes). Both end with isEnd=false. Release m_video_cs at function exit (RAII WDL_MutexLock).
        NOTE: the two-call approach is preferred over a single combined heap alloc because (a) NinjamZap source uses single RawDataSendWrite for the marker (24 bytes is on-stack); the 4-byte prefix has the same on-stack character; the data pointer is owned by the encoder thread's slab pool — a combined alloc would double-copy the (potentially large) NAL bytes. (b) The receive-side reassembler at Phase 21 handles arbitrary chunk boundaries from MAX_ENC_BLOCKSIZE splitting anyway, so two queue items that get drained back-to-back have identical wire semantics to one queue item with a combined payload.
    - Add SetVideoSPSPPS(const void* data, int len) — acquires m_video_spspps_cs, if (data && len > 0) ResizeOK(len) + memcpy into m_video_spspps; else Resize(0). Releases. Verbatim port of `ninjamzap-core/njclient.cpp:2175-2188`.
  </behavior>
  <action>
    Implement the additions verbatim per the `<interfaces>` block + behavior list above. Use JamWide's existing WDL_MutexLock RAII pattern (search njclient.cpp for existing `WDL_MutexLock <name>(&m_<...>_cs);` usage for the style match; m_log_cs and m_misc_cs are precedent). Place `readGuidSeqlock` and `writeGuidSeqlock` declarations in njclient.h public-API region near where Local_Channel is declared, with the seqlock helper functions declared as free functions in the njclient namespace (not as Local_Channel members — they need access to NJClient infrastructure but are pure functions). Definition lives in njclient.cpp NEAR the existing m_curwritefile.guid write site (line 2606) so the writer-side wrapping at Task 2 is local. After this task, the audit-allowlist envelope from Plan 20-00 covers every new audio-thread site introduced here — verify by re-running the auditor (`./scripts/audit-realtime-audio.sh` or equivalent; check the audit-allowlist file path is honored) and asserting zero CRITICAL counts.
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target njclient -- -j8 2>&amp;1 | tail -20</automated>
    njclient static lib builds clean with the new video state machine + APIs.
  </verify>
  <done>
    NJClient compiles with: 10 new private state members; 4 new public APIs (SetVideoChannel, SetVideoBroadcastActive, QueueVideoFrame, SetVideoSPSPPS); the on_new_interval video block under whole-block m_video_cs per D-08; nested m_video_spspps_cs acquisition for SPS/PPS read; the seqlock helper declarations exist (definitions land in Task 2). No `writeLog` calls anywhere on this path (Plan 20-00's invariant is preserved). The 4-byte BE length-prefix wrapping inside QueueVideoFrame is per COD-02. The full test suite from Plan 20-00 (test_rawdata_send 8 sub-tests) is still green — the substrate is unchanged, only NEW consumers are added.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Implement per-channel atomic seqlock on Local_Channel::m_curwritefile.guid (R3 Must-fix 1 — deterministic GUID race resolution) + dedicated test</name>
  <files>
    src/core/njclient.h,
    src/core/njclient.cpp,
    tests/test_curwritefile_guid_seqlock.cpp,
    CMakeLists.txt
  </files>
  <behavior>
    - Add the seqlock counter to Local_Channel: `std::atomic<uint64_t> m_curwritefile_guid_seq{0};` per `<interfaces>`. Local_Channel is declared in njclient.h (currently around line 837 area where m_locchannels is). The std::atomic addition does not break trivial-copyability of Local_Channel (atomics are not trivially copyable but Local_Channel is already non-trivially-copyable because of WDL_Heap members; Phase 15.1-06 HIGH-2 already establishes the lifetime contract via DeleteLocalChannel deferred-free).
    - Implement `bool readGuidSeqlock(Local_Channel& lc, unsigned char out16[16]) noexcept` per `<interfaces>`:
        for (int attempt = 0; attempt < 4; ++attempt) {
          uint64_t begin = lc.m_curwritefile_guid_seq.load(std::memory_order_acquire);
          if (begin & 1) continue;                                       // writer is mid-update; spin once
          memcpy(out16, lc.m_curwritefile.guid, 16);
          uint64_t end = lc.m_curwritefile_guid_seq.load(std::memory_order_acquire);
          if (begin == end) return true;                                 // clean read
          // mismatch: writer ran during our memcpy; retry
        }
        memset(out16, 0, 16);                                            // retry cap exceeded; zero-fill (NinjamZap NONE-match)
        return false;
    - Implement `void writeGuidSeqlock(Local_Channel& lc, const unsigned char in16[16]) noexcept`:
        lc.m_curwritefile_guid_seq.fetch_add(1, std::memory_order_release);  // begin: bump to odd parity
        memcpy(lc.m_curwritefile.guid, in16, 16);
        lc.m_curwritefile_guid_seq.fetch_add(1, std::memory_order_release);  // end: bump back to even parity
    - Wrap the existing run-thread writer at src/core/njclient.cpp:2606 — find the memcpy/Resize that updates m_curwritefile.guid (per RESEARCH §Pitfall 1 + the line 2606 cite); the current code likely does `memcpy(lc->m_curwritefile.guid, newGuid, 16);` or `WDL_RNG_bytes(lc->m_curwritefile.guid, 16);` (depending on the path). Replace any direct write to lc->m_curwritefile.guid with `writeGuidSeqlock(*lc, newGuid16);` — for the RNG path, populate a local stack buffer first via WDL_RNG_bytes then pass to writeGuidSeqlock. If multiple write sites exist (audit by `grep -nE "m_curwritefile\\.guid\\s*[=,]\\|memcpy.*m_curwritefile\\.guid" src/core/njclient.cpp`), wrap each one; this discipline is the Must-fix 1 resolution.
    - tests/test_curwritefile_guid_seqlock.cpp implements:
        Test A (concurrency stress): 1 writer thread bumps the GUID via writeGuidSeqlock at 100 Hz for 5 seconds; 1 reader thread reads via readGuidSeqlock at 1 kHz for the same window. After both threads join, assert: every read either returned a "complete" GUID matching one of the writer's published values (track all published values in a thread-safe vector for verification) OR returned 16 zero bytes (retry cap exhausted, NONE-match fallback); NO torn read (a 16-byte value that's NOT in the published set AND not zero) ever occurred.
        Test B (retry-cap fallback): writer thread sets the seqlock to odd parity manually (or simulates a mid-write hang by sleeping between the two fetch_add bumps for ~10ms while holding odd parity). Reader runs concurrently; assert: reader returns false (retry cap exceeded) and out16 is 16 zero bytes.
        Test C (TSan clean): compile this test under --tsan (JAMWIDE_TSAN=ON) if the build supports it; assert TSan reports zero data races on the m_curwritefile.guid memory + the m_curwritefile_guid_seq counter. (Conditional sub-test: only runs the assertion if compiled with JAMWIDE_TSAN; otherwise prints "skipped — non-TSan build" and PASSes.)
        Test D (single-thread non-contended): trivial single-thread write-then-read returns true with the exact published bytes.
    - CMakeLists.txt: add the test executable wiring (link njclient, JAMWIDE_BUILD_TESTS define, add_test).
  </behavior>
  <action>
    The seqlock pattern is a classical seqlock — same shape as `linux/include/linux/seqlock.h` minus the cache-line padding. The std::memory_order_release on the writer pair + std::memory_order_acquire on the reader load creates a happens-before edge from the post-bump write to the post-read load; the memcpy in the middle (writer side) is guarded by the odd-parity bracket on the reader. Note that the reader's memcpy reads bytes that may be torn — the parity check after the memcpy is what catches this and triggers a retry. JamWide's existing audit-allowlist envelope (Plan 20-00) covers the `readGuidSeqlock` site at the marker construction (D-09 + D-20 carve-out); the `writeGuidSeqlock` site is on the run thread, no audit-allowlist needed. After the writer-site wrapping, search the codebase for any remaining direct writes to `m_curwritefile.guid` and either wrap them in `writeGuidSeqlock` or document why they don't race with the audio-thread read (e.g. constructor-time initialization before any thread can observe the Local_Channel is a non-racing write).
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target test_curwritefile_guid_seqlock njclient -- -j8 &amp;&amp; ctest -R curwritefile_guid_seqlock --output-on-failure 2>&amp;1 | tail -30</automated>
    test_curwritefile_guid_seqlock builds + executes; sub-tests A/B/C/D all PASSED.
  </verify>
  <done>
    Local_Channel has the seqlock counter; readGuidSeqlock + writeGuidSeqlock are defined and callable; the run-thread writer at line 2606 (and any other discovered sites) uses writeGuidSeqlock; test_curwritefile_guid_seqlock's 4 sub-tests are green. Under --tsan build (if available), test_curwritefile_guid_seqlock runs clean. R3 must-fix item 1 is closed deterministically — no TSan-discovery deferral.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 3: tests/test_video_state_machine.cpp — state-machine ordering + wire-format invariants + cold-start option (b) + audit-allowlist sanity</name>
  <files>
    tests/test_video_state_machine.cpp,
    CMakeLists.txt
  </files>
  <behavior>
    Sub-tests (each using TEST/PASS/FAIL macros from test_rawdata_send pattern):

    1. test_video_block_emits_begin_marker_sps_when_active —
       - Construct NJClient. Initialize: set m_video_active=true via SetVideoBroadcastActive(true); call SetVideoChannel(1, MAKE_NJ_FOURCC('H','2','6','4')); call SetVideoSPSPPS(spspps_bytes, S).
       - Drive on_new_interval (use the JAMWIDE_BUILD_TESTS-only helper if one exists, or directly call NJClient::on_new_interval — if it's private, expose via a friend-test or a JAMWIDE_BUILD_TESTS public test helper `RunOneIntervalForTest()`).
       - Drain m_rawdata_sendq via DrainRawDataSendQueueForTest; assert wire order:
           item[0]: BEGIN(g, H264, 1, 0); record g
           item[1]: WRITE(g, marker24, 24, isEnd=false); marker[0..3]==[0,0,0,20]; marker[4..7]==BE(1) for m_sync_interval_cnt; marker[8..23]==0 (no Local_Channel yet)
           item[2]: WRITE(g, spspps, S, isEnd=false)
       - This validates the END/BEGIN/marker/SPS-PPS order under whole-block m_video_cs per R3 MF6 closure.

    2. test_video_block_emits_end_only_when_deactivated —
       - Continue from sub-test 1's state (m_video_interval_open=true, m_video_active=true).
       - Call SetVideoBroadcastActive(false).
       - Drive on_new_interval again.
       - Drain; assert: exactly one item: WRITE(g, NULL, 0, isEnd=true) ← END at deactivate branch.

    3. test_video_frame_during_marker_interleave_is_blocked —
       - Set up NJClient with active broadcast as in sub-test 1.
       - Drive on_new_interval (which holds m_video_cs across the whole block) while a parallel thread tries to call QueueVideoFrame(frameBytes, F) ~every microsecond.
       - After on_new_interval returns and the parallel thread completes, drain m_rawdata_sendq; assert: ALL of the parallel thread's frame writes appear in the queue AFTER the BEGIN+marker+SPS-PPS triplet (i.e. the marker/SPS-PPS triplet is never interleaved with a frame). This is the R3 MF item 6 enforcement test.
       - Stress: run the test for at least 100 intervals × 10000 attempted frame interleaves; assert no order violation across all iterations.

    4. test_video_marker_uses_audio_ch0_guid —
       - Create a Local_Channel at index 0 with a known GUID via the existing public API (look for SetLocalChannelInfo paths; if not directly settable, use a JAMWIDE_BUILD_TESTS helper that creates the channel and sets the guid via writeGuidSeqlock).
       - Drive on_new_interval; assert marker[8..23] == knownGuid16.

    5. test_video_cold_start_marker_only_first_interval (Must-fix 3 option (b) validation) —
       - Set up NJClient with active broadcast BUT do NOT call SetVideoSPSPPS yet (cold-start state).
       - Drive on_new_interval; drain.
       - Assert: BEGIN + marker, but NO SPS/PPS chunk (m_video_spspps.GetSize() == 0 → conditional skip per D-13 + `ninjamzap-core/njclient.cpp:3074-3076`).
       - Then call SetVideoSPSPPS(bytes, S) to simulate encoder warm-up completing.
       - Drive on_new_interval again; assert: END(prev) + BEGIN(new) + marker + SPS/PPS — all four in order.

    6. test_video_inactive_no_emission —
       - NJClient with m_video_active=false and m_video_interval_open=false (initial state); drive on_new_interval; drain; assert: zero RawDataQueueItems related to video (the video block's else-if also doesn't fire because there's no open interval to close).

    7. test_video_audio_interval_seq_increments —
       - Drive on_new_interval N=10 times; after each call, assert GetAudioIntervalSeq() == call_count.
       - This validates D-15 — Plan 20-01's encoder thread can read the atomic to detect interval boundaries.

    Note on driving on_new_interval from a test: NJClient::on_new_interval is currently `void NJClient::on_new_interval()` and is called from the audio thread inside AudioProc. For testing, expose a `RunOneIntervalForTest()` public test-only method (gated under JAMWIDE_BUILD_TESTS like the existing DrainRawDataSendQueueForTest at line 3084) that just calls `on_new_interval()` — this lets the test drive the audio-thread block from the test main thread without spinning up a real AudioProc loop.

    Wire-up CMakeLists.txt for test_video_state_machine: add_executable, link njclient, target_compile_definitions JAMWIDE_BUILD_TESTS=1, add_test NAME video_state_machine.
  </behavior>
  <action>
    Implement the 7 sub-tests above; expose `RunOneIntervalForTest()` as a JAMWIDE_BUILD_TESTS-gated public method on NJClient (declaration in njclient.h, definition in njclient.cpp, both inside the existing `#ifdef JAMWIDE_BUILD_TESTS` block around lines 3079-3110). Use the same TEST/PASS/FAIL macros and the same scaffold pattern as test_rawdata_send.cpp + test_video_encoder.cpp. The cross-producer stress in sub-test 3 needs careful thread teardown — use std::thread + a std::atomic<bool> stop_flag; the parallel thread checks stop_flag between QueueVideoFrame calls.
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; cmake --build . --target test_video_state_machine -- -j8 &amp;&amp; ctest -R video_state_machine --output-on-failure 2>&amp;1 | tail -30</automated>
    test_video_state_machine builds and executes; 7/7 sub-tests pass.
  </verify>
  <done>
    test_video_state_machine.cpp's 7 sub-tests are all green under `ctest -R video_state_machine`. The R3 MF item 6 closure (wire-ordering under whole-block m_video_cs) is verified deterministically. The R3 MF item 3 cold-start option (b) is validated via sub-test 5. Audit-allowlist sanity: re-running the realtime-audio-reviewer over the audio-thread paths surfaces zero CRITICAL findings (the envelope from Plan 20-00 covers every site this plan adds). Full suite `ctest --output-on-failure` is green.
  </done>
</task>

</tasks>

<verification>
- `cd build-juce && cmake --build . --target njclient -- -j8` exits 0
- `cd build-juce && cmake --build . --target test_curwritefile_guid_seqlock test_video_state_machine -- -j8` exits 0
- `ctest -R "curwritefile_guid_seqlock|video_state_machine|rawdata_send|video_encoder" --output-on-failure` exits 0; all targeted test executables fully green
- `cd build-juce && ctest --output-on-failure` exits 0 (full-suite regression check)
- `grep -c "m_video_cs.Enter\\|WDL_MutexLock.*m_video_cs" src/core/njclient.cpp` returns ≥ 2 (the on_new_interval video block + the QueueVideoFrame body each acquire m_video_cs)
- `grep -c "readGuidSeqlock\\|writeGuidSeqlock" src/core/njclient.cpp` returns ≥ 3 (1 read site in on_new_interval, ≥1 write site wrapping line 2606)
- realtime-audio-reviewer audit (run via the existing audit hook; this verification block re-confirms the carve-out envelope holds): zero CRITICAL findings on `src/core/njclient.cpp` audio paths
- Under `--tsan` build (optional but recommended): `ctest -R "curwritefile_guid_seqlock|video_state_machine"` shows zero TSan-reported races on m_video_cs / m_video_spspps_cs / m_curwritefile_guid_seq counters
</verification>

<success_criteria>
- Plan 20-02 lands the NinjamZap-literal send-side state machine per CONTEXT.md D-08 / D-09 / D-11 / D-13 / D-18 / D-20.
- R3 must-fix item 1 (D-20 GUID race determinism) is closed via the per-channel atomic seqlock + writer-site wrapping at line 2606, NOT deferred to TSan discovery.
- R3 must-fix item 3 (cold-start SPS/PPS) is closed via option (b) marker-only-first-interval as accepted NinjamZap behavior; WIRE-01 wording is reconciled in this plan's acceptance.
- R3 must-fix item 6 (whole-block m_video_cs wire-ordering) is closed via test_video_state_machine sub-test 3.
- COD-02 (4-byte BE length-prefix per frame) is delivered by QueueVideoFrame's two-call split (prefix + data).
- WIRE-01 (NinjamZap-compatible wire format: fourCC H264 + 24-byte marker + SPS/PPS chunk #2 + per-frame BE length prefix) is delivered end-to-end on the send-side.
- Auditor zero-CRITICAL gate passes because Plan 20-00 pre-published the audit-allowlist envelope.
</success_criteria>

<output>
On completion, write `.planning/phases/20-h-264-encoder-send-pipeline/20-02-SUMMARY.md` per the get-shit-done summary template. Capture in the summary: (a) the exact line numbers of the run-thread writer sites for `m_curwritefile.guid` that ended up wrapped with `writeGuidSeqlock` (audit discipline — should match RESEARCH.md §Pitfall 1's cited line 2606 plus any others discovered); (b) whether all 7 test_video_state_machine sub-tests passed first-try or required iteration; (c) the cold-start exception wording added to the inline acceptance per Must-fix 3; (d) one head -40 of the on_new_interval video block as actually committed so subsequent plan reviewers can see the final NinjamZap-literal pattern.
</output>
