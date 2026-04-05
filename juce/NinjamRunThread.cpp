#include "NinjamRunThread.h"
#include "JamWideJuceProcessor.h"
#include "core/njclient.h"
#include "threading/ui_command.h"
#include "threading/ui_event.h"
#include "ui/ui_state.h"
#include "net/server_list.h"

#include <chrono>
#include <mutex>
#include <type_traits>
#include <variant>
#include <ctime>

namespace {

//==============================================================================
// Helper: format current time as HH:MM string
std::string currentTimeString()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min);
    return std::string(buf);
}

//==============================================================================
// Chat callback -- full implementation ported from src/threading/run_thread.cpp
void chat_callback(void* user_data, NJClient* /*client*/,
                   const char** parms, int nparms)
{
    auto& proc = *static_cast<JamWideJuceProcessor*>(user_data);
    if (nparms < 1) return;

    const std::string type = parms[0] ? parms[0] : "";
    const std::string user = (nparms > 1 && parms[1]) ? parms[1] : "";
    const std::string text = (nparms > 2 && parms[2]) ? parms[2] : "";

    auto ts = currentTimeString();

    if (type == "TOPIC")
    {
        if (nparms > 2)
        {
            std::string line;
            if (!user.empty())
            {
                if (!text.empty())
                    line = user + " sets topic to: " + text;
                else
                    line = user + " removes topic.";
            }
            else
            {
                if (!text.empty())
                    line = "Topic is: " + text;
                else
                    line = "No topic is set.";
            }

            ChatMessage msg;
            msg.type = ChatMessageType::Topic;
            msg.sender = user;
            msg.content = line;
            msg.timestamp = ts;
            proc.chat_queue.try_push(std::move(msg));
        }

        jamwide::TopicChangedEvent topicEvt;
        topicEvt.topic = text;
        proc.evt_queue.try_push(std::move(topicEvt));
        return;
    }

    if (type == "MSG")
    {
        if (!user.empty() && !text.empty())
        {
            ChatMessage msg;
            if (text.rfind("/me ", 0) == 0)
            {
                std::string action = text.substr(3);
                std::size_t pos = action.find_first_not_of(' ');
                if (pos != std::string::npos)
                    action = action.substr(pos);
                msg.type = ChatMessageType::Action;
                msg.sender = user;
                msg.content = action;
            }
            else
            {
                msg.type = ChatMessageType::Message;
                msg.sender = user;
                msg.content = text;
            }
            msg.timestamp = ts;
            proc.chat_queue.try_push(std::move(msg));
        }
        return;
    }

    if (type == "PRIVMSG")
    {
        if (!user.empty() && !text.empty())
        {
            ChatMessage msg;
            msg.type = ChatMessageType::PrivateMessage;
            msg.sender = user;
            msg.content = text;
            msg.timestamp = ts;
            proc.chat_queue.try_push(std::move(msg));
        }
        return;
    }

    if (type == "JOIN" || type == "PART")
    {
        if (!user.empty())
        {
            ChatMessage msg;
            msg.type = (type == "JOIN") ? ChatMessageType::Join : ChatMessageType::Part;
            msg.sender = user;
            // Strip @IP for display
            std::string displayName = user;
            auto atPos = displayName.find('@');
            if (atPos != std::string::npos && atPos > 0)
                displayName = displayName.substr(0, atPos);
            msg.content = displayName + ((type == "JOIN")
                ? " has joined the server"
                : " has left the server");
            msg.timestamp = ts;
            proc.chat_queue.try_push(std::move(msg));
        }
        return;
    }
}

//==============================================================================
// License callback -- blocking implementation ported from src/threading/run_thread.cpp
// REVIEW FIX #2: Releases clientLock before blocking wait, re-acquires after.
int license_callback(void* user_data, const char* license_text)
{
    auto& proc = *static_cast<JamWideJuceProcessor*>(user_data);

    // Store license text for UI to display
    {
        std::lock_guard<std::mutex> lock(proc.license_mutex);
        proc.license_text = license_text ? license_text : "";
    }

    // Signal UI that license response is needed
    proc.license_response.store(0, std::memory_order_release);
    proc.license_pending.store(true, std::memory_order_release);

    // REVIEW FIX #2: Release clientLock before blocking wait.
    // The ScopedLock in the run loop still holds the lock here.
    // We must release it so the audio thread can run processBlock,
    // and re-acquire it after the user responds.
    proc.getClientLock().exit();

    // Wait for UI response (or shutdown/timeout)
    {
        std::unique_lock<std::mutex> lock(proc.license_mutex);
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::seconds(60);
        proc.license_cv.wait_until(lock, deadline, [&]() {
            return proc.license_response.load(std::memory_order_acquire) != 0;
        });
    }

    int response = proc.license_response.load(std::memory_order_acquire);
    if (response == 0)
    {
        // Timeout -- default to reject
        response = -1;
        proc.license_response.store(response, std::memory_order_release);
    }
    proc.license_pending.store(false, std::memory_order_release);

    // Re-acquire clientLock before returning to NJClient::Run()
    proc.getClientLock().enter();

    return response > 0 ? 1 : 0;
}

} // anonymous namespace

//==============================================================================
NinjamRunThread::NinjamRunThread(JamWideJuceProcessor& p)
    : juce::Thread("NinjamRun"),
      processor(p)
{
}

NinjamRunThread::~NinjamRunThread()
{
    // Unblock any pending license wait before stopping thread
    processor.license_response.store(-1, std::memory_order_release);
    processor.license_cv.notify_one();

    // CRITICAL: Ensure thread stops before destruction (JUCE requirement).
    // This is the safety-net -- normal shutdown path is via releaseResources().
    stopThread(5000);
}

//==============================================================================
void NinjamRunThread::run()
{
    lastStatus_ = NJClient::NJC_STATUS_DISCONNECTED;

    // Set up callbacks
    NJClient* client = processor.getClient();
    if (client)
    {
        client->ChatMessage_Callback = chat_callback;
        client->ChatMessage_User = &processor;
        client->LicenseAgreementCallback = license_callback;
        client->LicenseAgreement_User = &processor;
    }

    while (!threadShouldExit())
    {
        client = processor.getClient();
        if (!client)
        {
            wait(50);
            continue;
        }

        // Process commands from UI/editor
        processCommands(client);

        // Poll server list fetcher (outside clientLock)
        {
            jamwide::ServerListResult result;
            if (serverListFetcher.poll(result))
            {
                jamwide::ServerListEvent evt;
                evt.servers = std::move(result.servers);
                evt.error = std::move(result.error);
                processor.evt_queue.try_push(std::move(evt));
            }
        }

        // Run NJClient under lock
        {
            const juce::ScopedLock sl(processor.getClientLock());
            while (!client->Run())
            {
                if (threadShouldExit()) return;
            }

            int currentStatus = client->GetStatus();
            client->cached_status.store(currentStatus, std::memory_order_release);

            // Push status change event
            if (currentStatus != lastStatus_)
            {
                jamwide::StatusChangedEvent statusEvt;
                statusEvt.status = currentStatus;
                // Prefer server-provided error; fall back to generic text
                const char* err = client->GetErrorStr();
                if (err && err[0])
                    statusEvt.error_msg = err;
                else if (currentStatus == NJClient::NJC_STATUS_CANTCONNECT)
                    statusEvt.error_msg = "Connection failed";
                else if (currentStatus == NJClient::NJC_STATUS_INVALIDAUTH)
                    statusEvt.error_msg = "Invalid credentials";
                processor.evt_queue.try_push(std::move(statusEvt));

                lastStatus_ = currentStatus;

                // On connect: grace period to suppress false BPM/BPI "changed" messages.
                // Server sends actual config AFTER status becomes OK, so the first
                // few iterations see default→real transitions that aren't real changes.
                if (currentStatus == NJClient::NJC_STATUS_OK)
                    connectGrace_ = 5;

                // On connect: set up all 4 local channels per D-12
                if (currentStatus == NJClient::NJC_STATUS_OK)
                {
                    // Channel 0: stereo input, transmit=true (default)
                    client->SetLocalChannelInfo(0, "Ch1",
                        true, processor.localInputSelector[0] * 2 | (1 << 10),  // stereo pair: pair index → mono start ch
                        true, 256,               // 256 kbps
                        true, processor.localTransmit[0]);

                    // Channels 1-3: stereo input pairs, transmit from processor state
                    // Use input bus and transmit state from processor (persisted per D-14, D-21)
                    for (int ch = 1; ch < 4; ++ch)
                    {
                        juce::String name = "Ch" + juce::String(ch + 1);
                        int srcch = processor.localInputSelector[ch] * 2 | (1 << 10);  // stereo pair: pair index → mono start ch
                        client->SetLocalChannelInfo(ch, name.toRawUTF8(),
                            true, srcch,
                            true, 256,
                            true, processor.localTransmit[ch]);
                    }

                    // Apply persisted routing mode for future auto-assign (per D-12)
                    // REVIEW FIX: Use .load() since routingMode is std::atomic<int>
                    int rm = processor.routingMode.load(std::memory_order_relaxed);
                    client->config_remote_autochan = rm;
                    // REVIEW FIX: nch=32 excludes metronome bus from auto-assign search
                    client->config_remote_autochan_nch = (rm > 0) ? 32 : 0;
                    // Set metronome to last bus (per D-11)
                    client->SetMetronomeChannel(32);
                }
            }

            // REVIEW FIX: HasUserInfoChanged() is DESTRUCTIVE (clears flag on read).
            // Call EXACTLY ONCE per loop iteration. Store result in a bool.
            bool usersChanged = client->HasUserInfoChanged();
            if (usersChanged)
            {
                // REVIEW FIX #3 + GetRemoteUsersSnapshot: Use the thread-safe snapshot API
                // instead of manual GetUserState/GetNumUsers enumeration.
                // GetRemoteUsersSnapshot() locks internally and returns a complete copy.
                std::vector<NJClient::RemoteUserInfo> snapshot;
                client->GetRemoteUsersSnapshot(snapshot);

                processor.cachedUsers = std::move(snapshot);
                processor.userCount.store(static_cast<int>(processor.cachedUsers.size()),
                                          std::memory_order_relaxed);
                processor.evt_queue.try_push(jamwide::UserInfoChangedEvent{});
            }

            // Update remote VU levels every iteration (not just on user info change).
            // VU levels change continuously with audio; the structural snapshot above
            // only fires on join/leave/config changes.
            {
                auto& users = processor.cachedUsers;
                for (size_t ui = 0; ui < users.size(); ++ui)
                {
                    for (size_t ci = 0; ci < users[ui].channels.size(); ++ci)
                    {
                        auto& ch = users[ui].channels[ci];
                        // Use ch.channel_index (NINJAM bit index), not ci (vector index)
                        ch.vu_left = client->GetUserChannelPeak(
                            static_cast<int>(ui), ch.channel_index, 0);
                        ch.vu_right = client->GetUserChannelPeak(
                            static_cast<int>(ui), ch.channel_index, 1);
                    }
                }
            }

            // Update atomic snapshot for UI polling
            // BPM/BPI change detection (D-10, D-11) -- read previous values before storing new
            float prevBpm = processor.uiSnapshot.bpm.load(std::memory_order_relaxed);
            int prevBpi = processor.uiSnapshot.bpi.load(std::memory_order_relaxed);

            float newBpm = static_cast<float>(client->GetActualBPM());
            processor.uiSnapshot.bpm.store(newBpm, std::memory_order_relaxed);
            int bpi = client->GetBPI();
            processor.uiSnapshot.bpi.store(bpi, std::memory_order_relaxed);

            // Detect BPM/BPI changes (skip during connect grace period —
            // server sends actual config AFTER status becomes OK)
            if (connectGrace_ > 0)
            {
                --connectGrace_;
            }
            else
            {
                // Detect BPM change
                if (prevBpm > 0.0f && static_cast<int>(prevBpm) != static_cast<int>(newBpm))
                {
                    processor.evt_queue.try_push(jamwide::BpmChangedEvent{prevBpm, newBpm});

                    ChatMessage msg;
                    msg.type = ChatMessageType::System;
                    msg.content = "[Server] BPM changed from "
                                  + std::to_string(static_cast<int>(prevBpm))
                                  + " to " + std::to_string(static_cast<int>(newBpm));
                    msg.timestamp = currentTimeString();
                    processor.chat_queue.try_push(std::move(msg));

                    // Auto-disable sync if active (D-05)
                    int expected = JamWideJuceProcessor::kSyncActive;
                    if (processor.syncState_.compare_exchange_strong(expected,
                            JamWideJuceProcessor::kSyncIdle, std::memory_order_acq_rel))
                    {
                        processor.evt_queue.try_push(jamwide::SyncStateChangedEvent{
                            JamWideJuceProcessor::kSyncIdle,
                            jamwide::SyncReason::ServerBpmChanged});

                        ChatMessage syncMsg;
                        syncMsg.type = ChatMessageType::System;
                        syncMsg.content = "[Server] Sync disabled -- BPM changed";
                        syncMsg.timestamp = currentTimeString();
                        processor.chat_queue.try_push(std::move(syncMsg));
                    }
                }

                // Detect BPI change
                if (prevBpi > 0 && prevBpi != bpi)
                {
                    processor.evt_queue.try_push(jamwide::BpiChangedEvent{prevBpi, bpi});

                    ChatMessage msg;
                    msg.type = ChatMessageType::System;
                    msg.content = "[Server] BPI changed from "
                                  + std::to_string(prevBpi)
                                  + " to " + std::to_string(bpi);
                    msg.timestamp = currentTimeString();
                    processor.chat_queue.try_push(std::move(msg));
                }
            }

            // Session tracking (SYNC-03) -- expose interval count and elapsed time
            processor.uiSnapshot.interval_count.store(client->GetLoopCount(),
                                                       std::memory_order_relaxed);
            processor.uiSnapshot.session_elapsed_ms.store(client->GetSessionPosition(),
                                                           std::memory_order_relaxed);

            int iPos = 0, iLen = 0;
            client->GetPosition(&iPos, &iLen);
            processor.uiSnapshot.interval_position.store(iPos, std::memory_order_relaxed);
            processor.uiSnapshot.interval_length.store(iLen, std::memory_order_relaxed);

            int beat = (iLen > 0) ? (bpi * iPos / iLen) : 0;
            processor.uiSnapshot.beat_position.store(beat, std::memory_order_relaxed);

            // Local VU for all 4 channels
            for (int ch = 0; ch < 4; ++ch)
            {
                processor.uiSnapshot.local_ch_vu_left[ch].store(
                    client->GetLocalChannelPeak(ch, 0), std::memory_order_relaxed);
                processor.uiSnapshot.local_ch_vu_right[ch].store(
                    client->GetLocalChannelPeak(ch, 1), std::memory_order_relaxed);
            }
            // Keep backward compat: also write channel 0 to the original fields
            processor.uiSnapshot.local_vu_left.store(
                processor.uiSnapshot.local_ch_vu_left[0].load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            processor.uiSnapshot.local_vu_right.store(
                processor.uiSnapshot.local_ch_vu_right[0].load(std::memory_order_relaxed),
                std::memory_order_relaxed);

            // Master VU (output peak)
            processor.uiSnapshot.master_vu_left.store(
                client->GetOutputPeak(0), std::memory_order_relaxed);
            processor.uiSnapshot.master_vu_right.store(
                client->GetOutputPeak(1), std::memory_order_relaxed);
        }

        // Adaptive sleep: connected = 20ms, disconnected = 50ms
        wait(lastStatus_ == NJClient::NJC_STATUS_OK ? 20 : 50);
    }
}

//==============================================================================
void NinjamRunThread::processCommands(NJClient* client)
{
    processor.cmd_queue.drain([&](jamwide::UiCommand&& cmd) {
        const juce::ScopedLock sl(processor.getClientLock());
        std::visit([&](auto&& c) {
            using T = std::decay_t<decltype(c)>;

            if constexpr (std::is_same_v<T, jamwide::ConnectCommand>)
            {
                std::string effectiveUser = c.username;
                if (c.password.empty() &&
                    effectiveUser.rfind("anonymous", 0) != 0)
                    effectiveUser = "anonymous:" + c.username;
                client->Connect(c.server.c_str(),
                                effectiveUser.c_str(),
                                c.password.c_str());
            }
            else if constexpr (std::is_same_v<T, jamwide::DisconnectCommand>)
            {
                client->cached_status.store(
                    NJClient::NJC_STATUS_DISCONNECTED,
                    std::memory_order_release);
                client->Disconnect();
            }
            else if constexpr (std::is_same_v<T, jamwide::SetEncoderFormatCommand>)
            {
                client->SetEncoderFormat(c.fourcc);
            }
            else if constexpr (std::is_same_v<T, jamwide::SendChatCommand>)
            {
                if (!c.type.empty() && !c.text.empty())
                {
                    if (c.type == "PRIVMSG")
                        client->ChatMessage_Send(c.type.c_str(),
                                                 c.target.c_str(),
                                                 c.text.c_str());
                    else
                        client->ChatMessage_Send(c.type.c_str(),
                                                 c.text.c_str());
                }
            }
            else if constexpr (std::is_same_v<T, jamwide::SetLocalChannelInfoCommand>)
            {
                client->SetLocalChannelInfo(c.channel, c.name.c_str(),
                    c.set_srcch, c.srcch,
                    c.set_bitrate, c.bitrate,
                    c.set_transmit, c.transmit);
            }
            else if constexpr (std::is_same_v<T, jamwide::SetUserChannelStateCommand>)
            {
                client->SetUserChannelState(
                    c.user_index, c.channel_index,
                    c.set_sub, c.subscribed,
                    c.set_vol, c.volume,
                    c.set_pan, c.pan,
                    c.set_mute, c.mute,
                    c.set_solo, c.solo,
                    c.set_outch, c.outchannel);
            }
            else if constexpr (std::is_same_v<T, jamwide::RequestServerListCommand>)
            {
                serverListFetcher.request(c.url.empty()
                    ? "http://autosong.ninjam.com/serverlist.php"
                    : c.url);
            }
            else if constexpr (std::is_same_v<T, jamwide::SetUserStateCommand>)
            {
                client->SetUserState(c.user_index,
                                     c.set_vol, c.volume,
                                     c.set_pan, c.pan,
                                     c.set_mute, c.mute);
            }
            else if constexpr (std::is_same_v<T, jamwide::SetLocalChannelMonitoringCommand>)
            {
                client->SetLocalChannelMonitoring(
                    c.channel,
                    c.set_volume, c.volume,
                    c.set_pan, c.pan,
                    c.set_mute, c.mute,
                    c.set_solo, c.solo);
            }
            else if constexpr (std::is_same_v<T, jamwide::SyncCommand>)
            {
                int expected = JamWideJuceProcessor::kSyncIdle;
                if (processor.syncState_.compare_exchange_strong(expected,
                        JamWideJuceProcessor::kSyncWaiting, std::memory_order_acq_rel))
                {
                    processor.evt_queue.try_push(jamwide::SyncStateChangedEvent{
                        JamWideJuceProcessor::kSyncWaiting, jamwide::SyncReason::UserRequest});
                }
            }
            else if constexpr (std::is_same_v<T, jamwide::SyncCancelCommand>)
            {
                int expected = JamWideJuceProcessor::kSyncWaiting;
                if (processor.syncState_.compare_exchange_strong(expected,
                        JamWideJuceProcessor::kSyncIdle, std::memory_order_acq_rel))
                {
                    processor.evt_queue.try_push(jamwide::SyncStateChangedEvent{
                        JamWideJuceProcessor::kSyncIdle, jamwide::SyncReason::UserCancel});
                }
            }
            else if constexpr (std::is_same_v<T, jamwide::SyncDisableCommand>)
            {
                int expected = JamWideJuceProcessor::kSyncActive;
                if (processor.syncState_.compare_exchange_strong(expected,
                        JamWideJuceProcessor::kSyncIdle, std::memory_order_acq_rel))
                {
                    processor.evt_queue.try_push(jamwide::SyncStateChangedEvent{
                        JamWideJuceProcessor::kSyncIdle, jamwide::SyncReason::UserDisable});
                }
            }
            else if constexpr (std::is_same_v<T, jamwide::SetRoutingModeCommand>)
            {
                // REVIEW FIX: Use nch=32 (not 34) to exclude metronome bus (channels 32-33)
                // from auto-assign search space. find_unused_output_channel_pair() searches
                // channels 2 through nch-1, so nch=32 limits to channels 2-31 (buses 1-15).
                client->config_remote_autochan = c.mode;
                client->config_remote_autochan_nch = (c.mode > 0) ? 32 : 0;

                // Quick-assign sweep: reset all existing users then re-assign per D-02, Pitfall 4
                if (c.mode > 0)
                {
                    // First pass: reset all channels to bus 0
                    int numUsers = client->GetNumUsers();
                    for (int u = 0; u < numUsers; ++u)
                    {
                        for (int ch = 0; ; ++ch)
                        {
                            int ci = client->EnumUserChannels(u, ch);
                            if (ci < 0) break;
                            client->SetUserChannelState(u, ci,
                                false, false, false, 0.0f, false, 0.0f,
                                false, false, false, false,
                                true, 0);
                        }
                    }
                    // Second pass: assign sequential buses using find_unused_output_channel_pair()
                    for (int u = 0; u < numUsers; ++u)
                    {
                        int assignedBus = -1;  // For by-user mode: all channels of same user share one bus
                        for (int ch = 0; ; ++ch)
                        {
                            int ci = client->EnumUserChannels(u, ch);
                            if (ci < 0) break;
                            if (c.mode == 2 && assignedBus >= 0)
                            {
                                // By-user: reuse same bus for all channels of this user (per D-03)
                                client->SetUserChannelState(u, ci,
                                    false, false, false, 0.0f, false, 0.0f,
                                    false, false, false, false,
                                    true, assignedBus);
                            }
                            else
                            {
                                int newBus = client->find_unused_output_channel_pair();
                                // For a clean sweep (we just reset all to 0), any non-zero return is valid.
                                if (newBus >= 2)
                                {
                                    client->SetUserChannelState(u, ci,
                                        false, false, false, 0.0f, false, 0.0f,
                                        false, false, false, false,
                                        true, newBus);
                                    if (c.mode == 2)
                                        assignedBus = newBus;
                                }
                                // else: newBus < 2 means nch < 4 (shouldn't happen), stay on Main Mix
                            }
                        }
                    }
                }
                else
                {
                    // Manual mode: reset all channels to bus 0 (Main Mix) per D-01
                    int numUsers = client->GetNumUsers();
                    for (int u = 0; u < numUsers; ++u)
                    {
                        for (int ch = 0; ; ++ch)
                        {
                            int ci = client->EnumUserChannels(u, ch);
                            if (ci < 0) break;
                            client->SetUserChannelState(u, ci,
                                false, false, false, 0.0f, false, 0.0f,
                                false, false, false, false,
                                true, 0);
                        }
                    }
                }
            }
        }, std::move(cmd));
    });
}
