/*
    NINJAM CLAP Plugin - ui_chat.cpp
    Chat UI widget
*/

#include "ui_chat.h"
#include "plugin/ninjam_plugin.h"
#include "threading/ui_command.h"
#include "core/njclient.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <string>

namespace {

ImVec4 color_for_type(ChatMessageType type) {
    switch (type) {
        case ChatMessageType::PrivateMessage:
            return ImVec4(0.4f, 0.9f, 0.9f, 1.0f);
        case ChatMessageType::Topic:
            return ImVec4(0.9f, 0.8f, 0.2f, 1.0f);
        case ChatMessageType::Join:
            return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
        case ChatMessageType::Part:
            return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        case ChatMessageType::Action:
            return ImVec4(0.9f, 0.5f, 0.9f, 1.0f);
        case ChatMessageType::System:
            return ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        case ChatMessageType::Message:
        default:
            return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    }
}

std::string trim_left(std::string value) {
    std::size_t pos = 0;
    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos]))) {
        ++pos;
    }
    return value.substr(pos);
}

bool parse_chat_input(const char* input, ninjam::SendChatCommand& cmd) {
    if (!input || !*input) return false;
    std::string text = trim_left(input);
    if (text.empty()) return false;

    cmd.type = "MSG";
    cmd.target.clear();
    cmd.text = text;

    if (text[0] != '/') {
        return true;
    }

    if (text.rfind("/me ", 0) == 0) {
        cmd.type = "MSG";
        cmd.text = text;
        return true;
    }
    if (text.rfind("/topic ", 0) == 0) {
        cmd.type = "TOPIC";
        cmd.text = trim_left(text.substr(7));
        return !cmd.text.empty();
    }
    if (text.rfind("/msg ", 0) == 0) {
        std::string rest = trim_left(text.substr(5));
        std::size_t space = rest.find(' ');
        if (space == std::string::npos) {
            return false;
        }
        cmd.type = "PRIVMSG";
        cmd.target = rest.substr(0, space);
        cmd.text = trim_left(rest.substr(space + 1));
        return !cmd.target.empty() && !cmd.text.empty();
    }

    cmd.type = "MSG";
    cmd.text = text;
    return true;
}

std::string format_line(const ChatMessage& message) {
    const std::string prefix = message.timestamp.empty()
        ? ""
        : (message.timestamp + " ");
    switch (message.type) {
        case ChatMessageType::Action:
            return prefix + "* " + message.sender + " " + message.content;
        case ChatMessageType::Join:
        case ChatMessageType::Part:
        case ChatMessageType::Topic:
        case ChatMessageType::System:
            return prefix + "*** " + message.content;
        case ChatMessageType::PrivateMessage:
            return prefix + "[PM from " + message.sender + "] " + message.content;
        case ChatMessageType::Message:
        default:
            return prefix + "<" + message.sender + "> " + message.content;
    }
}

std::string make_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &now_time);
#else
    localtime_r(&now_time, &tm_buf);
#endif
    char buf[6] = {};
    if (std::strftime(buf, sizeof(buf), "%H:%M", &tm_buf)) {
        return std::string(buf);
    }
    return {};
}

} // namespace

void ui_render_chat(ninjam::NinjamPlugin* plugin) {
    if (!plugin) return;

    auto& state = plugin->ui_state;
    if (!state.show_chat) {
        return;
    }

    if (!ImGui::CollapsingHeader("Chat", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Indent();

    if (!state.server_topic.empty()) {
        ImGui::Text("Topic: %s", state.server_topic.c_str());
    }

    ImGui::BeginChild("##chat_history", ImVec2(0.0f, 160.0f), true);

    const int size = UiState::kChatHistorySize;
    const int start = (state.chat_history_index - state.chat_history_count + size) % size;
    for (int i = 0; i < state.chat_history_count; ++i) {
        const int idx = (start + i) % size;
        const ChatMessage& msg = state.chat_history[idx];
        ImGui::PushStyleColor(ImGuiCol_Text, color_for_type(msg.type));
        ImGui::TextWrapped("%s", format_line(msg).c_str());
        ImGui::PopStyleColor();
    }

    if (state.chat_scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
        state.chat_scroll_to_bottom = false;
    }

    ImGui::EndChild();

    bool send = false;
    ImGui::SetNextItemWidth(-40.0f);
    if (ImGui::InputText("##chat_input", state.chat_input,
                         sizeof(state.chat_input),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        send = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Send##chat")) {
        send = true;
    }

    if (send) {
        if (state.status != NJClient::NJC_STATUS_OK) {
            ChatMessage msg;
            msg.type = ChatMessageType::System;
            msg.content = "error: not connected to a server.";
            msg.timestamp = make_timestamp();
            state.chat_history[state.chat_history_index] = std::move(msg);
            state.chat_history_index =
                (state.chat_history_index + 1) % UiState::kChatHistorySize;
            if (state.chat_history_count < UiState::kChatHistorySize) {
                state.chat_history_count++;
            }
            state.chat_scroll_to_bottom = true;
        } else {
            ninjam::SendChatCommand cmd;
            if (parse_chat_input(state.chat_input, cmd)) {
                plugin->cmd_queue.try_push(std::move(cmd));
            } else {
                ChatMessage msg;
                msg.type = ChatMessageType::System;
                msg.content = "error: invalid command.";
                msg.timestamp = make_timestamp();
                state.chat_history[state.chat_history_index] = std::move(msg);
                state.chat_history_index =
                    (state.chat_history_index + 1) % UiState::kChatHistorySize;
                if (state.chat_history_count < UiState::kChatHistorySize) {
                    state.chat_history_count++;
                }
                state.chat_scroll_to_bottom = true;
            }
        }
        state.chat_input[0] = '\0';
    }

    ImGui::Unindent();
}
