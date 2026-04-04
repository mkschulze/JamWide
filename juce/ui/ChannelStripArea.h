#pragma once
#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "core/njclient.h"  // For RemoteUserInfo
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

    std::function<void()> onBrowseClicked;
    int getDesiredWidth() const;

private:
    void timerCallback() override;  // 30Hz: tick all VU meters
    void rebuildStrips();

    JamWideJuceProcessor& processorRef;

    ChannelStrip localStrip;
    ChannelStrip masterStrip;
    std::vector<std::unique_ptr<ChannelStrip>> remoteStrips;

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
