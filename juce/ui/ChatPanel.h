#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include "ui/ui_state.h"

class JamWideJuceProcessor;

class ChatMessageListComponent : public juce::Component
{
public:
    ChatMessageListComponent() = default;
    void addMessage(const ChatMessage& msg);
    void loadHistory(const std::vector<ChatMessage>& history);
    void setTopic(const juce::String& topic);
    void paint(juce::Graphics& g) override;
    int getContentHeight() const;

private:
    struct RenderedMessage {
        ChatMessageType type;
        juce::String sender;
        juce::String content;
        juce::String timestamp;
        int height = 0;
    };
    std::vector<RenderedMessage> messages;
    juce::String topicText;
    int cachedWidth = 0;
    void recalculateHeights(int width);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatMessageListComponent)
};

class ChatPanel : public juce::Component,
                  private juce::Timer
{
public:
    explicit ChatPanel(JamWideJuceProcessor& processor);
    ~ChatPanel() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void addMessage(const ChatMessage& msg);
    void loadHistory(const std::vector<ChatMessage>& history);
    void setTopic(const juce::String& topic);
    void setNotConnectedState();
    void setConnectedState();

private:
    void handleSend();
    bool isScrolledToBottom() const;
    void scrollToBottom();
    void timerCallback() override;

    JamWideJuceProcessor& processorRef;
    juce::Label topicLabel;
    juce::Viewport chatViewport;
    ChatMessageListComponent messageList;
    juce::TextEditor chatInput;
    juce::TextButton sendButton;
    juce::TextButton jumpToBottomButton;
    juce::Label tipLabel;
    bool autoScroll = true;
    bool isConnected_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatPanel)
};
