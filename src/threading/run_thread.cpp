/*
    NINJAM CLAP Plugin - run_thread.cpp
    Network thread implementation
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#include "run_thread.h"
#include "plugin/ninjam_plugin.h"
#include "core/njclient.h"
#include "net/server_list.h"
#include "debug/logging.h"

#include <chrono>
#include <utility>
#include <memory>
#include <type_traits>
#include <vector>

namespace ninjam {

namespace {

void chat_callback(void* user_data, NJClient* client, const char** parms, int nparms) {
    auto* plugin = static_cast<NinjamPlugin*>(user_data);
    if (!plugin || nparms < 1) {
        return;
    }

    ChatMessageEvent event;
    event.type = parms[0] ? parms[0] : "";
    event.user = (nparms > 1 && parms[1]) ? parms[1] : "";
    event.text = (nparms > 2 && parms[2]) ? parms[2] : "";

    const bool is_topic = (event.type == "TOPIC");
    const std::string topic_text = event.text;

    plugin->ui_queue.try_push(std::move(event));

    if (is_topic) {
        TopicChangedEvent topic_event;
        topic_event.topic = topic_text;
        plugin->ui_queue.try_push(std::move(topic_event));
    }
}

int license_callback(void* user_data, const char* license_text) {
    NLOG("[License] license_callback called\n");
    auto* plugin = static_cast<NinjamPlugin*>(user_data);
    if (!plugin) {
        NLOG("[License] ERROR: plugin is null!\n");
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(plugin->license_mutex);
        plugin->license_text = license_text ? license_text : "";
    }
    NLOG("[License] License text received, waiting for user response...\n");

    plugin->license_response.store(0, std::memory_order_release);
    plugin->license_pending.store(true, std::memory_order_release);
    plugin->license_cv.notify_one();

    std::unique_lock<std::mutex> lock(plugin->license_mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    plugin->license_cv.wait_until(lock, deadline, [&]() {
        return plugin->license_response.load(std::memory_order_acquire) != 0 ||
               plugin->shutdown.load(std::memory_order_acquire);
    });

    int response = plugin->license_response.load(std::memory_order_acquire);
    NLOG("[License] Got response: %d\n", response);
    if (response == 0) {
        NLOG("[License] Timeout - defaulting to reject\n");
        response = -1;
        plugin->license_response.store(response, std::memory_order_release);
    }
    plugin->license_pending.store(false, std::memory_order_release);
    NLOG("[License] Returning %d (1=accept, 0=reject)\n", response > 0 ? 1 : 0);
    return response > 0 ? 1 : 0;
}

void setup_callbacks(NinjamPlugin* plugin) {
    if (!plugin || !plugin->client) {
        return;
    }
    plugin->client->ChatMessage_Callback = chat_callback;
    plugin->client->ChatMessage_User = plugin;
    plugin->client->LicenseAgreementCallback = license_callback;
    plugin->client->LicenseAgreement_User = plugin;
}

void process_commands(NinjamPlugin* plugin,
                      NJClient* client,
                      ServerListFetcher& server_list) {
    if (!plugin) {
        return;
    }

    plugin->cmd_queue.drain([&](UiCommand&& cmd) {
        std::visit([&](auto&& c) {
            using T = std::decay_t<decltype(c)>;

            if constexpr (std::is_same_v<T, ConnectCommand>) {
                if (!client) return;
                {
                    std::lock_guard<std::mutex> lock(plugin->state_mutex);
                    plugin->server = c.server;
                    plugin->username = c.username;
                    plugin->password = c.password;
                }
                client->Connect(c.server.c_str(),
                                c.username.c_str(),
                                c.password.c_str());
            } else if constexpr (std::is_same_v<T, DisconnectCommand>) {
                if (!client) return;
                client->Disconnect();
            } else if constexpr (std::is_same_v<T, SetLocalChannelInfoCommand>) {
                if (!client) return;
                client->SetLocalChannelInfo(
                    c.channel,
                    c.name.c_str(),
                    false, 0,
                    c.set_bitrate, c.bitrate,
                    c.set_transmit, c.transmit);
            } else if constexpr (std::is_same_v<T, SetLocalChannelMonitoringCommand>) {
                if (!client) return;
                client->SetLocalChannelMonitoring(
                    c.channel,
                    c.set_volume, c.volume,
                    c.set_pan, c.pan,
                    c.set_mute, c.mute,
                    c.set_solo, c.solo);
            } else if constexpr (std::is_same_v<T, SetUserStateCommand>) {
                if (!client) return;
                client->SetUserState(
                    c.user_index,
                    false, 0.0f,
                    false, 0.0f,
                    c.set_mute, c.mute);
            } else if constexpr (std::is_same_v<T, SetUserChannelStateCommand>) {
                if (!client) return;
                client->SetUserChannelState(
                    c.user_index, c.channel_index,
                    c.set_sub, c.subscribed,
                    c.set_vol, c.volume,
                    c.set_pan, c.pan,
                    c.set_mute, c.mute,
                    c.set_solo, c.solo);
            } else if constexpr (std::is_same_v<T, RequestServerListCommand>) {
                server_list.request(c.url);
            }
        }, std::move(cmd));
    });
}

void apply_remote_snapshot(NinjamPlugin* plugin,
                           const std::vector<NJClient::RemoteUserInfo>& snapshot) {
    if (!plugin) return;

    std::vector<RemoteUser> converted;
    converted.reserve(snapshot.size());
    for (const auto& user : snapshot) {
        RemoteUser ui_user;
        ui_user.name = user.name;
        ui_user.mute = user.mute;

        ui_user.channels.reserve(user.channels.size());
        for (const auto& chan : user.channels) {
            RemoteChannel ui_channel;
            ui_channel.name = chan.name;
            ui_channel.channel_index = chan.channel_index;
            ui_channel.subscribed = chan.subscribed;
            ui_channel.volume = chan.volume;
            ui_channel.pan = chan.pan;
            ui_channel.mute = chan.mute;
            ui_channel.solo = chan.solo;
            ui_channel.vu_left = chan.vu_left;
            ui_channel.vu_right = chan.vu_right;
            ui_user.channels.push_back(std::move(ui_channel));
        }
        converted.push_back(std::move(ui_user));
    }

    {
        std::lock_guard<std::mutex> lock(plugin->state_mutex);
        plugin->ui_state.remote_users = std::move(converted);
        plugin->ui_state.users_dirty = false;
    }
}

/**
 * Main run thread function.
 * Continuously calls NJClient::Run() while plugin is active.
 */
void run_thread_func(std::shared_ptr<NinjamPlugin> plugin) {
    if (!plugin) {
        return;
    }
    NLOG("[RunThread] Started\n");
    int last_status = NJClient::NJC_STATUS_DISCONNECTED;
    int loop_count = 0;
    ServerListFetcher server_list;
    std::vector<NJClient::RemoteUserInfo> remote_snapshot;

    while (!plugin->shutdown.load(std::memory_order_acquire)) {
        bool status_changed = false;
        int current_status = last_status;
        std::string error_msg;
        bool user_info_changed = false;
        bool update_remote = false;

        // Get client pointer under lock, but call Run() outside lock
        // This is necessary because Run() can call license_callback which
        // blocks waiting for UI, and UI needs the lock to refresh display.
        NJClient* client = nullptr;
        {
            std::lock_guard<std::mutex> lock(plugin->state_mutex);
            client = plugin->client.get();
        }
        
        process_commands(plugin.get(), client, server_list);

        if (!client) {
            ServerListResult list_result;
            if (server_list.poll(list_result)) {
                ServerListEvent event;
                event.servers = std::move(list_result.servers);
                event.error = std::move(list_result.error);
                plugin->ui_queue.try_push(std::move(event));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        
        // Call Run() WITHOUT holding state_mutex to avoid deadlock with UI
        // Run() returns 0 while there's more work to do
        int run_result;
        NLOG_VERBOSE("[RunThread] Calling client->Run()\n");
        while (!(run_result = client->Run())) {
            // Check shutdown between iterations
            if (plugin->shutdown.load(std::memory_order_acquire)) {
                NLOG("[RunThread] Shutdown requested\n");
                return;
            }
        }
        NLOG_VERBOSE("[RunThread] client->Run() returned %d\n", run_result);

        current_status = client->GetStatus();
        if (current_status != last_status) {
            status_changed = true;
            NLOG("[RunThread] Status changed: %d -> %d\n", last_status, current_status);
            last_status = current_status;
            const char* err = client->GetErrorStr();
            if (err && err[0]) {
                error_msg = err;
                NLOG("[RunThread] Error: %s\n", err);
            }
        }

        if (client->HasUserInfoChanged()) {
            user_info_changed = true;
            update_remote = true;
        }

        if (current_status == NJClient::NJC_STATUS_OK) {
            int pos = 0;
            int len = 0;
            client->GetPosition(&pos, &len);

            int bpi = client->GetBPI();
            float bpm = client->GetActualBPM();

            int beat_pos = 0;
            if (len > 0 && bpi > 0) {
                beat_pos = (pos * bpi) / len;
            }

            plugin->ui_snapshot.bpm.store(bpm, std::memory_order_relaxed);
            plugin->ui_snapshot.bpi.store(bpi, std::memory_order_relaxed);
            plugin->ui_snapshot.interval_position.store(pos, std::memory_order_relaxed);
            plugin->ui_snapshot.interval_length.store(len, std::memory_order_relaxed);
            plugin->ui_snapshot.beat_position.store(beat_pos, std::memory_order_relaxed);

            if ((loop_count++ % 5) == 0) {
                update_remote = true;
            }
        }

        if (update_remote) {
            NLOG_VERBOSE("[RunThread] Getting remote users snapshot\n");
            client->GetRemoteUsersSnapshot(remote_snapshot);
            NLOG_VERBOSE("[RunThread] Got %zu users, applying snapshot\n", remote_snapshot.size());
            apply_remote_snapshot(plugin.get(), remote_snapshot);
            NLOG_VERBOSE("[RunThread] Applied snapshot\n");
        }

        if (status_changed) {
            StatusChangedEvent event;
            event.status = current_status;
            event.error_msg = error_msg;
            plugin->ui_queue.try_push(std::move(event));
        }

        if (user_info_changed) {
            plugin->ui_queue.try_push(UserInfoChangedEvent{});
        }

        {
            ServerListResult list_result;
            if (server_list.poll(list_result)) {
                ServerListEvent event;
                event.servers = std::move(list_result.servers);
                event.error = std::move(list_result.error);
                plugin->ui_queue.try_push(std::move(event));
            }
        }
        
        // Adaptive sleep based on connection state
        // Connected: faster polling for responsiveness
        // Disconnected: slower polling to save resources
        int status = NJClient::NJC_STATUS_DISCONNECTED;
        if (client) {
            status = client->cached_status.load(std::memory_order_acquire);
        }
        
        auto sleep_time = (status == NJClient::NJC_STATUS_DISCONNECTED)
            ? std::chrono::milliseconds(50)  // Disconnected: 20 Hz
            : std::chrono::milliseconds(20); // Connected/connecting: 50 Hz
        
        std::this_thread::sleep_for(sleep_time);
    }
}

} // anonymous namespace

void run_thread_start(NinjamPlugin* plugin,
                      std::shared_ptr<NinjamPlugin> keepalive) {
    if (!plugin || !keepalive) {
        return;
    }
    // Clear shutdown flag
    plugin->shutdown.store(false, std::memory_order_release);

    setup_callbacks(plugin);

    // Start the thread
    plugin->run_thread = std::thread(run_thread_func, std::move(keepalive));
}

void run_thread_stop(NinjamPlugin* plugin) {
    // Signal shutdown
    plugin->shutdown.store(true, std::memory_order_release);
    
    // Wake up license wait if blocked
    // This prevents deadlock if Run thread is waiting for license response
    {
        std::lock_guard<std::mutex> lock(plugin->license_mutex);
        plugin->license_response.store(-1, std::memory_order_release);
        plugin->license_pending.store(false, std::memory_order_release);
    }
    plugin->license_cv.notify_one();
    
    // Join thread
    if (plugin->run_thread.joinable()) {
        plugin->run_thread.join();
    }
}

} // namespace ninjam
