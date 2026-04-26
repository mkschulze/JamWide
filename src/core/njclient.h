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

  void mixInChannel(RemoteUser *user, int chanidx,
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
};



#define MAX_USER_CHANNELS 32
#define MAX_LOCAL_CHANNELS 32 // probably want to use NJClient::GetMaxLocalChannels() if determining when it's OK to add a channel,etc
#define DOWNLOAD_TIMEOUT 8


#endif//_NJCLIENT_H_
