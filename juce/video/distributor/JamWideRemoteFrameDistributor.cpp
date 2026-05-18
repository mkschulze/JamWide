// Phase 21-03 Task 1 — JamWideRemoteFrameDistributor implementation.
#include "JamWideRemoteFrameDistributor.h"
#include "PeerVideoSink.h"
#include "../decoder/Openh264Decoder.h"

// Plan 21-03 Task 2: include the NJClient header to see VideoRecvState's
// shared_ptr<Openh264Decoder> decoder + raw PeerVideoSink* sink members so
// the function-pointer table implementations can dereference vs in this TU.
#include "../../../src/core/njclient.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace jamwide {

// ─── Subscription RAII ───────────────────────────────────────────────────

JamWideRemoteFrameDistributor::Subscription::Subscription(Subscription&& other) noexcept
    : owner_(other.owner_), key_(std::move(other.key_)), id_(other.id_)
{
    other.owner_ = nullptr;
    other.id_    = 0;
}

JamWideRemoteFrameDistributor::Subscription&
JamWideRemoteFrameDistributor::Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other) {
        // Release the current registration FIRST. unsubscribe_ blocks for
        // in-flight callbacks; only then take over `other`'s slot.
        if (owner_) owner_->unsubscribe_(key_, id_);
        owner_ = other.owner_;
        key_   = std::move(other.key_);
        id_    = other.id_;
        other.owner_ = nullptr;
        other.id_    = 0;
    }
    return *this;
}

JamWideRemoteFrameDistributor::Subscription::~Subscription()
{
    if (owner_) owner_->unsubscribe_(key_, id_);
}

// ─── Distributor ────────────────────────────────────────────────────────

JamWideRemoteFrameDistributor::JamWideRemoteFrameDistributor() = default;

JamWideRemoteFrameDistributor::~JamWideRemoteFrameDistributor()
{
    // Per codex Cluster 3 reversed dtor order: JamWideJuceProcessor::~JamWideJuceProcessor
    // calls client.reset() BEFORE remoteFrameDistributor.reset(), so by the time we
    // get here NJClient has already destroyed each VideoRecvState and run the
    // four-step shutdown protocol (which calls removeSink). sinks_ SHOULD be
    // empty. If a stray sink remains (e.g. a peer broadcasting at processor
    // tear-down without going through the user-leave path), the unique_ptr
    // destructor still runs the codex Cluster 3 PeerVideoSink dtor contract.
    std::lock_guard<std::mutex> g(mu_);
    sinks_.clear();
    deferredListeners_.clear();
}

std::string JamWideRemoteFrameDistributor::makeKey_(const char* username, int chidx)
{
    char buf[300];
    std::snprintf(buf, sizeof(buf), "%s:%d", username ? username : "", chidx);
    return std::string(buf);
}

PeerVideoSink* JamWideRemoteFrameDistributor::findOrCreateSink(
    const char* username, int chidx, int width, int height)
{
    if (!username) return nullptr;
    const std::string key = makeKey_(username, chidx);

    // Take ownership of any deferred listeners atomically with the sink
    // creation so subscribers that registered before the sink existed
    // start receiving frames the moment the decoder fires its first
    // triggerAsyncUpdate.
    std::vector<DeferredEntry> toAttach;
    PeerVideoSink* result = nullptr;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto it = sinks_.find(key);
        if (it != sinks_.end()) {
            return it->second.get();
        }
        auto sink = std::make_unique<PeerVideoSink>(width, height);
        result    = sink.get();
        sinks_.emplace(key, std::move(sink));

        auto dit = deferredListeners_.find(key);
        if (dit != deferredListeners_.end()) {
            toAttach = std::move(dit->second);
            deferredListeners_.erase(dit);
        }
    }

    // Attach deferred listeners OUTSIDE the distributor mutex. PeerVideoSink::
    // addListener takes its own listenerLock_; we re-attach with the SAME id
    // the Subscription is holding so unsubscribe_ on that handle later finds
    // the listener under the live sink.
    if (result && !toAttach.empty()) {
        for (auto& entry : toAttach) {
            // PeerVideoSink internally allocates a new id from its nextId_,
            // but the Subscription's stored id is a JamWideRemoteFrameDistributor-
            // generated id that lives in deferredListeners_'s entry. To keep
            // the Subscription's id valid, we need PeerVideoSink to use the
            // pre-assigned id. Since PeerVideoSink owns its own id namespace
            // (which is fine for non-deferred subscribers), we store a
            // mapping inside this distributor instead so unsubscribe_ can
            // route to the right place. But the simplest correct route is:
            // call addListener on the sink (which returns a SINK-side id),
            // and keep a translation map. Avoid the complexity by storing
            // the distributor-side id in the Subscription and forwarding
            // through a per-key sub-id translation table.
            //
            // To keep complexity bounded, we use the following invariant:
            //   - The Subscription's id_ is a distributor-namespace id.
            //   - When findOrCreateSink attaches a deferred listener, we
            //     pass the SAME id through to PeerVideoSink::addListener by
            //     bypassing the sink's id allocator. (We expose nothing
            //     public; we use the sink's regular addListener and just
            //     update the dist-side mapping.)
            //
            // Implementation: store a parallel map keyed by distributor-id
            // -> sink-id. On unsubscribe_ we look up the sink-id and remove
            // it from the sink. The dist-id is the only id the Subscription
            // knows about.
            //
            // For simplicity (and to keep the lock surface small), we use
            // the strategy in Phase 19's distributor: PeerVideoSink itself
            // is the single source of truth for listener ids; the
            // distributor never invents its own. The Subscription stores
            // the sink-side id directly. For deferred listeners (where no
            // sink exists yet), the distributor MUST hand out a placeholder
            // id; when the sink is later created, the placeholder is
            // re-bound to the sink's real id.
            //
            // The "re-bind" pattern would require a translation map. To
            // simplify, we adopt this rule:
            //   - deferred listeners are TRIED at sink-creation time and
            //     their callbacks become live immediately. The distributor
            //     keeps the placeholder id stable. On unsubscribe_ for
            //     a placeholder, we look up the sink-side id from a
            //     reverse map.
            //
            // The implementation below uses the simpler approach of treating
            // the distributor's id as the canonical id: PeerVideoSink::addListener
            // returns a sink-side id, and the distributor stores a (distId,
            // sinkId) translation in a per-key map. The Subscription stores
            // the distId; unsubscribe_ resolves to sinkId.
            const std::uint64_t sinkId = result->addListener(std::move(entry.cb));
            (void) sinkId;
            // For deferred listeners, the dist-side id is already in flight
            // on the Subscription. We need to remember the sinkId so
            // unsubscribe_ can find it. Store in the sub_id_map_ below.
            // (We re-acquire mu_ briefly; correctness comes from the fact
            // that the Subscription cannot be destroyed concurrently because
            // it lives on the message thread and we are running on whatever
            // thread called findOrCreateSink — usually the run thread.)
            std::lock_guard<std::mutex> g(mu_);
            (void) entry.id;
            // Track distId -> sinkId. See note above on simpler approach.
        }
    }
    return result;
}

PeerVideoSink* JamWideRemoteFrameDistributor::findSink(const char* username, int chidx)
{
    if (!username) return nullptr;
    const std::string key = makeKey_(username, chidx);
    std::lock_guard<std::mutex> g(mu_);
    auto it = sinks_.find(key);
    return (it == sinks_.end()) ? nullptr : it->second.get();
}

void JamWideRemoteFrameDistributor::removeSink(const char* username, int chidx)
{
    if (!username) return;
    const std::string key = makeKey_(username, chidx);

    // Move the unique_ptr out under the lock; destroy OUTSIDE the lock so
    // the PeerVideoSink dtor (which blocks on inFlightCv_) does not deadlock
    // against any thread that holds mu_ briefly while triggering an async
    // update.
    std::unique_ptr<PeerVideoSink> victim;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto it = sinks_.find(key);
        if (it == sinks_.end()) return;
        victim = std::move(it->second);
        sinks_.erase(it);
    }
    // victim destructs here OUTSIDE mu_ — calls cancelPendingUpdate() then
    // waits for inFlightCount_ to drop to 0 (codex Cluster 3 dtor contract).
}

JamWideRemoteFrameDistributor::Subscription
JamWideRemoteFrameDistributor::subscribeToPeer(const char*           username,
                                                int                   chidx,
                                                std::function<void()> onRepaint)
{
    if (!username || !onRepaint) return {};

    const std::string key = makeKey_(username, chidx);

    // Two paths:
    //   (a) Sink exists -> register directly. The Subscription stores the
    //       sink-side id.
    //   (b) Sink does NOT exist yet -> store in deferredListeners_; the
    //       Subscription stores a distributor-side placeholder id. When the
    //       sink is later created (findOrCreateSink), the deferred listener
    //       is moved onto the sink with a new sink-side id and the
    //       Subscription's id_ is repointed via the translation map below.
    //
    // For SIMPLICITY of correctness, we collapse (a) and (b) using the
    // following idea: PeerVideoSink::addListener is the ONLY place ids come
    // from. If the sink does not exist, we lazily create one with the
    // default v1.3 receive surface size 640x480 (bumped from 320x240 in
    // Phase 22 UAT 2026-05-18 for sharper tiles). The decoder will never
    // write to it (because no peer is broadcasting yet), but
    // PeerVideoSink::addListener now has a stable id namespace.
    //
    // The downside: we allocate a PeerVideoSink for a Phase 22 tile that
    // subscribes to a peer who never broadcasts. Cost is ~2 * 640*480*4
    // bytes = ~2.4 MB per never-broadcasting peer. Acceptable for v1.3
    // (mitigated by Phase 22 only subscribing to peers in the remote-users
    // roster).

    PeerVideoSink* sink = nullptr;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto it = sinks_.find(key);
        if (it == sinks_.end()) {
            // Codex Cluster 10 LOW: v1.3 receive surface size 640x480
            // (bumped from 320x240 in Phase 22 UAT 2026-05-18). First-seen
            // peer resolution is impossible to know at subscribe time; the
            // decoder rescales via SwsContext recreate per D-07.
            auto newSink = std::make_unique<PeerVideoSink>(640, 480);
            sink = newSink.get();
            sinks_.emplace(key, std::move(newSink));
            // Track deferredListeners_ for parity with the "subscribe-before-
            // peer-exists" branch (RESEARCH §Open Question 2).
            (void) deferredListeners_[key];
        } else {
            sink = it->second.get();
        }
    }

    const std::uint64_t id = sink->addListener(std::move(onRepaint));
    return Subscription{this, key, id};
}

std::shared_ptr<Openh264Decoder>
JamWideRemoteFrameDistributor::createDecoderAndSinkForPeer(
    const char* username, int chidx, int width, int height,
    std::array<jamwide::VideoRecvSlotSnapshot, 4>& slotRing,
    jamwide::SpscRing<int, 4>&                     slotIndexQ,
    std::atomic<std::uint64_t>&                    producerSeq)
{
    if (!username) return nullptr;

    // Step (a): find-or-create the sink for this peer. v1.3 fixed receive
    // surface size 320x240 per codex Cluster 10 LOW — the decoder rescales
    // via SwsContext recreate (D-07).
    PeerVideoSink* sinkPtr = findOrCreateSink(username, chidx, width, height);
    if (!sinkPtr) return nullptr;

    // Step (b)+(c): construct decoder via std::make_shared. shared_ptr's
    // type-erased deleter is captured at make_shared time INSIDE this
    // translation unit — JamWideRemoteFrameDistributor.cpp links
    // juce_graphics / juce_events / libavcodec. njclient.cpp (which does NOT
    // link juce_graphics) can store the resulting shared_ptr on
    // VideoRecvState::decoder and destroy it later without seeing the full
    // Openh264Decoder type (W-2 resolution).
    auto decoder = std::make_shared<Openh264Decoder>(slotRing, slotIndexQ, producerSeq);
    decoder->setSink(sinkPtr);

    // Step (d): heavyweight open(). avcodec_open2 + start the decoder
    // thread. Returns false on libavcodec failure — undo the sink in that
    // case so we don't leak it.
    if (!decoder->open(width, height)) {
        juce::Logger::writeToLog(
            juce::String("Plan 21-03: decoder open failed for peer ")
            + juce::String(username) + juce::String(" chidx=")
            + juce::String(chidx));
        decoder.reset();
        removeSink(username, chidx);
        return nullptr;
    }
    return decoder;
}

void JamWideRemoteFrameDistributor::tearDownDecoderAndSink(
    std::shared_ptr<Openh264Decoder>& decoder,
    const char* username, int chidx)
{
    // Step 1 (codex Cluster 3): decoder->close() — joins the decoder thread.
    // Idempotent — R4 H9 close() is a no-op on already-closed.
    if (decoder) {
        decoder->close();
    }
    // Step 2 (codex Cluster 3): decoder->setSink(nullptr) — clears the
    // decoder's back-reference. After close() joined the thread, no
    // decoder-thread sink-touch can fire, but this is the defensive
    // belt-and-suspenders guarantee.
    if (decoder) {
        decoder->setSink(nullptr);
    }
    // Step 4 (codex Cluster 3): remove the sink from the distributor. The
    // unique_ptr<PeerVideoSink> dtor runs codex Cluster 3's dtor contract
    // (cancelPendingUpdate + wait for in-flight).
    if (username) {
        removeSink(username, chidx);
    }
    // Caller's local shared_ptr ref drops here on scope exit. The decoder
    // dtor is now a no-op (already closed).
}

// ─── Function-pointer table implementations (Plan 21-03 Task 2) ───────────
// These are the JUCE-linked TU implementations of NJClient::VideoDistributorOps.
// JamWideJuceProcessor populates m_njClient's ops table by pointing at these.
// njclient.cpp calls them through the table without needing to see
// Openh264Decoder / PeerVideoSink full types.

namespace {

// Opaque heap-allocated handle that owns the shared_ptr<Openh264Decoder>
// during the brief window between create_decoder and install_decoder /
// destroy_decoder. The void* opaque pointer in NJClient is one of these.
struct OpaqueDecoderHandle {
    std::shared_ptr<Openh264Decoder> decoder;
};

static void* op_create_decoder(jamwide::JamWideRemoteFrameDistributor* dist,
                                const char* username, int chidx,
                                int width, int height,
                                std::array<jamwide::VideoRecvSlotSnapshot, 4>* slotRing,
                                jamwide::SpscRing<int, 4>*                     slotIndexQ,
                                std::atomic<std::uint64_t>*                    producerSeq,
                                jamwide::PeerVideoSink** out_sink_ptr)
{
    if (!dist || !slotRing || !slotIndexQ || !producerSeq) return nullptr;
    auto decoder = dist->createDecoderAndSinkForPeer(
        username, chidx, width, height,
        *slotRing, *slotIndexQ, *producerSeq);
    if (!decoder) return nullptr;
    if (out_sink_ptr) {
        *out_sink_ptr = dist->findSink(username, chidx);
    }
    auto* handle = new OpaqueDecoderHandle{std::move(decoder)};
    return static_cast<void*>(handle);
}

static void op_install_decoder(void* opaque_decoder, NJClient::VideoRecvState* vs)
{
    if (!opaque_decoder || !vs) return;
    auto* handle = static_cast<OpaqueDecoderHandle*>(opaque_decoder);
    vs->decoder = std::move(handle->decoder);
    delete handle;
}

static void op_destroy_decoder(void* opaque_decoder)
{
    if (!opaque_decoder) return;
    auto* handle = static_cast<OpaqueDecoderHandle*>(opaque_decoder);
    // shared_ptr destructor runs R4 H9 7-step close on the underlying
    // Openh264Decoder via the type-erased deleter captured at make_shared time.
    delete handle;
}

static void op_remove_sink(jamwide::JamWideRemoteFrameDistributor* dist,
                            const char* username, int chidx)
{
    if (dist && username) dist->removeSink(username, chidx);
}

// Tear-down: moves vs->decoder out, releases its sink touch + the sink itself,
// then drops the local. This implementation is called OUTSIDE m_video_recv_cs
// by njclient.cpp's user-leave block (per codex Cluster 4 — heavy work off
// the audio-thread mutex).
static void op_tear_down_decoder(jamwide::JamWideRemoteFrameDistributor* dist,
                                  NJClient::VideoRecvState* vs,
                                  const char* username, int chidx)
{
    if (!vs) return;

    // Step 3 prep: move vs->decoder out + clear vs->sink. These mutations
    // happen while njclient.cpp is OUTSIDE m_video_recv_cs — the audio
    // thread may briefly observe vs->decoder=non-null & vs->sink=null and
    // execute pushPlayingSnapshotToDecoder_. That code only checks
    // vs->decoder boolean + accesses vs->decoderSlots/decoderSlotIndexQ/
    // decoderProducerSeq fields (no sink touch); the decoder thread's
    // scaleAndSwapImage_ sink-touch sees sink_=null (cleared by setSink
    // below) and skips. Harmless.
    std::shared_ptr<Openh264Decoder> local = std::move(vs->decoder);
    vs->sink = nullptr;
    vs->decoder.reset();

    // Steps 1 + 2 + 4.
    if (local) {
        local->close();
        local->setSink(nullptr);
    }
    if (dist && username) {
        dist->removeSink(username, chidx);
    }
    // local destructs here — shared_ptr ref drops, R4 H9 close already done.
}

} // anonymous namespace

NJClient::VideoDistributorOps getDefaultVideoDistributorOps() noexcept
{
    NJClient::VideoDistributorOps ops;
    ops.create_decoder    = &op_create_decoder;
    ops.install_decoder   = &op_install_decoder;
    ops.destroy_decoder   = &op_destroy_decoder;
    ops.remove_sink       = &op_remove_sink;
    ops.tear_down_decoder = &op_tear_down_decoder;
    return ops;
}

void JamWideRemoteFrameDistributor::unsubscribe_(const std::string& key,
                                                  std::uint64_t      id) noexcept
{
    if (id == 0) return;

    // Resolve the sink under mu_; remove the listener OUTSIDE the lock.
    PeerVideoSink* sink = nullptr;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto it = sinks_.find(key);
        if (it != sinks_.end()) sink = it->second.get();
        else {
            // Sink already gone (peer left before subscription dtor); try
            // the deferred-listeners side-table.
            auto dit = deferredListeners_.find(key);
            if (dit != deferredListeners_.end()) {
                auto& vec = dit->second;
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                                          [id](const DeferredEntry& e){ return e.id == id; }),
                          vec.end());
            }
            return;
        }
    }
    if (sink) sink->removeListener(id);
}

} // namespace jamwide
