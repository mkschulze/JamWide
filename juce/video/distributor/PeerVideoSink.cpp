// Phase 21-03 Task 1 — PeerVideoSink implementation.
#include "PeerVideoSink.h"

#include <utility>
#include <vector>

namespace jamwide {

PeerVideoSink::PeerVideoSink(int width, int height)
    // Codex Cluster 10 LOW: v1.3 fixed receive surface size — Openh264Decoder
    // handles peers broadcasting at different resolutions by recreating its
    // SwsContext to scale to these fixed sink dimensions (D-07 + Pitfall 7).
    : image_front(juce::Image::ARGB, width, height, /*clearImage=*/true)
    , image_back (juce::Image::ARGB, width, height, /*clearImage=*/true)
{
}

PeerVideoSink::~PeerVideoSink()
{
    // Codex review Cluster 3 — formal lifetime contract on destruction.
    //
    // Step (a): cancelPendingUpdate() — JUCE API. Removes this sink from
    //   the AsyncUpdater pending queue so a triggerAsyncUpdate() that is
    //   already on the message-thread queue but has NOT yet been picked
    //   up by handleAsyncUpdate is dropped silently. This is the "cancel
    //   pending" half of the codex cluster 3 contract.
    cancelPendingUpdate();

    // Step (b): wait for any handleAsyncUpdate that has ALREADY started
    //   on the message thread to return. inFlightCount_ is bumped at the
    //   top of handleAsyncUpdate and decremented at the bottom; the cv
    //   wait blocks until 0.
    {
        std::unique_lock<std::mutex> lock(inFlightLock_);
        inFlightCv_.wait(lock, [this]{ return inFlightCount_ == 0; });
    }

    // After this returns, no listener callback can fire. Listeners + sinks
    // map members destruct safely.
}

std::uint64_t PeerVideoSink::addListener(std::function<void()> cb)
{
    if (!cb) return 0;
    std::lock_guard<std::mutex> g(listenerLock_);
    const std::uint64_t id = nextId_++;
    auto entry = std::make_shared<ListenerEntry>();
    entry->cb  = std::move(cb);
    listeners_.emplace(id, std::move(entry));
    return id;
}

void PeerVideoSink::removeListener(std::uint64_t id)
{
    if (id == 0) return;

    // Clear the listener under the lock so no concurrent
    // handleAsyncUpdate-snapshot can capture it.
    {
        std::lock_guard<std::mutex> g(listenerLock_);
        listeners_.erase(id);
    }

    // Wait for any in-flight handleAsyncUpdate to return — the listener
    // shared_ptr may still be live in another thread's snapshot vector.
    // Pairing with handleAsyncUpdate's inFlightCv_.notify_all() at the
    // bottom of its body, this is the HIGH-2 mirror.
    std::unique_lock<std::mutex> lock(inFlightLock_);
    inFlightCv_.wait(lock, [this]{ return inFlightCount_ == 0; });
}

void PeerVideoSink::handleAsyncUpdate()
{
    // Bump in-flight under inFlightLock_; ~PeerVideoSink waits on this
    // counter via cv.
    {
        std::lock_guard<std::mutex> g(inFlightLock_);
        ++inFlightCount_;
    }

    // Snapshot the listener list under listenerLock_, then release the
    // lock before fan-out so callbacks can call back into the sink/distributor
    // (e.g. unsubscribe from inside a listener) without deadlocking.
    std::vector<std::shared_ptr<ListenerEntry>> snapshot;
    {
        std::lock_guard<std::mutex> g(listenerLock_);
        snapshot.reserve(listeners_.size());
        for (auto& kv : listeners_) {
            snapshot.push_back(kv.second);
        }
    }

    // Fan out OUTSIDE locks.
    for (auto& entry : snapshot) {
        if (entry && entry->cb) entry->cb();
    }

    // Decrement and notify any waiter (either ~PeerVideoSink or
    // removeListener — both block on the same cv).
    {
        std::lock_guard<std::mutex> g(inFlightLock_);
        --inFlightCount_;
        if (inFlightCount_ == 0) inFlightCv_.notify_all();
    }
}

#ifdef JAMWIDE_BUILD_TESTS
std::int64_t PeerVideoSink::getLastObservedSenderSeqForTest() const noexcept
{
    return last_sender_seq_.load(std::memory_order_relaxed);
}

void PeerVideoSink::setLastObservedSenderSeqForTest(std::int64_t seq) noexcept
{
    last_sender_seq_.store(seq, std::memory_order_relaxed);
}
#endif

} // namespace jamwide
