/*
    JamWide Plugin - spsc_payloads.h
    Audio-thread SPSC payload records for run-thread <-> audio-thread state transfer

    Phase 15.1-04 (Wave 0). Originally FINALIZED at 15.1-04 per Codex M-9 — no
    later sub-plan (15.1-05 / 06 / 07a / 07b / 07c / 08 / 09) modified this
    header. SUPERSEDED 2026-05-02 to add PeerChannelInfoUpdate — see deviation
    block at the new struct definition and `.planning/debug/remote-channels-cutoff.md`
    for authority. The header remained stable across 15.1-05/06/07a/07b/07c/08/09;
    this is a post-15.1 stabilization patch closing the orphan-fields gap left
    by 15.1-07a.

    Codex HIGH-2 response: NO raw-pointer escape hatches into run-thread-owned
    state. Mirrors are populated by VALUE — payload structs deliberately do
    NOT carry mirror-back-pointer fields into Local_Channel or RemoteUser.
    The only pointers that cross thread boundaries are ones whose ownership
    transfers (DecodeState handover, deferred-delete transports for canonical
    objects whose audio-thread observation has provably ceased).

    Codex M-7 response: MAX_BLOCK_SAMPLES is the source of truth for the
    maximum audio-host samples-per-block. NJClient::SetMaxAudioBlockSize
    (introduced in 15.1-08, called from JUCE prepareToPlay) MUST assert
    maxSamplesPerBlock <= MAX_BLOCK_SAMPLES before audio starts; per-callsite
    BlockRecord producers (15.1-07b) MUST also bounds-check sample_count
    and nch defensively.

    SPSC-element-type contract (per src/threading/spsc_ring.h:29):
    every payload MUST be std::is_trivially_copyable. Each variant + POD
    locks this contract via a static_assert below.

    Phase 14.3-02 carve-out: RawDataItem POD + RAWDATA_SEND_QUEUE_CAPACITY
    constant for the NinjamZap-compatible codec-agnostic transport
    scaffolding. Producer = any thread (RawDataSendBegin/Write callers,
    including UI thread and future audio thread for QueueVideoFrame).
    Consumer = run thread inside NJClient::Run drain block. Payload
    ownership: the variable-length opaque byte stream is carried via a
    WDL_HeapBuf* owned-pointer field; producer allocates, drain (or
    discard-on-Disconnect) deletes. Mirrors the DecodeState* ownership-
    transfer pattern used by PeerNextDsUpdate.
*/

#ifndef SPSC_PAYLOADS_H
#define SPSC_PAYLOADS_H

#include <atomic>      // matches house style; not strictly required here
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace jamwide {

// ---------------------------------------------------------------------------
// Forward declarations.
//
// DecodeState is constructed on the run thread; ownership transfers to the
// audio thread via PeerNextDsUpdate; the audio thread later defers its delete
// via the DecodeState* deferred-delete queue. Single-owner-at-a-time invariant.
// No shared run+audio access to the same DecodeState from both threads — only
// via SPSC handoff. (Codex HIGH-2 sanity: this is ownership-transfer, NOT an
// escape-hatch view onto run-thread-owned state.)
// ---------------------------------------------------------------------------
class DecodeState;
class RemoteUser;     // forward-decl ONLY for the deferred-free transport (HIGH-3).
class Local_Channel;  // forward-decl ONLY for the deferred-free transport (HIGH-3).

} // close namespace jamwide briefly for the global-scope forward-decl below

// 14.3-02: forward-decl for the RawDataItem.payload owned-pointer field.
// WDL_HeapBuf is defined in `wdl/heapbuf.h`. RawDataItem only stores a
// pointer to it, never dereferences here — the payload's lifetime is
// managed by the producer (allocate on RawDataSendWrite), the drain (delete
// after Send), or the discard branch (delete without sending on null netcon).
class WDL_HeapBuf;

namespace jamwide {

// ===========================================================================
// (a) RemoteUserUpdate — Use 2 in 15.1-RESEARCH.md (consumed by 15.1-07a).
//
// Audio thread maintains a mirror[MAX_PEERS] populated by draining this queue
// at the top of AudioProc. Run-thread mutators (peer add/remove, mask change,
// vol/pan/mute/solo, codec swap, next_ds advance) push variants here.
//
// Codex HIGH-2: PeerAddedUpdate carries identity-by-value (user_index). The
// audio thread does NOT keep a `RemoteUser*` mirror field; mute/solo/vol/pan
// flow through PeerVolPanUpdate / PeerChannelMaskUpdate, NOT through
// dereferencing a back-pointer.
// ===========================================================================

struct PeerAddedUpdate {
    int slot = 0;
    int user_index = 0;             // server-assigned user index, stable per-session
    // GUID/username are deliberately NOT carried into the audio thread mirror —
    // they're a UI/mixer concern, not an audio-thread one.
};

struct PeerRemovedUpdate {
    int slot = 0;
};

struct PeerChannelMaskUpdate {
    int slot = 0;
    int submask = 0;
    int chanpresentmask = 0;
    int mutedmask = 0;
    int solomask = 0;
};

struct PeerVolPanUpdate {
    int   slot = 0;
    bool  muted = false;
    float volume = 1.0f;
    float pan = 0.0f;
};

// PeerNextDsUpdate transfers ownership of a freshly-prepared DecodeState* from
// the run thread (which constructed/armed it) to the audio thread (which slots
// it into chans[channel].next_ds[slot_idx] and later pushes the previous DS
// onto the deferred-delete queue). Single-owner invariant — see comment on
// the DecodeState forward-decl above.
struct PeerNextDsUpdate {
    int          slot = 0;
    int          channel = 0;
    int          slot_idx = 0;       // 0 or 1 — which next_ds slot to populate
    DecodeState* ds = nullptr;
};

struct PeerCodecSwapUpdate {
    int          slot = 0;
    int          channel = 0;
    unsigned int new_fourcc = 0;
};

// -------------------------------------------------------------------------
// DEVIATION FROM CODEX M-9 "FINAL" — 2026-05-02.
//
// Authority: .planning/debug/remote-channels-cutoff.md (gsd-debugger,
// status=diagnosed, root cause CONFIRMED).
//
// 15.1-07a's RemoteUserMirror migration moved the audio-thread reads of
// four per-channel fields (flags, volume, pan, out_chan_index) onto the
// mirror but shipped no SPSC payload to carry them. Audio mixed every
// remote channel as if flags=0 / volume=1.0 / pan=0.0 / out_chan_index=0
// regardless of canonical state — instamode peers starved after 1-2
// NINJAM intervals; user-set output routing was silently bypassed.
//
// Same shape as the three already-fixed 15.1-07a regressions (b9899a0,
// e827453, e151dd8) and matches the legacy-invariant audit pattern in
// memory `feedback_legacy_invariant_audit.md`.
//
// Closing this gap requires breaking the M-9 FINAL marker on this header.
// The header-level FINAL annotation has been updated to mark the
// supersession explicitly. RT-safety review by realtime-audio-reviewer
// is the gating pre-commit step (see PLAN Task 3).
// -------------------------------------------------------------------------
struct PeerChannelInfoUpdate {
    int          slot = 0;
    int          channel = 0;
    unsigned int flags = 0;
    float        volume = 1.0f;
    float        pan = 0.0f;
    int          out_chan_index = 0;
};

using RemoteUserUpdate = std::variant<
    PeerAddedUpdate,
    PeerRemovedUpdate,
    PeerChannelMaskUpdate,
    PeerVolPanUpdate,
    PeerNextDsUpdate,
    PeerCodecSwapUpdate,
    PeerChannelInfoUpdate
>;

static_assert(std::is_trivially_copyable_v<RemoteUserUpdate>,
              "RemoteUserUpdate must be trivially copyable for audio-thread SPSC handoff");

// ===========================================================================
// (b) LocalChannelUpdate — Use 3 in 15.1-RESEARCH.md (consumed by 15.1-06).
//
// 15.1-06 publishes ALL fields the audio thread needs for each local channel
// by VALUE into its mirror. No `Local_Channel*` field (Codex HIGH-2). The
// BlockRecord SPSC (15.1-07b) replaces the m_bq.AddBlock dereference path
// that previously implied an escape-hatch pointer.
// ===========================================================================

// LocalChannelAddedUpdate is a strict subset of jamwide::SetLocalChannelInfoCommand
// from src/threading/ui_command.h, deliberately omitting `std::string name`
// (heap-backed, not trivially copyable; the audio thread does not need names).
struct LocalChannelAddedUpdate {
    int          channel = 0;
    int          srcch = 0;
    int          bitrate = 0;
    bool         bcast = false;
    int          outch = -1;
    unsigned int flags = 0;
    bool         mute = false;
    bool         solo = false;
    float        volume = 1.0f;
    float        pan = 0.0f;
};

struct LocalChannelRemovedUpdate {
    int channel = 0;
};

// "Info" updates fields that may change after Add (srcch/bitrate/bcast/outch/flags).
struct LocalChannelInfoUpdate {
    int          channel = 0;
    int          srcch = 0;
    int          bitrate = 0;
    bool         bcast = false;
    int          outch = -1;
    unsigned int flags = 0;
};

// Monitoring state (volume/pan/mute/solo) — set_* flags identify partial updates.
struct LocalChannelMonitoringUpdate {
    int   channel = 0;
    bool  set_volume = false;
    float volume = 1.0f;
    bool  set_pan = false;
    float pan = 0.0f;
    bool  set_mute = false;
    bool  mute = false;
    bool  set_solo = false;
    bool  solo = false;
};

using LocalChannelUpdate = std::variant<
    LocalChannelAddedUpdate,
    LocalChannelRemovedUpdate,
    LocalChannelInfoUpdate,
    LocalChannelMonitoringUpdate
>;

static_assert(std::is_trivially_copyable_v<LocalChannelUpdate>,
              "LocalChannelUpdate must be trivially copyable for audio-thread SPSC handoff");

// ===========================================================================
// (c) BlockRecord — Use 4 in 15.1-RESEARCH.md (consumed by 15.1-07b).
//
// Codex M-7 contract:
//   MAX_BLOCK_SAMPLES is the source of truth for max samples-per-block.
//   NJClient::SetMaxAudioBlockSize (introduced in 15.1-08) MUST assert
//   maxSamplesPerBlock <= MAX_BLOCK_SAMPLES before any audio starts. JUCE
//   prepareToPlay calls SetMaxAudioBlockSize. If a host claims a larger
//   block, the assertion fires immediately — payloads cannot truncate.
//   BlockRecord-producing call sites in 15.1-07b ALSO defensively assert
//   sample_count <= MAX_BLOCK_SAMPLES and nch <= MAX_BLOCK_CHANNELS.
// ===========================================================================

inline constexpr int MAX_BLOCK_SAMPLES = 2048;
inline constexpr int MAX_BLOCK_CHANNELS = 2;  // stereo

struct BlockRecord {
    int    attr = 0;
    double startpos = 0.0;
    int    sample_count = 0;   // MUST be <= MAX_BLOCK_SAMPLES; producer asserts.
    int    nch = 0;            // MUST be <= MAX_BLOCK_CHANNELS; producer asserts.
    float  samples[MAX_BLOCK_SAMPLES * MAX_BLOCK_CHANNELS] = {};
};

static_assert(std::is_trivially_copyable_v<BlockRecord>,
              "BlockRecord must be trivially copyable for audio-thread SPSC handoff");

// Loose size bound: header fields (4+8+4+4) + sample buffer + 16 bytes padding.
// Guards against accidental balloon if a future field is added.
static_assert(sizeof(BlockRecord)
                  <= 4 + 8 + 4 + 4
                     + sizeof(float) * MAX_BLOCK_SAMPLES * MAX_BLOCK_CHANNELS
                     + 16,
              "BlockRecord size unexpected (alignment padding budget exceeded)");

// ===========================================================================
// (d) DecodeChunk — Use 5 in 15.1-RESEARCH.md (consumed by 15.1-07c).
// ===========================================================================

inline constexpr int CHUNK_BYTES = 4096;

struct DecodeChunk {
    int     len = 0;            // MUST be <= CHUNK_BYTES; producer asserts.
    uint8_t data[CHUNK_BYTES] = {};
};

static_assert(std::is_trivially_copyable_v<DecodeChunk>,
              "DecodeChunk must be trivially copyable for audio-thread SPSC handoff");

// ===========================================================================
// (e) DeferredDelete capacities — Use 1 in 15.1-RESEARCH.md (consumed by 15.1-05).
//
// Audio thread pushes DecodeState* onto m_deferred_delete_q; run thread drains
// and deletes off-thread. Capacity sized to absorb a worst-case interval-
// boundary burst (peers x channels x 2 next_ds slots).
//
// Usage in NJClient:
//     SpscRing<DecodeState*, DEFERRED_DELETE_CAPACITY> m_deferred_delete_q;
// ===========================================================================

inline constexpr std::size_t DEFERRED_DELETE_CAPACITY = 256;

// ===========================================================================
// (f) DecodeArmRequest — 15.1-09 sessionmode rearm.
//
// INCLUDED HERE per Codex M-9: payload shape finalized at Wave 0 so 15.1-09
// does not mutate this header.
//
// Audio thread (when sessionmode wants the next chunk decoded) pushes a
// DecodeArmRequest onto m_arm_request_q; run thread drains, calls
// start_decode off-thread, then publishes the prepared DecodeState* via
// PeerNextDsUpdate.
// ===========================================================================

struct DecodeArmRequest {
    int          slot = 0;
    int          channel = 0;
    int          slot_idx = 0;          // 0 or 1 — which next_ds slot to populate
    unsigned int fourcc = 0;
    int          srate = 0;
    int          nch = 0;
    unsigned char guid[16] = {0};
};

static_assert(std::is_trivially_copyable_v<DecodeArmRequest>,
              "DecodeArmRequest must be trivially copyable for SPSC handoff");

inline constexpr std::size_t ARM_REQUEST_CAPACITY = 256;

// ===========================================================================
// (h) RawDataItem — Phase 14.3-02 video transport scaffolding.
//
// Codec-agnostic upload queue for the NinjamZap-compatible RawDataSendBegin/
// RawDataSendWrite public API. The send queue (NJClient::m_rawdata_sendq)
// has producers on ANY thread (RawDataSendBegin/Write public API callers
// including UI thread and future audio thread for QueueVideoFrame) and a
// single consumer on the run thread, inside NJClient::Run's drain block
// lexically adjacent to the existing audio-encoder Send loop. Direct
// m_netcon->Send from a non-run-thread producer is forbidden — Landmine L2
// (Net_Connection::Send is NOT thread-safe; see src/core/netmsg.cpp:289).
//
// Payload ownership: the variable-length opaque byte stream lives in a
// WDL_HeapBuf* owned by the queued item. The producer allocates on
// RawDataSendWrite (only when dataLen > 0; otherwise nullptr). The drain
// branch deletes after Sending; the discard branch (null m_netcon)
// deletes without sending and increments the overflow counter. This
// mirrors the DecodeState* ownership-transfer pattern in PeerNextDsUpdate.
//
// Trivially copyable invariant: stored fields are int / unsigned char /
// unsigned int / unsigned char[16] / pointer — every field is POD. The
// SpscRing<RawDataItem, N> contract (see spsc_ring.h:29) is satisfied.
// ===========================================================================

struct RawDataItem {
    int           type = 0;            // 0 = begin, 1 = data/end
    unsigned char guid[16] = {0};
    unsigned int  fourcc = 0;
    int           chidx = 0;
    int           estsize = 0;
    int           flags = 0;           // & 1 = end (for type == 1)
    WDL_HeapBuf*  payload = nullptr;   // owned; drain or discard deletes.
                                       // nullptr for type == 0 (begin) and
                                       // for empty-payload writes.
};

static_assert(std::is_trivially_copyable_v<RawDataItem>,
              "RawDataItem must be trivially copyable for SPSC handoff");

// 14.3-02: capacity of the per-NJClient RawData send queue.
//
// Producer side: any thread calling NJClient::RawDataSendBegin /
// RawDataSendWrite (UI thread today; future audio thread for the v1.3
// QueueVideoFrame path). On try_push failure, the producer bumps
// m_rawdata_sendq_overflows (atomic, relaxed) and writeLog warns.
// Consumer side: run thread inside NJClient::Run's drain block
// (lexically adjacent to the existing audio-encoder Send loop at
// njclient.cpp:2502 / 2549). Drains, splits payloads into MAX_ENC_BLOCKSIZE
// chunks, and emits mpb_client_upload_interval_{begin,write} via
// m_netcon->Send.
// Capacity rationale: video keyframe upload runs at the NINJAM interval
// cadence (≤ 2-4 RawData items/sec/peer typical). 64 provides ~16x worst-
// case headroom over the natural arrival rate. Power-of-2 for SpscRing's
// mask-based wrap.
// Post-UAT invariant (14.3 phase verification): the executor asserts
// GetRawDataSendQueueOverflowCount() == 0 after standard UAT. Non-zero
// == architectural defect (queue undersized for the workload).
inline constexpr std::size_t RAWDATA_SEND_QUEUE_CAPACITY = 64;

// ===========================================================================
// (g) RemoteUser / Local_Channel deferred-free transports — Codex HIGH-3.
//
// Lifetime contract (audio-thread-clean handoff for canonical objects whose
// ownership is run-thread but which the audio-thread mirror previously held
// a stable reference into):
//
//   1. Run thread mutator pushes PeerRemovedUpdate / LocalChannelRemovedUpdate
//      first.
//   2. Audio thread observes via drainRemoteUserUpdates / drainLocalChannelUpdates,
//      sets mirror[slot].active = false, stops dereferencing the slot.
//   3. AT LEAST one audio callback later (verified by an audio_drain_generation
//      counter — see 15.1-07a Task 2 / 15.1-06 Task 4), the run thread enqueues
//      the canonical RemoteUser* / Local_Channel* onto the corresponding
//      deferred-delete queue.
//   4. Run thread drains the deferred-delete queue at the NEXT tick (NOT the
//      same one that pushed). This ensures the audio thread cannot still hold
//      a stale view.
//
// The "at least one audio callback later" gate is implemented in 15.1-06 /
// 15.1-07a; this header just provides the capacity constants used by the ring
// declarations on NJClient.
//
// Usage in NJClient (lands in 15.1-06 / 15.1-07a):
//     SpscRing<RemoteUser*,    REMOTE_USER_DEFERRED_DELETE_CAPACITY>  m_remoteuser_deferred_delete_q;
//     SpscRing<Local_Channel*, LOCAL_CHANNEL_DEFERRED_DELETE_CAPACITY> m_locchan_deferred_delete_q;
// ===========================================================================

inline constexpr std::size_t REMOTE_USER_DEFERRED_DELETE_CAPACITY  = 64;
inline constexpr std::size_t LOCAL_CHANNEL_DEFERRED_DELETE_CAPACITY = 32;

} // namespace jamwide

#endif // SPSC_PAYLOADS_H
