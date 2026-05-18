#include "VideoTileBase.h"
#include "../JamWideLookAndFeel.h"

namespace jamwide {

namespace {

// Shared overlay paint: faint dark-translucent backdrop with soft-white text
// centred on the tile. Used for both "video starting..." (Phase 21 D-19) and
// "syncing..." (Phase 21 D-17).
void drawOverlay_(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text)
{
    g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceScrim));
    g.fillRoundedRectangle(bounds.toFloat().reduced(8.0f), 4.0f);

    g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary).withAlpha(0.70f));
    g.setFont(juce::Font(12.0f));
    g.drawFittedText(text, bounds, juce::Justification::centred, 1);
}

// Top-right popout icon: ~14x14 box at (getWidth() - 18, 4); diagonal line
// from bottom-left to top-right of the icon box renders the ↗ glyph.
constexpr int kPopoutIconSize  = 14;
constexpr int kPopoutIconInset = 4;
constexpr int kPopoutHitInset  = 22; // hit-region right/top inset (slightly larger than icon for fingertip ergonomics)

} // anonymous namespace

void VideoTileBase::paintCommon(juce::Graphics& g,
                                const juce::Image& frame,
                                bool firstFrameSeen,
                                int  holdCount,
                                bool synced,
                                const juce::String& username,
                                bool hovering)
{
    // Background fill (child-surface tone).
    g.fillAll(juce::Colour(JamWideLookAndFeel::kSurfaceChild));

    // 4:3 letterbox paint — RectanglePlacement::centred preserves aspect so
    // a 320x240 image inside a 400x300 tile renders centred with side bars.
    if (frame.isValid()) {
        g.drawImage(frame, getLocalBounds().toFloat(),
                    juce::RectanglePlacement::centred);
    }

    // Phase 21 status overlays.
    if (!firstFrameSeen) {
        // D-19: "video starting..." until the first decoded frame is observed.
        drawOverlay_(g, getLocalBounds(), juce::String("video starting..."));
    } else if (holdCount >= 2 && !synced) {
        // D-17: "syncing..." when the audio-video pairing has stalled.
        drawOverlay_(g, getLocalBounds(), juce::String("syncing..."));
    }

    // Username strip (bottom, ~18px tall) — suppressed when hovering so the
    // user can inspect the full frame.
    if (!hovering && username.isNotEmpty()) {
        constexpr int kStripHeight = 18;
        auto stripBounds = getLocalBounds().removeFromBottom(kStripHeight);
        g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceScrim));
        g.fillRect(stripBounds);

        g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary).withAlpha(0.70f));
        g.setFont(juce::Font(11.0f));
        g.drawFittedText(username,
                         stripBounds.reduced(4, 0),
                         juce::Justification::centredLeft,
                         1);
    }

    // Popout ↗ icon — top-right corner, always visible (D-04).
    {
        const int x = getWidth()  - kPopoutIconSize - kPopoutIconInset;
        const int y = kPopoutIconInset;
        juce::Rectangle<int> iconBox{x, y, kPopoutIconSize, kPopoutIconSize};

        // Faint dark-translucent backdrop matches Phase 21 overlay style.
        g.setColour(juce::Colour(JamWideLookAndFeel::kSurfaceScrim));
        g.fillRoundedRectangle(iconBox.toFloat(), 2.0f);

        // Diagonal line from bottom-left to top-right inside the box renders ↗.
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary).withAlpha(0.70f));
        const float pad = 3.0f;
        const float bl_x = static_cast<float>(x) + pad;
        const float bl_y = static_cast<float>(y + kPopoutIconSize) - pad;
        const float tr_x = static_cast<float>(x + kPopoutIconSize) - pad;
        const float tr_y = static_cast<float>(y) + pad;
        g.drawLine(bl_x, bl_y, tr_x, tr_y, 1.5f);
        // Small arrowhead at the top-right tip.
        g.drawLine(tr_x - 3.5f, tr_y, tr_x,         tr_y, 1.5f);
        g.drawLine(tr_x,        tr_y, tr_x,        tr_y + 3.5f, 1.5f);
    }
}

bool VideoTileBase::popoutIconHitTest_(const juce::MouseEvent& e) const noexcept
{
    // Top-right corner hit region: from x = (width - 22) to right edge, and
    // from y = 0 to y = 22. Generous fingertip ergonomic margin around the
    // 14x14 icon (kPopoutIconSize + kPopoutIconInset * 2 = 22).
    const int x = e.getPosition().getX();
    const int y = e.getPosition().getY();
    return x >= (getWidth() - kPopoutHitInset) && y <= kPopoutHitInset;
}

} // namespace jamwide
