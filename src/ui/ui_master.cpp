/*
    NINJAM CLAP Plugin - ui_master.cpp
    Master panel rendering
*/

#include "ui_master.h"
#include "ui_meters.h"
#include "plugin/jamwide_plugin.h"
#include "core/njclient.h"
#include "imgui.h"

void ui_render_master_panel(jamwide::JamWidePlugin* plugin) {
    if (!plugin) return;

    if (!ImGui::CollapsingHeader("Master", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Indent();

    float master_vol = plugin->param_master_volume.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat("Master Volume", &master_vol, 0.0f, 2.0f, "%.2f")) {
        plugin->param_master_volume.store(master_vol, std::memory_order_relaxed);
    }

    ImGui::SameLine();

    bool master_mute = plugin->param_master_mute.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("M##master", &master_mute)) {
        plugin->param_master_mute.store(master_mute, std::memory_order_relaxed);
    }

    ImGui::SameLine();

    float master_vu_l = plugin->ui_snapshot.master_vu_left.load(
        std::memory_order_relaxed);
    float master_vu_r = plugin->ui_snapshot.master_vu_right.load(
        std::memory_order_relaxed);
    render_vu_meter("##master_vu", master_vu_l, master_vu_r);

    ImGui::Spacing();

    float metro_vol = plugin->param_metro_volume.load(std::memory_order_relaxed);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat("Metronome", &metro_vol, 0.0f, 2.0f, "%.2f")) {
        plugin->param_metro_volume.store(metro_vol, std::memory_order_relaxed);
    }

    ImGui::SameLine();

    bool metro_mute = plugin->param_metro_mute.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("M##metro", &metro_mute)) {
        plugin->param_metro_mute.store(metro_mute, std::memory_order_relaxed);
    }

    ImGui::Unindent();
}
