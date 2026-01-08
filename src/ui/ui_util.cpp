/*
    NINJAM CLAP Plugin - ui_util.cpp
    Shared UI helper functions
*/

#include "ui_util.h"
#include "plugin/ninjam_plugin.h"
#include "core/njclient.h"

void ui_update_solo_state(ninjam::NinjamPlugin* plugin) {
    if (!plugin) return;

    bool any_solo_active = plugin->ui_state.local_solo;

    std::unique_lock<std::mutex> client_lock(plugin->client_mutex);
    NJClient* client = plugin->client.get();
    if (client) {
        const int num_users = client->GetNumUsers();
        for (int u = 0; u < num_users && !any_solo_active; ++u) {
            for (int c = 0; ; ++c) {
                const int channel_index = client->EnumUserChannels(u, c);
                if (channel_index < 0) {
                    break;
                }
                bool solo = false;
                if (client->GetUserChannelState(u, channel_index, nullptr, nullptr,
                                                nullptr, nullptr, &solo, nullptr,
                                                nullptr)) {
                    if (solo) {
                        any_solo_active = true;
                        break;
                    }
                }
            }
        }
    }

    plugin->ui_state.any_solo_active = any_solo_active;
}
