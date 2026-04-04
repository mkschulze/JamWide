#pragma once
#include <JuceHeader.h>
#include "VuMeter.h"
#include "VbFader.h"
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

    // Mixer control setters (update display, no notification)
    void setVolume(float vol);
    void setPan(float pan);
    void setMuted(bool m);
    void setSoloed(bool s);

    // Component access for APVTS attachment in Plan 03
    VbFader& getFader();
    juce::Slider& getPanSlider();
    juce::TextButton& getMuteButton();
    juce::TextButton& getSoloButton();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override;

    // Existing callbacks
    std::function<void(int)> onRoutingChanged;
    std::function<void(bool)> onSubscribeToggled;
    std::function<void(bool)> onTransmitToggled;
    std::function<void()> onExpandToggled;

    // Mixer control callbacks
    std::function<void(float)> onVolumeChanged;   // 0.0-2.0 linear
    std::function<void(float)> onPanChanged;       // -1.0 to 1.0
    std::function<void(bool)>  onMuteToggled;      // true = muted
    std::function<void(bool)>  onSoloToggled;      // true = soloed
    std::function<void(int)>   onInputBusChanged;  // srcch value for NJClient

    // Input bus selector access
    juce::ComboBox& getInputBusSelector();
    void setInputBus(int busIndex);  // Update display (for state restore). 0-based pair index.
    void setRoutingBus(int busIndex);  // Update routing selector display without firing callback

    StripType getType() const { return stripType; }
    bool isExpanded() const { return expanded_; }
    int getChannelCount() const { return channelCount_; }

private:
    StripType stripType = StripType::Remote;
    juce::Label nameLabel;
    juce::Label codecLabel;
    juce::TextButton expandButton;
    juce::ComboBox routingSelector;
    juce::TextButton subTxButton;
    VuMeter vuMeter;
    VbFader fader;
    juce::Slider panSlider;
    juce::TextButton muteButton;
    juce::TextButton soloButton;
    juce::ComboBox inputBusSelector;
    bool expanded_ = false;
    int channelCount_ = 1;

    static constexpr int kStripWidth = 100;
    static constexpr int kHeaderHeight = 66;
    static constexpr int kFooterHeight = 38;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};
