#include "VbFader.h"
#include "JamWideLookAndFeel.h"

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

    // 3. dB scale ticks to the right of track
    {
        const float tickX = trackX + static_cast<float>(kTrackWidth) + 4.0f;
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary)); // 0xff8888AA
        g.setFont(juce::Font(9.0f));

        struct TickMark { float linear; const char* label; };
        const TickMark ticks[] = {
            { 2.0f,    "+6"   },
            { 1.0f,    "0"    },
            { 0.5012f, "-6"   },
            { 0.1259f, "-18"  },
            { 0.0f,    "-inf" }
        };

        for (const auto& tick : ticks)
        {
            float tickY = valueToY(tick.linear);
            // 1px horizontal tick mark
            g.drawHorizontalLine(static_cast<int>(tickY), tickX, tickX + 4.0f);
            // Label
            g.drawText(tick.label,
                       static_cast<int>(tickX + 6.0f),
                       static_cast<int>(tickY - 5.0f),
                       30, 10,
                       juce::Justification::centredLeft, false);
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
}

//==============================================================================
void VbFader::mouseDown(const juce::MouseEvent& e)
{
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

void VbFader::mouseWheelMove(const juce::MouseEvent& /*e*/,
                              const juce::MouseWheelDetails& wheel)
{
    // Convert current value to dB
    float currentDb = (value_ <= 0.0001f) ? -100.0f
                                           : 20.0f * std::log10(value_);

    // 0.5 dB per scroll step
    currentDb += (wheel.deltaY > 0.0f) ? 0.5f : -0.5f;

    // Clamp dB range
    currentDb = juce::jlimit(-100.0f, 6.0f, currentDb);

    // Convert back to linear
    float newLinear = (currentDb <= -100.0f) ? 0.0f
                                              : std::pow(10.0f, currentDb / 20.0f);

    setValue(juce::jlimit(kMinLinear, kMaxLinear, newLinear));

    // Consume the event -- do NOT call Component::mouseWheelMove()
    // to prevent viewport scrolling conflict
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
