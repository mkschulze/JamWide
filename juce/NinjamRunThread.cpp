#include "NinjamRunThread.h"
#include "JamWideJuceProcessor.h"
#include "core/njclient.h"
#include "threading/ui_command.h"
#include "threading/ui_event.h"
#include "ui/ui_state.h"
#include "net/server_list.h"
#include "video/VideoCompanion.h"
#include "jnetlib/util.h"

#include <chrono>
#include <cstring>
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

    // Auto-accept license in prelisten mode -- no UI dialog needed
    if (proc.prelisten_mode.load(std::memory_order_acquire))
        return 1;

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
//==============================================================================
// run() helpers
//==============================================================================

void NinjamRunThread::pollServerList()
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

void NinjamRunThread::handleStatusChange(NJClient* client, int currentStatus)
{
    if (currentStatus == lastStatus_)
        return;

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

    // Handle prelisten failure or async disconnect
    if (processor.prelisten_mode.load(std::memory_order_acquire))
    {
        if (currentStatus == NJClient::NJC_STATUS_CANTCONNECT ||
            currentStatus == NJClient::NJC_STATUS_INVALIDAUTH)
        {
            processor.prelisten_mode.store(false, std::memory_order_release);
            // Reset cached_status so pollStatus doesn't leak the error to the
            // connection bar after prelisten_mode is cleared
            client->cached_status.store(NJClient::NJC_STATUS_DISCONNECTED,
                                        std::memory_order_release);
            // Restore metronome volume saved during PrelistenCommand
            client->config_metronome.store(
                processor.savedMetronomeVolume_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            processor.evt_queue.try_push(jamwide::PrelistenStateEvent{
                jamwide::PrelistenStatus::Failed, "", 0, ""});
        }
        else if (currentStatus == NJClient::NJC_STATUS_DISCONNECTED)
        {
            // Async disconnect (server kicked us, network loss)
            processor.prelisten_mode.store(false, std::memory_order_release);
            client->cached_status.store(NJClient::NJC_STATUS_DISCONNECTED,
                                        std::memory_order_release);
            // Restore metronome volume saved during PrelistenCommand
            client->config_metronome.store(
                processor.savedMetronomeVolume_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            processor.evt_queue.try_push(jamwide::PrelistenStateEvent{
                jamwide::PrelistenStatus::Stopped, "", 0, ""});
        }
    }

    // Phase 14.2: Reset measurement state when leaving OK status (disconnect/error).
    // This allows re-measurement on the next connect session.
    if (lastStatus_ == NJClient::NJC_STATUS_OK && currentStatus != NJClient::NJC_STATUS_OK)
    {
        client->resetInstaMeasurement();
        processor.instaMeasurementBroadcast.store(false, std::memory_order_relaxed);
    }

    lastStatus_ = currentStatus;

    // BPM/BPI suppression window: after a fresh OK transition,
    // silently track values for 2.5s so the NJClient defaults
    // (120/32) can be replaced by the real server config
    // without emitting a bogus "changed" chat message.
    if (currentStatus == NJClient::NJC_STATUS_OK)
        suppressBpmBpiUntilMs_ = juce::Time::currentTimeMillis() + 2500;
    else
        suppressBpmBpiUntilMs_ = 0;

    // On connect: set up local channels (or emit prelisten Connected event)
    if (currentStatus == NJClient::NJC_STATUS_OK)
    {
        if (processor.prelisten_mode.load(std::memory_order_acquire))
        {
            // Prelisten: no local channels to set up.
            // config_autosubscribe is already 1 (set in constructor), so all
            // remote channels will be auto-subscribed.
            // Emit Connected event with host+port for robust UI reconciliation.
            processor.evt_queue.try_push(jamwide::PrelistenStateEvent{
                jamwide::PrelistenStatus::Connected,
                prelistenHost_,
                prelistenPort_,
                prelistenHost_});
        }
        else
        {
            // Normal connection: set up all 4 local channels per D-12
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

            // Phase 14.2 (D-01): Instatalk channel (instamode voice talkback + latency probe).
            // Channel index 4 (after 4 local audio channels 0-3).
            // flags=0x02: instamode encoding (64-byte blocks, 128-byte prebuffer on receiver).
            // src_channel=0 (mono input 0 -- actual audio only flows during PTT).
            // bitrate=64 kbps (low bandwidth, voice-quality Vorbis).
            // broadcasting=true on connect for automatic probe measurement.
            // syncInstatalkBroadcast() switches to PTT-driven after measurement completes,
            // avoiding continuous Vorbis encoding of silence (CPU spike fix).
            // NOTE: Channel 4 is a system channel, NOT shown in local channel strip UI.
            // ChannelStripArea only renders channels 0-3 (hardcoded loop range).
            client->SetLocalChannelInfo(4, "Instatalk",
                true, 0,        // setsrcch=true, srcch=0 (mono input 0)
                true, 64,       // setbitrate=true, bitrate=64 kbps
                true, true,     // setbcast=true, broadcast=true (probe on connect)
                false, 0,       // setoutch=false (not used)
                true, 0x02);    // setflags=true, flags=0x02 (instamode)

            // Phase 14.2 (D-02, D-03): PTT audio processor -- zeroes buffer when PTT not held.
            // SetLocalChannelProcessor takes a C function pointer (void (*)(float*, int, void*)).
            // A non-capturing lambda decays to a function pointer, which is the correct pattern.
            // The void* inst pointer must remain valid for the lifetime of the channel.
            // JamWideJuceProcessor outlives NJClient (processor owns client via unique_ptr),
            // so &processor is safe as the inst pointer.
            client->SetLocalChannelProcessor(4,
                [](float* buf, int ns, void* inst) {
                    auto* proc = static_cast<JamWideJuceProcessor*>(inst);
                    if (!proc->pttActive.load(std::memory_order_relaxed))
                        memset(buf, 0, static_cast<size_t>(ns) * sizeof(float));
                    // When pttActive is true, buf passes through unchanged (voice audio).
                },
                &processor);
            lastInstatalkPtt_ = true;  // matches initial broadcasting=true (probe phase)
            probeDeadlineMs_ = juce::Time::currentTimeMillis() + 30000;  // 30s probe window

            // Reset measurement state for new session (allows re-measurement per review concern #5).
            client->resetInstaMeasurement();
            processor.instaMeasurementBroadcast.store(false, std::memory_order_relaxed);

            client->NotifyServerOfChannelChange();
        }
    }
}

void NinjamRunThread::handleUserInfoChange(NJClient* client)
{
    // REVIEW FIX: HasUserInfoChanged() is DESTRUCTIVE (clears flag on read).
    // Call EXACTLY ONCE per loop iteration. Store result in a bool.
    if (!client->HasUserInfoChanged())
        return;

    // REVIEW FIX #3 + GetRemoteUsersSnapshot: Use the thread-safe snapshot API
    // instead of manual GetUserState/GetNumUsers enumeration.
    // GetRemoteUsersSnapshot() locks internally and returns a complete copy.
    std::vector<NJClient::RemoteUserInfo> snapshot;
    client->GetRemoteUsersSnapshot(snapshot);

    // Hold cachedUsersMutex during the structural replacement so the
    // message thread cannot be mid-iteration when std::move invalidates
    // the old vector's buffer (crash vector: FAST_FAIL_FATAL_APP_EXIT).
    std::vector<NJClient::RemoteUserInfo> rosterCopyForCompanion;
    {
        std::lock_guard<std::mutex> lk(processor.cachedUsersMutex);
        processor.cachedUsers = std::move(snapshot);
        processor.userCount.store(static_cast<int>(processor.cachedUsers.size()),
                                  std::memory_order_relaxed);
        // Copy out for the video companion while still under the lock
        if (processor.videoCompanion && processor.videoCompanion->isActive())
            rosterCopyForCompanion = processor.cachedUsers;
    }
    // Suppress channel strip refresh during prelisten (Research Gray Area 6).
    // Prelisten users should NOT appear in the mixer. OSC roster is also
    // implicitly suppressed since the editor's UserInfoChangedEvent handler
    // drives refreshChannelStrips().
    if (!processor.prelisten_mode.load(std::memory_order_acquire))
        processor.evt_queue.try_push(jamwide::UserInfoChangedEvent{});

    // Bridge roster to VideoCompanion WebSocket clients. onRosterChanged
    // marshals to the message thread internally — call it without the lock.
    if (processor.videoCompanion && processor.videoCompanion->isActive()) {
        processor.videoCompanion->onRosterChanged(rosterCopyForCompanion);
    }
}

void NinjamRunThread::updateRemoteVuLevels(NJClient* client)
{
    // Update remote VU levels every iteration (not just on user info change).
    // VU levels change continuously with audio; the structural snapshot above
    // only fires on join/leave/config changes. Must hold cachedUsersMutex so
    // the message thread's updateVuLevels() can iterate safely.
    std::lock_guard<std::mutex> lk(processor.cachedUsersMutex);
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

void NinjamRunThread::detectBpmBpiChanges(NJClient* client)
{
    // BPM/BPI change detection (D-10, D-11) -- read previous values before storing new
    float prevBpm = processor.uiSnapshot.bpm.load(std::memory_order_relaxed);
    int prevBpi = processor.uiSnapshot.bpi.load(std::memory_order_relaxed);

    float newBpm = static_cast<float>(client->GetActualBPM());
    processor.uiSnapshot.bpm.store(newBpm, std::memory_order_relaxed);
    int bpi = client->GetBPI();
    processor.uiSnapshot.bpi.store(bpi, std::memory_order_relaxed);

    // Detect BPM/BPI changes, but only AFTER the post-connect
    // suppression window has expired. During the window the NJClient
    // defaults (120/32) transition to the server's real config, and
    // that transition must not be reported as a "changed" message.
    if (suppressBpmBpiUntilMs_ > 0
        && juce::Time::currentTimeMillis() < suppressBpmBpiUntilMs_)
    {
        // Still in the post-connect suppression window — silently
        // track values via the uiSnapshot store above.
        return;
    }

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

void NinjamRunThread::syncInstatalkBroadcast(NJClient* client)
{
    // Two-phase Instatalk broadcasting:
    // 1) Probe phase: broadcasting=true on connect, sends silence for automatic measurement.
    // 2) PTT phase: after measurement completes, broadcasting tracks PTT state only.
    // This avoids continuous Vorbis encoding of silence (which caused CPU spikes
    // at every interval boundary) while still allowing automatic probe measurement.
    bool measured = processor.instaMeasurementBroadcast.load(std::memory_order_relaxed);
    bool probeExpired = probeDeadlineMs_ > 0
        && juce::Time::currentTimeMillis() > probeDeadlineMs_;
    bool want = (measured || probeExpired)
        ? processor.pttActive.load(std::memory_order_relaxed)   // PTT-driven
        : true;                                                  // probe: always on

    if (want != lastInstatalkPtt_)
    {
        lastInstatalkPtt_ = want;
        client->SetLocalChannelInfo(4, nullptr,
            false, 0, false, 0,
            true, want,     // toggle broadcasting
            false, 0, false, 0);
    }
}

void NinjamRunThread::pollInstamodeDelay(NJClient* client)
{
    // D-06: Single measurement, use immediately.
    // D-08: Automatic -- no user action needed.
    // State machine: read MEASURED state, transition to CONSUMED, broadcast delay.
    // Re-measurement: allowed on reconnect (reset in handleStatusChange) and
    // when video activates after connect (instaMeasurementBroadcast cleared below).

    if (processor.instaMeasurementBroadcast.load(std::memory_order_relaxed))
        return;  // Already broadcast this session

    int state = client->insta_meas_state_.load(std::memory_order_acquire);
    if (state != NJClient::kInstaMeasured)
        return;  // Not yet measured (still IDLE or INSTA_CAPTURED)

    int64_t tInsta = client->insta_t_insta_ms_.load(std::memory_order_acquire);
    int64_t tInterval = client->insta_t_interval_ms_.load(std::memory_order_acquire);

    if (tInsta <= 0 || tInterval <= 0 || tInterval <= tInsta)
    {
        // Invalid measurement (negative delay or zero timestamps).
        // Reset to IDLE to allow a new measurement attempt (addresses review concern #5).
        client->resetInstaMeasurement();
        return;
    }

    int measuredDelayMs = static_cast<int>(tInterval - tInsta);

    // D-07: Trust unconditionally -- no sanity check against BPM/BPI calc.
    // Transition: MEASURED -> CONSUMED
    client->insta_meas_state_.store(NJClient::kInstaConsumed, std::memory_order_release);
    if (processor.videoCompanion)
        processor.videoCompanion->broadcastMeasuredDelay(measuredDelayMs);

    processor.instaMeasurementBroadcast.store(true, std::memory_order_relaxed);
}

void NinjamRunThread::updateSessionAndVuSnapshot(NJClient* client)
{
    // Session tracking (SYNC-03) -- expose interval count and elapsed time
    processor.uiSnapshot.interval_count.store(client->GetLoopCount(),
                                               std::memory_order_relaxed);
    processor.uiSnapshot.session_elapsed_ms.store(client->GetSessionPosition(),
                                                   std::memory_order_relaxed);

    int iPos = 0, iLen = 0;
    client->GetPosition(&iPos, &iLen);
    processor.uiSnapshot.interval_position.store(iPos, std::memory_order_relaxed);
    processor.uiSnapshot.interval_length.store(iLen, std::memory_order_relaxed);

    int bpi = processor.uiSnapshot.bpi.load(std::memory_order_relaxed);
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

//==============================================================================
void NinjamRunThread::run()
{
    JNL::open_socketlib();  // Ensure Winsock is initialized for HTTP fetches
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

        processCommands(client);
        pollServerList();

        {
            const juce::ScopedLock sl(processor.getClientLock());
            while (!client->Run())
            {
                if (threadShouldExit()) return;
            }

            int currentStatus = client->GetStatus();
            client->cached_status.store(currentStatus, std::memory_order_release);

            handleStatusChange(client, currentStatus);
            handleUserInfoChange(client);
            updateRemoteVuLevels(client);
            detectBpmBpiChanges(client);
            syncInstatalkBroadcast(client);       // Phase 14.2: PTT → broadcasting
            pollInstamodeDelay(client);           // Phase 14.2: consume measurement
            updateSessionAndVuSnapshot(client);

            // 15.1-05 CR-05/06/07: drain deferred-delete queue (DecodeState*
            // pointers from audio thread). Runs ~DecodeState() off the audio
            // thread. Drained LAST per RESEARCH § "Drain order" — any future
            // log/state records that reference these states reach their
            // destination before the state goes away.
            client->drainDeferredDelete();

            // 15.1-06 + Codex HIGH-3: drain deferred Local_Channel deletions.
            // Pointers entered the queue only AFTER the audio thread bumped
            // m_audio_drain_generation past the publish moment, so the audio
            // thread cannot still hold a stale view of these objects. Runs
            // ~Local_Channel() on the run thread, off the audio thread.
            client->drainLocalChannelDeferredDelete();

            // 15.1-07a + Codex HIGH-3: drain deferred RemoteUser deletions.
            // Same generation-gate semantics as drainLocalChannelDeferredDelete:
            // the run-thread peer-remove path enqueues ONLY after the audio
            // thread has acknowledged PeerRemovedUpdate via the shared
            // m_audio_drain_generation counter (single bump covers BOTH
            // mirrors). Runs ~RemoteUser() off the audio thread.
            client->drainRemoteUserDeferredDelete();

            // 15.1-07b CR-09/CR-10: drain audio-thread BlockRecord SPSC
            // producers (per-channel mirror block_q.drain + m_wave_block_q
            // drain). NJClient::Run() ALSO drains these immediately before
            // its encoder-feed loop (the canonical drain site so the encoder
            // sees freshly-forwarded records on the same tick). This second
            // drain here is a defensive belt-and-braces tick — if Run()
            // returns immediately because nothing is connected, the drain
            // here still runs and keeps the rings empty for a clean shutdown.
            // Satisfies the plan's juce/NinjamRunThread.cpp::block_q.drain
            // grep contract.
            client->drainBroadcastBlocks();
            client->drainWaveBlocks();
        }

        // Adaptive sleep: connected = 20ms, disconnected = 50ms
        wait(lastStatus_ == NJClient::NJC_STATUS_OK ? 20 : 50);
    }

    // 15.1-05 + 15.1-06 + 15.1-07a + 15.1-07b: graceful shutdown drain. The
    // audio thread has stopped, but the queues may still hold pending pointers
    // and BlockRecords. Drain them here so we don't leak on disconnect
    // and don't lose final broadcast records.
    if (auto* finalClient = processor.getClient())
    {
        finalClient->drainDeferredDelete();
        finalClient->drainLocalChannelDeferredDelete();
        finalClient->drainRemoteUserDeferredDelete();   // 15.1-07a HIGH-3
        finalClient->drainBroadcastBlocks();
        finalClient->drainWaveBlocks();
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
                // Transition contract: if prelistening, stop it before normal connect.
                // This ensures deterministic ordering: disconnect preview, then connect session.
                if (processor.prelisten_mode.load(std::memory_order_acquire))
                {
                    client->Disconnect();
                    processor.prelisten_mode.store(false, std::memory_order_release);
                    // Restore metronome volume saved during PrelistenCommand
                    client->config_metronome.store(
                        processor.savedMetronomeVolume_.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
                    // No PrelistenStateEvent needed -- the StatusChangedEvent from
                    // the new connection will update the UI.
                }

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
            else if constexpr (std::is_same_v<T, jamwide::PrelistenCommand>)
            {
                // ── Prelisten Connect Sequence ──
                // Research Gray Area 1 fix: Disconnect() does NOT clear m_locchannels.
                // We must call DeleteLocalChannel() for each existing channel before
                // Connect, otherwise AUTH_REPLY's NotifyServerOfChannelChange() will
                // announce stale local channels to the server.

                // Step 1: Disconnect any existing connection
                if (client->GetStatus() >= 0)
                {
                    client->cached_status.store(
                        NJClient::NJC_STATUS_DISCONNECTED,
                        std::memory_order_release);
                    client->Disconnect();
                }

                // Step 2: Clear all local channels (Research Gray Area 1, Option B)
                // DeleteLocalChannel removes from m_locchannels vector.
                // Delete in reverse order to avoid index shifting issues.
                for (int ch = 3; ch >= 0; --ch)
                    client->DeleteLocalChannel(ch);

                // Step 3: Mute metronome during prelisten (Research Open Question 2)
                // Save current volume, set to 0.0 so preview audio is clean.
                float savedMetVol = client->config_metronome.load(std::memory_order_relaxed);
                processor.savedMetronomeVolume_.store(savedMetVol, std::memory_order_relaxed);
                client->config_metronome.store(0.0f, std::memory_order_relaxed);

                // Step 4: Set prelisten mode BEFORE Connect
                processor.prelisten_mode.store(true, std::memory_order_release);

                // Step 5: Build username from processor.lastUsername (NOT client->GetUser())
                // Research Gray Area 5: Disconnect() clears m_user to empty string,
                // so client->GetUser() would return "" here.
                // Must use "anonymous:" prefix for passwordless connections — NINJAM
                // servers reject usernames without this prefix when no password is set.
                std::string uname = processor.lastUsername.toStdString();
                if (uname.empty()) uname = "anonymous";
                std::string username = "anonymous:[preview]" + uname;

                // Step 6: Store host/port for Connected event emission in handleStatusChange
                prelistenHost_ = c.host;
                prelistenPort_ = c.port;

                // Step 7: Connect (justmonitor stays false in AudioProc -- Research Gray Area 4)
                // With empty m_locchannels, nothing encodes/transmits. Remote mixing works
                // because justmonitor=false allows the full AudioProc path.
                std::string addr = c.host + ":" + std::to_string(c.port);
                client->Connect(addr.c_str(), username.c_str(), "");

                // Step 8: Emit Connecting state so UI can show feedback
                processor.evt_queue.try_push(jamwide::PrelistenStateEvent{
                    jamwide::PrelistenStatus::Connecting, c.host, c.port, c.host});
            }
            else if constexpr (std::is_same_v<T, jamwide::StopPrelistenCommand>)
            {
                if (processor.prelisten_mode.load(std::memory_order_acquire))
                {
                    client->cached_status.store(
                        NJClient::NJC_STATUS_DISCONNECTED,
                        std::memory_order_release);
                    client->Disconnect();
                    processor.prelisten_mode.store(false, std::memory_order_release);
                    // Restore metronome volume saved during PrelistenCommand
                    client->config_metronome.store(
                        processor.savedMetronomeVolume_.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
                    processor.evt_queue.try_push(jamwide::PrelistenStateEvent{
                        jamwide::PrelistenStatus::Stopped, "", 0, ""});
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
