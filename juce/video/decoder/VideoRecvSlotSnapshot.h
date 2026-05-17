#pragma once
// Phase 21-02 — VideoRecvSlotSnapshot POD (Plan 21-02 Task 1).
//
// **Codex review Cluster 2 Option A** (codex-preferred over Option B):
//
//   `VideoRecvState` owns `std::array<VideoRecvSlotSnapshot, 4>
//   decoderSlots` (four 4 MB pre-allocated buffers) plus an
//   `SpscRing<int, 4>` carrying integer indices. The audio thread, inside
//   `runVideoReceiveBlock_` under `m_video_recv_cs`, picks the next-fill
//   slot index (`nextDecoderSlotFillIndex.fetch_add(1) & 3`), memcpys the
//   playing-slot bytes + frameOffsets into `decoderSlots[idx]` via
//   `copyFromVideoRecvBuffer`, then pushes the integer `idx` onto
//   `decoderSlotIndexQ`. The decoder thread (on a separate `juce::Thread`)
//   pops the integer index and reads `decoderSlots[popped_idx]`. Ownership
//   is unambiguous: the slot ring lives in `VideoRecvState`, not in the
//   decoder.
//
//   This REPLACES the previously envisioned `SpscRing<VideoRecvBufferView, 4>`
//   design (raw pointer into a single shared `slotCopyBuf_`) — codex review
//   identified that as a HIGH-severity lifetime / back-to-back-overwrite
//   hazard. Each push now writes to a DIFFERENT slot index, so two back-
//   to-back pushes (A then B) before the decoder pops either are safely
//   stored at distinct slots; `test_back_to_back_push_preserves_order`
//   (W-3 regression guard) verifies the property end-to-end.
//
// Memory cost: 4 MB × 4 slots = 16 MB per peer for the snapshot ring.
// Combined with the existing 4× 4 MB `VideoRecvBuffer` slots from Plan
// 21-01 (accumulating / next / pending / playing), total per-peer memory
// is ~32 MB. The trade-off is unambiguous ownership + structural push-
// order preservation, accepted under the D-11 + D-16 envelope (per-peer
// memory budget is amortised across a finite number of broadcasting
// peers; bounded at peer count, not at user count).
//
// Wire-format contract: the bytes stored here are EXACTLY the contents of
// the `playing` slot's `VideoRecvBuffer::data` (see Plan 21-01 wire-format
// contract in src/core/njclient.h above `struct VideoRecvBuffer`). The
// outer 4B BE length prefix has already been CONSUMED by the WRITE-handler
// accumulator; this snapshot holds raw frame-payload bytes only. The
// `frameOffsets` array mirrors the playing slot's `frameOffsets`:
// `frameOffsets[i]` is the byte offset INTO `bytes` of frame i's first
// payload byte, with `frameOffsets[frameCount] == size`.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace jamwide {

// 4 MB per-slot cap (matches Plan 21-01 D-11 — the VideoRecvBuffer slot
// it copies from is itself capped at 4 MB by the WRITE-handler clamp logic
// in `handleVideoRecvWrite_`).
static constexpr int kVideoRecvSlotPayloadCapBytes = 4 * 1024 * 1024;

// Upper bound on per-slot frame count. Research §Pitfall 9 derives the
// upper bound from upstream's interval-size budget (`MAX_ENC_BLOCKSIZE`
// chunker + per-frame minimum NAL size); typical slot has 1-3 frames
// (marker + optional SPS/PPS chunk + 1 IDR or P-frame). 256 is comfortably
// above the worst case and keeps the `frameOffsets` array small (1028 B
// per snapshot).
static constexpr int kVideoRecvSlotMaxFramesPerSlot = 256;

struct VideoRecvSlotSnapshot {
    // Pre-allocated 4 MB payload buffer (mandatory: must NOT allocate
    // on the audio thread). The audio thread memcpys from
    // `VideoRecvBuffer::data` into `bytes[0..size-1]` under
    // `m_video_recv_cs`.
    std::array<unsigned char, kVideoRecvSlotPayloadCapBytes> bytes;
    int size = 0;

    // Pre-allocated per-frame offset table with one trailing terminator
    // entry. The audio thread copies from `VideoRecvBuffer::frameOffsets`
    // (the byte offset of each frame's payload start in
    // `VideoRecvBuffer::data` — see Plan 21-01 wire-format contract).
    std::array<int, kVideoRecvSlotMaxFramesPerSlot + 1> frameOffsets;
    int frameCount = 0;

    // Reset to empty (does NOT zero the bytes buffer — too expensive on
    // the audio thread; just resets the logical size). The next
    // `copyFromVideoRecvBuffer` call will overwrite the prefix.
    void reset() noexcept {
        size       = 0;
        frameCount = 0;
    }

    // Memcpy bytes + offsets from a `VideoRecvBuffer`. Called by the
    // audio thread under `m_video_recv_cs`. Defensively bounds-clamps to
    // `kVideoRecvSlotPayloadCapBytes` and `kVideoRecvSlotMaxFramesPerSlot`;
    // under normal operation `srcSize <= 4 MB` (Plan 21-01 D-11 cap)
    // and `srcFrameCount <= 256` (well within the bound).
    //
    // `srcFrameOffsets` is expected to be the address of
    // `VideoRecvBuffer::frameOffsets.Get()` (WDL_TypedBuf<int>::Get); the
    // number of entries is `srcFrameCount` (each frame's start). Plan
    // 21-02 does NOT require a terminator entry from the caller — we
    // synthesise `frameOffsets[srcFrameCount] = clampedSize` ourselves so
    // the decoder thread can compute `frameOffsets[i+1] - frameOffsets[i]`
    // for every frame including the last without a bounds check.
    void copyFromVideoRecvBuffer(const void* srcData, int srcSize,
                                 const int*  srcFrameOffsets,
                                 int         srcFrameCount) noexcept {
        const int clampedSize = (srcSize > kVideoRecvSlotPayloadCapBytes)
                                    ? kVideoRecvSlotPayloadCapBytes
                                    : (srcSize < 0 ? 0 : srcSize);
        const int clampedFc   = (srcFrameCount > kVideoRecvSlotMaxFramesPerSlot)
                                    ? kVideoRecvSlotMaxFramesPerSlot
                                    : (srcFrameCount < 0 ? 0 : srcFrameCount);

        if (clampedSize > 0 && srcData != nullptr) {
            std::memcpy(bytes.data(), srcData, (std::size_t)clampedSize);
        }
        size = clampedSize;

        if (clampedFc > 0 && srcFrameOffsets != nullptr) {
            std::memcpy(frameOffsets.data(), srcFrameOffsets,
                        (std::size_t)clampedFc * sizeof(int));
        }
        // Synthesise terminator so frameSize computation has no bounds check.
        frameOffsets[(std::size_t)clampedFc] = clampedSize;
        frameCount                            = clampedFc;
    }
};

} // namespace jamwide
