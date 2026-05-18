#pragma once
// Plan 22-03 Task 2 — DetachedGridPlaceholderCard: near-clone of
// PopoutPlaceholderCard rendered in the in-main-view band when the
// whole-grid detached window is open. Single full-band card with default
// label "Grid is in detached window →"; clicking the card calls the
// editor's reattachGrid (destroys the DetachedGridWindow, restores live
// tile rendering in-place).
//
// D-03 mapping: "When the grid is detached, the in-main-view band renders
// a single DetachedGridPlaceholderCard."

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jamwide {

class DetachedGridPlaceholderCard : public juce::Component {
public:
    DetachedGridPlaceholderCard();
    ~DetachedGridPlaceholderCard() override = default;

    DetachedGridPlaceholderCard(const DetachedGridPlaceholderCard&) = delete;
    DetachedGridPlaceholderCard& operator=(const DetachedGridPlaceholderCard&) = delete;

    void setLabel(const juce::String& s);

    // juce::Component
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;

    // Fired on mouseDown — editor's lambda calls reattachGrid().
    std::function<void()> onBringBack;

private:
    juce::String label_ = "Grid is in detached window \xE2\x86\x92";   // U+2192 RIGHTWARDS ARROW
};

} // namespace jamwide
