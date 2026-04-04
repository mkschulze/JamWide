#include "ChannelStrip.h"
#include "JamWideLookAndFeel.h"

ChannelStrip::ChannelStrip()
{
    // Name label
    addAndMakeVisible(nameLabel);
    nameLabel.setFont(juce::FontOptions(11.0f));
    nameLabel.setColour(juce::Label::textColourId,
                        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    nameLabel.setJustificationType(juce::Justification::centred);

    // Codec label
    addAndMakeVisible(codecLabel);
    codecLabel.setFont(juce::FontOptions(11.0f));
    codecLabel.setColour(juce::Label::textColourId,
                         juce::Colour(JamWideLookAndFeel::kAccentConnect));
    codecLabel.setJustificationType(juce::Justification::centred);

    // Expand button (for multi-channel remote users)
    addChildComponent(expandButton);
    expandButton.setButtonText("+");
    expandButton.onClick = [this]()
    {
        expanded_ = !expanded_;
        expandButton.setButtonText(expanded_ ? "-" : "+");
        if (onExpandToggled)
            onExpandToggled();
    };

    // Routing selector
    addChildComponent(routingSelector);
    for (int i = 0; i < 16; ++i)
        routingSelector.addItem("Out " + juce::String(i + 1) + "/" + juce::String(i + 2), i + 1);
    routingSelector.setSelectedId(1, juce::dontSendNotification);
    routingSelector.onChange = [this]()
    {
        if (onRoutingChanged)
            onRoutingChanged(routingSelector.getSelectedId() - 1);
    };

    // Subscribe / Transmit toggle button
    addChildComponent(subTxButton);
    subTxButton.setClickingTogglesState(true);
    subTxButton.onClick = [this]()
    {
        bool toggled = subTxButton.getToggleState();
        if (stripType == StripType::Local)
        {
            if (onTransmitToggled)
                onTransmitToggled(toggled);
        }
        else
        {
            if (onSubscribeToggled)
                onSubscribeToggled(toggled);
        }
    };

    // VU Meter
    addAndMakeVisible(vuMeter);

    // VbFader
    addAndMakeVisible(fader);
    fader.onValueChanged = [this](float val) {
        if (onVolumeChanged)
            onVolumeChanged(val);
    };

    // Pan slider (horizontal, -1.0 to 1.0)
    addAndMakeVisible(panSlider);
    panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0, juce::dontSendNotification);
    panSlider.setDoubleClickReturnValue(true, 0.0);  // D-04: double-click resets to center
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    panSlider.setColour(juce::Slider::thumbColourId,
                        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    panSlider.onValueChange = [this]() {
        if (onPanChanged)
            onPanChanged(static_cast<float>(panSlider.getValue()));
    };

    // Mute button per D-09
    addAndMakeVisible(muteButton);
    muteButton.setButtonText("M");
    muteButton.setClickingTogglesState(true);
    muteButton.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    muteButton.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colour(JamWideLookAndFeel::kAccentDestructive));  // 0xffE04040
    muteButton.setColour(juce::TextButton::textColourOffId,
                         juce::Colour(JamWideLookAndFeel::kTextSecondary));
    muteButton.setColour(juce::TextButton::textColourOnId,
                         juce::Colour(JamWideLookAndFeel::kTextPrimary));
    muteButton.onClick = [this]() {
        if (onMuteToggled)
            onMuteToggled(muteButton.getToggleState());
    };

    // Input bus selector (hidden by default, shown for Local child strips)
    addChildComponent(inputBusSelector);
    inputBusSelector.addItem("Input 1-2", 1);
    inputBusSelector.addItem("Input 3-4", 2);
    inputBusSelector.addItem("Input 5-6", 3);
    inputBusSelector.addItem("Input 7-8", 4);
    inputBusSelector.setSelectedId(1, juce::dontSendNotification);
    inputBusSelector.onChange = [this]() {
        if (onInputBusChanged)
        {
            int selectedId = inputBusSelector.getSelectedId();
            // Convert to NJClient srcch: stereo pair index with bit 10 set for stereo
            // Input 1-2 = 0 | (1<<10), Input 3-4 = 1 | (1<<10), etc.
            int srcch = (selectedId - 1) | (1 << 10);
            onInputBusChanged(srcch);
        }
    };

    // Solo button per D-09
    addAndMakeVisible(soloButton);
    soloButton.setButtonText("S");
    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    soloButton.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colour(JamWideLookAndFeel::kAccentWarning));  // 0xffCCB833
    soloButton.setColour(juce::TextButton::textColourOffId,
                         juce::Colour(JamWideLookAndFeel::kTextSecondary));
    soloButton.setColour(juce::TextButton::textColourOnId,
                         juce::Colour(0xff000000));  // Black text on yellow
    soloButton.onClick = [this]() {
        if (onSoloToggled)
            onSoloToggled(soloButton.getToggleState());
    };
}

void ChannelStrip::configure(StripType type, const juce::String& name,
                             const juce::String& codecStr,
                             int channelCount, bool expanded)
{
    stripType = type;
    channelCount_ = channelCount;
    expanded_ = expanded;

    nameLabel.setText(name, juce::dontSendNotification);
    codecLabel.setText(codecStr, juce::dontSendNotification);
    codecLabel.setVisible(codecStr.isNotEmpty());

    // Configure visibility based on strip type
    switch (type)
    {
        case StripType::Local:
            routingSelector.setVisible(false);
            subTxButton.setVisible(true);
            subTxButton.setButtonText("TX");
            subTxButton.setToggleState(true, juce::dontSendNotification);
            expandButton.setVisible(channelCount > 1);
            expandButton.setButtonText(expanded ? "-" : "+");
            break;

        case StripType::Remote:
            routingSelector.setVisible(true);
            subTxButton.setVisible(true);
            subTxButton.setButtonText("Sub");
            subTxButton.setToggleState(true, juce::dontSendNotification);
            expandButton.setVisible(channelCount > 1);
            expandButton.setButtonText(expanded ? "-" : "+");
            break;

        case StripType::RemoteChild:
            routingSelector.setVisible(true);
            subTxButton.setVisible(true);
            subTxButton.setButtonText("Sub");
            subTxButton.setToggleState(true, juce::dontSendNotification);
            expandButton.setVisible(false);
            break;

        case StripType::Master:
            routingSelector.setVisible(false);
            subTxButton.setVisible(false);
            expandButton.setVisible(false);
            // D-11: Master strip has no solo. Also no pan (master outputs to main mix stereo).
            soloButton.setVisible(false);
            panSlider.setVisible(false);
            break;
    }

    resized();
    repaint();
}

void ChannelStrip::setVuLevels(float left, float right)
{
    vuMeter.setLevels(left, right);
}

void ChannelStrip::tickVu()
{
    vuMeter.tick();
}

void ChannelStrip::setSubscribed(bool sub)
{
    subTxButton.setToggleState(sub, juce::dontSendNotification);
}

void ChannelStrip::setTransmitting(bool tx)
{
    subTxButton.setToggleState(tx, juce::dontSendNotification);
}

// Mixer control setters (update display without triggering callbacks)
void ChannelStrip::setVolume(float vol) { fader.setValue(vol); }
void ChannelStrip::setPan(float pan) { panSlider.setValue(pan, juce::dontSendNotification); }
void ChannelStrip::setMuted(bool m) { muteButton.setToggleState(m, juce::dontSendNotification); }
void ChannelStrip::setSoloed(bool s) { soloButton.setToggleState(s, juce::dontSendNotification); }

// Component access for APVTS attachment in Plan 03
VbFader& ChannelStrip::getFader() { return fader; }
juce::Slider& ChannelStrip::getPanSlider() { return panSlider; }
juce::TextButton& ChannelStrip::getMuteButton() { return muteButton; }
juce::TextButton& ChannelStrip::getSoloButton() { return soloButton; }
juce::ComboBox& ChannelStrip::getInputBusSelector() { return inputBusSelector; }
void ChannelStrip::setInputBus(int busIndex) {
    inputBusSelector.setSelectedId(busIndex + 1, juce::dontSendNotification);
}

void ChannelStrip::paint(juce::Graphics& g)
{
    // Background fill per strip type
    juce::uint32 bgColour;
    switch (stripType)
    {
        case StripType::Master:      bgColour = JamWideLookAndFeel::kBgElevated; break;
        case StripType::RemoteChild: bgColour = JamWideLookAndFeel::kSurfaceChild; break;
        case StripType::Local:
        case StripType::Remote:      bgColour = JamWideLookAndFeel::kSurfaceStrip; break;
    }
    g.fillAll(juce::Colour(bgColour));

    // 1px right border
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds();

    // Header zone (top 66px)
    auto header = area.removeFromTop(kHeaderHeight);
    auto nameArea = header.removeFromTop(18);
    nameLabel.setBounds(nameArea.reduced(2, 0));

    auto codecArea = header.removeFromTop(16);
    codecLabel.setBounds(codecArea.reduced(2, 0));

    // Input bus selector (if visible)
    if (inputBusSelector.isVisible())
    {
        auto selectorRow = header.removeFromTop(14);
        inputBusSelector.setBounds(selectorRow.reduced(4, 0));
    }

    // Expand button in header (if visible)
    if (expandButton.isVisible())
    {
        auto expandArea = header.removeFromTop(16);
        expandButton.setBounds(expandArea.reduced(2, 0));
    }

    // Routing selector (if visible)
    if (routingSelector.isVisible())
    {
        auto routeArea = header.removeFromTop(18);
        routingSelector.setBounds(routeArea.reduced(2, 0));
    }

    // Sub/TX button (rest of header)
    if (subTxButton.isVisible())
    {
        auto subArea = header.removeFromTop(18).reduced(2, 0);
        if (subArea.getHeight() > 0)
            subTxButton.setBounds(subArea);
    }

    // Footer zone (bottom 38px) per UI-SPEC footer layout
    auto footer = area.removeFromBottom(kFooterHeight);
    {
        // Pan row: 16px tall, 4px left/right margin
        auto panRow = footer.removeFromTop(16);
        panSlider.setBounds(panRow.reduced(4, 0));

        // 2px gap
        footer.removeFromTop(2);

        // Button row: 16px tall
        auto btnRow = footer.removeFromTop(16);
        auto btnArea = btnRow.reduced(4, 0);
        int btnW = (btnArea.getWidth() - 2) / 2;  // 2px gap between buttons
        muteButton.setBounds(btnArea.removeFromLeft(btnW));
        btnArea.removeFromLeft(2);  // gap
        soloButton.setBounds(btnArea);

        // remaining 4px is bottom padding
    }

    // Center zone: VU meter + fader side-by-side per D-10
    auto center = area;  // remaining area after header and footer
    int vuW = 24;
    int gap = 6;
    int faderW = VbFader::kThumbDiameter;  // 44px to accommodate the thumb
    int totalControlW = vuW + gap + faderW;
    int controlX = (center.getWidth() - totalControlW) / 2;

    auto vuBounds = center.withX(center.getX() + controlX).withWidth(vuW);
    vuMeter.setBounds(vuBounds.reduced(0, 2));

    auto faderBounds = center.withX(center.getX() + controlX + vuW + gap).withWidth(faderW);
    fader.setBounds(faderBounds.reduced(0, 2));
}

// D-02: scroll anywhere on strip adjusts fader
// REVIEW CONCERN ADDRESSED: consume vertical scroll to prevent viewport conflict.
void ChannelStrip::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel)
{
    // Only vertical wheel adjusts fader. Horizontal wheel is NOT consumed
    // and propagates to the Viewport for horizontal scrolling of the strip area.
    if (std::abs(wheel.deltaY) > 0.0f)
    {
        fader.adjustByDb(wheel.deltaY > 0 ? 0.5f : -0.5f);
        // Do NOT call Component::mouseWheelMove -- consume vertical scroll event
    }
    else
    {
        // Horizontal scroll: let it propagate to viewport
        Component::mouseWheelMove(e, wheel);
    }
}
