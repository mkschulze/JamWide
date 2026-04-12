#include "ChannelStrip.h"
#include "JamWideLookAndFeel.h"
#include "../midi/MidiMapper.h"
#include "../midi/MidiLearnManager.h"

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
    routingSelector.addItem("Main Mix", 1);
    for (int i = 0; i < 15; ++i)  // 15 remote buses (1-15), bus 16 reserved for metronome
        routingSelector.addItem("Remote " + juce::String(i + 1), i + 2);
    routingSelector.setSelectedId(1, juce::dontSendNotification);
    routingSelector.onChange = [this]()
    {
        int selectedId = routingSelector.getSelectedId();
        int busIndex = selectedId - 1;  // ID 1 = bus 0 (Main Mix), ID 2 = bus 1, etc.
        // Green text when routed to dedicated bus, normal when Main Mix
        if (busIndex > 0)
            routingSelector.setColour(juce::ComboBox::textColourId,
                juce::Colour(0xFF40E070));  // kAccentConnect
        else
            routingSelector.setColour(juce::ComboBox::textColourId,
                juce::Colour(0xFFDDDDEE));  // kTextPrimary
        if (onRoutingChanged)
            onRoutingChanged(busIndex);
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
            // Convert to NJClient srcch: mono channel start index with bit 10 set for stereo
            // Input 1-2 = 0 | (1<<10), Input 3-4 = 2 | (1<<10), etc.
            int srcch = ((selectedId - 1) * 2) | (1 << 10);
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

    // Register mouse listener on child controls for MIDI Learn right-click interception
    panSlider.addMouseListener(this, false);
    muteButton.addMouseListener(this, false);
    soloButton.addMouseListener(this, false);
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
            // D-16: Master footer is replaced by metronome controls (managed by ChannelStripArea).
            soloButton.setVisible(false);
            panSlider.setVisible(false);
            muteButton.setVisible(false);
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

void ChannelStrip::setRoutingBus(int busIndex)
{
    int selectorId = busIndex + 1;  // bus 0 = ID 1 (Main Mix), bus 1 = ID 2 (Remote 1), etc.
    routingSelector.setSelectedId(selectorId, juce::dontSendNotification);
    // Update text color: green for dedicated bus, normal for Main Mix
    if (busIndex > 0)
        routingSelector.setColour(juce::ComboBox::textColourId,
            juce::Colour(0xFF40E070));  // kAccentConnect
    else
        routingSelector.setColour(juce::ComboBox::textColourId,
            juce::Colour(0xFFDDDDEE));  // kTextPrimary
}

void ChannelStrip::paint(juce::Graphics& g)
{
    // Background fill per strip type
    juce::uint32 bgColour = JamWideLookAndFeel::kSurfaceStrip;
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
    int gap = 4;
    int faderW = 62;  // wider than thumb (44px) to fit dB scale labels
    int totalControlW = vuW + gap + faderW;
    int controlX = (center.getWidth() - totalControlW) / 2;

    auto vuBounds = center.withX(center.getX() + controlX).withWidth(vuW);
    vuMeter.setBounds(vuBounds.reduced(0, 2));

    auto faderBounds = center.withX(center.getX() + controlX + vuW + gap).withWidth(faderW);
    fader.setBounds(faderBounds.reduced(0, 2));
}

// MIDI Learn context menu interception for child controls (pan, mute, solo)
void ChannelStrip::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && midiMapper_ != nullptr && midiLearnMgr_ != nullptr)
    {
        juce::String paramId;
        if (e.eventComponent == &panSlider) paramId = panParamId_;
        else if (e.eventComponent == &muteButton) paramId = muteParamId_;
        else if (e.eventComponent == &soloButton) paramId = soloParamId_;

        if (paramId.isNotEmpty())
        {
            showMidiLearnMenu(paramId, e.eventComponent, e.getScreenPosition());
            return;
        }
    }
    juce::Component::mouseDown(e);
}

void ChannelStrip::showMidiLearnMenu(const juce::String& paramId, juce::Component* target,
                                      juce::Point<int> screenPos)
{
    juce::PopupMenu menu;
    bool hasMidi = midiMapper_->hasMapping(paramId);
    menu.addItem(1, "MIDI Learn");
    if (hasMidi) menu.addItem(2, "Clear MIDI");

    menu.showMenuAsync(juce::PopupMenu::Options()
        .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, paramId, target](int result) {
            if (result == 1)
            {
                // Visual feedback: green/mint border on target control
                learningComponent_ = target;
                if (auto* btn = dynamic_cast<juce::TextButton*>(target))
                    btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff40E070));
                else if (auto* slider = dynamic_cast<juce::Slider*>(target))
                    slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xff40E070));

                midiLearnMgr_->startLearning(paramId,
                    [this, paramId, target](int number, int ch, MidiMsgType type) {
                        midiMapper_->addMapping(paramId, number, ch, type);
                        // Restore appearance
                        if (auto* btn = dynamic_cast<juce::TextButton*>(target))
                            btn->setColour(juce::TextButton::buttonColourId,
                                           juce::Colour(0xff2A2D48u));  // kSurfaceStrip
                        else if (auto* slider = dynamic_cast<juce::Slider*>(target))
                            slider->setColour(juce::Slider::thumbColourId,
                                              juce::Colour(JamWideLookAndFeel::kTextPrimary));
                        learningComponent_ = nullptr;
                    });

                // Auto-cancel after 10s — only cancel global learn if still ours
                juce::Timer::callAfterDelay(10000, [this, paramId, target]() {
                    if (learningComponent_ == target)
                    {
                        if (midiLearnMgr_->getLearningParamId() == paramId)
                            midiLearnMgr_->cancelLearning();
                        if (auto* btn = dynamic_cast<juce::TextButton*>(target))
                            btn->setColour(juce::TextButton::buttonColourId,
                                           juce::Colour(0xff2A2D48u));
                        else if (auto* slider = dynamic_cast<juce::Slider*>(target))
                            slider->setColour(juce::Slider::thumbColourId,
                                              juce::Colour(JamWideLookAndFeel::kTextPrimary));
                        learningComponent_ = nullptr;
                    }
                });
            }
            else if (result == 2)
                midiMapper_->removeMapping(paramId);
        });
}

void ChannelStrip::setMidiLearnContext(MidiMapper* mapper, MidiLearnManager* learnMgr,
                                        const juce::String& volParamId,
                                        const juce::String& panParamId,
                                        const juce::String& muteParamId,
                                        const juce::String& soloParamId)
{
    midiMapper_ = mapper;
    midiLearnMgr_ = learnMgr;
    volParamId_ = volParamId;
    panParamId_ = panParamId;
    muteParamId_ = muteParamId;
    soloParamId_ = soloParamId;

    // Forward MIDI Learn context to the VbFader for volume control
    fader.setMidiLearnContext(mapper, learnMgr, volParamId);

    // Poll for externally-initiated learning (e.g. from MIDI config dialog)
    if (learnMgr != nullptr)
        startTimerHz(4);
}

void ChannelStrip::timerCallback()
{
    if (midiLearnMgr_ == nullptr || learningComponent_ != nullptr)
        return;  // local learn in progress, don't interfere

    bool isLearning = midiLearnMgr_->isLearning();
    juce::Component* target = nullptr;

    if (isLearning)
    {
        auto learnParam = midiLearnMgr_->getLearningParamId();
        if (learnParam == panParamId_)        target = &panSlider;
        else if (learnParam == muteParamId_)  target = &muteButton;
        else if (learnParam == soloParamId_)  target = &soloButton;
    }

    if (target != nullptr && externalLearnTarget_ == nullptr)
    {
        // Color the target green to indicate waiting for MIDI
        externalLearnTarget_ = target;
        if (auto* btn = dynamic_cast<juce::TextButton*>(target))
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff40E070));
        else if (auto* slider = dynamic_cast<juce::Slider*>(target))
            slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xff40E070));
    }
    else if (target == nullptr && externalLearnTarget_ != nullptr)
    {
        // Learning ended — restore original colors
        if (auto* btn = dynamic_cast<juce::TextButton*>(externalLearnTarget_))
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2A2D48u));
        else if (auto* slider = dynamic_cast<juce::Slider*>(externalLearnTarget_))
            slider->setColour(juce::Slider::thumbColourId,
                              juce::Colour(JamWideLookAndFeel::kTextPrimary));
        externalLearnTarget_ = nullptr;
    }
}

// Pass all scroll events through to parent (viewport).
// Faders are not scroll-wheel controlled per user preference.
void ChannelStrip::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel)
{
    Component::mouseWheelMove(e, wheel);
}
