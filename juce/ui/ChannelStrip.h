#pragma once
#include <JuceHeader.h>
#include "VuMeter.h"
#include <functional>

class ChannelStrip : public juce::Component
{
public:
    enum class StripType { Local, Remote, RemoteChild, Master };

    ChannelStrip();

    void configure(StripType type, const juce::String& name,
                   const juce::String& codecStr = {},
                   int channelCount = 1, bool expanded = false);

    void setVuLevels(float left, float right);
    void tickVu();  // Delegates to vuMeter.tick()
    void setSubscribed(bool sub);
    void setTransmitting(bool tx);

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(int)> onRoutingChanged;
    std::function<void(bool)> onSubscribeToggled;
    std::function<void(bool)> onTransmitToggled;
    std::function<void()> onExpandToggled;

    StripType getType() const { return stripType; }
    bool isExpanded() const { return expanded_; }

private:
    StripType stripType = StripType::Remote;
    juce::Label nameLabel;
    juce::Label codecLabel;
    juce::TextButton expandButton;
    juce::ComboBox routingSelector;
    juce::TextButton subTxButton;
    VuMeter vuMeter;
    bool expanded_ = false;
    int channelCount_ = 1;

    static constexpr int kStripWidth = 100;
    static constexpr int kHeaderHeight = 66;
    static constexpr int kFooterHeight = 38;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};
