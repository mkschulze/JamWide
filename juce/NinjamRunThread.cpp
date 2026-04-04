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
            msg.content = user + ((type == "JOIN")
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
                if (currentStatus == NJClient::NJC_STATUS_CANTCONNECT)
                    statusEvt.error_msg = "Connection failed";
                else if (currentStatus == NJClient::NJC_STATUS_INVALIDAUTH)
                    statusEvt.error_msg = "Invalid credentials";
                else
                {
                    const char* err = client->GetErrorStr();
                    if (err && err[0])
                        statusEvt.error_msg = err;
                }
                processor.evt_queue.try_push(std::move(statusEvt));

                lastStatus_ = currentStatus;

                // On connect: set up default local channel
                if (currentStatus == NJClient::NJC_STATUS_OK)
                {
                    client->SetLocalChannelInfo(0, "Channel",
                        true, 0 | (1 << 10),    // stereo input
                        true, 256,               // 256 kbps
                        true, true);             // transmit enabled
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
                        ch.vu_left = client->GetUserChannelPeak(
                            static_cast<int>(ui), static_cast<int>(ci), 0);
                        ch.vu_right = client->GetUserChannelPeak(
                            static_cast<int>(ui), static_cast<int>(ci), 1);
                    }
                }
            }

            // Update atomic snapshot for UI polling
            processor.uiSnapshot.bpm.store(static_cast<float>(client->GetActualBPM()),
                                            std::memory_order_relaxed);
            int bpi = client->GetBPI();
            processor.uiSnapshot.bpi.store(bpi, std::memory_order_relaxed);

            int iPos = 0, iLen = 0;
            client->GetPosition(&iPos, &iLen);
            processor.uiSnapshot.interval_position.store(iPos, std::memory_order_relaxed);
            processor.uiSnapshot.interval_length.store(iLen, std::memory_order_relaxed);

            int beat = (iLen > 0) ? (bpi * iPos / iLen) : 0;
            processor.uiSnapshot.beat_position.store(beat, std::memory_order_relaxed);

            // Local VU
            processor.uiSnapshot.local_vu_left.store(
                client->GetLocalChannelPeak(0, 0), std::memory_order_relaxed);
            processor.uiSnapshot.local_vu_right.store(
                client->GetLocalChannelPeak(0, 1), std::memory_order_relaxed);

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
                    false, 0,
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
                    c.set_solo, c.solo);
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
                                     false, 0.0f,   // no volume change
                                     false, 0.0f,   // no pan change
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
        }, std::move(cmd));
    });
}
