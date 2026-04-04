#include "JamWideJuceEditor.h"
#include "core/njclient.h"
#include "threading/ui_event.h"

#include <variant>

JamWideJuceEditor::JamWideJuceEditor(JamWideJuceProcessor& p)
    : AudioProcessorEditor(p),
      processorRef(p),
      connectionBar(p),
      chatPanel(p)
{
    setLookAndFeel(&lookAndFeel);
    setSize(kBaseWidth, kBaseHeight);
    setResizable(false, false);  // D-22: not resizable

    // ConnectionBar
    addAndMakeVisible(connectionBar);
    connectionBar.onBrowseClicked = [this]() { showServerBrowser(); };
    connectionBar.onScaleChanged = [this](float factor) { applyScale(factor); };

    // ChatPanel
    addAndMakeVisible(chatPanel);

    // Load persistent chat history from processor
    const auto& history = processorRef.chatHistory.getMessages();
    if (!history.empty())
        chatPanel.loadHistory(history);

    // Mixer placeholder (center area)
    mixerPlaceholder.setText("Mixer area (Plan 03)", juce::dontSendNotification);
    mixerPlaceholder.setFont(juce::FontOptions(16.0f));
    mixerPlaceholder.setColour(juce::Label::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextSecondary));
    mixerPlaceholder.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(mixerPlaceholder);

    // Start 20Hz timer for event drain and status polling
    startTimerHz(20);

    // Apply initial scale if not 1.0
    if (!juce::approximatelyEqual(processorRef.scaleFactor, 1.0f))
        applyScale(processorRef.scaleFactor);
}

JamWideJuceEditor::~JamWideJuceEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void JamWideJuceEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(JamWideLookAndFeel::kBgPrimary));
}

void JamWideJuceEditor::resized()
{
    auto area = getLocalBounds();

    // ConnectionBar at top: full width, kConnectionBarHeight tall
    connectionBar.setBounds(area.removeFromTop(kConnectionBarHeight));

    // ChatPanel at right: kChatPanelWidth wide, remaining height
    if (chatSidebarVisible)
        chatPanel.setBounds(area.removeFromRight(kChatPanelWidth));

    // Mixer placeholder takes remaining center area
    mixerPlaceholder.setBounds(area);
}

void JamWideJuceEditor::timerCallback()
{
    drainEvents();
    pollStatus();
}

void JamWideJuceEditor::drainEvents()
{
    // Drain event queue (StatusChanged, ServerList, TopicChanged, UserInfoChanged)
    processorRef.evt_queue.drain([this](jamwide::UiEvent&& evt) {
        std::visit([this](auto&& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, jamwide::StatusChangedEvent>)
            {
                // Status updates are handled in pollStatus()
            }
            else if constexpr (std::is_same_v<T, jamwide::ServerListEvent>)
            {
                // Store server list on processor for server browser (Plan 04)
                processorRef.cachedServerList = std::move(e.servers);
            }
            else if constexpr (std::is_same_v<T, jamwide::TopicChangedEvent>)
            {
                chatPanel.setTopic(juce::String(e.topic));
            }
            else if constexpr (std::is_same_v<T, jamwide::UserInfoChangedEvent>)
            {
                // User info refresh handled in pollStatus() via userCount
            }
        }, std::move(evt));
    });

    // Drain chat queue
    processorRef.chat_queue.drain([this](ChatMessage&& msg) {
        chatPanel.addMessage(msg);
    });

    // Check license pending
    if (processorRef.license_pending.load(std::memory_order_acquire))
    {
        showLicenseDialog();
    }
}

void JamWideJuceEditor::pollStatus()
{
    auto* client = processorRef.getClient();
    if (!client) return;

    int status = client->cached_status.load(std::memory_order_acquire);
    float bpm = processorRef.uiSnapshot.bpm.load(std::memory_order_relaxed);
    int bpi = processorRef.uiSnapshot.bpi.load(std::memory_order_relaxed);
    int beat = processorRef.uiSnapshot.beat_position.load(std::memory_order_relaxed);
    int numUsers = processorRef.userCount.load(std::memory_order_relaxed);

    connectionBar.updateStatus(status, bpm, bpi, beat, numUsers);

    // REVIEW FIX: Use member variable prevPollStatus_ instead of static int lastStatus.
    // This prevents state leaking across editor reconstructions.
    if (status != prevPollStatus_)
    {
        if (status == NJClient::NJC_STATUS_OK)
            chatPanel.setConnectedState();
        else
            chatPanel.setNotConnectedState();
        prevPollStatus_ = status;
    }
}

void JamWideJuceEditor::showServerBrowser()
{
    // Placeholder for Plan 04 -- real overlay implementation later
    DBG("Server browser not yet implemented");
}

void JamWideJuceEditor::showLicenseDialog()
{
    // Placeholder for Plan 04 -- auto-accept license for now
    processorRef.license_response.store(1, std::memory_order_release);
    processorRef.license_cv.notify_one();
}

void JamWideJuceEditor::applyScale(float factor)
{
    // REVIEW FIX #1: Transform ONLY. Do NOT call setSize with scaled dimensions.
    // Plugin hosts see the component bounds as the "real" size. If we both
    // transform AND resize, the host applies its own DPI scaling on top,
    // resulting in incorrect 2x or 3x scaling.
    //
    // setSize stays at kBaseWidth x kBaseHeight. The AffineTransform handles
    // the visual scaling. The host constrains the window to base dimensions,
    // and the transform makes content appear larger within those bounds.
    setTransform(juce::AffineTransform::scale(factor));

    // In standalone mode only, the window can grow.
    // In plugin mode, the host manages the window size.
    if (auto* peer = getPeer())
    {
        if (peer->getStyleFlags() & juce::ComponentPeer::windowHasTitleBar)
        {
            // Standalone: resize window to fit scaled content
            setSize(static_cast<int>(kBaseWidth * factor),
                    static_cast<int>(kBaseHeight * factor));
        }
    }
}
