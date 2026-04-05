#include "ChannelStripArea.h"
#include "JamWideLookAndFeel.h"
#include "../JamWideJuceProcessor.h"
#include "threading/ui_command.h"

ChannelStripArea::ChannelStripArea(JamWideJuceProcessor& processor)
    : processorRef(processor)
{
    // Configure local strip as expandable parent (4 channels)
    localStrip.configure(ChannelStrip::StripType::Local, "Local", {}, 4, false);

    // Wire local strip mixer callbacks to SetLocalChannelMonitoringCommand
    localStrip.onVolumeChanged = [this](float vol) {
        jamwide::SetLocalChannelMonitoringCommand cmd;
        cmd.channel = 0;
        cmd.set_volume = true;
        cmd.volume = vol;
        processorRef.cmd_queue.try_push(std::move(cmd));
    };
    localStrip.onPanChanged = [this](float pan) {
        jamwide::SetLocalChannelMonitoringCommand cmd;
        cmd.channel = 0;
        cmd.set_pan = true;
        cmd.pan = pan;
        processorRef.cmd_queue.try_push(std::move(cmd));
    };
    localStrip.onMuteToggled = [this](bool mute) {
        jamwide::SetLocalChannelMonitoringCommand cmd;
        cmd.channel = 0;
        cmd.set_mute = true;
        cmd.mute = mute;
        processorRef.cmd_queue.try_push(std::move(cmd));
    };
    localStrip.onSoloToggled = [this](bool solo) {
        jamwide::SetLocalChannelMonitoringCommand cmd;
        cmd.channel = 0;
        cmd.set_solo = true;
        cmd.solo = solo;
        processorRef.cmd_queue.try_push(std::move(cmd));
    };

    localStrip.onTransmitToggled = [this](bool tx) {
        processorRef.localTransmit[0] = tx;
        jamwide::SetLocalChannelInfoCommand cmd;
        cmd.channel = 0;
        cmd.set_transmit = true;
        cmd.transmit = tx;
        processorRef.cmd_queue.try_push(std::move(cmd));
    };

    // Make expand button visible on local strip (4ch badge)
    localStrip.onExpandToggled = [this]() {
        localExpanded_ = !localExpanded_;
        rebuildStrips();
        if (onLayoutChanged) onLayoutChanged();
    };

    // Show input bus selector on parent local strip (channel 0 per D-14)
    localStrip.getInputBusSelector().setVisible(true);
    localStrip.setInputBus(processorRef.localInputSelector[0]);
    localStrip.onInputBusChanged = [this](int srcch) {
        processorRef.localInputSelector[0] = srcch & 0x3FF;
        jamwide::SetLocalChannelInfoCommand cmd;
        cmd.channel = 0;
        cmd.set_srcch = true;
        cmd.srcch = srcch;
        processorRef.cmd_queue.try_push(std::move(cmd));
    };

    // Create 3 child strips for local channels 1-3
    for (int ch = 1; ch < 4; ++ch)
    {
        auto child = std::make_unique<ChannelStrip>();
        child->configure(ChannelStrip::StripType::Local,
                         "Ch" + juce::String(ch + 1));

        // Set initial TX state from persisted array
        child->setTransmitting(processorRef.localTransmit[ch]);

        // Show input bus selector on local child strips (per D-14)
        child->getInputBusSelector().setVisible(true);
        child->setInputBus(processorRef.localInputSelector[ch]);

        // Wire input bus selector to command queue (per D-14)
        child->onInputBusChanged = [this, ch](int srcch) {
            processorRef.localInputSelector[ch] = srcch & 0x3FF;
            jamwide::SetLocalChannelInfoCommand cmd;
            cmd.channel = ch;
            cmd.set_srcch = true;
            cmd.srcch = srcch;
            processorRef.cmd_queue.try_push(std::move(cmd));
        };

        // Wire child channel controls to command queue
        child->onVolumeChanged = [this, ch](float vol) {
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = ch;
            cmd.set_volume = true;
            cmd.volume = vol;
            processorRef.cmd_queue.try_push(std::move(cmd));
        };
        child->onPanChanged = [this, ch](float pan) {
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = ch;
            cmd.set_pan = true;
            cmd.pan = pan;
            processorRef.cmd_queue.try_push(std::move(cmd));
        };
        child->onMuteToggled = [this, ch](bool mute) {
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = ch;
            cmd.set_mute = true;
            cmd.mute = mute;
            processorRef.cmd_queue.try_push(std::move(cmd));
        };
        child->onSoloToggled = [this, ch](bool solo) {
            // NJClient handles additive solo internally via m_issoloactive bitmask.
            // No UI-side solo tracking needed.
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = ch;
            cmd.set_solo = true;
            cmd.solo = solo;
            processorRef.cmd_queue.try_push(std::move(cmd));
        };
        child->onTransmitToggled = [this, ch](bool tx) {
            processorRef.localTransmit[ch] = tx;
            jamwide::SetLocalChannelInfoCommand cmd;
            cmd.channel = ch;
            cmd.set_transmit = true;
            cmd.transmit = tx;
            processorRef.cmd_queue.try_push(std::move(cmd));
        };

        localChildStrips.push_back(std::move(child));
    }

    // APVTS attachments for local channel 0 (on localStrip)
    {
        auto* volParam = processorRef.apvts.getParameter("localVol_0");
        if (volParam)
            localStrip.getFader().attachToParameter(*volParam);

        localAttachments_[0].pan = std::make_unique<juce::SliderParameterAttachment>(
            *processorRef.apvts.getParameter("localPan_0"),
            localStrip.getPanSlider());

        localAttachments_[0].mute = std::make_unique<juce::ButtonParameterAttachment>(
            *processorRef.apvts.getParameter("localMute_0"),
            localStrip.getMuteButton());
    }

    // APVTS attachments for local channels 1-3 (on child strips)
    for (int ch = 1; ch < 4; ++ch)
    {
        auto& child = *localChildStrips[ch - 1];
        juce::String suffix = juce::String(ch);

        auto* volParam = processorRef.apvts.getParameter("localVol_" + suffix);
        if (volParam)
            child.getFader().attachToParameter(*volParam);

        localAttachments_[ch].pan = std::make_unique<juce::SliderParameterAttachment>(
            *processorRef.apvts.getParameter("localPan_" + suffix),
            child.getPanSlider());

        localAttachments_[ch].mute = std::make_unique<juce::ButtonParameterAttachment>(
            *processorRef.apvts.getParameter("localMute_" + suffix),
            child.getMuteButton());
    }

    // Configure master strip
    masterStrip.configure(ChannelStrip::StripType::Master, "Master");

    // Wire master strip fader to APVTS masterVol parameter
    masterStrip.onVolumeChanged = [this](float vol) {
        if (auto* param = processorRef.apvts.getParameter("masterVol"))
            param->setValueNotifyingHost(
                param->getNormalisableRange().convertTo0to1(vol));
    };
    masterStrip.onMuteToggled = [this](bool mute) {
        if (auto* param = processorRef.apvts.getParameter("masterMute"))
            param->setValueNotifyingHost(mute ? 1.0f : 0.0f);
    };

    // Set initial master fader value from APVTS
    masterStrip.setVolume(*processorRef.apvts.getRawParameterValue("masterVol"));
    masterStrip.setMuted(*processorRef.apvts.getRawParameterValue("masterMute") >= 0.5f);

    // Metronome label
    addAndMakeVisible(metroLabel);
    metroLabel.setText("Metronome", juce::dontSendNotification);
    metroLabel.setFont(juce::FontOptions(9.0f));
    metroLabel.setColour(juce::Label::textColourId,
                         juce::Colour(JamWideLookAndFeel::kTextSecondary));
    metroLabel.setJustificationType(juce::Justification::centredLeft);

    // Metronome horizontal slider (yellow fill) per D-16, D-17, D-18
    addAndMakeVisible(metroSlider);
    metroSlider.setName("MetroSlider");
    metroSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    metroSlider.setRange(0.0, 2.0, 0.01);
    metroSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);

    // Attach to APVTS metroVol
    metroSliderAttachment_ = std::make_unique<juce::SliderParameterAttachment>(
        *processorRef.apvts.getParameter("metroVol"), metroSlider);

    // Metronome mute button
    addAndMakeVisible(metroMuteBtn);
    metroMuteBtn.setButtonText("M");
    metroMuteBtn.setClickingTogglesState(true);
    metroMuteBtn.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(JamWideLookAndFeel::kBgElevated));
    metroMuteBtn.setColour(juce::TextButton::buttonOnColourId,
                           juce::Colour(JamWideLookAndFeel::kAccentDestructive));
    metroMuteBtn.setColour(juce::TextButton::textColourOffId,
                           juce::Colour(JamWideLookAndFeel::kTextSecondary));
    metroMuteBtn.setColour(juce::TextButton::textColourOnId,
                           juce::Colour(JamWideLookAndFeel::kTextPrimary));

    // Attach to APVTS metroMute
    metroMuteAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(
        *processorRef.apvts.getParameter("metroMute"), metroMuteBtn);

    // Set up viewport with strip container
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&stripContainer, false);
    viewport.setScrollBarsShown(false, false); // no scrollbar -- Fit button + mouse wheel to navigate

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
    // Destroy APVTS attachments before the UI components they're attached to
    for (auto& att : localAttachments_)
    {
        att.vol.reset();
        att.pan.reset();
        att.mute.reset();
    }
    metroSliderAttachment_.reset();
    metroMuteAttachment_.reset();
    // Also detach local strip faders
    localStrip.getFader().detachFromParameter();
    for (auto& child : localChildStrips)
        child->getFader().detachFromParameter();
}

// Stable identity lookup: resolve current user_index and channel_index
// from username + channel name. Returns {-1, -1} if user/channel gone.
std::pair<int, int> ChannelStripArea::findRemoteIndex(
    const juce::String& userName, const juce::String& channelName) const
{
    const auto& users = processorRef.cachedUsers;
    for (int u = 0; u < static_cast<int>(users.size()); ++u)
    {
        if (juce::String(users[u].name) == userName)
        {
            for (int c = 0; c < static_cast<int>(users[u].channels.size()); ++c)
            {
                if (juce::String(users[u].channels[c].name) == channelName)
                    return {u, users[u].channels[c].channel_index};
            }
        }
    }
    return {-1, -1};
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
    for (auto& child : localChildStrips)
        if (child->isVisible())
            child->tickVu();
    for (auto& strip : remoteStrips)
        strip->tickVu();
}

void ChannelStripArea::updateVuLevels()
{
    // Local channel 0 VU (on localStrip)
    localStrip.setVuLevels(
        processorRef.uiSnapshot.local_ch_vu_left[0].load(std::memory_order_relaxed),
        processorRef.uiSnapshot.local_ch_vu_right[0].load(std::memory_order_relaxed));

    // Local channels 1-3 VU (on child strips)
    for (int ch = 1; ch < 4; ++ch)
    {
        if (ch - 1 < static_cast<int>(localChildStrips.size()))
        {
            localChildStrips[ch - 1]->setVuLevels(
                processorRef.uiSnapshot.local_ch_vu_left[ch].load(std::memory_order_relaxed),
                processorRef.uiSnapshot.local_ch_vu_right[ch].load(std::memory_order_relaxed));
        }
    }

    // Master VU from processBlock output
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
    // REVIEW CONCERN: Attachment lifetime during strip rebuilds.
    // Detach any APVTS parameter attachments before destroying strips.
    for (auto& strip : remoteStrips)
        strip->getFader().detachFromParameter();  // Safe no-op if not attached
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
                const auto& ch = user.channels[0];
                strip->setSubscribed(ch.subscribed);

                // Set initial mixer state from cached data
                strip->setVolume(ch.volume);
                strip->setPan(ch.pan);
                strip->setMuted(ch.mute);
                strip->setSoloed(ch.solo);

                // Capture stable identity, NOT indices (REVIEW CONCERN: stale user_index)
                juce::String uName(user.name);
                juce::String cName(ch.name);

                // Wire subscribe toggle to push command (also uses stable identity)
                strip->onSubscribeToggled = [this, uName, cName](bool sub)
                {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;  // User left, ignore
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_sub = true;
                    cmd.subscribed = sub;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                // Wire volume/pan/mute/solo callbacks with stable identity
                strip->onVolumeChanged = [this, uName, cName](float vol) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_vol = true;
                    cmd.volume = vol;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                strip->onPanChanged = [this, uName, cName](float pan) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_pan = true;
                    cmd.pan = pan;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                strip->onMuteToggled = [this, uName, cName](bool mute) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_mute = true;
                    cmd.mute = mute;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                strip->onSoloToggled = [this, uName, cName](bool solo) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_solo = true;
                    cmd.solo = solo;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                strip->onRoutingChanged = [this, uName, cName](int busIndex) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_outch = true;
                    cmd.outchannel = busIndex * 2;  // Convert bus index to channel pair offset
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                // Refresh routing selector from snapshot out_chan_index
                int busIndex = ch.out_chan_index / 2;
                strip->setRoutingBus(busIndex);
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

                // Set initial mixer state from cached data
                childStrip->setVolume(ch.volume);
                childStrip->setPan(ch.pan);
                childStrip->setMuted(ch.mute);
                childStrip->setSoloed(ch.solo);

                // Capture stable identity, NOT indices
                juce::String uName(user.name);
                juce::String cName(ch.name);

                childStrip->onSubscribeToggled = [this, uName, cName](bool sub)
                {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_sub = true;
                    cmd.subscribed = sub;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                childStrip->onVolumeChanged = [this, uName, cName](float vol) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_vol = true;
                    cmd.volume = vol;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                childStrip->onPanChanged = [this, uName, cName](float pan) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_pan = true;
                    cmd.pan = pan;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                childStrip->onMuteToggled = [this, uName, cName](bool mute) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_mute = true;
                    cmd.mute = mute;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                childStrip->onSoloToggled = [this, uName, cName](bool solo) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_solo = true;
                    cmd.solo = solo;
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                childStrip->onRoutingChanged = [this, uName, cName](int busIndex) {
                    auto [uIdx, cIdx] = findRemoteIndex(uName, cName);
                    if (uIdx < 0) return;
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = uIdx;
                    cmd.channel_index = cIdx;
                    cmd.set_outch = true;
                    cmd.outchannel = busIndex * 2;  // Convert bus index to channel pair offset
                    processorRef.cmd_queue.try_push(std::move(cmd));
                };

                // Refresh routing selector from snapshot out_chan_index
                int busIndex = ch.out_chan_index / 2;
                childStrip->setRoutingBus(busIndex);

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

    // Add local child strips when expanded
    if (localExpanded_)
    {
        for (auto& child : localChildStrips)
            stripContainer.addAndMakeVisible(*child);
    }
    else
    {
        for (auto& child : localChildStrips)
            child->setVisible(false);
    }

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
    metroLabel.setVisible(false);
    metroSlider.setVisible(false);
    metroMuteBtn.setVisible(false);
    emptyStateLabel.setVisible(true);
    browseButton.setVisible(true);

    resized();
}

void ChannelStripArea::setConnectedState()
{
    isConnected = true;

    viewport.setVisible(true);
    masterStrip.setVisible(true);
    metroLabel.setVisible(true);
    metroSlider.setVisible(true);
    metroMuteBtn.setVisible(true);
    emptyStateLabel.setVisible(false);
    browseButton.setVisible(false);

    resized();
}

void ChannelStripArea::mouseDown(const juce::MouseEvent& e)
{
    // Forward right-clicks to parent (editor) so the scale menu works anywhere
    if (e.mods.isPopupMenu())
    {
        if (auto* parent = getParentComponent())
            parent->mouseDown(e.getEventRelativeTo(parent));
    }
    else
    {
        Component::mouseDown(e);
    }
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

    // Fit button: bottom-left corner of the strip area, above viewport
    // Master strip pinned right
    auto masterArea = area.removeFromRight(kMasterWidth);
    masterStrip.setBounds(masterArea);

    // Separator
    area.removeFromRight(2);

    // Viewport with local + remote strips
    viewport.setBounds(area);

    // Layout strips inside container
    int localStripCount = 1 + (localExpanded_ ? static_cast<int>(localChildStrips.size()) : 0);
    int visibleRemoteStrips = 0;
    for (auto& strip : remoteStrips)
        if (strip->isVisible())
            ++visibleRemoteStrips;
    const int totalStrips = localStripCount + visibleRemoteStrips;
    const int containerWidth = totalStrips * kStripPitch;
    const int containerHeight = area.getHeight();
    stripContainer.setBounds(0, 0, containerWidth, containerHeight);

    int x = 0;

    // Local strip
    localStrip.setBounds(x, 0, kStripWidth, containerHeight);
    x += kStripPitch;

    // Local child strips (when expanded)
    if (localExpanded_)
    {
        for (auto& child : localChildStrips)
        {
            child->setBounds(x, 0, kStripWidth, containerHeight);
            x += kStripPitch;
        }
    }

    // Remote strips
    for (auto& strip : remoteStrips)
    {
        if (strip->isVisible())
        {
            strip->setBounds(x, 0, kStripWidth, containerHeight);
            x += kStripPitch;
        }
    }

    // Position metronome controls below master strip's fader, inside footer area.
    // toFront() ensures they render above the masterStrip's background.
    auto masterBounds = masterStrip.getBounds();
    auto metroArea = juce::Rectangle<int>(
        masterBounds.getX(), masterBounds.getBottom() - 38,
        masterBounds.getWidth(), 38);

    // "Metronome" label row
    auto labelRow = metroArea.removeFromTop(10);
    metroLabel.setBounds(labelRow.reduced(4, 0));
    metroArea.removeFromTop(1);

    // Slider + mute button side by side
    auto controlRow = metroArea.removeFromTop(14);
    auto controlArea = controlRow.reduced(4, 0);
    metroMuteBtn.setBounds(controlArea.removeFromRight(18));
    controlArea.removeFromRight(2);
    metroSlider.setBounds(controlArea);

    metroLabel.toFront(false);
    metroSlider.toFront(false);
    metroMuteBtn.toFront(false);
}

int ChannelStripArea::getDesiredWidth() const
{
    int localStripCount = 1 + (localExpanded_ ? static_cast<int>(localChildStrips.size()) : 0);
    const int totalStrips = localStripCount + static_cast<int>(remoteStrips.size());
    return totalStrips * kStripPitch + kMasterWidth;
}
