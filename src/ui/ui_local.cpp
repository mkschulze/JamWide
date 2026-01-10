/*
    NINJAM CLAP Plugin - ui_local.cpp
    Local channel panel rendering
*/

#include "ui_local.h"
#include "ui_meters.h"
#include "ui_latency_guide.h"
#include "ui_util.h"
#include "threading/ui_command.h"
#include "plugin/jamwide_plugin.h"
#include "core/njclient.h"
#include "imgui.h"

namespace {

const char* const kBitrateLabels[] = {
    "32 kbps", "64 kbps", "96 kbps", "128 kbps", "192 kbps", "256 kbps"
};
const int kBitrateValues[] = { 32, 64, 96, 128, 192, 256 };

int clamp_bitrate_index(int index) {
    const int max_index = static_cast<int>(sizeof(kBitrateValues) /
                                           sizeof(kBitrateValues[0])) - 1;
    if (index < 0) return 0;
    if (index > max_index) return max_index;
    return index;
}

} // namespace

void ui_render_local_channel(jamwide::JamWidePlugin* plugin) {
    if (!plugin) return;

    auto& state = plugin->ui_state;
    state.local_bitrate_index = clamp_bitrate_index(state.local_bitrate_index);

    if (!ImGui::CollapsingHeader("Local Channel", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Indent();

    if (ImGui::InputText("Name##local", state.local_name_input,
                         sizeof(state.local_name_input))) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            jamwide::SetLocalChannelInfoCommand cmd;
            cmd.channel = 0;
            cmd.name = state.local_name_input;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("Bitrate##local", &state.local_bitrate_index,
                     kBitrateLabels,
                     static_cast<int>(sizeof(kBitrateLabels) /
                                      sizeof(kBitrateLabels[0])))) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            const int bitrate = kBitrateValues[state.local_bitrate_index];
            jamwide::SetLocalChannelInfoCommand cmd;
            cmd.channel = 0;
            cmd.name = state.local_name_input;
            cmd.set_bitrate = true;
            cmd.bitrate = bitrate;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    if (ImGui::Checkbox("Transmit##local", &state.local_transmit)) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            jamwide::SetLocalChannelInfoCommand cmd;
            cmd.channel = 0;
            cmd.name = state.local_name_input;
            cmd.set_transmit = true;
            cmd.transmit = state.local_transmit;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Volume##local", &state.local_volume,
                           0.0f, 2.0f, "%.2f")) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = 0;
            cmd.set_volume = true;
            cmd.volume = state.local_volume;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::SliderFloat("Pan##local", &state.local_pan,
                           -1.0f, 1.0f, "%.2f")) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = 0;
            cmd.set_pan = true;
            cmd.pan = state.local_pan;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    if (ImGui::Checkbox("M##local_mute", &state.local_mute)) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = 0;
            cmd.set_mute = true;
            cmd.mute = state.local_mute;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
    }

    ImGui::SameLine();

    if (ImGui::Checkbox("S##local_solo", &state.local_solo)) {
        if (state.status == NJClient::NJC_STATUS_OK) {
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = 0;
            cmd.set_solo = true;
            cmd.solo = state.local_solo;
            plugin->cmd_queue.try_push(std::move(cmd));
        }
        ui_update_solo_state(plugin);
    }

    ImGui::SameLine();

    float vu_left = plugin->ui_snapshot.local_vu_left.load(
        std::memory_order_relaxed);
    float vu_right = plugin->ui_snapshot.local_vu_right.load(
        std::memory_order_relaxed);
    render_vu_meter("##local_vu", vu_left, vu_right);

    if (state.status == NJClient::NJC_STATUS_OK) {
        ImGui::Spacing();
        ImGui::Checkbox("Timing Guide##toggle", &state.show_latency_guide);
        if (state.show_latency_guide) {
            ui_render_latency_guide(plugin);
        }
    }

    ImGui::Unindent();
}
