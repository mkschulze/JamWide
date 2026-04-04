#include "ChannelStripArea.h"
#include "JamWideLookAndFeel.h"
#include "../JamWideJuceProcessor.h"
#include "threading/ui_command.h"

ChannelStripArea::ChannelStripArea(JamWideJuceProcessor& processor)
    : processorRef(processor)
{
    // Configure local strip
    localStrip.configure(ChannelStrip::StripType::Local, "Local");

    // Configure master strip
    masterStrip.configure(ChannelStrip::StripType::Master, "Master");

    // Set up viewport with strip container
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&stripContainer, false);
    viewport.setScrollBarsShown(false, true); // horizontal scroll only

    // Empty state label (D-28)
    addChildComponent(emptyStateLabel);
    emptyStateLabel.setText("Connect to a server to start jamming",
                            juce::dontSendNotification);
    emptyStateLabel.setFont(juce::FontOptions(16.0f));
    emptyStateLabel.setColour(juce::Label::textColourId,
                              juce::Colour(JamWideLookAndFeel::kTextSecondary));
    emptyStateLabel.setJustificationType(juce::Justification::centred);

    // Browse Servers button (D-29)
    addChildComponent(browseButton);
    browseButton.setButtonText("Browse Servers");
    browseButton.setColour(juce::TextButton::textColourOffId,
                           juce::Colour(JamWideLookAndFeel::kAccentConnect));
    browseButton.setColour(juce::TextButton::textColourOnId,
                           juce::Colour(JamWideLookAndFeel::kAccentConnect));
    browseButton.onClick = [this]()
    {
        if (onBrowseClicked)
            onBrowseClicked();
    };

    // Add master strip (outside viewport, pinned right)
    addAndMakeVisible(masterStrip);

    // Start centralized 30Hz timer (REVIEW FIX #7)
    startTimerHz(30);

    // Start in disconnected state
    setDisconnectedState();
}

ChannelStripArea::~ChannelStripArea()
{
    stopTimer();
}

// REVIEW FIX #7: single centralized timer drives all VU meters
void ChannelStripArea::timerCallback()
{
    if (!isConnected) return;

    // Update VU target levels from processor data
    updateVuLevels();

    // Tick all VU meters (apply ballistics + repaint)
    localStrip.tickVu();
    masterStrip.tickVu();
    for (auto& strip : remoteStrips)
        strip->tickVu();
}

void ChannelStripArea::updateVuLevels()
{
    // Local VU from atomic snapshot
    localStrip.setVuLevels(
        processorRef.uiSnapshot.local_vu_left.load(std::memory_order_relaxed),
        processorRef.uiSnapshot.local_vu_right.load(std::memory_order_relaxed));

    // Master VU from atomic snapshot
    masterStrip.setVuLevels(
        processorRef.uiSnapshot.master_vu_left.load(std::memory_order_relaxed),
        processorRef.uiSnapshot.master_vu_right.load(std::memory_order_relaxed));

    // REVIEW FIX #6: Remote VU from cachedUsers -- NOT hardcoded zero.
    // cachedUsers is populated by run thread via GetRemoteUsersSnapshot()
    // which includes vu_left/vu_right per channel.
    const auto& users = processorRef.cachedUsers;
    int stripIdx = 0;
    for (const auto& user : users)
    {
        for (const auto& ch : user.channels)
        {
            if (stripIdx < static_cast<int>(remoteStrips.size()))
            {
                remoteStrips[stripIdx]->setVuLevels(ch.vu_left, ch.vu_right);
            }
            ++stripIdx;
        }
    }
}

void ChannelStripArea::refreshFromUsers(const std::vector<NJClient::RemoteUserInfo>& users)
{
    remoteStrips.clear();

    for (size_t userIdx = 0; userIdx < users.size(); ++userIdx)
    {
        const auto& user = users[userIdx];
        // Strip @IP suffix from username (e.g. "user@1.2.3.4" -> "user")
        juce::String userName(user.name);
        int atIdx = userName.lastIndexOfChar('@');
        if (atIdx > 0)
            userName = userName.substring(0, atIdx);

        if (user.channels.size() <= 1)
        {
            // Single channel user: one strip
            auto strip = std::make_unique<ChannelStrip>();
            juce::String codecStr;  // Codec badge -- could be extended later
            strip->configure(ChannelStrip::StripType::Remote, userName, codecStr);

            if (!user.channels.empty())
            {
                strip->setSubscribed(user.channels[0].subscribed);

                // Wire subscribe toggle to push command
                const int uIdx = static_cast<int>(userIdx);
                const int chIdx = user.channels[0].channel_index;
                strip->onSubscribeToggled = [this, uIdx, chIdx](bool sub)
                {
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = chIdx;
                    cmd.set_sub = true;
                    cmd.subscribed = sub;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };
            }

            remoteStrips.push_back(std::move(strip));
        }
        else
        {
            // Multi-channel user: parent strip + child strips
            auto parentStrip = std::make_unique<ChannelStrip>();
            parentStrip->configure(ChannelStrip::StripType::Remote, userName, {},
                                   static_cast<int>(user.channels.size()), false);

            // Expand toggle rebuilds child visibility
            parentStrip->onExpandToggled = [this]()
            {
                rebuildStrips();
            };

            remoteStrips.push_back(std::move(parentStrip));

            // Child strips (initially hidden, shown when expanded)
            for (size_t chIdx = 0; chIdx < user.channels.size(); ++chIdx)
            {
                const auto& ch = user.channels[chIdx];
                auto childStrip = std::make_unique<ChannelStrip>();
                childStrip->configure(ChannelStrip::StripType::RemoteChild,
                                      juce::String(ch.name));
                childStrip->setSubscribed(ch.subscribed);

                const int uIdx = static_cast<int>(userIdx);
                const int cIdx = ch.channel_index;
                childStrip->onSubscribeToggled = [this, uIdx, cIdx](bool sub)
                {
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_sub = true;
                    cmd.subscribed = sub;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                remoteStrips.push_back(std::move(childStrip));
            }
        }
    }

    rebuildStrips();
}

void ChannelStripArea::rebuildStrips()
{
    // Remove all children from strip container
    stripContainer.removeAllChildren();

    // Add local strip
    stripContainer.addAndMakeVisible(localStrip);

    // Add visible remote strips, respecting expand/collapse state.
    // Parent strips with channelCount > 1 control child visibility:
    // when collapsed, skip the child strips that follow the parent.
    bool skipChildren = false;
    for (auto& strip : remoteStrips)
    {
        if (strip->getType() == ChannelStrip::StripType::Remote)
        {
            // Parent strip — always visible
            stripContainer.addAndMakeVisible(*strip);
            // If this parent has multiple channels, collapse hides children
            skipChildren = !strip->isExpanded()
                           && strip->getChannelCount() > 1;
        }
        else if (strip->getType() == ChannelStrip::StripType::RemoteChild)
        {
            if (!skipChildren)
                stripContainer.addAndMakeVisible(*strip);
            else
                strip->setVisible(false);
        }
        else
        {
            stripContainer.addAndMakeVisible(*strip);
        }
    }

    resized();
}

void ChannelStripArea::setDisconnectedState()
{
    isConnected = false;

    viewport.setVisible(false);
    masterStrip.setVisible(false);
    emptyStateLabel.setVisible(true);
    browseButton.setVisible(true);

    resized();
}

void ChannelStripArea::setConnectedState()
{
    isConnected = true;

    viewport.setVisible(true);
    masterStrip.setVisible(true);
    emptyStateLabel.setVisible(false);
    browseButton.setVisible(false);

    resized();
}

void ChannelStripArea::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kBgPrimary));
}

void ChannelStripArea::resized()
{
    auto area = getLocalBounds();

    if (!isConnected)
    {
        // Center empty state message + Browse Servers button
        auto center = area.withSizeKeepingCentre(300, 80);
        emptyStateLabel.setBounds(center.removeFromTop(40));
        browseButton.setBounds(center.withSizeKeepingCentre(160, 30));
        return;
    }

    // Master strip pinned right
    auto masterArea = area.removeFromRight(kMasterWidth);
    masterStrip.setBounds(masterArea);

    // Separator
    area.removeFromRight(2);

    // Viewport with local + remote strips
    viewport.setBounds(area);

    // Layout strips inside container
    const int totalStrips = 1 + static_cast<int>(remoteStrips.size()); // local + remotes
    const int containerWidth = totalStrips * kStripPitch;
    const int containerHeight = area.getHeight();
    stripContainer.setBounds(0, 0, containerWidth, containerHeight);

    int x = 0;

    // Local strip
    localStrip.setBounds(x, 0, kStripWidth, containerHeight);
    x += kStripPitch;

    // Remote strips
    for (auto& strip : remoteStrips)
    {
        strip->setBounds(x, 0, kStripWidth, containerHeight);
        x += kStripPitch;
    }
}

int ChannelStripArea::getDesiredWidth() const
{
    const int totalStrips = 1 + static_cast<int>(remoteStrips.size());
    return totalStrips * kStripPitch + kMasterWidth;
}
