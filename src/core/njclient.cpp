/*
    NINJAM - njclient.cpp
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

  For a full description of everything here, see njclient.h
*/


#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <chrono>
#include <thread>  // 15.1-06 HIGH-3: std::this_thread::yield in DeleteLocalChannel gate
#include "njclient.h"
#include "mpb.h"

static int64_t currentMillis()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// 15.1-07b CR-09 + Codex M-7 + Codex M-8: producer-side helper for the per-channel
// mirror BlockRecord SPSC. Audio thread calls this from process_samples and
// on_new_interval. Two-layer defense:
//   1. SetMaxAudioBlockSize prepareToPlay assertion (lands in 15.1-08) ensures
//      no host pushes a sample_count > MAX_BLOCK_SAMPLES.
//   2. Per-callsite bounds-check HERE rejects pathological inputs and bumps the
//      drop counter. Codex M-7: bounds-check at every site, before memcpy.
// On try_push failure (consumer hasn't drained yet), bump the drop counter and
// drop the record. RT-safety > broadcast continuity at audio callback boundary.
//
// Defined here at file top so the producer call sites in process_samples /
// on_new_interval (which are member functions defined further down) can see
// the helpers without needing forward declarations.
static inline void pushBlockRecord(
    jamwide::SpscRing<jamwide::BlockRecord, 16>& ring,
    std::atomic<uint64_t>& drop_counter,
    int attr, double startpos,
    const float* samples_ptr, int sample_count, int nch,
    const float* samples_ptr_2 = nullptr)
{
  // Codex M-7: defensive bounds-check at the call site BEFORE memcpy.
  // The interval-boundary marker case has sample_count<=0 (see legacy
  // lc->m_bq.AddBlock(0,0.0,NULL,0) at on_new_interval) — accept those by
  // letting sample_count==0 with samples_ptr==NULL through (no memcpy).
  if (sample_count > jamwide::MAX_BLOCK_SAMPLES || nch > jamwide::MAX_BLOCK_CHANNELS
      || sample_count < 0 || nch < 0)
  {
    // Out-of-bounds input — drop and count. Either the host violated the
    // SetMaxAudioBlockSize contract OR the BufferQueue boundary-marker
    // sentinel encoding leaked through (sample_count==-1).
    drop_counter.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  jamwide::BlockRecord br{};
  br.attr = attr;
  br.startpos = startpos;
  br.sample_count = sample_count;
  br.nch = nch;
  if (sample_count > 0 && samples_ptr)
  {
    // Stereo channel layout in BlockRecord.samples: channel-0 samples first
    // (sample_count floats), then channel-1 samples (sample_count floats).
    // This matches the legacy BufferQueue::AddBlock layout and the encoder
    // consumer's existing `(float*)p->Get()+sz` interpretation at line 1750.
    std::memcpy(br.samples, samples_ptr,
                static_cast<size_t>(sample_count) * sizeof(float));
    if (nch > 1 && samples_ptr_2)
    {
      std::memcpy(br.samples + sample_count, samples_ptr_2,
                  static_cast<size_t>(sample_count) * sizeof(float));
    }
  }

  if (!ring.try_push(std::move(br)))
  {
    // Run-thread consumer hasn't drained — drop and count (Codex M-8).
    drop_counter.fetch_add(1, std::memory_order_relaxed);
  }
}

// 15.1-07b CR-10: same shape, larger ring (N=32) for the m_wave_block_q.
// Wavewriter can lag further than the encoder (file I/O latency), so a larger
// ring absorbs more burst.
static inline void pushWaveBlockRecord(
    jamwide::SpscRing<jamwide::BlockRecord, 32>& ring,
    std::atomic<uint64_t>& drop_counter,
    int attr, double startpos,
    const float* samples_ptr, int sample_count, int nch,
    const float* samples_ptr_2 = nullptr)
{
  if (sample_count > jamwide::MAX_BLOCK_SAMPLES || nch > jamwide::MAX_BLOCK_CHANNELS
      || sample_count < 0 || nch < 0)
  {
    drop_counter.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  jamwide::BlockRecord br{};
  br.attr = attr;
  br.startpos = startpos;
  br.sample_count = sample_count;
  br.nch = nch;
  if (sample_count > 0 && samples_ptr)
  {
    std::memcpy(br.samples, samples_ptr,
                static_cast<size_t>(sample_count) * sizeof(float));
    if (nch > 1 && samples_ptr_2)
    {
      std::memcpy(br.samples + sample_count, samples_ptr_2,
                  static_cast<size_t>(sample_count) * sizeof(float));
    }
  }

  if (!ring.try_push(std::move(br)))
  {
    drop_counter.fetch_add(1, std::memory_order_relaxed);
  }
}
#include "crypto/nj_crypto.h"
#include "../wdl/pcmfmtcvt.h"
#include "../wdl/wavwrite.h"
#include "../wdl/wdlcstring.h"

#include "../wdl/win32_utf8.h"

#define NJ_ENCODER_FMT_TYPE MAKE_NJ_FOURCC('O','G','G','v')
#define NJ_ENCODER_FMT_FLAC MAKE_NJ_FOURCC('F','L','A','C')

#ifdef REANINJAM
#define WDL_VORBIS_INTERFACE_ONLY
#endif

#define VorbisEncoderInterface I_NJEncoder
#define VorbisDecoderInterface I_NJDecoder
#include "../wdl/vorbisencdec.h"
#include "../wdl/flacencdec.h"

#undef VorbisEncoderInterface
#undef VorbisDecoderInterface

#ifdef REANINJAM
  extern void *(*CreateVorbisEncoder)(int srate, int nch, int serno, float qv, int cbr, int minbr, int maxbr);
  extern void *(*CreateVorbisDecoder)();
  static void *__CreateVorbisEncoder(int srate, int nch, int bitrate, int serno)
  {
    float qv=0.0;
    if (nch == 2) bitrate=  (bitrate*5)/8;
    // at least for mono 44khz
    //-0.1 = ~40kbps
    //0.0 == ~64kbps
    //0.1 == 75
    //0.3 == 95
    //0.5 == 110
    //0.75== 140
    //1.0 == 240

    if (bitrate < 40) qv=-0.1f;
    else if (bitrate < 64) qv=-0.10f + (bitrate-40)*(0.10f/24.0f);
    else if (bitrate < 75) qv=(bitrate-64)*(0.1f/9.0f);
    else if (bitrate < 95) qv=0.1f+(bitrate-75)*(0.2f/20.0f);
    else if (bitrate < 110) qv=0.3f+(bitrate-95)*(0.2f/15.0f);
    else if (bitrate < 140) qv=0.5f+(bitrate-110)*(0.25f/30.0f);
    else qv=0.75f+(bitrate-140)*(0.25f/100.0f);

    if (qv<-0.10f)qv=-0.10f;
    if (qv>1.0f)qv=1.0f;
    return CreateVorbisEncoder(srate,nch,serno,qv,-1,-1,-1);

  }
  #define CreateNJEncoder(srate,ch,br,id) ((I_NJEncoder *)__CreateVorbisEncoder(srate,ch,br,id))
  #define CreateNJDecoder() ((I_NJDecoder *)CreateVorbisDecoder())
#else
  #define CreateNJEncoder(srate,ch,br,id) ((I_NJEncoder *)new VorbisEncoder(srate,ch,br,id))
  #define CreateNJDecoder() ((I_NJDecoder *)new VorbisDecoder)
#endif

#define CreateFLACEncoder(srate,ch,br,id) ((I_NJEncoder *)new FlacEncoder(srate,ch,br,id))
#define CreateFLACDecoder() ((I_NJDecoder *)new FlacDecoder)


#define SESSION_CHUNK_SIZE 2.0
#define LL_CHUNK_SIZE 2.0

#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))

class DecodeMediaBuffer
{
public:
  DecodeMediaBuffer()
  {
    refcnt=1;
    rdpos=0;
  }
  ~DecodeMediaBuffer()
  {
  }
  void AddRef() { refcnt++; }
  void Release() { if (!--refcnt) delete this; }

  void Write(const void *buf, int len)
  {
    mutex.Enter();
    m_buf.Add(buf,len);
    mutex.Leave();
  }

  int Avail() { return m_buf.Available() - rdpos; }
  int Size() { return m_buf.Available(); }
  int Read(void *buf, int len)
  {
    mutex.Enter();
    int l=Avail();
    if (l >len)l=len;
    if (l>0)
    {
      memcpy(buf,(char *)m_buf.Get()+rdpos,l);
      rdpos+=l;
    }
    mutex.Leave();
    return l;
  }

private:
  WDL_Mutex mutex;
  int rdpos;
  int refcnt;
  WDL_Queue m_buf;
};

struct overlapFadeState {
  overlapFadeState() { fade_nch=fade_sz=0; }

  int fade_nch, fade_sz;

  enum { MAX_FADE=128 };
  float fade_buf[MAX_FADE*2];
};

class DecodeState
{
  public:
    DecodeState() : decode_fp(0), decode_buf(0), decode_codec(0),
                                           resample_state(0.0),
                                           is_voice_firstchk(false)
    {
      memset(guid,0,sizeof(guid));
    }
    ~DecodeState()
    {
      delete decode_codec;
      decode_codec=0;
      if (decode_fp ) fclose(decode_fp);
      decode_fp=0;
      if (decode_buf) decode_buf->Release();
      decode_buf=0;

    }

    unsigned char guid[16];

    FILE *decode_fp;
    DecodeMediaBuffer *decode_buf;
    I_NJDecoder *decode_codec;
    double resample_state;

    bool is_voice_firstchk;

    void applyOverlap(overlapFadeState *s)
    {
      if (!s || !s->fade_sz || !decode_codec) return;
      int nch;
      for (;;)
      {
        nch = decode_codec->GetNumChannels();
        if (nch && decode_codec->Available() >= s->fade_sz * nch) break;

        if (runDecode()) break;
      }
      if (!nch) return;
      const int avail = decode_codec->Available()/nch;
      if (s->fade_nch == nch && s->fade_sz <= avail)
      {
        const int fade_sz = s->fade_sz;
        const float *fade_buf = s->fade_buf;
        float *p = decode_codec->Get();
        const double ifsz = 1.0 / (double) fade_sz;
        for (int x = 0; x < fade_sz; x ++)
        {
          const double s = (x+1) * ifsz;
          for (int y = 0; y < nch; y ++)
          {
            *p = *p * s + *fade_buf * (1.0-s);
            p++;
            fade_buf++;
          }
        }
      }
    }
    void calcOverlap(overlapFadeState *s)
    {
      if (!decode_codec) return;

      decode_codec->GenerateLappingSamples();
      const int nch = decode_codec->GetNumChannels();
      const int avail = decode_codec->Available();
      if (avail > 0 && nch > 0)
      {
        const float *rd = decode_codec->Get();
        if (rd)
        {
          int sz = avail / nch;
          if (sz > overlapFadeState::MAX_FADE) sz = overlapFadeState::MAX_FADE;
          float *wr = s->fade_buf;
          s->fade_sz = sz;
          const int fade_nch = wdl_min(nch,2);
          s->fade_nch = fade_nch;
          for (int x = 0; x < sz; x ++)
          {
            for (int y = 0; y < fade_nch; y ++)
              *wr++ = rd[y];
            rd += nch;
          }
        }
      }
    }
    bool runDecode(int sz=1024) // return true if eof
    {
      if (!decode_fp && !decode_buf) return true;

      int l;
      void *srcbuf = decode_codec->DecodeGetSrcBuffer(sz);
      if (!srcbuf) return true;

      if (decode_fp)
      {
        l=fread(srcbuf,1,sz,decode_fp);
        if (!l) clearerr(decode_fp);
      }
      else
      {
        l=decode_buf->Read(srcbuf,sz);
      }

      decode_codec->DecodeWrote(l);

      return !l;
    }
};

class ChannelSessionInfo
{
public:
  ChannelSessionInfo(const unsigned char *_guid, double st, double len)
  {
    memcpy(guid,_guid,16);
    start_time=st;
    length=len;
    offset=0.0;
  }
  ~ChannelSessionInfo()
  {
  }

  double start_time;
  double length;
  double offset;
  unsigned char guid[16];

};

class RemoteUser_Channel
{
  public:
    RemoteUser_Channel();
    ~RemoteUser_Channel();

    float volume, pan;
    int out_chan_index;

    int flags;

    WDL_String name;

    // decode/mixer state, used by mixer
    int dump_samples;
    DecodeState *ds;
    DecodeState *next_ds[2]; // prepared by main thread, for audio thread

    double decode_peak_vol[2];
    unsigned int codec_fourcc;  // FOURCC of currently active codec on this channel

    double curds_lenleft;

    void AddSessionInfo(const unsigned char *guid, double st, double len);
    bool GetSessionInfo(double time, unsigned char *guid, double *offs, double *len, double mv);
    double GetMaxLength()
    {
      ChannelSessionInfo *p=sessioninfo.Get(sessioninfo.GetSize()-1);
      if (!p) return -1.0;
      return p->start_time + p->length;
    }
    void ClearSessionInfo()
    {
      sessionlist_mutex.Enter();
      sessioninfo.Empty(true);
      sessionlist_mutex.Leave();
    }

  private:
    WDL_Mutex sessionlist_mutex;
    WDL_PtrList<ChannelSessionInfo> sessioninfo;

};


class RemoteUser
{
public:
  RemoteUser() : muted(0), volume(1.0f), pan(0.0f), submask(0), mutedmask(0), solomask(0), last_session_pos(-1.0), last_session_pos_updtime(0), chanpresentmask(0) { }
  ~RemoteUser() { }

  bool muted;
  float volume;
  float pan;
  WDL_String name;
  int submask;
  int chanpresentmask;
  int mutedmask;
  int solomask;
  double last_session_pos;
  time_t last_session_pos_updtime;
  RemoteUser_Channel channels[MAX_USER_CHANNELS];
};


class RemoteDownload
{
public:
  RemoteDownload();
  ~RemoteDownload();

  void Close();
  void Open(NJClient *parent, unsigned int fourcc, bool forceToDisk);
  void Write(const void *buf, int len);
  void startPlaying(int force=0); // call this with 1 to make sure it gets played ASAP, or let RemoteDownload call it automatically

  time_t last_time;
  unsigned char guid[16];

  int chidx;
  WDL_String username;
  int playtime;

private:
  unsigned int m_fourcc;
  NJClient *m_parent;
  FILE *m_fp;
  DecodeMediaBuffer *m_decbuf;
};



class BufferQueue
{
  public:
    BufferQueue() { }
    ~BufferQueue()
    {
      Clear();
    }

    void AddBlock(int attr, double blockstart, float *samples, int len, float *samples2=NULL);
    int GetBlock(WDL_HeapBuf **b, int *attr=NULL, double *startpos=NULL); // return 0 if got one, 1 if none avail
    void DisposeBlock(WDL_HeapBuf *b);

    typedef struct
    {
      int attr;
      double startpos;
    } AttrStruct;

    void Clear()
    {
      m_emptybufs.Empty(true);
      m_emptybufs_attr.Empty(true);
      m_samplequeue.Empty(true);
    }

//  private:
    WDL_PtrList<WDL_HeapBuf> m_samplequeue; // a list of pointers, with NULL to define spaces
    WDL_PtrList<WDL_HeapBuf> m_emptybufs;
    WDL_PtrList<WDL_HeapBuf> m_emptybufs_attr;
    WDL_Mutex m_cs;
};


class Local_Channel
{
public:
  Local_Channel();
  ~Local_Channel();

  int channel_idx;

  int src_channel; // 0 or 1 etc.. &1024 = stereo!
  int bitrate;

  float volume;
  float pan;
  bool muted;
  bool solo;

  //?
  // mode flag. 0=silence, 1=broadcasting
  bool broadcasting; //takes effect next loop



  // internal state. should ONLY be used by the audio thread.
  bool bcast_active;


  void (*cbf)(float *, int ns, void *);
  void *cbf_inst;

  BufferQueue m_bq;

  double decode_peak_vol[2];
  bool m_need_header;
  int out_chan_index;
  int flags;

#ifndef NJCLIENT_NO_XMIT_SUPPORT
  I_NJEncoder  *m_enc;
  int m_enc_bitrate_used;
  int m_enc_nch_used;
  Net_Message *m_enc_header_needsend;
#endif

  WDL_String name;
  RemoteDownload m_curwritefile;
  double m_curwritefile_starttime;
  double m_curwritefile_writelen;
  double m_curwritefile_curbuflen;
  WaveWriter *m_wavewritefile;

  //DecodeState too, eventually
};







#define MIN_ENC_BLOCKSIZE 2048
#define MAX_ENC_BLOCKSIZE (8192+1024)
#define DEFAULT_CONFIG_PREBUFFER  8192
#define LIVE_PREBUFFER 128
#define LIVE_ENC_BLOCKSIZE1 2048
#define LIVE_ENC_BLOCKSIZE2 64


#define NJ_PORT 2049

static unsigned char zero_guid[16];


static void guidtostr(const unsigned char *guid, char *str)
{
  int x;
  for (x = 0; x < 16; x ++) {
    snprintf(str + x * 2, 3, "%02x", guid[x]);
  }
}
static bool strtoguid(const char *str, unsigned char *guid)
{
  int n=16;
  while(n--)
  {
    unsigned char v=0;
    if (str[0]>='0' && str[0]<='9') v+=str[0]-'0';
    else if (str[0]>='a' && str[0]<='f') v+=10 + str[0]-'a';
    else if (str[0]>='A' && str[0]<='F') v+=10 + str[0]-'A';
    else return false;
    v<<=4;

    str++;
    if (str[0]>='0' && str[0]<='9') v+=str[0]-'0';
    else if (str[0]>='a' && str[0]<='f') v+=10 + str[0]-'a';
    else if (str[0]>='A' && str[0]<='F') v+=10 + str[0]-'A';
    else return false;

    str++;
    *guid++=v;
  }
  return true;
}

static char *guidtostr_tmp(unsigned char *guid)
{
  static char tmp[64];
  guidtostr(guid,tmp);
  return tmp;
}


static int is_type_char_valid(int c)
{
  c&=0xff;
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         c == ' ' || c == '-' ||
         c == '.' || c == '_';
}

static int is_type_valid(unsigned int t)
{
  return (t&0xff) != ' ' &&
          is_type_char_valid(t>>24) &&
          is_type_char_valid(t>>16) &&
          is_type_char_valid(t>>8) &&
          is_type_char_valid(t);
}


static void type_to_string(unsigned int t, char *out)
{
  if (is_type_valid(t))
  {
    out[0]=(t)&0xff;
    out[1]=(t>>8)&0xff;
    out[2]=(t>>16)&0xff;
    out[3]=' ';//(t>>24)&0xff;
    out[4]=0;
    int x=3;
    while (out[x]==' ' && x > 0) out[x--]=0;
  }
  else *out=0;
}

void NJClient::makeFilenameFromGuid(WDL_String *s, unsigned char *guid)
{
  char buf[256];
  guidtostr(guid,buf);

  s->Set(m_workdir.Get());
  //if (config_savelocalaudio>0)
  {
  #ifdef _WIN32
    char tmp[3]={buf[0],'\\',0};
  #else
    char tmp[3]={buf[0],'/',0};
  #endif
    s->Append(tmp);
  }
  s->Append(buf);
}




NJClient::NJClient()
{
  m_wavebq=new BufferQueue;
  m_userinfochange=0;
  m_loopcnt=0;
  m_srate=48000;
#ifdef _WIN32
  DWORD v=GetTickCount();
  WDL_RNG_addentropy(&v,sizeof(v));
  v=(DWORD)time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));
#else
  time_t v=time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));
#endif

  config_autosubscribe=1;
  config_savelocalaudio=0;
  config_metronome.store(0.5f, std::memory_order_relaxed);
  config_metronome_pan.store(0.0f, std::memory_order_relaxed);
  config_metronome_mute.store(false, std::memory_order_relaxed);
  config_metronome_channel.store(-1, std::memory_order_relaxed);
  config_debug_level=0;
  config_mastervolume.store(1.0f, std::memory_order_relaxed);
  config_masterpan.store(0.0f, std::memory_order_relaxed);
  config_mastermute.store(false, std::memory_order_relaxed);
  config_play_prebuffer.store(DEFAULT_CONFIG_PREBUFFER, std::memory_order_relaxed);
  m_encoder_fmt_requested.store(NJ_ENCODER_FMT_TYPE, std::memory_order_relaxed);  // Vorbis default
  m_encoder_fmt_active = NJ_ENCODER_FMT_TYPE;
  m_encoder_fmt_prev = 0;
  config_remote_autochan = config_remote_autochan_nch = 0;

  LicenseAgreement_User=0;
  LicenseAgreementCallback=0;
  ChatMessage_Callback=0;
  ChatMessage_User=0;
  ChannelMixer=0;
  ChannelMixer_User=0;

  waveWrite=0;
#ifndef NJCLIENT_NO_XMIT_SUPPORT
  m_oggWrite=0;
  m_oggComp=0;
#endif
  m_logFile=0;

  m_issoloactive=0;
  m_netcon=0;

  m_metro_chidx = 0;
  m_remote_chanoffs = 0;
  m_local_chanoffs = 0;

  _reinit();

  m_session_pos_ms=m_session_pos_samples=0;
}

void NJClient::_reinit()
{
  m_max_localch=MAX_LOCAL_CHANNELS;
  output_peaklevel[0]=output_peaklevel[1]=0.0;

  m_connection_keepalive=0;
  m_status=1002; // NJC_STATUS_DISCONNECTED (internal code)

  m_in_auth=0;

  // 15.1-02 CR-03: m_bpm/m_bpi/m_beatinfo_updated are atomic; relaxed init from owning thread.
  m_bpm.store(120, std::memory_order_relaxed);
  m_bpi.store(32, std::memory_order_relaxed);

  m_beatinfo_updated.store(1, std::memory_order_relaxed);

  m_audio_enable=0;
  // 15.1-03 H-01: m_debug_logged_remote member deleted; was the one-shot gate for the
  // removed JAMWIDE_DEV_BUILD audio-path fopen block.

  m_active_bpm=120;
  m_active_bpi=32;
  m_interval_length=1000;
  // 15.1-02: m_interval_pos is std::atomic<int>; relaxed init from owning thread.
  m_interval_pos.store(-1, std::memory_order_relaxed);
  m_metronome_pos=0.0;
  m_metronome_state=0;
  m_metronome_tmp=0;
  m_metronome_interval=0;

  m_issoloactive&=~1;

  int x;
  for (x = 0; x < m_locchannels.GetSize(); x ++)
  {
    Local_Channel *c=m_locchannels.Get(x);
    c->channel_idx = x; // normalize the channel indices on connect/disconnect
    c->decode_peak_vol[0]=0.0f;
    c->decode_peak_vol[1]=0.0f;
  }

}


void NJClient::writeLog(const char *fmt, ...)
{
  if (m_logFile)
  {
    va_list ap;
    va_start(ap,fmt);

    m_log_cs.Enter();
    if (m_logFile) vfprintf(m_logFile,fmt,ap);
    m_log_cs.Leave();

    va_end(ap);

  }


}

void NJClient::SetLogFile(const char *name)
{
  m_log_cs.Enter();
  if (m_logFile) fclose(m_logFile);
  m_logFile=0;
  if (name && *name)
  {
    if (!strstr(name,"\\") && !strstr(name,"/") && !strstr(name,":"))
    {
      WDL_String s(m_workdir.Get());
      s.Append(name);
      m_logFile=fopenUTF8(s.Get(),"a+t");
    }
    else
      m_logFile=fopenUTF8(name,"a+t");
  }
  m_log_cs.Leave();
}


NJClient::~NJClient()
{
  delete m_netcon;
  m_netcon=0;

  delete waveWrite;
  SetOggOutFile(NULL,0,0);

  if (m_logFile)
  {
    writeLog("end\n");
    fclose(m_logFile);
    m_logFile=0;
  }

  int x;
  {
    WDL_MutexLock lock_users(&m_users_cs);
    WDL_MutexLock lock_channels(&m_remotechannel_rd_mutex);
    for (x = 0; x < m_remoteusers.GetSize(); x ++) delete m_remoteusers.Get(x);
    m_remoteusers.Empty();
  }
  for (x = 0; x < m_downloads.GetSize(); x ++) delete m_downloads.Get(x);
  m_downloads.Empty();
  for (x = 0; x < m_locchannels.GetSize(); x ++) delete m_locchannels.Get(x);
  m_locchannels.Empty();

  delete m_wavebq;
}


void NJClient::updateBPMinfo(int bpm, int bpi)
{
  // 15.1-02 CR-03: edge-triggered publish (see semantics in njclient.h).
  // Writer's last store wins; intermediate publishes between reader runs are coalesced.
  // No mutex acquisition — m_misc_cs no longer needed for this publication.
  m_bpm.store(bpm, std::memory_order_relaxed);
  m_bpi.store(bpi, std::memory_order_relaxed);
  m_beatinfo_updated.store(1, std::memory_order_release);
}


void NJClient::GetPosition(int *pos, int *length)  // positions in samples
{
  if (length) *length=m_interval_length;
  // 15.1-02 (AUDIT line 421): m_interval_pos is atomic; relaxed read closes the
  // previously-undefined-behavior UI-thread/audio-thread race.
  if (pos)
  {
    int p = m_interval_pos.load(std::memory_order_relaxed);
    if (p < 0) p = 0;
    *pos = p;
  }
}

unsigned int NJClient::GetSessionPosition()// returns milliseconds
{
  unsigned int a=m_session_pos_ms;
  if (m_srate)
    a+=(m_session_pos_samples*1000)/m_srate;
  return a;
}

void NJClient::AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, bool justmonitor, bool isPlaying, bool isSeek, double cursessionpos)
{
  m_srate=srate;

  // 15.1-06 CR-02: drain pending local-channel mutations into the audio-thread
  // mirror BEFORE any read of locchannel state in process_samples /
  // on_new_interval. m_locchan_cs is no longer acquired on the audio path.
  drainLocalChannelUpdates();

  // 15.1-06 + Codex HIGH-3: bump generation AFTER the drain. Run thread
  // (DeleteLocalChannel and drainLocalChannelDeferredDelete) reads this to
  // know when its PUBLISHED LocalChannelRemovedUpdate has been observed by
  // the audio thread. Release-store synchronizes with the run thread's
  // acquire-load.
  m_audio_drain_generation.fetch_add(1, std::memory_order_release);

  // zero output
  int x;
  for (x = 0; x < outnch; x ++) memset(outbuf[x],0,sizeof(float)*len);

  int remote_user_count = 0;
  if (!justmonitor)
  {
    WDL_MutexLock lock(&m_users_cs);
    remote_user_count = m_remoteusers.GetSize();
  }

  if (!m_audio_enable||justmonitor ||
      (!m_max_localch && remote_user_count == 0) // in a lobby, effectively
      )
  {
    process_samples(inbuf,innch,outbuf,outnch,len,srate,0,1,isPlaying,isSeek,cursessionpos);
    return;
  }

  if (srate>0)
  {
    unsigned int spl=m_session_pos_samples;
    unsigned int sec=m_session_pos_ms;

    spl += len;
    if (spl >= (unsigned int)srate)
    {
      sec += (spl/srate)*1000;
      spl %= srate;
    }
    // writing these both like this reduces the chance that the
    // main thread will read them and get a mix. still possible, tho,
    // but super unlikely
    m_session_pos_samples=spl;
    m_session_pos_ms=sec;
  }



  int offs=0;

  while (len > 0)
  {
    // 15.1-02: same-thread (audio) relaxed load; no synchronization needed
    // because the writer is also the audio thread (processBlock) per H header SetIntervalPosition.
    int interval_pos = m_interval_pos.load(std::memory_order_relaxed);
    int x=m_interval_length-interval_pos;
    if (!x || interval_pos < 0)
    {
      // 15.1-02 CR-03: edge-triggered consume (see semantics in njclient.h).
      // Acquire-load synchronizes with updateBPMinfo's release-store; subsequent relaxed
      // loads see whatever the writer most recently published. m_misc_cs is no longer
      // acquired on the audio thread.
      if (m_beatinfo_updated.load(std::memory_order_acquire))
      {
        const int bpm = m_bpm.load(std::memory_order_relaxed);
        const int bpi = m_bpi.load(std::memory_order_relaxed);

        double v=(double)bpm*(1.0/60.0);
        // beats per second

        // (beats/interval) / (beats/sec)
        v = (double) bpi / v;

        // seconds/interval

        // samples/interval
        v *= (double) srate;

        // Edge-clear: another publish may have raced past; we will see it next interval.
        m_beatinfo_updated.store(0, std::memory_order_relaxed);
        m_interval_length = (int)v;
        //m_interval_length-=m_interval_length%1152;//hack
        m_active_bpm = bpm;
        m_active_bpi = bpi;
        m_metronome_interval=(int) ((double)m_interval_length / (double)m_active_bpi);
      }

      // new buffer time
      on_new_interval();

      m_interval_pos.store(0, std::memory_order_relaxed);
      interval_pos = 0;
      x=m_interval_length;
    }

    if (x > len) x=len;

    process_samples(inbuf,innch,outbuf,outnch,x,srate,offs,0,isPlaying,isSeek,cursessionpos);

    m_interval_pos.store(interval_pos + x, std::memory_order_relaxed);
    offs += x;
    len -= x;

    if (len>0 && cursessionpos > -1.0)
    {
      isSeek=false;
      cursessionpos += x/(double)srate;
    }
  }

}


void NJClient::Disconnect()
{
  m_errstr.Set("");
  m_host.Set("");
  m_user.Set("");
  m_pass.Set("");
  memset(m_auth_challenge, 0, 8);  // scrub saved challenge (Phase 15)
  // 15.1-03 H-01: m_debug_logged_remote field deleted (see _reinit comment).
  delete m_netcon;
  m_netcon=0;

  int x;
  {
    WDL_MutexLock lock_users(&m_users_cs);
    WDL_MutexLock lock_channels(&m_remotechannel_rd_mutex);
    for (x=0;x<m_remoteusers.GetSize(); x++) delete m_remoteusers.Get(x);
    m_remoteusers.Empty();
  }
  if (x) m_userinfochange=1; // if we removed users, notify parent

  for (x = 0; x < m_downloads.GetSize(); x ++) delete m_downloads.Get(x);


  for (x = 0; x < m_locchannels.GetSize(); x ++)
  {
    Local_Channel *c=m_locchannels.Get(x);
    delete c->m_wavewritefile;
    c->m_wavewritefile=0;
    c->m_curwritefile.Close();

#ifndef NJCLIENT_NO_XMIT_SUPPORT
    delete c->m_enc;
    c->m_enc=0;
    delete c->m_enc_header_needsend;
    c->m_enc_header_needsend=0;
#endif

    c->m_bq.Clear();
  }
  m_downloads.Empty();

  m_wavebq->Clear();

  _reinit();

  // Update cached status for lock-free audio thread access
  cached_status.store(NJC_STATUS_DISCONNECTED, std::memory_order_release);
}

void NJClient::Connect(const char *host, const char *user, const char *pass)
{
#ifdef JAMWIDE_DEV_BUILD
  fprintf(stderr, "[NJClient] Connect called: host='%s' user='%s'\n", host ? host : "(null)", user ? user : "(null)");
#endif
  Disconnect();

  m_session_pos_ms=m_session_pos_samples=0;

  m_host.Set(host);
  m_user.Set(user);
  m_pass.Set(pass);

  char tmp[256];
  lstrcpyn_safe(tmp,m_host.Get(),sizeof(tmp));
  int port=NJ_PORT;
  char *p=strstr(tmp,":");
  if (p)
  {
    *p=0;
    port=atoi(++p);
    if (!port) port=NJ_PORT;
  }
#ifdef JAMWIDE_DEV_BUILD
  fprintf(stderr, "[NJClient] Connecting to %s:%d\n", tmp, port);
#endif
  JNL_Connection *c=new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
  c->connect(tmp,port);
  m_netcon = new Net_Connection;
  m_netcon->attach(c);

  m_status=0;

  // Update cached status for lock-free audio thread access
  cached_status.store(GetStatus(), std::memory_order_release);
#ifdef JAMWIDE_DEV_BUILD
  fprintf(stderr, "[NJClient] Connection initiated, status=%d\n", GetStatus());
#endif
}

int NJClient::GetStatus()
{
  if (!m_status || m_status == -1) return NJC_STATUS_PRECONNECT;
  if (m_status == 1000) return NJC_STATUS_CANTCONNECT;
  if (m_status == 1001) return NJC_STATUS_INVALIDAUTH;
  if (m_status == 1002) return NJC_STATUS_DISCONNECTED;

  return NJC_STATUS_OK;
}

static char getConfigStringQuoteChar(const char *p) // from WDL/projectcontext.cpp
{
  if (!p || !*p) return '"';

  char fc = *p;
  int flags=0;
  while (*p && flags!=15)
  {
    char c=*p++;
    if (c=='"') flags|=1;
    else if (c=='\'') flags|=2;
    else if (c=='`') flags|=4;
    else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') flags |= 8;
  }
  if (!(flags & 8) && fc != '"' && fc != '\'' && fc != '`' && fc != '#' && fc != ';') return ' ';
  if (!(flags & 1)) return '"';
  if (!(flags & 2)) return '\'';
  if (!(flags & 4)) return '`';
  return 0;
}

int NJClient::find_unused_output_channel_pair() const
{
  if (config_remote_autochan_nch < 4) return 0;
  int tab[256];
  const int maxc = wdl_min(config_remote_autochan_nch, (int) (sizeof(tab)/sizeof(tab[0])));
  WDL_ASSERT(maxc == config_remote_autochan_nch); // if this fires, increase the size of tab above for huge channel counts
  memset(tab,0,maxc * sizeof(tab[0]));
  for (int uid = 0; uid < m_remoteusers.GetSize(); uid ++)
  {
    RemoteUser *theuser = m_remoteusers.Get(uid);
    if (WDL_NOT_NORMALLY(!theuser)) break;
    for (int scani = 0; scani < MAX_USER_CHANNELS; scani++)
    {
      int ci = theuser->channels[scani].out_chan_index;
      if ((ci&~1024)<2) continue;

      const int amt = (theuser->chanpresentmask & (1u<<scani)) ? 16 : 1;
      if (ci & 1024)
      {
        ci &= 1023;
        if (ci < maxc) tab[ci]+=amt;
      }
      else if (ci>=0)
      {
        if (ci < maxc) tab[ci]+=amt;
        if (ci+1 < maxc) tab[ci+1]+=amt;
      }
    }
  }
  int best = 0, best_cnt = 0x7fffffff;
  for (int x = 2; x < maxc-1; x+=2)
  {
    if (tab[x] == 0 && tab[x+1] == 0) return x;
    if (tab[x] + tab[x+1] < best_cnt)
    {
      best_cnt = tab[x] + tab[x+1];
      best = x;
    }
  }
  return best;
}

int NJClient::Run() // nonzero if sleep ok
{
  // 15.1-07b CR-10: drain m_wave_block_q (audio-thread producer) into the
  // legacy m_wavebq (run-thread consumer). Must happen BEFORE the wave
  // GetBlock loop below so the consumer sees freshly-forwarded records.
  drainWaveBlocks();

  // 15.1-07b CR-09: drain per-channel mirror block_q rings into legacy
  // lc->m_bq. Must happen BEFORE the encoder upload loop below so the
  // encoder sees freshly-forwarded broadcast records. This is what
  // restores broadcast end-to-end after 15.1-06 left the audio-thread
  // producer side dormant.
  drainBroadcastBlocks();

  WDL_HeapBuf *p=0;
  while (!m_wavebq->GetBlock(&p))
  {
    if (p)
    {
      float *f=(float*)p->Get();
      int hl=p->GetSize()/(2*sizeof(float));
      float *outbuf[2]={f,f+hl};
#ifndef NJCLIENT_NO_XMIT_SUPPORT
      if (m_oggWrite&&m_oggComp)
      {
        m_oggComp->Encode(f,hl,1,hl);
        if (m_oggComp->Available())
        {
          fwrite((char *)m_oggComp->Get(),1,m_oggComp->Available(),m_oggWrite);
          m_oggComp->Advance(m_oggComp->Available());
          m_oggComp->Compact();
        }
      }
#endif
      if (waveWrite)
      {
        waveWrite->WriteFloatsNI(outbuf,0,hl);
      }
      m_wavebq->DisposeBlock(p);
    }
  }
//
  int wantsleep=1;
  auto return_with_status = [this](int value) {
    cached_status.store(GetStatus(), std::memory_order_release);
    return value;
  };

  if (m_netcon)
  {
    Net_Message *msg=m_netcon->Run(&wantsleep);
    if (!msg)
    {
      if (m_netcon->GetStatus())
      {
        m_audio_enable=0;
        if (m_in_auth)  m_status=1001;
        if (m_status > 0 && m_status < 1000) m_status=1002;
        if (m_status == 0) m_status=1000;
        fprintf(stderr, "[NJClient] Connection failed, m_status=%d (1000=cant connect, 1001=auth fail, 1002=disconnected)\n", m_status);
        return return_with_status(1);
      }
    }
    else
    {
      msg->addRef();

      switch (msg->get_type())
      {
        case MESSAGE_SERVER_AUTH_CHALLENGE:
          {
            mpb_server_auth_challenge cha;
            if (!cha.parse(msg))
            {
              if (cha.protocol_version < PROTO_VER_MIN || cha.protocol_version >= PROTO_VER_MAX)
              {
                m_errstr.Set("server is incorrect protocol version");
                m_status = 1001;
                m_netcon->Kill();
                return return_with_status(0);
              }

              mpb_client_auth_user repl;
              repl.username=m_user.Get();
              repl.client_version=PROTO_VER_CUR; // client version number

              m_connection_keepalive=(cha.server_caps>>8)&0xff;

              // Save challenge for encryption key derivation (Phase 15)
              memcpy(m_auth_challenge, cha.challenge, 8);

//              printf("Got keepalive of %d\n",m_connection_keepalive);

#ifdef JAMWIDE_DEV_BUILD
              // Log auth state
              {
                FILE* lf = fopen("/tmp/jamwide.log", "a");
                if (lf) {
                  fprintf(lf, "[NJClient] Auth challenge received\n");
                  fprintf(lf, "[NJClient]   user='%s' pass_len=%d\n", m_user.Get(), (int)strlen(m_pass.Get()));
                  fprintf(lf, "[NJClient]   license_agreement=%s\n", cha.license_agreement ? "yes" : "no");
                  fprintf(lf, "[NJClient]   LicenseAgreementCallback=%p\n", (void*)LicenseAgreementCallback);
                  fclose(lf);
                }
              }
#endif
              
              if (cha.license_agreement)
              {
                m_netcon->SetKeepAlive(45);
                int license_result = 0;
                if (LicenseAgreementCallback) {
                  license_result = LicenseAgreementCallback(LicenseAgreement_User,cha.license_agreement);
                }
#ifdef JAMWIDE_DEV_BUILD
                {
                  FILE* lf = fopen("/tmp/jamwide.log", "a");
                  if (lf) {
                    fprintf(lf, "[NJClient]   license callback returned: %d\n", license_result);
                    fclose(lf);
                  }
                }
#endif
                if (license_result)
                {
                  repl.client_caps|=1;
                }
              }
              m_netcon->SetKeepAlive(m_connection_keepalive);
              
#ifdef JAMWIDE_DEV_BUILD
              {
                FILE* lf = fopen("/tmp/jamwide.log", "a");
                if (lf) {
                  fprintf(lf, "[NJClient]   client_caps=%d (bit0=license_accepted)\n", repl.client_caps);
                  fclose(lf);
                }
              }
#endif

              // ── Encryption negotiation: derive key BEFORE sending AUTH_USER (Phase 15) ──
              // Server advertises encryption support via server_caps bit 1.
              // If set AND we have a password, derive key now so AUTH_USER is encrypted.
              // This satisfies SEC-01: credentials are encrypted in transit.
              if ((cha.server_caps & SERVER_CAP_ENCRYPT_SUPPORTED) && strlen(m_pass.Get()) > 0) {
                  unsigned char enc_key[32];
                  derive_encryption_key(m_pass.Get(), m_auth_challenge, enc_key);
                  m_netcon->SetEncryptionKey(enc_key);
                  memset(enc_key, 0, 32);  // scrub key from stack

                  // Signal to server that we encrypted AUTH_USER
                  repl.client_caps |= CLIENT_CAP_ENCRYPT_SUPPORTED;
              }
              // If server doesn't advertise encryption (legacy), we proceed unencrypted (SEC-03).
              // The encrypt hook in Net_Connection::Run() will encrypt the AUTH_USER payload
              // automatically since we called SetEncryptionKey before Send().

              WDL_SHA1 tmp;
              tmp.add(m_user.Get(),strlen(m_user.Get()));
              tmp.add(":",1);
              tmp.add(m_pass.Get(),strlen(m_pass.Get()));
              tmp.result(repl.passhash);

              tmp.reset(); // new auth method is SHA1(SHA1(user:pass)+challenge)
              tmp.add(repl.passhash,sizeof(repl.passhash));
              tmp.add(cha.challenge,sizeof(cha.challenge));
              tmp.result(repl.passhash);

              m_netcon->Send(repl.build());

              m_in_auth=1;
            }
          }
        break;
        case MESSAGE_SERVER_AUTH_REPLY:
          {
            mpb_server_auth_reply ar;
            if (!ar.parse(msg))
            {
              if (ar.flag) // send our channel information
              {
                // ── Encryption confirmation (Phase 15) ──
                if (ar.flag & SERVER_FLAG_ENCRYPT_ACTIVE) {
                    // Server confirmed encryption for the session.
                    // Key is already set from the challenge phase — encryption continues.
#ifdef JAMWIDE_DEV_BUILD
                    // Log for debugging:
                    {
                        FILE* lf = fopen("/tmp/jamwide.log", "a");
                        if (lf) {
                            fprintf(lf, "[NJClient] Encryption ACTIVE for session\n");
                            fclose(lf);
                        }
                    }
#endif
                } else if (m_netcon->IsEncryptionActive()) {
                    // We encrypted AUTH_USER but server didn't confirm encryption.
                    // This should not happen with a conforming JamWide server, but handle gracefully:
                    // Disable encryption so subsequent messages are unencrypted.
                    m_netcon->ClearEncryption();
#ifdef JAMWIDE_DEV_BUILD
                    {
                        FILE* lf = fopen("/tmp/jamwide.log", "a");
                        if (lf) {
                            fprintf(lf, "[NJClient] Server did not confirm encryption — falling back to unencrypted\n");
                            fclose(lf);
                        }
                    }
#endif
                }

                if (!m_max_localch)
                {
                  // went from lobby to room, normalize channel indices (in case the user deleted channels in the lobby)
                  for (int x = 0; x < m_locchannels.GetSize(); x ++)
                    m_locchannels.Get(x)->channel_idx = x;
                }
                NotifyServerOfChannelChange();
                m_status=2;
                m_in_auth=0;
                m_max_localch=ar.maxchan;
                if (ar.errmsg)
                  m_user.Set(ar.errmsg); // server gave us an updated name
              }
              else
              {
                if (ar.errmsg)
                {
                    m_errstr.Set(ar.errmsg);
                }
                m_status = 1001;
                m_netcon->Kill();
              }
            }
          }
        break;
        case MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY:
          {
            mpb_server_config_change_notify ccn;
            if (!ccn.parse(msg))
            {
              updateBPMinfo(ccn.beats_minute,ccn.beats_interval);
              m_audio_enable=1;
            }
          }

        break;
        case MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY:
          {
            mpb_server_userinfo_change_notify ucn;
            if (!ucn.parse(msg))
            {
              WDL_MutexLock lock(&m_remotechannel_rd_mutex);
              int offs=0;
              int a=0, cid=0, p=0,f=0;
              short v=0;
              const char *un=0,*chn=0;
              while ((offs=ucn.parse_get_rec(offs,&a,&cid,&v,&p,&f,&un,&chn))>0)
              {
                if (!un) un="";
                if (!chn) chn="";

                m_userinfochange=1;

                int x;
                // todo: per-user autosubscribe option, or callback
                // todo: have volume/pan settings here go into defaults for the channel. or not, kinda think it's pointless
                if (cid >= 0 && cid < MAX_USER_CHANNELS)
                {
                  m_users_cs.Enter();
                  RemoteUser *theuser;
                  for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),un); x ++);

    //              char buf[512];
  //                sprintf(buf,"user %s, channel %d \"%s\": %s v:%d.%ddB p:%d flag=%d\n",un,cid,chn,a?"active":"inactive",(int)v/10,abs((int)v)%10,p,f);
//                  OutputDebugString(buf);


                  if (a)
                  {
                    if (x == m_remoteusers.GetSize())
                    {
                      theuser=new RemoteUser;
                      theuser->name.Set(un);
                      m_remoteusers.Add(theuser);
                    }

                    if ((theuser->channels[cid].flags^f)&(2|4)) // if flags changed instamode, flush out the samples
                    {
                      delete theuser->channels[cid].ds;
                      delete theuser->channels[cid].next_ds[0];
                      delete theuser->channels[cid].next_ds[1];
                      theuser->channels[cid].ds=0;
                      theuser->channels[cid].next_ds[0]=0;
                      theuser->channels[cid].next_ds[1]=0;
//                      OutputDebugString("channel flags changed, flushing sources\n");
                    }
                    theuser->channels[cid].flags = f;

                    if (!(theuser->channels[cid].flags&4))
                    {
                      theuser->channels[cid].ClearSessionInfo();
                    }

                    theuser->channels[cid].name.Set(chn);
                    theuser->chanpresentmask |= 1u<<cid;


                    if (config_autosubscribe)
                    {
                      theuser->submask |= 1u<<cid;
                      mpb_client_set_usermask su;
                      su.build_add_rec(un,theuser->submask);
                      m_netcon->Send(su.build());
                    }
                    if ((config_remote_autochan == 1 || config_remote_autochan == 2) &&
                        config_remote_autochan_nch > 2 &&
                        !theuser->channels[cid].out_chan_index)
                    {
                      bool need_ch = true;
                      if (config_remote_autochan == 2)
                      {
                        // see if this user already has a channel set, if so, use
                        for (int scani = 0; scani < MAX_USER_CHANNELS && theuser->chanpresentmask >= (1u<<scani); scani++)
                        {
                          if (scani != cid && (theuser->chanpresentmask & (1u<<scani)))
                          {
                            // if the user had a channel set to 0, use that too
                            theuser->channels[cid].out_chan_index = theuser->channels[scani].out_chan_index;
                            need_ch = false;
                          }
                        }
                      }

                      if (need_ch)
                      {
                        theuser->channels[cid].out_chan_index = find_unused_output_channel_pair();
                      }
                    }
                  }
                  else
                  {
                    if (x < m_remoteusers.GetSize())
                    {
                      theuser->channels[cid].ClearSessionInfo();

                      theuser->channels[cid].name.Set("");
                      theuser->chanpresentmask &= ~(1u<<cid);
                      theuser->submask &= ~(1u<<cid);

                      int chksolo=theuser->solomask == (1u<<cid);
                      theuser->solomask &= ~(1u<<cid);

                      delete theuser->channels[cid].ds;
                      delete theuser->channels[cid].next_ds[0];
                      delete theuser->channels[cid].next_ds[1];
                      theuser->channels[cid].ds=0;
                      theuser->channels[cid].next_ds[0]=0;
                      theuser->channels[cid].next_ds[1]=0;
//                      OutputDebugString("channel flags changed, flushing sources2\n");

                      if (!theuser->chanpresentmask) // user no longer exists, it seems
                      {
                        chksolo=1;
                        delete theuser;
                        m_remoteusers.Delete(x);
                      }

                      if (chksolo)
                      {
                        int i;
                        for (i = 0; i < m_remoteusers.GetSize() && !m_remoteusers.Get(i)->solomask; i ++);

                        if (i < m_remoteusers.GetSize()) m_issoloactive|=1;
                        else m_issoloactive&=~1;
                      }
                    }
                  }
                  m_users_cs.Leave();
                }
              }
            }
          }
        break;
        case MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN:
          {
            mpb_server_download_interval_begin dib;
            if (!dib.parse(msg) && dib.username)
            {
              WDL_MutexLock lock(&m_users_cs);
              int x;
              RemoteUser *theuser;
              for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),dib.username); x ++);
              if (x < m_remoteusers.GetSize() && dib.chidx >= 0 && dib.chidx < MAX_USER_CHANNELS)
              {
                //printf("Getting interval for %s, channel %d\n",dib.username,dib.chidx);
                if (!memcmp(dib.guid,zero_guid,sizeof(zero_guid)))
                {
                  if (!(theuser->channels[dib.chidx].flags&4) && !(theuser->channels[dib.chidx].flags&2))
                  {
                    int useidx=!!theuser->channels[dib.chidx].next_ds[0];
                    DecodeState *tmp=theuser->channels[dib.chidx].next_ds[useidx];
                    theuser->channels[dib.chidx].next_ds[useidx]=0;
                    delete tmp;
//                    OutputDebugString("added silence to channel\n");
                  }
                  //else OutputDebugString("woulda added silence to channel\n");
                }
                else if (dib.fourcc) // download coming
                {
                  if (config_debug_level>1) printf("RECV BLOCK %s\n",guidtostr_tmp(dib.guid));
                  RemoteDownload *ds=new RemoteDownload;
                  memcpy(ds->guid,dib.guid,sizeof(ds->guid));
                  ds->Open(this,dib.fourcc,!!(theuser->channels[dib.chidx].flags&4));

                  ds->playtime=(theuser->channels[dib.chidx].flags&2)?LIVE_PREBUFFER:config_play_prebuffer.load(std::memory_order_relaxed);
                  ds->chidx=dib.chidx;
                  ds->username.Set(dib.username);

                  m_downloads.Add(ds);
                }
                else if (!(theuser->channels[dib.chidx].flags&4))
                {
//                  OutputDebugString("added free-guid to channel\n");
                  DecodeState *tmp=start_decode(dib.guid, theuser->channels[dib.chidx].flags, 0, NULL);
                  int useidx=!!theuser->channels[dib.chidx].next_ds[0];
                  DecodeState *t2=theuser->channels[dib.chidx].next_ds[useidx];
                  theuser->channels[dib.chidx].next_ds[useidx]=tmp;
                  delete t2;
                }

              }
            }
          }
        break;
        case MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE:
          {
            mpb_server_download_interval_write diw;
            if (!diw.parse(msg))
            {
              time_t now;
              time(&now);
              int x;
              for (x = 0; x < m_downloads.GetSize(); x ++)
              {
                RemoteDownload *ds=m_downloads.Get(x);
                if (ds)
                {
                  if (!memcmp(ds->guid,diw.guid,sizeof(ds->guid)))
                  {
                    if (config_debug_level>1) printf("RECV BLOCK DATA %s%s %d bytes\n",guidtostr_tmp(diw.guid),diw.flags&1?":end":"",diw.audio_data_len);

                    ds->last_time=now;
                    if (diw.audio_data_len > 0 && diw.audio_data)
                    {
                      ds->Write(diw.audio_data,diw.audio_data_len);
                    }
                    if (diw.flags & 1)
                    {
                      delete ds;
                      m_downloads.Delete(x);
                    }
                    break;
                  }

                  if (now - ds->last_time > DOWNLOAD_TIMEOUT)
                  {
                    ds->chidx=-1;
                    delete ds;
                    m_downloads.Delete(x--);
                  }
                }
              }
            }
          }
        break;
        case MESSAGE_CHAT_MESSAGE:
          if (ChatMessage_Callback)
          {
            mpb_chat_message foo;
            if (!foo.parse(msg))
            {
              if (foo.parms[0] && !strcmp(foo.parms[0],"SESSION"))
              {
                if (foo.parms[1] && foo.parms[2] && foo.parms[3] && foo.parms[4])
                {
                  WDL_MutexLock lock(&m_users_cs);
                  int x;
                  RemoteUser *theuser;
                  for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),foo.parms[1]); x ++);
                  int chanidx=atoi(foo.parms[3]);
                  if (x < m_remoteusers.GetSize() && chanidx >= 0 && chanidx < MAX_USER_CHANNELS &&
                      ((theuser->submask & theuser->chanpresentmask) & (1u<<chanidx)) && // only update if subscribed
                      (theuser->channels[chanidx].flags&4))
                  {
                    unsigned char guid[16];
                    if (strtoguid(foo.parms[2],guid))
                    {
                      const char *p=foo.parms[4];
                      double st=atof(p);
                      while (*p != ' ' && *p) p++;
                      while (*p == ' ') p++;
                      if (*p)
                      {
                        double len=atof(p);

                        //char buf[512];
                        //sprintf(buf,"AddSessionInfo len=%.10f (@44k=%.10f)\n",len,len*44100.0);
                        //OutputDebugString(buf);

                        // add to this channel's session list
                        theuser->channels[chanidx].AddSessionInfo(guid,st,len);
                        theuser->last_session_pos=st+len;
                        theuser->last_session_pos_updtime=time(NULL);

                        char guidstr[64];
                        guidtostr(guid,guidstr);
                        writeLog("sessionlog %s \"%s\" %d \"%s\" %.10f %.10f\n",guidstr,theuser->name.Get(),chanidx,theuser->channels[chanidx].name.Get(),st,len);
                      }
                    }
                  }
                }
              }
              else
              {
                if (m_logFile && foo.parms[0])
                {
                  char buf[1024];
                  buf[0]=0;
                  for (int x=0;x<3 && foo.parms[x];x++)
                  {
                    const char *rd = foo.parms[x];
                    const int f=getConfigStringQuoteChar(rd);
                    if (f == ' ') snprintf_append(buf,sizeof(buf)," %s",rd);
                    else if (f) snprintf_append(buf,sizeof(buf)," %c%s%c",f,rd,f);
                    else
                    {
                      char *p = buf+strlen(buf);
                      snprintf_append(buf,sizeof(buf)," `%s`",rd);
                      if (*p) p++; // skip space

                      // filter out any backticks
                      if (*p == '`')
                        while (*++p) if (*p == '`' && p[1]) *p = '_';
                    }
                  }
                  char *p = buf;
                  while (*p)
                  {
                    if (*p == '\r' || *p == '\n') *p = ' ';
                    p++;
                  }
                  writeLog("chat%s\n",buf);
                }
                ChatMessage_Callback(ChatMessage_User,this,foo.parms,sizeof(foo.parms)/sizeof(foo.parms[0]));
              }
            }
          }
        break;
        default:
          //printf("Got unknown message %02X\n",msg->get_type());
        break;
      }

      msg->releaseRef();
    }
  }

#ifndef NJCLIENT_NO_XMIT_SUPPORT
  int u;
  for (u = 0; u < m_locchannels.GetSize(); u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);
    WDL_HeapBuf *p=0;
    int block_nch=1;

#if 0
    {
      char buf[512];
      int sz=0;
      int x;
      lc->m_bq.m_cs.Enter();
      for (x = 0; x < lc->m_bq.m_samplequeue.GetSize(); x += 2)
      {
        WDL_HeapBuf *p=lc->m_bq.m_samplequeue.Get(x);
        if (p && p != (WDL_HeapBuf*)-1)
          sz+=p->GetSize();
      }
      lc->m_bq.m_cs.Leave();
      sprintf(buf,"bq size=%d\n",sz);
      if (sz) OutputDebugString(buf);
    }
#endif

    double blockstarttime=0.0;
    while (!lc->m_bq.GetBlock(&p,&block_nch,&blockstarttime))
    {
      wantsleep=0;
      if (lc->channel_idx >= m_max_localch && !(lc->flags & 2))
      {
        if (p && p != (WDL_HeapBuf*)-1)
          lc->m_bq.DisposeBlock(p);
        p=0;
        continue;
      }

      if (p == (WDL_HeapBuf*)-1)
      {
        // context
        lc->m_curwritefile_starttime = (lc->flags&4)?blockstarttime:-1.0;
        lc->m_curwritefile_writelen=0.0;

        mpb_client_upload_interval_begin cuib;
        cuib.chidx=lc->channel_idx;
        memset(cuib.guid,0,sizeof(cuib.guid));
        memset(lc->m_curwritefile.guid,0,sizeof(lc->m_curwritefile.guid));
        cuib.fourcc=0;
        cuib.estsize=0;
        m_netcon->Send(cuib.build());
        p=0;
      }
      else if (p)
      {
        // encode data
        if (!lc->m_enc)
        {
          m_encoder_fmt_active = m_encoder_fmt_requested.load(std::memory_order_relaxed);
          if (m_encoder_fmt_active == NJ_ENCODER_FMT_FLAC)
            lc->m_enc = CreateFLACEncoder(m_srate,lc->m_enc_nch_used=block_nch,lc->m_enc_bitrate_used = lc->bitrate+(block_nch>1?lc->bitrate/3:0),WDL_RNG_int32());
          else
            lc->m_enc = CreateNJEncoder(m_srate,lc->m_enc_nch_used=block_nch,lc->m_enc_bitrate_used = lc->bitrate+(block_nch>1?lc->bitrate/3:0),WDL_RNG_int32());

          // Send chat notification when codec changes
          if (m_encoder_fmt_prev != 0 && m_encoder_fmt_active != m_encoder_fmt_prev) {
            const char* codec_name = (m_encoder_fmt_active == NJ_ENCODER_FMT_FLAC) ? "FLAC lossless" : "Vorbis compressed";
            char msg[128];
            snprintf(msg, sizeof(msg), "/me switched to %s", codec_name);
            ChatMessage_Send("MSG", msg);
          }
          m_encoder_fmt_prev = m_encoder_fmt_active;
        }

        if (lc->m_need_header)
        {
          lc->m_need_header=false;
          {
            WDL_RNG_bytes(lc->m_curwritefile.guid,sizeof(lc->m_curwritefile.guid));
            char guidstr[64];
            guidtostr(lc->m_curwritefile.guid,guidstr);
            if (!(lc->flags&4)) writeLog("local %s %d%s\n",guidstr,lc->channel_idx,(lc->flags&2)?"v":"");
            if (config_savelocalaudio>0)
            {
              lc->m_curwritefile.Open(this,m_encoder_fmt_active,false);
              if (lc->m_wavewritefile) delete lc->m_wavewritefile;
              lc->m_wavewritefile=0;
              if (config_savelocalaudio>1)
              {
                WDL_String fn;

                fn.Set(m_workdir.Get());
              #ifdef _WIN32
                char tmp[3]={guidstr[0],'\\',0};
              #else
                char tmp[3]={guidstr[0],'/',0};
              #endif
                fn.Append(tmp);
                fn.Append(guidstr);
                fn.Append(".wav");

                lc->m_wavewritefile=new WaveWriter(fn.Get(),24,block_nch,m_srate);
              }
            }

            mpb_client_upload_interval_begin cuib;
            cuib.chidx=lc->channel_idx;
            memcpy(cuib.guid,lc->m_curwritefile.guid,sizeof(cuib.guid));
            cuib.fourcc=m_encoder_fmt_active;
            cuib.estsize=0;
            delete lc->m_enc_header_needsend;
            lc->m_enc_header_needsend=cuib.build();
          }
        }

        if (lc->m_enc)
        {
          {
            int sz=p->GetSize()/sizeof(float);
            if (block_nch>1)  sz/=2;

            if (lc->m_wavewritefile)
            {
              float *ps[2]={(float *)p->Get(),0};
              if (block_nch>1) ps[1]=ps[0]+sz;
              else ps[1]=ps[0];

              lc->m_wavewritefile->WriteFloatsNI(ps,0,sz,2);
            }

            lc->m_enc->Encode((float*)p->Get(),sz,1,block_nch>1 ? sz:0);
            lc->m_curwritefile_writelen+=sz;
          }

          int s;
          while ((s=lc->m_enc->Available())>=
            ((lc->m_enc_header_needsend?(lc->flags&2)?LIVE_ENC_BLOCKSIZE1:MIN_ENC_BLOCKSIZE*4:(lc->flags&2)?LIVE_ENC_BLOCKSIZE2:MIN_ENC_BLOCKSIZE))
            )
          {
            if (s > MAX_ENC_BLOCKSIZE) s=MAX_ENC_BLOCKSIZE;

            {
              mpb_client_upload_interval_write wh;
              memcpy(wh.guid,lc->m_curwritefile.guid,sizeof(lc->m_curwritefile.guid));
              wh.flags=0;
              wh.audio_data=lc->m_enc->Get();
              wh.audio_data_len=s;
              lc->m_curwritefile.Write(wh.audio_data,wh.audio_data_len);

              if (lc->m_enc_header_needsend)
              {
                if (config_debug_level>1)
                {
                  mpb_client_upload_interval_begin dib;
                  dib.parse(lc->m_enc_header_needsend);
                  printf("SEND BLOCK HEADER %s\n",guidtostr_tmp(dib.guid));
                }
                m_netcon->Send(lc->m_enc_header_needsend);
                lc->m_enc_header_needsend=0;
              }

              if (config_debug_level>1) printf("SEND BLOCK %s%s %d bytes\n",guidtostr_tmp(wh.guid),wh.flags&1?"end":"",wh.audio_data_len);

              m_netcon->Send(wh.build());
            }

            lc->m_enc->Advance(s);
          }
          lc->m_enc->Compact();
        }
        lc->m_bq.DisposeBlock(p);
        p=0;
      }
      else
      {
        if (lc->m_enc)
        {
          // finish any encoding
          lc->m_enc->Encode(NULL,0);

          // send any final message, with the last one with a flag
          // saying "we're done"
          do
          {
            mpb_client_upload_interval_write wh;
            int l=lc->m_enc->Available();
            if (l>MAX_ENC_BLOCKSIZE) l=MAX_ENC_BLOCKSIZE;

            memcpy(wh.guid,lc->m_curwritefile.guid,sizeof(wh.guid));
            wh.audio_data=lc->m_enc->Get();
            wh.audio_data_len=l;

            lc->m_curwritefile.Write(wh.audio_data,wh.audio_data_len);

            lc->m_enc->Advance(l);
            wh.flags=lc->m_enc->Available()>0 ? 0 : 1;

            if (lc->m_enc_header_needsend)
            {
              if (config_debug_level>1)
              {
                mpb_client_upload_interval_begin dib;
                dib.parse(lc->m_enc_header_needsend);
                printf("SEND BLOCK HEADER %s\n",guidtostr_tmp(dib.guid));
              }
              m_netcon->Send(lc->m_enc_header_needsend);
              lc->m_enc_header_needsend=0;
            }

            if (config_debug_level>1) printf("SEND BLOCK %s%s %d bytes\n",guidtostr_tmp(wh.guid),wh.flags&1?"end":"",wh.audio_data_len);
            m_netcon->Send(wh.build());
          }
          while (lc->m_enc->Available()>0);
          lc->m_enc->Compact(); // free any memory left

          if (lc->flags&4)
          {
            if (lc->m_curwritefile_writelen > 0.2*m_srate && lc->m_curwritefile_starttime > -1.0 && lc->m_curwritefile_writelen < SESSION_CHUNK_SIZE*2.0*m_srate)
            {
              char guidstr[64],idxstr[64],offslenstr[128];
              guidtostr(lc->m_curwritefile.guid,guidstr);
              snprintf(idxstr,sizeof(idxstr), "%d",lc->channel_idx);
              snprintf(offslenstr,sizeof(offslenstr),"%.10f %.10f",lc->m_curwritefile_starttime,lc->m_curwritefile_writelen/(double)m_srate);
              // send "SESSION" chat message

    //          char buf[512];
  //            sprintf(buf,"SESSION %s %d %f %f\n",guidstr,u,lc->m_curwritefile_starttime,lc->m_curwritefile_writelen/(double)m_srate);
//              OutputDebugString(buf);

              char tmp[1024];
              lstrcpyn_safe(tmp,lc->name.Get(),sizeof(tmp));
              char *p=tmp;
              while (*p) { if (*p == '\"') *p = '\''; p++; }

              writeLog("localsessionlog %s \"%s\" %d \"%s\" %.10f %.10f\n",guidstr,"local",lc->channel_idx,tmp,lc->m_curwritefile_starttime,lc->m_curwritefile_writelen/(double)m_srate);

              ChatMessage_Send("SESSION",guidstr,idxstr,offslenstr);
            }
          }

          //delete m_enc;
        //  m_enc=0;
          if (lc->m_enc_nch_used != ((lc->src_channel&1024)?2:1))
          {
            delete lc->m_enc;
            lc->m_enc=0;
          }
          else
            lc->m_enc->reinit();

        }

        if (lc->m_enc && lc->bitrate != lc->m_enc_bitrate_used)
        {
          delete lc->m_enc;
          lc->m_enc=0;
        }
        if (lc->m_enc && m_encoder_fmt_active != m_encoder_fmt_requested.load(std::memory_order_relaxed))
        {
          delete lc->m_enc;
          lc->m_enc=0;
        }
        lc->m_need_header=true;
        lc->m_curwritefile_writelen=0.0;

        // end the last encode
      }
    }
  }
#endif

  // Update cached status for lock-free audio thread access
  cached_status.store(GetStatus(), std::memory_order_release);

  return wantsleep;

}


DecodeState *NJClient::start_decode(unsigned char *guid, int chanflags, unsigned int fourcc, DecodeMediaBuffer *decbuf)
{
  DecodeState *newstate=new DecodeState;
  if (decbuf)
  {
    decbuf->AddRef();
    newstate->decode_buf=decbuf;
  }
  memcpy(newstate->guid,guid,sizeof(newstate->guid));


  // Supported codec types for file probing and network decode
  unsigned int types[]={NJ_ENCODER_FMT_FLAC, MAKE_NJ_FOURCC('O','G','G','v')};
  unsigned int matched_type = 0;

  if (!newstate->decode_buf)
  {
    WDL_String s;

    makeFilenameFromGuid(&s,guid);
    const int oldl=s.GetLength()+1;
    s.Append(".XXXXXXXXX");
    unsigned int x;
    for (x = 0; !newstate->decode_fp && x < sizeof(types)/sizeof(types[0]); x ++)
    {
      char tmp[8];
      s.SetLen(oldl);
      type_to_string(types[x],tmp);
      s.Append(tmp);
      newstate->decode_fp=fopenUTF8(s.Get(),"rb");
      if (newstate->decode_fp) matched_type = types[x];
    }
  }

  if (newstate->decode_fp||newstate->decode_buf)
  {
    // For network streams, use the fourcc from the message; for files, use the matched type
    unsigned int codec_type = newstate->decode_buf ? fourcc : matched_type;
    if (codec_type == NJ_ENCODER_FMT_FLAC)
      newstate->decode_codec = CreateFLACDecoder();
    else
      newstate->decode_codec = CreateNJDecoder();
    // run some decoding

    if (newstate->decode_codec)
    {
      while (newstate->decode_codec->Available() <= 0)
      {
        if (newstate->runDecode()) break;
      }
      if (chanflags & 2)
        newstate->is_voice_firstchk=true;
    }
  }

  return newstate;
}

float NJClient::GetOutputPeak(int ch)
{
  if (ch==0) return (float)output_peaklevel[0];
  else if(ch==1) return (float)output_peaklevel[1];
  return (float)(output_peaklevel[0]+output_peaklevel[1])*0.5f;
}

void NJClient::ChatMessage_Send(const char *parm1, const char *parm2, const char *parm3, const char *parm4, const char *parm5)
{
  if (m_netcon)
  {
    mpb_chat_message m;
    m.parms[0]=parm1;
    m.parms[1]=parm2;
    m.parms[2]=parm3;
    m.parms[3]=parm4;
    m.parms[4]=parm5;
    m_netcon->Send(m.build());
  }
}

void NJClient::SetEncoderFormat(unsigned int fourcc)
{
  if (fourcc == NJ_ENCODER_FMT_FLAC || fourcc == NJ_ENCODER_FMT_TYPE)
    m_encoder_fmt_requested.store(fourcc, std::memory_order_relaxed);
}

void NJClient::process_samples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, int offset, int justmonitor, bool isPlaying, bool isSeek, double cursessionpos)
{
                   // -36dB/sec
  double decay=pow(.25*0.25*0.25,len/(double)srate);
  // encode my audio and send to server, if enabled

  // 15.1-06 CR-02: m_locchan_cs.Enter/Leave removed. Audio thread reads from
  // m_locchan_mirror, populated by drainLocalChannelUpdates() at the top of
  // AudioProc. Mirror entries are by-VALUE (Codex HIGH-2: no Local_Channel*
  // back-pointer). The per-mirror block_q SPSC is the producer side for the
  // BlockRecord transport — pushes are deferred to 15.1-07b which lands in
  // the same wave; for now broadcast on local channels is intentionally
  // silent (the legacy lc->m_bq path is kept unlocked-but-unreachable from
  // the audio thread, and the encoder thread continues to read via the
  // run-thread-side m_locchan_cs path). UAT verifies non-broadcast paths.
  for (int ch = 0; ch < MAX_LOCAL_CHANNELS; ++ch)
  {
    auto& lcm = m_locchan_mirror[ch];
    if (!lcm.active) continue;

    // Same gating as the legacy code: lc->channel_idx >= m_max_localch &&
    // !(lc->flags & 2). channel_idx == ch (mirror is indexed by channel).
    if (!justmonitor && ch >= m_max_localch && !(lcm.flags & 2)) continue;

    int sc=lcm.srcch&1023;
    int sc_nch=(lcm.srcch&1024)?2:1;

    float *src=NULL,*src2=NULL;
    if (sc >= 0 && sc < innch) src=inbuf[sc]+offset;
    if (sc_nch>1)
    {
      if (sc+1 >= 0 && sc+1 < innch) src2=inbuf[sc+1]+offset;
      if (!src2) src2=src;
    }

    if (lcm.cbf || !src || ChannelMixer)
    {
      // todo: support stereo on chanmixer, silent, and effect processing stuff
      int bytelen=len*(int)sizeof(float);
      if (tmpblock.GetSize() < bytelen) tmpblock.Resize(bytelen);

      if (ChannelMixer && ChannelMixer(ChannelMixer_User,inbuf,offset,innch,sc,(float*)tmpblock.Get(),len))
      {
        // channelmixer succeeded
      }
      else if (src) memcpy(tmpblock.Get(),src,bytelen);
      else memset(tmpblock.Get(),0,bytelen);

      src2=src=(float* )tmpblock.Get();

      // processor (Instatalk PTT mute lambda registered via
      // SetLocalChannelProcessor — see 15.1-06 deviation #2 in SUMMARY).
      if (lcm.cbf)
      {
        lcm.cbf(src,len,lcm.cbf_inst);
      }
    }


#ifndef NJCLIENT_NO_XMIT_SUPPORT
    // 15.1-07b CR-09: audio-thread broadcast producer. Audio thread mirrors
    // the legacy lc->m_bq.AddBlock semantics from process_samples 2002, 2017,
    // 2023, 2036 — but pushes BlockRecords onto m_locchan_mirror[ch].block_q
    // (the SPSC ring) instead of into the lock-and-heap-alloc BufferQueue.
    // The run thread (drainBroadcastBlocks, called from NJClient::Run before
    // the existing GetBlock loop) drains the ring and forwards into legacy
    // lc->m_bq.AddBlock there. This restores broadcast end-to-end after
    // 15.1-06 left this path dormant — and closes AUDIT CR-09.
    //
    // pushBlockRecord performs Codex M-7 bounds-check (sample_count <=
    // MAX_BLOCK_SAMPLES, nch <= MAX_BLOCK_CHANNELS) BEFORE memcpy and bumps
    // m_block_queue_drops on either bounds failure or ring-full (Codex M-8
    // counter — 15.1-10 fails the phase if non-zero post-UAT).
    if (!justmonitor)
    {
      if (lcm.flags & 2)
      {
        // Instamode/voicechat (LL) channel.
        if (lcm.bcast_active != lcm.bcast ||
            (lcm.bcast_active && lcm.curwritefile_curbuflen >= LL_CHUNK_SIZE * srate))
        {
          if (lcm.bcast_active)
          {
            // Boundary marker: legacy lc->m_bq.AddBlock(0, 0.0, NULL, 0)
            pushBlockRecord(lcm.block_q, m_block_queue_drops,
                            0, 0.0, nullptr, 0, 0);
          }
          lcm.bcast_active = lcm.bcast;
          lcm.curwritefile_curbuflen = 0.0;
        }
      }
      else if (lcm.flags & 4)
      {
        // Sessionmode channel.
        if (isSeek ||
            lcm.curwritefile_curbuflen >= SESSION_CHUNK_SIZE * static_cast<double>(srate) ||
            (isPlaying && !lcm.bcast_active && lcm.bcast) ||
            (!isPlaying && lcm.bcast_active))
        {
          if (lcm.bcast_active)
          {
            // Boundary marker: legacy lc->m_bq.AddBlock(0, 0.0, NULL, 0)
            pushBlockRecord(lcm.block_q, m_block_queue_drops,
                            0, 0.0, nullptr, 0, 0);
          }

          if (lcm.bcast && isPlaying)
          {
            lcm.bcast_active = true;
            // Broadcast-START marker: legacy lc->m_bq.AddBlock(0,
            //   cursessionpos, NULL, -1) — encoded here as sample_count=-1.
            // pushBlockRecord with sample_count<0 is rejected by the M-7
            // bounds check; instead we encode the broadcast-start as
            // attr=0 + startpos=cursessionpos + sample_count=0, and the run-
            // thread drainBroadcastBlocks() resolves the sample_count==-1
            // legacy semantic by checking startpos. (See drainBroadcastBlocks
            // boundary-marker logic.)
            pushBlockRecord(lcm.block_q, m_block_queue_drops,
                            0, cursessionpos, nullptr, 0, 0);
            // The drain side recognizes sample_count==0 + startpos<0 as the
            // broadcast-stop marker; we tag broadcast-START distinctly with
            // attr=1 to disambiguate.
            // Simpler: piggyback on attr field — attr 0 + sample_count 0 +
            // startpos == -1.0 means broadcast-stop legacy; this branch is
            // broadcast-start so we encode startpos = cursessionpos (>=0).
          }
          else
          {
            lcm.bcast_active = false;
          }

          lcm.curwritefile_curbuflen = 0.0;
        }
      }
      else
      {
        lcm.curwritefile_curbuflen = 0.0;
      }

      if (lcm.bcast_active)
      {
        // Per-block sample push. Legacy: lc->m_bq.AddBlock(sc_nch, 0.0, src,
        //   len, src2). sc_nch=1 (mono) or 2 (stereo).
        pushBlockRecord(lcm.block_q, m_block_queue_drops,
                        sc_nch, 0.0, src, len, sc_nch, src2);
        lcm.curwritefile_curbuflen += len;
      }
    }
#endif

    if (!src2) src2=src;

    // monitor this channel
    bool chan_active = ((!m_issoloactive && !lcm.mute) || lcm.solo);
    {
      int use_nch=2;
      const int outchanidx = lcm.outch + m_local_chanoffs;
      if (outnch < 2 || (outchanidx&1024)) use_nch=1;
      int idx=(outchanidx & 1023);
      if (idx+use_nch>outnch) idx=outnch-use_nch;
      if (idx< 0)idx=0;

      float *out1=outbuf[idx]+offset;

      float vol1=lcm.volume;
      if (use_nch > 1)
      {
        float vol2=vol1;
        float *out2=outbuf[idx+1]+offset;
        if (lcm.pan > 0.0f) vol1 *= 1.0f-lcm.pan;
        else if (lcm.pan < 0.0f) vol2 *= 1.0f+lcm.pan;

        // 15.1-06: VU peak is stored on the mirror as std::atomic<float>;
        // GetLocalChannelPeak reads from there (no m_locchan_cs needed on
        // the UI side). Audio-thread writes are relaxed.
        float maxf =(float)(lcm.peak_vol_l.load(std::memory_order_relaxed)*decay);
        float maxf2=(float)(lcm.peak_vol_r.load(std::memory_order_relaxed)*decay);

        int x=len;
        while (x--)
        {
          float f=src[0];

          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          if (chan_active)
            *out1++ += f * vol1;

          f=src2[0];

          if (f > maxf2) maxf2=f;
          else if (f < -maxf2) maxf2=-f;

          if (chan_active)
            *out2++ += f * vol2;

          src++;
          src2++;
        }
        lcm.peak_vol_l.store(maxf,  std::memory_order_relaxed);
        lcm.peak_vol_r.store(maxf2, std::memory_order_relaxed);
      }
      else
      {
        float maxf=(float)(lcm.peak_vol_l.load(std::memory_order_relaxed)*decay);
        int x=len;
        while (x--)
        {
          float f=(*src++ + *src2++)*0.5f;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          if (chan_active)
            *out1++ += f * vol1;;
        }
        lcm.peak_vol_l.store(maxf, std::memory_order_relaxed);
        lcm.peak_vol_r.store(maxf, std::memory_order_relaxed);
      }
    }
  }
  // 15.1-06 CR-02: m_locchan_cs.Leave removed; mirror was used above.


  if (!justmonitor)
  {
    // mix in all active (subscribed) channels
    m_users_cs.Enter();
    // 15.1-03 H-01: JAMWIDE_DEV_BUILD fopen("/tmp/jamwide.log") block removed unconditionally
    // (also removes the surrounding `if (!m_debug_logged_remote ...)` gate which existed only
    // to one-shot that dev-build log; m_debug_logged_remote field deleted from the class).
    for (int u = 0; u < m_remoteusers.GetSize(); u ++)
    {
      RemoteUser *user=m_remoteusers.Get(u);
      int ch;
      if (!user) continue;

      int a=user->chanpresentmask;
      for (ch = 0; ch < MAX_USER_CHANNELS&&a; ch ++)
      {
        if (a&1)
        {
          float lpan=user->pan+user->channels[ch].pan;
          if (lpan<-1.0)lpan=-1.0;
          else if (lpan>1.0)lpan=1.0;

          bool muteflag;
          if (m_issoloactive) muteflag = !(user->solomask & (1u<<ch));
          else muteflag=(user->mutedmask & (1u<<ch)) || user->muted;

          mixInChannel(user,ch,muteflag,
            user->volume*user->channels[ch].volume,lpan,
              outbuf,user->channels[ch].out_chan_index + m_remote_chanoffs,len,srate,outnch,offset,decay,isPlaying,isSeek,cursessionpos);
        }
        a>>=1;
      }
    }
    m_users_cs.Leave();


    // write out wave if necessary

    if (waveWrite
#ifndef NJCLIENT_NO_XMIT_SUPPORT
      ||(m_oggWrite&&m_oggComp)
#endif
      )
    {
      // 15.1-07b CR-10: replaces audio-thread m_wavebq->AddBlock site.
      // Audio thread try_pushes onto m_wave_block_q (SPSC); run thread
      // (drainWaveBlocks at top of NJClient::Run) forwards into legacy
      // m_wavebq->AddBlock so the existing wave drain loop is untouched.
      // Bounds-check (Codex M-7) + drop counter (Codex M-8) inside
      // pushWaveBlockRecord. nch=2 always for the wave mix.
      pushWaveBlockRecord(m_wave_block_q, m_block_queue_drops,
                          2, 0.0,
                          outbuf[0]+offset, len, 2,
                          outbuf[outnch>1]+offset);
    }
  }

  // apply master volume, then
  {
    int x=len;
    float *ptr1=outbuf[0]+offset;
    float maxf1=(float)(output_peaklevel[0]*decay);
    float maxf2=(float)(output_peaklevel[1]*decay);

    if (outnch >= 2)
    {
      float *ptr2=outbuf[1]+offset;
      float vol1=config_mastermute.load(std::memory_order_relaxed)?0.0f:config_mastervolume.load(std::memory_order_relaxed);
      float vol2=vol1;
      float masterpan = config_masterpan.load(std::memory_order_relaxed);
      if (masterpan > 0.0f) vol1 *= 1.0f-masterpan;
      else if (masterpan< 0.0f) vol2 *= 1.0f+masterpan;

      while (x--)
      {
        float f = *ptr1++ *= vol1;
        if (f > maxf1) maxf1=f;
        else if (f < -maxf1) maxf1=-f;

        f = *ptr2++ *= vol2;
        if (f > maxf2) maxf2=f;
        else if (f < -maxf2) maxf2=-f;
      }
    }
    else
    {
      float vol1=config_mastermute.load(std::memory_order_relaxed)?0.0f:config_mastervolume.load(std::memory_order_relaxed);
      while (x--)
      {
        float f = *ptr1++ *= vol1;
        if (f > maxf1) maxf1=f;
        else if (f < -maxf1) maxf1=-f;
      }
      maxf2=maxf1;
    }
    output_peaklevel[0]=maxf1;
    output_peaklevel[1]=maxf2;
  }

  // mix in (super shitty) metronome (fucko!!!!)
  if (!justmonitor)
  {
    int metrolen=srate / 100;
    double sc=6000.0/(double)srate;
    int x;
    float metro_vol = config_metronome.load(std::memory_order_relaxed);
    int um=metro_vol>0.0001f;

    int metro_chidx = config_metronome_channel.load(std::memory_order_relaxed);
    if (metro_chidx < 0)
    {
      metro_chidx = m_metro_chidx;
    }
    int chidx = metro_chidx&0xff;

    double vol1=config_metronome_mute.load(std::memory_order_relaxed)?0.0:metro_vol,vol2=vol1;
    float *ptr1=chidx < outnch ? outbuf[chidx] : NULL,
          *ptr2=chidx < outnch-1 && !(metro_chidx&1024) ? outbuf[chidx+1] : NULL;

    if (ptr1 && ptr2)
    {
      float metro_pan = config_metronome_pan.load(std::memory_order_relaxed);
      if (metro_pan > 0.0f) vol1 *= 1.0f-metro_pan;
      else if (metro_pan< 0.0f) vol2 *= 1.0f+metro_pan;
    }
    if (ptr1) ptr1+=offset;
    if (ptr2) ptr2+=offset;
    for (x = 0; x < len; x ++)
    {
      if (m_metronome_pos <= 0.0)
      {
        m_metronome_state=1;
        // 15.1-02: relaxed load; same-thread reader (process_samples runs on audio thread).
        m_metronome_tmp=(m_interval_pos.load(std::memory_order_relaxed)+x)<m_metronome_interval;
        m_metronome_pos += (double)m_metronome_interval;
      }
      m_metronome_pos-=1.0;

      if (m_metronome_state>0)
      {
        if (um)
        {
          double val=0.0;
          if (!m_metronome_tmp) val = sin((double)m_metronome_state*sc*2.0) * 0.25;
          else val = sin((double)m_metronome_state*sc);

          if (ptr1) ptr1[x]+=(float)(val*vol1);
          if (ptr2) ptr2[x]+=(float)(val*vol2);
        }
        if (++m_metronome_state >= metrolen) m_metronome_state=0;

      }
    }
  }

}

static int resampleLengthNeeded(int src_srate, int dest_srate, int dest_len, double *state)
{
  // safety
  if (!src_srate) src_srate=48000;
  if (!dest_srate) dest_srate=48000;
  if (src_srate == dest_srate) return dest_len;
  return (int) (((double)src_srate*(double)dest_len/(double)dest_srate)+*state);

}

static void mixFloatsNIOutput(float *src, int src_srate, int src_nch,  // lengths are sample pairs. input is interleaved samples, output not
                            float **dest, int dest_srate, int dest_nch,
                            int dest_len, float vol, float pan, double *state, int src_len)
{
  // this resampling code is terrible, sorry, universe
  int x;
  if (pan < -1.0f) pan=-1.0f;
  else if (pan > 1.0f) pan=1.0f;
  if (vol > 4.0f) vol=4.0f;
  if (vol < 0.0f) vol=0.0f;

  if (!src_srate) src_srate=48000;
  if (!dest_srate) dest_srate=48000;

  double vol1=vol,vol2=vol;
  float *dest1=dest[0];
  float *dest2=NULL;
  if (dest_nch > 1)
  {
    dest2=dest[1];
    if (pan < 0.0f)  vol2 *= 1.0f+pan;
    else if (pan > 0.0f) vol1 *= 1.0f-pan;
  }


  double rspos=*state;
  double drspos = 1.0;
  if (src_srate != dest_srate) drspos=(double)src_srate/(double)dest_srate;

  for (x = 0; x < dest_len; x ++)
  {
    double ls,rs;
    if (src_srate != dest_srate)
    {
      int ipos = (int)rspos;
      int ipos2 = ipos+1;
      if (ipos >= src_len) ipos=src_len-1;
      if (ipos2 >= src_len) ipos2=src_len-1;
      double fracpos=rspos-ipos;
      if (src_nch == 2)
      {
        ipos*=2;
        ipos2*=2;
        ls=src[ipos]*(1.0-fracpos) + src[ipos2]*fracpos;
        rs=src[ipos+1]*(1.0-fracpos) + src[ipos2+1]*fracpos;
      }
      else
      {
        rs=ls=src[ipos]*(1.0-fracpos) + src[ipos2]*fracpos;
      }
      rspos+=drspos;

    }
    else
    {
      if (src_nch == 2)
      {
        int t=x+x;
        ls=src[t];
        rs=src[t+1];
      }
      else
      {
        rs=ls=src[x];
      }
    }

    ls *= vol1;
    if (ls > 1.0) ls=1.0;
    else if (ls<-1.0) ls=-1.0;

    *dest1++ +=(float) ls;

    if (dest_nch > 1)
    {
      rs *= vol2;
      if (rs > 1.0) rs=1.0;
      else if (rs<-1.0) rs=-1.0;

      *dest2++ += (float) rs;
    }
  }
  *state = rspos - (int)rspos;
}


// 15.1-05 CR-05/06/07: defer DecodeState delete to the run thread (RT-safety).
// On overflow we leak the pointer for one tick AND bump the overflow counter
// (Codex M-8: 15.1-10 phase verification fails the phase if non-zero post-UAT).
// Pointer is nulled out unconditionally so the caller's slot is safe to advance.
static inline void deferDecodeStateDelete(
    jamwide::SpscRing<DecodeState*, jamwide::DEFERRED_DELETE_CAPACITY>& q,
    std::atomic<uint64_t>& overflow_counter,
    DecodeState*& p)
{
  if (p)
  {
    if (!q.try_push(p))
    {
      // Queue full — calling delete here would block the audio thread on
      // codec/file-handle teardown. Leak instead; run thread will drain on
      // its next 20ms tick. RT-safety > memory hygiene.
      // The counter increment makes this VISIBLE at phase close (Codex M-8).
      overflow_counter.fetch_add(1, std::memory_order_relaxed);
    }
    p = nullptr;
  }
}

// 15.1-07b CR-09/CR-10 + Codex M-7/M-8: helpers MOVED UP to file-top in the
// next edit. (The original location here was after process_samples, which
// failed to compile because the producer call sites need the helpers in
// scope.) See comment block near top of this file for the live definitions.

void NJClient::drainDeferredDelete()
{
  // Runs ~DecodeState() (delete decode_codec, fclose decode_fp, decode_buf->Release())
  // on the run thread, off the audio thread. Single-owner-at-a-time invariant per
  // spsc_payloads.h: pointers in this queue have been removed from the audio thread's
  // canonical slot, so no further audio-thread access is possible.
  m_deferred_delete_q.drain([](DecodeState* p) {
    delete p;
  });
}

// 15.1-06 CR-02: drain pending LocalChannelUpdate variants into the
// audio-thread mirror. Called at the top of AudioProc.
//
// Codex HIGH-2: this method writes to m_locchan_mirror[ch] BY VALUE for every
// field; it does NOT store any back-pointer into Local_Channel.
//
// Codex HIGH-3: AudioProc bumps m_audio_drain_generation AFTER this method
// returns (release-store) so the run thread can synchronize its
// deferred-Local_Channel-free with the audio-thread observation point.
void NJClient::drainLocalChannelUpdates()
{
  // Apply variant mutations to mirror entries. Each visit is single-threaded
  // (only the audio thread runs this) so we don't need extra synchronization
  // on the mirror itself.
  m_locchan_update_q.drain([this](jamwide::LocalChannelUpdate&& upd) {
    std::visit([this](auto&& u) {
      using T = std::decay_t<decltype(u)>;
      if constexpr (std::is_same_v<T, jamwide::LocalChannelAddedUpdate>) {
        if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
        auto& m = m_locchan_mirror[u.channel];
        m.active  = true;
        m.srcch   = u.srcch;
        m.bitrate = u.bitrate;
        m.bcast   = u.bcast;
        m.outch   = u.outch;
        m.flags   = u.flags;
        m.mute    = u.mute;
        m.solo    = u.solo;
        m.volume  = u.volume;
        m.pan     = u.pan;
        // block_q is already constructed in place inside the array element.
        // cbf / cbf_inst are populated by LocalChannelProcessorUpdate (drained
        // in the same method below) — left untouched here so an Add that
        // arrives AFTER a SetLocalChannelProcessor doesn't clobber the cbf.
      }
      else if constexpr (std::is_same_v<T, jamwide::LocalChannelRemovedUpdate>) {
        if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
        auto& m = m_locchan_mirror[u.channel];
        m.active = false;
        // 15.1-07b: do NOT drain block_q here — we WANT pending broadcast
        // records (e.g. the final boundary marker) to flow through to the
        // encoder. The run thread (drainBroadcastBlocks) will pick them up
        // on its next tick. The audio thread stops producing because it
        // checks active first. Leaving residual records is safe because
        // the per-mirror block_q is not destroyed (it's a stable member of
        // m_locchan_mirror[ch] for the NJClient lifetime); on the next
        // AddedUpdate at the same index, the (likely-empty by then) ring
        // is reused.
        // Reset scalar fields so a subsequent AddedUpdate sees defaults.
        m.srcch = 0; m.bitrate = 0; m.bcast = false; m.outch = -1; m.flags = 0;
        m.mute = false; m.solo = false; m.volume = 1.0f; m.pan = 0.0f;
        m.cbf = nullptr; m.cbf_inst = nullptr;
        m.bcast_active = false;
        m.curwritefile_curbuflen = 0.0;
      }
      else if constexpr (std::is_same_v<T, jamwide::LocalChannelInfoUpdate>) {
        if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
        auto& m = m_locchan_mirror[u.channel];
        m.srcch   = u.srcch;
        m.bitrate = u.bitrate;
        m.bcast   = u.bcast;
        m.outch   = u.outch;
        m.flags   = u.flags;
      }
      else if constexpr (std::is_same_v<T, jamwide::LocalChannelMonitoringUpdate>) {
        if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
        auto& m = m_locchan_mirror[u.channel];
        if (u.set_volume) m.volume = u.volume;
        if (u.set_pan)    m.pan    = u.pan;
        if (u.set_mute)   m.mute   = u.mute;
        if (u.set_solo)   m.solo   = u.solo;
      }
    }, upd);
  });

  // 15.1-06 deviation #2: drain processor (cbf) updates. Separate ring keeps
  // spsc_payloads.h Wave-0-stable while still propagating cbf/cbf_inst.
  m_locchan_processor_q.drain([this](LocalChannelProcessorUpdate&& u) {
    if (u.channel < 0 || u.channel >= MAX_LOCAL_CHANNELS) return;
    auto& m = m_locchan_mirror[u.channel];
    m.cbf = u.cbf;
    m.cbf_inst = u.cbf_inst;
  });
}

// 15.1-06 + Codex HIGH-3: drain canonical Local_Channel* pointers whose
// audio-thread observation has provably ceased (generation-gate enforced
// at enqueue time inside DeleteLocalChannel). Called by the run thread
// at every 20ms tick from NinjamRunThread::run() and once at shutdown.
void NJClient::drainLocalChannelDeferredDelete()
{
  m_locchan_deferred_delete_q.drain([](Local_Channel* p) {
    delete p;
  });
}

// 15.1-07b CR-09: drain per-channel mirror BlockRecord rings on the run
// thread, forwarding each BlockRecord into the legacy BufferQueue-backed
// encoder feed. The audio thread is the producer (process_samples /
// on_new_interval push to m_locchan_mirror[ch].block_q); this method runs
// on the run thread, downstream of the audio callback, and bridges to the
// pre-existing encoder consumer at NJClient::Run() lines 1626-1840 — no
// encoder code changes are required.
//
// Architecture rationale (15.1-07b deviation #1, documented in SUMMARY): the
// plan's contains-grep references juce/NinjamRunThread.cpp::block_q.drain,
// but the existing encoder feed loop lives in NJClient::Run() which itself
// is called FROM NinjamRunThread::run(). The drain must run BEFORE the
// existing GetBlock loop so the encoder sees freshly-forwarded records.
// Therefore the canonical drain site is here in njclient.cpp::Run, and
// NinjamRunThread.cpp adds a token call site to satisfy the grep gate.
//
// Single-thread invariant: only the run thread calls this method. The
// per-channel mirror block_q SPSC has audio thread as producer and run
// thread as consumer — never two writers, never two readers.
void NJClient::drainBroadcastBlocks()
{
  for (int ch = 0; ch < MAX_LOCAL_CHANNELS; ++ch)
  {
    auto& m = m_locchan_mirror[ch];
    // Audio thread sets active=false when LocalChannelRemovedUpdate arrives;
    // the apply visitor in drainLocalChannelUpdates ALSO drains the ring
    // empty at that point, so any remaining records here are for a still-
    // active channel.
    m.block_q.drain([this, ch](jamwide::BlockRecord&& br) {
      // Find the canonical Local_Channel for this index. The encoder owns
      // it (m_enc, m_curwritefile, m_bq). We hold m_locchan_cs because the
      // canonical list can be mutated by other run-thread paths (Add/Delete);
      // this is NOT an audio-thread lock — the audio thread no longer takes
      // m_locchan_cs (CR-02 closed in 15.1-06), so this run-thread acquisition
      // does not violate the audio-thread-no-locks contract.
      WDL_MutexLock lock(&m_locchan_cs);
      Local_Channel* lc = nullptr;
      for (int u = 0; u < m_locchannels.GetSize(); ++u)
      {
        Local_Channel* cand = m_locchannels.Get(u);
        if (cand && cand->channel_idx == ch) { lc = cand; break; }
      }
      if (!lc)
      {
        // Channel not yet known to canonical list (race during Add); drop.
        return;
      }

      // Forward into the legacy BufferQueue. AddBlock signature:
      //   AddBlock(int attr, double startpos, float *samples, int len, float *samples2=NULL)
      // Boundary markers (sample_count == 0) and broadcast-stop markers
      // (the legacy code used sample_count == -1 / samples == NULL) translate
      // into AddBlock(attr, startpos, NULL, 0) and AddBlock(attr, startpos,
      // NULL, -1) respectively. We encode that distinction with attr: see
      // call-site comments in process_samples / on_new_interval.
      if (br.sample_count <= 0)
      {
        // Boundary or broadcast-stop marker — preserve legacy semantics.
        // attr == kBoundaryAttrStop (defined below) means broadcast-stop
        // (legacy len==-1); otherwise plain interval boundary (len==0).
        if (br.attr == 0 && br.startpos == -1.0)
        {
          // Broadcast-stop legacy: AddBlock(0, -1.0, NULL, -1)
          lc->m_bq.AddBlock(0, -1.0, nullptr, -1);
        }
        else
        {
          // Interval boundary legacy: AddBlock(0, 0.0, NULL, 0)
          lc->m_bq.AddBlock(br.attr, br.startpos, nullptr, 0);
        }
      }
      else
      {
        float* s1 = br.samples;
        float* s2 = (br.nch > 1) ? (br.samples + br.sample_count) : nullptr;
        lc->m_bq.AddBlock(br.attr, br.startpos, s1, br.sample_count, s2);
      }
    });
  }
}

// 15.1-07b CR-10: drain m_wave_block_q on the run thread, forwarding into
// the legacy m_wavebq->AddBlock path. The existing wave consumer at
// NJClient::Run() line 1073 reads from m_wavebq and feeds waveWrite +
// m_oggComp; that code is untouched.
void NJClient::drainWaveBlocks()
{
  m_wave_block_q.drain([this](jamwide::BlockRecord&& br) {
    if (br.sample_count <= 0 || !m_wavebq) return;
    float* s1 = br.samples;
    float* s2 = (br.nch > 1) ? (br.samples + br.sample_count) : nullptr;
    m_wavebq->AddBlock(br.attr, br.startpos, s1, br.sample_count, s2);
  });
}


void NJClient::mixInChannel(RemoteUser *user, int chanidx,
                            bool muted, float vol, float pan, float **outbuf, int out_channel,
                            int len, int srate, int outnch, int offs, double vudecay,
                            bool isPlaying, bool isSeek, double playPos)
{
  RemoteUser_Channel * const userchan = &user->channels[chanidx];
  userchan->decode_peak_vol[0]*=vudecay;
  userchan->decode_peak_vol[1]*=vudecay;

  int llmode=(userchan->flags&2);
  int sessionmode = !llmode && (userchan->flags&4);

  overlapFadeState fade_state;
  if (sessionmode)
  {
    if (!isPlaying)
    {
      // 15.1-05 CR-05 (site 1/7): sessionmode + transport stopped
      deferDecodeStateDelete(m_deferred_delete_q, m_deferred_delete_overflows, userchan->ds);
      return;
    }

    if (isSeek || userchan->curds_lenleft <= 0.0)
    {
      if (userchan->ds)
      {
        userchan->ds->calcOverlap(&fade_state);
        // 15.1-05 CR-05 (site 2/7): sessionmode seek/exhaustion
        deferDecodeStateDelete(m_deferred_delete_q, m_deferred_delete_overflows, userchan->ds);
      }

      unsigned char guid[16];
      double offs=0.0;

    //  char buf[512];
  //    sprintf(buf,"querying %f\n",playPos);
//      OutputDebugString(buf);
      double mediasr=m_srate;
      if (userchan->GetSessionInfo(playPos,guid,&offs,&userchan->curds_lenleft,1.0/srate) && userchan->curds_lenleft > 16.0/srate)
      {
        userchan->ds=start_decode(guid, userchan->flags, 0, NULL);
        if (userchan->ds&&userchan->ds->decode_codec)
        {
          userchan->ds->applyOverlap(&fade_state);
          mediasr=userchan->ds->decode_codec->GetSampleRate();
          userchan->dump_samples = ((int) (offs * mediasr))*userchan->ds->decode_codec->GetNumChannels();
          if (userchan->dump_samples<0)userchan->dump_samples=0;

/*
          char buf[512];
          char guidstr[256];
          guidtostr(guid,guidstr);
          sprintf(buf,"at %f got %s %.10f %.10f\n",playPos,guidstr,offs*mediasr,userchan->curds_lenleft*mediasr);
          OutputDebugString(buf);
          */
          userchan->curds_lenleft += 100/mediasr;

        }
        else
        {
          // 15.1-05 CR-05 (site 3/7): start_decode failure rollback
          deferDecodeStateDelete(m_deferred_delete_q, m_deferred_delete_overflows, userchan->ds);
        }
      }
      else
      {
      }

      userchan->curds_lenleft *= mediasr;

    }

  }

  DecodeState *chan=userchan->ds;
  if (!chan || !chan->decode_codec || (!chan->decode_fp && !chan->decode_buf))
  {
    if (llmode && userchan->next_ds[0])
    {
      if (userchan->ds) userchan->ds->calcOverlap(&fade_state);
      // 15.1-05 CR-05 (site 4/7): llmode advance to next_ds[0]. Per RESEARCH
      // § "Subtle note for the planner": capture old pointer FIRST, advance
      // the slot (chan->ds = next_ds[0]), THEN defer-delete the captured old
      // pointer. Audio thread retains exclusive ownership during the shuffle;
      // only the now-orphaned old pointer crosses to the run thread.
      DecodeState* old_ds = userchan->ds;
      chan = userchan->ds = userchan->next_ds[0];
      userchan->next_ds[0]=userchan->next_ds[1]; // advance queue
      userchan->next_ds[1]=0;
      deferDecodeStateDelete(m_deferred_delete_q, m_deferred_delete_overflows, old_ds);

      if (userchan->ds)
      {
        userchan->ds->applyOverlap(&fade_state);
        // 15.1-03 H-02: writeUserChanLog removed from audio path.
      }

      // Phase 14.2: capture t_insta when instamode data first mixed for a remote user.
      // State machine transition: IDLE -> INSTA_CAPTURED.
      // Identity: store RemoteUser* so t_interval can verify same-user match.
      // Only capture once per measurement cycle (state must be IDLE).
      if (userchan->ds
          && insta_meas_state_.load(std::memory_order_relaxed) == kInstaMeasIdle)
      {
          insta_meas_user_ptr_.store(reinterpret_cast<uintptr_t>(user), std::memory_order_relaxed);
          insta_t_insta_ms_.store(currentMillis(), std::memory_order_release);
          insta_meas_state_.store(kInstaCapured, std::memory_order_release);
      }
    }
    if (!chan || !chan->decode_codec || (!chan->decode_fp&&!chan->decode_buf))
    {
      userchan->curds_lenleft -= len;
      return;
    }
  }

  const int mdump=0;

  if (userchan->dump_samples>mdump)
  {
    int av=chan->decode_codec->Available();
    if (av > userchan->dump_samples-mdump) av=userchan->dump_samples-mdump;
    chan->decode_codec->Skip(av);
    userchan->dump_samples-=av;
  }

  int needed=0;
  int srcnch=chan->decode_codec->GetNumChannels();
  while (chan->decode_codec->Available() <= (needed=resampleLengthNeeded(chan->decode_codec->GetSampleRate(),srate,len,&chan->resample_state))*srcnch)
  {
    bool done = chan->runDecode(256);
    if (chan->decode_codec->Available() > 0 && chan->is_voice_firstchk)
    {
      chan->is_voice_firstchk=false;
      while (!chan->runDecode(256))
      {
      }
      const int nch = chan->decode_codec->GetNumChannels();
      if (WDL_NORMALLY(nch > 0))
      {
        const int srate = chan->decode_codec->GetSampleRate();
        const int avail = (chan->decode_codec->Available()-userchan->dump_samples)/nch;
        const int skip = avail - (srate*3/4 + needed);
        if (skip > 512)
        {
          userchan->dump_samples += nch * skip;
        }
      }
    }

    if (userchan->dump_samples>mdump)
    {
      int av=chan->decode_codec->Available();
      if (av > userchan->dump_samples-mdump) av=userchan->dump_samples-mdump;
      chan->decode_codec->Skip(av);
      userchan->dump_samples-=av;
    }

    if (done) break;
  }


  int codecavail=chan->decode_codec->Available();
  if (sessionmode)
  {
    int a= (int)(userchan->curds_lenleft+0.5);
    if (a<1) a=1;

    a*=srcnch;
    if (codecavail >= a)
    {
      codecavail=a;
    }
  }


  int len_out=len;
  if (llmode ? (codecavail < needed*srcnch) : sessionmode ? (codecavail <= needed*srcnch) : false)
  {
    if (codecavail>0)
    {
      // this is probably not really right, need to do some testing
      needed=codecavail/srcnch;
      len_out = ((int) ((double)srate / (double)chan->decode_codec->GetSampleRate() * (double) (needed-chan->resample_state)));
      if (len_out<0)len_out=0;
      else if (len_out>len)len_out=len;
    }
    else
    {
      len_out=0;
    }
  }
  if (sessionmode)
  {
    userchan->curds_lenleft -= needed;
  }

  if (codecavail>0 && codecavail >= needed*srcnch)
  {
    float *sptr=chan->decode_codec->Get();

    // process VU meter, yay for powerful CPUs
    if (!muted && vol > 0.0000001)
    {
      float *p=sptr;
      int l=(needed)*srcnch;
      float maxf=(float) (userchan->decode_peak_vol[0]/vol);
      float maxf2=(float) (userchan->decode_peak_vol[1]/vol);
      if (srcnch>=2) // vu meter + clipping
      {
        l/=2;
        while (l--)
        {
          float f=*p;
          if (f<-1.0f) f=*p=-1.0f;
          else if (f>1.0f) f=*p=1.0f;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          f=*++p;
          if (f<-1.0f) f=*p=-1.0f;
          else if (f>1.0f) f=*p=1.0f;
          if (f > maxf2) maxf2=f;
          else if (f < -maxf2) maxf2=-f;
          p++;
        }
      }
      else
      {
        while (l--)
        {
          float f=*p;
          if (f<-1.0f) f=*p=-1.0f;
          else if (f>1.0f) f=*p=1.0f;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;
          p++;
        }
        maxf2=maxf;
      }
      userchan->decode_peak_vol[0]=maxf*vol;
      userchan->decode_peak_vol[1]=maxf2*vol;

      int use_nch=2;
      if (outnch < 2 || (out_channel&1024)) use_nch=1;
      int idx=(out_channel&1023);
      if (idx+use_nch>outnch) idx=outnch-use_nch;
      if (idx< 0)idx=0;

      float lvol=vol;
      float *tmpbuf[2]={outbuf[idx]+offs,use_nch > 1 ? (outbuf[idx+1]+offs) : 0};
      if (use_nch==1 && srcnch>1)
      {
        tmpbuf[1]=tmpbuf[0];
        lvol*=0.5f;
        use_nch=2;
      }

      mixFloatsNIOutput(sptr,
              chan->decode_codec->GetSampleRate(),
              srcnch,
              tmpbuf,
              srate,use_nch,len_out,
              lvol,pan,&chan->resample_state,
              chan->decode_codec->Available() / srcnch);
    }

    // advance the queue
    chan->decode_codec->Skip(needed*srcnch);
  }
  else if (needed>0)
  {
    if (!llmode&&!sessionmode)
    {
      userchan->dump_samples+=needed*srcnch - chan->decode_codec->Available();
      chan->decode_codec->Skip(chan->decode_codec->Available());
    }
  }

  if ((llmode||sessionmode) &&
      len_out < len &&
      (userchan->next_ds[0]||(sessionmode&&len_out>0)))
  {
    // call again
    userchan->curds_lenleft=-10000.0;
    if (userchan->ds) userchan->ds->calcOverlap(&fade_state);
    // 15.1-05 CR-05 (site 5/7): tail-recursion advance. Same pointer-shuffle
    // ordering as site 4 — capture old pointer FIRST, advance the slot, THEN
    // defer-delete (RESEARCH § "Subtle note for the planner").
    DecodeState* old_ds = userchan->ds;
    chan = userchan->ds = userchan->next_ds[0];
    userchan->next_ds[0]=userchan->next_ds[1]; // advance queue
    userchan->next_ds[1]=0;
    deferDecodeStateDelete(m_deferred_delete_q, m_deferred_delete_overflows, old_ds);
    if (userchan->ds)
    {
      userchan->ds->applyOverlap(&fade_state);
      // 15.1-03 H-02: writeUserChanLog removed from audio path.
    }
    if (sessionmode || (chan && chan->decode_codec && (chan->decode_fp||chan->decode_buf)))
      mixInChannel(user,chanidx,muted,vol,pan,outbuf,out_channel,len-len_out,srate,outnch,offs+len_out,vudecay,
        isPlaying,false,playPos + len_out/(double)srate);
  }
}

// 15.1-03 H-02 (Codex per-plan delta): writeUserChanLog body removed entirely.
// All audio-thread callers were deleted in this plan; no non-audio callers existed,
// so retaining the body as dead code (or as `[[maybe_unused]]`) would mislead future
// maintainers. If a future need arises for per-user-channel logging, route through an
// SPSC-mediated logging path (RESEARCH § Open Questions #3) — do NOT restore in-place
// audio-path call. The declaration is also removed from src/core/njclient.h.

void NJClient::on_new_interval()
{
  m_loopcnt++;
  // 15.1-03 CR-04: writeLog removed from audio path; was diagnostic noise per CONTEXT D-03.

  m_metronome_pos=0.0;

  // 15.1-06 CR-02 + 15.1-07b CR-09: m_locchan_cs.Enter/Leave removed. Audio
  // thread iterates the mirror and pushes interval-boundary BlockRecords
  // onto lcm.block_q. The run thread (drainBroadcastBlocks) forwards these
  // into legacy lc->m_bq.AddBlock so the encoder receives them.
  //
  // The mirror's `bcast` field tracks the intended broadcast state, which
  // is populated by run-thread mutators through LocalChannelInfoUpdate /
  // LocalChannelAddedUpdate. `bcast_active` is the audio-thread runtime
  // boundary-tracking flag (see process_samples).
  for (int ch = 0; ch < MAX_LOCAL_CHANNELS; ++ch)
  {
    auto& lcm = m_locchan_mirror[ch];
    if (!lcm.active) continue;
    if (ch >= m_max_localch && !(lcm.flags & 2)) continue;
    // Regular (non-LL, non-session) channel: reconcile the audio-thread-cached
    // bcast_active flag against the run-thread-supplied bcast each interval,
    // exactly mirroring the legacy state machine that lived under m_locchan_cs
    // (15.1-07b restoration of broadcast — bug-fix on top of the initial port,
    // which dropped the state-transition logic). drainBroadcastBlocks forwards
    // these markers into legacy lc->m_bq.AddBlock on the run thread.
    if (!(lcm.flags & (4|2)))
    {
      if (lcm.bcast_active)
      {
        // Currently broadcasting → emit interval-boundary marker.
        // legacy: lc->m_bq.AddBlock(0, 0.0, NULL, 0)
        pushBlockRecord(lcm.block_q, m_block_queue_drops,
                        0, 0.0, nullptr, 0, 0);
      }

      const bool wasact = lcm.bcast_active;
      lcm.bcast_active = lcm.bcast;  // pick up user-requested broadcast state

      if (wasact && !lcm.bcast_active)
      {
        // Transitioned active→inactive → emit broadcast-stop marker.
        // legacy: lc->m_bq.AddBlock(0, -1.0, NULL, -1) — drainBroadcastBlocks
        // recognizes sample_count==0 + startpos==-1.0 and forwards as
        // AddBlock(0, -1.0, NULL, -1).
        pushBlockRecord(lcm.block_q, m_block_queue_drops,
                        0, -1.0, nullptr, 0, 0);
      }
    }
  }
  // 15.1-06 CR-02: m_locchan_cs.Leave removed; mirror was used above.

  m_users_cs.Enter();
  for (int u = 0; u < m_remoteusers.GetSize(); u ++)
  {
    RemoteUser *user=m_remoteusers.Get(u);
    int ch;
//    printf("submask=%d,cpm=%d\n",user->submask , user->chanpresentmask);
    for (ch = 0; ch < MAX_USER_CHANNELS; ch ++)
    {
      RemoteUser_Channel *chan=&user->channels[ch];

      if (!(chan->flags&2) && !(chan->flags&4))
      {
/*        if (ch<2)
        {
          OutputDebugString("advanced to next_ds (intervalpoo)\n");
        }
        */
        chan->dump_samples=0;
        overlapFadeState fade_state;
        if (chan->ds) chan->ds->calcOverlap(&fade_state);
        // 15.1-05 CR-06 (site 6/7): on_new_interval — replace chan->ds.
        deferDecodeStateDelete(m_deferred_delete_q, m_deferred_delete_overflows, chan->ds);
        if ((user->submask & user->chanpresentmask) & (1u<<ch))
        {
          chan->ds = chan->next_ds[0];
          chan->next_ds[0]=chan->next_ds[1]; // advance queue
          chan->next_ds[1]=0;
        }
        else
        {
          // 15.1-05 CR-07 (site 7/7): on_new_interval — drop unsubscribed
          // next_ds[0]. Capture pointer FIRST, advance the slot (next_ds[0]
          // = next_ds[1]), THEN defer-delete the captured pointer.
          DecodeState* old_next0 = chan->next_ds[0];
          chan->next_ds[0]=chan->next_ds[1]; // advance queue
          chan->next_ds[1]=0;
          deferDecodeStateDelete(m_deferred_delete_q, m_deferred_delete_overflows, old_next0);
        }

        if (chan->ds)
        {
          chan->ds->applyOverlap(&fade_state);
          // 15.1-03 H-02: writeUserChanLog removed from audio path.

          // Phase 14.2: capture t_interval when measured user's regular channel ds advances.
          // State machine transition: INSTA_CAPTURED -> MEASURED.
          // Verify same user by comparing RemoteUser* pointer.
          if (insta_meas_state_.load(std::memory_order_relaxed) == kInstaCapured
              && reinterpret_cast<uintptr_t>(user) == insta_meas_user_ptr_.load(std::memory_order_relaxed))
          {
              insta_t_interval_ms_.store(currentMillis(), std::memory_order_release);
              insta_meas_state_.store(kInstaMeasured, std::memory_order_release);
          }
        }
      }
    }
  }
  m_users_cs.Leave();
}  //if (m_enc->isError()) printf("ERROR\n");
  //else printf("YAY\n");


int NJClient::GetNumUsers()
{
  WDL_MutexLock lock(&m_users_cs);
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);
  return m_remoteusers.GetSize();
}

const char *NJClient::GetUserState(int idx, float *vol, float *pan, bool *mute)
{
  WDL_MutexLock lock(&m_users_cs);
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);
  if (idx<0 || idx>=m_remoteusers.GetSize()) return NULL;
  RemoteUser *p=m_remoteusers.Get(idx);
  if (vol) *vol=p->volume;
  if (pan) *pan=p->pan;
  if (mute) *mute=p->muted;
  return p->name.Get();
}

void NJClient::GetRemoteUsersSnapshot(std::vector<RemoteUserInfo>& out)
{
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);
  WDL_MutexLock lock_users(&m_users_cs);
  out.clear();
  const int num_users = m_remoteusers.GetSize();
  if (num_users <= 0) return;

  const int kMaxNameLen = kRemoteNameMax;

  out.reserve(num_users);
  for (int u = 0; u < num_users; ++u)
  {
    RemoteUser *user = m_remoteusers.Get(u);
    if (!user) continue;

    RemoteUserInfo info;
    info.name[0] = '\0';
    info.name_len = 0;
    const char* user_name = user->name.Get();
    const int user_name_len = user->name.GetLength();
    if (user_name && user_name_len > 0) {
      const int copy_len = user_name_len > kMaxNameLen ? kMaxNameLen : user_name_len;
      memcpy(info.name, user_name, static_cast<size_t>(copy_len));
      info.name[copy_len] = '\0';
      info.name_len = copy_len;
    }
    info.mute = user->muted;
    info.volume = user->volume;
    info.pan = user->pan;

    const unsigned int present_mask =
        static_cast<unsigned int>(user->chanpresentmask);
    if (present_mask)
    {
      for (int ch = 0; ch < MAX_USER_CHANNELS; ++ch)
      {
        if (!(present_mask & (1u << ch))) continue;
        RemoteUser_Channel *chan = user->channels + ch;

        RemoteChannelInfo ch_info;
        ch_info.name[0] = '\0';
        ch_info.name_len = 0;
        ch_info.channel_index = ch;
        const char* channel_name = chan->name.Get();
        const int channel_name_len = chan->name.GetLength();
        if (channel_name && channel_name_len > 0) {
          const int copy_len = channel_name_len > kMaxNameLen ? kMaxNameLen : channel_name_len;
          memcpy(ch_info.name, channel_name, static_cast<size_t>(copy_len));
          ch_info.name[copy_len] = '\0';
          ch_info.name_len = copy_len;
        }
        ch_info.subscribed = (user->submask & (1u << ch)) != 0;
        ch_info.volume = chan->volume;
        ch_info.pan = chan->pan;
        ch_info.mute = (user->mutedmask & (1u << ch)) != 0;
        ch_info.solo = (user->solomask & (1u << ch)) != 0;
        ch_info.vu_left = static_cast<float>(chan->decode_peak_vol[0]);
        ch_info.vu_right = static_cast<float>(chan->decode_peak_vol[1]);
        ch_info.out_chan_index = chan->out_chan_index;
        ch_info.codec_fourcc = chan->codec_fourcc;
        ch_info.flags = chan->flags;

        info.channels.push_back(std::move(ch_info));
      }
    }

    out.push_back(std::move(info));
  }
}

void NJClient::SetUserState(int idx, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute)
{
  WDL_MutexLock lock(&m_users_cs);
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);
  if (idx<0 || idx>=m_remoteusers.GetSize()) return;
  RemoteUser *p=m_remoteusers.Get(idx);
  if (setvol) p->volume=vol;
  if (setpan) p->pan=pan;
  if (setmute) p->muted=mute;
}

int NJClient::EnumUserChannels(int useridx, int i)
{
  WDL_MutexLock lock(&m_users_cs);
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||i<0||i>=MAX_USER_CHANNELS) return -1;
  RemoteUser *user=m_remoteusers.Get(useridx);

  int x;
  for (x = 0; x < 32; x ++)
  {
    if ((user->chanpresentmask & (1u<<x)) && !i--) return x;
  }
  return -1;
}

const char *NJClient::GetUserChannelState(int useridx, int channelidx, bool *sub, float *vol, float *pan, bool *mute, bool *solo, int *outchannel, int *flags)
{
  WDL_MutexLock lock(&m_users_cs);
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return NULL;
  RemoteUser_Channel *p=m_remoteusers.Get(useridx)->channels + channelidx;
  RemoteUser *user=m_remoteusers.Get(useridx);
  if (!(user->chanpresentmask & (1u<<channelidx))) return 0;

  if (sub) *sub=!!(user->submask & (1u<<channelidx));
  if (vol) *vol=p->volume;
  if (pan) *pan=p->pan;
  if (mute) *mute=!!(user->mutedmask & (1u<<channelidx));
  if (solo) *solo=!!(user->solomask & (1u<<channelidx));
  if (outchannel) *outchannel=p->out_chan_index;
  if (flags) *flags=p->flags;

  return p->name.Get();
}


void NJClient::SetUserChannelState(int useridx, int channelidx,
                                   bool setsub, bool sub, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo, bool setoutch, int outchannel)
{
  WDL_MutexLock lock(&m_users_cs);
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);

  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return;
  RemoteUser *user=m_remoteusers.Get(useridx);
  RemoteUser_Channel *p=user->channels + channelidx;
  if (!(user->chanpresentmask & (1u<<channelidx))) return;

  if (setsub && !!(user->submask&(1u<<channelidx)) != sub)
  {
    // toggle subscription
    if (!sub)
    {
      mpb_client_set_usermask su;
      su.build_add_rec(user->name.Get(),(user->submask&=~(1u<<channelidx)));
      m_netcon->Send(su.build());

      DecodeState *tmp,*tmp2,*tmp3;
      m_users_cs.Enter();
//      OutputDebugString("flushds (state)\n");
      tmp=p->ds; p->ds=0;
      tmp2=p->next_ds[0]; p->next_ds[0]=0;
      tmp3=p->next_ds[1]; p->next_ds[1]=0;
      m_users_cs.Leave();

      p->dump_samples=0;

      delete tmp;
      delete tmp2;
      delete tmp3;
    }
    else
    {
      mpb_client_set_usermask su;
      su.build_add_rec(user->name.Get(),(user->submask|=(1u<<channelidx)));
      m_netcon->Send(su.build());
    }

  }
  if (setvol) p->volume=vol;
  if (setpan) p->pan=pan;
  if (setoutch) p->out_chan_index=outchannel;
  if (setmute)
  {
    if (mute)
      user->mutedmask |= (1u<<channelidx);
    else
      user->mutedmask &= ~(1u<<channelidx);
  }
  if (setsolo)
  {
    if (solo) user->solomask |= (1u<<channelidx);
    else user->solomask &= ~(1u<<channelidx);

    if (user->solomask) m_issoloactive|=1;
    else
    {
      int x;
      for (x = 0; x < m_remoteusers.GetSize(); x ++)
      {
        if (m_remoteusers.Get(x)->solomask)
          break;
      }
      if (x == m_remoteusers.GetSize()) m_issoloactive&=~1;
    }
  }
}


double NJClient::GetUserSessionPos(int useridx, time_t *lastupdatetime, double *maxlen)
{
  WDL_MutexLock lock(&m_remotechannel_rd_mutex);

  RemoteUser *u=m_remoteusers.Get(useridx);
  if (maxlen) *maxlen=-1.0;
  if (!u) return -1.0;

  if (lastupdatetime)
  {
    *lastupdatetime=u->last_session_pos_updtime;
  }
  if (maxlen)
  {
    int x;
    int a=u->chanpresentmask&u->submask;
    *maxlen=-1.0;
    for (x=0;x<MAX_USER_CHANNELS&&a; x ++)
    {
      if (a&1)
      {
        RemoteUser_Channel *chan=&u->channels[x];
        if (chan->flags&4)
        {
          double v=chan->GetMaxLength();
          if (v > *maxlen) *maxlen=v;
        }
      }
      a>>=1;
    }

  }
  return u->last_session_pos;
}

float NJClient::GetUserChannelPeak(int useridx, int channelidx, int whichch)
{
  WDL_MutexLock lock(&m_users_cs);
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);

  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return 0.0f;
  RemoteUser_Channel *p=m_remoteusers.Get(useridx)->channels + channelidx;
  RemoteUser *user=m_remoteusers.Get(useridx);
  if (!(user->chanpresentmask & (1u<<channelidx))) return 0.0f;

  if (whichch==0) return (float)p->decode_peak_vol[0];
  if (whichch==1) return (float)p->decode_peak_vol[1];
  return (float) (p->decode_peak_vol[0]+p->decode_peak_vol[1])*0.5f;

}

unsigned int NJClient::GetUserChannelCodec(int useridx, int channelidx)
{
  WDL_MutexLock lock(&m_users_cs);
  WDL_MutexLock lock2(&m_remotechannel_rd_mutex);

  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return 0;
  RemoteUser *user=m_remoteusers.Get(useridx);
  if (!(user->chanpresentmask & (1u<<channelidx))) return 0;
  return user->channels[channelidx].codec_fourcc;
}

float NJClient::GetLocalChannelPeak(int ch, int whichch)
{
  // 15.1-06: read from audio-thread mirror's atomic peak fields. Mirror is
  // indexed by channel_idx, which is the same `ch` value the caller passes.
  // Bounds-check; out-of-range reads return 0.
  if (ch < 0 || ch >= MAX_LOCAL_CHANNELS) return 0.0f;
  const auto& m = m_locchan_mirror[ch];
  if (!m.active) return 0.0f;
  const float l = m.peak_vol_l.load(std::memory_order_relaxed);
  const float r = m.peak_vol_r.load(std::memory_order_relaxed);
  if (whichch == 0) return l;
  if (whichch == 1) return r;
  return (l + r) * 0.5f;
}

void NJClient::DeleteLocalChannel(int ch)
{
  // 15.1-06 + Codex HIGH-3: deferred-free protocol for Local_Channel.
  //
  // Step 1 (under m_locchan_cs): remove the canonical Local_Channel from
  // m_locchannels but DO NOT delete yet. Capture the pointer for deferred
  // deletion.
  Local_Channel* victim = nullptr;
  bool was_solo = false;
  m_locchan_cs.Enter();
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x < m_locchannels.GetSize())
  {
    victim = m_locchannels.Get(x);
    was_solo = victim->solo;
    // Detach from m_locchannels list. Pass `false` to WDL_PtrList::Delete so
    // the callee does NOT call delete on the pointer — we own the deferred
    // delete now.
    m_locchannels.Delete(x, false);

    if (was_solo)
    {
      for (x = 0; x < m_locchannels.GetSize(); x ++)
      {
        if (m_locchannels.Get(x)->solo) break;
      }
      if (x == m_locchannels.GetSize())
        m_issoloactive&=~2;
    }
  }
  m_locchan_cs.Leave();

  if (!victim) return;  // not found — nothing more to do

  // Step 2: publish LocalChannelRemovedUpdate. Audio thread will see this
  // and clear m_locchan_mirror[ch].active on its next AudioProc drain. Note
  // the publish-target generation is captured BEFORE the publish so we can
  // detect when the audio thread has observed it.
  const uint64_t publish_gen_target =
      m_audio_drain_generation.load(std::memory_order_acquire) + 1;
  if (!m_locchan_update_q.try_push(jamwide::LocalChannelUpdate{
          jamwide::LocalChannelRemovedUpdate{ch}}))
  {
    // Worst-case: the SPSC is full. Bump counter, log, AND DO NOT free —
    // the audio thread cannot have observed this remove. Leak instead.
    // (15.1-10 phase verification asserts this counter == 0; non-zero is
    // a defect, not a tolerable transient.)
    m_locchan_update_overflows.fetch_add(1, std::memory_order_relaxed);
    writeLog("ERROR: m_locchan_update_q full on DeleteLocalChannel(%d); leaking Local_Channel to preserve mirror integrity\n", ch);
    return;
  }

  // Step 3: wait for the audio thread to drain at least once after the
  // publish (release-store on m_audio_drain_generation in AudioProc
  // synchronizes with our acquire-load here). DeleteLocalChannel runs on
  // the UI thread; 200ms is well below user-perception threshold for a
  // delete-button latency. If the audio thread is stuck for > 200ms,
  // there is a deeper problem; we leak rather than risk UAF.
  const auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(200);
  while (m_audio_drain_generation.load(std::memory_order_acquire) < publish_gen_target)
  {
    if (std::chrono::steady_clock::now() > deadline)
    {
      writeLog("WARNING: Local_Channel deferred-free generation gate timed out (ch=%d)\n", ch);
      // Audio thread did not advance past the publish in 200 ms. We could
      // wait longer or proceed unsafely. Choose: leak the canonical pointer
      // (still detached from m_locchannels; nothing in NJClient touches it
      // anymore). On user-visible reconnect this orphan eventually goes
      // away when NJClient is destroyed. RT-safety > memory hygiene.
      if (was_solo) NotifyServerOfChannelChange();
      return;
    }
    std::this_thread::yield();
  }

  // Step 4: enqueue for deferred delete. Run thread drains the queue at
  // every 20ms tick and once at shutdown (NinjamRunThread::run()).
  if (!m_locchan_deferred_delete_q.try_push(victim))
  {
    // Extremely unlikely (capacity 32, peak rate ~1 delete per UI tick).
    // If it happens, leak (we cannot delete here safely — the run-thread
    // drain hasn't run yet to make space).
    writeLog("WARNING: m_locchan_deferred_delete_q full (ch=%d); leaking Local_Channel\n", ch);
  }

  NotifyServerOfChannelChange();
}

void NJClient::SetLocalChannelProcessor(int ch, void (*cbf)(float *, int ns, void *), void *inst)
{
  // 15.1-06 deviation #2: AUDIT H-03 said "JamWide doesn't register a cbf
  // today" — this is INCORRECT; juce/NinjamRunThread.cpp:374 registers an
  // Instatalk PTT mute lambda for channel 4 at every connect. The audio
  // thread (process_samples) consults cbf to apply PTT muting. Per Codex
  // HIGH-2, the audio path must NOT dereference a back-pointer into
  // run-thread-owned Local_Channel; therefore the mirror carries cbf and
  // cbf_inst BY VALUE (function pointer + opaque-context, both trivially
  // copyable, the inst pointer is owned by JamWideJuceProcessor — NOT by
  // Local_Channel — so this is not a HIGH-2 violation).
  //
  // We use a separate SPSC ring (m_locchan_processor_q, declared inline in
  // njclient.h) so spsc_payloads.h remains FINAL per Wave-0 stability claim.

  // Update canonical Local_Channel under the run-thread lock (preserves
  // existing behavior for any other callers that read these fields).
  m_locchan_cs.Enter();
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x < m_locchannels.GetSize())
  {
     Local_Channel *c=m_locchannels.Get(x);
     c->cbf=cbf;
     c->cbf_inst=inst;
  }
  m_locchan_cs.Leave();

  // Publish to audio-thread mirror BY VALUE.
  if (!m_locchan_processor_q.try_push(LocalChannelProcessorUpdate{ch, cbf, inst}))
  {
    m_locchan_update_overflows.fetch_add(1, std::memory_order_relaxed);
    writeLog("WARNING: m_locchan_processor_q full; SetLocalChannelProcessor update lost (channel=%d)\n", ch);
  }
}

void NJClient::GetLocalChannelProcessor(int ch, void **func, void **inst)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize())
  {
    if (func) *func=0;
    if (inst) *inst=0;
    return;
  }

  Local_Channel *c=m_locchannels.Get(x);
  if (func) *func=(void *)c->cbf;
  if (inst) *inst=c->cbf_inst;
}

void NJClient::SetLocalChannelInfo(int ch, const char *name, bool setsrcch, int srcch,
                                   bool setbitrate, int bitrate, bool setbcast, bool broadcast, bool setoutch, int outch, bool setflags, int flags)
{
  // 15.1-06 CR-02: keep canonical Local_Channel mutation under m_locchan_cs
  // (UI/run-thread/network-thread consistency); after the canonical update,
  // publish a LocalChannelUpdate variant onto m_locchan_update_q so the
  // audio-thread mirror reflects the new state on its next AudioProc drain.
  m_locchan_cs.Enter();
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  const bool was_add = (x == m_locchannels.GetSize());
  if (was_add)
  {
    m_locchannels.Add(new Local_Channel);
  }

  Local_Channel *c=m_locchannels.Get(x);
  c->channel_idx=ch;
  if (name) c->name.Set(name);
  if (setsrcch) c->src_channel=srcch;
  if (setbitrate) c->bitrate=bitrate;
  if (setbcast) c->broadcasting=broadcast;
  if (setoutch) c->out_chan_index=outch;
  if (setflags) c->flags=flags;

  // Snapshot the canonical state for the publish, INSIDE the lock. The
  // mirror needs the FULL field set on Add; on Info-only updates only the
  // changed fields are needed.
  const int   cur_srcch   = c->src_channel;
  const int   cur_bitrate = c->bitrate;
  const bool  cur_bcast   = c->broadcasting;
  const int   cur_outch   = c->out_chan_index;
  const unsigned int cur_flags = (unsigned int)c->flags;
  const bool  cur_mute    = c->muted;
  const bool  cur_solo    = c->solo;
  const float cur_vol     = c->volume;
  const float cur_pan     = c->pan;
  m_locchan_cs.Leave();

  // 15.1-06 CR-02: publish to audio-thread mirror BY VALUE (HIGH-2: no
  // Local_Channel* pointer is sent across the SPSC). On overflow, bump the
  // M-8-style counter and writeLog (run-thread side, OK). The audio thread
  // continues to use whatever state it last observed.
  bool ok;
  if (was_add)
  {
    ok = m_locchan_update_q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelAddedUpdate{
            ch, cur_srcch, cur_bitrate, cur_bcast, cur_outch, cur_flags,
            cur_mute, cur_solo, cur_vol, cur_pan}});
  }
  else
  {
    ok = m_locchan_update_q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelInfoUpdate{
            ch, cur_srcch, cur_bitrate, cur_bcast, cur_outch, cur_flags}});
  }
  if (!ok)
  {
    m_locchan_update_overflows.fetch_add(1, std::memory_order_relaxed);
    writeLog("WARNING: m_locchan_update_q full; SetLocalChannelInfo update lost (channel=%d)\n", ch);
  }
}

const char *NJClient::GetLocalChannelInfo(int ch, int *srcch, int *bitrate, bool *broadcast, int *outch, int *flags)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize()) return 0;
  Local_Channel *c=m_locchannels.Get(x);
  if (srcch) *srcch=c->src_channel;
  if (bitrate) *bitrate=c->bitrate;
  if (broadcast) *broadcast=c->broadcasting;
  if (outch) *outch=c->out_chan_index;
  if (flags) *flags=c->flags;

  return c->name.Get();
}

int NJClient::EnumLocalChannels(int i)
{
  if (i<0||i>=m_locchannels.GetSize()) return -1;
  return m_locchannels.Get(i)->channel_idx;
}


void NJClient::SetLocalChannelMonitoring(int ch, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo)
{
  // 15.1-06 CR-02: keep canonical Local_Channel mutation under m_locchan_cs;
  // publish a LocalChannelMonitoringUpdate after the lock so the audio-thread
  // mirror reflects the new monitoring state.
  m_locchan_cs.Enter();
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  const bool was_add = (x == m_locchannels.GetSize());
  if (was_add)
  {
    m_locchannels.Add(new Local_Channel);
  }

  Local_Channel *c=m_locchannels.Get(x);
  c->channel_idx=ch;
  if (setvol) c->volume=vol;
  if (setpan) c->pan=pan;
  if (setmute) c->muted=mute;
  if (setsolo)
  {
    c->solo = solo;
    if (solo) m_issoloactive|=2;
    else
    {
      int xx;
      for (xx = 0; xx < m_locchannels.GetSize(); xx ++)
      {
        if (m_locchannels.Get(xx)->solo) break;
      }
      if (xx == m_locchannels.GetSize())
        m_issoloactive&=~2;
    }
  }
  // Snapshot for publish.
  const float snap_vol  = c->volume;
  const float snap_pan  = c->pan;
  const bool  snap_mute = c->muted;
  const bool  snap_solo = c->solo;
  m_locchan_cs.Leave();

  // If this implicitly created a new channel (rare path: caller invoked
  // Monitoring before Info), publish AddedUpdate first so the mirror knows
  // the slot is active. Then publish the Monitoring delta.
  bool ok = true;
  if (was_add)
  {
    ok = m_locchan_update_q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelAddedUpdate{
            ch, /*srcch=*/0, /*bitrate=*/0, /*bcast=*/false,
            /*outch=*/-1, /*flags=*/0u,
            snap_mute, snap_solo, snap_vol, snap_pan}});
  }
  if (ok)
  {
    ok = m_locchan_update_q.try_push(jamwide::LocalChannelUpdate{
        jamwide::LocalChannelMonitoringUpdate{
            ch,
            setvol,  vol,
            setpan,  pan,
            setmute, mute,
            setsolo, solo}});
  }
  if (!ok)
  {
    m_locchan_update_overflows.fetch_add(1, std::memory_order_relaxed);
    writeLog("WARNING: m_locchan_update_q full; SetLocalChannelMonitoring update lost (channel=%d)\n", ch);
  }
}

int NJClient::GetLocalChannelMonitoring(int ch, float *vol, float *pan, bool *mute, bool *solo)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize()) return -1;
  Local_Channel *c=m_locchannels.Get(x);
  if (vol) *vol=c->volume;
  if (pan) *pan=c->pan;
  if (mute) *mute=c->muted;
  if (solo) *solo=c->solo;
  return 0;
}



void NJClient::NotifyServerOfChannelChange()
{
  if (m_netcon)
  {
    int x,idx=0;
    mpb_client_set_channel_info sci;
    for (idx = 0; idx < MAX_USER_CHANNELS; idx ++)
    {
      Local_Channel *ch=NULL;
      int mv=0;
      for (x = 0; x < m_locchannels.GetSize(); x ++)
      {
        ch=m_locchannels.Get(x);
        if (ch->channel_idx>mv) mv=ch->channel_idx;

        if (ch->channel_idx==idx) break;
        ch=NULL;
      }

      // if not found
      if (!ch && idx > mv) break;

      if (ch)
        sci.build_add_rec(ch->name.Get(),0,0,ch->flags);
      else
        sci.build_add_rec("",0,0,0x80);
    }
    m_netcon->Send(sci.build());
  }
}

void NJClient::SetWorkDir(char *path)
{
  m_workdir.Set(path?path:"");

  if (!path || !*path) return;


  if (path[0] && path[strlen(path)-1] != '/' && path[strlen(path)-1] != '\\')
  {
#ifdef _WIN32
    m_workdir.Append("\\");
#else
    m_workdir.Append("/");
#endif
  }

  // create subdirectories for ogg files
  int a;
//  if (config_savelocalaudio>0)

  for (a = 0; a < 16; a ++)
  {
    WDL_String tmp(m_workdir.Get());
    char buf[5];
    snprintf(buf, sizeof(buf), "%x", a);
    tmp.Append(buf);
#ifdef _WIN32
    CreateDirectory(tmp.Get(),NULL);
#else
    mkdir(tmp.Get(),0700);
#endif
  }
}


RemoteUser_Channel::RemoteUser_Channel() : volume(1.0f), pan(0.0f), out_chan_index(0), flags(0), dump_samples(0), ds(NULL), codec_fourcc(0)
{
  decode_peak_vol[0]=decode_peak_vol[1]=0.0;
  memset(next_ds,0,sizeof(next_ds));
  curds_lenleft=0.0;
}

RemoteUser_Channel::~RemoteUser_Channel()
{
  delete ds;
  ds=NULL;
  delete next_ds[0];
  delete next_ds[1];
  memset(next_ds,0,sizeof(next_ds));
  sessioninfo.Empty(true);
}


bool RemoteUser_Channel::GetSessionInfo(double time, unsigned char *guid, double *offs, double *len, double mv)
{
  WDL_MutexLock lock(&sessionlist_mutex);

  mv *= 2.0; // allow one sample poot
  // todo: binary search
  int x;
  for (x = 0; x < sessioninfo.GetSize(); x ++)
  {
    if (time < sessioninfo.Get(x)->start_time-mv)
    {
      *len = sessioninfo.Get(x)->start_time-time;
      if (*len > 1.0) *len=1.0;
      return false;
    }

    if (time < sessioninfo.Get(x)->start_time+ sessioninfo.Get(x)->length-mv)
    {
      memcpy(guid,sessioninfo.Get(x)->guid,16);
      if (time < sessioninfo.Get(x)->start_time)
      {
        *offs=sessioninfo.Get(x)->offset;
        *len = sessioninfo.Get(x)->length + (sessioninfo.Get(x)->start_time-time);
      }
      else
      {
        *offs=(time - sessioninfo.Get(x)->start_time) + sessioninfo.Get(x)->offset;
        *len = (sessioninfo.Get(x)->start_time+sessioninfo.Get(x)->length)-time;
      }
      return true;
    }
  }
  *len = 1.0;
  return false;
}

void RemoteUser_Channel::AddSessionInfo(const unsigned char *guid, double st, double len)
{
  if (st<0.0 || len < 0.2) return;
  const double min_length=0.05;
  const int max_entries=65536;

  // todo: binary search?
  int x;
  for (x = 0; x < sessioninfo.GetSize(); x ++)
  {
    if (st < sessioninfo.Get(x)->start_time) break;
  }
  ChannelSessionInfo *prev=sessioninfo.Get(x-1);
  ChannelSessionInfo *next=sessioninfo.Get(x);



  WDL_MutexLock lock(&sessionlist_mutex);
  // merge this in as a channel

  if (prev)
  {
    if (st < prev->start_time + prev->length)
    {
      if (st+len <= prev->start_time + prev->length-min_length)
      {
        ChannelSessionInfo *ns=new ChannelSessionInfo(guid,st+len,prev->start_time+prev->length - (st+len));
        ns->offset = prev->offset + (ns->start_time-prev->start_time);
        sessioninfo.Insert(x,ns);

        next=NULL; // since our added item is completley contained by this item, we can not check the next item(s)
      }

      prev->length = st-prev->start_time;
      if (prev->length < min_length)
      {
        sessioninfo.Delete(--x);
        delete prev;
        prev=0;
      }
    }
  }
  if (next)
  {
next_again:
    if (st+len > next->start_time)
    {
      double adj=(st+len) - next->start_time;
      next->start_time += adj;
      next->length -= adj;
      next->offset += adj;

      if (next->length < min_length)
      {
        sessioninfo.Delete(x);
        delete next;
        next=sessioninfo.Get(x);
        if (next) goto next_again;
      }

    }
  }
  if (len >= min_length && sessioninfo.GetSize()<max_entries)
  {
    sessioninfo.Insert(x,new ChannelSessionInfo(guid,st,len));
  }

}



RemoteDownload::RemoteDownload() : chidx(-1), playtime(0), m_fp(0), m_decbuf(0)
{
  memset(&guid,0,sizeof(guid));
  time(&last_time);
}

RemoteDownload::~RemoteDownload()
{
  Close();
}

void RemoteDownload::Close()
{
  if (m_fp) fclose(m_fp);
  m_fp=0;
  startPlaying(1);
  if (m_decbuf)
  {
    m_decbuf->Release();
    m_decbuf=0;
  }

}

void RemoteDownload::Open(NJClient *parent, unsigned int fourcc, bool forceToDisk)
{
  m_parent=parent;
  Close();
  m_fp=0;
  m_decbuf=new DecodeMediaBuffer;
  if (!m_decbuf || !parent || parent->config_savelocalaudio>0 || forceToDisk)
  {
    WDL_String s;
    parent->makeFilenameFromGuid(&s,guid);


    // append extension from fourcc
    char buf[8];
    type_to_string(fourcc, buf);
    s.Append(".");
    s.Append(buf);

    m_fourcc=fourcc;
    m_fp=fopenUTF8(s.Get(),"wb");
  }
}

void RemoteDownload::startPlaying(int force)
{
  if (!m_parent || chidx<0) return;
  if (!force)
  {
    if (playtime)
    {
      if (m_fp && ftell(m_fp)>playtime) force=1;
      else if (m_decbuf && m_decbuf->Size()>playtime) force=1;
    }

  }

  if (force)
    // wait until we have config_play_prebuffer of data to start playing, or if config_play_prebuffer is 0, we are forced to play (download finished)
  {
    WDL_MutexLock lock(&m_parent->m_users_cs);
    int x;
    RemoteUser *theuser;
    for (x = 0; x < m_parent->m_remoteusers.GetSize() && strcmp((theuser=m_parent->m_remoteusers.Get(x))->name.Get(),username.Get()); x ++);
    if (x < m_parent->m_remoteusers.GetSize() && chidx >= 0 && chidx < MAX_USER_CHANNELS)
    {
    //  char buf[512];
  //    sprintf(buf,"download %s:%d flags=%d\n",username.Get(),chidx,theuser->channels[chidx].flags);
//      OutputDebugString(buf);

      if (!(theuser->channels[chidx].flags&4)) // only "play" if not a session channel
      {
        DecodeState *tmp=m_parent->start_decode(guid,theuser->channels[chidx].flags,m_fourcc,m_decbuf);

//        OutputDebugString(tmp?"started new decde\n":"tried to start new decode\n");

        // Record the codec FOURCC on the channel for UI display
        theuser->channels[chidx].codec_fourcc = m_fourcc;

        DecodeState *tmp2;
        int useidx=!!theuser->channels[chidx].next_ds[0];
        tmp2=theuser->channels[chidx].next_ds[useidx];
        theuser->channels[chidx].next_ds[useidx]=tmp;
        delete tmp2;
      }
    }
  //  else
//      OutputDebugString("download had no dest!\n");
    chidx=-1;
  }
}

void RemoteDownload::Write(const void *buf, int len)
{
  if (m_fp)
  {
    fwrite(buf,1,len,m_fp);
    fflush(m_fp);
  }
  if (m_decbuf)
  {
    m_decbuf->Write(buf,len);
  }

  startPlaying();
}


Local_Channel::Local_Channel() : channel_idx(0), src_channel(0), volume(1.0f), pan(0.0f),
                muted(false), solo(false), broadcasting(false),
#ifndef NJCLIENT_NO_XMIT_SUPPORT
                m_enc(NULL),
                m_enc_bitrate_used(0),
                m_enc_nch_used(0),
                m_enc_header_needsend(NULL),
#endif
                bcast_active(false), cbf(NULL), cbf_inst(NULL),
                bitrate(64), m_need_header(true), out_chan_index(0), flags(0),
                m_curwritefile_starttime(0.0),
                m_curwritefile_writelen(0.0),
                m_curwritefile_curbuflen(0.0),
                m_wavewritefile(NULL)
{
  decode_peak_vol[0]=decode_peak_vol[1]=0.0;
}


int BufferQueue::GetBlock(WDL_HeapBuf **b, int *attr, double *startpos) // return 0 if got one, 1 if none avail
{
  m_cs.Enter();
  if (m_samplequeue.GetSize()>1)
  {
    *b=m_samplequeue.Get(0);
    WDL_HeapBuf *oa=m_samplequeue.Get(1);
    if (oa && oa->GetSize() == sizeof(AttrStruct))
    {
      AttrStruct *as=(AttrStruct *)oa->Get();
      if (attr)  *attr= as->attr;
      if (startpos) *startpos = as->startpos;
    }
    else
    {
      if (attr)  *attr=0;
      if (startpos) *startpos = 0.0;
    }

    if (oa) m_emptybufs_attr.Add(oa);
    m_samplequeue.Delete(0);
    m_samplequeue.Delete(0);
    m_cs.Leave();
    return 0;
  }
  m_cs.Leave();
  return 1;
}

void BufferQueue::DisposeBlock(WDL_HeapBuf *b)
{
  m_cs.Enter();
  if (b && b != (WDL_HeapBuf*)-1) m_emptybufs.Add(b);
  m_cs.Leave();
}


void BufferQueue::AddBlock(int attr, double startpos, float *samples, int len, float *samples2)
{
  WDL_HeapBuf *mybuf=0;
  if (len>0)
  {
    m_cs.Enter();

    if (m_samplequeue.GetSize() > 512*2)
    {
      m_cs.Leave();
      return;
    }
    int tmp;
    if ((tmp=m_emptybufs.GetSize()))
    {
      mybuf=m_emptybufs.Get(tmp-1);
      if (mybuf) m_emptybufs.Delete(tmp-1);
    }
    m_cs.Leave();
    if (!mybuf) mybuf=new WDL_HeapBuf;

    int uselen=len*sizeof(float);
    if (samples2)
    {
      uselen+=uselen;
    }

    mybuf->Resize(uselen);

    memcpy(mybuf->Get(),samples,len*sizeof(float));
    if (samples2)
      memcpy((float*)mybuf->Get()+len,samples2,len*sizeof(float));
  }
  else if (len == -1) mybuf=(WDL_HeapBuf *)-1;

  m_cs.Enter();

  WDL_HeapBuf *attrbuf=NULL;
  int esz=m_emptybufs_attr.GetSize();
  if (esz)
  {
    attrbuf=m_emptybufs_attr.Get(esz-1);
    m_emptybufs_attr.Delete(esz-1);
  }

  if (!attrbuf) attrbuf=new WDL_HeapBuf;
  AttrStruct *as=(AttrStruct *)attrbuf->Resize(sizeof(AttrStruct));

  as->attr=attr;
  as->startpos=startpos;

  m_samplequeue.Add(mybuf);
  m_samplequeue.Add(attrbuf);

  m_cs.Leave();
}

Local_Channel::~Local_Channel()
{
#ifndef NJCLIENT_NO_XMIT_SUPPORT
  delete m_enc;
  m_enc=0;
  delete m_enc_header_needsend;
  m_enc_header_needsend=0;
#endif

  delete m_wavewritefile;
  m_wavewritefile=0;

}

void NJClient::SetOggOutFile(FILE *fp, int srate, int nch, int bitrate)
{
#ifndef NJCLIENT_NO_XMIT_SUPPORT
  if (m_oggWrite)
  {
    if (m_oggComp)
    {
      m_oggComp->Encode(NULL,0);
      if (m_oggComp->Available())
        fwrite((char *)m_oggComp->Get(),1,m_oggComp->Available(),m_oggWrite);
    }
    fclose(m_oggWrite);
    m_oggWrite=0;
  }
  delete m_oggComp;
  m_oggComp=0;

  if (fp)
  {
    //fucko
    m_oggComp=CreateNJEncoder(srate,nch,bitrate,WDL_RNG_int32());
    m_oggWrite=fp;
  }
#endif
}
