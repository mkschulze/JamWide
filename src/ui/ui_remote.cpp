/*
    NINJAM CLAP Plugin - ui_remote.cpp
    Remote channels panel rendering
*/

#include "ui_remote.h"
#include "ui_meters.h"
#include "ui_util.h"
#include "threading/ui_command.h"
#include "plugin/ninjam_plugin.h"
#include "core/njclient.h"
#include "imgui.h"

void ui_render_remote_channels(ninjam::NinjamPlugin* plugin) {
    if (!plugin) return;

    // Make a local copy of remote_users under lock to avoid race with run thread
    std::vector<RemoteUser> users_copy;
    int status;
    {
        std::lock_guard<std::mutex> lock(plugin->state_mutex);
        status = plugin->ui_state.status;
        users_copy = plugin->ui_state.remote_users;
    }
    
    if (!ImGui::CollapsingHeader("Remote Users", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Indent();

    if (status != NJClient::NJC_STATUS_OK) {
        ImGui::TextDisabled("Not connected");
        ImGui::Unindent();
        return;
    }

    if (users_copy.empty()) {
        ImGui::TextDisabled("No remote users connected");
        ImGui::Unindent();
        return;
    }

    for (size_t u = 0; u < users_copy.size(); ++u) {
        auto& user = users_copy[u];
        ImGui::PushID(static_cast<int>(u));

        const bool user_open = ImGui::TreeNodeEx(
            user.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        ImGui::SameLine();
        if (ImGui::Checkbox("M##user", &user.mute)) {
            ninjam::SetUserStateCommand cmd;
            cmd.user_index = static_cast<int>(u);
            cmd.set_mute = true;
            cmd.mute = user.mute;
            plugin->cmd_queue.try_push(std::move(cmd));
        }

        if (user_open) {
            ImGui::Indent();

            for (size_t c = 0; c < user.channels.size(); ++c) {
                auto& channel = user.channels[c];
                ImGui::PushID(static_cast<int>(channel.channel_index));

                if (ImGui::Checkbox("##sub", &channel.subscribed)) {
                    ninjam::SetUserChannelStateCommand cmd;
                    cmd.user_index = static_cast<int>(u);
                    cmd.channel_index = channel.channel_index;
                    cmd.set_sub = true;
                    cmd.subscribed = channel.subscribed;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                ImGui::SameLine();
                ImGui::Text("%s", channel.name.c_str());

                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::SliderFloat("##vol", &channel.volume,
                                       0.0f, 2.0f, "%.2f")) {
                    ninjam::SetUserChannelStateCommand cmd;
                    cmd.user_index = static_cast<int>(u);
                    cmd.channel_index = channel.channel_index;
                    cmd.set_vol = true;
                    cmd.volume = channel.volume;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::SliderFloat("##pan", &channel.pan,
                                       -1.0f, 1.0f, "%.2f")) {
                    ninjam::SetUserChannelStateCommand cmd;
                    cmd.user_index = static_cast<int>(u);
                    cmd.channel_index = channel.channel_index;
                    cmd.set_pan = true;
                    cmd.pan = channel.pan;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                ImGui::SameLine();
                if (ImGui::Checkbox("M##chan_mute", &channel.mute)) {
                    ninjam::SetUserChannelStateCommand cmd;
                    cmd.user_index = static_cast<int>(u);
                    cmd.channel_index = channel.channel_index;
                    cmd.set_mute = true;
                    cmd.mute = channel.mute;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                ImGui::SameLine();
                if (ImGui::Checkbox("S##chan_solo", &channel.solo)) {
                    ninjam::SetUserChannelStateCommand cmd;
                    cmd.user_index = static_cast<int>(u);
                    cmd.channel_index = channel.channel_index;
                    cmd.set_solo = true;
                    cmd.solo = channel.solo;
                    plugin->cmd_queue.try_push(std::move(cmd));
                    ui_update_solo_state(plugin);
                }

                ImGui::SameLine();
                render_vu_meter("##chan_vu", channel.vu_left, channel.vu_right);

                ImGui::PopID();
            }

            ImGui::Unindent();
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    ImGui::Unindent();
}
