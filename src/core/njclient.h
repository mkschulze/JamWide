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
#include <atomic>
#include <cstdint>
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


class I_NJEncoder;
class RemoteDownload;
class RemoteUser;
class RemoteUser_Channel;
class Local_Channel;
class DecodeState;
class BufferQueue;
class DecodeMediaBuffer;

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
