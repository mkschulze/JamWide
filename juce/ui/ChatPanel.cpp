#include "ChatPanel.h"
#include "JamWideJuceProcessor.h"
#include "ui/JamWideLookAndFeel.h"
#include "threading/ui_command.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

// Color for each chat message type
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

// Strip @IP suffix from NINJAM usernames for display
juce::String stripIpFromSender(const std::string& sender)
{
    auto s = juce::String(sender);
    int atIdx = s.indexOfChar('@');
    return (atIdx > 0) ? s.substring(0, atIdx) : s;
}

juce::String formatMessage(const ChatMessage& msg)
{
    juce::String prefix = msg.timestamp.empty()
        ? juce::String()
        : juce::String("[") + juce::String(msg.timestamp) + "] ";

    auto displayName = stripIpFromSender(msg.sender);

    switch (msg.type)
    {
        case ChatMessageType::Action:
            return prefix + "* " + displayName + " " + juce::String(msg.content);
        case ChatMessageType::Join:
        case ChatMessageType::Part:
        case ChatMessageType::System:
            return prefix + juce::String(msg.content);
        case ChatMessageType::Topic:
            return prefix + "* " + juce::String(msg.content);
        case ChatMessageType::PrivateMessage:
            return prefix + "[PM] <" + displayName + "> " + juce::String(msg.content);
        case ChatMessageType::Message:
        default:
            return prefix + "<" + displayName + "> " + juce::String(msg.content);
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

constexpr float kFontSize = 13.0f;

} // anonymous namespace

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

    // Chat log — read-only, multi-line, supports text selection & copy
    chatLog.setMultiLine(true, true);
    chatLog.setReadOnly(true);
    chatLog.setCaretVisible(false);
    chatLog.setScrollbarsShown(true);
    chatLog.setFont(juce::FontOptions(kFontSize));
    chatLog.setColour(juce::TextEditor::backgroundColourId,
        juce::Colour(JamWideLookAndFeel::kBgPrimary));
    chatLog.setColour(juce::TextEditor::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    chatLog.setColour(juce::TextEditor::outlineColourId,
        juce::Colours::transparentBlack);
    chatLog.setColour(juce::TextEditor::focusedOutlineColourId,
        juce::Colours::transparentBlack);
    chatLog.setColour(juce::ScrollBar::thumbColourId,
        juce::Colour(JamWideLookAndFeel::kBorderSubtle));
    chatLog.addMouseListener(this, true);
    addAndMakeVisible(chatLog);

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

    // Emoji button
    emojiButton.setButtonText(juce::String(juce::CharPointer_UTF8("\xf0\x9f\x98\x80"))); // grin emoji
    emojiButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    emojiButton.setTooltip("Insert emoji");
    emojiButton.onClick = [this]() {
        static const char* emojis[] = {
            "\xf0\x9f\x98\x80", "\xf0\x9f\x98\x82", "\xf0\x9f\x98\x8e", "\xf0\x9f\xa4\x98",
            "\xf0\x9f\x8e\xb8", "\xf0\x9f\x8e\xb9", "\xf0\x9f\x8e\xa4", "\xf0\x9f\xa5\x81",
            "\xf0\x9f\x8e\xb5", "\xf0\x9f\x8e\xb6", "\xf0\x9f\x94\xa5", "\xf0\x9f\x91\x8d",
            "\xf0\x9f\x91\x8e", "\xf0\x9f\x91\x8f", "\xe2\x9c\x8c\xef\xb8\x8f", "\xf0\x9f\x92\xaa",
            "\xe2\x9d\xa4\xef\xb8\x8f", "\xf0\x9f\x98\xa2", "\xf0\x9f\x98\xa1", "\xf0\x9f\x98\xb4"
        };
        static const char* labels[] = {
            "grin", "laugh", "cool", "rock on",
            "guitar", "keys", "mic", "drums",
            "music", "notes", "fire", "thumbs up",
            "thumbs down", "clap", "peace", "strong",
            "heart", "sad", "angry", "sleep"
        };
        juce::PopupMenu menu;
        for (int i = 0; i < 20; ++i)
            menu.addItem(i + 1, juce::String(juce::CharPointer_UTF8(emojis[i])) + " " + labels[i]);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&emojiButton),
            [this](int result) {
                if (result > 0 && result <= 20)
                {
                    chatInput.insertTextAtCaret(juce::CharPointer_UTF8(emojis[result - 1]));
                    chatInput.grabKeyboardFocus();
                }
            });
    };
    addAndMakeVisible(emojiButton);

    // Send button
    sendButton.setButtonText("Send");
    sendButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    sendButton.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kTextPrimary));
    sendButton.onClick = [this]() { handleSend(); };
    addAndMakeVisible(sendButton);

    // Tip label -- chat commands, plus Reaper keyboard hint if running in Reaper
    {
        juce::PluginHostType hostType;
        juce::String tipText = "Commands: /msg /topic /me !vote";
        if (hostType.isReaper())
            tipText = "Tip: REAPER FX menu > 'Send keyboard input to plugin'\n" + tipText;
        tipLabel.setText(tipText, juce::dontSendNotification);
    }
    tipLabel.setFont(juce::FontOptions(9.0f));
    tipLabel.setColour(juce::Label::textColourId,
        juce::Colour(JamWideLookAndFeel::kTextSecondary).withAlpha(0.6f));
    tipLabel.setJustificationType(juce::Justification::centredLeft);
    tipLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(tipLabel);

    // Jump-to-bottom button (hidden by default)
    jumpToBottomButton.setButtonText(juce::CharPointer_UTF8("\xe2\x86\x93")); // down arrow
    jumpToBottomButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(JamWideLookAndFeel::kSurfaceStrip));
    jumpToBottomButton.setColour(juce::TextButton::textColourOffId,
        juce::Colour(JamWideLookAndFeel::kAccentConnect));
    jumpToBottomButton.setVisible(false);
    jumpToBottomButton.onClick = [this]() {
        chatLog.moveCaretToEnd();
        autoScroll = true;
        jumpToBottomButton.setVisible(false);
    };
    addAndMakeVisible(jumpToBottomButton);
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

    // Tip label at very bottom (2 lines in Reaper, 1 line otherwise)
    int tipH = tipLabel.getText().containsChar('\n') ? 22 : 12;
    tipLabel.setBounds(area.removeFromBottom(tipH));

    // Input row above tip
    auto inputRow = area.removeFromBottom(28);
    sendButton.setBounds(inputRow.removeFromRight(48));
    inputRow.removeFromRight(2);
    emojiButton.setBounds(inputRow.removeFromRight(28));
    inputRow.removeFromRight(2);
    chatInput.setBounds(inputRow);

    area.removeFromBottom(4);

    // Jump-to-bottom button above input
    jumpToBottomButton.setBounds(
        area.getRight() - 28, area.getBottom() - 24, 24, 20);

    // Chat log takes remaining space
    chatLog.setBounds(area);
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

void ChatPanel::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &chatLog || chatLog.isParentOf(e.eventComponent))
    {
        autoScroll = false;
        jumpToBottomButton.setVisible(true);
    }
}

void ChatPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails&)
{
    if (e.eventComponent == &chatLog || chatLog.isParentOf(e.eventComponent))
    {
        autoScroll = false;
        jumpToBottomButton.setVisible(true);
    }
}

void ChatPanel::addMessage(const ChatMessage& msg)
{
    // 2026-05-03 chat-scroll fix: use leading newline instead of trailing
    // newline so the caret never lands on an empty line below the most
    // recent message. With the previous trailing-"\n" pattern, JUCE scrolled
    // to keep the caret visible — and the caret was on an empty line below
    // the latest message, so the visible viewport ended in blank space and
    // the actual message appeared "lifted up" by 1+ lines.
    juce::String text = formatMessage(msg);
    const bool first = chatLog.getText().isEmpty();
    juce::String toInsert = first ? text : (juce::String("\n") + text);

    // Store in processor's persistent chat history
    ChatMessage copy = msg;
    processorRef.chatHistory.addMessage(std::move(copy));

    chatLog.setColour(juce::TextEditor::textColourId, colorForType(msg.type));

    if (autoScroll)
    {
        chatLog.moveCaretToEnd();
        chatLog.insertTextAtCaret(toInsert);
        // Force scroll to absolute end. JUCE's TextEditor sometimes
        // leaves a partial-line offset after insertTextAtCaret; setting
        // the caret by absolute character index reliably triggers
        // scrollToMakeSureCursorIsVisible against the very last char.
        chatLog.setCaretPosition(chatLog.getText().length());
    }
    else
    {
        // Append at end without disturbing the user's scroll/selection position.
        // All calls are synchronous so only the final state is painted.
        auto savedSel = chatLog.getHighlightedRegion();
        int savedPos = chatLog.getCaretPosition();

        chatLog.moveCaretToEnd();
        chatLog.insertTextAtCaret(toInsert);

        if (!savedSel.isEmpty())
            chatLog.setHighlightedRegion(savedSel);
        else
            chatLog.setCaretPosition(savedPos);
    }
}

void ChatPanel::loadHistory(const std::vector<ChatMessage>& history)
{
    chatLog.clear();
    bool first = true;
    for (auto& msg : history)
    {
        chatLog.setColour(juce::TextEditor::textColourId, colorForType(msg.type));
        chatLog.moveCaretToEnd();
        const juce::String line = formatMessage(msg);
        chatLog.insertTextAtCaret(first ? line : (juce::String("\n") + line));
        first = false;
    }
    chatLog.moveCaretToEnd();
    autoScroll = true;
    jumpToBottomButton.setVisible(false);
}

void ChatPanel::setTopic(const juce::String& topic)
{
    topicLabel.setText(topic, juce::dontSendNotification);
    resized();
}

void ChatPanel::setNotConnectedState()
{
    isConnected_ = false;
    chatInput.setEnabled(false);
    sendButton.setEnabled(false);
    chatLog.clear();
    topicLabel.setText("", juce::dontSendNotification);
    repaint();
}

void ChatPanel::setConnectedState()
{
    isConnected_ = true;
    chatInput.setEnabled(true);
    sendButton.setEnabled(true);
    repaint();
}

void ChatPanel::focusChatInput()
{
    if (isConnected_ && chatInput.isEnabled())
        chatInput.grabKeyboardFocus();
}

void ChatPanel::handleSend()
{
    auto text = chatInput.getText();
    if (text.isEmpty())
        return;

    // Local-only diagnostic commands handled before parseChatInput so they
    // work in any connection state and never reach the server.
    {
        const auto trimmed = text.trim();
        if (trimmed == "/rcmstats" || trimmed == "/rcm")
        {
            handleRcmStats();
            chatInput.clear();
            chatInput.grabKeyboardFocus();
            return;
        }
    }

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

// /rcmstats — dump the four diagnostic counter arrays added in commit 5b745ab
// for the tx-silent-and-orphan-cutoff debug session. Read-only; no audio-path
// impact. Output is local-only (System messages, never sent to the server).
bool ChatPanel::handleRcmStats()
{
    std::string ts;
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char buf[6] = {};
#ifdef _WIN32
        strftime(buf, sizeof(buf), "%H:%M", &tm_buf);
#else
        std::strftime(buf, sizeof(buf), "%H:%M", &tm_buf);
#endif
        ts = buf;
    }

    auto pushSystem = [&](std::string content) {
        ChatMessage m;
        m.type = ChatMessageType::System;
        m.content = std::move(content);
        m.timestamp = ts;
        addMessage(m);
    };

    pushSystem("--- rcm counter readout ---");

    {
        const juce::ScopedLock lock(processorRef.getClientLock());
        NJClient* client = processorRef.getClient();
        if (!client)
        {
            pushSystem("(no NJClient instance)");
            return true;
        }

        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "overflows: bq_drops=%llu rmuser_upd=%llu defer_del=%llu",
            (unsigned long long) client->GetBlockQueueDropCount(),
            (unsigned long long) client->GetRemoteUserUpdateOverflowCount(),
            (unsigned long long) client->GetDeferredDeleteOverflowCount());
        pushSystem(buf);

        int nonzero = 0;
        // Track which peer-slots have any non-zero counter so we can dump
        // their peer-level snapshot once at the end without duplicating per-channel.
        bool slot_seen[MAX_PEERS] = {};
        for (int slot = 0; slot < MAX_PEERS; ++slot)
        {
            for (int ch = 0; ch < MAX_USER_CHANNELS; ++ch)
            {
                const uint64_t pub = client->GetChannelInfoPublishCount(slot, ch);
                const uint64_t app = client->GetChannelInfoApplyCount(slot, ch);
                if (!pub && !app) continue;
                const long long delta = (long long) app - (long long) pub;
                std::snprintf(buf, sizeof(buf),
                    "slot %2d ch %2d: pub=%llu app=%llu delta=%+lld%s",
                    slot, ch,
                    (unsigned long long) pub,
                    (unsigned long long) app,
                    delta,
                    delta != 0 ? " <-- GAP" : "");
                pushSystem(buf);

                NJClient::MirrorChannelSnapshot cs{};
                if (client->GetMirrorChannelSnapshot(slot, ch, &cs))
                {
                    std::snprintf(buf, sizeof(buf),
                        "  vol=%.4f pan=%+.3f flags=0x%x outch=%d codec=0x%08x",
                        cs.volume, cs.pan, cs.flags, cs.out_chan_index, cs.codec_fourcc);
                    pushSystem(buf);
                    std::snprintf(buf, sizeof(buf),
                        "  present=%d muted=%d solo=%d ds=%c next=[%c %c] dump=%d curds=%.2f",
                        cs.present ? 1 : 0,
                        cs.muted   ? 1 : 0,
                        cs.solo    ? 1 : 0,
                        cs.ds_active        ? 'Y' : '-',
                        cs.next_ds0_active  ? 'Y' : '-',
                        cs.next_ds1_active  ? 'Y' : '-',
                        cs.dump_samples,
                        cs.curds_lenleft);
                    pushSystem(buf);
                }

                slot_seen[slot] = true;
                ++nonzero;
            }
        }
        if (nonzero == 0)
        {
            pushSystem("(no chinfo publishes/applies observed yet)");
        }
        else
        {
            for (int slot = 0; slot < MAX_PEERS; ++slot)
            {
                if (!slot_seen[slot]) continue;
                NJClient::MirrorPeerSnapshot ps{};
                if (!client->GetMirrorPeerSnapshot(slot, &ps)) continue;
                std::snprintf(buf, sizeof(buf),
                    "peer %2d: active=%d uidx=%d vol=%.4f pan=%+.3f muted=%d "
                    "subm=0x%x presm=0x%x mutm=0x%x solm=0x%x",
                    slot, ps.active ? 1 : 0, ps.user_index,
                    ps.volume, ps.pan, ps.muted ? 1 : 0,
                    (unsigned) ps.submask, (unsigned) ps.chanpresentmask,
                    (unsigned) ps.mutedmask, (unsigned) ps.solomask);
                pushSystem(buf);
            }
        }
    }

    return true;
}
