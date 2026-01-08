/*
    NINJAM CLAP Plugin - ui_main.cpp
    Main UI render function - Phase 4 panel routing
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#include "ui_main.h"
#include "plugin/ninjam_plugin.h"
#include "core/njclient.h"
#include "ui_status.h"
#include "ui_connection.h"
#include "ui_local.h"
#include "ui_master.h"
#include "ui_remote.h"
#include "ui_server_browser.h"
#include "imgui.h"

using namespace ninjam;

//------------------------------------------------------------------------------
// UI Render Frame - Stub Implementation
//------------------------------------------------------------------------------

void ui_render_frame(NinjamPlugin* plugin) {
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
                plugin->ui_state.status = e.status;
                plugin->ui_state.connection_error = e.error_msg;
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

    // Check for license prompt (dedicated slot)
    if (plugin->license_pending.load(std::memory_order_acquire)) {
        plugin->ui_state.show_license_dialog = true;
        {
            std::lock_guard<std::mutex> lock(plugin->license_mutex);
            plugin->ui_state.license_text = plugin->license_text;
        }
    }

    if (plugin->ui_state.status == NJClient::NJC_STATUS_OK) {
        plugin->ui_state.bpm =
            plugin->ui_snapshot.bpm.load(std::memory_order_relaxed);
        plugin->ui_state.bpi =
            plugin->ui_snapshot.bpi.load(std::memory_order_relaxed);
        plugin->ui_state.interval_position =
            plugin->ui_snapshot.interval_position.load(std::memory_order_relaxed);
        plugin->ui_state.interval_length =
            plugin->ui_snapshot.interval_length.load(std::memory_order_relaxed);
        plugin->ui_state.beat_position =
            plugin->ui_snapshot.beat_position.load(std::memory_order_relaxed);
    }

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

    ImGui::Begin("NINJAM", nullptr, flags);

    ui_render_status_bar(plugin);
    ImGui::Separator();
    ui_render_connection_panel(plugin);
    ImGui::Separator();
    ui_render_server_browser(plugin);
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
                plugin->license_response.store(1, std::memory_order_release);
                plugin->license_cv.notify_one();
                plugin->ui_state.show_license_dialog = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Reject", ImVec2(120, 0))) {
                plugin->license_response.store(-1, std::memory_order_release);
                plugin->license_cv.notify_one();
                plugin->ui_state.show_license_dialog = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}
