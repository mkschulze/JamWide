#pragma once
#include <string>
#include <vector>
#include "ui/ui_state.h"  // For ChatMessage, ChatMessageType

class ChatMessageModel
{
public:
    static constexpr int kMaxMessages = 500;

    void addMessage(ChatMessage&& msg)
    {
        if (messages_.size() >= kMaxMessages)
            messages_.erase(messages_.begin());
        messages_.push_back(std::move(msg));
    }

    const std::vector<ChatMessage>& getMessages() const { return messages_; }
    void clear() { messages_.clear(); }
    int size() const { return static_cast<int>(messages_.size()); }

private:
    std::vector<ChatMessage> messages_;
};
