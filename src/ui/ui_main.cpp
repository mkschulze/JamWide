/*
    NINJAM CLAP Plugin - ui_main.cpp
    Main UI render function - Phase 3 stub implementation
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#include "ui_main.h"
#include "plugin/ninjam_plugin.h"
#include "core/njclient.h"
#include "imgui.h"

using namespace ninjam;

//------------------------------------------------------------------------------
// UI Render Frame - Stub Implementation
//------------------------------------------------------------------------------

void ui_render_frame(NinjamPlugin* plugin) {
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

    // Update status from cached atomics (lock-free reads)
    if (plugin->client) {
        plugin->ui_state.status =
            plugin->client->cached_status.load(std::memory_order_acquire);

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
    }

    // Create main window (fills entire area)
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("NINJAM", nullptr, flags);

    // === Status Bar ===
    {
        ImVec4 color;
        const char* status_text;

        int status = plugin->ui_state.status;
        if (status == NJClient::NJC_STATUS_OK) {
            color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
            status_text = "Connected";
        } else if (status == NJClient::NJC_STATUS_PRECONNECT) {
            color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
            status_text = "Connecting...";
        } else {
            color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            status_text = "Disconnected";
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Bullet();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("%s", status_text);

        if (status == NJClient::NJC_STATUS_OK) {
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
            ImGui::Text("%.1f BPM | %d BPI | Beat %d",
                        plugin->ui_state.bpm,
                        plugin->ui_state.bpi,
                        plugin->ui_state.beat_position + 1);
        }
    }

    ImGui::Separator();

    // === Connection Panel ===
    if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Server", plugin->ui_state.server_input,
                         sizeof(plugin->ui_state.server_input));
        ImGui::InputText("Username", plugin->ui_state.username_input,
                         sizeof(plugin->ui_state.username_input));
        ImGui::InputText("Password", plugin->ui_state.password_input,
                         sizeof(plugin->ui_state.password_input),
                         ImGuiInputTextFlags_Password);

        int status = plugin->ui_state.status;
        bool is_connected = (status == NJClient::NJC_STATUS_OK ||
                             status == NJClient::NJC_STATUS_PRECONNECT);

        if (!is_connected) {
            if (ImGui::Button("Connect")) {
                std::lock_guard<std::mutex> lock(plugin->state_mutex);
                plugin->server = plugin->ui_state.server_input;
                plugin->username = plugin->ui_state.username_input;
                plugin->password = plugin->ui_state.password_input;

                if (plugin->client) {
                    plugin->client->Connect(
                        plugin->server.c_str(),
                        plugin->username.c_str(),
                        plugin->password.c_str()
                    );
                }
            }
        } else {
            if (ImGui::Button("Disconnect")) {
                std::lock_guard<std::mutex> lock(plugin->state_mutex);
                if (plugin->client) {
                    plugin->client->Disconnect();
                }
            }
        }

        if (!plugin->ui_state.connection_error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "Error: %s", plugin->ui_state.connection_error.c_str());
        }
    }

    ImGui::Separator();

    // === Master Controls ===
    if (ImGui::CollapsingHeader("Master", ImGuiTreeNodeFlags_DefaultOpen)) {
        float master_vol = plugin->param_master_volume.load(std::memory_order_relaxed);
        if (ImGui::SliderFloat("Volume##master", &master_vol, 0.0f, 2.0f, "%.2f")) {
            plugin->param_master_volume.store(master_vol, std::memory_order_relaxed);
        }

        bool master_mute = plugin->param_master_mute.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Mute##master", &master_mute)) {
            plugin->param_master_mute.store(master_mute, std::memory_order_relaxed);
        }

        ImGui::Separator();

        float metro_vol = plugin->param_metro_volume.load(std::memory_order_relaxed);
        if (ImGui::SliderFloat("Metronome##metro", &metro_vol, 0.0f, 2.0f, "%.2f")) {
            plugin->param_metro_volume.store(metro_vol, std::memory_order_relaxed);
        }

        bool metro_mute = plugin->param_metro_mute.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Mute##metro", &metro_mute)) {
            plugin->param_metro_mute.store(metro_mute, std::memory_order_relaxed);
        }
    }

    ImGui::Separator();

    // === Local Channel ===
    if (ImGui::CollapsingHeader("Local Channel", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Name##local", plugin->ui_state.local_name_input,
                         sizeof(plugin->ui_state.local_name_input));

        ImGui::Checkbox("Transmit", &plugin->ui_state.local_transmit);

        // VU meters
        float vu_left = plugin->ui_snapshot.local_vu_left.load(std::memory_order_relaxed);
        float vu_right = plugin->ui_snapshot.local_vu_right.load(std::memory_order_relaxed);

        ImGui::ProgressBar(vu_left, ImVec2(-1, 8), "");
        ImGui::ProgressBar(vu_right, ImVec2(-1, 8), "");
    }

    ImGui::Separator();

    // === Remote Users (placeholder) ===
    if (ImGui::CollapsingHeader("Remote Users")) {
        if (plugin->ui_state.status == NJClient::NJC_STATUS_OK) {
            ImGui::Text("(Remote user list will be implemented in Phase 4)");
        } else {
            ImGui::TextDisabled("Not connected");
        }
    }

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
