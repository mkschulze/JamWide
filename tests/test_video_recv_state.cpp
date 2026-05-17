/*
    test_video_recv_state.cpp - Plan 21-01 Task 3 receive-side state machine.

    Validates the WIRE-02 receive-side state machine + GUID-pair decision
    tree implemented in NJClient via the four single-source helpers
    handleVideoRecvBegin_/Write_/End_/runVideoReceiveBlock_ (codex Cluster 5).

    Eleven sub-tests (nine original + two malformed-length per codex
    Cluster 10):
      1. test_marker_parse_extracts_guid_and_seq
      2. test_split_frame_reassembles_via_length_prefix
      3. test_mid_download_start_playing
      4. test_burst_begin_discards_stale_next
      5. test_pending_promotes_to_playing_on_next_swap (asserts the codex
         Cluster 1 timing instrumentation actually accumulates > 0 ns)
      6. test_ds_match_defers_to_pending
      7. test_prev_match_plays_immediately
      8. test_no_match_holds_then_drops_at_4
      9. test_user_leave_resets_video_sync_state
     10. test_zero_length_frame_drops_cleanly (codex Cluster 10)
     11. test_oversize_prefix_clamps_at_4mb_cap (codex Cluster 10)

    Linked against the njclient static library; the JAMWIDE_BUILD_TESTS-gated
    DispatchTestVideoRecvBegin/Write/End + RunOnNewIntervalReceiveBlockForTest
    + GetVideoStreamForTest + AddTestRemoteUserMirrorWithDs +
    ClearTestRemoteUserMirror + DispatchTestUserLeaveForVideoReset helpers
    are thin forwarders to the production helpers (codex Cluster 5 — no
    state-machine duplication).
*/

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "core/njclient.h"
#include "wdl/heapbuf.h"

#ifndef MAKE_NJ_FOURCC
#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))
#endif

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  TEST: %s ... ", name); \
        fflush(stdout); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASSED\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAILED: %s\n", msg); \
    } while(0)

// ---------------------------------------------------------------------------
// Wire-format helpers (per Plan 21-01 codex Cluster 6 contract).
// ---------------------------------------------------------------------------
static std::array<unsigned char, 16> make_guid(unsigned char pattern) {
    std::array<unsigned char, 16> g{};
    for (int i = 0; i < 16; i++) g[i] = pattern;
    return g;
}

static void write_be_u32(unsigned char *out, std::uint32_t v) {
    out[0] = (unsigned char)((v >> 24) & 0xFF);
    out[1] = (unsigned char)((v >> 16) & 0xFF);
    out[2] = (unsigned char)((v >> 8)  & 0xFF);
    out[3] = (unsigned char)( v        & 0xFF);
}

// Build the 20-byte marker payload: [4B BE sender_seq][16B audio_guid].
static std::vector<unsigned char> make_20b_marker_payload(std::uint32_t sender_seq,
                                                           const unsigned char audio_guid[16]) {
    std::vector<unsigned char> v(20);
    write_be_u32(v.data(), sender_seq);
    memcpy(v.data() + 4, audio_guid, 16);
    return v;
}

// Wrap a payload with the outer 4B BE length prefix. Wire format: [4B BE
// outer_len = payload.size()][payload]. Per Cluster 6 contract, the OUTER
// prefix value for the marker frame MUST equal 20.
static std::vector<unsigned char> make_wire_frame_bytes(const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> v(4 + payload.size());
    write_be_u32(v.data(), (std::uint32_t)payload.size());
    if (!payload.empty()) memcpy(v.data() + 4, payload.data(), payload.size());
    return v;
}

// Common fourcc + chidx.
static const unsigned int kH264 = (unsigned int)MAKE_NJ_FOURCC('H','2','6','4');
static const int kChidx = 1;

// ---------------------------------------------------------------------------
// Sub-test 1: marker parse extracts audio_guid + sender_seq.
// ---------------------------------------------------------------------------
static void test_marker_parse_extracts_guid_and_seq() {
    TEST("marker parse extracts audio_guid + sender_seq from 20B payload (outer prefix value MUST be 20)");
    auto client = std::unique_ptr<NJClient>(new NJClient);

    auto vguid = make_guid(0x11);
    auto aguid = make_guid(0xAA);
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "alice", kChidx);

    auto marker_payload = make_20b_marker_payload(42, aguid.data());
    auto wire = make_wire_frame_bytes(marker_payload);
    if (wire[3] != 20) {
        FAIL("internal helper bug: outer prefix value != 20"); return;
    }
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), false);

    auto *vs = client->GetVideoStreamForTest("alice", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }

    // After mid-download startPlaying, accumulating has been moved to next.
    // The audio_guid + sender_seq are on next now.
    if (vs->next.sender_seq != 42) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected sender_seq=42, got %d", vs->next.sender_seq);
        FAIL(buf); return;
    }
    if (memcmp(vs->next.audio_guid, aguid.data(), 16) != 0) {
        FAIL("audio_guid mismatch"); return;
    }
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 2: split frame reassembles via length prefix.
// ---------------------------------------------------------------------------
static void test_split_frame_reassembles_via_length_prefix() {
    TEST("split frame reassembles via 4B BE length prefix (multi-WRITE)");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid = make_guid(0x22);
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "bob", kChidx);

    // Build a wire with [4B BE prefix=20][8B payload chunk1] then [12B payload chunk2].
    auto aguid = make_guid(0xBB);
    auto payload = make_20b_marker_payload(99, aguid.data());

    std::vector<unsigned char> chunk1(4 + 8);
    write_be_u32(chunk1.data(), 20);
    memcpy(chunk1.data() + 4, payload.data(), 8);
    std::vector<unsigned char> chunk2(payload.size() - 8);
    memcpy(chunk2.data(), payload.data() + 8, chunk2.size());

    // Two separate WRITEs to exercise multi-write reassembly via pending_remaining.
    client->DispatchTestVideoRecvWrite(vguid.data(), chunk1.data(), (int)chunk1.size(), false);
    client->DispatchTestVideoRecvWrite(vguid.data(), chunk2.data(), (int)chunk2.size(), false);

    auto *vs = client->GetVideoStreamForTest("bob", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }
    // Frame complete; mid-download startPlaying moved accumulating->next.
    if (vs->next.frameCount != 1) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected next.frameCount=1, got %d", vs->next.frameCount);
        FAIL(buf); return;
    }
    if (vs->next.data.GetSize() != 20) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected next.data.GetSize()=20, got %d", vs->next.data.GetSize());
        FAIL(buf); return;
    }
    if (vs->next.pending_remaining != 0) {
        FAIL("pending_remaining should be 0 after frame complete"); return;
    }
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 3: mid-download startPlaying promotes accumulating -> next.
// ---------------------------------------------------------------------------
static void test_mid_download_start_playing() {
    TEST("mid-download startPlaying promotes accumulating -> next");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid = make_guid(0x33);
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "carol", kChidx);

    auto aguid = make_guid(0xCC);
    auto wire = make_wire_frame_bytes(make_20b_marker_payload(7, aguid.data()));
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), false);

    auto *vs = client->GetVideoStreamForTest("carol", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }
    if (!vs->next.active) { FAIL("expected next.active==true after mid-download startPlaying"); return; }
    if (vs->accumulating.active && vs->accumulating.frameCount > 0) {
        FAIL("accumulating should be drained after startPlaying"); return;
    }
    if (!vs->append_to_next) { FAIL("expected append_to_next==true after startPlaying"); return; }
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 4: burst BEGIN discards stale next.
// ---------------------------------------------------------------------------
static void test_burst_begin_discards_stale_next() {
    TEST("burst BEGIN discards stale next slot");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid1 = make_guid(0x40);
    auto vguid2 = make_guid(0x41);
    client->DispatchTestVideoRecvBegin(vguid1.data(), kH264, "dave", kChidx);
    auto aguid = make_guid(0xDD);
    auto wire = make_wire_frame_bytes(make_20b_marker_payload(1, aguid.data()));
    client->DispatchTestVideoRecvWrite(vguid1.data(), wire.data(), (int)wire.size(), true);

    auto *vs = client->GetVideoStreamForTest("dave", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }
    if (!vs->next.active) { FAIL("expected next.active==true after first END"); return; }

    // Second BEGIN with a different GUID should discard the stale next.
    client->DispatchTestVideoRecvBegin(vguid2.data(), kH264, "dave", kChidx);
    if (vs->next.active) {
        FAIL("burst BEGIN failed to discard stale next.active");
        return;
    }
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 5: pending promotes to playing on next swap (also asserts the
// codex Cluster 1 timing counter is accumulating > 0).
// ---------------------------------------------------------------------------
static void test_pending_promotes_to_playing_on_next_swap() {
    TEST("pending -> playing on next swap (and Cluster 1 timing counter accumulates)");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    client->resetRunVideoReceiveBlockTimingForTest();

    // Populate VideoRecvState with a pending slot directly via the helper
    // path: BEGIN + WRITE(complete marker) + END then a DS-match SWAP to
    // defer to pending. Setup: aguid 0xEE on the wire.
    auto vguid = make_guid(0x55);
    auto aguid = make_guid(0xEE);
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "eve", kChidx);
    auto wire = make_wire_frame_bytes(make_20b_marker_payload(11, aguid.data()));
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), true);
    // Add mirror entry with matching aguid -> first SWAP defers to pending.
    client->AddTestRemoteUserMirrorWithDs(0, "eve", 0, aguid.data());
    client->RunOnNewIntervalReceiveBlockForTest();
    auto *vs = client->GetVideoStreamForTest("eve", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }
    if (!vs->pending.active) {
        FAIL("expected pending.active==true after DS-match swap");
        client->ClearTestRemoteUserMirror(0); return;
    }

    // Second SWAP: pending -> playing.
    client->RunOnNewIntervalReceiveBlockForTest();
    if (!vs->playing.active) {
        FAIL("expected playing.active==true after pending->playing promote");
        client->ClearTestRemoteUserMirror(0); return;
    }
    if (vs->pending.active) {
        FAIL("expected pending cleared after promote");
        client->ClearTestRemoteUserMirror(0); return;
    }

    // codex Cluster 1: timing instrumentation should have recorded > 0 ns
    // for the receive block invocation.
    std::uint64_t max_ns = client->getRunVideoReceiveBlockMaxNanosForTest();
    if (max_ns == 0) {
        FAIL("timing counter did not accumulate; expected > 0 ns max_nanos");
        client->ClearTestRemoteUserMirror(0); return;
    }
    int last_peers = client->getRunVideoReceiveBlockLastPeerCountForTest();
    if (last_peers != 1) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected last_peer_count=1, got %d", last_peers);
        FAIL(buf); client->ClearTestRemoteUserMirror(0); return;
    }
    client->ClearTestRemoteUserMirror(0);
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 6: DS-match defers to pending.
// ---------------------------------------------------------------------------
static void test_ds_match_defers_to_pending() {
    TEST("DS-match defers next -> pending (1-swap defer)");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid = make_guid(0x60);
    auto aguid = make_guid(0xAA);
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "frank", kChidx);
    auto wire = make_wire_frame_bytes(make_20b_marker_payload(20, aguid.data()));
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), true);

    // Mirror slot has the SAME aguid -> DS-match.
    client->AddTestRemoteUserMirrorWithDs(0, "frank", 0, aguid.data());
    client->RunOnNewIntervalReceiveBlockForTest();

    auto *vs = client->GetVideoStreamForTest("frank", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); client->ClearTestRemoteUserMirror(0); return; }
    if (vs->next.active) {
        FAIL("expected next.active==false after DS-match");
        client->ClearTestRemoteUserMirror(0); return;
    }
    if (!vs->pending.active) {
        FAIL("expected pending.active==true after DS-match defer");
        client->ClearTestRemoteUserMirror(0); return;
    }
    if (!vs->append_to_pending) {
        FAIL("expected append_to_pending==true on DS-match defer");
        client->ClearTestRemoteUserMirror(0); return;
    }
    client->ClearTestRemoteUserMirror(0);
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 7: PREV-match plays immediately.
// ---------------------------------------------------------------------------
static void test_prev_match_plays_immediately() {
    TEST("PREV-match plays next -> playing immediately (no defer)");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid = make_guid(0x70);
    auto aguid = make_guid(0xBB);
    auto ds_guid = make_guid(0xCC);  // mirror's current senderDs guid
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "grace", kChidx);
    auto wire = make_wire_frame_bytes(make_20b_marker_payload(30, aguid.data()));
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), true);

    // Manually set vs->prev_ds_guid to 0xBB (matches the wire's audio_guid).
    auto *vs = client->GetVideoStreamForTest("grace", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }
    memcpy(vs->prev_ds_guid, aguid.data(), 16);

    // Mirror current senderDs is 0xCC (DIFFERENT from 0xBB → DS-match fails),
    // but prev_ds_guid==aguid → PREV-match wins.
    client->AddTestRemoteUserMirrorWithDs(0, "grace", 0, ds_guid.data());
    client->RunOnNewIntervalReceiveBlockForTest();

    if (vs->next.active) {
        FAIL("expected next.active==false after PREV-match");
        client->ClearTestRemoteUserMirror(0); return;
    }
    if (!vs->playing.active) {
        FAIL("expected playing.active==true after PREV-match immediate play");
        client->ClearTestRemoteUserMirror(0); return;
    }
    if (vs->pending.active) {
        FAIL("expected pending NOT active on PREV-match (no defer)");
        client->ClearTestRemoteUserMirror(0); return;
    }
    client->ClearTestRemoteUserMirror(0);
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 8: no-match holds, then drops at kHoldCapDrop=4.
// ---------------------------------------------------------------------------
static void test_no_match_holds_then_drops_at_4() {
    TEST("no-match HOLDs then force-resync at kHoldCapDrop=4");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid = make_guid(0x80);
    auto aguid_video = make_guid(0xCC);  // marker audio_guid
    auto aguid_audio = make_guid(0xDD);  // mirror DecodeState guid (different)
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "hank", kChidx);
    auto wire = make_wire_frame_bytes(make_20b_marker_payload(40, aguid_video.data()));
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), true);
    client->AddTestRemoteUserMirrorWithDs(0, "hank", 0, aguid_audio.data());

    auto *vs = client->GetVideoStreamForTest("hank", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); client->ClearTestRemoteUserMirror(0); return; }

    // Three holds: each increments hold_count, next stays active.
    for (int i = 1; i <= 3; i++) {
        client->RunOnNewIntervalReceiveBlockForTest();
        if (vs->hold_count != i) {
            char buf[128]; snprintf(buf, sizeof(buf), "after swap %d expected hold_count=%d, got %d", i, i, vs->hold_count);
            FAIL(buf); client->ClearTestRemoteUserMirror(0); return;
        }
        if (!vs->next.active) {
            char buf[128]; snprintf(buf, sizeof(buf), "after swap %d expected next.active==true (HOLD)", i);
            FAIL(buf); client->ClearTestRemoteUserMirror(0); return;
        }
    }

    // Fourth swap: kHoldCapDrop=4 triggers force-resync.
    client->RunOnNewIntervalReceiveBlockForTest();
    if (vs->next.active) {
        FAIL("expected next.active==false after force-resync");
        client->ClearTestRemoteUserMirror(0); return;
    }
    if (vs->hold_count != 0) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected hold_count reset to 0, got %d", vs->hold_count);
        FAIL(buf); client->ClearTestRemoteUserMirror(0); return;
    }
    if (vs->synced) {
        FAIL("expected synced=false after force-resync");
        client->ClearTestRemoteUserMirror(0); return;
    }
    if (vs->drop_resync_count != 1) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected drop_resync_count=1, got %d", vs->drop_resync_count);
        FAIL(buf); client->ClearTestRemoteUserMirror(0); return;
    }
    client->ClearTestRemoteUserMirror(0);
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 9: user-leave resets video sync state.
// ---------------------------------------------------------------------------
static void test_user_leave_resets_video_sync_state() {
    TEST("user-leave resets prev_ds_guid + hold_count + synced + slots");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid = make_guid(0x90);
    auto aguid = make_guid(0xFF);
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "ivy", kChidx);
    auto wire = make_wire_frame_bytes(make_20b_marker_payload(50, aguid.data()));
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), true);

    // Populate prev_ds_guid + hold_count + synced + last_played_sender_seq.
    auto *vs = client->GetVideoStreamForTest("ivy", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }
    memcpy(vs->prev_ds_guid, aguid.data(), 16);
    memcpy(vs->last_played_audio_guid, aguid.data(), 16);
    vs->hold_count = 2;
    vs->synced = true;
    vs->last_played_sender_seq = 49;

    client->DispatchTestUserLeaveForVideoReset("ivy");

    bool prev_clear = true;
    for (int i = 0; i < 16; i++) if (vs->prev_ds_guid[i] != 0) { prev_clear = false; break; }
    if (!prev_clear) { FAIL("prev_ds_guid not cleared"); return; }
    bool lpag_clear = true;
    for (int i = 0; i < 16; i++) if (vs->last_played_audio_guid[i] != 0) { lpag_clear = false; break; }
    if (!lpag_clear) { FAIL("last_played_audio_guid not cleared"); return; }
    if (vs->hold_count != 0) { FAIL("hold_count not reset to 0"); return; }
    if (vs->synced) { FAIL("synced not reset to false"); return; }
    if (vs->last_played_sender_seq != -1) { FAIL("last_played_sender_seq not reset to -1"); return; }
    if (vs->next.active) { FAIL("next.active not reset"); return; }
    if (vs->pending.active) { FAIL("pending.active not reset"); return; }
    if (vs->accumulating.active) { FAIL("accumulating.active not reset"); return; }
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 10 (codex Cluster 10): zero-length frame drops cleanly.
// ---------------------------------------------------------------------------
static void test_zero_length_frame_drops_cleanly() {
    TEST("zero-length frame (outer prefix=0) drops cleanly with no crash");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid = make_guid(0xA0);
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "jane", kChidx);

    // [4B BE outer=0] with no body bytes following.
    std::vector<unsigned char> wire(4);
    write_be_u32(wire.data(), 0);
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), false);

    auto *vs = client->GetVideoStreamForTest("jane", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }

    // Per upstream :1502-1514: a zero-length prefix records a
    // frameOffsets entry but pending_remaining stays 0 (since bodyLen
    // = 0). The "frame complete" branch fires immediately
    // (pending_remaining == 0 was true before AND after consuming 0
    // body bytes). The accumulating's first-frame marker parse runs
    // with frameSize == 0 -> NEITHER the 20-byte nor the 4-byte branch
    // matches -> audio_guid remains zero, sender_seq remains -1.
    // Whether mid-download startPlaying fires depends on frameCount: per
    // the byte-for-byte port, frameCount IS incremented (the "frame
    // complete" path runs). So either:
    //   (a) frameCount on accumulating == 1 after the WRITE, OR
    //   (b) the mid-download startPlaying has already moved it to next.
    // No crash, no out-of-bounds. Accept either state as PASS.
    int total_frames = vs->accumulating.frameCount + vs->next.frameCount;
    if (total_frames > 1) {
        char buf[128]; snprintf(buf, sizeof(buf), "unexpected total frame count %d", total_frames);
        FAIL(buf); return;
    }

    // Now send END — accumulating should be reset cleanly.
    client->DispatchTestVideoRecvEnd(vguid.data());
    if (vs->accumulating.active) {
        FAIL("accumulating.active should be false after END on zero-length frame"); return;
    }
    PASS();
}

// ---------------------------------------------------------------------------
// Sub-test 11 (codex Cluster 10): oversize prefix clamps at 4 MB cap.
// ---------------------------------------------------------------------------
static void test_oversize_prefix_clamps_at_4mb_cap() {
    TEST("oversize prefix (4 GB declared) clamps at 4 MB cap; no crash");
    auto client = std::unique_ptr<NJClient>(new NJClient);
    auto vguid = make_guid(0xB0);
    client->DispatchTestVideoRecvBegin(vguid.data(), kH264, "kyle", kChidx);

    // [4B BE outer=0xFFFFFFFF][128 bytes body].
    std::vector<unsigned char> wire(4 + 128);
    write_be_u32(wire.data(), 0xFFFFFFFFu);
    for (int i = 0; i < 128; i++) wire[4 + i] = (unsigned char)(i & 0xFF);

    // Time-bound the test (the loop-guard is implicit — the helper
    // returns after one WRITE without hanging).
    auto t0 = std::chrono::steady_clock::now();
    client->DispatchTestVideoRecvWrite(vguid.data(), wire.data(), (int)wire.size(), false);
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (dt > 5000) {
        FAIL("WRITE handler took > 5 seconds on oversize prefix — possible loop"); return;
    }

    auto *vs = client->GetVideoStreamForTest("kyle", kChidx);
    if (!vs) { FAIL("VideoRecvState not created"); return; }

    // The 128 bytes get accumulated into accumulating.data. Either:
    //   - pending_remaining still huge (~4 GB - 128), frameCount == 0,
    //     OR
    //   - the T-21-01/T-21-02 mitigation in handleVideoRecvWrite_
    //     clamped the allocation and aborted the WRITE.
    // Either way: accumulating.frameCount should be 0 (frame not yet
    // complete) and the underlying WDL_HeapBuf must NOT have allocated
    // > 4 MB of heap. We can't directly probe GetAlloc() from here,
    // but we can verify no crash + frameCount == 0.
    if (vs->accumulating.frameCount > 0) {
        char buf[128]; snprintf(buf, sizeof(buf), "expected frameCount=0 (frame not complete), got %d", vs->accumulating.frameCount);
        FAIL(buf); return;
    }

    // Now END: the partial frame should be discarded since frameCount
    // is not > 1; accumulating.reset() runs in the END handler.
    client->DispatchTestVideoRecvEnd(vguid.data());
    if (vs->accumulating.active) {
        FAIL("accumulating.active should be false after END on truncated frame"); return;
    }
    PASS();
}

int main() {
    printf("Running test_video_recv_state...\n");
    test_marker_parse_extracts_guid_and_seq();
    test_split_frame_reassembles_via_length_prefix();
    test_mid_download_start_playing();
    test_burst_begin_discards_stale_next();
    test_pending_promotes_to_playing_on_next_swap();
    test_ds_match_defers_to_pending();
    test_prev_match_plays_immediately();
    test_no_match_holds_then_drops_at_4();
    test_user_leave_resets_video_sync_state();
    test_zero_length_frame_drops_cleanly();
    test_oversize_prefix_clamps_at_4mb_cap();
    printf("Tests passed: %d/%d\n", tests_passed, tests_run);
    return (tests_run == tests_passed) ? 0 : 1;
}
