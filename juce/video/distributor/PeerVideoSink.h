#pragma once
// Phase 21-03 Task 1 — PeerVideoSink: per-peer decoded-frame surface.
//
// Owned by JamWideRemoteFrameDistributor; one PeerVideoSink per (username, chidx)
// pair. Phase 22's grid tile + popout subscribe via JamWideRemoteFrameDistributor's
// listener API; the decoder thread (Plan 21-02 Openh264Decoder::scaleAndSwapImage_)
// swaps decoded BGRA frames into image_front under a brief bufferLock and triggers
// AsyncUpdate, which fans the "new frame ready" signal out to listeners on the
// JUCE message thread.
//
// Threading contract:
//   - Decoder thread (writer): writes pixels into image_back, takes a brief
//     juce::ScopedLock on bufferLock, std::swaps image_front <-> image_back,
//     bumps generation atomically, releases the lock, fires triggerAsyncUpdate.
//   - Message thread (reader via Phase 22 tile paint): takes the same bufferLock
//     briefly, snapshots image_front by ref-bump, reads generation atomically,
//     reads atomic status fields, releases the lock, paints OUTSIDE the lock.
//   - handleAsyncUpdate runs on the message thread; iterates listeners under
//     a brief listenerLock_ snapshot, fans out OUTSIDE the lock.
//
// Codex Cluster 3 lifetime guarantees:
//   - ~PeerVideoSink calls cancelPendingUpdate() FIRST (JUCE API — drops any
//     pending message-thread dispatch).
//   - Then takes inFlightLock_ and waits for inFlightCount_ == 0 via inFlightCv_.
//     This blocks ~PeerVideoSink until any handleAsyncUpdate dispatch that has
//     already started on the message thread completes.
//   - After dtor returns, no listener callback can fire; listeners + sinks
//     destruct safely.
//
// Codex Cluster 10 LOW: default sink dimensions are "v1.3 fixed receive surface
// size 320x240" — NOT "first-seen peer resolution" (which is impossible to know
// at BEGIN time; the first decoded frame's resolution may differ and triggers
// SwsContext recreate inside Openh264Decoder per D-07 + Pitfall 7).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace jamwide {

class PeerVideoSink : public juce::AsyncUpdater {
public:
    // Codex Cluster 10 LOW: default v1.3 fixed receive surface size 320x240.
    PeerVideoSink(int width, int height);
    ~PeerVideoSink() override;

    PeerVideoSink(const PeerVideoSink&) = delete;
    PeerVideoSink& operator=(const PeerVideoSink&) = delete;

    // ─── Image double-buffer (D-08) ─────────────────────────────────────
    // image_front is read on the message thread (Phase 22 paint).
    // image_back is written by the decoder thread.
    // Both swapped under brief bufferLock; ref-counted swap is O(1).
    juce::Image           image_front;
    juce::Image           image_back;
    juce::CriticalSection bufferLock;

    // ─── Lock-free generation counter (D-01) ─────────────────────────────
    // Bumped by decoder after each successful scaleAndSwap. UI paint reads
    // for "did this frame change?" detection (skip-paint when stale).
    std::atomic<std::uint64_t> generation{0};

    // ─── D-20 atomic status fields ──────────────────────────────────────
    // Read lock-free per-paint by Phase 22 tile; written by decoder thread
    // OR by runVideoReceiveBlock_ audio-thread receive block (hold_count
    // only — Plan 21-01).
    std::atomic<int>  hold_count{0};         // D-17 — overlay "syncing..." at >= 2
    std::atomic<int>  decode_error_count{0}; // D-18 — diagnostic
    std::atomic<int>  drop_resync_count{0};  // D-17 — diagnostic kHoldCapDrop hits
    std::atomic<bool> synced{false};         // first GUID-pair match observed
    std::atomic<bool> first_frame_seen{false}; // D-19 — Phase 22 overlay control

    // ─── Listener API (D-06: multi-subscriber for DISP-03 grid+popout) ──
    // addListener returns a stable handle id; removeListener detaches and
    // waits for any in-flight handleAsyncUpdate dispatch (HIGH-2 mirror).
    // Caller usually accesses these via JamWideRemoteFrameDistributor's
    // Subscription RAII handle, NOT directly.
    std::uint64_t addListener(std::function<void()> cb);
    void          removeListener(std::uint64_t id);

    // juce::AsyncUpdater override — runs on message thread; iterates
    // listeners under brief lock; fans OUTSIDE the lock.
    void handleAsyncUpdate() override;

#ifdef JAMWIDE_BUILD_TESTS
    // ─── Cross-plan codex concern: sender_seq monotonic alignment counter ─
    // Setter called by Plan 21-02 Openh264Decoder when it parses the 20-byte
    // marker frame (bytes 0..3 BE == sender_seq). test_video_sync_e2e asserts
    // the per-peer observed value advances monotonically across many
    // simulated intervals — catches slow-drift bugs UAT is too coarse to
    // detect.
    std::int64_t getLastObservedSenderSeqForTest() const noexcept;
    void         setLastObservedSenderSeqForTest(std::int64_t seq) noexcept;
#endif

private:
    // Codex Cluster 3: in-flight protection for ~PeerVideoSink and
    // removeListener. handleAsyncUpdate bumps inFlightCount_ before fanning
    // out; dec'ments + notifies after. ~PeerVideoSink calls cancelPendingUpdate()
    // then waits on inFlightCv_ for inFlightCount_ == 0.
    std::mutex              inFlightLock_;
    std::condition_variable inFlightCv_;
    int                     inFlightCount_ = 0;

    struct ListenerEntry {
        std::function<void()> cb;
    };
    mutable std::mutex                                              listenerLock_;
    std::unordered_map<std::uint64_t, std::shared_ptr<ListenerEntry>> listeners_;
    std::uint64_t                                                   nextId_ = 1;

#ifdef JAMWIDE_BUILD_TESTS
    std::atomic<std::int64_t> last_sender_seq_{-1};
#endif
};

} // namespace jamwide
