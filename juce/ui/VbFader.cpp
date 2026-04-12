#include "VbFader.h"
#include "JamWideLookAndFeel.h"
#include "../midi/MidiMapper.h"
#include "../midi/MidiLearnManager.h"

//==============================================================================
VbFader::VbFader()
{
    setRepaintsOnMouseActivity(false);
}

VbFader::~VbFader()
{
    // Ensure attachment is destroyed before component
    attachment_.reset();
}

//==============================================================================
juce::String VbFader::formatDb(float linear)
{
    if (linear < 0.001f)
        return "-inf";

    float db = 20.0f * std::log10(linear);
    db = juce::jmin(db, 6.0f);

    if (db >= 0.0f)
        return "+" + juce::String(db, 1);

    return juce::String(db, 1);
}

//==============================================================================
float VbFader::valueToY(float val) const
{
    float norm = val / kMaxLinear; // 0.0 to 1.0
    float curveNorm = std::pow(norm, 1.0f / 2.5f); // inverse power curve for better dB resolution

    float topPad = kThumbDiameter / 2.0f;
    float botPad = kThumbDiameter / 2.0f;
    float travel = static_cast<float>(getHeight()) - topPad - botPad;

    return topPad + travel * (1.0f - curveNorm); // top = max, bottom = min
}

float VbFader::yToValue(int y) const
{
    float topPad = kThumbDiameter / 2.0f;
    float botPad = kThumbDiameter / 2.0f;
    float travel = static_cast<float>(getHeight()) - topPad - botPad;

    float curveNorm = 1.0f - (static_cast<float>(y) - topPad) / travel;
    curveNorm = juce::jlimit(0.0f, 1.0f, curveNorm);

    float norm = std::pow(curveNorm, 2.5f); // apply power curve
    return juce::jlimit(kMinLinear, kMaxLinear, norm * kMaxLinear);
}

//==============================================================================
void VbFader::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float trackX = (w - static_cast<float>(kTrackWidth)) / 2.0f;
    const float thumbY = valueToY(value_);

    // 1. Track background
    g.setColour(juce::Colour(JamWideLookAndFeel::kVuBackground)); // 0xff0A0D18
    g.fillRoundedRectangle(trackX, kThumbDiameter / 2.0f,
                           static_cast<float>(kTrackWidth),
                           h - static_cast<float>(kThumbDiameter),
                           2.0f);

    // 2. Green fill from bottom of track up to thumb position
    const float trackBottom = h - kThumbDiameter / 2.0f;
    if (thumbY < trackBottom)
    {
        g.setColour(juce::Colour(0xff40E070u)); // kAccentConnect
        g.fillRoundedRectangle(trackX, thumbY,
                               static_cast<float>(kTrackWidth),
                               trackBottom - thumbY,
                               2.0f);
    }

    // 3. dB scale ticks with labels to the left of the track
    {
        g.setFont(juce::FontOptions(8.0f));

        struct TickMark { float linear; const char* label; };
        const TickMark ticks[] = {
            { 2.0f,    "+6"  },
            { 1.0f,    "0"   },
            { 0.5012f, "-6"  },
            { 0.1259f, "-18" },
            { 0.0f,    "-inf"}
        };

        for (const auto& tick : ticks)
        {
            float tickY = valueToY(tick.linear);

            // Tick mark on track
            g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary).withAlpha(0.5f));
            g.drawHorizontalLine(static_cast<int>(tickY),
                                 trackX - 2.0f, trackX);
            g.drawHorizontalLine(static_cast<int>(tickY),
                                 trackX + static_cast<float>(kTrackWidth),
                                 trackX + static_cast<float>(kTrackWidth) + 2.0f);

            // Label to the left of the track
            g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary).withAlpha(0.7f));
            g.drawText(tick.label,
                       0, static_cast<int>(tickY - 5),
                       static_cast<int>(trackX - 3), 10,
                       juce::Justification::centredRight, false);
        }
    }

    // 4. Thumb: 44px diameter circle
    {
        const float thumbX = w / 2.0f - kThumbDiameter / 2.0f;
        const float thumbTop = thumbY - kThumbDiameter / 2.0f;

        // Fill
        g.setColour(juce::Colour(0xff2A2D48u)); // kSurfaceStrip
        g.fillEllipse(thumbX, thumbTop,
                      static_cast<float>(kThumbDiameter),
                      static_cast<float>(kThumbDiameter));

        // Border
        g.setColour(juce::Colour(0xff40E070u)); // kAccentConnect
        g.drawEllipse(thumbX + 1.0f, thumbTop + 1.0f,
                      static_cast<float>(kThumbDiameter) - 2.0f,
                      static_cast<float>(kThumbDiameter) - 2.0f,
                      2.0f);
    }

    // 5. dB text ON thumb
    {
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextPrimary)); // 0xffE0E0E0
        g.setFont(juce::Font(11.0f).boldened());

        juce::String dbText = formatDb(value_);
        g.drawText(dbText,
                   0, static_cast<int>(thumbY - kThumbDiameter / 2.0f),
                   static_cast<int>(w), kThumbDiameter,
                   juce::Justification::centred, false);
    }

    // 6. MIDI Learn visual feedback (D-02, UI-SPEC section 3)
    bool externalLearning = (!midiLearning_ && midiLearnMgr_ != nullptr
        && midiLearnMgr_->isLearning()
        && midiLearnMgr_->getLearningParamId() == midiParamId_);
    if (midiLearning_ || externalLearning)
    {
        // Pulsing kAccentWarning border (UI-SPEC: 2px, #CCB833, opacity 0.6-1.0 over 600ms)
        float phase = std::fmod(static_cast<float>(juce::Time::getMillisecondCounterHiRes() / 600.0), 1.0f);
        float alpha = 0.6f + 0.4f * std::abs(std::sin(phase * juce::MathConstants<float>::pi));
        g.setColour(juce::Colour(0xff40E070).withAlpha(alpha));
        g.drawRect(getLocalBounds().toFloat(), 2.0f);

        // Overlay text: "MIDI?" (green/mint to avoid solo-yellow conflict)
        g.setColour(juce::Colour(0xff40E070));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("MIDI?", getLocalBounds().removeFromTop(16),
                   juce::Justification::centred, false);
    }
    else if (learnedNumber_ >= 0)
    {
        // Learned state: solid kAccentConnect border + type-aware text
        g.setColour(juce::Colour(0xff40E070));
        g.drawRect(getLocalBounds().toFloat(), 2.0f);
        g.setFont(juce::FontOptions(10.0f));
        juce::String prefix = (learnedType_ == MidiMsgType::Note) ? "N" : "CC";
        g.drawText(prefix + juce::String(learnedNumber_) + " Ch" + juce::String(learnedCh_),
                   getLocalBounds().removeFromTop(16),
                   juce::Justification::centred, false);
    }
    else if (midiMapper_ != nullptr && midiParamId_.isNotEmpty() && midiMapper_->hasMapping(midiParamId_))
    {
        // Subtle indicator that this control has a MIDI mapping (thin border)
        g.setColour(juce::Colour(0xff40E070).withAlpha(0.25f));
        g.drawRect(getLocalBounds().toFloat(), 1.0f);
    }
}

//==============================================================================
void VbFader::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        // MIDI Learn context menu (D-01)
        if (midiMapper_ != nullptr && midiLearnMgr_ != nullptr && midiParamId_.isNotEmpty())
        {
            juce::PopupMenu menu;
            bool hasMidiMapping = midiMapper_->hasMapping(midiParamId_);

            menu.addItem(1, "MIDI Learn");
            if (hasMidiMapping)
                menu.addItem(2, "Clear MIDI");

            auto mouseScreenPos = e.getScreenPosition();
        menu.showMenuAsync(juce::PopupMenu::Options()
                .withTargetScreenArea(juce::Rectangle<int>(mouseScreenPos.x, mouseScreenPos.y, 1, 1)),
                [this](int result) {
                    if (result == 1)
                    {
                        // Start MIDI Learn (D-02: visual feedback)
                        midiLearning_ = true;
                        learnedNumber_ = -1;
                        learnedCh_ = -1;
                        repaint();
                        midiLearnMgr_->startLearning(midiParamId_,
                            [this](int number, int ch, MidiMsgType type) {
                                // Learned callback -- add mapping
                                midiMapper_->addMapping(midiParamId_, number, ch, type);
                                midiLearning_ = false;
                                learnedNumber_ = number;
                                learnedCh_ = ch;
                                learnedType_ = type;
                                learnCompletedTime_ = juce::Time::currentTimeMillis();
                                repaint();
                                // Auto-dismiss learned indicator after 1200ms (per UI-SPEC)
                                juce::Timer::callAfterDelay(1200, [this]() {
                                    learnedNumber_ = -1;
                                    learnedCh_ = -1;
                                    repaint();
                                });
                            });
                        // Auto-cancel learn after 10 seconds (per UI-SPEC)
                        // Only cancel global learn if it still belongs to this fader's param
                        // (prevents race when user starts a different learn elsewhere)
                        juce::Timer::callAfterDelay(10000, [this]() {
                            if (midiLearning_)
                            {
                                if (midiLearnMgr_ != nullptr
                                    && midiLearnMgr_->getLearningParamId() == midiParamId_)
                                    midiLearnMgr_->cancelLearning();
                                midiLearning_ = false;
                                repaint();
                            }
                        });
                    }
                    else if (result == 2)
                    {
                        midiMapper_->removeMapping(midiParamId_);
                        repaint();
                    }
                });
            return;
        }

        // Fallback: forward to top-level for scale menu (existing behavior for non-MIDI-context faders)
        if (auto* top = getTopLevelComponent())
            top->mouseDown(e.getEventRelativeTo(top));
        return;
    }

    dragStartY_ = e.y;
    dragStartValue_ = value_;
    gestureActive_ = true;

    if (attachment_)
        attachment_->beginGesture();
}

void VbFader::mouseDrag(const juce::MouseEvent& e)
{
    float newVal = yToValue(e.y);

    // Snap near-zero
    if (newVal < 0.001f)
        newVal = 0.0f;

    setValue(newVal);
}

void VbFader::mouseUp(const juce::MouseEvent& /*e*/)
{
    gestureActive_ = false;

    if (attachment_)
        attachment_->endGesture();
}

void VbFader::mouseDoubleClick(const juce::MouseEvent& /*e*/)
{
    setValue(defaultValue_);
}

void VbFader::mouseWheelMove(const juce::MouseEvent& e,
                              const juce::MouseWheelDetails& wheel)
{
    // Pass scroll events through to parent (viewport) -- faders are not
    // scroll-wheel controlled per user preference.
    Component::mouseWheelMove(e, wheel);
}

//==============================================================================
void VbFader::adjustByDb(float dbDelta)
{
    float currentDb = (value_ <= 0.0001f) ? -100.0f
                                           : 20.0f * std::log10(value_);
    currentDb += dbDelta;
    currentDb = juce::jlimit(-100.0f, 6.0f, currentDb);

    float newLinear = (currentDb <= -100.0f) ? 0.0f
                                              : std::pow(10.0f, currentDb / 20.0f);

    setValue(juce::jlimit(kMinLinear, kMaxLinear, newLinear));
}

//==============================================================================
void VbFader::setValue(float newValue)
{
    newValue = juce::jlimit(kMinLinear, kMaxLinear, newValue);

    if (gestureActive_ && attachment_)
        attachment_->setValueAsPartOfGesture(newValue);
    else if (attachment_)
        attachment_->setValueAsCompleteGesture(newValue);

    value_ = newValue;

    if (onValueChanged)
        onValueChanged(value_);

    repaint();
}

//==============================================================================
void VbFader::attachToParameter(juce::RangedAudioParameter& param,
                                 juce::UndoManager* um)
{
    attachment_ = std::make_unique<juce::ParameterAttachment>(
        param,
        [this](float v) { value_ = v; repaint(); },
        um);

    attachment_->sendInitialUpdate();
}

void VbFader::detachFromParameter()
{
    attachment_.reset();
}

//==============================================================================
void VbFader::setMidiLearnContext(MidiMapper* mapper, MidiLearnManager* learnMgr,
                                   const juce::String& paramId)
{
    midiMapper_ = mapper;
    midiLearnMgr_ = learnMgr;
    midiParamId_ = paramId;

    // Low-frequency polling to detect externally-initiated MIDI learn
    if (learnMgr != nullptr)
        startTimerHz(4);
}

void VbFader::timerCallback()
{
    bool externalLearning = (!midiLearning_ && midiLearnMgr_ != nullptr
        && midiLearnMgr_->isLearning()
        && midiLearnMgr_->getLearningParamId() == midiParamId_);

    if (midiLearning_ || externalLearning)
    {
        // Bump to 30fps for smooth pulsing animation
        if (getTimerInterval() > 40)
            startTimerHz(30);
        repaint();
    }
    else if (getTimerInterval() < 40)
    {
        // Drop back to low-frequency polling
        startTimerHz(4);
        repaint(); // one final repaint to clear the indication
    }
}
