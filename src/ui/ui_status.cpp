/*
    NINJAM CLAP Plugin - ui_status.cpp
    Status bar rendering
*/

#include "ui_status.h"
#include "build_number.h"
#include "plugin/ninjam_plugin.h"
#include "core/njclient.h"
#include "imgui.h"

#include <cstdio>

namespace {

float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

} // namespace

void ui_render_status_bar(ninjam::NinjamPlugin* plugin) {
    if (!plugin) return;

    const auto& state = plugin->ui_state;
    float status_line_y = ImGui::GetCursorPosY();

    ImVec4 color;
    const char* status_text;

    switch (state.status) {
        case NJClient::NJC_STATUS_OK:
            color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
            status_text = "Connected";
            break;
        case NJClient::NJC_STATUS_PRECONNECT:
            color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
            status_text = "Connecting...";
            break;
        default:
            color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            status_text = "Disconnected";
            break;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::Bullet();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%s", status_text);

    if (state.status == NJClient::NJC_STATUS_OK) {
        ImGui::SameLine();
        ImGui::Text("| %.1f BPM | %d BPI | Beat %d",
                    state.bpm,
                    state.bpi,
                    state.beat_position + 1);

        float progress = 0.0f;
        if (state.interval_length > 0) {
            progress = static_cast<float>(state.interval_position) /
                       static_cast<float>(state.interval_length);
        }
        progress = clamp01(progress);

        ImGui::SameLine();
        ImGui::ProgressBar(progress, ImVec2(100.0f, 0.0f), "");
    }

    float after_status_y = ImGui::GetCursorPosY();
    char build_label[16];
    snprintf(build_label, sizeof(build_label), "r%d", NINJAM_BUILD_NUMBER);
    ImVec2 build_size = ImGui::CalcTextSize(build_label);
    float right_x = ImGui::GetWindowContentRegionMax().x;

    ImGui::SetCursorPosY(status_line_y);
    ImGui::SetCursorPosX(right_x - build_size.x);
    ImGui::TextDisabled("%s", build_label);

    if (ImGui::GetCursorPosY() < after_status_y) {
        ImGui::SetCursorPosY(after_status_y);
    }
}
