#include "NinjamRunThread.h"
#include "JamWideJuceProcessor.h"
#include "core/njclient.h"
#include "threading/ui_command.h"

#include <type_traits>
#include <variant>

namespace {

//==============================================================================
// Chat callback -- no-op stub for Phase 3.
// Must be set to prevent NJClient default behavior.
// Phase 4 will add chat UI integration.
void chat_callback(void* /*user_data*/, NJClient* /*client*/,
                   const char** /*parms*/, int /*nparms*/)
{
    // No-op: chat messages are silently consumed.
    // Phase 4 will push these to a UI event queue.
}

//==============================================================================
// License callback -- auto-accept for Phase 3.
// Phase 4 will add a proper license dialog.
int license_callback(void* /*user_data*/, const char* /*license_text*/)
{
    // Auto-accept: return 1 to accept the license agreement.
    return 1;
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
    // CRITICAL: Ensure thread stops before destruction (JUCE requirement).
    // This is the safety-net -- normal shutdown path is via releaseResources().
    stopThread(5000);
}

//==============================================================================
void NinjamRunThread::run()
{
    int lastStatus = NJClient::NJC_STATUS_DISCONNECTED;

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

        // Run NJClient under lock
        {
            const juce::ScopedLock sl(processor.getClientLock());
            while (!client->Run())
            {
                if (threadShouldExit()) return;
            }

            int currentStatus = client->GetStatus();
            client->cached_status.store(currentStatus, std::memory_order_release);

            if (currentStatus != lastStatus)
            {
                lastStatus = currentStatus;

                // On connect: set up default local channel
                if (currentStatus == NJClient::NJC_STATUS_OK)
                {
                    client->SetLocalChannelInfo(0, "Channel",
                        true, 0 | (1 << 10),    // stereo input
                        true, 256,               // 256 kbps
                        true, true);             // transmit enabled
                }
            }
        }

        // Adaptive sleep: connected = 20ms, disconnected = 50ms
        wait(lastStatus == NJClient::NJC_STATUS_OK ? 20 : 50);
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
            // RequestServerListCommand, SetLocalChannelMonitoringCommand,
            // and SetUserStateCommand will be handled when Phase 4/5 UI
            // needs them. Safe to ignore for now.
        }, std::move(cmd));
    });
}
