#pragma once
// Plan 22-03 Task 2 — PopoutPlaceholderCard: dark VB-style frame card that
// replaces the live peer tile in the in-main-view band (and the detached
// band, if open) while that peer's popout window is open or hidden.
//
// Layout/UX (codex H3 mapping):
//   - State (A) "absent" / (D) "destroyed"   → card is HIDDEN, live tile renders.
//   - State (B) "popout visible"              → card visible with "Popped out →"
//                                              label; clicking the card brings
//                                              back (destroys popout → state D).
//   - State (C) "popout hidden"               → card visible identically to (B);
//                                              clicking the card also destroys.
//   The card's CLICK is the EXCLUSIVE destroy path per codex H3 — the tile
//   `↗` toggles visibility but does NOT destroy.
//
// Visual: dark VB-style rounded rectangle matching the surrounding chrome,
// faint dark-translucent backdrop, soft-white centred label.

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jamwide {

class PopoutPlaceholderCard : public juce::Component {
public:
    PopoutPlaceholderCard();
    ~PopoutPlaceholderCard() override = default;

    PopoutPlaceholderCard(const PopoutPlaceholderCard&) = delete;
    PopoutPlaceholderCard& operator=(const PopoutPlaceholderCard&) = delete;

    // Override the default "Popped out →" label.
    void setLabel(const juce::String& s);

    // juce::Component
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;

    // codex H3 exclusive destroy path — fired on mouseDown. The editor's
    // lambda calls bringBackRemotePopout(username), which destroys the
    // popout window and clears poppedOutPeers_ on BOTH bands (M7 sync).
    std::function<void()> onBringBack;

private:
    juce::String label_ = "Popped out \xE2\x86\x92";   // U+2192 RIGHTWARDS ARROW
};

} // namespace jamwide
