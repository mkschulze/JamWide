/*
    NINJAM CLAP Plugin - ui_util.cpp
    Shared UI helper functions
*/

#include "ui_util.h"
#include "plugin/ninjam_plugin.h"

void ui_update_solo_state(ninjam::NinjamPlugin* plugin) {
    if (!plugin) return;

    auto& state = plugin->ui_state;
    state.any_solo_active = state.local_solo;

    for (const auto& user : state.remote_users) {
        for (const auto& channel : user.channels) {
            if (channel.solo) {
                state.any_solo_active = true;
                return;
            }
        }
    }
}
