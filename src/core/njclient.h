/*
    NINJAM - njclient.h
    Copyright (C) 2005 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*

  This file defines the interface for the NJClient class, which handles
  the bulk of logic and state of the client.

  The basic premise of the NJClient class is, the UI code tells NJClient
  when the user tweaks something, and NJClient tells the UI code when
  it needs to update something.

  NJClient::Run() needs to be called regularly (preferably every 50ms or less).
  When calling, if Run() returns 0, you should immediately call it again. i.e.:

  while (!myClient->Run());

  Is how Run() should usually be called. In general it is easier to call Run()
  from the UI thread in a timer, for example, but it turns out it's a lot better
  to call it from its own thread to ensure that some UI issue doesn't end up
  stalling it. If you go this route, you will want to put the Run() call inside
  of a mutex lock, and also any code that reads/writes remote channel state or
  writes to local channel state, in that mutex lock as well. This is a bit of
  a pain, but not really that bad.

  Additionally, NJClient::AudioProc() needs to be called from the audio thread.
  It is not necessary to do any sort of mutex protection around these calls,
  though, as they are done internally.


  Some other notes:

    + Currently only OGG Vorbis is supported. There's hooks in there to add support
      for more formats, but the requirements for the formats are a little high, so
      currently OGG Vorbis is the only thing we've seen that would work well. And it
      really rocks for this application.

    + OK maybe that's it for now? :)

*/

#ifndef _NJCLIENT_H_
#define _NJCLIENT_H_

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <memory.h>
#endif
#include <stdio.h>
#include <time.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../wdl/wdlstring.h"
#include "../wdl/ptrlist.h"
#include "../wdl/jnetlib/jnetlib.h"
#include "../wdl/sha.h"
#include "../wdl/rng.h"
#include "../wdl/mutex.h"

#include "../wdl/wavwrite.h"

#include "netmsg.h"

// 15.1-05 CR-05/06/07: deferred-delete SPSC infrastructure (Wave 0 finalized in 15.1-04).
#include "../threading/spsc_ring.h"
#include "../threading/spsc_payloads.h"

// Plan 21-02 Task 1 (codex Cluster 2 Option A): VideoRecvState owns a 4-slot
// snapshot ring. Header MUST be included here because std::array<VideoRecvSlotSnapshot, 4>
// template instantiation in struct VideoRecvState requires the full POD definition.
// NalChunk and Openh264Decoder are NOT included — NalChunks are decoder-thread-
// local (codex Cluster 2) and Openh264Decoder is forward-declared below.
#include "../../juce/video/decoder/VideoRecvSlotSnapshot.h"

// Plan 21-02 Task 1: forward declarations for the decoder + sink types
// owned by Plan 21-03's distributor. VideoRecvState carries a unique_ptr
// to the decoder and a raw pointer to the sink; both .cpp files include
// the full headers where needed.
namespace jamwide {
class Openh264Decoder;
class PeerVideoSink;
// Plan 21-03 Task 1 forward decl: receive-side distributor injected by
// JamWideJuceProcessor.
class JamWideRemoteFrameDistributor;
} // namespace jamwide


class I_NJEncoder;
class RemoteDownload;
class RemoteUser;
class RemoteUser_Channel;
class Local_Channel;
class DecodeState;
class BufferQueue;
class DecodeMediaBuffer;

// Plan 20-02 Task 2: per-channel atomic two-uint64_t halves seqlock helpers
// for the canonical audio_ch0_guid (R3 MF1 + R4 H8 — TSan-clean by C++ memory
// model). The 16-byte GUID payload lives in two `std::atomic<uint64_t>`
// halves (m_curwritefile_guid_lo for bytes 0..7, m_curwritefile_guid_hi for
// bytes 8..15) framed by a `std::atomic<uint64_t>` parity counter
// (m_curwritefile_guid_seq). Even parity = stable; odd parity = writer is
// mid-update.
//
// readGuidSeqlock attempts 4 reads against the seqlock; on each attempt it
// acquire-loads the seq counter, checks parity, relaxed-loads lo + hi into
// stack temporaries, acquire-fences, and relaxed-loads the seq counter again.
// If the two seq values match (and were even), the bytes in out16 are a
// consistent snapshot. After 4 failed attempts, out16 is zero-filled and the
// function returns false — receivers treat zero-GUID as the NinjamZap
// NONE-match path.
//
// writeGuidSeqlock release-bumps the seq counter (odd parity → writer mid-
// update), relaxed-stores lo + hi (extracted from in16 via memcpy onto local
// temporaries — safe: temporaries are not shared), then release-bumps the
// seq counter again (even parity → writer done). Audio-thread reads of the
// non-atomic legacy m_curwritefile.guid[16] byte array are FORBIDDEN by the
// audit-allowlist envelope (R4 H8): the audio thread reads ONLY through
// readGuidSeqlock.
//
// Declared at file scope so they can be invoked from
//   - audio thread: NJClient::on_new_interval marker construction
//   - run thread:   src/core/njclient.cpp:2606 m_curwritefile.guid write site
// without taking Local_Channel as a template parameter — Local_Channel is
// defined inline in njclient.cpp (line ~813), not in the header.
bool readGuidSeqlock(Local_Channel& lc, unsigned char out16[16]) noexcept;
void writeGuidSeqlock(Local_Channel& lc, const unsigned char in16[16]) noexcept;

// 15.1-06 CR-02: maximum local channel count. Promoted from a #define at the
// bottom of this header so LocalChannelMirror[MAX_LOCAL_CHANNELS] declared on
// NJClient (below) sees the constant. Original #define preserved at the
// bottom of the file for source-compat with existing callers.
#ifndef MAX_LOCAL_CHANNELS
#define MAX_LOCAL_CHANNELS 32
#endif

// 15.1-07a CR-01: maximum user-channel count. Hoisted from a #define at the
// bottom of this header so RemoteUserChannelMirror::chans[MAX_USER_CHANNELS]
// (below) sees the constant. Original #define preserved at the bottom for
// source-compat with existing callers.
#ifndef MAX_USER_CHANNELS
#define MAX_USER_CHANNELS 32
#endif

// 15.1-07a CR-01: maximum simultaneous remote peers tracked by the audio-thread
// mirror. NINJAM servers cap typical jam-room sizes at 32-48 peers; 64 is a
// conservative ceiling. Used to size m_remoteuser_mirror[MAX_PEERS] below.
#ifndef MAX_PEERS
#define MAX_PEERS 64
#endif

// 15.1-06 CR-02: audio-thread-owned mirror of local-channel state.
//
// Updated by NJClient::drainLocalChannelUpdates() at top of AudioProc; never
// accessed off the audio thread. The audio thread reads exactly the fields
// it needs to mix/encode each local channel — every field is BY VALUE.
//
// Codex HIGH-2 architectural fix: NO Local_Channel* / lc_ptr / void*
// escape-hatch field. The original revision of this plan added an `lc_ptr`
// so the audio thread could call `lc_ptr->m_bq.AddBlock(...)` for the
// BufferQueue handoff; that undermined the mirror model because the audio
// thread still dereferenced run-thread-owned objects. This revision
// eliminates the back-pointer entirely. The per-channel BlockRecord SPSC
// (the only consumer of that pointer) is stored AS A MEMBER here.
//
// Notes on lifetime:
//   - The mirror is a fixed-size array on NJClient; lifetime is tied to the
//     NJClient instance. Mirror entries are constructed in place when the
//     enclosing NJClient is constructed; the per-entry block_q SpscRing is
//     non-copyable/non-movable but in-place default-constructible (verified
//     by reading src/threading/spsc_ring.h:43).
//   - block_q is the producer side for the BlockRecord SPSC consumed by the
//     encoder thread (wired in 15.1-07b). On RemovedUpdate apply, the audio
//     thread drains the ring empty and resets scalar fields; the same ring
//     is reused on the next AddedUpdate without ever being destroyed.
//
// 15.1-06 NinjamRunThread Instatalk processor (cbf) — see deviation #2 in
// 15.1-06-SUMMARY.md: the production Local_Channel.cbf is consulted from
// process_samples (Instatalk PTT mute lambda registered via
// SetLocalChannelProcessor at connect time). Both fields are
// trivially-copyable (function pointer + void*); cbf_inst is owned by
// JamWideJuceProcessor (which outlives NJClient), so this is NOT a
// HIGH-2 violation — the void* is a callback-context owned by the
// audio plugin host, not a back-pointer into a run-thread-owned
// Local_Channel object.
struct LocalChannelMirror {
    bool         active = false;
    int          srcch = 0;
    int          bitrate = 0;
    bool         bcast = false;
    bool         mute = false;
    bool         solo = false;
    float        volume = 1.0f;
    float        pan = 0.0f;
    int          outch = -1;
    unsigned int flags = 0;

    // 15.1-06: Instatalk PTT processor (and any future SetLocalChannelProcessor
    // user). Function pointer + opaque-context, both trivially copyable, both
    // owned by the audio-plugin host (JamWideJuceProcessor). NOT a HIGH-2
    // back-pointer (it is not derived from Local_Channel; the
    // SetLocalChannelProcessor caller passes its own pointer).
    void (*cbf)(float* /*buf*/, int /*ns*/, void* /*inst*/) = nullptr;
    void* cbf_inst = nullptr;

    // 15.1-06 + 15.1-07b: per-channel BlockRecord SPSC. process_samples /
    // on_new_interval is the producer side (15.1-07b will wire the actual
    // pushes). The encoder thread is the consumer (also 15.1-07b). Owned by
    // the mirror entry; lifetime is tied to the entry's active flag — drained
    // empty on RemovedUpdate apply, reused on the next AddedUpdate.
    jamwide::SpscRing<jamwide::BlockRecord, 16> block_q;

    // 15.1-06: per-channel VU peak. Audio thread writes (relaxed); UI/run
    // thread reads via NJClient::GetLocalChannelPeak (relaxed). Cross-thread
    // float atomic reads are well-defined; the values are display-only so
    // no synchronization-with-other-state is needed. Replaces the canonical
    // Local_Channel.decode_peak_vol[2] read path that previously required
    // m_locchan_cs from the UI thread.
    std::atomic<float> peak_vol_l{0.0f};
    std::atomic<float> peak_vol_r{0.0f};

    // 15.1-07b: audio-thread-owned broadcast state. Replaces
    // canonical Local_Channel.bcast_active and .m_curwritefile_curbuflen
    // for the audio-thread-only path; the canonical fields remain in
    // Local_Channel for the run-thread encoder, written exclusively from
    // run thread (NJClient::Run() lines 1667-1669). Audio thread reads the
    // intended `bcast` field (run-thread-published) and tracks its own
    // bcast_active boundary state here.
    bool   bcast_active = false;
    double curwritefile_curbuflen = 0.0;
};

// 15.1-07a CR-01: audio-thread-owned per-channel mirror of remote-user state.
//
// Updated by NJClient::drainRemoteUserUpdates() at the top of AudioProc; never
// accessed off the audio thread. Every field the audio thread needs to mix
// (process_samples / mixInChannel) is stored BY VALUE.
//
// Codex HIGH-2 architectural fix: NO RemoteUser_Channel* / RemoteUser* /
// user_ptr / void* escape-hatch field. The original revision of this plan
// added a `user_ptr` field so the audio thread could access fields not in the
// mirror; that undermined the mirror model because the audio thread still
// dereferenced run-thread-owned objects. This revision eliminates the back-
// pointer entirely.
//
// The DecodeState* members (ds, next_ds[0], next_ds[1]) are AUDIO-THREAD-OWNED
// once published via PeerNextDsUpdate from the run thread. This is documented
// ownership transfer (per spsc_payloads.h header comment) — NOT a back-
// reference into shared state. The audio thread frees old DecodeState
// pointers via the m_deferred_delete_q SPSC (15.1-05 helper).
//
// The session-info path (RemoteUser_Channel::GetSessionInfo, only reached when
// flags & 4) is NOT mirrored — sessionmode is unused by JamWide's UI today
// (see deferred-items.md / 15.1-MIRROR-AUDIT.md). The audio thread's
// sessionmode branch in mixInChannel becomes a no-op without mirror data; if
// a future plan exposes sessionmode, a separate session-info SPSC ring will
// need to be added to this struct (and the mirror keyed by stable identity
// preserved).
struct RemoteUserChannelMirror {
    bool         present = false;
    bool         muted = false;
    bool         solo = false;
    float        volume = 1.0f;
    float        pan = 0.0f;
    int          out_chan_index = 0;
    unsigned int flags = 0;
    unsigned int codec_fourcc = 0;

    // Audio-thread-owned DecodeState pointers; ownership transfers via
    // PeerNextDsUpdate. Freed via deferDecodeStateDelete (15.1-05).
    class ::DecodeState* ds = nullptr;
    class ::DecodeState* next_ds[2] = {nullptr, nullptr};

    // Audio-thread-only state: replaces RemoteUser_Channel::dump_samples and
    // .curds_lenleft for mixInChannel's resample/skip bookkeeping. NOT read
    // by run thread.
    int    dump_samples = 0;
    double curds_lenleft = 0.0;

    // Per-channel VU peak. Audio thread writes (relaxed); UI thread reads via
    // GetUserChannelPeak (relaxed). std::atomic<double> is too heavy on some
    // platforms — split into two atomic floats matching LocalChannelMirror.
    // The canonical RemoteUser_Channel.decode_peak_vol[2] remains as legacy
    // storage (no longer the UI source of truth once this lands).
    std::atomic<float> peak_vol_l{0.0f};
    std::atomic<float> peak_vol_r{0.0f};
};

// 15.1-07a CR-01: audio-thread-owned mirror of a remote peer.
//
// Indexed by a STABLE SLOT (not by m_remoteusers list position). The run thread
// allocates a slot when a peer is added (see SetUserChannelState publish path)
// and releases it after the audio thread acknowledges the corresponding
// PeerRemovedUpdate via the m_audio_drain_generation gate. This mitigates the
// "Bug A shape" risk identified in 15.1-MIRROR-AUDIT.md — m_remoteusers.Delete
// shifts subsequent list indices, but mirror entries are NEVER reindexed.
//
// Codex HIGH-2 architectural fix: NO RemoteUser* / user_ptr / void* escape-
// hatch field. Every field the audio thread needs to iterate peers and call
// mixInChannel is here BY VALUE. See RemoteUserChannelMirror above for the
// per-channel breakdown.
//
// Lifetime: members of NJClient::m_remoteuser_mirror[MAX_PEERS] — constructed
// in place when NJClient is constructed; per-mirror RemoteUserChannelMirror
// chans[] are POD-default-initialized. The std::atomic<float> peak fields are
// non-copyable/non-movable but in-place default-constructible.
struct RemoteUserMirror {
    bool active = false;
    int  user_index = 0;             // server-assigned, stable per-session
    int  submask = 0;
    int  chanpresentmask = 0;
    int  mutedmask = 0;
    int  solomask = 0;
    bool muted = false;
    float volume = 1.0f;
    float pan = 0.0f;
    RemoteUserChannelMirror chans[MAX_USER_CHANNELS];
};

// #define NJCLIENT_NO_XMIT_SUPPORT // might want to do this for njcast :)
//  it also removes mixed ogg writing support

class NJClient
{
  friend class RemoteDownload;
public:
  static constexpr int kRemoteNameMax = 128;

  // Phase 20-00 (D-19): forward-decl so the public test helpers below can name
  // the type; full declaration lives in the private section near
  // m_rawdata_sendq. Field shape mirrors `ninjamzap-core/njclient.h:311-321`
  // verbatim (int type / unsigned char guid[16] / unsigned int fourcc /
  // int chidx / int estsize / int flags / WDL_HeapBuf data).
  struct RawDataQueueItem;

  NJClient();
  ~NJClient();

  void Connect(const char *host, const char *user, const char *pass);
  void Disconnect();

  // call Run() from your main (UI) thread
  int Run();// returns nonzero if sleep is OK

  const char *GetErrorStr() { return m_errstr.Get(); }

  int IsAudioRunning() { return m_audio_enable; }
  // call AudioProc, (and only AudioProc) from your audio thread
  void AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, bool justmonitor=false, bool isPlaying=true, bool isSeek=false, double cursessionpos=-1.0); // len is number of sample pairs or samples


  // Basic configuration (non-atomic, require state_mutex)
  int   config_autosubscribe;
  int   config_savelocalaudio; // set 1 to save compressed files, set to 2 to save .wav files as well.
                                // -1 makes it try to delete the remote .oggs as soon as possible

  // MVP atomic config fields (thread-safe read from audio thread)
  std::atomic<float> config_metronome{0.5f};      // metronome volume
  std::atomic<float> config_metronome_pan{0.0f};  // metronome pan
  std::atomic<bool>  config_metronome_mute{false};
  std::atomic<int>   config_metronome_channel{-1};

  std::atomic<float> config_mastervolume{1.0f};   // master volume
  std::atomic<float> config_masterpan{0.0f};      // master pan
  std::atomic<bool>  config_mastermute{false};
  std::atomic<int>   config_play_prebuffer{8192}; // -1 means play instantly, 0 means play when full file is there

  // Codec format selection (UI thread writes via SetEncoderFormat, Run thread reads at interval boundary)
  std::atomic<unsigned int> m_encoder_fmt_requested{0};  // initialized in constructor
  unsigned int m_encoder_fmt_active = 0;  // only accessed by Run thread
  unsigned int m_encoder_fmt_prev = 0;    // previous format for chat notification

  void SetEncoderFormat(unsigned int fourcc);
  unsigned int GetEncoderFormat() const { return m_encoder_fmt_requested.load(std::memory_order_relaxed); }

  // Non-atomic config fields (require state_mutex)
  int   config_debug_level;
  int config_remote_autochan; // 1=auto-assign by channel, 2=auto-assign by user
  int config_remote_autochan_nch;

  float GetOutputPeak(int ch=-1);

  enum { NJC_STATUS_DISCONNECTED=-3,NJC_STATUS_INVALIDAUTH=-2, NJC_STATUS_CANTCONNECT=-1, NJC_STATUS_OK=0, NJC_STATUS_PRECONNECT};
  int GetStatus();

  // Lock-free status access for audio thread
  std::atomic<int> cached_status{NJC_STATUS_DISCONNECTED};

  // 15.1-02 CR-03: atomic publish/consume of beat-info from run thread to audio thread.
  //
  // PUBLICATION PROTOCOL (edge-triggered, best-effort — Codex review L-10):
  //   Writer (run thread, updateBPMinfo):
  //     1. m_bpm.store(latest, relaxed)
  //     2. m_bpi.store(latest, relaxed)
  //     3. m_beatinfo_updated.store(1, release)   <-- synchronizes with reader's acquire
  //   Reader (audio thread, AudioProc):
  //     1. if (m_beatinfo_updated.load(acquire)) {
  //          int bpm = m_bpm.load(relaxed);
  //          int bpi = m_bpi.load(relaxed);
  //          m_beatinfo_updated.store(0, relaxed);  // edge-clear; another publish may have raced past
  //          ... apply bpm/bpi ...
  //        }
  //
  // The reader sees the LATEST published payload, not every intermediate one. If the writer
  // publishes 5 times between two reader runs, the reader's single observation will see the
  // 5th (most recent) bpm/bpi and miss the 1st-4th. This is correct: BPM/BPI are config
  // values, only the most recent matters; on_new_interval recomputes interval state from
  // whatever the reader observed.
  //
  // This is NOT a last-value latch protocol (which would buffer every intermediate value);
  // it is an edge-triggered "something has changed, here is the current state" signal.
  std::atomic<int> m_beatinfo_updated{0};
  std::atomic<int> m_bpm{0};
  std::atomic<int> m_bpi{0};

  // 15.1-02 (AUDIT line 421): m_interval_pos was racing between processBlock writer and GetPosition reader.
  // Promoted to std::atomic<int>; processBlock writer uses store(relaxed); GetPosition reader uses
  // load(relaxed). AudioProc same-thread reads/writes also use relaxed.
  std::atomic<int> m_interval_pos{0};

  void SetWorkDir(char *path);
  const char *GetWorkDir() { return m_workdir.Get(); }

  const char *GetUser() { return m_user.Get(); }
  const char *GetHostName() { return m_host.Get(); }

  float GetActualBPM() { return (float) m_active_bpm; }
  int GetBPI() { return m_active_bpi; }
  void GetPosition(int *pos, int *length);  // positions in samples
  // Set interval position for DAW sync offset alignment (Phase 7 — SYNC-02).
  // Called from processBlock (audio thread). 15.1-02: m_interval_pos is now atomic;
  // relaxed store is sufficient because no other state's visibility depends on it.
  void SetIntervalPosition(int pos) { m_interval_pos.store(pos, std::memory_order_relaxed); }
  int GetLoopCount() { return m_loopcnt; }
  unsigned int GetSessionPosition(); // returns milliseconds

  int HasUserInfoChanged() { if (m_userinfochange) { m_userinfochange=0; return 1; } return 0; }
  int GetNumUsers();
  const char *GetUserState(int idx, float *vol=0, float *pan=0, bool *mute=0);
  void SetUserState(int idx, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute);

  struct RemoteChannelInfo {
    char name[kRemoteNameMax + 1] = {};
    int name_len = 0;
    int channel_index = -1;
    bool subscribed = false;
    float volume = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    float vu_left = 0.0f;
    float vu_right = 0.0f;
    int out_chan_index = 0;
    unsigned int codec_fourcc = 0;
    int flags = 0;   // Channel flags (0x02=instamode, 0x04=session mode, etc.)
  };

  struct RemoteUserInfo {
    char name[kRemoteNameMax + 1] = {};
    int name_len = 0;
    bool mute = false;
    float volume = 1.0f;
    float pan = 0.0f;
    std::vector<RemoteChannelInfo> channels;
  };

  void GetRemoteUsersSnapshot(std::vector<RemoteUserInfo>& out);

  float GetUserChannelPeak(int useridx, int channelidx, int whichch=-1);

  // Falsifiable UAT readout for the 2026-05-02 RemoteUserMirror orphan-fields
  // fix. Returns the per-(slot,channel) count of PeerChannelInfoUpdate
  // publishes (run thread) / applies (audio thread). Both relaxed-load.
  // See .planning/debug/remote-channels-cutoff.md.
  uint64_t GetChannelInfoPublishCount(int slot, int channel) const noexcept;
  uint64_t GetChannelInfoApplyCount  (int slot, int channel) const noexcept;

  // 2026-05-03 tx-silent-and-orphan-cutoff diagnostic: peak dump_samples ever
  // reached for this (slot,channel) since session start. dump_samples is the
  // skip-debt accumulator in mixInChannel — when codec underruns, the audio
  // thread bumps it by `needed*srcnch` and skips incoming samples to pay
  // down. A high peak indicates a single-shot underrun episode that produced
  // a multi-second silent gap. Audio-thread-writes / UI-thread-reads, relaxed.
  int GetDumpSamplesPeak(int slot, int channel) const noexcept;

  // Aggregate count of DecodeMediaBuffer SPSC ring-saturation drops since
  // session start. A non-zero value means the run thread was bursting bytes
  // into a per-channel decode buffer faster than the audio thread could drain
  // them — likely root cause of repeated codec underruns.
  uint64_t GetDecodeBufWriteDropTotal() const noexcept;

  // 2026-05-03 tx-silent-and-orphan-cutoff: read-only mirror-state inspector
  // for /rcmstats. Reads RemoteUserMirror[slot] and chans[channel] with
  // relaxed semantics — observability only, audio-thread races are accepted
  // (single-shot diagnostic, same risk profile as peak_vol_l/r reads from
  // GetUserChannelPeak). DecodeState* fields are reported as bool "active"
  // (non-null) only — the pointer values themselves are never exposed.
  // Returns false if (slot, channel) is out of bounds.
  struct MirrorChannelSnapshot {
      bool present;
      bool muted;
      bool solo;
      float volume;
      float pan;
      int out_chan_index;
      unsigned int flags;
      unsigned int codec_fourcc;
      bool ds_active;
      bool next_ds0_active;
      bool next_ds1_active;
      int dump_samples;
      double curds_lenleft;
  };
  struct MirrorPeerSnapshot {
      bool active;
      int user_index;
      int submask;
      int chanpresentmask;
      int mutedmask;
      int solomask;
      bool muted;
      float volume;
      float pan;
  };
  bool GetMirrorChannelSnapshot(int slot, int channel, MirrorChannelSnapshot* out) const noexcept;
  bool GetMirrorPeerSnapshot   (int slot,              MirrorPeerSnapshot*    out) const noexcept;

  // 2026-05-03 TX-silent investigation: local channel mirror snapshot for
  // diagnosing transmit-side bugs. Audio-thread-writes / UI-thread-reads,
  // relaxed semantics. Returns false if `ch` is out of bounds.
  struct LocalChannelMirrorSnapshot {
      bool active;
      bool bcast;
      bool bcast_active;
      bool mute;
      bool solo;
      int srcch;
      int bitrate;
      int outch;
      unsigned int flags;
      float volume;
      float pan;
      float peak_l;
      float peak_r;
  };
  bool GetLocalChannelMirrorSnapshot(int ch, LocalChannelMirrorSnapshot* out) const noexcept;

  // === Profiling (cpu-spikes-beta12-regression) — added in v1.1-beta.20.4.
  //   Cumulative atomic counters for steady_clock-measured durations of three
  //   hot paths used to confirm/falsify the CPU-spike regression hypotheses.
  //   Recording functions are RT-safe (3 relaxed atomic ops, no locks, no
  //   allocation). Snapshot is read from the message thread via /rcmstats or
  //   the DBG button. Static — counters are process-global.
  struct ProfilingSnapshot {
    uint64_t run_count;            // NJClient::Run() invocations (run thread)
    uint64_t run_total_ns;
    uint64_t run_max_ns;
    uint64_t decbuf_alloc_count;   // `new DecodeMediaBuffer` (run thread)
    uint64_t decbuf_alloc_total_ns;
    uint64_t decbuf_alloc_max_ns;
    uint64_t timer_cb_count;       // JamWideJuceEditor::timerCallback (msg thread)
    uint64_t timer_cb_total_ns;
    uint64_t timer_cb_max_ns;
  };
  static ProfilingSnapshot GetProfilingSnapshot() noexcept;
  static void ProfilingRecordRun(uint64_t ns) noexcept;
  static void ProfilingRecordDecbufAlloc(uint64_t ns) noexcept;
  static void ProfilingRecordTimerCallback(uint64_t ns) noexcept;

  // True if the run-thread JNL_Connection is non-null (i.e. NJClient::Run
  // has a transport to encode-and-send into). Relaxed-equivalent — m_netcon
  // is a plain pointer set/cleared on the run thread; the UI thread observes
  // a one-bit liveness flag for diagnostic purposes only.
  bool IsNetConnected() const noexcept;

  unsigned int GetUserChannelCodec(int useridx, int channelidx);
  double GetUserSessionPos(int useridx, time_t *lastupdatetime, double *maxlen);
  const char *GetUserChannelState(int useridx, int channelidx, bool *sub=0, float *vol=0, float *pan=0, bool *mute=0, bool *solo=0, int *outchannel=0, int *flags=0);
  void SetUserChannelState(int useridx, int channelidx, bool setsub, bool sub, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo, bool setoutch=false, int outchannel=0);
  int EnumUserChannels(int useridx, int i); // returns <0 if out of channels. start with i=0, and go upwards

  int GetMaxLocalChannels() { return m_max_localch; }
  void DeleteLocalChannel(int ch);
  int EnumLocalChannels(int i);
  float GetLocalChannelPeak(int ch, int whichch=-1);
  void SetLocalChannelProcessor(int ch, void (*cbf)(float *, int ns, void *), void *inst);
  void GetLocalChannelProcessor(int ch, void **func, void **inst);
  void SetLocalChannelInfo(int ch, const char *name, bool setsrcch, int srcch, bool setbitrate, int bitrate, bool setbcast, bool broadcast, bool setoutch=false, int outch=0, bool setflags=false, int flags=0);
  const char *GetLocalChannelInfo(int ch, int *srcch, int *bitrate, bool *broadcast, int *outch=0, int *flags=0);
  void SetLocalChannelMonitoring(int ch, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo);
  int GetLocalChannelMonitoring(int ch, float *vol, float *pan, bool *mute, bool *solo); // 0 on success
  void NotifyServerOfChannelChange(); // call after any SetLocalChannel* that occur after initial connect

  void SetMetronomeChannel(int chidx) { 
    config_metronome_channel.store(chidx, std::memory_order_relaxed);
    m_metro_chidx=chidx; 
  } // chidx&255 is stereo pair index, add 1024 for mono only
  int GetMetronomeChannel() const { 
    return config_metronome_channel.load(std::memory_order_relaxed);
  }

  void SetRemoteChannelOffset(int offs) { m_remote_chanoffs = offs; }
  void SetLocalChannelOffset(int offs) { m_local_chanoffs = offs; }

  int IsASoloActive() { return m_issoloactive; }

  void SetLogFile(const char *name=NULL);

  // 15.1-08 M-01 + Codex M-7: pre-grow tmpblock so the audio thread never
  // reallocates it. ALSO enforces the MAX_BLOCK_SAMPLES contract from
  // 15.1-04 spsc_payloads.h: throws std::runtime_error if maxSamplesPerBlock
  // > jamwide::MAX_BLOCK_SAMPLES. JUCE prepareToPlay catches this and
  // surfaces it as a host-incompatibility error (the M-03 jassert in the
  // processor's processBlock then catches debug-build violations of the
  // host-claimed bound at audio time). Idempotent and safe to call from
  // every prepareToPlay (Prealloc only grows, never shrinks).
  void SetMaxAudioBlockSize(int maxSamplesPerBlock);

  void SetOggOutFile(FILE *fp, int srate, int nch, int bitrate=128);
  WaveWriter *waveWrite;


  void *LicenseAgreement_User;
  int (*LicenseAgreementCallback)(void *userData, const char *licensetext); // return TRUE if user accepts


  // messages you can send:
  // "MSG" "text"  - broadcast "text" to everybody
  // "PRIVMSG" "username" "text"  - send text to "username"
  void ChatMessage_Send(const char *parm1, const char *parm2, const char *parm3=NULL, const char *parm4=NULL, const char *parm5=NULL);

  // messages you can receive from this:
  // "MSG" "user" "text"   - message from user to everybody (including you!), or if user is empty, from the server
  // "PRIVMSG "user" "text"   - private message from user

  // usernames are not case sensitive, but message names ARE.

  // note that nparms is the MAX number of parms, you still can get NULL parms entries in there (though rarely)
  void (*ChatMessage_Callback)(void *userData, NJClient *inst, const char **parms, int nparms);
  void *ChatMessage_User;

  // -------------------------------------------------------------------------
  // Phase 20-02: H.264 send-side video state machine. NinjamZap-literal per
  // CONTEXT.md D-08 / D-09 / D-11 / D-13 / D-18 / D-20 + R4 H8 (atomic two-
  // uint64_t halves seqlock for the canonical audio_ch0_guid read).
  //
  // Threading model (one mutex held across the whole on_new_interval video
  // block — D-08 NinjamZap-literal):
  //   - audio thread (NJClient::on_new_interval) acquires m_video_cs, emits
  //     END/BEGIN/marker/SPS-PPS/END-at-deactivate, releases.
  //   - encoder thread (Plan 20-01 Openh264Encoder publishEncodedNal callback)
  //     calls NJClient::QueueVideoFrame which acquires m_video_cs, validates
  //     m_video_active && m_video_interval_open, calls RawDataSendWrite with
  //     [4B BE length][NAL] (COD-02), releases m_video_cs.
  //   - message thread calls NJClient::SetVideoBroadcastActive(bool) to
  //     toggle m_video_active under m_video_cs.
  //   - run thread calls NJClient::SetVideoChannel(chidx, fourcc) once at
  //     connect-up to register the video channel.
  //
  // SPS/PPS storage is protected by a NESTED m_video_spspps_cs taken inside
  // the m_video_cs critical section (D-03). The audio_ch0_guid 16 bytes in
  // the marker is sourced via readGuidSeqlock against Local_Channel atomic
  // two-uint64_t halves — TSan-clean by C++ memory model (R4 H8).
  // -------------------------------------------------------------------------
  void SetVideoChannel(int chidx, unsigned int fourcc);     // run-thread; connect-up
  void SetVideoBroadcastActive(bool active);                // message-thread; Broadcast button
  void QueueVideoFrame(const void* data, int len);          // encoder-thread; publishEncodedNal callback
  void SetVideoSPSPPS(const void* data, int len);           // encoder-thread; publishSpsPps callback

  // Plan 20-01 encoder reads this atomic counter relaxed to detect interval
  // boundaries (D-15). Audio thread bumps with release before the rest of
  // the video block runs, so the encoder's per-frame `eForceIntraFrame`
  // flag tracks audio-thread interval cadence with at most 1 frame of drift.
  std::atomic<uint64_t>* getAudioIntervalSeqPtr() noexcept { return &m_audio_interval_seq; }
  uint64_t GetAudioIntervalSeq() const noexcept {
    return m_audio_interval_seq.load(std::memory_order_relaxed);
  }
  // Plan 20-01 publishes encoder input-drop counter via this pointer; Plan
  // 20-03 reads at UAT close (analogue of m_block_queue_drops gate).
  std::atomic<uint64_t>* getEncoderInputDropsPtr() noexcept { return &m_encoder_input_drops_mirror; }
  uint64_t GetEncoderInputDropCount() const noexcept {
    return m_encoder_input_drops_mirror.load(std::memory_order_relaxed);
  }

#ifdef JAMWIDE_BUILD_TESTS
  // Plan 20-03 Task 1 part E: audio-thread budget probe. Wraps the
  // on_new_interval video block; CAS-updates the worst-case nanosecond
  // duration. UAT acceptance gate: <= 200,000 ns (200 µs) worst-case under
  // populated HD broadcast load. JAMWIDE_BUILD_TESTS-gated — the
  // steady_clock::now() call is a vDSO read on macOS (~50 ns) but accept it
  // as test-only instrumentation (NinjamZap-literal audio path otherwise).
  uint64_t GetOnNewIntervalVideoBlockWorstCaseNs() const noexcept {
    return m_on_new_interval_video_block_worst_case_ns.load(std::memory_order_relaxed);
  }
  void ResetOnNewIntervalVideoBlockWorstCaseNs() noexcept {
    m_on_new_interval_video_block_worst_case_ns.store(0, std::memory_order_relaxed);
  }

  // Plan 20-03 Task 1: test accessors for the lifecycle tests. The video
  // state machine's m_video_active / m_video_interval_open fields live
  // under m_video_cs; the tests read them via these accessors for the
  // T-20-03 lifecycle-ordering test (sub-test 3) without exposing the
  // mutex.
  bool GetVideoActiveForTest() const {
    WDL_MutexLock lock(const_cast<WDL_Mutex*>(&m_video_cs));
    return m_video_active;
  }
  bool GetVideoIntervalOpenForTest() const {
    WDL_MutexLock lock(const_cast<WDL_Mutex*>(&m_video_cs));
    return m_video_interval_open;
  }

  // Plan 20-03 Task 2 sub-test 6 (R4 M11 path 2): JAMWIDE_BUILD_TESTS-only
  // test hook that runs ONLY the video-interval-cleanup branch of the
  // production Disconnect path. The full Disconnect calls into m_users_cs /
  // m_remoteusers / m_netcon teardown which a unit test cannot stand up
  // without a real socket; this hook isolates the END-emit logic so the
  // test can assert it runs WITHOUT requiring a full Net_Connection mock.
  // Production Disconnect inserts the SAME block at the matching call site
  // before m_netcon teardown.
  void DisconnectVideoIntervalForTest();
#endif

  // -------------------------------------------------------------------------
  // Phase 14.3-02 + Phase 20-00 (D-19): codec-agnostic transport scaffolding
  // (RawData send-side).
  //
  // Send side:
  //   Public RawDataSendBegin / RawDataSendWrite — callable from ANY thread;
  //   internally serialized via WDL_PtrList<RawDataQueueItem> m_rawdata_sendq
  //   protected by WDL_Mutex m_rawdata_cs. Drain runs in NJClient::Run on the
  //   run thread (sole writer to m_netcon->Send — Landmine L2: src/core/
  //   netmsg.cpp:289). The Phase 14.3-02 SpscRing<RawDataItem, 64> substrate
  //   was replaced in Phase 20-00 because the HYBRID video-emission model
  //   needs multi-producer safety (audio thread + encoder thread). The
  //   replacement matches `ninjamzap-core/njclient.cpp:2047-2082` verbatim;
  //   drain matches `ninjamzap-core/njclient.cpp:1984-2040` verbatim.
  //
  // Receive side (forward reference to 14.3-03):
  //   When the wire-format DOWNLOAD_INTERVAL_BEGIN handler at
  //   src/core/njclient.cpp receives a fourcc that is neither
  //   NJ_ENCODER_FMT_TYPE nor NJ_ENCODER_FMT_FLAC AND RawData_Callback is
  //   registered, 14.3-03 dispatches eventType=0 (begin)/=1 (data)/=2 (end)
  //   to the callback below instead of routing into the Vorbis decoder.
  //
  // API-consistency tax: symbol names are verbatim from
  // ninjamzap-core/njclient.h:205-216 (StudlyCaps verb-after-noun, matching
  // ChatMessage_Send / SetEncoderFormat) so grep-discoverability against the
  // upstream reference holds.
  // -------------------------------------------------------------------------

  // eventType: 0 = begin, 1 = data, 2 = end (mirrors ninjamzap upstream)
  typedef void (*RawDataCallback)(void *userData, int eventType,
                                  const unsigned char *guid, unsigned int fourcc,
                                  const char *username, int chidx,
                                  const void *data, int dataLen);
  RawDataCallback RawData_Callback;
  void *RawData_User;

  // Both methods are callable from any thread. They allocate a single
  // RawDataQueueItem (and, for Write, ResizeOK the inner WDL_HeapBuf when
  // dataLen > 0), then take m_rawdata_cs and Add() the item to m_rawdata_sendq.
  // No bounded-capacity drops (WDL_PtrList is unbounded per D-19; NinjamZap-
  // literal). ResizeOK-failure is treated as a silent drop matching NinjamZap;
  // Phase 20-03 observability watches the unbounded queue growing.
  void RawDataSendBegin(unsigned char outGuid[16], unsigned int fourcc,
                        int chidx, int estsize);
  void RawDataSendWrite(const unsigned char guid[16], const void *data,
                        int dataLen, bool isEnd);

  // -------------------------------------------------------------------------
  // Phase 21 receive pipeline (Plan 21-01 Task 1). Verbatim port of
  // ninjamzap-core/njclient.h:345-414 + cpp:2124-2173 (D-14).
  //
  // Public so future plans (21-02 decoder, 21-03 distributor) and the
  // JAMWIDE_BUILD_TESTS test harness can resolve a VideoRecvState by key
  // and inspect its slot bytes for parsing / verification.
  //
  // One amendment (D-11): VideoRecvBuffer's ctor pre-allocates 4 MB so
  // subsequent WRITE-handler Resize() calls within the cap are realloc-
  // free (RT-safe accumulation on the run thread, bounded memcpy cost
  // for audio-thread copyFrom under D-16).
  //
  // Wire-format contract for VideoRecvBuffer (codex review Cluster 6):
  //
  //   `data`             - concatenated frame payloads. The 4-byte BE OUTER length
  //                        prefix that the WRITE handler reads off the wire is
  //                        CONSUMED by the run-thread accumulator and DOES NOT appear
  //                        in `data`. Each frame's payload bytes appear back-to-back.
  //
  //   `frameOffsets[i]`  - byte offset INTO `data` of frame `i`'s FIRST payload byte.
  //                        `frameOffsets[i+1] - frameOffsets[i]` is frame `i`'s payload
  //                        size; `frameOffsets[frameCount]` == data.GetSize().
  //
  //   Frame payload layouts (each frame, after the outer prefix is stripped):
  //
  //     24-byte marker (first frame when frameCount == 1, payload size == 20 bytes):
  //         [4B BE sender_seq][16B audio_ch0_guid]
  //         The OUTER 4B BE prefix value for the marker frame MUST equal 20
  //         (decimal). Phase 20 commit 6d23b5c lost cycles to truncating the marker
  //         to 20 bytes on the wire (missing the outer prefix); this contract is
  //         the regression guard. Reference:
  //         /Users/cell/dev/ninjamzap-core/tests/video-sync/harness/TestClient.cpp:120-176
  //         (sendVideoFrame + sendFakeSPSPPS).
  //
  //     SPS/PPS chunk (when present): no Annex-B start codes; Annex-B wrap is the
  //         decoder's job (Plan 21-02 sendAnnexB_ helper):
  //         [2B BE sps_len][SPS_NAL_bytes][2B BE pps_len][PPS_NAL_bytes]
  //
  //     Per-frame NAL chunk (common case): single NAL unit, no prefix, no start code:
  //         [NAL_bytes]
  //
  // This contract is the AUTHORITY for the parser in Plan 21-02
  // `Openh264Decoder::parseSlotAndFeed_`. The contract is unit-tested in Plan 21-01
  // Task 3 (test_marker_parse_extracts_guid_and_seq) and Plan 21-02 Task 3
  // (test_pushSlotView_full_ninjamzap_bytes - codex Cluster 7).
  struct VideoRecvBuffer {
    WDL_HeapBuf data;
    WDL_TypedBuf<int> frameOffsets;
    int frameCount;
    char username[256];
    unsigned char guid[16];
    unsigned int fourcc;
    int chidx;
    int interval_seq; // receiver's interval counter at BEGIN time
    unsigned char audio_guid[16]; // sender's audio ch0 GUID for this interval (from marker chunk, zero=no marker)
    bool active;
    // Multi-write reassembly: when a logical frame is split across raw-data WRITEs by the
    // sender's MAX_ENC_BLOCKSIZE chunker, we accumulate bytes here until the frame is
    // complete. `pending_remaining` is bytes still expected before the in-progress frame
    // is finalized (frameCount++ happens at completion, not on every WRITE).
    int pending_remaining;
    int sender_seq; // sender's m_sync_interval_cnt for this interval (from 24B marker, -1=unknown)
    VideoRecvBuffer() : frameCount(0), fourcc(0), chidx(0), interval_seq(-1), active(false), pending_remaining(0), sender_seq(-1) {
      username[0] = 0; memset(guid, 0, 16); memset(audio_guid, 0, 16);
      // Phase 21 D-11: pre-allocate 4 MB so subsequent WRITE-handler Resize()
      // calls inside the WRITE accumulation never realloc. Resize(0, false)
      // shrinks the LOGICAL size to 0 without releasing the underlying
      // capacity (WDL_HeapBuf::Resize with resizedown=false: line 132-203 of
      // wdl/heapbuf.h - the alloc-size-change branch is only entered when
      // newsize > m_alloc OR newsize < resizedown_under, and resizedown_under
      // is 0 here, so the realloc branch is skipped and only m_size is
      // updated). Verified at Plan 21-01 Task 1 ack.
      data.Resize(4 * 1024 * 1024, true);
      data.Resize(0, false);
    }
    void reset() { data.Resize(0); frameOffsets.Resize(0); frameCount = 0; fourcc = 0; chidx = 0; interval_seq = -1; active = false; pending_remaining = 0; sender_seq = -1; username[0] = 0; memset(guid, 0, 16); memset(audio_guid, 0, 16); }
    void copyFrom(const VideoRecvBuffer &src) {
      fourcc = src.fourcc; chidx = src.chidx; active = src.active; frameCount = src.frameCount;
      memcpy(username, src.username, sizeof(username));
      memcpy(guid, src.guid, 16);
      int sz = src.data.GetSize();
      data.Resize(sz, false);
      if (sz > 0) memcpy(data.Get(), src.data.Get(), sz);
      frameOffsets.Resize(src.frameCount, false);
      if (src.frameCount > 0) memcpy(frameOffsets.Get(), src.frameOffsets.Get(), src.frameCount * sizeof(int));
      interval_seq = src.interval_seq;
      memcpy(audio_guid, src.audio_guid, 16);
      pending_remaining = src.pending_remaining;
      sender_seq = src.sender_seq;
    }
  };

  // Per-user video receive state.
  // Pipeline: accumulating (during interval download) -> next (after start/END) ->
  // pending (1-swap defer to align with audio output) -> playing. The pending slot adds
  // exactly one swap of latency so the video's first frame appears at the same moment
  // the matching audio becomes audible (audio's natural decoder/output-buffer lag is
  // ~1 interval). Going to 2 slots overshoots by one interval ("video se ve tarde").
  struct VideoRecvState {
    VideoRecvBuffer accumulating;
    VideoRecvBuffer next;
    VideoRecvBuffer pending; // matched at SWAP N, moves to playing at SWAP N+1
    VideoRecvBuffer playing;
    int frame_idx;
    int expected_frames;
    bool append_active;
    bool append_to_next;
    bool append_to_pending; // routing for late WRITEs when video is held in `pending` waiting for SWAP+1 promote
    unsigned char append_guid[16];
    // Stable identifiers - set at creation, never reset. accumulating's username/chidx
    // can be cleared by reset(), so we can't rely on those for stream lookup.
    char stream_username[256];
    int stream_chidx;
    char key[280]; // "username:chidx"
    int  empty_count;           // consecutive SWAPs with no video data
    int  hold_count;            // consecutive SWAPs where GUID mismatch held video
    unsigned char prev_ds_guid[16]; // ds->guid from the previous SWAP - video marker is 1 SWAP behind ds
    bool synced;                // true once we have aligned at least once via DS or PREV match
    int  last_played_sender_seq; // sender_seq of the last interval we played (-1 if never)
    unsigned char last_played_audio_guid[16]; // audio_guid of the last interval we played
    int  drop_resync_count;     // diagnostic: number of force-resyncs (HOLD cap exceeded)

    // Plan 21-02 Task 1 (D-09 / D-10 lazy lifecycle + codex Cluster 2 Option A):
    // VideoRecvState owns the 4-slot snapshot ring + integer-index SPSC + producer
    // sequence counter that the per-peer decoder thread consumes. Plan 21-03 lazy-
    // constructs `decoder` on first H264 BEGIN and registers `sink` via the
    // distributor; Plan 21-02 leaves both null and only wires the audio-thread
    // push path so the decoder, once instantiated, will immediately observe the
    // first playing slot.
    std::array<jamwide::VideoRecvSlotSnapshot, 4>     decoderSlots;          // 4× 4 MB owned
    jamwide::SpscRing<int, 4>                          decoderSlotIndexQ;     // audio→decoder
    std::atomic<int>                                   nextDecoderSlotFillIndex{0};
    std::atomic<std::uint64_t>                         decoderProducerSeq{0}; // codex Cluster 1
    // std::shared_ptr (NOT unique_ptr) so the type-erased deleter captured at
    // make_shared time obviates the need for the full Openh264Decoder type at
    // VideoRecvState's destruction point. njclient lib (which doesn't link
    // juce_graphics) can still destroy VideoRecvState without seeing the
    // decoder's full type. Plan 21-03's distributor TU (which DOES link JUCE)
    // creates the decoder via std::make_shared<Openh264Decoder>(...); the
    // captured deleter knows how to call ~Openh264Decoder.
    std::shared_ptr<jamwide::Openh264Decoder>          decoder;               // Plan 21-03 lazy
    jamwide::PeerVideoSink*                            sink = nullptr;        // Plan 21-03 owns

    // Plan 21-03 Task 2 (codex Cluster 4 two-phase lazy startup): Phase 1
    // sets this flag under m_video_recv_cs (microseconds — no allocation,
    // no avcodec_open2, no thread start). Phase 2 runs OUTSIDE the mutex
    // on the run thread and completes the heavy work: construct decoder +
    // sink + register with distributor + avcodec_open2 + start thread.
    // Phase 3 re-acquires the mutex briefly to install decoder + sink
    // pointers IF the VideoRecvState still exists (peer may have left
    // between Phase 1 and Phase 2). Idempotency: helpers early-return if
    // decoder is non-null OR decoder_startup_needed is true.
    bool decoder_startup_needed = false;

    // Plan 21-02 Task 1: ctor body lives in njclient.cpp (zero-inits scalars
    // the previous inline initializer list zero-initted). The dtor is
    // declared and defaulted inline (= default below) — std::shared_ptr's
    // type-erased deleter (W-2 resolution) handles the decoder cleanup
    // without requiring the full Openh264Decoder type in njclient.cpp's
    // translation unit.
    VideoRecvState();
    ~VideoRecvState() = default;
  };

  // Public accessors — production code must hold m_video_recv_cs across any
  // dereference (helpers themselves do NOT take the lock). The four private
  // state-machine helpers (handleVideoRecvBegin_ / handleVideoRecvWrite_ /
  // handleVideoRecvEnd_ / runVideoReceiveBlock_) DO take the lock internally
  // and are declared in the protected: section.
  VideoRecvState *findVideoStream(const char *username, int chidx);
  VideoRecvState *findOrCreateVideoStream(const char *username, int chidx);
  VideoRecvState *findVideoStreamByGUID(const unsigned char *guid);
  void removeVideoStream(const char *username, int chidx);

  // Phase 21-03 Task 1: injection of the receive-side distributor by
  // JamWideJuceProcessor at construction time. The pointer lifetime is
  // managed by the processor — see ~JamWideJuceProcessor dtor order (codex
  // Cluster 3 reversed: client.reset() runs BEFORE remoteFrameDistributor.reset()
  // so by the time the distributor itself is destroyed all VideoRecvStates
  // have already run the four-step shutdown protocol and removed their
  // sinks from the distributor's map).
  //
  // Plan 21-03 Task 2 wires the BEGIN-handler lazy-startup path + user-leave
  // four-step shutdown protocol that uses this pointer.
  void SetRemoteFrameDistributor(jamwide::JamWideRemoteFrameDistributor* d) noexcept;

  // Phase 21-03 Task 2: callback ops table. njclient.cpp is in the
  // `njclient` static library which does NOT link juce_graphics /
  // juce_events. Calling methods on JamWideRemoteFrameDistributor or
  // Openh264Decoder directly from njclient.cpp would force those modules
  // into njclient's link dependencies, breaking the tests that don't pull
  // JUCE. The processor passes function pointers via this table; tests
  // leave it null and the lazy-startup / shutdown helpers degrade to
  // no-ops (matching the m_remote_frame_distributor==nullptr branch).
  struct VideoDistributorOps {
    // Lazy-startup factory: distributor->createDecoderAndSinkForPeer.
    // Returns an opaque pointer that is a `new`-ed
    // std::shared_ptr<Openh264Decoder> heap object on success, OR nullptr
    // on failure. The opaque pointer is consumed by install_decoder OR
    // destroy_decoder. Sink pointer is returned via *out_sink_ptr.
    void* (*create_decoder)(jamwide::JamWideRemoteFrameDistributor* dist,
                             const char* username, int chidx,
                             int width, int height,
                             std::array<jamwide::VideoRecvSlotSnapshot, 4>* slotRing,
                             jamwide::SpscRing<int, 4>*                     slotIndexQ,
                             std::atomic<std::uint64_t>*                    producerSeq,
                             jamwide::PeerVideoSink** out_sink_ptr) = nullptr;
    // Install decoder onto vs->decoder (move-assigns from the opaque heap
    // shared_ptr; deletes the heap shared_ptr after move). The opaque
    // pointer is consumed.
    void  (*install_decoder)(void* opaque_decoder, VideoRecvState* vs) = nullptr;
    // Destroy a decoder produced by create_decoder without installing
    // (e.g. peer left between Phase 1 and Phase 2 completion). Deletes
    // the heap shared_ptr.
    void  (*destroy_decoder)(void* opaque_decoder) = nullptr;
    // Step 4 of the shutdown protocol — distributor->removeSink.
    void  (*remove_sink)(jamwide::JamWideRemoteFrameDistributor* dist,
                          const char* username, int chidx) = nullptr;
    // User-leave teardown: runs steps 1, 2, 4 (decoder->close, setSink(nullptr),
    // removeSink). Moves the shared_ptr<Openh264Decoder> out of vs->decoder
    // (vs->decoder is reset to null on return).
    void  (*tear_down_decoder)(jamwide::JamWideRemoteFrameDistributor* dist,
                                VideoRecvState* vs,
                                const char* username, int chidx) = nullptr;
  };
  void SetVideoDistributorOps(const VideoDistributorOps& ops) noexcept;

  // ---------------------------------------------------------------------
  // Phase 20-00 + R3 MF4: queue observability surface for Plan 20-03.
  //
  // High-water-mark + mutex-contention counter + total-enqueue counter
  // are declared here unconditionally. Plan 20-03's UAT acceptance gate
  // reads them; Plan 20-03 does NOT add any of them. The three counters
  // are bumped under m_rawdata_cs inside RawDataSendBegin and
  // RawDataSendWrite (high-water via CAS, contention sampled before the
  // Enter(), total-enqueue once per successful Add).
  //
  // Relaxed memory ordering on all three (observability only — not used
  // for inter-thread synchronization).
  // ---------------------------------------------------------------------
  uint64_t GetRawDataSendQueueHighWaterMark() const noexcept {
      return m_rawdata_sendq_high_water_mark.load(std::memory_order_relaxed);
  }
  uint64_t GetRawDataMutexContentionCount() const noexcept {
      return m_rawdata_cs_contention_count.load(std::memory_order_relaxed);
  }
  uint64_t GetRawDataSendQueueTotalEnqueueCount() const noexcept {
      return m_rawdata_sendq_total_enqueues.load(std::memory_order_relaxed);
  }

#ifdef JAMWIDE_BUILD_TESTS
  // Test-only public wrappers for the file-static is_video_fourcc helper +
  // observable drain/chunking. JAMWIDE_BUILD_TESTS is defined on the njclient
  // target at CMakeLists.txt:137 when tests are built; production builds do
  // not see these. See tests/test_rawdata_send.cpp for the consumers.
  bool IsVideoFourcc(unsigned int fcc) const;
  // Destructive drain: hands ownership of every queued RawDataQueueItem* back
  // to the caller (which must `delete` each pointer when done). Each item's
  // by-value WDL_HeapBuf is freed automatically when the item is deleted.
  void DrainRawDataSendQueueForTest(std::vector<RawDataQueueItem*>& out);
  // Chunking helper used by the run-thread drain block; exposed for test
  // observability so test_payload_chunking_at_max_enc_blocksize can verify
  // the chunk split without needing a real Net_Connection mock.
  static void ChunkRawDataItem(const RawDataQueueItem& item,
                               void (*emit)(void* ctx, const unsigned char guid[16],
                                            const void* data, int dataLen, int flags),
                               void* ctx);

  // 14.3-03: receive-path dispatch test helpers. These expose the
  // BEGIN/WRITE case body (extracted from NJClient::Run's message switch)
  // so unit tests can verify the dispatch logic in-process without a real
  // socket or Net_Connection mock. Take primitive fields (not the
  // mpb_server_download_interval_begin / write classes) so njclient.h does
  // not have to include mpb.h.
  //
  // AddTestRemoteUser allocates a RemoteUser, populates one channel (chidx
  // = ch), sets channels[ch].flags = chflags (0 by default — subscribed,
  // not session-mode, not flags&4 silence-pad), submask, chanpresentmask,
  // pushes onto m_remoteusers. Returns the slot used for findRemoteUserSlot.
  // GetRawDataDownloadCount + GetMDownloadsCount expose the WDL_PtrList
  // sizes so tests can assert which dispatch branch fired.
  // ClearTestRemoteUsers empties the list (frees each entry) so tests can
  // re-use the same NJClient across scenarios.
  void AddTestRemoteUser(const char* name, int ch, int chflags);
  void ClearTestRemoteUsers();
  void DispatchTestServerDownloadIntervalBegin(const unsigned char guid[16],
                                               unsigned int fourcc, int chidx,
                                               const char* username, int estsize);
  void DispatchTestServerDownloadIntervalWrite(const unsigned char guid[16],
                                               const void* audio_data,
                                               int audio_data_len, int flags);
  int GetRawDataDownloadCount() const { return m_rawdata_downloads.GetSize(); }
  int GetMDownloadsCount()      const { return m_downloads.GetSize(); }

  // Plan 20-02 Task 3: drive the audio-thread on_new_interval block from the
  // test main thread without spinning up a real AudioProc loop. This is the
  // same gating idiom used by DrainRawDataSendQueueForTest above.
  void RunOneIntervalForTest();

  // Plan 20-02 Task 2: opaque handle around a freshly-constructed
  // Local_Channel so tests can exercise the per-channel seqlock helpers
  // (R3 MF1 + R4 H8 atomic two-uint64_t halves) without exposing
  // Local_Channel's full definition in the public header. The handle is
  // an opaque type — the test uses a raw pointer; call
  // DestroyTestLocalChannelHandle to release it. (Using unique_ptr here
  // would need the full definition in scope of every test's destructor.)
  struct TestLocalChannelHandle;
  static TestLocalChannelHandle* CreateTestLocalChannelHandle();
  static void DestroyTestLocalChannelHandle(TestLocalChannelHandle* h);
  static void TestWriteGuidSeqlock(TestLocalChannelHandle& h,
                                   const unsigned char in16[16]);
  static bool TestReadGuidSeqlock(TestLocalChannelHandle& h,
                                  unsigned char out16[16]);
  // Force the seqlock counter to odd parity (writer-mid-update simulation)
  // for the retry-cap-fallback test path. After this call, every reader
  // attempt will spin to the retry cap and return false + zero-fill.
  static void TestForceOddParityForTest(TestLocalChannelHandle& h);
  static void TestRestoreEvenParityForTest(TestLocalChannelHandle& h);

  // ---------------------------------------------------------------------
  // Plan 21-01 Task 1 (codex Cluster 5): thin forwarder helpers exposing
  // the four single-source state-machine helpers + a few VideoRecvState
  // inspection / fixture helpers for unit tests. The Dispatch* functions
  // are ONE-LINE forwarders that call handleVideoRecvBegin_ /
  // handleVideoRecvWrite_ / handleVideoRecvEnd_ — they do NOT inline a
  // copy of the state-machine logic.
  // ---------------------------------------------------------------------
  void DispatchTestVideoRecvBegin(const unsigned char guid[16], unsigned int fourcc,
                                   const char *username, int chidx);
  void DispatchTestVideoRecvWrite(const unsigned char guid[16],
                                   const void *data, int dataLen, bool isEnd);
  void DispatchTestVideoRecvEnd(const unsigned char guid[16]);
  void RunOnNewIntervalReceiveBlockForTest();
  VideoRecvState* GetVideoStreamForTest(const char *username, int chidx);

  // Populate m_remoteuser_mirror[slot] so the GUID-pair decision tree in
  // runVideoReceiveBlock_ can find a "remote user" matching the
  // VideoRecvState's stream_username. Tests own the DecodeState lifetime;
  // ClearTestRemoteUserMirror frees the test-allocated DecodeStates and
  // resets the mirror slot.
  void AddTestRemoteUserMirrorWithDs(int slot, const char *username, int channel,
                                      const unsigned char guid[16]);
  void ClearTestRemoteUserMirror(int slot);

  // Run the user-leave video-state reset (mirrors the inline block at the
  // m_remoteusers Delete site). Tests call this to verify
  // test_user_leave_resets_video_sync_state.
  void DispatchTestUserLeaveForVideoReset(const char *username);

  // ---------------------------------------------------------------------
  // Plan 21-01 Task 2 (codex Cluster 1): runVideoReceiveBlock_ timing
  // instrumentation accessors. Production builds DO NOT see these — entire
  // JAMWIDE_BUILD_TESTS block compiles out.
  //
  // getRunVideoReceiveBlockMaxNanosForTest returns the worst-case
  // nanosecond duration observed across all invocations of
  // runVideoReceiveBlock_ since the last reset (or NJClient construction).
  // getRunVideoReceiveBlockLastPeerCountForTest returns the
  // m_video_streams.GetSize() observed during the most-recent invocation.
  // ---------------------------------------------------------------------
  std::uint64_t getRunVideoReceiveBlockMaxNanosForTest() const noexcept;
  int           getRunVideoReceiveBlockLastPeerCountForTest() const noexcept;
  void          resetRunVideoReceiveBlockTimingForTest() noexcept;
#endif

  // -- Phase 14.2: Instamode latency measurement (VID-13) --
  // State machine: IDLE -> INSTA_CAPTURED -> MEASURED -> CONSUMED
  // All state lives HERE in NJClient (single owner).
  // Audio thread writes; run thread reads.
  enum InstaMeasState : int { kInstaMeasIdle = 0, kInstaCapured = 1, kInstaMeasured = 2, kInstaConsumed = 3 };

  std::atomic<int>       insta_meas_state_{kInstaMeasIdle};
  std::atomic<int64_t>   insta_t_insta_ms_{0};     // wall-clock ms: first instamode data mixed
  std::atomic<int64_t>   insta_t_interval_ms_{0};  // wall-clock ms: regular channel ds advance
  std::atomic<uintptr_t> insta_meas_user_ptr_{0};  // RemoteUser* identity for cross-timestamp matching

  void resetInstaMeasurement()
  {
      insta_meas_state_.store(kInstaMeasIdle, std::memory_order_relaxed);
      insta_t_insta_ms_.store(0, std::memory_order_relaxed);
      insta_t_interval_ms_.store(0, std::memory_order_relaxed);
      insta_meas_user_ptr_.store(0, std::memory_order_relaxed);
  }

  // set these if you want to mix multiple channels into the output channel
  // return 0 if you want the default behavior
  int (*ChannelMixer)(void *userData, float **inbuf, int in_offset, int innch, int chidx, float *outbuf, int len);
  void *ChannelMixer_User;

  WDL_Mutex m_remotechannel_rd_mutex;

  bool is_likely_lobby() const {
    return !m_max_localch && !m_remoteusers.GetSize();
  }

  int GetSampleRate() const { return m_srate; }

  int find_unused_output_channel_pair() const;

  // 15.1-05 CR-05/06/07: deferred-delete drain. Called by run thread
  // (NinjamRunThread::run) at 20ms cadence and once at shutdown. Drains
  // the deferred-delete SPSC queue and runs ~DecodeState() on each pointer
  // off the audio thread.
  void drainDeferredDelete();

  // 15.1-05 + Codex M-8: phase-close verification reads this. MUST be 0
  // after UAT. Non-zero == architectural defect (queue undersized for
  // workload). 15.1-10 phase verification asserts this counter == 0.
  uint64_t GetDeferredDeleteOverflowCount() const noexcept {
      return m_deferred_delete_overflows.load(std::memory_order_relaxed);
  }

  // 15.1-07b CR-09/CR-10 + Codex M-8: BlockRecord SPSC overflow counter.
  // Audio thread bumps when the producer-side try_push (broadcast or wave)
  // fails because the run-thread consumer hasn't drained yet. 15.1-10 phase
  // verification asserts this == 0 post-UAT. Non-zero == architectural
  // defect (ring undersized for the worst-case run-thread drain latency).
  // Relaxed semantics — observability counter, no synchronization-with-other-state.
  uint64_t GetBlockQueueDropCount() const noexcept {
      return m_block_queue_drops.load(std::memory_order_relaxed);
  }

  // Phase 20-00 (D-19): GetRawDataSendQueueOverflowCount() +
  // GetRawDataSendQueueDiscardCount() accessors REMOVED — the SPSC capacity
  // bound + Pattern-C disconnect-drain branch they reported on no longer
  // exist. The replacement observability surface
  // (GetRawDataSendQueueHighWaterMark / GetRawDataMutexContentionCount /
  // GetRawDataSendQueueTotalEnqueueCount) is declared in the public RawData
  // block above.

  // 15.1-07b CR-09: drain per-channel mirror block_q rings on the run thread,
  // forwarding their BlockRecord payloads into the legacy lc->m_bq.AddBlock
  // path so the existing encoder consumer at NJClient::Run() lines 1626-1840
  // remains untouched. Producer = audio thread (process_samples / on_new_interval
  // try_push); consumer = run thread (this method). Called from
  // NJClient::Run() at the top of the upload loop AND from NinjamRunThread.cpp
  // (token call site to satisfy the plan's juce/NinjamRunThread.cpp grep gate).
  void drainBroadcastBlocks();

  // 15.1-07b CR-10: drain m_wave_block_q on the run thread, forwarding into
  // the legacy m_wavebq->AddBlock path so the existing wave drain at
  // NJClient::Run() line 1073 remains untouched.
  void drainWaveBlocks();

protected:
  double output_peaklevel[2];

  void _reinit();

  void makeFilenameFromGuid(WDL_String *s, unsigned char *guid);

  void updateBPMinfo(int bpm, int bpi);
  void process_samples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, int offset, int justmonitor, bool isPlaying, bool isSeek, double cursessionpos);
  void on_new_interval();
  // 15.1-03 H-02 (Codex per-plan delta): writeUserChanLog declaration removed.
  // All audio-thread callers eliminated; body deleted in njclient.cpp. Restore via
  // SPSC-mediated logging path if future need arises — do NOT add an in-place audio call.

  void writeLog(const char *fmt, ...);

  WDL_String m_errstr;

  WDL_String m_workdir;
  int m_status;
  int m_max_localch;
  int m_connection_keepalive;
  FILE *m_logFile;
#ifndef NJCLIENT_NO_XMIT_SUPPORT
  FILE *m_oggWrite;
  I_NJEncoder *m_oggComp;
#endif

  WDL_String m_user, m_pass, m_host;
  unsigned char m_auth_challenge[8] = {};  // saved for encryption key derivation (Phase 15)

  int m_in_auth;
  // 15.1-02: m_bpm, m_bpi, m_beatinfo_updated promoted to std::atomic<int>
  // (declared above near cached_status). Removed from this block.
  int m_audio_enable;
  int m_srate;
  int m_userinfochange;
  int m_issoloactive;
  // 15.1-03 H-01: m_debug_logged_remote member removed; gated the deleted
  // JAMWIDE_DEV_BUILD audio-path fopen block.

  unsigned int m_session_pos_ms,m_session_pos_samples; // samples just keeps track of any samples lost to precision errors

  int m_loopcnt;
  int m_active_bpm, m_active_bpi;
  int m_interval_length;
  // 15.1-02 (AUDIT line 421): m_interval_pos promoted to std::atomic<int> (declared above).
  int m_metronome_state, m_metronome_tmp,m_metronome_interval;
  double m_metronome_pos;

  int m_metro_chidx, m_remote_chanoffs, m_local_chanoffs;

  DecodeState *start_decode(unsigned char *guid, int chanflags, unsigned int fourcc, DecodeMediaBuffer *decbuf);

  BufferQueue *m_wavebq;

  WDL_PtrList<Local_Channel> m_locchannels;

  // 15.1-07a CR-01: mixInChannel takes a STABLE SLOT into m_remoteuser_mirror,
  // not a RemoteUser*. The audio thread reads ONLY mirror fields; no
  // dereference of run-thread-owned RemoteUser / RemoteUser_Channel objects
  // (Codex HIGH-2). DecodeState* pointer-shuffle operates entirely on the
  // mirror's RemoteUserChannelMirror::ds / next_ds.
  void mixInChannel(int slot, int chanidx,
                    bool muted, float vol, float pan, float **outbuf, int out_channel,
                    int len, int srate, int outnch, int offs, double vudecay, bool isPlaying, bool isSeek, double playPos);

  WDL_Mutex m_users_cs, m_locchan_cs, m_log_cs, m_misc_cs;
  Net_Connection *m_netcon;
  WDL_PtrList<RemoteUser> m_remoteusers;
  WDL_PtrList<RemoteDownload> m_downloads;

  // 14.3-02 + 14.3-03 prep: per-stream RawData receive tracker. POD struct
  // verbatim from ninjamzap-core/njclient.h:323-330. Architectural invariant
  // (14.3-03 will enforce): m_rawdata_downloads is touched ONLY inside
  // NJClient::Run's MESSAGE_SERVER_DOWNLOAD_INTERVAL_{BEGIN,WRITE} branches.
  // No audio-thread access — no mirror needed. Run-thread-only structure
  // mirroring the m_downloads pattern above. This plan (14.3-02) declares
  // the type and the empty list; 14.3-03 lands the population + dispatch
  // logic at njclient.cpp:2148 and the WRITE-handler.
  struct RawDataDownloadTracker {
      unsigned char guid[16];
      unsigned int  fourcc;
      char          username[256];
      int           chidx;
  };
  WDL_PtrList<RawDataDownloadTracker> m_rawdata_downloads;

  // Phase 21 (Plan 21-01 Task 1): per-peer video receive state list +
  // mutex. State struct definitions are in the public section above (so
  // Plan 21-02 decoder + JAMWIDE_BUILD_TESTS test harness can name them).
  WDL_PtrList<VideoRecvState> m_video_streams;
  WDL_Mutex m_video_recv_cs;

  // Phase 21 (Plan 21-01 Task 2 - codex Cluster 5): single-source private
  // state-machine helpers. Production dispatchers, test dispatchers, AND
  // the audio-thread on_new_interval video receive block all call into
  // these four helpers. The helpers each acquire m_video_recv_cs
  // internally - callers must NOT pre-acquire.
  void handleVideoRecvBegin_(const unsigned char guid[16], unsigned int fourcc,
                              const char *username, int chidx);
  void handleVideoRecvWrite_(const unsigned char guid[16],
                              const void *data, int dataLen);
  void handleVideoRecvEnd_(const unsigned char guid[16]);
  void runVideoReceiveBlock_();

  // Plan 21-02 Task 2 (codex Cluster 1 + Cluster 2 Option A): push the
  // current vs->playing slot bytes into the per-peer decoder via the
  // VideoRecvState-owned snapshot ring. Called from runVideoReceiveBlock_
  // at THREE sites (STAGE-1 promote, PREV/NONE-match immediate-play,
  // accumulating-fallback). Audio-thread-safe: memcpys bytes + offsets
  // into vs->decoderSlots[fillIdx], pushes fillIdx onto the SPSC,
  // bumps decoderProducerSeq.fetch_add(1, release). NO WaitableEvent::
  // signal — the decoder polls producer_seq with 15 ms timed wait.
  // No-op if vs->decoder is null (Plan 21-03 lazy-constructs the decoder
  // on first H264 BEGIN; Plan 21-02 leaves it null in production).
  void pushPlayingSnapshotToDecoder_(VideoRecvState* vs);

  // Phase 21-03 Task 1: receive-side distributor pointer injected by
  // JamWideJuceProcessor (see SetRemoteFrameDistributor in the public
  // section). nullable — when null, Plan 21-03 Task 2's lazy-startup +
  // user-leave shutdown blocks degrade to no-ops (the audio-thread
  // snapshot push code already no-ops when vs->decoder is null).
  jamwide::JamWideRemoteFrameDistributor* m_remote_frame_distributor = nullptr;

  // Phase 21-03 Task 2: function-pointer table injected by JamWideJuceProcessor.
  // Decouples njclient.cpp from juce_graphics/juce_events/libavcodec linkage
  // (see VideoDistributorOps doc comment above). null in tests = no-op.
  VideoDistributorOps m_video_distributor_ops{};

  // Phase 21-03 Task 2 (codex Cluster 4 — two-phase lazy decoder startup):
  // Phase 1 (under m_video_recv_cs in BEGIN): set vs->decoder_startup_needed
  // = true. NO allocation. NO avcodec_open2. NO thread start. Microseconds.
  // The audio thread's runVideoReceiveBlock_ no longer blocks against
  // heavy lazy-startup work — the only mutex hold here is the flag set.
  void ensureVideoDecoderForPeerLater_(VideoRecvState* vs, int width, int height);

  // Phase 2+3 (OUTSIDE m_video_recv_cs on the run thread): does the heavy
  // lifting — captures stable references under a brief mutex re-acquire,
  // constructs decoder + sink + registers with distributor, calls
  // avcodec_open2, starts the decoder thread. Phase 3 re-acquires the
  // mutex BRIEFLY to install the pointers if the VideoRecvState still
  // exists (peer may have left between Phase 1 and Phase 2). Idempotent
  // against burst BEGINs (early-returns if decoder is already non-null OR
  // the flag has been cleared by a racer).
  void completeVideoDecoderStartup_(const char *username, int chidx,
                                     int width, int height);

  WDL_HeapBuf tmpblock;

  // 15.1-05 CR-05/06/07: deferred-delete queue. Audio thread try_pushes
  // DecodeState*; run thread drainDeferredDelete() pops and runs ~DecodeState()
  // off-thread. Capacity 256 absorbs a worst-case interval-boundary burst
  // (peers x channels x 2 next_ds slots) per spsc_payloads.h DEFERRED_DELETE_CAPACITY.
  jamwide::SpscRing<DecodeState*, jamwide::DEFERRED_DELETE_CAPACITY> m_deferred_delete_q;

  // 15.1-05 + Codex M-8: overflow counter. Audio thread increments on try_push
  // failure (queue full); 15.1-10 phase verification asserts this is 0 after UAT.
  // Non-zero == architectural defect (queue undersized for workload). Relaxed
  // semantics are sufficient — this is an observability counter, no synchronization
  // dependency on it.
  std::atomic<uint64_t> m_deferred_delete_overflows{0};

  // 15.1-06 CR-02: audio-thread mirror of local-channel state. Replaces
  // m_locchan_cs.Enter/Leave at process_samples and on_new_interval.
  // Indexed by Local_Channel::channel_idx (which is bounded to
  // 0..MAX_LOCAL_CHANNELS-1 by NJClient::SetLocalChannelInfo callers).
  // Owned and mutated EXCLUSIVELY by the audio thread (drainLocalChannelUpdates
  // applies queued mutations at the top of AudioProc).
  LocalChannelMirror m_locchan_mirror[MAX_LOCAL_CHANNELS];

  // 15.1-06 CR-02: state-update queue. Run-thread mutators
  // (SetLocalChannelInfo / DeleteLocalChannel / SetLocalChannelMonitoring)
  // try_push variant records here; audio thread drains at top of AudioProc
  // via drainLocalChannelUpdates(). Capacity 32 matches MAX_LOCAL_CHANNELS;
  // local-channel mutations are UI-paced (≤ ~10 Hz worst case under fader
  // storms), so 32 is generous.
  jamwide::SpscRing<jamwide::LocalChannelUpdate, 32> m_locchan_update_q;

  // 15.1-06 deviation #2 (cbf processor): SetLocalChannelProcessor
  // publishes a separate update so audio-thread mirror.cbf/cbf_inst stay
  // accurate. Declared inline here (NOT in spsc_payloads.h, which is
  // FINAL per Wave-0 Codex M-9). Trivially copyable POD.
  struct LocalChannelProcessorUpdate {
      int channel = 0;
      void (*cbf)(float* /*buf*/, int /*ns*/, void* /*inst*/) = nullptr;
      void* cbf_inst = nullptr;
  };
  jamwide::SpscRing<LocalChannelProcessorUpdate, 16> m_locchan_processor_q;

  // 15.1-06 + Codex HIGH-3: drain-generation counter.
  //
  // The audio thread bumps this once per AudioProc (after
  // drainLocalChannelUpdates returns). The run thread reads it to know when
  // its PUBLISHED LocalChannelRemovedUpdate has been observed by the audio
  // thread (and only THEN is it safe to enqueue the canonical Local_Channel*
  // onto the deferred-delete queue).
  //
  // Release-store on the audio side synchronizes with acquire-load on the run
  // side (DeleteLocalChannel / drainLocalChannelDeferredDelete) — ensures the
  // audio thread's mirror update (active=false) is visible before the run
  // thread proceeds to the canonical free.
  std::atomic<uint64_t> m_audio_drain_generation{0};

  // 15.1-06 + Codex HIGH-3: deferred-free queue for run-thread-owned
  // Local_Channel objects. The run thread enqueues a Local_Channel* ONLY
  // AFTER:
  //   (a) it has pushed a LocalChannelRemovedUpdate to m_locchan_update_q,
  //       AND
  //   (b) it has observed m_audio_drain_generation increment past the
  //       publish moment (audio thread has drained the queue at least once
  //       after the publish).
  // This guarantees the audio thread never holds a stale view of the
  // removed slot when the canonical object's destructor runs.
  jamwide::SpscRing<Local_Channel*,
                    jamwide::LOCAL_CHANNEL_DEFERRED_DELETE_CAPACITY>
      m_locchan_deferred_delete_q;

  // 15.1-06: overflow counter for m_locchan_update_q (Codex M-8 style).
  // On try_push failure, the run-thread mutators bump this counter and log
  // a warning. Non-zero at phase close is observable for the 15.1-10 gate.
  // Run-thread side only — relaxed semantics are sufficient.
  std::atomic<uint64_t> m_locchan_update_overflows{0};

  // 15.1-07a CR-01: audio-thread mirror of remote-user state. Replaces
  // m_users_cs.Enter/Leave at AudioProc, process_samples (audit line 2360),
  // and on_new_interval (audit line 3231). Indexed by STABLE SLOT — the run
  // thread allocates a slot when a peer is first announced (auth path) and
  // releases it via the generation-gate after the audio thread acknowledges
  // PeerRemovedUpdate. NOT keyed by m_remoteusers list index (shifts on
  // Delete; see 15.1-MIRROR-AUDIT.md).
  RemoteUserMirror m_remoteuser_mirror[MAX_PEERS];

  // 15.1-07a CR-01: state-update queue. Run-thread mutators publish
  // RemoteUserUpdate variants here; audio thread drains at top of AudioProc
  // via drainRemoteUserUpdates(). Capacity 64 == MAX_PEERS — peer-churn is
  // human-paced (≤ 1 join/leave/sec normally), generous headroom.
  jamwide::SpscRing<jamwide::RemoteUserUpdate, 64> m_remoteuser_update_q;

  // 15.1-07a + Codex M-8: overflow counter for m_remoteuser_update_q. Run-
  // thread side bumps on try_push failure. 15.1-10 phase verification asserts
  // == 0 post-UAT. Relaxed semantics — observability only.
  std::atomic<uint64_t> m_remoteuser_update_overflows{0};

  // Phase 20-00 (D-19): NinjamZap-literal RawData send queue. Replaces the
  // Phase 14.3-02 `SpscRing<jamwide::RawDataItem, 64>` substrate with the
  // exact pattern from `ninjamzap-core/njclient.h:310-321 + cpp:1984-2082`:
  // a `WDL_PtrList<RawDataQueueItem>` protected by `WDL_Mutex m_rawdata_cs`.
  // Producer = ANY thread (RawDataSendBegin/Write callers — UI thread,
  // audio thread on `on_new_interval` per Plan 20-02, encoder thread on
  // QueueVideoFrame per Plan 20-01/02). Consumer = run thread inside
  // NJClient::Run drain block, pop-one-unlock-Send-relock matching
  // `ninjamzap-core/njclient.cpp:1984-2040`. Direct m_netcon->Send from a
  // non-run-thread producer is forbidden — Landmine L2 (Net_Connection::Send
  // is NOT thread-safe — see src/core/netmsg.cpp:289). Queue is unbounded
  // (matches NinjamZap; backpressure exists only at the TCP send queue
  // governed by ninjamzap-server's VideoCongestionThreshold).

  // Mirrors ninjamzap-core/njclient.h:311-321 field-for-field. Must be
  // `public:` because the public forward-decl at the top of the class
  // (right after kRemoteNameMax) was already `public:` — C++ requires the
  // access specifier to match between forward-decl and full-decl. The data
  // members of RawDataQueueItem are implementation-detail; production code
  // should treat the type as opaque (only the test harness in
  // tests/test_rawdata_send.cpp pokes at the fields directly, and that
  // poking is guarded by JAMWIDE_BUILD_TESTS=1).
public:
  struct RawDataQueueItem {
    int           type = 0;            // 0 = begin, 1 = data/end
    unsigned char guid[16] = {0};
    unsigned int  fourcc = 0;
    int           chidx = 0;
    int           estsize = 0;
    int           flags = 0;           // & 1 = end (for type == 1)
    WDL_HeapBuf   data;                // by-value; freed by ~WDL_HeapBuf when
                                       // the queue item is `delete`d.
  };
protected:

  WDL_PtrList<RawDataQueueItem> m_rawdata_sendq;
  WDL_Mutex                     m_rawdata_cs;

  // ----------------------------------------------------------------------
  // Phase 20-00 (R3 MF4): queue observability counters. Plan 20-03 reads
  // them at populated load to enforce contention-ratio + high-water-mark
  // thresholds. All three are owned here unconditionally; Plan 20-03 adds
  // none of them. Relaxed memory ordering — observability only.
  //
  // - high_water_mark: max WDL_PtrList depth observed at enqueue.
  // - cs_contention_count: bumped per RawDataSend{Begin,Write} call that
  //   could not enter m_rawdata_cs immediately. WDL_Mutex has no TryEnter
  //   today (wdl/mutex.h is Enter/Leave only), so the contention sampling
  //   path uses a coarser proxy — see RawDataSendBegin/RawDataSendWrite
  //   in njclient.cpp for the exact wiring. The total-enqueue counter is
  //   the denominator for the contention-ratio gate in Plan 20-03 UAT.
  // - total_enqueues: bumped once per successful Add() inside
  //   RawDataSendBegin AND once per successful Add() inside
  //   RawDataSendWrite, paired with the high-water-mark CAS under
  //   m_rawdata_cs.
  // ----------------------------------------------------------------------
  std::atomic<uint64_t> m_rawdata_sendq_high_water_mark{0};
  std::atomic<uint64_t> m_rawdata_cs_contention_count{0};
  std::atomic<uint64_t> m_rawdata_sendq_total_enqueues{0};

  // -------------------------------------------------------------------------
  // Phase 20-02: H.264 send-side video state members. NinjamZap-literal per
  // CONTEXT.md D-08 + D-11 + D-13. Initialized in NJClient() ctor. All access
  // to m_video_active / m_video_interval_open / m_video_guid / m_video_chidx /
  // m_video_fourcc / m_sync_interval_cnt is under m_video_cs (audio thread
  // holds it across the entire on_new_interval video block per D-08; encoder
  // thread takes it inside QueueVideoFrame; message thread takes it inside
  // SetVideoBroadcastActive; run thread takes it inside SetVideoChannel).
  // m_video_spspps is under m_video_spspps_cs (nested inside m_video_cs in
  // on_new_interval; held alone in SetVideoSPSPPS per D-03).
  // -------------------------------------------------------------------------
  WDL_Mutex             m_video_cs;
  WDL_Mutex             m_video_spspps_cs;
  bool                  m_video_active{false};
  bool                  m_video_interval_open{false};
  unsigned char         m_video_guid[16]{};
  int                   m_video_chidx{1};
  unsigned int          m_video_fourcc{0};   // initialized in NJClient() ctor to MAKE_NJ_FOURCC('H','2','6','4')
  WDL_HeapBuf           m_video_spspps;
  int                   m_sync_interval_cnt{0};
  // D-15: Plan 20-01 encoder thread reads this relaxed; audio thread bumps
  // release at top of every on_new_interval. Lock-free, no mutex.
  std::atomic<uint64_t> m_audio_interval_seq{0};
  // Plan 20-01 publishes encoder input drops here via getEncoderInputDropsPtr.
  // Plan 20-03 UAT asserts this == 0 at populated-load close.
  std::atomic<uint64_t> m_encoder_input_drops_mirror{0};

#ifdef JAMWIDE_BUILD_TESTS
  // Plan 20-03 Task 1 part E: audio-thread budget probe — worst-case
  // duration in nanoseconds of the on_new_interval video block. CAS-updated
  // inside on_new_interval; read via GetOnNewIntervalVideoBlockWorstCaseNs.
  // JAMWIDE_BUILD_TESTS-only so production builds do not run the
  // steady_clock::now() probe on the audio thread.
  std::atomic<uint64_t> m_on_new_interval_video_block_worst_case_ns{0};

  // Plan 21-01 Task 2 (codex Cluster 1): receive-side audio-thread budget
  // probe — worst-case nanos + last-observed peer count for
  // runVideoReceiveBlock_. CAS-update via compare_exchange_weak loop, see
  // njclient.cpp runVideoReceiveBlock_ body. Same shape as the send-side
  // probe above. JAMWIDE_BUILD_TESTS-only.
  std::atomic<std::uint64_t> m_run_video_receive_block_max_nanos_{0};
  std::atomic<int>           m_run_video_receive_block_last_peer_count_{0};
#endif

  // Diagnostic counters for the 2026-05-02 RemoteUserMirror orphan-fields fix.
  // Bumped at PeerChannelInfoUpdate publish (run thread) and apply (audio
  // thread) sites. Relaxed atomics — purpose is falsifiable UAT readout, not
  // synchronization. See .planning/debug/remote-channels-cutoff.md.
  std::atomic<uint64_t> m_chinfo_publishes_observed[MAX_PEERS][MAX_USER_CHANNELS]{};
  std::atomic<uint64_t> m_chinfo_applies_observed  [MAX_PEERS][MAX_USER_CHANNELS]{};

  // 2026-05-03: peak dump_samples per (slot,channel). Audio-thread-writes
  // (single-writer — only mixInChannel mutates), UI-thread-reads via
  // GetDumpSamplesPeak. Relaxed atomic; observability only.
  std::atomic<int> m_dump_samples_peak[MAX_PEERS][MAX_USER_CHANNELS]{};

  // 15.1-07a + Codex HIGH-3: deferred-free queue for run-thread-owned
  // RemoteUser objects. The run thread enqueues a RemoteUser* ONLY AFTER:
  //   (a) it has pushed a PeerRemovedUpdate to m_remoteuser_update_q, AND
  //   (b) it has observed m_audio_drain_generation increment past the
  //       publish moment (audio thread has drained the queue at least once
  //       after the publish).
  // Parallels the m_locchan_deferred_delete_q pattern from 15.1-06 / Codex
  // HIGH-3 closure. The audio thread cannot still hold a stale view of the
  // removed slot when the canonical destructor runs.
  jamwide::SpscRing<RemoteUser*,
                    jamwide::REMOTE_USER_DEFERRED_DELETE_CAPACITY>
      m_remoteuser_deferred_delete_q;

  // 15.1-07a + Codex M-8: name→slot lookup table for the run thread. Maps
  // canonical RemoteUser pointer (stable per-session) to its mirror slot in
  // m_remoteuser_mirror[]. Run-thread-only; no audio-thread access. Slot is
  // -1 when the entry is unused. Capacity matches MAX_PEERS.
  //
  // Allocation: linear search for the first slot with .user==nullptr;
  // wrap-around is fine — typical peer counts are ≤ 16. Slot is stable for
  // the lifetime of the canonical RemoteUser; released only via the
  // deferred-free protocol.
  struct RemoteUserSlotEntry {
      RemoteUser* user = nullptr;
  };
  RemoteUserSlotEntry m_remoteuser_slot_table[MAX_PEERS];

  // Run-thread helpers for slot allocation/release (defined in njclient.cpp).
  // Returns -1 if no free slot is available (peer count exceeds MAX_PEERS).
  int  allocRemoteUserSlot(RemoteUser* user);
  int  findRemoteUserSlot(RemoteUser* user) const;
  void releaseRemoteUserSlot(int slot);

  // 15.1-07b CR-10: BlockRecord SPSC for the wavewrite/oggcomp output mix.
  // Replaces the audio-thread m_wavebq->AddBlock site at process_samples:2182.
  // Producer = audio thread (one push per processed block when waveWrite or
  // m_oggWrite is on); consumer = run thread (drainWaveBlocks at the top of
  // NJClient::Run()). N=32 because the wavewriter can lag further than the
  // encoder (file I/O latency).
  jamwide::SpscRing<jamwide::BlockRecord, 32> m_wave_block_q;

  // 15.1-07b CR-09/CR-10 + Codex M-8: BlockRecord drop counter. Audio thread
  // increments on try_push failure (queue full). 15.1-10 phase verification
  // asserts this is 0 post-UAT. Non-zero means the run-thread drain didn't
  // keep pace with the audio-thread producer, which is an architectural
  // defect at this scale (5 minute populated-server session per phase
  // verification). Relaxed semantics — observability only.
  std::atomic<uint64_t> m_block_queue_drops{0};

  // 15.1-09 CR-08 + H-04 + Codex HIGH-1: sessionmode rearm requests from audio
  // thread → run thread. The current 15.1-07a refactor already collapses the
  // audio-thread sessionmode rearm to an early-return no-op (mixInChannel
  // sessionmode branch defer-deletes any in-flight ds and returns), so under
  // today's UI flow this SPSC is unused. It is retained for the day a future
  // plan re-enables sessionmode — the audio thread MUST emit DecodeArmRequest
  // here rather than calling start_decode directly. DecodeArmRequest payload
  // was finalized in 15.1-04 (Codex M-9); this plan does NOT modify
  // spsc_payloads.h.
  jamwide::SpscRing<jamwide::DecodeArmRequest, jamwide::ARM_REQUEST_CAPACITY>
      m_arm_request_q;

  // 15.1-09 + Codex M-8: arm-request drop counter. 15.1-10 asserts == 0
  // post-UAT. Same Codex M-8 pattern as m_deferred_delete_overflows /
  // m_locchan_update_overflows / m_remoteuser_update_overflows /
  // m_block_queue_drops. Relaxed — observability only.
  std::atomic<uint64_t> m_arm_request_drops{0};

  // 15.1-09 + Codex HIGH-1: per-tick refill SPSC overflow counter. The
  // refill loop drops at most CHUNK_BYTES per overflow event (it discards
  // the bytes already read into the local stack buffer and continues at
  // the file's next byte on the next tick — see refillSessionmodeBuffers
  // in njclient.cpp). 15.1-10 asserts == 0 post-UAT.
  std::atomic<uint64_t> m_sessionmode_refill_drops{0};

  // 15.1-09 + Codex HIGH-1: run-thread-private bookkeeping of active
  // sessionmode-style file readers. For every audio-thread-visible
  // DecodeState backed by an on-disk file (the H-04 path before this plan,
  // structurally unreachable after), the run thread keeps the FILE* HERE
  // — NOT in the audio-thread DecodeState. On every run-thread tick,
  // refillSessionmodeBuffers reads bytes from each active FILE* and pushes
  // them into the corresponding DecodeMediaBuffer (lock-free SPSC push from
  // 15.1-07c). The audio thread's runDecode reaches `decode_buf->Read` for
  // these states, NEVER `fread(decode_fp)`.
  //
  // Run-thread-only access; protected by NJClient's existing run-thread
  // serialization (NinjamRunThread holds processor.getClientLock() during
  // the run-loop body; m_users_cs would be redundant but is still acquired
  // inside the DOWNLOAD_INTERVAL_BEGIN handler which adds entries here).
  // The forward-declared DecodeMediaBuffer is sufficient — this struct
  // only stores a pointer, never dereferences.
  struct SessionmodeFileReader {
      FILE*               file = nullptr;     // owned here on the run thread
      DecodeMediaBuffer*  buffer = nullptr;   // refcounted; same instance the audio thread reads
      bool                eof = false;        // set when fread returns 0
  };
  std::vector<SessionmodeFileReader> m_sessionmode_file_readers;

public:
  // 15.1-06 CR-02: drain method called at the top of AudioProc — applies
  // pending LocalChannelUpdate variants to m_locchan_mirror. After draining,
  // bumps m_audio_drain_generation (release-store) so the run thread can
  // verify its published RemovedUpdate has been observed before queuing the
  // canonical Local_Channel* for deferred-free.
  void drainLocalChannelUpdates();

  // 15.1-06 + Codex HIGH-3: drain method called by the run thread (every
  // 20ms tick) and once at shutdown. Pops any Local_Channel* from
  // m_locchan_deferred_delete_q and runs the canonical destructor off the
  // audio thread. The generation-gate logic that enqueues these pointers
  // lives in DeleteLocalChannel.
  void drainLocalChannelDeferredDelete();

  // 15.1-06 + Codex M-8: phase-close verification reads this. MUST be 0
  // after UAT. Non-zero == architectural defect (run-thread mutators
  // overflowed the SPSC; a state change was lost). 15.1-10 phase
  // verification asserts this counter == 0.
  uint64_t GetLocalChannelUpdateOverflowCount() const noexcept {
      return m_locchan_update_overflows.load(std::memory_order_relaxed);
  }

  // 15.1-07a CR-01: drain method called at the top of AudioProc — applies
  // pending RemoteUserUpdate variants to m_remoteuser_mirror. Drained
  // ALONGSIDE drainLocalChannelUpdates BEFORE m_audio_drain_generation is
  // bumped, so the same generation gate covers both Local_Channel and
  // RemoteUser deferred-free protocols.
  void drainRemoteUserUpdates();

  // 15.1-07a + Codex HIGH-3: drain method called by the run thread (every
  // 20ms tick) and once at shutdown. Pops any RemoteUser* from
  // m_remoteuser_deferred_delete_q and runs the canonical destructor off
  // the audio thread. The generation-gate logic that enqueues these
  // pointers lives in the peer-remove path (auth-side handler when
  // chanpresentmask reaches 0; Disconnect; ~NJClient).
  void drainRemoteUserDeferredDelete();

  // 15.1-07a + Codex M-8: phase-close verification reads this. MUST be 0
  // after UAT. Non-zero == architectural defect (run-thread mutators
  // overflowed the SPSC; a peer state change was lost). 15.1-10 phase
  // verification asserts this counter == 0.
  uint64_t GetRemoteUserUpdateOverflowCount() const noexcept {
      return m_remoteuser_update_overflows.load(std::memory_order_relaxed);
  }

  // 15.1-09 + Codex M-8: arm-request drop counter accessor for 15.1-10
  // phase-close verification. MUST be 0 after UAT. Currently always 0
  // because sessionmode rearm is dormant in the audio thread (see comment
  // on m_arm_request_q in the protected section) — but the counter and
  // accessor are wired so the gate is in place if sessionmode is re-enabled.
  uint64_t GetArmRequestDropCount() const noexcept {
      return m_arm_request_drops.load(std::memory_order_relaxed);
  }

  // 15.1-09 + Codex HIGH-1: refill SPSC drop counter accessor. Bumped by
  // refillSessionmodeBuffers when the per-file DecodeMediaBuffer's SPSC is
  // full (audio thread has not drained recently). 15.1-10 asserts == 0
  // post-UAT. A non-zero value indicates the audio thread's runDecode is
  // not consuming fast enough OR the run thread's refill cadence is too
  // bursty — either is an architectural defect.
  uint64_t GetSessionmodeRefillDropCount() const noexcept {
      return m_sessionmode_refill_drops.load(std::memory_order_relaxed);
  }

  // 15.1-09 CR-08 + Codex HIGH-1: drain method called from the run thread
  // (NinjamRunThread::run) inside the locked block. Drains pending
  // DecodeArmRequest entries from m_arm_request_q; for each, calls
  // start_decode off the audio thread, inverts the FILE* into a
  // SessionmodeFileReader entry (so the audio-thread DecodeState has
  // decode_fp == nullptr), and publishes the result via PeerNextDsUpdate.
  // Currently a no-op under today's UI flow (no audio-thread emitter), but
  // wired so a future sessionmode re-enable doesn't have to re-architect.
  void drainArmRequests();

  // 15.1-09 + Codex HIGH-1: per-tick refill loop. Reads bytes from every
  // active SessionmodeFileReader's FILE* and pushes them into the
  // corresponding DecodeMediaBuffer (lock-free SPSC push from 15.1-07c).
  // The audio thread's runDecode → decode_buf->Read path is fed by THIS
  // method; without it, the buffer would drain and playback would silence
  // on file-backed sessions. Removes entries whose buffer's refcnt drops
  // to 1 (the audio side has Released its share — we can fclose and let go).
  void refillSessionmodeBuffers();

  // 15.1-09 + Codex HIGH-1: helper invoked from the run-thread side of
  // start_decode's call sites (DOWNLOAD_INTERVAL_BEGIN at njclient.cpp:1948
  // and RemoteDownload::startPlaying) for the file-backed code path. It
  // takes the FILE* off the just-constructed DecodeState, allocates a
  // fresh DecodeMediaBuffer, primes the buffer with one initial chunk, and
  // registers a SessionmodeFileReader entry so refillSessionmodeBuffers
  // will continue feeding it on subsequent ticks.
  //
  // After this returns, the DecodeState's decode_fp is nullptr and its
  // decode_buf is non-null — making the H-04 fread path structurally
  // unreachable from the audio thread. Returns false on allocation
  // failure; the caller should publish the DS unmodified (the audio
  // thread will see decode_fp set but the failure is exceedingly rare —
  // operator new for 4 KB does not fail under realistic conditions).
  bool inversionAttachSessionmodeReader(DecodeState* ds);
};



// 15.1-06 CR-02: MAX_LOCAL_CHANNELS hoisted above the NJClient class so the
// LocalChannelMirror[MAX_LOCAL_CHANNELS] member array can see the constant.
// 15.1-07a CR-01: MAX_USER_CHANNELS and MAX_PEERS similarly hoisted above the
// NJClient class so RemoteUserChannelMirror::chans[MAX_USER_CHANNELS] and
// NJClient::m_remoteuser_mirror[MAX_PEERS] see the constants.
// The #ifndef guards above prevent redefinition.
#define DOWNLOAD_TIMEOUT 8


#endif//_NJCLIENT_H_
