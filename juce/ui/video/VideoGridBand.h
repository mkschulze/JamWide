#pragma once
// Phase 22-02 Task 1 — VideoGridBand: in-main-view native video grid band.
//
// Container component that hosts the Plan 22-01 tile substrate
// (SelfVideoTile + RemotePeerTile) in a deterministic N×M grid layout
// driven by `computeGridLayout`. Inserted by JamWideJuceEditor between
// `sessionInfoStrip` and `channelStripArea` and toggled visible/hidden
// by the new ConnectionBar "Grid" button. The audio session is NEVER
// touched by the toggle (DISP-04: pure UI operation).
//
// Lifecycle (D-13 Option a):
//   - 30 Hz `juce::Timer` polls Phase 21's `JamWideRemoteFrameDistributor`
//     via `findSink(name, chidx)` per non-bot peer in the processor's
//     cached roster. New sinks mount a fresh `RemotePeerTile`;
//     vanished sinks unmount the tile.
//   - The same timer observes `connectionBar.getCameraIsBroadcasting()`
//     for D-07 self-tile gating: edge transitions mount or unmount a
//     fresh `SelfVideoTile`.
//
// Codex review closures embedded in this header:
//   - H1: filter callsites use `jamwide::isBot` / `jamwide::stripAtSuffix`
//         from `juce/ui/BotFilter.h` (extracted in Task 0).
//   - H2 NARROWED (iter-2): in-memory `peerTiles_` uses
//         `std::unordered_map<juce::String, std::unique_ptr<RemotePeerTile>>`.
//         `juce::HashMap` cannot hold a move-only `unique_ptr` because its
//         `set()` does copy-assign; the precedent at `juce/midi/MidiMapper.h:105`
//         proves `std::unordered_map<juce::String, V>` compiles cleanly
//         (JUCE 8 ships `std::hash<juce::String>`). The `juce::HashMap`
//         narrowing applies ONLY to Plan 22-04's copyable
//         `juce::Rectangle<int>`-valued popout-bounds map.
//   - M5: popout callback signature is
//         `std::function<void(jamwide::VideoPopoutTarget)>` — typed
//         enum-driven, no magic-string sentinel.
//   - M7: explicit nested `enum class Mode { MainBand, DetachedBand };`
//         drives Mode-conditional paint (detach `↗` only on `MainBand`)
//         and Mode-conditional auto-open behaviour. The DetachedGridWindow
//         in Plan 22-03 constructs its inner VideoGridBand with
//         `Mode::DetachedBand`; the editor still owns popout state and
//         drives both bands' tile visibility consistently.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "VideoTileBase.h"  // For jamwide::VideoPopoutTarget
#include "PopoutPlaceholderCard.h"
#include "DetachedGridPlaceholderCard.h"

class JamWideJuceProcessor;
class ConnectionBar;

namespace jamwide {

class JamWideFrameDistributor;
class JamWideRemoteFrameDistributor;
class SelfVideoTile;
class RemotePeerTile;

class VideoGridBand : public juce::Component,
                     private juce::Timer
{
public:
    // M7 codex closure — drives mode-conditional paint AND auto-open
    // behaviour. The editor constructs the main band with `Mode::MainBand`;
    // Plan 22-03's `DetachedGridWindow` constructs its inner band with
    // `Mode::DetachedBand`. Defaulted in the ctor so existing call sites
    // compile unchanged.
    enum class Mode { MainBand, DetachedBand };

    VideoGridBand(JamWideJuceProcessor&            processor,
                  ConnectionBar&                   bar,
                  JamWideFrameDistributor*         selfDist,
                  JamWideRemoteFrameDistributor*   remoteDist,
                  Mode                             mode = Mode::MainBand);
    ~VideoGridBand() override;

    // Copy semantics are deleted by JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // (expanded at the bottom of the class body).

    // juce::Component overrides — definitions live in .cpp where the full
    // SelfVideoTile/RemotePeerTile types are visible.
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // M5 typed-callback signature — VideoPopoutTarget carries Kind + username.
    // No magic-string sentinel exists anywhere in the dataflow.
    std::function<void()>                          onCloseRequested;
    std::function<void()>                          onDetachRequested;
    std::function<void(jamwide::VideoPopoutTarget)> onPeerPopoutRequested;
    std::function<void(int)>                       onHeightChangeRequested;

    // Placeholder bring-back forwarder (Plan 22-03 Task 2). The editor's
    // wired lambda dispatches: empty username → reattachGrid(), non-empty
    // username → bringBackRemotePopout(username). Both paths are the
    // codex H3 EXCLUSIVE destroy path for popouts / detached-grid.
    std::function<void(const juce::String& username)> onPlaceholderBringBack;

    // M7 plumbing — Plan 22-03 implements the bodies (swap tile for
    // placeholder card); declared here so the editor can call into BOTH
    // the main band and the detached band's inner VideoGridBand.
    void setPeerPoppedOut(const juce::String& username, bool poppedOut);
    void setDetachedActive(bool active);

    // Band height accessor pair — editor calls `getCurrentBandHeight()` to
    // compute `resized()` slot size; the resizer drag invokes
    // `setCurrentBandHeight()` and posts `onHeightChangeRequested`.
    int  getCurrentBandHeight() const noexcept { return currentBandHeight_; }
    void setCurrentBandHeight(int h) noexcept { currentBandHeight_ = juce::jlimit(140, 800, h); }

private:
    void timerCallback() override;

    JamWideJuceProcessor&            processorRef_;
    ConnectionBar&                   connectionBarRef_;
    JamWideFrameDistributor*         selfDistributor_;
    JamWideRemoteFrameDistributor*   remoteDistributor_;
    Mode                             mode_;

    // MEMBER-ORDER: tiles declared BEFORE selfBroadcastingLast_ + currentBandHeight_
    // so the child component destructors run before bool/int helper data.
    // `unique_ptr<SelfVideoTile>` deleter resolves in the .cpp where the
    // full SelfVideoTile type is visible (mirrors ConnectionBar pattern).
    std::unique_ptr<SelfVideoTile>                                            selfTile_;
    // H2 NARROWED iter-2 — std::unordered_map (NOT juce::HashMap) so the
    // move-only `std::unique_ptr<RemotePeerTile>` value type compiles.
    std::unordered_map<juce::String, std::unique_ptr<RemotePeerTile>>         peerTiles_;

    // Plan 22-03 Task 2 — placeholder cards swap in for tiles when a peer
    // is popped out OR the whole-grid is detached. Cards are children of
    // the band; they live alongside the tiles and are toggled by
    // setPeerPoppedOut / setDetachedActive + resized().
    //
    // H2 NARROWED — std::unordered_map for the per-peer placeholders too
    // (move-only unique_ptr value type; juce::HashMap::set copy-assigns).
    std::unordered_set<juce::String>                                            poppedOutPeers_;
    bool                                                                        detachedActive_ = false;
    std::unordered_map<juce::String, std::unique_ptr<jamwide::PopoutPlaceholderCard>> peerPlaceholders_;
    std::unique_ptr<jamwide::DetachedGridPlaceholderCard>                            detachedPlaceholder_;

    bool                             selfBroadcastingLast_ = false;
    int                              currentBandHeight_    = 280;
    bool                             resizing_             = false;
    int                              dragStartY_           = 0;
    int                              dragStartHeight_      = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoGridBand)
};

} // namespace jamwide
