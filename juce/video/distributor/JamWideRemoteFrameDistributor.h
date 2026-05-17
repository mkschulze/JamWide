#pragma once
// Phase 21-03 Task 1 — JamWideRemoteFrameDistributor: per-peer decoded-frame
// fan-out, symmetric inverse of Phase 19's camera-side JamWideFrameDistributor.
//
// Architecture (D-03, D-05, D-06):
//   - Owns a map of (username, chidx) -> unique_ptr<PeerVideoSink>.
//   - Phase 22's grid tile + popout subscribe via subscribeToPeer; multiple
//     subscribers per peer are supported (DISP-03 — grid+popout coexist on the
//     same peer).
//   - NJClient lazy-creates a sink when a peer's first H264 BEGIN arrives
//     (via Plan 21-03 Task 2's two-phase startup). Removes on user-leave (Plan
//     21-03 Task 2's four-step shutdown protocol per codex Cluster 3).
//
// Subscribe-before-peer-exists race (RESEARCH §Open Question 2):
//   Phase 22 may mount a tile before NJClient has observed the peer's first
//   BEGIN. subscribeToPeer handles this by storing the listener in a
//   deferredListeners_ side-table; findOrCreateSink flushes the deferred
//   listeners onto the newly-created sink atomically.
//
// Threading:
//   - subscribeToPeer / findOrCreateSink / removeSink / findSink: callable from
//     any thread (message thread Phase 22 mount; run thread Plan 21-03 BEGIN;
//     any thread on Subscription dtor). All serialized under mu_.
//   - Subscription::~Subscription calls back into the distributor's
//     unsubscribe_; safe to destroy on any thread.
//
// Codex Cluster 3 lifetime invariant (referenced from JamWideJuceProcessor dtor):
//   JamWideJuceProcessor::~JamWideJuceProcessor calls client.reset() BEFORE
//   remoteFrameDistributor.reset(). NJClient::~NJClient destroys all
//   VideoRecvStates, each of which runs the four-step shutdown protocol
//   (stopAndJoin -> setSink(nullptr) -> vs->sink = nullptr ->
//   distributor->removeSink). By the time remoteFrameDistributor.reset() runs,
//   sinks_ is empty and no decoder thread can still hold a stale sink pointer.

// NOTE: this header is included by src/core/njclient.{h,cpp} which links the
// `njclient` static library (does NOT link juce_graphics / juce_events / juce_core).
// Keep this header free of juce module includes — the .cpp file pulls juce
// where needed (Logger / make_shared<Openh264Decoder> live in the .cpp's TU
// per W-2 type-erased deleter resolution).

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../src/threading/spsc_ring.h"
#include "../decoder/VideoRecvSlotSnapshot.h"

// Plan 21-03 Task 2: include njclient.h to see NJClient::VideoDistributorOps
// nested struct (the function-pointer table populated by
// getDefaultVideoDistributorOps below). njclient.h is already on the include
// path via njclient's own PUBLIC include_directories.
#include "../../../src/core/njclient.h"

namespace jamwide {

class PeerVideoSink;
class Openh264Decoder;

class JamWideRemoteFrameDistributor {
public:
    // RAII handle returned by subscribeToPeer. Move-only. ~Subscription
    // calls unsubscribe_ which removes the listener and waits for in-flight
    // handleAsyncUpdate to return (HIGH-2 mirror).
    class Subscription {
    public:
        Subscription() = default;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        ~Subscription();
        bool isActive() const noexcept { return owner_ != nullptr; }

    private:
        friend class JamWideRemoteFrameDistributor;
        Subscription(JamWideRemoteFrameDistributor* owner,
                     std::string                    key,
                     std::uint64_t                  id) noexcept
            : owner_(owner), key_(std::move(key)), id_(id) {}
        JamWideRemoteFrameDistributor* owner_ = nullptr;
        std::string                    key_;
        std::uint64_t                  id_ = 0;
    };

    JamWideRemoteFrameDistributor();
    ~JamWideRemoteFrameDistributor();

    JamWideRemoteFrameDistributor(const JamWideRemoteFrameDistributor&) = delete;
    JamWideRemoteFrameDistributor& operator=(const JamWideRemoteFrameDistributor&) = delete;

    // ─── Sink lifecycle (called from NJClient run thread per Plan 21-03 Task 2) ─

    // Idempotent: returns the existing sink if one exists for (username, chidx),
    // otherwise constructs a new sink with the given dimensions, flushes any
    // deferred listeners (from subscribe-before-peer-exists), and returns it.
    PeerVideoSink* findOrCreateSink(const char* username, int chidx,
                                    int width, int height);

    // Lookup without create. Returns nullptr if no sink exists for the key.
    PeerVideoSink* findSink(const char* username, int chidx);

    // Removes the sink for (username, chidx) — called from NJClient's
    // user-leave four-step shutdown protocol (codex Cluster 3 step 4).
    // The unique_ptr<PeerVideoSink> destructor runs the codex Cluster 3
    // dtor contract (cancelPendingUpdate + wait for in-flight).
    void removeSink(const char* username, int chidx);

    // Plan 21-03 Task 2 (codex Cluster 4 Phase 2): factory for the
    // shared_ptr<Openh264Decoder> + PeerVideoSink* pair. Called by
    // NJClient::completeVideoDecoderStartup_ OUTSIDE m_video_recv_cs on the
    // run thread. The factory:
    //   (a) finds-or-creates the sink for (username, chidx) at (width, height);
    //   (b) constructs the decoder via std::make_shared<Openh264Decoder>
    //       (capturing the type-erased deleter inside THIS translation unit
    //       — JUCE-linked — per W-2 resolution);
    //   (c) calls decoder->setSink(sinkPtr);
    //   (d) calls decoder->open(width, height) — heavyweight; returns false
    //       if avcodec_open2 fails. On failure the sink is removed.
    // Returns the decoder as shared_ptr; the caller installs it onto
    // VideoRecvState::decoder.
    std::shared_ptr<Openh264Decoder> createDecoderAndSinkForPeer(
        const char* username, int chidx, int width, int height,
        std::array<jamwide::VideoRecvSlotSnapshot, 4>& slotRing,
        jamwide::SpscRing<int, 4>&                     slotIndexQ,
        std::atomic<std::uint64_t>&                    producerSeq);

    // Plan 21-03 Task 2 (codex Cluster 3 four-step shutdown protocol):
    // tear-down helper. Steps 1+2+4: decoder->close() + decoder->setSink(nullptr)
    // + removeSink(username, chidx). Caller's local shared_ptr ref drops on
    // scope exit; the decoder dtor is a no-op (already closed).
    void tearDownDecoderAndSink(std::shared_ptr<Openh264Decoder>& decoder,
                                 const char* username, int chidx);

    // ─── Subscription API (called from Phase 22 tile / popout) ─────────

    // [[nodiscard]] returns a Subscription RAII handle. Phase 22 stores it
    // as a member; on tile destroy the handle goes out of scope and
    // ~Subscription detaches the listener and waits for in-flight.
    //
    // Subscribe-before-peer-exists: if the sink does not yet exist for
    // (username, chidx), the listener is stored in deferredListeners_ and
    // attached the moment findOrCreateSink is called for the same key
    // (Plan 21-03 BEGIN handler triggers that).
    [[nodiscard]] Subscription subscribeToPeer(const char*             username,
                                               int                     chidx,
                                               std::function<void()>   onRepaint);

private:
    // Called by ~Subscription. Removes the listener from the sink (if it
    // exists) or from the deferredListeners_ map; PeerVideoSink::removeListener
    // blocks until any in-flight handleAsyncUpdate returns.
    void unsubscribe_(const std::string& key, std::uint64_t id) noexcept;

    static std::string makeKey_(const char* username, int chidx);

    struct DeferredEntry {
        std::uint64_t         id;
        std::function<void()> cb;
    };

    mutable std::mutex                                                   mu_;
    std::unordered_map<std::string, std::unique_ptr<PeerVideoSink>>      sinks_;
    std::unordered_map<std::string, std::vector<DeferredEntry>>          deferredListeners_;
};

// Plan 21-03 Task 2: free function returning a populated NJClient::VideoDistributorOps
// table that JamWideJuceProcessor passes into NJClient::SetVideoDistributorOps.
// Lives in JamWideRemoteFrameDistributor.cpp (JUCE-linked TU) so the function-
// pointer table's implementations can call Openh264Decoder methods + access
// VideoRecvState's full member layout (W-2 type-erased deleter resolution).
NJClient::VideoDistributorOps getDefaultVideoDistributorOps() noexcept;

} // namespace jamwide
