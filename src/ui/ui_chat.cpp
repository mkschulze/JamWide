/*
    JamWide Plugin - ui_chat.cpp
    Chat UI widget
*/

#include "ui_chat.h"
#include "plugin/jamwide_plugin.h"
#include "threading/ui_command.h"
#include "core/njclient.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

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

bool parse_chat_input(const char* input, jamwide::SendChatCommand& cmd) {
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

void push_system_line(UiState& state, std::string content, const std::string& timestamp) {
    ChatMessage msg;
    msg.type = ChatMessageType::System;
    msg.content = std::move(content);
    msg.timestamp = timestamp;
    state.chat_history[state.chat_history_index] = std::move(msg);
    state.chat_history_index =
        (state.chat_history_index + 1) % UiState::kChatHistorySize;
    if (state.chat_history_count < UiState::kChatHistorySize) {
        state.chat_history_count++;
    }
}

// /rcmstats — dump the four diagnostic counter arrays added in commit 5b745ab
// for the tx-silent-and-orphan-cutoff debug session. Read-only; no audio-path
// impact. Output is local-only (System messages, never sent to the server).
bool handle_rcmstats(jamwide::JamWidePlugin* plugin, UiState& state) {
    const std::string ts = [] {
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
        return std::string(buf);
    }();

    std::vector<std::string> lines;
    lines.reserve(16);
    lines.emplace_back("--- rcm counter readout ---");

    {
        std::unique_lock<std::mutex> client_lock(plugin->client_mutex);
        NJClient* client = plugin->client.get();
        if (!client) {
            lines.emplace_back("(no NJClient instance)");
        } else {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "overflows: bq_drops=%llu rmuser_upd=%llu defer_del=%llu",
                (unsigned long long) client->GetBlockQueueDropCount(),
                (unsigned long long) client->GetRemoteUserUpdateOverflowCount(),
                (unsigned long long) client->GetDeferredDeleteOverflowCount());
            lines.emplace_back(buf);

            int nonzero = 0;
            for (int slot = 0; slot < MAX_PEERS; ++slot) {
                for (int ch = 0; ch < MAX_USER_CHANNELS; ++ch) {
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
                    lines.emplace_back(buf);
                    ++nonzero;
                }
            }
            if (nonzero == 0) {
                lines.emplace_back("(no chinfo publishes/applies observed yet)");
            }
        }
    }

    for (auto& line : lines) {
        push_system_line(state, std::move(line), ts);
    }
    state.chat_scroll_to_bottom = true;
    return true;
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
#ifdef _WIN32
    if (strftime(buf, sizeof(buf), "%H:%M", &tm_buf)) {  // WDL redefines strftime to strftimeUTF8 on Windows
#else
    if (std::strftime(buf, sizeof(buf), "%H:%M", &tm_buf)) {
#endif
        return std::string(buf);
    }
    return {};
}

} // namespace

void ui_render_chat(jamwide::JamWidePlugin* plugin) {
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
    ImGui::SetNextItemWidth(-50.0f);  // Leave room for Send button + padding
    
    // Refocus input field after sending a message
    if (state.chat_refocus_input) {
        ImGui::SetKeyboardFocusHere(0);
        state.chat_refocus_input = false;
    }
    
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
        // Local-only commands handled before the connected-state check so
        // they work in any state (rcm counter readout works while connected
        // and immediately after a Disconnect, when state.status flips).
        {
            std::string text = trim_left(state.chat_input);
            if (text == "/rcmstats" || text == "/rcm") {
                handle_rcmstats(plugin, state);
                state.chat_input[0] = '\0';
                state.chat_refocus_input = true;
                ImGui::Unindent();
                return;
            }
        }
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
            jamwide::SendChatCommand cmd;
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
        // Request focus on input field for next frame
        state.chat_refocus_input = true;
    }

    ImGui::Unindent();
}
