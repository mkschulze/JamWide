/*
    OscServer.cpp - Bidirectional OSC server for JamWide

    Receives OSC messages on a configurable UDP port, dispatches to APVTS parameters
    and cmd_queue via callAsync() (preserving SPSC single-producer invariant per D-19).

    Sends dirty parameters as an OSC bundle every 100ms (per D-12) with echo
    suppression (per D-14) to prevent values set by incoming OSC from being
    echoed back on the next tick.

    Also broadcasts session telemetry and VU meters at 100ms rate.
*/

#include "osc/OscServer.h"
#include "JamWideJuceProcessor.h"
#include "threading/ui_command.h"
#include "core/njclient.h"

// Local fourcc definitions (matches pattern used in ui_local.cpp, ui_remote.cpp)
#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))
#define NJ_ENCODER_FMT_FLAC MAKE_NJ_FOURCC('F','L','A','C')
#define NJ_ENCODER_FMT_VORBIS MAKE_NJ_FOURCC('O','G','G','v')

OscServer::OscServer(JamWideJuceProcessor& proc)
    : processor(proc)
{
    // Force first send by setting all last-sent values to sentinel
    lastSentValues.fill(-999.0f);
    oscSourced.fill(false);
}

OscServer::~OscServer()
{
    alive->store(false, std::memory_order_release);
    stop();
}

bool OscServer::start(int receivePort, const juce::String& sendIP, int sendPort)
{
    // Clean up any existing connection first
    stop();

    currentReceivePort = receivePort;
    currentSendIP = sendIP;
    currentSendPort = sendPort;

    // Bind receiver
    if (!receiver.connect(receivePort))
    {
        receiverError.store(true, std::memory_order_relaxed);
        errorMsg = "Failed to bind port " + juce::String(receivePort);
        return false;
    }

    receiver.addListener(this);
    receiverConnected = true;

    // Connect sender
    if (!sender.connect(sendIP, sendPort))
    {
        receiver.removeListener(this);
        receiver.disconnect();
        receiverConnected = false;
        receiverError.store(true, std::memory_order_relaxed);
        errorMsg = "Failed to connect sender to " + sendIP + ":" + juce::String(sendPort);
        return false;
    }

    senderConnected = true;
    receiverError.store(false, std::memory_order_relaxed);
    errorMsg.clear();
    enabled.store(true, std::memory_order_relaxed);

    // Reset dirty tracking to force full state dump on connect
    lastSentValues.fill(-999.0f);
    oscSourced.fill(false);
    lastSentBpm = -1.0f;
    lastSentBpi = -1;
    lastSentBeat = -1;
    lastSentStatus = -999;
    lastSentUsers = -1;
    lastSentSampleRate = -1.0f;
    lastSentCodec.clear();

    // Phase 10: Reset remote user dirty tracking
    for (auto& u : lastSentRemoteUsers) u = RemoteUserLastSent{};
    for (auto& u : lastSentRemoteChannels) for (auto& c : u) c = RemoteChannelLastSent{};
    for (auto& u : remoteOscSourced) u = RemoteUserOscSourced{};
    for (auto& u : remoteChOscSourced) for (auto& c : u) c = RemoteChannelOscSourced{};
    lastSentRosterCount = -1;
    lastSentRosterHash = -1;

    // Start 100ms timer per D-12
    startTimer(100);

    return true;
}

void OscServer::stop()
{
    stopTimer();
    receiver.removeListener(this);
    receiver.disconnect();
    sender.disconnect();
    enabled.store(false, std::memory_order_relaxed);
    senderConnected = false;
    receiverConnected = false;
}

juce::String OscServer::getErrorMessage() const
{
    return errorMsg;
}

// ── Receive path (network thread -> message thread via callAsync) ──

void OscServer::oscMessageReceived(const juce::OSCMessage& msg)
{
    // Called on juce_osc network thread (RealtimeCallback)
    auto address = msg.getAddressPattern().toString();

    float value = 0.0f;
    if (msg.size() > 0)
    {
        if (msg[0].isFloat32())
            value = msg[0].getFloat32();
        else if (msg[0].isInt32())
            value = static_cast<float>(msg[0].getInt32());
        else
            return;  // Unsupported argument type
    }
    else
    {
        return;  // No arguments
    }

    // CRITICAL per D-19: dispatch to message thread to preserve SPSC single-producer invariant
    // Capture shared alive flag to guard against use-after-free if OscServer is destroyed
    // while lambdas are pending in the message queue
    auto aliveFlag = alive;
    juce::MessageManager::callAsync([this, aliveFlag, addr = std::move(address), val = value]()
    {
        if (!aliveFlag->load(std::memory_order_acquire))
            return;
        handleOscOnMessageThread(addr, val);
    });
}

void OscServer::oscBundleReceived(const juce::OSCBundle& bundle)
{
    for (const auto& element : bundle)
    {
        if (element.isMessage())
            oscMessageReceived(element.getMessage());
        else if (element.isBundle())
            oscBundleReceived(element.getBundle());
    }
}

// ── Message thread dispatch ──

void OscServer::handleOscOnMessageThread(const juce::String& address, float value)
{
    int idx = addressMap.resolve(address);
    if (idx < 0)
        return;  // Unknown address

    const auto& entry = addressMap.getEntry(idx);

    // Clamp value to entry's range (T-09-01: input validation)
    float clamped = juce::jlimit(entry.rangeMin, entry.rangeMax, value);

    // Mark echo suppression (per D-14)
    if (idx < kMaxParams)
        oscSourced[static_cast<size_t>(idx)] = true;

    switch (entry.type)
    {
        case OscParamType::ApvtsFloat:
        {
            auto* param = processor.apvts.getParameter(entry.apvtsId);
            if (param == nullptr)
                return;

            if (entry.isDbVariant)
            {
                // Convert from dB to linear, then to normalized 0-1
                float linear = OscAddressMap::dbToLinear(clamped);
                float norm = param->getNormalisableRange().convertTo0to1(linear);
                param->setValueNotifyingHost(norm);
            }
            else if (entry.isPanParam)
            {
                // Convert OSC 0-1 to APVTS -1..1, then normalize
                float apvtsPan = OscAddressMap::oscPanToApvts(clamped);
                float norm = param->getNormalisableRange().convertTo0to1(apvtsPan);
                param->setValueNotifyingHost(norm);
            }
            else
            {
                // OSC 0-1 maps to APVTS normalized 0-1 directly
                // Note: APVTS volume range is 0-2, but OSC sends 0-1 normalized
                param->setValueNotifyingHost(clamped);
            }
            break;
        }

        case OscParamType::ApvtsBool:
        {
            auto* param = processor.apvts.getParameter(entry.apvtsId);
            if (param == nullptr)
                return;

            param->setValueNotifyingHost(clamped >= 0.5f ? 1.0f : 0.0f);
            break;
        }

        case OscParamType::NjclientAtomic:
        {
            // Metro pan: convert OSC 0..1 to NJClient -1..1 (Pattern 4)
            auto* client = processor.getClient();
            if (client != nullptr)
            {
                float nativeVal = entry.isPanParam
                    ? OscAddressMap::oscPanToApvts(clamped)  // 0..1 -> -1..1
                    : clamped;
                client->config_metronome_pan.store(
                    juce::jlimit(-1.0f, 1.0f, nativeVal),
                    std::memory_order_relaxed);
            }
            break;
        }

        case OscParamType::CmdQueue:
        {
            // Local solo: dispatch via cmd_queue
            // Feedback reads actual state from NJClient::GetLocalChannelMonitoring()
            jamwide::SetLocalChannelMonitoringCommand cmd;
            cmd.channel = entry.channelIndex;
            cmd.set_solo = true;
            cmd.solo = (clamped >= 0.5f);
            processor.cmd_queue.try_push(cmd);
            break;
        }

        case OscParamType::ReadOnly:
            // Telemetry/VU is send-only; ignore incoming
            break;
    }
}

// ── Send path (100ms timer on message thread) ──

void OscServer::timerCallback()
{
    if (!enabled.load(std::memory_order_relaxed) || !senderConnected)
        return;

    juce::OSCBundle bundle;
    bool hasContent = false;

    sendDirtyApvtsParams(bundle, hasContent);
    sendDirtyNonApvtsParams(bundle, hasContent);
    sendDirtyTelemetry(bundle, hasContent);
    sendVuMeters(bundle, hasContent);
    sendRemoteRoster(bundle, hasContent);     // BEFORE dirty params (roster change resets cache)
    sendDirtyRemoteUsers(bundle, hasContent);
    sendRemoteVuMeters(bundle, hasContent);

    if (hasContent)
        sender.send(bundle);
}

void OscServer::sendDirtyApvtsParams(juce::OSCBundle& bundle, bool& hasContent)
{
    const int count = addressMap.getControllableCount();
    for (int i = 0; i < count; ++i)
    {
        const auto& entry = addressMap.getEntry(i);

        // Skip non-APVTS and non-bool types (handled in sendDirtyNonApvtsParams)
        if (entry.type != OscParamType::ApvtsFloat && entry.type != OscParamType::ApvtsBool)
            continue;

        // Echo suppression (per D-14): skip if value was set by incoming OSC
        if (i < kMaxParams && oscSourced[static_cast<size_t>(i)])
        {
            oscSourced[static_cast<size_t>(i)] = false;
            continue;
        }

        float current = getCurrentValue(i);

        // Dirty check (exact float comparison per research Pitfall 2)
        if (i < kMaxParams && current != lastSentValues[static_cast<size_t>(i)])
        {
            lastSentValues[static_cast<size_t>(i)] = current;
            bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(entry.oscAddress), current));
            hasContent = true;
        }
    }
}

void OscServer::sendDirtyNonApvtsParams(juce::OSCBundle& bundle, bool& hasContent)
{
    const int count = addressMap.getControllableCount();
    for (int i = 0; i < count; ++i)
    {
        const auto& entry = addressMap.getEntry(i);

        // Only handle NjclientAtomic and CmdQueue types here
        if (entry.type != OscParamType::NjclientAtomic && entry.type != OscParamType::CmdQueue)
            continue;

        // Echo suppression
        if (i < kMaxParams && oscSourced[static_cast<size_t>(i)])
        {
            oscSourced[static_cast<size_t>(i)] = false;
            continue;
        }

        float current = getCurrentValue(i);

        if (i < kMaxParams && current != lastSentValues[static_cast<size_t>(i)])
        {
            lastSentValues[static_cast<size_t>(i)] = current;
            bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(entry.oscAddress), current));
            hasContent = true;
        }
    }
}

void OscServer::sendDirtyTelemetry(juce::OSCBundle& bundle, bool& hasContent)
{
    // Read from uiSnapshot atomics (lock-free on message thread, per D-20)
    auto& snap = processor.uiSnapshot;

    float bpm = snap.bpm.load(std::memory_order_relaxed);
    if (bpm != lastSentBpm)
    {
        lastSentBpm = bpm;
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/session/bpm"), bpm));
        hasContent = true;
    }

    int bpi = snap.bpi.load(std::memory_order_relaxed);
    if (bpi != lastSentBpi)
    {
        lastSentBpi = bpi;
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/session/bpi"), static_cast<int32_t>(bpi)));
        hasContent = true;
    }

    int beat = snap.beat_position.load(std::memory_order_relaxed);
    if (beat != lastSentBeat)
    {
        lastSentBeat = beat;
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/session/beat"), static_cast<int32_t>(beat)));
        hasContent = true;
    }

    auto* client = processor.getClient();
    if (client != nullptr)
    {
        int status = client->cached_status.load(std::memory_order_relaxed);
        if (status != lastSentStatus)
        {
            lastSentStatus = status;
            bundle.addElement(juce::OSCMessage(
                juce::OSCAddressPattern("/JamWide/session/status"), static_cast<int32_t>(status)));
            hasContent = true;
        }
    }

    int users = processor.userCount.load(std::memory_order_relaxed);
    if (users != lastSentUsers)
    {
        lastSentUsers = users;
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/session/users"), static_cast<int32_t>(users)));
        hasContent = true;
    }

    float sampleRate = static_cast<float>(processor.getSampleRate());
    if (sampleRate != lastSentSampleRate)
    {
        lastSentSampleRate = sampleRate;
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/session/samplerate"), sampleRate));
        hasContent = true;
    }

    // Codec: derive string from encoder format fourcc
    if (client != nullptr)
    {
        unsigned int fourcc = client->GetEncoderFormat();
        juce::String codecStr;
        if (fourcc == NJ_ENCODER_FMT_FLAC)
            codecStr = "FLAC";
        else if (fourcc == NJ_ENCODER_FMT_VORBIS)
            codecStr = "Vorbis";
        else
            codecStr = "Unknown";

        if (codecStr != lastSentCodec)
        {
            lastSentCodec = codecStr;
            bundle.addElement(juce::OSCMessage(
                juce::OSCAddressPattern("/JamWide/session/codec"), codecStr));
            hasContent = true;
        }
    }
}

void OscServer::sendVuMeters(juce::OSCBundle& bundle, bool& hasContent)
{
    // VU meters are always "dirty" -- sent every tick (per D-16)
    auto& snap = processor.uiSnapshot;

    bundle.addElement(juce::OSCMessage(
        juce::OSCAddressPattern("/JamWide/master/vu/left"),
        snap.master_vu_left.load(std::memory_order_relaxed)));
    bundle.addElement(juce::OSCMessage(
        juce::OSCAddressPattern("/JamWide/master/vu/right"),
        snap.master_vu_right.load(std::memory_order_relaxed)));

    for (int ch = 0; ch < 4; ++ch)
    {
        juce::String chNum(ch + 1);
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/local/" + chNum + "/vu/left"),
            snap.local_ch_vu_left[static_cast<size_t>(ch)].load(std::memory_order_relaxed)));
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/local/" + chNum + "/vu/right"),
            snap.local_ch_vu_right[static_cast<size_t>(ch)].load(std::memory_order_relaxed)));
    }

    hasContent = true;
}

// ── Phase 10: Remote user send methods ──

void OscServer::sendDirtyRemoteUsers(juce::OSCBundle& bundle, bool& hasContent)
{
    const auto& users = processor.cachedUsers;
    const int count = juce::jmin(static_cast<int>(users.size()), kMaxRemoteSlots);

    for (int i = 0; i < count; ++i)
    {
        const auto& user = users[static_cast<size_t>(i)];
        auto& last = lastSentRemoteUsers[static_cast<size_t>(i)];
        auto& src = remoteOscSourced[static_cast<size_t>(i)];
        juce::String prefix = "/JamWide/remote/" + juce::String(i + 1);

        // Group bus volume: normalize NJClient 0-2 -> OSC 0-1
        float oscVol = user.volume * 0.5f;
        if (!src.volume && oscVol != last.volume)
        {
            bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(prefix + "/volume"), oscVol));
            bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(prefix + "/volume/db"),
                OscAddressMap::linearToDb(user.volume)));
            last.volume = oscVol;
            hasContent = true;
        }
        src.volume = false;

        // Group bus pan: NJClient -1..1 -> OSC 0-1
        float oscPan = OscAddressMap::apvtsPanToOsc(user.pan);
        if (!src.pan && oscPan != last.pan)
        {
            bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(prefix + "/pan"), oscPan));
            last.pan = oscPan;
            hasContent = true;
        }
        src.pan = false;

        // Group bus mute
        float oscMute = user.mute ? 1.0f : 0.0f;
        if (!src.mute && oscMute != last.mute)
        {
            bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(prefix + "/mute"), oscMute));
            last.mute = oscMute;
            hasContent = true;
        }
        src.mute = false;

        // Per sub-channel (per D-03)
        // {n} is SEQUENTIAL 1-based, matching array index in user.channels
        const int chCount = juce::jmin(static_cast<int>(user.channels.size()), kMaxSubChannels);
        for (int c = 0; c < chCount; ++c)
        {
            const auto& ch = user.channels[static_cast<size_t>(c)];
            auto& chLast = lastSentRemoteChannels[static_cast<size_t>(i)][static_cast<size_t>(c)];
            auto& chSrc = remoteChOscSourced[static_cast<size_t>(i)][static_cast<size_t>(c)];
            juce::String chPrefix = prefix + "/ch/" + juce::String(c + 1);

            float chOscVol = ch.volume * 0.5f;
            if (!chSrc.volume && chOscVol != chLast.volume)
            {
                bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(chPrefix + "/volume"), chOscVol));
                bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(chPrefix + "/volume/db"),
                    OscAddressMap::linearToDb(ch.volume)));
                chLast.volume = chOscVol;
                hasContent = true;
            }
            chSrc.volume = false;

            float chOscPan = OscAddressMap::apvtsPanToOsc(ch.pan);
            if (!chSrc.pan && chOscPan != chLast.pan)
            {
                bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(chPrefix + "/pan"), chOscPan));
                chLast.pan = chOscPan;
                hasContent = true;
            }
            chSrc.pan = false;

            float chOscMute = ch.mute ? 1.0f : 0.0f;
            if (!chSrc.mute && chOscMute != chLast.mute)
            {
                bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(chPrefix + "/mute"), chOscMute));
                chLast.mute = chOscMute;
                hasContent = true;
            }
            chSrc.mute = false;

            float chOscSolo = ch.solo ? 1.0f : 0.0f;
            if (!chSrc.solo && chOscSolo != chLast.solo)
            {
                bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(chPrefix + "/solo"), chOscSolo));
                chLast.solo = chOscSolo;
                hasContent = true;
            }
            chSrc.solo = false;
        }

        // Group bus solo feedback (derived -- see design note in objective)
        bool allSoloed = !user.channels.empty();
        for (const auto& ch : user.channels)
            if (!ch.solo) { allSoloed = false; break; }
        float oscGroupSolo = allSoloed ? 1.0f : 0.0f;
        if (oscGroupSolo != last.solo)
        {
            bundle.addElement(juce::OSCMessage(
                juce::OSCAddressPattern(prefix + "/solo"), oscGroupSolo));
            last.solo = oscGroupSolo;
            hasContent = true;
        }
    }
}

void OscServer::sendRemoteVuMeters(juce::OSCBundle& bundle, bool& hasContent)
{
    const auto& users = processor.cachedUsers;
    const int count = juce::jmin(static_cast<int>(users.size()), kMaxRemoteSlots);

    for (int i = 0; i < count; ++i)
    {
        const auto& user = users[static_cast<size_t>(i)];
        juce::String prefix = "/JamWide/remote/" + juce::String(i + 1);

        // Aggregate VU: max across sub-channels
        float maxL = 0.0f, maxR = 0.0f;
        for (const auto& ch : user.channels)
        {
            maxL = juce::jmax(maxL, ch.vu_left);
            maxR = juce::jmax(maxR, ch.vu_right);
        }
        bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(prefix + "/vu/left"), maxL));
        bundle.addElement(juce::OSCMessage(juce::OSCAddressPattern(prefix + "/vu/right"), maxR));

        // Per sub-channel VU
        const int chCount = juce::jmin(static_cast<int>(user.channels.size()), kMaxSubChannels);
        for (int c = 0; c < chCount; ++c)
        {
            juce::String chPrefix = prefix + "/ch/" + juce::String(c + 1);
            bundle.addElement(juce::OSCMessage(
                juce::OSCAddressPattern(chPrefix + "/vu/left"), user.channels[static_cast<size_t>(c)].vu_left));
            bundle.addElement(juce::OSCMessage(
                juce::OSCAddressPattern(chPrefix + "/vu/right"), user.channels[static_cast<size_t>(c)].vu_right));
        }
        hasContent = true;
    }
}

void OscServer::sendRemoteRoster(juce::OSCBundle& bundle, bool& hasContent)
{
    const auto& users = processor.cachedUsers;
    const int count = juce::jmin(static_cast<int>(users.size()), kMaxRemoteSlots);

    // Compute hash: size + sum of name lengths (simple, sufficient for change detection)
    int hash = count;
    for (int i = 0; i < count; ++i)
    {
        hash += static_cast<int>(strlen(users[static_cast<size_t>(i)].name));
        for (const auto& ch : users[static_cast<size_t>(i)].channels)
            hash += static_cast<int>(strlen(ch.name));
    }

    if (hash == lastSentRosterHash && count == lastSentRosterCount)
        return;  // No change

    // ── ROSTER CHANGED: Reset all per-slot cached state ──
    // Addresses review concern: "stale slot index handling" and "cached send-state reset"
    // This prevents a new user in slot N from inheriting the previous occupant's
    // echo suppression flags or last-sent values.
    for (auto& u : lastSentRemoteUsers) u = RemoteUserLastSent{};
    for (auto& u : lastSentRemoteChannels) for (auto& c : u) c = RemoteChannelLastSent{};
    for (auto& u : remoteOscSourced) u = RemoteUserOscSourced{};
    for (auto& u : remoteChOscSourced) for (auto& c : u) c = RemoteChannelOscSourced{};

    // Broadcast names for active slots
    for (int i = 0; i < count; ++i)
    {
        juce::String idx(i + 1);
        juce::String prefix = "/JamWide/remote/" + idx;

        // Strip @IP suffix from username (same pattern as ChannelStripArea.cpp)
        juce::String fullName(users[static_cast<size_t>(i)].name);
        int atIdx = fullName.lastIndexOfChar('@');
        juce::String displayName = (atIdx > 0) ? fullName.substring(0, atIdx) : fullName;

        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern(prefix + "/name"), displayName));

        // Sub-channel names (per D-08)
        const auto& user = users[static_cast<size_t>(i)];
        for (size_t c = 0; c < user.channels.size() && c < static_cast<size_t>(kMaxSubChannels); ++c)
        {
            juce::String chName(user.channels[c].name);
            bundle.addElement(juce::OSCMessage(
                juce::OSCAddressPattern(prefix + "/ch/" + juce::String(static_cast<int>(c) + 1) + "/name"),
                chName));
        }
    }

    // Clear empty slots (per D-06: send empty string)
    for (int i = count; i < kMaxRemoteSlots; ++i)
    {
        juce::String idx(i + 1);
        bundle.addElement(juce::OSCMessage(
            juce::OSCAddressPattern("/JamWide/remote/" + idx + "/name"), juce::String()));
    }

    lastSentRosterCount = count;
    lastSentRosterHash = hash;
    hasContent = true;
}

// ── Value getters for dirty comparison ──

float OscServer::getCurrentValue(int index)
{
    const auto& entry = addressMap.getEntry(index);

    switch (entry.type)
    {
        case OscParamType::ApvtsFloat:
        {
            auto* param = processor.apvts.getParameter(entry.apvtsId);
            if (param == nullptr)
                return 0.0f;

            if (entry.isDbVariant)
            {
                // Get unnormalized linear value, convert to dB
                float linear = param->getNormalisableRange().convertFrom0to1(param->getValue());
                return OscAddressMap::linearToDb(linear);
            }
            else if (entry.isPanParam)
            {
                // Get unnormalized APVTS pan (-1..1), convert to OSC 0..1
                float apvtsPan = param->getNormalisableRange().convertFrom0to1(param->getValue());
                return OscAddressMap::apvtsPanToOsc(apvtsPan);
            }
            else
            {
                // Return normalized 0-1 value directly
                return param->getValue();
            }
        }

        case OscParamType::ApvtsBool:
        {
            auto* param = processor.apvts.getParameter(entry.apvtsId);
            if (param == nullptr)
                return 0.0f;
            return param->getValue();
        }

        case OscParamType::NjclientAtomic:
        {
            // Metro pan: read NJClient -1..1, convert to OSC 0..1
            auto* client = processor.getClient();
            if (client != nullptr)
            {
                float nativeVal = client->config_metronome_pan.load(std::memory_order_relaxed);
                return entry.isPanParam ? OscAddressMap::apvtsPanToOsc(nativeVal) : nativeVal;
            }
            return 0.5f;  // center default for pan
        }

        case OscParamType::CmdQueue:
        {
            // Solo: read actual state from NJClient (fixes one-directional feedback gap)
            auto* client = processor.getClient();
            if (client != nullptr && entry.channelIndex >= 0)
            {
                bool solo = false;
                client->GetLocalChannelMonitoring(entry.channelIndex,
                    nullptr, nullptr, nullptr, &solo);
                return solo ? 1.0f : 0.0f;
            }
            return 0.0f;
        }

        case OscParamType::ReadOnly:
            return 0.0f;  // Telemetry handled separately
    }

    return 0.0f;
}
