/*
    JamWide Plugin - ui_connection.cpp
    Connection panel rendering
*/

#include "ui_connection.h"
#include "threading/ui_command.h"
#include "plugin/jamwide_plugin.h"
#include "core/njclient.h"
#include "imgui.h"
#include <cstdio>

static FILE* get_log_file() {
    static FILE* f = nullptr;
    if (!f) {
        f = fopen("/tmp/jamwide.log", "a");
        if (f) {
            fprintf(f, "\n=== JamWide Session Started ===\n");
            fflush(f);
        }
    }
    return f;
}

#define NLOG(...) do { FILE* f = get_log_file(); if (f) { fprintf(f, __VA_ARGS__); fflush(f); } } while(0)

void ui_render_connection_panel(jamwide::JamWidePlugin* plugin) {
    if (!plugin) return;

    auto& state = plugin->ui_state;
    if (!ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Indent();
    ImGui::InputText("Server", state.server_input, sizeof(state.server_input));
    ImGui::InputText("Username", state.username_input, sizeof(state.username_input));
    ImGui::InputText("Password", state.password_input, sizeof(state.password_input),
                     ImGuiInputTextFlags_Password);

    const bool is_connected =
        (state.status == NJClient::NJC_STATUS_OK ||
         state.status == NJClient::NJC_STATUS_PRECONNECT);

    // Show current status for debugging
    ImGui::TextDisabled("Status: %d", state.status);

    if (!is_connected) {
        if (ImGui::Button("Connect")) {
            NLOG("[UI] Connect button pressed! server='%s' user='%s'\n",
                    state.server_input, state.username_input);

            jamwide::ConnectCommand cmd;
            cmd.server = state.server_input;
            cmd.username = state.username_input;
            cmd.password = state.password_input;
            if (!plugin->cmd_queue.try_push(std::move(cmd))) {
                state.connection_error = "Connect request queue full";
            } else {
                state.connection_error.clear();
            }
        }
    } else {
        if (ImGui::Button("Disconnect")) {
            jamwide::DisconnectCommand cmd;
            if (!plugin->cmd_queue.try_push(std::move(cmd))) {
                state.connection_error = "Disconnect request queue full";
            }
        }
    }

    if (!state.connection_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "Error: %s", state.connection_error.c_str());
    }

    ImGui::Unindent();
}
