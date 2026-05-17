#pragma once
// Phase 21-02 — NalChunk POD (Plan 21-02 Task 1).
//
// **DECODER-THREAD-LOCAL INVARIANT (codex review Cluster 2):**
//   NalChunks NEVER cross the audio-thread boundary. They are constructed
//   inside `Openh264Decoder::parseSlotAndFeed_` (decoder thread) and
//   consumed inside the same call. The cross-thread payload between the
//   audio thread and the decoder thread is `VideoRecvSlotSnapshot`
//   (codex Cluster 2 Option A — VideoRecvState owns 4 of these), NOT
//   `NalChunk`. Treating `NalChunk` as a cross-thread payload was the
//   OLD design (CONTEXT.md D-12 pre-revision); B-1 resolution moved the
//   AVCC parser onto the decoder thread itself, so NalChunks are now a
//   purely-decoder-thread-local construct used to stage Annex-B-framed
//   bytes (00 00 00 01 + raw NAL) into reusable scratch buffers before
//   they are handed to libavcodec via `avcodec_send_packet`.
//
// The std::vector here exists so test-only `pushNalChunk` (JAMWIDE_BUILD_TESTS)
// can hand the decoder a pre-shaped NAL without going through the slot-
// snapshot parser. In production the bytes live inside the decoder's
// reusable `annexBScratch_` buffer (no allocation per frame).

#include <cstdint>
#include <vector>

namespace jamwide {

enum class NalKind : std::uint8_t {
    Frame    = 0,  // Per-frame H.264 NAL (slice, IDR slice, etc.)
    ParamSet = 1,  // Sequence parameter set (SPS) or picture parameter set (PPS)
};

struct NalChunk {
    NalKind                    kind  = NalKind::Frame;
    // Annex-B framed bytes: must start with 00 00 00 01 start code per D-13,
    // followed by the raw NAL unit bytes. The decoder hands the entire
    // vector contents directly to `avcodec_send_packet` via an AVPacket
    // pointing at `bytes.data()` with `size = (int)bytes.size()`.
    std::vector<unsigned char> bytes;
};

} // namespace jamwide
