#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include "ui/ui_state.h"

class JamWideJuceProcessor;

class ChatPanel : public juce::Component
{
public:
    explicit ChatPanel(JamWideJuceProcessor& processor);
    ~ChatPanel() override = default;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void addMessage(const ChatMessage& msg);
    void loadHistory(const std::vector<ChatMessage>& history);
    void setTopic(const juce::String& topic);
    void setNotConnectedState();
    void setConnectedState();
    void focusChatInput();

private:
    void handleSend();

    JamWideJuceProcessor& processorRef;
    juce::Label topicLabel;
    juce::TextEditor chatLog;
    juce::TextEditor chatInput;
    juce::TextButton emojiButton;
    juce::TextButton sendButton;
    juce::TextButton jumpToBottomButton;
    juce::Label tipLabel;
    bool autoScroll = true;
    bool isConnected_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatPanel)
};
