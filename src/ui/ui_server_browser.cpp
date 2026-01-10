/*
    NINJAM CLAP Plugin - ui_server_browser.cpp
    Public server list browser panel
*/

#include "ui_server_browser.h"
#include "threading/ui_command.h"
#include "plugin/jamwide_plugin.h"
#include "imgui.h"

#include <cstdio>

namespace {

void format_server_address(const ServerListEntry& entry,
                           char* buffer,
                           size_t size) {
    if (!buffer || size == 0) return;
    if (entry.port > 0) {
        std::snprintf(buffer, size, "%s:%d", entry.host.c_str(), entry.port);
    } else {
        std::snprintf(buffer, size, "%s", entry.host.c_str());
    }
}

} // namespace

void ui_render_server_browser(jamwide::JamWidePlugin* plugin) {
    if (!plugin) return;

    auto& state = plugin->ui_state;
    if (!ImGui::CollapsingHeader("Server Browser", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Indent();

    ImGui::InputText("List URL",
                     state.server_list_url,
                     sizeof(state.server_list_url));

    if (ImGui::Button("Refresh")) {
        jamwide::RequestServerListCommand cmd;
        cmd.url = state.server_list_url;
        if (!plugin->cmd_queue.try_push(std::move(cmd))) {
            state.server_list_error = "Server list request queue full";
        } else {
            state.server_list_loading = true;
            state.server_list_error.clear();
        }
    }

    if (state.server_list_loading) {
        ImGui::SameLine();
        ImGui::TextDisabled("Loading...");
    }

    if (!state.server_list_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "Error: %s", state.server_list_error.c_str());
    }

    if (state.server_list.empty()) {
        ImGui::TextDisabled("No server list loaded");
        ImGui::Unindent();
        return;
    }

    if (ImGui::BeginTable("ServerListTable", 5,
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerH |
                          ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Users");
        ImGui::TableSetupColumn("Topic");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();

        int idx = 0;
        for (const auto& entry : state.server_list) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(entry.name.empty()
                                       ? entry.host.c_str()
                                       : entry.name.c_str());

            ImGui::TableSetColumnIndex(1);
            char addr[256];
            format_server_address(entry, addr, sizeof(addr));
            ImGui::TextUnformatted(addr);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", entry.users);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(entry.topic.c_str());

            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(idx++);
            if (ImGui::SmallButton("Use")) {
                format_server_address(entry,
                                      state.server_input,
                                      sizeof(state.server_input));
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Unindent();
}
