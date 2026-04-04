#pragma once
#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "core/njclient.h"  // For RemoteUserInfo
#include <array>
#include <vector>

class JamWideJuceProcessor;

class ChannelStripArea : public juce::Component,
                          private juce::Timer  // REVIEW FIX #7: centralized timer
{
public:
    explicit ChannelStripArea(JamWideJuceProcessor& processor);
    ~ChannelStripArea() override;

    void refreshFromUsers(const std::vector<NJClient::RemoteUserInfo>& users);
    void updateVuLevels();  // Sets target levels from atomics/cachedUsers
    void setDisconnectedState();
    void setConnectedState();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    std::function<void()> onBrowseClicked;
    std::function<void()> onLayoutChanged;  // strip count or visibility changed
    int getDesiredWidth() const;

    // Look up current user_index and channel_index from stable identity.
    // Returns {-1, -1} if user/channel no longer exists (e.g., user left).
    std::pair<int, int> findRemoteIndex(const juce::String& userName,
                                         const juce::String& channelName) const;

private:
    void timerCallback() override;  // 30Hz: tick all VU meters
    void rebuildStrips();

    JamWideJuceProcessor& processorRef;

    ChannelStrip localStrip;
    ChannelStrip masterStrip;
    std::vector<std::unique_ptr<ChannelStrip>> remoteStrips;

    // Local channel child strips (channels 1-3; channel 0 is the existing localStrip)
    std::vector<std::unique_ptr<ChannelStrip>> localChildStrips;  // up to 3 children
    bool localExpanded_ = false;

    // Metronome controls in master strip footer
    juce::Label metroLabel;         // "Metronome" label
    juce::Slider metroSlider;       // horizontal, 0.0-2.0, yellow fill
    juce::TextButton metroMuteBtn;  // "M" toggle

    // APVTS attachments for local channel params
    // REVIEW CONCERN ADDRESSED: explicit ownership -- destroyed before strips in destructor
    struct LocalChannelAttachments {
        std::unique_ptr<juce::ParameterAttachment> vol;   // VbFader uses ParameterAttachment
        std::unique_ptr<juce::SliderParameterAttachment> pan;
        std::unique_ptr<juce::ButtonParameterAttachment> mute;
    };
    std::array<LocalChannelAttachments, 4> localAttachments_;

    // APVTS attachments for metronome
    std::unique_ptr<juce::SliderParameterAttachment> metroSliderAttachment_;
    std::unique_ptr<juce::ButtonParameterAttachment> metroMuteAttachment_;

    juce::Viewport viewport;
    juce::Component stripContainer;

    juce::Label emptyStateLabel;
    juce::TextButton browseButton;
    bool isConnected = false;

    static constexpr int kStripWidth = 100;
    static constexpr int kStripPitch = 106;
    static constexpr int kMasterWidth = 110;
    static constexpr int kMaxVisibleStrips = 13;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripArea)
};
