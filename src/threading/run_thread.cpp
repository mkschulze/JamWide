/*
    JamWide Plugin - run_thread.cpp
    Network thread implementation
    
    Copyright (C) 2024 JamWide Contributors
    Licensed under GPLv2+
*/

#include "run_thread.h"
#include "plugin/jamwide_plugin.h"
#include "core/njclient.h"
#include "net/server_list.h"
#include "debug/logging.h"

#include <chrono>
#include <utility>
#include <memory>
#include <type_traits>
#include <variant>
#include <vector>

namespace jamwide {

namespace {

void chat_callback(void* user_data, NJClient* client, const char** parms, int nparms) {
    auto* plugin = static_cast<JamWidePlugin*>(user_data);
    if (!plugin || nparms < 1) {
        return;
    }

    const std::string type = parms[0] ? parms[0] : "";
    const std::string user = (nparms > 1 && parms[1]) ? parms[1] : "";
    const std::string text = (nparms > 2 && parms[2]) ? parms[2] : "";

    ChatMessage message;

    if (type == "TOPIC") {
        if (nparms > 2) {
            std::string line;
            if (!user.empty()) {
                if (!text.empty()) {
                    line = user + " sets topic to: " + text;
                } else {
                    line = user + " removes topic.";
                }
            } else {
                if (!text.empty()) {
                    line = "Topic is: " + text;
                } else {
                    line = "No topic is set.";
                }
            }
            message.type = ChatMessageType::Topic;
            message.sender = user;
            message.content = line;
            plugin->chat_queue.try_push(std::move(message));
        }

        TopicChangedEvent topic_event;
        topic_event.topic = text;
        plugin->ui_queue.try_push(std::move(topic_event));
        return;
    }

    if (type == "MSG") {
        if (!user.empty() && !text.empty()) {
            if (text.rfind("/me ", 0) == 0) {
                std::string action = text.substr(3);
                std::size_t pos = action.find_first_not_of(' ');
                if (pos != std::string::npos) {
                    action = action.substr(pos);
                }
                message.type = ChatMessageType::Action;
                message.sender = user;
                message.content = action;
            } else {
                message.type = ChatMessageType::Message;
                message.sender = user;
                message.content = text;
            }
            plugin->chat_queue.try_push(std::move(message));
        }
        return;
    }

    if (type == "PRIVMSG") {
        if (!user.empty() && !text.empty()) {
            message.type = ChatMessageType::PrivateMessage;
            message.sender = user;
            message.content = text;
            plugin->chat_queue.try_push(std::move(message));
        }
        return;
    }

    if (type == "JOIN" || type == "PART") {
        if (!user.empty()) {
            message.type = (type == "JOIN")
                ? ChatMessageType::Join
                : ChatMessageType::Part;
            message.sender = user;
            message.content = user + (type == "JOIN"
                ? " has joined the server"
                : " has left the server");
            plugin->chat_queue.try_push(std::move(message));
        }
        return;
    }
}

int license_callback(void* user_data, const char* license_text) {
    NLOG("[License] license_callback called\n");
    auto* plugin = static_cast<JamWidePlugin*>(user_data);
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

    // Release client mutex while waiting for UI response (ReaNINJAM pattern).
    plugin->client_mutex.unlock();

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
    plugin->client_mutex.lock();
    NLOG("[License] Returning %d (1=accept, 0=reject)\n", response > 0 ? 1 : 0);
    return response > 0 ? 1 : 0;
}

void setup_callbacks(JamWidePlugin* plugin) {
    if (!plugin) {
        return;
    }
    std::lock_guard<std::mutex> lock(plugin->client_mutex);
    if (!plugin->client) {
        return;
    }
    plugin->client->ChatMessage_Callback = chat_callback;
    plugin->client->ChatMessage_User = plugin;
    plugin->client->LicenseAgreementCallback = license_callback;
    plugin->client->LicenseAgreement_User = plugin;
}

void process_commands(JamWidePlugin* plugin,
                      ServerListFetcher& server_list,
                      std::vector<UiCommand>& client_cmds) {
    if (!plugin) {
        return;
    }

    plugin->cmd_queue.drain([&](UiCommand&& cmd) {
        if (std::holds_alternative<RequestServerListCommand>(cmd)) {
            server_list.request(std::get<RequestServerListCommand>(cmd).url);
            return;
        }
        if (auto* connect = std::get_if<ConnectCommand>(&cmd)) {
            std::lock_guard<std::mutex> lock(plugin->state_mutex);
            plugin->server = connect->server;
            plugin->username = connect->username;
            plugin->password = connect->password;
        }
        client_cmds.push_back(std::move(cmd));
    });
}

void execute_client_commands(JamWidePlugin* plugin,
                             NJClient* client,
                             std::vector<UiCommand>& client_cmds) {
    if (!plugin || !client) {
        client_cmds.clear();
        return;
    }

    for (auto& cmd : client_cmds) {
        std::visit([&](auto&& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ConnectCommand>) {
                // For public servers (no password), prefix username with "anonymous:"
                // This is required by NINJAM server protocol for anonymous logins
                std::string effective_user = c.username;
                if (c.password.empty() && 
                    effective_user.rfind("anonymous", 0) != 0) {
                    effective_user = "anonymous:" + c.username;
                }
                NLOG("[RunThread] Executing ConnectCommand: server='%s' user='%s'\n",
                     c.server.c_str(), effective_user.c_str());
                client->Connect(c.server.c_str(),
                                effective_user.c_str(),
                                c.password.c_str());
            } else if constexpr (std::is_same_v<T, DisconnectCommand>) {
                NLOG("[RunThread] Executing DisconnectCommand\n");
                client->cached_status.store(NJClient::NJC_STATUS_DISCONNECTED,
                                            std::memory_order_release);
                client->Disconnect();
            } else if constexpr (std::is_same_v<T, SetLocalChannelInfoCommand>) {
                client->SetLocalChannelInfo(
                    c.channel,
                    c.name.c_str(),
                    false, 0,
                    c.set_bitrate, c.bitrate,
                    c.set_transmit, c.transmit);
            } else if constexpr (std::is_same_v<T, SetLocalChannelMonitoringCommand>) {
                client->SetLocalChannelMonitoring(
                    c.channel,
                    c.set_volume, c.volume,
                    c.set_pan, c.pan,
                    c.set_mute, c.mute,
                    c.set_solo, c.solo);
            } else if constexpr (std::is_same_v<T, SetUserStateCommand>) {
                client->SetUserState(
                    c.user_index,
                    false, 0.0f,
                    false, 0.0f,
                    c.set_mute, c.mute);
            } else if constexpr (std::is_same_v<T, SetUserChannelStateCommand>) {
                client->SetUserChannelState(
                    c.user_index, c.channel_index,
                    c.set_sub, c.subscribed,
                    c.set_vol, c.volume,
                    c.set_pan, c.pan,
                    c.set_mute, c.mute,
                    c.set_solo, c.solo);
            } else if constexpr (std::is_same_v<T, SendChatCommand>) {
                if (!c.type.empty() && !c.text.empty()) {
                    if (c.type == "PRIVMSG") {
                        client->ChatMessage_Send(c.type.c_str(),
                                                 c.target.c_str(),
                                                 c.text.c_str());
                    } else {
                        client->ChatMessage_Send(c.type.c_str(),
                                                 c.text.c_str());
                    }
                }
            } else {
                // RequestServerListCommand handled earlier.
            }
        }, cmd);
    }
    client_cmds.clear();
}

/**
 * Main run thread function.
 * Continuously calls NJClient::Run() while plugin is active.
 */
void run_thread_func(std::shared_ptr<JamWidePlugin> plugin) {
    if (!plugin) {
        return;
    }
    NLOG("[RunThread] Started\n");
    int last_status = NJClient::NJC_STATUS_DISCONNECTED;
    ServerListFetcher server_list;
    std::vector<UiCommand> client_cmds;

    while (!plugin->shutdown.load(std::memory_order_acquire)) {
        bool status_changed = false;
        int current_status = last_status;
        std::string error_msg;
        int pos = 0;
        int len = 0;
        int bpi = 0;
        float bpm = 0.0f;
        int beat_pos = 0;
        bool have_position = false;

        client_cmds.clear();
        process_commands(plugin.get(), server_list, client_cmds);

        plugin->client_mutex.lock();
        NJClient* client = plugin->client.get();
        if (!client) {
            plugin->client_mutex.unlock();
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
        
        execute_client_commands(plugin.get(), client, client_cmds);

        // Run() returns 0 while there's more work to do.
        int run_result;
        NLOG_VERBOSE("[RunThread] Calling client->Run()\n");
        while (!(run_result = client->Run())) {
            // Check shutdown between iterations
            if (plugin->shutdown.load(std::memory_order_acquire)) {
                NLOG("[RunThread] Shutdown requested\n");
                plugin->client_mutex.unlock();
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
            
            // Initialize default local channel when connection succeeds
            if (current_status == NJClient::NJC_STATUS_OK) {
                NLOG("[RunThread] Connection established, initializing local channel 0\n");
                std::lock_guard<std::mutex> state_lock(plugin->state_mutex);
                const char* ch_name = plugin->ui_state.local_name_input[0] ? 
                                     plugin->ui_state.local_name_input : "Channel";
                // Set default channel: stereo input (ch 0), 128kbps, transmit enabled
                client->SetLocalChannelInfo(0, ch_name, true, 0|(1<<10), true, 128, true, true);
                NLOG("[RunThread] Local channel 0 configured: name='%s'\n", ch_name);
            }
        }

        if (current_status == NJClient::NJC_STATUS_OK) {
            client->GetPosition(&pos, &len);

            bpi = client->GetBPI();
            bpm = client->GetActualBPM();

            if (len > 0 && bpi > 0) {
                beat_pos = (pos * bpi) / len;
            }
            have_position = true;
        }

        plugin->client_mutex.unlock();

        if (have_position) {
            plugin->ui_snapshot.bpm.store(bpm, std::memory_order_relaxed);
            plugin->ui_snapshot.bpi.store(bpi, std::memory_order_relaxed);
            plugin->ui_snapshot.interval_position.store(pos, std::memory_order_relaxed);
            plugin->ui_snapshot.interval_length.store(len, std::memory_order_relaxed);
            plugin->ui_snapshot.beat_position.store(beat_pos, std::memory_order_relaxed);
        }

        if (status_changed) {
            StatusChangedEvent event;
            event.status = current_status;
            event.error_msg = error_msg;
            plugin->ui_queue.try_push(std::move(event));
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
        auto sleep_time = (current_status == NJClient::NJC_STATUS_DISCONNECTED)
            ? std::chrono::milliseconds(50)  // Disconnected: 20 Hz
            : std::chrono::milliseconds(20); // Connected/connecting: 50 Hz
        
        std::this_thread::sleep_for(sleep_time);
    }
}

} // anonymous namespace

void run_thread_start(JamWidePlugin* plugin,
                      std::shared_ptr<JamWidePlugin> keepalive) {
    if (!plugin || !keepalive) {
        return;
    }
    // Clear shutdown flag
    plugin->shutdown.store(false, std::memory_order_release);

    setup_callbacks(plugin);

    // Start the thread
    plugin->run_thread = std::thread(run_thread_func, std::move(keepalive));
}

void run_thread_stop(JamWidePlugin* plugin) {
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

} // namespace jamwide
