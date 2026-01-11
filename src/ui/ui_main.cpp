/*
    JamWide Plugin - ui_main.cpp
    Main UI render function - Phase 4 panel routing
    
    Copyright (C) 2024 JamWide Contributors
    Licensed under GPLv2+
*/

#include "ui_main.h"
#include "plugin/jamwide_plugin.h"
#include "core/njclient.h"
#include "ui_status.h"
#include "ui_connection.h"
#include "ui_chat.h"
#include "ui_local.h"
#include "ui_master.h"
#include "ui_remote.h"
#include "ui_server_browser.h"
#include "debug/logging.h"
#include "imgui.h"
#include <chrono>
#include <ctime>

using namespace jamwide;

//------------------------------------------------------------------------------
// UI Render Frame - Stub Implementation
//------------------------------------------------------------------------------

void ui_render_frame(JamWidePlugin* plugin) {
    if (!plugin) {
        return;
    }

    // Guard: Ensure we have a valid ImGui context
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        return;
    }

    // Drain event queue (lock-free)
    plugin->ui_queue.drain([&](UiEvent&& event) {
        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, StatusChangedEvent>) {
                const int prev_status = plugin->ui_state.status;
                plugin->ui_state.status = e.status;
                plugin->ui_state.connection_error = e.error_msg;
                if (prev_status == NJClient::NJC_STATUS_OK &&
                    e.status != NJClient::NJC_STATUS_OK) {
                    plugin->ui_state.latency_history.fill(0.0f);
                    plugin->ui_state.latency_history_index = 0;
                    plugin->ui_state.latency_history_count = 0;
                    plugin->ui_state.chat_history.fill(ChatMessage{});
                    plugin->ui_state.chat_history_index = 0;
                    plugin->ui_state.chat_history_count = 0;
                    plugin->ui_state.chat_scroll_to_bottom = false;
                }
            }
            else if constexpr (std::is_same_v<T, UserInfoChangedEvent>) {
                plugin->ui_state.users_dirty = true;
            }
            else if constexpr (std::is_same_v<T, TopicChangedEvent>) {
                plugin->ui_state.server_topic = e.topic;
            }
            else if constexpr (std::is_same_v<T, ChatMessageEvent>) {
                // ChatMessageEvent ignored (no chat in MVP)
                (void)e;
            }
            else if constexpr (std::is_same_v<T, ServerListEvent>) {
                plugin->ui_state.server_list = std::move(e.servers);
                plugin->ui_state.server_list_error = e.error;
                plugin->ui_state.server_list_loading = false;
            }
        }, std::move(event));
    });

    auto make_timestamp = []() -> std::string {
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
    };

    plugin->chat_queue.drain([&](ChatMessage&& msg) {
        msg.timestamp = make_timestamp();
        plugin->ui_state.chat_history[plugin->ui_state.chat_history_index] =
            std::move(msg);
        plugin->ui_state.chat_history_index =
            (plugin->ui_state.chat_history_index + 1) % UiState::kChatHistorySize;
        if (plugin->ui_state.chat_history_count < UiState::kChatHistorySize) {
            plugin->ui_state.chat_history_count++;
        }
        plugin->ui_state.chat_scroll_to_bottom = true;
    });

    // Check for license prompt (dedicated slot)
    if (plugin->license_pending.load(std::memory_order_acquire)) {
        plugin->ui_state.show_license_dialog = true;
        {
            std::lock_guard<std::mutex> lock(plugin->license_mutex);
            plugin->ui_state.license_text = plugin->license_text;
        }
    }

    if (plugin->ui_state.status == NJClient::NJC_STATUS_OK) {
        const float new_bpm =
            plugin->ui_snapshot.bpm.load(std::memory_order_relaxed);
        const int new_bpi =
            plugin->ui_snapshot.bpi.load(std::memory_order_relaxed);

        if ((plugin->ui_state.bpm > 0.0f && new_bpm > 0.0f &&
             std::fabs(plugin->ui_state.bpm - new_bpm) > 0.001f) ||
            (plugin->ui_state.bpi > 0 && new_bpi > 0 &&
             plugin->ui_state.bpi != new_bpi)) {
            plugin->ui_state.latency_history.fill(0.0f);
            plugin->ui_state.latency_history_index = 0;
            plugin->ui_state.latency_history_count = 0;
        }

        plugin->ui_state.bpm = new_bpm;
        plugin->ui_state.bpi = new_bpi;
        plugin->ui_state.interval_position =
            plugin->ui_snapshot.interval_position.load(std::memory_order_relaxed);
        plugin->ui_state.interval_length =
            plugin->ui_snapshot.interval_length.load(std::memory_order_relaxed);
        plugin->ui_state.beat_position =
            plugin->ui_snapshot.beat_position.load(std::memory_order_relaxed);
    }

    float threshold = plugin->ui_state.transient_threshold;
    if (threshold < 0.0f) {
        threshold = 0.0f;
    } else if (threshold > 1.0f) {
        threshold = 1.0f;
    }
    plugin->ui_snapshot.transient_threshold.store(
        threshold, std::memory_order_relaxed);

    // Create main window (fills entire area)
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;  // Viewport not ready
    }
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("JamWide", nullptr, flags);

    ui_render_status_bar(plugin);
    ImGui::Separator();
    ui_render_connection_panel(plugin);
    ImGui::Separator();
    ui_render_server_browser(plugin);
    ImGui::Separator();
    ui_render_chat(plugin);
    ImGui::Separator();
    ui_render_master_panel(plugin);
    ImGui::Separator();
    ui_render_local_channel(plugin);
    ImGui::Separator();
    ui_render_remote_channels(plugin);

    ImGui::End();

    // === License Dialog (modal) ===
    if (plugin->ui_state.show_license_dialog) {
        ImGui::OpenPopup("Server License");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("Server License", nullptr,
                                   ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", plugin->ui_state.license_text.c_str());

            ImGui::Separator();

            if (ImGui::Button("Accept", ImVec2(120, 0))) {
                NLOG("[UI] License accepted\n");
                plugin->license_response.store(1, std::memory_order_release);
                plugin->license_cv.notify_one();
                plugin->ui_state.show_license_dialog = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Reject", ImVec2(120, 0))) {
                NLOG("[UI] License rejected\n");
                plugin->license_response.store(-1, std::memory_order_release);
                plugin->license_cv.notify_one();
                plugin->ui_state.show_license_dialog = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}
