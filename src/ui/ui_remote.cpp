/*
    NINJAM CLAP Plugin - ui_remote.cpp
    Remote channels panel rendering
*/

#include "ui_remote.h"
#include "ui_meters.h"
#include "ui_util.h"
#include "threading/ui_command.h"
#include "plugin/jamwide_plugin.h"
#include "core/njclient.h"
#include "imgui.h"

void ui_render_remote_channels(jamwide::JamWidePlugin* plugin) {
    if (!plugin) return;

    const int status = plugin->ui_state.status;
    
    if (!ImGui::CollapsingHeader("Remote Users", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Indent();

    if (status != NJClient::NJC_STATUS_OK) {
        ImGui::TextDisabled("Not connected");
        ImGui::Unindent();
        return;
    }

    std::unique_lock<std::mutex> client_lock(plugin->client_mutex);
    NJClient* client = plugin->client.get();
    if (!client) {
        ImGui::TextDisabled("Not connected");
        ImGui::Unindent();
        return;
    }

    const int num_users = client->GetNumUsers();
    if (num_users <= 0) {
        ImGui::TextDisabled("No remote users connected");
        ImGui::Unindent();
        return;
    }

    for (int u = 0; u < num_users; ++u) {
        float user_vol = 0.0f;
        float user_pan = 0.0f;
        bool user_mute = false;
        const char* user_name = client->GetUserState(u, &user_vol, &user_pan, &user_mute);
        const char* label = (user_name && *user_name) ? user_name : "User";

        ImGui::PushID(u);

        const bool user_open = ImGui::TreeNodeEx(
            label, ImGuiTreeNodeFlags_DefaultOpen);

        ImGui::SameLine();
        if (ImGui::Checkbox("M##user", &user_mute)) {
            jamwide::SetUserStateCommand cmd;
            cmd.user_index = u;
            cmd.set_mute = true;
            cmd.mute = user_mute;
            plugin->cmd_queue.try_push(std::move(cmd));
        }

        if (user_open) {
            ImGui::Indent();

            for (int c = 0; ; ++c) {
                const int channel_index = client->EnumUserChannels(u, c);
                if (channel_index < 0) {
                    break;
                }

                bool subscribed = false;
                float volume = 1.0f;
                float pan = 0.0f;
                bool mute = false;
                bool solo = false;
                int out_channel = 0;
                int flags = 0;
                const char* channel_name = client->GetUserChannelState(
                    u, channel_index, &subscribed, &volume, &pan,
                    &mute, &solo, &out_channel, &flags);
                if (!channel_name) {
                    continue;
                }

                const char* channel_label = (*channel_name) ? channel_name : "Channel";
                ImGui::PushID(channel_index);

                if (ImGui::Checkbox("##sub", &subscribed)) {
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = u;
                    cmd.channel_index = channel_index;
                    cmd.set_sub = true;
                    cmd.subscribed = subscribed;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                ImGui::SameLine();
                ImGui::Text("%s", channel_label);

                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::SliderFloat("##vol", &volume,
                                       0.0f, 2.0f, "%.2f")) {
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = u;
                    cmd.channel_index = channel_index;
                    cmd.set_vol = true;
                    cmd.volume = volume;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::SliderFloat("##pan", &pan,
                                       -1.0f, 1.0f, "%.2f")) {
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = u;
                    cmd.channel_index = channel_index;
                    cmd.set_pan = true;
                    cmd.pan = pan;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                ImGui::SameLine();
                if (ImGui::Checkbox("M##chan_mute", &mute)) {
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = u;
                    cmd.channel_index = channel_index;
                    cmd.set_mute = true;
                    cmd.mute = mute;
                    plugin->cmd_queue.try_push(std::move(cmd));
                }

                ImGui::SameLine();
                if (ImGui::Checkbox("S##chan_solo", &solo)) {
                    jamwide::SetUserChannelStateCommand cmd;
                    cmd.user_index = u;
                    cmd.channel_index = channel_index;
                    cmd.set_solo = true;
                    cmd.solo = solo;
                    plugin->cmd_queue.try_push(std::move(cmd));
                    ui_update_solo_state(plugin);
                }

                ImGui::SameLine();
                const float vu_left = client->GetUserChannelPeak(u, channel_index, 0);
                const float vu_right = client->GetUserChannelPeak(u, channel_index, 1);
                render_vu_meter("##chan_vu", vu_left, vu_right);

                ImGui::PopID();
            }

            ImGui::Unindent();
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    ImGui::Unindent();
}
