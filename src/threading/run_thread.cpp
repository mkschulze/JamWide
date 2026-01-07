/*
    NINJAM CLAP Plugin - run_thread.cpp
    Network thread implementation
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#include "run_thread.h"
#include "plugin/ninjam_plugin.h"
#include "core/njclient.h"

#include <chrono>
#include <utility>

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
    auto* plugin = static_cast<NinjamPlugin*>(user_data);
    if (!plugin) {
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(plugin->license_mutex);
        plugin->license_text = license_text ? license_text : "";
    }

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
    if (response == 0) {
        response = -1;
        plugin->license_response.store(response, std::memory_order_release);
    }
    plugin->license_pending.store(false, std::memory_order_release);
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

/**
 * Main run thread function.
 * Continuously calls NJClient::Run() while plugin is active.
 */
void run_thread_func(NinjamPlugin* plugin) {
    int last_status = NJClient::NJC_STATUS_DISCONNECTED;

    while (!plugin->shutdown.load(std::memory_order_acquire)) {
        bool status_changed = false;
        int current_status = last_status;
        std::string error_msg;
        bool user_info_changed = false;

        // Always call Run() regardless of connection state.
        // This allows Run() to process connection attempts and state changes.
        {
            std::lock_guard<std::mutex> lock(plugin->state_mutex);
            
            // Run() returns 0 while there's more work to do
            while (!plugin->client->Run()) {
                // Check shutdown between iterations
                if (plugin->shutdown.load(std::memory_order_acquire)) {
                    return;
                }
            }

            current_status = plugin->client->GetStatus();
            if (current_status != last_status) {
                status_changed = true;
                last_status = current_status;
                const char* err = plugin->client->GetErrorStr();
                if (err) {
                    error_msg = err;
                }
            }

            if (plugin->client->HasUserInfoChanged()) {
                user_info_changed = true;
            }

            if (current_status == NJClient::NJC_STATUS_OK) {
                int pos = 0;
                int len = 0;
                plugin->client->GetPosition(&pos, &len);

                int bpi = plugin->client->GetBPI();
                float bpm = plugin->client->GetActualBPM();

                int beat_pos = 0;
                if (len > 0 && bpi > 0) {
                    beat_pos = (pos * bpi) / len;
                }

                plugin->ui_snapshot.bpm.store(bpm, std::memory_order_relaxed);
                plugin->ui_snapshot.bpi.store(bpi, std::memory_order_relaxed);
                plugin->ui_snapshot.interval_position.store(pos, std::memory_order_relaxed);
                plugin->ui_snapshot.interval_length.store(len, std::memory_order_relaxed);
                plugin->ui_snapshot.beat_position.store(beat_pos, std::memory_order_relaxed);
            }
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
        
        // Adaptive sleep based on connection state
        // Connected: faster polling for responsiveness
        // Disconnected: slower polling to save resources
        int status = plugin->client->cached_status.load(std::memory_order_acquire);
        
        auto sleep_time = (status == NJClient::NJC_STATUS_DISCONNECTED)
            ? std::chrono::milliseconds(50)  // Disconnected: 20 Hz
            : std::chrono::milliseconds(20); // Connected/connecting: 50 Hz
        
        std::this_thread::sleep_for(sleep_time);
    }
}

} // anonymous namespace

void run_thread_start(NinjamPlugin* plugin) {
    // Clear shutdown flag
    plugin->shutdown.store(false, std::memory_order_release);

    setup_callbacks(plugin);

    // Start the thread
    plugin->run_thread = std::thread(run_thread_func, plugin);
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
