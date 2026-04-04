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
            expandButton.setVisible(false);
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

    // Footer zone (bottom 38px) -- placeholder for Phase 5 (pan + mute/solo)
    auto footer = area.removeFromBottom(kFooterHeight);
    (void)footer; // Phase 5: pan knob, mute/solo buttons

    // VU meter fills the remaining center area
    vuMeter.setBounds(area.reduced(4, 2));
}
