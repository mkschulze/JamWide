#include "VideoGridBand.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "computeGridLayout.h"
#include "RemotePeerTile.h"
#include "SelfVideoTile.h"
#include "VideoTileBase.h"

#include "../BotFilter.h"
#include "../ConnectionBar.h"
#include "../JamWideLookAndFeel.h"
#include "../../JamWideJuceProcessor.h"
#include "../../video/distributor/JamWideRemoteFrameDistributor.h"

namespace jamwide {

namespace {

constexpr int kHeaderHeight       = 24;
constexpr int kResizerHeight      = 4;
constexpr int kDetachIconRight    = 38;  // distance from right edge of band
constexpr int kCloseIconRight     = 18;
constexpr int kIconHitWidth       = 20;
constexpr int kIconHitHeight      = 20;

} // namespace

VideoGridBand::VideoGridBand(JamWideJuceProcessor&            processor,
                             ConnectionBar&                   bar,
                             JamWideFrameDistributor*         selfDist,
                             JamWideRemoteFrameDistributor*   remoteDist,
                             Mode                             mode)
    : processorRef_(processor)
    , connectionBarRef_(bar)
    , selfDistributor_(selfDist)
    , remoteDistributor_(remoteDist)
    , mode_(mode)
{
    // D-13 Option a — 30 Hz Timer poll cadence. Matches the rest of the UI
    // (`ChannelStripArea` uses the same cadence for VU + roster polling).
    startTimerHz(30);
}

VideoGridBand::~VideoGridBand()
{
    // Order matters — stop the timer FIRST so no further callbacks can land
    // on a half-destroyed instance. Then explicitly tear down child tiles
    // (selfTile_ + peerTiles_) before the base juce::Component dtor runs.
    stopTimer();
    peerTiles_.clear();
    selfTile_.reset();
}

void VideoGridBand::timerCallback()
{
    bool layoutChanged = false;

    // ─── Step A — D-07 self-tile gating via self-broadcast atomic ──────
    // The grid only shows the self-tile while the user is actively
    // broadcasting (Phase 20-03 setBroadcastVideo(true) flips the
    // `cameraIsBroadcasting_` flag inside ConnectionBar via the editor's
    // Broadcast lambda). Edge-triggered: mount on false→true; unmount on
    // true→false.
    const bool nowBroadcasting = connectionBarRef_.getCameraIsBroadcasting()
                              && selfDistributor_ != nullptr;
    if (nowBroadcasting != selfBroadcastingLast_)
    {
        if (nowBroadcasting)
        {
            selfTile_ = std::make_unique<SelfVideoTile>(*selfDistributor_);
            addAndMakeVisible(*selfTile_);
            // M5 typed-callback wiring — pass-through. The tile's
            // mouseDown sets `kind = Self`, `username = ""`.
            selfTile_->setOnPopoutClicked([this](jamwide::VideoPopoutTarget t) {
                if (onPeerPopoutRequested) onPeerPopoutRequested(std::move(t));
            });
        }
        else
        {
            if (selfTile_)
                removeChildComponent(selfTile_.get());
            selfTile_.reset();
        }
        selfBroadcastingLast_ = nowBroadcasting;
        layoutChanged = true;
    }

    // ─── Step B — D-13 Option a per-peer sink poll under cachedUsersMutex ─
    // Snapshot the non-bot peer name set under the same roster mutex
    // ChannelStripArea uses (H1 codex closure — `jamwide::isBot` /
    // `jamwide::stripAtSuffix` from BotFilter.h).
    std::vector<juce::String> currentNonBotPeers;
    if (remoteDistributor_ != nullptr)
    {
        std::lock_guard<std::mutex> lk(processorRef_.cachedUsersMutex);
        currentNonBotPeers.reserve(processorRef_.cachedUsers.size());
        for (const auto& u : processorRef_.cachedUsers)
        {
            juce::String rawName(u.name);
            if (jamwide::isBot(rawName))
                continue;
            currentNonBotPeers.push_back(jamwide::stripAtSuffix(rawName));
        }
    }

    // Mount new peer tiles (sink found and not yet in peerTiles_).
    for (const auto& name : currentNonBotPeers)
    {
        if (peerTiles_.find(name) != peerTiles_.end())
            continue;
        if (remoteDistributor_ == nullptr)
            continue;
        if (remoteDistributor_->findSink(name.toRawUTF8(), /*chidx*/ 1) == nullptr)
            continue;

        auto tile = std::make_unique<RemotePeerTile>(
            *remoteDistributor_, name, /*chidx*/ 1);
        // M5 typed-callback wiring — pass-through. The tile's mouseDown
        // sets `kind = RemotePeer`, `username = username_`.
        tile->setOnPopoutClicked([this](jamwide::VideoPopoutTarget t) {
            if (onPeerPopoutRequested) onPeerPopoutRequested(std::move(t));
        });
        addAndMakeVisible(*tile);
        peerTiles_.emplace(name, std::move(tile));
        layoutChanged = true;
    }

    // Unmount peer tiles whose name vanished from cachedUsers OR whose sink
    // disappeared (Phase 21 four-step user-leave shutdown protocol).
    std::vector<juce::String> toRemove;
    for (const auto& [name, tile] : peerTiles_)
    {
        const bool stillInRoster = std::find(currentNonBotPeers.begin(),
                                              currentNonBotPeers.end(),
                                              name) != currentNonBotPeers.end();
        const bool sinkAlive = remoteDistributor_ != nullptr
                            && remoteDistributor_->findSink(name.toRawUTF8(), 1) != nullptr;
        if (! stillInRoster || ! sinkAlive)
            toRemove.push_back(name);
    }
    for (const auto& name : toRemove)
    {
        if (auto it = peerTiles_.find(name); it != peerTiles_.end())
        {
            removeChildComponent(it->second.get());
            peerTiles_.erase(it);
            layoutChanged = true;
        }
    }

    if (layoutChanged)
        resized();
}

void VideoGridBand::resized()
{
    // Header (top 24 px) + resizer (bottom 4 px) are painted, not laid out
    // via Component children. Tiles fill the central area.
    auto bounds = getLocalBounds();
    bounds.removeFromTop(kHeaderHeight);
    bounds.removeFromBottom(kResizerHeight);
    const auto tileArea = bounds;

    if (tileArea.getWidth() <= 0 || tileArea.getHeight() <= 0)
        return;

    // ─── D-03: whole-band placeholder when grid is detached ───────────────
    // When the user detaches the grid into a DetachedGridWindow, the
    // in-main-view band renders a single full-band DetachedGridPlaceholderCard
    // ("Grid is in detached window →"). All tiles + peer placeholders are
    // hidden in this branch.
    if (detachedActive_ && detachedPlaceholder_)
    {
        if (selfTile_) selfTile_->setVisible(false);
        for (auto& [name, tile] : peerTiles_)
            tile->setVisible(false);
        for (auto& [name, ph] : peerPlaceholders_)
            ph->setVisible(false);

        detachedPlaceholder_->setVisible(true);
        detachedPlaceholder_->setBounds(tileArea);
        return;
    }
    // Hide the detached-grid placeholder otherwise (the editor's
    // setDetachedActive(false) path drives the transition).
    if (detachedPlaceholder_)
        detachedPlaceholder_->setVisible(false);

    // Self-tile is always the first slot per D-07; peer iteration order is
    // implementation-defined for std::unordered_map, but the tile slot order
    // is NOT part of the user-facing contract — layout is recomputed from
    // peer count alone, so the visible tile size + grid shape is stable.
    //
    // Plan 22-03 Task 2 — visible "slot" can be either the tile OR the
    // placeholder card. We build the visibleSlots vector with whichever
    // child component owns this slot now, and hide the other one.
    struct Slot { juce::Component* component; juce::String username; bool isSelf; };
    std::vector<Slot> visibleSlots;
    visibleSlots.reserve(1 + peerTiles_.size());

    if (selfTile_)
    {
        // Self-tile has no placeholder card (D-09 — Self-popout reuses
        // Phase 19's CameraPreviewWindow, which lives outside the band).
        selfTile_->setVisible(true);
        visibleSlots.push_back({ selfTile_.get(), juce::String{}, true });
    }
    for (auto& [name, tile] : peerTiles_)
    {
        const bool poppedOut = poppedOutPeers_.find(name) != poppedOutPeers_.end();
        if (poppedOut)
        {
            // Show placeholder card (lazy-created on demand by
            // setPeerPoppedOut), hide the live tile.
            tile->setVisible(false);
            auto ph_it = peerPlaceholders_.find(name);
            if (ph_it != peerPlaceholders_.end() && ph_it->second)
            {
                ph_it->second->setVisible(true);
                visibleSlots.push_back({ ph_it->second.get(), name, false });
            }
            else
            {
                // Defensive: placeholder slot missing somehow. Skip this
                // peer's slot rather than crashing. setPeerPoppedOut should
                // have created the placeholder before resized() runs.
            }
        }
        else
        {
            tile->setVisible(true);
            // Hide any placeholder that might exist for this peer.
            auto ph_it = peerPlaceholders_.find(name);
            if (ph_it != peerPlaceholders_.end() && ph_it->second)
                ph_it->second->setVisible(false);
            visibleSlots.push_back({ tile.get(), name, false });
        }
    }

    if (visibleSlots.empty())
        return;

    const auto layout = computeGridLayout(static_cast<int>(visibleSlots.size()),
                                          tileArea.getWidth(),
                                          tileArea.getHeight());
    if (layout.cols <= 0 || layout.tileW <= 0)
        return;

    for (int i = 0; i < static_cast<int>(visibleSlots.size()); ++i)
    {
        const int col = i % layout.cols;
        const int row = i / layout.cols;
        const int x   = tileArea.getX() + layout.marginX
                      + layout.spacing
                      + col * (layout.tileW + layout.spacing);
        const int y   = tileArea.getY() + layout.marginY
                      + layout.spacing
                      + row * (layout.tileH + layout.spacing);
        visibleSlots[(size_t) i].component->setBounds(x, y, layout.tileW, layout.tileH);
    }
}

void VideoGridBand::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Header strip — kSurfaceStrip background; same in both modes.
    auto headerArea = bounds.removeFromTop(kHeaderHeight);
    g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    g.fillRect(headerArea);

    // Peer count badge on the LEFT of the header — same in both modes.
    {
        const int peerCount = static_cast<int>(peerTiles_.size())
                            + (selfTile_ ? 1 : 0);
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        juce::String label = "Video grid";
        if (peerCount > 0)
            label += "  [" + juce::String(peerCount) + "]";
        g.drawText(label,
                   headerArea.reduced(8, 0),
                   juce::Justification::centredLeft,
                   false);
    }

    // M7 codex closure — detach `↗` AND `×` are drawn ONLY on the main band.
    // The detached band's `↗` would mean "recursive detach" (nonsensical) and
    // its `×` is handled by the wrapping DocumentWindow's title bar.
    if (mode_ == Mode::MainBand)
    {
        const int gw = getWidth();
        // ↗ detach icon
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
        const juce::Rectangle<float> detachR(
            (float)(gw - kDetachIconRight),
            (float)(headerArea.getY() + 2),
            (float) kIconHitWidth,
            (float) kIconHitHeight);
        g.drawText("\xE2\x86\x97",  // U+2197 NORTH EAST ARROW
                   detachR.toNearestInt(),
                   juce::Justification::centred,
                   false);

        // × close icon
        const juce::Rectangle<float> closeR(
            (float)(gw - kCloseIconRight),
            (float)(headerArea.getY() + 2),
            (float) kIconHitWidth,
            (float) kIconHitHeight);
        g.drawText("\xC3\x97",  // U+00D7 MULTIPLICATION SIGN
                   closeR.toNearestInt(),
                   juce::Justification::centred,
                   false);
    }

    // Bottom 4px resizer hint — both modes; the drag handler ignores it on
    // the detached band but the visual hint is harmless.
    auto resizerArea = getLocalBounds().removeFromBottom(kResizerHeight);
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.fillRect(resizerArea);
}

void VideoGridBand::mouseDown(const juce::MouseEvent& e)
{
    // Bottom 4px resizer-strip hit-test (main band only — the detached band
    // is sized by its wrapping DocumentWindow, not by this drag handler).
    if (mode_ == Mode::MainBand
        && e.y >= getHeight() - kResizerHeight)
    {
        resizing_        = true;
        dragStartY_      = e.getScreenY();
        dragStartHeight_ = currentBandHeight_;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    // Header click hit-tests.
    if (e.y < kHeaderHeight)
    {
        const int gw = getWidth();
        const bool inCloseHit  = (e.x >= gw - kCloseIconRight  - kIconHitWidth/2
                               && e.x <= gw - kCloseIconRight  + kIconHitWidth/2);
        const bool inDetachHit = (e.x >= gw - kDetachIconRight - kIconHitWidth/2
                               && e.x <= gw - kDetachIconRight + kIconHitWidth/2);

        // Close (×) — both modes get the callback wired (the editor's
        // detached-band close lambda closes the DocumentWindow).
        if (inCloseHit)
        {
            if (onCloseRequested) onCloseRequested();
            return;
        }

        // M7 — detach (↗) hit-region ONLY fires on the main band. The
        // detached band ignores clicks in that zone.
        if (inDetachHit && mode_ == Mode::MainBand)
        {
            if (onDetachRequested) onDetachRequested();
            return;
        }
    }
}

void VideoGridBand::mouseDrag(const juce::MouseEvent& e)
{
    if (! resizing_)
        return;
    const int delta     = e.getScreenY() - dragStartY_;
    const int newHeight = juce::jlimit(140, 800, dragStartHeight_ + delta);
    if (newHeight != currentBandHeight_)
    {
        currentBandHeight_ = newHeight;
        if (onHeightChangeRequested) onHeightChangeRequested(newHeight);
    }
}

void VideoGridBand::mouseUp(const juce::MouseEvent& /*e*/)
{
    if (resizing_)
    {
        resizing_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// ─── Plan 22-03 Task 2 — setPeerPoppedOut + setDetachedActive bodies ───────
// Toggle the placeholder-vs-tile slot rendering. Both methods mutate band
// state then call resized() to apply.

void VideoGridBand::setPeerPoppedOut(const juce::String& username,
                                     bool                poppedOut)
{
    if (username.isEmpty()) return;   // defensive — caller should never pass empty

    if (poppedOut)
    {
        poppedOutPeers_.insert(username);

        // Lazy-create the placeholder card for this peer (mirrors how
        // peerTiles_ are lazy-created in timerCallback). Hidden initially;
        // resized() flips it visible based on poppedOutPeers_ membership.
        if (peerPlaceholders_.find(username) == peerPlaceholders_.end())
        {
            auto ph = std::make_unique<jamwide::PopoutPlaceholderCard>();
            ph->setLabel(juce::String::fromUTF8("Popped out \xE2\x86\x92"));   // U+2192 RIGHTWARDS ARROW
            // codex H3 — placeholder click is the EXCLUSIVE destroy path.
            // Editor's wired onPlaceholderBringBack lambda routes the
            // non-empty username to bringBackRemotePopout.
            const juce::String capturedUsername = username;
            ph->onBringBack = [this, capturedUsername]() {
                if (onPlaceholderBringBack) onPlaceholderBringBack(capturedUsername);
            };
            addChildComponent(*ph);   // not visible until resized() picks it
            peerPlaceholders_.emplace(username, std::move(ph));
        }
    }
    else
    {
        poppedOutPeers_.erase(username);
        // We intentionally KEEP the placeholder card in peerPlaceholders_
        // (it's only ~tens of bytes) so re-popping out the same peer is
        // a no-allocation toggle. resized() will hide it via setVisible(false).
    }

    resized();
}

void VideoGridBand::setDetachedActive(bool active)
{
    detachedActive_ = active;

    if (active && ! detachedPlaceholder_)
    {
        detachedPlaceholder_ = std::make_unique<jamwide::DetachedGridPlaceholderCard>();
        detachedPlaceholder_->setLabel(juce::String::fromUTF8("Grid is in detached window \xE2\x86\x92"));
        // Empty-string username signals "reattach grid" in the editor's
        // onPlaceholderBringBack dispatch (vs. non-empty = bringBackRemotePopout).
        detachedPlaceholder_->onBringBack = [this]() {
            if (onPlaceholderBringBack) onPlaceholderBringBack(juce::String{});
        };
        addChildComponent(*detachedPlaceholder_);   // hidden until resized()
    }

    resized();
}

} // namespace jamwide
