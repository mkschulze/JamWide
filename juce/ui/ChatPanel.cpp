#include "ChatPanel.h"
#include "JamWideJuceProcessor.h"
#include "ui/JamWideLookAndFeel.h"
#include "threading/ui_command.h"

#include <cctype>
#include <string>

namespace {

//==============================================================================
// Color for each chat message type (using LookAndFeel constants)
juce::Colour colorForType(ChatMessageType type)
{
    switch (type)
    {
        case ChatMessageType::PrivateMessage:
            return juce::Colour(JamWideLookAndFeel::kChatPrivate);
        case ChatMessageType::Topic:
            return juce::Colour(JamWideLookAndFeel::kChatTopic);
        case ChatMessageType::Join:
            return juce::Colour(JamWideLookAndFeel::kChatJoin);
        case ChatMessageType::Part:
            return juce::Colour(JamWideLookAndFeel::kChatPart);
        case ChatMessageType::Action:
            return juce::Colour(JamWideLookAndFeel::kChatAction);
        case ChatMessageType::System:
            return juce::Colour(JamWideLookAndFeel::kChatSystem);
        case ChatMessageType::Message:
        default:
            return juce::Colour(JamWideLookAndFeel::kChatRegular);
    }
}

//==============================================================================
// Format a chat message for display
juce::String formatMessage(const ChatMessage& msg)
{
    juce::String prefix = msg.timestamp.empty()
        ? juce::String()
        : juce::String("[") + juce::String(msg.timestamp) + "] ";

    switch (msg.type)
    {
        case ChatMessageType::Action:
            return prefix + "* " + juce::String(msg.sender) + " " + juce::String(msg.content);
        case ChatMessageType::Join:
        case ChatMessageType::Part:
        case ChatMessageType::System:
            return prefix + juce::String(msg.content);
        case ChatMessageType::Topic:
            return prefix + "* " + juce::String(msg.content);
        case ChatMessageType::PrivateMessage:
            return prefix + "[PM] <" + juce::String(msg.sender) + "> " + juce::String(msg.content);
        case ChatMessageType::Message:
        default:
            return prefix + "<" + juce::String(msg.sender) + "> " + juce::String(msg.content);
    }
}

//==============================================================================
// Port of parse_chat_input from src/ui/ui_chat.cpp
std::string trimLeft(const std::string& value)
{
    std::size_t pos = 0;
    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])))
        ++pos;
    return value.substr(pos);
}

bool parseChatInput(const juce::String& input, jamwide::SendChatCommand& cmd)
{
    std::string text = trimLeft(input.toStdString());
    if (text.empty()) return false;

    cmd.type = "MSG";
    cmd.target.clear();
    cmd.text = text;

    if (text[0] != '/')
        return true;

    if (text.rfind("/me ", 0) == 0)
    {
        cmd.type = "MSG";
        cmd.text = text;
        return true;
    }
    if (text.rfind("/topic ", 0) == 0)
    {
        cmd.type = "TOPIC";
        cmd.text = trimLeft(text.substr(7));
        return !cmd.text.empty();
    }
    if (text.rfind("/msg ", 0) == 0)
    {
        std::string rest = trimLeft(text.substr(5));
        std::size_t space = rest.find(' ');
        if (space == std::string::npos)
            return false;
        cmd.type = "PRIVMSG";
        cmd.target = rest.substr(0, space);
        cmd.text = trimLeft(rest.substr(space + 1));
        return !cmd.target.empty() && !cmd.text.empty();
    }

    // Unknown /command -- send as MSG anyway
    cmd.type = "MSG";
    cmd.text = text;
    return true;
}

constexpr int kMessageGap = 4;
constexpr int kLeftPadding = 8;
constexpr float kFontSize = 13.0f;

} // anonymous namespace

//==============================================================================
// ChatMessageListComponent
//==============================================================================

void ChatMessageListComponent::addMessage(const ChatMessage& msg)
{
    RenderedMessage rm;
    rm.type = msg.type;
    rm.sender = juce::String(msg.sender);
    rm.content = formatMessage(msg);
    rm.timestamp = juce::String(msg.timestamp);
    messages.push_back(std::move(rm));

    if (getWidth() > 0)
        recalculateHeights(getWidth());
}

void ChatMessageListComponent::loadHistory(const std::vector<ChatMessage>& history)
{
    messages.clear();
    for (auto& msg : history)
    {
        RenderedMessage rm;
        rm.type = msg.type;
        rm.sender = juce::String(msg.sender);
        rm.content = formatMessage(msg);
        rm.timestamp = juce::String(msg.timestamp);
        messages.push_back(std::move(rm));
    }
    if (getWidth() > 0)
        recalculateHeights(getWidth());
}

void ChatMessageListComponent::setTopic(const juce::String& topic)
{
    topicText = topic;
    repaint();
}

void ChatMessageListComponent::paint(juce::Graphics& g)
{
    if (getWidth() != cachedWidth && getWidth() > 0)
        recalculateHeights(getWidth());

    float y = 0.0f;
    juce::Font font{juce::FontOptions(kFontSize)};

    for (auto& msg : messages)
    {
        g.setColour(colorForType(msg.type));

        juce::AttributedString attrStr;
        attrStr.append(msg.content, font, colorForType(msg.type));
        attrStr.setWordWrap(juce::AttributedString::WordWrap::byWord);

        juce::TextLayout layout;
        layout.createLayout(attrStr, static_cast<float>(getWidth() - kLeftPadding * 2));
        layout.draw(g, juce::Rectangle<float>(
            static_cast<float>(kLeftPadding), y,
            static_cast<float>(getWidth() - kLeftPadding * 2),
            static_cast<float>(msg.height)));

        y += static_cast<float>(msg.height) + kMessageGap;
    }
}

int ChatMessageListComponent::getContentHeight() const
{
    int total = 0;
    for (auto& msg : messages)
        total += msg.height + kMessageGap;
    return total > 0 ? total : 0;
}

void ChatMessageListComponent::recalculateHeights(int width)
{
    cachedWidth = width;
    juce::Font font{juce::FontOptions(kFontSize)};
    float availableWidth = static_cast<float>(width - kLeftPadding * 2);
    if (availableWidth <= 0)
        availableWidth = 100.0f;

    for (auto& msg : messages)
    {
        juce::AttributedString attrStr;
        attrStr.append(msg.content, font, colorForType(msg.type));
        attrStr.setWordWrap(juce::AttributedString::WordWrap::byWord);

        juce::TextLayout layout;
        layout.createLayout(attrStr, availableWidth);
        msg.height = juce::jmax(18, static_cast<int>(std::ceil(layout.getHeight())));
    }

    setSize(width, getContentHeight());
}

//==============================================================================
// ChatPanel
//==============================================================================

ChatPanel::ChatPanel(JamWideJuceProcessor& processor)
    : processorRef(processor)
{
    // Topic label
    topicLabel.setFont(juce::FontOptions(11.0f));
    topicLabel.setColour(juce::Label::textColourId, juce::Colour(JamWideLookAndFeel::kTextSecondary));
    topicLabel.setText("", juce::dontSendNotification);
    addAndMakeVisible(topicLabel);

    // Chat viewport with message list
    chatViewport.setViewedComponent(&messageList, false);
    chatViewport.setScrollBarsShown(true, false);
    chatViewport.setColour(juce::ScrollBar::thumbColourId, juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    addAndMakeVisible(chatViewport);

    // Chat input
    chatInput.setMultiLine(false);
    chatInput.setTextToShowWhenEmpty("Type a message...",
        juce::Colour(JamWideLookAndFeel::kTextSecondary));
    chatInput.setFont(juce::FontOptions(kFontSize));
    chatInput.setColour(juce::TextEditor::backgroundColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceInput));
    chatInput.setColour(juce::TextEditor::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    chatInput.setColour(juce::TextEditor::outlineColourId,
        juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    chatInput.onReturnKey = [this]() { handleSend(); };
    addAndMakeVisible(chatInput);

    // Send button
    sendButton.setButtonText("Send");
    sendButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    sendButton.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    sendButton.onClick = [this]() { handleSend(); };
    addAndMakeVisible(sendButton);

    // Jump-to-bottom button (hidden by default)
    jumpToBottomButton.setButtonText(juce::CharPointer_UTF8("\xe2\x86\x93")); // down arrow
    jumpToBottomButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    jumpToBottomButton.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect));
    jumpToBottomButton.setVisible(false);
    jumpToBottomButton.onClick = [this]() {
        scrollToBottom();
        autoScroll = true;
        jumpToBottomButton.setVisible(false);
    };
    addAndMakeVisible(jumpToBottomButton);

    // Timer to check scroll position
    startTimerHz(5);
}

ChatPanel::~ChatPanel()
{
    stopTimer();
}

void ChatPanel::resized()
{
    auto area = getLocalBounds().reduced(4);

    // Topic at top
    if (topicLabel.getText().isNotEmpty())
    {
        topicLabel.setBounds(area.removeFromTop(18));
        area.removeFromTop(2);
    }
    else
    {
        topicLabel.setBounds(area.removeFromTop(0));
    }

    // Input row at bottom
    auto inputRow = area.removeFromBottom(28);
    sendButton.setBounds(inputRow.removeFromRight(48));
    inputRow.removeFromRight(4);
    chatInput.setBounds(inputRow);

    area.removeFromBottom(4);

    // Jump-to-bottom button above input
    jumpToBottomButton.setBounds(
        area.getRight() - 28, area.getBottom() - 24, 24, 20);

    // Chat viewport takes remaining space
    chatViewport.setBounds(area);

    // Resize the message list to match viewport width
    messageList.setSize(chatViewport.getMaximumVisibleWidth(), messageList.getHeight());
}

void ChatPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(JamWideLookAndFeel::kBgPrimary));
    g.fillRect(getLocalBounds());

    // Left border separator
    g.setColour(juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));

    // If not connected, show placeholder text (D-30)
    if (!isConnected_)
    {
        g.setColour(juce::Colour(JamWideLookAndFeel::kTextSecondary));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("Connect to a server\nto start chatting",
                   getLocalBounds().reduced(20),
                   juce::Justification::centred, true);
    }
}

void ChatPanel::addMessage(const ChatMessage& msg)
{
    messageList.addMessage(msg);

    // Also store in processor's persistent chat history
    ChatMessage copy = msg;
    processorRef.chatHistory.addMessage(std::move(copy));

    if (autoScroll)
    {
        juce::MessageManager::callAsync([this]() {
            scrollToBottom();
        });
    }
    else
    {
        jumpToBottomButton.setVisible(true);
    }
}

void ChatPanel::loadHistory(const std::vector<ChatMessage>& history)
{
    messageList.loadHistory(history);
    if (!history.empty())
    {
        juce::MessageManager::callAsync([this]() {
            scrollToBottom();
        });
    }
}

void ChatPanel::setTopic(const juce::String& topic)
{
    topicLabel.setText(topic, juce::dontSendNotification);
    messageList.setTopic(topic);
    resized();
}

void ChatPanel::setNotConnectedState()
{
    isConnected_ = false;
    chatInput.setEnabled(false);
    sendButton.setEnabled(false);
    repaint();
}

void ChatPanel::setConnectedState()
{
    isConnected_ = true;
    chatInput.setEnabled(true);
    sendButton.setEnabled(true);
    repaint();
}

void ChatPanel::handleSend()
{
    auto text = chatInput.getText();
    if (text.isEmpty())
        return;

    jamwide::SendChatCommand cmd;
    if (parseChatInput(text, cmd))
    {
        processorRef.cmd_queue.try_push(std::move(cmd));
    }
    else
    {
        // Invalid command -- add system message locally
        ChatMessage errMsg;
        errMsg.type = ChatMessageType::System;
        errMsg.content = "error: invalid command.";
        addMessage(errMsg);
    }

    chatInput.clear();
    chatInput.grabKeyboardFocus();
}

bool ChatPanel::isScrolledToBottom() const
{
    auto viewArea = chatViewport.getViewArea();
    int contentH = messageList.getContentHeight();
    int viewBottom = viewArea.getBottom();
    // Consider "at bottom" if within 20px of the end
    return (contentH - viewBottom) < 20;
}

void ChatPanel::scrollToBottom()
{
    int contentH = messageList.getContentHeight();
    int viewH = chatViewport.getViewArea().getHeight();
    if (contentH > viewH)
        chatViewport.setViewPosition(0, contentH - viewH);
}

void ChatPanel::timerCallback()
{
    // Check scroll position to manage auto-scroll behavior
    bool atBottom = isScrolledToBottom();
    if (atBottom)
    {
        autoScroll = true;
        jumpToBottomButton.setVisible(false);
    }
    else if (autoScroll)
    {
        // User has scrolled up
        autoScroll = false;
        jumpToBottomButton.setVisible(true);
    }
}
