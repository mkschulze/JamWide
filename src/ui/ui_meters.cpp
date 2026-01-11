/*
    JamWide Plugin - ui_meters.cpp
    VU meter widget helpers
*/

#include "ui_meters.h"

#include <algorithm>

namespace {

float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

ImVec4 vu_color(float value) {
    if (value < 0.7f) {
        return ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    }
    if (value < 0.9f) {
        return ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
    }
    return ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
}

} // namespace

void render_vu_meter(const char* label, float left, float right) {
    const float level_left = clamp01(left);
    const float level_right = clamp01(right);
    const ImVec2 size(70.0f, 6.0f);

    ImGui::PushID(label ? label : "##vu_meter");
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, vu_color(level_left));
    ImGui::ProgressBar(level_left, size, "");
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, vu_color(level_right));
    ImGui::ProgressBar(level_right, size, "");
    ImGui::PopStyleColor();

    ImGui::EndGroup();
    ImGui::PopID();
}
