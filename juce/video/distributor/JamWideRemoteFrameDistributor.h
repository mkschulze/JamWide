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

#include <juce_core/juce_core.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace jamwide {

class PeerVideoSink;

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

} // namespace jamwide
