#include "BeatBar.h"
#include "JamWideLookAndFeel.h"
#include "../JamWideJuceProcessor.h"

BeatBar::BeatBar()
{
    setOpaque(true);
}

void BeatBar::update(int bpi, int currentBeat, int intervalPos, int intervalLen)
{
    bpi_ = bpi;
    currentBeat_ = currentBeat;
    intervalPos_ = intervalPos;
    intervalLen_ = intervalLen;
    repaint();
}

void BeatBar::triggerFlash()
{
    flashStartMs_ = juce::Time::getMillisecondCounterHiRes();
}

void BeatBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kVuBackground));

    // --- BPM/BPI Label Area (72px, left side) ---
    {
        auto labelArea = getLocalBounds().removeFromLeft(kLabelAreaWidth);

        // Flash color interpolation (D-10): text pulses green on BPM/BPI change
        juce::Colour textCol(JamWideLookAndFeel::kTextPrimary);
        if (flashStartMs_ > 0.0)
        {
            double now = juce::Time::getMillisecondCounterHiRes();
            double elapsed = now - flashStartMs_;
            if (elapsed < 2500.0)
            {
                float alpha = 1.0f - static_cast<float>(elapsed / 2500.0);
                auto flashCol = juce::Colour(JamWideLookAndFeel::kAccentConnect);
                textCol = flashCol.interpolatedWith(
                    juce::Colour(JamWideLookAndFeel::kTextPrimary), 1.0f - alpha);
            }
            else
            {
                flashStartMs_ = 0.0;
            }
        }

        // BPM/BPI: use a plain 13pt font. .withStyle("Bold") without a typeface
        // name falls back to a serif face on Windows (same JUCE quirk as the
        // status label), which makes the numbers look mismatched against the
        // sans-serif "Intervals:" / "Elapsed:" labels in the strip below.
        const auto numberFont = juce::FontOptions(13.0f);

        // BPM value (right-aligned in first 36px)
        auto bpmArea = labelArea.removeFromLeft(36);
        g.setColour(textCol);
        g.setFont(numberFont);
        g.drawText(juce::String(static_cast<int>(currentBpm_)), bpmArea,
                   juce::Justification::centredRight, false);

        // Separator "/"
        auto sepArea = labelArea.removeFromLeft(8);
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("/", sepArea, juce::Justification::centred, false);

        // BPI value (left-aligned in remaining ~28px)
        g.setColour(textCol);
        g.setFont(numberFont);
        g.drawText(juce::String(bpi_), labelArea, juce::Justification::centredLeft, false);
    }

    if (bpi_ <= 0) return;

    // --- Beat segments (shifted right by kLabelAreaWidth) ---
    // Width calculation uses (getWidth() - kLabelAreaWidth) instead of getWidth()
    // and all segment X positions offset by kLabelAreaWidth.
    const int beatAreaWidth = getWidth() - kLabelAreaWidth;
    const float segWidth = static_cast<float>(beatAreaWidth) / static_cast<float>(bpi_);
    const float h = static_cast<float>(getHeight());
    const auto green = juce::Colour(JamWideLookAndFeel::kAccentConnect);

    for (int i = 0; i < bpi_; ++i)
    {
        const float x = static_cast<float>(kLabelAreaWidth) + static_cast<float>(i) * segWidth;
        auto segBounds = juce::Rectangle<float>(x + 1.0f, 1.0f, segWidth - 2.0f, h - 2.0f);

        if (i < currentBeat_)
        {
            // Past beats: green at 35% opacity
            g.setColour(green.withAlpha(0.35f));
            g.fillRoundedRectangle(segBounds, 2.0f);
        }
        else if (i == currentBeat_)
        {
            // Current beat: full green
            g.setColour(green);
            g.fillRoundedRectangle(segBounds, 2.0f);

            // Beat number centered
            g.setColour(juce::Colour(JamWideLookAndFeel::kBgPrimary));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(juce::String(i + 1), segBounds.toNearestInt(),
                       juce::Justification::centred, false);
        }
        else
        {
            // Future beats: dark background
            g.setColour(juce::Colour(JamWideLookAndFeel::kVuUnlit));
            g.fillRoundedRectangle(segBounds, 2.0f);
        }

        // Downbeat accent (every 4 beats): 2px bar at top, green at 60%
        if (i % 4 == 0)
        {
            g.setColour(green.withAlpha(0.6f));
            g.fillRect(x + 1.0f, 0.0f, segWidth - 2.0f, 2.0f);
        }

        // BPI adaptation: numbering strategy
        // <=8: all numbered, 9-24: current + downbeats, 32+: group markers only
        if (i != currentBeat_)
        {
            bool showNumber = false;
            if (bpi_ <= 8)
                showNumber = true;
            else if (bpi_ <= 24)
                showNumber = (i % 4 == 0);
            else
                showNumber = (i % 8 == 0);

            if (showNumber)
            {
                g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary).withAlpha(0.6f));
                g.setFont(juce::FontOptions(9.0f));
                g.drawText(juce::String(i + 1), segBounds.toNearestInt(),
                           juce::Justification::centred, false);
            }
        }
    }
}

void BeatBar::mouseDown(const juce::MouseEvent& e)
{
    if (!processorRef_) return;

    int clickX = e.getPosition().getX();
    if (clickX > kLabelAreaWidth) return;  // Clicked on beat segments, ignore

    // Determine if BPM or BPI was clicked
    bool forBpm = (clickX < 36);  // BPM area is first 36px
    createVoteEditor(forBpm);
}

void BeatBar::createVoteEditor(bool forBpm)
{
    editingBpm_ = forBpm;
    int currentVal = forBpm ? static_cast<int>(currentBpm_) : bpi_;

    // Create TextEditor overlay
    voteEditor_ = std::make_unique<juce::TextEditor>();
    voteEditor_->setFont(juce::FontOptions(13.0f));
    voteEditor_->setColour(juce::TextEditor::backgroundColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    voteEditor_->setColour(juce::TextEditor::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    voteEditor_->setColour(juce::TextEditor::outlineColourId,
        juce::Colour(JamWideLookAndFeel::kBorderFocus));
    voteEditor_->setInputRestrictions(3, "0123456789");
    voteEditor_->setText(juce::String(currentVal), false);
    voteEditor_->selectAll();

    // Position over the clicked label area
    if (forBpm)
        voteEditor_->setBounds(0, 0, 36, getHeight());
    else
        voteEditor_->setBounds(44, 0, 28, getHeight());

    voteEditor_->onReturnKey = [this]()
    {
        int newVal = voteEditor_->getText().getIntValue();
        bool valid = editingBpm_
            ? (newVal >= 40 && newVal <= 400)
            : (newVal >= 2 && newVal <= 192);

        if (valid && processorRef_)
        {
            int existingVal = editingBpm_ ? static_cast<int>(currentBpm_) : bpi_;
            if (newVal != existingVal)
            {
                jamwide::SendChatCommand cmd;
                cmd.type = "MSG";
                cmd.text = editingBpm_
                    ? ("!vote bpm " + std::to_string(newVal))
                    : ("!vote bpi " + std::to_string(newVal));
                processorRef_->cmd_queue.try_push(std::move(cmd));
            }
        }
        // Defer destruction to avoid destroying TextEditor during its own callback
        juce::MessageManager::callAsync([this]() { dismissVoteEditor(); });
    };

    voteEditor_->onEscapeKey = [this]()
    {
        // Defer destruction (addresses Claude review: self-destruction during callback is UB)
        juce::MessageManager::callAsync([this]() { dismissVoteEditor(); });
    };

    // onFocusLost: Defer via MessageManager::callAsync to prevent destroying
    // the TextEditor during its own focusLost callback.
    // (Addresses Consensus #5: TextEditor self-destruction during focusLost)
    voteEditor_->onFocusLost = [this]()
    {
        juce::MessageManager::callAsync([this]() { dismissVoteEditor(); });
    };

    addAndMakeVisible(voteEditor_.get());
    voteEditor_->grabKeyboardFocus();
}

void BeatBar::dismissVoteEditor()
{
    voteEditor_.reset();
    if (auto* top = getTopLevelComponent())
        top->unfocusAllComponents();
}
